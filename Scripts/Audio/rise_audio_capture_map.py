"""Map recorded numeric audio requests to wwiser event/action metadata and motions.

Read-only game/unpacked inputs; derived files under project Saved, except an
explicit --publish metadata snapshot in docs/reference. Reuses
the fixed wwiser parser, existing M1 helpers and the existing motion-name table.
Does not infer actor ownership, selected random/Switch branch or exact Notify frames.
"""
from __future__ import annotations
import argparse
import bisect
from collections import Counter, defaultdict
import csv
import html
import json
import os
from pathlib import Path
import re
import shutil
import struct
import sys
import subprocess
import time

import rise_audio_m1 as m1

PROJECT = m1.PROJECT
WORK = PROJECT / 'Saved/Audio/RiseIG'
sys.path.insert(0, str(PROJECT / 'Scripts/MHRise'))
from recorder_path import recorder_dir
from build_truth_table import load_table_names, KNOWN_IDS


def csv_rows(path):
    with Path(path).open(encoding='utf-8-sig', newline='') as stream:
        rows = list(csv.DictReader(stream))
    if not rows or any(None in row or any(v is None for v in row.values()) for row in rows):
        raise ValueError(f'Empty or ragged input CSV: {path}')
    return rows


def checked_output(path):
    path = Path(path).resolve()
    if not path.is_relative_to((PROJECT / 'Saved').resolve()):
        raise ValueError('Output must remain inside project/Saved')
    if WORK.resolve().is_relative_to(path):
        raise ValueError('Output must not overwrite the existing M1 work root or its parents')
    return path


def checked_inputs(source, directory, output):
    for original in (source, directory):
        if original.is_relative_to(output) or output.is_relative_to(original):
            raise ValueError('Input and output directories must not overlap')


def locate_event_banks(source, wanted):
    """Byte prefilter only; every hit MUST later be verified as a CAkEvent."""
    result, scanned, size = [], 0, 0
    for path in sorted((source / 'Sound/Wwise').glob('*.bnk.2.X64')):
        if 'media' in path.name or 'madia' in path.name:
            continue
        if path.stat().st_size > 4 * 1024 * 1024 or scanned >= 1000:
            raise ValueError('Metadata bank scan exceeded its explicit bounds')
        data = path.read_bytes()
        scanned += 1
        size += len(data)
        if size > 32 * 1024 * 1024:
            raise ValueError('Metadata scan exceeded 32 MiB')
        hits = sorted(event for event in wanted if struct.pack('<I', event) in data)
        if hits:
            result.append({'source': path.as_posix(), 'bytes': len(data),
                           'sha256': m1.sha256(path), 'byte_hits': hits})
    return result, {'banks_scanned': scanned, 'metadata_bytes': size, 'candidate_banks': len(result)}


def parse_sources(sources):
    # wwiser owns binary HIRC/version parsing; this is only a public tree reader.
    sys.path.insert(0, str(WORK / 'Tools/wwiser-v20260808'))
    from wwiser.parser.wparser import Parser
    parser = Parser()
    parser.parse_banks([source['source'] for source in sources])
    nodes, errors = {}, []
    for bank in parser.get_banks('manual-all'):
        bank_name = Path(bank.get_filename()).name.removesuffix('.2.X64')
        hirc = bank.find1(name='HircChunk')
        if bank.get_error_count() or bank.get_skip_count():
            errors.append({'bank': bank_name, 'errors': bank.get_error_count(), 'skips': bank.get_skip_count()})
        if hirc is None:
            continue
        for group in hirc.get_children() or []:
            if group.get_nodename() != 'list':
                continue
            for node in group.get_children() or []:
                identity = next((f for f in node.get_children() or [] if f.get_name() == 'ulID'), None)
                if identity is None:
                    continue
                event_id = int(identity.get_attr('value'))
                action = node.find1(name='ulActionType')
                targets = node.finds(name='idExt')
                record = {'bank': bank_name, 'id': event_id, 'type': node.get_name(),
                          'action_type': action.get_attrs().get('valuefmt') if action else None,
                          'action_code': int(action.get_attr('value')) if action else None,
                          'actions': [int(f.get_attr('value')) for f in node.finds(name='ulActionID')],
                          'targets': [int(f.get_attr('value')) for f in targets],
                          'media_ids': sorted({int(f.get_attr('value')) for f in node.finds(name='sourceID')})}
                switch = node.find1(name='SwitchCntrInitialValues') if node.get_name() == 'CAkSwitchCntr' else None
                if switch:
                    record['switch_container'] = {
                        key: int(switch.find1(name=field).get_attr('value'))
                        for key, field in (('group_type', 'eGroupType'), ('group_id', 'ulGroupID'), ('default_value', 'ulDefaultSwitch'))
                    }
                nodes[(bank_name, event_id)] = record
    if errors:
        raise ValueError('wwiser parse errors/skips: ' + json.dumps(errors))
    return nodes


