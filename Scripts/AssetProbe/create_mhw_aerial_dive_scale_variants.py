"""RETIRED: source-scale variants are no longer part of this review.

The existing retargeted sequences contain the transferred body animation but
the IK Retargeter defaulted the left weapon bone to unit scale. This script
keeps that result as the unit-scale candidate and makes a second duplicate
whose L_Weapon_00 scale keys are copied from source bone_030. Translation and
rotation remain those of the retargeted target-skeleton animation.

Historical implementation below is disabled. Do not recreate these variants.
"""

import json
import unreal


ROOT = "/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/Review/MHW/AerialDive"
RETARGETED = ROOT + "/Retargeted"
TARGET_MESH_PATH = "/Game/Characters/Demo/Meshes/Body/SKM_Demo_Body"
RATE = 60.0
ENTRIES = (
    ("380", "AS_MHW_IG_AerialDive_LMT380_MHR_PreserveWeaponScale_Review"),
    ("381", "AS_MHW_IG_AerialDive_LMT381_MHR_PreserveWeaponScale_Review"),
)


def load_required(path):
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if asset is None:
        raise FileNotFoundError(path)
    return asset


def evaluate_options():
    options = unreal.AnimPoseEvaluationOptions()
    options.set_editor_property("evaluation_type", unreal.AnimDataEvalType.RAW)
    options.set_editor_property("should_retarget", False)
    options.set_editor_property("extract_root_motion", False)
    options.set_editor_property("incorporate_root_motion_into_pose", False)
    return options


def as_vector(value):
    return unreal.Vector(float(value.x), float(value.y), float(value.z))


def as_quat(value):
    return unreal.Quat(float(value.x), float(value.y), float(value.z), float(value.w))


def main():
    target_mesh = load_required(TARGET_MESH_PATH)
    target_skeleton = target_mesh.get_editor_property("skeleton")
    options = evaluate_options()
    report = []

    for index, preserve_name in ENTRIES:
        legacy_path = RETARGETED + "/" + preserve_name
        unit_name = preserve_name.replace("PreserveWeaponScale", "UnitWeaponScale")
        unit_path = RETARGETED + "/" + unit_name
        preserve_path = legacy_path

        # The first batch-retarget run produced unit-scale tracks under the
        # legacy preserve-scale name. Rename those instead of regenerating the
        # expensive retarget operation.
        if unreal.EditorAssetLibrary.does_asset_exist(legacy_path) and not unreal.EditorAssetLibrary.does_asset_exist(unit_path):
            if not unreal.EditorAssetLibrary.rename_asset(legacy_path, unit_path):
                raise RuntimeError("Could not relabel unit-scale candidate: " + legacy_path)
        if not unreal.EditorAssetLibrary.does_asset_exist(unit_path):
            raise FileNotFoundError("Missing unit-scale retarget result " + unit_path)
        if unreal.EditorAssetLibrary.does_asset_exist(preserve_path):
            raise RuntimeError("Refusing to overwrite existing source-scale candidate " + preserve_path)

        unit = load_required(unit_path)
        if unit.get_editor_property("skeleton") != target_skeleton:
            raise RuntimeError("Unit candidate is not bound to target Skeleton: " + unit_path)
        if not unreal.EditorAssetLibrary.duplicate_asset(unit_path, preserve_path):
            raise RuntimeError("Could not duplicate candidate to " + preserve_path)
        preserve = load_required(preserve_path)
        preserve.set_preview_skeletal_mesh(target_mesh)

        source = load_required(ROOT + "/AS_MHW_IG_AerialDive_LMT" + index + "_Review")
        length = float(unit.get_play_length())
        source_length = float(source.get_play_length())
        if abs(length - source_length) > (1.0 / RATE):
            raise RuntimeError("Retarget/source lengths differ: {} vs {}".format(length, source_length))
        key_count = int(round(length * RATE)) + 1
        positions, rotations, scales = [], [], []

        for frame in range(key_count):
            time = min(frame / RATE, length)
            source_pose = source.get_anim_pose_at_time(time, options)
            target_pose = unit.get_anim_pose_at_time(time, options)
            source_scale = source_pose.get_bone_pose(
                unreal.Name("bone_030"), unreal.AnimPoseSpaces.LOCAL).scale3d
            target_weapon = target_pose.get_bone_pose(
                unreal.Name("L_Weapon_00"), unreal.AnimPoseSpaces.LOCAL)
            positions.append(as_vector(target_weapon.translation))
            rotations.append(as_quat(target_weapon.rotation))
            # MHW's relevant animated scale is the local Y component of
            # bone_030. Copy that component without altering the retargeted
            # target bone's position, rotation, or other scale axes.
            scales.append(unreal.Vector(float(target_weapon.scale3d.x),
                                        float(source_scale.y),
                                        float(target_weapon.scale3d.z)))

        controller = preserve.get_editor_property("controller")
        if not controller.set_bone_track_keys(unreal.Name("L_Weapon_00"),
                                              positions, rotations, scales, False):
            raise RuntimeError("Could not write source scale keys for " + preserve_path)
        if not unreal.EditorAssetLibrary.save_loaded_asset(preserve, only_if_is_dirty=False):
            raise RuntimeError("Could not save source-scale candidate " + preserve_path)

        min_y = min(key.y for key in scales)
        max_y = max(key.y for key in scales)
        report.append({
            "unit_scale_candidate": unit.get_path_name(),
            "source_scale_candidate": preserve.get_path_name(),
            "target_skeleton": target_skeleton.get_path_name(),
            "preview_mesh": target_mesh.get_path_name(),
            "source_duration": source_length,
            "target_duration": float(preserve.get_play_length()),
            "rate_scale": float(preserve.get_editor_property("rate_scale")),
            "sample_rate_hz": RATE,
            "key_count": key_count,
            "bone": "L_Weapon_00 <- source bone_030",
            "copied_scale_axis": "local Y",
            "copied_scale_range": [min_y, max_y],
            "translation_rotation_source": "retargeted target-skeleton candidate",
        })

    result = {"status": "MHW_AERIAL_DIVE_SCALE_VARIANTS_CREATED",
              "source_review_assets_preserved": True,
              "gameplay_assets_modified": False,
              "animations": report}
    unreal.log("[MHW AerialDive Scale Variants] " + json.dumps(result, ensure_ascii=False))
    print("MHW_AERIAL_DIVE_SCALE_VARIANTS=" + json.dumps(result, ensure_ascii=False))


raise RuntimeError(
    "Source-scale variants were retired by user choice. Run "
    "repair_mhw_aerial_dive_root_motion.py for the UnitWeaponScale animations."
)
