"""Frozen MuGu source pool and weapon coverage transactions; audit by default.

Closed editor: Python commandlet -AllowCommandletAudio. Add
-MHGZCurrentWeaponAudioApply only for explicit writes. Fresh-process default audit
confirms disk references; never runs previous M2/M3 apply or compiles C++.
-MHGZCurrentWeaponExpand / --expand-weapon selects UEWeaponCoverage, appending
only its frozen new points and preserving every accepted old sound GUID.
"""
from pathlib import Path
import copy
import hashlib
import json
import shutil
import sys
import unreal

ROOT = Path(unreal.Paths.project_dir()).resolve()
sys.path.insert(0, str(ROOT / 'Scripts/Audio'))
import setup_swing_coverage as coverage
from plan_swing_coverage import fingerprint
from ue_asset_utils import read_field, blueprint_cdo

pilot = coverage.pilot
OUT = ROOT / 'Saved/Audio/RiseIG/Weapon20261019/UEAudio'
EXPANDING = '--expand-weapon' in sys.argv or '-MHGZCurrentWeaponExpand' in unreal.SystemLibrary.get_command_line()
if EXPANDING:
    OUT = OUT.with_name('UEWeaponCoverage')
if '--expand-actions' in sys.argv or '-MHGZCurrentWeaponActions' in unreal.SystemLibrary.get_command_line():
    OUT = OUT.with_name('UEActionRuntime')
OWNER = 'mhgz_audio_current_weapon_20261019_v1'
STATE = OUT / 'manifest.json'
pilot.inspector.OUT = OUT / 'NativeExports'


def load(path):
    return json.loads(path.read_text(encoding='utf-8'))


def non_tool_snapshot(snapshot, guids):
    result = copy.deepcopy(snapshot)
    result['existing_notifies'] = [r for r in result['existing_notifies'] if r['Guid'] not in guids]
    return result


def verify_author_data(before, after, old_guids, new_guids):
    return coverage.verify_montage(non_tool_snapshot(before, old_guids),
                                   non_tool_snapshot(after, new_guids))


def checkpoint(state, asset):
    path = asset.get_path_name()
    disk = pilot.inspector.disk(path)
    state['expected_hashes'][disk.as_posix()] = pilot.digest(disk)
    if path not in state['saved_packages']:
        state['saved_packages'].append(path)
    pilot.save_json(STATE, state)


