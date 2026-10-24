"""Reviewed powder lifecycle audio; resident assets, scoped backup, audit by default.

Reuse propulsion's Wave/Cue factory and shared spatial settings/exports. This
does not author Montage points or change any powder/gameplay configuration.
"""
import re
import shutil
import unreal
import audio_spatial_maintenance as shared
import setup_propulsion_audio as pool
from ue_asset_utils import read_field
from ue_asset_utils import blueprint_cdo

pilot = shared.pilot
OUT = shared.ROOT / 'Saved/Audio/RiseIG/AerialPowder20261023/Powder/UE'
OWNER = 'mhgz_audio_powder_20261023_v1'
CONFIG = '/Game/Weapons/InsectGlaive/Data/DA_IG_Combat'
CONCURRENCY = pilot.AUDIO + '/Settings/SCN_IG_FenChen'


def without_audio(text):
    return re.sub(r'^\s*Powder(?:ExplosionSound|VortexGatherSounds(?:\(\d+\))?|VortexExplosionSound)=.*(?:\n|$)',
                  '', text, flags=re.M)


def run(writing=False):
    OUT.mkdir(parents=True, exist_ok=True)
    pilot.inspector.OUT = OUT / 'NativeExports'
    plan = shared.load(OUT.parent / 'source-plan.json')
    for path, sha in plan['inputs'].items():
        if pilot.digest(path) != sha:
            raise RuntimeError('Frozen powder input changed: ' + path)
    checkpoint = OUT / 'state.json'
    state = shared.load(checkpoint) if checkpoint.exists() else None
    if writing and state:
        raise RuntimeError('Powder transaction exists; audit saved outputs, never replay Apply')
    config = unreal.load_asset(CONFIG)
    if not isinstance(config, unreal.InsectGlaiveCombatConfig):
        raise RuntimeError('Missing CombatConfig')
    migration_dir = shared.ROOT / 'Saved/Maintenance/PowderVortexConfig20261024'
    migration = shared.load(migration_dir / 'verified.json') if (migration_dir / 'verified.json').exists() else None
    vortex = None
    if migration:
        if writing:
            raise RuntimeError('Vortex audio migrated to GA; this historical Apply must not be replayed')
        ga_path = '/Game/Blueprints/Ability/InsectGlaive/GA_IG_PowderVortex'
        if pilot.digest(pilot.inspector.disk(ga_path)) != migration['ga_sha256']:
            raise RuntimeError('GA changed after the vortex migration; inspect current user edits')
        vortex = read_field(blueprint_cdo(unreal.load_asset(ga_path), unreal), 'powder_vortex')
    fields = ('powder_explosion_sound', 'powder_vortex_gather_sounds', 'powder_vortex_explosion_sound')
    if state is None:
        if any(read_field(config, k) for k in fields):
            raise RuntimeError('Powder sound fields already set; refuse overwrite')
        paths = [CONCURRENCY, *[s['cue'] for s in plan['sources']],
                 *[v['wave'] for s in plan['sources'] for v in s['variants']]]
        if any(unreal.EditorAssetLibrary.does_asset_exist(p) for p in paths):
            raise RuntimeError('New powder source name collision; do not overwrite assets')
        if not writing:
            print('POWDER_AUDIO_READY 6 Waves, 4 Cues, 1 concurrency, 3 config fields')
            return
        disk = pilot.inspector.disk(CONFIG)
        backup = OUT / 'Backup' / disk.relative_to(shared.ROOT)
        backup.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(disk, backup)
        protected = {}
        for folder in ('Content/Weapons/InsectGlaive/Audio', 'Content/Weapons/InsectGlaive/Data',
                       'Content/Weapons/InsectGlaive/Anims/Montage'):
            for path in (shared.ROOT / folder).rglob('*.uasset'):
                protected[str(path)] = pilot.digest(path)
        state = dict(phase='intent', protected=protected, new_assets={},
            backup=dict(path=str(backup), sha256=pilot.digest(disk)),
            config_before=pilot.inspector.text_export(config),
            plan_sha256=pilot.digest(OUT.parent / 'source-plan.json'))
        pilot.save_json(checkpoint, state)
        concurrency = shared.settings('SCN_IG_FenChen', count=8)
        settings = read_field(concurrency, 'concurrency')
        settings.set_editor_property('limit_to_owner', False)
        concurrency.set_editor_property('concurrency', settings)
        pilot.own(concurrency, OWNER)
        state['new_assets'][CONCURRENCY] = pilot.digest(pilot.inspector.disk(CONCURRENCY))
        pilot.save_json(checkpoint, state)
    if state['plan_sha256'] != pilot.digest(OUT.parent / 'source-plan.json'):
        raise RuntimeError('Powder source plan changed after intent')
    concurrency = pilot.existing(CONCURRENCY, unreal.SoundConcurrency, OWNER)
    if not concurrency:
        raise RuntimeError('Incomplete powder concurrency creation')
    settings = read_field(concurrency, 'concurrency')
    if (read_field(settings, 'max_count') != 8 or read_field(settings, 'limit_to_owner')
            or read_field(settings, 'resolution_rule') != unreal.MaxConcurrentResolutionRule.STOP_OLDEST):
        raise RuntimeError('Powder-only global concurrency policy changed')
    cues = {}
    for source in plan['sources']:
        cue = pool.sources(dict(source=source), writing, state, OUT, OWNER, CONCURRENCY)
        native = pilot.inspector.text_export(cue)
        if ('SoundNodeRandom' in native) != (len(source['variants']) > 1):
            raise RuntimeError('Powder random alternatives not preserved')
        cues[source['event_id']] = cue
    expected = dict(powder_explosion_sound=cues[615339279],
        powder_vortex_gather_sounds=[cues[18325887], cues[1714085883]],
        powder_vortex_explosion_sound=cues[647565313])
    if writing:
        for field, value in expected.items():
            config.set_editor_property(field, value)
        if without_audio(pilot.inspector.text_export(config)) != without_audio(state['config_before']):
            raise RuntimeError('Non-audio CombatConfig changed; refuse save')
        pilot.save(config)
        state['config_hash'] = pilot.digest(pilot.inspector.disk(CONFIG))
        state['phase'] = 'saved_pending_independent_reload'
        pilot.save_json(checkpoint, state)
    if 'config_hash' not in state:
        raise RuntimeError('Partial transaction; inspect saved assets before resuming')
    expected_config_hash = (shared.load(migration_dir / 'source.json')['sha256'][CONFIG]
                            if migration else state['config_hash'])
    actual_config_hash = pilot.digest(pilot.inspector.disk(CONFIG))
    if actual_config_hash != expected_config_hash:
        import sys
        sys.path.insert(0, str(shared.ROOT / 'Scripts/AssetOrganization'))
        from migrate_ig_action_config import accepts_combat_cleanup
        if not accepts_combat_cleanup(shared.ROOT, expected_config_hash, actual_config_hash):
            raise RuntimeError('Powder config changed outside the verified Combat cleanup')
    if not migration and without_audio(pilot.inspector.text_export(config)) != without_audio(state['config_before']):
        raise RuntimeError('Non-audio config differs after reload')
    for field, value in expected.items():
        owner = vortex if migration and field.startswith('powder_vortex_') else config
        target_field = field.removeprefix('powder_vortex_') if owner is vortex else field
        actual = read_field(owner, target_field)
        if (list(actual) if isinstance(value, list) else actual) != value:
            raise RuntimeError('Missing runtime reference: ' + field)
    for path, sha in state['new_assets'].items():
        if pilot.digest(pilot.inspector.disk(path)) != sha:
            raise RuntimeError('Powder audio asset changed: ' + path)
    for path, sha in state['protected'].items():
        if path != str(pilot.inspector.disk(CONFIG)) and pilot.digest(path) != sha:
            raise RuntimeError('Protected package changed: ' + path)
    state['phase'] = 'saved_pending_independent_reload' if writing else 'independently_reloaded'
    pilot.save_json(checkpoint, state)
    result = dict(phase=state['phase'], new_audio_assets=len(state['new_assets']),
        config_fields=list(expected), original_gameplay_preserved=True,
        protected_unchanged=len(state['protected'])-1, spatial_attenuation='SA_IG_DongZuo',
        concurrency='global8 powder-only; StopOldest', pie_audible='not_tested')
    pilot.save_json(OUT / ('apply.json' if writing else 'audit.json'), result)
    print('POWDER_AUDIO ' + str(result))
