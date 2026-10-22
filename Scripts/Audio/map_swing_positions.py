"""Offline swing-position candidates, not exact original Notify truth.

Reuses the capture reader/dominant-node selector and read-only UE snapshot.
Legacy mode reads three fixed captures. Explicit --map-current-weapon mode
uses one reviewed Primary capture and branch-scoped exclusions instead.
Writes Saved; --publish writes only the selected mode's dedicated references.
Never imports audio or changes UE assets.
"""
from __future__ import annotations
import argparse
import bisect
import copy
from collections import defaultdict
import json
from pathlib import Path
import re
import statistics

import rise_audio_capture_map as capture_map
import rise_audio_m1 as m1

ROOT = m1.PROJECT
OUT = ROOT / 'Saved/Audio/RiseIG/SwingMap'
SWINGS = (2231624382, 2702726931, 2037497988, 106259332, 1392529433)
CAPTURES = ('mhrise_20261018_151536_02', 'mhrise_20261018_151312_01',
            'mhrise_20261018_131457_01')  # Prefer latest pure capture per motion.
MOTION = re.compile(r'^plw_InsectGlaive_100_([0-9]+)$')


def short(path):
    return path.rsplit('/', 1)[-1].split('.')[0]


def bracket(rows, times, time):
    """Keep edge/clip/reset ambiguity explicit; never interpolate across clips."""
    i = bisect.bisect_right(times, time) - 1
    before = rows[i] if i >= 0 else None
    after = rows[i + 1] if i + 1 < len(rows) else None
    result = {'before_sample': int(before['sample_index']) if before else None,
              'after_sample': int(after['sample_index']) if after else None,
              'motion': before['motion_name'] if before else None,
              'status': 'outside_samples', 'native_frame_estimate': None}
    if before is None or after is None:
        return result
    a, b = float(before['node_frame']), float(after['node_frame'])
    result.update(native_frames=[a, b], sample_times_s=[times[i], times[i + 1]],
                  native_end_frame=float(before['node_end_frame']), episode=before['_episode'])
    if before['_episode'] != after['_episode']:
        result['status'] = 'clip_or_reset_boundary'
    elif times[i + 1] - times[i] > 0.05 or b < a - 0.001:
        result['status'] = 'gap_or_reverse_clock'
    else:
        alpha = (time - times[i]) / (times[i + 1] - times[i])
        result.update(status='temporal_candidate', native_frame_estimate=a + alpha * (b - a))
    return result


def read_capture(directory, capture):
    paths = {kind: directory / (capture + suffix) for kind, suffix in
             [('audio', '_audio_native.csv'), ('animation', '_animation.csv')]}
    animation = capture_map.dominant_motion_rows(capture_map.csv_rows(paths['animation']))
    episodes = capture_map.motion_episodes(animation, {})
    starts = [row['start_s'] for row in episodes]
    for row in animation:
        row['_episode'] = bisect.bisect_right(starts, float(row['capture_time_s']))
    times = [float(row['capture_time_s']) for row in animation]
    events = []
    for row in capture_map.csv_rows(paths['audio']):
        identity = int(row['event_id'])
        if identity not in SWINGS:
            continue
        time = float(row['capture_time_estimate_s'])
        mapped = bracket_audio(animation, times, row)
        events.append(dict(mapped, event_id=identity, audio_row=int(row['row']), capture=capture,
                           capture_time_estimate_s=time, game_object_id=row['game_object_id']))
    return {'capture': capture, 'inputs': {k: {'path': str(v), 'sha256': m1.sha256(v)}
                                          for k, v in paths.items()},
            'events': events, 'dominant_episodes': len(episodes)}


