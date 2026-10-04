"""Read-only audit of the live Kinsect Slash and training-dummy asset defaults."""

import os

import unreal


RESULTS = []


def field(obj, name):
    if obj is None:
        return "<missing object>"
    try:
        return obj.get_editor_property(name)
    except Exception as exc:
        return "<{}: {}>".format(name, exc)


def show(label, value):
    RESULTS.append("{}={}".format(label, value))
    unreal.log("[SlashDummyAudit] {}={}".format(label, value))


dummy = unreal.EditorAssetLibrary.load_asset(
    "/Game/Monster/TrainingDummy/Data/DA_TrainingDummy")
show("dummy", dummy)
for index, zone in enumerate(field(dummy, "hitzones")):
    for name in ("bone_name", "hitzone_tag", "extract_color_tag",
                 "relative_location", "radius"):
        show("zone[{}].{}".format(index, name), field(zone, name))

combat = unreal.EditorAssetLibrary.load_asset(
    "/Game/Weapons/InsectGlaive/Data/DA_IG_Combat")
for name in ("aerial_fall_gravity_scale", "white_aerial_fall_gravity_scale",
             "aerial_fall_braking_deceleration", "kinsect_slash_max_distance",
             "kinsect_slash_dash_duration",
             "kinsect_slash_probe_radius", "kinsect_slash_probe_forward_offset",
             "kinsect_slash_probe_max_step", "kinsect_slash_late_hit_window"):
    show("combat." + name, field(combat, name))

ability_class = unreal.EditorAssetLibrary.load_blueprint_class(
    "/Game/Weapons/InsectGlaive/Abilities/GA_IG_KinsectSlash")
ability = unreal.get_default_object(ability_class) if ability_class else None
show("slash_ability", ability)
for index, segment in enumerate(field(ability, "attack_segments")):
    show("segment[{}].hitzone_query_tag".format(index),
         field(field(segment, "collision"), "hitzone_query_tag"))
    show("segment[{}].damage_effect_class".format(index),
         field(field(segment, "damage"), "damage_effect_class"))

dummy_class = unreal.EditorAssetLibrary.load_blueprint_class(
    "/Game/Blueprints/Monster/TrainingDummy/BP_TrainingDummy")
dummy_cdo = unreal.get_default_object(dummy_class) if dummy_class else None
show("dummy_blueprint", dummy_cdo)
show("dummy_blueprint.config", field(dummy_cdo, "dummy_config"))

output_dir = os.path.join(unreal.Paths.project_saved_dir(), "AssetProbe")
os.makedirs(output_dir, exist_ok=True)
with open(os.path.join(output_dir, "kinsect_slash_dummy.txt"), "w", encoding="utf-8") as handle:
    handle.write("\n".join(RESULTS) + "\n")
