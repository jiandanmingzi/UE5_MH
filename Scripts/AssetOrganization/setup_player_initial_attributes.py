"""Create/wire the editable player initial-attribute GE once; default is read-only.

Editor closed:
  UnrealEditor-Cmd MHGZ.uproject -run=pythonscript -script=<this file>
    -MHGZPlayerInitialAttributesApply -unattended -NullRHI
Omit -MHGZPlayerInitialAttributesApply for cold readback. No existing GE is overwritten.
Backs up BP_PlayerState, preserves other CoreAttributeEffects, writes reports under Saved.
Future balancing is done in GE_PlayerInitialAttributes, not by rerunning Apply.
"""
import hashlib
import json
from pathlib import Path
import re
import shutil
import sys

import unreal

ROOT = Path(unreal.Paths.project_dir()).resolve()
OUT = ROOT / "Saved/Maintenance/InitialAttributes20261017"
OUT.mkdir(parents=True, exist_ok=True)
sys.path.insert(0, str(ROOT / "Scripts"))
from ue_asset_utils import as_sequence, blueprint_cdo, decode_native_export

PS_PATH = "/Game/Blueprints/Characters/Demo/BP_PlayerState"
GE_PATH = "/Game/GameplayEffects/Core/GE_PlayerInitialAttributes"
GE_CLASS_PATH = GE_PATH + ".GE_PlayerInitialAttributes_C"
ATTRIBUTES = ("MaxHealth", "MaxStamina", "Health", "Stamina")
APPLY = "-MHGZPlayerInitialAttributesApply" in unreal.SystemLibrary.get_command_line()

def dump(name, value):
    (OUT / name).write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8")

def package_file(path):
    return ROOT / "Content" / (path.removeprefix("/Game/") + ".uasset")

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def export(obj, name):
    filename = OUT / name
    task = unreal.AssetExportTask()
    task.object, task.filename, task.exporter = obj, str(filename), unreal.ObjectExporterT3D()
    task.automated, task.prompt, task.replace_identical = True, False, True
    assert unreal.Exporter.run_asset_export_task(task), obj.get_path_name()
    return decode_native_export(filename.read_bytes())

def without_core_effects(text):
    return re.sub(r"(?m)^\s*CoreAttributeEffects(?:\(\d+\))?=[^\n]*\n?", "", text)

def core_paths(asc):
    return [value.get_path_name() for value in as_sequence(asc.get_editor_property("core_attribute_effects"))]

ps = unreal.load_asset(PS_PATH)
assert ps is not None
ps_cdo = blueprint_cdo(ps, unreal)
asc = ps_cdo.get_editor_property("ability_system_component")
before_text = export(ps_cdo, "PlayerState.current.copy")

