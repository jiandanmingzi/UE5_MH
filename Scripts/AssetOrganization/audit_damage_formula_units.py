"""Read-only audit of insect-glaive damage and physical hitzone values.

Run with UE 5.6:
    UnrealEditor-Cmd.exe MHGZ.uproject -run=pythonscript -script=<this file>

The script only reads loaded assets and writes a JSON report under Saved/.
It does not modify or save any asset.
"""

from __future__ import annotations

import json
from pathlib import Path
import sys

import unreal

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from ue_asset_utils import as_sequence, is_read_error, read_field_or_error as read_field


def scalable_float_value(value):
    if isinstance(value, dict):
        return value
    try:
        return {
            "value": value.get_editor_property("value"),
            "curve": str(value.get_editor_property("curve")),
        }
    except Exception:
        try:
            return {"repr": str(value), "type": str(type(value))}
        except Exception as exc:
            return {"read_error": repr(exc)}


def map_values(value):
    if isinstance(value, dict):
        return {str(key): item for key, item in value.items()}
    try:
        return {str(key): item for key, item in value.items()}
    except Exception:
        pass
    return {"repr": str(value), "type": str(type(value))}


def sequence_values(value):
    try:
        return as_sequence(value)
    except TypeError:
        return None


def main():
    root = "/Game/Blueprints/Ability/InsectGlaive"
    asset_paths = unreal.EditorAssetLibrary.list_assets(root, recursive=True, include_folder=False)
    abilities = []
    for asset_path in sorted(asset_paths):
        name = asset_path.rsplit("/", 1)[-1].split(".", 1)[0]
        if not name.startswith("GA_IG_"):
            continue
        asset = unreal.EditorAssetLibrary.load_asset(asset_path)
        if asset is None:
            abilities.append({"asset": asset_path, "load_error": True})
            continue
        try:
            generated_class = asset.generated_class()
            cdo = unreal.get_default_object(generated_class)
            segments = read_field(cdo, "attack_segments")
        except Exception as exc:
            abilities.append({"asset": asset_path, "read_error": repr(exc), "type": str(type(asset))})
            continue

        if is_read_error(segments):
            if "Failed to find property 'attack_segments'" in segments["read_error"]:
                abilities.append({"asset": asset_path, "class": str(generated_class),
                                  "segments": [], "skip_reason": "non-attack ability"})
            else:
                abilities.append({"asset": asset_path, **segments})
            continue

        rows = []
        segment_values = sequence_values(segments)
        if segment_values is not None:
            for index, segment in enumerate(segment_values):
                damage = read_field(segment, "damage")
                if isinstance(damage, dict):
                    rows.append({"index": index, "damage": damage})
                    continue
                damage_effect = read_field(damage, "damage_effect_class")
                rows.append({
                    "index": index,
                    "damage_effect": None if damage_effect is None else str(damage_effect),
                    "motion_value": scalable_float_value(read_field(damage, "motion_value")),
                })
        else:
            rows = {"segments_value": str(segments), "type": str(type(segments))}
        abilities.append({"asset": asset_path, "class": str(generated_class), "segments": rows})

    dummy_path = "/Game/Monster/TrainingDummy/Data/DA_TrainingDummy"
    dummy = unreal.EditorAssetLibrary.load_asset(dummy_path)
    dummy_hitzones = []
    dummy_error = None
    if dummy is None:
        dummy_error = "asset not found"
    else:
        zones = read_field(dummy, "hitzones")
        zone_values = sequence_values(zones)
        if zone_values is not None:
            for index, zone in enumerate(zone_values):
                dummy_hitzones.append({
                    "index": index,
                    "defense_multiplier": read_field(zone, "defense_multiplier"),
                    "physical_hitzone_multipliers": map_values(
                        read_field(zone, "physical_hitzone_multipliers")
                    ),
                    "element_hitzone_multipliers": map_values(
                        read_field(zone, "element_hitzone_multipliers")
                    ),
                })
        else:
            dummy_error = {"hitzones_value": str(zones), "type": str(type(zones))}

    config_path = "/Game/Weapons/InsectGlaive/Data/DA_IG_Combat"
    config = unreal.EditorAssetLibrary.load_asset(config_path)
    config_values = None
    config_error = None
    if config is None:
        config_error = "asset not found"
    else:
        config_values = {}
        for field in (
            "send_kinsect_motion_value",
            "draw_send_kinsect_motion_value",
            "awakened_kinsect_motion_value",
            "powder_damage_contribution",
            "powder_motion_value",
            "powder_damage_multiplier",
        ):
            config_values[field] = read_field(config, field)

    report = {
        "schema_version": 1,
        "mode": "AuditOnly",
        "abilities": abilities,
        "training_dummy": {"asset": dummy_path, "hitzones": dummy_hitzones, "error": dummy_error},
        "combat_config": {"asset": config_path, "values": config_values, "error": config_error},
    }
    output = Path(unreal.Paths.project_saved_dir()) / "DamageUnitsAudit" / "before.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2, default=str), encoding="utf-8")
    unreal.log("[DamageUnitsAudit] " + str(output))
    unreal.log("[DamageUnitsAudit] %d abilities, %d dummy hitzones" % (len(abilities), len(dummy_hitzones)))


main()
