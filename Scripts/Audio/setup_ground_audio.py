"""Scoped ground-event/twirl successor; reuse existing native snapshots and recipes."""
import shutil
import re
import unreal
import audio_spatial_maintenance as shared
from spatial_audio_rules import SOUND_TRACKS
from plan_swing_coverage import fingerprint
from ue_asset_utils import read_field, blueprint_cdo

pilot = shared.pilot
OUT = shared.ROOT / 'Saved/Audio/RiseIG/GroundEvents20261021'
TARGETS = ('AM_IG_ErLian', 'AM_IG_FeiYuan', 'AM_IG_HengSao', 'AM_IG_TuJinHuiXuan')


def descending_thrust_audio(writing=False, inspection=False):
    """Append-only two-Montage adaptation; no new source, gameplay or runtime code."""
    output = shared.ROOT / 'Saved/Audio/RiseIG/DescendingThrustAudio20261021'
    output.mkdir(parents=True, exist_ok=True)
    pilot.inspector.OUT = output / 'NativeExports'
    checkpoint = output / 'state.json'
    state = shared.load(checkpoint) if checkpoint.exists() else None
    targets = ('AM_IG_JiXiTuCi', 'AM_IG_JiXiTuCi_Over')
    def snapshot(path):
        asset = unreal.load_asset(path)
        result = pilot.inspector.montage(path)
        result['audio'] = shared.category_recipes(asset, result)
        return result
    if inspection:
        if state or writing:
            raise RuntimeError('Inspection must precede the transaction; never overwrite frozen evidence')
        names = targets + ('AM_IG_AirDodge', 'AM_IG_QianChengGanTiao',
            'AM_IG_QianChengGanTiao_W', 'AM_IG_ErLian', 'AM_IG_HengSao')
        montages = [snapshot('/Game/Weapons/InsectGlaive/Anims/Montage/' + n) for n in names]
        config = unreal.load_asset('/Game/Weapons/InsectGlaive/Data/DA_IG_Combat')
        profile = read_field(config, 'descending_thrust_air_profile')
        curve = read_field(profile, 'curve')
        if not isinstance(curve, unreal.CurveVector):
            raise RuntimeError('Missing live DescendingThrust curve')
        curve_rows = []
        for i in range(101):
            time = i / 120.
            value = curve.get_vector_value(time)
            curve_rows.append([time, float(value.x), float(value.y), float(value.z)])
        abilities = [pilot.inspector.ability('/Game/Blueprints/Ability/InsectGlaive/GA_IG_' + n)
                     for n in ('JiXiTuCi', 'AirDodge', 'QianChengGanTiao')]
        poses = {}
        for montage in montages:
            if montage['path'].split('.')[-1] not in ('AM_IG_JiXiTuCi_Over', 'AM_IG_ErLian', 'AM_IG_HengSao'):
                continue
            samples = []
            options = unreal.AnimPoseEvaluationOptions()
            options.evaluation_type = unreal.AnimDataEvalType.RAW
            options.should_retarget = False
            options.extract_root_motion = False
            options.incorporate_root_motion_into_pose = False
            for segment in montage['segments']:
                sequence = unreal.load_asset(segment['sequence'])
                duration = (segment['source_end_s'] - segment['source_start_s']) / abs(segment['effective_rate'])
                for i in range(int(duration * 60) + 1):
                    time = min(i / 60., duration)
                    pose = sequence.get_anim_pose_at_time(segment['source_start_s'] + time * segment['effective_rate'], options)
                    names = [str(n) for n in pose.get_bone_names() if str(n) in ('R_Weapon_00', 'R_Arm_03', 'L_Arm_03')]
                    if not names:
                        raise RuntimeError('No known weapon/hand bones for pose timing')
                    bones = {}
                    for name in names:
                        transform = pose.get_bone_pose(unreal.Name(name), unreal.AnimPoseSpaces.WORLD)
                        q, p = transform.rotation, transform.translation
                        bones[name] = dict(q=[q.x, q.y, q.z, q.w], p=[p.x, p.y, p.z])
                    samples.append(dict(time=segment['track_start_s'] + time, bones=bones))
            poses[montage['path']] = samples
        result = dict(montages=montages, sequences=pilot.inspector.sequences, abilities=abilities, poses=poses,
            config_native=pilot.inspector.text_export(config), curve=curve.get_path_name(), curve_rows=curve_rows)
        for path, sha in pilot.inspector.protected.items():
            if pilot.digest(path) != sha:
                raise RuntimeError('Read-only inspection changed package: ' + path)
        pilot.save_json(output / 'snapshot.json', result)
        print('DESCENDING_AUDIO_INSPECT complete')
        return
    plan = shared.load(output / 'source-plan.json')
    if fingerprint({k: v for k, v in plan.items() if k != 'plan_sha256'}) != plan['plan_sha256']:
        raise RuntimeError('Frozen DescendingThrust audio plan changed')
    for path, sha in plan['inputs'].items():
        if pilot.digest(path) != sha:
            raise RuntimeError('Frozen audio input changed: ' + path)
    bp = unreal.load_asset(plan['ability'])
    cdo = blueprint_cdo(bp, unreal)
    protected_cdo = lambda text: re.sub(r'^\s*bUseAuthoredSwingAudio=.*\n?', '', text, flags=re.M)
    if protected_cdo(pilot.inspector.text_export(cdo)) != protected_cdo(plan['before_ability']):
        raise RuntimeError('Non-enable GA data changed; refuse audio-only save')
    if writing and state:
        raise RuntimeError('Transaction exists; inspect saved checkpoints, never repeat Apply')
    if state is None:
        baseline = shared.load(output / 'snapshot.json')
        before = {m['path']: m for m in baseline['montages'] if m['path'].split('.')[-1] in targets}
        if set(plan['points']) != set(before):
            raise RuntimeError('Only the two exact DescendingThrust Montages may change')
        for path, original in before.items():
            current = snapshot(path)
            shared.coverage.verify_montage(original, current)
            if original['audio'] != current['audio']:
                raise RuntimeError('Original audio changed since inspection')
        if not writing:
            print('DESCENDING_AUDIO_READY ' + str(plan['points']))
            return
        state = dict(phase='intent', plan_sha256=plan['plan_sha256'], before=before, backups={}, changed={}, expected={})
        for path in [*before, plan['ability']]:
            disk = pilot.inspector.disk(path)
            backup = output / 'Backup' / disk.relative_to(shared.ROOT)
            backup.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(disk, backup)
            state['backups'][path] = dict(path=str(backup), sha256=pilot.digest(disk))
        pilot.save_json(checkpoint, state)
        for path, points in plan['points'].items():
            asset = unreal.load_asset(path)
            for row in points:
                cue = unreal.load_asset(row['sound'])
                if not isinstance(cue, unreal.SoundCue):
                    raise RuntimeError('Missing resident Cue: ' + row['sound'])
                track = SOUND_TRACKS[row['kind']]
                if not unreal.AnimationLibrary.is_valid_anim_notify_track_name(asset, track):
                    unreal.AnimationLibrary.add_animation_notify_track(asset, track, unreal.LinearColor(.7, .5, .3, 1))
                notify = unreal.AnimationLibrary.add_animation_notify_event(asset, track, row['time'], getattr(unreal, row['kind']))
                for key, value in dict(sound=cue, source=getattr(unreal.MHGZAnimationSoundSource, row['source']),
                        attach_socket=row['socket']).items():
                    notify.set_editor_property(key, value)
            validate_originals(path, before[path], [], len(points))
            pilot.save(asset)
            state['changed'][path] = pilot.digest(pilot.inspector.disk(path))
            state['expected'][path] = snapshot(path)['audio']
            pilot.save_json(checkpoint, state)
        cdo.set_editor_property('use_authored_swing_audio', True)
        if protected_cdo(pilot.inspector.text_export(cdo)) != protected_cdo(plan['before_ability']):
            raise RuntimeError('Unexpected GA field changed')
        pilot.save(bp)
        state['ability_sha256'] = pilot.digest(pilot.inspector.disk(plan['ability']))
        pilot.save_json(checkpoint, state)
    if state['plan_sha256'] != plan['plan_sha256'] or set(state['expected']) != set(plan['points']):
        raise RuntimeError('Partial or foreign transaction; do not repeat Apply')
    if (not read_field(cdo, 'use_authored_swing_audio') or
            pilot.digest(pilot.inspector.disk(plan['ability'])) != state.get('ability_sha256')):
        raise RuntimeError('GA audio enable incomplete or changed')
    for path, points in plan['points'].items():
        if pilot.digest(pilot.inspector.disk(path)) != state['changed'][path]:
            raise RuntimeError('Saved DescendingThrust package changed')
        validate_originals(path, state['before'][path], [], len(points))
        current = snapshot(path)['audio']
        if current != state['expected'][path]:
            raise RuntimeError('Saved audio differs after reload')
        original_ids = {r['guid'] for r in state['before'][path]['audio']}
        added = sorted((r for r in current if r['guid'] not in original_ids), key=lambda r: r['time'])
        if len(added) != len(points):
            raise RuntimeError('Added sound count mismatch')
        for actual, expected in zip(added, sorted(points, key=lambda r: r['time'])):
            if (abs(actual['time'] - expected['time']) > 1e-5 or actual['kind'] != expected['kind']
                    or actual['sound'].split('.')[0] != expected['sound'].split('.')[0]
                    or actual['source'] != str(getattr(unreal.MHGZAnimationSoundSource, expected['source']))
                    or actual['socket'] != expected['socket'] or actual['volume'] != 1. or actual['pitch'] != 1.):
                raise RuntimeError('Added recipe differs from frozen plan')
    state['phase'] = 'saved_pending_independent_reload' if writing else 'independently_reloaded'
    pilot.save_json(checkpoint, state)
    result = dict(phase=state['phase'], points=plan['points'], originals_preserved=True, ga_audio_enabled=True, pie_listened=False)
    pilot.save_json(output / ('apply.json' if writing else 'audit.json'), result)
    print('DESCENDING_AUDIO ' + str(dict(phase=state['phase'], points=sum(map(len, plan['points'].values())))))


