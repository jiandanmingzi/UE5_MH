"""Section-aware audio maintenance; invoked by the existing audio setup entry.

Reuses the native snapshot/notify readers. Never replay old frozen transactions.
Inspection writes a new Saved snapshot, not the mapping baseline used by old plans.
"""
import json
import copy
import shutil
import sys
import unreal
import audio_spatial_maintenance as shared
from section_audio_rules import fingerprint, OUT

ROOT = shared.ROOT


def inspect():
    inspector = shared.pilot.inspector
    inspector.OUT = OUT / 'Snapshot'
    inspector.sequences.clear()
    inspector.protected.clear()
    inspector.main()
    report = json.loads((inspector.OUT / 'asset-snapshot.json').read_text(encoding='utf-8'))
    for montage in report['montages']:
        asset = unreal.load_asset(montage['path'])
        montage['audio'] = shared.category_recipes(asset, montage)
    # Independent forward roll / sheathe are outside the IG Montage folder.
    for filename in sorted((ROOT / 'Content/Characters/Demo/Anims/Montage').glob('*.uasset')):
        path = '/Game/' + filename.relative_to(ROOT / 'Content').with_suffix('').as_posix()
        montage = inspector.montage(path)
        montage['audio'] = shared.category_recipes(unreal.load_asset(path), montage)
        report['montages'].append(montage)
    report['sequences'] = inspector.sequences
    report['protected_package_hashes'] = inspector.protected
    report['content_unchanged'] = all(shared.pilot.digest(p) == sha
                                    for p, sha in inspector.protected.items())
    if not report['content_unchanged']:
        raise RuntimeError('Inspection changed Content')
    shared.pilot.save_json(OUT / 'snapshot.json', report)
    print('SECTION_AUDIO_INSPECT ' + str(dict(montages=len(report['montages']),
        points=sum(len(m['audio']) for m in report['montages']), output=str(OUT))))


def verify(before, live, additions):
    # Preserve all original event objects/IDs/order/links/payload, not just damage windows.
    old = {r['Guid'] for r in before['existing_notifies']}
    normalized = copy.deepcopy(live)
    normalized['existing_notifies'] = [r for r in live['existing_notifies'] if r['Guid'] in old]
    shared.coverage.verify_montage(before, normalized)
    if [r for r in live['audio'] if r['guid'] in old] != before['audio']:
        raise RuntimeError('Original audio recipes changed')
    added = [r for r in live['audio'] if r['guid'] not in old]
    if len(added) != len(additions):
        raise RuntimeError('Unexpected added sound count')
    for point, expected in zip(added, additions):
        recipe = dict(expected['original'], guid=point['guid'], time=point['time'])
        if abs(point['time'] - expected['time']) > 1e-5 or point != recipe:
            raise RuntimeError('Branch copy differs from the original sound recipe')
    return added


