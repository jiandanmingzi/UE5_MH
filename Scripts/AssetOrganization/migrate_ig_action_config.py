"""Batch backend for migrate_powder_vortex_config.py -MHGZActionConfig.

First read freezes/backs up Combat and ten resident GAs BEFORE native edits.
After building, -MHGZActionConfigApply saves only those GAs, once; default then
verifies in a fresh process. No Montage, damage segment, mark projectile or
shared Combat value is changed. Source values, not native defaults, are truth.
"""
import hashlib
import json
import re
import shutil
import sys
from pathlib import Path

import unreal

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from ue_asset_utils import as_sequence, blueprint_cdo, decode_native_export

GROUPS = {
    "AirDodge": "AirDodgeHorizontalSpeed AirDodgeVerticalSpeed",
    "PoleVault": "VaultTunings",
    "KinsectSlash": "KinsectSlashDashProfile bUseCapturedKinsectSlashDash KinsectSlashMaxDistance KinsectSlashDashDuration KinsectSlashMaxUpwardLaunchPitchDegrees KinsectSlashDistanceCurve KinsectSlashDanceDamageMultipliers",
    "KinsectPiercer": "KinsectPiercerStartProfile KinsectPiercerDiveProfile KinsectPiercerLandingProfile KinsectPiercerDiveWatchdogSeconds KinsectPiercerLandingBlendInTime KinsectPiercerDanceDamageMultipliers",
    "DescendingThrust": "DescendingThrustAirProfile DescendingThrustAirWatchdogSeconds DescendingThrustLandingBlendInTime DescendingThrustAirControl DescendingThrustDanceDamageMultipliers",
    "AwakenedKinsectAttack": "AwakenedKinsectMotionValue AwakenedRedKinsectMotionValue AwakenedTripleKinsectMotionValue AwakenedHunterFlightDurationSeconds AwakenedHunterFlightSpeed AwakenedHunterFlightGravityMagnitude AwakenedGroundTargetMinimumDistance AwakenedGroundTargetVerticalLaunchSpeed AwakenedHunterFallHandoffVelocity AwakenedHunterFallGravityMagnitude AwakenedHunterFallMontage AwakenedAimHorizontalCorrectionAngle AwakenedAimCorrectionAngle",
    "DrawAndSendKinsect": "DrawSendKinsectMotionValue",
}
ASSETS = {
    "AirDodge": ["AirDodge"],
    "PoleVault": ["QianChengGanTiao", "HouChengGanTiao", "ZuoChengGanTiao", "YouChengGanTiao"],
    "KinsectSlash": ["CaoChongZhan"],
    "KinsectPiercer": ["QiangHuaCaoChongChuanCi"],
    "DescendingThrust": ["JiXiTuCi"],
    "AwakenedKinsectAttack": ["JueChongJi"],
    "DrawAndSendKinsect": ["DrawAndSendKinsect"],
}

# Native export text rounds floats to six decimals. Preserve reflected values
# recursively as well, so moving a 1/120s phase cannot subtly retime the action.
STRUCT_FIELDS = {
    "Vector": "x y z",
    "VaultTuning": "white vault_duration arc_duration distance apex_height",
    "SegmentedDirectionalMovementProfile": "initial_hold_seconds straight_seconds glide_seconds straight_speed glide_initial_speed glide_gravity_magnitude glide_forward_acceleration",
    "RecordedLocalTrajectoryProfile": "curve covered_seconds terminal_velocity_local terminal_acceleration_local",
}


def snake(name):
    if name.startswith("b") and name[1].isupper():
        name = name[1:]
    return re.sub(r"(?<!^)(?=[A-Z])", "_", name).lower()


def encode(v):
    if v is None or isinstance(v, (bool, int, float, str)):
        return v
    if isinstance(v, unreal.Object):
        return {"object": v.get_path_name()}
    if isinstance(v, unreal.StructBase):
        name = type(v).__name__
        assert name in STRUCT_FIELDS, ("Unregistered struct", name)
        return {"struct": name, "text": v.export_text(), "fields": {
            field: encode(v.get_editor_property(field)) for field in STRUCT_FIELDS[name].split()}}
    return [encode(x) for x in as_sequence(v)]


def decode(v):
    if isinstance(v, list):
        return [decode(x) for x in v]
    if isinstance(v, dict):
        if "object" in v:
            result = unreal.load_object(None, v["object"])
            assert result, v
            return result
        result = getattr(unreal, v["struct"])()
        assert result.import_text(v["text"]), v
        for field, item in v["fields"].items():
            result.set_editor_property(field, decode(item))
        return result
    return v


def accepts_combat_cleanup(root, expected, actual):
    """Only the explicitly verified cleanup may supersede a frozen Combat hash."""
    path = root / "Saved/Maintenance/CombatCleanup20261024/applied.json"
    if not path.exists():
        return False
    proof = json.loads(path.read_text(encoding="utf-8"))
    return proof.get("state") == "saved" and proof.get("before") == expected and proof.get("after") == actual