def four_hit_impact(writing=False, inspection=False):
    """Single-Montage successor. Reuse resident contact audio; never replay old edits."""
    output = shared.ROOT / 'Saved/Audio/RiseIG/SiLianImpact20261021'
    output.mkdir(parents=True, exist_ok=True)
    pilot.inspector.OUT = output / 'NativeExports'
    path = '/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_SiLianYinZhan'
    asset = unreal.load_asset(path)
    checkpoint = output / 'state.json'
    state = shared.load(checkpoint) if checkpoint.exists() else None
    def snapshot():
        result = pilot.inspector.montage(path)
        result['audio'] = shared.category_recipes(asset, result)
        return result
    if inspection:
        if writing or state:
            raise RuntimeError('Inspect is read-only and must precede the frozen transaction')
        pilot.save_json(output / 'snapshot.json', dict(montages=[snapshot()], sequences=pilot.inspector.sequences))
        print('SILIAN_IMPACT_INSPECT complete')
        return
    plan = shared.load(output / 'source-plan.json')
    if plan['plan_sha256'] != fingerprint({k: v for k, v in plan.items() if k != 'plan_sha256'}):
        raise RuntimeError('Impact plan fingerprint changed')
    for file, sha in plan['inputs'].items():
        if pilot.digest(file) != sha:
            raise RuntimeError('Impact evidence changed: ' + file)
    point = plan['point']
    if point['kind'] != 'AnimNotify_GroundImpact' or len(plan['points']) != 1:
        raise RuntimeError('Expected exactly one supported impact point')
    if writing and state:
        raise RuntimeError('Impact transaction exists; audit before any further writes')
    if state is None:
        before = shared.load(output / 'snapshot.json')['montages'][0]
        live = snapshot()
        shared.coverage.verify_montage(before, live)
        if before['audio'] != live['audio']:
            raise RuntimeError('Montage changed since impact planning')
        if not writing:
            print('SILIAN_IMPACT_READY ' + str(point))
            return
        disk = pilot.inspector.disk(path)
        backup = output / 'Backup' / disk.relative_to(shared.ROOT)
        backup.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(disk, backup)
        cue = unreal.load_asset(point['sound'])
        if not isinstance(cue, unreal.SoundCue):
            raise RuntimeError('Missing resident impact Cue')
        state = dict(phase='intent', before=before, backup=str(backup),
            before_sha256=pilot.digest(disk), plan_sha256=plan['plan_sha256'],
            cue_sha256=pilot.digest(pilot.inspector.disk(point['sound'])))
        pilot.save_json(checkpoint, state)
        track = SOUND_TRACKS[point['kind']]
        if not unreal.AnimationLibrary.is_valid_anim_notify_track_name(asset, track):
            unreal.AnimationLibrary.add_animation_notify_track(asset, track, unreal.LinearColor(.8, .4, .2, 1))
        notify = unreal.AnimationLibrary.add_animation_notify_event(asset, track, point['time'], unreal.AnimNotify_GroundImpact)
        notify.set_editor_property('sound', cue)
        validate_originals(path, before, [], 1)
        pilot.save(asset)
        state.update(phase='saved_pending_independent_reload', after_sha256=pilot.digest(disk), expected=snapshot()['audio'])
        pilot.save_json(checkpoint, state)
    if state.get('plan_sha256') != plan['plan_sha256'] or not state.get('expected'):
        raise RuntimeError('Partial/foreign impact transaction; inspect, do not retry Apply')
    if pilot.digest(pilot.inspector.disk(path)) != state['after_sha256']:
        raise RuntimeError('Saved impact Montage changed')
    if pilot.digest(pilot.inspector.disk(point['sound'])) != state['cue_sha256']:
        raise RuntimeError('Resident contact audio changed')
    validate_originals(path, state['before'], [], 1)
    live = snapshot()
    if live['audio'] != state['expected']:
        raise RuntimeError('Impact recipes differ after reload')
    added = [r for r in live['audio'] if r['guid'] not in {b['guid'] for b in state['before']['audio']}]
    if (len(added) != 1 or added[0]['kind'] != point['kind'] or abs(added[0]['time'] - point['time']) > 1e-5
            or added[0]['sound'].split('.')[0] != point['sound'].split('.')[0]
            or added[0]['source'] != str(unreal.MHGZAnimationSoundSource.GROUND_CONTACT)
            or added[0]['socket'] != 'Root' or added[0]['volume'] != 1. or added[0]['pitch'] != 1.):
        raise RuntimeError('Added impact recipe is not the frozen point')
    state['phase'] = 'saved_pending_independent_reload' if writing else 'independently_reloaded'
    pilot.save_json(checkpoint, state)
    result = dict(phase=state['phase'], montage=path, added=added, originals_preserved=True, pie_listened=False)
    pilot.save_json(output / ('apply.json' if writing else 'audit.json'), result)
    print('SILIAN_IMPACT ' + str(result))


