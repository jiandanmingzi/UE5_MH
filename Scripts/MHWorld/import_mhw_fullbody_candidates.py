"""Import the review-only MHW full-body rig and candidate actions into UE.

Run only with Unreal Editor closed, through UnrealEditor-Cmd -run=pythonscript.
The destination is separate from gameplay assets and imports are additive; a
partial or conflicting prior import causes a clear stop rather than replacement.
"""

from __future__ import annotations

import json
import os
import unreal


PROJECT_ROOT = unreal.Paths.project_dir()
SOURCE_DIR = os.path.join(PROJECT_ROOT, "Saved", "MHW_AerialDive_FullBody_Export_20261002")
DESTINATION = "/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/Review/Unknown/MHW_AerialDive_FullBody_Candidates"
REPORT_PATH = os.path.join(SOURCE_DIR, "ue_import_manifest.json")
BLENDER_MANIFEST = os.path.join(SOURCE_DIR, "fullbody_candidate_manifest.json")

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


def load_or_import_mesh():
    mesh_path = DESTINATION + "/SK_MHW_AerialDive_FullBody_ReviewRig"
    mesh_exists = unreal.EditorAssetLibrary.does_asset_exist(mesh_path)
    if mesh_exists:
        mesh = unreal.EditorAssetLibrary.load_asset(mesh_path)
        if mesh is None:
            raise RuntimeError("Existing review mesh cannot be loaded: " + mesh_path)
        skeleton = mesh.get_editor_property("skeleton")
        if skeleton is None:
            raise RuntimeError("Existing review mesh has no Skeleton: " + mesh_path)
        return mesh, skeleton

    imported = import_task(MODEL_FILE, "SK_MHW_AerialDive_FullBody_ReviewRig", skeletal_mesh=True)
    imported_paths = [path for path, _ in imported]
    if mesh_path not in imported_paths:
        raise RuntimeError("Expected the full-body SkeletalMesh, got: {}".format(imported_paths))
    mesh = unreal.EditorAssetLibrary.load_asset(mesh_path)
    if mesh is None:
        raise RuntimeError("Imported full-body review mesh cannot be loaded: " + mesh_path)
    skeleton = mesh.get_editor_property("skeleton")
    if skeleton is None:
        raise RuntimeError("Imported full-body review mesh has no Skeleton: " + mesh_path)
    return mesh, skeleton


def main():
    if not os.path.isfile(BLENDER_MANIFEST):
        raise FileNotFoundError("Missing Blender export report: " + BLENDER_MANIFEST)
    with open(BLENDER_MANIFEST, "r", encoding="utf-8") as source:
        blender_report = json.load(source)
    if blender_report.get("status") != "EXPORTED_FULLBODY_REVIEW_CANDIDATES":
        raise RuntimeError("Blender export report is not a successful full-body candidate export.")
    if float(blender_report["preserved_bind_pose"]["max_world_delta"]) > 1e-4:
        raise RuntimeError("Blender reported a bind-pose geometry change; refusing UE import.")

    mesh, skeleton = load_or_import_mesh()
    actual_skeleton_path = unreal.EditorAssetLibrary.get_path_name_for_loaded_asset(skeleton)
    if actual_skeleton_path != skeleton_path:
        raise RuntimeError("Unexpected Skeleton path: " + actual_skeleton_path)

    imported_animations = []
    for filename in ANIMATION_FILES:
        name = os.path.splitext(filename)[0].replace("MHW_WP10_", "AS_MHW_WP10_")
        asset_path = DESTINATION + "/" + name
        if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
            sequence = unreal.EditorAssetLibrary.load_asset(asset_path)
            if sequence is None:
                raise RuntimeError("Existing animation cannot be loaded: " + asset_path)
        else:
            imported = import_task(filename, name, skeletal_mesh=False, skeleton=skeleton)
            if len(imported) != 1 or imported[0][0] != asset_path:
                raise RuntimeError("Expected one animation at {}, got {}".format(
                    asset_path, [path for path, _ in imported]))
            sequence = imported[0][1]
        sequence_skeleton = sequence.get_editor_property("skeleton")
        if sequence_skeleton != skeleton:
            raise RuntimeError("Animation imported against a different Skeleton: " + asset_path)
        imported_animations.append({
            "asset": asset_path,
            "source_fbx": filename,
            "sequence_length_seconds": float(sequence.get_editor_property("sequence_length")),
            "skeleton": actual_skeleton_path,
        })

    report = {
        "status": "IMPORTED_FULLBODY_REVIEW_CANDIDATES",
        "destination": DESTINATION,
        "review_skeletal_mesh": DESTINATION + "/SK_MHW_AerialDive_FullBody_ReviewRig",
        "review_skeleton": actual_skeleton_path,
        "animations": imported_animations,
        "bind_pose_max_world_delta": blender_report["preserved_bind_pose"]["max_world_delta"],
        "warning": blender_report["mapping_warning"],
    }
    with open(REPORT_PATH, "w", encoding="utf-8") as output:
        json.dump(report, output, ensure_ascii=False, indent=2)
        output.write("\n")
    unreal.log("[MHW FullBody Candidate Import] " + json.dumps(report, ensure_ascii=False))
    print("MHW_FULLBODY_UE_IMPORT=" + json.dumps(report, ensure_ascii=False))


main()
