"""Historical Section handoff hypothesis; withdrawn after user clarification.

A source-relative onset can be correctly placed yet unreachable from another
entry that jumps over its prefix. Copy that onset into ONLY the skipping
predecessor, preserving its lead to the shared source frame. Never shift Core.
"""
import argparse
import copy
import hashlib
import json
import shutil
from pathlib import Path

import map_swing_positions as positions
from plan_swing_coverage import fingerprint

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'Saved/Audio/RiseIG/SectionAudio20261022'
# Reviewed short pre-Core onsets. This is not an automatic rule that moves all
# omitted sound points: unused action prefixes, holds and ground events stay put.
HANDOFFS = (
    ('AM_IG_SiLianYinZhan', 'Entry_From_HengSao', 'Core', 2231624382),
    ('AM_IG_FeiYuan', 'Entry_From_ErLian', 'Core', 1737389684),
    ('AM_IG_FeiYuan', 'Entry_From_HengSao', 'Core', 1737389684),
    ('AM_IG_ShangLao', 'Entry_From_HengSao', 'Core_From_Bridge', 4196403743),
    ('AM_IG_ShangLao', 'Entry_From_FeiShenYueRu', 'Core_From_Bridge', 4196403743),
)


def tag(row):
    value = row['event_tag']
    return value.split('"')[1] if '"' in value else value


def section_intervals(montage):
    sections = sorted(montage['sections'], key=lambda r: r['time_s'])
    return {s['name']: (s, s['time_s'], sections[i + 1]['time_s']
            if i + 1 < len(sections) else montage['length_s']) for i, s in enumerate(sections)}


def at_time(montage, time, slot):
    hits = [s for s in montage['segments'] if s['slot'] == slot
        and s['track_start_s'] <= time + 1e-6
        and time < s['track_start_s'] + (s['source_end_s'] - s['source_start_s'])
            * s['loops'] / abs(s['effective_rate']) - 1e-6]
    if len(hits) != 1:
        raise ValueError('Need one unambiguous segment at Section time')
    return hits[0]


def handoff_patch(montage, original, anchor, entry, target):
    intervals = section_intervals(montage)
    previous, begin, end = intervals[entry]
    _, cut, _ = intervals[target]
    if previous['next'] != target or entry == target:
        raise ValueError('Not a direct Section handoff')
    route = positions.route(montage, entry)
    if route['status'] != 'static_section_route' or positions.reachable(original['time'], route):
        raise ValueError('Original must be skipped, not replayed or inside a cycle')
    segment = at_time(montage, cut, anchor['slot'])
    if segment['sequence'] != anchor['sequence'] or segment['effective_rate'] <= 0 or segment['loops'] != 1:
        raise ValueError('Not the same forward source interval')
    source_cut = segment['source_start_s'] + (cut - segment['track_start_s']) * segment['effective_rate']
    lead = (source_cut - anchor['source_time_estimate_s']) / segment['effective_rate']
    time = end - lead
    if not (lead > 1e-6 and begin < time < end - 1e-6):
        raise ValueError('Source onset cannot fit inside skipping entry')
    for section in montage['sections']:
        route = positions.route(montage, section['name'])
        if positions.reachable(time, route) and positions.reachable(original['time'], route):
            raise ValueError('Copy would double-play on an existing entry route')
    if any(r['sound'] == original['sound'] and abs(r['time'] - time) < 1 / 60 for r in montage['audio']):
        raise ValueError('Equivalent onset already exists')
    return dict(entry=entry, target=target, time=time, lead_s=lead,
        source_cut_s=source_cut, source_guid=original['guid'],
        original=copy.deepcopy(original), source_anchor=copy.deepcopy(anchor))


def report(snapshot, anchors):
    drift, checked, routes = [], 0, []
    for montage in snapshot['montages']:
        for point in montage['audio']:
            anchor = anchors.get((montage['path'], tag(point)))
            if not anchor:
                continue
            checked += 1
            segment = next((s for s in montage['segments'] if (s['slot'], s['index']) ==
                (anchor['slot'], anchor['segment_index'])), None)
            if not segment or segment['sequence'] != anchor['sequence']:
                drift.append(dict(path=montage['path'], guid=point['guid'], reason='source_changed'))
                continue
            rate = segment['effective_rate']
            relative = (anchor['source_time_estimate_s'] - segment['source_start_s']) / rate if rate > 0 else (
                segment['source_end_s'] - anchor['source_time_estimate_s']) / -rate
            expected = segment['track_start_s'] + anchor['loop'] * (
                segment['source_end_s'] - segment['source_start_s']) / abs(rate) + relative
            if abs(expected - point['time']) > 1e-5:
                drift.append(dict(path=montage['path'], guid=point['guid'], expected=expected, actual=point['time']))
        if len(montage['sections']) < 2 or not montage['audio']:
            continue
        for section in montage['sections']:
            route = positions.route(montage, section['name'])
            elapsed, events = 0., []
            for start, end in route['intervals']:
                events.extend(dict(guid=p['guid'], montage_time=p['time'], traversal_time=elapsed + p['time'] - start)
                    for p in montage['audio'] if start <= p['time'] < end)
                elapsed += end - start
            routes.append(dict(path=montage['path'], entry=section['name'], route=route, events=events))
    return dict(montages=len(snapshot['montages']), points=sum(len(m['audio']) for m in snapshot['montages']),
        checked_source_anchors=checked, source_anchor_drift=drift, routes=routes,
        limits=['Static cycles list one visit only; runtime input/release/landing branches remain runtime-owned.',
                'No global timeline offset is added; sound onsets in skipped prefixes need separate handoff coverage.'])