def bank_role(bank):
    if bank in ('pl_wp_i_gla_com.bnk', 'pl_wp_ig_ins_com.bnk', 'pl_wp_com.bnk'):
        return 'weapon_or_kinsect'
    if bank.startswith('hit_'):
        return 'contact_candidate_not_yet_semantically_confirmed'
    if bank in ('pl_prop.bnk', 'pl_wire_action.bnk', 'buff.bnk'):
        return 'shared_action_layer_candidate'
    return 'outside_current_attack_audio_scope'


def describe_event(event_id, nodes):
    sources = []
    for (bank, identity), node in nodes.items():
        if identity != event_id or node['type'] != 'CAkEvent':
            continue
        actions = []
        for action_id in node['actions']:
            action = nodes.get((bank, action_id))
            if not action:
                raise ValueError(f'Missing action {action_id} in {bank}')
            actions.append(action)
        playable = any(action['type'] in ('CAkActionPlay', 'CAkActionPlayEvent') for action in actions)
        sources.append({'bank': bank, 'role': bank_role(bank), 'playable_action': playable,
                        'actions': actions})
    return {'event_id': event_id, 'sources': sources,
            'has_play_action': any(source['playable_action'] for source in sources),
            'in_scope_candidate': any(source['playable_action'] and source['role'] != 'outside_current_attack_audio_scope'
                                      for source in sources)}


def dominant_motion_rows(animation):
    """One highest-weight readable player00/layer0 node per sample."""
    best = {}
    for row in animation:
        if row['component'] != 'player00' or row['layer'] != '0' or row['read_status'] != 'readable':
            continue
        sample = int(row['sample_index'])
        if sample not in best or float(row['node_weight'] or 0) > float(best[sample]['node_weight'] or 0):
            best[sample] = row
    return [best[sample] for sample in sorted(best)]


def motion_episodes(animation, names):
    episodes = []
    previous = None
    for row in dominant_motion_rows(animation):
        time, frame = float(row['capture_time_s']), float(row['node_frame'])
        key = (row['motion_name'], row['action_state_id'])
        reset = previous and key == previous[0] and frame < previous[1] - 2
        if not previous or key != previous[0] or reset:
            if episodes:
                episodes[-1]['end_s'] = time
            state_id = str(int(float(row['action_state_id'])))
            episodes.append({'episode': len(episodes) + 1, 'motion': row['motion_name'],
                             'action_state_id': state_id, 'action_name': names.get(state_id, '未核对'),
                             'start_s': time, 'end_s': time, 'first_observed_frame': frame,
                             'last_observed_frame': frame, 'temporal_event_counts': {}})
        episodes[-1]['last_observed_frame'] = frame
        episodes[-1]['end_s'] = time
        previous = key, frame
    return episodes


