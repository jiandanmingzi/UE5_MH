"""Weapon contact M2: default read-only audit, --apply / -MHGZHitAudioApply writes.

Creates five owned audio/Cue packages only. Reuses pilot asset/import helpers;
never edits GAs, Montages, hit windows or existing sounds. With Commandlet use
-AllowCommandletAudio, NOT -nosound. A fresh-process audit confirms persistence.
"""
from pathlib import Path
import json
import sys
import unreal

ROOT = Path(unreal.Paths.project_dir()).resolve()
sys.path.insert(0, str(ROOT / 'Scripts/Audio'))
import setup_swing_pilot as assets
from ue_asset_utils import blueprint_cdo, read_field

OWNER = 'mhgz_audio_m2_weapon_hit_v1'
OUT = ROOT / 'Saved/Audio/RiseIG/M2/WeaponHit'
OUT.mkdir(parents=True, exist_ok=True)
assets.inspector.OUT = OUT / 'NativeExports'
BASE = '/Game/Weapons/InsectGlaive/Audio'
WAVE = BASE + '/Waves/SW_IG_MingZhong_1932771116'
CUE = BASE + '/Cues/SC_IG_MingZhong'
ATT = BASE + '/Settings/SA_IG_MingZhong'
CON = BASE + '/Settings/SCN_IG_MingZhong'
GC = '/Game/GameplayCues/GCN_Hit_WeaponSound'
TAG = 'GameplayCue.Hit.WeaponSound'
EXPECTED_SHA = 'fa7f4d72d0986846edd31d51974ca3467d5244ca69ff3aad4563764866972969'


def source():
    audition = ROOT / 'Saved/Audio/RiseIG/CaptureMap/mhrise_20261018_151536_02/audition.json'
    row = next(r for r in json.loads(audition.read_text(encoding='utf-8'))['previews']
               if r['event_id'] == 1932771116)
    if (row['sha256'] != EXPECTED_SHA or assets.digest(row['wav']) != EXPECTED_SHA
            or row['semantic_review']['status'] != 'user_confirmed'
            or row['semantic_review']['reviewed_wav_sha256'] != EXPECTED_SHA
            or row['channels'] != 1 or row['sample_rate'] != 48000):
        raise RuntimeError('Unconfirmed or changed hit source; no import permitted')
    return row


def existing(path, cls):
    return assets.existing(path, cls, OWNER)


def create(path, cls, factory):
    return assets.create(path, cls, factory, OWNER)


def own(asset):
    assets.own(asset, OWNER)


def protected_hashes():
    # Cheap package-hash checks, not asset loading/full validation.
    paths = set()
    for directory in ('Content/Blueprints/Ability/InsectGlaive',
                      'Content/Weapons/InsectGlaive/Anims/Montage',
                      'Content/GameplayCues', 'Content/Weapons/InsectGlaive/Audio'):
        paths.update((ROOT / directory).rglob('*.uasset'))
    ours = {assets.inspector.disk(p) for p in (WAVE, CUE, ATT, CON, GC)}
    return {p.relative_to(ROOT).as_posix(): assets.digest(p) for p in sorted(paths - ours)}


def apply(row):
    att, fresh = create(ATT, unreal.SoundAttenuation, unreal.SoundAttenuationFactory())
    if fresh:
        settings = read_field(att, 'attenuation')
        settings.set_editor_property('attenuate', True)
        settings.set_editor_property('spatialize', True)
        settings.set_editor_property('attenuation_shape_extents', unreal.Vector(500, 0, 0))
        settings.set_editor_property('falloff_distance', 2500.0)
        att.set_editor_property('attenuation', settings)
        own(att)
    con, fresh = create(CON, unreal.SoundConcurrency, unreal.SoundConcurrencyFactory())
    if fresh:
        settings = read_field(con, 'concurrency')
        settings.set_editor_property('max_count', 16)
        settings.set_editor_property('limit_to_owner', False)
        settings.set_editor_property('resolution_rule', unreal.MaxConcurrentResolutionRule.STOP_OLDEST)
        con.set_editor_property('concurrency', settings)
        own(con)
    wave = existing(WAVE, unreal.SoundWave)
    if wave is None:
        task = unreal.AssetImportTask()
        task.filename = row['wav']
        task.destination_path, task.destination_name = WAVE.rsplit('/', 1)
        task.automated, task.save, task.replace_existing = True, False, False
        task.factory = unreal.SoundFactory()
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        wave = unreal.load_asset(WAVE)
        if not isinstance(wave, unreal.SoundWave):
            raise RuntimeError('Hit Wave import failed')
        wave.set_editor_property('looping', False)
        unreal.EditorAssetLibrary.set_metadata_tag(wave, 'MHGZ.Audio.SourceSHA256', EXPECTED_SHA)
        own(wave)
    if unreal.EditorAssetLibrary.get_metadata_tag(wave, 'MHGZ.Audio.SourceSHA256') != EXPECTED_SHA:
        raise RuntimeError('Owned Wave no longer matches confirmed source')
    factory = unreal.SoundCueFactoryNew()
    factory.set_editor_property('initial_sound_waves', [wave])
    cue, fresh = create(CUE, unreal.SoundCue, factory)
    if fresh:
        cue.set_editor_property('volume_multiplier', 1.0)
        cue.set_editor_property('attenuation_settings', att)
        cue.set_editor_property('concurrency_set', [con])
        own(cue)
    factory = unreal.BlueprintFactory()
    factory.set_editor_property('parent_class', unreal.MHGZWeaponHitSoundCue)
    gc, fresh = create(GC, unreal.Blueprint, factory)
    if fresh:
        unreal.BlueprintEditorLibrary.compile_blueprint(gc)
        cdo = blueprint_cdo(gc, unreal)
        cdo.set_editor_property('gameplay_cue_tag', assets.tag(TAG))
        # GameplayCueName is NOT Python-exposed in UE5.6. Native Serialize and
        # PostEditChangeProperty derive it from the GCN_Hit_WeaponSound name.
        cdo.set_editor_property('hit_sound', cue)
        cdo.set_editor_property('volume_multiplier', 1.0)
        cdo.set_editor_property('pitch_multiplier', 1.0)
        own(gc)


