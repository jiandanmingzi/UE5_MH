"""Build a review-only full-body MHW WP10 candidate rig from armor MOD3 parts.

The body MOD3 supplies the canonical animated hierarchy. Extra MOD3 parts are
rebound to it by raw bone ID. A part-specific alias is created only when the
source part has a bone absent from the body rig or a materially different rest
matrix; candidate LMT tracks are duplicated onto animated aliases. The script
checks evaluated mesh positions before/after rebinding and refuses to export if
the bind-pose geometry changes beyond tolerance.

This is an inspection/export pipeline, not a gameplay asset generator. It does
not overwrite an existing output directory.
"""

import argparse
import json
import math
from pathlib import Path
import re
import sys

import bpy


BODY_FILE = "f_body001_0000.mod3"
REST_MATRIX_TOLERANCE = 1e-5
GEOMETRY_TOLERANCE = 1e-4
RUNTIME_TO_CANDIDATES = {
    49532: [380],
    49533: [381, 360],
}


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--lmt", type=Path, required=True)
    parser.add_argument("--mod3", type=Path, action="append", required=True)
    parser.add_argument("--addon-path", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    return parser.parse_args(sys.argv[sys.argv.index("--") + 1:])


def assert_fresh_background():
    if not bpy.app.background or "--factory-startup" not in sys.argv:
        raise RuntimeError("Use a separate Blender --background --factory-startup process.")
    if bpy.data.filepath:
        raise RuntimeError(f"Refusing to use an already-open project: {bpy.data.filepath}")


def source_objects(path, load_mod3):
    part = path.stem.replace("f_", "")
    objects = load_mod3(
        str(path), LOD=0, fix_rotation=True, fix_scale=True,
        obj_name="MHW_" + part, rename_bones=False, connect_bones=False,
    )
    armature = next((obj for obj in objects if obj.type == "ARMATURE"), None)
    meshes = [obj for obj in objects if obj.type == "MESH"]
    if armature is None or not meshes:
        raise RuntimeError(f"Expected an armature and LOD0 mesh in {path}")
    return {"name": part, "path": path, "armature": armature, "meshes": meshes}


def bone_map_for_part(part, canonical_bones):
    armature = part["armature"]
    names = {bone.name for bone in armature.data.bones}
    mapping = {"bone_root": "bone_root"}
    aliases = {}
    for bone in armature.data.bones:
        if bone.name == "bone_root":
            continue
        canonical = canonical_bones.get(bone.name)
        needs_alias = canonical is None
        if canonical is not None:
            delta = max(
                abs(bone.matrix_local[row][column] - canonical.matrix_local[row][column])
                for row in range(4) for column in range(4)
            )
            needs_alias = delta > REST_MATRIX_TOLERANCE
        if needs_alias:
            alias = f"MHW_{part['name']}_{bone.name}"
            if alias in bpy.data.armatures:
                raise RuntimeError(f"Alias bone name collision: {alias}")
            mapping[bone.name] = alias
            aliases[bone.name] = alias
        else:
            # A part may omit an ancestor. If the numeric bone's bind matrix
            # matches the full body rig, use the full hierarchy's bone.
            mapping[bone.name] = bone.name
    missing_groups = []
    for obj in part["meshes"]:
        for group in obj.vertex_groups:
            if group.name.startswith("bone_") and group.name not in names:
                missing_groups.append({"mesh": obj.name, "group": group.name})
    if missing_groups:
        raise RuntimeError(f"Vertex groups have no source bone in {part['name']}: {missing_groups[:8]}")
    return mapping, aliases


def add_alias_bones(target, parts, part_maps):
    bpy.context.view_layer.objects.active = target
    bpy.ops.object.mode_set(mode="EDIT", toggle=False)
    target_edit = target.data.edit_bones
    for part in parts:
        mapping, aliases = part_maps[part["name"]]
        source_bones = part["armature"].data.bones
        pending = [bone for bone in source_bones if bone.name in aliases]
        while pending:
            progressed = False
            for source_bone in pending[:]:
                source_parent_name = source_bone.parent.name if source_bone.parent else None
                target_parent_name = mapping.get(source_parent_name, source_parent_name)
                if source_parent_name and target_parent_name not in target_edit:
                    continue
                alias_name = aliases[source_bone.name]
                new_bone = target_edit.new(alias_name)
                if source_parent_name:
                    new_bone.parent = target_edit[target_parent_name]
                new_bone.matrix = source_bone.matrix_local.copy()
                new_bone.length = source_bone.length
                new_bone.use_deform = source_bone.use_deform
                new_bone.inherit_scale = source_bone.inherit_scale
                pending.remove(source_bone)
                progressed = True
            if not progressed:
                unresolved = [bone.name for bone in pending]
                raise RuntimeError(f"Could not resolve alias parents for {part['name']}: {unresolved}")
    bpy.ops.object.mode_set(mode="OBJECT", toggle=False)


def world_vertices(obj, depsgraph):
    evaluated = obj.evaluated_get(depsgraph)
    mesh = evaluated.to_mesh()
    try:
        return [obj.matrix_world @ vertex.co for vertex in mesh.vertices]
    finally:
        evaluated.to_mesh_clear()


def snapshot_mesh_positions(parts):
    bpy.context.view_layer.update()
    depsgraph = bpy.context.evaluated_depsgraph_get()
    return {
        obj.name: world_vertices(obj, depsgraph)
        for part in parts for obj in part["meshes"]
    }


def rebind_meshes(parts, target, part_maps):
    for part in parts:
        mapping, _ = part_maps[part["name"]]
        if part["armature"] is target:
            continue
        for obj in part["meshes"]:
            original_world = obj.matrix_world.copy()
            for group in obj.vertex_groups:
                new_name = mapping.get(group.name)
                if new_name and new_name != group.name:
                    group.name = new_name
            obj.parent = target
            obj.matrix_parent_inverse = target.matrix_world.inverted()
            obj.matrix_world = original_world
            for modifier in obj.modifiers:
                if modifier.type == "ARMATURE":
                    modifier.object = target
            if not any(mod.type == "ARMATURE" for mod in obj.modifiers):
                modifier = obj.modifiers.new("MHW_FullBody_Armature", "ARMATURE")
                modifier.object = target
    bpy.context.view_layer.update()


def verify_mesh_positions(before, parts):
    bpy.context.view_layer.update()
    depsgraph = bpy.context.evaluated_depsgraph_get()
    results = {}
    max_delta = 0.0
    for part in parts:
        for obj in part["meshes"]:
            previous = before[obj.name]
            current = world_vertices(obj, depsgraph)
            if len(previous) != len(current):
                raise RuntimeError(f"Vertex count changed during bind check for {obj.name}")
            delta = max(((a - b).length for a, b in zip(previous, current)), default=0.0)
            max_delta = max(max_delta, delta)
            results[obj.name] = {"vertices": len(current), "max_world_delta": delta}
    if max_delta > GEOMETRY_TOLERANCE:
        raise RuntimeError(
            f"Rebinding changed bind-pose geometry (max {max_delta:.6g} > "
            f"{GEOMETRY_TOLERANCE}); refusing export."
        )
    return {"max_world_delta": max_delta, "mesh_count": len(results), "meshes": results}


def base_pose_maps(armature):
    positions = {}
    rotations = {}
    for bone in armature.data.bones:
        if bone.parent is not None:
            positions[bone.name] = (
                bone.parent.matrix_local.inverted() @ bone.matrix_local
            ).to_translation()
            rotations[bone.name] = bone.matrix_local.inverted().to_quaternion()
    return positions, rotations


def action_curves(action):
    if hasattr(action, "fcurves"):
        return action.fcurves
    if not action.slots or not action.layers or not action.layers[0].strips:
        return []
    bag = action.layers[0].strips[0].channelbag(action.slots[0])
    return bag.fcurves if bag else []


def new_action_curves(action):
    if hasattr(action, "fcurves"):
        return action.fcurves
    slot = action.slots.new(id_type="OBJECT", name="MHW FullBody")
    layer = action.layers.new("MHW LMT")
    strip = layer.strips.new(type="KEYFRAME")
    return strip.channelbag(slot, ensure=True).fcurves


def copy_curve(source, destination_curves, destination_path):
    group_name = source.group.name if source.group else "MHW LMT"
    if hasattr(destination_curves, "new"):
        try:
            destination = destination_curves.new(
                data_path=destination_path,
                index=source.array_index,
                group_name=group_name,
            )
        except TypeError:
            destination = destination_curves.new(
                data_path=destination_path,
                index=source.array_index,
                action_group=group_name,
            )
    for source_key in source.keyframe_points:
        key = destination.keyframe_points.insert(
            float(source_key.co.x), float(source_key.co.y)
        )
        key.interpolation = source_key.interpolation
        key.handle_left_type = source_key.handle_left_type
        key.handle_right_type = source_key.handle_right_type
    destination.update()


def merge_lmt_action(lmt_data, parts, target, part_maps, load_lmt_data):
    merged = bpy.data.actions.new(lmt_data["name"] + "_FullBodyReview")
    merged.use_fake_user = True
    destination_curves = new_action_curves(merged)
    target_bone_names = {bone.name for bone in target.data.bones}
    curve_count = 0
    for part in parts:
        source_armature = part["armature"]
        mapping, _ = part_maps[part["name"]]
        source_bone_names = {bone.name for bone in source_armature.data.bones}
        allowed_ids = {-1}
        allowed_ids.update(
            int(name[len("bone_"):]) for name in source_bone_names
            if name.startswith("bone_") and name[len("bone_"):].isdigit()
        )
        filtered = dict(lmt_data)
        filtered["bone_action"] = {
            track_id: track for track_id, track in lmt_data["bone_action"].items()
            if int(track_id) in allowed_ids
        }
        positions, rotations = base_pose_maps(source_armature)
        source_action = load_lmt_data(
            filtered, positions, rotations, rename_bones=False, set_fake_user=True
        )
        for curve in action_curves(source_action):
            match = re.match(r'pose\.bones\["(bone_\d+)"\]', curve.data_path)
            if match is None:
                if part["armature"] is target:
                    copy_curve(curve, destination_curves, curve.data_path)
                    curve_count += 1
                continue
            source_bone_name = match.group(1)
            destination_bone_name = mapping.get(source_bone_name, source_bone_name)
            if destination_bone_name == source_bone_name:
                if source_armature is not target:
                    continue
            elif destination_bone_name not in target_bone_names:
                raise RuntimeError(f"Merged rig lacks destination bone {destination_bone_name}")
            path = curve.data_path.replace(
                f'pose.bones["{source_bone_name}"]',
                f'pose.bones["{destination_bone_name}"]',
                1,
            )
            copy_curve(curve, destination_curves, path)
            curve_count += 1
        bpy.data.actions.remove(source_action)
    if not curve_count:
        raise RuntimeError(f"No animation curves were merged for {lmt_data['name']}")
    return merged, curve_count


def export_model(target, meshes, filepath):
    bpy.ops.object.select_all(action="DESELECT")
    target.select_set(True)
    for obj in meshes:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = target
    result = bpy.ops.export_scene.fbx(
        filepath=str(filepath), use_selection=True,
        object_types={"ARMATURE", "MESH"}, axis_forward="-Y", axis_up="Z",
        add_leaf_bones=False, use_armature_deform_only=False,
        bake_anim=False,
    )
    if result != {"FINISHED"} or filepath.stat().st_size < 1000:
        raise RuntimeError(f"Failed to export full-body reference mesh: {filepath}")


def export_animation(target, action, filepath):
    target.animation_data_create()
    target.animation_data.action = action
    if not hasattr(action, "fcurves") and action.slots:
        target.animation_data.action_slot = action.slots[0]
    frame_values = [
        int(round(key.co.x))
        for curve in action_curves(action)
        for key in curve.keyframe_points
    ]
    start, end = min(frame_values), max(frame_values)
    scene = bpy.context.scene
    scene.frame_start, scene.frame_end = start, end
    scene.frame_set(start)
    bpy.ops.object.select_all(action="DESELECT")
    target.select_set(True)
    bpy.context.view_layer.objects.active = target
    result = bpy.ops.export_scene.fbx(
        filepath=str(filepath), use_selection=True,
        object_types={"ARMATURE"}, axis_forward="-Y", axis_up="Z",
        add_leaf_bones=False, use_armature_deform_only=False,
        bake_anim=True, bake_anim_use_all_actions=False,
        bake_anim_use_nla_strips=False, bake_anim_step=1.0,
        bake_anim_simplify_factor=0.0,
    )
    if result != {"FINISHED"} or filepath.stat().st_size < 1000:
        raise RuntimeError(f"Failed to export full-body animation: {filepath}")
    return start, end


def main():
    assert_fresh_background()
    args = parse_args()
    for path in [args.lmt, *args.mod3]:
        if not path.is_file():
            raise FileNotFoundError(path)
    if not (args.addon_path / "mhworld_tools_suite" / "__init__.py").is_file():
        raise FileNotFoundError(args.addon_path / "mhworld_tools_suite" / "__init__.py")
    if args.output_dir.exists():
        raise FileExistsError(f"Refusing to overwrite existing output directory: {args.output_dir}")
    if not any(path.name == BODY_FILE for path in args.mod3):
        raise RuntimeError(f"Include the canonical body MOD3 {BODY_FILE}")

    sys.path.insert(0, str(args.addon_path))
    from mhworld_tools_suite.mod3.mod3_loader import load_mod3
    from mhworld_tools_suite.lmt.lmt_parser import LmtParser
    from mhworld_tools_suite.lmt.lmt_loader import load_lmt_data

    parts = [source_objects(path, load_mod3) for path in args.mod3]
    body = next(part for part in parts if part["path"].name == BODY_FILE)
    target = body["armature"]
    canonical_bones = {bone.name: bone for bone in target.data.bones}

    for part in parts:
        object_delta = max(
            abs(part["armature"].matrix_world[row][column] - target.matrix_world[row][column])
            for row in range(4) for column in range(4)
        )
        if object_delta > 1e-5:
            raise RuntimeError(f"MOD3 armature object transforms differ: {part['name']}")

    part_maps = {}
    for part in parts:
        if part is body:
            part_maps[part["name"]] = (
                {bone.name: bone.name for bone in target.data.bones}, {}
            )
        else:
            part_maps[part["name"]] = bone_map_for_part(part, canonical_bones)

    before = snapshot_mesh_positions(parts)
    add_alias_bones(target, [part for part in parts if part is not body], part_maps)
    rebind_meshes(parts, target, part_maps)
    geometry_check = verify_mesh_positions(before, parts)
    meshes = [obj for part in parts for obj in part["meshes"]]

    parsed_actions = LmtParser(str(args.lmt)).read()
    parsed_by_slot = {
        int(action["name"].rsplit("_", 1)[-1]): action
        for action in parsed_actions
    }
    requested_slots = sorted({slot for slots in RUNTIME_TO_CANDIDATES.values() for slot in slots})
    missing_slots = [slot for slot in requested_slots if slot not in parsed_by_slot]
    if missing_slots:
        raise RuntimeError(f"Selected candidate slots are missing from LMT: {missing_slots}")

    args.output_dir.mkdir(parents=True)
    scene = bpy.context.scene
    scene.render.fps = 60
    scene.render.fps_base = 1.0
    model_fbx = args.output_dir / "SK_MHW_AerialDive_FullBody_ReviewRig.fbx"
    export_model(target, meshes, model_fbx)

    action_outputs = {}
    for slot in requested_slots:
        lmt_data = parsed_by_slot[slot]
        action, curve_count = merge_lmt_action(lmt_data, parts, target, part_maps, load_lmt_data)
        filepath = args.output_dir / f"MHW_WP10_Candidate_LMT{slot:03d}_FullBody.fbx"
        start, end = export_animation(target, action, filepath)
        action_outputs[str(slot)] = {
            "action_name": lmt_data["name"],
            "merged_curve_count": curve_count,
            "start_frame": start,
            "end_frame": end,
            "fbx": filepath.name,
        }

    target.animation_data.action = None
    scene.frame_set(0)
    blend_file = args.output_dir / "MHW_AerialDive_FullBody_Review.blend"
    bpy.ops.wm.save_as_mainfile(filepath=str(blend_file))
    aliases = {
        part_name: alias_map
        for part_name, (_, alias_map) in part_maps.items() if alias_map
    }
    report = {
        "status": "EXPORTED_FULLBODY_REVIEW_CANDIDATES",
        "source_lmt": str(args.lmt),
        "source_mod3": {part["name"]: str(part["path"]) for part in parts},
        "canonical_skeleton_part": BODY_FILE,
        "rebound_mesh_count": len(meshes),
        "preserved_bind_pose": geometry_check,
        "part_specific_alias_bones": aliases,
        "animation_candidates": action_outputs,
        "reference_mesh_fbx": model_fbx.name,
        "saved_blend": blend_file.name,
        "mapping_warning": (
            "These LMT slots remain visual candidates; the runtime action-ID to slot mapping "
            "is not proven. This review rig is not a production MHW skeleton."
        ),
    }
    manifest_path = args.output_dir / "fullbody_candidate_manifest.json"
    manifest_path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print("MHW_FULLBODY_EXPORT=" + json.dumps(report, ensure_ascii=False))


if __name__ == "__main__":
    main()
