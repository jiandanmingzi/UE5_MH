"""Try a review-only full-body mesh import against the known MHW body Skeleton.

This is a separate, additive compatibility probe. It never replaces the earlier
imports or gameplay assets. The existing body-only Skeleton is supplied
explicitly because UE's first automatic full-body import did not associate one.
"""

from __future__ import annotations

import json
import os
import unreal


PROJECT_ROOT = unreal.Paths.project_dir()
SOURCE_DIR = os.path.join(PROJECT_ROOT, "Saved", "MHW_AerialDive_FullBody_Export_20261002")
DESTINATION = "/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/Review/Unknown/MHW_AerialDive_FullBody_AssignedSkeleton"
CANONICAL_SKELETON = "/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/Review/Unknown/MHW_AerialDive_Candidates/SK_MHW_AerialDive_ReviewRig_Skeleton"
REPORT_PATH = os.path.join(SOURCE_DIR, "ue_assigned_skeleton_import_manifest.json")
MODEL_FILE = "SK_MHW_AerialDive_FullBody_ReviewRig.fbx"
ANIMATION_FILES = (
    "MHW_WP10_Candidate_LMT380_FullBody.fbx",
    "MHW_WP10_Candidate_LMT381_FullBody.fbx",
    "MHW_WP10_Candidate_LMT360_FullBody.fbx",
)


def import_task(filename: str, destination_name: str, *, skeletal_mesh: bool, skeleton=None):
    source = os.path.join(SOURCE_DIR, filename)
    if not os.path.isfile(source):
        raise FileNotFoundError(source)
    options = unreal.FbxImportUI()
    options.set_editor_property("import_mesh", skeletal_mesh)
    options.set_editor_property("import_as_skeletal", True)
    options.set_editor_property("import_animations", not skeletal_mesh)
    options.set_editor_property("import_materials", False)
    options.set_editor_property("import_textures", False)
    if skeleton is not None:
        options.set_editor_property("skeleton", skeleton)
    if hasattr(options, "mesh_type_to_import"):
        options.set_editor_property(
            "mesh_type_to_import",
            unreal.FBXImportType.FBXIT_SKELETAL_MESH if skeletal_mesh
            else unreal.FBXImportType.FBXIT_ANIMATION,
        )
    if hasattr(options, "automated_import_should_detect_type"):
        options.set_editor_property("automated_import_should_detect_type", False)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", source)
    task.set_editor_property("destination_path", DESTINATION)
    task.set_editor_property("destination_name", destination_name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", False)
    task.set_editor_property("save", True)
    task.set_editor_property("options", options)
    task.set_editor_property("factory", unreal.FbxFactory())
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    paths = list(task.get_editor_property("imported_object_paths"))
    if not paths:
        raise RuntimeError("Import produced no objects for {}".format(filename))
    assets = [(path.split(".")[0], unreal.EditorAssetLibrary.load_asset(path)) for path in paths]
    if any(asset is None for _, asset in assets):
        raise RuntimeError("An imported object could not be loaded: {}".format(paths))
    return assets


def main():
    skeleton = unreal.EditorAssetLibrary.load_asset(CANONICAL_SKELETON)
    if skeleton is None:
        raise FileNotFoundError("Canonical review Skeleton is unavailable: " + CANONICAL_SKELETON)
    if not os.path.isfile(os.path.join(SOURCE_DIR, "fullbody_candidate_manifest.json")):
        raise FileNotFoundError("Missing full-body Blender export manifest")

    mesh_name = "SK_MHW_AerialDive_FullBody_ReviewRig_Assigned"
    mesh_path = DESTINATION + "/" + mesh_name
    if unreal.EditorAssetLibrary.does_asset_exist(mesh_path):
        raise FileExistsError("Probe destination already exists; refusing to overwrite: " + mesh_path)
    imported = import_task(MODEL_FILE, mesh_name, skeletal_mesh=True, skeleton=skeleton)
    if mesh_path not in [path for path, _ in imported]:
        raise RuntimeError("Expected review mesh at {}, got {}".format(mesh_path, [p for p, _ in imported]))
    mesh = unreal.EditorAssetLibrary.load_asset(mesh_path)
    if mesh is None:
        raise RuntimeError("Imported full-body mesh could not be loaded: " + mesh_path)
    actual_skeleton = mesh.get_editor_property("skeleton")
    if actual_skeleton != skeleton:
        actual_path = (unreal.EditorAssetLibrary.get_path_name_for_loaded_asset(actual_skeleton)
                       if actual_skeleton else None)
        raise RuntimeError("Full-body mesh did not bind to requested Skeleton; got {}".format(actual_path))

    animations = []
    for filename in ANIMATION_FILES:
        asset_name = os.path.splitext(filename)[0].replace("MHW_WP10_", "AS_MHW_WP10_")
        imported_animation = import_task(filename, asset_name, skeletal_mesh=False, skeleton=skeleton)
        expected_path = DESTINATION + "/" + asset_name
        if len(imported_animation) != 1 or imported_animation[0][0] != expected_path:
            raise RuntimeError("Unexpected animation import paths: {}".format(
                [path for path, _ in imported_animation]))
        sequence = imported_animation[0][1]
        if sequence.get_editor_property("skeleton") != skeleton:
            raise RuntimeError("Animation did not bind to canonical Skeleton: " + expected_path)
        animations.append({
            "asset": expected_path,
            "source_fbx": filename,
            "sequence_length_seconds": float(sequence.get_editor_property("sequence_length")),
        })

    report = {
        "status": "IMPORTED_FULLBODY_WITH_EXISTING_SKELETON",
        "destination": DESTINATION,
        "skeletal_mesh": mesh_path,
        "skeleton": CANONICAL_SKELETON,
        "animations": animations,
        "warning": "Review-only; the candidate runtime-ID to LMT-slot mapping is still unproven.",
    }
    with open(REPORT_PATH, "w", encoding="utf-8") as output:
        json.dump(report, output, ensure_ascii=False, indent=2)
        output.write("\n")
    unreal.log("[MHW FullBody Existing Skeleton Probe] " + json.dumps(report, ensure_ascii=False))
    print("MHW_FULLBODY_EXISTING_SKELETON=" + json.dumps(report, ensure_ascii=False))


main()
