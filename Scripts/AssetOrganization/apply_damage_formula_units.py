"""Apply the insect-glaive damage-unit migration to project assets.

Run only against the matching MHGZ project with UE 5.6:
    UnrealEditor-Cmd.exe MHGZ.uproject -ExecutePythonScript=<this file>

The script backs up every package it changes under Saved/DamageUnitsAudit/Backup,
updates damaging GA_IG segments to raw MV 10, converts legacy normalized physical
hitzones to 0..100, and writes before/after JSON reports. It does not run tests.
"""

from __future__ import annotations

import json
import os
import shutil
import traceback
from pathlib import Path
import sys

import unreal

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from ue_asset_utils import as_sequence, map_items, read_field as field


ABILITY_ROOT = "/Game/Blueprints/Ability/InsectGlaive"
DUMMY_PATH = "/Game/Monster/TrainingDummy/Data/DA_TrainingDummy"
COMBAT_CONFIG_PATH = "/Game/Weapons/InsectGlaive/Data/DA_IG_Combat"
TARGET_GA_MOTION_VALUE = 10.0
TARGET_PHYSICAL_DEFAULT = 100.0
TARGET_CONFIG_VALUES = {
    "send_kinsect_motion_value": 20.0,
    "draw_send_kinsect_motion_value": 20.0,
    "awakened_kinsect_motion_value": 30.0,
    "powder_motion_value": 100.0,
}


def log(message):
    unreal.log("[DamageUnitsApply] " + str(message))


def package_file_paths(asset_path):
    package = asset_path.split(".", 1)[0]
    relative = package.removeprefix("/Game/").replace("/", os.sep)
    base = Path(unreal.Paths.project_content_dir()) / relative
    return [Path(str(base) + suffix) for suffix in (".uasset", ".uexp", ".ubulk")]


def backup_package(asset_path):
    backup_root = Path(unreal.Paths.project_saved_dir()) / "DamageUnitsAudit" / "Backup"
    package = asset_path.split(".", 1)[0]
    relative = package.removeprefix("/Game/").replace("/", os.sep)
    destination_base = backup_root / "Content" / relative
    destination_base.parent.mkdir(parents=True, exist_ok=True)
    copied = []
    for source in package_file_paths(asset_path):
        if source.is_file():
            destination = Path(str(destination_base) + source.suffix)
            shutil.copy2(source, destination)
            copied.append(str(destination))
    if not any(path.endswith(".uasset") for path in copied):
        raise RuntimeError("Could not back up package before mutation: " + asset_path)
    return copied


def save_asset(asset, path):
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, False):
        raise RuntimeError("UE failed to save asset: " + path)


def scalable_float_payload(value):
    return {
        "value": float(field(value, "value")),
        "curve": str(field(value, "curve")),
    }


def update_abilities(changed_assets):
    changed_rows = []
    paths = unreal.EditorAssetLibrary.list_assets(
        ABILITY_ROOT, recursive=True, include_folder=False
    )
    for asset_path in sorted(paths):
        asset_name = asset_path.rsplit("/", 1)[-1].split(".", 1)[0]
        if not asset_name.startswith("GA_IG_"):
            continue
        log("Inspecting " + asset_path)
        asset = unreal.EditorAssetLibrary.load_asset(asset_path)
        if asset is None:
            raise RuntimeError("Unable to load ability asset: " + asset_path)
        generated_class = asset.generated_class()
        if generated_class is None:
            raise RuntimeError("Ability has no generated class: " + asset_path)
        cdo = unreal.get_default_object(generated_class)
        try:
            segments = as_sequence(field(cdo, "attack_segments"))
        except Exception as exc:
            if "Failed to find property 'attack_segments'" in str(exc):
                log("Skipping non-attack ability without AttackSegments: " + asset_path)
                changed_rows.append({
                    "asset": asset_path,
                    "segments": [],
                    "changed": False,
                    "skip_reason": "no AttackSegments property",
                })
                continue
            raise
        updated_segments = []
        asset_rows = []
        changed = False
        for index, segment in enumerate(segments):
            damage = field(segment, "damage")
            damage_effect = field(damage, "damage_effect_class")
            if damage_effect is not None:
                motion = field(damage, "motion_value")
                old_payload = scalable_float_payload(motion)
                curve = field(motion, "curve")
                curve_table = field(curve, "curve_table")
                curve_row_value = field(curve, "row_name")
                curve_row = str(curve_row_value) if curve_row_value is not None else ""
                if curve_table is not None or curve_row not in ("", "None", "NAME_None"):
                    raise RuntimeError(
                        "Refusing to overwrite curve-driven MV at %s AttackSegments[%d]"
                        % (asset_path, index)
                    )
                old_value = float(field(motion, "value"))
                if abs(old_value - TARGET_GA_MOTION_VALUE) > 0.0001:
                    motion.set_editor_property("value", TARGET_GA_MOTION_VALUE)
                    damage.set_editor_property("motion_value", motion)
                    segment.set_editor_property("damage", damage)
                    changed = True
                asset_rows.append({
                    "index": index,
                    "damage_effect": str(damage_effect),
                    "before": old_payload,
                    "after": scalable_float_payload(field(field(segment, "damage"), "motion_value")),
                })
            updated_segments.append(segment)
        if changed:
            cdo.set_editor_property("attack_segments", updated_segments)
            log("Backing up and saving " + asset_path)
            backup_package(asset_path)
            save_asset(asset, asset_path)
            changed_assets.append(asset_path)
            log("Saved " + asset_path)
        changed_rows.append({"asset": asset_path, "segments": asset_rows, "changed": changed})
    return changed_rows


