"""Read-only asset probe for the M5 aerial/back-vault audit.

Run headless:

    UnrealEditor-Cmd.exe <abs>/MHGZ.uproject -run=pythonscript \
        -script="Scripts/AssetProbe/probe_m5_assets.py" \
        -unattended -nop4 -nosplash -stdout

It NEVER saves, renames or modifies an asset. Every section is wrapped so a
missing engine API degrades to a printed note instead of aborting the run, and
each section prints what it could not read.

What it answers
---------------
A8  why the back-vault apex reaches only 443.9 cm instead of the configured
    579.7: are the BackVaultTrajectory keys normalised to a peak below 1.0, or
    does the apex fall before StartProgress?  Also quantifies the recorded
    lateral (Y) amplitude, which Rise says should be near zero.
A9  whether AM_IG_AerialFall / the fall source sequences carry a root-bone
    offset in the pose.  The Host disables root-motion *extraction* only, so a
    non-zero root still displaces the mesh.
A7  the live values of every DanceVault* field, and whether this DataAsset
    overrides the C++ class defaults at all.
"""

from __future__ import annotations

import json
import os
import traceback

import unreal


OUT_DIR = os.path.join(unreal.Paths.project_saved_dir(), "AssetProbe")
OUT_JSON = os.path.join(OUT_DIR, "m5_assets.json")

GA_BACK_VAULT = "/Game/Weapons/InsectGlaive/Abilities/GA_IG_HouChengGanTiao"
GA_COUNTER = "/Game/Blueprints/Ability/InsectGlaive/GA_IG_TuJinHuiXuanZhan"
DA_COMBAT = "/Game/Weapons/InsectGlaive/Data/DA_IG_Combat"
DA_COMBO = "/Game/Weapons/InsectGlaive/Data/DA_IG_Combo"
DA_INPUT = "/Game/Weapons/InsectGlaive/Data/DA_IG_InputProfile"

MONTAGE_DIR = "/Game/Weapons/InsectGlaive/Anims/Montage"
AERIAL_MONTAGES = ["AM_IG_HouChengGanTiao", "AM_IG_HouChengGanTiao_W",
                   "AM_IG_AerialFall", "AM_IG_AerialFall_W", "AM_IG_AerialLanding"]

SEQ_DIR = "/Game/Weapons/InsectGlaive/Anims/Sequences/Imported"
ROOT_TRACK_SEQUENCES = ["AS_Unsh_Fall", "AS_Unsh_Fall_Loop", "AS_Unsh_Fall_W_Jump",
                        "AS_Unsh_WuTa", "AS_Unsh_Fall_Higher", "AS_Unsh_Fall_Junp",
                        # The back-vault montage is rebuilt at runtime from these
                        # four, so their extraction settings -- not the montage's --
                        # decide whether the Jump segment lifts the capsule at all.
                        # The binary name-presence shortcut is useless here: the
                        # property name is serialised for every sequence regardless
                        # of value (AS_Unsh_Fall contains it and is False).
                        "AS_Unsh_Jump_Back", "AS_Unsh_Jump_Over_Back",
                        "AS_Unsh_W_Jump_Back", "AS_Unsh_W_Jump_Over_Back"]

REPORT = {"sections": {}, "notes": []}


def note(message):
    REPORT["notes"].append(message)
    unreal.log("[probe] {}".format(message))


def section(name, fn):
    try:
        REPORT["sections"][name] = fn()
    except Exception as exc:  # noqa: BLE001 - probe must survive any API gap
        REPORT["sections"][name] = {"error": str(exc)}
        note("{} failed: {}".format(name, exc))
        unreal.log_error("[probe] {} failed:\n{}".format(name, traceback.format_exc()))


def prop(obj, name, default=None):
    """Read a snake_case editor property, returning default when absent."""
    try:
        return obj.get_editor_property(name)
    except Exception:  # noqa: BLE001
        return default


def load(path):
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        return None
    return unreal.EditorAssetLibrary.load_asset(path)


def load_cdo(blueprint_path):
    """Class default object of a Blueprint's generated class, or None."""
    if not unreal.EditorAssetLibrary.does_asset_exist(blueprint_path):
        return None
    bp = unreal.EditorAssetLibrary.load_asset(blueprint_path)
    cls = unreal.EditorAssetLibrary.load_blueprint_class(blueprint_path) if bp else None
    if cls is None:
        return None
    return unreal.get_default_object(cls)


def vector_dict(value):
    if value is None:
        return None
    try:
        return {"x": float(value.x), "y": float(value.y), "z": float(value.z)}
    except Exception:  # noqa: BLE001
        return repr(value)


