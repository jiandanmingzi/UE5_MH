"""Read-only comparison of raw LMT and exported review rig scale tracks.

Run in a separate Blender --background --factory-startup process. Writes only
the JSON diagnostic report; never saves or modifies a blend/FBX/UE asset.
"""

import argparse
import json
from pathlib import Path
import sys

import bpy


def limits(values):
    return {"min": [min(row[i] for row in values) for i in range(3)],
            "max": [max(row[i] for row in values) for i in range(3)]}


def curves(action):
    for layer in action.layers:
        for strip in layer.strips:
            for bag in strip.channelbags:
                yield from bag.fcurves


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--addon-path", required=True)
    parser.add_argument("--lmt", required=True)
    parser.add_argument("--blend", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:])
    if not bpy.app.background or "--factory-startup" not in sys.argv:
        raise RuntimeError("Requires an isolated background process")
    sys.path.insert(0, str(Path(args.addon_path).parent))
    from mhworld_tools_suite.lmt.lmt_parser import LmtParser

    raw_actions = LmtParser(args.lmt).read()
    raw_actions = {int(row["name"].rsplit("_", 1)[-1]): row for row in raw_actions}
    bpy.ops.wm.open_mainfile(filepath=args.blend)
    rig = max((obj for obj in bpy.data.objects if obj.type == "ARMATURE"),
              key=lambda obj: len(obj.data.bones))
    names = ("bone_root", "bone_000", "bone_008", "bone_012", "bone_029", "bone_030", "bone_031")
    report = {"source_lmt": args.lmt, "review_blend": args.blend,
              "armature_world_matrix": [list(row) for row in rig.matrix_world],
              "bone_count": len(rig.data.bones), "bones": {}, "animations": {}}
    for name in names:
        bone = rig.data.bones.get(name)
        if bone is None:
            continue
        weighted_vertices = 0
        for obj in bpy.data.objects:
            if obj.type != "MESH":
                continue
            group = obj.vertex_groups.get(name)
            if group:
                weighted_vertices += sum(1 for v in obj.data.vertices
                                         if any(g.group == group.index and g.weight > 0 for g in v.groups))
        report["bones"][name] = {"parent": bone.parent.name if bone.parent else None,
                                  "children": [c.name for c in bone.children],
                                  "inherit_scale": bone.inherit_scale,
                                  "rest_head": list(bone.head_local),
                                  "rest_matrix_scale": list(bone.matrix_local.to_scale()),
                                  "weighted_vertices": weighted_vertices}
    rig.animation_data_create()
    for index in (380, 381):
        raw = raw_actions[index]
        raw_tracks = raw["bone_action"].get(30, {})
        action = next(a for a in bpy.data.actions if a.name == f"w10_00_{index}_FullBodyReview")
        rig.animation_data.action = action
        rig.animation_data.action_slot = action.slots[0]
        action_curves = list(curves(action))
        start = min(k.co.x for c in action_curves for k in c.keyframe_points)
        end = max(k.co.x for c in action_curves for k in c.keyframe_points)
        samples = []
        for frame in range(int(start), int(end) + 1):
            bpy.context.scene.frame_set(frame)
            bone = rig.pose.bones["bone_030"]
            parent_relative = bone.parent.matrix.inverted() @ bone.matrix if bone.parent else bone.matrix
            samples.append({"frame": frame, "channel_scale": list(bone.scale),
                            "component_scale": list(bone.matrix.to_scale()),
                            "parent_relative_scale": list(parent_relative.to_scale())})
        raw_scale = list(raw_tracks.get("scl", {}).values())
        report["animations"][str(index)] = {
            "frame_range": [start, end], "fps": bpy.context.scene.render.fps,
            "raw_bone030_scale_reference": raw_tracks.get("scl_referencial"),
            "raw_bone030_scale_keys": raw_tracks.get("scl"),
            "raw_bone030_scale_range": limits(raw_scale) if raw_scale else None,
            "blender_channel_scale_range": limits([s["channel_scale"] for s in samples]),
            "blender_component_scale_range": limits([s["component_scale"] for s in samples]),
            "blender_parent_relative_scale_range": limits([s["parent_relative_scale"] for s in samples]),
            "samples": samples,
        }
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print("MHW_WEAPON_SCALE_PROBE=" + json.dumps({
        "bones": report["bones"], "animations": {
            k: {n: v for n, v in row.items() if n not in ("samples", "raw_bone030_scale_keys")}
            for k, row in report["animations"].items()}}, ensure_ascii=False))


main()
