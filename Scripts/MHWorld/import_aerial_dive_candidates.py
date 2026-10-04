"""Import the exported MHW aerial-dive review FBXs as isolated UE assets.

Run with:

    UnrealEditor-Cmd.exe MHGZ.uproject -run=pythonscript \
      -script="Scripts/MHWorld/import_aerial_dive_candidates.py" \
      -unattended -nop4 -nosplash -stdout

This is intentionally additive and refuses to run if any destination asset
already exists. It creates a dedicated MHW source SkeletalMesh/Skeleton and
imports candidate AnimSequences against that Skeleton; it does not retarget or
modify gameplay assets.
"""

from __future__ import annotations

import json
import os
import unreal


PROJECT_ROOT = unreal.Paths.project_dir()
SOURCE_DIR = os.path.join(PROJECT_ROOT, "Saved", "MHW_AerialDive_Export_20261002")
DESTINATION = "/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/Review/Unknown/MHW_AerialDive_Candidates"
REPORT_PATH = os.path.join(SOURCE_DIR, "ue_import_manifest.json")

MODEL_FILE = "SK_MHW_AerialDive_ReviewRig.fbx"
ANIMATION_FILES = (
    "MHW_WP10_Runtime49532_Candidate_LMT380.fbx",
    "MHW_WP10_Runtime49533_Candidate_LMT381.fbx",
    "MHW_WP10_Runtime49533_Candidate_LMT360.fbx",
)


def import_task(filename: str, destination_name: str, *, skeletal_mesh: bool,
                skeleton=None):
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
        import_type = (unreal.FBXImportType.FBXIT_SKELETAL_MESH if skeletal_mesh
                       else unreal.FBXImportType.FBXIT_ANIMATION)
        options.set_editor_property("mesh_type_to_import", import_type)
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
    assets = [(path.split(".")[0], unreal.EditorAssetLibrary.load_asset(path))
              for path in paths]
    if any(asset is None for _, asset in assets):
        raise RuntimeError("Import produced an object that cannot be loaded: {}".format(paths))
    return assets


def main():
    if not os.path.isdir(SOURCE_DIR):
        raise FileNotFoundError(SOURCE_DIR)
    if not os.path.isfile(os.path.join(SOURCE_DIR, "candidate_manifest.json")):
        raise FileNotFoundError("Missing Blender candidate manifest")

    mesh_path = DESTINATION + "/SK_MHW_AerialDive_ReviewRig"
    skeleton_path_expected = DESTINATION + "/SK_MHW_AerialDive_ReviewRig_Skeleton"
    mesh_exists = unreal.EditorAssetLibrary.does_asset_exist(mesh_path)
    skeleton_exists = unreal.EditorAssetLibrary.does_asset_exist(skeleton_path_expected)
    if mesh_exists != skeleton_exists:
        raise RuntimeError("Partial review-rig import detected; inspect destination before continuing.")
    if mesh_exists:
        mesh = unreal.EditorAssetLibrary.load_asset(mesh_path)
        if mesh is None:
            raise RuntimeError("Existing review SkeletalMesh cannot be loaded: " + mesh_path)
    else:
        imported = import_task(
            MODEL_FILE, "SK_MHW_AerialDive_ReviewRig", skeletal_mesh=True)
        imported_paths = [path for path, _ in imported]
        if mesh_path not in imported_paths or skeleton_path_expected not in imported_paths:
            raise RuntimeError("Review-rig import did not produce the expected SkeletalMesh and Skeleton: {}".format(
                imported_paths))
        mesh = unreal.EditorAssetLibrary.load_asset(mesh_path)
        if mesh is None:
            raise RuntimeError("Imported review SkeletalMesh cannot be loaded: " + mesh_path)
    skeleton = mesh.get_editor_property("skeleton")
    if skeleton is None:
        raise RuntimeError("Imported review SkeletalMesh has no Skeleton: " + mesh_path)
    skeleton_path = unreal.EditorAssetLibrary.get_path_name_for_loaded_asset(skeleton)

    imported_animations = []
    for filename in ANIMATION_FILES:
        name = os.path.splitext(filename)[0].replace("MHW_WP10_", "AS_MHW_WP10_")
        asset_path = DESTINATION + "/" + name
        if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
            sequence = unreal.EditorAssetLibrary.load_asset(asset_path)
            if sequence is None:
                raise RuntimeError("Existing animation cannot be loaded: " + asset_path)
        else:
            imported = import_task(
                filename, name, skeletal_mesh=False, skeleton=skeleton)
            if len(imported) != 1 or imported[0][0] != asset_path:
                raise RuntimeError("Expected one animation asset at {}, got {}".format(
                    asset_path, [path for path, _ in imported]))
            sequence = imported[0][1]
        sequence_skeleton = sequence.get_editor_property("skeleton")
        if sequence_skeleton != skeleton:
            raise RuntimeError("Animation imported against a different Skeleton: " + asset_path)
        sequence_length = float(sequence.get_editor_property("sequence_length"))
        imported_animations.append({
            "asset": asset_path,
            "source_fbx": filename,
            "sequence_length_seconds": sequence_length,
            "skeleton": skeleton_path,
        })

    report = {
        "status": "IMPORTED_CANDIDATES",
        "destination": DESTINATION,
        "review_skeletal_mesh": mesh_path,
        "review_skeleton": skeleton_path,
        "animations": imported_animations,
        "warning": (
            "These are visual-review candidates. Runtime IDs 49532/49533 have not been "
            "proven to map to the selected w10_00 LMT slots."
        ),
    }
    with open(REPORT_PATH, "w", encoding="utf-8") as output:
        json.dump(report, output, ensure_ascii=False, indent=2)
        output.write("\n")
    unreal.log("[MHW Aerial Dive Import] " + json.dumps(report, ensure_ascii=False))
    print("MHW_AERIAL_DIVE_UE_IMPORT=" + json.dumps(report, ensure_ascii=False))


main()
