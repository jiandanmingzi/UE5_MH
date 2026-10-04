"""Read the live Blueprint CDO camera values without editing assets.

UnrealEditor-Cmd.exe MHGZ.uproject -run=pythonscript \
    -script=Scripts/AssetProbe/inspect_camera_defaults.py -unattended -nop4 -nosplash

The Python commandlet suppresses print/unreal.log, so the report is written to
Saved/_camera_defaults_probe.json.
"""

import json
from pathlib import Path

import unreal


PROJECT_DIR = Path(unreal.Paths.project_dir())
REPORT = PROJECT_DIR / "Saved" / "_camera_defaults_probe.json"


def value(obj, property_name):
    try:
        result = obj.get_editor_property(property_name)
    except Exception as exc:
        return {"error": f"{type(exc).__name__}: {exc}"}
    if hasattr(result, "x") and hasattr(result, "y") and hasattr(result, "z"):
        return {"x": result.x, "y": result.y, "z": result.z}
    if isinstance(result, (int, float, str, bool)) or result is None:
        return result
    return str(result)


def inspect_character(path):
    result = {"asset": path}
    cls = unreal.EditorAssetLibrary.load_blueprint_class(path)
    if cls is None:
        result["error"] = "Blueprint class not found"
        return result
    cdo = unreal.get_default_object(cls)
    for label, cdo_property, properties in (
        (
            "camera_boom",
            "camera_boom",
            ("target_arm_length", "target_offset", "socket_offset", "relative_location", "relative_rotation", "use_pawn_control_rotation"),
        ),
        (
            "follow_camera",
            "follow_camera",
            ("field_of_view", "relative_location", "relative_rotation", "use_pawn_control_rotation"),
        ),
    ):
        try:
            component = cdo.get_editor_property(cdo_property)
            result[label] = {name: value(component, name) for name in properties}
        except Exception as exc:
            result[label] = {"error": f"{type(exc).__name__}: {exc}"}
    return result


def inspect_controller(path):
    result = {"asset": path}
    cls = unreal.EditorAssetLibrary.load_blueprint_class(path)
    if cls is None:
        result["error"] = "Blueprint class not found"
        return result
    cdo = unreal.get_default_object(cls)
    camera_manager_class = cdo.get_editor_property("player_camera_manager_class")
    result["player_camera_manager_class"] = str(camera_manager_class)
    if camera_manager_class is not None:
        try:
            camera_manager_cdo = unreal.get_default_object(camera_manager_class)
            result["player_camera_manager_defaults"] = {
                "view_pitch_min": value(camera_manager_cdo, "view_pitch_min"),
                "view_pitch_max": value(camera_manager_cdo, "view_pitch_max"),
            }
        except Exception as exc:
            result["player_camera_manager_defaults"] = {
                "error": f"{type(exc).__name__}: {exc}"
            }
    return result


REPORT.parent.mkdir(parents=True, exist_ok=True)
REPORT.write_text(
    json.dumps(
        {
            "character": inspect_character("/Game/Blueprints/Characters/Demo/BP_IG_Character"),
            "controller": inspect_controller("/Game/Blueprints/PlayerController/Demo/BP_MHGZ_PlayerController"),
        },
        ensure_ascii=False,
        indent=2,
    ),
    encoding="utf-8",
)
