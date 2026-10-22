"""Coverage-only helper for map_swing_positions --audit-coverage.

Reuses the established node/episode/bracket/route helpers and combat auditor.
No new audio parser, decoder, game probe or asset writer. Old mappings stay put.
"""
from collections import Counter, defaultdict
import bisect
import csv
import json
from pathlib import Path

import rise_audio_capture_map as capture_map
import rise_audio_m1 as m1
import map_swing_positions as positions
from rise_audio_combat import audit_rows

ACTION_NAMES = {
    'BaDaoZhan': '拔刀斩', 'CaoChongZhan': '操虫斩', 'ChongYin': '虫印斩',
    'ErLian': '二连斩', 'FeiShenYueRu': '飞身跃入斩', 'FeiYuan': '飞圆斩',
    'HengSao': '横扫斩', 'HouChe': '后撤斩', 'JiaSha': '袈裟斩',
    'JiXiTuCi': '急袭突刺', 'JueChongJi': '觉虫击',
    'QiangHuaCaoChongChuanCi': '强化操虫穿刺', 'ShangLao': '上捞斩',
    'SiLianYinZhan': '四连印斩', 'TuCi': '突刺', 'TuJinHuiXuanZhan': '突进回旋斩',
}
# Human-confirmed meanings only. Auxiliary effects never become weapon swings.
SOUND_NAMES = {**{e: '挥击' for e in positions.SWINGS}, 1932771116: '命中',
    3520842405: '空中推进', 2878059743: '操虫武器', 3014295296: '撑杆插地',
    3777612407: '尘土', 1707236307: '落地/水花', 2304525234: '落地',
    3971364902: '精华交付', 806767037: '精华交付'}


def read_rows(path):
    if path.stat().st_size > 64 * 1024 * 1024:
        raise ValueError('Coverage CSV exceeds64MiB: ' + str(path))
    with path.open(encoding='utf-8-sig', newline='') as stream:
        rows = list(csv.DictReader(stream))
    if any(None in r or any(v is None for v in r.values()) for r in rows):
        raise ValueError('Ragged coverage CSV: ' + str(path))
    return rows


def source_id(motion):
    match = positions.MOTION.fullmatch(motion or '')
    return int(match[1]) if match else None


