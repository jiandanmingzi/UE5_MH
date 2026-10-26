"""应用已签定的21项Cue音量；由现有setup_current_weapon_audio入口调度。

默认只读；显式--apply/-MHGZCurrentWeaponAudioApply仅首次写入。
复用原生T3D导出与保存工具，不重放历史音频事务，不生成/修改音源。
"""
from pathlib import Path
import json
import math
import os
import re
import shutil
import struct

import unreal
import setup_swing_pilot as pilot
from ue_asset_utils import read_field

ROOT = Path(unreal.Paths.project_dir()).resolve()
PLAN_DIR = ROOT / 'Saved/Audio/RiseIG/LoudnessPlan20261026'
OUT = ROOT / 'Saved/Audio/RiseIG/LoudnessApply20261026'
PLAN_SHA256 = '3655510138d687fee1d0fde794e2eb721de5fd6e20cd04e70965468e92b3ab81'
UNITY_OUT = ROOT / 'Saved/Audio/RiseIG/UnityVolume20261026'


def load(path):
    return json.loads(path.read_text(encoding='utf-8'))


def float32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def protected_native(cue):
    """仅排除Cue自身的VolumeMultiplier；所有子节点和其余字段保留比较。"""
    return re.sub(r'^   VolumeMultiplier=.*\n?', '', pilot.inspector.text_export(cue), flags=re.M)


def verify_hashes(hashes):
    for filename, expected in hashes.items():
        path = Path(filename).resolve()
        if not path.is_relative_to(ROOT / 'Content') or pilot.digest(path) != expected:
            raise RuntimeError('Protected package changed: ' + str(path))


