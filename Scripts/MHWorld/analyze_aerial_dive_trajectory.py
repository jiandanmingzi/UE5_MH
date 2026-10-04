"""Read-only numerical audit of MHW aerial dive's runtime 49532 / LMT380 phase.

Source CSVs are never edited. Outputs are analysis sources, not UE assets.
MHW world coordinates are kept in their recorded units; +Y is world up.
"""

from __future__ import annotations

import argparse
from array import array
import csv
import hashlib
import json
from pathlib import Path

import numpy as np


FIELDS = [
    "sample_index", "quest_time_s", "delta_time_s", "position_x", "position_y", "position_z",
    "velocity_x_per_game_second", "velocity_y_per_game_second", "velocity_z_per_game_second",
    "internal_increment_x", "internal_increment_y", "internal_increment_z",
    "facing_forward_x", "facing_forward_y", "facing_forward_z", "facing_yaw_yup_local_z_deg",
    "action_lmt_id", "action_fsm_id", "action_frame", "action_frame_end",
    "action_frame_speed", "action_frame_speed_multiplier", "gravity", "fall_speed",
    "health", "airborne", "aiming", "hunter_state_available",
    "input_left_stick_up_down", "input_left_stick_right_down",
    "input_left_stick_down_down", "input_left_stick_left_down",
    "input_rt_down", "input_rt_pressed", "input_rb_down", "input_a_pressed",
    "input_l_stick_digital_x", "input_l_stick_digital_y", "input_l_stick_digital_magnitude",
]


def load_csv(path):
    columns = {name: array("d") for name in FIELDS}
    with path.open(encoding="utf-8-sig", newline="") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            for name, column in columns.items():
                try:
                    column.append(float(row.get(name, "")))
                except (ValueError, TypeError):
                    column.append(float("nan"))
    return {name: np.asarray(values) for name, values in columns.items()}