def bracket_audio(rows, times, event):
    """Join audio to animation by Present counter, NOT delayed IPC-start time.

    These recorder sessions share their process Present epoch. A Lua reload or
    missing/non-overlapping counter must be investigated, never silently aligned
    by capture_time_estimate_s. Callback ordering leaves about one frame of phase
    uncertainty; this is still a request-position estimate, not final voice time.
    """
    frame = int(event.get('native_present_frame') or -1)
    index = bisect.bisect_left(rows, frame, key=lambda row: int(row['render_callback']))
    if frame < 0 or index >= len(rows) or int(rows[index]['render_callback']) != frame:
        return dict(status='native_present_frame_missing_or_epoch_mismatch', motion=None,
                    native_frame_estimate=None, timing_source='native_present_frame')
    before, after = rows[index], rows[min(index + 1, len(rows) - 1)]
    same = (index + 1 < len(rows) and before['motion_name'] == after['motion_name']
            and before.get('_episode') == after.get('_episode')
            and float(after['node_frame']) >= float(before['node_frame'])
            and int(after['render_callback']) - frame <= 1)
    return dict(status='temporal_candidate' if same else 'clip_or_reset_boundary',
        before_sample=int(before['sample_index']), after_sample=int(after['sample_index']),
        motion=before['motion_name'], native_frame_estimate=float(before['node_frame']) if same else None,
        native_frames=[float(before['node_frame']), float(after['node_frame'])],
        native_end_frame=float(before['node_end_frame']), episode=before.get('_episode'),
        sample_times_s=[float(before['capture_time_s']), float(after['capture_time_s'])],
        timing_source='native_present_frame', native_present_frame=frame,
        estimate_clock_offset_s=float(before['capture_time_s']) - float(event['capture_time_estimate_s']))


def cluster_positions(events, max_span=2.5):
    """Bounded frame clusters; a repeated event within an episode cannot merge."""
    groups = defaultdict(list)
    for row in events:
        if row['status'] == 'temporal_candidate' and MOTION.fullmatch(row['motion'] or ''):
            groups[(row['motion'], row['event_id'])].append(row)
    clusters = []
    for (motion, event), values in sorted(groups.items()):
        buckets = []
        for row in sorted(values, key=lambda x: x['native_frame_estimate']):
            frame = row['native_frame_estimate']
            key = (row['capture'], row['episode'])
            target = next((b for b in reversed(buckets)
                           if frame - b[0]['native_frame_estimate'] <= max_span
                           and key not in {(x['capture'], x['episode']) for x in b}), None)
            if target is None:
                buckets.append([row])
            else:
                target.append(row)
        for index, bucket in enumerate(buckets):
            frames = [r['native_frame_estimate'] for r in bucket]
            ends = sorted({r['native_end_frame'] for r in bucket})
            clusters.append({'id': f'{motion}:{event}:{index + 1}', 'motion': motion,
                             'source_id': int(MOTION.fullmatch(motion)[1]), 'event_id': event,
                             'native_frame_estimate': statistics.median(frames),
                             'native_frame_range': [min(frames), max(frames)],
                             'native_end_frames': ends, 'observations': len(bucket),
                             'capture': bucket[0]['capture'],
                             'evidence': [{'audio_row': r['audio_row'], 'episode': r['episode'],
                                           'native_frames': r['native_frames'],
                                           'sample_times_s': r['sample_times_s']} for r in bucket]})
    return clusters


def frame_calibration(sequence, cluster):
    """Clip-length check, not blanket RateScale or original-FPS inference.

    Previously root-calibrated Rise160 uses 2x source frames. Admit that scale
    for another clip only when numeric source identity and its own frame extent
    agree. Exported FBX and UE both report60Hz; do not mislabel them30Hz.
    """
    if cluster['source_id'] not in sequence['import_numeric_ids']:
        return None
    ends = cluster['native_end_frames']
    if len(ends) != 1 or ends[0] <= 1:
        return None
    expected = 2 * (ends[0] - 1)
    if abs(sequence['frames'] - expected) > 0.05:
        return None
    return {'native_to_ue_frame_scale': 2, 'offset_ue_frames': 0,
            'status': 'numeric_source_and_frame_extent_candidate',
            'ue_frames': sequence['frames'], 'native_end_frame': ends[0],
            'ue_fps': sequence['fps'], 'import_file_fps': sequence['import_file_fps'],
            'limitation': 'length agreement, not independent pose or exact audio clock calibration'}