def prepare():
    raise ValueError('Handoff compensation was withdrawn; inspect source-time/pose evidence instead')


def _historical_prepare():
    if (OUT / 'state.json').exists() or (OUT / 'plan.json').exists():
        raise ValueError('Frozen Section repair exists; audit, do not replan')
    snapshot_path = OUT / 'snapshot.json'
    plan_path = ROOT / 'Saved/Audio/RiseIG/Weapon20261019/UEActionRuntime/plan.json'
    snapshot = json.loads(snapshot_path.read_text(encoding='utf-8'))
    old = json.loads(plan_path.read_text(encoding='utf-8'))
    anchors = {(m['path'], r['event_tag']): r for m in old['montages'] for r in m['recipes']}
    finding = report(snapshot, anchors)
    if finding['source_anchor_drift']:
        raise ValueError('Resolve changed source anchors before applying the reviewed handoffs')
    patches = {}
    for name, entry, target, event in HANDOFFS:
        montage = next(m for m in snapshot['montages'] if m['path'].split('.')[-1] == name)
        matches = []
        for original in montage['audio']:
            anchor = anchors.get((montage['path'], tag(original)))
            if not anchor or anchor['event_id'] != event or original['kind'] != 'AnimNotify_SwingSound':
                continue
            try:
                patch = handoff_patch(montage, original, anchor, entry, target)
            except ValueError:
                continue
            matches.append(patch)
        if len(matches) != 1:
            raise ValueError('Reviewed handoff is no longer unique: ' + str((name, entry, matches)))
        patches.setdefault(montage['path'], []).extend(matches)
    plan = dict(patches=patches, inputs={str(p): hashlib.sha256(p.read_bytes()).hexdigest()
                for p in (snapshot_path, plan_path)}, protected=snapshot['protected_package_hashes'],
                findings=finding, non_goals=['No animation/Section/GA/attack window changes; no new sound assets or runtime timers.'])
    plan['plan_sha256'] = fingerprint(plan)
    (OUT / 'plan.json').write_text(json.dumps(plan, ensure_ascii=False, indent=2), encoding='utf-8')
    lines = ['# 分支蒙太奇音效边界修复', '',
        f"已读{finding['montages']}份Montage、{finding['points']}个声音/表现点；{finding['checked_source_anchors']}个已有源帧锚点无时间漂移。",
        '', '问题是共用Core的前置挥击声位于另一入口，跳段路径会漏掉起声；不是所有声音都算错了绝对时间。', '',
        '| Montage | 入口 → 共用段 | 原点所在时间 | 当前路径补点 | 提前于共用段 |', '|---|---|---:|---:|---:|']
    for path, rows in patches.items():
        for p in rows:
            lines.append(f"| {path.split('.')[-1]} | {p['entry']} → {p['target']} | {p['original']['time']:.6f}s | {p['time']:.6f}s | {p['lead_s']:.6f}s |")
    lines += ['', '仅补跳段入口；原正常入口和共用段的通知不移位，逐入口检查不能同时经过原点与副本。',
        '时间为当前Montage时间轴，不是激活后的计时。循环/中断仍由实际Montage经过哪些区间决定，不排队补播未经过的点。',
        '本报告不替代PIE耳听；原录制的帧锚点仍属已有采样关联精度。']
    (OUT / 'findings.md').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    print(dict(patches=sum(map(len, patches.values())), montages=len(patches), checked=finding['checked_source_anchors']))


