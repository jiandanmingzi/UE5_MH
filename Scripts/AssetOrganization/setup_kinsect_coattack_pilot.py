"""Authorized cooperative-attack assets; default read-only, no tests/PIE.

Open editor: ue_remote.py <this file> [--script-arg=--apply].
Closed editor: -run=pythonscript -script=<this file> [-MHGZCoAttackPilotApply].
Apply backs up only GA_IG_TuCi and refuses a previous intent. Subsequent runs
verify the saved transaction, including protected GA/Combat/Combo/Montage hashes.
Uses existing reflected-value adapters and native T3D export, no new editor bridge.

Approved five-GA expansion (ShangLao/JiaSha/ErLian/HengSao/FeiYuan):
remote --script-arg=--ground-five [--script-arg=--apply]; Commandlet
-MHGZCoAttackGroundFive [-MHGZCoAttackGroundFiveApply]. Separate transaction;
only CoAttack changes, each real ContactOnly source segment gets kinsect MV10.

Second approved group (TuJinHuiXuanZhan/SiLianYinZhan/BaDaoZhan/FeiShenYueRu/HouChe):
--ground-second-five / -MHGZCoAttackGroundSecondFive, explicit write flag
-MHGZCoAttackGroundSecondFiveApply (or remote --apply). Same protected transaction.

Special descending-thrust read-only audit: --descending-thrust /
-MHGZCoAttackDescendingThrust. Verifies inherited defaults and existing final
ground window, without enabling ordinary coattack or saving any package.
"""
import hashlib
import json
from pathlib import Path
import re
import shutil
import sys

import unreal

ROOT = Path(unreal.Paths.project_dir()).resolve()
COMMAND_LINE = unreal.SystemLibrary.get_command_line()
GROUND_FIVE = "--ground-five" in sys.argv or "-MHGZCoAttackGroundFive" in COMMAND_LINE
GROUND_SECOND_FIVE = "--ground-second-five" in sys.argv or "-MHGZCoAttackGroundSecondFive" in COMMAND_LINE
DESCENDING_THRUST = "--descending-thrust" in sys.argv or "-MHGZCoAttackDescendingThrust" in COMMAND_LINE
assert sum((GROUND_FIVE, GROUND_SECOND_FIVE, DESCENDING_THRUST)) <= 1, "Choose one approved asset group"
BATCH_FOLDER = "E1GroundSecondFive" if GROUND_SECOND_FIVE else ("E1GroundFive" if GROUND_FIVE else None)
OUT = ROOT / "Saved/Maintenance/KinsectCoAttack20261023" / ("E1DescendingThrust" if DESCENDING_THRUST else (BATCH_FOLDER or "E1TuCi"))
OUT.mkdir(parents=True, exist_ok=True)
sys.path.insert(0, str(ROOT / "Scripts"))
from ue_asset_utils import as_sequence, blueprint_cdo, decode_native_export, export_records, map_items, read_field

GA = "/Game/Blueprints/Ability/InsectGlaive/GA_IG_TuCi"
GROUND_GAS = tuple("/Game/Blueprints/Ability/InsectGlaive/GA_IG_" + name
                   for name in ("ShangLao", "JiaSha", "ErLian", "HengSao", "FeiYuan"))
GROUND_SECOND_GAS = tuple("/Game/Blueprints/Ability/InsectGlaive/GA_IG_" + name
                          for name in ("TuJinHuiXuanZhan", "SiLianYinZhan", "BaDaoZhan", "FeiShenYueRu", "HouChe"))
