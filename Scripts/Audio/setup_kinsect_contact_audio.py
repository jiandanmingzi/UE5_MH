"""Existing Kinsect HitCue leaf; reuse resident audio, no Montage/GA/mix changes.

Invoked through setup_current_weapon_audio --kinsect-contact. Default plan/audit;
one explicit first Apply creates one GCN. Existing accepted packages stay exact.
"""
from pathlib import Path
import unreal
import audio_spatial_maintenance as shared
from ue_asset_utils import blueprint_cdo, read_field

pilot = shared.pilot
OUT = shared.ROOT / 'Saved/Audio/RiseIG/KinsectContact20261020/UE'
OWNER = 'mhgz_audio_kinsect_contact_20261020_v1'


def run(writing=False):
    plan = shared.load(OUT.parent / 'plan.json')
    for p, sha in plan['inputs'].items():
        if pilot.digest(p) != sha:
            raise RuntimeError('Kinsect contact source changed: ' + p)
    OUT.mkdir(parents=True, exist_ok=True)
    pilot.inspector.OUT = OUT / 'NativeExports'
    cue_path = plan['gameplay_cue']
    checkpoint = OUT / 'state.json'
    state = shared.load(checkpoint) if checkpoint.exists() else None
    if writing and state:
        raise RuntimeError('Transaction exists; inspect postconditions instead of replaying Apply')
    source = unreal.load_asset(plan['source']['cue'])
    if not isinstance(source, unreal.SoundCue) or 'bLooping=True' in pilot.inspector.text_export(source):
        raise RuntimeError('Missing/non-short resident source')
    for variant in plan['source']['variants']:
        wave = unreal.load_asset(variant['wave'])
        if (not isinstance(wave, unreal.SoundWave) or read_field(wave, 'looping')
                or unreal.EditorAssetLibrary.get_metadata_tag(wave, 'MHGZ.Audio.SourceSHA256') != variant['sha256']):
            raise RuntimeError('Resident source no longer matches prepared current weapon')
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.scan_paths_synchronous(['/Game/GameplayCues'], force_rescan=True)
    def handlers():
        return [str(a.package_name) for a in registry.get_assets_by_path('/Game/GameplayCues', recursive=True)
                if str(a.get_tag_value('GameplayCueName')) == plan['tag']]
    if not state:
        if unreal.EditorAssetLibrary.does_asset_exist(cue_path) or handlers():
            raise RuntimeError('Existing handler requires reconciliation, not overwrite/duplicate')
        protected = {}
        for folder in ('Content/Weapons/InsectGlaive', 'Content/Blueprints/Ability/InsectGlaive', 'Content/GameplayCues'):
            for p in (shared.ROOT / folder).rglob('*.uasset'):
                protected[p.as_posix()] = pilot.digest(p)
        state = dict(phase='read_only_plan', protected=protected, gameplay_cue=cue_path,
                     code_sha256={str(shared.ROOT/p):pilot.digest(shared.ROOT/p) for p in (
                         'Source/MHGZ/GameplayCues/MHGZWeaponHitSoundCue.cpp',
                         'Source/MHGZ/GameplayCues/MHGZWeaponHitSoundCue.h')})
        pilot.save_json(OUT / 'plan.json', state)
        if not writing:
            print('KINSECT_CONTACT_PLAN one new GCN; no existing package writes')
            return
        state['phase'] = 'intent'
        pilot.save_json(checkpoint, state)
        factory = unreal.BlueprintFactory()
        factory.set_editor_property('parent_class', unreal.MHGZWeaponHitSoundCue)
        bp, fresh = pilot.create(cue_path, unreal.Blueprint, factory, OWNER)
        if not fresh:
            raise RuntimeError('Creation race')
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        cdo = blueprint_cdo(bp, unreal)
        cdo.set_editor_property('gameplay_cue_tag', pilot.tag(plan['tag']))
        cdo.set_editor_property('contact_source', unreal.MHGZDamageSourceType.KINSECT)
        cdo.set_editor_property('hit_sound', source)
        cdo.set_editor_property('volume_multiplier', 1.0)
        cdo.set_editor_property('pitch_multiplier', 1.0)
        pilot.own(bp, OWNER)
        state['expected_hash'] = pilot.digest(pilot.inspector.disk(cue_path))
        state['phase'] = 'saved_pending_independent_reload'
        pilot.save_json(checkpoint, state)
    if 'expected_hash' not in state:
        raise RuntimeError('Partial transaction; inspect existing package before retry')
    for p, sha in {**state['protected'], **state['code_sha256']}.items():
        if pilot.digest(p) != sha:
            raise RuntimeError('Protected existing file changed: ' + p)
    if pilot.digest(pilot.inspector.disk(cue_path)) != state['expected_hash']:
        raise RuntimeError('New handler package changed')
    bp = pilot.existing(cue_path, unreal.Blueprint, OWNER)
    cdo = blueprint_cdo(bp, unreal)
    registry.scan_paths_synchronous(['/Game/GameplayCues'], force_rescan=True)
    if (not isinstance(cdo, unreal.MHGZWeaponHitSoundCue)
            or read_field(cdo, 'contact_source') != unreal.MHGZDamageSourceType.KINSECT
            or read_field(cdo, 'hit_sound') != source
            or plan['tag'] not in read_field(cdo, 'gameplay_cue_tag').export_text()
            or handlers() != [cue_path]):
        raise RuntimeError('Saved source filter, hard reference or unique Cue registration failed')
    state['phase'] = 'saved_pending_independent_reload' if writing else 'independently_reloaded'
    pilot.save_json(checkpoint, state)
    report = dict(phase=state['phase'], handlers=handlers(), sound=source.get_path_name(),
                  contact_source='Kinsect', protected_existing_packages=len(state['protected']),
                  powder_configured=False, mix_changed=False, pie_listened=False)
    pilot.save_json(OUT / ('apply.json' if writing else 'audit.json'), report)
    print('KINSECT_CONTACT_AUDIO ' + str(report))
