"""Scoped resident propulsion sound/Notify authoring. Audit or plan by default.

Reuses prepared current-weapon media, existing audio factory patterns, precise
ActionSound, current CDO references and source-frame mapping. No C++ or fine mix.
"""
from collections import Counter
from pathlib import Path
import shutil
import unreal

import audio_spatial_maintenance as shared
import rise_audio_weapon_mapping as mapping
from ue_asset_utils import read_field

pilot = shared.pilot
OUT = shared.ROOT / 'Saved/Audio/RiseIG/Propulsion20261020/UE'
OWNER = 'mhgz_audio_propulsion_20261020_v1'
ABILITIES = ('QianChengGanTiao', 'HouChengGanTiao', 'ZuoChengGanTiao', 'YouChengGanTiao',
             'CaoChongZhan', 'QiangHuaCaoChongChuanCi')


def sources(plan, writing, state, output=OUT, owner=OWNER, concurrency_path=None):
    waves = []
    for variant in plan['source']['variants']:
        if pilot.digest(variant['wav']) != variant['sha256']:
            raise RuntimeError('Prepared propulsion WAV changed')
        wave = pilot.existing(variant['wave'], unreal.SoundWave, owner)
        if wave is None and writing:
            task = unreal.AssetImportTask()
            task.filename = variant['wav']
            task.destination_path, task.destination_name = variant['wave'].rsplit('/', 1)
            task.automated, task.save, task.replace_existing = True, False, False
            task.factory = unreal.SoundFactory()
            unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
            wave = unreal.load_asset(variant['wave'])
            if not isinstance(wave, unreal.SoundWave):
                raise RuntimeError('Propulsion Wave import failed')
            wave.set_editor_property('looping', False)
            unreal.EditorAssetLibrary.set_metadata_tag(wave, 'MHGZ.Audio.SourceSHA256', variant['sha256'])
            pilot.own(wave, owner)
            state['new_assets'][variant['wave']] = pilot.digest(pilot.inspector.disk(variant['wave']))
            pilot.save_json(output / 'state.json', state)
        if wave is None or read_field(wave, 'looping') or int(read_field(wave, 'num_channels')) != variant['channels']:
            raise RuntimeError('Missing/wrong resident propulsion Wave')
        if (unreal.EditorAssetLibrary.get_metadata_tag(wave, 'MHGZ.Audio.SourceSHA256') != variant['sha256']
                or abs(float(read_field(wave, 'duration')) - variant['seconds']) > .002):
            raise RuntimeError('Propulsion Wave differs from prepared source')
        waves.append(wave)
    cue_path = plan['source']['cue']
    cue = pilot.existing(cue_path, unreal.SoundCue, owner)
    attenuation = unreal.load_asset(pilot.AUDIO + '/Settings/SA_IG_DongZuo')
    concurrency = unreal.load_asset(concurrency_path or pilot.AUDIO + '/Settings/SCN_IG_DongZuo')
    if not attenuation or not concurrency:
        raise RuntimeError('Reuse existing action spatial settings; do not guess new mix')
    if cue is None and writing:
        factory = unreal.SoundCueFactoryNew()
        factory.set_editor_property('initial_sound_waves', waves)
        cue, fresh = pilot.create(cue_path, unreal.SoundCue, factory, owner)
        if not fresh:
            raise RuntimeError('Unexpected source creation race')
        cue.set_editor_property('volume_multiplier', 1.0)
        cue.set_editor_property('attenuation_settings', attenuation)
        cue.set_editor_property('concurrency_set', [concurrency])
        unreal.EditorAssetLibrary.set_metadata_tag(cue, 'MHGZ.Audio.GraphSHA256', plan['source']['txtp_sha256'])
        pilot.own(cue, owner)
        state['new_assets'][cue_path] = pilot.digest(pilot.inspector.disk(cue_path))
        pilot.save_json(output / 'state.json', state)
    if not cue:
        raise RuntimeError('Propulsion Cue missing')
    native = pilot.inspector.text_export(cue)
    if ('FirstNode=' not in native or 'SoundNodeWavePlayer' not in native or 'bLooping=True' in native
            or any(v['wave'] not in native for v in plan['source']['variants'])
            or read_field(cue, 'attenuation_settings') != attenuation
            or list(read_field(cue, 'concurrency_set')) != [concurrency]
            or unreal.EditorAssetLibrary.get_metadata_tag(cue, 'MHGZ.Audio.GraphSHA256') != plan['source']['txtp_sha256']):
        raise RuntimeError('Resident propulsion Cue graph/settings mismatch')
    return cue


