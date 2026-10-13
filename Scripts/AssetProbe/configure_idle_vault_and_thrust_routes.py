"""Update Idle Insect Glaive pole-vault and thrust Combo routes.

Run from the project root:
  UnrealEditor-Cmd.exe MHGZ.uproject -run=pythonscript \
    -script=Scripts/AssetProbe/configure_idle_vault_and_thrust_routes.py \
    -unattended -nop4 -nosplash -stdout

Only DA_IG_Combo is modified. The script checks the expected current rows,
copies all fields when adding directional thrust edges, saves once, validates
the final in-memory table, and writes a report under Saved/AssetProbe. Run the
read-only M6 baseline audit in a fresh commandlet process for disk readback.
"""

from __future__ import annotations

import json
import os

import unreal


COMBO_PATH = "/Game/Weapons/InsectGlaive/Data/DA_IG_Combo"
REPORT_PATH = os.path.join(
    unreal.Paths.project_saved_dir(),
    "AssetProbe",
    "idle_vault_and_thrust_routes_apply.json",
)

IDLE = "Idle"
RTA = "Input.Weapon.RTA"
Y = "Input.Weapon.Y"
FORWARD_VAULT_ID = "NoneRTA_QianChengGanTiao"
OLD_FORWARD_VAULT_ID = "ForwardRTA_QianChengGanTiao"
OLD_BACK_VAULT_ID = "BackRTA_HouChengGanTiao"
THRUST_SOURCE_ID = "ForwardY_TuCi"
THRUST_GA_PATH = "/Game/Blueprints/Ability/InsectGlaive/GA_IG_TuCi.GA_IG_TuCi_C"
FRONT_VAULT_GA_PATH = (
    "/Game/Blueprints/Ability/InsectGlaive/GA_IG_QianChengGanTiao.GA_IG_QianChengGanTiao_C"
)

TRANSITION_FIELDS = (
    "transition_id",
    "source_state",
    "match_any_state",
    "blocked_source_states",
    "input_tag",
    "direction",
    "execution_policy",
    "ability_class",
    "montage_blend_in_time",
    "max_correction_angle",
    "target_state",
    "state_policy",
    "landing_policy",
    "required_tags",
    "blocked_tags",
    "stamina_required",
    "requires_combo_window",
    "requires_dodge_accept_window",
    "grant_timing",
    "granted_tags",
    "auto_transition",
    "priority",
)


def prop(obj, name):
    return obj.get_editor_property(name)


def enum_member(member_name):
    for enum_type_name in ("DirectionalInput", "EDirectionalInput"):
        enum_type = getattr(unreal, enum_type_name, None)
        if enum_type is not None and hasattr(enum_type, member_name):
            return getattr(enum_type, member_name)
    raise RuntimeError("Could not resolve EDirectionalInput." + member_name)


def text(value):
    if value is None:
        return ""
    if type(value).__name__ == "GameplayTag":
        try:
            return str(value.get_editor_property("tag_name"))
        except Exception:  # noqa: BLE001 - fall back to UE's display string
            pass
    return str(value)


def rows_by_id(rows):
    result = {}
    for row in rows:
        transition_id = text(prop(row, "transition_id"))
        if transition_id in result:
            raise RuntimeError("Duplicate TransitionID: " + transition_id)
        result[transition_id] = row
    return result


def set_id(row, transition_id):
    row.set_editor_property("transition_id", unreal.Name(transition_id))


def clone_transition(source, transition_id, direction):
    transition_type = getattr(unreal, "ComboTransition", None)
    if transition_type is None:
        raise RuntimeError("Python reflection type unreal.ComboTransition is unavailable")
    clone = transition_type()
    for field in TRANSITION_FIELDS:
        clone.set_editor_property(field, prop(source, field))
    set_id(clone, transition_id)
    clone.set_editor_property("direction", direction)
    return clone


def describe(row):
    return {
        "transition_id": text(prop(row, "transition_id")),
        "source_state": text(prop(row, "source_state")),
        "match_any_state": bool(prop(row, "match_any_state")),
        "blocked_source_states": [text(value) for value in prop(row, "blocked_source_states")],
        "input_tag": text(prop(row, "input_tag")),
        "direction": text(prop(row, "direction")),
        "ability_class": prop(row, "ability_class").get_path_name()
        if prop(row, "ability_class") else "",
        "max_correction_angle": float(prop(row, "max_correction_angle")),
        "target_state": text(prop(row, "target_state")),
    }


def named_map(value):
    try:
        return {text(key): text(section) for key, section in value.items()}
    except Exception:  # noqa: BLE001 - include reflection limitations in the report
        return {"_unreadable": repr(value)}