def segment_positions(segment, sequence, cluster):
    calibration = frame_calibration(sequence, cluster)
    if calibration is None:
        return []
    source = cluster['native_frame_estimate'] * 2 / sequence['fps']
    lo, hi = [f * 2 / sequence['fps'] for f in cluster['native_frame_range']]
    start, end, rate = segment['source_start_s'], segment['source_end_s'], segment['effective_rate']
    if not start <= source < end or rate == 0:
        return []
    duration = (end - start) / abs(rate)
    result = []
    for loop in range(segment['loops']):
        relative = (source - start) / rate if rate > 0 else (end - source) / -rate
        montage_time = segment['track_start_s'] + loop * duration + relative
        result.append({'cluster_id': cluster['id'], 'event_id': cluster['event_id'],
                       'segment_index': segment['index'], 'slot': segment['slot'],
                       'loop': loop, 'sequence': segment['sequence'], 'source_id': cluster['source_id'],
                       'source_time_estimate_s': source, 'montage_time_estimate_s': montage_time,
                       'cut_boundary_ambiguous': lo < start or hi >= end,
                       'calibration': calibration})
    return result


def route(montage, entry):
    sections = sorted(montage['sections'], key=lambda r: r['time_s'])
    if not sections:
        return {'entry': entry, 'status': 'linear', 'intervals': [[0, montage['length_s']]], 'sections': []}
    entry = entry if entry not in ('None', '', None) else sections[0]['name']
    by_name = {s['name']: i for i, s in enumerate(sections)}
    if entry not in by_name:
        return {'entry': entry, 'status': 'invalid_entry', 'intervals': [], 'sections': []}
    seen, intervals = [], []
    name = entry
    while name not in ('None', '', None):
        if name in seen:
            return {'entry': entry, 'status': 'cycle_prefix_only', 'cycle': name,
                    'intervals': intervals, 'sections': seen}
        if name not in by_name:
            return {'entry': entry, 'status': 'missing_next_section', 'intervals': intervals, 'sections': seen}
        index = by_name[name]
        section = sections[index]
        intervals.append([section['time_s'], sections[index + 1]['time_s']
                          if index + 1 < len(sections) else montage['length_s']])
        seen.append(name)
        name = section['next']
    return {'entry': entry, 'status': 'static_section_route', 'intervals': intervals, 'sections': seen}


def entry_for(config, transition, source):
    for field, key in (('entry_section_by_transition_id', transition),
                       ('entry_section_by_source_state', source)):
        value = config.get(field, {})
        if isinstance(value, dict) and key in value:
            return value[key]
    value = config.get('default_entry_section')
    return value if isinstance(value, str) and value not in ('None', '') else None


def reachable(position, routing):
    return any(lo <= position < hi for lo, hi in routing['intervals'])