def run(writing=False):
    pilot.inspector.OUT = OUT / 'NativeExports'
    plan = shared.load(OUT.parent / 'source-plan.json')
    for path, expected in plan['inputs'].items():
        if pilot.digest(path) != expected:
            raise RuntimeError('Frozen propulsion input changed: ' + path)
    checkpoint = OUT / 'state.json'
    state = shared.load(checkpoint) if checkpoint.exists() else None
    if writing and state:
        raise RuntimeError('Propulsion transaction exists; inspect/audit before any retry')
    if not state:
        # A scattered source (notably white forward arc156) is not made "stable" by
        # retaining just its one coincident repeated bucket.
        populations = Counter(c['source_id'] for c in plan['clusters'])
        clusters = [c for c in plan['clusters'] if c['observations'] >= 2 and populations[c['source_id']] == 1]
        abilities = [pilot.inspector.ability('/Game/Blueprints/Ability/InsectGlaive/GA_IG_' + name) for name in ABILITIES]
        paths = sorted({p for a in abilities for p in a['montages']})
        snapshots = [pilot.inspector.montage(p) for p in paths]
        pilot.save_json(OUT / 'snapshot.json', dict(montages=snapshots, sequences=pilot.inspector.sequences,
                                                   stable_clusters=clusters, abilities=abilities))
        mapped = mapping.map_montages(dict(montages=snapshots, sequences=pilot.inspector.sequences), clusters)
        by_path = {s['path']: s for s in snapshots}
        state = dict(phase='read_only_plan', targets={}, before={}, recipes_before={}, expected={},
                     changed={}, backups={}, new_assets={}, isolated_sources={str(k): v for k, v in populations.items() if v > 1})
        for montage in mapped:
            points = [p for p in montage['candidates'] if not p['cut_boundary_ambiguous']]
            existing = {(p['cluster_id'], p['segment_index'], p['slot'], p['loop']) for p in points}
            for segment in by_path[montage['path']]['segments']:
                for cluster in clusters:
                    for p in mapping.end_frame_hold_positions(segment, pilot.inspector.sequences[segment['sequence']], cluster):
                        key = (p['cluster_id'], p['segment_index'], p['slot'], p['loop'])
                        if key not in existing:
                            points.append(p)
                            existing.add(key)
            points.sort(key=lambda p: p['montage_time_estimate_s'])
            if points:
                p = montage['path']
                state['targets'][p] = points
                state['before'][p] = by_path[p]
                state['recipes_before'][p] = shared.category_recipes(unreal.load_asset(p), by_path[p])
                if any(r['sound'] and '_3520842405_' in r['sound'] for r in state['recipes_before'][p]):
                    raise RuntimeError('Existing propulsion event requires reconciliation, not duplication')
        if not state['targets']:
            raise RuntimeError('No current referenced clips match stable propulsion positions')
        state['protected'] = {}
        for folder in ('Content/Weapons/InsectGlaive/Anims', 'Content/Blueprints/Ability/InsectGlaive',
                       'Content/Weapons/InsectGlaive/Audio', 'Content/Weapons/InsectGlaive/Data'):
            for disk in (shared.ROOT / folder).rglob('*.uasset'):
                state['protected'][disk.as_posix()] = pilot.digest(disk)
        pilot.save_json(OUT / 'plan.json', state)
        print('PROPULSION_PLAN ' + str(len(state['targets'])) + ' montages, '
              + str(sum(len(p) for p in state['targets'].values())) + ' points')
        if not writing:
            return
        for p in state['targets']:
            disk = pilot.inspector.disk(p)
            backup = OUT / 'Backup' / disk.relative_to(shared.ROOT)
            backup.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(disk, backup)
            state['backups'][p] = dict(path=str(backup), sha256=pilot.digest(disk))
        state['phase'] = 'intent'
        pilot.save_json(checkpoint, state)
        cue = sources(plan, True, state)
        for path, points in state['targets'].items():
            asset = unreal.load_asset(path)
            if not unreal.AnimationLibrary.is_valid_anim_notify_track_name(asset, 'ActionAudio'):
                unreal.AnimationLibrary.add_animation_notify_track(asset, 'ActionAudio', unreal.LinearColor(.8, .6, .2, 1))
            for point in points:
                notify = unreal.AnimationLibrary.add_animation_notify_event(asset, 'ActionAudio',
                    point['montage_time_estimate_s'], unreal.AnimNotify_ActionSound)
                notify.set_editor_property('sound', cue)
                notify.set_editor_property('source', unreal.MHGZAnimationSoundSource.CHARACTER_SOCKET)
                notify.set_editor_property('attach_socket', 'Root')
            after = pilot.inspector.montage(path)
            shared.coverage.verify_montage(state['before'][path], shared.verify_preserved(state['before'][path], after, []))
            rows = shared.category_recipes(asset, after)
            old = {r['guid'] for r in state['recipes_before'][path]}
            if [r for r in rows if r['guid'] in old] != state['recipes_before'][path] or len(rows) != len(old) + len(points):
                raise RuntimeError('Original sound events changed or unexpected added points')
            pilot.save(asset)
            state['expected'][path] = rows
            state['changed'][path] = pilot.digest(pilot.inspector.disk(path))
            pilot.save_json(checkpoint, state)
        state['phase'] = 'saved_pending_independent_reload'
        pilot.save_json(checkpoint, state)
    if set(state['expected']) != set(state['targets']):
        raise RuntimeError('Partial propulsion transaction: inspect saved packages/checkpoint')
    for path, sha in {**state['new_assets'], **state['changed']}.items():
        if pilot.digest(pilot.inspector.disk(path)) != sha:
            raise RuntimeError('Propulsion package edited after save: ' + path)
    sources(plan, False, state)
    for p, expected in state['expected'].items():
        snapshot = pilot.inspector.montage(p)
        shared.coverage.verify_montage(state['before'][p], shared.verify_preserved(state['before'][p], snapshot, []))
        if shared.category_recipes(unreal.load_asset(p), snapshot) != expected:
            raise RuntimeError('Reloaded propulsion audio differs: ' + p)
    changed = {pilot.inspector.disk(p).as_posix() for p in state['changed']}
    for path, sha in state['protected'].items():
        if path not in changed and pilot.digest(path) != sha:
            raise RuntimeError('Unrelated existing package changed: ' + path)
    result = dict(phase='saved_pending_independent_reload' if writing else 'independently_reloaded',
        montages=len(state['changed']), added_points=sum(len(p) for p in state['targets'].values()),
        new_audio_packages=len(state['new_assets']), protected_unchanged=len(state['protected'])-len(changed),
        original_audio_and_gameplay_preserved=True, precursor_action_requests_not_authored=True,
        fine_mix_or_recorder_changed=False, pie_listened=False)
    state['phase'] = result['phase']
    pilot.save_json(checkpoint, state)
    pilot.save_json(OUT / ('apply.json' if writing else 'audit.json'), result)
    print('PROPULSION_AUDIO ' + str(result))


