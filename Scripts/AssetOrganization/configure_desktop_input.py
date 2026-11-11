"""Configure the demo's shared Enhanced Input context for keyboard/mouse.

Default is read-only. With the editor closed, run the Python Commandlet with
-MHGZDesktopInputApply once; otherwise use ue_remote.py --script-arg=--apply.
Only IMC_MHGZ_Demo and IA_Move are saved. Existing gamepad/MouseLook mappings,
chords, Combo, GA and user-authored Montages are not changed.
"""
from pathlib import Path
import hashlib
import json
import shutil
import sys
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT = ROOT / "Saved/Maintenance/DesktopInput20261111"
OUT.mkdir(parents=True, exist_ok=True)
IMC_PATH = "/Game/Input/Contexts/IMC_MHGZ_Demo"
MOVE_PATH = "/Game/Input/Actions/MHGZ/IA_Move"
FILES = [ROOT / "Content/Input/Contexts/IMC_MHGZ_Demo.uasset", ROOT / "Content/Input/Actions/MHGZ/IA_Move.uasset"]
DESIRED = {"IA_A": ["SpaceBar"], "IA_Y": ["LeftMouseButton"], "IA_B": ["RightMouseButton"],
           "IA_RT": ["LeftControl", "RightControl"], "IA_LT": ["R"],
           "IA_RB": ["LeftShift", "RightShift"], "IA_Move": ["W", "A", "S", "D"]}

def prop(obj, name): return obj.get_editor_property(name)
def key_name(mapping): return str(prop(prop(mapping, "key"), "key_name"))
def snapshot(imc, move):
    return {"mappings": [{"action": prop(m, "action").get_path_name(), "key": key_name(m),
                          "modifiers": [x.get_class().get_name() for x in prop(m, "modifiers")]} for m in prop(imc, "mappings")],
            "movement_accumulation": str(prop(move, "accumulation_behavior"))}

imc, move = unreal.load_asset(IMC_PATH), unreal.load_asset(MOVE_PATH)
if not imc or not move: raise RuntimeError("Missing resident input assets")
before = snapshot(imc, move)
apply = "--apply" in sys.argv or "-MHGZDesktopInputApply" in unreal.SystemLibrary.get_command_line()
if apply:
    if (OUT / "applied.json").exists(): raise RuntimeError("Already applied; run read-only verification instead")
    for path in FILES:
        dest = OUT / "Backup" / path.relative_to(ROOT)
        if dest.exists():
            if dest.read_bytes() != path.read_bytes():
                raise RuntimeError("Asset differs from backup; audit before retrying")
        else:
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, dest)
    mappings = [m for m in prop(imc, "mappings")
                if prop(m, "action").get_name() not in DESIRED or key_name(m).startswith("Gamepad_")]
    for name, keys in DESIRED.items():
        action = unreal.load_asset("/Game/Input/Actions/MHGZ/" + name)
        if not action: raise RuntimeError("Missing action " + name)
        for key in keys:
            modifiers = []
            if key in ("A", "S"):
                negate = unreal.InputModifierNegate(outer=imc)
                modifiers.append(negate)
            if key in ("W", "S"):
                swizzle = unreal.InputModifierSwizzleAxis(outer=imc)
                swizzle.set_editor_property("order", unreal.InputAxisSwizzle.YXZ)
                modifiers.append(swizzle)
            physical = unreal.Key()
            physical.set_editor_property("key_name", key)
            mapping = unreal.EnhancedActionKeyMapping()
            mapping.set_editor_property("action", action)
            mapping.set_editor_property("key", physical)
            mapping.set_editor_property("modifiers", modifiers)
            mappings.append(mapping)
    imc.set_editor_property("mappings", mappings)
    move.set_editor_property("accumulation_behavior", unreal.InputActionAccumulationBehavior.CUMULATIVE)
    for asset in (imc, move):
        if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
            raise RuntimeError("Save failed: " + asset.get_path_name())
    (OUT / "applied.json").write_text(json.dumps({"before": before, "after": snapshot(imc, move)}, indent=2), encoding="utf-8")

current = snapshot(imc, move)
actual = {(prop(m, "action").get_name(), key_name(m)) for m in prop(imc, "mappings")}
checks = {name + ":" + key: (name, key) in actual for name, keys in DESIRED.items() for key in keys}
checks["movement_opposite_keys_cancel"] = prop(move, "accumulation_behavior") == unreal.InputActionAccumulationBehavior.CUMULATIVE
if (OUT / "applied.json").exists():
    transaction = json.loads((OUT / "applied.json").read_text(encoding="utf-8"))
    checks["gamepad_and_mouse_look_preserved"] = all(
        m in current["mappings"] for m in transaction["before"]["mappings"]
        if m["key"].startswith("Gamepad_") or m["key"] == "Mouse2D")
report = {"apply": apply, "current": current, "checks": checks,
          "hashes": {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in FILES}}
(OUT / ("apply-report.json" if apply else "audit-report.json")).write_text(json.dumps(report, indent=2), encoding="utf-8")
if not all(checks.values()): raise RuntimeError("Desktop input checks failed: " + str(checks))
unreal.log("DESKTOP_INPUT_CONFIG_OK " + str(len(checks)))
