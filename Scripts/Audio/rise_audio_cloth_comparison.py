"""Armor comparison mode of map_swing_positions; reuse ownership, clocks and wwiser.

No decoding, game changes or UE writes. A silent plugin may have a source ID:
Media-ID presence alone is not evidence of audible armor in the naked control.
"""
from collections import Counter
import bisect
from pathlib import Path
import re

import rise_audio_weapon_mapping as mapping
from rise_audio_coverage import read_rows
import rise_audio_m1 as m1

OWNED = {'master_emitter_exact', 'unique_native_owner_is_master'}
POWDER_CANDIDATES = {
    615339279: '普通粉尘爆炸基础层候选；聚爆引爆时也出现',
    647565313: '聚爆引爆附加层候选',
    18325887: '聚爆聚集开始层候选A',
    1714085883: '聚爆聚集开始层候选B',
}


def aerial_montage(name):
    """Bound the write set; a ground clip borrowed by an air action is not enough."""
    return bool(re.fullmatch(r'AM_IG_(?:(?:Qian|Hou|Zuo|You)ChengGanTiao(?:_W)?(?:_Over)?'
        r'|AirDodge|AerialFall(?:_W)?|AerialLanding|CaoChongZhan(?:_Dance)?'
        r'|JueChongJi(?:_Fall)?|QiangHuaCaoChongChuanCi(?:_Over)?)', name))


def powder_hit_neighbors(hits, posts):
    """Keep repeated co-occurrence separate from exact attacker/emitter ownership."""
    return [dict(hit_row=h['row'], native_frame=int(float(h['frame_callback'])),
        attacker=h['attacker_address'],
        audio=[dict(audio_row=p['row'], event_id=int(p['event_id']),
                    frame_delta=int(p['native_present_frame'])-int(float(h['frame_callback'])))
               for p in posts if int(p['event_id']) in POWDER_CANDIDATES
               and abs(int(p['native_present_frame'])-int(float(h['frame_callback']))) <= 2])
        for h in hits if h['kind'] == 'damage_side_callback' and h['attacker_name'] == 'bomb']


def plugin_silence(variant):
    lines = [line.strip() for line in variant['render_lines'] if line.strip()]
    return bool(lines) and all(line.startswith('?.plugin-silence ') for line in lines)


def paired_windows(original, control, span=2.5):
    """Mutual unique source/event/frame matches; no cross-clip or nearest-time borrowing."""
    def matches(a, b):
        return ((a['motion'], a['event_id']) == (b['motion'], b['event_id'])
                and a['native_end_frames'] == b['native_end_frames']
                and abs(a['native_frame_estimate'] - b['native_frame_estimate']) <= span)
    result = []
    for a in original:
        candidates = [b for b in control if matches(a, b)]
        if len(candidates) != 1:
            continue
        b = candidates[0]
        if min(a['observations'], b['observations']) < 2:
            continue
        if sum(matches(o, b) for o in original) != 1:
            continue
        result.append(dict(a, control_cluster=b))
    return result


def capture_rows(directory, capture):
    paths = {k: directory / f'{capture}_{k}.csv' for k in
             ('animation', 'audio_native', 'audio_context', 'audio_objects', 'hits', 'events')}
    inputs = {str(p): m1.sha256(p) for p in paths.values()}
    rows = {k: read_rows(p) for k, p in paths.items()}
    masters = {mapping.pointer(r['attacker_address']) for r in rows['hits']
               if r['kind'] == 'damage_side_callback' and r['attacker_relation'] == 'master_gameobject_exact'}
    if len(masters) != 1 or None in masters:
        raise ValueError('Comparison requires one exact hunter identity: ' + capture)
    stops = [r for r in rows['events'] if r['event'] == 'capture_stopped']
    if not stops or stops[-1]['details'] != 'hotkey F9':
        raise ValueError('Comparison capture was not normally saved: ' + capture)
    gaps = sum(any(t in r.get(field, '') for t in ('gap', 'loss', 'drop', 'barrier'))
               for key, field in (('audio_context', 'phase'), ('audio_objects', 'semantics')) for r in rows[key])
    posts = mapping.scoped_posts(rows['audio_context'], rows['audio_objects'], rows['audio_native'], next(iter(masters)))
    return rows, posts, inputs, dict(capture=capture, seconds=float(stops[-1]['capture_time_s']),
                                    posts=len(posts), owned=sum(p['owner_relation'] in OWNED for p in posts), gaps=gaps)