def source_pool(plan, writing, state):
    attenuation = pilot.existing(pilot.ATTENUATION, unreal.SoundAttenuation)
    concurrency = pilot.existing(pilot.CONCURRENCY, unreal.SoundConcurrency)
    if not attenuation or not concurrency:
        raise RuntimeError('Missing existing shared audio settings')
    waves, report = {}, []
    for source in plan['sources']:
        selected = []
        for variant in source['variants']:
            if pilot.digest(variant['wav']) != variant['sha256']:
                raise RuntimeError('Frozen WAV changed')
            path = variant['wave']
            wave = waves.get(path) or pilot.existing(path, unreal.SoundWave, OWNER)
            if wave is None and writing:
                task = unreal.AssetImportTask()
                task.filename = variant['wav']
                task.destination_path, task.destination_name = path.rsplit('/', 1)
                task.automated, task.save, task.replace_existing = True, False, False
                task.factory = unreal.SoundFactory()
                unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
                wave = unreal.load_asset(path)
                if not isinstance(wave, unreal.SoundWave):
                    raise RuntimeError('Wave import failed: ' + path)
                wave.set_editor_property('looping', False)
                unreal.EditorAssetLibrary.set_metadata_tag(wave, 'MHGZ.Audio.SourceSHA256', variant['sha256'])
                pilot.own(wave, OWNER)
                checkpoint(state, wave)
            if wave:
                if (unreal.EditorAssetLibrary.get_metadata_tag(wave, 'MHGZ.Audio.SourceSHA256') != variant['sha256']
                        or int(read_field(wave, 'num_channels')) != variant['channels']
                        or read_field(wave, 'looping')
                        or abs(float(read_field(wave, 'duration')) - variant['seconds']) > .002):
                    raise RuntimeError('Wave differs from decoded source: ' + path)
                waves[path] = wave
            selected.append(wave)
        cue = pilot.existing(source['cue'], unreal.SoundCue, OWNER)
        if cue is None and all(selected) and writing:
            factory = unreal.SoundCueFactoryNew()
            factory.set_editor_property('initial_sound_waves', selected)
            cue, fresh = pilot.create(source['cue'], unreal.SoundCue, factory, OWNER)
            if not fresh:
                raise RuntimeError('Unexpected Cue creation race')
            cue.set_editor_property('volume_multiplier', 1.0)
            cue.set_editor_property('attenuation_settings', attenuation)
            cue.set_editor_property('concurrency_set', [concurrency])
            unreal.EditorAssetLibrary.set_metadata_tag(cue, 'MHGZ.Audio.GraphSHA256', source['txtp_sha256'])
            pilot.own(cue, OWNER)
            checkpoint(state, cue)
        if cue:
            native = pilot.inspector.text_export(cue)
            if ('FirstNode=' not in native or 'SoundNodeWavePlayer' not in native
                    or ('SoundNodeRandom' in native) != (len(selected) > 1)
                    or any(v['wave'] not in native for v in source['variants'])
                    or 'bLooping=True' in native
                    or read_field(cue, 'attenuation_settings') != attenuation
                    or list(read_field(cue, 'concurrency_set')) != [concurrency]
                    or unreal.EditorAssetLibrary.get_metadata_tag(cue, 'MHGZ.Audio.GraphSHA256') != source['txtp_sha256']):
                raise RuntimeError('Cue graph/settings differ from frozen graph: ' + source['cue'])
        report.append(dict(branch_id=source['branch_id'], event_id=source['event_id'],
                           cue=source['cue'], wave_count=len(selected), linked=bool(cue and all(selected))))
    return report


def verify_recipes(item, rows, sources):
    expected = {r['event_tag']: r for r in item['recipes']}
    if len(expected) != len(rows):
        raise RuntimeError('Unexpected current tool point count: ' + item['path'])
    seen = set()
    for row in rows:
        key = row['event_tag'].removeprefix('(TagName="').removesuffix('")')
        recipe = expected.get(key)
        if not recipe or key in seen:
            raise RuntimeError('Unknown/duplicate tool point')
        source = sources[recipe['branch_id']]
        cue_path = source['cue'] + '.' + source['cue'].rsplit('/', 1)[-1]
        if (row['sound'] != cue_path or abs(row['time_s'] - recipe['montage_time_estimate_s']) > 1e-5
                or row['legacy_index'] != -1 or row['volume'] != 1.0 or row['pitch'] != 1.0):
            raise RuntimeError('Saved recipe changed: ' + key)
        seen.add(key)


def verify_previous(previous, current):
    """Every old sound object identity/recipe remains, not just non-audio windows."""
    kept = {r['guid']: r for r in current}
    if any(kept.get(row['guid']) != row for row in previous):
        raise RuntimeError('Previously accepted sound point changed or disappeared')


def non_runtime_audio_config(text):
    import re
    return [line for line in text.splitlines() if not re.match(
        r'^\s*(?:KinsectLaunchSound|KinsectFlightLoopSound|ExtractCollectedSound)=', line)]