def map_capture(source, directory, capture, output):
    if not re.fullmatch(r'mhrise_[0-9_]+', capture):
        raise ValueError('Invalid capture basename')
    paths = {kind: directory / (capture + suffix) for kind, suffix in
             [('audio', '_audio_native.csv'), ('animation', '_animation.csv')]}
    audio, animation = csv_rows(paths['audio']), csv_rows(paths['animation'])
    counts = Counter(int(row['event_id']) for row in audio)
    sources, scan = locate_event_banks(source, set(counts))
    nodes = parse_sources(sources)
    names = {**{str(key): value for key, value in KNOWN_IDS.items()}, **load_table_names()}
    episodes = motion_episodes(animation, names)
    starts = [episode['start_s'] for episode in episodes]
    event_info = [describe_event(event, nodes) for event in counts]
    info = {row['event_id']: row for row in event_info}
    contexts = defaultdict(Counter)
    timeline = []
    for row in audio:
        event = int(row['event_id'])
        time = float(row['capture_time_estimate_s'])
        index = bisect.bisect_right(starts, time) - 1
        episode = episodes[index] if index >= 0 else None
        if episode:
            key = str(event)
            episode['temporal_event_counts'][key] = episode['temporal_event_counts'].get(key, 0) + 1
            contexts[event][episode['motion']] += 1
        timeline.append({**row, 'episode': episode['episode'] if episode else None,
                         'action_name': episode['action_name'] if episode else None,
                         'motion': episode['motion'] if episode else None,
                         'phase_elapsed_estimate_s': time - episode['start_s'] if episode else None,
                         'banks': [source['bank'] for source in info[event]['sources']],
                         'association': 'temporal_only_not_actor_or_exact_frame_truth'})
    for row in event_info:
        row['calls'] = counts[row['event_id']]
        row['temporal_contexts'] = dict(contexts[row['event_id']].most_common())
    report = {'schema': 'mhgz_rise_capture_audio_map_v1', 'capture': capture, 'scan': scan,
              'input_files': {kind: {'path': path.as_posix(), 'sha256': m1.sha256(path)} for kind, path in paths.items()},
              'source_banks': sources, 'audio_rows': len(audio), 'unique_events': len(counts),
              'located_events': sum(bool(row['sources']) for row in event_info),
              'play_events': sum(row['has_play_action'] for row in event_info),
              'scope_candidates': sum(row['in_scope_candidate'] for row in event_info),
              'events': sorted(event_info, key=lambda row: (-row['calls'], row['event_id'])),
              'episodes': episodes, 'timeline': timeline,
              'limits': ['post requests, not audible success', 'temporal context is not actor ownership',
                         'no selected Switch/State/random branch was captured', 'no exact Notify frame claim']}
    m1.write_json(output / 'map.json', report)
    # No audio or raw capture content in the persistent metadata summary.
    return report


def copy_verified(original, target, copies):
    fingerprint = m1.sha256(original)
    if target.exists() and m1.sha256(target) != fingerprint:
        raise ValueError(f'Refusing to overwrite differing bank: {target}')
    target.parent.mkdir(parents=True, exist_ok=True)
    if not target.exists():
        shutil.copyfile(original, target)
    if m1.sha256(target) != fingerprint:
        raise ValueError('Bank copy hash mismatch')
    copies[target.as_posix()] = {'source': original.as_posix(), 'copy': target.as_posix(), 'sha256': fingerprint}


def render_preview(variant, event, source, output, copies, extracted, variant_count, media_locations=None):
    """Shared finite rendering path; keep wwiser instructions/gains intact."""
    original = Path(variant['file'])
    text = original.read_text(encoding='utf-8-sig')
    selected_path = output / 'Events/Selected' / original.name
    locations = (media_locations if media_locations is not None else
                 json.loads((WORK / 'Inventory/media_locations.json').read_text(encoding='utf-8'))['media'])
    lines = []
    for line in text.splitlines():
        dependency = re.match(r'(\s*)(.*?\.(?:bnk|wem))(?=\s|$)(.*)', line)
        if dependency and not line.lstrip().startswith('#'):
            leaf = Path(dependency[2]).name
            if leaf.endswith('.wem'):
                if not re.fullmatch(r'[0-9]+\.wem', leaf):
                    raise ValueError('Unsupported external WEM name: ' + leaf)
                media_id = int(leaf.removesuffix('.wem'))
                candidates = locations.get(str(media_id), [])
                if not candidates:
                    raise ValueError('external_WEM_not_yet_located:' + leaf)
                bank = output / 'Media' / leaf
                extracted[media_id] = m1.export_media_slice(candidates[0], media_id, bank)
            else:
                bank = output / 'Banks' / leaf
                copy_verified(source / 'Sound/Wwise' / (leaf + '.2.X64'), bank, copies)
            line = dependency[1] + Path(os.path.relpath(bank, selected_path.parent)).as_posix() + dependency[3]
        lines.append(line)
    selected_path.parent.mkdir(parents=True, exist_ok=True)
    selected_path.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    decoder = WORK / 'Tools/vgmstream-maintainer/vgmstream-cli.exe'
    metadata_run = subprocess.run([str(decoder), '-I', '-m', str(selected_path)],
                                  capture_output=True, timeout=15, check=True)
    metadata = json.loads(metadata_run.stdout.decode('utf-8'))
    seconds = metadata['numberOfSamples'] / metadata['sampleRate']
    if not 0 < seconds <= 12:
        raise ValueError('preview_length_outside_0_to_12_seconds')
    wave = output / 'Preview' / f"{event['event_id']}-{m1.sha256(original)[:8]}.wav"
    wave.parent.mkdir(parents=True, exist_ok=True)
    m1.run_logged([str(decoder), '-i', '-o', str(wave), str(selected_path)],
                  output / 'Logs' / f"{event['event_id']}-{m1.sha256(original)[:8]}.log")
    metrics = m1.wav_metrics(wave)
    if not metrics['non_silent']:
        raise ValueError('silent_preview')
    return {'event_id': event['event_id'], 'calls': event['calls'],
            'banks': [s['bank'] for s in event['sources']],
            **{key: variant[key] for key in ('conditions', 'gamesync_conditions', 'state_conditions', 'condition_sources')},
            'media_ids': variant['media_ids'], 'variant_count': variant_count,
            'original_txtp': original.as_posix(), 'selected_txtp': selected_path.as_posix(),
            'wav': wave.as_posix(), **metrics,
            'branch_selection': 'offline_candidate_NOT_runtime_selected_branch'}