def cleanup_combat():
    root = Path(unreal.Paths.project_dir()).resolve()
    out = root / "Saved/Maintenance/CombatCleanup20261024"
    out.mkdir(parents=True, exist_ok=True)
    package = "/Game/Weapons/InsectGlaive/Data/DA_IG_Combat"
    asset = unreal.load_asset(package)
    assert asset
    source_file, applied_file = out / "source.json", out / "applied.json"
    writing = "-MHGZCombatCleanupApply" in unreal.SystemLibrary.get_command_line()

    def disk(p):
        return root / ("Content/" + p.removeprefix("/Game/") + ".uasset")

    def digest(p):
        return hashlib.sha256(disk(p).read_bytes()).hexdigest()

    def snapshot(removed, phase):
        path = out / (phase + ".t3d")
        task = unreal.AssetExportTask()
        task.object, task.filename, task.exporter = asset, str(path), unreal.ObjectExporterT3D()
        task.automated, task.prompt, task.replace_identical = True, False, True
        assert unreal.Exporter.run_asset_export_task(task)
        text = decode_native_export(path.read_bytes())
        return re.sub(r"(?m)^\s*(?:" + "|".join(removed) + r")(?:\(\d+\))?=.*(?:\n|$)", "", text)

    if not source_file.exists():
        assert not writing, "Freeze before deleting native fields"
        header = (root / "Source/MHGZ/InsectGlaive/InsectGlaiveCombatConfig.h").read_text(encoding="utf-8")
        removed = [re.search(r"(\w+)\s*(?:=[\s\S]*)?$", m[1]).group(1) for m in re.finditer(
            r"UPROPERTY\(meta = \(DeprecatedProperty\)\)\s*\n([^;]+);", header)]
        removed += ["KinsectGlideMarkMaxDistance", "KinsectGlideFallbackDistance", "KinsectGlideFallbackLiftHeight"]
        assert len(removed) == 66
        old = json.loads((root / "Saved/Maintenance/IGActionConfig20261024/ActionDefaults/source.json").read_text(encoding="utf-8"))
        protected = sorted((set(old["targets"]) | set(old["protected"])) - {package})
        hashes = {p: digest(p) for p in protected + [package]}
        backup = out / "DA_IG_Combat.before.uasset"
        shutil.copy2(disk(package), backup)
        assert hashlib.sha256(backup.read_bytes()).hexdigest() == hashes[package]
        source = dict(removed=removed, protected=protected, hashes=hashes, active_text=snapshot(removed, "before"))
        source_file.write_text(json.dumps(source, ensure_ascii=False, indent=2), encoding="utf-8")
        unreal.log("COMBAT_CLEANUP_FROZEN " + str(len(removed)))
        return
    source = json.loads(source_file.read_text(encoding="utf-8"))
    for p in source["protected"]:
        assert digest(p) == source["hashes"][p], ("Protected package changed", p)
    assert snapshot(source["removed"], "staged") == source["active_text"], "Shared values changed"
    if writing:
        assert not applied_file.exists(), "Do not replay cleanup"
        assert digest(package) == source["hashes"][package]
        for field in source["removed"]:
            try:
                asset.get_editor_property(snake(field))
            except Exception as exc:
                assert "Failed to find property" in str(exc), (field, str(exc))
            else:
                raise AssertionError(("Native field still exists", field))
        applied_file.write_text(json.dumps(dict(state="saving", before=digest(package))), encoding="utf-8")
        assert unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False)
        applied_file.write_text(json.dumps(dict(state="saved", before=source["hashes"][package], after=digest(package)), indent=2), encoding="utf-8")
    else:
        proof = json.loads(applied_file.read_text(encoding="utf-8"))
        assert proof["state"] == "saved" and digest(package) == proof["after"]
    result = dict(success=True, removed_fields=len(source["removed"]), protected_unchanged=len(source["protected"]), shared_values_unchanged=True)
    (out / ("applied-values.json" if writing else "verified.json")).write_text(json.dumps(result, indent=2), encoding="utf-8")
    unreal.log("COMBAT_CLEANUP_OK " + json.dumps(result))


