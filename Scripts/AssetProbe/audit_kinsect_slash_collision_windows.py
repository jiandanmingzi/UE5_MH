"""Read-only inspection of the saved Kinsect Slash montage windows and config.

Run with UnrealEditor-Cmd and PythonScriptPlugin enabled:

    UnrealEditor-Cmd.exe MHGZ.uproject -run=pythonscript \
        -script="Scripts/AssetProbe/audit_kinsect_slash_collision_windows.py" \
        -unattended -nop4 -nosplash -stdout

This script never saves assets. It writes only a JSON report under
Saved/AssetProbe so exact notify timing can be reused by the migration tool.
"""

from __future__ import annotations

import json
import os

import unreal


MONTAGES = {
    "main": "/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_CaoChongZhan",
    "dance": "/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_CaoChongZhan_Dance",
}
ABILITY = "/Game/Weapons/InsectGlaive/Abilities/GA_IG_CaoChongZhan"
COMBAT = "/Game/Weapons/InsectGlaive/Data/DA_IG_Combat"
GLAIVE = "/Game/Weapons/InsectGlaive/Meshes/Glaive/SKM_IG_Glaive"
OUT_PATH = os.path.join(unreal.Paths.project_saved_dir(), "AssetProbe",
                        "kinsect_slash_collision_windows.json")


def prop(obj, name, default=None):
    try:
        return obj.get_editor_property(name)
    except Exception:
        return default


def property_probe(obj, name):
    try:
        value = obj.get_editor_property(name)
        try:
            length = len(value)
        except Exception:
            length = None
        return {"value": repr(value)[:500], "type": str(type(value)), "length": length}
    except Exception as exc:
        return {"error": str(exc)}


def object_path(value):
    if value is None:
        return None
    try:
        return value.get_path_name()
    except Exception:
        return str(value)


def class_path(value):
    if value is None:
        return None
    try:
        return value.get_class().get_path_name()
    except Exception:
        return str(value)


def call_time(event, method_name, property_name):
    method = getattr(event, method_name, None)
    if callable(method):
        try:
            return float(method())
        except Exception:
            pass
    value = prop(event, property_name)
    try:
        return float(value)
    except Exception:
        return None


def describe_notify(event):
    notify_state = prop(event, "notify_state_class")
    notify = prop(event, "notify")
    state_object = notify_state or notify
    config_index = prop(state_object, "config_index")
    return {
        "name": str(prop(event, "notify_name", "")),
        "notify_class": class_path(notify_state) if notify_state else class_path(notify),
        "config_index": int(config_index) if isinstance(config_index, int) else None,
        "start_seconds": call_time(event, "get_trigger_time", "trigger_time"),
        "end_seconds": call_time(event, "get_end_trigger_time", "end_trigger_time"),
        "track_index": prop(event, "track_index"),
        "available_time_api": [name for name in dir(event)
                               if "trigger_time" in name.lower()],
    }


def member_names(obj, token):
    if obj is None:
        return []
    return [name for name in dir(obj) if token in name.lower()]


def read_notify_collection(obj, field_name):
    value = prop(obj, field_name)
    if value is None:
        return None
    try:
        return [describe_notify(event) for event in value]
    except Exception as exc:
        return {"error": str(exc), "type": str(type(value)), "repr": repr(value)}


def describe_segment(segment):
    damage = prop(segment, "damage")
    collision = prop(segment, "collision")
    regions = []
    for region in prop(collision, "trace_regions", []) or []:
        regions.append({
            "start_socket": str(prop(region, "start_socket_name", "")),
            "end_socket": str(prop(region, "end_socket_name", "")),
            "radius": prop(region, "radius"),
            "max_sample_spacing": prop(region, "max_sample_spacing"),
        })
    return {
        "damage_effect": object_path(prop(damage, "damage_effect_class")),
        "motion_value": prop(damage, "motion_value"),
        "requires_hit_to_continue": prop(damage, "requires_hit_to_continue"),
        "multi_hit_count": prop(segment, "multi_hit_count"),
        "multi_hit_policy": str(prop(segment, "multi_hit_policy", "")),
        "trace_mesh_component_tag": str(prop(collision, "trace_mesh_component_tag", "")),
        "hitzone_query_tag": str(prop(collision, "hitzone_query_tag", "")),
        "trace_regions": regions,
    }


