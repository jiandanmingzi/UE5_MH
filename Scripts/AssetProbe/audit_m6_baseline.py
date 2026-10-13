"""Read-only M6.1 baseline audit for weapon wiring and Insect Glaive assets.

Run the data-asset portion with the project's UE 5.6 Python commandlet:

    UnrealEditor-Cmd.exe MHGZ.uproject -run=pythonscript \
        -script="Scripts/AssetProbe/audit_m6_baseline.py" \
        -unattended -nop4 -nosplash -stdout

Writes a JSON report under Saved/M6Audit. For complete sampled-key, curve,
notify and import-source data, also run `-run=MHGZM6BaselineAudit`; Python does
not expose every protected UAnimSequence property. Neither audit saves assets.
"""

from __future__ import annotations

import json
import os
import traceback

import unreal


OUT_DIR = os.path.join(unreal.Paths.project_saved_dir(), "M6Audit")
OUT_JSON = os.path.join(OUT_DIR, "m6_baseline_audit.json")
ASSET_ROOT = "/Game/Weapons/InsectGlaive/Anims/Sequences/Imported"

DATA_ASSETS = {
    "weapon_runtime": "/Game/Weapons/InsectGlaive/Data/DA_WeaponRuntime_IG",
    "combat": "/Game/Weapons/InsectGlaive/Data/DA_IG_Combat",
    "input_profile": "/Game/Weapons/InsectGlaive/Data/DA_IG_InputProfile",
    "combo": "/Game/Weapons/InsectGlaive/Data/DA_IG_Combo",
}

SEQUENCE_IDS = {
    193: "AS_Unsh_JueChongJi",
    194: "AS_Unsh_JueChongJi_Hold",
    195: "AS_Unsh_JueChongJi_Send",
    196: "AS_Unsh_JueChongJi_Pose",
    199: "AS_Unsh_JueChongJi_Flying",
    206: "AS_Unsh_LieChongHuaJueChongJiFall",
    189: "AS_Unsh_CaoChongZhanJueChongJiWuTa",
}

COMBO_FIELDS = (
    "transition_id", "source_state", "match_any_state", "blocked_source_states",
    "input_tag", "direction", "execution_policy", "ability_class",
    "montage_blend_in_time", "max_correction_angle", "target_state",
    "state_policy", "landing_policy", "required_tags", "blocked_tags",
    "stamina_required", "requires_combo_window", "requires_dodge_accept_window",
    "granted_tags", "grant_timing", "auto_transition", "priority",
)

CHORD_FIELDS = (
    "output_tag", "trigger_controls", "required_held_modifiers",
    "required_context_tags", "blocked_context_tags", "require_exact_modifiers",
    "modifiers_must_precede_triggers", "priority", "dispatch_policy",
    "max_press_duration_seconds", "release_control_tag",
    "consume_trigger_controls", "chord_grace_period_override_seconds",
    "additional_release_control_tags", "aim_snapshot_context",
)

COMBAT_FIELDS = (
    "kinsect_data", "kinsect_attach_socket", "awakened_kinsect_motion_value",
    "awakened_pierce_hit_interval", "awakened_kinsect_max_distance",
    "awakened_hunter_flight_max_distance", "awakened_hunter_flight_start_time",
    "awakened_aim_correction_angle", "powder_gather_radius",
    "powder_gather_duration", "powder_gather_max_count", "powder_actor_class",
    "powder_lifetime", "powder_damage_contribution", "powder_vortex_base_damage",
    "powder_vortex_explosion_radius", "powder_vortex_base_stagger",
    "powder_vortex_hit_cue_tag",
)


def get(obj, name, default=None):
    try:
        return obj.get_editor_property(name)
    except Exception:  # noqa: BLE001 - missing UE property is audit evidence
        return default


