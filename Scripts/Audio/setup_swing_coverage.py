"""M3 frozen recorded swing recipes. Audit by default; --apply/-MHGZSwingCoverageApply writes.

Reuses pilot factories, native exports and exact preservation guards. Saves only
missing owned audio, recipe point Notifies and GA audio opt-in/identity. Backups
precede mutation, partial saves are checkpointed, retries never duplicate points.
Use a fresh-process audit after apply. Commandlet: -AllowCommandletAudio, no -nosound.
"""
from pathlib import Path
import copy
import json
import re
import shutil
import sys
import unreal

ROOT = Path(unreal.Paths.project_dir()).resolve()
sys.path.insert(0, str(ROOT / 'Scripts/Audio'))
import setup_swing_pilot as pilot
from ue_asset_utils import blueprint_cdo, read_field, export_records, export_fields
from plan_swing_coverage import fingerprint

OUT = ROOT / 'Saved/Audio/RiseIG/M3'
OWNER = 'mhgz_audio_m3_swing_v1'
STATE = OUT / 'manifest.json'
pilot.inspector.OUT = OUT / 'NativeExports'


def source_asset(path, cls):
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        return None
    obj = unreal.load_asset(path)
    owner = unreal.EditorAssetLibrary.get_metadata_tag(obj, 'MHGZ.Audio.Owner')
    if not isinstance(obj, cls) or owner not in (OWNER, pilot.OWNER):
        raise RuntimeError('Refuse non-owned/wrong-class audio: ' + path)
    return obj


def audio_sources(plan, writing):
    result = []
    att = pilot.existing(pilot.ATTENUATION, unreal.SoundAttenuation)
    con = pilot.existing(pilot.CONCURRENCY, unreal.SoundConcurrency)
    if not att or not con:
        raise RuntimeError('Shared pilot settings must already exist')
    for source in plan['sources']:
        if pilot.digest(source['wav']) != source['sha256']:
            raise RuntimeError('Confirmed source changed')
        wave = source_asset(source['wave'], unreal.SoundWave)
        if wave is None and writing:
            task = unreal.AssetImportTask()
            task.filename = source['wav']
            task.destination_path, task.destination_name = source['wave'].rsplit('/', 1)
            task.automated, task.save, task.replace_existing = True, False, False
            task.factory = unreal.SoundFactory()
            unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
            wave = unreal.load_asset(source['wave'])
            if not isinstance(wave, unreal.SoundWave):
                raise RuntimeError('Wave import failed')
            wave.set_editor_property('looping', False)
            unreal.EditorAssetLibrary.set_metadata_tag(wave, 'MHGZ.Audio.SourceSHA256', source['sha256'])
            pilot.own(wave, OWNER)
        cue = source_asset(source['cue'], unreal.SoundCue)
        if cue is None and wave and writing:
            factory = unreal.SoundCueFactoryNew()
            factory.set_editor_property('initial_sound_waves', [wave])
            cue, fresh = pilot.create(source['cue'], unreal.SoundCue, factory, OWNER)
            if fresh:
                cue.set_editor_property('volume_multiplier', 1.0)
                cue.set_editor_property('attenuation_settings', att)
                cue.set_editor_property('concurrency_set', [con])
                pilot.own(cue, OWNER)
        if wave:
            if (unreal.EditorAssetLibrary.get_metadata_tag(wave, 'MHGZ.Audio.SourceSHA256') != source['sha256']
                    or int(read_field(wave, 'num_channels')) != 1 or read_field(wave, 'looping')
                    or abs(float(read_field(wave, 'duration')) - source['seconds']) > .002):
                raise RuntimeError('Wave differs from confirmed source')
        if cue:
            native = pilot.inspector.text_export(cue)
            if ('FirstNode=' not in native or 'SoundNodeWavePlayer' not in native
                    or source['wave'] not in native or 'bLooping=True' in native
                    or read_field(cue, 'attenuation_settings') != att
                    or list(read_field(cue, 'concurrency_set')) != [con]):
                raise RuntimeError('Cue graph/settings changed')
        result.append({'event_id': source['event_id'], 'wave': source['wave'], 'cue': source['cue'],
                       'linked': bool(wave and cue), 'sha256': source['sha256']})
    return result


