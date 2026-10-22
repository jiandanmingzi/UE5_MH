"""Current-weapon mode of map_swing_positions. No UE/game/install writes.

Reuse resident capture/wwiser/TXTP and segment-time helpers. The missing piece
is actor-scoped conditions plus branch-level exclusions, not another decoder.
Only the explicitly selected primary recording supplies sound/timing evidence.
"""
from collections import Counter, defaultdict
import bisect
import json
from pathlib import Path
import re
import statistics
import sys

import map_swing_positions as positions
import rise_audio_capture_map as captures
import rise_audio_m1 as m1
from rise_audio_combat import pointer
from rise_audio_coverage import read_rows

BANKS = {'pl_wp_i_gla_com.bnk': '武器层', 'pl_wp_com.bnk': '通用武器层',
         'pl_prop.bnk': '地面/动作附加层', 'pl_equip.bnk': '装备动作层',
         'pl_wp_ig_ins_com.bnk': '猎虫运行时层', 'hit_pl.bnk': '真实命中运行时层',
         'buff.bnk': '精华/增益运行时层'}
MOTION = re.compile(r'^plw_InsectGlaive_100_(\d+)(?:_Loop)?$')
TOKEN = re.compile(r'^(\[|\()(\d+)=(\d+)(?:\]|\))$')
PREFIX = '<!-- generated: map_swing_positions --map-current-weapon -->'


