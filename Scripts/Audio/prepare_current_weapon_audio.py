"""Prepare current-weapon audio; default reconciles ONLY the former 70 tool points.

No UE/game writes. Reuses the finite vgmstream/TXTP path; checkpoints every WAV.
--expand-weapon reuses the completed pool and freezes repeated, actual-GA-route
weapon observations in a distinct append-only plan. Other lifecycle layers stay open.
"""
from collections import defaultdict
import argparse
import copy
import hashlib
import json
from pathlib import Path
import re
import sys

import rise_audio_capture_map as captures
import rise_audio_m1 as m1
from plan_swing_coverage import fingerprint
import map_swing_positions as positions

ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / 'Saved/Audio/RiseIG/Weapon20261019'
OUT = BASE / 'UEAudio'
AUDIO = '/Game/Weapons/InsectGlaive/Audio'
sys.path.insert(0, str(ROOT / 'Scripts'))
from ue_asset_utils import decode_native_export, export_records


def load(path):
    return json.loads(path.read_text(encoding='utf-8'))


def choices(branch):
    path = Path(branch['txtp'])
    if m1.sha256(path) != branch['txtp_sha256']:
        raise ValueError('Mapped source graph changed')
    variant = m1.describe_txtp(path, {})
    if (variant['event_id'] != branch['event_id'] or variant['conditions'] != branch['conditions']
            or variant['render_lines'] != branch['render_lines']):
        raise ValueError('Mapped graph identity/conditions changed')
    if len(variant['render_lines']) == 1:
        if len(variant['media_ids']) != 1 or not re.match(r'\s*wem/\d+\.wem\s', variant['render_lines'][0]):
            raise ValueError('Only explicit single sources or flat random graphs supported')
        return [dict(selection=1, selected_media_id=variant['media_ids'][0],
                     body=path.read_text(encoding='utf-8-sig'))]
    return captures.random_comparison_bodies(variant)


def point_key(row):
    return (row['event_id'], row['sequence'], row['slot'], row['segment_index'], row['loop'])


def reconcile(old_plan, mapping):
    """Correspondence by same clip/event occurrence count, never nearest-time guessing.

    A consistent clock/entry shift is permitted. Unequal counts, multiple branches,
    cut boundaries, missing observations and unreachable routes are not guessed.
    """
    current = {m['path']: m for m in mapping['montages']}
    montages = []
    for old in old_plan['montages']:
        old_groups, new_groups = defaultdict(list), defaultdict(list)
        for row in old['recipes']:
            old_groups[point_key(row)].append(row)
        for row in current.get(old['path'], {}).get('candidates', []):
            if not row['cut_boundary_ambiguous'] and row['layer'] in ('武器层', '通用武器层'):
                new_groups[point_key(row)].append(row)
        recipes, retired = [], []
        for key, previous in old_groups.items():
            candidates = sorted(new_groups[key], key=lambda r: r['montage_time_estimate_s'])
            previous = sorted(previous, key=lambda r: r['montage_time_estimate_s'])
            unique = len({r['branch_id'] for r in candidates}) == 1
            if len(candidates) != len(previous) or not unique:
                retired.extend(dict(r, reason='missing_or_ambiguous_current_occurrence_count',
                                    candidate_count=len(candidates)) for r in previous)
                continue
            for before, after in zip(previous, candidates):
                time = after['montage_time_estimate_s']
                if not 0 <= time < old['before']['length_s'] or not any(
                        lo <= time < hi for route in old['routes'] for lo, hi in route['intervals']):
                    raise ValueError('Current point is outside actual reachable Montage route')
                recipes.append(dict(after, event_tag=before['event_tag'], previous_time_s=before['montage_time_estimate_s']))
        montages.append(dict(path=old['path'], before=old['before'], recipes=recipes,
                             retired=retired, routes=old['routes']))
    return montages