def authored(montage, snapshot):
    result = []
    for _, notify in pilot.notify_objects(montage):
        native = next(row for row in snapshot['existing_notifies'] if re.search(
            r"'(?:[^']*:)?" + re.escape(notify.get_name()) + r"'", row.get('Notify', '')))
        sound = read_field(notify, 'sound')
        result.append({'event_tag': read_field(notify, 'event_tag').export_text(),
                       'time_s': float(native.get('LinkValue', 0)), 'guid': native['Guid'],
                       'track_index': int(native.get('TrackIndex', 0)),
                       'sound': sound.get_path_name() if sound else None,
                       'legacy_index': int(read_field(notify, 'legacy_attack_segment_index')),
                       'volume': float(read_field(notify, 'volume_multiplier')),
                       'pitch': float(read_field(notify, 'pitch_multiplier'))})
    return result


def find_recipe(rows, recipe, sources):
    matches = [r for r in rows if r['event_tag'] == '(TagName="' + recipe['event_tag'] + '")']
    if len(matches) > 1:
        raise RuntimeError('Duplicate recipe point: ' + recipe['event_tag'])
    if not matches:
        return None
    row = matches[0]
    cue = sources[recipe['event_id']]['cue']
    if (abs(row['time_s'] - recipe['montage_time_estimate_s']) > 1e-5
            or row['sound'] != cue + '.' + cue.rsplit('/', 1)[-1] or row['legacy_index'] != -1):
        raise RuntimeError('Existing recipe point was edited: ' + recipe['event_tag'])
    return row


def tracks(asset):
    return export_records(pilot.inspector.text_export(asset), 'AnimNotifyTracks')


def verify_tracks(before, after):
    if after[:len(before)] != before:
        raise RuntimeError('Original Notify tracks changed')
    if len(after) > len(before) and (len(after) != len(before) + 1
                                    or after[-1].get('TrackName') != '"SwingAudio"'):
        raise RuntimeError('Unexpected track added')


def verify_montage(before, after):
    """Keep exact author data; log narrow native absolute-link cache refreshes.

    UE AnimLinkableElement::RefreshSegmentOnLoad recomputes SegmentLength.
    The measured FeiShen difference is 1.783334 -> 1.783333; LinkValue and
    Duration are unchanged. At the same single-clip absolute end UE can clear
    the derived segment reference. Relative/proportional links are never exempt.
    """
    normalized = copy.deepcopy(after)
    originals = {r['Guid']: r for r in before['existing_notifies']}
    notes = []
    for row in normalized['existing_notifies']:
        old = originals.get(row.get('Guid'))
        if not old:
            continue
        for container in ('self', 'EndLink'):
            a = old if container == 'self' else export_fields(old.get('EndLink', '()'))
            b = row if container == 'self' else export_fields(row.get('EndLink', '()'))
            if a.get('SegmentLength') == b.get('SegmentLength'):
                continue
            absolute = all(link.get(field, 'Absolute') == 'Absolute'
                           for link in (a, b) for field in ('LinkMethod', 'CachedLinkMethod'))
            derived = {'SegmentIndex', 'SegmentBeginTime', 'SegmentLength', 'LinkedSequence'}
            # Measured HouChe end-link canonicalization: only a single-clip,
            # unchanged absolute final time, exact invalid cache markers, and
            # the same authored fields/linked Montage are admissible.
            cleared_end = (container == 'EndLink' and absolute and len(before['segments']) == 1
                and a.get('LinkValue') == b.get('LinkValue') and 'LinkValue' in a
                and abs(float(a['LinkValue']) - before['length_s']) <= 1.1e-6
                and a.get('SegmentIndex') == '0' and float(a.get('SegmentBeginTime', 0)) == 0
                and before['segments'][0]['sequence'] in a.get('LinkedSequence', '')
                and abs(float(a.get('SegmentLength', -2)) - before['length_s']) <= 1.1e-6
                and b.get('SegmentIndex') in (None, '-1') and 'LinkedSequence' not in b
                and b.get('SegmentBeginTime') == b.get('SegmentLength') == '-1.000000'
                and {k: v for k, v in a.items() if k not in derived}
                    == {k: v for k, v in b.items() if k not in derived})
            if cleared_end:
                notes.append({'guid': row['Guid'], 'field': 'EndLink.segment_cache',
                              'kind': 'native_absolute_single_clip_end_unlinked', 'before': a, 'after': b})
                row['EndLink'] = old['EndLink']
                continue
            if (not absolute or 'SegmentLength' not in a or 'SegmentLength' not in b
                    or abs(float(a['SegmentLength']) - float(b['SegmentLength'])) > 1.1e-6):
                continue  # The original exact guard below must reject this.
            if container == 'EndLink' and ({k: v for k, v in a.items() if k != 'SegmentLength'}
                                         != {k: v for k, v in b.items() if k != 'SegmentLength'}):
                continue
            notes.append({'guid': row['Guid'], 'field': container + '.SegmentLength',
                          'before': a['SegmentLength'], 'after': b['SegmentLength']})
            if container == 'self':
                row['SegmentLength'] = a['SegmentLength']
            else:
                row['EndLink'] = old['EndLink']
    pilot.verify_originals(before, normalized)
    return notes