def timed_posts(rows, posts, branches):
    animation = mapping.captures.dominant_motion_rows(rows['animation'])
    episodes = mapping.captures.motion_episodes(animation, {})
    starts = [e['start_s'] for e in episodes]
    for row in animation:
        row['_episode'] = bisect.bisect_right(starts, float(row['capture_time_s']))
    times = [float(r['capture_time_s']) for r in animation]
    result, isolated = [], Counter()
    for e in posts:
        eid = int(e['event_id'])
        if eid not in branches:
            continue
        if e['owner_relation'] not in OWNED:
            isolated[e['owner_relation']] += 1
            continue
        context = mapping.positions.bracket_audio(animation, times, e)
        if context['status'] != 'temporal_candidate':
            isolated[context['status']] += 1
            continue
        result.append(dict(context, event_id=eid, branch_id=branches[eid]['id'],
                           layer=branches[eid]['layer'], audio_row=e['row']))
    return mapping.cluster_events(result), dict(isolated)


def run(directory, control, evidence, output, source):
    output = mapping.captures.checked_output(output.resolve())
    mapping.captures.checked_inputs(source, directory, output)
    target = output / 'comparison.json'
    if target.exists():
        report = mapping.load(target)
        if report['control_capture'] != control:
            raise ValueError('Existing comparison belongs to a different control capture')
        for path, expected in report['inputs'].items():
            if m1.sha256(Path(path)) != expected:
                raise ValueError('Frozen comparison input changed: ' + path)
        print('CLOTH_COMPARISON retained ' + str(target))
        return
    original_path = evidence / 'MappingFinal/mapping.json'
    original = mapping.load(original_path)
    primary = original['capture']
    metadata_path = evidence / primary / 'map.json'
    metadata = mapping.load(metadata_path)
    branches = {b['event_id']: b for b in original['branches'] if b['layer'] == '装备动作层'}
    if len(branches) > 8 or not branches:
        raise ValueError('Narrow comparison to 1..8 existing equipment events')
    metadata['events'] = [e for e in metadata['events'] if e['event_id'] in branches]
    control_rows, control_posts, inputs, control_summary = capture_rows(directory, control)
    primary_rows, primary_posts, primary_inputs, primary_summary = capture_rows(directory, primary)
    del primary_rows
    inputs.update(primary_inputs)
    inputs.update({str(p): m1.sha256(p) for p in (original_path, metadata_path)})
    selected = [dict(e, event_id=int(e['event_id'])) for e in control_posts
                if e['owner_relation'] in OWNED and int(e['event_id']) in branches]
    branch_dir = output / 'NoArmor'
    existing = sorted((branch_dir / 'Branches').rglob('*.txtp'))
    if existing:
        variants = {eid: [] for eid in branches}
        for path in existing:
            v = m1.describe_txtp(path, {})
            if v['event_id'] in variants:
                variants[v['event_id']].append(v)
        for bank in ('Init.bnk', 'pl_equip.bnk'):
            src, copied = source / 'Sound/Wwise' / (bank + '.2.X64'), branch_dir / 'Banks' / bank
            if m1.sha256(src) != m1.sha256(copied):
                raise ValueError('Comparison bank differs from source')
    else:
        branch_dir.mkdir(parents=True, exist_ok=True)
        variants, _ = mapping.generate_variants(source, branch_dir, metadata, selected)
    outcomes, confirmed = [], set()
    for eid, old in branches.items():
        observations = [e for e in selected if e['event_id'] == eid]
        choices = [mapping.choose_variant(variants[eid], e['switches'], e['states'])[0] for e in observations]
        no_armor_silent = bool(choices) and all(v is not None and plugin_silence(v) for v in choices)
        old_posts = [e for e in primary_posts if int(e['event_id']) == eid and e['owner_relation'] in OWNED]
        old_match = [mapping.condition_match(old, e['switches'], e['states']) for e in old_posts]
        # The original graph must really render WEM, not just list source/plugin IDs.
        original_audible_graph = any('.wem' in line for line in old['render_lines']) and all(old_match) and bool(old_match)
        if no_armor_silent and original_audible_graph:
            confirmed.add(eid)
        outcomes.append(dict(event_id=eid, branch_id=old['id'], group_conditions=old['conditions'],
            original_requests=len(old_posts), no_armor_requests=len(observations),
            original_media_ids=old['media_ids'], original_audible_graph=original_audible_graph,
            no_armor_all_plugin_silence=no_armor_silent,
            no_armor_conditions=sorted({c for v in choices if v for c in v['conditions']}),
            status='armor_dependent_equipment_layer' if eid in confirmed else 'unresolved'))
        inputs[old['txtp']] = m1.sha256(Path(old['txtp']))
    for values in variants.values():
        for v in values:
            inputs[v['file']] = m1.sha256(Path(v['file']))
    for bank in ('Init.bnk', 'pl_equip.bnk'):
        p = source / 'Sound/Wwise' / (bank + '.2.X64')
        inputs[str(p)] = m1.sha256(p)
    clusters, isolated = timed_posts(control_rows, control_posts, branches)
    old_clusters = [c for c in original['clusters'] if c['event_id'] in confirmed]
    windows = paired_windows(old_clusters, clusters)
    ids = {c['id'] for c in windows}
    paths = [m['path'] for m in original['montages']
             if any(c['cluster_id'] in ids and not c['cut_boundary_ambiguous'] for c in m['candidates'])]
    report = dict(schema='mhgz_cloth_comparison_v1', primary_capture=primary, control_capture=control,
        inputs=inputs, captures=[primary_summary, control_summary], events=outcomes,
        matched_windows=windows, montage_candidates=paths, control_isolated=isolated,
        unmatched_original_windows=len(old_clusters)-len(windows),
        policy='preserve timing and notifications; Sound stays None; no audio media deletion',
        limits=['Armor-dependent layer includes cloth and armor hardware; submaterials are not proven.',
                'Positions are source-clock estimates, not original exact notify frames.',
                'Control covers ground actions only; unmatched/air windows are not auto-authored.',
                'Wwise graph matches requests, not final PlayingID or audible playback.'])
    for path, expected in inputs.items():
        if m1.sha256(Path(path)) != expected:
            raise ValueError('Comparison input changed while reading: ' + path)
    m1.write_json(target, report)
    print('CLOTH_COMPARISON ' + str(len(confirmed)) + ' events; ' + str(len(windows)) + ' paired windows; ' + str(target))