def migrate_actions():
    root = Path(unreal.Paths.project_dir()).resolve()
    out = root / "Saved/Maintenance/IGActionConfig20261024/ActionDefaults"
    out.mkdir(parents=True, exist_ok=True)
    combat_path = "/Game/Weapons/InsectGlaive/Data/DA_IG_Combat"
    targets = {f"/Game/Blueprints/Ability/InsectGlaive/GA_IG_{suffix}": group
               for group, suffixes in ASSETS.items() for suffix in suffixes}
    # Protect explicitly excluded assets, Combat, and referenced action Montages.
    protected = {combat_path, "/Game/Blueprints/Ability/InsectGlaive/GA_IG_ChongYin",
                 "/Game/Blueprints/Ability/InsectGlaive/GA_IG_PowderVortex"}
    assets = {p: unreal.load_asset(p) for p in targets}
    assert all(assets.values()), "Missing resident GA"
    cdos = {p: blueprint_cdo(a, unreal) for p, a in assets.items()}
    config = unreal.load_asset(combat_path)
    assert config, "Missing Combat"

    def disk(p):
        return root / ("Content/" + p.removeprefix("/Game/") + ".uasset")

    def digest(p):
        return hashlib.sha256(disk(p).read_bytes()).hexdigest()

    def cdo_text(p, phase):
        task = unreal.AssetExportTask()
        dest = out / (assets[p].get_name() + "." + phase + ".t3d")
        task.object, task.filename, task.exporter = cdos[p], str(dest), unreal.ObjectExporterT3D()
        task.automated, task.prompt, task.replace_identical = True, False, True
        assert unreal.Exporter.run_asset_export_task(task), p
        text = decode_native_export(dest.read_bytes())
        names = "|".join(GROUPS[targets[p]].split())
        return re.sub(r"(?m)^\s*(?:" + names + r")(?:\(\d+\))?=.*(?:\n|$)", "", text)

    source_path, applied_path = out / "source.json", out / "applied.json"
    apply = "-MHGZActionConfigApply" in unreal.SystemLibrary.get_command_line()
    if not source_path.exists():
        assert not apply, "Freeze source in read-only mode first"
        values = {g: {n: encode(config.get_editor_property(snake(n))) for n in ns.split()}
                  for g, ns in GROUPS.items()}
        baseline = {p: cdo_text(p, "before") for p in targets}
        for text in baseline.values():
            protected.update(re.findall(r"(/Game/[^\s\"']+)\.[^\s\"']+", text))
        protected.difference_update(targets)
        protected = {p for p in protected if disk(p).exists()}
        hashes = {p: digest(p) for p in set(targets) | protected}
        for p in hashes:
            dest = out / "Backup" / disk(p).relative_to(root)
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(disk(p), dest)
            assert hashlib.sha256(dest.read_bytes()).hexdigest() == hashes[p]
        source = dict(values=values, targets=targets, protected=sorted(protected), hashes=hashes, cdo=baseline)
        source_path.write_text(json.dumps(source, ensure_ascii=False, indent=2), encoding="utf-8")
        print("IG_ACTION_SOURCE_FROZEN " + json.dumps(dict(groups=len(values), gas=len(targets), protected=len(protected))))
        return

    source = json.loads(source_path.read_text(encoding="utf-8"))
    assert source["targets"] == targets
    for p in source["protected"]:
        actual_hash = digest(p)
        assert actual_hash == source["hashes"][p] or (p == combat_path and accepts_combat_cleanup(root, source["hashes"][p], actual_hash)), ("Protected package changed", p)
    if apply:
        assert not applied_path.exists(), "Already migrated; do not replay Apply"
        for p in targets:
            assert digest(p) == source["hashes"][p], ("GA changed since freeze", p)
        # Stage/validate all values before the first package save.
        for p, group in targets.items():
            for field, v in source["values"][group].items():
                cdos[p].set_editor_property(snake(field), decode(v))
            assert cdo_text(p, "staged") == source["cdo"][p], ("Other GA defaults changed", p)
    else:
        assert applied_path.exists(), "Explicit Apply required after native build"
    for p, group in targets.items():
        actual = {field: encode(cdos[p].get_editor_property(snake(field))) for field in GROUPS[group].split()}
        assert actual == source["values"][group], ("Migrated values differ", p, actual)
        assert cdo_text(p, "checked") == source["cdo"][p], ("Other GA defaults changed", p)
    if apply:
        # Intent guards partial-save failure: never blindly replay this transaction.
        applied_path.write_text(json.dumps(dict(state="saving", targets=list(targets))), encoding="utf-8")
        for p, a in assets.items():
            assert unreal.EditorAssetLibrary.save_loaded_asset(a, only_if_is_dirty=False), p
        applied_path.write_text(json.dumps(dict(state="saved", hashes={p: digest(p) for p in targets}), indent=2), encoding="utf-8")
    for p in source["protected"]:
        actual_hash = digest(p)
        assert actual_hash == source["hashes"][p] or (p == combat_path and accepts_combat_cleanup(root, source["hashes"][p], actual_hash)), p
    unchanged = sum(digest(p) == source["hashes"][p] for p in source["protected"])
    report = dict(success=True, gas=len(targets), field_values=sum(len(source["values"][g]) for g in targets.values()),
                  protected_unchanged=unchanged, authorized_combat_cleanup=unchanged != len(source["protected"]))
    (out / ("applied-values.json" if apply else "verified.json")).write_text(json.dumps(report, indent=2), encoding="utf-8")
    print("IG_ACTION_CONFIG_OK " + json.dumps(report))
