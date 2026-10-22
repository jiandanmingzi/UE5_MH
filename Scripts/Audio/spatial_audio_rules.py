"""Pure recipe rules for the scoped spatial successor, not another audio mapper."""
import copy
import re

GROUND_BRANCHES = {'B008', 'B016'}
ACTION_BRANCHES = {'B008', 'B011', 'B015', 'B016', 'B031', 'B033'}
SOUND_TRACKS = {
    'AnimNotify_SwingSound': 'SwingAudio',
    'AnimNotify_ActionSound': 'ActionAudio',
    'AnimNotify_DustSound': 'DustAudio',
    'AnimNotify_GroundDust': 'DustAudio',  # retained track/index, now an effect event
    'AnimNotify_GroundImpact': 'ImpactAudio',
    'AnimNotify_LandingSound': 'LandingAudio',
    'AnimNotify_LandingEvent': 'LandingAudio',
    'AnimNotify_ClothSound': 'ClothAudio',
}


def verify_sound_tracks(before, after, assignments):
    """Normalize only explicitly authorized sound TrackIndex changes for strict validation."""
    normalized = copy.deepcopy(after)
    originals = {r['Guid']: r for r in before['existing_notifies']}
    if not set(assignments) <= set(originals):
        raise RuntimeError('Track assignment contains an unknown event GUID')
    for row in normalized['existing_notifies']:
        guid = row['Guid']
        if guid not in assignments:
            continue
        if int(row.get('TrackIndex', 0)) != assignments[guid]:
            raise RuntimeError('Sound event is not on its category track')
        if 'TrackIndex' in originals[guid]:
            row['TrackIndex'] = originals[guid]['TrackIndex']
        else:
            row.pop('TrackIndex', None)
    return normalized


def notify_category(sound):
    """Only user-confirmed meanings; never equate the equipment bank with cloth."""
    match = re.search(r'/SC_IG_MuGu_(\d+)_', sound or '')
    return {'3777612407': 'AnimNotify_GroundDust',
            '1762027986': 'AnimNotify_LandingEvent',
            '2304525234': 'AnimNotify_LandingEvent'}.get(match[1]) if match else None


def emission(branch, montage):
    if branch in GROUND_BRANCHES:
        return 'GROUND_CONTACT', 'Root'
    if branch in ACTION_BRANCHES:
        return 'CHARACTER_SOCKET', 'Root'
    # Initial project anchors, editable per point; not claimed as original-game socket truth.
    rear = branch in {'B018', 'B022'} or any(n in montage for n in ('ChongYin', 'JueChongJi'))
    return 'WEAPON_SOCKET', 'IG_RearTip' if rear else 'IG_FrontTip'


def transfer_vault_stage_audio(source, target, source_id, target_id):
    """Explicit same-stage project fallback for uncaptured normal forward vault.

    Preserve source-frame time, not a normalized whole-animation stretch. This
    is not a claim of identical white/no-white original-game sound scheduling.
    """
    if (source_id, target_id) not in {(155, 141), (156, 142)}:
        raise ValueError('Only explicit forward-vault white-to-normal stage pairs are allowed')
    if len(source['segments']) != 1 or len(target['segments']) != 1:
        raise ValueError('A vault stage must have one known source segment')
    src, dst = source['segments'][0], target['segments'][0]
    if src['effective_rate'] <= 0 or dst['effective_rate'] <= 0:
        raise ValueError('Reverse/zero rate is not a stage-time mapping')
    result = []
    for point in source['audio']:
        if not point['sound'] or point['kind'] == 'AnimNotify_ClothSound':
            continue
        source_time = src['source_start_s'] + (point['time']-src['track_start_s'])*src['effective_rate']
        time = dst['track_start_s']+(source_time-dst['source_start_s'])/dst['effective_rate']
        if not dst['source_start_s'] <= source_time < dst['source_end_s'] or not 0 <= time < target['length_s']:
            raise ValueError('Transferred sound lies outside the actual target stage; do not clamp it')
        row = copy.deepcopy(point)
        row['source_guid'] = row.pop('guid')
        row.update(time=time, placement_policy='project_same_stage_source_frame_transfer',
                   source_montage=source['path'])
        result.append(row)
    return result


def independent_points(mapping):
    """Keep existing clock mapping; collapse overlapping same-frame B022 hypotheses once."""
    result = []
    for montage in mapping['montages']:
        name = montage['path'].split('.')[-1]
        independent = name == 'AM_IG_AerialLanding' or name.startswith('AM_IG_Dodge_') or name == 'AM_IG_ShouDao'
        send = name in {'AM_IG_SendKinsect', 'AM_IG_DrawAndSendKinsect'}
        candidates = [c for c in montage['candidates']
                      if not c['cut_boundary_ambiguous']
                      and ((independent and (c['branch_id'] in ACTION_BRANCHES
                                             or (name == 'AM_IG_ShouDao' and c['layer'] in ('武器层', '通用武器层'))))
                           or (send and c['branch_id'] == 'B022'))]
        selected = []
        for candidate in sorted(candidates, key=lambda c: (-c['observations'], c['montage_time_estimate_s'])):
            if any(c['branch_id'] == candidate['branch_id']
                   and abs(c['montage_time_estimate_s'] - candidate['montage_time_estimate_s']) < 1 / 60
                   for c in selected):
                continue
            selected.append(candidate)
        for c in selected:
            result.append(dict(c, montage=montage['path'],
                               point_id=name + ':' + c['cluster_id'] + ':' + str(c['segment_index'])))
    return result


def verify_preserved(before, after, audio_guids):
    """Existing event IDs/timing/ordering, gameplay configuration and animation stay exact."""
    result = copy.deepcopy(after)
    known = {r['Guid'] for r in before['existing_notifies']}
    result['existing_notifies'] = [r for r in result['existing_notifies'] if r['Guid'] in known]
    # Delegate UE's narrowly documented absolute-link load caches to existing validator.
    if len(known) != len(before['existing_notifies']):
        raise RuntimeError('Duplicate original notify GUID')
    if not set(audio_guids) <= known:
        raise RuntimeError('Unknown migrated audio event')
    return result