def convert_legacy_percent(value, label):
    value = float(value)
    if value < 0.0 or value > 100.0:
        raise RuntimeError("Physical hitzone is outside 0..100 at %s: %s" % (label, value))
    # The audited pre-migration assets used normalized multipliers (0..1.5).
    return value * 100.0 if value <= 1.5 else value


def update_dummy(changed_assets):
    asset = unreal.EditorAssetLibrary.load_asset(DUMMY_PATH)
    if asset is None:
        raise RuntimeError("Unable to load training-dummy config: " + DUMMY_PATH)
    zones = as_sequence(field(asset, "hitzones"))
    updated_zones = []
    rows = []
    changed = False
    for index, zone in enumerate(zones):
        old_default = float(field(zone, "defense_multiplier"))
        new_default = convert_legacy_percent(old_default, "Hitzones[%d].DefenseMultiplier" % index)
        old_map = map_items(field(zone, "physical_hitzone_multipliers"))
        converted_map = {}
        for key, old_value in old_map:
            converted_map[key] = convert_legacy_percent(
                old_value, "Hitzones[%d].PhysicalHitzoneMultipliers[%s]" % (index, key)
            )
        if abs(new_default - old_default) > 0.0001 or any(
            abs(float(converted_map[key]) - float(value)) > 0.0001
            for key, value in old_map
        ):
            zone.set_editor_property("defense_multiplier", new_default)
            zone.set_editor_property("physical_hitzone_multipliers", converted_map)
            changed = True
        rows.append({
            "index": index,
            "defense_before": old_default,
            "defense_after": new_default,
            "physical_map_before": {str(key): float(value) for key, value in old_map},
            "physical_map_after": {str(key): float(value) for key, value in converted_map.items()},
        })
        updated_zones.append(zone)
    if changed:
        asset.set_editor_property("hitzones", updated_zones)
        backup_package(DUMMY_PATH)
        save_asset(asset, DUMMY_PATH)
        changed_assets.append(DUMMY_PATH)
    return {"asset": DUMMY_PATH, "hitzones": rows, "changed": changed}


def update_combat_config(changed_assets):
    asset = unreal.EditorAssetLibrary.load_asset(COMBAT_CONFIG_PATH)
    if asset is None:
        raise RuntimeError("Unable to load insect-glaive config: " + COMBAT_CONFIG_PATH)
    before = {}
    changed = False
    for property_name, target_value in TARGET_CONFIG_VALUES.items():
        old_value = float(field(asset, property_name))
        before[property_name] = old_value
        if abs(old_value - target_value) > 0.0001:
            asset.set_editor_property(property_name, target_value)
            changed = True
    if changed:
        backup_package(COMBAT_CONFIG_PATH)
        save_asset(asset, COMBAT_CONFIG_PATH)
        changed_assets.append(COMBAT_CONFIG_PATH)
    after = {name: float(field(asset, name)) for name in TARGET_CONFIG_VALUES}
    return {"asset": COMBAT_CONFIG_PATH, "before": before, "after": after, "changed": changed}


def main():
    changed_assets = []
    report_root = Path(unreal.Paths.project_saved_dir()) / "DamageUnitsAudit"
    report_root.mkdir(parents=True, exist_ok=True)
    try:
        abilities = update_abilities(changed_assets)
        dummy = update_dummy(changed_assets)
        combat_config = update_combat_config(changed_assets)
        report = {
            "mode": "Apply",
            "target_ga_motion_value": TARGET_GA_MOTION_VALUE,
            "changed_assets": changed_assets,
            "abilities": abilities,
            "training_dummy": dummy,
            "combat_config": combat_config,
            "backup_root": str(report_root / "Backup"),
        }
        output = report_root / "after.json"
        output.write_text(json.dumps(report, ensure_ascii=False, indent=2, default=str), encoding="utf-8")
        log("Saved migration report: " + str(output))
        log(json.dumps({"changed_assets": changed_assets, "training_dummy": dummy, "combat_config": combat_config}, ensure_ascii=False, default=str))
    except Exception:
        failure = {
            "mode": "ApplyFailed",
            "changed_assets_saved_before_failure": changed_assets,
            "traceback": traceback.format_exc(),
            "backup_root": str(report_root / "Backup"),
        }
        output = report_root / "failure.json"
        output.write_text(json.dumps(failure, ensure_ascii=False, indent=2), encoding="utf-8")
        unreal.log_error("[DamageUnitsApply] " + failure["traceback"])
        log("Saved failure report: " + str(output))
        raise


main()
