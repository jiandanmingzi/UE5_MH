"""Scoped successor to the frozen current-weapon transactions; audit by default.

Invoked through setup_current_weapon_audio --spatial-maintenance. Reuses its
export, backup, factory and author-data helpers. Never replay the old Apply.
"""
from pathlib import Path
import json
import re
import shutil
import sys
import unreal

import setup_swing_coverage as coverage
from ue_asset_utils import blueprint_cdo, read_field
from spatial_audio_rules import ACTION_BRANCHES, emission, independent_points, verify_preserved, notify_category

pilot = coverage.pilot
ROOT = pilot.ROOT
BASE = ROOT / 'Saved/Audio/RiseIG/Weapon20261019'
OUT = ROOT / 'Saved/Audio/RiseIG/SpatialMaintenance20261020'
CATEGORY_OUT = ROOT / 'Saved/Audio/RiseIG/NotifyCategories20261020'
CLOTH_OUT = ROOT / 'Saved/Audio/RiseIG/ClothComparison20261020/UE'
OUT.mkdir(parents=True, exist_ok=True)
pilot.inspector.OUT = OUT / 'NativeExports'


def load(path):
    return json.loads(Path(path).read_text(encoding='utf-8'))


def preflight():
    plan = load(BASE / 'UEActionRuntime/plan.json')
    sources = {s['branch_id']: s for s in plan['sources']}
    result = dict(phase='read_only_preflight', sources={}, settings={}, montages={})
    for branch in ('B008', 'B016', 'B020', 'B021', 'B022', 'B026'):
        source = sources[branch]
        cue = unreal.load_asset(source['cue'])
        result['sources'][branch] = dict(
            cue=source['cue'], volume=float(read_field(cue, 'volume_multiplier')),
            attenuation=str(read_field(cue, 'attenuation_settings')),
            concurrency=[str(v) for v in read_field(cue, 'concurrency_set')],
            variants=[{k: v[k] for k in ('rms_dbfs', 'peak', 'channels', 'wav')} for v in source['variants']],
            native=pilot.inspector.text_export(cue))
    for path in (pilot.ATTENUATION, pilot.CONCURRENCY):
        result['settings'][path] = pilot.inspector.text_export(unreal.load_asset(path))
    for package in sorted((ROOT / 'Content/Weapons/InsectGlaive/Anims/Montage').glob('*.uasset')):
        path = '/Game/' + package.relative_to(ROOT / 'Content').with_suffix('').as_posix()
        result['montages'][path] = pilot.inspector.montage(path)
    character = blueprint_cdo(unreal.load_asset('/Game/Blueprints/Characters/Demo/BP_IG_Character'), unreal)
    mesh = read_field(character, 'mesh')
    result['character_sockets'] = [str(n) for n in mesh.get_all_socket_names()]
    weapon = unreal.load_asset('/Game/Weapons/InsectGlaive/Data/DA_IG_HuoLongGun')
    result['weapon_definition'] = pilot.inspector.text_export(weapon)
    result['combat_config'] = pilot.inspector.text_export(unreal.load_asset('/Game/Weapons/InsectGlaive/Data/DA_IG_Combat'))
    result['sheathed_dodge'] = pilot.inspector.montage('/Game/Characters/Demo/Anims/Montage/AM_Shth_Dodge')
    protected = {}
    for folder in ('Content/Weapons/InsectGlaive/Anims', 'Content/Blueprints/Ability',
                   'Content/Weapons/InsectGlaive/Audio', 'Content/Weapons/InsectGlaive/Data'):
        for filename in (ROOT / folder).rglob('*.uasset'):
            protected[filename.as_posix()] = pilot.digest(filename)
    baseline = OUT / 'baseline.json'
    if not baseline.exists():
        pilot.save_json(baseline, dict(protected=protected, montages=result['montages']))
    pilot.save_json(OUT / 'preflight.json', result)
    print('AUDIO_SPATIAL_PREFLIGHT ' + json.dumps(dict(
        montages=len(result['montages']), sources=len(result['sources']),
        sockets=result['character_sockets'], output=str(OUT))))


def sources():
    return {s['branch_id']: s for s in load(BASE / 'UEActionRuntime/plan.json')['sources']}


