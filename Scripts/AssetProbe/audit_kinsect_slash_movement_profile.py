"""Read saved Kinsect Slash defaults after a cold build; never save assets."""

import json
import math
import os

import unreal


def path(value):
    return value.get_path_name() if value else None


config = unreal.load_asset("/Game/Weapons/InsectGlaive/Data/DA_IG_Combat")
ability_asset = unreal.load_asset("/Game/Blueprints/Ability/InsectGlaive/GA_IG_CaoChongZhan")
if config is None or ability_asset is None:
    raise RuntimeError("Missing existing CombatConfig or production Kinsect Slash blueprint")

ability = unreal.get_default_object(ability_asset.generated_class())
profile = config.get_editor_property("kinsect_slash_dash_profile")
fields = (
    "initial_hold_seconds", "straight_seconds", "glide_seconds", "straight_speed",
    "glide_initial_speed", "glide_gravity_magnitude", "glide_forward_acceleration",
)
values = {field: float(profile.get_editor_property(field)) for field in fields}
if not all(math.isfinite(value) and value >= 0 for value in values.values()):
    raise RuntimeError("Non-finite/negative measured movement profile")
if not config.get_editor_property("use_captured_kinsect_slash_dash"):
    raise RuntimeError("Existing CombatConfig opted out of the new measured profile")

report = {
    "asset": path(config),
    "ability": path(ability),
    "captured_dash_enabled": True,
    "profile": values,
    "duration_seconds": sum(values[field] for field in fields[:3]),
    "legacy_distance_curve_unused": path(config.get_editor_property("kinsect_slash_distance_curve")),
    "miss_gravity_magnitude": ability.get_editor_property("miss_gravity_magnitude"),
    "dance": {},
    "montages": {},
}
for field in ("dance_from_dash_tuning", "dance_from_miss_tuning"):
    tuning = ability.get_editor_property(field)
    report["dance"][field] = {
        name: tuning.get_editor_property(name) for name in (
            "use_captured_trajectory", "prep_duration", "flight_duration", "launch_up_speed",
            "launch_backward_speed", "gravity_magnitude", "backward_acceleration",
        )
    }
for field in ("attack_montage", "dance_montage"):
    montage = ability.get_editor_property(field)
    if not montage:
        raise RuntimeError("Missing existing " + field)
    report["montages"][field] = {
        "asset": path(montage),
        "play_length": float(montage.get_play_length()),
        "sections": [str(montage.get_section_name(index))
                     for index in range(montage.get_num_sections())],
    }

output = os.path.join(unreal.Paths.project_saved_dir(), "AssetProbe", "kinsect_slash_movement_profile.json")
os.makedirs(os.path.dirname(output), exist_ok=True)
with open(output, "w", encoding="utf-8") as stream:
    json.dump(report, stream, ensure_ascii=False, indent=2)
unreal.log("[KinsectSlashMovementAudit] " + json.dumps(report, ensure_ascii=False))
