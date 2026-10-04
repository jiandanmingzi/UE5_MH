"""Export review FBXs from selected MHW WP10 LMT entries.

Run only in a fresh Blender background process, for example:

    blender.exe --background --factory-startup --python-exit-code 1 \
      --python Scripts/MHWorld/export_aerial_dive_candidates.py -- \
      --lmt D:/study/MHW/chunk/hm/wp/wp10/mot/w10_00/w10_00.lmt \
      --mod3 D:/study/MHW/chunk/pl/f_equip/pl001_0000/body/mod/f_body001_0000.mod3 \
      --addon-path "C:/Users/ROG/AppData/Roaming/Blender Foundation/Blender/5.2/scripts/addons" \
      --output-dir Saved/MHW_AerialDive_Export_20261002

The runtime IDs 49532/49533 do not directly identify LMT slots. The entries
exported here are explicitly marked as candidates for visual verification.
The script refuses to overwrite an existing output directory.
"""

import argparse
import json
import math
from pathlib import Path
import sys

import bpy


RUNTIME_TO_CANDIDATES = {
    # Slot 380 is the only populated WP10 LMT slot with a final key at frame 69.
    49532: [380],
    # Both slots have a final key at frame 161. Slot 381 also has substantial
    # root translation and immediately follows slot 380; 360 is retained as an
    # alternate because duration alone cannot prove the runtime mapping.
    49533: [381, 360],
}


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--lmt", type=Path, required=True)
    parser.add_argument("--mod3", type=Path, required=True)
    parser.add_argument("--addon-path", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    return parser.parse_args(sys.argv[sys.argv.index("--") + 1:])


def assert_fresh_background():
    if not bpy.app.background or "--factory-startup" not in sys.argv:
        raise RuntimeError("Use a separate Blender --background --factory-startup process.")
    if bpy.data.filepath:
        raise RuntimeError(f"Refusing to use an already-open project: {bpy.data.filepath}")


def action_metadata(action_data):
    frames = [
        int(frame)
        for bone in action_data["bone_action"].values()
        for channel in ("pos", "rot", "scl")
        for frame in bone.get(channel, {})
    ]
    end_frame = max(frames, default=0)
    root_pos = action_data["bone_action"].get(-1, {}).get("pos", {})
    root_keys = sorted((int(frame), tuple(float(v) for v in value))
                       for frame, value in root_pos.items())
    root_distance = 0.0
    root_peak_from_start = 0.0
    if root_keys:
        start = root_keys[0][1]
        end = root_keys[-1][1]
        root_distance = math.sqrt(sum((end[i] - start[i]) ** 2 for i in range(3)))
        root_peak_from_start = max(
            math.sqrt(sum((key[1][i] - start[i]) ** 2 for i in range(3)))
            for key in root_keys
        )
    return {
        "lmt_action_name": action_data["name"],
        "lmt_slot": int(action_data["name"].rsplit("_", 1)[-1]),
        "last_key_frame": end_frame,
        "root_position_key_count": len(root_keys),
        "root_start_to_end_distance_source_units": root_distance,
        "root_max_distance_from_start_source_units": root_peak_from_start,
        "bone_track_count": len(action_data["bone_action"]),
    }


def set_selection(objects, active):
    bpy.ops.object.select_all(action="DESELECT")
    for obj in objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = active


def main():
    assert_fresh_background()
    args = parse_args()
    for path in (args.lmt, args.mod3):
        if not path.is_file():
            raise FileNotFoundError(path)
    if not (args.addon_path / "mhworld_tools_suite" / "__init__.py").is_file():
        raise FileNotFoundError(args.addon_path / "mhworld_tools_suite" / "__init__.py")
    if args.output_dir.exists():
        raise FileExistsError(f"Refusing to overwrite existing output directory: {args.output_dir}")

    args.output_dir.mkdir(parents=True)
    sys.path.insert(0, str(args.addon_path))
    from mhworld_tools_suite.mod3.mod3_loader import load_mod3
    from mhworld_tools_suite.lmt.lmt_loader import load_lmt_data
    from mhworld_tools_suite.lmt.lmt_parser import LmtParser

    scene = bpy.context.scene
    scene.render.fps = 60
    scene.render.fps_base = 1.0

    imported_objects = load_mod3(
        str(args.mod3), LOD=0, fix_rotation=True, fix_scale=True,
        obj_name="MHW_AerialDive_Review", rename_bones=False,
        connect_bones=False,
    )
    rig = next((obj for obj in imported_objects if obj.type == "ARMATURE"), None)
    if rig is None:
        raise RuntimeError("MOD3 import produced no armature.")
    mesh_objects = [obj for obj in imported_objects if obj.type == "MESH"]
    if not mesh_objects:
        raise RuntimeError("MOD3 import produced no LOD0 mesh; cannot create a UE review Skeleton.")

    parsed_actions = LmtParser(str(args.lmt)).read()
    parsed_by_slot = {
        int(action["name"].rsplit("_", 1)[-1]): action
        for action in parsed_actions
    }
    requested_slots = sorted({slot for slots in RUNTIME_TO_CANDIDATES.values() for slot in slots})
    missing = [slot for slot in requested_slots if slot not in parsed_by_slot]
    if missing:
        raise RuntimeError(f"Requested LMT slots are absent: {missing}")

    # Keep only the chosen source actions in this review scene, not all 170 LMT
    # entries. This makes the saved .blend and FBX outputs easy to inspect.
    action_records = {}
    for slot in requested_slots:
        source_data = parsed_by_slot[slot]
        meta = action_metadata(source_data)
        action = load_lmt_data(source_data, {}, {}, rename_bones=False, set_fake_user=True)
        if not action.slots:
            raise RuntimeError(f"Blender action {action.name} has no Action slot.")
        action_records[slot] = {"action": action, "metadata": meta}

    # Reference skeletal mesh gives Unreal a dedicated MHW review Skeleton.
    set_selection(imported_objects, rig)
    scene.frame_start = 0
    scene.frame_end = 1
    scene.frame_set(0)
    model_fbx = args.output_dir / "SK_MHW_AerialDive_ReviewRig.fbx"
    result = bpy.ops.export_scene.fbx(
        filepath=str(model_fbx), use_selection=True,
        object_types={"ARMATURE", "MESH"}, axis_forward="-Y", axis_up="Z",
        add_leaf_bones=False, use_armature_deform_only=False,
        bake_anim=False,
    )
    if result != {"FINISHED"} or model_fbx.stat().st_size < 1000:
        raise RuntimeError("Failed to export the MHW reference skeletal mesh FBX.")

    for runtime_id, slots in RUNTIME_TO_CANDIDATES.items():
        for slot in slots:
            record = action_records[slot]
            action = record["action"]
            rig.animation_data_create()
            rig.animation_data.action = action
            rig.animation_data.action_slot = action.slots[0]
            start = math.floor(action.frame_range[0])
            end = math.ceil(action.frame_range[1])
            scene.frame_start, scene.frame_end = start, end
            scene.frame_set(start)
            set_selection([rig], rig)
            fbx = args.output_dir / f"MHW_WP10_Runtime{runtime_id}_Candidate_LMT{slot:03d}.fbx"
            result = bpy.ops.export_scene.fbx(
                filepath=str(fbx), use_selection=True,
                object_types={"ARMATURE"}, axis_forward="-Y", axis_up="Z",
                add_leaf_bones=False, use_armature_deform_only=False,
                bake_anim=True, bake_anim_use_all_actions=False,
                bake_anim_use_nla_strips=False,
                bake_anim_step=1.0, bake_anim_simplify_factor=0.0,
                bake_anim_force_startend_keying=True,
            )
            if result != {"FINISHED"} or fbx.stat().st_size < 1000:
                raise RuntimeError(f"Failed to export {fbx.name}")
            record["fbx"] = fbx.name

    # Preserve the review rig and the three selected Actions for Blender-side
    # playback if the Unreal import needs a second look.
    set_selection(imported_objects, rig)
    scene.frame_set(0)
    blend_file = args.output_dir / "MHW_AerialDive_CandidateReview.blend"
    bpy.ops.wm.save_as_mainfile(filepath=str(blend_file))

    manifest = {
        "status": "EXPORTED_CANDIDATES",
        "source_lmt": str(args.lmt),
        "source_mod3": str(args.mod3),
        "blender_version": bpy.app.version_string,
        "scene_fps": scene.render.fps,
        "mapping_warning": (
            "Runtime action_lmt_id values 49532/49533 are not direct indices in this LMT. "
            "Candidates were selected using key-span correspondence and slot adjacency; "
            "the exact FSM-to-LMT-slot mapping remains unproven."
        ),
        "reference_mesh_fbx": model_fbx.name,
        "runtime_candidates": {
            str(runtime_id): [action_records[slot]["metadata"] | {"fbx": action_records[slot]["fbx"]}
                              for slot in slots]
            for runtime_id, slots in RUNTIME_TO_CANDIDATES.items()
        },
        "saved_blend": blend_file.name,
    }
    manifest_path = args.output_dir / "candidate_manifest.json"
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
                             encoding="utf-8")
    print("MHW_AERIAL_DIVE_EXPORT=" + json.dumps(manifest, ensure_ascii=False))


if __name__ == "__main__":
    main()