def analyse_capture(directory, capture, nodes, metadata):
    kinds = ('audio_native', 'animation', 'hits', 'audio_objects', 'audio_context', 'events')
    paths = {k: directory / f'{capture}_{k}.csv' for k in kinds}
    inputs = {k: {'path': str(p), 'sha256': m1.sha256(p), 'bytes': p.stat().st_size}
              for k, p in paths.items() if p.is_file()}
    if 'animation' not in inputs:
        return {'capture': capture, 'inputs': inputs, 'excluded': 'no_animation_sidecar'}, [], [], []
    rows = {k: read_rows(p) if k in inputs else [] for k, p in paths.items()}
    animation = capture_map.dominant_motion_rows(rows['animation'])
    episodes = capture_map.motion_episodes(animation, {})
    starts = [e['start_s'] for e in episodes]
    for r in animation:
        r['_episode'] = bisect.bisect_right(starts, float(r['capture_time_s']))
    times = [float(r['capture_time_s']) for r in animation]
    by_sample = {int(r['sample_index']): r for r in animation}
    observations = [{'capture': capture, 'source_id': source_id(r['motion_name']),
                     'frame': float(r['node_frame']), 'end_frame': float(r['node_end_frame']),
                     'sample_index': int(r['sample_index'])} for r in animation
                    if source_id(r['motion_name']) is not None]
    events, isolated = [], Counter()
    for r in rows['audio_native']:
        event = int(r['event_id'])
        meta = metadata.get(event, {})
        if event not in SOUND_NAMES and not meta.get('in_scope_candidate'):
            continue
        context = positions.bracket(animation, times, float(r['capture_time_estimate_s']))
        identity = source_id(context['motion'])
        if context['status'] != 'temporal_candidate' or identity is None:
            isolated[context['status'] if context['status'] != 'temporal_candidate' else 'non_numeric_or_loop_motion'] += 1
            continue
        events.append({'capture': capture, 'audio_row': r['row'], 'source_id': identity,
                       'event_id': event, 'kind': SOUND_NAMES.get(event, '未试听武器/公共候选'),
                       'banks': [s['bank'] for s in meta.get('sources', [])],
                       'native_frame_estimate': context['native_frame_estimate'],
                       'association': 'temporal_candidate_not_actor_or_exact_frame'})
    # Damage callbacks are never inferred from sounds or added to sound-context rows.
    damage = [r for r in rows['hits'] if r['kind'] == 'damage_side_callback']
    local = [r for r in damage if r['attacker_relation'] == 'master_gameobject_exact' and r['read_errors'] == 'none']
    combat = audit_rows(rows['hits'], rows['audio_native'], rows['audio_context'], rows['audio_objects'], nodes) if all(
        k in inputs for k in ('hits', 'audio_objects', 'audio_context')) else None
    joins = defaultdict(list)
    if combat:
        for r in combat['hit_audio']:
            if r['unique_hit_row'] is not None:
                joins[r['unique_hit_row']].append(r)
    hits = []
    for r in local:
        sample = by_sample.get(int(float(r['sample_index'])))
        if not sample:
            isolated['hit_sample_missing'] += 1
            continue
        context = positions.bracket(animation, times, float(sample['capture_time_s']))
        identity = source_id(context['motion'])
        if context['status'] != 'temporal_candidate' or identity is None:
            isolated['hit_clip_boundary_or_unmapped'] += 1
            continue
        hits.append({'capture': capture, 'hit_row': r['row'], 'source_id': identity,
                     'sample_index': int(float(r['sample_index'])), 'native_frame': float(sample['node_frame']),
                     'association': 'actual_callback_last_completed_animation_sample_not_GA_proof',
                     'exact_audio_joins': [{'post_row': j['post_row'], 'switches': j['switches'],
                                            'global_state_requested': j['global_state_requested']} for j in joins[r['row']]]})
    for i, e in enumerate(episodes):
        e.update(capture=capture, source_id=source_id(e['motion']),
                 previous_source_id=source_id(episodes[i-1]['motion']) if i else None)
    health = {'capture': capture, 'inputs': inputs, 'primary_samples': len(animation),
              'episodes': episodes, 'posts': len(rows['audio_native']),
              'damage_callbacks': len(damage), 'master_callbacks': len(local),
              'sound_post_snapshots': sum(r['kind'] == 'hit_sound_context_post' for r in rows['hits']),
              'object_gap_markers': sum('queue_gap' in r['semantics'] for r in rows['audio_objects']),
              'condition_gap_markers': sum(any(s in r['phase'] for s in ('gap', 'drop', 'barrier')) for r in rows['audio_context']),
              'normal_stop': any(r['event'] == 'capture_stopped' and r['details'] == 'hotkey F9' for r in rows['events']),
              'isolated_contexts': dict(isolated), 'combat_summary': combat['summary'] if combat else None}
    for item in inputs.values():
        if m1.sha256(Path(item['path'])) != item['sha256']:
            raise ValueError('Capture changed during coverage audit: '+item['path'])
    return health, observations, events, hits


def route_chain(montage, route, sequences):
    """Static prefix only; preserve the helper's cycle/dynamic-jump limitation."""
    result = []
    for lo, hi in route['intervals']:
        for seg in sorted(montage['segments'], key=lambda s: s['track_start_s']):
            if seg['effective_rate'] == 0:
                if None not in result: result.append(None)
                continue
            duration = (seg['source_end_s']-seg['source_start_s']) / abs(seg['effective_rate']) * seg['loops']
            if max(lo, seg['track_start_s']) >= min(hi, seg['track_start_s'] + duration) - 1e-6:
                continue
            ids = sequences[seg['sequence']]['import_numeric_ids']
            token = ids[0] if len(ids) == 1 else None
            if not result or result[-1] != token:
                result.append(token)
    return result