def value(obj):
    if obj is None:
        return None
    if isinstance(obj, (bool, int, float, str)):
        return obj
    if isinstance(obj, unreal.Object):
        return obj.get_path_name()
    type_name = type(obj).__name__
    if type_name == "GameplayTag":
        tag_name = get(obj, "tag_name")
        return str(tag_name) if tag_name is not None else str(obj)
    if type_name == "GameplayTagContainer":
        tags = get(obj, "gameplay_tags")
        return [value(tag) for tag in (tags or [])]
    if isinstance(obj, (list, tuple)):
        return [value(item) for item in obj]
    items_method = getattr(obj, "items", None)
    if callable(items_method):
        try:
            return {str(value(key)): value(item) for key, item in items_method()}
        except Exception:  # noqa: BLE001 - unsupported map iteration is diagnostic data
            pass
    try:
        iterator = iter(obj)
    except TypeError:
        iterator = None
    if iterator is not None:
        try:
            return [value(item) for item in iterator]
        except Exception:  # noqa: BLE001 - unsupported array conversion is diagnostic data
            pass
    return str(obj)


def fields(obj, names):
    result = {}
    for name in names:
        item = get(obj, name)
        result[name] = value(item)
    return result


def source_files(asset):
    import_data = get(asset, "asset_import_data")
    if not import_data:
        return []
    source_data = get(import_data, "source_data")
    if source_data:
        try:
            return [str(path) for path in source_data.get_source_files()]
        except Exception:  # noqa: BLE001
            pass
    try:
        return [str(item) for item in import_data.extract_filenames()]
    except Exception:  # noqa: BLE001
        return []


def notify_rows(sequence):
    rows = []
    for notify in get(sequence, "notifies", []) or []:
        state_class = get(notify, "notify_state_class")
        notify_object = get(notify, "notify")
        rows.append({
            "time": value(get(notify, "time")),
            "duration": value(get(notify, "duration")),
            "track_index": value(get(notify, "track_index")),
            "notify_name": value(get(notify, "notify_name")),
            "notify_class": value(state_class or (notify_object.get_class() if notify_object else None)),
        })
    return rows


def audit_data_assets():
    out = {}
    for key, path in DATA_ASSETS.items():
        asset = unreal.EditorAssetLibrary.load_asset(path)
        if not asset:
            out[key] = {"path": path, "error": "asset not found"}
            continue
        entry = {"path": path, "class": asset.get_class().get_path_name()}
        if key == "weapon_runtime":
            entry.update(fields(asset, (
                "weapon_type_tag", "resource_component_class", "input_profile",
                "combat_config", "resource_widget_class")))
        elif key == "combat":
            entry.update(fields(asset, COMBAT_FIELDS))
        elif key == "input_profile":
            entry["settings"] = fields(asset, (
                "chord_grace_period", "direction_input_threshold", "forward_cone_half_angle"))
            entry["raw_action_to_physical_input_tag"] = value(
                get(asset, "raw_action_to_physical_input_tag", {}))
            entry["chords"] = [fields(chord, CHORD_FIELDS)
                               for chord in (get(asset, "chords", []) or [])]
        elif key == "combo":
            entry["settings"] = fields(asset, ("weapon_type_tag", "global_combo_timeout", "pre_input_lifetime"))
            entry["transitions"] = [fields(edge, COMBO_FIELDS)
                                    for edge in (get(asset, "transitions", []) or [])]
        out[key] = entry
    return out


