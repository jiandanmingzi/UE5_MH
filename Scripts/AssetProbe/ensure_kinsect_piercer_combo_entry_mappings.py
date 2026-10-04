"""Mirror explicit Montage entry mappings for copied Kinsect Piercer Combo routes.

This narrowly scoped migration preserves the Montage entry selected by the source
Ground.TuCi edge when its copied transition has a new globally unique ID.
"""

from __future__ import annotations

import json
import os

import unreal


COMBO_PATH = "/Game/Weapons/InsectGlaive/Data/DA_IG_Combo"
ABILITY_BLUEPRINT_PATH = "/Game/Blueprints/Ability/InsectGlaive/GA_IG_FeiShenYueRu"
SOURCE_TRANSITION_ID = "TuCi_ForwardB_FeiShenYueRu"
CLONE_TRANSITION_ID = "KinsectPiercer_TuCi_ForwardB_FeiShenYueRu"
SOURCE_STATE = "Ground.TuCi"
CLONE_SOURCE_STATE = "IG.Aerial.KinsectPiercer"
EXPECTED_SECTION = "Entry_WithBlend"
REPORT_PATH = os.path.join(
    unreal.Paths.project_saved_dir(), "AssetProbe", "kinsect_piercer_combo_entry_mapping_apply.json"
)


def prop(obj, name, default=None):
    try:
        return obj.get_editor_property(name)
    except Exception:
        return default


report = {"success": False, "changed": False, "errors": []}
try:
    combo = unreal.load_asset(COMBO_PATH)
    if combo is None:
        raise RuntimeError("DA_IG_Combo could not be loaded")
    transitions = list(prop(combo, "transitions", []))
    source = next(
        (row for row in transitions if str(prop(row, "transition_id", "")) == SOURCE_TRANSITION_ID),
        None,
    )
    clone = next(
        (row for row in transitions if str(prop(row, "transition_id", "")) == CLONE_TRANSITION_ID),
        None,
    )
    if source is None or clone is None:
        raise RuntimeError("Expected source and cloned Combo rows were not both found")
    if str(prop(source, "source_state", "")) != SOURCE_STATE:
        raise RuntimeError("Source Combo row no longer has SourceState Ground.TuCi")
    if str(prop(clone, "source_state", "")) != CLONE_SOURCE_STATE:
        raise RuntimeError("Copied Combo row has an unexpected SourceState")
    source_ability_class = prop(source, "ability_class")
    clone_ability_class = prop(clone, "ability_class")
    if source_ability_class != clone_ability_class:
        raise RuntimeError("Copied Combo row no longer targets the same ability as its source")
    if source_ability_class is None or source_ability_class.get_path_name() != (
        "/Game/Blueprints/Ability/InsectGlaive/GA_IG_FeiShenYueRu.GA_IG_FeiShenYueRu_C"
    ):
        raise RuntimeError("Source Combo row targets an unexpected GA")

    ability_blueprint = unreal.load_asset(ABILITY_BLUEPRINT_PATH)
    if ability_blueprint is None:
        raise RuntimeError("GA_IG_FeiShenYueRu Blueprint could not be loaded")
    ability_cdo = unreal.get_default_object(source_ability_class)
    mappings = dict(prop(ability_cdo, "entry_section_by_transition_id", {}))
    source_section = mappings.get(unreal.Name(SOURCE_TRANSITION_ID))
    if source_section is None or str(source_section) != EXPECTED_SECTION:
        raise RuntimeError("Source edge is not explicitly mapped to Entry_WithBlend")

    clone_section = mappings.get(unreal.Name(CLONE_TRANSITION_ID))
    if clone_section is not None and str(clone_section) != EXPECTED_SECTION:
        raise RuntimeError("Copied edge already has a conflicting explicit Montage entry")
    if clone_section is None:
        mappings[unreal.Name(CLONE_TRANSITION_ID)] = unreal.Name(EXPECTED_SECTION)
        ability_blueprint.modify()
        ability_cdo.modify()
        ability_cdo.set_editor_property("entry_section_by_transition_id", mappings)
        if not unreal.EditorAssetLibrary.save_loaded_asset(ability_blueprint, only_if_is_dirty=False):
            raise RuntimeError("Failed to save GA_IG_FeiShenYueRu Blueprint")
        report["changed"] = True

    report["source_transition_id"] = SOURCE_TRANSITION_ID
    report["clone_transition_id"] = CLONE_TRANSITION_ID
    report["ability_blueprint"] = ABILITY_BLUEPRINT_PATH
    report["source_entry_section"] = str(source_section)
    report["clone_entry_section"] = EXPECTED_SECTION
    report["success"] = True
except Exception as exc:  # noqa: BLE001 - write a machine-readable failure report
    report["errors"].append(type(exc).__name__ + ": " + str(exc))

os.makedirs(os.path.dirname(REPORT_PATH), exist_ok=True)
with open(REPORT_PATH, "w", encoding="utf-8") as handle:
    json.dump(report, handle, ensure_ascii=False, indent=2, default=repr)