def adjust_dust_volume(writing=False):
    """Single-Cue successor: exactly volume=1, never replay the old mix/recall edit."""
    output = shared.ROOT / 'Saved/Audio/RiseIG/DustVolume20261021'
    output.mkdir(parents=True, exist_ok=True)
    pilot.inspector.OUT = output / 'NativeExports'
    path = '/Game/Weapons/InsectGlaive/Audio/Cues/MuGu/SC_IG_MuGu_3777612407_afd40217'
    cue = unreal.load_asset(path)
    if not isinstance(cue, unreal.SoundCue):
        raise RuntimeError('Missing resident dust Cue')
    disk = pilot.inspector.disk(path)
    checkpoint = output / 'state.json'
    state = shared.load(checkpoint) if checkpoint.exists() else None
    protected = lambda: re.sub(r'^   VolumeMultiplier=.*\n?', '', pilot.inspector.text_export(cue), flags=re.M)
    if state is None:
        state = dict(path=path, before_sha256=pilot.digest(disk),
            before_volume=float(read_field(cue, 'volume_multiplier')), after_volume=1.,
            protected_native=protected(), phase='planned')
        if not writing:
            pilot.save_json(output / 'plan.json', state)
            print('DUST_VOLUME_PLAN ' + str(dict(before=state['before_volume'], after=1.)))
            return
        backup = output / 'Backup' / disk.relative_to(shared.ROOT)
        backup.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(disk, backup)
        state.update(backup=str(backup), phase='intent')
        pilot.save_json(checkpoint, state)
        cue.set_editor_property('volume_multiplier', 1.)
        if protected() != state['protected_native']:
            raise RuntimeError('Dust graph/other audio settings changed')
        pilot.save(cue)
        state.update(after_sha256=pilot.digest(disk), phase='saved_pending_independent_reload')
        pilot.save_json(checkpoint, state)
    elif writing:
        raise RuntimeError('Dust adjustment exists; use default audit, never replay Apply')
    if (state.get('after_sha256') != pilot.digest(disk)
            or float(read_field(cue, 'volume_multiplier')) != 1.
            or protected() != state['protected_native']):
        raise RuntimeError('Dust volume or protected graph differs from the saved adjustment')
    result = dict(phase='saved_pending_independent_reload' if writing else 'independently_reloaded',
        volume_before=state['before_volume'], volume_after=1., graph_and_other_settings_preserved=True)
    state['phase'] = result['phase']
    pilot.save_json(checkpoint, state)
    pilot.save_json(output / ('apply.json' if writing else 'audit.json'), result)
    print('DUST_VOLUME ' + str(result))


