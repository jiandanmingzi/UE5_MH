"""Audio M2 TuCi only. Default audit; --apply / -MHGZSwingPilotApply writes.

Reuses native UE asset snapshot helpers and Epic SoundCueFactoryNew's initial
Wave graph construction. No UI automation or new editor bridge. Backups and
manifest are saved before mutation. Re-execution audits owned assets, never
reimports existing Waves or duplicates point Notifies. No hit Cue in this step.
Commandlet audio work requires -AllowCommandletAudio, without -nosound, so
Epic's AudioDeviceManager registers the Wave decoders before import/cache.
"""
from pathlib import Path
import hashlib
import json
import re
import shutil
import sys
import unreal

ROOT = Path(unreal.Paths.project_dir()).resolve()
OUT = ROOT / 'Saved/Audio/RiseIG/M2/TuCi'
OUT.mkdir(parents=True, exist_ok=True)
sys.path.insert(0, str(ROOT / 'Scripts'))
sys.path.insert(0, str(ROOT / 'Scripts/Audio'))
from ue_asset_utils import as_sequence, blueprint_cdo, read_field
import audit_swing_assets as inspector
inspector.OUT = OUT / 'NativeExports'

MON = '/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_TuCi'
GA = '/Game/Blueprints/Ability/InsectGlaive/GA_IG_TuCi'
AUDIO = '/Game/Weapons/InsectGlaive/Audio'
ATTENUATION = AUDIO + '/Settings/SA_IG_HuiJi'
CONCURRENCY = AUDIO + '/Settings/SCN_IG_HuiJi'
TRACK = 'SwingAudio'
OWNER = 'mhgz_audio_m2_tuci_v1'
MANIFEST = OUT / 'manifest.json'


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def save_json(path, value):
    Path(path).write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding='utf-8')


def tag(name):
    value = unreal.GameplayTag()
    value.import_text('(TagName="' + name + '")')
    if name not in value.export_text():
        raise RuntimeError('Could not construct registered tag ' + name)
    return value


def save(asset):
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError('Could not save ' + asset.get_path_name())


def own(asset, owner=None):
    unreal.EditorAssetLibrary.set_metadata_tag(asset, 'MHGZ.Audio.Owner', owner or OWNER)
    save(asset)


def existing(path, cls, owner=None):
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        return None
    asset = unreal.load_asset(path)
    if not isinstance(asset, cls) or unreal.EditorAssetLibrary.get_metadata_tag(asset, 'MHGZ.Audio.Owner') != (owner or OWNER):
        raise RuntimeError('Refuse existing non-owned/wrong-class asset: ' + path)
    return asset


def create(path, cls, factory, owner=None):
    result = existing(path, cls, owner)
    if result:
        return result, False
    directory, name = path.rsplit('/', 1)
    result = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, directory, cls, factory)
    if result is None:
        raise RuntimeError('Creation failed ' + path)
    return result, True


def recipes():
    source = ROOT / 'docs/reference/虫棍挥击音效位置配置.json'
    plan = json.loads(source.read_text(encoding='utf-8'))
    mon = next(m for m in plan['montages'] if m['path'] == MON + '.AM_IG_TuCi')
    auditions = json.loads((ROOT / 'Saved/Audio/RiseIG/CaptureMap/mhrise_20261018_151536_02/audition.json').read_text(encoding='utf-8'))
    result = []
    expected = [2702726931, 2231624382]
    if [c['event_id'] for c in mon['candidates']] != expected:
        raise RuntimeError('TuCi candidate contract changed; review before writing')
    for index, candidate in enumerate(mon['candidates']):
        preview = next(r for r in auditions['previews'] if r['event_id'] == candidate['event_id'])
        if (candidate['cut_boundary_ambiguous'] or preview['channels'] != 1
                or preview['semantic_review']['status'] != 'user_confirmed'
                or digest(preview['wav']) != preview['sha256']):
            raise RuntimeError('Unverified source/cut/mono sound for pilot')
        suffix = str(candidate['event_id'])
        result.append({'event_id': candidate['event_id'], 'cluster_id': candidate['cluster_id'],
                       'time_s': candidate['montage_time_estimate_s'],
                       'event_tag': 'Audio.Swing.IG.TuCi.' + ('First' if index == 0 else 'Second'),
                       'wav': preview['wav'], 'wav_sha256': preview['sha256'],
                       'seconds': preview['seconds'], 'sample_rate': preview['sample_rate'],
                       'wave': AUDIO + '/Waves/SW_IG_HuiJi_' + suffix,
                       'cue': AUDIO + '/Cues/SC_IG_HuiJi_' + suffix})
    return result, plan


