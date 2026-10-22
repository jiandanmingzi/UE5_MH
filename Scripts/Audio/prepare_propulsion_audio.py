"""Prepare only the confirmed aerial-propulsion event from the current weapon capture.

Uses the resident ownership/clock mapper, wwiser, media slicer and finite decoder.
No UE, game, recorder, audio-volume tuning or historical plan rewrites.
"""
import bisect
from pathlib import Path

import rise_audio_weapon_mapping as mapping
import prepare_current_weapon_audio as prepare
from rise_audio_coverage import read_rows

ROOT = mapping.m1.PROJECT
OUT = ROOT / 'Saved/Audio/RiseIG/Propulsion20261020'
EVIDENCE = ROOT / 'Saved/Audio/RiseIG/Weapon20261019'
EVENT = 3520842405


def run(source, directory):
    target = OUT / 'source-plan.json'
    if target.exists():
        plan = mapping.load(target)
        for path, sha in plan['inputs'].items():
            if mapping.m1.sha256(Path(path)) != sha:
                raise ValueError('Frozen propulsion input changed: ' + path)
        for v in plan['source']['variants']:
            if mapping.m1.sha256(Path(v['wav'])) != v['sha256']:
                raise ValueError('Frozen propulsion WAV changed')
        print('PROPULSION_PREPARE retained ' + str(target))
        return
    original = mapping.load(EVIDENCE / 'MappingFinal/mapping.json')
    capture = original['capture']
    paths = {k: directory / f'{capture}_{k}.csv' for k in
             ('animation', 'audio_native', 'audio_context', 'audio_objects', 'hits')}
    inputs = {str(p): mapping.m1.sha256(p) for p in paths.values()}
    data = {k: read_rows(p) for k, p in paths.items()}
    masters = {mapping.pointer(h['attacker_address']) for h in data['hits']
               if h['kind'] == 'damage_side_callback' and h['attacker_relation'] == 'master_gameobject_exact'}
    if len(masters) != 1 or None in masters:
        raise ValueError('Require one exact current hunter identity')
    master = next(iter(masters))
    posts = mapping.scoped_posts(data['audio_context'], data['audio_objects'], data['audio_native'], master)
    linked, isolated = mapping.hunter_fx_posts(data['audio_context'], data['audio_objects'], posts, master, {EVENT})
    if len(linked) < 2:
        raise ValueError('No repeated exact hunter FX-container evidence')
    selected = [dict(p, event_id=EVENT) for p in linked]
    metadata_path = EVIDENCE / capture / 'map.json'
    metadata = mapping.load(metadata_path)
    metadata['events'] = [e for e in metadata['events'] if e['event_id'] == EVENT]
    inputs[str(metadata_path)] = mapping.m1.sha256(metadata_path)
    OUT.mkdir(parents=True, exist_ok=True)
    variants, copies = mapping.generate_variants(source, OUT, metadata, selected)
    chosen = []
    for p in selected:
        variant, reason = mapping.choose_variant(variants[EVENT], p['switches'], p['states'])
        if variant is None:
            raise ValueError('Current propulsion branch is not uniquely determined: ' + reason)
        chosen.append(variant)
    if len({mapping.graph_key(v) for v in chosen}) != 1:
        raise ValueError('Multiple propulsion graphs: split explicit branch policy before authoring')
    variant = chosen[0]
    branch = dict(id='P001', event_id=EVENT, layer='地面/动作附加层', conditions=variant['conditions'],
                  media_ids=variant['media_ids'], txtp=variant['file'], render_lines=variant['render_lines'],
                  txtp_sha256=mapping.m1.sha256(Path(variant['file'])))
    locations = prepare.media_index(source, [branch])
    decoded, extracted = [], {}
    options = prepare.choices(branch)
    for choice in options:
        path = OUT / 'Decode/Input' / f"P001-{choice['selection']}.txtp"
        path.parent.mkdir(parents=True, exist_ok=True)
        if path.exists() and path.read_text(encoding='utf-8') != choice['body']:
            raise ValueError('A different selected propulsion TXTP already exists')
        path.write_text(choice['body'], encoding='utf-8')
        preview = mapping.captures.render_preview(mapping.m1.describe_txtp(path, {}),
            dict(event_id=EVENT, calls=len(linked), sources=[dict(bank='pl_wp_i_gla_com.bnk')]),
            source, OUT / 'Decode', copies, extracted, len(options), locations)
        if preview['channels'] not in (1, 2) or preview['sample_rate'] != 48000:
            raise ValueError('Unexpected propulsion source channel/rate')
        decoded.append(dict(preview, selected_media_id=choice['selected_media_id'],
            wave=f"{prepare.AUDIO}/Waves/MuGu/SW_IG_MuGu_{choice['selected_media_id']}_{preview['sha256'][:8]}"))
    animation = mapping.captures.dominant_motion_rows(data['animation'])
    episodes = mapping.captures.motion_episodes(animation, {})
    starts = [e['start_s'] for e in episodes]
    for row in animation:
        row['_episode'] = bisect.bisect_right(starts, float(row['capture_time_s']))
    times = [float(r['capture_time_s']) for r in animation]
    timed = []
    for p in selected:
        context = mapping.positions.bracket_audio(animation, times, p)
        if context['status'] != 'temporal_candidate':
            isolated.append(dict(audio_row=p['row'], reason=context['status']))
            continue
        timed.append(dict(context, event_id=EVENT, branch_id='P001', layer=branch['layer'],
                          audio_row=p['row'], fx_evidence=p['fx_evidence']))
    clusters = mapping.cluster_events(timed)
    source_spec = dict(branch_id='P001', event_id=EVENT, conditions=branch['conditions'], variants=decoded,
        txtp_sha256=branch['txtp_sha256'], cue=f"{prepare.AUDIO}/Cues/MuGu/SC_IG_MuGu_{EVENT}_{branch['txtp_sha256'][:8]}")
    for copied in copies.values():
        inputs[copied['source']] = copied['sha256']
    inputs[branch['txtp']] = branch['txtp_sha256']
    for path, sha in inputs.items():
        if mapping.m1.sha256(Path(path)) != sha:
            raise ValueError('Propulsion input changed during preparation: ' + path)
    result = dict(schema='mhgz_propulsion_source_v1', capture=capture, inputs=inputs,
        observed_posts=sum(int(p['event_id']) == EVENT for p in posts), linked_posts=len(linked),
        timed_posts=timed, clusters=clusters, isolated=isolated, source=source_spec, branch=branch,
        limits=['Hunter-container association is not final PlayingID/Media proof.',
                'Only this user-confirmed FX event is opted in; normal ownership rules stay strict.',
                'Character Root emission is a project adaptation of the original transient FX emitter.',
                'Source clocks give candidate positions, not exact original notify frames.'])
    mapping.m1.write_json(target, result)
    print('PROPULSION_PREPARE ' + str(len(linked)) + ' linked, ' + str(len(clusters))
          + ' clusters, ' + str(len(decoded)) + ' WAV; ' + str(target))