if APPLY:
    assert not (OUT / "applied.json").exists(), "Already applied; use read-only verification"
    assert not unreal.EditorAssetLibrary.does_asset_exist(GE_PATH), "Existing GE must not be overwritten"
    backup = OUT / "Backup/BP_PlayerState.uasset"
    backup.parent.mkdir(parents=True, exist_ok=True)
    assert not backup.exists(), "Prior transaction backup exists; inspect before retry"
    shutil.copy2(package_file(PS_PATH), backup)
    files = list((ROOT / "Content/Blueprints/Ability/InsectGlaive").glob("*.uasset"))
    files += list((ROOT / "Content/Weapons/InsectGlaive/Anims/Montage").glob("*.uasset"))
    files += list((ROOT / "Content/Weapons/InsectGlaive/Data").glob("*.uasset"))
    files += list((ROOT / "Content/UI").rglob("*.uasset"))
    protected = {str(path.relative_to(ROOT)): sha(path) for path in files}
    before_paths = core_paths(asc)
    assert GE_CLASS_PATH not in before_paths
    dump("intent.json", {"core_before": before_paths, "protected_sha256": protected,
                         "backup_sha256": sha(backup), "player_state_before": before_text})

    # Use the existing reflected FGameplayAttribute format, avoiding a new runtime
    # class/editor bridge solely to assign the four FProperty-backed attributes.
    template = as_sequence(unreal.get_default_object(unreal.MHGZStaminaCostGameplayEffect)
                           .get_editor_property("modifiers"))[0].get_editor_property("attribute").export_text()
    modifiers = []
    for name in ATTRIBUTES:
        attribute_text = template.replace('AttributeName="Stamina"', 'AttributeName="' + name + '"')
        attribute_text = attribute_text.replace("MHGZAttributeSet:Stamina", "MHGZAttributeSet:" + name)
        modifier = unreal.GameplayModifierInfo()
        modifier.import_text("(Attribute=" + attribute_text + ",ModifierOp=Override,"
                             "ModifierMagnitude=(MagnitudeCalculationType=ScalableFloat,"
                             "ScalableFloatMagnitude=(Value=200.0)))")
        assert ("MHGZAttributeSet:" + name) in modifier.get_editor_property("attribute").export_text()
        assert "OVERRIDE" in str(modifier.get_editor_property("modifier_op"))
        modifiers.append(modifier)

    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", unreal.GameplayEffect)
    ge = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "GE_PlayerInitialAttributes", "/Game/GameplayEffects/Core", unreal.Blueprint, factory)
    assert ge is not None
    unreal.BlueprintEditorLibrary.compile_blueprint(ge)
    ge_cdo = blueprint_cdo(ge, unreal)
    ge_cdo.set_editor_property("duration_policy", unreal.GameplayEffectDurationType.INSTANT)
    ge_cdo.set_editor_property("modifiers", modifiers)
    assert unreal.EditorAssetLibrary.save_loaded_asset(ge, False)

    core = as_sequence(asc.get_editor_property("core_attribute_effects"))
    core.append(ge.generated_class())
    asc.modify()
    asc.set_editor_property("core_attribute_effects", core)
    after_text = export(ps_cdo, "PlayerState.after.copy")
    assert without_core_effects(before_text) == without_core_effects(after_text), "Unrelated PlayerState default changed"
    assert unreal.EditorAssetLibrary.save_loaded_asset(ps, False)
    dump("applied.json", {"core_after": core_paths(asc), "player_state_sha256": sha(package_file(PS_PATH)),
                          "ge_sha256": sha(package_file(GE_PATH)), "player_state_after": after_text})
else:
    intent = json.loads((OUT / "intent.json").read_text(encoding="utf-8"))
    applied = json.loads((OUT / "applied.json").read_text(encoding="utf-8"))
    assert core_paths(asc) == applied["core_after"]
    assert core_paths(asc).count(GE_CLASS_PATH) == 1
    assert core_paths(asc)[:-1] == intent["core_before"]
    assert without_core_effects(before_text) == without_core_effects(intent["player_state_before"])
    ge = unreal.load_asset(GE_PATH)
    ge_cdo = blueprint_cdo(ge, unreal)

current_modifiers = as_sequence(ge_cdo.get_editor_property("modifiers"))
assert len(current_modifiers) == 4
assert "INSTANT" in str(ge_cdo.get_editor_property("duration_policy"))
rows = []
for name, modifier in zip(ATTRIBUTES, current_modifiers):
    attribute = modifier.get_editor_property("attribute").export_text()
    magnitude = modifier.get_editor_property("modifier_magnitude").export_text()
    assert ("MHGZAttributeSet:" + name) in attribute
    assert "OVERRIDE" in str(modifier.get_editor_property("modifier_op"))
    assert "MagnitudeCalculationType=ScalableFloat" in magnitude
    value = float(re.search(r"ScalableFloatMagnitude=\(Value=([-+0-9.eE]+)", magnitude).group(1))
    rows.append({"attribute": name, "operation": "Override", "value": value})
export(ge_cdo, "InitialEffect.current.copy")
if not APPLY:
    assert all(sha(ROOT / path) == value for path, value in intent["protected_sha256"].items())
    dump("verified.json", {"success": True, "core_effects": core_paths(asc), "modifiers": rows,
                           "duration": "Instant", "protected_packages": len(intent["protected_sha256"])})
unreal.log("[InitialAttributes] " + ("Applied" if APPLY else "Cold verified") + ": " + json.dumps(rows))
