"""Armor comparison mode of map_swing_positions; reuse ownership, clocks and wwiser.

No decoding, game changes or UE writes. A silent plugin may have a source ID:
Media-ID presence alone is not evidence of audible armor in the naked control.
"""
from collections import Counter
import bisect
from pathlib import Path

import rise_audio_weapon_mapping as mapping
from rise_audio_coverage import read_rows
import rise_audio_m1 as m1

OWNED = {'master_emitter_exact', 'unique_native_owner_is_master'}


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
