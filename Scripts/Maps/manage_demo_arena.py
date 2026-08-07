"""Create and verify the Demo arena, then remove superseded template maps.

Default execution is read-only. Use ``--create`` to create the arena and
``--delete-old`` only after DefaultEngine.ini points at the new map and the
delete backup has been prepared.
"""

from __future__ import annotations

import hashlib
import json
import os
import sys
import traceback
from datetime import datetime, timezone

import unreal


TARGET_MAP = "/Game/Maps/L_DemoArena"
GAME_MODE = "/Game/Blueprints/GameModes/BP_Demo_GameMode"
TRAINING_DUMMY = "/Game/Blueprints/Monster/BP_TrainingDummy"
CUBE_MESH = "/Engine/BasicShapes/Cube.Cube"
FLOOR_MATERIAL = "/Game/LevelPrototyping/Materials/MI_PrototypeGrid_Gray"
WALL_MATERIAL = "/Game/LevelPrototyping/Materials/MI_PrototypeGrid_TopDark"
BACKUP_DIRECTORY = "Saved/AssetOrganizationBackup/phase10-map-cleanup-pre-delete"

OLD_MAPS = [
    "/Game/Map/NewMap",
    "/Game/ThirdPerson/Lvl_ThirdPerson",
]
EXTERNAL_ROOTS = [
    "/Game/__ExternalActors__/ThirdPerson/Lvl_ThirdPerson",
    "/Game/__ExternalActors__/Variant_Combat/Lvl_Combat",
    "/Game/__ExternalActors__/Variant_Platforming/Lvl_Platforming",
    "/Game/__ExternalActors__/Variant_SideScrolling/Lvl_SideScrolling",
    "/Game/__ExternalObjects__/ThirdPerson/Lvl_ThirdPerson",
    "/Game/__ExternalObjects__/Variant_Combat/Lvl_Combat",
    "/Game/__ExternalObjects__/Variant_Platforming/Lvl_Platforming",
    "/Game/__ExternalObjects__/Variant_SideScrolling/Lvl_SideScrolling",
]
EXPECTED_EXTERNAL_PACKAGE_COUNT = 507
SPECIAL_CLEANUP = [
    "/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple",
    "/Game/ThirdPerson/MI_ThirdPersonColWay",
]

CREATE = "--create" in sys.argv
DELETE_OLD = "--delete-old" in sys.argv
if CREATE and DELETE_OLD:
    raise RuntimeError("Use --create and --delete-old in separate editor runs")


def _log(message: str) -> None:
    unreal.log(f"[DemoArena] {message}")


def _asset_exists(path: str) -> bool:
    return unreal.EditorAssetLibrary.does_asset_exist(path)


def _load_required(path: str):
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if asset is None:
        raise RuntimeError(f"Required asset is missing: {path}")
    return asset


def _load_blueprint_class(path: str):
    loaded_class = unreal.EditorAssetLibrary.load_blueprint_class(path)
    if loaded_class is None:
        raise RuntimeError(f"Required Blueprint class is missing: {path}")
    return loaded_class


def _set_property_if_available(target, name: str, value) -> None:
    try:
        target.set_editor_property(name, value)
    except Exception as exc:
        _log(f"Optional property skipped: {target}.{name}: {exc}")


def _spawn_static_mesh(actor_subsystem, mesh, material, label, location, scale, folder):
    actor = actor_subsystem.spawn_actor_from_class(
        unreal.StaticMeshActor, location, unreal.Rotator(0.0, 0.0, 0.0)
    )
    if actor is None:
        raise RuntimeError(f"Failed to spawn static mesh actor: {label}")
    actor.set_actor_label(label)
    actor.set_folder_path(folder)
    actor.set_actor_scale3d(scale)
    component = actor.get_editor_property("static_mesh_component")
    component.set_static_mesh(mesh)
    if material is not None:
        component.set_material(0, material)
    return actor


def _current_world():
    editor_subsystem = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    world = editor_subsystem.get_editor_world()
    if world is None:
        raise RuntimeError("No editor world is available")
    return world