APPROVED_BATCHES = (("E1GroundFive", GROUND_GAS), ("E1GroundSecondFive", GROUND_SECOND_GAS))
BATCH_GAS = dict(APPROVED_BATCHES).get(BATCH_FOLDER, ())
COMBAT = "/Game/Weapons/InsectGlaive/Data/DA_IG_Combat"
COMBO = "/Game/Weapons/InsectGlaive/Data/DA_IG_Combo"
RUNTIME = "/Game/Weapons/InsectGlaive/Data/DA_WeaponRuntime_IG"
MONTAGE = "/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_TuCi"
WEAPON = "/Game/Weapons/InsectGlaive/Meshes/Glaive/SKM_IG_Glaive"
TARGET_MVS = {"0": 10.0, "1": 10.0}
APPLY = "--apply" in sys.argv or (
    "-MHGZCoAttackGroundSecondFiveApply" if GROUND_SECOND_FIVE else (
        "-MHGZCoAttackGroundFiveApply" if GROUND_FIVE else "-MHGZCoAttackPilotApply")) in COMMAND_LINE


def dump(name, value):
    (OUT / name).write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8")


def disk(path):
    return ROOT / "Content" / (path.split(".", 1)[0].removeprefix("/Game/") + ".uasset")


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def export(obj, name):
    filename = OUT / name
    task = unreal.AssetExportTask()
    task.object, task.filename, task.exporter = obj, str(filename), unreal.ObjectExporterT3D()
    task.automated, task.prompt, task.replace_identical = True, False, True
    assert unreal.Exporter.run_asset_export_task(task), obj.get_path_name()
    return decode_native_export(filename.read_bytes())


def without_coattack(text):
    return re.sub(r"(?m)^\s*CoAttack=[^\n]*\n?", "", text)


def attack_windows(text):
    """Resolve native notify payloads, never infer ConfigIndex from object suffixes."""
    payloads = {}
    pattern = r"(?ms)^   Begin Object([^\n]*)\n(.*?)^   End Object[ \t]*$"
    for block in re.finditer(pattern, text):
        header, body = block.groups()
        if "/Script/MHGZ.AnimNotifyState_AttackCollision" not in header:
            continue
        name = re.search(r'\bName="([^"]+)"', header)
        assert name, header
        index = re.search(r"(?m)^\s+ConfigIndex=(-?\d+)\s*$", body)
        # Native ConfigIndex defaults to zero; T3D omits default-valued fields.
        payloads[name.group(1)] = int(index.group(1)) if index else 0
    rows = []
    for event in export_records(text, "Notifies"):
        reference = event.get("NotifyStateClass", "")
        if not reference.startswith('"/Script/MHGZ.AnimNotifyState_AttackCollision\''):
            continue
        name = reference.split("'", 2)[1].rsplit(":", 1)[-1]
        assert name in payloads, "Missing native AttackCollision payload: " + reference
        start, duration = float(event.get("LinkValue", 0)), float(event.get("Duration", 0))
        assert duration > 0, "AttackCollision requires a real window"
        rows.append({"index": payloads[name], "start_s": start, "end_s": start + duration,
                     "guid": event["Guid"], "notify_object": name})
    assert rows, "No real AttackCollision windows"
    return rows