def point_rows(asset, snapshot):
    result = []
    for _, notify in pilot.notify_objects(asset):
        native = next(r for r in snapshot['existing_notifies'] if re.search(
            r"'(?:[^']*:)?" + re.escape(notify.get_name()) + r"'", r.get('Notify', '')))
        sound = read_field(notify, 'sound')
        result.append(dict(guid=native['Guid'], time=float(native.get('LinkValue', 0)),
                           sound=sound.get_path_name() if sound else None,
                           source=str(read_field(notify, 'source')), socket=str(read_field(notify, 'attach_socket')),
                           volume=float(read_field(notify, 'volume_multiplier')), pitch=float(read_field(notify, 'pitch_multiplier')),
                           kind=notify.get_class().get_name()))
    return result


def settings(name, count=None):
    path = pilot.AUDIO + '/Settings/' + name
    cls, factory = ((unreal.SoundConcurrency, unreal.SoundConcurrencyFactory()) if count
                    else (unreal.SoundAttenuation, unreal.SoundAttenuationFactory()))
    obj = unreal.load_asset(path) if pilot.inspector.disk(path).exists() else None
    if obj:
        raise RuntimeError('New settings already exist without a completed checkpoint: ' + path)
    obj = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, path.rsplit('/', 1)[0], cls, factory)
    if count:
        value = read_field(obj, 'concurrency')
        value.set_editor_property('max_count', count)
        value.set_editor_property('limit_to_owner', True)
        value.set_editor_property('resolution_rule', unreal.MaxConcurrentResolutionRule.STOP_OLDEST)
        obj.set_editor_property('concurrency', value)
    else:
        value = read_field(obj, 'attenuation')
        inner, falloff, db = ((75, 3500, -30) if name.endswith('LieChong') else (700, 3000, -36))
        for field, val in dict(attenuate=True, spatialize=True, attenuate_with_lpf=False,
                               enable_occlusion=False, enable_reverb_send=False,
                               attenuation_shape_extents=unreal.Vector(inner, 0, 0),
                               falloff_distance=float(falloff), d_b_attenuation_at_max=float(db),
                               distance_algorithm=unreal.AttenuationDistanceModel.NATURAL_SOUND).items():
            value.set_editor_property(field, val)
        obj.set_editor_property('attenuation', value)
    pilot.save(obj)
    return obj