def run(writing=False):
    pilot = shared.pilot
    pilot.inspector.OUT = OUT / ('ApplyExports' if writing else 'AuditExports')
    plan = shared.load(OUT / 'plan.json')
    state_path = OUT / 'state.json'
    state = shared.load(state_path) if state_path.exists() else None
    if plan['plan_sha256'] != fingerprint({k: v for k, v in plan.items() if k != 'plan_sha256'}):
        raise RuntimeError('Frozen plan changed')
    for file, sha in plan['inputs'].items():
        if pilot.digest(file) != sha:
            raise RuntimeError('Frozen evidence changed: ' + file)
    if writing and state:
        raise RuntimeError('Repair already started; inspect saved postconditions, do not retry Apply')
    if state and state['phase'] not in ('saved_pending_independent_reload', 'independently_reloaded'):
        raise RuntimeError('Incomplete repair; audit per-package checkpoint before recovery')
    expected = state['expected_hashes'] if state else plan['protected']
    for file, sha in expected.items():
        if pilot.digest(file) != sha:
            raise RuntimeError('Saved package changed: ' + file)
    before = {m['path']: m for m in shared.load(OUT / 'snapshot.json')['montages']}
    loaded = {path: unreal.load_asset(path) for path in plan['patches']}
    if any(not isinstance(asset, unreal.AnimMontage) for asset in loaded.values()):
        raise RuntimeError('Missing target Montage')
    def snapshot(path):
        result = pilot.inspector.montage(path)
        result['audio'] = shared.category_recipes(loaded[path], result)
        return result
    if not state:
        for path in loaded:
            shared.coverage.verify_montage(before[path], snapshot(path))
        if not writing:
            print('SECTION_AUDIO_READY ' + str(dict(montages=len(loaded), points=sum(map(len, plan['patches'].values())))))
            return
        state = dict(phase='intent', expected_hashes=dict(plan['protected']), backups={}, added={})
        for path in loaded:
            disk = pilot.inspector.disk(path)
            backup = OUT / 'Backup' / disk.relative_to(ROOT)
            backup.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(disk, backup)
            state['backups'][path] = str(backup)
        pilot.save_json(state_path, state)
        for path, asset in loaded.items():
            originals = {n.get_name(): n for _, n in pilot.notify_objects(asset)}
            for patch in plan['patches'][path]:
                native = next(r for r in before[path]['existing_notifies'] if r['Guid'] == patch['source_guid'])
                import re
                source = next(n for name, n in originals.items() if re.search(
                    r"'(?:[^']*:)?" + re.escape(name) + r"'", native.get('Notify', '')))
                tracks = unreal.AnimationLibrary.get_animation_notify_track_names(asset)
                track_name = tracks[int(native.get('TrackIndex', 0))]
                added = unreal.AnimationLibrary.add_animation_notify_event(asset, track_name, patch['time'], source.get_class())
                for field in ('event_tag', 'sound', 'source', 'attach_socket', 'volume_multiplier',
                              'pitch_multiplier', 'legacy_attack_segment_index', 'ground_trace_distance'):
                    added.set_editor_property(field, shared.read_field(source, field))
            added = verify(before[path], snapshot(path), plan['patches'][path])
            pilot.save(asset)
            state['expected_hashes'][pilot.inspector.disk(path).as_posix()] = pilot.digest(pilot.inspector.disk(path))
            state['added'][path] = added
            pilot.save_json(state_path, state)
        state['phase'] = 'saved_pending_independent_reload'
        pilot.save_json(state_path, state)
    results = {}
    for path in loaded:
        results[path] = verify(before[path], snapshot(path), plan['patches'][path])
        if results[path] != state['added'][path]:
            raise RuntimeError('Saved copy GUID or recipe changed')
    for file, sha in state['expected_hashes'].items():
        if pilot.digest(file) != sha:
            raise RuntimeError('Protected package changed during repair: ' + file)
    state['phase'] = 'saved_pending_independent_reload' if writing else 'independently_reloaded'
    pilot.save_json(state_path, state)
    result = dict(phase=state['phase'], added=results, originals_preserved=True, protected_unchanged=True, pie_listened=False)
    pilot.save_json(OUT / ('apply.json' if writing else 'audit.json'), result)
    print('SECTION_AUDIO ' + str(dict(phase=state['phase'], montages=len(results), points=sum(map(len, results.values())))))


def main():
    command = unreal.SystemLibrary.get_command_line()
    if '--source-time-inspect' in sys.argv or '-MHGZAudioSourceTimeInspect' in command:
        return inspect_contact_source()
    if '--source-time-repair' in sys.argv or '-MHGZAudioSourceTimeRepair' in command:
        return repair_source_times('--apply' in sys.argv or '-MHGZCurrentWeaponAudioApply' in command)
    if '--inspect' in sys.argv or '-MHGZAudioSectionInspect' in command:
        if (OUT / 'plan.json').exists() or (OUT / 'state.json').exists():
            raise RuntimeError('Frozen evidence exists; do not overwrite inspection')
        return inspect()
    if (ROOT / 'Saved/Audio/RiseIG/SourceTime20261022/state.json').exists():
        return repair_source_times('--apply' in sys.argv or '-MHGZCurrentWeaponAudioApply' in command)
    return run('--apply' in sys.argv or '-MHGZCurrentWeaponAudioApply' in command)