def _arena_actor_inventory(load_map: bool) -> dict:
    if not _asset_exists(TARGET_MAP):
        return {"exists": False, "actor_count": 0, "labels": []}
    level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if load_map and not level_subsystem.load_level(TARGET_MAP):
        raise RuntimeError(f"Failed to load target map: {TARGET_MAP}")
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    labels = sorted(actor.get_actor_label() for actor in actors if actor is not None)
    required_labels = {
        "Arena_PlayerStart",
        "Arena_TrainingDummy",
        "Arena_Wall_North",
        "Arena_Wall_South",
        "Arena_Wall_East",
        "Arena_Wall_West",
    }
    missing_labels = sorted(required_labels - set(labels))
    floor_tile_count = sum(1 for label in labels if label.startswith("Arena_Floor_"))
    return {
        "exists": True,
        "actor_count": len(labels),
        "floor_tile_count": floor_tile_count,
        "missing_required_labels": missing_labels,
        "labels": labels,
    }


def _create_arena() -> None:
    if _asset_exists(TARGET_MAP):
        inventory = _arena_actor_inventory(True)
        if inventory["floor_tile_count"] != 100 or inventory["missing_required_labels"]:
            raise RuntimeError(f"Existing arena is incomplete: {inventory}")
        _log("Arena already exists and is complete.")
        return

    cube = _load_required(CUBE_MESH)
    floor_material = _load_required(FLOOR_MATERIAL)
    wall_material = _load_required(WALL_MATERIAL)
    dummy_class = _load_blueprint_class(TRAINING_DUMMY)
    game_mode_class = _load_blueprint_class(GAME_MODE)

    level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not hasattr(level_subsystem, "new_level"):
        raise RuntimeError("LevelEditorSubsystem.new_level is unavailable")
    if not level_subsystem.new_level(TARGET_MAP):
        raise RuntimeError(f"Failed to create level: {TARGET_MAP}")

    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    tile_size_cm = 200.0
    half_grid = 4.5
    for row in range(10):
        for column in range(10):
            x = (column - half_grid) * tile_size_cm
            y = (row - half_grid) * tile_size_cm
            _spawn_static_mesh(
                actor_subsystem,
                cube,
                floor_material,
                f"Arena_Floor_{row:02d}_{column:02d}",
                unreal.Vector(x, y, -10.0),
                unreal.Vector(2.0, 2.0, 0.2),
                "Arena/Geometry/Floor",
            )

    wall_specs = [
        ("Arena_Wall_North", unreal.Vector(0.0, 1025.0, 150.0), unreal.Vector(20.0, 0.5, 3.0)),
        ("Arena_Wall_South", unreal.Vector(0.0, -1025.0, 150.0), unreal.Vector(20.0, 0.5, 3.0)),
        ("Arena_Wall_East", unreal.Vector(1025.0, 0.0, 150.0), unreal.Vector(0.5, 20.0, 3.0)),
        ("Arena_Wall_West", unreal.Vector(-1025.0, 0.0, 150.0), unreal.Vector(0.5, 20.0, 3.0)),
    ]
    for label, location, scale in wall_specs:
        _spawn_static_mesh(
            actor_subsystem,
            cube,
            wall_material,
            label,
            location,
            scale,
            "Arena/Geometry/Walls",
        )

    player_start = actor_subsystem.spawn_actor_from_class(
        unreal.PlayerStart,
        unreal.Vector(-700.0, 0.0, 110.0),
        unreal.Rotator(0.0, 0.0, 0.0),
    )
    if player_start is None:
        raise RuntimeError("Failed to spawn PlayerStart")
    player_start.set_actor_label("Arena_PlayerStart")
    player_start.set_folder_path("Arena/Gameplay")

    dummy = actor_subsystem.spawn_actor_from_class(
        dummy_class,
        unreal.Vector(450.0, 0.0, 0.0),
        unreal.Rotator(0.0, 180.0, 0.0),
    )
    if dummy is None:
        raise RuntimeError("Failed to spawn BP_TrainingDummy")
    dummy.set_actor_label("Arena_TrainingDummy")
    dummy.set_folder_path("Arena/Gameplay")

    directional = actor_subsystem.spawn_actor_from_class(
        unreal.DirectionalLight,
        unreal.Vector(0.0, 0.0, 800.0),
        unreal.Rotator(-45.0, -35.0, 0.0),
    )
    directional.set_actor_label("Arena_DirectionalLight")
    directional.set_folder_path("Arena/Lighting")
    directional_component = directional.get_component_by_class(unreal.DirectionalLightComponent)
    if directional_component is not None:
        _set_property_if_available(directional_component, "intensity", 8.0)
        _set_property_if_available(directional_component, "atmosphere_sun_light", True)

    skylight = actor_subsystem.spawn_actor_from_class(
        unreal.SkyLight, unreal.Vector(), unreal.Rotator()
    )
    skylight.set_actor_label("Arena_SkyLight")
    skylight.set_folder_path("Arena/Lighting")
    skylight_component = skylight.get_component_by_class(unreal.SkyLightComponent)
    if skylight_component is not None:
        _set_property_if_available(skylight_component, "real_time_capture", True)

    atmosphere = actor_subsystem.spawn_actor_from_class(
        unreal.SkyAtmosphere, unreal.Vector(), unreal.Rotator()
    )
    atmosphere.set_actor_label("Arena_SkyAtmosphere")
    atmosphere.set_folder_path("Arena/Lighting")

    fog = actor_subsystem.spawn_actor_from_class(
        unreal.ExponentialHeightFog, unreal.Vector(0.0, 0.0, -100.0), unreal.Rotator()
    )
    fog.set_actor_label("Arena_HeightFog")
    fog.set_folder_path("Arena/Lighting")

    world_settings = _current_world().get_world_settings()
    _set_property_if_available(world_settings, "kill_z", -2000.0)
    _set_property_if_available(world_settings, "default_game_mode", game_mode_class)

    if not level_subsystem.save_current_level():
        raise RuntimeError("Failed to save the Demo arena")
    inventory = _arena_actor_inventory(False)
    if inventory["floor_tile_count"] != 100 or inventory["missing_required_labels"]:
        raise RuntimeError(f"Created arena failed verification: {inventory}")
    _log(f"Arena created: actors={inventory['actor_count']}")