def audition_capture(report, source, output):
    copies = {}
    selected_events = [event for event in report['events'] if event['in_scope_candidate']]
    if len(selected_events) > 48:
        raise ValueError('At most 48 recorded candidate events per audition batch')
    old_variants = json.loads((WORK / 'Inventory/variants.json').read_text(encoding='utf-8'))
    by_event = defaultdict(list)
    for cached in old_variants:
        variant = m1.describe_txtp(Path(cached['file']), {})
        by_event[variant['event_id']].append(variant)
    bank_root = source / 'Sound/Wwise'
    # Generate only new candidate banks/events; keep the original M1 outputs intact.
    new_banks = {s['bank'] for e in selected_events for s in e['sources']
                 if s['role'] != 'outside_current_attack_audio_scope' and not by_event[e['event_id']]}
    filtered_ids = [str(e['event_id']) for e in selected_events if not by_event[e['event_id']]]
    generated = output / 'Events' / ('Filtered-' + str(time.time_ns()))
    if filtered_ids:
        needed = {'Init.bnk', *new_banks}
        for bank in new_banks:
            needed.update(p.name.removesuffix('.2.X64') for p in bank_root.glob(bank.removesuffix('.bnk') + '*media.bnk.2.X64'))
        if len(needed) > 40:
            raise ValueError('Too many additional banks')
        for bank in sorted(needed):
            copy_verified(bank_root / (bank + '.2.X64'), output / 'Banks' / bank, copies)
        m1.run_logged([sys.executable, str(WORK / 'Tools/wwiser-v20260808/wwiser.py'), *sorted(needed),
                       '-d', 'none', '-g', '-gu', '-gd', '-go', '../Events/' + generated.name, '-gf', *filtered_ids],
                      output / 'Logs/generate.log', cwd=output / 'Banks')
        for path in sorted(generated.glob('*.txtp')):
            variant = m1.describe_txtp(path, {})
            if variant['event_id'] in {int(value) for value in filtered_ids}:
                by_event[variant['event_id']].append(variant)
    previews, failures, extracted = [], [], {}
    for event in selected_events:
        variants = by_event[event['event_id']]
        # Candidate only: neither this order nor dependency availability proves
        # the actual run-time Switch/State/random selection.
        variants.sort(key=lambda v: (bool(v['conditions']), sum(not d['exists'] for d in v['dependencies']), v['file']))
        if not variants:
            failures.append({'event_id': event['event_id'], 'reason': 'no_renderable_txtp_from_wwiser',
                             'actions': event['sources']})
            continue
        variant = variants[0]
        try:
            preview = render_preview(variant, event, source, output, copies, extracted, len(variants))
            previews.append(preview)
            print('PREVIEW', event['event_id'], f"{preview['seconds']:.3f}s", flush=True)
        except (ValueError, OSError, RuntimeError, subprocess.SubprocessError) as error:
            failures.append({'event_id': event['event_id'], 'reason': str(error)[:500]})
            print('UNRESOLVED', event['event_id'], str(error)[:120], flush=True)
    for item in copies.values():
        if m1.sha256(Path(item['source'])) != item['sha256'] or m1.sha256(Path(item['copy'])) != item['sha256']:
            raise ValueError('Source/copy changed during audition generation')
    result = {'capture': report['capture'], 'previews': previews, 'unresolved': failures,
              'bank_copies': list(copies.values()), 'extracted_media': list(extracted.values()),
              'ue_imported': False, 'listening_accepted': False}
    m1.write_json(output / 'audition.json', result)
    audition_page(report, result, output)
    return result