def inspect_contact_source():
    """Read the actual161 weapon endpoints at the recorded and surrounding frames."""
    output = ROOT / 'Saved/Audio/RiseIG/SourceTime20261022'
    output.mkdir(parents=True, exist_ok=True)
    pilot = shared.pilot
    pilot.inspector.OUT = output / 'NativeExports'
    path = '/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_SiLianYinZhan'
    montage = pilot.inspector.montage(path)
    asset = unreal.load_asset(path)
    montage['audio'] = shared.category_recipes(asset, montage)
    source = unreal.load_asset('/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_R_YinZhan')
    body = unreal.load_asset('/Game/Characters/Demo/Meshes/Body/SKM_Demo_Body')
    weapon = unreal.load_asset('/Game/Weapons/InsectGlaive/Meshes/Glaive/SKM_IG_Glaive')
    definition = unreal.load_asset('/Game/Weapons/InsectGlaive/Data/DA_IG_HuoLongGun')
    attach = shared.read_field(definition, 'attach_socket')
    skeleton = shared.read_field(weapon, 'skeleton')
    reference = unreal.AnimPoseExtensions.get_reference_pose(skeleton)
    # SkeletalMesh.Sockets is protected in5.6; use the already-verified indexed API.
    sockets = [weapon.get_socket_by_index(i) for i in range(weapon.num_sockets())]
    # NumSockets/GetSocketByIndex already include this mesh AND its skeleton.
    endpoints = {}
    for socket in sockets:
        name = str(shared.read_field(socket, 'socket_name'))
        if name in ('IG_FrontTip', 'IG_RearTip'):
            parent = shared.read_field(socket, 'bone_name')
            bone = reference.get_bone_pose(parent, unreal.AnimPoseSpaces.WORLD)
            endpoints[name] = bone.transform_location(shared.read_field(socket, 'relative_location'))
    if len(endpoints) != 2:
        raise RuntimeError('Missing real weapon endpoint sockets')
    options = unreal.AnimPoseEvaluationOptions()
    options.evaluation_type = unreal.AnimDataEvalType.RAW
    options.should_retarget = False
    options.extract_root_motion = False
    options.incorporate_root_motion_into_pose = False
    options.optional_skeletal_mesh = body
    def xyz(v):
        return [float(v.x), float(v.y), float(v.z)]
    samples = []
    segment = next(s for s in montage['segments'] if s['sequence'] == source.get_path_name() and s['index'] == 2)
    frame_rate = source.data_model_interface.get_frame_rate()
    fps = frame_rate.numerator / frame_rate.denominator
    for frame in range(160, 301):
        time = frame / fps
        pose = source.get_anim_pose_at_time(time, options)
        transform = pose.get_socket_pose(attach, unreal.AnimPoseSpaces.WORLD)
        row = dict(frame=frame, source_time=time, native_frame=frame / 2.,
            montage_time=segment['track_start_s'] + (time - segment['source_start_s']) / segment['effective_rate'],
            endpoints={name: xyz(transform.transform_location(v)) for name, v in endpoints.items()},
            bones={name: xyz(pose.get_bone_pose(name, unreal.AnimPoseSpaces.WORLD).translation)
                   for name in ('Root', 'R_Weapon_00', 'L_Weapon_00', 'R_Leg_03', 'L_Leg_03')})
        samples.append(row)
    report = dict(montage=montage, samples=samples, attach_socket=str(attach),
                  weapon_endpoints_reference={name: xyz(v) for name, v in endpoints.items()})
    if any(pilot.digest(p) != sha for p, sha in pilot.inspector.protected.items()):
        raise RuntimeError('Pose inspection changed Content')
    pilot.save_json(output / 'contact-pose.json', report)
    print('CONTACT_SOURCE_INSPECT ' + str(dict(samples=len(samples), attach=str(attach), output=str(output))))