def snapshot(ga=GA, expected_montage=MONTAGE):
    bp = unreal.load_asset(ga)
    cdo = blueprint_cdo(bp, unreal)
    combat = unreal.load_asset(COMBAT)
    montage = read_field(cdo, "attack_montage")
    assert montage, "Missing attack Montage: " + ga
    if expected_montage:
        assert montage.get_path_name().split(".", 1)[0] == expected_montage
    co = read_field(cdo, "co_attack")
    tuning = read_field(combat, "co_attack_tuning")
    kinsect = read_field(combat, "kinsect_data")
    weapon_component = unreal.new_object(unreal.SkeletalMeshComponent)
    weapon_component.set_skinned_asset_and_update(unreal.load_asset(WEAPON))
    assert weapon_component.does_socket_exist("IG_FrontTip"), "Missing actual weapon socket/bone"
    segments = as_sequence(read_field(cdo, "attack_segments"))
    prefix = ga.rsplit("/", 1)[-1] + "." if BATCH_FOLDER else ""
    montage_text = export(montage, prefix + "montage.current.copy")
    cdo_text = export(cdo, prefix + "ability.current.copy")
    rows = []
    for i, segment in enumerate(segments):
        damage = read_field(segment, "damage")
        effect = read_field(damage, "damage_effect_class")
        rows.append({"index": i, "config": segment.export_text(),
                     "damage_effect": effect.get_path_name() if effect else None,
                     "weapon_mv": read_field(read_field(damage, "motion_value"), "value"),
                     "multi_hit_policy": str(read_field(segment, "multi_hit_policy"))})
    row = {"ga": ga, "coattack": co.export_text(),
           "offset_xyz": [float(read_field(co, "local_offset").x),
                          float(read_field(co, "local_offset").y), float(read_field(co, "local_offset").z)],
           "segment_motion_values": {str(k): float(v) for k, v in map_items(read_field(co, "segment_motion_values"))},
           "tuning": tuning.export_text(), "delay_s": float(read_field(tuning, "trajectory_delay_seconds")),
           "kinsect_data": kinsect.get_path_name(), "return_speed_cm_s": float(read_field(kinsect, "return_speed")),
           "segments": rows, "montage": montage.get_path_name(),
           "notifies": export_records(montage_text, "Notifies"),
           "attack_windows": attack_windows(montage_text),
           "sections": export_records(montage_text, "CompositeSections"),
           "cdo_except_coattack": without_coattack(cdo_text),
           "coattack_fields": {"mesh_tag": str(read_field(co, "mesh_tag")),
                               "socket": str(read_field(co, "socket")),
                               "local_offset": read_field(co, "local_offset").export_text()}}
    return bp, cdo, row


def verify_target(row, target_mvs=TARGET_MVS):
    assert re.search(r"\bbEnabled=True\b", row["coattack"]), row["coattack"]
    assert row["segment_motion_values"] == target_mvs, row["segment_motion_values"]
    assert row["coattack_fields"]["mesh_tag"] == "WeaponTrace"
    assert row["coattack_fields"]["socket"] == "IG_FrontTip"
    assert row["offset_xyz"] == [0.0, 0.0, 0.0]
    assert abs(row["delay_s"] - .2) < 1.e-6


def ground_target(row):
    indices = sorted({window["index"] for window in row["attack_windows"]})
    for index in indices:
        assert 0 <= index < len(row["segments"]), (row["ga"], index)
        segment = row["segments"][index]
        assert "CONTACT_ONLY" in segment["multi_hit_policy"] and segment["damage_effect"], segment
    assert abs(row["delay_s"] - .2) < 1.e-6 and row["return_speed_cm_s"] > 0
    return {str(index): 10.0 for index in indices}


def verify_unchanged(before, after):
    for key in ("ga", "cdo_except_coattack", "segments", "montage", "notifies",
                "attack_windows", "sections", "tuning", "kinsect_data", "return_speed_cm_s"):
        assert before[key] == after[key], "Unexpected change to " + before["ga"] + ": " + key


def verify_hashes(protected):
    for path, expected in protected.items():
        assert sha(ROOT / path) == expected, "Protected package changed: " + path


def verify_successor_protection(original, batches=APPROVED_BATCHES):
    """Recognize only explicit later transactions with a proven backup -> save chain."""
    protected = dict(original)
    for folder, gas in batches:
        batch = ROOT / "Saved/Maintenance/KinsectCoAttack20261023" / folder
        if not (batch / "intent.json").exists():
            continue
        successor = json.loads((batch / "intent.json").read_text(encoding="utf-8"))
        applied = json.loads((batch / "applied.json").read_text(encoding="utf-8"))
        verified = json.loads((batch / "verified.json").read_text(encoding="utf-8"))
        assert applied["success"] and verified["success"]
        assert set(applied["ga_sha256"]) == set(gas)
        assert verified["ga_sha256"] == applied["ga_sha256"]
        verify_hashes(successor["backup_sha256"])
        for ga in gas:
            path = disk(ga).relative_to(ROOT).as_posix()
            backup = (batch / "Backup" / disk(ga).name).relative_to(ROOT).as_posix()
            assert protected[path] == successor["backup_sha256"][backup], "Unproven successor baseline: " + ga
            protected[path] = applied["ga_sha256"][ga]
    verify_hashes(protected)