def media_index(source, branches):
    """Supplement only named banks used by this map; keep the old M1 index intact."""
    result = load(captures.WORK / 'Inventory/media_locations.json')['media']
    banks = sorted({Path(b['txtp']).parent.name for b in branches})
    root = source / 'Sound/Wwise'
    paths = {root / (bank + suffix + '.bnk.2.X64') for bank in banks for suffix in ('', '_media')}
    if 'pl_equip' in banks:
        # pl_equip is the routing bank; limb/material media live in pl_eq_*.
        paths.update(root.glob('pl_eq_*_media.bnk.2.X64'))
    added = []
    for path in sorted(paths):
        if not path.is_file():
            continue
        sha = m1.sha256(path)
        index = m1.bank_index(path.read_bytes())
        added.append(dict(source=path.as_posix(), sha256=sha, media_count=len(index['media'])))
        for row in index['media']:
            records = result.setdefault(str(row['media_id']), [])
            if not any(r['source'] == path.as_posix() for r in records):
                records.append(dict(row, source=path.as_posix(), source_sha256=sha))
    m1.write_json(OUT / 'media-index.json', dict(banks=added, media=result))
    return result


def prepare(source):
    mapping_path = BASE / 'MappingFinal/mapping.json'
    mapping = load(mapping_path)
    if mapping['capture'] != 'mhrise_20261019_175309_05':
        raise ValueError('Wrong primary capture')
    for path, sha in mapping['inputs'].items():
        if m1.sha256(Path(path)) != sha:
            raise ValueError('Mapping input changed: ' + path)
    checkpoint_path = OUT / 'decode-checkpoint.json'
    checkpoint = load(checkpoint_path) if checkpoint_path.exists() else {
        'mapping_sha256': m1.sha256(mapping_path), 'variants': {}, 'copies': {}, 'extracted': {}}
    if checkpoint['mapping_sha256'] != m1.sha256(mapping_path):
        raise ValueError('Decode checkpoint belongs to another map')
    locations = media_index(source, mapping['branches'])
    sources = []
    for branch in mapping['branches']:
        variants = []
        for choice in choices(branch):
            identity = branch['id'] + ':' + str(choice['selection'])
            preview = checkpoint['variants'].get(identity)
            if preview:
                if m1.sha256(Path(preview['wav'])) != preview['sha256']:
                    raise ValueError('Previously decoded WAV changed')
            else:
                path = OUT / 'Decode/Input' / f"{branch['id']}-{choice['selection']}.txtp"
                if path.exists() and path.read_text(encoding='utf-8') != choice['body']:
                    raise ValueError('Different existing selected TXTP')
                path.parent.mkdir(parents=True, exist_ok=True)
                if not path.exists():
                    path.write_text(choice['body'], encoding='utf-8')
                event = dict(event_id=branch['event_id'], calls=0,
                             sources=[dict(bank=Path(branch['txtp']).parent.name)])
                preview = captures.render_preview(m1.describe_txtp(path, {}), event, source,
                            OUT / 'Decode', checkpoint['copies'], checkpoint['extracted'], len(choices(branch)), locations)
                preview.update(selected_media_id=choice['selected_media_id'], selection=choice['selection'])
                checkpoint['variants'][identity] = preview
                m1.write_json(checkpoint_path, checkpoint)
            if preview['channels'] not in (1, 2) or preview['sample_rate'] != 48000:
                raise ValueError('Unexpected decoded channel/rate')
            variants.append(dict(preview, wave=f"{AUDIO}/Waves/MuGu/SW_IG_MuGu_{choice['selected_media_id']}_{preview['sha256'][:8]}"))
        sources.append(dict(branch_id=branch['id'], event_id=branch['event_id'], layer=branch['layer'],
                            conditions=branch['conditions'], txtp_sha256=branch['txtp_sha256'], variants=variants,
                            cue=f"{AUDIO}/Cues/MuGu/SC_IG_MuGu_{branch['event_id']}_{branch['txtp_sha256'][:8]}"))
    old_dir = ROOT / 'Saved/Audio/RiseIG/M3'
    old = load(old_dir / 'plan.json')
    audit = load(old_dir / 'audit.json')
    state = load(old_dir / 'manifest.json')
    if not audit['linked'] or state['phase'] != 'independently_reloaded':
        raise ValueError('Require saved/reloaded original author baseline')
    protected = dict(state['expected_hashes'])
    for folder in ('Content/Weapons/InsectGlaive/Audio', 'Content/GameplayCues'):
        for path in (ROOT / folder).rglob('*.uasset'):
            # Do not accidentally bless outputs from an already-started transaction.
            if '/MuGu/' not in path.as_posix():
                protected.setdefault(path.as_posix(), m1.sha256(path))
    montages = reconcile(old, mapping)
    previous = {m['path']: m['authored'] for m in audit['montages']}
    for montage in montages:
        montage['previous_authored'] = previous[montage['path']]
    plan = dict(schema='mhgz_current_weapon_audio_v1', capture=mapping['capture'],
                mapping_sha256=m1.sha256(mapping_path), sources=sources, montages=montages,
                protected_package_hashes=protected, required_tags=old['required_tags'],
                limits=['candidate timing, not exact original Notify frames',
                        'flat random alternatives use UE random, not original weights/history',
                        'only former tool points reconciled; other map/runtime layers not wired',
                        'old generic weapon hit Cue unchanged; current conditions unresolved'])
    plan['plan_sha256'] = fingerprint(plan)
    filename = OUT / 'plan.json'
    if filename.exists() and load(filename) != plan:
        raise ValueError('Frozen plan already exists and differs')
    if not filename.exists():
        for path, sha in protected.items():
            if m1.sha256(Path(path)) != sha:
                raise ValueError('Stale protected package: ' + path)
        m1.write_json(filename, plan)
    print(json.dumps(dict(graphs=len(sources), variants=sum(len(s['variants']) for s in sources),
        unique_waves=len({v['wave'] for s in sources for v in s['variants']}),
        montages=len(montages), points=sum(len(m['recipes']) for m in montages),
        retired=sum(len(m['retired']) for m in montages))))