def refresh_saved_metadata(audition):
    """Refresh cached metadata only, validating every resident preview hash."""
    variants_path = WORK / 'Inventory/variants.json'
    previews_path = WORK / 'Inventory/previews.json'
    locations = json.loads((WORK / 'Inventory/media_locations.json').read_text(encoding='utf-8'))['media']
    old = json.loads(variants_path.read_text(encoding='utf-8'))
    variants = [m1.describe_txtp(Path(row['file']), locations) for row in old]
    previews = [m1.refreshed_preview(row) for row in json.loads(previews_path.read_text(encoding='utf-8'))]
    refreshed = [m1.refreshed_preview(row) for row in audition['previews']]
    changed = sum(a['conditions'] != b['conditions'] for a, b in zip(audition['previews'], refreshed))
    m1.write_json(variants_path, variants)
    m1.write_json(previews_path, previews)
    return {**audition, 'previews': refreshed, 'metadata_refresh': {
        'parser': 'txtp_selected_headers_v2', 'changed_capture_previews': changed,
        'refreshed_m1_variants': len(variants), 'refreshed_m1_previews': len(previews),
        'existing_audio_redecoded': False}}


def comparison_groups(paths):
    groups = {}
    for path in paths:
        variant = m1.describe_txtp(path, {})
        key = tuple(line.strip() for line in variant['render_lines'])
        groups.setdefault(key, []).append(variant)
    return list(groups.values())


def random_comparison_bodies(variant):
    """Force each choice of one flat wwiser random group; reject complex graphs.

    wwiser's own writer uses `group = -R<count>><selection>` for selectable
    random groups. Only that selector is changed; gain/timing/leaf lines stay.
    """
    body = Path(variant['file']).read_text(encoding='utf-8-sig')
    lines = variant['render_lines']
    match = re.fullmatch(r'group\s*=\s*-R([2-4])>\d+', lines[-1].strip()) if lines else None
    if not match or len(lines) != int(match[1]) + 1:
        raise ValueError('Comparison supports only one flat random group of 2 to 4 leaves')
    # wwiser emits both bank subsongs (##123.wem) and external WEM paths.
    media = [re.search(r'(?:##|(?:^|[/\\]))(\d+)\.wem(?=\s|$)', line) for line in lines[:-1]]
    if not all(media):
        raise ValueError('Comparison requires explicit per-leaf media identities')
    return [{'selection': index, 'selected_media_id': int(identity[1]),
             'body': re.sub(r'(^group[ \t]*=[ \t]*-R[2-4]>)\d+([ \t]*$)',
                            lambda found: found[1] + str(index) + found[2], body, count=1, flags=re.M)}
            for index, identity in enumerate(media, 1)]


