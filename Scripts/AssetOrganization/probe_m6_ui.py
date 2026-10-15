"""Read-only M6 UI asset/reference probe; does not inspect unexposed WidgetTree."""

import json
from pathlib import Path
import sys
import unreal

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from ue_asset_utils import blueprint_cdo


def main():
    widget_paths = {
        "main_hud_class": "/Game/UI/HUD/WBP_HUD",
        "health_bar_class": "/Game/UI/HUD/WBP_HealthBar",
        "stamina_bar_class": "/Game/UI/HUD/WBP_StaminaBar",
        "crosshair_class": "/Game/UI/HUD/WBP_Crosshair",
        "resource_widget_class": "/Game/UI/InsectGlaive/WBP_IG_ResourcePanel",
    }
    report = {"schema_version": 1, "mode": "AuditOnly", "assets": {}, "references": {}, "errors": []}
    bridge = getattr(unreal, "MHGZUIAssetSetupLibrary", None)
    report["ui_tree_builder_available"] = callable(getattr(bridge, "build_e63_widget_blueprint", None))
    for field, path in widget_paths.items():
        asset = unreal.EditorAssetLibrary.load_asset(path)
        generated = asset.generated_class() if asset is not None else None
        report["assets"][field] = {"path": path, "class": str(generated)}
        if generated is None:
            report["errors"].append("Missing/uncompiled widget: " + path)

    sources = (
        ("/Game/Blueprints/HUD/BP_MHGZHUD", tuple(widget_paths)[:4], True),
        ("/Game/Weapons/InsectGlaive/Data/DA_WeaponRuntime_IG", ("resource_widget_class",), False),
        ("/Game/Blueprints/GameModes/Demo/BP_Demo_GameMode", ("hud_class",), True),
    )
    expected_hud = unreal.EditorAssetLibrary.load_asset(sources[0][0])
    expected_hud_class = expected_hud.generated_class() if expected_hud is not None else None
    for path, fields, blueprint in sources:
        try:
            asset = unreal.EditorAssetLibrary.load_asset(path)
            obj = blueprint_cdo(asset, unreal) if blueprint else asset
            for field in fields:
                value = obj.get_editor_property(field)
                report["references"][path + ":" + field] = str(value)
                if value is None:
                    report["errors"].append("Unwired reference: " + path + ":" + field)
                elif field in widget_paths and str(value) != report["assets"][field]["class"]:
                    report["errors"].append("Unexpected widget reference: " + path + ":" + field)
                elif field == "hud_class" and value != expected_hud_class:
                    report["errors"].append("GameMode uses an unexpected HUD class: " + path)
        except Exception as exc:
            report["errors"].append(path + ": " + repr(exc))
    unreal.log("[M6UIProbe] " + json.dumps(report, ensure_ascii=False))
    if report["errors"]:
        raise RuntimeError("M6 UI probe failed: " + "; ".join(report["errors"]))


if __name__ == "__main__":
    main()