def verify_checkpoints(plan, state):
    expected = dict(plan['protected_package_hashes']) if state is None else state['expected_hashes']
    for path, sha in expected.items():
        if pilot.digest(path) != sha:
            raise RuntimeError('Changed/stale package, inspect before resume: ' + path)


def checkpoint(state, asset):
    filename = pilot.inspector.disk(asset.get_path_name())
    state['expected_hashes'][filename.as_posix()] = pilot.digest(filename)
    state['saved_packages'].append(asset.get_path_name())
    pilot.save_json(STATE, state)


def main():
    plan = json.loads((OUT / 'plan.json').read_text(encoding='utf-8'))
    if plan['plan_sha256'] != fingerprint({k: v for k, v in plan.items() if k != 'plan_sha256'}):
        raise RuntimeError('Frozen plan modified')
    if '-MHGZSwingCoverageDiagnose' in unreal.SystemLibrary.get_command_line():
        # Collect the complete saved state without accepting differences or writing Content.
        current = {'montages': [pilot.inspector.montage(m['path']) for m in plan['montages']],
                   'cdos': {a['path']: pilot.inspector.text_export(blueprint_cdo(unreal.load_asset(a['path']), unreal))
                            for a in plan['abilities']}}
        pilot.save_json(OUT / 'readonly-differences.json', current)
        print('SWING_COVERAGE_DIAGNOSE readonly snapshots=' + str(len(current['montages'])))
        return
    writing = '--apply' in sys.argv or '-MHGZSwingCoverageApply' in unreal.SystemLibrary.get_command_line()
    state = json.loads(STATE.read_text(encoding='utf-8')) if STATE.exists() else None
    if state and state['plan_sha256'] != plan['plan_sha256']:
        raise RuntimeError('Manifest belongs to another plan')
    verify_checkpoints(plan, state)
    for name in plan['required_tags']:
        pilot.tag(name)
    source_map = {s['event_id']: s for s in plan['sources']}
    # Preflight ALL existing recipe structures before any Content write.
    loaded = {}
    baseline_cdos = {} if state is None else state['before_cdos']
    baseline_tracks = {} if state is None else state['before_tracks']
    for item in plan['montages']:
        path = item['path']
        asset = unreal.load_asset(path)
        current = pilot.inspector.montage(path)
        verify_montage(item['before'], current)
        existing = authored(asset, current)
        expected = {r['event_tag'] for r in item['recipes']}
        if any(r['event_tag'].removeprefix('(TagName="').removesuffix('")') not in expected for r in existing):
            raise RuntimeError('Unknown pre-existing swing point; preserve/review: ' + path)
        for recipe in item['recipes']:
            find_recipe(existing, recipe, source_map)
        if path not in baseline_tracks:
            baseline_tracks[path] = tracks(asset)
        verify_tracks(baseline_tracks[path], tracks(asset))
        loaded[path] = asset
    for ability in plan['abilities']:
        path = ability['path']
        bp = unreal.load_asset(path)
        cdo = blueprint_cdo(bp, unreal)
        current = pilot.inspector.text_export(cdo)
        if path not in baseline_cdos:
            baseline_cdos[path] = current
        if pilot.non_audio_cdo(current) != pilot.non_audio_cdo(baseline_cdos[path]):
            raise RuntimeError('GA non-audio defaults changed')
        loaded[path] = bp
    if writing and state is None:
        expected = dict(plan['protected_package_hashes'])
        # Preserve all existing audio/GameplayCue packages, including hit audio.
        for folder in ('Content/Weapons/InsectGlaive/Audio', 'Content/GameplayCues'):
            for filename in (ROOT / folder).rglob('*.uasset'):
                expected[filename.as_posix()] = pilot.digest(filename)
        state = {'plan_sha256': plan['plan_sha256'], 'phase': 'intent', 'expected_hashes': expected,
                 'before_cdos': baseline_cdos, 'before_tracks': baseline_tracks,
                 'saved_packages': [], 'original_packages': {}}
        for path, asset in loaded.items():
            filename = pilot.inspector.disk(path)
            backup = OUT / 'Backup' / filename.relative_to(ROOT)
            backup.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(filename, backup)
            state['original_packages'][filename.as_posix()] = {'backup': str(backup), 'sha256': pilot.digest(filename)}
        pilot.save_json(STATE, state)
    source_report = audio_sources(plan, writing)
    montage_report = []
    for item in plan['montages']:
        path, recipes = item['path'], item['recipes']
        asset = loaded[path]
        current = pilot.inspector.montage(path)
        rows = authored(asset, current)
        missing = [r for r in recipes if not find_recipe(rows, r, source_map)]
        if missing and writing:
            if not unreal.AnimationLibrary.is_valid_anim_notify_track_name(asset, pilot.TRACK):
                unreal.AnimationLibrary.add_animation_notify_track(asset, pilot.TRACK, unreal.LinearColor(.2, .65, 1, 1))
            for recipe in missing:
                notify = unreal.AnimationLibrary.add_animation_notify_event(asset, pilot.TRACK,
                            recipe['montage_time_estimate_s'], unreal.AnimNotify_SwingSound)
                notify.set_editor_property('event_tag', pilot.tag(recipe['event_tag']))
                notify.set_editor_property('sound', unreal.load_asset(source_map[recipe['event_id']]['cue']))
            current = pilot.inspector.montage(path)
            verify_montage(item['before'], current)
            verify_tracks(baseline_tracks[path], tracks(asset))
            rows = authored(asset, current)
            if not all(find_recipe(rows, r, source_map) for r in recipes):
                raise RuntimeError('In-memory recipe failure, do not save')
            pilot.save(asset)
            checkpoint(state, asset)
        montage_report.append({'path': path, 'authored': rows,
            'linked': all(find_recipe(rows, r, source_map) for r in recipes),
            'absolute_link_cache_refresh': verify_montage(item['before'], current),
            'routes': item['routes'], 'missing_evidence': item['missing_evidence']})
    ability_report = []
    for ability in plan['abilities']:
        path, identity = ability['path'], ability['audio_identity']
        bp = loaded[path]
        cdo = blueprint_cdo(bp, unreal)
        enabled = bool(read_field(cdo, 'use_authored_swing_audio'))
        same_identity = read_field(cdo, 'audio_identity_tag').export_text() == '(TagName="' + identity + '")'
        if writing and not (enabled and same_identity):
            cdo.set_editor_property('use_authored_swing_audio', True)
            cdo.set_editor_property('audio_identity_tag', pilot.tag(identity))
            if pilot.non_audio_cdo(pilot.inspector.text_export(cdo)) != pilot.non_audio_cdo(baseline_cdos[path]):
                raise RuntimeError('GA non-audio mutation, do not save')
            pilot.save(bp)
            checkpoint(state, bp)
        ability_report.append({'path': path, 'enabled': bool(read_field(cdo, 'use_authored_swing_audio')),
                               'audio_identity': read_field(cdo, 'audio_identity_tag').export_text()})
    if state:
        verify_checkpoints(plan, state)
    linked = all(s['linked'] for s in source_report) and all(m['linked'] for m in montage_report)
    linked = linked and all(a['enabled'] and a['audio_identity'] == '(TagName="' + wanted['audio_identity'] + '")'
                            for a, wanted in zip(ability_report, plan['abilities']))
    if writing and not linked:
        raise RuntimeError('Postconditions incomplete')
    report = {'mode': 'apply' if writing else 'audit', 'linked': linked,
              'plan_sha256': plan['plan_sha256'], 'sources': source_report,
              'montages': montage_report, 'abilities': ability_report,
              'protected_original_notifies_tracks_segments_sections': True,
              'complete_original_audio_coverage': False, 'pie_audible': 'not_tested'}
    pilot.save_json(OUT / ('apply.json' if writing else 'audit.json'), report)
    if state and linked:
        state['phase'] = 'saved_pending_independent_reload' if writing else 'independently_reloaded'
        pilot.save_json(STATE, state)
    print('SWING_COVERAGE ' + json.dumps({'linked': linked, 'montages': len(montage_report),
          'abilities': len(ability_report), 'points': sum(len(m['authored']) for m in montage_report),
          'sources': len(source_report), 'phase': state['phase'] if state else 'not_applied'}))


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        pilot.save_json(OUT / 'failure.json', {'error': repr(error),
            'note': 'Inspect actual saved packages/checkpoints before retry; no automatic rollback.'})
        raise
