"""Rebuild the isolated MHW Aerial Dive review import with canonical names.

Run with UnrealEditor-Cmd while the editor is closed. The script imports and
validates a new, independent review set before deleting only the exact legacy
MHW review packages listed below. Source FBX/Blend files and gameplay assets
are intentionally outside the deletion scope.
"""

from __future__ import annotations

import json
import os
import unreal


PROJECT_ROOT = unreal.Paths.project_dir()
SOURCE_DIR = os.path.join(PROJECT_ROOT, "Saved", "MHW_AerialDive_FullBody_Export_20261002")
DESTINATION = "/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/Review/MHW/AerialDive"
REPORT_PATH = os.path.join(SOURCE_DIR, "ue_normalized_import_manifest.json")
BLENDER_MANIFEST = os.path.join(SOURCE_DIR, "fullbody_candidate_manifest.json")

MESH_FBX = "SK_MHW_AerialDive_FullBody_ReviewRig.fbx"
MESH_NAME = "SK_MHW_IG_AerialDive_ReviewRig"
SKELETON_NAME = MESH_NAME + "_Skeleton"
PHYSICS_NAME = MESH_NAME + "_PhysicsAsset"
ANIMATIONS = (
    ("MHW_WP10_Candidate_LMT380_FullBody.fbx", "AS_MHW_IG_AerialDive_LMT380_Review"),
    ("MHW_WP10_Candidate_LMT381_FullBody.fbx", "AS_MHW_IG_AerialDive_LMT381_Review"),
    ("MHW_WP10_Candidate_LMT360_FullBody.fbx", "AS_MHW_IG_AerialDive_LMT360_Review"),
)

# Exact packages produced by the earlier MHW review-import attempts.
# Do not expand this list to directories or wildcard matches.
LEGACY_PACKAGES = (
    "/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/Review/Unknown/MHW_AerialDive_Candidates/AS_MHW_WP10_Runtime49532_Candidate_LMT380",
    "/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/Review/Unknown/MHW_AerialDive_Candidates/AS_MHW_WP10_Runtime49533_Candidate_LMT360",
    "/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/Review/Unknown/MHW_AerialDive_Candidates/AS_MHW_WP10_Runtime49533_Candidate_LMT381",
    "/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/Review/Unknown/MHW_AerialDive_Candidates/SK_MHW_AerialDive_ReviewRig",
    "/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/Review/Unknown/MHW_AerialDive_Candidates/SK_MHW_AerialDive_ReviewRig_Skeleton",
    "/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/Review/Unknown/MHW_AerialDive_FullBody_Candidates/SK_MHW_AerialDive_FullBody_ReviewRig",
    "/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/Review/Unknown/MHW_AerialDive_FullBody_AssignedSkeleton/AS_MHW_WP10_Candidate_LMT360_FullBody",
    "/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/Review/Unknown/MHW_AerialDive_FullBody_AssignedSkeleton/AS_MHW_WP10_Candidate_LMT380_FullBody",
    "/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/Review/Unknown/MHW_AerialDive_FullBody_AssignedSkeleton/AS_MHW_WP10_Candidate_LMT381_FullBody",
    "/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/Review/Unknown/MHW_AerialDive_FullBody_AssignedSkeleton/SK_MHW_AerialDive_FullBody_ReviewRig_Assigned",
    "/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/Review/Unknown/MHW_AerialDive_FullBody_AssignedSkeleton/SK_MHW_AerialDive_FullBody_ReviewRig_Assigned_PhysicsAsset",
)


def package_path(asset_path: str) -> str:
    return asset_path.split(".", 1)[0]


def save(asset) -> None:
    if asset is None or not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError("Failed to save asset: " + str(asset))