def aerial_powder_supplement(directory, aerial, vortex, evidence, output, source):
    """Reuse confirmed armor classes and actual request conditions, not new parsers.

    Equipment markers remain silent, like the accepted ground workflow. The four
    new powder sounds are auditions only until their semantics are reviewed.
    """
    output = mapping.captures.checked_output(output)
    mapping.captures.checked_inputs(source, directory, output)
    comparison = output / 'comparison.json'
    audition_file = output / 'Powder/audition.json'
    if comparison.exists():
        previous = mapping.load(comparison)
        if previous['primary_capture'] != aerial or previous['vortex_capture'] != vortex:
            raise ValueError('Supplement belongs to different captures; do not overwrite')
        for path, expected in previous['inputs'].items():
            if m1.sha256(Path(path)) != expected:
                raise ValueError('Supplement input changed: ' + path)
        if audition_file.exists():
            for preview in mapping.load(audition_file)['previews']:
                m1.refreshed_preview(preview)
            print('AERIAL_POWDER retained ' + str(output))
            return
    original_file = evidence / 'MappingFinal/mapping.json'
    cloth_file = evidence.parent / 'ClothComparison20261020/comparison.json'
    original, proven = mapping.load(original_file), mapping.load(cloth_file)
    confirmed = {e['event_id'] for e in proven['events']
                 if e['status'] == 'armor_dependent_equipment_layer'}
    if len(confirmed) != 4:
        raise ValueError('Expected the four previously confirmed equipment classes')
    branches = {b['event_id']: b for b in original['branches'] if b['event_id'] in confirmed}
    rows, posts, inputs, summary = capture_rows(directory, aerial)
    vortex_rows, vortex_posts, vortex_inputs, vortex_summary = capture_rows(directory, vortex)
    if summary['gaps'] or vortex_summary['gaps']:
        raise ValueError('Capture has native queue gaps; do not author from it')
    inputs.update(vortex_inputs)
    for path in (original_file, cloth_file, output / 'Aerial/map.json', output / 'Vortex/map.json'):
        inputs[str(path)] = m1.sha256(path)
    for path, expected in proven['inputs'].items():
        if m1.sha256(Path(path)) != expected:
            raise ValueError('Original armor proof changed: ' + path)
    clusters, isolated = timed_posts(rows, posts, branches)
    repeated = [c for c in clusters if c['observations'] >= 2 and c['source_id'] is not None]
    paths = ['/Game/Weapons/InsectGlaive/Anims/Montage/' + p.stem
             for p in sorted((m1.PROJECT / 'Content/Weapons/InsectGlaive/Anims/Montage').glob('*.uasset'))
             if aerial_montage(p.stem)]
    result = dict(schema='mhgz_aerial_equipment_supplement_v1', primary_capture=aerial,
        vortex_capture=vortex, inputs=inputs, captures=[summary, vortex_summary],
        events=[e for e in proven['events'] if e['event_id'] in confirmed],
        matched_windows=repeated, montage_candidates=paths, control_isolated=isolated,
        policy='new silent ClothAudio points only; preserve all existing audio/gameplay/segments',
        timing_source='shared_native_present_frame_not_IPC_start_estimate',
        limits=['armor classes proven by earlier naked control; newest capture supplies aerial timing',
                'source-frame estimates retain about one render-frame uncertainty',
                'no MHW-only JiXiTuCi source/armor timing inferred from Rise'])
    if comparison.exists() and mapping.load(comparison) != result:
        raise ValueError('Frozen supplement differs; never replace it')
    m1.write_json(comparison, result)
    native = dict(captures=[summary, vortex_summary],
        ordinary_bomb_neighbors=powder_hit_neighbors(rows['hits'], posts),
        vortex_bomb_neighbors=powder_hit_neighbors(vortex_rows['hits'], vortex_posts),
        candidates={str(e): dict(label=label,
            counts=[sum(int(p['event_id']) == e for p in group) for group in (posts, vortex_posts)])
            for e, label in POWDER_CANDIDATES.items()},
        final_media_observed=False, exact_powder_attacker_emitter_link=False,
        semantics='repeated native-frame co-occurrence, not an exact damage transaction',
        unknown_539819764_assigned=False, runtime_or_ue_audio_changed=False)
    m1.write_json(output / 'powder-evidence.json', native)
    metadata = mapping.load(output / 'Vortex/map.json')
    if metadata['capture'] != vortex:
        raise ValueError('Wrong Vortex metadata capture')
    selected = [dict(p, event_id=int(p['event_id'])) for p in [*posts, *vortex_posts]
                if int(p['event_id']) in POWDER_CANDIDATES]
    preview_dir = output / 'Powder'
    variants, copies = mapping.generate_variants(source, preview_dir, metadata, selected)
    previews, unresolved, extracted = [], [], {}
    for eid, label in POWDER_CANDIDATES.items():
        requests = [p for p in selected if int(p['event_id']) == eid]
        graphs = {}
        for post in requests:
            variant, status = mapping.choose_variant(variants[eid], post['switches'], post['states'])
            if variant is None:
                raise ValueError(f'Cannot choose actual observed candidate {eid}: {status}')
            graphs[mapping.graph_key(variant)] = variant
        if not 1 <= len(graphs) <= 2:
            raise ValueError('Audition must have 1..2 distinct observed graphs per event')
        event = dict(next(e for e in metadata['events'] if e['event_id'] == eid), calls=len(requests))
        for variant in graphs.values():
            preview = mapping.captures.render_preview(variant, event, source, preview_dir,
                copies, extracted, len(variants[eid]))
            preview['review_note'] = label + '；按实际请求条件匹配，最终随机子音未被录制，不先接入UE'
            previews.append(preview)
    for path, expected in inputs.items():
        if m1.sha256(Path(path)) != expected:
            raise ValueError('Input changed during preparation: ' + path)
    audition = dict(capture=aerial + ' + ' + vortex, previews=previews, unresolved=unresolved,
        bank_copies=list(copies.values()), extracted_media=list(extracted.values()),
        ue_imported=False, listening_accepted=False)
    m1.write_json(audition_file, audition)
    # Same existing player/page; use Present-derived labels, not the legacy IPC
    # temporal contexts in map.json, which must not be mistaken for placement.
    page_metadata = dict(metadata, capture=audition['capture'], episodes=[dict(
        action_name='粉尘/聚爆（用途待试听确认）', motion='非蒙太奇时序；实际爆炸/聚集生命周期',
        temporal_event_counts={str(e): sum(int(p['event_id']) == e for p in selected)
                               for e in POWDER_CANDIDATES})])
    mapping.captures.audition_page(page_metadata, audition, preview_dir)
    print('AERIAL_POWDER ' + str(len(repeated)) + ' repeated equipment clusters; '
          + str(len(previews)) + ' powder candidate previews; ' + str(preview_dir / 'index.html'))