def build(directory, snapshot):
    if not snapshot['success'] or not snapshot['content_unchanged']:
        raise ValueError('Require a successful read-only asset snapshot')
    captures = [read_capture(directory, capture) for capture in CAPTURES]
    preferred = {}
    selected = []
    for capture in captures:
        motions = {r['motion'] for r in capture['events'] if r['status'] == 'temporal_candidate'
                   and MOTION.fullmatch(r['motion'] or '')}
        for motion in sorted(motions):
            if motion not in preferred:
                preferred[motion] = capture['capture']
                selected.extend(r for r in capture['events'] if r['motion'] == motion)
    all_clusters = cluster_positions(selected)
    referenced_ids = {i for s in snapshot['sequences'].values() for i in s['import_numeric_ids']}
    clusters = [c for c in all_clusters if c['source_id'] in referenced_ids]
    montages = []
    for montage in snapshot['montages']:
        candidates, missing = [], []
        for segment in montage['segments']:
            sequence = snapshot['sequences'][segment['sequence']]
            relevant = [c for c in clusters if c['source_id'] in sequence['import_numeric_ids']]
            for cluster in relevant:
                candidates.extend(segment_positions(segment, sequence, cluster))
            if not relevant:
                missing.append({'segment_index': segment['index'], 'sequence': segment['sequence'],
                                'source_ids': sequence['import_numeric_ids'],
                                'reason': 'no_confirmed_swing_observation_in_existing_captures'})
            elif not any(frame_calibration(sequence, c) for c in relevant):
                missing.append({'segment_index': segment['index'], 'sequence': segment['sequence'],
                                'source_ids': sequence['import_numeric_ids'],
                                'reason': 'frame_extent_mismatch_no_automatic_time_conversion'})
        routes = []
        for section in montage['sections'] or [{'name': None}]:
            routing = route(montage, section['name'])
            routing['candidate_indices'] = [i for i, c in enumerate(candidates)
                                            if reachable(c['montage_time_estimate_s'], routing)]
            routes.append(routing)
        montages.append({'path': montage['path'], 'length_s': montage['length_s'],
                         'rate_scale': montage['rate_scale'], 'segments': montage['segments'],
                         'sections': montage['sections'], 'candidates': sorted(candidates,
                         key=lambda c: (c['montage_time_estimate_s'], c['event_id'])),
                         'routes': routes, 'missing_evidence': missing})
        # Route indices must refer to final sorted candidates, not insertion order.
        for routing in routes:
            routing['candidate_indices'] = [i for i, c in enumerate(montages[-1]['candidates'])
                                            if reachable(c['montage_time_estimate_s'], routing)]
    abilities = []
    for ability in snapshot['abilities']:
        edges = [e for e in snapshot['combo_transitions'] if e['ability_class'] == ability['class']]
        entries = []
        primary = next((m for m in snapshot['montages'] if m['path'] == ability['primary_montage']), None)
        for edge in edges:
            entry = entry_for(ability['entry_config'], edge['transition_id'], edge['source_state'])
            routing = route(primary, entry) if primary else None
            entries.append(dict(edge, selected_entry=entry, static_route=routing))
        abilities.append(dict(ability, combo_entries=entries))
    return {'schema': 'mhgz_swing_position_candidates_v1', 'runtime_ready': False,
            'captures': captures, 'preferred_capture_by_motion': preferred,
            'selected_event_count': len(selected), 'clusters': clusters, 'montages': montages,
            'unreferenced_source_clusters': [c for c in all_clusters if c['source_id'] not in referenced_ids],
            'sequences': snapshot['sequences'],
            'abilities': abilities, 'protected_package_hashes': snapshot['protected_package_hashes'],
            'limits': ['post requests are all-world, unattributed; temporal association only',
                       'sample brackets do not bound the unknown IPC/Render clock delay',
                       'frame extent calibration is a candidate, not exact original Notify truth',
                       'static Section routes do not predict runtime jumps/cancel/hold exits',
                       'missing observations do not prove silence; no audio assets or Notifies written']}