def chain_candidates(episodes_by_capture, chain, allowed_previous):
    if not chain or None in chain or not allowed_previous:
        return []
    matches = []
    for capture, episodes in episodes_by_capture.items():
        for i in range(1, len(episodes)-len(chain)+1):
            if [e['source_id'] for e in episodes[i:i+len(chain)]] == chain and episodes[i-1]['source_id'] in allowed_previous:
                matches.append({'capture': capture, 'episode': episodes[i]['episode'],
                                'previous_source_id': episodes[i-1]['source_id'], 'start_s': episodes[i]['start_s']})
    return matches


def build(snapshot, frozen_positions, authored, captures):
    sources, events_by_id, hits_by_id = defaultdict(list), defaultdict(list), defaultdict(list)
    for _, observations, events, hits in captures:
        for r in observations: sources[r['source_id']].append(r)
        for r in events: events_by_id[r['source_id']].append(r)
        for r in hits: hits_by_id[r['source_id']].append(r)
    montages = {m['path']: m for m in snapshot['montages']}
    abilities = {a['path']: a for a in frozen_positions['abilities']}
    primary_ids = {p: sorted({i for seg in montages[a['primary_montage']]['segments']
                             for i in snapshot['sequences'][seg['sequence']]['import_numeric_ids']})
                   for p, a in abilities.items() if a['primary_montage'] in montages}
    state_ids = defaultdict(set)
    for t in snapshot['combo_transitions']:
        p = t['ability_class'].removesuffix('_C')
        state_ids[t['target_state']].update(primary_ids.get(p, []))
    owners = defaultdict(set)
    for p, ids in primary_ids.items():
        for identity in ids: owners[identity].add(p)
    authored_by_path = {m['path']: m for m in authored['montages']}
    actions, routes = [], []
    episodes_by_capture = {h['capture']: h.get('episodes', []) for h, *_ in captures}
    for p, a in abilities.items():
        key = positions.short(p).removeprefix('GA_IG_')
        ids = primary_ids.get(p, [])
        missing = sorted(i for i in ids if not sources[i])
        shared = sorted(i for i in ids if len(owners[i]) > 1)
        hit_rows = [r for i in ids for r in hits_by_id[i]]
        strong = sum(bool(r['exact_audio_joins']) and r['source_id'] not in shared for r in hit_rows)
        condition_requests = defaultdict(Counter)
        for r in hit_rows:
            if r['source_id'] in shared: continue
            for j in r['exact_audio_joins']:
                for switch in j['switches']:
                    if switch['runtime_request_observed']:
                        condition_requests[str(switch['group_id'])][str(switch['requested_value'])] += 1
        wind = Counter(r['event_id'] for i in ids for r in events_by_id[i] if r['event_id'] in positions.SWINGS)
        aux = Counter(r['event_id'] for i in ids for r in events_by_id[i] if r['event_id'] not in positions.SWINGS and r['kind'] != '命中')
        task = '已有动作/候选，继续分析，不重录'
        if key in ACTION_NAMES:
            if key == 'JiXiTuCi': task = '世界动作：仅做项目共享适配，不向崛起索取原声'
            elif ids and not any(sources[i] for i in ids): task = '整招缺动作/声音记录；定点补空挥与命中'
            elif missing: task = '主体已有；补命中，入口缺段先核对'
            elif not strong: task = '已有动作/挥击；仅补新版命中归属证据'
        actions.append({'ability': p, 'name': ACTION_NAMES.get(key, key), 'scope': '攻击' if key in ACTION_NAMES else '支援/功能，仅盘点',
                        'primary_montage': a['primary_montage'], 'primary_source_ids': ids,
                        'owned_montages': a['montages'], 'missing_source_ids': missing, 'shared_source_ids': shared,
                        'swing_event_counts': dict(wind), 'other_event_counts': dict(aux),
                        'last_sample_master_callbacks': len(hit_rows), 'unique_hit_sound_contexts': strong,
                        'shared_clip_exact_sound_contexts': sum(bool(r['exact_audio_joins']) and r['source_id'] in shared for r in hit_rows),
                        'observed_switch_requests': {k: dict(v) for k,v in condition_requests.items()},
                        'authored_points': sum(len(authored_by_path.get(m, {}).get('authored', [])) for m in a['montages']),
                        'task': task})
        for entry in a.get('combo_entries', []):
            routing = entry.get('static_route')
            if not routing or a['primary_montage'] not in montages:
                continue
            chain = route_chain(montages[a['primary_montage']], routing, snapshot['sequences'])
            candidates = chain_candidates(episodes_by_capture, chain, state_ids.get(entry['source_state'], set()))
            routes.append({'ability': p, 'name': ACTION_NAMES.get(key,key), 'source_state': entry['source_state'],
                           'transition_id': entry['transition_id'], 'entry': entry['selected_entry'],
                           'static_route_status': routing['status'], 'source_chain': chain,
                           'missing_source_ids': [i for i in chain if i is not None and not sources[i]],
                           'adjacent_clip_candidates': candidates,
                           'proof': 'adjacent_dominant_animation_chain_only_not_input_or_Combo_commit'})
    source_report = []
    for identity in sorted(sources):
        samples = sources[identity]
        source_report.append({'source_id': identity, 'samples': len(samples),
            'native_frame_range': [min(r['frame'] for r in samples), max(r['frame'] for r in samples)] if samples else [],
            'captures': sorted({r['capture'] for r in samples}), 'events': events_by_id[identity],
            'damage_contexts': hits_by_id[identity], 'primary_ability_owners': sorted(owners[identity])})
    return {'schema': 'mhgz_existing_action_audio_coverage_v1', 'captures': [h for h,*_ in captures],
            'sources': source_report, 'actions': actions, 'entry_routes': routes,
            'summary': {'captures': len(captures), 'abilities': len(actions),
                'attack_abilities': sum(a['scope']=='攻击' for a in actions),
                'authored_montages': len(authored['montages']),
                'authored_points': sum(len(m['authored']) for m in authored['montages']),
                'independent_master_callbacks': sum(len(v) for v in hits_by_id.values()),
                'exact_hit_audio_contexts': sum(bool(r['exact_audio_joins']) for v in hits_by_id.values() for r in v)},
            'limits': ['Recorded clip context is not proof of a specific GA/Combo edge; shared clips are explicit.',
                'Swing mapping is temporal; boundary/clock ambiguities stay isolated, no exact Notify frame claim.',
                'Missing known swing event is not proof of silence or a request to re-record an observed clip.',
                'Declared bank defaults, pooled-emitter ambiguities and final Media remain unverified.',
                'An actual damage-side callback does not alone prove final HP loss.']}