def load(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def excluded(event, switches, role, review):
    if any(int(r['event_id']) == event for r in review.get('confirmed_exclusions', [])):
        return 'confirmed_event_exclusion'
    for rule in review.get('confirmed_branch_exclusions', []):
        condition = rule.get('switch_conditions', {})
        if int(rule['event_id']) == event and rule.get('capture_role') == role and condition and all(
                switches.get(str(g)) == str(v) for g, v in condition.items()):
            return 'confirmed_branch_exclusion'
    # Unnamed/pending sounds are NOT an exclusion or a listening gate.
    return None


def condition_match(variant, switches, states):
    for text in variant['conditions']:
        text = text.removeprefix('{s}=')
        token = TOKEN.fullmatch(text)
        if token is None:
            return False
        kind, group, value = token.groups()
        if (switches if kind == '[' else states).get(group) != value:
            return False
    return True


def graph_key(variant):
    # Preserve gains/delays/random grouping. Normalize only leaf directory names.
    lines = [re.sub(r'[^\s]*[/\\]([0-9]+\.wem)', r'\1', line.strip())
             for line in variant['render_lines']]
    return (tuple(variant['media_ids']), tuple(lines))


def choose_variant(variants, switches, states):
    matched = [v for v in variants if condition_match(v, switches, states)]
    groups = defaultdict(list)
    for v in matched:
        groups[graph_key(v)].append(v)
    if len(groups) != 1:
        return None, 'missing_observed_conditions_or_branch' if not groups else 'multiple_distinct_graphs'
    chosen = next(iter(groups.values()))[0]
    if not chosen['media_ids']:
        return None, 'matched_graph_has_no_media_not_audible_proof'
    return chosen, 'observed_request_matched_candidate_not_final_media'


def scoped_posts(contexts, objects, posts, master):
    """Exact Wwise ID/engine pointer/owner edges, never nearest-time ownership.

    Gaps invalidate histories prospectively; later exact setters restore only
    their own keys. Reused pointers with multiple owners remain ambiguous.
    """
    switches, states, keys, owners = {}, {}, defaultdict(set), defaultdict(set)
    stream = [(int(r['qpc']), 0, r) for r in contexts if r.get('qpc')]
    stream += [(int(r['qpc']), 1, r) for r in objects if r.get('qpc')]
    stream += [(int(r['qpc']), 2, r) for r in posts if r.get('qpc')]
    result = []
    for _, source, r in sorted(stream, key=lambda v: (v[0], v[1])):
        go, kind = r.get('game_object_id', ''), r['kind']
        if source == 0:
            if kind in ('register_requested', 'unregister_requested'):
                switches = {k: v for k, v in switches.items() if go != '18446744073709551615' and k[0] != go}
            if any(t in r.get('phase', '') for t in ('gap', 'loss', 'drop', 'barrier')):
                switches.clear(); states.clear()
            if kind == 'switch_requested':
                switches[(go, r['group_or_parameter_id'])] = r['value_id']
            elif kind == 'state_requested':
                states[r['group_or_parameter_id']] = r['value_id']
        elif source == 1:
            if kind == 'object_key_return':
                address = pointer(r['engine_gameobject_address'])
                if address is not None:
                    keys[go].add(address)
            elif kind == 'container_trigger_context':
                address, owner = pointer(r['engine_gameobject_address']), pointer(r['owner_gameobject_address'])
                if address is not None and owner is not None:
                    owners[address].add(owner)
            elif kind == 'unregister_requested':
                keys.pop(go, None)
            if any(t in r.get('semantics', '') for t in ('gap', 'loss', 'drop', 'barrier')):
                keys.clear(); owners.clear()
        else:
            candidates = keys.get(go, set())
            emitter = next(iter(candidates)) if len(candidates) == 1 else None
            owner_set = owners.get(emitter, set())
            if emitter == master and master is not None:
                relation = 'master_emitter_exact'
            elif master is not None and owner_set == {master}:
                relation = 'unique_native_owner_is_master'
            elif emitter is not None and len(owner_set) == 1:
                relation = 'other_owner_exact'
            else:
                relation = 'owner_unresolved_or_reused'
            result.append({**r, 'switches': {g: v for (obj, g), v in switches.items() if obj == go},
                           'states': dict(states), 'owner_relation': relation})
    return result


def cluster_events(events, span=2.5):
    grouped = defaultdict(list)
    for e in events:
        grouped[(e['motion'], e['event_id'], e['branch_id'])].append(e)
    result = []
    for (motion, event, branch), values in sorted(grouped.items()):
        buckets = []
        for e in sorted(values, key=lambda v: v['native_frame_estimate']):
            bucket = next((b for b in reversed(buckets)
                           if e['native_frame_estimate'] - b[0]['native_frame_estimate'] <= span
                           and e['episode'] not in {x['episode'] for x in b}), None)
            if bucket is None:
                buckets.append([e])
            else:
                bucket.append(e)
        for i, bucket in enumerate(buckets):
            frames = [e['native_frame_estimate'] for e in bucket]
            native = MOTION.fullmatch(motion)
            result.append({'id': f'{motion}:{event}:{branch}:{i}', 'motion': motion,
                'source_id': int(native[1]) if native else None, 'event_id': event, 'branch_id': branch,
                'layer': bucket[0]['layer'], 'native_frame_estimate': statistics.median(frames),
                'native_frame_range': [min(frames), max(frames)],
                'native_end_frames': sorted({e['native_end_frame'] for e in bucket}),
                'observations': len(bucket), 'audio_rows': [e['audio_row'] for e in bucket]})
    return result


def hunter_fx_posts(contexts, objects, posts, master, event_ids):
    """Narrow opt-in for confirmed FX sounds, not a relaxation of scoped_posts.

    Exact Wwise key -> engine object -> trigger in the SAME native Present
    frame, through a container also directly observed on the master hunter.
    Pool pointers alone, a trigger's numeric ID, or nearby wall time do not link.
    """
    triggers = [r for r in objects if r['kind'] == 'container_trigger_context']
    containers = {r['container_address'] for r in triggers
                  if pointer(r['engine_gameobject_address']) == master
                  and pointer(r['owner_gameobject_address']) == master}
    containers -= {r['container_address'] for r in triggers
                   if pointer(r['owner_gameobject_address']) not in (None, master)
                   and pointer(r['owner_gameobject_address']) != pointer(r['engine_gameobject_address'])}
    keys, trigger_by_object, lifecycle = defaultdict(list), defaultdict(list), defaultdict(list)
    barriers = []
    for r in objects:
        if r['kind'] == 'object_key_return':
            keys[r['game_object_id']].append(r)
        elif r['kind'] == 'container_trigger_context':
            trigger_by_object[pointer(r['engine_gameobject_address'])].append(r)
    for r in [*contexts, *objects]:
        if r['kind'] in ('register_requested', 'unregister_requested'):
            lifecycle[r.get('game_object_id', '')].append(r)
        if any(t in (r.get('phase', '') + r.get('semantics', '')) for t in ('gap', 'loss', 'drop', 'barrier')):
            barriers.append(int(r['qpc']))
    for group in (keys, trigger_by_object):
        for records in group.values():
            records.sort(key=lambda r: int(r['qpc']))
    linked, isolated = [], []
    for p in posts:
        if int(p['event_id']) not in event_ids:
            continue
        qpc = int(p['qpc'])
        boundary = max([0, *(q for q in barriers if q <= qpc)])
        life = [r for r in lifecycle[p['game_object_id']] if int(r['qpc']) <= qpc]
        key_boundary = max([boundary, *(int(r['qpc']) for r in life if r['kind'] == 'unregister_requested')])
        possible_keys = [r for r in keys[p['game_object_id']] if key_boundary < int(r['qpc']) <= qpc]
        key = possible_keys[-1] if possible_keys else None
        registrations = [r for r in life if r['kind'] == 'register_requested']
        registration = max(registrations, key=lambda r: int(r['qpc'])) if registrations else None
        # The native constructor obtains its key BEFORE registering that ID.
        # Admit that order only with exact same-frame evidence, never a stale
        # key from an earlier frame or from before an unregister/gap.
        fresh_registration = (not registration or not key or int(registration['qpc']) < int(key['qpc'])
            or (registration.get('native_present_frame') == p['native_present_frame']
                and key.get('native_present_frame') == p['native_present_frame']))
        # The enclosing trigger may CREATE/register the transient Wwise object.
        # Registration invalidates old key bindings, not that same-frame caller.
        possible_triggers = [r for r in trigger_by_object[pointer(key['engine_gameobject_address'])]
                             if key_boundary < int(r['qpc']) <= qpc] if key else []
        trigger = possible_triggers[-1] if possible_triggers else None
        if (fresh_registration and trigger and trigger['container_address'] in containers
                and trigger['native_present_frame'] == p['native_present_frame']
                and pointer(trigger['owner_gameobject_address']) == pointer(trigger['engine_gameobject_address'])):
            linked.append(dict(p, fx_evidence=dict(key_row=key['row'], trigger_row=trigger['row'],
                container=trigger['container_address'], engine_object=trigger['engine_gameobject_address'],
                native_present_frame=trigger['native_present_frame'],
                status='exact_hunter_container_same_native_frame_not_final_voice')))
        else:
            isolated.append(dict(audio_row=p['row'], reason='no_exact_same_frame_hunter_fx_chain'))
    return linked, isolated


def end_frame_hold_positions(segment, sequence, cluster, margin=1/60):
    """Explicit project seam adaptation, not a claim of exact original Notify time.

    Native last-frame HOLD [N-1,N) cannot be mapped beyond the last FBX key.
    Only repeated wholly-in-that-bin observations on a fully retained source
    tail qualify. Schedule just before that part ends so its lease still exists.
    """
    calibration = positions.frame_calibration(sequence, cluster)
    if not calibration or cluster['observations'] < 2:
        return []
    end_frame = cluster['native_end_frames'][0]
    lo, hi = cluster['native_frame_range']
    if not end_frame-1 <= lo <= hi < end_frame:
        return []
    start, end, rate = segment['source_start_s'], segment['source_end_s'], segment['effective_rate']
    if rate <= 0 or abs(end-sequence['length_s']) > 1e-5:
        return []
    duration = (end-start)/rate
    if duration <= margin:
        return []
    return [dict(cluster_id=cluster['id'], event_id=cluster['event_id'], segment_index=segment['index'],
        slot=segment['slot'], loop=loop, sequence=segment['sequence'], source_id=cluster['source_id'],
        source_time_estimate_s=end, montage_time_estimate_s=segment['track_start_s']+(loop+1)*duration-margin,
        cut_boundary_ambiguous=False, calibration=calibration, observations=cluster['observations'],
        branch_id=cluster['branch_id'], layer=cluster['layer'],
        placement_policy='project_segment_end_minus_one_display_frame_for_native_end_hold',
        original_native_frame_range=cluster['native_frame_range']) for loop in range(segment['loops'])]


def transition_precursor_posts(animation, posts, target_source_id, max_lead_frames=2):
    """Narrow transition evidence, not nearest-time ownership or a global remap.

    Input posts must already have independently verified emitter ownership. A
    unique request at/just before a fresh player00 source entry is retained only
    when every intervening native render frame was actually sampled.
    """
    rows = captures.dominant_motion_rows(animation)
    by_frame = {int(r['render_callback']): r for r in rows}
    result = []
    previous = None
    for row in rows:
        match = MOTION.fullmatch(row['motion_name'])
        target = bool(match and int(match[1]) == target_source_id)
        if (target and previous and previous['motion_name'] != row['motion_name']
                and 0 <= float(row['node_frame']) <= 1):
            frame = int(row['render_callback'])
            candidates = [p for p in posts
                          if 0 <= frame-int(p['native_present_frame']) <= max_lead_frames]
            if len(candidates) == 1:
                p = candidates[0]
                start = int(p['native_present_frame'])
                if all(f in by_frame for f in range(start, frame+1)):
                    result.append(dict(audio_row=p['row'], entry_sample=row['sample_index'],
                        entry_render_frame=frame, lead_render_frames=frame-start,
                        entry_native_frame=float(row['node_frame']), target_source_id=target_source_id,
                        previous_motion=previous['motion_name']))
        previous = row
    return result


def map_montages(snapshot, clusters):
    sequences = json.loads(json.dumps(snapshot['sequences']))
    # Female filename identity still needs its own frame-extent check. Never
    # equate plc_Field IDs with the same-numbered InsectGlaive IDs.
    for seq in sequences.values():
        if not seq['import_numeric_ids']:
            ids = {int(m[1]) for s in seq['import_sources']
                   if (m := re.fullmatch(r'(?:female|male)(\d+)\.fbx', Path(s).name, re.I))}
            seq['import_numeric_ids'] = sorted(ids)
    result = []
    for montage in snapshot['montages']:
        mapped = []
        for seg in montage['segments']:
            seq = sequences[seg['sequence']]
            for c in clusters:
                if c['source_id'] is None or c['layer'] in ('真实命中运行时层', '猎虫运行时层', '精华/增益运行时层'):
                    continue
                for position in positions.segment_positions(seg, seq, c):
                    position.update(branch_id=c['branch_id'], layer=c['layer'], observations=c['observations'])
                    mapped.append(position)
        result.append({'path': montage['path'], 'candidates': sorted(mapped,
                       key=lambda p: (p['montage_time_estimate_s'], p['event_id'], p['branch_id']))})
    return result


def generate_variants(source, output, metadata, events):
    copies, index = {}, defaultdict(list)
    wanted = sorted({int(e['event_id']) for e in events})
    banks = sorted({s['bank'] for e in metadata['events'] if e['event_id'] in wanted for s in e['sources'] if s['bank'] in BANKS})
    for name in ['Init.bnk', *banks]:
        captures.copy_verified(source/'Sound/Wwise'/(name+'.2.X64'), output/'Banks'/name, copies)
    nodes = captures.parse_sources([{'source':str(source/'Sound/Wwise'/(b+'.2.X64'))} for b in banks])
    # Actual observed tuples, not a Cartesian product of all possible surfaces,
    # armour, parts and weapon models. '/' is wwiser's documented combo separator.
    event_banks = {e['event_id']:{s['bank'] for s in e['sources']} for e in metadata['events']}
    for bank in banks:
        bank_events = [e for e in events if bank in event_banks[e['event_id']]]
        groups = {str(n['switch_container']['group_id']) for n in nodes.values()
                  if n['bank']==bank and n.get('switch_container',{}).get('group_type')==0}
        signatures = observed_signatures(bank_events,groups)
        if len(signatures)>64:
            raise ValueError('More than64 observed bank condition tuples; split explicitly')
        params = []
        for signature in sorted(signatures):
            if params: params.append('/')
            params.extend(f'[{g}={v}]' if kind=='sw' else f'({g}={v})' for kind,g,v in signature)
        args = [sys.executable,str(captures.WORK/'Tools/wwiser-v20260808/wwiser.py'),'Init.bnk',bank,
                '-d','none','-g','-gd','-go','../Branches/'+bank.removesuffix('.bnk'),
                '-gf',*map(str,sorted({e['event_id'] for e in bank_events}))]
        if params: args += ['-gp',*params]
        if sum(len(a)+3 for a in args)>28000:
            raise ValueError('Condition command exceeds explicit Windows bound')
        m1.run_logged(args,output/(bank+'.generation.log'),cwd=output/'Banks')
    paths = sorted((output/'Branches').rglob('*.txtp'))
    if len(paths) > 4096:
        raise ValueError('Generated metadata exceeds4096 variants; narrow conditions before continuing')
    for path in paths:
        v = m1.describe_txtp(path, {})
        if v['event_id'] in wanted:
            index[v['event_id']].append(v)
    return index, copies


def observed_signatures(events,groups):
    return {tuple(sorted(('sw',g,e['switches'][g]) for g in groups if g in e['switches']))+
            tuple(sorted(('st',g,v) for g,v in e['states'].items())) for e in events}


def markdown(report):
    s = report['summary']
    lines = ['# 当前武器逐动作音效映射', '', PREFIX, '',
        '更新：2026-10-19。仅使用明确指定的当前武器主录制，不借旧录制或Control音色补项。未改UE资产。', '',
        '**不要求逐音效命名验收。** 已确认不要的声音才排除；其它声音按实际条件与时序映射。', '',
        f'录制：`{report["capture"]}`；主角色归属明确的相关请求{s["owned_posts"]}条；确认排除{s["excluded_posts"]}条；',
        f'源动画内条件匹配{s["timed_posts"]}条，形成{s["clusters"]}组候选；{s["branches"]}个音源图；',
        f'{s["montages_with_positions"]}个Montage共{s["montage_positions"]}个时间候选。边界/缺条件单列，不猜填。', '',
        '## 1. 固定边界', '',
        '- 排除1942118895刀尖机构层；938078130仅Control条件分支排除，Primary保留。',
        '- 1364862806/1816305690用途未命名不是阻塞或自动排除依据。',
        '- 使用同一Wwise对象上的实际Switch/全局State请求；条件缺口后清空，后续重新采到的setter才恢复。',
        '- 银行条件匹配只定位候选音源图，不冒充最终PlayingID、随机叶子或实际可听成功。',
        '- 命中/猎虫/精华是运行时层，不把伤害音直接烘成攻击Montage通知。',
        '- Stop请求单独记录生命周期，不作为缺失WAV或播放点；本轮不解码新音频、不生成Notify。',
        '- 姿态首尾、循环重置、未知时钟延迟都保留限制；时间不是原作精确Notify帧真值。', '',
        '## 2. 音源图目录', '', '| 分支 | 事件 | 音层 | 条件 | 候选Media |', '|---|---|---|---|---|']
    for b in report['branches']:
        lines.append(f'| {b["id"]} | {b["event_id"]} | {b["layer"]} | {" ".join(b["conditions"]) or "无选择条件"} | {"/".join(map(str,b["media_ids"])) or "无Media"} |')
    lines += ['', '## 3. Montage时间候选', '',
              '按每条序列的导入身份及帧长度校核后换算；裁切范围外不补播。循环/取消与最终Notify生成尚未实施。', '',
              '| Montage | 时间候选（秒：事件@分支） |', '|---|---|']
    for m in report['montages']:
        if m['candidates']:
            short = positions.short(m['path'])
            text = '；'.join(f'{p["montage_time_estimate_s"]:.3f}：{p["event_id"]}@{p["branch_id"]}'
                            + ('（切边界待核）' if p['cut_boundary_ambiguous'] else '') for p in m['candidates'])
            lines.append(f'| {short} | {text} |')
    lines += ['', '## 4. 功能动作与Loop的源动画时机', '',
              '以下非攻击源只输出实际动画位置，不因名字相似强行换算到MM/收刀序列；完整攻击源点位在JSON。', '',
              '| 原作主节点 | 原生帧估计：事件@分支（观察次数） |', '|---|---|']
    grouped = defaultdict(list)
    for c in report['clusters']:
        if c['source_id'] is None or c['source_id'] < 100:
            grouped[c['motion']].append(c)
    for motion, clusters in sorted(grouped.items()):
        text = '；'.join(f'{c["native_frame_estimate"]:.2f}：{c["event_id"]}@{c["branch_id"]}（{c["observations"]}）' for c in clusters)
        lines.append(f'| {motion} | {text} |')
    lines += ['', '## 5. 暂缺与隔离', '',
        '| 原因 | 请求条数 |', '|---|---:|']
    for reason, count in sorted(report['isolated_counts'].items()):
        lines.append(f'| {reason} | {count} |')
    lines += ['', '| 未决事件/实际源节点 | 原因 | 次数 |', '|---|---|---:|']
    for item in report['unresolved_groups']:
        lines.append(f'| {item["event_id"]} / {item["motion"] or "无可靠源节点"} | {item["reason"]} | {item["count"]} |')
    lines += ['', '## 6. 仍需技术核对的范围', '',
        '- 操虫斩188、无白灯前撑杆跳141未在主录制出现；不能借Control样本算作当前武器覆盖，不要求立即重录。',
        '- 普通移动/Loop及female收刀段保留实际源节点时机；不能把Field与武器的同尾号动画视为同一序列。',
        '- 当前Montage点位仍需实际Section入口/取消/循环路由筛选；世界急袭突刺不从崛起猜造原声。',
        '- 对象复用、缺少请求条件或姿态切换边界留未知；不按最近时间归属或借默认音色补齐。',
        f'- 单独保留{len(report["control_requests"])}条Stop/非播放控制请求，实际接入时走生命周期而非播放Notify。']
    lines += ['', '本表不要求整套重录或逐条试听命名。先用已匹配部分推进，只有影响实际接入的缺口再单独处理。',
              f'完整逐源位置、音源图与证据路径：`{report["local_report"]}`。', '']
    return '\n'.join(lines)


def run(directory, capture, evidence, output, source, publish=False):
    output = captures.checked_output(output)
    captures.checked_inputs(source, directory, output)
    if (output/'mapping.json').exists():
        raise FileExistsError('Preserve completed map; choose a new output')
    review_path, metadata_path = evidence/'user-review.json', evidence/capture/'map.json'
    review, metadata = load(review_path), load(metadata_path)
    if metadata['capture'] != capture or not re.fullmatch(r'mhrise_\d{8}_\d{6}_\d+', capture):
        raise ValueError('Explicit current capture/map identity mismatch')
    primary = next((r['capture'] for r in review.get('pending_branches', []) if r.get('capture_role') == 'Primary'), None)
    if primary != capture or review.get('event_id_only_exclusion_for_938078130') is not False:
        raise ValueError('Require reviewed Primary identity and branch-only938 exclusion')
    paths = {k: directory/f'{capture}_{k}.csv' for k in ('animation','audio_native','audio_context','audio_objects','hits')}
    fingerprints = {str(p): m1.sha256(p) for p in [*paths.values(), review_path, metadata_path]}
    for v in metadata['input_files'].values():
        if m1.sha256(Path(v['path'])) != v['sha256']:
            raise ValueError('Recorded inputs changed after initial map')
    data = {k: read_rows(p) for k,p in paths.items()}
    masters = {pointer(r['attacker_address']) for r in data['hits'] if r['kind']=='damage_side_callback' and r['attacker_relation']=='master_gameobject_exact'}
    if len(masters) != 1:
        raise ValueError('Need unique observed main hunter identity')
    events = scoped_posts(data['audio_context'],data['audio_objects'],data['audio_native'],next(iter(masters)))
    event_info = {e['event_id']: e for e in metadata['events']}
    isolated, selected, runtime, owned, controls = Counter(), [], [], 0, []
    for e in events:
        eid = int(e['event_id'])
        banks = [s['bank'] for s in event_info[eid]['sources'] if s['bank'] in BANKS]
        if not banks:
            continue
        reason = excluded(eid,e['switches'],'Primary',review)
        if reason:
            isolated[reason] += 1
            continue
        if not event_info[eid]['has_play_action']:
            controls.append({'event_id':eid,'audio_row':e['row'],'owner_relation':e['owner_relation'],
                'actions':[a for s in event_info[eid]['sources'] for a in s['actions']],
                'purpose':'recorded_control_request_not_an_audio_play_or_missing_wave'})
            continue
        layer = BANKS[banks[0]] if len(banks)==1 else None
        is_runtime = layer in ('真实命中运行时层','猎虫运行时层','精华/增益运行时层')
        if e['owner_relation'] not in ('master_emitter_exact','unique_native_owner_is_master') and not is_runtime:
            isolated[e['owner_relation']] += 1
            runtime.append(e)
            continue
        owned += e['owner_relation'] in ('master_emitter_exact','unique_native_owner_is_master')
        if len(banks) != 1:
            isolated['multiple_banks_for_event'] += 1
            continue
        selected.append({**e,'event_id':eid,'layer':BANKS[banks[0]]})
    if not selected or len({e['event_id'] for e in selected}) > 64:
        raise ValueError('Expected1..64 relevant observed events')
    variants, copies = generate_variants(source, output, metadata, selected)
    animation = captures.dominant_motion_rows(data['animation'])
    episodes = captures.motion_episodes(animation,{})
    starts = [e['start_s'] for e in episodes]
    for r in animation:
        r['_episode'] = bisect.bisect_right(starts,float(r['capture_time_s']))
    times = [float(r['capture_time_s']) for r in animation]
    branches, branch_ids, timed, runtime_mapped, unresolved = [], {}, [], [], []
    for e in selected:
        v, status = choose_variant(variants[e['event_id']],e['switches'],e['states'])
        if v is None:
            isolated[status] += 1
            ctx = positions.bracket_audio(animation,times,e)
            unresolved.append((e['event_id'],ctx['motion'],status))
            continue
        key = (e['event_id'], graph_key(v))
        if key not in branch_ids:
            identity = f'B{len(branches)+1:03}'
            branch_ids[key] = identity
            branches.append({'id':identity,'event_id':e['event_id'],'layer':e['layer'],
                'conditions':v['conditions'],'media_ids':v['media_ids'],'txtp':v['file'],
                'txtp_sha256':m1.sha256(Path(v['file'])),'render_lines':v['render_lines'],
                'selection_status':status,'runtime_final_media':'unknown'})
        identity = branch_ids[key]
        if e['layer'] in ('真实命中运行时层','猎虫运行时层','精华/增益运行时层'):
            runtime_mapped.append({'audio_row':e['row'],'event_id':e['event_id'],'branch_id':identity,
                                   'layer':e['layer'],'owner_relation':e['owner_relation']})
            continue
        context = positions.bracket_audio(animation,times,e)
        if context['status'] != 'temporal_candidate':
            isolated[context['status']] += 1
            continue
        timed.append({**context,'event_id':e['event_id'],'branch_id':identity,'layer':e['layer'],'audio_row':e['row']})
    clusters = cluster_events(timed)
    unresolved_counts = Counter(unresolved)
    snapshot_path = m1.PROJECT/'Saved/Audio/RiseIG/SwingMap/asset-snapshot.json'
    snapshot = load(snapshot_path)
    manifest = load(m1.PROJECT/'Saved/Audio/RiseIG/M3/manifest.json')
    protected = {**snapshot['protected_package_hashes'],**manifest['expected_hashes']}
    if any(m1.sha256(Path(p)) != sha for p,sha in protected.items()):
        raise ValueError('Asset snapshot stale; no automatic time publication')
    montages = map_montages(snapshot,clusters)
    for p,sha in fingerprints.items():
        if m1.sha256(Path(p)) != sha:
            raise ValueError('Input changed during mapping: '+p)
    for copied in copies.values():
        if m1.sha256(Path(copied['source'])) != copied['sha256']:
            raise ValueError('Source bank changed during mapping')
    report = {'schema':'mhgz_current_weapon_motion_mapping_v1','capture':capture,'role':'Primary',
        'inputs':fingerprints,'review':review,'asset_snapshot_sha256':m1.sha256(snapshot_path),
        'protected_packages':len(protected),'local_report':str(output/'mapping.json'),
        'summary':{'owned_posts':owned,
            'excluded_posts':isolated['confirmed_event_exclusion']+isolated['confirmed_branch_exclusion'],
            'timed_posts':len(timed),'clusters':len(clusters),'branches':len(branches),
            'montages_with_positions':sum(bool(m['candidates']) for m in montages),
            'montage_positions':sum(len(m['candidates']) for m in montages)},
        'branches':branches,'clusters':clusters,'montages':montages,'runtime_layers':runtime_mapped,
        'control_requests':controls,'unresolved_groups':[{'event_id':eid,'motion':motion,'reason':reason,'count':count}
            for (eid,motion,reason),count in sorted(unresolved_counts.items(),key=lambda x:(x[0][0],str(x[0][1]),x[0][2]))],
        'unattributed_or_other_owner_candidates':len(runtime),'isolated_counts':dict(isolated),
        'ue_modified':False,'final_random_voice_observed':False,'user_naming_gate':False}
    m1.write_json(output/'mapping.json',report)
    page = markdown(report)
    (output/'mapping.md').write_text(page,encoding='utf-8')
    if publish:
        target = m1.PROJECT/'docs/reference/当前武器逐动作音效映射.md'
        if target.exists() and PREFIX not in target.read_text(encoding='utf-8-sig'):
            raise ValueError('Refusing to overwrite unrelated reference')
        target.write_text(page,encoding='utf-8')
        compact = {k:v for k,v in report.items() if k not in ('inputs','runtime_layers')}
        m1.write_json(target.with_suffix('.json'),compact)
    print(json.dumps({'summary':report['summary'],'isolated':dict(isolated),'report':str(output/'mapping.json')},ensure_ascii=False))
