"""Repair body and helper tracks of the MHW->MHR review-only FK bake.

The source has a pelvis-centred reference pose and an extra FBX Armature root.
UE's default height-normalised Pelvis Motion is not valid for this hierarchy.
Transfer bone_root component displacement to Root, and bone_000/bone_013 local
animation to Cog/Waist_00. Keep the existing FK limb bake and unit weapon scale.
No gameplay, source animation, mesh, or Skeleton asset is modified.
"""

import hashlib
import json
import math
import shutil
from datetime import datetime
from pathlib import Path

import unreal


ROOT = "/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/Review/MHW/AerialDive"
TARGET_MESH = "/Game/Characters/Demo/Meshes/Body/SKM_Demo_Body"
TARGET_SKELETON = "/Game/Characters/Demo/Meshes/Body/SK_Demo_Body"
RATE = 60.0
IDS = (380, 381)
RETARGETED_BASELINE = ROOT + "/Retargeted"
OUTPUT_SUBDIR = globals().get("OUTPUT_SUBDIR", "Retargeted_FingerFixed")
OUTPUT_SUFFIX = globals().get("OUTPUT_SUFFIX", "_MHR_FingerFixed_Review")
CHECK_BONES = (("bone_000", "Cog"), ("bone_013", "Waist_00"),
               ("bone_001", "Spine_00"), ("bone_004", "Head_00"),
               ("bone_017", "L_Leg_03"), ("bone_021", "R_Leg_03"),
               ("bone_249", "R_Foot_IK"), ("bone_250", "L_Foot_IK"),
               ("bone_252", "LookAt"), ("bone_253", "PL_Cam"),
               ("bone_253", "Parts"))
ATTACHED_WEAPON_BONE_MAP = (("bone_030", "L_Weapon_00"),)


def load(path):
    asset = unreal.load_asset(path)
    if asset is None:
        raise RuntimeError("Missing asset: " + path)
    return asset


def unit_path(index):
    return ROOT + f"/{OUTPUT_SUBDIR}/AS_MHW_IG_AerialDive_LMT{index}{OUTPUT_SUFFIX}"


def baseline_path(index):
    return RETARGETED_BASELINE + f"/AS_MHW_IG_AerialDive_LMT{index}_MHR_UnitWeaponScale_Review"


def preserve_path(index):
    if "UnitWeaponScale" not in OUTPUT_SUFFIX:
        return None
    return unit_path(index).replace("UnitWeaponScale", "PreserveWeaponScale")


def filename(path):
    if not path.startswith("/Game/"):
        raise ValueError("Not a project content package: " + path)
    return Path(unreal.Paths.project_content_dir()) / (path[6:] + ".uasset")


def digest(path):
    return hashlib.sha256(filename(path).read_bytes()).hexdigest()


def options(kind=unreal.AnimDataEvalType.RAW):
    result = unreal.AnimPoseEvaluationOptions()
    result.set_editor_property("evaluation_type", kind)
    result.set_editor_property("should_retarget", False)
    result.set_editor_property("extract_root_motion", False)
    result.set_editor_property("incorporate_root_motion_into_pose", False)
    return result


def vec(v):
    return unreal.Vector(float(v.x), float(v.y), float(v.z))


def quat(q):
    return unreal.Quat(float(q.x), float(q.y), float(q.z), float(q.w))


def components(v):
    return [float(v.x), float(v.y), float(v.z)]


def distance(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(components(a), components(b))))