def audit(row):
    wave = existing(WAVE, unreal.SoundWave)
    cue = existing(CUE, unreal.SoundCue)
    att = existing(ATT, unreal.SoundAttenuation)
    con = existing(CON, unreal.SoundConcurrency)
    gc = existing(GC, unreal.Blueprint)
    if not all((wave, cue, att, con, gc)):
        return {'linked': False, 'missing': [p for p, a in zip((WAVE, CUE, ATT, CON, GC), (wave, cue, att, con, gc)) if not a]}
    cdo = blueprint_cdo(gc, unreal)
    assert isinstance(cdo, unreal.MHGZWeaponHitSoundCue), 'Wrong GameplayCue parent'
    assert read_field(cdo, 'hit_sound') == cue, 'Unwired hit sound'
    assert TAG in read_field(cdo, 'gameplay_cue_tag').export_text(), 'Wrong Cue tag'
    assert read_field(cue, 'attenuation_settings') == att, 'Unwired attenuation'
    assert list(read_field(cue, 'concurrency_set')) == [con], 'Unwired concurrency'
    assert unreal.EditorAssetLibrary.get_metadata_tag(wave, 'MHGZ.Audio.SourceSHA256') == EXPECTED_SHA
    assert int(read_field(wave, 'num_channels')) == 1 and not read_field(wave, 'looping')
    assert abs(float(read_field(wave, 'duration')) - row['seconds']) < .002
    native = assets.inspector.text_export(cue)
    assert 'SoundNodeWavePlayer' in native and WAVE in native, 'Cue has no expected WavePlayer'
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.scan_paths_synchronous(['/Game/GameplayCues'], force_rescan=True)
    handlers = [str(a.package_name) for a in registry.get_assets_by_path('/Game/GameplayCues', recursive=True)
                if str(a.get_tag_value('GameplayCueName')) == TAG]
    assert handlers == [GC], 'Cue registration missing/duplicate: ' + repr(handlers)
    settings = read_field(con, 'concurrency')
    attenuation = read_field(att, 'attenuation')
    return {'linked': True, 'registered_handlers': handlers, 'source_sha256': EXPECTED_SHA,
            'duration_s': float(read_field(wave, 'duration')), 'cue': CUE, 'gameplay_cue': GC,
            'cue_volume': float(read_field(cue, 'volume_multiplier')),
            'handler_volume': float(read_field(cdo, 'volume_multiplier')),
            'handler_pitch': float(read_field(cdo, 'pitch_multiplier')),
            'spatialize': bool(read_field(attenuation, 'spatialize')),
            'inner_radius_cm': float(read_field(attenuation, 'attenuation_shape_extents').x),
            'falloff_cm': float(read_field(attenuation, 'falloff_distance')),
            'max_concurrent': int(read_field(settings, 'max_count')),
            'limit_to_owner': bool(read_field(settings, 'limit_to_owner')),
            'pie_audible': 'not_tested'}


def main():
    writing = '--apply' in sys.argv or '-MHGZHitAudioApply' in unreal.SystemLibrary.get_command_line()
    row = source()
    assets.tag(TAG)
    manifest = OUT / 'manifest.json'
    state = json.loads(manifest.read_text(encoding='utf-8')) if manifest.exists() else None
    before = protected_hashes()
    if state and before != state['protected_packages']:
        raise RuntimeError('Protected package changed since intent; inspect before retry')
    if writing:
        if state is None:
            state = {'owner': OWNER, 'phase': 'intent', 'protected_packages': before,
                     'source': row, 'write_scope': [WAVE, CUE, ATT, CON, GC]}
            assets.save_json(manifest, state)
        apply(row)
    result = audit(row)
    assert before == protected_hashes(), 'Existing package modified'
    if writing and not result['linked']:
        raise RuntimeError('Hit assets not linked after apply')
    if state and result['linked']:
        state['phase'] = 'saved' if writing else 'independently_reloaded'
        assets.save_json(manifest, state)
    result.update(mode='apply' if writing else 'audit', protected_package_count=len(before))
    assets.save_json(OUT / ('apply.json' if writing else 'audit.json'), result)
    print('WEAPON_HIT_AUDIO ' + json.dumps(result))


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        assets.save_json(OUT / 'failure.json', {'error': repr(error),
            'note': 'Inspect saved postconditions before retry; do not blindly overwrite.'})
        raise