def markdown(report):
    lines = ['# 虫棍挥击音效位置配置候选表', '',
             '更新：2026-10-18。由 `Scripts/Audio/map_swing_positions.py --publish` 生成；仅元数据。', '',
             '候选是否已接线以当前签收及音效资源映射的M3实际记录为准；不从本表的runtime_ready推断UE是否有声音。', '',
             '## 1. 使用边界', '',
             '**这是源动画到当前 Montage 的时间关联候选，不是原作精确帧真值。实际接入状态见资源映射第14节和当前签收。**', '',
             '- 5种风声已人工确认；时间仍是全场景 Post 请求与猎人动画的时间关联，未捕获发声对象/实际分支。',
             '- 每种源动画优先用最新纯净录制；只在其缺失时补前两份已有录制，不要求重录。',
             '- 原始帧单位与 UE 帧不可直接相等。每条序列按数字源ID与 `UE帧数=2×(原生EndFrame−1)` 独立校核；当前导入文件/UE采样率均为60Hz，不能误称原文件30Hz。',
             '- 上述是长度校准候选；既有160的Root校准支持2倍关系，但没有把它不加检查推广到全部动画。位姿、音频锚点延迟仍未独立校准。',
             '- 原生帧范围只显示本次观测散布，不包括无法量化的IPC/Render延迟；单次样本尤其不能当精确帧。',
             '- 换算：`源秒=原生帧×2/序列FPS`；`Montage秒=片段StartPos+(源秒−AnimStartTime)/有效倍率`。有效倍率为序列RateScale×片段PlayRate；反向/重复另算。',
             '- 下表为Montage时间轴秒数，不是GA激活后的墙钟时间。跳Section按实际经过的区间播放，不补跳过的声音；取消后不补播。',
             '- 仅检查5种已确认风声；没有候选不等于原作无声。世界380/381另属项目共享适配，不能套崛起原作真值。', '',
              '## 2. 音色简记', '', '| 代码 | EventID | 用途 |', '|---|---:|---|']
    codes = {event: chr(65 + i) for i, event in enumerate(SWINGS)}
    lines += [f'| {codes[e]} | {e} | 已确认挥击声 |' for e in SWINGS]
    lines += ['', '## 3. 源动画事件位置', '',
              '帧列使用原生帧估计；同一动画同一音色重复出现保留为多行。边界/不连续采样已隔离在Saved完整报告。', '',
              '| 源ID | 声音 | 原生帧中值（观测范围） | 次数 | 录制尾号 |', '|---:|---|---|---:|---|']
    for c in sorted(report['clusters'], key=lambda x: (x['source_id'], x['native_frame_estimate'], x['event_id'])):
        lo, hi = c['native_frame_range']
        lines.append(f"| {c['source_id']} | {codes[c['event_id']]} | {c['native_frame_estimate']:.2f}（{lo:.2f}–{hi:.2f}） | {c['observations']} | {c['capture'].removeprefix('mhrise_20261018_')} |")
    lines += ['', '## 4. 当前Montage的候选位置', '',
              '每项格式为“时间s:音色[源ID]”；★表示观测范围跨裁切边界，后续接线前必须单独裁定。仅反映经过片段的候选，不等于该Montage全部声音已齐。', '',
              '| Montage | 候选配置 | 缺少证据的源ID/片段 |', '|---|---|---|']
    for m in report['montages']:
        items = [f"{c['montage_time_estimate_s']:.3f}:{codes[c['event_id']]}[{c['source_id']}]" +
                 ('★' if c['cut_boundary_ambiguous'] else '') for c in m['candidates']]
        missing = ', '.join('/'.join(map(str, r['source_ids'])) or short(r['sequence']) for r in m['missing_evidence'])
        lines.append(f"| {short(m['path'])} | {'；'.join(items) or '无候选'} | {missing or '本批涉及源均有观测，非完整签收'} |")
    lines += ['', '## 5. 当前Combo配置入口与静态经过区间', '',
              '按当前GA的TransitionID→SourceState→Default优先级解析主AttackMontage；同一Section合并列出Combo边。只列该入口会经过的候选，不补播其它Entry。派生类运行时跳Section/取消仍须单独检查，次Montage不套用主入口。', '',
              '| GA / 主Montage | Section入口 | Combo边 | 静态路径中的候选时间s:音色 |', '|---|---|---|---|']
    for a in report['abilities']:
        grouped = defaultdict(list)
        for e in a['combo_entries']:
            grouped[e['selected_entry']].append(e)
        primary = next((m for m in report['montages'] if m['path'] == a['primary_montage']), None)
        if not grouped:
            lines.append(f"| {short(a['path'])} | 无该Combo表入口 | 调用方控制 | 见第4节/JSON |")
        for entry, edges in grouped.items():
            routing = edges[0]['static_route']
            candidates = [c for c in primary['candidates'] if reachable(c['montage_time_estimate_s'], routing)] if primary and routing else []
            values = '；'.join(f"{c['montage_time_estimate_s']:.3f}:{codes[c['event_id']]}" for c in candidates)
            if routing and routing['status'] not in ('static_section_route', 'linear'):
                values += f"（{routing['status']}，仅静态前缀）"
            lines.append(f"| {short(a['path'])} / {short(a['primary_montage']) if primary else '由派生类决定'} | {entry or 'Montage开头/派生类控制'} | {'、'.join(e['transition_id'] for e in edges)} | {values or '无候选/未解析主Montage'} |")
    lines += ['', '## 6. 下一步接线规则', '',
              '1. 公用Notify/声音解析链独立实现；声音通知跟随实际Montage时间，不用激活计时器。保留原伤害/标签/入口/倍率。',
              '2. 先对无边界歧义、重复样本稳定的基础地面招式做链路小样；本表不是全量自动写入许可。',
              '3. 多入口按Section经过区间筛选；静态图遇Hold循环只列循环前缀，运行时跳转、提前派生和Over独立检查。',
              '4. 缺项先用已有素材补证据，必要时明确标项目共享/替代；觉虫击及世界急袭不伪称已录到。真实命中、尘土和落地不混入本表。',
              '', f"统计：3份录制共{sum(len(c['events']) for c in report['captures'])}次已确认风声请求；优先选片后{report['selected_event_count']}次时间关联。当前引用源中形成{len(report['clusters'])}组事件候选；{len(report['unreferenced_source_clusters'])}组未被当前Montage引用的动画上下文单独隔离，不冒充攻击待办。",
              '', '完整样本、隔离条目、Section区间、源/资产哈希见 `Saved/Audio/RiseIG/SwingMap/positions.json`；便携候选元数据见同名reference JSON。', '']
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', required=True, type=Path)
    parser.add_argument('--publish', action='store_true')
    parser.add_argument('--audit-coverage', action='store_true', help='Inventory existing captures/hit proof/entries; never overwrite frozen swing positions')
    parser.add_argument('--map-current-weapon', action='store_true', help='Map one explicit current-weapon capture with same-object conditions and branch-scoped user exclusions')
    parser.add_argument('--compare-cloth', action='store_true', help='Compare explicit no-armor capture with frozen current-weapon equipment graphs; Saved only')
    parser.add_argument('--capture', help='Explicit current weapon capture; never falls back to old captures')
    parser.add_argument('--evidence-root', type=Path, help='Existing capture map and user-review root')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--source-root', type=Path, default=Path('D:/study/MHR/natives/STM'))
    args = parser.parse_args()
    if args.compare_cloth:
        if args.map_current_weapon or args.audit_coverage or args.publish or not args.capture or not args.evidence_root or not args.output:
            parser.error('--compare-cloth requires --capture, --evidence-root, --output; cannot combine other modes/publish')
        from rise_audio_cloth_comparison import run
        run(args.directory.resolve(), args.capture, args.evidence_root.resolve(), args.output, args.source_root.resolve())
        return
    if args.map_current_weapon:
        if args.audit_coverage or not args.capture or not args.evidence_root or not args.output:
            parser.error('--map-current-weapon requires --capture, --evidence-root, --output and cannot combine --audit-coverage')
        from rise_audio_weapon_mapping import run
        run(args.directory.resolve(), args.capture, args.evidence_root.resolve(), args.output,
            args.source_root.resolve(), args.publish)
        return
    if args.capture or args.evidence_root:
        parser.error('--capture and --evidence-root require --map-current-weapon')
    if args.audit_coverage:
        from rise_audio_coverage import run
        run(args.directory.resolve(), args.output or ROOT/'Saved/Audio/RiseIG/Coverage20261019', args.publish, args.source_root.resolve())
        return
    if args.output:
        parser.error('--output requires an explicit mapping/audit mode; original frozen position output is unchanged')
    snapshot_path = OUT / 'asset-snapshot.json'
    snapshot = json.loads(snapshot_path.read_text(encoding='utf-8'))
    # Reject stale disk snapshots before publishing times for current assets.
    for path, fingerprint in snapshot['protected_package_hashes'].items():
        if m1.sha256(Path(path)) != fingerprint:
            raise ValueError('Asset changed after snapshot: ' + path)
    report = build(args.directory.resolve(), snapshot)
    report['asset_snapshot_sha256'] = m1.sha256(snapshot_path)
    m1.write_json(OUT / 'positions.json', report)
    (OUT / 'positions.md').write_text(markdown(report), encoding='utf-8')
    if args.publish:
        compact = copy.deepcopy({k: v for k, v in report.items() if k not in ('captures', 'protected_package_hashes')})
        compact['asset_package_hashes'] = {Path(k).relative_to(ROOT).as_posix(): v
                                           for k, v in report['protected_package_hashes'].items()}
        compact['capture_inputs'] = [{'capture': c['capture'], 'inputs': c['inputs']} for c in report['captures']]
        for cluster in compact['clusters'] + compact['unreferenced_source_clusters']:
            cluster.pop('evidence', None)
        m1.write_json(ROOT / 'docs/reference/虫棍挥击音效位置配置.json', compact)
        (ROOT / 'docs/reference/虫棍挥击音效位置配置.md').write_text(markdown(report), encoding='utf-8')
    print(json.dumps({'captures': len(report['captures']), 'selected_requests': report['selected_event_count'],
                      'source_clusters': len(report['clusters']), 'montages': len(report['montages']),
                      'mapped_positions': sum(len(m['candidates']) for m in report['montages']),
                      'runtime_ready': False}, ensure_ascii=False))


if __name__ == '__main__':
    main()