def ground_batch():
    assets = [snapshot(ga, None) for ga in BATCH_GAS]
    rows = {row["ga"]: row for _, _, row in assets}
    targets = {ga: ground_target(row) for ga, row in rows.items()}
    dump("inspection.json", {"snapshots": rows, "target_mvs": targets})
    intent_path = OUT / "intent.json"
    if not APPLY:
        if not intent_path.exists():
            dump("audit.json", {"success": True, "snapshots": rows, "target_mvs": targets})
        else:
            intent = json.loads(intent_path.read_text(encoding="utf-8"))
            assert set(intent["before"]) == set(BATCH_GAS)
            assert targets == intent["target_mvs"], "Authored attack windows changed"
            applied = json.loads((OUT / "applied.json").read_text(encoding="utf-8"))
            for ga, row in rows.items():
                verify_target(row, targets[ga])
                verify_unchanged(intent["before"][ga], row)
                assert sha(disk(ga)) == applied["ga_sha256"][ga], "Saved GA changed: " + ga
            batch_index = next(i for i, (folder, _) in enumerate(APPROVED_BATCHES) if folder == BATCH_FOLDER)
            verify_successor_protection(intent["protected_sha256"], APPROVED_BATCHES[batch_index + 1:])
            verify_hashes(intent["backup_sha256"])
            dump("verified.json", {"success": True, "snapshots": rows, "target_mvs": targets,
                                   "ga_sha256": applied["ga_sha256"],
                                   "protected_count": len(intent["protected_sha256"])})
        unreal.log("[CoAttackBatch:" + BATCH_FOLDER + "] Read-only audit/verification succeeded")
        return

    assert not intent_path.exists(), "Transaction exists; inspect postconditions, never replay Apply"
    audit = json.loads((OUT / "audit.json").read_text(encoding="utf-8"))
    assert audit["success"] and audit["target_mvs"] == targets, "Audit first; source mapping changed"
    for ga, row in rows.items():
        verify_unchanged(audit["snapshots"][ga], row)
        # Native CoAttack export covers every source field without Python wrapper addresses.
        assert audit["snapshots"][ga]["coattack"] == row["coattack"], "CoAttack changed since audit"
    dirty = {p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    assert not dirty.intersection(BATCH_GAS), "Selected GA has unsaved edits"
    for row in rows.values():
        assert "bEnabled=True" not in row["coattack"] and not row["segment_motion_values"], "Unexpected enabled baseline"
    selected = {disk(ga) for ga in BATCH_GAS}
    files = set((ROOT / "Content/Blueprints/Ability/InsectGlaive").glob("*.uasset"))
    files.update(disk(path) for path in (COMBAT, COMBO, RUNTIME, WEAPON))
    files.update(disk(row[key]) for row in rows.values() for key in ("montage", "kinsect_data"))
    protected = {p.relative_to(ROOT).as_posix(): sha(p) for p in files - selected}
    backups = {}
    for ga in BATCH_GAS:
        backup = OUT / "Backup" / disk(ga).name
        assert not backup.exists(), "Preserve previous backup: " + str(backup)
        backup.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(disk(ga), backup)
        backups[backup.relative_to(ROOT).as_posix()] = sha(backup)
        assert sha(backup) == sha(disk(ga)), "Backup mismatch"
    # Freeze every selected original package before any in-memory mutation or save.
    dump("intent.json", {"before": rows, "target_mvs": targets,
                         "protected_sha256": protected, "backup_sha256": backups})
    after_rows = {}
    for bp, cdo, before in assets:
        ga = before["ga"]
        co = read_field(cdo, "co_attack")
        mapping = ",".join("(" + index + ",10.0)" for index in targets[ga])
        co.import_text('(bEnabled=True,MeshTag="WeaponTrace",Socket="IG_FrontTip",'
                       'LocalOffset=(X=0,Y=0,Z=0),SegmentMotionValues=(' + mapping + '))')
        cdo.set_editor_property("co_attack", co)
        _, _, after = snapshot(ga, None)
        verify_target(after, targets[ga])
        verify_unchanged(before, after)
        after_rows[ga] = after
    for bp, _, before in assets:
        assert unreal.EditorAssetLibrary.save_loaded_asset(bp, only_if_is_dirty=False), before["ga"]
    verify_hashes(protected)
    dump("applied.json", {"success": True, "snapshots": after_rows,
                          "ga_sha256": {ga: sha(disk(ga)) for ga in BATCH_GAS}})
    unreal.log("[CoAttackBatch:" + BATCH_FOLDER + "] Saved only approved GA CoAttack defaults; cold readback required")


def audit_descending_thrust():
    assert not APPLY, "Special descending thrust is read-only; no asset Apply is needed"
    ga = "/Game/Blueprints/Ability/InsectGlaive/GA_IG_JiXiTuCi"
    cdo = blueprint_cdo(unreal.load_asset(ga), unreal)
    montage = read_field(cdo, "landing_montage")
    assert montage, "Missing existing ground Over Montage"
    paths = (ga, COMBAT, read_field(cdo, "attack_montage").get_path_name(), montage.get_path_name())
    before = {path: sha(disk(path)) for path in paths}
    mv = float(read_field(cdo, "kinsect_dive_motion_value"))
    assert mv == 10.0, "Unexpected inherited special dive MV"
    assert not read_field(read_field(cdo, "co_attack"), "enabled"), "Do not enable ordinary ground coattack on aerial GA"
    montage_text = export(montage, "landing.current.copy")
    windows = attack_windows(montage_text)
    last = max(windows, key=lambda row: row["end_s"])
    assert last["index"] == 3 and {row["index"] for row in windows} == {2, 3}, windows
    segments = as_sequence(read_field(cdo, "attack_segments"))
    assert len(segments) == 4, "Preserve four original weapon segments"
    combat = unreal.load_asset(COMBAT)
    interval = float(read_field(combat, "awakened_pierce_hit_interval"))
    speed = float(read_field(read_field(combat, "kinsect_data"), "flight_speed"))
    assert interval > 0 and speed > 0
    kinsect = read_field(combat, "kinsect_data")
    mesh = read_field(kinsect, "kinsect_mesh")
    if not isinstance(mesh, unreal.SkeletalMesh):
        mesh = unreal.load_asset(str(mesh))
    assert mesh, "Missing actual kinsect mesh"
    bounds = mesh.get_imported_bounds()
    visual = {"mesh": mesh.get_path_name(),
              "imported_center_xyz": [bounds.origin.x, bounds.origin.y, bounds.origin.z],
              "imported_extent_xyz": [bounds.box_extent.x, bounds.box_extent.y, bounds.box_extent.z]}
    references = set(re.findall(r'AnimReference="/Script/Engine.AnimSequence\x27([^\x27]+)', montage_text))
    assert len(references) == 1, "Endpoint audit currently expects the existing single-sequence Over"
    sequence = unreal.load_asset(next(iter(references)))
    root_samples = []
    for time in (0.0, last["end_s"], last["end_s"] + 0.2, sequence.get_play_length()):
        time = min(time, sequence.get_play_length())
        pose = unreal.AnimationLibrary.get_bone_pose_for_time(sequence, unreal.Name("root"), time, False)
        root_samples.append({"time_s": time, "root_local_xyz": [pose.translation.x, pose.translation.y, pose.translation.z]})
    assert before == {path: sha(disk(path)) for path in paths}, "Read-only audit changed package files"
    dump("verified.json", {"success": True, "ga": ga, "kinsect_mv": mv,
                           "shared_pierce_interval_s": interval, "species_flight_speed_cm_s": speed,
                           "landing_montage": montage.get_path_name(), "ground_windows": windows,
                           "original_weapon_segment_count": len(segments), "unchanged_sha256": before,
                           "kinsect_visual": visual, "over_root_samples": root_samples})
    unreal.log("[CoAttackDescendingThrust] Read-only native-default/final-ground-window audit succeeded")


def main():
    if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world():
        raise RuntimeError("Stop PIE before running the asset transaction")
    if DESCENDING_THRUST:
        return audit_descending_thrust()
    if BATCH_FOLDER:
        return ground_batch()
    bp, cdo, before = snapshot()
    dump("inspection.json", before)
    assert abs(before["delay_s"] - .2) < 1.e-6, "Do not silently change shared combat tuning"
    assert before["return_speed_cm_s"] > 0, "Invalid original entry/return speed"
    assert len(before["segments"]) == 2
    assert all("CONTACT_ONLY" in s["multi_hit_policy"] and s["damage_effect"] for s in before["segments"])
    assert any("AttackCollision" in n.get("NotifyName", "") for n in before["notifies"])
    if not APPLY:
        if (OUT / "intent.json").exists():
            intent = json.loads((OUT / "intent.json").read_text(encoding="utf-8"))
            verify_target(before)
            assert before["cdo_except_coattack"] == intent["before"]["cdo_except_coattack"], "Other GA properties changed"
            assert before["segments"] == intent["before"]["segments"], "Original weapon segments changed"
            verify_successor_protection(intent["protected_sha256"])
            assert sha(OUT / "Backup/GA_IG_TuCi.uasset") == intent["backup_sha256"]
            if (OUT / "applied.json").exists():
                applied = json.loads((OUT / "applied.json").read_text(encoding="utf-8"))
                assert sha(disk(GA)) == applied["ga_sha256"], "Pilot package changed since application"
            dump("verified.json", {"success": True, "snapshot": before,
                                    "ga_sha256": sha(disk(GA)), "protected_count": len(intent["protected_sha256"])})
        else:
            dump("audit.json", {"success": True, "snapshot": before})
        unreal.log("[CoAttackPilot] Read-only audit/verification succeeded")
        return

    assert not (OUT / "intent.json").exists(), "Transaction exists; verify before any retry"
    dirty = {p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    assert GA not in dirty, "Pilot GA has unsaved edits"
    assert "bEnabled=True" not in before["coattack"] and not before["segment_motion_values"], "Unexpected pilot baseline"
    files = list((ROOT / "Content/Blueprints/Ability/InsectGlaive").glob("*.uasset"))
    files += [disk(p) for p in (COMBAT, COMBO, RUNTIME, MONTAGE, WEAPON, before["kinsect_data"])]
    protected = {p.relative_to(ROOT).as_posix(): sha(p) for p in files if p != disk(GA)}
    backup = OUT / "Backup/GA_IG_TuCi.uasset"
    backup.parent.mkdir(parents=True, exist_ok=True)
    assert not backup.exists(), "Preserve previous backup"
    shutil.copy2(disk(GA), backup)
    dump("intent.json", {"before": before, "protected_sha256": protected, "backup_sha256": sha(backup)})

    co = read_field(cdo, "co_attack")
    co.import_text('(bEnabled=True,MeshTag="WeaponTrace",Socket="IG_FrontTip",LocalOffset=(X=0,Y=0,Z=0),SegmentMotionValues=((0,10.0),(1,10.0)))')
    cdo.set_editor_property("co_attack", co)
    _, _, after = snapshot()
    verify_target(after)
    assert after["cdo_except_coattack"] == before["cdo_except_coattack"], "Other GA properties changed"
    assert after["segments"] == before["segments"], "Original weapon segments changed"
    assert unreal.EditorAssetLibrary.save_loaded_asset(bp, only_if_is_dirty=False), "Pilot save failed"
    for path, expected in protected.items():
        assert sha(ROOT / path) == expected, "Protected package changed: " + path
    dump("applied.json", {"success": True, "snapshot": after, "ga_sha256": sha(disk(GA))})
    unreal.log("[CoAttackPilot] Saved only GA_IG_TuCi; cold readback still required")


main()