def apply():
    if (OUT / 'state.json').exists():
        raise RuntimeError('Existing transaction: audit first; never replay Apply or discard its baseline')
    baseline = load(OUT / 'baseline.json')
    for path, sha in baseline['protected'].items():
        if pilot.digest(path) != sha:
            raise RuntimeError('Asset changed since preflight: ' + path)
    mapping, pool = load(BASE / 'MappingFinal/mapping.json'), sources()
    additions = independent_points(mapping)
    by_cue = {s['cue'] + '.' + s['cue'].rsplit('/', 1)[-1]: key for key, s in pool.items()}
    paths = {p['montage'].split('.')[0] for p in additions}
    old_points = {}
    for path, snap in baseline['montages'].items():
        rows = point_rows(unreal.load_asset(path), snap)
        if rows:
            paths.add(path)
            old_points[path] = rows
    tune = {branch: (4.0 if branch == 'B008' else 1.2 if branch == 'B016'
                      else 2.0 if branch in {'B022', 'B026'} else 10 ** (-4 / 20) if branch == 'B021' else 1.0)
            for branch in ACTION_BRANCHES | {'B022', 'B026', 'B021'}}
    loop = pilot.AUDIO + '/Cues/MuGu/SC_IG_LieChong_FlightLoop_2640762921'
    save_paths = paths | {pool[b]['cue'] for b in tune} | {loop}
    state = dict(phase='intent', before={}, old_points=old_points, planned_additions=additions,
                 changed={}, new_assets=[], original_hashes=baseline['protected'], backups={})
    for path in sorted(save_paths):
        disk = pilot.inspector.disk(path)
        backup = OUT / 'Backup' / disk.relative_to(ROOT)
        backup.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(disk, backup)
        state['backups'][path] = dict(path=str(backup), sha256=pilot.digest(disk))
        if path in paths:
            state['before'][path] = pilot.inspector.montage(path)
    pilot.save_json(OUT / 'state.json', state)
    flight = settings('SA_IG_LieChong')
    action = settings('SA_IG_DongZuo')
    extract = settings('SA_IG_JingHua')
    flight_cc = settings('SCN_IG_LieChong', 4)
    action_cc = settings('SCN_IG_DongZuo', 16)
    extract_cc = settings('SCN_IG_JingHua', 4)
    for obj in (flight, action, extract, flight_cc, action_cc, extract_cc):
        state['new_assets'].append(obj.get_path_name())
        state['changed'][obj.get_path_name()] = pilot.digest(pilot.inspector.disk(obj.get_path_name()))
    for branch, volume in tune.items():
        obj = unreal.load_asset(pool[branch]['cue'])
        obj.set_editor_property('volume_multiplier', volume)
        obj.set_editor_property('attenuation_settings', flight if branch == 'B021' else extract if branch == 'B026' else action)
        obj.set_editor_property('concurrency_set', [flight_cc if branch == 'B021' else extract_cc if branch == 'B026' else action_cc])
        pilot.save(obj)
        state['changed'][obj.get_path_name()] = pilot.digest(pilot.inspector.disk(obj.get_path_name()))
    obj = unreal.load_asset(loop)
    obj.set_editor_property('volume_multiplier', 10 ** (-8 / 20))
    obj.set_editor_property('attenuation_settings', flight)
    obj.set_editor_property('concurrency_set', [flight_cc])
    pilot.save(obj)
    state['changed'][obj.get_path_name()] = pilot.digest(pilot.inspector.disk(obj.get_path_name()))
    pilot.save_json(OUT / 'state.json', state)
    for path in sorted(paths):
        asset = unreal.load_asset(path)
        for _, notify in pilot.notify_objects(asset):
            sound = read_field(notify, 'sound')
            branch = by_cue.get(sound.get_path_name() if sound else '')
            if not branch:
                raise RuntimeError('Unrecognized/user-authored sound cannot be migrated automatically: ' + path)
            kind, socket = emission(branch, path)
            # Existing user-assigned named anchors take precedence; only fill the old None defaults.
            if str(read_field(notify, 'attach_socket')) not in ('None', ''):
                raise RuntimeError('Existing named audio anchor requires review before migration: ' + path)
            notify.set_editor_property('source', getattr(unreal.MHGZAnimationSoundSource, kind))
            notify.set_editor_property('attach_socket', socket)
        new = [p for p in additions if p['montage'].split('.')[0] == path]
        if new:
            if not unreal.AnimationLibrary.is_valid_anim_notify_track_name(asset, 'ActionAudio'):
                unreal.AnimationLibrary.add_animation_notify_track(asset, 'ActionAudio', unreal.LinearColor(.8, .6, .2, 1))
            for point in new:
                notify = unreal.AnimationLibrary.add_animation_notify_event(asset, 'ActionAudio',
                    point['montage_time_estimate_s'], unreal.AnimNotify_ActionSound)
                kind, socket = emission(point['branch_id'], path)
                notify.set_editor_property('source', getattr(unreal.MHGZAnimationSoundSource, kind))
                notify.set_editor_property('attach_socket', socket)
                notify.set_editor_property('sound', unreal.load_asset(pool[point['branch_id']]['cue']))
        snap = pilot.inspector.montage(path)
        coverage.verify_montage(state['before'][path], verify_preserved(
            state['before'][path], snap, [r['guid'] for r in old_points.get(path, [])]))
        old = {r['guid']: r for r in point_rows(asset, snap)}
        for row in old_points.get(path, []):
            for field in ('time', 'sound', 'volume', 'pitch'):
                if old[row['guid']][field] != row[field]:
                    raise RuntimeError('Original sound point author data changed: ' + field)
        pilot.save(asset)
        state['changed'][asset.get_path_name()] = pilot.digest(pilot.inspector.disk(path))
        state.setdefault('expected_points', {})[path] = point_rows(asset, snap)
        pilot.save_json(OUT / 'state.json', state)
    state['phase'] = 'saved_pending_independent_reload'
    pilot.save_json(OUT / 'state.json', state)
    audit(independent=False)


