"""Create finger-chain-fixed MHW LMT 380/381 review retargets in UE5.6."""

import json
import runpy
from pathlib import Path
import unreal


ROOT = "/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/Review/MHW/AerialDive"
SETUP = ROOT + "/Retargeting"
OUTPUT_SUBDIR = "Retargeted_FingerFixed"
OUTPUT = ROOT + "/" + OUTPUT_SUBDIR
OUTPUT_SUFFIX = "_MHR_FingerFixed_Review"
SOURCE_MESH_PATH = ROOT + "/SK_MHW_IG_AerialDive_ReviewRig"
TARGET_MESH_PATH = "/Game/Characters/Demo/Meshes/Body/SKM_Demo_Body"
RETARGETER_PATH = SETUP + "/IKRT_MHW_IG_AerialDive_ToDemoIG"
SOURCE_ANIMATIONS = (
    ROOT + "/AS_MHW_IG_AerialDive_LMT380_Review",
    ROOT + "/AS_MHW_IG_AerialDive_LMT381_Review",
)


def load_required(path):
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if asset is None:
        raise FileNotFoundError(path)
    return asset


def package(path):
    return path.split(".", 1)[0]


def finalize_review_outputs():
    # The default pelvis-height solve is invalid for this imported hierarchy.
    # Skeleton/length alone is NOT sufficient verification of a retarget bake.
    script_globals = {"OUTPUT_SUBDIR": OUTPUT_SUBDIR, "OUTPUT_SUFFIX": OUTPUT_SUFFIX}
    runpy.run_path(str(Path(__file__).with_name("repair_mhw_aerial_dive_root_motion.py")),
                   init_globals=script_globals, run_name="__main__")
    # Evaluate the saved result too, including compressed data. A separate
    # commandlet invocation of this verifier additionally proves disk reload.
    runpy.run_path(str(Path(__file__).with_name("verify_mhw_aerial_dive_root_motion.py")),
                   init_globals=script_globals, run_name="__main__")
    runpy.run_path(str(Path(__file__).with_name("audit_mhw_aerial_dive_finger_retarget.py")),
                   run_name="__main__")


def main():
    source_mesh = load_required(SOURCE_MESH_PATH)
    target_mesh = load_required(TARGET_MESH_PATH)
    target_skeleton = target_mesh.get_editor_property("skeleton")
    retargeter = load_required(RETARGETER_PATH)
    unreal.EditorAssetLibrary.make_directory(OUTPUT)

    source_sequences = [load_required(path) for path in SOURCE_ANIMATIONS]
    expected = {}
    for source in source_sequences:
        index = "380" if "LMT380" in source.get_name() else "381"
        dest_name = "AS_MHW_IG_AerialDive_LMT{}{}".format(index, OUTPUT_SUFFIX)
        expected[source.get_name()] = (index, dest_name)

    existing = [unreal.EditorAssetLibrary.does_asset_exist(OUTPUT + "/" + row[1])
                for row in expected.values()]
    if all(existing):
        created = []
        for index, dest_name in expected.values():
            new_path = OUTPUT + "/" + dest_name
            asset = load_required(new_path)
            if asset.get_editor_property("skeleton") != target_skeleton:
                raise RuntimeError("Existing review result has the wrong Skeleton: " + new_path)
            asset.set_preview_skeletal_mesh(target_mesh)
            asset.set_editor_property("rate_scale", 1.0)
            unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False)
            created.append({
                "asset": new_path,
                "skeleton": target_skeleton.get_path_name(),
                "preview_mesh": target_mesh.get_path_name(),
                "sequence_length_seconds": float(asset.get_editor_property("sequence_length")),
                "rate_scale": float(asset.get_editor_property("rate_scale")),
                "weapon_scale_policy": "unit scale only",
            })
        finalize_review_outputs()
        report = {"status": "EXISTING_MHW_AERIAL_DIVE_FINGERFIX_RESULTS_REPAIRED_AND_VERIFIED",
                  "source_assets_preserved": True,
                  "gameplay_assets_modified": False,
                  "animations": sorted(created, key=lambda row: row["asset"])}
        print("MHW_AERIAL_DIVE_RETARGET=" + json.dumps(report, ensure_ascii=False))
        return
    if any(existing):
        raise RuntimeError("Only some unit-scale outputs exist; refusing a partial retarget rerun")

    # UE 5.6's batch API accesses Slate Content Browser after baking; it can
    # crash a commandlet before the mandatory post-processing is reached.
    if "-run=" in unreal.SystemLibrary.get_command_line().lower():
        raise RuntimeError("Initial IK batch export requires the full editor, not a commandlet. "
                           "Existing review outputs can be repaired headlessly.")

    asset_data = [unreal.EditorAssetLibrary.find_asset_data(path) for path in SOURCE_ANIMATIONS]
    if any(not data.is_valid() for data in asset_data):
        raise RuntimeError("Could not resolve both source animations in Asset Registry")

    results = unreal.IKRetargetBatchOperation.duplicate_and_retarget(
        asset_data, source_mesh, target_mesh, retargeter,
        search="_Review", replace="", suffix=OUTPUT_SUFFIX,
        include_referenced_assets=False,
    )
    if len(results) != 2:
        raise RuntimeError("IK retargeter returned {} assets; expected 2".format(len(results)))

    created = []
    for data in results:
        asset = data.get_asset()
        if asset is None:
            raise RuntimeError("Retarget batch returned an invalid AssetData")
        asset_name = asset.get_name()
        matching = [value for value in expected.values() if value[1] == asset_name]
        if len(matching) != 1:
            raise RuntimeError("Unexpected retarget result name: " + asset_name)
        index, dest_name = matching[0]
        old_path = unreal.EditorAssetLibrary.get_path_name_for_loaded_asset(asset)
        new_path = OUTPUT + "/" + dest_name
        sequence_skeleton = asset.get_editor_property("skeleton")
        if sequence_skeleton != target_skeleton:
            raise RuntimeError("Retarget result did not use SK_Demo_Body Skeleton: " + old_path)
        if not unreal.EditorAssetLibrary.rename_asset(package(old_path), new_path):
            raise RuntimeError("Could not move retargeted result into review folder: " + old_path)
        asset = load_required(new_path)
        asset.set_preview_skeletal_mesh(target_mesh)
        asset.set_editor_property("rate_scale", 1.0)
        if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
            raise RuntimeError("Could not save retargeted result " + new_path)
        created.append({
            "asset": new_path,
            "skeleton": asset.get_editor_property("skeleton").get_path_name(),
            "preview_mesh": target_mesh.get_path_name(),
            "sequence_length_seconds": float(asset.get_editor_property("sequence_length")),
            "rate_scale": float(asset.get_editor_property("rate_scale")),
            "weapon_scale_policy": "unit scale only",
        })

    finalize_review_outputs()
    report = {
        "status": "MHW_AERIAL_DIVE_FINGERFIX_RETARGETED_AND_ROOT_TRACKS_VERIFIED",
        "source_skeleton": source_mesh.get_editor_property("skeleton").get_path_name(),
        "target_skeleton": target_skeleton.get_path_name(),
        "retargeter": retargeter.get_path_name(),
        "animations": sorted(created, key=lambda row: row["asset"]),
        "source_assets_preserved": True,
        "gameplay_assets_modified": False,
    }
    unreal.log("[MHW AerialDive Retarget] " + json.dumps(report, ensure_ascii=False))
    print("MHW_AERIAL_DIVE_RETARGET=" + json.dumps(report, ensure_ascii=False))


main()
