"""Discover MHRise motion IDs associated with controller input transitions.

The recorder already writes player_motion_old_id and controller button
pressed/down/released columns. This read-only tool correlates those streams so
an undocumented action can be identified from one isolated capture.
"""

from __future__ import annotations

import argparse
import csv
import json
from collections import Counter
from pathlib import Path
from typing import TextIO

from audit_action_motion_captures import parse_action_id, parse_number
from recorder_path import recorder_dir


BUTTON_PREFIX = "input_gamepad_button_"
EDGE_SUFFIXES = ("_pressed", "_released")


def is_set(value: object) -> bool:
    if value is None:
        return False
    text = str(value).strip().lower()
    if text in {"", "0", "false", "no", "off", "none"}:
        return False
    try:
        return float(text) != 0.0
    except ValueError:
        return text in {"1", "true", "yes", "on", "down"}


def discover_stream(stream: TextIO) -> dict:
    reader = csv.DictReader(stream)
    fields = set(reader.fieldnames or ())
    if "player_motion_old_id" not in fields:
        raise ValueError("CSV has no player_motion_old_id column")

    button_columns = sorted(
        field for field in fields
        if field.startswith(BUTTON_PREFIX)
        and field.endswith(("_down", "_pressed", "_released"))
    )
    if not any(field.endswith("_pressed") for field in button_columns):
        raise ValueError("CSV has no controller button transition columns")

    total_rows = 0
    id_rows: Counter[int] = Counter()
    raw_input_events: list[dict] = []
    edge_counts: Counter[tuple[str, str]] = Counter()
    episodes: list[dict] = []
    motion_transitions: list[dict] = []
    active: dict | None = None
    previous_id: int | None = None
    previous_id_initialized = False

    def finish(next_time: float | None, next_id: int | None) -> None:
        nonlocal active
        if active is None:
            return
        active["sampled_span_s"] = (
            None if active["time_start_s"] is None or active["time_end_s"] is None
            else max(0.0, active["time_end_s"] - active["time_start_s"])
        )
        active["time_until_transition_s"] = (
            None if active["time_start_s"] is None or next_time is None
            else max(0.0, next_time - active["time_start_s"])
        )
        active["transition_to_id"] = next_id
        episodes.append(active)
        active = None

    for row in reader:
        total_rows += 1
        action_id = parse_action_id(row.get("player_motion_old_id"))
        sample_time = parse_number(row.get("capture_time_s"))
        sample_index = row.get("sample_index", "")
        if action_id is not None:
            id_rows[action_id] += 1

        down = sorted(
            column[len(BUTTON_PREFIX):-len("_down")]
            for column in button_columns
            if column.endswith("_down") and is_set(row.get(column))
        )
        edges = []
        for column in button_columns:
            if not (column.endswith("_pressed") or column.endswith("_released")):
                continue
            if is_set(row.get(column)):
                suffix = "_pressed" if column.endswith("_pressed") else "_released"
                button = column[len(BUTTON_PREFIX):-len(suffix)]
                edges.append({"button": button, "edge": suffix[1:]})
        if edges:
            for edge in edges:
                edge_counts[(edge["button"], edge["edge"])] += 1
            raw_input_events.append({
                "sample_index": sample_index,
                "time_s": sample_time,
                "motion_id": action_id,
                "buttons_down": down,
                "edges": edges,
            })

        if previous_id_initialized and action_id != previous_id:
            motion_transitions.append({
                "sample_index": sample_index,
                "time_s": sample_time,
                "from_id": previous_id,
                "to_id": action_id,
            })
        previous_id = action_id
        previous_id_initialized = True

        if action_id is None:
            finish(sample_time, None)
            continue
        if active is None or active["motion_id"] != action_id:
            finish(sample_time, action_id)
            active = {
                "motion_id": action_id,
                "sample_start": sample_index,
                "sample_end": sample_index,
                "time_start_s": sample_time,
                "time_end_s": sample_time,
                "samples": 0,
                "buttons_down_rows": Counter(),
                "button_edge_counts": Counter(),
            }
        active["sample_end"] = sample_index
        active["time_end_s"] = sample_time
        active["samples"] += 1
        active["buttons_down_rows"].update(down)
        active["button_edge_counts"].update(
            (edge["button"], edge["edge"]) for edge in edges
        )

    finish(None, None)
    for episode in episodes:
        episode["buttons_down_rows"] = dict(sorted(episode["buttons_down_rows"].items()))
        episode["button_edge_counts"] = {
            f"{button}:{edge}": count
            for (button, edge), count in sorted(episode["button_edge_counts"].items())
        }

    # Summarize input around each motion boundary instead of dumping every row:
    # some older captures repeat synthetic directional edges each frame.
    for transition in motion_transitions:
        transition_time = transition["time_s"]
        nearby = [
            event for event in raw_input_events
            if transition_time is not None and event["time_s"] is not None
            and transition_time - 0.25 <= event["time_s"] <= transition_time + 0.10
        ]
        nearby_edges: Counter[str] = Counter()
        nearby_held: Counter[str] = Counter()
        for event in nearby:
            nearby_edges.update(f"{edge['button']}:{edge['edge']}" for edge in event["edges"])
            nearby_held.update(event["buttons_down"])
        transition["nearby_input"] = {
            "event_rows": len(nearby),
            "edge_counts": dict(sorted(nearby_edges.items())),
            "held_row_counts": dict(sorted(nearby_held.items())),
        }

    return {
        "rows": total_rows,
        "button_columns": button_columns,
        "button_edge_counts": {
            f"{button}:{edge}": count
            for (button, edge), count in sorted(edge_counts.items())
        },
        "motion_id_row_counts": dict(sorted(id_rows.items())),
        "episodes": episodes,
        "motion_transitions": motion_transitions,
    }