def validate(source, target, root_ref, kind=unreal.AnimDataEvalType.RAW,
             weapon_baseline=None):
    """Verify motion preservation, anatomy relative to Root, and unit scale."""
    length = float(source.get_play_length())
    count = int(round(length * RATE)) + 1
    source_options, target_options = options(), options(kind)
    first_root = source.get_anim_pose_at_time(0.0, source_options).get_bone_pose(
        "bone_root", unreal.AnimPoseSpaces.WORLD)
    errors = {target_name: 0.0 for _, target_name in CHECK_BONES}
    weapon_errors = {target_name: {"source_bone": source_name,
                                   "translation_cm_max": 0.0,
                                   "rotation_degrees_max": 0.0}
                     for source_name, target_name in ATTACHED_WEAPON_BONE_MAP}
    if weapon_baseline:
        weapon_errors["R_Weapon_00"] = {"source_asset": weapon_baseline.get_path_name(),
                                        "translation_cm_max": 0.0,
                                        "rotation_degrees_max": 0.0}
    root_error = 0.0
    max_pelvis_from_root = 0.0
    first_target_root = last_target_root = None
    max_frame_step = {"Root": 0.0, "Waist_00_relative_to_Root": 0.0}
    previous_root = previous_pelvis = None
    for frame in range(count):
        time = min(frame / RATE, length)
        source_pose = source.get_anim_pose_at_time(time, source_options)
        target_pose = target.get_anim_pose_at_time(time, target_options)
        source_root = source_pose.get_bone_pose("bone_root", unreal.AnimPoseSpaces.WORLD)
        target_root = target_pose.get_bone_pose("Root", unreal.AnimPoseSpaces.WORLD)
        expected = root_ref.translation + (source_root.translation - first_root.translation)
        root_error = max(root_error, distance(expected, target_root.translation))
        if frame == 0:
            first_target_root = target_root.translation
        last_target_root = target_root.translation
        for source_bone, target_bone in CHECK_BONES:
            src = source_pose.get_bone_pose(source_bone, unreal.AnimPoseSpaces.WORLD)
            dst = target_pose.get_bone_pose(target_bone, unreal.AnimPoseSpaces.WORLD)
            # Comparing in each animated Root's coordinates also checks the
            # parent rotation, not just a similar-looking component position.
            src_relative = source_root.inverse_transform_location(src.translation)
            dst_relative = target_root.inverse_transform_location(dst.translation)
            errors[target_bone] = max(errors[target_bone], distance(src_relative, dst_relative))
            if target_bone == "Waist_00":
                max_pelvis_from_root = max(max_pelvis_from_root, distance(dst.translation, target_root.translation))
                if previous_pelvis is not None:
                    max_frame_step["Waist_00_relative_to_Root"] = max(
                        max_frame_step["Waist_00_relative_to_Root"], distance(dst_relative, previous_pelvis))
                previous_pelvis = dst_relative
        for source_bone, target_bone in ATTACHED_WEAPON_BONE_MAP:
            src = source_pose.get_bone_pose(source_bone, unreal.AnimPoseSpaces.LOCAL)
            dst = target_pose.get_bone_pose(target_bone, unreal.AnimPoseSpaces.LOCAL)
            weapon_errors[target_bone]["translation_cm_max"] = max(
                weapon_errors[target_bone]["translation_cm_max"],
                distance(src.translation, dst.translation))
            weapon_errors[target_bone]["rotation_degrees_max"] = max(
                weapon_errors[target_bone]["rotation_degrees_max"],
                math.degrees(src.rotation.angular_distance(dst.rotation)))
        if weapon_baseline:
            baseline_pose = weapon_baseline.get_anim_pose_at_time(time, source_options)
            baseline_r = baseline_pose.get_bone_pose("R_Weapon_00", unreal.AnimPoseSpaces.LOCAL)
            target_r = target_pose.get_bone_pose("R_Weapon_00", unreal.AnimPoseSpaces.LOCAL)
            weapon_errors["R_Weapon_00"]["translation_cm_max"] = max(
                weapon_errors["R_Weapon_00"]["translation_cm_max"],
                distance(baseline_r.translation, target_r.translation))
            weapon_errors["R_Weapon_00"]["rotation_degrees_max"] = max(
                weapon_errors["R_Weapon_00"]["rotation_degrees_max"],
                math.degrees(baseline_r.rotation.angular_distance(target_r.rotation)))
        if previous_root is not None:
            max_frame_step["Root"] = max(max_frame_step["Root"], distance(target_root.translation, previous_root))
        previous_root = target_root.translation
        for name in ("Root", "Cog", "Waist_00", "R_Foot_IK", "L_Foot_IK",
                     "LookAt", "PL_Cam", "Parts", "L_Weapon_00", "R_Weapon_00"):
            bone = target_pose.get_bone_pose(name, unreal.AnimPoseSpaces.LOCAL)
            values = components(bone.translation) + components(bone.scale3d)
            values += [float(bone.rotation.x), float(bone.rotation.y), float(bone.rotation.z), float(bone.rotation.w)]
            if not all(math.isfinite(value) for value in values):
                raise RuntimeError(f"Non-finite {name} at frame {frame}")
            if max(abs(value - 1.0) for value in components(bone.scale3d)) > 1e-5:
                raise RuntimeError(f"Non-unit {name} scale at frame {frame}")
    if root_error > 0.05 or max(errors.values()) > 1.0:
        raise RuntimeError(f"Root/anatomy verification failed: root={root_error}, body={errors}")
    for target_bone, row in weapon_errors.items():
        if row["translation_cm_max"] > 0.01 or row["rotation_degrees_max"] > 0.05:
            raise RuntimeError("Weapon local-track transfer verification failed: " +
                               json.dumps({target_bone: row}))
    return {"evaluation": str(kind), "sample_count": count,
            "root_position_error_max_cm": root_error,
            "root_relative_bone_error_max_cm": errors,
            "weapon_local_track_transfer_max_error": weapon_errors,
            "pelvis_distance_from_root_max_cm": max_pelvis_from_root,
            "root_start_cm": components(first_target_root),
            "root_end_cm": components(last_target_root),
            "root_displacement_cm": components(last_target_root - first_target_root),
            "max_frame_step_cm": max_frame_step}


