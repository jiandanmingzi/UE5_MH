"""Pure offline combat audit used by rise_audio_capture_map --audit-combat.

No time-nearest hit association, cross-object Switch borrowing or assumed zero.
Defaults come from the actual bank, labelled separately from runtime requests.
"""
from collections import Counter, defaultdict
from pathlib import Path
import csv
import re


def pointer(value):
    value = str(value or '').strip()
    if not value or value == 'unknown':
        return None
    if value.startswith('userdata:'):
        return int(value.split(':',1)[1].strip(),16)
    return int(value,16 if value.startswith('0x') else 10)


def audit_rows(hits, posts, contexts, objects, nodes):
    target_event = '1932771116'
    # Read the declared default from the ROOT actually played by this event.
    roots = []
    for (bank, _), node in nodes.items():
        if node.get('type') != 'CAkEvent' or str(node.get('id')) != target_event:
            continue
        for action_id in node['actions']:
            action = nodes.get((bank,action_id),{})
            if action.get('type') != 'CAkActionPlay':
                continue
            for root_id in action.get('targets',[]):
                root = nodes.get((bank,root_id),{})
                if root.get('switch_container',{}).get('group_type') == 0:
                    roots.append({'bank':bank,'root_id':root_id,**root['switch_container']})
    default = roots[0] if len(roots) == 1 else None
    history = {}
    keys = defaultdict(list)
    triggers = defaultdict(list)
    hit_index = defaultdict(list)
    result, warnings = [], Counter()
    object_gap = any('gap' in r.get('semantics','') for r in objects)
    condition_gap = any(any(s in r.get('phase','') for s in ('gap','loss','drop','barrier')) for r in contexts)
    if object_gap: warnings['object_gaps'] = 1
    if condition_gap: warnings['condition_gaps'] = 1
    for h in hits:
        if h['kind'] == 'damage_side_callback':
            hit_index[(pointer(h['hit_info_address']),pointer(h['thread_context_address']))].append(h)
        coverage = h.get('field_coverage','') + ';' + ';'.join(h.get(k,'') for k in ('flow_fields','sound_context_fields','attack_shape_fields','damage_shape_fields'))
        if any(s in coverage for s in ('limit','schema_error','required_missing','no_selected_fields')):
            warnings['incomplete_field_rows'] += 1
    stream = [(int(r['qpc']),0,r) for r in contexts if r.get('qpc') and r.get('kind') != 'unknown']
    stream += [(int(r['qpc']),1,r) for r in objects if r.get('qpc') and r.get('kind') != 'unknown']
    stream += [(int(r['qpc']),2,r) for r in posts]
    for qpc, source, r in sorted(stream,key=lambda v:(v[0],v[1])):
        kind, go = r['kind'],r.get('game_object_id','')
        if source == 0:
            if kind in ('register_requested','unregister_requested'):
                history = {k:v for k,v in history.items() if k[0] == 'global' or
                    (go != '18446744073709551615' and k[0] != go)}
            elif kind == 'switch_requested':
                history[(go,r['group_or_parameter_id'])] = r['value_id']
            elif kind == 'state_requested':
                history[('global',r['group_or_parameter_id'])] = r['value_id']
            if any(s in r.get('phase','') for s in ('gap','loss','drop','barrier')):
                history.clear()
        elif source == 1:
            if kind == 'object_key_return':
                keys[go].append(r)
            elif kind == 'container_trigger_context':
                triggers[pointer(r['engine_gameobject_address'])].append(r)
            elif kind == 'unregister_requested':
                keys.pop(go,None)
            if 'gap' in r.get('semantics',''):
                keys.clear();triggers.clear()
        else:
            candidates = keys.get(go,[])
            pointers = {pointer(k['engine_gameobject_address']) for k in candidates}
            emitter = next(iter(pointers)) if len(pointers) == 1 and not object_gap else None
            if emitter is not None: warnings['exact_id_object_matches'] += 1
            else: warnings['unresolved_object_posts'] += 1
            if r['event_id'] != target_event:
                continue
            relation = triggers.get(emitter,[])
            # Exact pointer edge; if pool reuse yields multiple edges, leave it
            # ambiguous instead of selecting the nearest timestamp.
            edge = relation[0] if len(relation) == 1 else None
            matched = []
            if edge and edge.get('hit_info_address') and edge.get('thread_context_address'):
                matched = hit_index.get((pointer(edge['hit_info_address']),pointer(edge['thread_context_address'])),[])
            condition = []
            for group in ('3805839008','2207689817','3914809010'):
                actual = history.get((go,group)) if not condition_gap else None
                fallback = default['default_value'] if default and str(default['group_id']) == group else None
                condition.append({'group_id':int(group),'requested_value':int(actual) if actual else None,
                    'declared_bank_default':fallback,'provenance':'observed_same_object_request' if actual else
                    'bank_default_candidate_no_observed_override' if fallback is not None else 'unknown',
                    'runtime_request_observed':actual is not None,'runtime_effective_value_verified':False})
            result.append({'post_row':r['row'],'qpc':qpc,'game_object_id':go,'engine_gameobject_address':emitter,
                'native_relationship':edge,'hit_identity_matches':len(matched),
                'unique_hit_row':matched[0]['row'] if len(matched) == 1 else None,
                'relationship_status':'exact_native_callchain_and_unique_hit_identity' if len(matched)==1 else
                    'native_edge_with_pooled_hit_identity_ambiguity' if matched else
                    'native_owner_edge_only' if edge else 'unknown_or_reused_emitter',
                'switches':condition,'global_state_requested':history.get(('global','200845913')) if not condition_gap else None,
                'final_media':'unknown'})
    return {'schema':'mhgz_combat_condition_audit_v2','bank_root_defaults':roots,
        'summary':{'damage_side_callbacks':sum(h['kind']=='damage_side_callback' for h in hits),
            'sound_post_snapshots':sum(h['kind']=='hit_sound_context_post' for h in hits),
            'posts':len(posts),'hit_audio_posts':len(result),**dict(warnings)},'hit_audio':result,
        'limits':['Bank default is not an observed setter or a final-Media selection.',
            'Raw HitInfo pool addresses are not unique transaction IDs; ambiguous reuse stays unknown.',
            'No Switch value is borrowed from a different GameObject.']}


def audit_combat(directory: Path, capture: str, nodes):
    if not re.fullmatch(r'mhrise_\d{8}_\d{6}_\d+',capture):
        raise ValueError('Invalid recorder basename')
    inputs = {}
    for suffix in ('hits','audio_native','audio_context','audio_objects'):
        path = directory / f'{capture}_{suffix}.csv'
        if path.stat().st_size > 64*1024*1024:
            raise ValueError('Combat sidecar exceeds64MiB bound')
        with path.open(encoding='utf-8-sig',newline='') as stream:
            values = list(csv.DictReader(stream))
        if any(None in r or any(v is None for v in r.values()) for r in values):
            raise ValueError(f'Ragged {suffix} sidecar')
        inputs[suffix] = values
    return {'capture':capture,**audit_rows(inputs['hits'],inputs['audio_native'],inputs['audio_context'],inputs['audio_objects'],nodes)}