def playback_routes(snapshot):
    """Real GA defaults/Combo entries, secondary clips and verified hold release.

    The JueChongJi edge mirrors ApplyHoldReleaseRouteIfReady, not a guessed
    timer: Startup193/hold can go to Send195, which follows its authored chain.
    """
    by_path = {m['path']: m for m in snapshot['montages']}
    result = defaultdict(list)
    for ability in snapshot['abilities']:
        for path in ability['montages']:
            montage = by_path[path]
            entries = {None}
            if path == ability['primary_montage']:
                config = ability['entry_config']
                entries = {positions.entry_for(config, '', '')}
                entries.update(positions.entry_for(config, edge['transition_id'], edge['source_state'])
                    for edge in snapshot['combo_transitions'] if edge['ability_class'] == ability['class'])
            if path.endswith('/AM_IG_JueChongJi.AM_IG_JueChongJi'):
                if {s['name'] for s in montage['sections']} != {
                        'Startup193', 'Hold193Loop', 'Send195', 'PreFlight196', 'HunterFlight199'}:
                    raise ValueError('Awakened runtime release contract changed')
                entries.add('Send195')
            for entry in sorted(entries, key=lambda x: x or ''):
                route = positions.route(montage, entry)
                if route['status'] in ('invalid_entry', 'missing_next_section'):
                    raise ValueError('Invalid actual entry: ' + path)
                result[path].append(dict(route, ability=ability['path'], runtime_time='actual_traversal_only'))
    return result


