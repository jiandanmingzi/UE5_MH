"""Read-only source-root and AnimBP audit, run with Unreal Python commandlet."""
import json
import os
import unreal

path = "/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_CaoChongChuanCi_Over"
sequence = unreal.EditorAssetLibrary.load_asset(path)
if sequence is None:
    raise RuntimeError(path)
options = unreal.AnimPoseEvaluationOptions()
options.evaluation_type = unreal.AnimDataEvalType.RAW
options.should_retarget = False
options.extract_root_motion = False
options.incorporate_root_motion_into_pose = False
length = sequence.get_play_length()
poses = []
for t in (0.0, length * 0.25, length * 0.5, length * 0.75, length):
    pose = sequence.get_anim_pose_at_time(t, options)
    root = pose.get_bone_names()[0]
    transform = pose.get_bone_pose(root, unreal.AnimPoseSpaces.LOCAL)
    poses.append({"time": t, "bone": str(root), "transform": str(transform)})
bp = unreal.EditorAssetLibrary.load_blueprint_class("/Game/Blueprints/Characters/Demo/Animation/ABP_MH_Character")
cdo = unreal.get_default_object(bp)
data = {"source": path, "length": length, "rate_scale": sequence.get_editor_property("rate_scale"),
        "root_motion_mode": str(cdo.get_editor_property("root_motion_mode")), "poses": poses,
        "sequence_root_apis": [n for n in dir(sequence) if "root_motion" in n.lower()]}
out = os.path.join(unreal.Paths.project_saved_dir(), "AssetProbe", "kinsect_piercer_root_motion.json")
os.makedirs(os.path.dirname(out), exist_ok=True)
with open(out, "w", encoding="utf-8") as stream:
    json.dump(data, stream, ensure_ascii=False, indent=2)
unreal.log("[PiercerRootAudit] " + json.dumps(data, ensure_ascii=False))
