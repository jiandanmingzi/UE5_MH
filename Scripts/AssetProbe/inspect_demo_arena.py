"""Read actor placement, bounds, collision, materials, and lights in L_DemoArena.

Run with UnrealEditor-Cmd -run=pythonscript -script=Scripts/AssetProbe/inspect_demo_arena.py.
The result is written under Saved/AssetProbe because this commandlet suppresses stdout.
"""

import json
from pathlib import Path

import unreal


PROJECT_DIR = Path(unreal.Paths.project_dir())
REPORT = PROJECT_DIR / "Saved" / "AssetProbe" / "demo_arena_inspection.json"
MAP_PATH = "/Game/Maps/L_DemoArena"


def serial(value):
    if value is None or isinstance(value, (str, int, float, bool)):
        return value
    if all(hasattr(value, name) for name in ("x", "y", "z")):
        result = {"x": value.x, "y": value.y, "z": value.z}
        if hasattr(value, "w"):
            result["w"] = value.w
        return result
    return str(value)


def prop(obj, name):
    try:
        return serial(obj.get_editor_property(name))
    except Exception as exc:
        return "<{}: {}>".format(type(exc).__name__, exc)


def inspect_component(component):
    result = {
        "name": component.get_name(),
        "class": component.get_class().get_path_name(),
    }
    class_name = component.get_class().get_name()
    if class_name == "StaticMeshComponent":
        for name in ("relative_location", "relative_rotation", "relative_scale3d",
                     "static_mesh", "visible", "hidden_in_game", "cast_shadow"):
            result[name] = prop(component, name)
        try:
            result["collision_profile"] = str(component.get_collision_profile_name())
            result["collision_enabled"] = str(component.get_collision_enabled())
        except Exception as exc:
            result["collision_query_error"] = "{}: {}".format(type(exc).__name__, exc)
    elif class_name in ("DirectionalLightComponent", "SkyLightComponent"):
        for name in ("mobility", "intensity"):
            result[name] = prop(component, name)
    try:
        origin, extent = component.get_component_bounds()
        result["bounds_origin"] = serial(origin)
        result["bounds_extent"] = serial(extent)
    except Exception:
        pass
    return result


world = unreal.EditorLoadingAndSavingUtils.load_map(MAP_PATH)
if world is None:
    raise RuntimeError("Could not load {}".format(MAP_PATH))

actors = unreal.EditorLevelLibrary.get_all_level_actors()
rows = []
floor_bounds = []
for actor in actors:
    label = str(actor.get_actor_label())
    if label.startswith("Arena_Floor_"):
        try:
            origin, extent = actor.get_actor_bounds(False, False)
            floor_bounds.append((origin, extent))
        except Exception:
            pass
    is_dummy = isinstance(actor, unreal.MHGZTrainingDummy)
    if not (label.startswith("Arena_Wall_") or label == "Arena_AirCeiling"
            or label == "Arena_DirectionalLight" or label == "Arena_SkyLight"
            or is_dummy or isinstance(actor, unreal.PlayerStart)):
        continue
    try:
        origin, extent = actor.get_actor_bounds(False, False)
    except Exception:
        origin, extent = None, None
    try:
        components = actor.get_components_by_class(unreal.ActorComponent)
    except Exception:
        components = []
    row = {
        "name": actor.get_name(),
        "label": label,
        "class": actor.get_class().get_path_name(),
        "location": serial(actor.get_actor_location()),
        "rotation": serial(actor.get_actor_rotation()),
        "scale": serial(actor.get_actor_scale3d()),
        "bounds_origin": serial(origin),
        "bounds_extent": serial(extent),
        "components": [inspect_component(component) for component in components
                       if component.get_class().get_name() in
                       ("StaticMeshComponent", "DirectionalLightComponent", "SkyLightComponent")],
    }
    if is_dummy:
        row["dummy_config"] = prop(actor, "dummy_config")
        row["infinite_health"] = prop(actor, "infinite_health")
        capsule = actor.get_component_by_class(unreal.CapsuleComponent)
        row["capsule"] = {"radius": capsule.get_unscaled_capsule_radius(),
                          "half_height": capsule.get_unscaled_capsule_half_height(),
                          "collision_profile": str(capsule.get_collision_profile_name())}
    rows.append(row)

REPORT.parent.mkdir(parents=True, exist_ok=True)
if floor_bounds:
    xs = [v for origin, extent in floor_bounds for v in (origin.x - extent.x, origin.x + extent.x)]
    ys = [v for origin, extent in floor_bounds for v in (origin.y - extent.y, origin.y + extent.y)]
    floor_summary = {"tiles": len(floor_bounds), "min_x": min(xs), "max_x": max(xs),
                     "min_y": min(ys), "max_y": max(ys)}
else:
    floor_summary = {"tiles": 0}
REPORT.write_text(json.dumps({"map": MAP_PATH, "actor_count": len(actors),
                             "floor": floor_summary, "actors": rows},
                             ensure_ascii=False, indent=2), encoding="utf-8")
