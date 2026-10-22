"""Map only requested recovery twirls and advancing-slash landing from one capture.

Reuses exact actor/condition joins, frame calibration and existing resident Cues.
No new bank parser/decoder, game writes or old-plan replay. Outputs only under Saved.
"""
import argparse
import bisect
from collections import Counter
from pathlib import Path
import json
import re
import math
import sys

import rise_audio_weapon_mapping as mapping
import map_swing_positions as positions
import prepare_current_weapon_audio as prepare
from plan_swing_coverage import fingerprint

ROOT = mapping.m1.PROJECT
OUT = ROOT / 'Saved/Audio/RiseIG/GroundEvents20261021'
BASE = ROOT / 'Saved/Audio/RiseIG/Weapon20261019'
TARGETS = {'AM_IG_ErLian', 'AM_IG_FeiYuan', 'AM_IG_HengSao', 'AM_IG_TuJinHuiXuan'}
LANDING_EVENT = 1762027986
IMPACT_OUT = ROOT / 'Saved/Audio/RiseIG/SiLianImpact20261021'
DESCENDING_OUT = ROOT / 'Saved/Audio/RiseIG/DescendingThrustAudio20261021'


def descending_points(snapshot, impact_recipe):
    """Explicit cross-game adaptation, using reference attack phases, not raw clip times."""
    by_name = {m['path'].split('.')[-1]: m for m in snapshot['montages']}
    main, over, sweep, double = [by_name[n] for n in
        ('AM_IG_JiXiTuCi', 'AM_IG_JiXiTuCi_Over', 'AM_IG_HengSao', 'AM_IG_ErLian')]
    def attacks(montage):
        rows = [r for r in montage['existing_notifies'] if 'AnimNotifyState_AttackCollision' in r.get('NotifyStateClass', '')]
        return sorted([(float(r.get('LinkValue', 0)), float(r['Duration'])) for r in rows])
    dest, src_sweep, src_double = attacks(over), attacks(sweep), attacks(double)
    if len(dest) != 2 or len(src_sweep) != 2 or len(src_double) != 3:
        raise ValueError('Ground attack topology changed; review adaptation anchors')
    def recipe(montage, event, ordinal=0):
        rows = [r for r in montage['audio'] if r['sound'] and f'_MuGu_{event}_' in r['sound']]
        rows.sort(key=lambda r: r['time'])
        if len(rows) <= ordinal:
            raise ValueError('Missing selected reference Cue/point')
        return rows[ordinal]
    def point(row, time, reason, kind=None):
        return dict(kind=kind or row['kind'], time=time, sound=row['sound'].split('.')[0],
            source='GROUND_CONTACT' if (kind or row['kind']) in ('AnimNotify_GroundDust', 'AnimNotify_GroundImpact')
                   else 'CHARACTER_SOCKET' if row['socket'] == 'Root' else 'WEAPON_SOCKET',
            socket=row['socket'], reason=reason,
            reference=dict(guid=row['guid'], time=row['time'], kind=row['kind']))
    # The native movement basis switches horizontal derivative0.6->1 and vertical
    # acceleration~-1573->~-9300 at this point. Keep it strictly before loop entry.
    rows = snapshot['curve_rows']
    dt = rows[1][0] - rows[0][0]
    onset = next((rows[i][0] for i in range(1, len(rows)-1)
                  if (rows[i+1][3] - 2*rows[i][3] + rows[i-1][3]) / dt**2 < -5000), None)
    loop = next(s['time_s'] for s in main['sections'] if s['name'] == 'FallingLoop')
    if onset is None or not 0 < onset < loop:
        raise ValueError('No unique pre-loop acceleration onset; do not place propulsion in loop')
    propulsion = recipe(by_name['AM_IG_AirDodge'], 3520842405)
    result = {main['path']: [point(propulsion, onset, '曲线快速下潜起效；空回/撑杆跳同款推进层')], over['path']: []}
    if impact_recipe['kind'] != 'AnimNotify_GroundImpact' or '_1762027986_' not in impact_recipe['sound']:
        raise ValueError('Need the accepted existing ground-impact recipe')
    result[over['path']].append(point(impact_recipe, 0., '第二段真实落地入口', 'AnimNotify_GroundImpact'))
    # Opening sweep: retain its lead to the first attack, not the entire Rise clip.
    for event in (4196403743, 2231624382):
        row = recipe(sweep, event)
        result[over['path']].append(point(row, dest[0][0] + row['time'] - src_sweep[0][0], '横扫首挥相对首个攻击窗口的提前量'))
    # Large finishing swing: use the third red double-slash attack's two layers.
    for event in (2702726931, 1392529433):
        row = recipe(double, event)
        result[over['path']].append(point(row, dest[1][0] + row['time'] - src_double[2][0], '二连末挥两层相对末个攻击窗口的位置'))
    for source, index, target_index, ordinal in ((sweep, 0, 0, 0), (double, 2, 1, 1)):
        row = recipe(source, 3777612407, ordinal)
        time = sum(dest[target_index]) + row['time'] - sum(attacks(source)[index])
        result[over['path']].append(point(row, time, '对应挥动结束附近的独立扬尘，不与入口砸地重复'))
    # One recovery twirl, tied to the corresponding strongest RAW weapon angular
    # speed peak (not the huge speed discontinuity at ErLian's sequence seam).
    def recovery_peak(montage, lo, hi):
        samples = snapshot['poses'][montage['path']]
        values = []
        for a, b in zip(samples, samples[1:]):
            delta = b['time'] - a['time']
            if delta < .001 or not lo <= b['time'] <= hi:
                continue
            qa, qb = a['bones']['R_Weapon_00']['q'], b['bones']['R_Weapon_00']['q']
            dot = min(1., abs(sum(x*y for x, y in zip(qa, qb))))
            values.append((math.acos(dot) / delta, b['time']))
        if not values:
            raise ValueError('Missing recovery pose samples')
        return max(values)[1]
    twirl = recipe(double, 938078130)
    time = recovery_peak(over, 1.8, 2.25) + twirl['time'] - recovery_peak(double, 2.4, 2.8)
    result[over['path']].append(point(twirl, time, '二连保留的木骨武器棍花分支，按回收旋棍速度峰对齐'))
    for path, points in result.items():
        montage = next(m for m in snapshot['montages'] if m['path'] == path)
        points.sort(key=lambda r: r['time'])
        if montage['audio'] or any(not 0 <= r['time'] < montage['length_s'] for r in points):
            raise ValueError('Existing target audio or out-of-range adaptation; do not duplicate/clamp')
    return result