def notify_objects(montage):
    # The array itself is protected, the library-returned event fields are public.
    result = []
    for event in unreal.AnimationLibrary.get_animation_notify_events(montage):
        notify = read_field(event, 'notify')
        if isinstance(notify, unreal.AnimNotify_SwingSound):
            result.append((event, notify))
    return result


def verify_originals(before, after):
    for field in ('length_s', 'rate_scale', 'segments', 'sections'):
        if before[field] != after[field]:
            raise RuntimeError('Protected Montage structure changed: ' + field)
    guids = {row['Guid'] for row in before['existing_notifies']}
    retained = [row for row in after['existing_notifies'] if row['Guid'] in guids]
    if retained != before['existing_notifies']:
        raise RuntimeError('Original Notify GUID/order/track/time/payload changed')


def non_audio_cdo(text):
    return [line for line in text.splitlines()
            if not re.match(r'^\s*(?:AudioIdentityTag|bUseAuthoredSwingAudio)=', line)]


def audit(montage, bp, recipe, before=None):
    snapshot = inspector.montage(MON)
    if before:
        verify_originals(before, snapshot)
    authored = []
    for event, notify in notify_objects(montage):
        native = next(row for row in snapshot['existing_notifies']
                      if re.search(r"'(?:[^']*:)?" + re.escape(notify.get_name()) + r"'", row.get('Notify', '')))
        authored.append({'event_tag': read_field(notify, 'event_tag').export_text(),
                         'time_s': float(native.get('LinkValue', 0)), 'guid': native['Guid'],
                         'track_index': int(native.get('TrackIndex', 0)),
                         'sound': read_field(notify, 'sound').get_path_name() if read_field(notify, 'sound') else None,
                         'legacy_index': int(read_field(notify, 'legacy_attack_segment_index'))})
    cdo = blueprint_cdo(bp, unreal)
    linked = len(authored) == len(recipe)
    for wanted in recipe:
        hits = [n for n in authored if wanted['event_tag'] in n['event_tag']]
        linked = linked and len(hits) == 1 and abs(hits[0]['time_s'] - wanted['time_s']) < 1e-5 and hits[0]['sound'] == wanted['cue'] + '.' + wanted['cue'].rsplit('/', 1)[-1] and hits[0]['legacy_index'] == -1
    assets = []
    for wanted in recipe:
        wave = existing(wanted['wave'], unreal.SoundWave)
        cue = existing(wanted['cue'], unreal.SoundCue)
        item = {'wave': wanted['wave'], 'cue': wanted['cue'], 'wave_exists': wave is not None, 'cue_exists': cue is not None}
        if wave:
            item.update(duration=float(read_field(wave, 'duration')), channels=int(read_field(wave, 'num_channels')),
                        looping=bool(read_field(wave, 'looping')))
            if abs(item['duration'] - wanted['seconds']) > 0.002 or item['channels'] != 1 or item['looping']:
                raise RuntimeError('Imported Wave disagrees with source WAV')
        if cue:
            native = inspector.text_export(cue)
            if ('FirstNode=' not in native or 'SoundNodeWavePlayer' not in native
                    or wanted['wave'] not in native or 'bLooping=True' in native):
                raise RuntimeError('Cue is empty, looping or references the wrong Wave')
            attenuation = read_field(cue, 'attenuation_settings')
            concurrency = as_sequence(read_field(cue, 'concurrency_set'))
            if (attenuation is None or attenuation.get_path_name().split('.')[0] != ATTENUATION
                    or len(concurrency) != 1 or concurrency[0].get_path_name().split('.')[0] != CONCURRENCY
                    or abs(float(read_field(cue, 'volume_multiplier')) - 1.0) > 1e-5):
                raise RuntimeError('Cue settings are not connected')
        assets.append(item)
    return {'montage': snapshot, 'authored': authored, 'assets': assets,
            'ga_enabled': bool(read_field(cdo, 'use_authored_swing_audio')),
            'ga_audio_identity': read_field(cdo, 'audio_identity_tag').export_text(),
            'linked': bool(linked and all(a['wave_exists'] and a['cue_exists'] for a in assets)
                           and read_field(cdo, 'use_authored_swing_audio')
                           and 'Audio.Swing.IG.TuCi' in read_field(cdo, 'audio_identity_tag').export_text())}


