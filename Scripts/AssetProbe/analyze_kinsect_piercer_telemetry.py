"""Read-only Kinsect Piercer PIE audit; output evidence, not an automatic sign-off."""

import argparse
import bisect
import collections
import csv
import json
import re
from pathlib import Path


def load_csv(path):
    with path.open(encoding="utf-8-sig", newline="") as stream:
        reader = csv.DictReader(stream)
        rows = list(reader)
        malformed = [i + 2 for i, row in enumerate(rows)
                     if None in row or any(value is None for value in row.values())]
        return rows, {"columns": len(reader.fieldnames or []), "rows": len(rows),
                      "malformed_lines": malformed[:20], "malformed_count": len(malformed)}


def number(row, field, default=0.0):
    return float(row.get(field, default) or default)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", required=True, type=Path)
    parser.add_argument("--engine-log", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    session = args.session.resolve()
    playback, schema = load_csv(session / "Animation/Playback.csv")
    state_rows, state_schema = load_csv(session / "Character/State.csv")
    spatial_rows, spatial_schema = load_csv(session / "Character/Spatial.csv")
    input_rows, input_schema = load_csv(session / "Input/ParsedInput.csv")
    montage_rows, montage_schema = load_csv(session / "Animation/MontageInstances.csv")
    query_rows, query_schema = load_csv(session / "MotionMatching/Query.csv")
    states = {int(row["Frame"]): row for row in state_rows}
    state_frames = sorted(states)
    spatial = {int(row["Frame"]): row for row in spatial_rows}
    queries = {int(row["Frame"]): row for row in query_rows}
    montages = collections.defaultdict(dict)
    for row in montage_rows:
        if row["InstanceID"]:
            montages[int(row["Frame"])][int(row["InstanceID"])] = row
    groups = collections.defaultdict(list)
    for index, row in enumerate(playback):
        if row["KinsectPiercerActive"] == "1":
            groups[int(row["KinsectPiercerActivationSequenceID"])].append((index, row))

    checks = collections.Counter()
    violations = []

    def check(condition, label, action, row):
        checks[label] += 1
        if not condition:
            violations.append({"check": label, "action": action,
                               "frame": int(row["Frame"]), "world_time": number(row, "WorldTimeSeconds")})

    result_actions = []
    for action, indexed_rows in sorted(groups.items()):
        rows = [row for _, row in indexed_rows]
        phase_runs = []
        previous_phase = None
        previous_clock = {}
        previous_hits = [0, 0, 0]
        yaw = (number(rows[0], "KinsectPiercerLockedYawX"), number(rows[0], "KinsectPiercerLockedYawY"))
        for row in rows:
            phase = int(row["KinsectPiercerPhase"])
            frame = int(row["Frame"])
            position = spatial.get(frame, {})
            if phase != previous_phase:
                phase_runs.append({"phase": phase, "start_frame": frame,
                                   "start_time": number(row, "WorldTimeSeconds"),
                                   "first_clock": number(row, "KinsectPiercerCurveElapsedSeconds"),
                                   "location": [number(position, "Location" + axis) for axis in "XYZ"],
                                   "velocity": [number(position, "Velocity" + axis) for axis in "XYZ"],
                                   "main_instance": int(row["KinsectPiercerMainMontageInstanceID"]),
                                   "landing_instance": int(row["KinsectPiercerLandingMontageInstanceID"])})
                previous_phase = phase
            phase_runs[-1]["end_frame"] = frame
            phase_runs[-1]["end_time"] = number(row, "WorldTimeSeconds")
            phase_runs[-1]["last_clock"] = number(row, "KinsectPiercerCurveElapsedSeconds")
            check(f"GA_IG_QiangHuaCaoChongChuanCi_C#{action}" in row["ActiveActions"], "registered_same_action", action, row)
            check(int(row["KinsectPiercerSegmentConfigCount"]) == 3, "three_segment_configs", action, row)
            check(abs(number(row, "KinsectPiercerLockedYawX") - yaw[0]) < 0.0002
                  and abs(number(row, "KinsectPiercerLockedYawY") - yaw[1]) < 0.0002,
                  "frozen_yaw_during_action", action, row)
            for segment in range(3):
                hits = int(row[f"KinsectPiercerSegment{segment}SuccessfulTargets"])
                check(previous_hits[segment] <= hits <= 1, f"dummy_segment_{segment}_quota", action, row)
                previous_hits[segment] = hits
            if phase in (1, 2):
                clock = number(row, "KinsectPiercerCurveElapsedSeconds")
                check(clock >= previous_clock.get(phase, clock) - 0.0001,
                      "phase_curve_clock_never_rewinds", action, row)
                previous_clock[phase] = clock
                check(row["KinsectPiercerRecordedTrajectoryActive"] == "1"
                      and row["KinsectPiercerActionMovementOwned"] == "1"
                      and row["KinsectPiercerLandingRootMotionOwned"] == "0",
                      "air_curve_exclusive_owner", action, row)
                check(row["KinsectPiercerSourceTagPresent"] == "1"
                      and row["KinsectPiercerDanceSource"] == "1"
                      and row["KinsectPiercerAerialAttackBudgetSpent"] == "1",
                      "air_source_and_budget", action, row)
            elif phase == 3:
                check(row["KinsectPiercerSourceTagPresent"] == "0"
                      and row["KinsectPiercerDanceSource"] == "0"
                      and row["KinsectPiercerAerialAttackBudgetSpent"] == "0",
                      "landing_clears_source_and_budget", action, row)
                check(row["KinsectPiercerRecordedTrajectoryActive"] == "0"
                      and row["KinsectPiercerActionMovementOwned"] == "0"
                      and row["KinsectPiercerLandingRootMotionOwned"] == "1",
                      "landing_montage_exclusive_owner", action, row)
                # UE 5.6 AnimEnums.h: NoExtraction=0, Ignore=1, Everything=2, MontagesOnly=3.
                check(row["KinsectPiercerAnimRootMotionMode"] == "3"
                      and row["KinsectPiercerLandingRootMotionModeCaptured"] == "1",
                      "landing_extracts_montage_only", action, row)
                check(int(row["KinsectPiercerMainMontageInstanceID"]) == -1
                      and int(row["KinsectPiercerLandingMontageInstanceID"]) >= 0,
                      "landing_has_new_registered_instance", action, row)
                instance = montages.get(frame, {}).get(int(row["KinsectPiercerLandingMontageInstanceID"]))
                check(instance is not None and "AM_IG_QiangHuaCaoChongChuanCi_Over" in instance["Montage"],
                      "landing_instance_observed_in_engine", action, row)
                if position:
                    check(position["MovementMode"] in ("1", "2"), "landing_is_grounded", action, row)
                check(not row["AerialLandingMontageReferenced"] == "1",
                      "system_landing_presentation_not_competing", action, row)
        after_index = indexed_rows[-1][0] + 1
        after = playback[after_index] if after_index < len(playback) else None
        after_state = states.get(int(after["Frame"]), {}) if after else {}
        if after:
            check(f"GA_IG_QiangHuaCaoChongChuanCi_C#{action}" not in after["ActiveActions"]
                  and f"GA_IG_QiangHuaCaoChongChuanCi_C#{action}" not in after["MontageRootMotionOwnerAction"],
                  "old_action_and_montage_owner_retired", action, after)
            if not after["ActiveActions"]:
                check(after_state.get("BlockMovement") == "0",
                      "natural_end_has_no_input_lock", action, after)
                query = queries.get(int(after["Frame"]), {})
                check(number(query, "ForceIdleReleaseHoldRemaining") == 0.0,
                      "natural_end_has_no_timed_idle_hold", action, after)
            check(spatial.get(int(after["Frame"]), {}).get("AnimRootMotionMode") == "2",
                  "end_restores_root_motion_from_everything", action, after)
        force_idle_clear = next((row for row in playback[after_index:after_index + 20]
                                 if states.get(int(row["Frame"]), {}).get("ForceMMIdle") == "0"), None)
        phases = [run["phase"] for run in phase_runs]
        check(phases == [1, 2, 3], "one_200_201_203_chain", action, rows[-1])
        latest_input = next((row for row in reversed(input_rows)
                             if row["ResolvedInputTag"] == "Input.Weapon.RTY"
                             and row["Phase"] == "Started"
                             and number(row, "WorldTimeSeconds") <= number(rows[0], "WorldTimeSeconds") + 0.02), None)
        result_actions.append({
            "action": action, "rows": len(rows),
            "start_time": number(rows[0], "WorldTimeSeconds"),
            "end_time": number(rows[-1], "WorldTimeSeconds"),
            "pitch_at_start": number(rows[0], "CameraViewPitch"), "locked_yaw_xy": yaw,
            "phases": phase_runs, "successful_targets_by_config": previous_hits,
            "landing_montage_last_position": number(rows[-1], "KinsectPiercerLandingMontageTimeSeconds"),
            "next_actions": after["ActiveActions"] if after else None,
            "next_root_motion_mode": spatial.get(int(after["Frame"]), {}).get("AnimRootMotionMode") if after else None,
            "next_block_movement": after_state.get("BlockMovement"),
            "next_force_idle": after_state.get("ForceMMIdle"),
            "force_idle_flag_clear_delay_seconds": (
                number(force_idle_clear, "WorldTimeSeconds") - number(after, "WorldTimeSeconds")
                if after and force_idle_clear else None),
            "entry_input_context": latest_input,
        })

    movement_phase_names = collections.Counter()
    action_events = collections.defaultdict(list)
    movement_rows = 0
    with (session / "Character/MovementPhases.csv").open(encoding="utf-8-sig", newline="") as stream:
        for row in csv.DictReader(stream):
            movement_rows += 1
            movement_phase_names[row["Phase"]] += 1
            identities = re.findall(r"GA_IG_QiangHuaCaoChongChuanCi_C#(\d+)", row["ActiveActions"])
            if identities and any(keyword in row["Phase"].lower()
                                  for keyword in ("land", "recorded", "handoff", "release")):
                for action_text in set(identities):
                    action_events[int(action_text)].append({key: row[key] for key in (
                        "Frame", "WorldTimeSeconds", "Phase", "MovementMode", "ActionMovementOwner",
                        "MontageRootMotionOwner", "LocationZ", "VelocityX", "VelocityY", "VelocityZ")})

    for action in result_actions:
        action["movement_events"] = action_events.get(action["action"], [])
    commands, landing_logs, piercer_errors = [], collections.Counter(), []
    log_session_found = False
    log_session_active = False
    last_enable_command = None
    if args.engine_log:
        with args.engine_log.open(encoding="utf-8-sig", errors="replace") as stream:
            for line_number, line in enumerate(stream, 1):
                command = {"line": line_number, "text": line.strip()}
                if "Cmd: mhgz.Telemetry.Enable 1" in line:
                    last_enable_command = command
                if "[RuntimeTelemetry] Started:" in line and session.name in line:
                    log_session_active = True
                    log_session_found = True
                    if last_enable_command:
                        commands.append(last_enable_command)
                if not log_session_active:
                    continue
                if "Cmd:" in line and any(text in line.lower() for text in ("telemetry", "slomo")):
                    commands.append(command)
                if "Cmd: mhgz.Telemetry.Enable 0" in line:
                    log_session_active = False
                    continue
                match = re.search(r"\[KinsectPiercer\] landed action=(\d+)", line)
                if match:
                    landing_logs[int(match[1])] += 1
                if "[KinsectPiercer]" in line and any(text in line for text in ("Warning:", "Error:")):
                    piercer_errors.append({"line": line_number, "text": line.strip()})

    input_starts = [row for row in input_rows
                    if row["ResolvedInputTag"] == "Input.Weapon.RTY" and row["Phase"] == "Started"]
    input_attempts = []
    for row in input_starts:
        t = number(row, "WorldTimeSeconds")
        matching = [action["action"] for action in result_actions
                    if -0.03 <= action["start_time"] - t <= 0.10]
        input_attempts.append({"time": t, "frame": int(row["Frame"]),
                               "context": row["ContextTags"], "activation_candidates": matching})
        state_index = bisect.bisect_left(state_frames, int(row["Frame"])) - 1
        prior = states[state_frames[state_index]] if state_index >= 0 else {}
        input_attempts[-1]["prior_sample_frame"] = prior.get("Frame")
        input_attempts[-1]["prior_sample_tags"] = prior.get("OwnedGameplayTags", "")
    report = {
        "session": str(session), "capture_range_world_seconds": [
            number(playback[0], "WorldTimeSeconds"), number(playback[-1], "WorldTimeSeconds")],
        "csv_schema": {"Playback": schema, "State": state_schema, "Spatial": spatial_schema,
                       "ParsedInput": input_schema, "MontageInstances": montage_schema,
                       "Query": query_schema},
        "activation_count": len(result_actions), "sample_checks": dict(checks),
        "sample_violation_count": len(violations),
        "sample_violation_counts": dict(collections.Counter(row["check"] for row in violations)),
        "sample_violations": violations[:100],
        "actions": result_actions, "movement_rows": movement_rows,
        "movement_phase_counts": dict(movement_phase_names),
        "landing_log_counts": dict(sorted(landing_logs.items())),
        "engine_log_matching_session_found": log_session_found,
        "piercer_warning_error_log_lines": piercer_errors,
        "commands": commands, "rty_input_attempts": input_attempts,
        "interpretation_limits": [
            "Sample checks describe captured frames, not all sub-frame callbacks or unrecorded scenarios.",
            "The one-target quota limit assumes this capture's single TrainingDummy scene.",
            "No automatic stage sign-off; user visual confirmation and deferred scope are recorded separately.",
            "Collision wall/ceiling tests are explicitly deferred by the user for this acceptance.",
        ],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"report": str(args.output), "activations": len(result_actions),
                      "sample_violations": len(violations),
                      "landing_log_counts": dict(sorted(landing_logs.items())),
                      "commands": commands}, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