def markdown(report):
    summary = report['summary']
    lines = ['# 虫棍逐招式音效覆盖与补录清单', '', '<!-- generated: map_swing_positions --audit-coverage -->', '',
             '更新：2026-10-19。仅盘点已有证据，不改UE/录制器，不覆盖已签收音源或旧配置。', '',
             f"范围：{summary['captures']}份原生音频录制，{summary['abilities']}个当前GA（{summary['attack_abilities']}个攻击），已接线{summary['authored_montages']}个Montage/{summary['authored_points']}个声音点。相关{report.get('verified_asset_packages','未核对')}包按M3回载哈希复核。", '',
             '## 1. 如何读表', '',
             '- “动作记录”只证明出现过源动画，不证明整个招式/所有入口录全。',
             '- 挥击为五种已确认风声的时间关联，不把尘土、落地、推进声当风声。',
             '- “伤害回调/精确声链”按主角色独立回调计，不把声音pre/post多算为伤害；源动画上下文仍不是GA提交证明。',
             '- 共用源片段的数据不自动归给两个GA；入口链只作相邻动画候选。最终Media未直接捕获。', '',
             '## 2. 攻击动作覆盖', '',
             '| 招式 | 主Montage源ID（缺项用★） | 已见挥击EventID | 伤害回调/精确声链上下文 | 已接声音点 | 当前处理 |',
             '|---|---|---|---:|---:|---|']
    for a in report['actions']:
        if a['scope'] != '攻击': continue
        ids = '/'.join(str(i)+('★' if i in a['missing_source_ids'] else '') for i in a['primary_source_ids']) or '非崛起源'
        sounds = '、'.join(map(str,a['swing_event_counts'])) or '未见这五种风声，不代表无声'
        lines.append(f"| {a['name']} | {ids} | {sounds} | {a['last_sample_master_callbacks']}/{a['unique_hit_sound_contexts']} | {a['authored_points']} | {a['task']} |")
    lines += ['', '## 3. 最小补充范围', '',
              '下表不是全套重录。只有整招都没有动作/声音记录的，才补空挥与命中；主体已有的只补新版命中证据。孤立入口编号缺失先核对，不要求玩家强行播放。', '',
              '| 招式 | 需要补什么 | 保留/限制 |', '|---|---|---|']
    for a in report['actions']:
        if a['scope'] != '攻击' or a['unique_hit_sound_contexts'] or a['name']=='急袭突刺': continue
        missing = a['missing_source_ids']
        ids = a['primary_source_ids']
        any_observed = any(s['samples'] for s in report['sources'] if s['source_id'] in ids)
        need = '空挥/完整执行及命中版' if not any_observed else '新版录制器下的木桩命中版'
        note = ('整招未观测；按正常流程执行' if not any_observed else '主体/已录挥击保留，不重录整套空挥')
        if missing: note += '；未见源'+','.join(map(str,missing))+'仅列对照缺口'
        if a['shared_source_ids']: note += '；共用源需分清入口'
        lines.append(f"| {a['name']} | {need} | {note} |")
    lines += ['', '已有完整链的招式不为这一步重复录制。急袭突刺是世界380/381，崛起只能提供明确标注的共享适配，不列崛起补录项。',
              '同一装备录制即可，可把多招放一份中，不需卡帧/毫秒。地面以项目使用的红灯强化版为主；觉虫击三灯/非三灯分开留样。突进回旋斩按原作普通流程，不录项目自定义蓄力。',
              '操虫/收虫/聚爆等支援动作只盘点；猎虫伤害、粉尘及精华交付音色不自动扩为本轮待办。', '',
              '## 4. 多入口的相邻动画证据', '',
              '这里只检查配置的静态链前缀及前一个源片段；不等于输入/Combo成功提交，也不因缺相邻链就要求重录已覆盖风声。Hold循环之后的动态跳转不按静态路径宣称完整。', '',
              '| 招式 | 入口 | 前置状态 | 静态源链 | 相邻链候选次数 | 未见源 |', '|---|---|---|---|---:|---|']
    for r in report['entry_routes']:
        if r['name'] not in ('上捞斩','飞圆斩','横扫斩','突进回旋斩','袈裟斩'): continue
        chain = '→'.join(str(i) if i is not None else '?' for i in r['source_chain'])
        lines.append(f"| {r['name']} | {r['entry']} | {r['source_state']} | {chain} | {len(r['adjacent_clip_candidates'])} | {','.join(map(str,r['missing_source_ids'])) or '无'} |")
    lines += ['', '## 5. 已录的其它音层与命中条件', '',
              '这些不是新增风声配置：已确认尘土/落地/推进仍分层，未试听候选不冒充武器音。命中条件只列同一发声对象的实际请求，不把银行默认填成实采。', '',
              '| 招式 | 其它已确认事件（次数） | 未试听候选事件 |', '|---|---|---|']
    for a in report['actions']:
        if a['scope']!='攻击' or not a['other_event_counts']: continue
        known = '；'.join(f"{SOUND_NAMES[e]} {e}×{n}" for e,n in a['other_event_counts'].items() if e in SOUND_NAMES)
        unknown = '、'.join(str(e) for e in a['other_event_counts'] if e not in SOUND_NAMES)
        lines.append(f"| {a['name']} | {known or '无'} | {unknown or '无'} |")
    lines += ['', '| 招式 | 同对象Switch组→请求值（次数） |', '|---|---|']
    for a in report['actions']:
        if not a['observed_switch_requests']: continue
        values = '；'.join(group+'→'+','.join(v+'×'+str(n) for v,n in choices.items()) for group,choices in a['observed_switch_requests'].items())
        lines.append(f"| {a['name']} | {values} |")
    lines += ['', '## 6. 录制质量与证据分级', '',
              '| 录制 | Post | 主角色伤害回调 | 后快照 | 对象gap | 正常F9结束 |', '|---|---:|---:|---:|---:|---|']
    for c in report['captures']:
        lines.append(f"| {c['capture']} | {c.get('posts',0)} | {c.get('master_callbacks',0)} | {c.get('sound_post_snapshots',0)} | {c.get('object_gap_markers',0)} | {'是' if c.get('normal_stop') else '未确认'} |")
    lines += ['', '旧版无命中侧车不表示未打中，只表示不能作为独立命中归属证据。有gap的对象链不强配；主角色伤害回调本身及旧挥击时间候选仍可使用。',
              '', '## 7. 下一步', '',
              '1. 先利用本表已有源/入口与EventID做映射，保留原24个Montage/70点。',
              '2. 按第3节定点补充，不需要重新测试录制器或录全套地面动作。',
              '3. 对仍无法确定的音色/默认分支提供有限试听，不声称已确认最终WEM。',
              '', '完整输入路径/哈希、隔离边界、逐源样本/事件/命中与入口候选见 `Saved/Audio/RiseIG/Coverage20261019/report.json`。', '']
    return '\n'.join(lines)


