"""Inventory MHW LMT actions for possible aerial-fall loop sources.

Run in an isolated Blender background process. This only reads MOD3/LMT files
and writes a compact JSON inventory under the project Saved directory; it does
not export FBX files, create Blender assets, or modify game/project Content.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys


TOOL_VERSION = "mhw-lmt-action-inventory/2"
PROJECT = Path(__file__).resolve().parents[2]
OUT = PROJECT / "Saved/AssetProbe/DescendingThrust/common_lmt_action_inventory.json"


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--addon-path", type=Path, required=True)
    parser.add_argument("--mod3", type=Path, required=True)
    parser.add_argument("--lmt", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=OUT)
    return parser.parse_args(sys.argv[sys.argv.index("--") + 1:])


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def channel_delta(channel):
    if not isinstance(channel, dict) or len(channel) < 2:
        return 0.0
    ordered = sorted(channel.items(), key=lambda pair: int(pair[0]))
    baseline = ordered[0][1]
    return max(abs(float(component) - float(baseline[index]))
               for _, row in ordered[1:]
               for index, component in enumerate(row))


def channel_report(channels):
    result = {}
    for name in ("pos", "rot", "scl"):
        channel = channels.get(name)
        if not isinstance(channel, dict):
            continue
        keys = [int(frame) for frame in channel]
        result[name] = {
            "key_count": len(keys),
            "first_frame": min(keys) if keys else None,
            "last_frame": max(keys) if keys else None,
            "max_component_delta_from_first_raw_value": channel_delta(channel),
        }
    return result


def main():
    args = parse_args()
    for path in (args.mod3, args.lmt):
        if not path.is_file():
            raise FileNotFoundError(path)
    if args.output.exists():
        raise FileExistsError(f"Refusing to overwrite audit output: {args.output}")
    sys.path.insert(0, str(args.addon_path))
    from mhworld_tools_suite.lmt.lmt_parser import LmtParser
    from mhworld_tools_suite.mod3.mod3_parser import Mod3Parser

    bones, _ = Mod3Parser(path=str(args.mod3)).read()
    actions = LmtParser(path=str(args.lmt)).read()
    bone_names = {}
    for bone in bones:
        remap = int(bone["remap"])
        bone_names[remap] = str(bone.get("name", remap))

    rows = []
    for action in actions:
        action_name = str(action["name"])
        try:
            slot = int(action_name.rsplit("_", 1)[-1])
        except ValueError:
            slot = None
        tracks = []
        frame_ends = []
        root_position_deltas = []
        for raw_bone_id, raw_channels in action.get("bone_action", {}).items():
            bone_id = int(raw_bone_id)
            channels = channel_report(raw_channels)
            for channel in channels.values():
                if channel["last_frame"] is not None:
                    frame_ends.append(channel["last_frame"])
            position = channels.get("pos", {})
            if bone_id == -1 and position:
                root_position_deltas.append(position["max_component_delta_from_first_raw_value"])
            tracks.append({
                "bone_id": bone_id,
                "bone_name": "Root" if bone_id == -1 else bone_names.get(bone_id, "<unmapped>"),
                "channels": channels,
            })
        animated = [track for track in tracks
                    if any(channel["max_component_delta_from_first_raw_value"] > 1e-5
                           for channel in track["channels"].values())]
        rows.append({
            "slot": slot,
            "action_name": action_name,
            "last_key_frame": max(frame_ends) if frame_ends else None,
            "track_count": len(tracks),
            "tracks_with_raw_component_change": len(animated),
            "root_position_raw_delta_max_component": max(root_position_deltas) if root_position_deltas else None,
            "tracks": tracks,
        })

    rows.sort(key=lambda item: (item["slot"] is None, item["slot"] if item["slot"] is not None else 0))
    report = {
        "status": "COMPLETE_READ_ONLY_LMT_INVENTORY_REQUIRES_SEMANTIC_REVIEW",
        "tool_version": TOOL_VERSION,
        "python_version": sys.version.split()[0],
        "sources": {
            "lmt": {"path": str(args.lmt), "sha256": sha256(args.lmt)},
            "mod3": {"path": str(args.mod3), "sha256": sha256(args.mod3)},
        },
        "action_count": len(rows),
        "actions": rows,
        "limits": [
            "Action labels are slot IDs and do not by themselves identify gameplay semantics.",
            "Raw channel deltas are inventory hints, not decoded component-space pose errors or loop validation.",
            "No action was excluded; no FBX or Unreal asset was generated.",
        ],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"MHW_LMT_INVENTORY={args.output}")
    print(f"ACTION_COUNT={len(rows)}")
    for row in sorted(rows, key=lambda item: (
            -(item["last_key_frame"] or 0),
            item["root_position_raw_delta_max_component"] or 0))[:20]:
        print("CANDIDATE_HINT " + json.dumps({key: row[key] for key in (
            "slot", "action_name", "last_key_frame", "track_count",
            "tracks_with_raw_component_change", "root_position_raw_delta_max_component")},
            ensure_ascii=False))


if __name__ == "__main__":
    main()