def inspect_aerial_audio():
    """Fresh, read-only route/recipe audit; does not replay the old transaction."""
    from ue_asset_utils import blueprint_cdo
    output = shared.ROOT / 'Saved/Audio/RiseIG/AerialAudioFix20261020'
    output.mkdir(parents=True, exist_ok=True)
    pilot.inspector.OUT = output / 'NativeExports'
    # Remote-editor Python may retain imported modules between invocations.
    # A fresh route audit must not inherit another transaction's cached hashes
    # or source lengths, even though commandlet processes already start empty.
    pilot.inspector.protected.clear()
    pilot.inspector.sequences.clear()
    names = ('QianChengGanTiao', 'HouChengGanTiao', 'AirDodge')
    abilities = []
    for name in names:
        path = '/Game/Blueprints/Ability/InsectGlaive/GA_IG_' + name
        row = pilot.inspector.ability(path)
        cdo = blueprint_cdo(unreal.load_asset(path), unreal)
        row['audio_enabled'] = bool(read_field(cdo, 'use_authored_swing_audio'))
        row['audio_identity'] = read_field(cdo, 'audio_identity_tag').export_text()
        if name != 'AirDodge':
            row['handoff_lead_s'] = float(read_field(cdo, 'vault_handoff_lead_seconds'))
        abilities.append(row)
    montages = []
    for path in sorted({p for row in abilities for p in row['montages']}):
        snap = pilot.inspector.montage(path)
        snap['audio'] = shared.category_recipes(unreal.load_asset(path), snap)
        montages.append(snap)
    for path, sha in pilot.inspector.protected.items():
        if pilot.digest(path) != sha:
            raise RuntimeError('Read-only audit changed package: ' + path)
    pilot.save_json(output / 'inspect.json', dict(abilities=abilities, montages=montages,
                    sequences=pilot.inspector.sequences, protected=pilot.inspector.protected))
    print('AERIAL_AUDIO_INSPECT ' + str(len(montages)) + ' current referenced montages; Content unchanged')