def audit(independent=True):
    state = load(OUT / 'state.json')
    for path, expected in state['changed'].items():
        if pilot.digest(pilot.inspector.disk(path)) != expected:
            raise RuntimeError('Saved package changed: ' + path)
    changed_disk = {pilot.inspector.disk(p).as_posix() for p in state['changed']}
    unchanged = 0
    for path, expected in state['original_hashes'].items():
        if path not in changed_disk:
            if pilot.digest(path) != expected:
                raise RuntimeError('Unrelated protected package changed: ' + path)
            unchanged += 1
    total = 0
    for path, before in state['before'].items():
        snap = pilot.inspector.montage(path)
        rows = point_rows(unreal.load_asset(path), snap)
        if rows != state['expected_points'][path]:
            raise RuntimeError('Reloaded sound point changed: ' + path)
        coverage.verify_montage(before, verify_preserved(before, snap, [r['guid'] for r in state['old_points'].get(path, [])]))
        total += len(rows)
    mix = {}
    for name in ('SA_IG_LieChong', 'SA_IG_DongZuo', 'SA_IG_JingHua'):
        obj = unreal.load_asset(pilot.AUDIO + '/Settings/' + name)
        value = read_field(obj, 'attenuation')
        row = dict(spatialize=bool(read_field(value, 'spatialize')),
                   lowpass=bool(read_field(value, 'attenuate_with_lpf')),
                   reverb=bool(read_field(value, 'enable_reverb_send')),
                   model=str(read_field(value, 'distance_algorithm')),
                   inner_cm=float(read_field(value, 'attenuation_shape_extents').x),
                   falloff_cm=float(read_field(value, 'falloff_distance')),
                   db_at_max=float(read_field(value, 'd_b_attenuation_at_max')))
        if not row['spatialize'] or row['lowpass'] or row['reverb'] or 'NATURAL_SOUND' not in row['model']:
            raise RuntimeError('Spatial mix policy differs: ' + name)
        mix[name] = row
    report = dict(phase='independently_reloaded' if independent else 'saved_pending_independent_reload', points=total, added=len(state['planned_additions']),
                  changed_packages=len(state['changed']), protected_unchanged=unchanged,
                  original_audio_ids_and_times_preserved=True, gameplay_and_animation_preserved=True,
                  pie_audible='pending_user_confirmation', spatial_mix=mix,
                  limits=['initial endpoint anchors are project configuration, not original socket truth',
                          'B022 overlapping same-frame hypotheses authored once, at the dominant observed time',
                          'sheathed dodge requires its own mapped source; not silently copying held-weapon timing'])
    pilot.save_json(OUT / 'audit.json', report)
    if independent:
        state['phase'] = 'independently_reloaded'
        pilot.save_json(OUT / 'state.json', state)
    print('AUDIO_SPATIAL ' + json.dumps(report))


def restrict_sheathe():
    """Remove only this transaction's five accidental equipment-layer additions."""
    state = load(OUT / 'state.json')
    path = '/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_ShouDao'
    asset = unreal.load_asset(path)
    current = pilot.inspector.montage(path)
    if point_rows(asset, current) != state['expected_points'][path]:
        raise RuntimeError('Sheathe author data changed after checkpoint; no correction applied')
    if state['old_points'].get(path):
        raise RuntimeError('Refuse to clear track with pre-existing sound points')
    allowed = independent_points(load(BASE / 'MappingFinal/mapping.json'))
    wanted = [p for p in allowed if p['montage'].split('.')[0] == path]
    if len(wanted) != 5:
        raise RuntimeError('Expected exactly five weapon-only sheathe points')
    if unreal.AnimationLibrary.remove_animation_notify_events_by_track(asset, 'ActionAudio') != 10:
        raise RuntimeError('Unexpected generated track count; not saving')
    pool = sources()
    for point in wanted:
        notify = unreal.AnimationLibrary.add_animation_notify_event(asset, 'ActionAudio',
            point['montage_time_estimate_s'], unreal.AnimNotify_ActionSound)
        kind, socket = emission(point['branch_id'], path)
        notify.set_editor_property('source', getattr(unreal.MHGZAnimationSoundSource, kind))
        notify.set_editor_property('attach_socket', socket)
        notify.set_editor_property('sound', unreal.load_asset(pool[point['branch_id']]['cue']))
    snapshot = pilot.inspector.montage(path)
    coverage.verify_montage(state['before'][path], verify_preserved(state['before'][path], snapshot, []))
    pilot.save(asset)
    state['changed'][asset.get_path_name()] = pilot.digest(pilot.inspector.disk(path))
    state['expected_points'][path] = point_rows(asset, snapshot)
    state['planned_additions'] = allowed
    state['correction'] = 'Removed five equipment-layer points introduced by this transaction, preserving original gameplay data'
    state['phase'] = 'saved_pending_independent_reload'
    pilot.save_json(OUT / 'state.json', state)
    audit(independent=False)


def inspect_external():
    path = '/Game/Characters/Demo/Anims/Montage/AM_Shth_Dodge'
    snap = pilot.inspector.montage(path)
    pilot.save_json(OUT / 'sheathed-dodge-source.json', dict(montage=snap, sequences=pilot.inspector.sequences))
    print('SHEATHED_DODGE_SOURCE ' + json.dumps(pilot.inspector.sequences))


