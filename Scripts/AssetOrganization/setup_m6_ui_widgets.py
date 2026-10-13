"""Create and wire the E6.3 HUD Widget Blueprints in UE 5.6.

Run while the project editor is open, through UE Python Remote Execution:
    C:/apps/anaconda/python.exe Scripts/AssetOrganization/run_m6_ui_remote.py

UMG's WidgetTree is not exposed by the UE Python API in this engine version.
The native MHGZUIAssetSetupLibrary editor bridge builds the Designer trees;
this script owns asset creation, compilation, defaults, references, and saving.
It never replaces an existing WBP or changes camera configuration.
"""

import os

import unreal


WIDGETS = {
    "WBP_HUD": ("/Game/UI/HUD", "/Script/MHGZ.MHGZMainHUDWidget"),
    "WBP_HealthBar": ("/Game/UI/HUD", "/Script/MHGZ.MHGZAttributeBarWidget"),
    "WBP_StaminaBar": ("/Game/UI/HUD", "/Script/MHGZ.MHGZAttributeBarWidget"),
    "WBP_Crosshair": ("/Game/UI/HUD", "/Script/MHGZ.MHGZCrosshairWidget"),
    "WBP_IG_ResourcePanel": (
        "/Game/UI/InsectGlaive",
        "/Script/MHGZ.MHGZInsectGlaiveResourceWidget",
    ),
}


def log(message):
    unreal.log("[E6.3WidgetSetup] " + str(message))


def require_asset(path):
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if asset is None:
        raise RuntimeError("Required asset could not be loaded: " + path)
    return asset


def require_native_class(path):
    cls = unreal.load_class(None, path)
    if cls is None:
        raise RuntimeError("Required native class is not loaded: " + path)
    return cls


def find_interrupted_widget_set():
    """Resume only a complete, unsaved set created by an interrupted run."""
    present = {
        name: unreal.EditorAssetLibrary.does_asset_exist(folder + "/" + name)
        for name, (folder, _) in WIDGETS.items()
    }
    if not any(present.values()):
        return None
    if not all(present.values()):
        raise RuntimeError(
            "Only part of the E6.3 Widget Blueprint set exists in the editor; "
            "refusing to modify a partial set: " + repr(present)
        )

    content_dir = unreal.Paths.project_content_dir()
    disk_paths = {}
    for name, (folder, _) in WIDGETS.items():
        relative_folder = folder.removeprefix("/Game/").replace("/", os.sep)
        disk_paths[name] = os.path.join(
            content_dir, relative_folder, name + ".uasset"
        )
    persisted = {name: os.path.isfile(path) for name, path in disk_paths.items()}
    if any(persisted.values()):
        raise RuntimeError(
            "Widget assets already exist on disk; refusing to overwrite them: "
            + repr({name: disk_paths[name] for name, exists in persisted.items() if exists})
        )

    assets = {
        name: require_asset(folder + "/" + name)
        for name, (folder, _) in WIDGETS.items()
    }
    if any(not isinstance(asset, unreal.WidgetBlueprint) for asset in assets.values()):
        raise RuntimeError("An in-memory E6.3 asset is not a Widget Blueprint")
    if any(asset.generated_class() is None for asset in assets.values()):
        raise RuntimeError("An in-memory E6.3 Widget Blueprint is not compiled")
    log("Resuming the complete unsaved Widget Blueprint set from this session")
    return assets


def create_widget_asset(asset_tools, name, folder, parent_class):
    asset_path = folder + "/" + name
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        raise RuntimeError("Refusing to overwrite existing widget: " + asset_path)
    factory = unreal.WidgetBlueprintFactory()
    factory.set_editor_property("parent_class", parent_class)
    blueprint = asset_tools.create_asset(
        name, folder, unreal.WidgetBlueprint, factory
    )
    if blueprint is None:
        raise RuntimeError("Widget Blueprint creation failed: " + asset_path)
    if not unreal.MHGZUIAssetSetupLibrary.build_e63_widget_blueprint(
        blueprint, unreal.Name(name)
    ):
        raise RuntimeError("Native Designer-tree setup failed: " + asset_path)
    return blueprint