def compare_event(report, audition, event_id, source, output):
    event = next(e for e in report['events'] if e['event_id'] == event_id and e['in_scope_candidate'])
    existing = next(p for p in audition['previews'] if p['event_id'] == event_id)
    original = Path(existing['original_txtp'])
    prefix = original.name.split('event', 1)[0] + 'event'
    paths = sorted(original.parent.glob(prefix + '*.txtp'))
    if not 1 <= len(paths) <= 16:
        raise ValueError('Comparison requires 1 to 16 existing wwiser combinations')
    groups = comparison_groups(paths)
    if any(v['event_id'] != event_id for group in groups for v in group):
        raise ValueError('Comparison contains a different event')
    existing_graph = tuple(line.strip() for line in m1.describe_txtp(original, {})['render_lines'])
    copies, extracted, entries = {}, {}, []
    new_count = 0
    for index, variants in enumerate(groups, 1):
        variant = variants[0]
        graph = tuple(line.strip() for line in variant['render_lines'])
        silent = len(graph) == 1 and graph[0].startswith('?.plugin-silence ')
        entry = {'group': index, 'combinations': [
            {'source_txtp': v['file'], 'source_sha256': m1.sha256(Path(v['file'])),
             'conditions': v['conditions'], 'condition_sources': v['condition_sources']}
            for v in variants], 'candidate_media_ids': variant['media_ids'],
            'explicit_silence': silent, 'previews': []}
        if silent:
            entry['note'] = 'explicit wwiser silence-plugin branch; no silent WAV generated'
        elif graph == existing_graph:
            entry['previews'] = [{**m1.refreshed_preview(existing), 'reused_original_preview': True}]
        else:
            choices = random_comparison_bodies(variant)
            new_count += len(choices)
            if new_count > 8:
                raise ValueError('Comparison limited to 8 additional finite previews')
            for choice in choices:
                path = output / 'Events/ComparisonInput' / f"event-{event_id}-group-{index}-choice-{choice['selection']}.txtp"
                if path.exists() and path.read_text(encoding='utf-8') != choice['body']:
                    raise ValueError('Refusing to overwrite a different comparison TXTP')
                path.parent.mkdir(parents=True, exist_ok=True)
                if not path.exists():
                    path.write_text(choice['body'], encoding='utf-8')
                preview = render_preview(m1.describe_txtp(path, {}), event, source, output, copies, extracted, len(variants))
                entry['previews'].append({**preview, 'selection': choice['selection'],
                                          'selected_media_id': choice['selected_media_id'],
                                          'comparison_source_txtp': variant['file']})
        entries.append(entry)
    for copied in copies.values():
        if m1.sha256(Path(copied['source'])) != copied['sha256']:
            raise ValueError('Source bank changed during comparison')
    result = {'event_id': event_id, 'condition_combinations': len(paths), 'groups': entries,
              'bank_copies': list(copies.values()), 'extracted_media': list(extracted.values()),
              'runtime_selection_known': False, 'added_previews': new_count}
    m1.write_json(output / f'comparison-{event_id}.json', result)
    return result


def comparison_html(audition, output):
    rows = []
    for comparison in audition.get('comparisons', []):
        for group in comparison['groups']:
            choices = []
            for preview in group['previews']:
                label = ('原第三项（未重解码）' if preview.get('reused_original_preview') else
                         '随机素材 ' + str(preview['selected_media_id']))
                url = Path(preview['wav']).relative_to(output).as_posix()
                choices.append(html.escape(label) + f'<br><audio controls preload="none" src="{html.escape(url, quote=True)}"></audio>')
            conditions = '<br>'.join(html.escape(' '.join(c['conditions']) or '未列条件') for c in group['combinations'])
            rows.append(f"<tr><td>{comparison['event_id']} / 组{group['group']}<br>{len(group['combinations'])}组条件</td>" +
                        '<td><details><summary>完整候选条件</summary>' + conditions + '</details><br>Media ' +
                        html.escape(str(group['candidate_media_ids'])) + '</td><td>' +
                        ('明确静音分支；不生成空白试听' if group['explicit_silence'] else '<br>'.join(choices)) + '</td></tr>')
    if not rows:
        return ''
    return ('<section id="branch-comparison"><h2>原第三项：分支对照</h2><p>同一个EventID的离线候选，不表示录制时实际选择。' +
            '相同渲染指令合并显示，随机组的每个素材分别试听；不是连续发生的多段音效。原第三项保留不变。</p>' +
            '<table><tr><th>分组</th><th>条件/媒体</th><th>对照试听</th></tr>' + ''.join(rows) + '</table></section>')


