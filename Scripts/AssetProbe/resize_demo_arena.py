"""Expand L_DemoArena and turn its boundary walls into invisible blockers.

This is an editor-only, guarded map migration. It rewrites the existing 10x10
floor tile grid from a 2 km square to a 6 km square, moves/scales the existing
four wall meshes while preserving their collision, and adds a hidden cube mesh
as an invisible ceiling blocker. A copy of the original map must exist under
Saved/Backups before running this script.
"""

import json
import re
from pathlib import Path

import unreal


PROJECT_DIR = Path(unreal.Paths.project_dir())
MAP_PATH = "/Game/Maps/L_DemoArena"
BACKUP = PROJECT_DIR / "Saved" / "Backups" / "DemoArenaLayout20260928" / "L_DemoArena.umap"
REPORT = PROJECT_DIR / "Saved" / "AssetProbe" / "demo_arena_resize.json"
CUBE_PATH = "/Engine/BasicShapes/Cube.Cube"


def vec(value):
    return {"x": value.x, "y": value.y, "z": value.z}


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def set_location(actor, x, y, z):
    actor.set_actor_location(unreal.Vector(x, y, z), False, True)


world = None
report = {"map": MAP_PATH, "backup": str(BACKUP), "saved": False}
try:
    require(BACKUP.is_file(), "The pre-edit L_DemoArena backup is missing; refusing to modify the map.")
    world = unreal.EditorLoadingAndSavingUtils.load_map(MAP_PATH)
    require(world is not None, "Could not load {}".format(MAP_PATH))

    actors = unreal.EditorLevelLibrary.get_all_level_actors()
    floor_by_index = {}
    walls = {}
    for actor in actors:
        label = actor.get_actor_label()
        floor_match = re.fullmatch(r"Arena_Floor_(\d{2})_(\d{2})", label)
        if floor_match:
            floor_by_index[(int(floor_match.group(1)), int(floor_match.group(2)))] = actor
        elif label in ("Arena_Wall_North", "Arena_Wall_South", "Arena_Wall_East", "Arena_Wall_West"):
            walls[label] = actor

    expected_indices = {(row, col) for row in range(10) for col in range(10)}
    require(set(floor_by_index) == expected_indices,
            "Expected exactly one tile for every Arena_Floor row/column in a 10x10 grid.")
    require(set(walls) == {"Arena_Wall_North", "Arena_Wall_South", "Arena_Wall_East", "Arena_Wall_West"},
            "Expected all four named arena wall meshes.")
    ceiling = next((actor for actor in actors if str(actor.get_actor_label()) == "Arena_AirCeiling"), None)

    cube = unreal.load_asset(CUBE_PATH)
    require(cube is not None, "Missing {} collision mesh.".format(CUBE_PATH))

    # The 100 floor cubes are 200 units wide at 200-unit intervals. Re-space
    # them to 600 units and scale to 600 units so their combined bounds meet.
    for (row, col), actor in floor_by_index.items():
        actor.modify()
        set_location(actor, -2700.0 + col * 600.0, -2700.0 + row * 600.0, -10.0)
        actor.set_actor_scale3d(unreal.Vector(6.0, 6.0, 0.2))

    wall_specs = {
        "Arena_Wall_North": ((0.0, 3025.0, 2250.0), (60.0, 0.5, 45.0)),
        "Arena_Wall_South": ((0.0, -3025.0, 2250.0), (60.0, 0.5, 45.0)),
        "Arena_Wall_East": ((3025.0, 0.0, 2250.0), (0.5, 60.0, 45.0)),
        "Arena_Wall_West": ((-3025.0, 0.0, 2250.0), (0.5, 60.0, 45.0)),
    }
    for label, actor in walls.items():
        actor.modify()
        location, scale = wall_specs[label]
        set_location(actor, *location)
        actor.set_actor_scale3d(unreal.Vector(*scale))
        for component in actor.get_components_by_class(unreal.StaticMeshComponent):
            component.set_editor_property("cast_shadow", False)
            # Hidden rendering does not disable the StaticMeshComponent collision.
            component.set_visibility(False, True)
        actor.set_actor_hidden_in_game(True)

    if ceiling is None:
        ceiling = unreal.EditorLevelLibrary.spawn_actor_from_class(
            unreal.StaticMeshActor, unreal.Vector(0.0, 0.0, 4525.0), unreal.Rotator(0.0, 0.0, 0.0))
        require(ceiling is not None, "Could not spawn the invisible ceiling blocker.")
        ceiling.set_actor_label("Arena_AirCeiling")
    ceiling.modify()
    ceiling.set_actor_scale3d(unreal.Vector(60.0, 60.0, 0.5))
    components = ceiling.get_components_by_class(unreal.StaticMeshComponent)
    require(len(components) == 1, "Expected one StaticMeshComponent on the ceiling blocker.")
    ceiling_mesh = components[0]
    ceiling_mesh.set_static_mesh(cube)
    ceiling_mesh.set_collision_profile_name("BlockAll")
    ceiling_mesh.set_editor_property("cast_shadow", False)
    ceiling_mesh.set_visibility(False, True)
    ceiling.set_actor_hidden_in_game(True)

    require(unreal.EditorLevelLibrary.save_current_level(), "UE failed to save the modified map.")
    report.update({
        "saved": True,
        "floor_tile_count": len(floor_by_index),
        "floor_center_min": [-2700.0, -2700.0, -10.0],
        "floor_center_max": [2700.0, 2700.0, -10.0],
        "floor_bounds_xy": [-3000.0, 3000.0],
        "wall_labels": sorted(walls),
        "wall_mesh_rendering_hidden": True,
        "wall_collision_preserved": True,
        "wall_top_z": 4500.0,
        "ceiling_label": str(ceiling.get_actor_label()),
        "ceiling_location": vec(ceiling.get_actor_location()),
        "ceiling_scale": vec(ceiling.get_actor_scale3d()),
        "ceiling_mesh": cube.get_path_name(),
        "ceiling_mesh_collision_profile": str(ceiling_mesh.get_collision_profile_name()),
        "ceiling_rendering_hidden": True,
    })
except Exception as exc:
    report["error"] = "{}: {}".format(type(exc).__name__, exc)

REPORT.parent.mkdir(parents=True, exist_ok=True)
REPORT.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
if report.get("saved") is not True:
    raise RuntimeError(report.get("error", "Map resize did not complete."))
