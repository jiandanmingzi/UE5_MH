"""Read-only audit of copied TuCi combo routes and target montage entry selection.

Run with UnrealEditor-Cmd -run=pythonscript -script=Scripts/AssetProbe/audit_kinsect_piercer_combo_entries.py.
The script reads Combo data and attack ability defaults, and writes a JSON report under Saved/AssetProbe.
"""

from __future__ import annotations

import json
import os

import unreal


COMBO_PATH = "/Game/Weapons/InsectGlaive/Data/DA_IG_Combo"
SOURCE_STATE = "Ground.TuCi"
CLONED_SOURCE_STATE = "IG.Aerial.KinsectPiercer"
CLONE_PREFIX = "KinsectPiercer_"
REPORT_PATH = os.path.join(
    unreal.Paths.project_saved_dir(), "AssetProbe", "kinsect_piercer_combo_entries.json"
)


def prop(obj, name, default=None):
    try:
        return obj.get_editor_property(name)
    except Exception:
        return default


def name_string(value):
    return "" if value is None else str(value)


def map_as_strings(value):
    if value is None:
        return {}
    try:
        return {name_string(key): name_string(section) for key, section in value.items()}
    except Exception as exc:  # report reflection limitations, never hide them
        return {"_error": str(exc)}


def select_entry(transition_id, source_state, defaults):
    by_transition = defaults["by_transition"]
    by_source = defaults["by_source"]
    if transition_id in by_transition:
        return {"kind": "TransitionID", "section": by_transition[transition_id]}
    if source_state in by_source:
        return {"kind": "SourceState", "section": by_source[source_state]}
    if defaults["default"]:
        return {"kind": "Default", "section": defaults["default"]}
    return {"kind": "MontageStart", "section": ""}


def ability_defaults(ability_class):
    cdo = unreal.get_default_object(ability_class)
    return {
        "by_transition": map_as_strings(prop(cdo, "entry_section_by_transition_id", {})),
        "by_source": map_as_strings(prop(cdo, "entry_section_by_source_state", {})),
        "default": name_string(prop(cdo, "default_entry_section", "")),
    }


report = {"success": False, "source_rows": [], "cloned_rows": [], "errors": []}
try:
    combo = unreal.load_asset(COMBO_PATH)
    if combo is None:
        raise RuntimeError("DA_IG_Combo could not be loaded")
    rows = list(prop(combo, "transitions", []))
    row_by_id = {name_string(prop(row, "transition_id")): row for row in rows}
    originals = [row for row in rows if name_string(prop(row, "source_state")) == SOURCE_STATE]
    if not originals:
        raise RuntimeError("No Combo rows have SourceState Ground.TuCi")

    for original in originals:
        original_id = name_string(prop(original, "transition_id"))
        clone_id = CLONE_PREFIX + original_id
        clone = row_by_id.get(clone_id)
        ability_class = prop(original, "ability_class")
        if clone is None:
            raise RuntimeError("Missing expected copied row: " + clone_id)
        if name_string(prop(clone, "source_state")) != CLONED_SOURCE_STATE:
            raise RuntimeError("Copied row has the wrong source state: " + clone_id)
        if prop(clone, "ability_class") != ability_class:
            raise RuntimeError("Copied row changed target ability: " + clone_id)

        defaults = ability_defaults(ability_class)
        source_entry = select_entry(original_id, SOURCE_STATE, defaults)
        clone_entry = select_entry(clone_id, CLONED_SOURCE_STATE, defaults)
        entry_equal = source_entry == clone_entry
        common = {
            "input_tag": name_string(prop(original, "input_tag")),
            "direction": name_string(prop(original, "direction")),
            "ability_class": ability_class.get_path_name() if ability_class else "",
            "target_state": name_string(prop(original, "target_state")),
            "source_entry": source_entry,
            "clone_entry": clone_entry,
            "entry_selection_matches": entry_equal,
            "target_entry_defaults": defaults,
        }
        report["source_rows"].append({"transition_id": original_id, "source_state": SOURCE_STATE, **common})
        report["cloned_rows"].append({"transition_id": clone_id, "source_state": CLONED_SOURCE_STATE, **common})

    report["success"] = True
except Exception as exc:  # noqa: BLE001 - always emit a machine-readable result
    report["errors"].append(type(exc).__name__ + ": " + str(exc))

os.makedirs(os.path.dirname(REPORT_PATH), exist_ok=True)
with open(REPORT_PATH, "w", encoding="utf-8") as handle:
    json.dump(report, handle, ensure_ascii=False, indent=2, default=repr)