def audition_page(report, audition, output):
    names = sorted({e['action_name'] for e in report['episodes']})
    options = ''.join(f'<option>{html.escape(name)}</option>' for name in names)
    details = {e['event_id']: e for e in report['events']}
    rows = []
    # Put the three newly located contact candidates first, without assigning
    # them a material/damage type solely from their bank name.
    ordered = sorted(audition['previews'], key=lambda p: (not any(b.startswith('hit_') for b in p['banks']), -p['calls']))
    for preview in ordered:
        event = details[preview['event_id']]
        contexts = [(episode['action_name'], episode['motion'], episode['temporal_event_counts'].get(str(event['event_id']), 0))
                    for episode in report['episodes'] if episode['temporal_event_counts'].get(str(event['event_id']), 0)]
        context_names = sorted({item[0] for item in contexts})
        motions = sorted({item[1] for item in contexts})
        url = Path(preview['wav']).relative_to(output).as_posix()
        rows.append('<tr data-actions="' + html.escape('|'.join(context_names), quote=True) + '"><td>' +
                    '<br>'.join(html.escape(bank) for bank in preview['banks']) + f"</td><td>{event['event_id']}<br>{event['calls']}次请求</td><td>" +
                    html.escape('、'.join(context_names)) + '<details><summary>实际动画上下文</summary>' +
                    '<br>'.join(html.escape(motion) for motion in motions) + '</details></td><td>' +
                    html.escape(str(preview['conditions']) if preview['conditions'] else '无显式条件') +
                    f"<br>{preview['variant_count']}种离线组合；{preview['seconds']:.3f}s" +
                    ('<p>' + html.escape(preview['review_note']) + '</p>' if preview.get('review_note') else '') +
                    '<details><summary>候选MediaID</summary>' + html.escape(str(preview['media_ids'])) + '</details></td>' +
                    f'<td><audio controls preload="none" src="{html.escape(url, quote=True)}"></audio></td></tr>')
    unresolved = ''.join(f"<li>{row['event_id']}：{html.escape(row['reason'])}</li>" for row in audition['unresolved'])
    page = ('<!doctype html><meta charset="utf-8"><title>虫棍实录音效候选</title><style>body{font:16px sans-serif;background:#171b21;color:#eee;margin:24px}td,th{padding:12px;border-bottom:1px solid #53606c;text-align:left;vertical-align:top}table{border-collapse:collapse}select{padding:6px}details{margin-top:8px;color:#b9c5cf}audio{max-width:260px}</style>' +
           '<h1>虫棍实录音效候选</h1><p>这批声音的EventID确实在录制中提交过。动作栏仅表示同一时间段，不证明发声对象归属。' +
           '带条件的试听是可解码候选分支，不保证是当次游戏选中的型号、状态或随机声音。没有加入环境声、菜单声、人物语音和纯停止事件。' +
           '优先核对hit_pl候选是否为木桩接触音；不凭银行名字认定斩击/打击或肉质。</p>' +
           '<p>录制：' + html.escape(report['capture']) + f"；{report['audio_rows']}条请求 / {report['unique_events']}种事件；本页{len(audition['previews'])}项主试听。</p>" +
           ('<p><a href="#branch-comparison">查看原第三项的分支对照</a>（前3项顺序与音频保持不变）</p>' if audition.get('comparisons') else '') +
           '<label>按动作上下文筛选 <select id="action"><option value="">全部</option>' + options + '</select></label>' +
           '<table><thead><tr><th>来源</th><th>事件</th><th>发生时的动作</th><th>分支/媒体</th><th>试听</th></tr></thead><tbody>' + ''.join(rows) +
           '</tbody></table>' + comparison_html(audition, output) + '<h2>未生成试听</h2><ul>' + unresolved + '</ul>' +
           '<script>document.querySelector("#action").onchange=e=>document.querySelectorAll("tr[data-actions]").forEach(r=>r.hidden=!!e.target.value&&!r.dataset.actions.split("|").includes(e.target.value));</script>')
    (output / 'index.html').write_text(page, encoding='utf-8')