def expansion_candidates(candidates, routes, baseline, clusters, montage,
                         layers=('武器层', '通用武器层'), namespace='Audio.Swing.IG'):
    """Append supported weapon points; never reinterpret or replace the saved 59.

    Require >=2 observations and no nearby competing occurrence/branch within
    one target 60Hz frame. Sparse or clock-split candidates stay explicitly open.
    """
    retained = {(r['cluster_id'], r['slot'], r['segment_index'], r['loop']) for r in baseline}
    new, skipped = [], []
    weapon = [r for r in candidates if r['layer'] in layers]
    for row in weapon:
        identity = (row['cluster_id'], row['slot'], row['segment_index'], row['loop'])
        if identity in retained:
            continue
        reason = None
        time = row['montage_time_estimate_s']
        if row['cut_boundary_ambiguous']:
            reason = 'source_cut_boundary'
        elif row['observations'] < 2:
            reason = 'single_observation_not_new_authoring_evidence'
        elif any(other is not row and point_key(other) == point_key(row)
                 and abs(other['montage_time_estimate_s'] - time) < 1 / 60
                 for other in weapon):
            reason = 'nearby_occurrence_or_branch_ambiguity'
        elif not any(positions.reachable(time, r) for r in routes):
            reason = 'outside_actual_ga_routes'
        else:
            cluster = clusters[row['cluster_id']]
            segment = next(s for s in montage['segments'] if (s['slot'], s['index']) == (row['slot'], row['segment_index']))
            delta = [abs(frame - cluster['native_frame_estimate']) * 2 / row['calibration']['ue_fps']
                     / abs(segment['effective_rate']) for frame in cluster['native_frame_range']]
            bounds = [time - delta[0], time + delta[1]]
            cuts = sorted({s['time_s'] for s in montage['sections']})
            if any(bounds[0] < cut <= bounds[1] for cut in cuts):
                reason = 'section_boundary_clock_range'
        if reason:
            skipped.append(dict(row, reason=reason))
        else:
            suffix = montage['path'].split('.')[-1].removeprefix('AM_IG_')
            cluster_index = row['cluster_id'].rsplit(':', 1)[-1]
            tag = (f'{namespace}.{suffix}.Current.S{row["segment_index"]}.L{row["loop"]}'
                   f'.E{row["event_id"]}.{row["branch_id"]}.C{cluster_index}')
            new.append(dict(row, event_tag=tag))
    return new, skipped