def migrate_landing_events(writing=False):
    """Class-only successor; preserve every existing point, recipe and dust clock."""
    output = shared.ROOT / 'Saved/Audio/RiseIG/LandingEvents20261021'
    output.mkdir(parents=True, exist_ok=True)
    pilot.inspector.OUT = output / 'NativeExports'
    checkpoint = output / 'state.json'
    state = shared.load(checkpoint) if checkpoint.exists() else None
    if writing and state:
        raise RuntimeError('Landing migration exists; audit saved packages, never replay Apply')
    if state is None:
        files = sorted(p for folder in ('Content/Weapons/InsectGlaive/Anims/Montage',
                                        'Content/Characters/Demo/Anims/Montage')
                       for p in (shared.ROOT / folder).glob('*.uasset'))
        before, targets = {}, {}
        for disk in files:
            if b'AnimNotify_LandingSound' not in disk.read_bytes():
                continue
            path = '/Game/' + disk.relative_to(shared.ROOT / 'Content').with_suffix('').as_posix()
            asset = unreal.load_asset(path)
            snapshot = pilot.inspector.montage(path)
            rows = shared.category_recipes(asset, snapshot)
            guids = [r['guid'] for r in rows if r['kind'] == 'AnimNotify_LandingSound']
            if guids:
                snapshot['audio'] = rows
                before[path], targets[path] = snapshot, guids
        if not targets:
            raise RuntimeError('No legacy landing events found; inspect current authoring before migration')
        state = dict(phase='planned', before=before, targets=targets, backups={}, changed={}, expected={},
            protected={p.as_posix(): pilot.digest(p) for p in files})
        if not writing:
            pilot.save_json(output / 'plan.json', state)
            print('LANDING_EVENTS_PLAN ' + str(dict(montages=len(targets), points=sum(map(len, targets.values())))))
            return
        for path in targets:
            disk = pilot.inspector.disk(path)
            backup = output / 'Backup' / disk.relative_to(shared.ROOT)
            backup.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(disk, backup)
            state['backups'][path] = dict(path=str(backup), sha256=pilot.digest(disk))
        state['phase'] = 'intent'
        pilot.save_json(checkpoint, state)
        for path, guids in targets.items():
            asset = unreal.load_asset(path)
            for guid in guids:
                value = unreal.Guid()
                value.import_text(guid)
                if not unreal.MHGZAnimationSeamLibrary.replace_sound_notify_class(asset, value, unreal.AnimNotify_LandingEvent):
                    raise RuntimeError('Cannot migrate exact landing event: ' + path + ' ' + guid)
            validate_originals(path, before[path], guids, 0, 'AnimNotify_LandingEvent')
            pilot.save(asset)
            state['changed'][path] = pilot.digest(pilot.inspector.disk(path))
            state['expected'][path] = shared.category_recipes(asset, pilot.inspector.montage(path))
            pilot.save_json(checkpoint, state)
        state['phase'] = 'saved_pending_independent_reload'
        pilot.save_json(checkpoint, state)
    if set(state['expected']) != set(state['targets']):
        raise RuntimeError('Partial landing migration; inspect checkpoints/backups, do not replay Apply')
    for path, guids in state['targets'].items():
        asset = unreal.load_asset(path)
        if pilot.digest(pilot.inspector.disk(path)) != state['changed'][path]:
            raise RuntimeError('Migrated landing package changed: ' + path)
        validate_originals(path, state['before'][path], guids, 0, 'AnimNotify_LandingEvent')
        if shared.category_recipes(asset, pilot.inspector.montage(path)) != state['expected'][path]:
            raise RuntimeError('Landing recipes differ after reload: ' + path)
        for _, notify in pilot.notify_objects(asset):
            if isinstance(notify, unreal.AnimNotify_LandingEvent) and (
                    read_field(notify, 'trigger_ground_dust') or read_field(notify, 'ground_dust_sound')):
                raise RuntimeError('Migration must not add automatic dust/audio over existing points')
    changed = {pilot.inspector.disk(p).as_posix() for p in state['changed']}
    for path, sha in state['protected'].items():
        if path not in changed and pilot.digest(path) != sha:
            raise RuntimeError('Unrelated Montage changed: ' + path)
    result = dict(phase='saved_pending_independent_reload' if writing else 'independently_reloaded',
        montages=len(state['changed']), landing_events=sum(map(len, state['targets'].values())),
        other_montages_unchanged=len(state['protected']) - len(changed),
        original_timing_recipes_and_separate_dust_preserved=True, pie_observed=False)
    state['phase'] = result['phase']
    pilot.save_json(checkpoint, state)
    pilot.save_json(output / ('apply.json' if writing else 'audit.json'), result)
    print('LANDING_EVENTS ' + str(result))