report = {"read_only": True, "montages": {}, "segments": [], "combat": {},
          "glaive_sockets": [], "errors": []}

for label, asset_path in MONTAGES.items():
    montage = unreal.EditorAssetLibrary.load_asset(asset_path)
    if montage is None:
        report["errors"].append("Missing montage: " + asset_path)
        continue
    report["montages"][label] = {
        "path": montage.get_path_name(),
        "play_length": float(montage.get_play_length()),
        "notifies": read_notify_collection(montage, "notifies"),
        "notify_related_api": member_names(montage, "notif"),
        "track_related_api": member_names(montage, "track"),
        "slot_anim_tracks": [],
        "property_probes": {
            field: property_probe(montage, field)
            for field in ("notifies", "slot_anim_tracks", "composite_sections")
        },
    }
    for slot_track in prop(montage, "slot_anim_tracks", []) or []:
        anim_track = prop(slot_track, "anim_track")
        report["montages"][label]["slot_anim_tracks"].append({
            "slot_name": str(prop(slot_track, "slot_name", "")),
            "track_type": str(type(anim_track)),
            "track_notify_api": member_names(anim_track, "notif"),
            "track_members": member_names(anim_track, "track"),
            "notifies": read_notify_collection(anim_track, "notifies"),
        })
    try:
        library = unreal.AnimationBlueprintLibrary
        events = library.get_animation_notify_events(montage)
        report["montages"][label]["animation_library_events"] = {
            "count": len(events),
            "items": [{
                "type": str(type(event)),
                "members": member_names(event, "time") + member_names(event, "notify"),
                "repr": repr(event)[:400],
            } for event in events],
        }
    except Exception as exc:
        report["montages"][label]["animation_library_events"] = {"error": str(exc)}

ability_class = unreal.EditorAssetLibrary.load_blueprint_class(ABILITY)
ability = unreal.get_default_object(ability_class) if ability_class else None
if ability is None:
    report["errors"].append("Missing ability Blueprint/class: " + ABILITY)
else:
    report["segments"] = [describe_segment(segment)
                          for segment in (prop(ability, "attack_segments", []) or [])]
    report["ability_montage"] = object_path(prop(ability, "attack_montage"))
    report["dance_montage"] = object_path(prop(ability, "dance_montage"))
    report["ability_notify_api"] = member_names(ability, "notif")

combat = unreal.EditorAssetLibrary.load_asset(COMBAT)
if combat is None:
    report["errors"].append("Missing combat config: " + COMBAT)
else:
    report["combat"] = {
        "path": combat.get_path_name(),
        "kinsect_slash_attach_socket": str(prop(combat, "kinsect_slash_attach_socket", "<property unavailable>")),
        "legacy_probe_radius": prop(combat, "kinsect_slash_probe_radius"),
    }

glaive = unreal.EditorAssetLibrary.load_asset(GLAIVE)
if glaive is None:
    report["errors"].append("Missing glaive mesh: " + GLAIVE)
else:
    report["glaive_socket_related_api"] = member_names(glaive, "socket")
    report["glaive_socket_properties"] = {
        field: (str(type(prop(glaive, field))) if prop(glaive, field) is not None else None)
        for field in ("sockets", "skeleton", "reference_skeleton")
    }
    try:
        report["glaive_has_IG_Kinsect"] = glaive.find_socket("IG_Kinsect") is not None
    except Exception as exc:
        report["glaive_has_IG_Kinsect"] = "query failed: " + str(exc)
    for socket in prop(glaive, "sockets", []) or []:
        report["glaive_sockets"].append(str(prop(socket, "socket_name", socket)))

os.makedirs(os.path.dirname(OUT_PATH), exist_ok=True)
with open(OUT_PATH, "w", encoding="utf-8") as handle:
    json.dump(report, handle, indent=2, ensure_ascii=False, default=str)

unreal.log("[KinsectSlashWindowAudit] wrote " + OUT_PATH)
unreal.log("[KinsectSlashWindowAudit] " + json.dumps(report, ensure_ascii=False, default=str))
print("KINSECT_SLASH_WINDOW_AUDIT_DONE " + OUT_PATH)