def expand_weapon(actions=False):
    """Reuse the completed source pool; freeze a distinct append-only transaction."""
    previous_output = BASE / 'UEWeaponCoverage' if actions else OUT
    output = BASE / ('UEActionRuntime' if actions else 'UEWeaponCoverage')
    layers = ('地面/动作附加层',) if actions else ('武器层', '通用武器层')
    filename = output / 'plan.json'
    if filename.exists():
        raise ValueError('Expansion already frozen; use its executor/audit, not replan')
    mapping_path = BASE / 'MappingFinal/mapping.json'
    mapping, old, state, audit = (load(mapping_path), load(previous_output / 'plan.json'),
                                 load(previous_output / 'manifest.json'), load(previous_output / 'audit.json'))
    if not audit['linked'] or state['phase'] != 'independently_reloaded':
        raise ValueError('Require completed source-pool transaction')
    snapshot_path = ROOT / 'Saved/Audio/RiseIG/SwingMap/asset-snapshot.json'
    if m1.sha256(snapshot_path) != mapping['asset_snapshot_sha256']:
        raise ValueError('Static animation/entry baseline changed')
    snapshot = load(snapshot_path)
    protected = {**snapshot['protected_package_hashes'], **state['expected_hashes']}
    for path, sha in {**mapping['inputs'], **protected}.items():
        if m1.sha256(Path(path)) != sha:
            raise ValueError('Changed input/package: ' + path)
    routes = playback_routes(snapshot)
    old_montages = {m['path']: m for m in old['montages']}
    old_authored = {m['path']: m['authored'] for m in audit['montages']}
    by_path = {m['path']: m for m in snapshot['montages']}
    clusters = {c['id']: c for c in mapping['clusters']}
    montages, skipped = [], []
    for mapped in mapping['montages']:
        path = mapped['path']
        previous = old_montages.get(path, {}).get('recipes', [])
        if path not in routes:
            skipped.extend(dict(r, montage=path, reason='no_exact_ga_montage_owner') for r in mapped['candidates']
                           if r['layer'] in layers)
            continue
        added, isolated = expansion_candidates(mapped['candidates'], routes[path], previous, clusters, by_path[path],
                            layers, 'Audio.Action.IG' if actions else 'Audio.Swing.IG')
        skipped.extend(dict(r, montage=path) for r in isolated)
        if not added:
            continue
        before = copy.deepcopy(by_path[path])
        if path in old_montages:
            native = previous_output / 'NativeExports/exports' / (path.replace('/Game/', '').replace('/', '__') + '.copy')
            before['existing_notifies'] = export_records(decode_native_export(native.read_bytes()), 'Notifies')
            if not {r['guid'] for r in old_authored[path]}.issubset({r['Guid'] for r in before['existing_notifies']}):
                raise ValueError('Last independent export does not contain saved tool GUIDs')
        montages.append(dict(path=path, before=before, recipes=previous + added,
            previous_authored=old_authored.get(path, []), retired=[], routes=routes[path], added_count=len(added)))
    selected = {m['path'] for m in montages}
    abilities = [dict(a, audio_identity='Audio.Swing.IG.' + a['path'].split('.')[-1].removeprefix('GA_IG_'))
                 for a in snapshot['abilities'] if selected.intersection(a['montages'])]
    required = sorted({r['event_tag'] for m in montages for r in m['recipes']} | {a['audio_identity'] for a in abilities})
    config = ROOT / 'Config/DefaultGameplayTags.ini'
    text = config.read_text(encoding='utf-8')
    existing = set(re.findall(r'\+GameplayTagList=\(Tag="([^"]+)"', text))
    missing = [t for t in required if t not in existing]
    block = '\n; Current-weapon recorded ' + ('action' if actions else 'swing') + ' expansion 2026-10-19; independent of damage windows.\n' + ''.join(
        f'+GameplayTagList=(Tag="{tag}",DevComment="当前武器实录音效事件；独立于伤害段")\n' for tag in missing)
    result = dict(schema='mhgz_current_weapon_audio_expansion_v1', capture=mapping['capture'],
        mapping_sha256=m1.sha256(mapping_path), sources=old['sources'], montages=montages,
        abilities=abilities, protected_package_hashes=protected, preserve_previous=True,
        required_tags=required, new_tags=missing, skipped=skipped,
        tag_config_before_sha256=m1.sha256(config),
        tag_config_after_text_sha256=hashlib.sha256((text + block).encode('utf-8')).hexdigest(),
        runtime_contract_sha256=m1.sha256(ROOT / 'Source/MHGZ/ActionSystem/MHGZAwakenedKinsectAttackAbility.cpp'),
        limits=['timing candidates, not exact original Notify frames',
                'observed flat random alternatives use UE random, not original weights/history',
                'only exact GA-owned weapon layers; runtime hit/kinsect/buff/movement not wired',
                'new points require repeated observations; sparse/boundary/nearby alternatives remain open'])
    if actions:
        config_path = '/Game/Weapons/InsectGlaive/Data/DA_IG_Combat.DA_IG_Combat'
        disk = ROOT / ('Content/' + config_path.split('.')[0].removeprefix('/Game/') + '.uasset')
        result['protected_package_hashes'][disk.as_posix()] = m1.sha256(disk)
        pool = {s['branch_id']: s for s in old['sources']}
        result['runtime_audio'] = dict(config=config_path, loop_source=pool['B020'],
            loop_cue=AUDIO + '/Cues/MuGu/SC_IG_LieChong_FlightLoop_2640762921',
            properties={'kinsect_launch_sound': pool['B021']['cue'],
                        'kinsect_flight_loop_sound': AUDIO + '/Cues/MuGu/SC_IG_LieChong_FlightLoop_2640762921',
                        'extract_collected_sound': pool['B026']['cue']},
            semantic_limits=['2640762921 is a recorded loop stopped by3041892567',
                             '1805087271 adapted to successful departure, not every unexplained request',
                             '3971364902 is the user-confirmed shared delivery sound',
                             '603272525/539819764 and triple/expiry sounds not assigned without semantic evidence'])
        result['code_sha256'] = {str(ROOT / path): m1.sha256(ROOT / path) for path in (
            'Source/MHGZ/InsectGlaive/Kinsect/Kinsect.cpp',
            'Source/MHGZ/InsectGlaive/Kinsect/Kinsect.h',
            'Source/MHGZ/InsectGlaive/InsectGlaiveCombatConfig.h',
            'Source/MHGZ/AttributeSystem/Res_InsectGlaive.cpp',
            'Source/MHGZ/AttributeSystem/Res_InsectGlaive.h')}
        result['limits'] = ['repeated exact-GA-owned action additions; not ordinary MM footsteps',
            'free-flight/hover/return loop is a project lifecycle adaptation, not final Media proof',
            'equipment/unknown kinsect contact/triple/powder/propulsion layers still open']
    result['plan_sha256'] = fingerprint(result)
    m1.write_json(filename, result)
    (output / 'tags-config-before.ini').write_bytes(config.read_bytes())
    (output / 'tags-block.txt').write_text(block, encoding='utf-8')
    print(json.dumps(dict(montages=len(montages), abilities=len(abilities), new_tags=len(missing),
        added_points=sum(m['added_count'] for m in montages),
        retained_points=sum(len(m['previous_authored']) for m in montages), isolated=len(skipped),
        used_events=sorted({r['event_id'] for m in montages for r in m['recipes']}))))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, default=Path('D:/study/MHR/natives/STM'))
    parser.add_argument('--expand-weapon', action='store_true', help='Reuse decoded pool; freeze append-only weapon coverage')
    parser.add_argument('--expand-actions', action='store_true', help='Reuse weapon transaction; add action props and runtime kinsect configuration')
    parser.add_argument('--propulsion', action='store_true', help='Prepare one confirmed FX event; preserve historical source pools')
    parser.add_argument('--kinsect-contact', action='store_true', help='Reuse resident source; prepare bounded kinsect contact evidence')
    parser.add_argument('--powder-reviewed', action='store_true', help='After explicit listening approval, decode all flat alternatives of the four AerialPowder20261023 candidates')
    parser.add_argument('--capture-directory', type=Path, help='Explicit recorder directory for scoped propulsion/contact preparation')
    args = parser.parse_args()
    if args.powder_reviewed:
        if args.expand_weapon or args.expand_actions or args.propulsion or args.kinsect_contact:
            parser.error('--powder-reviewed cannot combine other stages')
        from rise_audio_cloth_comparison import prepare_powder_sources
        prepare_powder_sources(args.source_root.resolve())
        sys.exit(0)
    if args.kinsect_contact:
        if args.expand_weapon or args.expand_actions or args.propulsion or not args.capture_directory:
            parser.error('--kinsect-contact requires --capture-directory and cannot combine other stages')
        from prepare_kinsect_contact_audio import run
        run(args.capture_directory.resolve())
        sys.exit(0)
    if args.propulsion:
        if args.expand_weapon or args.expand_actions or not args.capture_directory:
            parser.error('--propulsion requires --capture-directory and cannot combine other stages')
        from prepare_propulsion_audio import run
        run(args.source_root.resolve(), args.capture_directory.resolve())
        sys.exit(0)
    if args.expand_weapon and args.expand_actions:
        parser.error('Choose only one expansion stage')
    expand_weapon(args.expand_actions) if args.expand_weapon or args.expand_actions else prepare(args.source_root)
