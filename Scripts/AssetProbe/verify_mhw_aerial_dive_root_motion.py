"""Read-only RAW/compressed verification of the saved review bake.

Run as a separate commandlet to verify disk reload; it can also be called after
the final write within the generation process to enforce post-save checks.
"""

import importlib.util
import json
from pathlib import Path
import runpy
import unreal

OUTPUT_SUBDIR = globals().get("OUTPUT_SUBDIR", "Retargeted_FingerFixed")
OUTPUT_SUFFIX = globals().get("OUTPUT_SUFFIX", "_MHR_FingerFixed_Review")

spec = importlib.util.spec_from_file_location(
    "mhw_root_recipe", Path(__file__).with_name("repair_mhw_aerial_dive_root_motion.py"))
recipe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(recipe)
recipe.OUTPUT_SUBDIR = OUTPUT_SUBDIR
recipe.OUTPUT_SUFFIX = OUTPUT_SUFFIX

target_skeleton = recipe.load(recipe.TARGET_SKELETON)
reference = unreal.AnimPoseExtensions.get_reference_pose(target_skeleton)
root_ref = reference.get_ref_bone_pose("Root", unreal.AnimPoseSpaces.LOCAL)
report = {"status": "SAVED_REVIEW_RAW_COMPRESSED_AND_WEAPON_PASS", "animations": []}
for index in recipe.IDS:
    source = recipe.load(recipe.ROOT + f"/AS_MHW_IG_AerialDive_LMT{index}_Review")
    target = recipe.load(recipe.unit_path(index))
    weapon_baseline = recipe.load(recipe.baseline_path(index))
    if target.get_editor_property("skeleton") != target_skeleton:
        raise RuntimeError("Wrong target Skeleton")
    if abs(float(source.get_play_length()) - float(target.get_play_length())) > 1e-5:
        raise RuntimeError("Source/target duration differs")
    if float(target.get_editor_property("rate_scale")) != 1.0:
        raise RuntimeError("Unexpected playback rate")
    preserve = recipe.preserve_path(index)
    if preserve and unreal.EditorAssetLibrary.does_asset_exist(preserve):
        raise RuntimeError("Retired comparison asset still exists")
    report["animations"].append({"asset": recipe.unit_path(index),
        "raw": recipe.validate(source, target, root_ref, weapon_baseline=weapon_baseline),
        "compressed": recipe.validate(source, target, root_ref,
                                       unreal.AnimDataEvalType.COMPRESSED,
                                       weapon_baseline=weapon_baseline)})

output = Path(unreal.Paths.project_saved_dir()) / "AssetProbe/mhw_aerial_dive_fingerfixed_root_verification.json"
output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
print("MHW_ROOT_VERIFICATION=" + json.dumps(report, ensure_ascii=False))

# A numerical pose comparison against the actual unsheathed IG idle is part of
# this review bake's acceptance, including attachment socket metadata when UE
# exposes it. Keep its report separate from the raw/compressed track audit.
runpy.run_path(str(Path(__file__).with_name("audit_mhw_aerial_dive_weapon_pose.py")),
               run_name="__main__")