def prepare_powder_sources(source):
    """User reviewed all four IDs; reuse the existing flat-random decoder."""
    import prepare_current_weapon_audio as prepare
    output = m1.PROJECT / 'Saved/Audio/RiseIG/AerialPowder20261023/Powder'
    target = output / 'source-plan.json'
    if target.exists():
        for path, expected in mapping.load(target)['inputs'].items():
            if m1.sha256(Path(path)) != expected:
                raise ValueError('Prepared powder input changed: ' + path)
        print('POWDER_SOURCES retained ' + str(target))
        return
    audition_file = output / 'audition.json'
    audition = mapping.load(audition_file)
    if {p['event_id'] for p in audition['previews']} != set(POWDER_CANDIDATES):
        raise ValueError('Review must cover exactly the four scoped powder/vortex events')
    inputs = {str(audition_file): m1.sha256(audition_file)}
    sources, copies, extracted = [], {}, {}
    for preview in audition['previews']:
        m1.refreshed_preview(preview)
        path = Path(preview['original_txtp'])
        variant = m1.describe_txtp(path, {})
        branch = dict(event_id=preview['event_id'], txtp=str(path), txtp_sha256=m1.sha256(path),
            conditions=variant['conditions'], render_lines=variant['render_lines'])
        if branch['conditions'] or variant['media_ids'] != preview['media_ids']:
            raise ValueError('Reviewed powder graph/conditions changed')
        inputs[str(path)] = branch['txtp_sha256']
        variants = []
        choices = prepare.choices(branch)
        for choice in choices:
            selected = output / 'Decode/Input' / f"{branch['event_id']}-{choice['selection']}.txtp"
            selected.parent.mkdir(parents=True, exist_ok=True)
            if selected.exists() and selected.read_text(encoding='utf-8') != choice['body']:
                raise ValueError('Existing powder alternative differs')
            selected.write_text(choice['body'], encoding='utf-8')
            decoded = mapping.captures.render_preview(m1.describe_txtp(selected, {}),
                dict(event_id=branch['event_id'], calls=preview['calls'], sources=[dict(bank='pl_wp_i_gla_com.bnk')]),
                source, output / 'Decode', copies, extracted, len(choices))
            if decoded['channels'] not in (1, 2) or decoded['sample_rate'] != 48000:
                raise ValueError('Powder sources must preserve supported original mono/stereo 48kHz')
            variants.append(dict(decoded, selected_media_id=choice['selected_media_id'],
                wave=f"{prepare.AUDIO}/Waves/MuGu/SW_IG_MuGu_{choice['selected_media_id']}_{decoded['sha256'][:8]}"))
            inputs[decoded['wav']] = decoded['sha256']
        sources.append(dict(event_id=branch['event_id'], conditions=[], variants=variants,
            txtp_sha256=branch['txtp_sha256'],
            cue=f"{prepare.AUDIO}/Cues/MuGu/SC_IG_MuGu_{branch['event_id']}_{branch['txtp_sha256'][:8]}"))
    for copy in copies.values():
        inputs[copy['source']] = copy['sha256']
    plan = dict(schema='mhgz_powder_audio_sources_v1', user_confirmed_events=list(POWDER_CANDIDATES),
        sources=sources, inputs=inputs, mix='Cue volume1; existing SA_IG_DongZuo; separate global8 powder concurrency',
        runtime_contract='natural once per explosion; gather2 at arrival and stop on commit/cancel/teardown; vortex base+extra once if count>0',
        limits=['flat random alternatives use UE random, not original random weights/history',
                'user confirmed semantics; recorder still does not observe final random Media/voice'])
    for path, expected in inputs.items():
        if m1.sha256(Path(path)) != expected:
            raise ValueError('Powder source changed while preparing: ' + path)
    m1.write_json(target, plan)
    print('POWDER_SOURCES ' + str(len(sources)) + ' Cues / '
          + str(sum(len(s['variants']) for s in sources)) + ' Waves; ' + str(target))