# --------------------------------------------------------------------------
# Back-vault trajectory keys  (A8)
# --------------------------------------------------------------------------
def probe_back_vault():
    out = {}
    for path, label in ((GA_BACK_VAULT, "normal"),):
        cdo = load_cdo(path)
        if cdo is None:
            out[label] = {"error": "blueprint or class not found"}
            continue
        entry = {
            "class": cdo.get_class().get_name(),
            "attack_montage": None,
            "white_attack_montage": None,
        }
        for field in ("attack_montage", "white_attack_montage"):
            value = prop(cdo, field)
            entry[field] = value.get_path_name() if value else None

        for field, holder in (("back_jump_duration", "scalars"),
                              ("back_jump_over_duration", "scalars"),
                              ("white_back_jump_duration", "scalars"),
                              ("white_back_jump_over_duration", "scalars")):
            entry[field] = prop(cdo, field)

        for field in ("back_vault_trajectory", "white_back_vault_trajectory"):
            keys = prop(cdo, field)
            if keys is None:
                entry[field] = {"error": "property not readable"}
                continue
            samples = []
            for key in keys:
                samples.append({
                    "time": float(prop(key, "time", 0.0)),
                    "pos": vector_dict(prop(key, "normalized_position")),
                })
            summary = summarize_keys(samples)
            entry[field] = {"count": len(samples), "summary": summary,
                            "samples": samples}
        out[label] = entry
    return out


def summarize_keys(samples):
    """Where the recorded path peaks, in the key frame's own normalised space."""
    if not samples:
        return {}
    summary = {}
    for axis, name in ((0, "x"), (1, "y"), (2, "z")):
        values = [(s["time"], s["pos"][name]) for s in samples
                  if s["pos"] is not None]
        if not values:
            continue
        peak_t, peak_v = max(values, key=lambda p: p[1])
        trough_t, trough_v = min(values, key=lambda p: p[1])
        summary[name] = {
            "min": trough_v, "min_time": trough_t,
            "max": peak_v, "max_time": peak_t,
        }
    return summary


# --------------------------------------------------------------------------
# Montage structure and blend settings
# --------------------------------------------------------------------------
def probe_montage(name):
    path = "{}/{}".format(MONTAGE_DIR, name)
    montage = load(path)
    if montage is None:
        return {"error": "not found"}
    entry = {
        "play_length": float(montage.get_play_length()),
        "enable_auto_blend_out": prop(montage, "enable_auto_blend_out"),
        "blend_in": prop(montage, "blend_in", None),
        "blend_out": prop(montage, "blend_out", None),
        "sections": [],
        "slots": [],
    }
    try:
        for section in montage.get_editor_property("composite_sections"):
            entry["sections"].append({
                "name": str(section.get_editor_property("section_name")),
                "start_time": float(section.get_editor_property("start_time")),
                "next_section": str(section.get_editor_property("next_section_name")),
            })
    except Exception as exc:  # noqa: BLE001
        entry["sections"] = {"error": str(exc)}
    try:
        for track in montage.get_editor_property("slot_anim_tracks"):
            entry["slots"].append(str(track.get_editor_property("slot_name")))
    except Exception as exc:  # noqa: BLE001
        entry["slots"] = {"error": str(exc)}
    return entry


# --------------------------------------------------------------------------
# Root bone track of the fall clips  (A9)
# --------------------------------------------------------------------------
def probe_sequence_root(seq_name):
    path = "{}/{}".format(SEQ_DIR, seq_name)
    seq = load(path)
    if seq is None:
        return {"error": "not found"}
    entry = {
        "play_length": float(seq.get_play_length()),
        # A non-1.0 rate scale shortens the *effective* duration, which is why a
        # 3.233 s sequence can become a 1.617 s dynamic montage.
        "rate_scale": prop(seq, "rate_scale"),
        "enable_root_motion": prop(seq, "enable_root_motion"),
        "root_motion_root_lock": str(prop(seq, "root_motion_root_lock")),
    }

    # UAnimSequence exposes its data model under several names across versions.
    model = None
    for accessor in ("data_model", "get_data_model"):
        try:
            candidate = getattr(seq, accessor)
            model = candidate() if callable(candidate) else candidate
        except Exception:  # noqa: BLE001
            model = None
        if model is not None:
            entry["data_model_accessor"] = accessor
            break
    if model is None:
        entry["data_model"] = None
        entry["sequence_api"] = sorted(
            n for n in dir(seq) if not n.startswith("_"))[:80]
        return entry

    entry["num_frames"] = prop(model, "number_of_frames")
    entry["data_model_api"] = sorted(
        n for n in dir(model) if not n.startswith("_"))[:80]

    root_name = None
    try:
        names = model.get_bone_track_names()
        entry["bone_track_count"] = len(names)
        if names:
            # The root is the first track in the skeleton's track order.
            root_name = str(names[0])
            entry["root_bone"] = root_name
            entry["first_tracks"] = [str(n) for n in list(names)[:4]]
    except Exception as exc:  # noqa: BLE001
        entry["bone_track_names"] = {"error": str(exc)}
        return entry

    if root_name is None:
        return entry
    try:
        track = model.get_bone_track(root_name)
        positions = track.get_editor_property("pos_keys") if track else None
        if positions is None:
            entry["root_positions"] = {"error": "pos_keys not readable"}
            return entry
        values = [(float(p.time), float(p.x), float(p.y), float(p.z))
                  for p in positions]
        entry["root_position_key_count"] = len(values)
        if values:
            entry["root_positions"] = {
                "x": {"min": min(v[1] for v in values), "max": max(v[1] for v in values)},
                "y": {"min": min(v[2] for v in values), "max": max(v[2] for v in values)},
                "z": {"min": min(v[3] for v in values), "max": max(v[3] for v in values)},
                "first": values[0], "last": values[-1],
                "max_abs_xy": max(max(abs(v[1]), abs(v[2])) for v in values),
            }
    except Exception as exc:  # noqa: BLE001
        entry["root_positions"] = {"error": str(exc)}
    return entry