def audit_sequences():
    assets = unreal.EditorAssetLibrary.list_assets(ASSET_ROOT, recursive=True, include_folder=False)
    wanted = set(SEQUENCE_IDS.values())
    found = {}
    for path in assets:
        leaf = path.rsplit("/", 1)[-1].split(".", 1)[0]
        if leaf not in wanted:
            continue
        asset = unreal.EditorAssetLibrary.load_asset(path)
        if not asset:
            continue
        skeleton = get(asset, "skeleton")
        root_lock = get(asset, "root_motion_root_lock")
        play_length = get(asset, "sequence_length")
        if play_length is None:
            play_length = getattr(asset, "get_play_length", lambda: None)()
        number_of_frames = getattr(asset, "get_number_of_frames", lambda: None)()
        curve_names = None
        curve_api = []
        animation_library = getattr(unreal, "AnimationLibrary", None)
        if animation_library is not None:
            curve_api = [name for name in dir(animation_library)
                         if "curve" in name.lower()]
            get_curves = getattr(animation_library, "get_curve_names", None)
            if get_curves:
                try:
                    curve_names = value(get_curves(asset))
                except Exception as exc:  # noqa: BLE001
                    curve_names = {"error": str(exc)}
        root_track = {"available": False}
        try:
            pose = asset.get_anim_pose_at_time(
                0.0, unreal.AnimPoseEvaluationOptions())
            bone_names = [str(name) for name in pose.get_bone_names()]
            root_name = next(name for name in bone_names if name.lower() == "root")
            animation_library = unreal.AnimationLibrary
            start = animation_library.get_bone_pose_for_time(
                asset, unreal.Name(root_name), 0.0, False)
            end_time = max(float(play_length) - 1.0e-4, 0.0)
            end = animation_library.get_bone_pose_for_time(
                asset, unreal.Name(root_name), end_time, False)
            start_xyz = [float(start.translation.x), float(start.translation.y),
                         float(start.translation.z)]
            end_xyz = [float(end.translation.x), float(end.translation.y),
                       float(end.translation.z)]
            root_track = {
                "available": True,
                "bone_name": root_name,
                "local_start_cm": start_xyz,
                "local_end_cm": end_xyz,
                "local_delta_cm": [end_xyz[i] - start_xyz[i] for i in range(3)],
            }
        except Exception as exc:  # noqa: BLE001 - retain the precise unsupported case
            root_track = {"available": False, "error": str(exc)}
        notifies = get(asset, "notifies")
        found.setdefault(leaf, []).append({
            "path": path,
            "class": asset.get_class().get_path_name(),
            "skeleton": value(skeleton),
            "sequence_length_seconds": value(play_length),
            "number_of_frames": value(number_of_frames),
            "rate_scale": value(get(asset, "rate_scale")),
            "enable_root_motion": value(get(asset, "enable_root_motion")),
            "force_root_lock": value(get(asset, "force_root_lock")),
            "root_motion_root_lock": value(root_lock),
            "notify_property_readable": notifies is not None,
            "notify_count": len(notifies or []),
            "notifies": notify_rows(asset),
            "curve_names": curve_names,
            "curve_api_candidates": curve_api,
            "root_track_local": root_track,
            "sequence_api_candidates": [name for name in dir(asset)
                                        if any(token in name.lower()
                                               for token in ("frame", "curve", "track", "notify", "model"))],
            "import_sources": source_files(asset),
        })
    return {
        str(asset_id): {"expected_name": name, "matches": found.get(name, [])}
        for asset_id, name in SEQUENCE_IDS.items()
    }


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    report = {
        "engine_version": unreal.SystemLibrary.get_engine_version(),
        "project": unreal.Paths.project_dir(),
        "read_only": True,
        "data_assets": audit_data_assets(),
        "animation_sequences": audit_sequences(),
    }
    with open(OUT_JSON, "w", encoding="utf-8") as output:
        json.dump(report, output, ensure_ascii=False, indent=2)
    unreal.log("[M6.1Audit] Wrote read-only asset report: {}".format(OUT_JSON))
    unreal.log("[M6.1Audit] Data assets: {}; sequence IDs: {}".format(
        len(report["data_assets"]), len(report["animation_sequences"])))


try:
    main()
except Exception as exc:  # noqa: BLE001
    unreal.log_error("[M6.1Audit] Failed: {}\n{}".format(exc, traceback.format_exc()))
    raise