def run(directory, output, publish, source_root):
    output = capture_map.checked_output(output)
    capture_map.checked_inputs(source_root.resolve(), directory.resolve(), output)
    if (output / 'report.json').exists():
        raise FileExistsError('Preserve previous coverage report; select a new --output')
    root = m1.PROJECT
    snapshot_path = root / 'Saved/Audio/RiseIG/SwingMap/asset-snapshot.json'
    frozen_path = snapshot_path.with_name('positions.json')
    manifest_path = root / 'Saved/Audio/RiseIG/M3/manifest.json'
    audit_path = manifest_path.with_name('audit.json')
    load = lambda p: json.loads(p.read_text(encoding='utf-8-sig'))
    snapshot, frozen, manifest, authored = map(load, (snapshot_path, frozen_path, manifest_path, audit_path))
    if not snapshot['success'] or not snapshot['content_unchanged'] or not authored['linked'] or manifest['phase'] != 'independently_reloaded':
        raise ValueError('Require accepted read-only snapshot and independently reloaded M3')
    hashes = {**snapshot['protected_package_hashes'], **manifest['expected_hashes']}
    for path, sha in hashes.items():
        if m1.sha256(Path(path)) != sha:
            raise ValueError('Asset changed after accepted M3 snapshot: '+path)
    captures = sorted(p.name.removesuffix('_audio_native.csv') for p in directory.glob('mhrise_*_audio_native.csv'))
    if not captures or len(captures) > 32:
        raise ValueError('Expected1..32 explicit local audio captures')
    bank = source_root / 'Sound/Wwise/hit_pl.bnk.2.X64'
    if bank.stat().st_size > 4*1024*1024: raise ValueError('Hit metadata bank too large')
    nodes = capture_map.parse_sources([{'source': str(bank)}])
    metadata = {}
    metadata_inputs = {}
    for path in sorted((root / 'Saved/Audio/RiseIG/CaptureMap').glob('*/map.json')):
        metadata_inputs[str(path)] = m1.sha256(path)
        for row in load(path)['events']:
            metadata[row['event_id']] = row
    analysed = [analyse_capture(directory,c,nodes,metadata) for c in captures]
    report = build(snapshot,frozen,authored,analysed)
    report['verified_asset_packages'] = len(hashes)
    report['asset_evidence'] = {str(p): m1.sha256(p) for p in (snapshot_path,frozen_path,manifest_path,audit_path)}
    report['bank'] = {'path': str(bank), 'sha256': m1.sha256(bank)}
    report['event_metadata_inputs'] = metadata_inputs
    text = markdown(report)
    target = root / 'docs/reference/虫棍逐招式音效覆盖与补录清单.md'
    if publish and target.exists() and '<!-- generated: map_swing_positions --audit-coverage -->' not in target.read_text(encoding='utf-8'):
        raise ValueError('Do not overwrite an unowned reference document')
    m1.write_json(output/'report.json',report)
    (output/'report.md').write_text(text,encoding='utf-8')
    if publish: target.write_text(text,encoding='utf-8')
    print(json.dumps({'summary': report['summary'], 'verified_packages': len(hashes),
                      'report': str(output/'report.md'), 'published': publish},ensure_ascii=False))
