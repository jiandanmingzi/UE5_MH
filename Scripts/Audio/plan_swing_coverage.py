"""Freeze M3 recipes from the refreshed snapshot and existing capture mapping.

Pure Python, no UE/game mutation. Writes Saved/Audio/RiseIG/M3 only. This is
recorded-event coverage, not fabricated audio for clips lacking observations.
"""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'Saved/Audio/RiseIG/M3'
EVENTS = (2231624382, 2702726931, 2037497988, 106259332, 1392529433)
BASE = '/Game/Weapons/InsectGlaive/Audio'


def fingerprint(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, ensure_ascii=False).encode('utf-8')).hexdigest()


def short(path):
    return path.rsplit('/', 1)[-1].split('.')[0]


def make_plan(snapshot, positions, audition):
    if not snapshot['success'] or not snapshot['content_unchanged']:
        raise ValueError('Require current, successful read-only snapshot')
    sources = []
    for event in EVENTS:
        row = next(r for r in audition['previews'] if r['event_id'] == event)
        review = row['semantic_review']
        if (review['status'] != 'user_confirmed' or review['reviewed_wav_sha256'] != row['sha256']
                or row['channels'] != 1 or row['sample_rate'] != 48000):
            raise ValueError('Unconfirmed/changed/unsupported source ' + str(event))
        sources.append({k: row[k] for k in ('event_id', 'wav', 'sha256', 'seconds', 'channels', 'sample_rate')})
        sources[-1].update(wave=f'{BASE}/Waves/SW_IG_HuiJi_{event}',
                           cue=f'{BASE}/Cues/SC_IG_HuiJi_{event}')
    snapshots = {m['path']: m for m in snapshot['montages']}
    montages, skipped = [], []
    for montage in positions['montages']:
        recipes = []
        suffix = short(montage['path']).removeprefix('AM_IG_')
        for index, point in enumerate(montage['candidates']):
            time = point['montage_time_estimate_s']
            if point['cut_boundary_ambiguous']:
                skipped.append(dict(point, montage=montage['path'], reason='ambiguous_cut'))
                continue
            if not 0 <= time < montage['length_s'] or point['event_id'] not in EVENTS:
                raise ValueError('Invalid source event or Montage time')
            if not any(any(lo <= time < hi for lo, hi in r['intervals']) for r in montage['routes']):
                raise ValueError('Candidate has no reachable Section route')
            cluster_index = point['cluster_id'].rsplit(':', 1)[-1]
            key = (f'Audio.Swing.IG.{suffix}.S{point["segment_index"]}.L{point["loop"]}'
                   f'.E{point["event_id"]}.C{cluster_index}')
            if suffix == 'TuCi':
                # Preserve the accepted pilot keys, time and hard sources exactly.
                if [p['event_id'] for p in montage['candidates']] != [2702726931, 2231624382]:
                    raise ValueError('TuCi pilot changed; explicit review required')
                key = 'Audio.Swing.IG.TuCi.' + ('First' if index == 0 else 'Second')
            recipes.append(dict(point, event_tag=key))
        if recipes:
            keys = [r['event_tag'] for r in recipes]
            if len(keys) != len(set(keys)):
                raise ValueError('Duplicate recipe identity')
            montages.append({'path': montage['path'], 'before': snapshots[montage['path']],
                             'recipes': recipes, 'routes': montage['routes'],
                             'missing_evidence': montage['missing_evidence']})
    selected = {m['path'] for m in montages}
    abilities = [dict(a, audio_identity='Audio.Swing.IG.' + short(a['path']).removeprefix('GA_IG_'))
                 for a in positions['abilities'] if selected.intersection(a['montages'])]
    if any(not any(m['path'] in a['montages'] for a in abilities) for m in montages):
        raise ValueError('Do not author an unowned/unused Montage')
    tags = sorted({a['audio_identity'] for a in abilities}
                  | {r['event_tag'] for m in montages for r in m['recipes']})
    result = {'schema': 'mhgz_swing_coverage_m3_v1', 'sources': sources,
              'montages': montages, 'abilities': abilities, 'required_tags': tags,
              'skipped': skipped, 'protected_package_hashes': snapshot['protected_package_hashes'],
              'capture_inputs': [{'capture': c['capture'], 'inputs': c['inputs']} for c in positions['captures']],
              'missing_evidence': [{'montage': m['path'], **r}
                                   for m in positions['montages'] for r in m['missing_evidence']],
              'limits': positions['limits'], 'complete_original_audio_coverage': False}
    result['plan_sha256'] = fingerprint(result)
    return result


def main():
    snapshot_path = ROOT / 'Saved/Audio/RiseIG/SwingMap/asset-snapshot.json'
    positions_path = snapshot_path.with_name('positions.json')
    snapshot = json.loads(snapshot_path.read_text(encoding='utf-8'))
    positions = json.loads(positions_path.read_text(encoding='utf-8'))
    if hashlib.sha256(snapshot_path.read_bytes()).hexdigest() != positions['asset_snapshot_sha256']:
        raise ValueError('Position map and current snapshot disagree')
    audition = json.loads((ROOT / 'Saved/Audio/RiseIG/CaptureMap/mhrise_20261018_151536_02/audition.json').read_text(encoding='utf-8'))
    plan = make_plan(snapshot, positions, audition)
    for path, sha in plan['protected_package_hashes'].items():
        if hashlib.sha256(Path(path).read_bytes()).hexdigest() != sha:
            raise ValueError('Stale package ' + path)
    for source in plan['sources']:
        if hashlib.sha256(Path(source['wav']).read_bytes()).hexdigest() != source['sha256']:
            raise ValueError('Changed source WAV')
    for capture in plan['capture_inputs']:
        for item in capture['inputs'].values():
            if hashlib.sha256(Path(item['path']).read_bytes()).hexdigest() != item['sha256']:
                raise ValueError('Changed capture input')
    OUT.mkdir(parents=True, exist_ok=True)
    filename = OUT / 'plan.json'
    if filename.exists() and json.loads(filename.read_text(encoding='utf-8'))['plan_sha256'] != plan['plan_sha256']:
        raise ValueError('Existing frozen M3 plan differs; inspect rather than overwrite')
    filename.write_text(json.dumps(plan, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({'plan_sha256': plan['plan_sha256'], 'montages': len(plan['montages']),
                      'abilities': len(plan['abilities']), 'points': sum(len(m['recipes']) for m in plan['montages']),
                      'required_tags': plan['required_tags'], 'skipped': len(plan['skipped'])}))


if __name__ == '__main__':
    main()