def category_recipes(asset, snapshot):
    rows = point_rows(asset, snapshot)
    objects = {notify.get_name(): notify for _, notify in pilot.notify_objects(asset)}
    for row in rows:
        native = next(r for r in snapshot['existing_notifies'] if r['Guid'] == row['guid'])
        notify = next(n for name, n in objects.items() if re.search(
            r"'(?:[^']*:)?" + re.escape(name) + r"'", native.get('Notify', '')))
        row.update(event_tag=read_field(notify, 'event_tag').export_text(),
                   legacy_index=int(read_field(notify, 'legacy_attack_segment_index')),
                   ground_trace_distance=float(read_field(notify, 'ground_trace_distance')))
    return rows


def verify_category_events(before, after, targets):
    import copy
    normalized = copy.deepcopy(after)
    old = {r['Guid']: r for r in before['existing_notifies']}
    for row in normalized['existing_notifies']:
        if row['Guid'] in targets:
            # Only the embedded UObject class/reference may change, never the event envelope.
            row['Notify'] = old[row['Guid']]['Notify']
    coverage.verify_montage(before, normalized)


def categories(writing):
    previous_export_dir = pilot.inspector.OUT
    try:
        pilot.inspector.OUT = CATEGORY_OUT / 'NativeExports'
        return _categories(writing)
    finally:
        pilot.inspector.OUT = previous_export_dir


def _categories(writing):
    old_state = load(OUT / 'state.json')
    out = CATEGORY_OUT
    out.mkdir(parents=True, exist_ok=True)
    checkpoint_file = out / 'state.json'
    state = load(checkpoint_file) if checkpoint_file.exists() else None
    classes = {name: getattr(unreal, name) for name in (
        'AnimNotify_DustSound', 'AnimNotify_LandingSound', 'AnimNotify_ClothSound')}
    if writing and state:
        raise RuntimeError('Category transaction exists: audit first; do not replay Apply')
    if not state:
        state = dict(phase='read_only_plan', targets={}, before={}, recipes_before={}, expected={}, backups={}, changed={},
                     total_sound_points=0, scanned_montages=0)
        for path in old_state['expected_points']:
            asset = unreal.load_asset(path)
            snap = pilot.inspector.montage(path)
            rows = category_recipes(asset, snap)
            state['total_sound_points'] += len(rows)
            state['scanned_montages'] += 1
            target = {r['guid']: notify_category(r['sound']) for r in rows if notify_category(r['sound'])}
            if target:
                state['targets'][path] = target
                state['before'][path] = snap
                state['recipes_before'][path] = rows
        all_protected = set(old_state['original_hashes']) | {
            pilot.inspector.disk(path).as_posix() for path in old_state['changed']}
        state['protected'] = {path: pilot.digest(path) for path in all_protected}
        pilot.save_json(out / 'plan.json', state)
        if not writing:
            print('AUDIO_CATEGORIES_PLAN ' + json.dumps(dict(montages=len(state['targets']),
                points=sum(len(v) for v in state['targets'].values()), cloth_points=0)))
            return
        for path in state['targets']:
            disk = pilot.inspector.disk(path)
            backup = out / 'Backup' / disk.relative_to(ROOT)
            backup.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(disk, backup)
            state['backups'][path] = dict(path=str(backup), sha256=pilot.digest(disk))
        state['phase'] = 'intent'
        pilot.save_json(checkpoint_file, state)
        for path, targets in state['targets'].items():
            asset = unreal.load_asset(path)
            for guid, cls in targets.items():
                value = unreal.Guid()
                value.import_text(guid)
                if not unreal.MHGZAnimationSeamLibrary.replace_sound_notify_class(asset, value, classes[cls]):
                    raise RuntimeError('Cannot migrate exact sound event: ' + path + ' ' + guid)
            after = pilot.inspector.montage(path)
            verify_category_events(state['before'][path], after, targets)
            rows = category_recipes(asset, after)
            old_rows = {r['guid']: r for r in state['recipes_before'][path]}
            for row in rows:
                old = dict(old_rows[row['guid']])
                old['kind'] = targets.get(row['guid'], old['kind'])
                if row != old:
                    raise RuntimeError('A sound recipe changed during class-only migration: ' + row['guid'])
            pilot.save(asset)
            state['expected'][path] = rows
            state['changed'][path] = pilot.digest(pilot.inspector.disk(path))
            pilot.save_json(checkpoint_file, state)
        state['phase'] = 'saved_pending_independent_reload'
        pilot.save_json(checkpoint_file, state)
    if set(state['expected']) != set(state['targets']):
        raise RuntimeError('Incomplete category transaction: inspect checkpoint/backups before any retry')
    counts = {name: 0 for name in classes}
    for path, expected in state['expected'].items():
        if pilot.digest(pilot.inspector.disk(path)) != state['changed'][path]:
            raise RuntimeError('Category package edited after checkpoint: ' + path)
        snap = pilot.inspector.montage(path)
        rows = category_recipes(unreal.load_asset(path), snap)
        if rows != expected:
            raise RuntimeError('Reloaded category recipes differ: ' + path)
        verify_category_events(state['before'][path], snap, state['targets'][path])
        for row in rows:
            if row['kind'] in counts:
                counts[row['kind']] += 1
    changed_disk = {pilot.inspector.disk(p).as_posix() for p in state['changed']}
    unchanged = 0
    for path, expected in state['protected'].items():
        if path not in changed_disk:
            if pilot.digest(path) != expected:
                raise RuntimeError('Unrelated package changed: ' + path)
            unchanged += 1
    result = dict(phase='saved_pending_independent_reload' if writing else 'independently_reloaded',
                  migrated=sum(len(v) for v in state['targets'].values()), classes=counts,
                  total_sound_points=state['total_sound_points'], scanned_montages=state['scanned_montages'],
                  changed_montages=len(state['changed']), protected_unchanged=unchanged,
                  envelope_and_recipe_preserved=True, no_audio_media_or_mix_changed=True,
                  cloth_media_identified=False, pie_audible='not_tested')
    state['phase'] = result['phase']
    pilot.save_json(checkpoint_file, state)
    pilot.save_json(out / ('apply.json' if writing else 'audit.json'), result)
    print('AUDIO_CATEGORIES ' + json.dumps(result))


