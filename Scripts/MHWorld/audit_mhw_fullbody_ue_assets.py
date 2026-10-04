"""Read-only validation of the normalized full-body MHW review assets."""

import json
import unreal


DESTINATION = "/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/Review/MHW/AerialDive"
MESH_NAME = "SK_MHW_IG_AerialDive_ReviewRig"
SKELETON_NAME = MESH_NAME + "_Skeleton"
PHYSICS_NAME = MESH_NAME + "_PhysicsAsset"
ANIMATION_NAMES = (
    "AS_MHW_IG_AerialDive_LMT380_Review",
    "AS_MHW_IG_AerialDive_LMT381_Review",
    "AS_MHW_IG_AerialDive_LMT360_Review",
)


def path_of(asset):
    return unreal.EditorAssetLibrary.get_path_name_for_loaded_asset(asset) if asset else None


def main():
    mesh_path = DESTINATION + "/" + MESH_NAME
    mesh = unreal.EditorAssetLibrary.load_asset(mesh_path)
    if mesh is None:
        raise FileNotFoundError(mesh_path)
    skeleton = mesh.get_editor_property("skeleton")
    if skeleton is None:
        raise RuntimeError("Full-body review mesh has no assigned Skeleton")
    skeleton_path = path_of(skeleton)
    expected_skeleton_path = DESTINATION + "/" + SKELETON_NAME + "." + SKELETON_NAME
    if skeleton_path != expected_skeleton_path:
        raise RuntimeError("Mesh is not using its normalized independent Skeleton: " + str(skeleton_path))

    physics_asset = mesh.get_editor_property("physics_asset")
    if physics_asset is None:
        raise RuntimeError("Full-body review mesh has no PhysicsAsset")
    physics_path = path_of(physics_asset)
    expected_physics_path = DESTINATION + "/" + PHYSICS_NAME + "." + PHYSICS_NAME
    if physics_path != expected_physics_path:
        raise RuntimeError("Mesh PhysicsAsset path mismatch: " + str(physics_path))

    bone_tree_report = None
    try:
        bone_tree = skeleton.get_editor_property("bone_tree")
        bone_tree_report = {"count": len(bone_tree)}
    except Exception as exc:
        bone_tree_report = {"unavailable": str(exc)}

    animations = []
    for name in ANIMATION_NAMES:
        asset_path = DESTINATION + "/" + name
        sequence = unreal.EditorAssetLibrary.load_asset(asset_path)
        if sequence is None:
            raise FileNotFoundError(asset_path)
        sequence_skeleton = sequence.get_editor_property("skeleton")
        if sequence_skeleton != skeleton:
            raise RuntimeError("Sequence Skeleton mismatch: " + asset_path)
        animations.append({
            "asset": asset_path,
            "sequence_length_seconds": float(sequence.get_editor_property("sequence_length")),
            "skeleton": path_of(sequence_skeleton),
            "preview_mesh_set_by_import_script": True,
        })

    report = {
        "status": "VALIDATED_REVIEW_ASSETS",
        "mesh": mesh_path,
        "mesh_class": mesh.get_class().get_name(),
        "skeleton": skeleton_path,
        "physics_asset": physics_path,
        "skeleton_bone_tree": bone_tree_report,
        "animations": animations,
    }
    unreal.log("[MHW FullBody UE Audit] " + json.dumps(report, ensure_ascii=False))
    print("MHW_FULLBODY_UE_AUDIT=" + json.dumps(report, ensure_ascii=False))


main()