report = {"success": False, "changed": False, "errors": []}

try:
    combo = unreal.load_asset(COMBO_PATH)
    if combo is None:
        raise RuntimeError("Could not load " + COMBO_PATH)

    rows = list(prop(combo, "transitions"))
    by_id = rows_by_id(rows)
    front_vault = by_id.get(FORWARD_VAULT_ID)
    thrust_source = by_id.get(THRUST_SOURCE_ID)
    if front_vault is None or thrust_source is None:
        raise RuntimeError("Expected front-vault or forward-thrust source row is missing")

    if prop(front_vault, "ability_class").get_path_name() != FRONT_VAULT_GA_PATH:
        raise RuntimeError("The neutral Idle RTA row no longer targets the front-vault GA")
    if text(prop(front_vault, "source_state")) != IDLE or bool(prop(front_vault, "match_any_state")):
        raise RuntimeError("The neutral Idle RTA row has unexpected source routing")
    if text(prop(front_vault, "input_tag")) != RTA:
        raise RuntimeError("The neutral Idle RTA row has an unexpected input tag")
    if prop(thrust_source, "ability_class").get_path_name() != THRUST_GA_PATH:
        raise RuntimeError("ForwardY_TuCi no longer targets GA_IG_TuCi")
    if text(prop(thrust_source, "source_state")) != IDLE or text(prop(thrust_source, "input_tag")) != Y:
        raise RuntimeError("ForwardY_TuCi has unexpected source/input routing")

    # Build new rows before touching the asset, so reflection mismatches fail safely.
    directions = {
        "Forward": enum_member("FORWARD"),
        "Back": enum_member("BACK"),
        "Left": enum_member("LEFT"),
        "Right": enum_member("RIGHT"),
    }
    thrust_rows = {"Forward": thrust_source}
    for direction_name in ("Back", "Left", "Right"):
        new_id = direction_name + "Y_TuCi"
        existing = by_id.get(new_id)
        if existing is None:
            thrust_rows[direction_name] = clone_transition(
                thrust_source, new_id, directions[direction_name]
            )
        else:
            if (text(prop(existing, "source_state")) != IDLE
                    or text(prop(existing, "input_tag")) != Y
                    or prop(existing, "ability_class").get_path_name() != THRUST_GA_PATH
                    or prop(existing, "direction") != directions[direction_name]):
                raise RuntimeError("Existing directional thrust row conflicts: " + new_id)
            thrust_rows[direction_name] = existing

    changed = False

    # Remove exact Idle RTA routes for directional side/back vaults, plus the
    # old forward-only edge that would outrank the new neutral-direction edge.
    remove_ids = {OLD_FORWARD_VAULT_ID, OLD_BACK_VAULT_ID}
    remove_ids.update(
        text(prop(row, "transition_id")) for row in rows
        if not bool(prop(row, "match_any_state"))
        and text(prop(row, "source_state")) == IDLE
        and text(prop(row, "input_tag")) == RTA
        and prop(row, "direction") in (
            directions["Back"], directions["Left"], directions["Right"]
        )
    )
    old_count = len(rows)
    rows = [row for row in rows if text(prop(row, "transition_id")) not in remove_ids]
    changed |= len(rows) != old_count

    # Any-state directional vaults remain available from their other states,
    # but may not bypass the single Idle -> front-vault route.
    for row in rows:
        if (bool(prop(row, "match_any_state"))
                and text(prop(row, "input_tag")) == RTA
                and prop(row, "direction") in (
                    directions["Back"], directions["Left"], directions["Right"]
                )):
            blocked = list(prop(row, "blocked_source_states"))
            if not any(text(value) == IDLE for value in blocked):
                blocked.append(unreal.Name(IDLE))
                row.set_editor_property("blocked_source_states", blocked)
                changed = True

    # Keep one Idle front-vault GA, selected for every input direction.
    if prop(front_vault, "direction") != enum_member("NONE"):
        front_vault.set_editor_property("direction", enum_member("NONE"))
        changed = True
    if float(prop(front_vault, "max_correction_angle")) != 180.0:
        front_vault.set_editor_property("max_correction_angle", 180.0)
        changed = True

    # Add missing directional thrust edges, copying every authored field from the
    # forward row so tags, policies, target state and priority stay identical.
    present_ids = {text(prop(row, "transition_id")) for row in rows}
    for direction_name in ("Back", "Left", "Right"):
        row = thrust_rows[direction_name]
        if text(prop(row, "transition_id")) not in present_ids:
            rows.append(row)
            present_ids.add(text(prop(row, "transition_id")))
            changed = True
    for row in thrust_rows.values():
        row.set_editor_property("max_correction_angle", 180.0)

    if changed:
        combo.modify()
        combo.set_editor_property("transitions", rows)
        if not unreal.EditorAssetLibrary.save_loaded_asset(combo, only_if_is_dirty=False):
            raise RuntimeError("Failed to save DA_IG_Combo")

    # Assert the saved table. A fresh commandlet process performs the disk readback.
    combo = unreal.load_asset(COMBO_PATH)
    rows = list(prop(combo, "transitions"))
    by_id = rows_by_id(rows)
    front_vault = by_id[FORWARD_VAULT_ID]
    idle_rta = [row for row in rows
                if text(prop(row, "source_state")) == IDLE
                and text(prop(row, "input_tag")) == RTA]
    if len(idle_rta) != 1 or text(prop(idle_rta[0], "transition_id")) != FORWARD_VAULT_ID:
        raise RuntimeError("Idle RTA must have only the neutral front-vault edge")
    if text(prop(front_vault, "direction")) != "<DirectionalInput.NONE: 0>":
        raise RuntimeError("Idle front-vault edge is not Direction=None")
    if float(prop(front_vault, "max_correction_angle")) != 180.0:
        raise RuntimeError("Idle front-vault correction angle is not 180 degrees")

    for direction_name in ("Back", "Left", "Right"):
        matches = [row for row in rows
                   if bool(prop(row, "match_any_state"))
                   and text(prop(row, "input_tag")) == RTA
                   and prop(row, "direction") == directions[direction_name]]
        if len(matches) != 1 or not any(
                text(value) == IDLE for value in prop(matches[0], "blocked_source_states")):
            raise RuntimeError("ANY " + direction_name + " vault is still allowed from Idle")

    idle_y = [row for row in rows
              if text(prop(row, "source_state")) == IDLE
              and text(prop(row, "input_tag")) == Y
              and prop(row, "ability_class")
              and prop(row, "ability_class").get_path_name() == THRUST_GA_PATH]
    for direction_name in directions:
        expected = directions[direction_name]
        found = [row for row in idle_y if prop(row, "direction") == expected]
        if len(found) != 1 or float(prop(found[0], "max_correction_angle")) != 180.0:
            raise RuntimeError("Idle thrust route missing or misconfigured: " + direction_name)

    neutral_thrust = [row for row in rows
                      if text(prop(row, "source_state")) == IDLE
                      and text(prop(row, "input_tag")) == Y
                      and text(prop(row, "transition_id")) == "Y_ShangLao"]
    if len(neutral_thrust) != 1 or text(prop(neutral_thrust[0], "direction")) != "<DirectionalInput.NONE: 0>":
        raise RuntimeError("Neutral Y -> ShangLao route changed unexpectedly")

    thrust_cdo = unreal.get_default_object(prop(thrust_source, "ability_class"))
    thrust_entry_by_transition = named_map(
        prop(thrust_cdo, "entry_section_by_transition_id")
    )
    thrust_entry_by_source = named_map(prop(thrust_cdo, "entry_section_by_source_state"))
    thrust_default_entry = text(prop(thrust_cdo, "default_entry_section"))

    report.update({
        "success": True,
        "changed": changed,
        "combo_path": COMBO_PATH,
        "idle_rta": [describe(row) for row in idle_rta],
        "idle_y_thrust": [describe(row) for row in idle_y],
        "idle_any_directional_vaults_blocked": [
            describe(row) for row in rows
            if bool(prop(row, "match_any_state"))
            and text(prop(row, "input_tag")) == RTA
            and prop(row, "direction") in (
                directions["Back"], directions["Left"], directions["Right"]
            )
        ],
        "neutral_y_route": describe(neutral_thrust[0]),
        "thrust_montage_entry_by_transition": thrust_entry_by_transition,
        "thrust_montage_entry_by_source_state": thrust_entry_by_source,
        "thrust_montage_default_entry": thrust_default_entry,
    })
except Exception as exc:  # noqa: BLE001 - preserve a machine-readable failure record
    report["errors"].append(type(exc).__name__ + ": " + str(exc))

os.makedirs(os.path.dirname(REPORT_PATH), exist_ok=True)
with open(REPORT_PATH, "w", encoding="utf-8") as handle:
    json.dump(report, handle, ensure_ascii=False, indent=2, default=repr)

if not report["success"]:
    unreal.log_error("[IdleVaultRoutes] Failed: {}".format(report["errors"]))
else:
    unreal.log("[IdleVaultRoutes] Applied and verified DA_IG_Combo: {}".format(REPORT_PATH))