def cloth_placeholders(writing, output=CLOTH_OUT):
    """Keep proven armor timings as silent authoring points; never clear/rebuild a track."""
    if (not writing and output == ROOT / 'Saved/Audio/RiseIG/AerialPowder20261023/UE'
            and (ROOT / 'Saved/Audio/RiseIG/ClothTracks20261023/StableLanes/state.json').exists()):
        from audio_notify_tracks import run
        return run(False, cloth_only=True)
    previous_export_dir = pilot.inspector.OUT
    try:
        pilot.inspector.OUT = output / 'NativeExports'
        return _cloth_placeholders(writing, output)
    finally:
        pilot.inspector.OUT = previous_export_dir


def _cloth_placeholders(writing, output=CLOTH_OUT):
    import rise_audio_weapon_mapping as mapping
    output.mkdir(parents=True, exist_ok=True)
    comparison_file = output.parent / 'comparison.json'
    comparison = load(comparison_file)
    for path, expected in comparison['inputs'].items():
        if pilot.digest(path) != expected:
            raise RuntimeError('Cloth comparison input changed: ' + path)
    checkpoint = output / 'state.json'
    state = load(checkpoint) if checkpoint.exists() else None
    if writing and state:
        raise RuntimeError('Cloth authoring transaction exists; inspect/audit, never replay Apply')
    if not state:
        state = dict(phase='read_only_plan', before={}, recipes_before={}, targets={}, expected={},
                     changed={}, backups={}, new_points={}, comparison_sha256=pilot.digest(comparison_file))
        snapshots = [pilot.inspector.montage(p) for p in comparison['montage_candidates']]
        mapped = mapping.map_montages(dict(montages=snapshots, sequences=pilot.inspector.sequences), comparison['matched_windows'])
        by_path = {s['path']: s for s in snapshots}
        for montage in mapped:
            points = [p for p in montage['candidates'] if not p['cut_boundary_ambiguous']]
            keys = [(p['cluster_id'], p['slot'], p['segment_index'], p['loop']) for p in points]
            if len(keys) != len(set(keys)):
                raise RuntimeError('Duplicate cloth source identity; do not auto-collapse distinct author events')
            if points:
                path = montage['path']
                state['targets'][path] = points
                state['before'][path] = by_path[path]
                state['recipes_before'][path] = category_recipes(unreal.load_asset(path), by_path[path])
        previous = load(CATEGORY_OUT / 'state.json')
        state['protected'] = {p: pilot.digest(p) for p in previous['protected']}
        pilot.save_json(output / 'plan.json', state)
        if not writing:
            print('CLOTH_PLACEHOLDERS_PLAN ' + json.dumps(dict(montages=len(state['targets']),
                new_silent_points=sum(len(p) for p in state['targets'].values()), existing_points_removed=0)))
            return
        for path in state['targets']:
            disk = pilot.inspector.disk(path)
            backup = output / 'Backup' / disk.relative_to(ROOT)
            backup.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(disk, backup)
            state['backups'][path] = dict(path=str(backup), sha256=pilot.digest(disk))
        state['phase'] = 'intent'
        pilot.save_json(checkpoint, state)
        for path, points in state['targets'].items():
            asset = unreal.load_asset(path)
            if not unreal.AnimationLibrary.is_valid_anim_notify_track_name(asset, 'ClothAudio'):
                unreal.AnimationLibrary.add_animation_notify_track(asset, 'ClothAudio', unreal.LinearColor(.6, .5, .8, 1))
            authored = []
            for point in points:
                notify = unreal.AnimationLibrary.add_animation_notify_event(asset, 'ClothAudio',
                    point['montage_time_estimate_s'], unreal.AnimNotify_ClothSound)
                # No default Cue, GA override, guessed replacement sample or timer.
                notify.set_editor_property('sound', None)
                authored.append((notify.get_name(), point))
            # AddAnimationNotifyEvent refreshes UE's cache and may move earlier
            # overlapping points to another track. Restore every new event by
            # GUID only after all additions, using the existing non-sorting bridge.
            from spatial_audio_rules import plan_cloth_lanes
            from audio_notify_tracks import track_names
            created = pilot.inspector.montage(path)
            tracks = track_names(asset)
            new_ids = {r['Guid'] for r in created['existing_notifies'] if any(re.search(
                r"'(?:[^']*:)?" + re.escape(name) + r"'", r.get('Notify', '')) for name, _ in authored)}
            assignments, future = plan_cloth_lanes(created['existing_notifies'], new_ids, tracks)
            for identity, index in assignments.items():
                guid = unreal.Guid()
                guid.import_text(identity)
                if not unreal.MHGZAnimationSeamLibrary.move_sound_notify_to_track(asset, guid, future[index]):
                    raise RuntimeError('Could not retain new equipment event on ClothAudio')
            after = pilot.inspector.montage(path)
            coverage.verify_montage(state['before'][path], verify_preserved(state['before'][path], after, []))
            old_ids = {r['guid'] for r in state['recipes_before'][path]}
            rows = category_recipes(asset, after)
            if [r for r in rows if r['guid'] in old_ids] != state['recipes_before'][path]:
                raise RuntimeError('Existing audio authoring changed: ' + path)
            if len(rows) != len(old_ids) + len(points):
                raise RuntimeError('Unexpected audio event count')
            provenance = []
            for name, point in authored:
                native = next(r for r in after['existing_notifies'] if re.search(
                    r"'(?:[^']*:)?" + re.escape(name) + r"'", r.get('Notify', '')))
                provenance.append(dict(point, guid=native['Guid']))
            for row in rows:
                if row['guid'] not in old_ids and (row['sound'] is not None or row['kind'] != 'AnimNotify_ClothSound'):
                    raise RuntimeError('New cloth placeholder must be silent and categorized')
                if row['guid'] not in old_ids:
                    native = next(r for r in after['existing_notifies'] if r['Guid'] == row['guid'])
                    tracks = track_names(asset)
                    if not re.fullmatch(r'ClothAudio(?:_(?:[2-9]|[1-9]\d+))?', tracks[int(native.get('TrackIndex', 0))]):
                        raise RuntimeError('New equipment event escaped ClothAudio')
            pilot.save(asset)
            state['new_points'][path] = provenance
            state['expected'][path] = rows
            state['changed'][path] = pilot.digest(pilot.inspector.disk(path))
            pilot.save_json(checkpoint, state)
        state['phase'] = 'saved_pending_independent_reload'
        pilot.save_json(checkpoint, state)
    if set(state['expected']) != set(state['targets']):
        raise RuntimeError('Partial cloth transaction: inspect saved hashes/backups before retry')
    if pilot.digest(comparison_file) != state['comparison_sha256']:
        raise RuntimeError('Cloth evidence changed since authoring')
    for path, expected in state['expected'].items():
        if pilot.digest(pilot.inspector.disk(path)) != state['changed'][path]:
            raise RuntimeError('Cloth package changed after save: ' + path)
        snap = pilot.inspector.montage(path)
        rows = category_recipes(unreal.load_asset(path), snap)
        from spatial_audio_rules import same_audio_recipes
        if not same_audio_recipes(expected, rows):
            pilot.save_json(output / 'reload-diff.json', dict(path=path, expected=expected, actual=rows))
            raise RuntimeError('Reloaded cloth/audio recipes differ: ' + path)
        coverage.verify_montage(state['before'][path], verify_preserved(state['before'][path], snap, []))
    changed_disk = {pilot.inspector.disk(p).as_posix() for p in state['changed']}
    # The explicitly subsequent powder transaction changes this one DA, not the
    # aerial Montage authoring. Accept only its frozen, independently reloaded
    # successor hash; never bless arbitrary current config edits.
    powder_successor = ROOT / 'Saved/Audio/RiseIG/AerialPowder20261023/Powder/UE/state.json'
    successor_config_hash = None
    accepted_successor_config = False
    if output == ROOT / 'Saved/Audio/RiseIG/AerialPowder20261023/UE' and powder_successor.exists():
        successor = load(powder_successor)
        if successor.get('phase') == 'independently_reloaded':
            successor_config_hash = successor['config_hash']
    for path, expected in state['protected'].items():
        if successor_config_hash and Path(path) == pilot.inspector.disk('/Game/Weapons/InsectGlaive/Data/DA_IG_Combat'):
            accepted_successor_config = expected != successor_config_hash
            expected = successor_config_hash
        if path not in changed_disk and pilot.digest(path) != expected:
            raise RuntimeError('Unrelated protected package changed: ' + path)
    result = dict(phase='saved_pending_independent_reload' if writing else 'independently_reloaded',
                  montages=len(state['changed']), silent_cloth_points=sum(len(p) for p in state['new_points'].values()),
                  original_audio_and_gameplay_preserved=True,
                  protected_unchanged=len(state['protected'])-len(changed_disk)-int(accepted_successor_config),
                  accepted_successor_config=accepted_successor_config,
                  source_audio_linked=False, audio_media_deleted=False, pie_tested=False)
    state['phase'] = result['phase']
    pilot.save_json(checkpoint, state)
    pilot.save_json(output / ('apply.json' if writing else 'audit.json'), result)
    print('CLOTH_PLACEHOLDERS ' + json.dumps(result))


