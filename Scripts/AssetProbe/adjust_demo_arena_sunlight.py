"""Lower only the Demo Arena's directional sun intensity, with guarded readback.

Run from the project root with UnrealEditor-Cmd -run=pythonscript. The script
does not alter the SkyLight, exposure, post-process settings, or other actors.
"""

import json
import math
from pathlib import Path

import unreal


PROJECT_DIR = Path(unreal.Paths.project_dir())
MAP_PATH = "/Game/Maps/L_DemoArena"
ACTOR_LABEL = "Arena_DirectionalLight"
EXPECTED_OLD_INTENSITY = 8.0
TARGET_INTENSITY = 6.0
REPORT = PROJECT_DIR / "Saved" / "AssetProbe" / "demo_arena_sunlight_adjustment.json"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


report = {
    "map": MAP_PATH,
    "actor_label": ACTOR_LABEL,
    "expected_old_intensity": EXPECTED_OLD_INTENSITY,
    "target_intensity": TARGET_INTENSITY,
    "saved": False,
}

try:
    world = unreal.EditorLoadingAndSavingUtils.load_map(MAP_PATH)
    require(world is not None, "Could not load {}.".format(MAP_PATH))

    matches = [actor for actor in unreal.EditorLevelLibrary.get_all_level_actors()
               if str(actor.get_actor_label()) == ACTOR_LABEL]
    require(len(matches) == 1,
            "Expected exactly one actor labeled {}; found {}.".format(ACTOR_LABEL, len(matches)))

    actor = matches[0]
    component = actor.get_component_by_class(unreal.DirectionalLightComponent)
    require(component is not None, "{} has no DirectionalLightComponent.".format(ACTOR_LABEL))
    old_intensity = float(component.get_editor_property("intensity"))
    report["old_intensity"] = old_intensity

    if math.isclose(old_intensity, TARGET_INTENSITY, rel_tol=0.0, abs_tol=0.001):
        pass
    else:
        require(math.isclose(old_intensity, EXPECTED_OLD_INTENSITY,
                             rel_tol=0.0, abs_tol=0.001),
                "Sun intensity changed unexpectedly ({}); refusing to overwrite.".format(old_intensity))
        component.modify()
        component.set_editor_property("intensity", TARGET_INTENSITY)

    report["new_intensity"] = float(component.get_editor_property("intensity"))
    require(math.isclose(report["new_intensity"], TARGET_INTENSITY,
                         rel_tol=0.0, abs_tol=0.001),
            "Directional light readback did not match the target value.")
    require(unreal.EditorLevelLibrary.save_current_level(),
            "UE failed to save the modified level.")
    report["saved"] = True
except Exception as exc:
    report["error"] = "{}: {}".format(type(exc).__name__, exc)

REPORT.parent.mkdir(parents=True, exist_ok=True)
REPORT.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
if report.get("saved") is not True:
    raise RuntimeError(report.get("error", "Sunlight adjustment did not complete."))