def runtime_audio(plan, state, writing, config):
    wanted = plan['runtime_audio']
    source = wanted['loop_source']
    if len(source['variants']) != 1:
        raise RuntimeError('Expected one recorded wing-loop source')
    cue = pilot.existing(wanted['loop_cue'], unreal.SoundCue, OWNER)
    if cue is None and writing:
        factory = unreal.SoundCueFactoryNew()
        factory.set_editor_property('initial_sound_waves', [unreal.load_asset(source['variants'][0]['wave'])])
        cue, _ = pilot.create(wanted['loop_cue'], unreal.SoundCue, factory, OWNER)
        node = unreal.find_object(cue, 'SoundNodeWavePlayer_0')
        if not isinstance(node, unreal.SoundNodeWavePlayer):
            raise RuntimeError('Factory did not produce expected resident WavePlayer')
        # Wave.bLooping is cleared when used through a WavePlayer. Set the node,
        # not the shared finite preview Wave, so existing sources stay unchanged.
        node.set_editor_property('looping', True)
        cue.set_editor_property('volume_multiplier', 1.0)
        cue.set_editor_property('attenuation_settings', unreal.load_asset(pilot.ATTENUATION))
        cue.set_editor_property('concurrency_set', [unreal.load_asset(pilot.CONCURRENCY)])
        pilot.own(cue, OWNER)
        checkpoint(state, cue)
    if not cue:
        raise RuntimeError('Kinsect loop cue missing')
    native = pilot.inspector.text_export(cue)
    if source['variants'][0]['wave'] not in native or 'bLooping=True' not in native:
        raise RuntimeError('Loop graph does not reference the frozen source')
    changed = False
    for field, path in wanted['properties'].items():
        sound = unreal.load_asset(path)
        if not sound:
            raise RuntimeError('Missing runtime audio source: ' + path)
        if field != 'kinsect_flight_loop_sound' and 'bLooping=True' in pilot.inspector.text_export(sound):
            raise RuntimeError('Expected one-shot runtime sound')
        if read_field(config, field) != sound:
            if not writing:
                raise RuntimeError('Runtime config reference missing: ' + field)
            config.set_editor_property(field, sound)
            changed = True
    if non_runtime_audio_config(pilot.inspector.text_export(config)) != non_runtime_audio_config(state['before_runtime_config']):
        raise RuntimeError('Non-audio CombatConfig author data changed')
    if changed:
        pilot.save(config)
        checkpoint(state, config)
    return dict(config=wanted['config'], properties=wanted['properties'], loop_cue=wanted['loop_cue'], linked=True)