# --------------------------------------------------------------------------
# DataAssets
# --------------------------------------------------------------------------
def probe_combat_config():
    asset = load(DA_COMBAT)
    if asset is None:
        return {"error": "not found"}
    fields = ["dance_vault_ballistic_mode", "dance_vault_apex_height",
              "dance_vault_duration", "dance_vault_distance",
              "dance_vault_launch_velocity",
              "back_vault_duration", "back_vault_distance", "back_vault_apex_height",
              "white_back_vault_duration", "white_back_vault_distance",
              "white_back_vault_apex_height", "back_vault_free_fall_handoff_progress",
              "white_back_vault_free_fall_handoff_progress",
              "max_dance_stacks", "dance_damage_multipliers",
              "aerial_fall_gravity_scale", "white_aerial_fall_gravity_scale",
              "aerial_fall_braking_deceleration"]
    return {f: normalise(prop(asset, f, "<absent>")) for f in fields}


def normalise(value):
    if value is None or isinstance(value, (bool, int, float, str)):
        return value
    try:
        return [normalise(v) for v in value]
    except TypeError:
        return vector_dict(value) or repr(value)


def probe_serialized_props():
    """Which properties this DataAsset actually serialises (vs class default)."""
    asset = load(DA_COMBAT)
    if asset is None:
        return {"error": "not found"}
    try:
        export = unreal.EditorAssetLibrary.find_asset_data(DA_COMBAT)
        return {"package": export.package_name if export else None}
    except Exception as exc:  # noqa: BLE001
        return {"error": str(exc)}


def probe_combo():
    asset = load(DA_COMBO)
    if asset is None:
        return {"error": "not found"}
    out = {"fields": [n for n in dir(asset) if not n.startswith("_")][:60]}
    for field in ("transitions", "edges", "entries"):
        value = prop(asset, field, "<absent>")
        if value != "<absent>":
            try:
                out[field] = [describe_row(r) for r in value]
            except Exception as exc:  # noqa: BLE001
                out[field] = {"error": str(exc)}
    return out


def describe_row(row):
    fields = ["source_state", "target_state", "transition_id", "activation_ability",
              "landing_policy", "auto_transition", "required_tags", "blocked_tags"]
    return {f: repr(prop(row, f, "<absent>"))[:160] for f in fields}


def probe_input_profile():
    asset = load(DA_INPUT)
    if asset is None:
        return {"error": "not found"}
    out = {}
    for field in ("chords", "entries", "bindings"):
        value = prop(asset, field, "<absent>")
        if value != "<absent>":
            try:
                out[field] = [describe_row(r) for r in value]
            except Exception as exc:  # noqa: BLE001
                out[field] = {"error": str(exc)}
    if not out:
        out["fields"] = [n for n in dir(asset) if not n.startswith("_")][:60]
    return out


def probe_counter_ability():
    cdo = load_cdo(GA_COUNTER)
    if cdo is None:
        return {"error": "not found"}
    fields = ["dance_vault_sequence", "dance_vault_animation_play_rate",
              "dance_vault_blend_in_time", "dance_vault_blend_out_time",
              "dance_vault_montage_slot"]
    return {f: repr(prop(cdo, f, "<absent>"))[:200] for f in fields}


def main():
    os.makedirs(OUT_DIR, exist_ok=True)

    section("back_vault_ability", probe_back_vault)
    section("counter_ability", probe_counter_ability)
    section("combat_config", probe_combat_config)
    section("combo", probe_combo)
    section("input_profile", probe_input_profile)

    montages = {}
    for name in AERIAL_MONTAGES:
        section("montage:{}".format(name), lambda n=name: probe_montage(n))
        montages[name] = REPORT["sections"]["montage:{}".format(name)]

    for name in ROOT_TRACK_SEQUENCES:
        section("sequence:{}".format(name), lambda n=name: probe_sequence_root(n))

    with open(OUT_JSON, "w", encoding="utf-8") as handle:
        json.dump(REPORT, handle, indent=2, ensure_ascii=False, default=repr)

    unreal.log("[probe] wrote {}".format(OUT_JSON))
    print("PROBE_DONE {}".format(OUT_JSON))


main()
