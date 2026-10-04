"""Report which MOD3 bone IDs actually change in selected MHW LMT entries.

This is a read-only diagnostic. It distinguishes an FBX/import problem from an
LMT action that simply does not animate the lower-body tracks.
"""

import argparse
import json
from pathlib import Path
import sys


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--addon-path", type=Path, required=True)
    parser.add_argument("--mod3", type=Path, required=True)
    parser.add_argument("--lmt", type=Path, required=True)
    parser.add_argument("--slots", default="360,380,381")
    return parser.parse_args(sys.argv[sys.argv.index("--") + 1:])


def track_delta(values):
    if not values:
        return 0.0
    rows = list(values.values())
    if len(rows) < 2:
        return 0.0
    first = rows[0]
    return max(
        abs(float(component) - float(first[index]))
        for row in rows[1:]
        for index, component in enumerate(row)
    )


def summarize_channel(data, name):
    values = data.get(name)
    if not isinstance(values, dict):
        return None
    frames = [int(frame) for frame in values]
    return {
        "keys": len(values),
        "first_frame": min(frames) if frames else None,
        "last_frame": max(frames) if frames else None,
        "max_abs_component_delta_from_first": track_delta(values),
    }


def main():
    args = parse_args()
    for path in (args.mod3, args.lmt):
        if not path.is_file():
            raise FileNotFoundError(path)
    sys.path.insert(0, str(args.addon_path))
    from mhworld_tools_suite.mod3.mod3_parser import Mod3Parser
    from mhworld_tools_suite.lmt.lmt_parser import LmtParser

    mod3 = Mod3Parser(path=str(args.mod3))
    bones, _ = mod3.read()
    bone_by_id = {
        int(bone["remap"]): {
            "bone_index": int(bone["id"]),
            "parent_remap": (None if int(bone["parent"]) == 255
                             else int(bones[int(bone["parent"])]["remap"])),
            "global_translation": [
                float(bone["global_matrix"][row][3]) for row in range(3)
            ],
        }
        for bone in bones
    }
    actions = LmtParser(path=str(args.lmt)).read()
    by_slot = {int(action["name"].rsplit("_", 1)[-1]): action for action in actions}
    report = {"lmt": str(args.lmt), "reference_mod3": str(args.mod3), "actions": []}
    for slot in (int(value.strip()) for value in args.slots.split(",") if value.strip()):
        action = by_slot.get(slot)
        if action is None:
            report["actions"].append({"slot": slot, "present": False})
            continue
        rows = []
        for raw_id, channels in sorted(action["bone_action"].items(), key=lambda item: int(item[0])):
            bone_id = int(raw_id)
            if bone_id == -1:
                bone_info = {"role": "root"}
            else:
                bone_info = bone_by_id.get(bone_id, {"missing_from_reference_mod3": True})
            channel_data = {
                key: summary for key in ("pos", "rot", "scl")
                if (summary := summarize_channel(channels, key)) is not None
            }
            rows.append({"bone_id": bone_id, **bone_info, "channels": channel_data})
        report["actions"].append({
            "slot": slot,
            "name": action["name"],
            "track_count": len(rows),
            "tracks_with_transform_motion": sum(
                1 for row in rows
                if any(value["max_abs_component_delta_from_first"] > 1e-5
                       for value in row["channels"].values())
            ),
            "tracks": rows,
        })
    print("MHW_LMT_ACTION_MOTION=" + json.dumps(report, ensure_ascii=False))


if __name__ == "__main__":
    main()
