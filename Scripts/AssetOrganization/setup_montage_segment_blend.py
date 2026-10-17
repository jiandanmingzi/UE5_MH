"""Audit/install the graph or configure internal blending on multi-sequence Montages.

Default: read-only. --apply backs up and saves only ABP_MH_Character, after validation.
Open editor: Scripts/ue_remote.py --script <this file> -- --apply.
Closed editor: UnrealEditor-Cmd MHGZ.uproject -run=pythonscript -script=<this file> -MHGZSegmentBlendApply -NullRHI -unattended.
--configure-montages scans /Game; --apply saves only multi-sequence Montage metadata.
The batch duration defaults to 0.05s. --verify-montages checks the saved batch read-only.
Commandlet batch switches: -MHGZSegmentBlendMontages / -MHGZSegmentBlendVerify.
Metadata mode does not change segments, sections, notifies, BlendIn/Out, ABP or Sequences.
--remove-synthetic-bridges removes the five approved Blend clips in three Montages,
retiming sections/notifies; source Sequences, holds, charge and blend settings stay intact.
Commandlet switches: -MHGZSegmentBridgeRemoval / -MHGZSegmentBridgeVerify.
--remove-attack-holds removes ten approved attack EntryHolds, merging collapsed
Sections and remapping four GA entry maps; dodge holds and source assets are untouched.
Commandlet switches: -MHGZAttackHoldRemoval / -MHGZAttackHoldVerify.
--weapon-grip installs the post-inertialization grip correction and opts in only
FeiYuan's explicit ErLian entry->Core seam. --verify-weapon-grip is read-only.
Commandlet switches: -MHGZWeaponGrip / -MHGZWeaponGripVerify; apply remains explicit.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import shutil
import sys
import time
import unreal


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def montage_snapshot(asset, out, phase):
    from ue_asset_utils import decode_native_export, export_fields, export_records, split_export_fields
    name = asset.get_path_name().split('.', 1)[0]
    filename = out / phase / (name.removeprefix('/Game/').replace('/', '__') + '.copy')
    filename.parent.mkdir(parents=True, exist_ok=True)
    task = unreal.AssetExportTask()
    task.object, task.filename = asset, str(filename)
    task.exporter = unreal.ObjectExporterT3D()
    task.automated, task.prompt, task.replace_identical = True, False, True
    if not unreal.Exporter.run_asset_export_task(task):
        raise RuntimeError('Montage export failed: ' + name)
    text = decode_native_export(filename.read_bytes())
    sequences, slots, segments = set(), [], []
    for slot in export_records(text, 'SlotAnimTracks'):
        slots.append(slot.get('SlotName', 'DefaultSlot').strip('"'))
        track = export_fields(slot['AnimTrack'])
        for segment in split_export_fields(track.get('AnimSegments', '()')[1:-1]):
            row = export_fields(segment)
            reference = row.get('AnimReference', '')
            match = re.search(r"/Script/Engine\.AnimSequence(?:Base)?'([^']+)'", reference)
            if match:
                sequences.add(match.group(1))
                seq = unreal.load_asset(match.group(1))
                rate = float(row.get('AnimPlayRate', 1))
                effective = rate * float(seq.get_editor_property('rate_scale'))
                if effective <= 0:
                    raise RuntimeError('Unsupported nonpositive clip rate')
                start, end = float(row.get('AnimStartTime', 0)), float(row['AnimEndTime'])
                loops = int(row.get('LoopingCount', 1))
                segments.append({'asset': match.group(1), 'start': float(row.get('StartPos', 0)),
                                 'source_start': start, 'source_end': end, 'play_rate': rate,
                                 'loops': loops, 'duration': (end - start) * loops / effective,
                                 'effective_rate': effective})
            elif reference and reference != 'None':
                raise RuntimeError('Unsupported segment reference: ' + reference)
    metadata = [item for item in unreal.AnimationLibrary.get_meta_data(asset)
                if isinstance(item, unreal.MHGZMontageSegmentBlendSettings)]
    if len(metadata) > 1:
        raise RuntimeError('Duplicate blend metadata: ' + name)
    settings = None
    if metadata:
        item = metadata[0]
        settings = {'enabled': bool(item.get_editor_property('enabled')),
                    'duration': float(item.get_editor_property('duration'))}
        escaped = re.escape(item.get_name())
        # Strip only this metadata's declaration, value block and array reference.
        # The rest of the native export (including notify payloads) must stay identical.
        text = re.sub(r'(?ms)^\s*Begin Object [^\n]*\bName="' + escaped
                      + r'"[^\n]*\n.*?^\s*End Object\r?\n', '', text)
        text = re.sub(r'(?m)^\s*MetaData\(\d+\)=[^\n]*\b' + escaped + r"'[^\n]*\n", '', text)
    events = []
    for row in export_records(text, 'Notifies'):
        start = float(row.get('LinkValue', 0))
        end = start + float(row.get('Duration', 0))
        if 'NotifyStateClass' in row:
            end = float(export_fields(row['EndLink']).get('LinkValue', end))
        events.append({'start': start, 'end': end, 'row': row})
    unaffected = re.sub(r'(?m)^\s*(?:CompositeSections|SlotAnimTracks|Notifies|BranchingPointMarkers)\(\d+\)=.*\n', '', text)
    unaffected = re.sub(r'(?m)^\s*(?:SequenceLength|NumberOfFrames|NumberOfKeys)=.*\n', '', unaffected)
    return {'asset': name, 'sequences': sorted(sequences), 'slots': slots,
            'settings': settings, 'structure_sha256': hashlib.sha256(text.encode()).hexdigest(),
            'segments': segments, 'sections': export_records(text, 'CompositeSections'),
            'notifies': events, 'length': asset.get_play_length(),
            'unaffected_sha256': hashlib.sha256(unaffected.encode()).hexdigest()}


BRIDGE_REMOVALS = {
    'AM_IG_FeiYuan': ['AS_IG_FeiYuan_ErLianCoreBlend', 'AS_IG_FeiYuan_HengSaoCoreBlend', 'AS_IG_FeiYuan_RecoveryBlend'],
    'AM_IG_HengSao': ['AS_IG_HengSao_CoreBlend'],
    'AM_IG_TuJinHuiXuan': ['AS_IG_TuJinHuiXuan_EntryToCoreBlend'],
}

ATTACK_HOLD_REMOVALS = {
    'AM_IG_ChongYin': ['AS_IG_ChongYin_HengSaoEntryHold', 'AS_IG_ChongYin_IdleEntryHold'],
    'AM_IG_FeiShenYueRu': ['AS_IG_FeiShenYueRu_EntryHold'],
    'AM_IG_HengSao': ['AS_IG_HengSao_EntryHold'],
    'AM_IG_HouChe': ['AS_IG_HouChe_EntryHold'],
    'AM_IG_JiaSha': ['AS_IG_JiaSha_TuJinEntryHold'],
    'AM_IG_SiLianYinZhan': ['AS_IG_SiLianYinZhan_SharedEntryHold'],
    'AM_IG_TuCi': ['AS_IG_TuCi_FeiYuanEntryHold', 'AS_IG_TuCi_EntryHold'],
    'AM_IG_TuJinHuiXuan': ['AS_IG_TuJinHuiXuan_EntryHold'],
}
HOLD_SECTION_ALIASES = {
    'AM_IG_FeiShenYueRu': {'Entry_WithBlend': 'Core'},
    'AM_IG_JiaSha': {'Entry_From_TuJinHuiXuanZhan': 'Entry_From_TuCi'},
    'AM_IG_SiLianYinZhan': {'Entry_From_ShangLaoFeiShenHouChe': 'Entry_From_ErLian'},
    'AM_IG_TuCi': {'Entry_From_FeiYuan': 'Core'},
}


def ability_entry_maps(asset):
    from ue_asset_utils import blueprint_cdo, map_items
    cdo = blueprint_cdo(asset, unreal)
    fields = ('entry_section_by_source_state', 'entry_section_by_transition_id')
    return {'default': str(cdo.get_editor_property('default_entry_section')),
            **{key: {str(k): str(v) for k, v in map_items(cdo.get_editor_property(key))} for key in fields}}


def write_ability_entry_maps(asset, maps):
    from ue_asset_utils import blueprint_cdo
    cdo = blueprint_cdo(asset, unreal)
    asset.modify(); cdo.modify()
    cdo.set_editor_property('default_entry_section', unreal.Name(maps['default']))
    for key in ('entry_section_by_source_state', 'entry_section_by_transition_id'):
        cdo.set_editor_property(key, {unreal.Name(k): unreal.Name(v) for k, v in maps[key].items()})


def ability_unchanged_hash(asset, out, phase):
    from ue_asset_utils import blueprint_cdo, decode_native_export
    exports = []
    for obj in (asset, blueprint_cdo(asset, unreal)):
        task = unreal.AssetExportTask()
        task.object = obj
        filename = out / phase / (obj.get_name() + '.copy')
        filename.parent.mkdir(parents=True, exist_ok=True)
        task.filename, task.exporter = str(filename), unreal.ObjectExporterT3D()
        task.automated, task.prompt, task.replace_identical = True, False, True
        if not unreal.Exporter.run_asset_export_task(task):
            raise RuntimeError('GA/CDO export failed')
        text = decode_native_export(filename.read_bytes())
        text = re.sub(r'(?m)^\s*(?:DefaultEntrySection|EntrySectionBySourceState|EntrySectionByTransitionID)(?:\(\d+\))?=.*\n', '', text)
        exports.append(text)
    return hashlib.sha256('\n'.join(exports).encode()).hexdigest()


def verify_bridge_removal(current, baseline, recipe):
    if current['slots'] != baseline['slots'] or current['settings'] != baseline['settings']:
        raise RuntimeError('Slot/blend metadata changed')
    if current['unaffected_sha256'] != baseline['unaffected_sha256']:
        raise RuntimeError('Non-timing Montage fields or notify payloads changed')
    if len(current['segments']) != len(recipe['segments']):
        raise RuntimeError('Remaining clip count mismatch')
    position = 0.0
    for clip, want in zip(current['segments'], recipe['segments']):
        for key in ('asset', 'loops'):
            if clip[key] != want[key]:
                raise RuntimeError('Remaining clip identity mismatch')
        for key in ('source_start', 'source_end', 'play_rate'):
            if abs(clip[key] - want[key]) > 2e-5:
                raise RuntimeError('Remaining source cut/rate changed')
        if abs(clip['start'] - position) > 2e-5:
            raise RuntimeError('Noncontinuous remaining clips')
        position += clip['duration']
    if abs(current['length'] - position) > 2e-5:
        raise RuntimeError('Montage length mismatch')
    if len(current['sections']) != len(recipe['sections']):
        raise RuntimeError('Section count changed')
    for row, want in zip(current['sections'], recipe['sections']):
        if row['SectionName'].strip('"') != want['name'] or row.get('NextSectionName', '').strip('"') != want['next']:
            raise RuntimeError('Section routing changed')
        if abs(float(row.get('LinkValue', 0)) - want['time']) > 2e-5:
            raise RuntimeError('Section time mismatch')
    if len(current['notifies']) != len(baseline['notifies']):
        raise RuntimeError('Notify count changed')
    link_fields = {'LinkedMontage', 'SegmentIndex', 'SegmentBeginTime', 'SegmentLength',
                   'LinkValue', 'LinkedSequence', 'EndLink', 'Duration', 'TriggerTimeOffset', 'EndTriggerTimeOffset'}
    for event, old, want in zip(current['notifies'], baseline['notifies'], recipe['notifies']):
        stable = lambda row: {key: value for key, value in row.items() if key not in link_fields}
        if stable(event['row']) != stable(old['row']):
            raise RuntimeError('Notify identity/track/authored settings changed')
        if max(abs(event['start'] - want['start']), abs(event['end'] - want['end'])) > 2e-5:
            raise RuntimeError('Notify source-phase retiming mismatch')


def current_tujin_idle_recipe(recipe):
    """Current shared Entry; discard superseded appended Idle/Release clips."""
    updated = json.loads(json.dumps(recipe))
    clips = updated['segments']
    if len(clips) != 6 or any(not clips[i]['asset'].endswith(
            'AS_Unsh_TuJinHuiXuan.AS_Unsh_TuJinHuiXuan') for i in (4, 5)):
        raise RuntimeError('Unexpected historical TuJin idle layout')
    if (min(abs(clips[4]['source_start'] - frame/60) for frame in (0, 14)) > 2e-5
            or abs(clips[4]['source_end'] - 55/60) > 2e-5
            or abs(clips[5]['source_start'] - 55/60) > 2e-5
            or abs(clips[5]['source_end'] - 67/60) > 2e-5):
        raise RuntimeError('Unexpected historical TuJin idle source cut')
    sections = {row['name']: row for row in updated['sections']}
    if list(sections) != ['Entry', 'Charge', 'Core', 'Entry_From_Idle', 'Release_From_Idle']:
        raise RuntimeError('Unexpected historical TuJin sections')
    if any(row['end'] > sections['Entry_From_Idle']['time'] + 2e-5 for row in updated['notifies']):
        raise RuntimeError('Notify in discarded idle region needs an explicit plan')
    updated['segments'] = clips[:4]
    updated['sections'] = updated['sections'][:3]
    return updated


def remove_synthetic_bridges(args, root, hold_mode=False):
    from ue_asset_utils import montage_removal_recipe
    latest = root / 'Saved/Maintenance/AttackEntryHoldRemoval'
    if not hold_mode and (latest / 'applied.json').exists():
        # Intentional later cleanup supersedes the old bridge-only snapshot.
        return remove_synthetic_bridges(args, root, hold_mode=True)
    removals = ATTACK_HOLD_REMOVALS if hold_mode else BRIDGE_REMOVALS
    out = latest if hold_mode else root / 'Saved/Maintenance/MontageBridgeRemoval'
    out.mkdir(parents=True, exist_ok=True)
    prefix = '/Game/Weapons/InsectGlaive/Anims/Montage/'
    generated = '/Game/Weapons/InsectGlaive/Anims/Sequences/Generated/'
    assets = {name: unreal.load_asset(prefix + name) for name in removals}
    ga_prefix = '/Game/Blueprints/Ability/InsectGlaive/'
    abilities = {name: unreal.load_asset(ga_prefix + name.replace('AM_', 'GA_', 1))
                 for name in HOLD_SECTION_ALIASES} if hold_mode else {}
    if not all(assets.values()):
        raise RuntimeError('Approved Montage missing')
    done = out / 'applied.json'
    if done.exists():
        if args.apply:
            raise RuntimeError('Bridge removal already applied; verify without apply')
        baseline = json.loads((out / 'before.json').read_text(encoding='utf-8'))
        recipes = json.loads((out / 'recipes.json').read_text(encoding='utf-8'))
        for name, asset in assets.items():
            recipe = recipes[name]
            if hold_mode and name == 'AM_IG_TuJinHuiXuan':
                recipe = current_tujin_idle_recipe(recipe)
            verify_bridge_removal(montage_snapshot(asset, out, 'verify'), baseline['assets'][name], recipe)
        for name, asset in abilities.items():
            if ability_entry_maps(asset) != baseline['ga_after'][name]:
                raise RuntimeError('GA entry routing mismatch: ' + name)
            if ability_unchanged_hash(asset, out, 'verify') != baseline['ga_unchanged'][name]:
                raise RuntimeError('Unexpected non-entry GA edit: ' + name)
        for disk, digest in baseline['unchanged_files'].items():
            shared_entry = root / 'Saved/Maintenance/TuJinSharedEntry20261015/applied.json'
            if disk.replace('\\', '/') == 'Content/Blueprints/Ability/InsectGlaive/GA_IG_TuJinHuiXuanZhan.uasset' and shared_entry.exists():
                # The later approved route cleanup saved this exact GA only.
                digest = json.loads(shared_entry.read_text(encoding='utf-8'))['after_hashes'][disk.replace('\\', '/')]
            if sha(root / disk) != digest:
                raise RuntimeError('Out-of-scope asset changed: ' + disk)
        report = json.loads(done.read_text(encoding='utf-8'))
        report['disk_verified'] = True
        (out / 'disk-verify.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
        unreal.log('[MontageBridgeRemoval] Saved batch verified; no writes')
        return
    if getattr(args, 'verify_bridge_removal', False) or getattr(args, 'verify_attack_holds', False):
        raise RuntimeError('No completed bridge removal checkpoint')
    baseline = {'assets': {name: montage_snapshot(asset, out, 'before') for name, asset in assets.items()}}
    recipes = {}
    for name, row in baseline['assets'].items():
        if row['slots'] != ['DefaultSlot']:
            raise RuntimeError('Approved recipe requires the existing single DefaultSlot')
        removed = {generated + value + '.' + value for value in removals[name]}
        recipes[name] = montage_removal_recipe(row, removed,
            HOLD_SECTION_ALIASES.get(name, {}) if hold_mode else None, clamp_accept_ends=hold_mode)
    if hold_mode:
        baseline['ga_before'] = {name: ability_entry_maps(asset) for name, asset in abilities.items()}
        baseline['ga_after'] = {}
        baseline['ga_unchanged'] = {}
        for name, asset in abilities.items():
            if asset is None:
                raise RuntimeError('Approved GA missing: ' + name)
            aliases = HOLD_SECTION_ALIASES[name]
            old = baseline['ga_before'][name]
            baseline['ga_after'][name] = {'default': aliases.get(old['default'], old['default']),
                **{key: {k: aliases.get(v, v) for k, v in old[key].items()}
                   for key in ('entry_section_by_source_state', 'entry_section_by_transition_id')}}
            baseline['ga_unchanged'][name] = ability_unchanged_hash(asset, out, 'before')
    (out / 'audit.json').write_text(json.dumps(baseline, ensure_ascii=False, indent=2), encoding='utf-8')
    (out / 'recipes.json').write_text(json.dumps(recipes, ensure_ascii=False, indent=2), encoding='utf-8')
    if not args.apply:
        unreal.log('[MontageBridgeRemoval] Read-only recipes for five approved clips ready')
        return
    if (out / 'before.json').exists():
        raise RuntimeError('Incomplete removal checkpoint; inspect progress before retrying')
    if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is not None:
        raise RuntimeError('Stop PIE before changing Montage clips')
    dirty = unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()
    if any(asset.get_outermost() in dirty for asset in [*assets.values(), *abilities.values()]):
        raise RuntimeError('Target Montage has unsaved edits')
    selected = {Path('Content') / (prefix.removeprefix('/Game/') + name + '.uasset') for name in assets}
    selected.update(Path('Content') / (ga_prefix.removeprefix('/Game/') + name.replace('AM_', 'GA_', 1) + '.uasset')
                    for name in abilities)
    # Hash /Game Montages and directly associated assets only, not a full content validation.
    batch = json.loads((root / 'Saved/Maintenance/MontageSegmentBlendBatch/audit.json').read_text(encoding='utf-8'))
    paths = {Path('Content') / (path.removeprefix('/Game/') + '.uasset') for path in batch['assets']}
    paths.update(Path('Content') / (clip['asset'].split('.')[0].removeprefix('/Game/') + '.uasset')
                 for row in baseline['assets'].values() for clip in row['segments'])
    paths.update(path.relative_to(root) for path in (root / 'Content/Blueprints/Ability/InsectGlaive').glob('GA_IG_*.uasset'))
    paths.update([Path('Content/Weapons/InsectGlaive/Data/DA_IG_Combo.uasset'),
                  Path('Content/Blueprints/Characters/Demo/Animation/ABP_MH_Character.uasset')])
    baseline['unchanged_files'] = {str(path.relative_to(root) if path.is_absolute() else path): sha(root / path)
                                   for path in paths if path not in selected}
    backup = root / 'Saved/AssetBackups' / (('AttackEntryHoldRemoval-' if hold_mode else 'MontageBridgeRemoval-') + str(time.time_ns()))
    for disk in selected:
        for suffix in ('.uasset', '.uexp', '.ubulk'):
            source = (root / disk).with_suffix(suffix)
            if source.exists():
                target = backup / disk.with_suffix(suffix)
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, target)
                if sha(source) != sha(target):
                    raise RuntimeError('Backup verification failed')
    baseline['backup'] = str(backup)
    (out / 'before.json').write_text(json.dumps(baseline, ensure_ascii=False, indent=2), encoding='utf-8')
    (backup / 'manifest.json').write_text(json.dumps(baseline, ensure_ascii=False, indent=2), encoding='utf-8')
    # Validate all in-memory postconditions before saving any selected Montage.
    for name, asset in assets.items():
        if not unreal.MHGZAnimationSeamLibrary.rebuild_single_slot_montage(asset, json.dumps(recipes[name])):
            raise RuntimeError('Native removal recipe rejected: ' + name)
        verify_bridge_removal(montage_snapshot(asset, out, 'after'), baseline['assets'][name], recipes[name])
    for name, asset in abilities.items():
        write_ability_entry_maps(asset, baseline['ga_after'][name])
        if ability_entry_maps(asset) != baseline['ga_after'][name] or ability_unchanged_hash(asset, out, 'after') != baseline['ga_unchanged'][name]:
            raise RuntimeError('GA entry remapping postcondition failed: ' + name)
    saved = []
    to_save = {prefix + name: asset for name, asset in assets.items()}
    to_save.update({ga_prefix + name.replace('AM_', 'GA_', 1): asset for name, asset in abilities.items()})
    for path, asset in to_save.items():
        if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=True):
            raise RuntimeError('Asset save failed: ' + path)
        saved.append(path)
        (out / 'progress.json').write_text(json.dumps(saved, indent=2), encoding='utf-8')
    result = {'backup': str(backup), 'saved': saved, 'removed': removals}
    (out / 'applied.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.log('[MontageBridgeRemoval] Saved approved removal batch: ' + str(len(saved)) + ' assets')


def configure_montages(args, root):
    out = root / 'Saved/Maintenance/MontageSegmentBlendBatch'
    out.mkdir(parents=True, exist_ok=True)
    sys.path.insert(0, str(root / 'Scripts'))
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.scan_paths_synchronous(['/Game'], force_rescan=False)
    assets = {}
    for data in registry.get_assets_by_path('/Game', recursive=True):
        if str(data.asset_class_path.asset_name) == 'AnimMontage':
            asset = data.get_asset()
            if asset is None:
                raise RuntimeError('Could not load Montage: ' + str(data.package_name))
            assets[str(data.package_name)] = asset
    before = {path: montage_snapshot(asset, out, 'verify' if args.verify_montages else 'before')
              for path, asset in sorted(assets.items())}
    targets = [path for path, row in before.items() if len(row['sequences']) > 1]
    def disk(path):
        return root / ('Content/' + path.removeprefix('/Game/') + '.uasset')
    hashes = {path: sha(disk(path)) for path in assets}
    if args.verify_montages:
        applied = json.loads((out / 'applied.json').read_text(encoding='utf-8'))
        baseline = json.loads((out / 'before.json').read_text(encoding='utf-8'))
        hold_checkpoint = root / 'Saved/Maintenance/AttackEntryHoldRemoval/applied.json'
        if hold_checkpoint.exists():
            if sorted(assets) != sorted(baseline['assets']):
                raise RuntimeError('Montage inventory changed after saved cleanup')
            remove_synthetic_bridges(args, root, hold_mode=True)
            for path in applied['targets']:
                settings = before[path]['settings']
                if not settings or not settings['enabled'] or abs(settings['duration'] - applied['duration']) > 1e-6:
                    raise RuntimeError('Persisted 0.05s settings changed: ' + path)
            report = dict(applied, disk_verified=True, scanned=len(assets),
                          configured=len(applied['targets']), current_multi_sequence=len(targets))
            (out / 'disk-verify.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
            return
        if targets != applied['targets'] or sorted(assets) != sorted(baseline['assets']):
            raise RuntimeError('Montage inventory changed after batch save')
        removal = root / 'Saved/Maintenance/MontageBridgeRemoval'
        approved_removal = None
        if (removal / 'applied.json').exists():
            approved_removal = (json.loads((removal / 'before.json').read_text(encoding='utf-8')),
                                json.loads((removal / 'recipes.json').read_text(encoding='utf-8')))
        for path, row in before.items():
            if row['structure_sha256'] != baseline['assets'][path]['structure_sha256']:
                name = path.rsplit('/', 1)[1]
                if not approved_removal or name not in approved_removal[1]:
                    raise RuntimeError('Montage structure changed: ' + path)
                verify_bridge_removal(row, approved_removal[0]['assets'][name], approved_removal[1][name])
            if path in targets:
                settings = row['settings']
                if not settings or not settings['enabled'] or abs(settings['duration'] - applied['duration']) > 1e-6:
                    raise RuntimeError('Saved blend metadata mismatch: ' + path)
            elif hashes[path] != baseline['hashes'][path]:
                raise RuntimeError('Non-target Montage changed: ' + path)
        report = dict(applied, disk_verified=True, scanned=len(assets), configured=len(targets))
        (out / 'disk-verify.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
        unreal.log('[MontageSegmentBlendBatch] ' + json.dumps(report, ensure_ascii=False))
        return
    report = {'scanned': len(assets), 'targets': targets, 'duration': args.duration,
              'assets': before, 'hashes': hashes}
    (out / 'audit.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    if not args.apply:
        unreal.log('[MontageSegmentBlendBatch] ' + json.dumps(
            {key: report[key] for key in ('scanned', 'targets', 'duration')}, ensure_ascii=False))
        return
    if not math.isfinite(args.duration) or args.duration <= 0:
        raise RuntimeError('Batch blend duration must be finite and positive')
    if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is not None:
        raise RuntimeError('Stop PIE before changing Montage metadata')
    dirty = unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()
    if any(assets[path].get_outermost() in dirty for path in targets):
        raise RuntimeError('A target Montage has unsaved changes; refusing to overwrite')
    if (out / 'before.json').exists():
        raise RuntimeError('Batch checkpoint exists; run --verify-montages before another apply')
    backup = root / 'Saved/AssetBackups' / ('MontageSegmentBlendBatch-' + str(time.time_ns()))
    backup.mkdir(parents=True, exist_ok=False)
    for path in targets:
        for suffix in ('.uasset', '.uexp', '.ubulk'):
            source = disk(path).with_suffix(suffix)
            if source.exists():
                target = backup / source.relative_to(root)
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, target)
                if sha(source) != sha(target):
                    raise RuntimeError('Montage backup mismatch: ' + path)
    report['backup'] = str(backup)
    (out / 'before.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    (backup / 'manifest.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    saved = []
    for path in targets:
        asset = assets[path]
        if not unreal.MHGZAnimationSeamLibrary.set_montage_segment_blend(asset, args.duration):
            raise RuntimeError('Metadata configuration failed: ' + path)
        current = montage_snapshot(asset, out, 'after')
        if current['structure_sha256'] != before[path]['structure_sha256']:
            raise RuntimeError('Unexpected structure edit; refusing save: ' + path)
        if not current['settings']['enabled'] or abs(current['settings']['duration'] - args.duration) > 1e-6:
            raise RuntimeError('Metadata postcondition failed: ' + path)
        if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=True):
            raise RuntimeError('Montage save failed: ' + path)
        saved.append(path)
        (out / 'progress.json').write_text(json.dumps(saved, indent=2), encoding='utf-8')
    result = {'targets': targets, 'duration': args.duration, 'backup': str(backup), 'saved': saved}
    (out / 'applied.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.log('[MontageSegmentBlendBatch] ' + json.dumps(result, ensure_ascii=False))


def configure_weapon_grip(args, root):
    """Two-asset transaction; never touch Combo, source clips or notify layout."""
    out = root / 'Saved/Maintenance/FeiYuanWeaponGrip'
    out.mkdir(parents=True, exist_ok=True)
    bp_path = '/Game/Blueprints/Characters/Demo/Animation/ABP_MH_Character'
    montage_path = '/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_FeiYuan'
    sequence_base = '/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/'
    from_path = sequence_base + 'AS_Unsh_R_ErLianFeiYuan'
    to_path = sequence_base + 'AS_Unsh_R_FeiYuan'
    blueprint, montage = unreal.load_asset(bp_path), unreal.load_asset(montage_path)
    if blueprint is None or montage is None:
        raise RuntimeError('Grip target asset missing')
    install = unreal.MHGZAnimationSeamLibrary.install_weapon_grip_constraint
    def disk(path):
        return root / ('Content/' + path.removeprefix('/Game/').split('.')[0] + '.uasset')
    def settings():
        values = [v for v in unreal.AnimationLibrary.get_meta_data(montage)
                  if isinstance(v, unreal.MHGZMontageSegmentBlendSettings)]
        if len(values) != 1:
            raise RuntimeError('Expected exactly one existing blend metadata')
        return values[0]
    def verify(baseline):
        from ue_asset_utils import decode_native_export
        if not install(blueprint, False):
            raise RuntimeError('Grip graph does not match approved wiring')
        item = settings()
        if (not item.get_editor_property('enabled')
                or abs(item.get_editor_property('duration') - .05) > 1e-6
                or not item.get_editor_property('enable_weapon_grip_constraint')
                or item.get_editor_property('weapon_grip_from_sequence').get_path_name().split('.')[0] != from_path
                or item.get_editor_property('weapon_grip_to_sequence').get_path_name().split('.')[0] != to_path
                or abs(item.get_editor_property('weapon_grip_release_duration') - 1 / 30) > 1e-6):
            raise RuntimeError('Grip metadata mismatch')
        current = montage_snapshot(montage, out, 'verify')
        # UE rebuilds cached FAnimLinkableElement::SegmentLength on reload;
        # a 0.383334->0.383333 rounding is not a notify timing edit. Compare
        # everything else byte-for-byte, excluding only this derived cache and
        # the deliberately edited metadata object.
        def canonical(phase):
            path = out / phase / (montage_path.removeprefix('/Game/').replace('/', '__') + '.copy')
            text = decode_native_export(path.read_bytes())
            label = re.escape(settings().get_name())
            text = re.sub(r'(?ms)^\s*Begin Object [^\n]*\bName="' + label
                          + r'"[^\n]*\n.*?^\s*End Object\r?\n', '', text)
            text = re.sub(r'(?m)^\s*MetaData\(\d+\)=[^\n]*\b' + label + r"'[^\n]*\n", '', text)
            return re.sub(r'\bSegmentLength=[-+0-9.eE]+', 'SegmentLength=RECOMPUTED', text)
        if canonical('before') != canonical('verify') or any(
                current[key] != baseline['montage'][key]
                for key in ('segments', 'sections', 'length', 'settings', 'slots', 'sequences')):
            raise RuntimeError('Montage timing/sections/notifies/blending changed')
        for path, digest in baseline['unchanged'].items():
            if sha(root / path) != digest:
                raise RuntimeError('Out-of-scope asset changed: ' + path)
    applied = out / 'applied.json'
    if args.verify_weapon_grip or applied.exists():
        if args.apply:
            raise RuntimeError('Grip transaction exists; use read-only verification')
        baseline = json.loads((out / 'before.json').read_text(encoding='utf-8'))
        result = json.loads(applied.read_text(encoding='utf-8'))
        verify(baseline)
        for path, digest in result['saved_hashes'].items():
            if sha(disk(path)) != digest:
                raise RuntimeError('Saved target has changed: ' + path)
        result['disk_verified'] = True
        (out / 'disk-verify.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
        unreal.log('[WeaponGrip] Independent saved graph/metadata/notify verification passed')
        return
    snapshot = montage_snapshot(montage, out, 'before')
    if not args.apply:
        unreal.log('[WeaponGrip] ' + json.dumps({'graph_installed': install(blueprint, False), 'montage': snapshot}, ensure_ascii=False))
        return
    if (out / 'before.json').exists():
        raise RuntimeError('Incomplete grip transaction; inspect progress before retry')
    if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is not None:
        raise RuntimeError('Stop PIE before graph modification')
    dirty = unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()
    if any(a.get_outermost() in dirty for a in (blueprint, montage)):
        raise RuntimeError('Target has unsaved edits')
    item = settings()
    if not snapshot['settings']['enabled'] or abs(snapshot['settings']['duration'] - .05) > 1e-6:
        raise RuntimeError('Expected approved existing 0.05s blend')
    if snapshot['segments'][0]['asset'].split('.')[0] != from_path or snapshot['segments'][3]['asset'].split('.')[0] != to_path:
        raise RuntimeError('Expected explicit ErLian->Core layout')
    paths = {disk(p).relative_to(root) for p in snapshot['sequences']}
    paths.add(Path('Content/Weapons/InsectGlaive/Data/DA_IG_Combo.uasset'))
    backup = root / 'Saved/AssetBackups' / ('FeiYuanWeaponGrip-' + str(time.time_ns()))
    baseline = {'montage': snapshot, 'backup': str(backup),
                'unchanged': {str(p): sha(root / p) for p in paths}}
    for path in (bp_path, montage_path):
        for suffix in ('.uasset', '.uexp', '.ubulk'):
            source = disk(path).with_suffix(suffix)
            if source.exists():
                target = backup / source.relative_to(root)
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, target)
                if sha(source) != sha(target):
                    raise RuntimeError('Grip backup mismatch')
    (out / 'before.json').write_text(json.dumps(baseline, ensure_ascii=False, indent=2), encoding='utf-8')
    (backup / 'manifest.json').write_text(json.dumps(baseline, ensure_ascii=False, indent=2), encoding='utf-8')
    if not install(blueprint, True):
        raise RuntimeError('Graph install/compile failed; discard this process without saving')
    item.set_editor_property('enable_weapon_grip_constraint', True)
    item.set_editor_property('weapon_grip_from_sequence', unreal.load_asset(from_path))
    item.set_editor_property('weapon_grip_to_sequence', unreal.load_asset(to_path))
    item.set_editor_property('weapon_grip_release_duration', 1 / 30)
    montage.modify()
    verify(baseline)
    saved = {}
    for path, asset in ((bp_path, blueprint), (montage_path, montage)):
        if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
            raise RuntimeError('Grip save failed: ' + path)
        saved[path] = sha(disk(path))
        (out / 'progress.json').write_text(json.dumps(saved, indent=2), encoding='utf-8')
    result = {'backup': str(backup), 'saved_hashes': saved, 'hold': .05, 'release': 1 / 30,
              'from': from_path, 'to': to_path}
    (out / 'applied.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.log('[WeaponGrip] Saved only ABP_MH_Character and AM_IG_FeiYuan')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--apply', action='store_true')
    parser.add_argument('--configure-montages', action='store_true')
    parser.add_argument('--verify-montages', action='store_true')
    parser.add_argument('--remove-synthetic-bridges', action='store_true')
    parser.add_argument('--verify-bridge-removal', action='store_true')
    parser.add_argument('--remove-attack-holds', action='store_true')
    parser.add_argument('--verify-attack-holds', action='store_true')
    parser.add_argument('--weapon-grip', action='store_true')
    parser.add_argument('--verify-weapon-grip', action='store_true')
    parser.add_argument('--duration', type=float, default=0.05)
    args, _ = parser.parse_known_args()
    # UE commandlet switches are not included in Python sys.argv. A dedicated,
    # explicit switch avoids ambiguous nested -script quoting on Windows.
    switches = unreal.SystemLibrary.get_command_line().split()
    args.apply = args.apply or '-MHGZSegmentBlendApply' in switches
    args.configure_montages = args.configure_montages or '-MHGZSegmentBlendMontages' in switches
    args.verify_montages = args.verify_montages or '-MHGZSegmentBlendVerify' in switches
    args.remove_synthetic_bridges = args.remove_synthetic_bridges or '-MHGZSegmentBridgeRemoval' in switches
    args.verify_bridge_removal = args.verify_bridge_removal or '-MHGZSegmentBridgeVerify' in switches
    args.remove_attack_holds = args.remove_attack_holds or '-MHGZAttackHoldRemoval' in switches
    args.verify_attack_holds = args.verify_attack_holds or '-MHGZAttackHoldVerify' in switches
    args.weapon_grip = args.weapon_grip or '-MHGZWeaponGrip' in switches
    args.verify_weapon_grip = args.verify_weapon_grip or '-MHGZWeaponGripVerify' in switches
    root = Path(unreal.Paths.project_dir())
    sys.path.insert(0, str(root / 'Scripts'))
    if sum((args.configure_montages, args.verify_montages, args.remove_synthetic_bridges, args.verify_bridge_removal,
            args.remove_attack_holds, args.verify_attack_holds, args.weapon_grip, args.verify_weapon_grip)) > 1:
        raise RuntimeError('Choose exactly one asset operation')
    if args.weapon_grip or args.verify_weapon_grip:
        if args.verify_weapon_grip and args.apply:
            raise RuntimeError('Verification is read-only')
        configure_weapon_grip(args, root)
        return
    if args.remove_attack_holds or args.verify_attack_holds:
        if args.verify_attack_holds and args.apply:
            raise RuntimeError('Verification is read-only')
        remove_synthetic_bridges(args, root, hold_mode=True)
        return
    if args.remove_synthetic_bridges or args.verify_bridge_removal:
        if args.verify_bridge_removal and args.apply:
            raise RuntimeError('Verification is read-only')
        remove_synthetic_bridges(args, root)
        return
    if args.configure_montages or args.verify_montages:
        if args.verify_montages and args.apply:
            raise RuntimeError('Verification is read-only; do not combine with apply')
        configure_montages(args, root)
        return
    out = root / 'Saved/Maintenance/AttackInertializationStatus'
    out.mkdir(parents=True, exist_ok=True)
    path = '/Game/Blueprints/Characters/Demo/Animation/ABP_MH_Character'
    disk = root / 'Content/Blueprints/Characters/Demo/Animation/ABP_MH_Character.uasset'
    blueprint = unreal.load_asset(path)
    if blueprint is None:
        raise RuntimeError('ABP_MH_Character missing')
    install = unreal.MHGZAnimationSeamLibrary.install_montage_segment_inertialization
    before = sha(disk)
    installed_before = install(blueprint, False)
    backup = None
    if args.apply and not installed_before:
        # Match the project's existing authoring preflight; never compile a live PIE graph.
        if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is not None:
            raise RuntimeError('Stop PIE before changing the AnimBlueprint')
        dirty = unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()
        if blueprint.get_outermost() in dirty:
            raise RuntimeError('ABP has unsaved changes; refusing to overwrite them')
        backup = root / 'Saved/AssetBackups' / ('MontageSegmentBlend-' + str(time.time_ns()))
        backup.mkdir(parents=True, exist_ok=False)
        for suffix in ('.uasset', '.uexp', '.ubulk'):
            source = disk.with_suffix(suffix)
            if source.exists():
                target = backup / source.name
                shutil.copy2(source, target)
                if sha(source) != sha(target):
                    raise RuntimeError('Backup verification failed')
        if not install(blueprint, True):
            raise RuntimeError('Graph insertion/compile failed; do not save; discard the in-memory edit')
        if not unreal.EditorAssetLibrary.save_loaded_asset(blueprint, only_if_is_dirty=True):
            raise RuntimeError('ABP save failed')
    installed_after = install(blueprint, False)
    if args.apply and not installed_after:
        raise RuntimeError('Postcondition failed')
    task = unreal.AssetExportTask()
    task.object = blueprint
    task.filename = str(out / ('ABP_MH_Character-installed.copy' if installed_after else 'ABP_MH_Character-before.copy'))
    task.exporter = unreal.ObjectExporterT3D()
    task.automated = True
    task.prompt = False
    task.replace_identical = True
    if not unreal.Exporter.run_asset_export_task(task):
        raise RuntimeError('Native graph export failed')
    report = {'apply': args.apply, 'asset': path, 'installed_before': installed_before,
              'installed_after': installed_after, 'saved': before != sha(disk),
              'before_sha256': before, 'after_sha256': sha(disk),
              'backup': str(backup) if backup else None, 'montages_configured': 0}
    (out / ('install.json' if args.apply else 'disk-audit.json')).write_text(
        json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.log('[MontageSegmentBlend] ' + json.dumps(report, ensure_ascii=False))


if __name__ == '__main__':
    main()
