"""Deprecated: UE 5.6 Python does not expose WidgetBlueprint.widget_tree."""

raise SystemExit(
    "Use run_m6_ui_remote.py --probe for the supported read-only asset/ref audit."
)

import unreal


ASSET_PATHS = (
    "/Game/UI/HUD/WBP_HUD",
    "/Game/UI/HUD/WBP_HealthBar",
    "/Game/UI/HUD/WBP_StaminaBar",
    "/Game/UI/HUD/WBP_Crosshair",
    "/Game/UI/InsectGlaive/WBP_IG_ResourcePanel",
)


def describe(path):
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if asset is None:
        return {"asset": path, "loaded": False}

    result = {
        "asset": path,
        "loaded": True,
        "class": asset.get_class().get_name(),
        "generated_class": str(asset.generated_class()),
    }
    generated_class = asset.generated_class()
    try:
        cdo = unreal.get_default_object(generated_class)
        result["cdo_widget_methods"] = [
            name for name in dir(cdo) if "widget" in name.lower()
        ]
        result["cdo_slots"] = {}
        for field in (
            "health_bar_slot", "stamina_bar_slot", "crosshair_slot",
            "weapon_resource_slot",
        ):
            try:
                value = cdo.get_editor_property(field)
                result["cdo_slots"][field] = str(value)
            except Exception as exc:
                result["cdo_slots"][field] = repr(exc)
        try:
            result["cdo_named_widgets"] = {}
            for name in (
                "HealthBarSlot", "StaminaBarSlot", "CrosshairSlot",
                "WeaponResourceSlot", "RootCanvas",
            ):
                found = cdo.get_widget_from_name(unreal.Name(name))
                result["cdo_named_widgets"][name] = str(found)
        except Exception as exc:
            result["cdo_named_widgets_error"] = repr(exc)
    except Exception as exc:
        result["cdo_error"] = repr(exc)
    try:
        tree = asset.get_editor_property("widget_tree")
    except Exception as exc:
        result["tree_error"] = repr(exc)
        result["asset_properties"] = [
            name for name in dir(asset) if "tree" in name.lower()
        ]
        return result

    result["tree"] = str(tree)
    if tree is not None:
        try:
            root = tree.get_editor_property("root_widget")
            result["root"] = str(root)
            result["root_name"] = root.get_name() if root else None
            result["root_class"] = root.get_class().get_name() if root else None
        except Exception as exc:
            result["root_error"] = repr(exc)
        result["tree_methods"] = [
            name for name in dir(tree)
            if "widget" in name.lower() or "all" in name.lower()
        ]
        try:
            widgets = tree.get_all_widgets()
            result["widgets"] = [
                (widget.get_name(), widget.get_class().get_name(),
                 bool(widget.get_editor_property("is_variable")))
                for widget in widgets
            ]
        except Exception as exc:
            result["widgets_error"] = repr(exc)

    return result


for asset_path in ASSET_PATHS:
    unreal.log("[E6.3WidgetAudit] " + repr(describe(asset_path)))

hud_class = unreal.EditorAssetLibrary.load_asset(
    "/Game/Blueprints/HUD/BP_MHGZHUD"
).generated_class()
hud_cdo = unreal.get_default_object(hud_class)
unreal.log("[E6.3WidgetAudit] HUD refs " + repr({
    field: str(hud_cdo.get_editor_property(field))
    for field in (
        "main_hud_class", "health_bar_class", "stamina_bar_class",
        "crosshair_class",
    )
}))
