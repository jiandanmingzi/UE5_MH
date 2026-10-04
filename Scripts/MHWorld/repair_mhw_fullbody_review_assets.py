"""Repair only the isolated MHW full-body review mesh and preview assets.

Creates the missing review PhysicsAsset and explicitly assigns the full-body
SkeletalMesh as the preview mesh for each candidate AnimSequence. It does not
modify gameplay assets or delete the failed first-import package.
"""

import json
import unreal


DESTINATION = "/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/Review/Unknown/MHW_AerialDive_FullBody_AssignedSkeleton"
MESH_NAME = "SK_MHW_AerialDive_FullBody_ReviewRig_Assigned"
ANIMATION_NAMES = (
    "AS_MHW_WP10_Candidate_LMT380_FullBody",
    "AS_MHW_WP10_Candidate_LMT381_FullBody",
    "AS_MHW_WP10_Candidate_LMT360_FullBody",
)


def asset_path(name):
    return DESTINATION + "/" + name


def save(asset):
    if asset is None or not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError("Failed to save review asset: " + str(asset))


def main():
    mesh_path = asset_path(MESH_NAME)
    mesh = unreal.EditorAssetLibrary.load_asset(mesh_path)
    if mesh is None:
        raise FileNotFoundError(mesh_path)
    skeleton = mesh.get_editor_property("skeleton")
    if skeleton is None:
        raise RuntimeError("The assigned-review SkeletalMesh has no Skeleton; refusing to repair it")

    physics_asset = mesh.get_editor_property("physics_asset")
    created_physics_asset = False
    if physics_asset is None:
        # UE 5.6 Python exposes this editor-subsystem wrapper with the mesh
        # argument only; newer API docs also list set_to_mesh/lod_index.
        physics_asset = unreal.SkeletalMeshEditorSubsystem.create_physics_asset(mesh)
        if physics_asset is None:
            raise RuntimeError("UE failed to generate a PhysicsAsset for the review mesh")
        created_physics_asset = True

    save(physics_asset)
    save(mesh)

    animations = []
    for name in ANIMATION_NAMES:
        sequence_path = asset_path(name)
        sequence = unreal.EditorAssetLibrary.load_asset(sequence_path)
        if sequence is None:
            raise FileNotFoundError(sequence_path)
        if sequence.get_editor_property("skeleton") != skeleton:
            raise RuntimeError("Sequence Skeleton mismatch: " + sequence_path)
        sequence.set_preview_skeletal_mesh(mesh)
        save(sequence)
        animations.append({
            "asset": sequence_path,
            "preview_mesh": mesh_path,
            "sequence_length_seconds": float(sequence.get_editor_property("sequence_length")),
        })

    report = {
        "status": "REPAIRED_REVIEW_PHYSICS_AND_PREVIEW_MESH",
        "skeletal_mesh": mesh_path,
        "skeleton": unreal.EditorAssetLibrary.get_path_name_for_loaded_asset(skeleton),
        "physics_asset": unreal.EditorAssetLibrary.get_path_name_for_loaded_asset(physics_asset),
        "physics_asset_created": created_physics_asset,
        "animations": animations,
        "scope": "Review assets only; no gameplay assets changed or deleted.",
    }
    unreal.log("[MHW FullBody Review Repair] " + json.dumps(report, ensure_ascii=False))
    print("MHW_FULLBODY_REVIEW_REPAIR=" + json.dumps(report, ensure_ascii=False))


main()
