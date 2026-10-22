"""Track-only successor transaction; called by the resident audio maintenance entry.

Reuses snapshot, recipe, backup and exact-event validation helpers. No event
reconstruction/deletion, track deletion, audio changes or runtime code changes.
"""
from collections import Counter
import json
import shutil
import unreal

import audio_spatial_maintenance as maintenance
from spatial_audio_rules import SOUND_TRACKS, verify_sound_tracks

pilot = maintenance.pilot
OUT = maintenance.ROOT / 'Saved/Audio/RiseIG/NotifyTracks20261020'


def track_names(asset):
    return [str(n) for n in unreal.AnimationLibrary.get_animation_notify_track_names(asset)]


def validate_package(path, before, before_tracks, recipes, assignments):
    asset = unreal.load_asset(path)
    after = pilot.inspector.montage(path)
    maintenance.coverage.verify_montage(before, verify_sound_tracks(before, after, assignments))
    if maintenance.category_recipes(asset, after) != recipes:
        raise RuntimeError('Track-only edit changed an audio recipe: ' + path)
    tracks = track_names(asset)
    if tracks[:len(before_tracks)] != before_tracks or len(tracks) != len(set(tracks)):
        raise RuntimeError('Original tracks changed or duplicate tracks created')
    by_guid = {r['Guid']: r for r in after['existing_notifies']}
    for row in recipes:
        index = int(by_guid[row['guid']].get('TrackIndex', 0))
        if tracks[index] != SOUND_TRACKS[row['kind']]:
            raise RuntimeError('Sound event does not occupy its type track')
    return tracks


def run(writing=False):
    previous = pilot.inspector.OUT
    try:
        pilot.inspector.OUT = OUT / 'NativeExports'
        _run(writing)
    finally:
        pilot.inspector.OUT = previous


def _run(writing):
    checkpoint = OUT / 'state.json'
    state = maintenance.load(checkpoint) if checkpoint.exists() else None
    if writing and state:
        raise RuntimeError('Track transaction exists: audit first, never replay Apply')
    if not state:
        baseline = maintenance.load(maintenance.OUT / 'state.json')
        state = dict(phase='read_only_plan', before={}, tracks_before={}, recipes={}, assignments={},
                     expected_tracks={}, backups={}, changed={}, moved=0, counts={})
        counts = Counter()
        for path in baseline['expected_points']:
            asset = unreal.load_asset(path)
            snapshot = pilot.inspector.montage(path)
            rows = maintenance.category_recipes(asset, snapshot)
            tracks = track_names(asset)
            future = list(tracks)
            assignments = {}
            native = {r['Guid']: r for r in snapshot['existing_notifies']}
            for row in rows:
                name = SOUND_TRACKS[row['kind']] # Unknown types require a deliberate category decision.
                counts[name] += 1
                if name not in future:
                    future.append(name)
                target = future.index(name)
                assignments[row['guid']] = target
                state['moved'] += int(native[row['guid']].get('TrackIndex', 0)) != target
            if future != tracks or any(int(native[g].get('TrackIndex', 0)) != i for g, i in assignments.items()):
                state['before'][path] = snapshot
                state['tracks_before'][path] = tracks
                state['recipes'][path] = rows
                state['assignments'][path] = assignments
                state['expected_tracks'][path] = future
        state['counts'] = dict(counts)
        # Capture the current author state, not an obsolete pre-cloth hash baseline.
        protected = maintenance.load(maintenance.CLOTH_OUT / 'state.json')['protected']
        state['protected'] = {p: pilot.digest(p) for p in protected}
        pilot.save_json(OUT / 'plan.json', state)
        if not writing:
            print('AUDIO_TRACKS_PLAN ' + json.dumps(dict(montages=len(state['before']),
                moved=state['moved'], counts=state['counts'])))
            return
        for path in state['before']:
            disk = pilot.inspector.disk(path)
            backup = OUT / 'Backup' / disk.relative_to(maintenance.ROOT)
            backup.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(disk, backup)
            state['backups'][path] = dict(path=str(backup), sha256=pilot.digest(disk))
        state['phase'] = 'intent'
        pilot.save_json(checkpoint, state)
        for path, assignments in state['assignments'].items():
            asset = unreal.load_asset(path)
            recipes = {r['guid']: r for r in state['recipes'][path]}
            for guid in assignments:
                value = unreal.Guid()
                value.import_text(guid)
                if not unreal.MHGZAnimationSeamLibrary.move_sound_notify_to_track(asset, value, SOUND_TRACKS[recipes[guid]['kind']]):
                    raise RuntimeError('Exact sound track move failed: ' + path + ' ' + guid)
            actual = validate_package(path, state['before'][path], state['tracks_before'][path], state['recipes'][path], assignments)
            if actual != state['expected_tracks'][path]:
                raise RuntimeError('Unexpected category tracks')
            pilot.save(asset)
            state['changed'][path] = pilot.digest(pilot.inspector.disk(path))
            pilot.save_json(checkpoint, state)
        state['phase'] = 'saved_pending_independent_reload'
        pilot.save_json(checkpoint, state)
    if set(state['changed']) != set(state['before']):
        raise RuntimeError('Partial track transaction: inspect saved hashes/backups before retry')
    for path, expected in state['changed'].items():
        if pilot.digest(pilot.inspector.disk(path)) != expected:
            raise RuntimeError('Track package edited since save: ' + path)
        tracks = validate_package(path, state['before'][path], state['tracks_before'][path], state['recipes'][path], state['assignments'][path])
        if tracks != state['expected_tracks'][path]:
            raise RuntimeError('Reloaded tracks differ')
    changed_disk = {pilot.inspector.disk(p).as_posix() for p in state['changed']}
    for path, expected in state['protected'].items():
        if path not in changed_disk and pilot.digest(path) != expected:
            raise RuntimeError('Unrelated protected asset changed: ' + path)
    result = dict(phase='saved_pending_independent_reload' if writing else 'independently_reloaded',
                  changed_montages=len(state['changed']), relocated_points=state['moved'], category_counts=state['counts'],
                  protected_unchanged=len(state['protected'])-len(changed_disk),
                  audio_and_gameplay_preserved=True, cloth_sound_still_unset=True,
                  no_notifies_or_tracks_deleted=True, pie_tested=False)
    state['phase'] = result['phase']
    pilot.save_json(checkpoint, state)
    pilot.save_json(OUT / ('apply.json' if writing else 'audit.json'), result)
    print('AUDIO_TRACKS ' + json.dumps(result))