def _package_files_for_delete() -> list[str]:
    project_dir = os.path.abspath(unreal.Paths.project_dir())
    content_dir = os.path.join(project_dir, "Content")
    files = []
    for map_path in OLD_MAPS:
        files.append(
            os.path.join(content_dir, map_path[len("/Game/") :].replace("/", os.sep) + ".umap")
        )
    for root in EXTERNAL_ROOTS:
        root_dir = os.path.join(content_dir, root[len("/Game/") :].replace("/", os.sep))
        if os.path.isdir(root_dir):
            for current_root, _, names in os.walk(root_dir):
                for name in names:
                    if name.endswith(".uasset"):
                        files.append(os.path.join(current_root, name))
    for asset_path in SPECIAL_CLEANUP:
        files.append(
            os.path.join(content_dir, asset_path[len("/Game/") :].replace("/", os.sep) + ".uasset")
        )
    return sorted(set(os.path.abspath(path) for path in files))


def _sha256(path: str) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _verify_delete_backup(files: list[str]) -> None:
    project_dir = os.path.abspath(unreal.Paths.project_dir())
    content_dir = os.path.abspath(unreal.Paths.project_content_dir())
    backup_root = os.path.abspath(os.path.join(project_dir, BACKUP_DIRECTORY, "Content"))
    failures = []
    for source_file in files:
        if not os.path.isfile(source_file):
            failures.append(f"Missing delete source: {source_file}")
            continue
        relative = os.path.relpath(source_file, content_dir)
        backup_file = os.path.join(backup_root, relative)
        if not os.path.isfile(backup_file):
            failures.append(f"Missing delete backup: {backup_file}")
        elif _sha256(source_file) != _sha256(backup_file):
            failures.append(f"Delete backup hash mismatch: {source_file}")
    if failures:
        raise RuntimeError("Delete backup verification failed:\n" + "\n".join(failures))


def _external_package_count() -> int:
    return sum(
        len(unreal.EditorAssetLibrary.list_assets(root, recursive=True, include_folder=False))
        for root in EXTERNAL_ROOTS
    )