def prepare_descending():
    if (DESCENDING_OUT / 'state.json').exists():
        raise RuntimeError('Audio transaction exists; audit, never re-prepare')
    snapshot_path = DESCENDING_OUT / 'snapshot.json'
    snapshot = mapping.load(snapshot_path)
    impact_file = IMPACT_OUT / 'audit.json'
    points = descending_points(snapshot, mapping.load(impact_file)['added'][0])
    # Include the exact already-exported CDO, so only its audio enable flag can change.
    cdo_file = DESCENDING_OUT / 'NativeExports/exports/Blueprints__Ability__InsectGlaive__GA_IG_JiXiTuCi.Default__GA_IG_JiXiTuCi_C.copy'
    sys.path.insert(0, str(ROOT / 'Scripts'))
    from ue_asset_utils import decode_native_export
    inputs = [snapshot_path, impact_file, ROOT / 'Source/MHGZ/ActionSystem/MHGZDescendingThrustAbility.cpp',
        ROOT / 'Source/MHGZ/ActionSystem/MHGZAirDodgeAbility.cpp', ROOT / 'Source/MHGZ/Generated/MHGZVaultCurveTables.h']
    inputs += [ROOT / ('Content/' + r['sound'].removeprefix('/Game/') + '.uasset') for ps in points.values() for r in ps]
    inputs += [ROOT / ('Content/' + snapshot['curve'].split('.')[0].removeprefix('/Game/') + '.uasset')]
    report = dict(points=points, inputs={str(p): mapping.m1.sha256(p) for p in inputs},
        ability='/Game/Blueprints/Ability/InsectGlaive/GA_IG_JiXiTuCi', before_ability=decode_native_export(cdo_file.read_bytes()),
        limitation='Project adaptation of Rise audio to World380/381, not recorded World sound or exact original Notify truth')
    report['plan_sha256'] = fingerprint(report)
    (DESCENDING_OUT / 'source-plan.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('DESCENDING_AUDIO_PLAN', {p.split('.')[-1]: [(r['kind'],round(r['time'],4),r['sound'].split('_MuGu_')[-1]) for r in rows] for p, rows in points.items()})


def impact_point(snapshot, cluster, sound):
    """Map the observed161 contact through the retained source, never clamp/retime it."""
    montage = snapshot['montages'][0]
    if montage['path'].split('.')[-1] != 'AM_IG_SiLianYinZhan' or cluster['source_id'] != 161:
        raise ValueError('Impact scope is only SiLian161')
    if cluster['event_id'] != LANDING_EVENT or cluster['observations'] < 2:
        raise ValueError('Need repeated observed ground contact')
    points = [p for segment in montage['segments'] for p in positions.segment_positions(
        segment, snapshot['sequences'][segment['sequence']], cluster) if not p['cut_boundary_ambiguous']]
    if len(points) != 1:
        raise ValueError('Contact must map to exactly one retained source interval')
    placement = points[0]
    time = placement['montage_time_estimate_s']
    if any(r['sound'] and r['sound'].split('.')[0] == sound.split('.')[0]
           and abs(r['time'] - time) <= 1 / 60 for r in montage['audio']):
        raise ValueError('Contact already authored; do not duplicate')
    return dict(time=time, sound=sound, kind='AnimNotify_GroundImpact', placement=placement)


def prepare_impact(directory, capture):
    if (IMPACT_OUT / 'state.json').exists():
        raise RuntimeError('Impact transaction exists; audit it, never overwrite evidence')
    snapshot_path = IMPACT_OUT / 'snapshot.json'
    paths = {kind: directory / f'{capture}_{kind}.csv' for kind in
             ('animation', 'audio_native', 'audio_context', 'audio_objects')}
    data = {k: mapping.read_rows(p) for k, p in paths.items()}
    # This capture has an empty hit CSV. The immediately following live report
    # attests the master; require matching native ownership, not proximity alone.
    live = directory / 'combat_capture_live.txt'
    text = live.read_text(encoding='utf-8')
    delta = live.stat().st_mtime - paths['animation'].stat().st_mtime
    match = re.search(r'^master_gameobject_address=(\d+)$', text, re.M)
    if not match or not 0 <= delta <= 60:
        raise ValueError('No contemporaneous master report; do not guess hunter ownership')
    master = int(match[1])
    if not any(mapping.pointer(r.get('engine_gameobject_address')) == master
               and mapping.pointer(r.get('owner_gameobject_address')) == master for r in data['audio_objects']):
        raise ValueError('Live master absent from this capture native owner graph')
    posts = mapping.scoped_posts(data['audio_context'], data['audio_objects'], data['audio_native'], master)
    fx, isolated = mapping.hunter_fx_posts(data['audio_context'], data['audio_objects'], posts, master, {LANDING_EVENT})
    source_file = OUT / 'source-plan.json'
    source = mapping.load(source_file)['new_source']
    rows = mapping.captures.dominant_motion_rows(data['animation'])
    starts = [e['start_s'] for e in mapping.captures.motion_episodes(rows, {})]
    for r in rows:
        r['_episode'] = bisect.bisect_right(starts, float(r['capture_time_s']))
    times = [float(r['capture_time_s']) for r in rows]
    events, evidence = [], []
    for post in fx:
        context = positions.bracket_audio(rows, times, post)
        if context.get('motion') != 'plw_InsectGlaive_100_161' or context['status'] != 'temporal_candidate':
            continue
        branch, reason = mapping.choose_variant([source['branch']], post['switches'], post['states'])
        if not branch:
            raise ValueError('Resident contact branch does not match: ' + reason)
        evidence.append(post)
        events.append(dict(context, event_id=LANDING_EVENT, branch_id='G001', layer='地面/动作附加层', audio_row=post['row']))
    clusters = mapping.cluster_events(events)
    if len(clusters) != 1:
        raise ValueError('No single repeated SiLian contact cluster')
    point = impact_point(mapping.load(snapshot_path), clusters[0], source['cue'])
    report = dict(capture=capture, inputs={str(p): mapping.m1.sha256(p) for p in [*paths.values(), snapshot_path, source_file]},
        master_report=dict(text=text, report_time=live.stat().st_mtime, capture_time=paths['animation'].stat().st_mtime),
        master=master, contact_evidence=evidence, isolated=isolated, cluster=clusters[0], point=point, points=[point],
        limitation='recorded request/condition/frame estimate; not final random Media or exact original Notify frame')
    report['plan_sha256'] = fingerprint(report)
    (IMPACT_OUT / 'source-plan.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('SILIAN_IMPACT_PLAN', point)


def contact_source(posts):
    """Decode only the exact transient hunter-FX contact observed on Rise160."""
    output = OUT / 'ContactSource'
    metadata = mapping.load(BASE / 'mhrise_20261019_175309_05/map.json')
    metadata['events'] = [e for e in metadata['events'] if e['event_id'] == LANDING_EVENT]
    source_root = Path('D:/study/MHR/natives/STM')
    variants, copies = mapping.generate_variants(source_root, output, metadata,
        [dict(p, event_id=LANDING_EVENT) for p in posts])
    selected = [mapping.choose_variant(variants[LANDING_EVENT], p['switches'], p['states'])[0] for p in posts]
    if not selected or any(v is None for v in selected) or len({mapping.graph_key(v) for v in selected}) != 1:
        raise ValueError('Contact FX lacks one exact condition branch')
    variant = selected[0]
    branch = dict(id='G001', event_id=LANDING_EVENT, layer='地面/动作附加层',
        conditions=variant['conditions'], media_ids=variant['media_ids'], txtp=variant['file'],
        txtp_sha256=mapping.m1.sha256(Path(variant['file'])), render_lines=variant['render_lines'])
    index = prepare.media_index(source_root, [branch])
    decoded, extracted = [], {}
    for choice in prepare.choices(branch):
        path = output / 'DecodeInput' / f"G001-{choice['selection']}.txtp"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(choice['body'], encoding='utf-8')
        preview = mapping.captures.render_preview(mapping.m1.describe_txtp(path, {}),
            dict(event_id=LANDING_EVENT, calls=len(posts), sources=[dict(bank='pl_wp_com.bnk')]),
            source_root, output / 'Decode', copies, extracted, 4, index)
        decoded.append(dict(preview, wave=f"{prepare.AUDIO}/Waves/MuGu/SW_IG_MuGu_{choice['selected_media_id']}_{preview['sha256'][:8]}"))
    return dict(branch_id='G001', event_id=LANDING_EVENT, branch=branch, variants=decoded,
        cue=f"{prepare.AUDIO}/Cues/MuGu/SC_IG_MuGu_{LANDING_EVENT}_{branch['txtp_sha256'][:8]}")


def candidate_points(snapshot, clusters, sources):
    result = {}
    for montage in snapshot['montages']:
        name = montage['path'].split('.')[-1]
        if name not in TARGETS:
            continue
        rows = []
        # Three recovery twirls live in the final segment, not the red attack.
        segment = montage['segments'][-1]
        for cluster in clusters:
            source = sources.get(cluster['branch_id'])
            if not source or cluster['observations'] < 2:
                continue
            landing = name == 'AM_IG_TuJinHuiXuan'
            if landing and cluster['event_id'] != LANDING_EVENT:
                continue
            if not landing and cluster['layer'] not in ('武器层', '通用武器层'):
                continue
            for point in positions.segment_positions(segment, snapshot['sequences'][segment['sequence']], cluster):
                if point['cut_boundary_ambiguous']:
                    continue
                cue = source['cue']
                time = point['montage_time_estimate_s']
                if any(r['sound'] and r['sound'].split('.')[0] == cue.split('.')[0]
                       and abs(r['time'] - time) <= 1 / 60 for r in montage['audio']):
                    continue
                rows.append(dict(time=time, sound=cue,
                    kind='AnimNotify_LandingEvent' if landing else 'AnimNotify_SwingSound',
                    source='GROUND_CONTACT' if landing else 'WEAPON_SOCKET',
                    socket='Root' if landing else 'IG_FrontTip',
                    volume=1., pitch=1., ground_trace_distance=150.,
                    cluster=cluster, placement=point))
        result[montage['path']] = sorted(rows, key=lambda r: r['time'])
    return result


def run(directory, capture):
    if (OUT / 'state.json').exists():
        raise RuntimeError('Authoring transaction exists; do not overwrite its frozen evidence')
    paths = {kind: directory / f'{capture}_{kind}.csv' for kind in
             ('animation', 'audio_native', 'audio_context', 'audio_objects', 'hits')}
    data = {k: mapping.read_rows(p) for k, p in paths.items()}
    masters = {mapping.pointer(r['attacker_address']) for r in data['hits']
               if r['kind'] == 'damage_side_callback' and r['attacker_relation'] == 'master_gameobject_exact'}
    if len(masters) != 1 or None in masters:
        raise ValueError('Need one exactly identified hunter')
    snapshot = mapping.load(OUT / 'snapshot.json')
    original = mapping.load(BASE / 'MappingFinal/mapping.json')
    source_plan = mapping.load(BASE / 'UEActionRuntime/plan.json')
    sources = {s['branch_id']: s for s in source_plan['sources']}
    rows = mapping.captures.dominant_motion_rows(data['animation'])
    episodes = mapping.captures.motion_episodes(rows, {})
    starts = [e['start_s'] for e in episodes]
    for r in rows:
        r['_episode'] = bisect.bisect_right(starts, float(r['capture_time_s']))
    times = [float(r['capture_time_s']) for r in rows]
    posts = mapping.scoped_posts(data['audio_context'], data['audio_objects'],
                                 data['audio_native'], next(iter(masters)))
    # These are transient FX emitters, not the main actor's Wwise ID. Use the
    # existing same-Present-frame hunter-container proof, never nearest-time ownership.
    contacts, contact_isolated = mapping.hunter_fx_posts(data['audio_context'], data['audio_objects'],
        posts, next(iter(masters)), {LANDING_EVENT})
    contacts = [p for p in contacts if positions.bracket_audio(rows, times, p)['motion'] == 'plw_InsectGlaive_100_160']
    if len(contacts) < 2:
        raise ValueError('No repeated exact hunter-FX landing evidence')
    new_source = contact_source(contacts)
    sources['G001'] = new_source
    events, isolated = [], Counter()
    for post in posts:
        if post['owner_relation'] not in ('master_emitter_exact', 'unique_native_owner_is_master'):
            isolated['unresolved_or_other_actor'] += 1
            continue
        context = positions.bracket_audio(rows, times, post)
        if context['status'] != 'temporal_candidate':
            isolated[context['status']] += 1
            continue
        branches = [b for b in original['branches'] if b['event_id'] == int(post['event_id'])]
        branch, reason = mapping.choose_variant(branches, post['switches'], post['states'])
        if branch is None:
            isolated[reason] += 1
            continue
        events.append(dict(context, event_id=int(post['event_id']), branch_id=branch['id'],
                           layer=branch['layer'], audio_row=post['row']))
    for post in contacts:
        context = positions.bracket_audio(rows, times, post)
        if context['status'] == 'temporal_candidate':
            events.append(dict(context, event_id=LANDING_EVENT, branch_id='G001',
                layer='地面/动作附加层', audio_row=post['row']))
    clusters = mapping.cluster_events(events)
    points = candidate_points(snapshot, clusters, sources)
    report = dict(capture=capture, inputs={str(p): mapping.m1.sha256(p) for p in paths.values()},
        snapshot_sha256=mapping.m1.sha256(OUT / 'snapshot.json'),
        branches_sha256=mapping.m1.sha256(BASE / 'MappingFinal/mapping.json'),
        clusters=[c for c in clusters if c['source_id'] in (109, 111, 160)],
        contact_evidence=contacts, contact_isolated=contact_isolated, new_source=new_source,
        isolated_counts=dict(isolated), points=points,
        limitation='recorded request/condition/frame estimate; final random Media and original Notify frame unknown')
    report['plan_sha256'] = fingerprint(report)
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / 'source-plan.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('GROUND_AUDIO_PLAN', {p.split('.')[-1]: [(r['cluster']['event_id'], round(r['time'], 4),
        r['cluster']['observations']) for r in rs] for p, rs in points.items()})


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path)
    parser.add_argument('--capture')
    parser.add_argument('--descending-thrust', action='store_true', help='Adapt existing reference recipes to380/381')
    parser.add_argument('--four-hit-impact', action='store_true', help='Only prepare the SiLian missing ground strike')
    args = parser.parse_args()
    if args.descending_thrust:
        prepare_descending()
    else:
        if not args.directory or not args.capture:
            parser.error('--directory and --capture are required for capture-driven modes')
        (prepare_impact if args.four_hit_impact else run)(args.directory, args.capture)
