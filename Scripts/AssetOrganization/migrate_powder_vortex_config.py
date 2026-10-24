"""GA config migration; existing full damage migration also rewrites unrelated attacks.

Completed non-projectile action batch: -MHGZActionConfig dispatches the bounded
migrate_ig_action_config backend (default verifies, Apply cannot be replayed).

Default: freeze/read the current Combat source and back up GA + Combat. After the
native settings change is built, pass -MHGZPowderVortexConfigApply once. Subsequent
default runs verify the saved GA. No other asset, notification or global setting
is written. Use ue_remote.py if the matching editor is running.
"""
import hashlib
import json
import re
import shutil
import sys
from pathlib import Path

import unreal

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from ue_asset_utils import as_sequence, blueprint_cdo, decode_native_export, read_field

ROOT = Path(unreal.Paths.project_dir()).resolve()
OUT = ROOT / "Saved/Maintenance/PowderVortexConfig20261024"
GA = "/Game/Blueprints/Ability/InsectGlaive/GA_IG_PowderVortex"
COMBAT = "/Game/Weapons/InsectGlaive/Data/DA_IG_Combat"
FIELDS = {
    "powder_gather_radius": "gather_radius",
    "powder_vortex_delay": "gather_delay",
    "powder_vortex_damage_at1": "motion_value_at1",
    "powder_vortex_damage_at5": "motion_value_at5",
    "powder_vortex_damage_at10": "motion_value_at10",
    "powder_vortex_explosion_radius": "explosion_radius",
    "powder_vortex_hit_cue_tag": "hit_cue_tag",
    "powder_vortex_explosion_cue_tag": "explosion_cue_tag",
    "powder_vortex_gather_sounds": "gather_sounds",
    "powder_vortex_explosion_sound": "explosion_sound",
}


def disk(package):
    return ROOT / ("Content/" + package.removeprefix("/Game/") + ".uasset")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def value(obj, name):
    item = read_field(obj, name)
    if "sounds" in name:
        return [x.get_path_name() for x in as_sequence(item)]
    if "sound" in name:
        return item.get_path_name() if item else None
    if "tag" in name:
        return item.export_text()
    return float(item)


def dump_cdo(cdo, name):
    path = OUT / name
    task = unreal.AssetExportTask()
    task.object, task.filename, task.exporter = cdo, str(path), unreal.ObjectExporterT3D()
    task.automated, task.prompt, task.replace_identical = True, False, True
    assert unreal.Exporter.run_asset_export_task(task), name
    text = decode_native_export(path.read_bytes())
    return re.sub(r"(?m)^\s*(?:PowderVortex|CommittedPowderVortex)=.*(?:\n|$)", "", text)


def main():
    if "-MHGZCombatCleanup" in unreal.SystemLibrary.get_command_line():
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        from migrate_ig_action_config import cleanup_combat
        cleanup_combat()
        return
    if "-MHGZActionConfig" in unreal.SystemLibrary.get_command_line():
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        from migrate_ig_action_config import migrate_actions
        migrate_actions()
        return
    OUT.mkdir(parents=True, exist_ok=True)
    source_file = OUT / "source.json"
    applied_file = OUT / "applied.json"
    ga = unreal.load_asset(GA)
    config = unreal.load_asset(COMBAT)
    assert ga and config, "Required resident GA/Combat missing"
    cdo = blueprint_cdo(ga, unreal)
    writing = "-MHGZPowderVortexConfigApply" in unreal.SystemLibrary.get_command_line()
    if not source_file.exists():
        assert not writing, "Run the read-only source audit before Apply"
        snapshots = {k: value(config, k) for k in FIELDS}
        snapshots["marked_hit_powder_cooldown"] = value(config, "marked_hit_powder_cooldown")
        snapshots["powder_motion_value"] = value(config, "powder_motion_value")
        hashes = {p: digest(disk(p)) for p in (GA, COMBAT)}
        for p in hashes:
            backup = OUT / "Backup" / disk(p).relative_to(ROOT)
            backup.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(disk(p), backup)
            assert digest(backup) == hashes[p]
        source = dict(values=snapshots, sha256=hashes, cdo=dump_cdo(cdo, "before.t3d"))
        source_file.write_text(json.dumps(source, ensure_ascii=False, indent=2), encoding="utf-8")
        print("POWDER_VORTEX_SOURCE_FROZEN " + json.dumps(snapshots, ensure_ascii=False))
        return
    source = json.loads(source_file.read_text(encoding="utf-8"))
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from migrate_ig_action_config import accepts_combat_cleanup
    def combat_unchanged():
        actual = digest(disk(COMBAT))
        return actual == source["sha256"][COMBAT] or accepts_combat_cleanup(ROOT, source["sha256"][COMBAT], actual)
    assert combat_unchanged(), "Combat changed outside the verified cleanup; preserve user edits"
    settings = read_field(cdo, "powder_vortex")
    if writing:
        assert not applied_file.exists(), "Already migrated; do not replay Apply"
        assert digest(disk(GA)) == source["sha256"][GA], "GA changed; preserve user edits"
        for old, new in FIELDS.items():
            v = source["values"][old]
            if "sounds" in new:
                v = [unreal.load_asset(p) for p in v]
            elif "sound" in new:
                v = unreal.load_asset(v) if v else None
            elif "tag" in new:
                # Hidden legacy properties are intentionally not editor-readable.
                # Import the exact pre-build native struct text, not a guessed tag.
                tag = unreal.GameplayTag()
                assert tag.import_text(v), (old, v)
                v = tag
            settings.set_editor_property(new, v)
        cdo.set_editor_property("powder_vortex", settings)
        staged = {old: value(settings, new) for old, new in FIELDS.items()}
        assert all(staged[k] == source["values"][k] for k in FIELDS), (staged, source["values"])
        assert dump_cdo(cdo, "applied.t3d") == source["cdo"], "Non-vortex GA defaults changed"
        assert unreal.EditorAssetLibrary.save_loaded_asset(ga, only_if_is_dirty=False), GA
        applied_file.write_text(json.dumps(dict(ga_sha256=digest(disk(GA))), indent=2), encoding="utf-8")
    else:
        assert applied_file.exists(), "Native schema ready; explicit Apply is still required"
    actual = {old: value(read_field(cdo, "powder_vortex"), new) for old, new in FIELDS.items()}
    assert all(actual[k] == source["values"][k] for k in FIELDS), (actual, source["values"])
    assert dump_cdo(cdo, "verified.t3d") == source["cdo"], "Non-vortex GA defaults changed"
    assert combat_unchanged(), "Combat changed outside the verified cleanup"
    same_combat = digest(disk(COMBAT)) == source["sha256"][COMBAT]
    result = dict(success=True, values=actual, ga_sha256=digest(disk(GA)),
                  combat_unchanged=same_combat, authorized_combat_cleanup=not same_combat)
    (OUT / ("applied-values.json" if writing else "verified.json")).write_text(
        json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    print("POWDER_VORTEX_CONFIG_OK " + json.dumps(result, ensure_ascii=False))


main()
