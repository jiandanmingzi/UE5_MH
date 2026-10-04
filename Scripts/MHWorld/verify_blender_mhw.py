"""Validate the staged MHW add-on and a synthetic skeleton/animation FBX round trip.

This deliberately does not claim to verify a real game's MOD3/LMT decoding.
Run with Blender --background --factory-startup --python-exit-code 1 --python ...
and pass -- --addon-path PATH --output-dir PATH.
"""

import argparse
import importlib.util
import json
import math
from pathlib import Path
import sys

import bpy
from mathutils import Matrix, Quaternion, Vector


def arguments():
    parser = argparse.ArgumentParser()
    parser.add_argument("--addon-path", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    return parser.parse_args(sys.argv[sys.argv.index("--") + 1:])


def load_addon(path):
    name = "mhworld_tools_suite"
    spec = importlib.util.spec_from_file_location(
        name, path / "__init__.py", submodule_search_locations=[str(path)])
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    module.register()
    assert bpy.ops.mhworld_import.mhworld_mod3.get_rna_type()
    assert bpy.ops.mhworld_import.mhworld_lmt.get_rna_type()
    return module


def fixture_mod3_loader():
    from mhworld_tools_suite.mod3 import mod3_loader
    identity = [list(row) for row in Matrix.Identity(4)]
    offset = Matrix.Translation((0, 100, 0)).transposed()
    bones = [
        {"id": 0, "remap": 0, "parent": 255, "local_matrix": identity,
         "global_matrix": identity, "x": 0, "y": 100, "z": 0, "length": 100},
        {"id": 1, "remap": 1, "parent": 0,
         "local_matrix": [list(row) for row in offset], "global_matrix": identity,
         "x": 0, "y": 100, "z": 0, "length": 100},
    ]
    mesh = {"lod": 0, "group": 0, "id": 0, "material": "Synthetic",
            "material_name_hash": 0,
            "positions": [(0, 100, 0), (10, 100, 0), (0, 110, 0)],
            "faces": [(0, 1, 2)], "normals": [(0, 0, 1)] * 3,
            "weights_names": [["bone_001"]] * 3, "weights_values": [[1.0]] * 3}

    class FixtureParser:
        def __init__(self, **_kwargs):
            pass

        def read(self):
            return bones, [mesh]

    original = mod3_loader.Mod3Parser
    try:
        mod3_loader.Mod3Parser = FixtureParser
        objects = mod3_loader.load_mod3("SYNTHETIC_PIPELINE_CHECK.mod3",
                                        fix_rotation=True, fix_scale=True)
    finally:
        mod3_loader.Mod3Parser = original
    return next(obj for obj in objects if obj.type == "ARMATURE"), objects


def fixture_lmt_loader(rig):
    from mhworld_tools_suite.lmt.lmt_loader import load_lmt_data
    q = Quaternion((1, 0, 0), math.radians(30))
    data = {"name": "SYNTHETIC_LMT_000", "bone_action": {
        -1: {"pos_referencial": "global", "pos": {
            0: [0, 0, 0], 15: [50, 10, 25], 30: [100, 20, 50]}},
        0: {"rot_referencial": "local", "rot": {
            0: [0, 0, 0, 1], 30: [q.x, q.y, q.z, q.w]}},
        1: {"scl": {0: [1, 1, 1], 30: [1, 1, 1]}},
    }}
    action = load_lmt_data(data, {}, {}, set_fake_user=True)
    rig.animation_data_create()
    rig.animation_data.action = action
    if hasattr(action, "slots"):
        rig.animation_data.action_slot = action.slots[0]
    assert list(action.frame_range) == [0.0, 30.0]
    return action


def root_samples(rig):
    result = {}
    for frame in (0, 15, 30):
        bpy.context.scene.frame_set(frame)
        bpy.context.view_layer.update()
        matrix = rig.matrix_world @ rig.pose.bones["bone_root"].matrix
        result[frame] = matrix.translation.copy()
    return result


def mesh_samples():
    result = {}
    meshes = [obj for obj in bpy.context.scene.objects if obj.type == "MESH"]
    assert len(meshes) == 1
    for frame in (0, 15, 30):
        bpy.context.scene.frame_set(frame)
        bpy.context.view_layer.update()
        evaluated = meshes[0].evaluated_get(bpy.context.evaluated_depsgraph_get())
        result[frame] = [evaluated.matrix_world @ vertex.co for vertex in evaluated.data.vertices]
    return result


def main():
    if not bpy.app.background or "--factory-startup" not in sys.argv:
        raise RuntimeError("Run this check in a separate Blender --background --factory-startup process.")
    if bpy.data.filepath:
        raise RuntimeError("Refusing to run with an explicitly loaded .blend file.")
    args = arguments()
    if args.output_dir.exists():
        raise RuntimeError(f"Output directory exists; refusing to overwrite: {args.output_dir}")
    args.output_dir.mkdir(parents=True)
    load_addon(args.addon_path)
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    rig, objects = fixture_mod3_loader()
    fixture_lmt_loader(rig)
    scene = bpy.context.scene
    scene.render.fps = 60
    scene.render.fps_base = 1.0
    scene.frame_start, scene.frame_end = 0, 30
    samples_before = root_samples(rig)
    mesh_before = mesh_samples()
    expected_final = Vector((1.0, -0.5, 0.2))
    assert (samples_before[30] - expected_final).length < 1e-5
    bpy.ops.object.select_all(action="DESELECT")
    for obj in objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = rig
    scene.frame_set(0)
    fbx = args.output_dir / "synthetic_pipeline_check.fbx"
    result = bpy.ops.export_scene.fbx(
        filepath=str(fbx), use_selection=True, object_types={"ARMATURE", "MESH"},
        axis_forward="-Y", axis_up="Z", add_leaf_bones=False,
        use_armature_deform_only=False, bake_anim=True,
        bake_anim_use_all_actions=False, bake_anim_use_nla_strips=False,
        bake_anim_step=1.0, bake_anim_simplify_factor=0.0,
        bake_anim_force_startend_keying=True)
    assert result == {"FINISHED"} and fbx.stat().st_size > 1000
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    assert bpy.ops.import_scene.fbx(filepath=str(fbx), use_anim=True, anim_offset=0.0,
                                    automatic_bone_orientation=False) == {"FINISHED"}
    imported = [obj for obj in bpy.context.scene.objects if obj.type == "ARMATURE"]
    assert len(imported) == 1
    rig = imported[0]
    assert set(rig.data.bones.keys()) == {"bone_root", "bone_000", "bone_001"}
    assert rig.data.bones["bone_000"].parent.name == "bone_root"
    assert rig.animation_data and rig.animation_data.action
    samples_after = root_samples(rig)
    mesh_after = mesh_samples()
    max_error = max((samples_after[f] - samples_before[f]).length for f in samples_before)
    assert max_error < 1e-4, f"FBX root-motion round trip error: {max_error}"
    for frame in mesh_before:
        assert len(mesh_before[frame]) == len(mesh_after[frame])
    mesh_error = max(min((a - b).length for b in mesh_after[f])
                     for f in mesh_before for a in mesh_before[f])
    assert mesh_error < 1e-4, f"FBX deformed-mesh round trip error: {mesh_error}"
    report = {"status": "PASS", "blender_version": bpy.app.version_string,
              "source": "Synthetic parser-output fixture; no actual game source file",
              "mod3_operator_registered": True, "lmt_operator_registered": True,
              "action_slot_binding": True, "frame_range": [0, 30], "fps": 60,
              "fbx_bytes": fbx.stat().st_size, "bone_count": len(rig.data.bones),
              "root_world_samples_before": {str(k): list(v) for k, v in samples_before.items()},
              "root_world_samples_after": {str(k): list(v) for k, v in samples_after.items()},
              "max_root_world_error_blender_units": max_error,
              "max_deformed_mesh_world_error_blender_units": mesh_error}
    (args.output_dir / "verification.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print("MHGZ_MHW_VERIFICATION=" + json.dumps(report))


if __name__ == "__main__":
    main()
