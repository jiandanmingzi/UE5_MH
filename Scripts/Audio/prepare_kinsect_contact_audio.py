"""Bounded evidence/source preparation for the existing kinsect contact Cue.

Reuse current-weapon source pool and strict emitter ownership, not new decoding.
Timing co-occurrence is labelled evidence, never an exact hit/audio transaction.
"""
from pathlib import Path
import re

import rise_audio_weapon_mapping as mapping
from rise_audio_coverage import read_rows

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'Saved/Audio/RiseIG/KinsectContact20261020'
BASE = ROOT / 'Saved/Audio/RiseIG/Weapon20261019'
EVENT = 603272525


def evidence(hits, contexts, objects, posts):
    kinsect_hits = [h for h in hits if h['kind'] == 'damage_side_callback'
                   and re.fullmatch(r'IG_Ins\d+', h.get('attacker_name', ''))]
    actors = {mapping.pointer(h['attacker_address']) for h in kinsect_hits}
    if len(actors) != 1 or None in actors:
        raise ValueError('Require one recorded kinsect attacker; no hunter/companion borrowing')
    scoped = mapping.scoped_posts(contexts, objects, posts, next(iter(actors)))
    selected = [p for p in scoped if int(p['event_id']) == EVENT]
    if len(selected) < 2 or any(p['owner_relation'] != 'master_emitter_exact' for p in selected):
        raise ValueError('Require repeated exact kinsect emitter evidence')
    observations = []
    for post in selected:
        # A diagnostic observation, NOT a causal hit pairing or playback clock.
        neighbors = [h['row'] for h in kinsect_hits
                     if abs(float(h['capture_time_last_sample_s']) - float(post['capture_time_estimate_s'])) < .04]
        observations.append(dict(audio_row=post['row'], emitter_id=post['game_object_id'],
                                 nearby_hit_rows=neighbors, temporal_only=True))
    if any(len(o['nearby_hit_rows']) != 1 for o in observations):
        raise ValueError('Ambiguous/missing repeated contact co-occurrence; review evidence')
    return dict(event_id=EVENT, kinsect_actor=next(iter(actors)),
                exact_emitter_posts=len(selected), kinsect_hit_rows=len(kinsect_hits),
                observations=observations, inference='repeated_kinsect_contact_accompaniment_not_exact_transaction',
                universal_original_hit_coverage_proven=False,
                unknown_539819764_or_powder_assigned=False)


def run(directory):
    target = OUT / 'plan.json'
    if target.exists():
        plan = mapping.load(target)
        for p, sha in plan['inputs'].items():
            if mapping.m1.sha256(Path(p)) != sha:
                raise ValueError('Frozen kinsect contact input changed: ' + p)
        print('KINSECT_CONTACT retained ' + str(target))
        return
    capture = 'mhrise_20261019_175309_05'
    paths = {k: directory / f'{capture}_{k}.csv' for k in ('hits', 'audio_context', 'audio_objects', 'audio_native')}
    inputs = {str(p): mapping.m1.sha256(p) for p in paths.values()}
    data = {k: read_rows(p) for k, p in paths.items()}
    report = evidence(data['hits'], data['audio_context'], data['audio_objects'], data['audio_native'])
    pool = BASE / 'UEAudio/plan.json'
    mapped = BASE / 'MappingFinal/mapping.json'
    source = next(s for s in mapping.load(pool)['sources'] if s['event_id'] == EVENT)
    branch = next(b for b in mapping.load(mapped)['branches'] if b['event_id'] == EVENT)
    if len(source['variants']) != 1 or source['conditions'] or len(branch['media_ids']) != 1:
        raise ValueError('Require unique unconditional current source, not a guessed branch')
    for p in (pool, mapped, Path(branch['txtp'])):
        inputs[str(p)] = mapping.m1.sha256(p)
    if inputs[str(Path(branch['txtp']))] != source['txtp_sha256']:
        raise ValueError('Source graph changed')
    variant = source['variants'][0]
    if not variant['non_silent'] or variant['channels'] != 1:
        raise ValueError('Expected non-silent mono resident source')
    inputs[variant['wav']] = variant['sha256']
    for p, sha in inputs.items():
        if mapping.m1.sha256(Path(p)) != sha:
            raise ValueError('Input changed during preparation: ' + p)
    result = dict(capture=capture, source=source, evidence=report, inputs=inputs,
        gameplay_cue='/Game/GameplayCues/GCN_Hit_Kinsect', tag='GameplayCue.Hit.Kinsect',
        adaptation='one generic contact layer per accepted Kinsect hit; original piercing layers remain unproven',
        powder_sound_ready=False, montage_changes=False, decoder_or_recorder_changed=False)
    mapping.m1.write_json(target, result)
    print('KINSECT_CONTACT_PREPARE ' + str(report['exact_emitter_posts']) + ' exact kinsect posts; ' + str(target))