def _delete_old_content() -> None:
    if not _asset_exists(TARGET_MAP):
        raise RuntimeError("Target Demo arena does not exist")
    config_path = os.path.join(os.path.abspath(unreal.Paths.project_config_dir()), "DefaultEngine.ini")
    with open(config_path, "r", encoding="utf-8-sig") as handle:
        config_text = handle.read()
    expected_config_path = f"{TARGET_MAP}.{TARGET_MAP.rsplit('/', 1)[1]}"
    if expected_config_path not in config_text:
        raise RuntimeError("DefaultEngine.ini does not point to the Demo arena")

    external_count = _external_package_count()
    if external_count != EXPECTED_EXTERNAL_PACKAGE_COUNT:
        raise RuntimeError(
            f"External package count changed: expected={EXPECTED_EXTERNAL_PACKAGE_COUNT}, actual={external_count}"
        )
    files = _package_files_for_delete()
    if len(files) != EXPECTED_EXTERNAL_PACKAGE_COUNT + len(OLD_MAPS) + len(SPECIAL_CLEANUP):
        raise RuntimeError(f"Unexpected delete file count: {len(files)}")
    _verify_delete_backup(files)

    level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not level_subsystem.load_level(TARGET_MAP):
        raise RuntimeError("Failed to load the target map before deletion")

    failures = []
    for root in EXTERNAL_ROOTS:
        if unreal.EditorAssetLibrary.does_directory_exist(root):
            if not unreal.EditorAssetLibrary.delete_directory(root):
                failures.append(f"Failed to delete external root: {root}")
    for map_path in OLD_MAPS:
        if _asset_exists(map_path) and not unreal.EditorAssetLibrary.delete_asset(map_path):
            failures.append(f"Failed to delete old map: {map_path}")
    for asset_path in SPECIAL_CLEANUP:
        if _asset_exists(asset_path) and not unreal.EditorAssetLibrary.delete_asset(asset_path):
            failures.append(f"Failed to delete cleanup asset: {asset_path}")
    if failures:
        raise RuntimeError("Old content deletion failed:\n" + "\n".join(failures))

    remaining = [
        path
        for path in OLD_MAPS + SPECIAL_CLEANUP
        if _asset_exists(path)
    ]
    remaining_external = _external_package_count()
    if remaining or remaining_external:
        raise RuntimeError(
            f"Delete verification failed: remaining={remaining}, external={remaining_external}"
        )
    _log(f"Deleted old maps and external packages: files={len(files)}")


def _audit_state(registry) -> dict:
    old_maps = []
    for path in OLD_MAPS:
        old_maps.append(
            {
                "path": path,
                "exists": _asset_exists(path),
                "referencers": sorted(
                    str(value)
                    for value in unreal.EditorAssetLibrary.find_package_referencers_for_asset(
                        path, False
                    )
                )
                if _asset_exists(path)
                else [],
            }
        )
    special = []
    for path in SPECIAL_CLEANUP:
        asset_data = registry.get_assets_by_package_name(path)
        special.append(
            {
                "path": path,
                "exists": bool(asset_data),
                "asset_classes": [str(item.asset_class_path.asset_name) for item in asset_data],
                "referencers": sorted(
                    str(value)
                    for value in unreal.EditorAssetLibrary.find_package_referencers_for_asset(
                        path, False
                    )
                )
                if asset_data
                else [],
            }
        )
    return {
        "target_map": _arena_actor_inventory(True),
        "old_maps": old_maps,
        "external_package_count": _external_package_count(),
        "special_cleanup": special,
    }


def _write_report(state: dict, mode: str, outcome: str) -> str:
    report_dir = os.path.join(
        os.path.abspath(unreal.Paths.project_dir()), "Saved", "AssetOrganization"
    )
    os.makedirs(report_dir, exist_ok=True)
    report_path = os.path.join(report_dir, f"phase10-demo-arena-{mode}.json")
    report = {
        "mode": mode,
        "outcome": outcome,
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "target_map": TARGET_MAP,
        "arena_dimensions": {
            "tiles": [10, 10],
            "tile_size_cm": 200,
            "platform_size_cm": [2000, 2000],
            "wall_height_cm": 300,
        },
        "state": state,
    }
    with open(report_path, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(report, handle, ensure_ascii=False, indent=2)
        handle.write("\n")
    return report_path


def main() -> None:
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.wait_for_completion()
    mode = "audit"
    outcome = "ready"
    if CREATE:
        mode = "create"
        _create_arena()
        outcome = "created"
    elif DELETE_OLD:
        mode = "delete-old"
        _delete_old_content()
        outcome = "deleted"
    state = _audit_state(registry)
    report_path = _write_report(state, mode, outcome)
    _log(f"Result={outcome}; report={report_path}")


try:
    main()
except Exception:
    unreal.log_error("[DemoArena] Operation failed")
    unreal.log_error(traceback.format_exc())
    raise
