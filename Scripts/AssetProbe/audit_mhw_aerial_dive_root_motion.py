"""Read source/target hierarchy motion without changing Unreal content."""

import json
from pathlib import Path
import unreal

ROOT = "/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/Review/MHW/AerialDive"
SOURCES = [(str(i), ROOT + f"/AS_MHW_IG_AerialDive_LMT{i}_Review",
            ["Armature_MHW_body001_0000", "bone_root", "bone_000", "bone_013", "bone_001", "bone_004", "bone_017", "bone_021"])
           for i in (380, 381)]
TARGETS = [(str(i) + "_target", ROOT + f"/Retargeted/AS_MHW_IG_AerialDive_LMT{i}_MHR_UnitWeaponScale_Review",
            ["Root", "Cog", "Waist_00", "Spine_00", "Head_00", "L_Leg_03", "R_Leg_03"])
           for i in (380, 381)]
TARGETS.append(("native_idle", "/Game/Characters/Demo/Anims/LocomotionRefactor/RuntimeInPlace/Sheathed/AS_Shth_Idle_IP",
                ["Root", "Cog", "Waist_00", "Spine_00", "Head_00", "L_Leg_03", "R_Leg_03"]))


def vec(v):
    return [float(v.x), float(v.y), float(v.z)]


def transform(t):
    return {"p": vec(t.translation), "q": [float(t.rotation.x), float(t.rotation.y),
                                          float(t.rotation.z), float(t.rotation.w)], "s": vec(t.scale3d)}


def main():
    options = unreal.AnimPoseEvaluationOptions()
    options.set_editor_property("evaluation_type", unreal.AnimDataEvalType.RAW)
    options.set_editor_property("should_retarget", False)
    options.set_editor_property("extract_root_motion", False)
    options.set_editor_property("incorporate_root_motion_into_pose", False)
    report = {"sequences": {}, "retarget_api": {}}
    for label, path, bones in SOURCES + TARGETS:
        seq = unreal.load_asset(path)
        if not seq:
            raise RuntimeError("Missing " + path)
        length = float(seq.get_play_length())
        rows = []
        for i in range(int(round(length * 60)) + 1):
            time = min(i / 60, length)
            pose = seq.get_anim_pose_at_time(time, options)
            rows.append({"frame": i, "time": time, "bones": {
                name: {"local": transform(pose.get_bone_pose(unreal.Name(name), unreal.AnimPoseSpaces.LOCAL)),
                       "component": transform(pose.get_bone_pose(unreal.Name(name), unreal.AnimPoseSpaces.WORLD))}
                for name in bones}})
        report["sequences"][label] = {"asset": path, "duration": length, "frames": rows}
    controller = unreal.IKRetargeterController.get_controller(unreal.load_asset(ROOT + "/Retargeting/IKRT_MHW_IG_AerialDive_ToDemoIG"))
    report["retarget_api"]["controller"] = [name for name in dir(controller) if not name.startswith("_")]
    for name in dir(controller):
        if any(token in name for token in ("op", "pose", "root_translation")):
            report["retarget_api"][name] = getattr(getattr(controller, name, None), "__doc__", None)
    report["transform_api"] = [name for name in dir(unreal.Transform) if any(token in name for token in ("inverse", "relative", "multiply", "transform"))]
    report["quat_api"] = [name for name in dir(unreal.Quat) if any(token in name for token in ("inverse", "multiply", "rotate"))]
    output = Path(unreal.Paths.project_saved_dir()) / "AssetProbe/mhw_aerial_dive_root_motion_audit.json"
    output.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    print("MHW_ROOT_AUDIT=" + str(output))


main()