def inspect():
    pilot.inspector.OUT = OUT / 'NativeExports'
    packages = []
    for file in (shared.ROOT / 'Content/Weapons/InsectGlaive/Anims/Montage').glob('*.uasset'):
        # Restrict full native reads to the four requested Montages and dust owners.
        if file.stem in TARGETS or b'AnimNotify_DustSound' in file.read_bytes():
            packages.append('/Game/' + file.relative_to(shared.ROOT / 'Content').with_suffix('').as_posix())
    montages = []
    for path in sorted(packages):
        asset = unreal.load_asset(path)
        snapshot = pilot.inspector.montage(path)
        snapshot['audio'] = shared.category_recipes(asset, snapshot)
        montages.append(snapshot)
    result = dict(montages=montages, sequences=pilot.inspector.sequences)
    pilot.save_json(OUT / 'snapshot.json', result)
    print('GROUND_AUDIO_INSPECT ' + str(dict(montages=len(montages),
        targets={m['path'].split('/')[-1]: len(m['audio']) for m in montages if m['path'].split('.')[-1] in TARGETS})))
    return result


def run(writing=False, inspection=False, resume=False):
    pilot.inspector.OUT = OUT / 'NativeExports'
    if inspection:
        if (OUT / 'state.json').exists():
            raise RuntimeError('Frozen authoring transaction exists; use its default audit')
        return inspect()
    plan = shared.load(OUT / 'source-plan.json')
    if plan['plan_sha256'] != fingerprint({k: v for k, v in plan.items() if k != 'plan_sha256'}):
        raise RuntimeError('Frozen source plan changed')
    for path, sha in plan['inputs'].items():
        if pilot.digest(path) != sha:
            raise RuntimeError('Capture input changed: ' + path)
    if pilot.digest(OUT / 'snapshot.json') != plan['snapshot_sha256']:
        raise RuntimeError('Frozen asset snapshot changed')
    checkpoint = OUT / 'state.json'
    state = shared.load(checkpoint) if checkpoint.exists() else None
    if writing and state:
        if not resume or state['phase'] != 'intent' or state['changed'] or state['expected']:
            raise RuntimeError('Ground transaction exists; do not replay Apply or overwrite a partial Montage save')
        # The only supported resume is before the FIRST Montage save, after a
        # diagnostic failure. Existing imported sources are verified, not overwritten.
        for path, sha in state['protected'].items():
            if pilot.digest(path) != sha:
                raise RuntimeError('Cannot resume: original package changed: ' + path)
        for path, sha in state['new_assets'].items():
            if pilot.digest(pilot.inspector.disk(path)) != sha:
                raise RuntimeError('Cannot resume: prepared source changed: ' + path)
    if state and state['plan_sha256'] != plan['plan_sha256']:
        raise RuntimeError('Transaction belongs to a different plan')
    if state is None:
        snapshot = shared.load(OUT / 'snapshot.json')
        before, migrations = {}, {}
        for m in snapshot['montages']:
            dust = [r['guid'] for r in m['audio'] if r['kind'] == 'AnimNotify_DustSound']
            if not dust and not plan['points'].get(m['path']):
                continue
            live = pilot.inspector.montage(m['path'])
            shared.coverage.verify_montage(m, live)
            if shared.category_recipes(unreal.load_asset(m['path']), live) != m['audio']:
                raise RuntimeError('Audio changed since source planning: ' + m['path'])
            before[m['path']] = m
            migrations[m['path']] = dust
        if not all(plan['points'].get(next(p for p in plan['points'] if p.split('.')[-1] == n)) for n in TARGETS):
            raise RuntimeError('One of the four requested missing-sound scopes has no supported points')
        protected = {p.as_posix(): pilot.digest(p) for folder in
            ('Content/Weapons/InsectGlaive', 'Content/Blueprints/Ability/InsectGlaive', 'Content/GameplayCues')
            for p in (shared.ROOT / folder).rglob('*.uasset')}
        state = dict(phase='planned', plan_sha256=plan['plan_sha256'], before=before,
            migrations=migrations, protected=protected, expected={}, backups={}, changed={}, new_assets={})
        if not writing:
            print('GROUND_AUDIO_READY ' + str(dict(montages=len(before),
                dust_events=sum(map(len, migrations.values())), additions=sum(map(len, plan['points'].values())))))
            return
        for path in before:
            disk = pilot.inspector.disk(path)
            backup = OUT / 'Backup' / disk.relative_to(shared.ROOT)
            backup.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(disk, backup)
            state['backups'][path] = dict(path=str(backup), sha256=pilot.digest(disk))
        state['phase'] = 'intent'
        pilot.save_json(checkpoint, state)

    from setup_propulsion_audio import sources
    source = dict(plan['new_source'], txtp_sha256=plan['new_source']['branch']['txtp_sha256'])
    sources(dict(source=source), writing, state, output=OUT, owner='mhgz_audio_ground_20261021_v1')
    if writing:
        for path, before in state['before'].items():
            asset = unreal.load_asset(path)
            for guid in state['migrations'][path]:
                value = unreal.Guid()
                value.import_text(guid)
                if not unreal.MHGZAnimationSeamLibrary.replace_sound_notify_class(asset, value, unreal.AnimNotify_GroundDust):
                    raise RuntimeError('Cannot migrate exact dust event: ' + path + ' ' + guid)
            for row in plan['points'].get(path, []):
                track = SOUND_TRACKS[row['kind']]
                if not unreal.AnimationLibrary.is_valid_anim_notify_track_name(asset, track):
                    unreal.AnimationLibrary.add_animation_notify_track(asset, track, unreal.LinearColor(.8, .6, .2, 1))
                notify = unreal.AnimationLibrary.add_animation_notify_event(asset, track, row['time'], getattr(unreal, row['kind']))
                enum = getattr(unreal.MHGZAnimationSoundSource, row['source'])
                for key, value in dict(sound=unreal.load_asset(row['sound']), source=enum,
                    attach_socket=row['socket'], volume_multiplier=row['volume'], pitch_multiplier=row['pitch'],
                    ground_trace_distance=row['ground_trace_distance']).items():
                    if key == 'sound' and value is None:
                        raise RuntimeError('Missing exact resident sound: ' + row['sound'])
                    notify.set_editor_property(key, value)
            validate_originals(path, before, state['migrations'][path], len(plan['points'].get(path, [])))
            pilot.save(asset)
            state['changed'][path] = pilot.digest(pilot.inspector.disk(path))
            state['expected'][path] = shared.category_recipes(asset, pilot.inspector.montage(path))
            pilot.save_json(checkpoint, state)
        state['phase'] = 'saved_pending_independent_reload'
        pilot.save_json(checkpoint, state)
    if set(state['expected']) != set(state['before']):
        raise RuntimeError('Partial transaction: inspect checkpoints before any retry')
    for path, before in state['before'].items():
        if pilot.digest(pilot.inspector.disk(path)) != state['changed'][path]:
            raise RuntimeError('Saved package changed since transaction: ' + path)
        validate_originals(path, before, state['migrations'][path], len(plan['points'].get(path, [])))
        if shared.category_recipes(unreal.load_asset(path), pilot.inspector.montage(path)) != state['expected'][path]:
            raise RuntimeError('Reloaded audio recipe changed: ' + path)
    changed = {pilot.inspector.disk(p).as_posix() for p in state['changed']}
    volume_successors = 0
    for path, sha in state['protected'].items():
        if path not in changed and pilot.digest(path) != sha:
            successor = shared.ROOT / 'Saved/Audio/RiseIG/DustVolume20261021/state.json'
            volume = shared.load(successor) if successor.exists() else {}
            if (volume.get('phase') in ('saved_pending_independent_reload', 'independently_reloaded')
                    and volume.get('path') == '/Game/Weapons/InsectGlaive/Audio/Cues/MuGu/SC_IG_MuGu_3777612407_afd40217'
                    and pilot.inspector.disk(volume['path']).as_posix() == path
                    and volume.get('before_sha256') == sha and volume.get('after_sha256') == pilot.digest(path)):
                volume_successors += 1
            else:
                raise RuntimeError('Unrelated package changed: ' + path)
    for path, sha in state['new_assets'].items():
        if pilot.digest(pilot.inspector.disk(path)) != sha:
            raise RuntimeError('New source package changed since creation: ' + path)
    state['phase'] = 'saved_pending_independent_reload' if writing else 'independently_reloaded'
    pilot.save_json(checkpoint, state)
    result = dict(phase=state['phase'], montages=len(state['changed']),
        dust_events=sum(map(len, state['migrations'].values())),
        added_points=sum(map(len, plan['points'].values())), new_source_packages=len(state['new_assets']),
        other_packages_unchanged=len(state['protected']) - len(changed) - volume_successors,
        authorized_dust_volume_successors=volume_successors, pie_listened=False)
    pilot.save_json(OUT / ('apply.json' if writing else 'audit.json'), result)
    print('GROUND_AUDIO_RESULT ' + str(result))


def validate_originals(path, before, migrations, additions, migration_kind='AnimNotify_GroundDust'):
    after = pilot.inspector.montage(path)
    normalized = shared.verify_preserved(before, after, [])
    originals = {r['Guid']: r for r in before['existing_notifies']}
    for event in normalized['existing_notifies']:
        if event['Guid'] in migrations:
            event['Notify'] = originals[event['Guid']]['Notify']
    shared.coverage.verify_montage(before, normalized)
    rows = shared.category_recipes(unreal.load_asset(path), after)
    old_rows = {r['guid']: r for r in before['audio']}
    if len(rows) != len(old_rows) + additions:
        raise RuntimeError('Unexpected audio event count: ' + path)
    for row in rows:
        if row['guid'] not in old_rows:
            continue
        expected = dict(old_rows[row['guid']])
        if row['guid'] in migrations:
            expected['kind'] = migration_kind
        if row != expected:
            raise RuntimeError('Original event recipe changed: ' + row['guid'])
