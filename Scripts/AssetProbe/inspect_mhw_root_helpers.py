"""Read-only compare Root helper bones in native MHR and MHW retarget bakes."""

import json
from pathlib import Path
import unreal

ROOT = "/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/Review/MHW/AerialDive"
PATHS = {
    "native_idle": "/Game/Characters/Demo/Anims/LocomotionRefactor/RuntimeInPlace/Sheathed/AS_Shth_Idle_IP",
    "380": ROOT + "/Retargeted/AS_MHW_IG_AerialDive_LMT380_MHR_UnitWeaponScale_Review",
    "381": ROOT + "/Retargeted/AS_MHW_IG_AerialDive_LMT381_MHR_UnitWeaponScale_Review",
    "source_380": ROOT + "/AS_MHW_IG_AerialDive_LMT380_Review",
    "source_381": ROOT + "/AS_MHW_IG_AerialDive_LMT381_Review",
}
TARGET_BONES = ["Root", "Cog", "Waist_00", "R_Foot_IK", "L_Foot_IK",
                "R_Foot_IK_end", "L_Foot_IK_end", "LookAt", "PL_Cam", "Parts",
                "R_Leg_03", "L_Leg_03"]
SOURCE_BONES = ["Armature_MHW_body001_0000", "bone_root", "bone_000", "bone_013",
                "bone_004", "bone_017", "bone_021", "bone_249", "bone_250",
                "bone_251", "bone_252", "bone_253"]
options = unreal.AnimPoseEvaluationOptions()
options.set_editor_property("evaluation_type", unreal.AnimDataEvalType.RAW)
options.set_editor_property("should_retarget", False)
report = {}
for label, path in PATHS.items():
    asset = unreal.load_asset(path)
    if asset is None:
        raise RuntimeError("Missing " + path)
    bones = SOURCE_BONES if label.startswith("source_") else TARGET_BONES
    samples = []
    for time in [0, asset.get_play_length() * 0.5, asset.get_play_length()]:
        pose = asset.get_anim_pose_at_time(time, options)
        samples.append({"time": time, "bones": {name: {
            space: [float(v) for v in (
                pose.get_bone_pose(name, enum).translation.x,
                pose.get_bone_pose(name, enum).translation.y,
                pose.get_bone_pose(name, enum).translation.z)]
            for space, enum in [("local", unreal.AnimPoseSpaces.LOCAL),
                                ("component", unreal.AnimPoseSpaces.WORLD)]}
            for name in bones}})
    report[label] = {"asset": path, "skeleton": asset.get_editor_property("skeleton").get_path_name(),
                     "samples": samples}
output = Path(unreal.Paths.project_saved_dir()) / "AssetProbe/mhw_root_helpers.json"
output.write_text(json.dumps(report, indent=2), encoding="utf-8")
print("MHW_ROOT_HELPERS=" + str(output))