def audit_file(path: Path) -> dict:
    with path.open("r", encoding="utf-8-sig", newline="") as stream:
        return {"file": path.name, **discover_stream(stream)}


def format_report(path: Path, report: dict) -> str:
    lines = [
        "MHRise controller-input / motion-ID discovery (read-only)",
        f"Capture: {path}",
        f"Rows: {report['rows']}",
        "",
        "Motion ID row counts:",
    ]
    if report["motion_id_row_counts"]:
        lines.extend(
            f"  {motion_id}: {count} rows"
            for motion_id, count in report["motion_id_row_counts"].items()
        )
    else:
        lines.append("  No readable player_motion_old_id values.")

    lines.extend(("", "Controller edge totals (button names are RE Engine enums):"))
    if report["button_edge_counts"]:
        lines.extend(
            f"  {name}: {count}"
            for name, count in report["button_edge_counts"].items()
        )
    else:
        lines.append("  No pressed/released edges found; check input_gamepad_available and the recorder schema.")

    lines.extend(("", "Motion-ID transitions and nearby controller edges:"))
    if report["motion_transitions"]:
        transitions = report["motion_transitions"]
        for transition in transitions[:80]:
            time_text = "?" if transition["time_s"] is None else f"{transition['time_s']:.3f}s"
            nearby_input = transition["nearby_input"]
            edge_summary = ",".join(
                f"{name}={count}" for name, count in nearby_input["edge_counts"].items()
            ) or "none"
            held_summary = ",".join(
                f"{name}:{count}" for name, count in nearby_input["held_row_counts"].items()
            ) or "none"
            lines.append(
                f"  sample={transition['sample_index']} t={time_text} "
                f"id={transition['from_id']} -> {transition['to_id']} "
                f"nearbyRows={nearby_input['event_rows']} edges=[{edge_summary}] heldRows=[{held_summary}]"
            )
        if len(transitions) > 80:
            lines.append(f"  ... {len(transitions) - 80} further transitions retained in JSON only")
    else:
        lines.append("  No motion-ID transitions found.")

    lines.extend(("", "Contiguous motion-ID episodes:"))
    if report["episodes"]:
        episodes = report["episodes"]
        for episode in episodes[:120]:
            start = episode["time_start_s"]
            span = episode["sampled_span_s"]
            start_text = "?" if start is None else f"{start:.3f}s"
            span_text = "?" if span is None else f"{span:.3f}s sampled"
            duration = episode["time_until_transition_s"]
            if duration is not None:
                span_text += f", {duration:.3f}s to next ID"
            held = ",".join(f"{key}:{value}" for key, value in episode["buttons_down_rows"].items()) or "none"
            edge_counts_text = ",".join(
                f"{key}:{value}" for key, value in episode["button_edge_counts"].items()
            ) or "none"
            lines.append(
                f"  id={episode['motion_id']} samples={episode['samples']} "
                f"start={start_text} {span_text} -> {episode['transition_to_id']} "
                f"heldRows=[{held}] edges=[{edge_counts_text}]"
            )
    else:
        lines.append("  No motion-ID episodes found.")
        if len(episodes) > 120:
            lines.append(f"  ... {len(episodes) - 120} further episodes retained in JSON only")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--directory", type=Path, default=recorder_dir(),
        help="Recorder directory; the newest capture is selected unless --capture is given",
    )
    parser.add_argument("--capture", type=Path, help="Specific *_samples.csv to inspect")
    parser.add_argument("--json-out", type=Path, help="Optional machine-readable report path")
    args = parser.parse_args()

    if args.capture is not None:
        capture = args.capture.expanduser().resolve()
    else:
        directory = args.directory.expanduser().resolve()
        captures = sorted(
            directory.glob("*_samples.csv"), key=lambda item: item.stat().st_mtime
        ) if directory.is_dir() else []
        if not captures:
            parser.error(f"No *_samples.csv captures found in {directory}")
        capture = captures[-1]

    try:
        report = audit_file(capture)
    except (OSError, UnicodeError, csv.Error, ValueError) as error:
        parser.error(f"Cannot audit {capture}: {error}")

    print(format_report(capture, report))
    if args.json_out is not None:
        output = args.json_out.expanduser()
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(report, indent=2), encoding="utf-8")
        print(f"JSON report: {output.resolve()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