def main():
    args = sys.argv + unreal.SystemLibrary.get_command_line().split()
    if '--organize-tracks' in args or '-MHGZAudioOrganizeTracks' in args:
        from audio_notify_tracks import run
        run('--apply' in args or '-MHGZCurrentWeaponAudioApply' in args)
    elif '--cloth-placeholders' in args or '-MHGZAudioClothPlaceholders' in args:
        cloth_placeholders('--apply' in args or '-MHGZCurrentWeaponAudioApply' in args)
    elif '--aerial-cloth-placeholders' in args or '-MHGZAudioAerialCloth' in args:
        cloth_placeholders('--apply' in args or '-MHGZCurrentWeaponAudioApply' in args,
            ROOT / 'Saved/Audio/RiseIG/AerialPowder20261023/UE')
    elif '--classify-notifies' in args or '-MHGZAudioNotifyCategories' in args:
        categories('--apply' in args or '-MHGZCurrentWeaponAudioApply' in args)
    elif '-MHGZAudioInspectExternal' in args:
        inspect_external()
        if (ROOT / 'Saved/Audio/RiseIG/NotifyTracks20261020/state.json').exists():
            from audio_notify_tracks import run
            run(False)
        elif (CLOTH_OUT / 'state.json').exists():
            cloth_placeholders(False)
        elif (CATEGORY_OUT / 'state.json').exists():
            categories(False)
        elif (OUT / 'state.json').exists():
            audit()
    elif '-MHGZAudioRestrictSheathe' in args:
        restrict_sheathe()
    elif '--apply' in args or '-MHGZCurrentWeaponAudioApply' in args:
        apply()
    elif (ROOT / 'Saved/Audio/RiseIG/ClothTracks20261023/StableLanes/state.json').exists():
        from audio_notify_tracks import run
        run(False, cloth_only=True)
    elif (ROOT / 'Saved/Audio/RiseIG/AerialPowder20261023/UE/state.json').exists():
        cloth_placeholders(False, ROOT / 'Saved/Audio/RiseIG/AerialPowder20261023/UE')
    elif (ROOT / 'Saved/Audio/RiseIG/NotifyTracks20261020/state.json').exists():
        from audio_notify_tracks import run
        run(False)
    elif (CLOTH_OUT / 'state.json').exists():
        cloth_placeholders(False)
    elif (CATEGORY_OUT / 'state.json').exists():
        categories(False)
    elif (OUT / 'state.json').exists():
        audit()
    else:
        preflight()