def repair_source_times(writing=False):
    output = ROOT / 'Saved/Audio/RiseIG/SourceTime20261022'
    pilot = shared.pilot
    pilot.inspector.OUT = output / ('ApplyExports' if writing else 'ReloadExports')
    plan = shared.load(output / 'plan.json')
    checkpoint = output / 'state.json'
    state = shared.load(checkpoint) if checkpoint.exists() else None
    if fingerprint({k: v for k, v in plan.items() if k != 'plan_sha256'}) != plan['plan_sha256']:
        raise RuntimeError('Source time plan changed')
    if writing and state:
        raise RuntimeError('Source time repair already started; inspect before retry')
    if state and state['phase'] not in ('saved_pending_independent_reload', 'independently_reloaded'):
        raise RuntimeError('Incomplete source-time transaction; no automatic replay')
    for file, sha in plan['inputs'].items():
        if pilot.digest(file) != sha:
            raise RuntimeError('Recording or frozen source plan changed')
    expected = state['expected_hashes'] if state else plan['protected']
    for file, sha in expected.items():
        if pilot.digest(file) != sha:
            raise RuntimeError('Changed package: ' + file)
    before = {m['path']: m for m in plan['before']['montages']}
    loaded = {path: unreal.load_asset(path) for path in plan['changes']}
    def snapshot(path):
        result = pilot.inspector.montage(path)
        result['audio'] = shared.category_recipes(loaded[path], result)
        return result
    def validate(path):
        live = snapshot(path)
        ops = {c['guid']: c for c in plan['changes'][path]}
        normalized_before, normalized_after = copy.deepcopy(before[path]), copy.deepcopy(live)
        normalized_before['existing_notifies'] = [n for n in before[path]['existing_notifies'] if n['Guid'] not in ops]
        normalized_after['existing_notifies'] = [n for n in live['existing_notifies'] if n['Guid'] not in ops]
        shared.coverage.verify_montage(normalized_before, normalized_after)
        old = {n['Guid']: n for n in before[path]['existing_notifies']}
        current = {n['Guid']: n for n in live['existing_notifies']}
        if set(current) != set(old) - {guid for guid, c in ops.items() if c['remove']}:
            raise RuntimeError('Unapproved notify identity change')
        allowed = {'LinkValue', 'SegmentIndex', 'SegmentBeginTime', 'SegmentLength', 'LinkedSequence', 'TriggerTimeOffset'}
        for guid, op in ops.items():
            if op['remove']:
                continue
            if {k: v for k, v in old[guid].items() if k not in allowed} != {
                    k: v for k, v in current[guid].items() if k not in allowed}:
                raise RuntimeError('Retime changed a non-time event property')
        by_audio = {r['guid']: r for r in live['audio']}
        for row in before[path]['audio']:
            op = ops.get(row['guid'])
            if op and op['remove']:
                if row['guid'] in by_audio: raise RuntimeError('Removed wrong-source sound still exists')
                continue
            actual = by_audio[row['guid']]
            time = op['time'] if op else row['time']
            if abs(actual['time'] - time) > 1e-5 or dict(actual, time=row['time']) != row:
                raise RuntimeError('Wrong retime or changed audio recipe')
        return dict(remaining_audio=len(live['audio']), operations=len(ops), gameplay_and_structure_preserved=True)
    if not state:
        for path in loaded:
            shared.coverage.verify_montage(before[path], snapshot(path))
        if not writing:
            print('SOURCE_TIME_READY ' + str(dict(montages=len(loaded))))
            return
        state = dict(phase='intent', expected_hashes=dict(plan['protected']), backups={}, saved=[])
        for path in loaded:
            disk = pilot.inspector.disk(path)
            backup = output / 'Backup' / disk.relative_to(ROOT)
            backup.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(disk, backup)
            state['backups'][path] = str(backup)
        pilot.save_json(checkpoint, state)
        for path, asset in loaded.items():
            ops = [{k: v for k, v in row.items() if k in ('guid', 'expected_time', 'time', 'remove')}
                   for row in plan['changes'][path]]
            if not unreal.MHGZAnimationSeamLibrary.edit_sound_notify_times(asset, json.dumps(ops)):
                raise RuntimeError('Guarded native time edit rejected: ' + path)
            validate(path)
            pilot.save(asset)
            state['saved'].append(path)
            state['expected_hashes'][pilot.inspector.disk(path).as_posix()] = pilot.digest(pilot.inspector.disk(path))
            pilot.save_json(checkpoint, state)
        state['phase'] = 'saved_pending_independent_reload'
        pilot.save_json(checkpoint, state)
    result = {path: validate(path) for path in loaded}
    if any(pilot.digest(file) != sha for file, sha in state['expected_hashes'].items()):
        raise RuntimeError('Protected package changed after source-time edit')
    state['phase'] = 'saved_pending_independent_reload' if writing else 'independently_reloaded'
    pilot.save_json(checkpoint, state)
    removed = sum(c['remove'] for ops in plan['changes'].values() for c in ops)
    retimed = sum(not c['remove'] for ops in plan['changes'].values() for c in ops)
    pilot.save_json(output / ('apply.json' if writing else 'audit.json'),
        dict(phase=state['phase'], results=result, retimed=retimed, removed=removed, pie_audible=False))
    print('SOURCE_TIME_REPAIR ' + str(dict(phase=state['phase'], montages=len(result), retimed=retimed, removed=removed)))