def withdraw():
    """Undo only the exact three still-unchanged packages from our last write.

    Run only with the UE editor closed. Preserve both versions and all evidence.
    """
    state_path = OUT / 'state.json'
    state = json.loads(state_path.read_text(encoding='utf-8'))
    plan = json.loads((OUT / 'plan.json').read_text(encoding='utf-8'))
    checkpoint = OUT / 'Withdrawal/state.json'
    if checkpoint.exists() or state['phase'] != 'independently_reloaded':
        raise ValueError('Withdrawal already started or unexpected predecessor state')
    files = []
    for package, backup in state['backups'].items():
        target = (ROOT / ('Content/' + package.split('.')[0].removeprefix('/Game/') + '.uasset')).resolve()
        if not target.is_relative_to((ROOT / 'Content/Weapons/InsectGlaive/Anims/Montage').resolve()):
            raise ValueError('Unexpected restore target')
        current_sha = hashlib.sha256(target.read_bytes()).hexdigest()
        original_sha = hashlib.sha256(Path(backup).read_bytes()).hexdigest()
        if current_sha != state['expected_hashes'][target.as_posix()] or original_sha != plan['protected'][target.as_posix()]:
            raise ValueError('User/package changed since our write; no automatic restore')
        files.append((target, Path(backup), current_sha, original_sha))
    recovery = dict(phase='intent', reason='User clarified source-time/pose mismatch; speculative prefix copies not justified', files=[])
    checkpoint.parent.mkdir(parents=True, exist_ok=True)
    for target, backup, current_sha, original_sha in files:
        retained = checkpoint.parent / 'Before' / target.relative_to(ROOT)
        retained.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(target, retained)
        recovery['files'].append(dict(target=target.as_posix(), retained=retained.as_posix(),
            old_sha256=current_sha, restored_sha256=original_sha))
    checkpoint.write_text(json.dumps(recovery, indent=2), encoding='utf-8')
    for target, backup, _, original_sha in files:
        shutil.copy2(backup, target)
        if hashlib.sha256(target.read_bytes()).hexdigest() != original_sha:
            raise RuntimeError('Restore verification failed')
    recovery['phase'] = 'withdrawn_and_original_hashes_verified'
    checkpoint.write_text(json.dumps(recovery, indent=2), encoding='utf-8')
    state['phase'] = 'withdrawn_after_user_clarification'
    state['withdrawal'] = checkpoint.as_posix()
    state_path.write_text(json.dumps(state, ensure_ascii=False, indent=2), encoding='utf-8')
    print(dict(phase=state['phase'], packages=len(files), removed_points=sum(map(len, state['added'].values()))))