def main():
    target_mesh = load(TARGET_MESH)
    target_skeleton = load(TARGET_SKELETON)
    reference = unreal.AnimPoseExtensions.get_reference_pose(target_skeleton)
    root_ref = reference.get_ref_bone_pose("Root", unreal.AnimPoseSpaces.LOCAL)
    targets = [load(unit_path(index)) for index in IDS]
    sources = [load(ROOT + f"/AS_MHW_IG_AerialDive_LMT{index}_Review") for index in IDS]
    for source, target in zip(sources, targets):
        if target.get_editor_property("skeleton") != target_skeleton:
            raise RuntimeError("Wrong target Skeleton: " + target.get_path_name())
        if abs(float(source.get_play_length()) - float(target.get_play_length())) > 1e-5:
            raise RuntimeError("Source/target duration differs")

    # These paired weapon bones share the same local reference basis, so their
    # source local transforms can be transferred exactly. The default IK
    # Retarget bake drops these short one-bone chains' translations; do not
    # apply this recipe if a future source rig changes its weapon-bone basis.
    source_reference = unreal.AnimPoseExtensions.get_reference_pose(
        sources[0].get_editor_property("skeleton"))
    weapon_reference_errors = {}
    for source_bone, target_bone in ATTACHED_WEAPON_BONE_MAP:
        source_ref = source_reference.get_ref_bone_pose(source_bone, unreal.AnimPoseSpaces.LOCAL)
        target_ref = reference.get_ref_bone_pose(target_bone, unreal.AnimPoseSpaces.LOCAL)
        position_error = distance(source_ref.translation, target_ref.translation)
        rotation_error = math.degrees(source_ref.rotation.angular_distance(target_ref.rotation))
        weapon_reference_errors[target_bone] = {
            "source_bone": source_bone,
            "reference_local_translation_error_cm": position_error,
            "reference_local_rotation_error_degrees": rotation_error,
        }
        if position_error > 0.05 or rotation_error > 0.1:
            raise RuntimeError("Weapon reference bases differ; refusing direct local transfer: " +
                               json.dumps({target_bone: weapon_reference_errors[target_bone]}))

    # Resolve deletion targets and external references before any content edit.
    deletions = [path for index in IDS for path in [preserve_path(index)]
                 if path and unreal.EditorAssetLibrary.does_asset_exist(path)]
    deletion_references = {}
    for path in deletions:
        refs = unreal.EditorAssetLibrary.find_package_referencers_for_asset(path, True)
        deletion_references[path] = [str(ref) for ref in refs]
        if refs:
            raise RuntimeError(f"Cannot delete referenced comparison asset {path}: {deletion_references[path]}")

    protected = [ROOT + f"/AS_MHW_IG_AerialDive_LMT{index}_Review" for index in (380, 381, 360)]
    protected += [TARGET_MESH, TARGET_SKELETON]
    hashes_before = {path: digest(path) for path in protected}
    backup = Path(unreal.Paths.project_saved_dir()) / "Backups" / (
        "MHW_AerialDive_FingerFixed_RootRepair_" + datetime.now().strftime("%Y%m%d_%H%M%S_%f"))
    backup.mkdir(parents=True, exist_ok=False)
    for path in [unit_path(index) for index in IDS] + deletions:
        shutil.copy2(filename(path), backup / filename(path).name)
    old_audit = Path(unreal.Paths.project_saved_dir()) / "AssetProbe/mhw_aerial_dive_root_motion_audit.json"
    if old_audit.exists():
        shutil.copy2(old_audit, backup / "root_motion_before.json")
    (backup / "manifest.json").write_text(json.dumps({
        "modified_packages": [unit_path(index) for index in IDS],
        "removed_packages": deletions, "deletion_references": deletion_references,
        "protected_sha256": hashes_before,
    }, indent=2), encoding="utf-8")

    results = []
    baselines = [load(baseline_path(index)) for index in IDS]
    for index, source, target, baseline in zip(IDS, sources, targets, baselines):
        if baseline.get_editor_property("skeleton") != target_skeleton:
            raise RuntimeError("Weapon baseline has the wrong Skeleton: " + baseline.get_path_name())
        if abs(float(baseline.get_play_length()) - float(source.get_play_length())) > 1e-5:
            raise RuntimeError("Weapon baseline/source duration differs")
        length = float(source.get_play_length())
        count = int(round(length * RATE)) + 1
        tracks = {name: ([], [], []) for name in
                  ("Root", "Cog", "Waist_00", "R_Foot_IK", "L_Foot_IK",
                   "LookAt", "PL_Cam", "Parts", "L_Weapon_00", "R_Weapon_00")}
        first_source_root = source.get_anim_pose_at_time(0.0, options()).get_bone_pose(
            "bone_root", unreal.AnimPoseSpaces.WORLD)
        for frame in range(count):
            time = min(frame / RATE, length)
            source_pose = source.get_anim_pose_at_time(time, options())
            target_pose = target.get_anim_pose_at_time(time, options())
            baseline_pose = baseline.get_anim_pose_at_time(time, options())
            source_root = source_pose.get_bone_pose("bone_root", unreal.AnimPoseSpaces.WORLD)
            values = {
                "Cog": source_pose.get_bone_pose("bone_000", unreal.AnimPoseSpaces.LOCAL),
                "Waist_00": source_pose.get_bone_pose("bone_013", unreal.AnimPoseSpaces.LOCAL),
                # These MHW bones are direct children of bone_root, matching
                # the target helper bones' direct-child relationship to Root.
                # The source/target Root bases were checked above.
                "R_Foot_IK": source_pose.get_bone_pose("bone_249", unreal.AnimPoseSpaces.LOCAL),
                "L_Foot_IK": source_pose.get_bone_pose("bone_250", unreal.AnimPoseSpaces.LOCAL),
                "LookAt": source_pose.get_bone_pose("bone_252", unreal.AnimPoseSpaces.LOCAL),
                "PL_Cam": source_pose.get_bone_pose("bone_253", unreal.AnimPoseSpaces.LOCAL),
                "Parts": source_pose.get_bone_pose("bone_253", unreal.AnimPoseSpaces.LOCAL),
                # Weapon_L is the actual post-sheathe attachment socket. Its
                # source/target reference local bases match, and the source
                # end pose agrees with native MHR unsheathed idle. Retargeter
                # drops its animated translation, so transfer that track.
                "L_Weapon_00": source_pose.get_bone_pose("bone_030", unreal.AnimPoseSpaces.LOCAL),
                # bone_047 has a large action-specific local offset unlike
                # native MHR idle. It is not the configured Weapon_L attach
                # point; preserve the original retargeter result for review.
                "R_Weapon_00": baseline_pose.get_bone_pose("R_Weapon_00", unreal.AnimPoseSpaces.LOCAL),
            }
            root_position = root_ref.translation + (source_root.translation - first_source_root.translation)
            # These two review clips use the same constant root orientation as
            # the target. Refuse to silently reuse this recipe on a new basis.
            if source_root.rotation.angular_distance(root_ref.rotation) > 0.0001:
                raise RuntimeError("Source/target Root basis differs; a new retarget recipe is required")
            tracks["Root"][0].append(vec(root_position))
            tracks["Root"][1].append(quat(root_ref.rotation))
            tracks["Root"][2].append(unreal.Vector(1, 1, 1))
            for name, value in values.items():
                tracks[name][0].append(vec(value.translation))
                tracks[name][1].append(quat(value.rotation))
                tracks[name][2].append(unreal.Vector(1, 1, 1))
        controller = target.get_editor_property("controller")
        controller.open_bracket(unreal.Text("Repair MHW review Root/Cog/Waist"), False)
        try:
            for name, (positions, rotations, scales) in tracks.items():
                if not controller.set_bone_track_keys(unreal.Name(name), positions, rotations, scales, False):
                    raise RuntimeError("Failed to write " + name)
        finally:
            controller.close_bracket(False)
        target.set_preview_skeletal_mesh(target_mesh)
        target.set_editor_property("rate_scale", 1.0)
        checks = validate(source, target, root_ref, weapon_baseline=baseline)
        if not unreal.EditorAssetLibrary.save_loaded_asset(target, only_if_is_dirty=False):
            raise RuntimeError("Failed to save " + target.get_path_name())
        results.append({"asset": unit_path(index), "duration_seconds": length,
                        "rate_scale": 1.0, "verification": checks})

    for path in deletions:
        if not unreal.EditorAssetLibrary.delete_asset(path):
            raise RuntimeError("Failed to remove comparison asset " + path)
    if hashes_before != {path: digest(path) for path in protected}:
        raise RuntimeError("A protected source/mesh/Skeleton file changed")
    report = {"status": "REVIEW_ROOT_BODY_WEAPON_TRACKS_REPAIRED_UNIT_SCALE",
              "backup_directory": str(backup), "deleted_assets": deletions,
              "deletion_references": deletion_references,
              "protected_source_mesh_skeleton_hashes_preserved": True,
              "root_origin_policy": "target ref Root + source bone_root component displacement from first frame",
              "body_policy": "existing FK limbs; source bone_000/bone_013 local Cog/Waist tracks",
            "helper_policy": "bone_249/250 -> R/L_Foot_IK; bone_252 -> LookAt; bone_253 -> PL_Cam/Parts",
            "weapon_policy": "Configured Weapon_L attach chain: bone_030 local translation+rotation -> L_Weapon_00; R_Weapon_00 preserved from original IK Retarget bake; unit scale",
            "weapon_reference_basis_errors": weapon_reference_errors,
              "animations": results}
    output = Path(unreal.Paths.project_saved_dir()) / "AssetProbe/mhw_aerial_dive_fingerfixed_root_repair.json"
    output.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    print("MHW_ROOT_REPAIR=" + json.dumps(report, ensure_ascii=False))


if __name__ == "__main__":
    main()