def run(writing=False):
    if (UNITY_OUT / 'state.json').exists():
        raise RuntimeError('The old loudness plan is superseded; use --unity-volume audit, never restore old gains')
    OUT.mkdir(parents=True, exist_ok=True)
    pilot.inspector.OUT = OUT / ('ApplyNative' if writing else 'AuditNative')
    plan_file = PLAN_DIR / 'gain-plan.json'
    if pilot.digest(plan_file) != PLAN_SHA256:
        raise RuntimeError('Approved loudness plan identity changed')
    plan = load(plan_file)
    snapshot = load(PLAN_DIR / 'asset-snapshot.json')
    scope = load(PLAN_DIR / 'scope.json')
    if not plan['success'] or not plan['proposal_only'] or not snapshot['success']:
        raise RuntimeError('Approved plan or source snapshot is incomplete')
    rows = [r for r in plan['rows'] if r['actual_change']]
    if len(rows) != 21 or len({r['cue'] for r in rows}) != 21:
        raise RuntimeError('Frozen write scope must contain exactly 21 distinct Cues')
    if len(plan['rows']) != 36 or sum(not r['actual_change'] for r in plan['rows']) != 15:
        raise RuntimeError('Frozen keep scope changed')
    for filename, expected in plan['input_files'].items():
        if pilot.digest(filename) != expected:
            raise RuntimeError('Loudness measurement input changed: ' + filename)
    sources = {w['filenames'][0]: w['source_sha256']
               for cue in snapshot['sounds'].values() for w in cue['waves']}
    if len(sources) != 46 or any(pilot.digest(p) != h for p, h in sources.items()):
        raise RuntimeError('Original WAV source identity changed')

    checkpoint = OUT / 'state.json'
    state = load(checkpoint) if checkpoint.exists() else None
    if writing and state:
        raise RuntimeError('Loudness transaction already exists; use read-only audit, never replay Apply')
    if state and state['plan_sha256'] != PLAN_SHA256:
        raise RuntimeError('Foreign loudness checkpoint')

    if not state:
        verify_hashes(snapshot['protected_hashes'])
        combat = ROOT / 'Content/Weapons/InsectGlaive/Data/DA_IG_Combat.uasset'
        if pilot.digest(combat) != scope['preserve_user_dirty_combat_sha256']:
            raise RuntimeError('User Combat edit changed since planning')
        dirty = {p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
        prepared = []
        for row in rows:
            path = row['cue']
            if not path.startswith('/Game/Weapons/InsectGlaive/Audio/Cues/'):
                raise RuntimeError('Cue outside approved audio directory: ' + path)
            cue = unreal.load_asset(path)
            if not isinstance(cue, unreal.SoundCue):
                raise RuntimeError('Resident SoundCue unavailable: ' + path)
            if path.split('.')[0] in dirty:
                raise RuntimeError('Target Cue has unsaved editor changes: ' + path)
            wanted = float32(row['proposed_cue_volume'])
            current = float(read_field(cue, 'volume_multiplier'))
            if current != row['current_cue_volume'] or not math.isfinite(wanted) or wanted <= 0:
                raise RuntimeError('Live gain does not match the approved plan: ' + path)
            if abs(20 * math.log10(wanted / current) - row['delta_db']) > .00001:
                raise RuntimeError('Float32 gain differs from proposed dB: ' + path)
            prepared.append((cue, dict(path=path, before_volume=current, after_volume=wanted,
                before_sha256=pilot.digest(pilot.inspector.disk(path)),
                protected_native=protected_native(cue))))
        if not writing:
            result = dict(phase='ready_no_writes', changes=len(rows), keep=15,
                          source_wavs_verified=46, plan_sha256=PLAN_SHA256)
            pilot.save_json(OUT / 'preflight.json', result)
            print('LOUDNESS_PLAN ' + json.dumps(result))
            return

        # 全部前置核对完成后先备份21份原包，再记录意图，最后才写内存与磁盘。
        for _, record in prepared:
            disk = pilot.inspector.disk(record['path'])
            backup = OUT / 'Backup' / disk.relative_to(ROOT)
            backup.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(disk, backup)
            if pilot.digest(backup) != record['before_sha256']:
                raise RuntimeError('Cue backup mismatch: ' + record['path'])
            record['backup'] = str(backup)
        state = dict(phase='intent', plan_sha256=PLAN_SHA256, apply_pid=os.getpid(),
            before_hashes=snapshot['protected_hashes'], changed={},
            records=[record for _, record in prepared], source_wavs_verified=46)
        pilot.save_json(checkpoint, state)
        for cue, record in prepared:
            cue.set_editor_property('volume_multiplier', record['after_volume'])
            if protected_native(cue) != record['protected_native']:
                raise RuntimeError('Cue graph or non-volume field changed: ' + record['path'])
            pilot.save(cue)
            filename = pilot.inspector.disk(record['path']).as_posix()
            state['changed'][filename] = pilot.digest(filename)
            pilot.save_json(checkpoint, state)
        expected = dict(state['before_hashes'], **state['changed'])
        verify_hashes(expected)
        state['phase'] = 'saved_pending_independent_reload'
        pilot.save_json(checkpoint, state)
    else:
        if state['phase'] not in ('saved_pending_independent_reload', 'independently_reloaded'):
            raise RuntimeError('Partial transaction; inspect state and packages before any retry')
        if state['apply_pid'] == os.getpid():
            raise RuntimeError('Independent reload requires a fresh process/editor session')
        if len(state['changed']) != 21:
            raise RuntimeError('Saved write set incomplete')
        verify_hashes(dict(state['before_hashes'], **state['changed']))

    # 新进程重新加载21份Cue，完整原生导出只允许音量字段不同。
    values = []
    for record in state['records']:
        cue = unreal.load_asset(record['path'])
        if (not isinstance(cue, unreal.SoundCue)
                or float(read_field(cue, 'volume_multiplier')) != record['after_volume']
                or protected_native(cue) != record['protected_native']):
            raise RuntimeError('Saved Cue gain, graph, or other fields differ: ' + record['path'])
        if pilot.digest(record['backup']) != record['before_sha256']:
            raise RuntimeError('Original Cue backup changed: ' + record['path'])
        values.append(dict(path=record['path'], before=record['before_volume'],
                           after=float(read_field(cue, 'volume_multiplier'))))
    phase = 'saved_pending_independent_reload' if writing else 'independently_reloaded'
    result = dict(success=True, phase=phase, changes=21, keep=15,
        protected_other_assets=len(state['before_hashes']) - len(state['changed']),
        cue_graphs_and_other_fields_preserved=True, source_wavs_verified=46,
        changed_volume_values=values, pie_listened=False, plan_sha256=PLAN_SHA256)
    state['phase'] = phase
    if not writing:
        state['audit_pid'] = os.getpid()
    pilot.save_json(checkpoint, state)
    pilot.save_json(OUT / ('apply.json' if writing else 'audit.json'), result)
    print('LOUDNESS_APPLY ' + json.dumps({k: v for k, v in result.items() if k != 'changed_volume_values'}))


def run_unity(writing=False):
    """用户覆盖旧分档：全部虫棍Cue/Wave音量设1，其余音频行为保持。"""
    output = UNITY_OUT
    output.mkdir(parents=True, exist_ok=True)
    pilot.inspector.OUT = output / ('ApplyNative' if writing else 'AuditNative')
    checkpoint = output / 'state.json'
    state = load(checkpoint) if checkpoint.exists() else None
    if writing and state:
        raise RuntimeError('Unity transaction exists; use read-only audit, never replay Apply')

    audio_files = sorted({p for folder in ('Content/Weapons/InsectGlaive/Audio',
                                          'Content/Audio/Combat/InsectGlaive')
                          for p in (ROOT / folder).rglob('*.uasset')})
    montage_files = sorted({p for folder in ('Content/Weapons/InsectGlaive/Anims/Montage',
                                            'Content/Characters/Demo/Anims/Montage')
                            for p in (ROOT / folder).glob('*.uasset')})
    extra_files = [ROOT / p for p in (
        'Content/GameplayCues/GCN_Hit_Kinsect.uasset',
        'Content/GameplayCues/GCN_Hit_WeaponSound.uasset',
        'Content/Blueprints/Ability/InsectGlaive/GA_IG_PowderVortex.uasset',
        'Content/Weapons/InsectGlaive/Data/DA_IG_Combat.uasset',
        'Content/Weapons/InsectGlaive/Data/DA_IG_HuoLongGun.uasset')]
    before = {p.as_posix(): pilot.digest(p) for p in audio_files + montage_files + extra_files}
    current, objects, other_classes = [], {}, {}
    for disk in audio_files:
        package = '/Game/' + disk.relative_to(ROOT / 'Content').with_suffix('').as_posix()
        asset = unreal.load_asset(package)
        if isinstance(asset, unreal.SoundCue):
            field, native_field = 'volume_multiplier', 'VolumeMultiplier'
        elif isinstance(asset, unreal.SoundWave):
            field, native_field = 'volume', 'Volume'
        else:
            kind = asset.get_class().get_name() if asset else 'Unavailable'
            other_classes[kind] = other_classes.get(kind, 0) + 1
            if kind not in ('SoundAttenuation', 'SoundConcurrency'):
                raise RuntimeError('Inspect additional audio class before unity writes: ' + kind)
            continue
        objects[asset.get_path_name()] = asset
        current.append(dict(path=asset.get_path_name(), field=field, native_field=native_field,
            before_volume=float(read_field(asset, field)), kind=asset.get_class().get_name(),
            before_sha256=pilot.digest(disk)))

    # 播放入口已为1时只检查、不重存；不清空/恢复装备和其它空音源通知。
    notify_count, silent_count = 0, 0
    for disk in montage_files:
        package = '/Game/' + disk.relative_to(ROOT / 'Content').with_suffix('').as_posix()
        montage = unreal.load_asset(package)
        for event in unreal.AnimationLibrary.get_animation_notify_events(montage):
            notify = read_field(event, 'notify')
            if isinstance(notify, unreal.AnimNotify_SwingSound):
                if float(read_field(notify, 'volume_multiplier')) != 1.0:
                    raise RuntimeError('Non-unity notify requires a scoped entry edit: ' + notify.get_path_name())
                notify_count += 1
                silent_count += read_field(notify, 'sound') is None
    from ue_asset_utils import blueprint_cdo
    for path in ('/Game/GameplayCues/GCN_Hit_WeaponSound', '/Game/GameplayCues/GCN_Hit_Kinsect'):
        cdo = blueprint_cdo(unreal.load_asset(path), unreal)
        if float(read_field(cdo, 'volume_multiplier')) != 1.0:
            raise RuntimeError('Non-unity hit handler requires a scoped entry edit: ' + path)
    component = read_field(unreal.get_default_object(unreal.Kinsect), 'flight_audio')
    if float(read_field(component, 'volume_multiplier')) != 1.0:
        raise RuntimeError('Native flight component gain changed')

    def native(record):
        text = re.sub(r'^   ' + record['native_field'] + r'=.*\n?', '',
                      pilot.inspector.text_export(objects[record['path']]), flags=re.M)
        # UE5.6 SoundCue::Serialize在保存时将无FirstNode的空Cue缓存时长重算为0。
        # 仅允许这个已从引擎源码核实的派生缓存变化，不放宽有声Cue或其它字段。
        if (record['kind'] == 'SoundCue'
                and not re.search(r'Class=/Script/Engine\.SoundNode\w+', text)
                and not re.search(r'^   FirstNode=', text, re.M)):
            text = re.sub(r'^   Duration=0\.000000\n?', '', text, flags=re.M)
        return text

    if state is None:
        changed = [r for r in current if r['before_volume'] != 1.0]
        dirty = {p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
        if any(r['path'].split('.')[0] in dirty for r in changed):
            raise RuntimeError('A target audio package has unsaved changes')
        for record in changed:
            record['protected_native'] = native(record)
        if not writing:
            result = dict(phase='ready_no_writes', total_sounds=len(current), changes=len(changed),
                          audio_classes=other_classes, notification_count=notify_count)
            pilot.save_json(output / 'preflight.json', result)
            print('UNITY_VOLUME ' + json.dumps(result))
            return
        for record in changed:
            disk = pilot.inspector.disk(record['path'])
            backup = output / 'Backup' / disk.relative_to(ROOT)
            backup.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(disk, backup)
            if pilot.digest(backup) != record['before_sha256']:
                raise RuntimeError('Unity backup mismatch: ' + record['path'])
            record['backup'] = str(backup)
        state = dict(phase='intent', apply_pid=os.getpid(), before_hashes=before,
            all_sounds=current, records=changed, changed={}, other_audio_classes=other_classes)
        pilot.save_json(checkpoint, state)
        for record in changed:
            asset = objects[record['path']]
            asset.set_editor_property(record['field'], 1.0)
            if native(record) != record['protected_native']:
                raise RuntimeError('Non-volume audio data changed: ' + record['path'])
            pilot.save(asset)
            disk = pilot.inspector.disk(record['path']).as_posix()
            state['changed'][disk] = pilot.digest(disk)
            pilot.save_json(checkpoint, state)
        state['phase'] = 'saved_pending_independent_reload'
    else:
        fully_saved = (len(state['changed']) == len(state['records'])
                       and set(state['changed']) == {
                           pilot.inspector.disk(r['path']).as_posix() for r in state['records']})
        if state['phase'] == 'intent' and fully_saved:
            # 首次保存已全部完成、仅最终校验失败时，只读核对后续状态，不重放写入。
            state['recovered_by_read_only_validation'] = True
        elif state['phase'] not in ('saved_pending_independent_reload', 'independently_reloaded'):
            raise RuntimeError('Partial unity transaction; inspect saved packages before retry')
        if state['apply_pid'] == os.getpid():
            raise RuntimeError('Unity audit needs a new process/editor session')
        if {r['path'] for r in current} != {r['path'] for r in state['all_sounds']}:
            raise RuntimeError('Audio asset inventory changed after unity apply')

    verify_hashes(dict(state['before_hashes'], **state['changed']))
    if any(float(read_field(objects[r['path']], r['field'])) != 1.0 for r in current):
        raise RuntimeError('Not every audio gain equals 1.0')
    for record in state['records']:
        if native(record) != record['protected_native'] or pilot.digest(record['backup']) != record['before_sha256']:
            raise RuntimeError('Unity non-volume data or backup differs: ' + record['path'])
    state['phase'] = 'saved_pending_independent_reload' if writing else 'independently_reloaded'
    if not writing:
        state['audit_pid'] = os.getpid()
    result = dict(success=True, phase=state['phase'], all_sound_gains_unity=True,
        sound_cues=sum(r['kind']=='SoundCue' for r in current),
        sound_waves=sum(r['kind']=='SoundWave' for r in current),
        changed_packages=len(state['changed']), protected_other_packages=len(before)-len(state['changed']),
        notification_gains_checked=notify_count, silent_notifications_preserved=silent_count,
        hit_handler_gains_checked=2, flight_component_unity=True,
        graphs_pitch_attenuation_concurrency_and_sources_preserved=True,
        empty_cue_duration_cache_zero_allowed=True, pie_listened=False)
    pilot.save_json(checkpoint, state)
    pilot.save_json(output / ('apply.json' if writing else 'audit.json'), result)
    print('UNITY_VOLUME ' + json.dumps(result))