def compile_and_save_blueprint(blueprint):
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    generated_class = blueprint.generated_class()
    if generated_class is None:
        raise RuntimeError("Widget Blueprint has no generated class: "
                           + blueprint.get_name())
    return generated_class


def save_asset(asset):
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, False):
        raise RuntimeError("Failed to save asset: " + asset.get_path_name())


def main():
    # A normal persisted asset is never overwritten. The only resume case is
    # the complete unsaved set produced by the immediately preceding failed run.
    interrupted_assets = find_interrupted_widget_set()

    hud_asset = require_asset("/Game/Blueprints/HUD/BP_MHGZHUD")
    runtime_asset = require_asset(
        "/Game/Weapons/InsectGlaive/Data/DA_WeaponRuntime_IG"
    )
    game_mode = require_asset("/Game/Blueprints/GameModes/Demo/BP_Demo_GameMode")
    hud_class = hud_asset.generated_class()
    game_mode_class = game_mode.generated_class()
    if hud_class is None or game_mode_class is None:
        raise RuntimeError("Existing HUD or demo GameMode has no generated class")

    game_mode_cdo = unreal.get_default_object(game_mode_class)
    configured_hud_class = game_mode_cdo.get_editor_property("hud_class")
    if configured_hud_class is not None and configured_hud_class != hud_class:
        raise RuntimeError("Demo GameMode already references a different HUD; "
                           "nothing was modified")

    hud_cdo = unreal.get_default_object(hud_class)
    hud_fields = (
        "main_hud_class", "health_bar_class", "stamina_bar_class",
        "crosshair_class",
    )
    occupied_fields = [
        field for field in hud_fields
        if hud_cdo.get_editor_property(field) is not None
    ]
    if occupied_fields:
        raise RuntimeError("BP_MHGZHUD already has UI class references "
                           "({}); refusing to overwrite them".format(
                               ", ".join(occupied_fields)))
    if runtime_asset.get_editor_property("resource_widget_class") is not None:
        raise RuntimeError("DA_WeaponRuntime_IG already has a resource widget "
                           "class; refusing to overwrite it")

    for folder in sorted({folder for folder, _ in WIDGETS.values()}):
        if not unreal.EditorAssetLibrary.does_directory_exist(folder):
            unreal.EditorAssetLibrary.make_directory(folder)

    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    native_classes = {
        parent_path: require_native_class(parent_path)
        for _, parent_path in WIDGETS.values()
    }
    created = interrupted_assets or {}
    if not interrupted_assets:
        for name, (folder, parent_path) in WIDGETS.items():
            created[name] = create_widget_asset(
                asset_tools, name, folder, native_classes[parent_path]
            )

    widget_classes = {
        name: compile_and_save_blueprint(blueprint)
        for name, blueprint in created.items()
    }
    unreal.get_default_object(widget_classes["WBP_HealthBar"]).set_editor_property(
        "bar_kind", unreal.MHGZAttributeBarKind.HEALTH
    )
    unreal.get_default_object(widget_classes["WBP_StaminaBar"]).set_editor_property(
        "bar_kind", unreal.MHGZAttributeBarKind.STAMINA
    )

    hud_cdo.set_editor_property("main_hud_class", widget_classes["WBP_HUD"])
    hud_cdo.set_editor_property("health_bar_class", widget_classes["WBP_HealthBar"])
    hud_cdo.set_editor_property("stamina_bar_class", widget_classes["WBP_StaminaBar"])
    hud_cdo.set_editor_property("crosshair_class", widget_classes["WBP_Crosshair"])
    runtime_asset.set_editor_property(
        "resource_widget_class", widget_classes["WBP_IG_ResourcePanel"]
    )
    if configured_hud_class is None:
        game_mode_cdo.set_editor_property("hud_class", hud_class)

    # Save only after every tree, class, and reference has validated.
    for blueprint in created.values():
        save_asset(blueprint)
    save_asset(hud_asset)
    save_asset(runtime_asset)
    if configured_hud_class is None:
        save_asset(game_mode)

    log("Created and wired five E6.3 Widget Blueprints into existing "
        "BP_MHGZHUD and DA_WeaponRuntime_IG. Camera configuration untouched.")


main()