def apply(recipe, montage, bp):
    attenuation, fresh = create(ATTENUATION, unreal.SoundAttenuation, unreal.SoundAttenuationFactory())
    if fresh:
        settings = read_field(attenuation, 'attenuation')
        settings.set_editor_property('attenuate', True)
        settings.set_editor_property('spatialize', True)
        settings.set_editor_property('attenuation_shape_extents', unreal.Vector(500, 0, 0))
        settings.set_editor_property('falloff_distance', 2500.0)
        attenuation.set_editor_property('attenuation', settings)
        own(attenuation)
    concurrency, fresh = create(CONCURRENCY, unreal.SoundConcurrency, unreal.SoundConcurrencyFactory())
    if fresh:
        settings = read_field(concurrency, 'concurrency')
        settings.set_editor_property('max_count', 8)
        settings.set_editor_property('limit_to_owner', True)
        settings.set_editor_property('resolution_rule', unreal.MaxConcurrentResolutionRule.STOP_OLDEST)
        concurrency.set_editor_property('concurrency', settings)
        own(concurrency)
    for wanted in recipe:
        wave = existing(wanted['wave'], unreal.SoundWave)
        if wave is None:
            directory, name = wanted['wave'].rsplit('/', 1)
            task = unreal.AssetImportTask()
            task.filename = wanted['wav']
            task.destination_path, task.destination_name = directory, name
            task.automated, task.save, task.replace_existing = True, False, False
            task.factory = unreal.SoundFactory()
            unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
            wave = unreal.load_asset(wanted['wave'])
            if not isinstance(wave, unreal.SoundWave):
                raise RuntimeError('Wave import failed')
            wave.set_editor_property('looping', False)
            unreal.EditorAssetLibrary.set_metadata_tag(wave, 'MHGZ.Audio.SourceSHA256', wanted['wav_sha256'])
            own(wave)
        elif unreal.EditorAssetLibrary.get_metadata_tag(wave, 'MHGZ.Audio.SourceSHA256') != wanted['wav_sha256']:
            raise RuntimeError('Refuse changed source for owned Wave')
        factory = unreal.SoundCueFactoryNew()
        factory.set_editor_property('initial_sound_waves', [wave])
        cue, fresh = create(wanted['cue'], unreal.SoundCue, factory)
        if fresh:
            cue.set_editor_property('volume_multiplier', 1.0)
            cue.set_editor_property('attenuation_settings', attenuation)
            cue.set_editor_property('concurrency_set', [concurrency])
            own(cue)
    authored = notify_objects(montage)
    if not unreal.AnimationLibrary.is_valid_anim_notify_track_name(montage, TRACK):
        unreal.AnimationLibrary.add_animation_notify_track(montage, TRACK, unreal.LinearColor(0.2, 0.65, 1, 1))
    for wanted in recipe:
        hits = [n for _, n in authored if wanted['event_tag'] in read_field(n, 'event_tag').export_text()]
        if hits:
            if len(hits) != 1:
                raise RuntimeError('Duplicate authored sound; audit instead of overwriting')
            continue
        notify = unreal.AnimationLibrary.add_animation_notify_event(montage, TRACK, wanted['time_s'], unreal.AnimNotify_SwingSound)
        notify.set_editor_property('event_tag', tag(wanted['event_tag']))
        notify.set_editor_property('sound', unreal.load_asset(wanted['cue']))
    cdo = blueprint_cdo(bp, unreal)
    cdo.set_editor_property('use_authored_swing_audio', True)
    cdo.set_editor_property('audio_identity_tag', tag('Audio.Swing.IG.TuCi'))