def prepare_source_times():
    """Re-read exact original audio rows; correct frames THEN map source clips.

    Only two proven delayed-START recordings are repaired. Other primary/cloth
    captures keep their accepted candidate precision; never shift all assets by
    one recording's clock offset. Current sessions have a shared Present epoch.
    """
    import statistics
    import rise_audio_capture_map as capture
    output = ROOT / 'Saved/Audio/RiseIG/SourceTime20261022'
    if (output / 'plan.json').exists() or (output / 'state.json').exists():
        raise ValueError('Source-time transaction exists; audit, do not replan')
    snapshot_file = OUT / 'snapshot.json'
    snapshot = json.loads(snapshot_file.read_text(encoding='utf-8'))
    by_path = {m['path']: m for m in snapshot['montages']}
    inputs = {snapshot_file.as_posix(): hashlib.sha256(snapshot_file.read_bytes()).hexdigest()}
    changes, evidence = {}, []
    def load(file):
        inputs[Path(file).as_posix()] = hashlib.sha256(Path(file).read_bytes()).hexdigest()
        return json.loads(Path(file).read_text(encoding='utf-8'))
    def live_point(path, point):
        found = [a for a in by_path[path]['audio'] if a['sound']
            and a['sound'].split('.')[0] == point['sound'].split('.')[0] and abs(a['time'] - point['time']) < 1e-5]
        if len(found) != 1:
            raise ValueError('Old sound point is not unique/current')
        return found[0]
    def add(path, old, **kwargs):
        row = dict(guid=old['guid'], expected_time=old['time'], remove=False, original=old, **kwargs)
        changes.setdefault(path, []).append(row)
        return row
    for folder in ('GroundEvents20261021', 'SiLianImpact20261021'):
        plan = load(ROOT / 'Saved/Audio/RiseIG' / folder / 'source-plan.json')
        files = [Path(p) for p in plan['inputs'] if p.endswith(('_animation.csv', '_audio_native.csv'))]
        for file in files:
            if hashlib.sha256(file.read_bytes()).hexdigest() != plan['inputs'][str(file)]:
                # Path slash normalization can differ between Windows exports.
                raise ValueError('Original recording changed')
            inputs[file.as_posix()] = hashlib.sha256(file.read_bytes()).hexdigest()
        animation_file = next(p for p in files if p.name.endswith('_animation.csv'))
        audio_file = next(p for p in files if p.name.endswith('_audio_native.csv'))
        rows = capture.dominant_motion_rows(capture.csv_rows(animation_file))
        times = [float(r['capture_time_s']) for r in rows]
        audio = {r['row']: r for r in capture.csv_rows(audio_file)}
        point_map = plan['points'] if isinstance(plan['points'], dict) else {
            next(p for p in by_path if 'AM_IG_SiLianYinZhan.' in p): plan['points']}
        for path, points in point_map.items():
            for point in points:
                cluster = point.get('cluster', plan.get('cluster'))
                observed = [dict(positions.bracket_audio(rows, times, audio[str(i)]), audio_row=str(i))
                            for i in cluster['audio_rows']]
                valid = [r for r in observed if r['status'] == 'temporal_candidate' and r['motion'] == cluster['motion']]
                old = live_point(path, point)
                if not valid:
                    # Four old109 records are actually304, already authored in
                    # FeiYuan's304 entry. Do not leave its sound on HengSao too.
                    if {r['motion'] for r in observed} != {'plw_InsectGlaive_100_304'}:
                        raise ValueError('Unexpected changed motion attribution')
                    item = add(path, old, reason='estimated_clock_misattributed_304_as109', observations=observed)
                    item['remove'] = True
                    continue
                if len(valid) < 2:
                    raise ValueError('Need repeated exact-frame source evidence')
                corrected = dict(cluster,
                    native_frame_estimate=statistics.median(r['native_frame_estimate'] for r in valid),
                    native_frame_range=[min(r['native_frame_estimate'] for r in valid), max(r['native_frame_estimate'] for r in valid)],
                    native_end_frames=sorted({r['native_end_frame'] for r in valid}), observations=len(valid))
                anchor = point['placement']
                segment = next(s for s in by_path[path]['segments'] if (s['slot'], s['index']) ==
                    (anchor['slot'], anchor['segment_index']))
                seq = snapshot['sequences'][segment['sequence']]
                mapped = positions.segment_positions(segment, seq, corrected)
                if len(mapped) != 1 or mapped[0]['cut_boundary_ambiguous']:
                    raise ValueError('Corrected event falls outside the retained source interval')
                item = add(path, old, time=mapped[0]['montage_time_estimate_s'],
                    source= mapped[0], corrected_cluster=corrected, observations=observed,
                    reason='exact_present_source_frame_then_trim_rate_segment_mapping')
                evidence.append(dict(capture=plan['capture'], guid=old['guid'],
                    old_native_frame=cluster['native_frame_estimate'], new_native_frame=corrected['native_frame_estimate'],
                    old_montage_time=old['time'], new_montage_time=item['time']))
    # The explicitly shared World381 recovery twirl was authored relative to the
    # now-corrected ErLian onset, not a separate World recording. Propagate delta.
    descendant = load(ROOT / 'Saved/Audio/RiseIG/DescendingThrustAudio20261021/source-plan.json')
    reference = next(c for p, cs in changes.items() if 'AM_IG_ErLian.' in p for c in cs)
    over_path = next(p for p in descendant['points'] if 'AM_IG_JiXiTuCi_Over.' in p)
    twirl = next(p for p in descendant['points'][over_path] if '_938078130_' in p['sound'])
    old = live_point(over_path, twirl)
    add(over_path, old, time=old['time'] + reference['time'] - reference['expected_time'],
        reason='existing_ErLian_shared_recovery_twirl_delta', reference_guid=reference['guid'])
    for file, sha in snapshot['protected_package_hashes'].items():
        if hashlib.sha256(Path(file).read_bytes()).hexdigest() != sha:
            raise ValueError('Asset changed after withdrawal: ' + file)
    result = dict(changes=changes, inputs=inputs, evidence=evidence, before=snapshot,
        protected=snapshot['protected_package_hashes'],
        uncertainty='Shared Present epoch offset0 for these current sessions; callbacks have about1 display frame phase uncertainty.',
        limits='Requests are not final voice timestamps; no new audio decode, attack timing or sequence edits.')
    result['plan_sha256'] = fingerprint(result)
    (output / 'plan.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    print(dict(montages=len(changes), retimed=sum(not c['remove'] for cs in changes.values() for c in cs),
        removed=sum(c['remove'] for cs in changes.values() for c in cs)))
    for path, cs in changes.items():
        print(path.split('.')[-1], [(round(c['expected_time'], 6), round(c['time'], 6) if not c['remove'] else 'remove') for c in cs])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--prepare', action='store_true')
    mode.add_argument('--withdraw', action='store_true')
    mode.add_argument('--prepare-source-times', action='store_true')
    args = parser.parse_args()
    if args.prepare_source_times:
        prepare_source_times()
    else:
        withdraw() if args.withdraw else prepare()