def main():
    global OUT
    if '--section-maintenance' in sys.argv or '-MHGZAudioSectionMaintenance' in unreal.SystemLibrary.get_command_line():
        import audio_section_maintenance
        OUT = audio_section_maintenance.OUT
        return audio_section_maintenance.main()
    if '--descending-thrust' in sys.argv or '-MHGZAudioDescendingThrust' in unreal.SystemLibrary.get_command_line():
        from setup_ground_audio import descending_thrust_audio
        return descending_thrust_audio('--apply' in sys.argv or '-MHGZCurrentWeaponAudioApply' in unreal.SystemLibrary.get_command_line(),
            '--inspect' in sys.argv or '-MHGZAudioGroundInspect' in unreal.SystemLibrary.get_command_line())
    if '--ground-impact' in sys.argv or '-MHGZAudioGroundImpact' in unreal.SystemLibrary.get_command_line():
        from setup_ground_audio import four_hit_impact
        return four_hit_impact('--apply' in sys.argv or '-MHGZCurrentWeaponAudioApply' in unreal.SystemLibrary.get_command_line(),
            '--inspect' in sys.argv or '-MHGZAudioGroundInspect' in unreal.SystemLibrary.get_command_line())
    if '--landing-events' in sys.argv or '-MHGZAudioLandingEvents' in unreal.SystemLibrary.get_command_line():
        from setup_ground_audio import migrate_landing_events
        return migrate_landing_events('--apply' in sys.argv or '-MHGZCurrentWeaponAudioApply' in unreal.SystemLibrary.get_command_line())
    if '--dust-volume' in sys.argv or '-MHGZAudioDustVolume' in unreal.SystemLibrary.get_command_line():
        from setup_ground_audio import adjust_dust_volume
        return adjust_dust_volume('--apply' in sys.argv or '-MHGZCurrentWeaponAudioApply' in unreal.SystemLibrary.get_command_line())
    if '--ground-events' in sys.argv or '-MHGZAudioGroundEvents' in unreal.SystemLibrary.get_command_line():
        from setup_ground_audio import run
        return run('--apply' in sys.argv or '-MHGZCurrentWeaponAudioApply' in unreal.SystemLibrary.get_command_line(),
                   '--inspect' in sys.argv or '-MHGZAudioGroundInspect' in unreal.SystemLibrary.get_command_line(),
                   '--resume' in sys.argv or '-MHGZAudioGroundResume' in unreal.SystemLibrary.get_command_line())
    if '--repair-aerial-audio' in sys.argv or '-MHGZAudioRepairAerial' in unreal.SystemLibrary.get_command_line():
        from setup_propulsion_audio import repair_aerial_audio
        return repair_aerial_audio('--apply' in sys.argv or '-MHGZCurrentWeaponAudioApply' in unreal.SystemLibrary.get_command_line())
    if '--inspect-aerial-audio' in sys.argv or '-MHGZAudioInspectAerial' in unreal.SystemLibrary.get_command_line():
        from setup_propulsion_audio import inspect_aerial_audio
        return inspect_aerial_audio()
    if '--kinsect-contact' in sys.argv or '-MHGZAudioKinsectContact' in unreal.SystemLibrary.get_command_line():
        from setup_kinsect_contact_audio import run
        return run('--apply' in sys.argv or '-MHGZCurrentWeaponAudioApply' in unreal.SystemLibrary.get_command_line())
    if '--propulsion' in sys.argv or '-MHGZAudioPropulsion' in unreal.SystemLibrary.get_command_line():
        from setup_propulsion_audio import run
        return run('--apply' in sys.argv or '-MHGZCurrentWeaponAudioApply' in unreal.SystemLibrary.get_command_line())
    if ('--spatial-maintenance' in sys.argv
            or '-MHGZAudioSpatialMaintenance' in unreal.SystemLibrary.get_command_line()):
        import audio_spatial_maintenance
        return audio_spatial_maintenance.main()
    plan = load(OUT / 'plan.json')
    if plan['plan_sha256'] != fingerprint({k: v for k, v in plan.items() if k != 'plan_sha256'}):
        raise RuntimeError('Frozen plan modified')
    if pilot.digest(OUT.parent / 'MappingFinal/mapping.json') != plan['mapping_sha256']:
        raise RuntimeError('Source map changed')
    for path, sha in plan.get('code_sha256', {}).items():
        if pilot.digest(path) != sha:
            raise RuntimeError('Runtime audio contract changed: ' + path)
    preserve = plan.get('preserve_previous', False)
    if preserve:
        config_text = (ROOT / 'Config/DefaultGameplayTags.ini').read_text(encoding='utf-8')
        if hashlib.sha256(config_text.encode('utf-8')).hexdigest() != plan['tag_config_after_text_sha256']:
            raise RuntimeError('Tag config differs from reviewed append-only patch')
        if pilot.digest(ROOT / 'Source/MHGZ/ActionSystem/MHGZAwakenedKinsectAttackAbility.cpp') != plan['runtime_contract_sha256']:
            raise RuntimeError('Awakened hold/release contract changed')
    writing = '--apply' in sys.argv or '-MHGZCurrentWeaponAudioApply' in unreal.SystemLibrary.get_command_line()
    state = load(STATE) if STATE.exists() else None
    if state and state['plan_sha256'] != plan['plan_sha256']:
        raise RuntimeError('Manifest belongs to another plan')
    coverage.verify_checkpoints(plan, state)
    for name in plan['required_tags']:
        pilot.tag(name)
    sources = {s['branch_id']: s for s in plan['sources']}
    loaded, before, tracks = {}, {}, {}
    # Entire old tool track must match the accepted author baseline before removal.
    # No user-authored/unknown event is allowed on that track for this operation.
    for item in plan['montages']:
        path = item['path']
        asset = unreal.load_asset(path)
        current = pilot.inspector.montage(path)
        rows = coverage.authored(asset, current)
        original = state['before'][path] if state else current
        old_guids = {r['guid'] for r in item['previous_authored']}
        if state and path in state['saved_packages']:
            verify_recipes(item, rows, sources)
        elif rows != item['previous_authored']:
            raise RuntimeError('Previous tool points were edited; refuse replacement: ' + path)
        else:
            track_rows = coverage.tracks(asset)
            track_index = next((i for i, r in enumerate(track_rows) if r.get('TrackName') == '"SwingAudio"'), None)
            if (old_guids and track_index is None) or (track_index is not None and
                    {r['Guid'] for r in current['existing_notifies'] if int(r.get('TrackIndex', 0)) == track_index} != old_guids):
                raise RuntimeError('SwingAudio contains unknown/user events; refuse track removal')
        if preserve:
            coverage.verify_montage(item['before'], current)
            verify_previous(item['previous_authored'], rows)
        else:
            verify_author_data(item['before'], current, old_guids, {r['guid'] for r in rows})
        before[path] = original
        tracks[path] = state['before_tracks'][path] if state else coverage.tracks(asset)
        coverage.verify_tracks(tracks[path], coverage.tracks(asset))
        loaded[path] = asset
    baseline_cdos = dict(state.get('before_cdos', {})) if state else {}
    for ability in plan.get('abilities', []):
        path = ability['path']
        bp = unreal.load_asset(path)
        cdo = blueprint_cdo(bp, unreal)
        native = pilot.inspector.text_export(cdo)
        if path not in baseline_cdos:
            baseline_cdos[path] = native
        if pilot.non_audio_cdo(native) != pilot.non_audio_cdo(baseline_cdos[path]):
            raise RuntimeError('Non-audio GA author data changed: ' + path)
        identity = read_field(cdo, 'audio_identity_tag').export_text()
        if identity not in ('(TagName="")', '(TagName=None)', '()', '(TagName="' + ability['audio_identity'] + '")'):
            raise RuntimeError('Refuse to replace a different GA audio identity: ' + path)
        loaded[path] = bp
    config = None
    before_runtime_config = None
    if plan.get('runtime_audio'):
        config = unreal.load_asset(plan['runtime_audio']['config'])
        before_runtime_config = state['before_runtime_config'] if state else pilot.inspector.text_export(config)
        if non_runtime_audio_config(pilot.inspector.text_export(config)) != non_runtime_audio_config(before_runtime_config):
            raise RuntimeError('CombatConfig non-audio author data changed')
        loaded[plan['runtime_audio']['config']] = config
    if writing and state is None:
        state = dict(plan_sha256=plan['plan_sha256'], phase='intent',
                     expected_hashes=dict(plan['protected_package_hashes']), saved_packages=[],
                     before=before, before_tracks=tracks, before_cdos=baseline_cdos, original_packages={})
        if config:
            state['before_runtime_config'] = before_runtime_config
        for path in loaded:
            disk = pilot.inspector.disk(path)
            backup = OUT / 'Backup' / disk.relative_to(ROOT)
            backup.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(disk, backup)
            state['original_packages'][disk.as_posix()] = dict(backup=backup.as_posix(), sha256=pilot.digest(disk))
        pilot.save_json(STATE, state)
    if state is None:
        raise RuntimeError('No applied transaction; run explicitly authorized Apply first')
    # Expansion may reference only the completed source pool; never reimport.
    pool = source_pool(plan, writing and not preserve, state)
    if not all(r['linked'] for r in pool):
        raise RuntimeError('Source pool incomplete, not writing Montage points')
    runtime_report = runtime_audio(plan, state, writing, config) if config else None
    report = []
    for item in plan['montages']:
        path, asset = item['path'], loaded[item['path']]
        if path not in state['saved_packages']:
            if not writing:
                raise RuntimeError('Unfinished Montage: ' + path)
            previous = coverage.authored(asset, pilot.inspector.montage(path))
            if not preserve:
                count = unreal.AnimationLibrary.remove_animation_notify_events_by_track(asset, pilot.TRACK)
                if count != len(item['previous_authored']):
                    raise RuntimeError('Unexpected removed tool count; do not save')
            elif not unreal.AnimationLibrary.is_valid_anim_notify_track_name(asset, pilot.TRACK):
                unreal.AnimationLibrary.add_animation_notify_track(asset, pilot.TRACK, unreal.LinearColor(.2, .65, 1, 1))
            existing_tags = {r['event_tag'] for r in previous} if preserve else set()
            for recipe in sorted(item['recipes'], key=lambda r: (r['montage_time_estimate_s'], r['event_tag'])):
                if '(TagName="' + recipe['event_tag'] + '")' in existing_tags:
                    continue
                notify = unreal.AnimationLibrary.add_animation_notify_event(asset, pilot.TRACK,
                            recipe['montage_time_estimate_s'], unreal.AnimNotify_SwingSound)
                notify.set_editor_property('event_tag', pilot.tag(recipe['event_tag']))
                notify.set_editor_property('sound', unreal.load_asset(sources[recipe['branch_id']]['cue']))
            current = pilot.inspector.montage(path)
            rows = coverage.authored(asset, current)
            verify_recipes(item, rows, sources)
            if preserve:
                coverage.verify_montage(before[path], current)
                verify_previous(item['previous_authored'], rows)
            else:
                verify_author_data(before[path], current, {r['guid'] for r in item['previous_authored']},
                                   {r['guid'] for r in rows})
            coverage.verify_tracks(tracks[path], coverage.tracks(asset))
            pilot.save(asset)
            checkpoint(state, asset)
        current = pilot.inspector.montage(path)
        rows = coverage.authored(asset, current)
        verify_recipes(item, rows, sources)
        if preserve:
            notes = coverage.verify_montage(before[path], current)
            verify_previous(item['previous_authored'], rows)
        else:
            notes = verify_author_data(before[path], current, {r['guid'] for r in item['previous_authored']},
                                       {r['guid'] for r in rows})
        report.append(dict(path=path, authored=rows, retired_count=len(item['retired']),
                           author_cache_refresh=notes))
    ability_report = []
    for ability in plan.get('abilities', []):
        path, identity = ability['path'], ability['audio_identity']
        bp = loaded[path]
        cdo = blueprint_cdo(bp, unreal)
        enabled = bool(read_field(cdo, 'use_authored_swing_audio'))
        same = read_field(cdo, 'audio_identity_tag').export_text() == '(TagName="' + identity + '")'
        if writing and not (enabled and same):
            cdo.set_editor_property('use_authored_swing_audio', True)
            cdo.set_editor_property('audio_identity_tag', pilot.tag(identity))
            if pilot.non_audio_cdo(pilot.inspector.text_export(cdo)) != pilot.non_audio_cdo(baseline_cdos[path]):
                raise RuntimeError('Unexpected non-audio GA mutation')
            pilot.save(bp)
            checkpoint(state, bp)
            enabled = same = True
        if not (enabled and same):
            raise RuntimeError('GA audio not enabled: ' + path)
        ability_report.append(dict(path=path, enabled=enabled, audio_identity=identity))
    coverage.verify_checkpoints(plan, state)
    result = dict(linked=True, mode='apply' if writing else 'audit', plan_sha256=plan['plan_sha256'],
                  source_pool=pool, montages=report, points=sum(len(m['authored']) for m in report),
                  retired=sum(m['retired_count'] for m in report),
                  protected_non_audio_author_data=True, preserved_previous_audio_guids=preserve,
                  ga_changed=[a['path'] for a in ability_report if a['path'] in state['saved_packages']],
                  abilities=ability_report,
                  runtime_audio=runtime_report,
                  complete_original_audio_coverage=False, pie_audible='not_tested', limits=plan['limits'])
    pilot.save_json(OUT / ('apply.json' if writing else 'audit.json'), result)
    state['phase'] = 'saved_pending_independent_reload' if writing else 'independently_reloaded'
    pilot.save_json(STATE, state)
    print('CURRENT_WEAPON_AUDIO ' + json.dumps(dict(linked=True, points=result['points'],
        retired=result['retired'], graphs=len(pool), phase=state['phase'])))


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        pilot.save_json(OUT / 'failure.json', dict(error=repr(error),
            note='Inspect saved packages/checkpoints before retry. No automatic rollback or reimport.'))
        raise
