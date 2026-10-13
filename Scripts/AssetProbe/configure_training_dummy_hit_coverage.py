"""Migrate only DA_TrainingDummy query geometry. Default is read-only.

UnrealEditor-Cmd ... -run=pythonscript -script=<this file> [-CoverageApply]
The three identities, flesh values, locations, colors and all other assets stay intact.
"""
import hashlib
import json
import shutil
from pathlib import Path
import unreal

project = Path(unreal.Paths.project_dir()).resolve()
asset_file = project / "Content/Monster/TrainingDummy/Data/DA_TrainingDummy.uasset"
apply = "-CoverageApply" in unreal.SystemLibrary.get_command_line()
asset = unreal.load_asset("/Game/Monster/TrainingDummy/Data/DA_TrainingDummy")
assert asset is not None
zones = list(asset.get_editor_property("hitzones"))
assert len(zones) == 3

def state(zone):
    return {
        "shape": str(zone.get_editor_property("shape")),
        "half_extent": [getattr(zone.get_editor_property("box_half_extent"), axis) for axis in "xyz"],
        "location": [getattr(zone.get_editor_property("relative_location"), axis) for axis in "xyz"],
        "bone": str(zone.get_editor_property("bone_name")),
        "part": str(zone.get_editor_property("hitzone_tag").get_editor_property("tag_name")),
        "extract": str(zone.get_editor_property("extract_color_tag").get_editor_property("tag_name")),
        "defense": zone.get_editor_property("defense_multiplier"),
        "stagger": zone.get_editor_property("stagger_rate"),
    }

before = [state(zone) for zone in zones]
assert sorted(row["location"][2] for row in before) == [-75.0, 0.0, 75.0]
assert all(row["bone"] == "None" and row["location"][:2] == [0.0, 0.0] for row in before)
changed = any(row["shape"] != str(unreal.MHGZHitzoneShape.BOX) or row["half_extent"] != [51.0, 51.0, 38.5] for row in before)
if apply and changed:
    backup = project / "Saved/AssetBackups/HitzoneCoverage20261012/DA_TrainingDummy.uasset"
    backup.parent.mkdir(parents=True, exist_ok=True)
    if not backup.exists():
        shutil.copy2(asset_file, backup)
    for zone in zones:
        zone.set_editor_property("shape", unreal.MHGZHitzoneShape.BOX)
        zone.set_editor_property("box_half_extent", unreal.Vector(51.0, 51.0, 38.5))
    asset.set_editor_property("hitzones", zones)
    assert unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False)

after = [state(zone) for zone in asset.get_editor_property("hitzones")]
assert all({k: v for k, v in old.items() if k not in ("shape", "half_extent")} ==
           {k: v for k, v in new.items() if k not in ("shape", "half_extent")} for old, new in zip(before, after))
if not apply:
    assert all(row["shape"] == str(unreal.MHGZHitzoneShape.BOX) and row["half_extent"] == [51.0, 51.0, 38.5] for row in after), "Coverage migration still required"
settings = unreal.ValidateAssetsSettings()
settings.set_editor_property("show_if_no_failures", False)
failures, validation = unreal.get_editor_subsystem(unreal.EditorValidatorSubsystem).validate_assets_with_settings(
    [unreal.EditorAssetLibrary.find_asset_data(asset.get_path_name())], settings)
report = {"mode": "Apply" if apply else "AuditOnly", "changed": bool(apply and changed),
          "asset": asset.get_path_name(), "before": before, "after": after,
          "sha256": hashlib.sha256(asset_file.read_bytes()).hexdigest(),
          "validation": {key: validation.get_editor_property(key) for key in
                         ("num_checked", "num_valid", "num_invalid", "num_warnings", "num_skipped")}}
output = project / ("Saved/M6Audit/hitzone-coverage-apply.json" if apply else "Saved/M6Audit/hitzone-coverage-audit.json")
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
assert failures == 0 and report["validation"]["num_invalid"] == 0 and report["validation"]["num_valid"] == 1
unreal.log("[HitzoneCoverageAsset] " + json.dumps(report, ensure_ascii=False))