def stats(values):
    values = np.asarray(values, dtype=float)
    values = values[np.isfinite(values)]
    if not len(values):
        return None
    return {"n": int(len(values)), "min": float(np.min(values)), "p05": float(np.quantile(values, .05)),
            "median": float(np.median(values)), "p95": float(np.quantile(values, .95)), "max": float(np.max(values))}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("capture", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    data = load_csv(args.capture)
    size = len(data["sample_index"])
    pos = np.column_stack([data[f"position_{a}"] for a in "xyz"])
    vel = np.column_stack([data[f"velocity_{a}_per_game_second"] for a in "xyz"])
    ids = np.nan_to_num(data["action_lmt_id"], nan=-1).astype(int)
    time = data["quest_time_s"]
    boundaries = np.r_[0, np.flatnonzero(ids[1:] != ids[:-1]) + 1, size]
    runs = [(int(s), int(e), int(ids[s])) for s, e in zip(boundaries[:-1], boundaries[1:])]
    args.output.mkdir(parents=True, exist_ok=True)
    attempts = []
    points = []
    for run_index, (start, end, action) in enumerate(runs):
        if action != 49532:
            continue
        index = len(attempts) + 1
        t = time[start:end] - time[start]
        selection = np.flatnonzero((t >= .1) & (t <= .25)) + start
        if not len(selection):
            selection = np.arange(start, min(start + 4, end))
        facing = np.array([np.nanmedian(data["facing_forward_x"][selection]), 0.,
                           np.nanmedian(data["facing_forward_z"][selection])])
        facing /= max(np.linalg.norm(facing), 1e-9)
        right = np.array([facing[2], 0., -facing[0]])
        displacement = pos[start:end] - pos[start]
        forward = displacement @ facing
        side = displacement @ right
        pre = np.arange(max(start - 6, 0), start)
        entry_vel = np.nanmedian(vel[pre], axis=0) if len(pre) else np.full(3, np.nan)
        horizontal_speed = np.linalg.norm(vel[start:end][:, [0, 2]], axis=1)
        stick = data["input_l_stick_digital_magnitude"][start:end]
        on = stick > .1
        changes = np.flatnonzero(on[1:] != on[:-1]) + 1
        yaw = np.unwrap(np.deg2rad(data["facing_yaw_yup_local_z_deg"][start:end]))
        frame = data["action_frame"][start:end]
        next_id = runs[run_index + 1][2] if run_index + 1 < len(runs) else -1
        prev_id = runs[run_index - 1][2] if run_index else -1
        quality = []
        if end >= size:
            quality.append("capture_tail")
        if next_id != 49533:
            quality.append("not_followed_by_49533")
        if np.nanmax(data["delta_time_s"][start:end]) > .025:
            quality.append("sampling_gap")
        if np.any(np.diff(time[start:end]) <= 0):
            quality.append("clock_nonmonotonic")
        audit = {
            "attempt": index, "run_index": run_index, "start_sample": int(data["sample_index"][start]),
            "end_sample": int(data["sample_index"][end - 1]), "start_row": start, "end_row_exclusive": end,
            "start_game_s": float(time[start]), "end_game_s": float(time[end - 1]),
            "sample_span_s": float(t[-1]), "boundary_duration_s": float(time[end] - time[start]) if end < size else None,
            "samples": end-start, "prev_id": prev_id, "next_id": next_id,
            "frame_first": float(frame[0]), "frame_last": float(frame[-1]),
            "frame_max": float(np.nanmax(frame)), "frame_end": float(data["action_frame_end"][start]),
            "start_world_y": float(pos[start,1]), "end_world_y": float(pos[end-1,1]),
            "dx": float(displacement[-1,0]), "dy": float(displacement[-1,1]), "dz": float(displacement[-1,2]),
            "forward": float(forward[-1]), "right": float(side[-1]), "up_max": float(np.max(displacement[:,1])),
            "entry_vx": float(entry_vel[0]), "entry_vy": float(entry_vel[1]), "entry_vz": float(entry_vel[2]),
            "entry_v_forward": float(entry_vel @ facing), "entry_v_right": float(entry_vel @ right),
            "entry_speed_xz": float(np.linalg.norm(entry_vel[[0,2]])),
            "first_speed_xz": float(horizontal_speed[0]), "end_speed_xz": float(horizontal_speed[-1]),
            "max_speed_xz": float(np.nanmax(horizontal_speed)), "end_vy": float(vel[end-1,1]),
            "stick_on_fraction": float(np.mean(on)), "stick_on_start": bool(on[0]), "stick_on_end": bool(on[-1]),
            "stick_on_off_changes_s": ";".join(f"{t[i]:.4f}:{int(on[i])}" for i in changes),
            "yaw_total_deg": float(np.rad2deg(yaw[-1]-yaw[0])), "max_yaw_step_deg": float(np.nanmax(np.abs(np.rad2deg(np.diff(yaw))))) if len(yaw)>1 else 0,
            # This is capture integrity only. Motion/contact filtering is separate.
            "max_gap_s": float(np.nanmax(data["delta_time_s"][start:end])), "quality": ";".join(quality) or "capture_ok",
            "basis_forward_x": float(facing[0]), "basis_forward_z": float(facing[2]),
        }
        rt_edges = np.flatnonzero(data["input_rt_pressed"][max(0,start-120):start+1] == 1) + max(0,start-120)
        if len(rt_edges):
            rt_row = int(rt_edges[-1])
            audit.update(rt_age_s=float(time[start]-time[rt_row]),
                         rt_stick_x=float(data["input_l_stick_digital_x"][rt_row]),
                         rt_stick_y=float(data["input_l_stick_digital_y"][rt_row]),
                         rt_yaw=float(data["facing_yaw_yup_local_z_deg"][rt_row]))
        else:
            audit.update(rt_age_s=None, rt_stick_x=None, rt_stick_y=None, rt_yaw=None)
        audit["entry_yaw"] = float(data["facing_yaw_yup_local_z_deg"][start])
        attempts.append(audit)
        for offset, row in enumerate(range(start, end)):
            points.append({
                "attempt":index, "sample_index":int(data["sample_index"][row]), "time_s":float(t[offset]),
                "action_frame":float(frame[offset]), "world_x":float(pos[row,0]), "world_y":float(pos[row,1]), "world_z":float(pos[row,2]),
                "forward":float(forward[offset]), "right":float(side[offset]), "up":float(displacement[offset,1]),
                "vx":float(vel[row,0]), "vy":float(vel[row,1]), "vz":float(vel[row,2]),
                "v_forward":float(vel[row]@facing), "v_right":float(vel[row]@right), "speed_xz":float(horizontal_speed[offset]),
                "stick_x":float(data["input_l_stick_digital_x"][row]), "stick_y":float(data["input_l_stick_digital_y"][row]),
                "stick_magnitude_digital":float(stick[offset]), "yaw_deg":float(data["facing_yaw_yup_local_z_deg"][row]),
                "gravity":float(data["gravity"][row]), "fall_speed":float(data["fall_speed"][row]),
                "internal_x":float(data["internal_increment_x"][row]),
                "internal_y":float(data["internal_increment_y"][row]),
                "internal_z":float(data["internal_increment_z"][row]),
                "frame_speed":float(data["action_frame_speed"][row]),
                "frame_speed_multiplier":float(data["action_frame_speed_multiplier"][row]),
                "rt_down":float(data["input_rt_down"][row]),
                "prev_id":prev_id, "next_id":next_id, "quality":audit["quality"],
            })
    for filename, rows in [("attempts.csv",attempts),("phase380_trials.csv",points)]:
        with (args.output/filename).open("w",encoding="utf-8-sig",newline="") as handle:
            if rows:
                writer=csv.DictWriter(handle,fieldnames=list(rows[0]))
                writer.writeheader();writer.writerows(rows)
    counts={str(k):int(v) for k,v in zip(*np.unique(ids,return_counts=True))}
    summary={"source":str(args.capture.resolve()),"source_sha256":hashlib.sha256(args.capture.read_bytes()).hexdigest(),
             "rows":size,"valid_hunter_rows":int(np.nansum(data["hunter_state_available"]==1)),
             "sample_dt":stats(data["delta_time_s"]),"action_counts":counts,"attempts":attempts}
    (args.output/"summary.json").write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding="utf-8")
    print(json.dumps({"rows":size,"attempts":len(attempts),"duration":stats([a["sample_span_s"] for a in attempts]),
                      "prev_ids":sorted(set(a["prev_id"] for a in attempts)),"next_ids":sorted(set(a["next_id"] for a in attempts)),
                      "output":str(args.output)},ensure_ascii=False))


if __name__=="__main__":
    main()
