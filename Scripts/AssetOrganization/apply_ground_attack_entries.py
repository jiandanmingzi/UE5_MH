"""Six requested entry routes only; default audit, --apply / -MHGZGroundEntriesApply writes.

Uses the existing native exporter/parser and AnimationSeamLibrary, not protected Python fields.
Backs up four Montages and two GA Blueprints. No source Sequence, Combo, or exit retiming recipe.
Open editor: ue_remote.py. Closed: Python commandlet with -MHGZGroundEntriesApply.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import sys
import time
import unreal

ROOT = Path(unreal.Paths.project_dir())
OUT = ROOT / 'Saved/Maintenance/GroundAttackEntryFix'
OUT.mkdir(parents=True, exist_ok=True)
sys.path.insert(0, str(ROOT / 'Scripts'))
from ue_asset_utils import as_sequence, blueprint_cdo, export_fields, export_records, map_items, read_field, split_export_fields

MONTAGE_ROOT = '/Game/Weapons/InsectGlaive/Anims/Montage/'
GA_ROOT = '/Game/Blueprints/Ability/InsectGlaive/'
SOURCE_ROOT = '/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/'
MONTAGES = ['AM_IG_ShangLao', 'AM_IG_JiaSha', 'AM_IG_BaDaoZhan', 'AM_IG_TuJinHuiXuan']
GA_NAMES = ['GA_IG_ShangLao', 'GA_IG_JiaSha', 'GA_IG_TuJinHuiXuanZhan']
WRITE_PATHS = [MONTAGE_ROOT + name for name in MONTAGES] + [GA_ROOT + name for name in GA_NAMES[1:]]
RM_COPY = '/Game/Weapons/InsectGlaive/Anims/Sequences/Generated/AS_IG_FeiShenYueRuShangLao_RM'


def number(row, key, default=0):
    return float(row.get(key, default))


def dump(asset, label):
    task = unreal.AssetExportTask()
    task.object = asset
    task.filename = str(OUT / (label + '.copy'))
    task.exporter = unreal.ObjectExporterT3D()
    task.automated = True
    task.prompt = False
    task.replace_identical = True
    if not unreal.Exporter.run_asset_export_task(task):
        raise RuntimeError('Native export failed: ' + label)
    text = Path(task.filename).read_text(encoding='utf-8-sig')
    slots = export_records(text, 'SlotAnimTracks')
    assert len(slots) == 1
    track = export_fields(slots[0]['AnimTrack'])
    clips = []
    for raw in split_export_fields(track['AnimSegments'][1:-1]):
        row = export_fields(raw)
        path = row['AnimReference'].split("'")[1]
        seq = unreal.load_asset(path)
        start, end = number(row, 'AnimStartTime'), number(row, 'AnimEndTime')
        rate = number(row, 'AnimPlayRate', 1)
        effective = rate * read_field(seq, 'rate_scale')
        clips.append({'asset': path, 'source_start': start, 'source_end': end, 'play_rate': rate,
                      'loops': number(row, 'LoopingCount', 1), 'start': number(row, 'StartPos'),
                      'duration': (end - start) / effective, 'effective_rate': effective})
    events = []
    for row in export_records(text, 'Notifies'):
        events.append({'name': row['NotifyName'].strip('"'), 'guid': row['Guid'],
                       'start': number(row, 'LinkValue'),
                       'end': number(row, 'LinkValue') + number(row, 'Duration'),
                       'track': row.get('TrackIndex', '0'),
                       'payload': {k: row[k] for k in ('Notify', 'NotifyStateClass') if k in row},
                       'row': row})
    return {'segments': clips, 'sections': export_records(text, 'CompositeSections'),
            'notifies': events, 'length': asset.get_play_length()}


def source_clip(name, start, end, rate=1):
    path = SOURCE_ROOT + name
    seq = unreal.load_asset(path)
    assert seq is not None
    return {'asset': seq.get_path_name(), 'source_start': start, 'source_end': end,
            'play_rate': rate, 'loops': 1}


def layout(clips):
    position = 0
    for clip in clips:
        clip['start'] = position
        seq = unreal.load_asset(clip['asset'])
        clip['effective_rate'] = clip['play_rate'] * read_field(seq, 'rate_scale')
        clip['duration'] = (clip['source_end'] - clip['source_start']) / clip['effective_rate']
        position += clip['duration']
    return position


def section(name, clip, clips, next_name='', source=None):
    time_value = clips[clip]['start']
    if source is not None:
        time_value += (source - clips[clip]['source_start']) / clips[clip]['effective_rate']
    return {'name': name, 'next': next_name, 'time': time_value}


def event(index, start, end, clone=False):
    return {'old_index': index, 'start': start, 'end': end, 'clone': clone}


def source_time(clip, source):
    return clip['start'] + (source - clip['source_start']) / clip['effective_rate']


def remap(old, clips, index, old_clip, new_clip):
    before, after = old['segments'][old_clip], clips[new_clip]
    notify = old['notifies'][index]
    def convert(t):
        source = before['source_start'] + (t - before['start']) * before['effective_rate']
        return source_time(after, source)
    return event(index, convert(notify['start']), convert(notify['end']))


def recipes(before):
    plans = {}
    old = before['AM_IG_ShangLao']
    assert len(old['segments']) == 7 and len(old['notifies']) == 9
    clips = [source_clip('AS_Unsh_R_HengSaoShangLao', 0, old['segments'][1]['source_end']),
             source_clip('AS_Unsh_R_HengSaoShangLao', 0, old['segments'][1]['source_end']),
             source_clip('AS_Unsh_FeiShenYueRuShangLao', 0,
                         unreal.load_asset(SOURCE_ROOT + 'AS_Unsh_FeiShenYueRuShangLao').get_play_length(), 2),
             source_clip('AS_Unsh_R_ShangLao', 0, old['segments'][6]['source_end'])]
    layout(clips)
    phases = [event(0, clips[1]['start'] + .0001, clips[1]['start'] + clips[1]['duration'] - .0001),
              event(2, clips[2]['start'] + .0001, clips[2]['start'] + clips[2]['duration'] - .0001),
              event(3, clips[3]['start'] + .0001, clips[3]['start'] + clips[3]['duration'] - .0001),
              event(0, .0001, clips[0]['duration'] - .0001, True)]
    collision = remap(old, clips, 1, 1, 1)
    back_collision = dict(collision, start=collision['start'] - clips[1]['start'],
                          end=collision['end'] - clips[1]['start'], clone=True)
    plans['AM_IG_ShangLao'] = {'segments': clips, 'sections': [
        section('Entry_From_TuJinHuiXuanZhan_Back', 0, clips, 'Core'),
        section('Entry_From_HengSao', 1, clips, 'Core_From_Bridge'),
        section('Entry_From_FeiShenYueRu', 2, clips, 'Core_From_Bridge'),
        section('Core', 3, clips, 'Core_From_Bridge'),
        section('Core_From_Bridge', 3, clips, source=7/60)],
        'notifies': phases + [collision, back_collision] + [remap(old, clips, i, 6, 3) for i in range(4, 9)]}

    old = before['AM_IG_JiaSha']
    assert len(old['segments']) == 4 and len(old['notifies']) == 7
    clips = [dict(old['segments'][i]) for i in (0, 1, 3)]
    layout(clips)
    plans['AM_IG_JiaSha'] = {'segments': clips, 'sections': [
        section('Entry_From_TuJinHuiXuanZhan', 0, clips, 'Entry_From_TuCi'),
        section('Entry_From_TuCi', 1, clips, 'Core'), section('Core', 2, clips)],
        'notifies': [remap(old, clips, 0, 1, 1)] + [remap(old, clips, i, 3, 2) for i in range(2, 7)]}

    old = before['AM_IG_TuJinHuiXuan']
    assert len(old['segments']) == 6 and len(old['notifies']) == 7
    clips = [dict(c) for c in old['segments']]
    clips += [source_clip('AS_Unsh_TuJinHuiXuan', 0, old['segments'][4]['source_start']),
              source_clip('AS_Unsh_TuJinHuiXuan', old['segments'][4]['source_start'], old['segments'][5]['source_start'])]
    layout(clips)
    plans['AM_IG_TuJinHuiXuan'] = {'segments': clips, 'sections': [
        section('Entry', 0, clips, 'Charge'), section('Charge', 4, clips, 'Core'),
        section('Core', 5, clips), section('Entry_From_Idle', 6, clips, 'Charge'),
        section('Release_From_Idle', 7, clips, 'Core')],
        'notifies': [event(i, row['start'], row['end']) for i, row in enumerate(old['notifies'])]}
    return plans


def maps(bp):
    cdo = blueprint_cdo(bp, unreal)
    return {str(k): str(v) for k, v in map_items(read_field(cdo, 'entry_section_by_transition_id'))}


def verify(assets, baseline, plans):
    result = {name: dump(assets[MONTAGE_ROOT + name], name + '-after') for name in MONTAGES}
    for name, plan in plans.items():
        actual = result[name]
        assert len(actual['segments']) == len(plan['segments']), name
        for found, want in zip(actual['segments'], plan['segments']):
            assert found['asset'] == want['asset'] and found['loops'] == want['loops'], name
            for key in ('source_start', 'source_end', 'play_rate', 'start'):
                assert abs(found[key] - want[key]) < 2e-5, (name, key, found[key], want[key])
        found_sections = {row['SectionName'].strip('"'): row for row in actual['sections']}
        assert set(found_sections) == {row['name'] for row in plan['sections']}
        for want in plan['sections']:
            found = found_sections[want['name']]
            assert abs(number(found, 'LinkValue') - want['time']) < 2e-5
            assert found.get('NextSectionName', '""').strip('"') == want['next']
        assert len(actual['notifies']) == len(plan['notifies'])
        by_guid = {row['guid']: row for row in actual['notifies']}
        for spec in plan['notifies']:
            if spec['clone']:
                continue
            prior = baseline[name]['notifies'][spec['old_index']]
            found = by_guid[prior['guid']]
            assert found['name'] == prior['name'] and found['payload'] == prior['payload']
            assert found['track'] == prior['track']
            assert abs(found['start'] - spec['start']) < 2e-5 and abs(found['end'] - spec['end']) < 2e-5
    draw = result['AM_IG_BaDaoZhan']
    assert abs(draw['segments'][1]['source_start'] - 67/60) < 2e-5
    assert draw['segments'][0] == baseline['AM_IG_BaDaoZhan']['segments'][0]
    assert draw['segments'][1]['source_end'] == baseline['AM_IG_BaDaoZhan']['segments'][1]['source_end']
    assert maps(assets[GA_ROOT + GA_NAMES[0]]) == baseline['ga_maps'][GA_NAMES[0]]
    for name, key, target in [(GA_NAMES[1], 'ShangLao_Y_JiaSha', 'Entry_From_TuCi'),
                              (GA_NAMES[2], 'ForwardYB_TuJinHuiXuanZhan', 'Entry_From_Idle')]:
        expected = dict(baseline['ga_maps'][name], **{key: target})
        assert maps(assets[GA_ROOT + name]) == expected
    charge = blueprint_cdo(assets[GA_ROOT + GA_NAMES[2]], unreal)
    charge_fields = {key: str(read_field(charge, key)) for key in ('charge_entry_section', 'charge_section', 'attack_section')}
    assert charge_fields == baseline['charge_fields']
    assert {str(k): str(v) for k, v in map_items(read_field(charge, 'uncharged_exit_section_by_entry'))} == {'Entry_From_Idle': 'Release_From_Idle'}
    copy = unreal.load_asset(RM_COPY)
    source = unreal.load_asset(SOURCE_ROOT + 'AS_Unsh_FeiShenYueRuShangLao')
    assert copy is not None and read_field(copy, 'enable_root_motion')
    assert not read_field(source, 'enable_root_motion') and read_field(source, 'rate_scale') == 1
    assert abs(copy.get_play_length() - source.get_play_length()) < 1e-6
    assert read_field(copy, 'rate_scale') == read_field(source, 'rate_scale')
    for sample_time in (0, source.get_play_length()/2, source.get_play_length()):
        for bone in source.data_model_interface.get_bone_track_names():
            # These bone tracks are copied, not rebaked or reoriented.
            original_pose = unreal.AnimationLibrary.get_bone_pose_for_time(source, unreal.Name(bone), sample_time, False)
            copied_pose = unreal.AnimationLibrary.get_bone_pose_for_time(copy, unreal.Name(bone), sample_time, False)
            for component, axes in (('translation', 'xyz'), ('rotation', 'xyzw'), ('scale3d', 'xyz')):
                left, right = getattr(original_pose, component), getattr(copied_pose, component)
                assert all(abs(getattr(left, axis) - getattr(right, axis)) < 1e-5 for axis in axes), bone
    (OUT / 'after.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--apply', action='store_true')
    args, _ = parser.parse_known_args()
    args.apply = args.apply or '-MHGZGroundEntriesApply' in unreal.SystemLibrary.get_command_line().split()
    paths = [MONTAGE_ROOT + n for n in MONTAGES] + [GA_ROOT + n for n in GA_NAMES]
    assets = {path: unreal.load_asset(path) for path in paths}
    assert all(assets.values())
    done = OUT / 'applied.json'
    if done.exists():
        # This later checkpoint verifies the new GA aliases and approved asset state.
        if (ROOT / 'Saved/Maintenance/AttackEntryHoldRemoval/applied.json').exists():
            sys.path.insert(0, str(ROOT / 'Scripts/AssetOrganization'))
            from setup_montage_segment_blend import remove_synthetic_bridges
            remove_synthetic_bridges(args, ROOT, hold_mode=True)
            unreal.log('[GroundEntries] Later attack-hold cleanup verified; no writes')
            return
        baseline = json.loads((OUT / 'before.json').read_text(encoding='utf-8'))
        plans = json.loads((OUT / 'recipes.json').read_text(encoding='utf-8'))
        # The later approved bridge removal supersedes TuJin's old eight-clip recipe.
        removed = ROOT / 'Saved/Maintenance/MontageBridgeRemoval'
        if (removed / 'applied.json').exists():
            replacement = json.loads((removed / 'recipes.json').read_text(encoding='utf-8'))['AM_IG_TuJinHuiXuan']
            layout(replacement['segments'])
            plans['AM_IG_TuJinHuiXuan'] = replacement
        verify(assets, baseline, plans)
        unreal.log('[GroundEntries] Previously applied; disk verification passed, no writes')
        return
    baseline = {name: dump(assets[MONTAGE_ROOT + name], name + '-before') for name in MONTAGES}
    baseline['ga_maps'] = {name: maps(assets[GA_ROOT + name]) for name in GA_NAMES}
    charge = blueprint_cdo(assets[GA_ROOT + GA_NAMES[2]], unreal)
    baseline['charge_fields'] = {key: str(read_field(charge, key)) for key in ('charge_entry_section', 'charge_section', 'attack_section')}
    plans = recipes(baseline)
    source = unreal.load_asset(SOURCE_ROOT + 'AS_Unsh_FeiShenYueRuShangLao')
    baseline['308_properties'] = {key: read_field(source, key) for key in ('rate_scale', 'enable_root_motion', 'force_root_lock')}
    (OUT / 'before.json').write_text(json.dumps(baseline, ensure_ascii=False, indent=2), encoding='utf-8')
    (OUT / 'recipes.json').write_text(json.dumps(plans, ensure_ascii=False, indent=2), encoding='utf-8')
    if not args.apply:
        unreal.log('[GroundEntries] Read-only snapshot/recipes complete; no assets modified')
        return
    assert unreal.EditorLevelLibrary.get_game_world() is None, 'Stop PIE first'
    dirty = {p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    assert not dirty.intersection(WRITE_PATHS), 'Selected asset has unsaved changes'
    assert not unreal.EditorAssetLibrary.does_asset_exist(RM_COPY), 'Generated 308 copy already exists; inspect before reapplying'
    backup = ROOT / 'Saved/AssetBackups' / ('GroundAttackEntries-' + str(time.time_ns()))
    manifest = []
    for path in WRITE_PATHS:
        disk = ROOT / 'Content' / (path.removeprefix('/Game/') + '.uasset')
        target = backup / disk.relative_to(ROOT)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(disk, target)
        assert hashlib.sha256(disk.read_bytes()).digest() == hashlib.sha256(target.read_bytes()).digest()
        manifest.append({'asset': path, 'backup': str(target), 'before_sha256': hashlib.sha256(disk.read_bytes()).hexdigest()})
    (backup / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')
    lib = unreal.MHGZAnimationSeamLibrary
    copy = lib.create_root_motion_copy(source, RM_COPY)
    assert copy is not None
    assets[RM_COPY] = copy
    plans['AM_IG_ShangLao']['segments'][2]['asset'] = copy.get_path_name()
    (OUT / 'recipes.json').write_text(json.dumps(plans, ensure_ascii=False, indent=2), encoding='utf-8')
    for name, plan in plans.items():
        assert lib.rebuild_single_slot_montage(assets[MONTAGE_ROOT + name], json.dumps(plan)), name
    assert lib.trim_segment_start(assets[MONTAGE_ROOT + 'AM_IG_BaDaoZhan'], 1, 67/60)
    for name in ('AM_IG_ShangLao', 'AM_IG_BaDaoZhan'):
        assert lib.set_montage_segment_blend(assets[MONTAGE_ROOT + name], .06)
    for name, key, target in [(GA_NAMES[1], 'ShangLao_Y_JiaSha', 'Entry_From_TuCi'),
                              (GA_NAMES[2], 'ForwardYB_TuJinHuiXuanZhan', 'Entry_From_Idle')]:
        bp = assets[GA_ROOT + name]
        cdo = blueprint_cdo(bp, unreal)
        mapping = dict(map_items(read_field(cdo, 'entry_section_by_transition_id')))
        mapping[unreal.Name(key)] = unreal.Name(target)
        bp.modify(); cdo.modify()
        cdo.set_editor_property('entry_section_by_transition_id', mapping)
    charge.set_editor_property('uncharged_exit_section_by_entry', {unreal.Name('Entry_From_Idle'): unreal.Name('Release_From_Idle')})
    verify(assets, baseline, plans)
    saved = []
    for path in [RM_COPY, *WRITE_PATHS]:
        assert unreal.EditorAssetLibrary.save_loaded_asset(assets[path], only_if_is_dirty=False), path
        saved.append(path)
    (OUT / 'applied.json').write_text(json.dumps({'backup': str(backup), 'saved': saved}, ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.log('[GroundEntries] Saved six assets; re-open in an independent process for verification')


if __name__ == '__main__':
    main()