def persistent_summary(report, audition, output):
    if report['capture'] != audition['capture']:
        raise ValueError('Map and audition must refer to the same capture')
    contexts = {}
    for episode in report['episodes']:
        key = (episode['action_state_id'], episode['action_name'], episode['motion'])
        context = contexts.setdefault(key, Counter())
        context.update(episode['temporal_event_counts'])
    return {'schema': 'mhgz_rise_recorded_audio_reference_v1', 'capture': report['capture'],
            'input_files': report['input_files'], 'source_banks': report['source_banks'],
            **{key: report[key] for key in ('scan', 'audio_rows', 'unique_events', 'located_events', 'play_events', 'scope_candidates')},
            'events': report['events'],
            'action_contexts': [{'action_state_id': key[0], 'action_name': key[1], 'motion': key[2],
                                 'temporal_event_counts': dict(counts)} for key, counts in contexts.items()],
            'previews': audition['previews'], 'unresolved': audition['unresolved'],
            'comparisons': audition.get('comparisons', []),
            'metadata_refresh': audition.get('metadata_refresh'),
            'user_review': audition.get('user_review'),
            'local_evidence': {'map': (output / 'map.json').as_posix(), 'audition': (output / 'audition.json').as_posix(),
                               'page': (output / 'index.html').as_posix()},
            'limits': report['limits'] + ['local absolute paths require regeneration on another machine',
                                          'metadata only; no audio or raw capture rows in this reference'],
            'ue_imported': False, 'listening_accepted': audition.get('listening_accepted', False)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture', required=True)
    parser.add_argument('--directory', type=Path, default=recorder_dir())
    parser.add_argument('--source-root', type=Path, default=Path('D:/study/MHR/natives/STM'))
    parser.add_argument('--output', type=Path)
    parser.add_argument('--audition', action='store_true', help='Render one explicitly unconfirmed branch per in-scope recorded Play event')
    parser.add_argument('--audit-combat', action='store_true', help='Audit exact hit/object relations and scoped conditions; no decoding, publishing or game writes')
    parser.add_argument('--publish', action='store_true', help='Publish metadata only; reuse existing map/audition without scanning or decoding')
    parser.add_argument('--refresh-metadata', action='store_true', help='Refresh existing capture/M1 conditions without replacing or decoding their WAVs')
    parser.add_argument('--compare-event', type=int, help='Compare <=16 existing conditions of one recorded event, <=8 additional previews')
    args = parser.parse_args()
    output = checked_output(args.output or WORK / 'CaptureMap' / args.capture)
    checked_inputs(args.source_root.resolve(), args.directory.resolve(), output)
    if args.audit_combat:
        if args.audition or args.publish or args.refresh_metadata or args.compare_event:
            parser.error('--audit-combat cannot render or publish/replace existing previews')
        if not re.fullmatch(r'mhrise_\d{8}_\d{6}_\d+',args.capture):
            parser.error('Expected a recorder capture basename')
        bank = args.source_root.resolve() / 'Sound/Wwise/hit_pl.bnk.2.X64'
        if not bank.is_file() or bank.stat().st_size > 4*1024*1024:
            raise ValueError('Expected bounded hit_pl metadata bank')
        nodes = parse_sources([{'source':str(bank)}])
        from rise_audio_combat import audit_combat
        report = audit_combat(args.directory.resolve(),args.capture,nodes)
        report['bank'] = {'path':bank.as_posix(),'sha256':m1.sha256(bank)}
        target = output / 'combat-audit.json'
        if target.exists():
            raise FileExistsError('Preserve previous audit; choose a new --output')
        m1.write_json(target,report)
        print(json.dumps({'capture':args.capture,'summary':report['summary'],'report':str(target)},ensure_ascii=False))
        return
    if args.publish or args.refresh_metadata or args.compare_event:
        if args.audition:
            parser.error('Cached metadata/comparison operations cannot be combined with --audition')
        report = json.loads((output / 'map.json').read_text(encoding='utf-8'))
        result = json.loads((output / 'audition.json').read_text(encoding='utf-8'))
        if report['capture'] != args.capture:
            raise ValueError('Requested capture differs from saved map')
        if result['capture'] != report['capture']:
            raise ValueError('Map and audition refer to different captures')
        if args.refresh_metadata:
            result = refresh_saved_metadata(result)
            m1.write_json(output / 'audition.json', result)
        if args.compare_event:
            previous = next((c for c in result.get('comparisons', []) if c['event_id'] == args.compare_event), None)
            if previous:
                for group in previous['groups']:
                    for preview in group['previews']:
                        m1.refreshed_preview(preview)  # verify before reusing
                comparison = previous
            else:
                comparison = compare_event(report, result, args.compare_event, args.source_root.resolve(), output)
            result['comparisons'] = [c for c in result.get('comparisons', []) if c['event_id'] != args.compare_event] + [comparison]
            m1.write_json(output / 'audition.json', result)
        if args.publish:
            target = PROJECT / 'docs/reference/虫棍音效运行时映射.json'
            m1.write_json(target, persistent_summary(report, result, output))
            print(target)
        audition_page(report, result, output)
        return
    report = map_capture(args.source_root.resolve(), args.directory.resolve(), args.capture, output)
    print(json.dumps({key: report[key] for key in ('scan', 'audio_rows', 'unique_events', 'located_events', 'play_events', 'scope_candidates')}, indent=2))
    for event in report['events']:
        print(event['event_id'], event['calls'],
              [(s['bank'], [a['action_type'] for a in s['actions']]) for s in event['sources']])
    if args.audition:
        result = audition_capture(report, args.source_root.resolve(), output)
        print(json.dumps({'previews': len(result['previews']), 'unresolved': len(result['unresolved']),
                          'index': (output / 'index.html').as_posix()}, ensure_ascii=False))


if __name__ == '__main__':
    main()