def import_one(filename: str, asset_name: str, *, as_mesh: bool, skeleton=None):
    source = os.path.join(SOURCE_DIR, filename)
    if not os.path.isfile(source):
        raise FileNotFoundError(source)

    options = unreal.FbxImportUI()
    options.set_editor_property("import_mesh", as_mesh)
    options.set_editor_property("import_as_skeletal", True)
    options.set_editor_property("import_animations", not as_mesh)
    options.set_editor_property("import_materials", False)
    options.set_editor_property("import_textures", False)
    options.set_editor_property("automated_import_should_detect_type", False)
    options.set_editor_property(
        "mesh_type_to_import",
        unreal.FBXImportType.FBXIT_SKELETAL_MESH if as_mesh else unreal.FBXImportType.FBXIT_ANIMATION,
    )
    if as_mesh:
        # UE creates a new Skeleton when importing a skeletal mesh with None.
        options.set_editor_property("skeleton", None)
        options.set_editor_property("create_physics_asset", True)
    else:
        if skeleton is None:
            raise RuntimeError("Animation import requires the newly imported review Skeleton")
        options.set_editor_property("skeleton", skeleton)

    task = unreal.AssetImportTask()
    task.set_editor_property("filename", source)
    task.set_editor_property("destination_path", DESTINATION)
    task.set_editor_property("destination_name", asset_name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", False)
    task.set_editor_property("save", True)
    task.set_editor_property("options", options)
    task.set_editor_property("factory", unreal.FbxFactory())
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    paths = list(task.get_editor_property("imported_object_paths"))
    expected_path = DESTINATION + "/" + asset_name
    if expected_path not in [package_path(path) for path in paths]:
        raise RuntimeError("Expected {}, importer returned {}".format(expected_path, paths))
    asset = unreal.EditorAssetLibrary.load_asset(expected_path)
    if asset is None:
        raise RuntimeError("Imported asset could not be loaded: " + expected_path)
    return asset


def validate_legacy_references(new_packages: set[str]) -> list[str]:
    old_packages = {package_path(path) for path in LEGACY_PACKAGES}
    failures = []
    for old_path in LEGACY_PACKAGES:
        if not unreal.EditorAssetLibrary.does_asset_exist(old_path):
            continue
        object_path = old_path + "." + old_path.rsplit("/", 1)[-1]
        refs = unreal.EditorAssetLibrary.find_package_referencers_for_asset(object_path, True)
        unexpected = []
        for ref in refs:
            ref_path = package_path(str(ref))
            if ref_path not in old_packages and ref_path not in new_packages:
                unexpected.append(str(ref))
        if unexpected:
            failures.append("{} is referenced by: {}".format(old_path, unexpected))
    return failures


def delete_legacy_packages() -> list[str]:
    # Delete dependent assets first; delete the old Skeleton last.
    order = (
        LEGACY_PACKAGES[0:3]
        + LEGACY_PACKAGES[5:6]
        + LEGACY_PACKAGES[6:9]
        + LEGACY_PACKAGES[9:11]
        + LEGACY_PACKAGES[3:4]
        + LEGACY_PACKAGES[4:5]
    )
    deleted = []
    for path in order:
        if unreal.EditorAssetLibrary.does_asset_exist(path):
            if not unreal.EditorAssetLibrary.delete_asset(path):
                raise RuntimeError("Refusing to report success; failed deleting legacy package: " + path)
            deleted.append(path)
    return deleted


def main() -> None:
    if not os.path.isfile(BLENDER_MANIFEST):
        raise FileNotFoundError("Missing full-body export manifest: " + BLENDER_MANIFEST)
    with open(BLENDER_MANIFEST, "r", encoding="utf-8") as source:
        blender_report = json.load(source)
    if blender_report.get("status") != "EXPORTED_FULLBODY_REVIEW_CANDIDATES":
        raise RuntimeError("Full-body FBX export manifest is not successful")
    if float(blender_report["preserved_bind_pose"]["max_world_delta"]) > 1e-4:
        raise RuntimeError("Bind pose validation failed; refusing import")

    if unreal.EditorAssetLibrary.does_asset_exist(DESTINATION + "/" + MESH_NAME):
        raise RuntimeError("Normalized destination already contains the mesh; refusing overwrite")
    for _, name in ANIMATIONS:
        if unreal.EditorAssetLibrary.does_asset_exist(DESTINATION + "/" + name):
            raise RuntimeError("Normalized destination already contains {}; refusing overwrite".format(name))

    mesh = import_one(MESH_FBX, MESH_NAME, as_mesh=True)
    skeleton = mesh.get_editor_property("skeleton")
    if skeleton is None:
        raise RuntimeError("Freshly imported full-body mesh has no Skeleton; legacy assets were not deleted")
    skeleton_path = unreal.EditorAssetLibrary.get_path_name_for_loaded_asset(skeleton)
    expected_skeleton_path = DESTINATION + "/" + SKELETON_NAME + "." + SKELETON_NAME
    if skeleton_path != expected_skeleton_path:
        raise RuntimeError("Unexpected newly created Skeleton path: " + skeleton_path)
    save(skeleton)

    physics_asset = mesh.get_editor_property("physics_asset")
    if physics_asset is None:
        physics_asset = unreal.SkeletalMeshEditorSubsystem.create_physics_asset(mesh, True, 0)
    if physics_asset is None:
        raise RuntimeError("Could not create a PhysicsAsset; legacy assets were not deleted")
    save(physics_asset)
    save(mesh)
    physics_path = unreal.EditorAssetLibrary.get_path_name_for_loaded_asset(physics_asset)

    animation_report = []
    for filename, name in ANIMATIONS:
        sequence = import_one(filename, name, as_mesh=False, skeleton=skeleton)
        if sequence.get_editor_property("skeleton") != skeleton:
            raise RuntimeError("Imported animation does not use new Skeleton: " + name)
        sequence.set_preview_skeletal_mesh(mesh)
        save(sequence)
        animation_report.append({
            "asset": DESTINATION + "/" + name,
            "source_fbx": filename,
            "length_seconds": float(sequence.get_editor_property("sequence_length")),
            "skeleton": skeleton_path,
            "preview_mesh": DESTINATION + "/" + MESH_NAME,
        })

    if mesh.get_editor_property("skeleton") != skeleton:
        raise RuntimeError("Mesh Skeleton assignment changed during import")
    if mesh.get_editor_property("physics_asset") != physics_asset:
        raise RuntimeError("Mesh PhysicsAsset assignment failed")

    new_packages = {package_path(DESTINATION + "/" + name) for name in
                    (MESH_NAME, SKELETON_NAME, PHYSICS_NAME, *(anim_name for _, anim_name in ANIMATIONS))}
    reference_failures = validate_legacy_references(new_packages)
    if reference_failures:
        raise RuntimeError("Legacy assets have external referencers; nothing was deleted: " + " | ".join(reference_failures))

    deleted = delete_legacy_packages()
    report = {
        "status": "NORMALIZED_REVIEW_IMPORT_COMPLETE",
        "destination": DESTINATION,
        "skeletal_mesh": DESTINATION + "/" + MESH_NAME,
        "skeleton": skeleton_path,
        "physics_asset": physics_path,
        "animations": animation_report,
        "legacy_packages_deleted": deleted,
        "source_files_preserved": True,
        "gameplay_assets_modified": False,
    }
    with open(REPORT_PATH, "w", encoding="utf-8") as output:
        json.dump(report, output, ensure_ascii=False, indent=2)
        output.write("\n")
    unreal.log("[MHW Normalized AerialDive Review Import] " + json.dumps(report, ensure_ascii=False))
    print("MHW_NORMALIZED_REVIEW_IMPORT=" + json.dumps(report, ensure_ascii=False))


main()