def main():
    writing = '--apply' in sys.argv or '-MHGZSwingPilotApply' in unreal.SystemLibrary.get_command_line()
    recipe, plan = recipes()
    montage, bp = unreal.load_asset(MON), unreal.load_asset(GA)
    if not montage or not bp:
        raise RuntimeError('Pilot Montage/GA missing')
    state = json.loads(MANIFEST.read_text(encoding='utf-8')) if MANIFEST.exists() else None
    before = state['before'] if state else inspector.montage(MON)
    current = audit(montage, bp, recipe, before if state else None)
    # Exercise tag conversion before creating/saving any Content.
    for value in ('Audio.Swing.IG.TuCi', 'Audio.Swing.IG.TuCi.First', 'Audio.Swing.IG.TuCi.Second'):
        tag(value)
    if state and 'before_cdo' not in state:
        # An earlier attempt stopped before saving GA/Montage. Capture its still
        # pristine CDO rather than treating an unknown current CDO as baseline.
        original = state['original_packages']['Content/Blueprints/Ability/InsectGlaive/GA_IG_TuCi.uasset']
        if digest(inspector.disk(GA)) != original['sha256']:
            raise RuntimeError('Missing CDO baseline and GA no longer pristine')
        state['before_cdo'] = inspector.text_export(blueprint_cdo(bp, unreal))
        save_json(MANIFEST, state)
    if writing and not current['linked']:
        if not state:
            # Freeze/backup both existing packages, refuse stale candidate inputs.
            originals = {}
            for path in (MON, GA):
                filename = inspector.disk(path)
                relative = filename.relative_to(ROOT).as_posix()
                if digest(filename) != plan['asset_package_hashes'][relative]:
                    raise RuntimeError('Candidate asset changed: ' + relative)
                backup = OUT / 'Backup' / relative
                backup.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(filename, backup)
                originals[relative] = {'sha256': digest(filename), 'backup': str(backup)}
            state = {'schema': OWNER, 'phase': 'intent', 'before': before, 'original_packages': originals,
                     'before_cdo': inspector.text_export(blueprint_cdo(bp, unreal)),
                     'recipes': recipe, 'protected_packages': {p: h for p, h in plan['asset_package_hashes'].items()
                                                              if p not in originals}}
            save_json(MANIFEST, state)
        apply(recipe, montage, bp)
        current = audit(montage, bp, recipe, before)
        if not current['linked']:
            raise RuntimeError('In-memory postconditions failed; Montage/GA not saved')
        if non_audio_cdo(inspector.text_export(blueprint_cdo(bp, unreal))) != non_audio_cdo(state['before_cdo']):
            raise RuntimeError('GA non-audio fields changed; Montage/GA not saved')
        save(montage)
        save(bp)
        state['phase'] = 'saved_pending_independent_reload'
        save_json(MANIFEST, state)
    if state:
        if non_audio_cdo(inspector.text_export(blueprint_cdo(bp, unreal))) != non_audio_cdo(state['before_cdo']):
            raise RuntimeError('GA fields changed outside audio opt-in/identity')
        for relative, expected in state['protected_packages'].items():
            if digest(ROOT / relative) != expected:
                raise RuntimeError('Unrelated package changed: ' + relative)
        if not writing and current['linked']:
            state['phase'] = 'independently_reloaded'
            save_json(MANIFEST, state)
    report = {'schema': OWNER, 'mode': 'apply' if writing else 'audit', 'success': True,
              'linked': current['linked'], 'recipes': recipe, 'audit': current,
              'original_notify_structure_preserved': bool(state), 'pie_audible': 'not_tested'}
    save_json(OUT / ('apply.json' if writing else 'audit.json'), report)
    print('SWING_PILOT ' + json.dumps({k: report[k] for k in ('mode', 'success', 'linked', 'pie_audible')}))


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        save_json(OUT / 'failure.json', {'error': repr(error), 'note': 'Inspect current postconditions before retry; no automatic rollback.'})
        raise