def repair_aerial_audio(writing=False):
    """Scoped successor: normal-vault coverage and source137 precursor sound."""
    from rise_audio_coverage import read_rows
    from spatial_audio_rules import transfer_vault_stage_audio, emission, notify_category, SOUND_TRACKS, ACTION_BRANCHES
    output = shared.ROOT / 'Saved/Audio/RiseIG/AerialAudioFix20261020'
    output.mkdir(parents=True, exist_ok=True)
    checkpoint = output / 'state.json'
    state = shared.load(checkpoint) if checkpoint.exists() else None
    if writing and state:
        raise RuntimeError('Aerial fix transaction exists; inspect/audit, never replay Apply')
    pilot.inspector.OUT = output / 'NativeExports'
    if not state:
        inspect_aerial_audio()
        snapshot = shared.load(output / 'inspect.json')
        by_name = {m['path'].split('.')[-1]:m for m in snapshot['montages']}
        source_plan = shared.load(OUT.parent / 'source-plan.json')
        for p, sha in source_plan['inputs'].items():
            if pilot.digest(p) != sha:
                raise RuntimeError('Frozen current-weapon capture changed')
        animation_path = next(p for p in source_plan['inputs'] if p.endswith('_animation.csv'))
        # Use exact native frames from already independently owned posts. Ignore
        # their old temporal animation labels, which describe the PREVIOUS action.
        posts = [dict(row=p['audio_row'], native_present_frame=p['fx_evidence']['native_present_frame'])
                 for p in source_plan['timed_posts']]
        precursors = mapping.transition_precursor_posts(read_rows(Path(animation_path)), posts, 137)
        if len(precursors) < 2 or {r['lead_render_frames'] for r in precursors} != {2}:
            raise RuntimeError('Expected repeated source137 two-frame precursor evidence; review before authoring')
        points = {}
        for normal, white, src_id, dst_id in (
            ('AM_IG_QianChengGanTiao','AM_IG_QianChengGanTiao_W',155,141),
            ('AM_IG_QianChengGanTiao_Over','AM_IG_QianChengGanTiao_W_Over',156,142)):
            src, dst = by_name[white], by_name[normal]
            for montage, numeric in ((src,src_id),(dst,dst_id)):
                sequence = snapshot['sequences'][montage['segments'][0]['sequence']]
                if sequence['import_numeric_ids'] != [numeric]:
                    raise RuntimeError('Vault stage identity changed; no white/normal inference')
            if dst['audio']:
                raise RuntimeError('Normal forward already has audio; reconcile instead of duplicate')
            points[dst['path']] = transfer_vault_stage_audio(src,dst,src_id,dst_id)
        original_map = shared.load(shared.BASE / 'MappingFinal/mapping.json')
        source_pool = shared.sources()
        mapped = mapping.map_montages(snapshot, original_map['clusters'])
        for name in ('AM_IG_HouChengGanTiao','AM_IG_HouChengGanTiao_Over'):
            dst = by_name[name]
            if dst['audio']:
                raise RuntimeError('Normal back already has audio; do not overwrite')
            candidates = next(m['candidates'] for m in mapped if m['path']==dst['path'])
            rows = []
            for c in candidates:
                if c['cut_boundary_ambiguous'] or c['layer'] not in ('武器层','通用武器层','地面/动作附加层'):
                    continue
                source = source_pool[c['branch_id']]
                location, socket = emission(c['branch_id'],dst['path'])
                kind = notify_category(source['cue']) or (
                    'AnimNotify_ActionSound' if c['branch_id'] in ACTION_BRANCHES else 'AnimNotify_SwingSound')
                rows.append(dict(time=c['montage_time_estimate_s'], sound=source['cue'],
                    kind=kind, source=location, socket=socket, volume=1.0,pitch=1.0,ground_trace_distance=150.,
                    placement_policy='direct_current_weapon_source_single_observation',
                    source_cluster=c['cluster_id'], observations=c['observations']))
            if not rows:
                raise RuntimeError('Missing direct normal-back source evidence')
            points[dst['path']] = rows
        propulsion = source_plan['source']['cue']
        base = dict(sound=propulsion, kind='AnimNotify_ActionSound', source='CHARACTER_SOCKET',socket='Root',
                    volume=1.,pitch=1.,ground_trace_distance=150.)
        back = by_name['AM_IG_HouChengGanTiao']
        points[back['path']].append(dict(base,time=back['length_s']-1/60,
            placement_policy='project_normal_vault_takeoff_boundary'))
        air = by_name['AM_IG_AirDodge']
        if snapshot['sequences'][air['segments'][0]['sequence']]['import_numeric_ids'] != [137]:
            raise RuntimeError('Actual AirDodge source is no longer137')
        if any(r['sound'] and '_3520842405_' in r['sound'] for r in air['audio']):
            raise RuntimeError('AirDodge propulsion already exists; reconcile instead of duplicate')
        points[air['path']] = [dict(base,time=1/60,
            placement_policy='project_first_interior_frame_after_source137_precursor', observations=len(precursors))]
        protected = {}
        for folder in ('Content/Weapons/InsectGlaive','Content/Blueprints/Ability/InsectGlaive','Content/GameplayCues'):
            for p in (shared.ROOT/folder).rglob('*.uasset'):
                protected[p.as_posix()] = pilot.digest(p)
        state = dict(phase='read_only_plan', points=points, before={p:m for p,m in
                     ((m['path'],m) for m in snapshot['montages']) if p in points},
                     precursor_evidence=precursors, protected=protected, changed={}, expected={}, backups={})
        for p, rows in points.items():
            for r in rows:
                if r['kind'] not in SOUND_TRACKS or not 0 < r['time'] < state['before'][p]['length_s']:
                    raise RuntimeError('Invalid/unsupported new sound point')
                if not isinstance(unreal.load_asset(r['sound']),unreal.SoundCue):
                    raise RuntimeError('Resident Cue missing; do not import new source')
        pilot.save_json(output / 'plan.json',state)
        print('AERIAL_AUDIO_PLAN ' + str(len(points)) + ' montages, '
              + str(sum(map(len,points.values()))) + ' points, source137 precursors=' + str(len(precursors)))
        if not writing:
            return
        for p in points:
            disk = pilot.inspector.disk(p)
            backup = output/'Backup'/disk.relative_to(shared.ROOT)
            backup.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(disk,backup)
            state['backups'][p] = dict(path=str(backup),sha256=pilot.digest(disk))
        state['phase']='intent'
        pilot.save_json(checkpoint,state)
        for path, rows in points.items():
            asset = unreal.load_asset(path)
            for r in rows:
                track = SOUND_TRACKS[r['kind']]
                if not unreal.AnimationLibrary.is_valid_anim_notify_track_name(asset,track):
                    unreal.AnimationLibrary.add_animation_notify_track(asset,track,unreal.LinearColor(.8,.6,.2,1))
                notify = unreal.AnimationLibrary.add_animation_notify_event(asset,track,r['time'],getattr(unreal,r['kind']))
                enum_name = next(n for n in ('WEAPON_SOCKET','CHARACTER_SOCKET','GROUND_CONTACT') if n in r['source'])
                for key,value in dict(sound=unreal.load_asset(r['sound']),
                    source=getattr(unreal.MHGZAnimationSoundSource,enum_name),attach_socket=r['socket'],
                    volume_multiplier=r['volume'],pitch_multiplier=r['pitch'],
                    ground_trace_distance=r['ground_trace_distance']).items():
                    notify.set_editor_property(key,value)
            after = pilot.inspector.montage(path)
            before=state['before'][path]
            shared.coverage.verify_montage(before,shared.verify_preserved(before,after,[]))
            recipes=shared.category_recipes(asset,after)
            old={r['guid'] for r in before['audio']}
            if [r for r in recipes if r['guid'] in old] != before['audio'] or len(recipes)!=len(old)+len(rows):
                raise RuntimeError('Original recipes changed or unexpected new sound count')
            pilot.save(asset)
            state['changed'][path]=pilot.digest(pilot.inspector.disk(path))
            state['expected'][path]=recipes
            pilot.save_json(checkpoint,state)
        state['phase']='saved_pending_independent_reload'
        pilot.save_json(checkpoint,state)
    if set(state['expected']) != set(state['points']):
        raise RuntimeError('Partial repair; inspect saved packages before any retry')
    for p,sha in state['changed'].items():
        if pilot.digest(pilot.inspector.disk(p))!=sha:
            raise RuntimeError('Repair package changed since save: '+p)
        after=pilot.inspector.montage(p)
        shared.coverage.verify_montage(state['before'][p],shared.verify_preserved(state['before'][p],after,[]))
        if shared.category_recipes(unreal.load_asset(p),after)!=state['expected'][p]:
            raise RuntimeError('Reloaded repair differs: '+p)
    changed={pilot.inspector.disk(p).as_posix() for p in state['changed']}
    for p,sha in state['protected'].items():
        if p not in changed and pilot.digest(p)!=sha:
            raise RuntimeError('Unrelated package changed: '+p)
    state['phase']='saved_pending_independent_reload' if writing else 'independently_reloaded'
    pilot.save_json(checkpoint,state)
    result=dict(phase=state['phase'],montages=len(state['changed']),added_points=sum(map(len,state['points'].values())),
        source137_precursors=len(state['precursor_evidence']),protected_unchanged=len(state['protected'])-len(changed),
        existing_audio_and_gameplay_preserved=True,volume_or_mix_changed=False,cpp_changed=False,pie_listened=False)
    pilot.save_json(output/('apply.json' if writing else 'audit.json'),result)
    print('AERIAL_AUDIO_REPAIR '+str(result))
