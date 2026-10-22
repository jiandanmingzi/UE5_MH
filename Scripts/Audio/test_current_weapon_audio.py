"""Focused source selection/reconciliation/author guard checks, no game or UE."""
import ast
import copy
from pathlib import Path
import unittest
from unittest.mock import patch

import prepare_current_weapon_audio as prepare
import rise_audio_capture_map as captures
from test_swing_coverage import scope as coverage_scope
from types import SimpleNamespace

guard_scope = dict(copy=copy, coverage=SimpleNamespace(verify_montage=coverage_scope['verify_montage']))
file = Path(__file__).with_name('setup_current_weapon_audio.py')
for node in ast.parse(file.read_text(encoding='utf-8')).body:
    if isinstance(node, ast.FunctionDef) and node.name in ('non_tool_snapshot', 'verify_author_data', 'verify_recipes', 'verify_previous', 'non_runtime_audio_config'):
        exec(compile(ast.Module(body=[node], type_ignores=[]), str(file), 'exec'), guard_scope)


class CurrentAudioTests(unittest.TestCase):
    def test_external_random_leaves_preserve_order_and_controls(self):
        body = '# header\n wem/33.wem #i #v -3dB ##memory\n wem/22.wem #i ##memory\ngroup = -R2>1\n'
        with patch.object(Path, 'read_text', return_value=body):
            result = captures.random_comparison_bodies(dict(file='graph.txtp', render_lines=body.splitlines()[1:]))
            self.assertEqual([r['selected_media_id'] for r in result], [33, 22])
            self.assertEqual(result[1]['body'], body.replace('-R2>1', '-R2>2'))

    def test_embedded_bank_random_still_supported(self):
        body = ' b.bnk#1 ##33.wem\n b.bnk#2 ##22.wem\ngroup = -R2>1\n'
        with patch.object(Path, 'read_text', return_value=body):
            self.assertEqual(len(captures.random_comparison_bodies(dict(file='g.txtp', render_lines=body.splitlines()))), 2)

    def fixture(self):
        point = dict(event_id=10, sequence='seq', slot=0, segment_index=0, loop=0,
                     montage_time_estimate_s=.3, event_tag='existing.tag')
        old = dict(montages=[dict(path='m', recipes=[point], before=dict(length_s=2),
                                 routes=[dict(intervals=[[0, 2]])])])
        new = dict(montages=[dict(path='m', candidates=[dict(point, montage_time_estimate_s=.49,
                    cut_boundary_ambiguous=False, layer='武器层', branch_id='B1')])])
        return old, new

    def test_unique_same_source_relocation_not_old_clock(self):
        old, new = self.fixture()
        result = prepare.reconcile(old, new)[0]
        self.assertEqual(result['recipes'][0]['montage_time_estimate_s'], .49)
        self.assertEqual(result['recipes'][0]['event_tag'], 'existing.tag')

    def test_unequal_occurrences_not_nearest_guess(self):
        old, new = self.fixture()
        new['montages'][0]['candidates'].append(dict(new['montages'][0]['candidates'][0], montage_time_estimate_s=.301))
        result = prepare.reconcile(old, new)[0]
        self.assertFalse(result['recipes']); self.assertEqual(len(result['retired']), 1)

    def test_source_mismatch_does_not_reuse_event(self):
        old, new = self.fixture(); new['montages'][0]['candidates'][0]['sequence'] = 'different'
        self.assertFalse(prepare.reconcile(old, new)[0]['recipes'])

    def test_cut_ambiguous_excluded(self):
        old, new = self.fixture(); new['montages'][0]['candidates'][0]['cut_boundary_ambiguous'] = True
        self.assertFalse(prepare.reconcile(old, new)[0]['recipes'])

    def test_multiple_branches_not_arbitrarily_selected(self):
        old, new = self.fixture()
        old['montages'][0]['recipes'].append(dict(old['montages'][0]['recipes'][0], event_tag='tag2'))
        new['montages'][0]['candidates'].append(dict(new['montages'][0]['candidates'][0], branch_id='B2'))
        self.assertFalse(prepare.reconcile(old, new)[0]['recipes'])

    def test_same_event_occurrences_keep_chronological_tags(self):
        old, new = self.fixture()
        old['montages'][0]['recipes'].append(dict(old['montages'][0]['recipes'][0], montage_time_estimate_s=.1, event_tag='first'))
        new['montages'][0]['candidates'].append(dict(new['montages'][0]['candidates'][0], montage_time_estimate_s=.2))
        self.assertEqual([r['event_tag'] for r in prepare.reconcile(old,new)[0]['recipes']], ['first','existing.tag'])

    def test_runtime_layers_not_baked(self):
        old, new = self.fixture(); new['montages'][0]['candidates'][0]['layer'] = '真实命中运行时层'
        self.assertFalse(prepare.reconcile(old,new)[0]['recipes'])

    def test_actual_route_reachability_required(self):
        old, new = self.fixture(); old['montages'][0]['routes'] = [dict(intervals=[[0, .4]])]
        with self.assertRaises(ValueError): prepare.reconcile(old,new)

    def test_non_audio_notify_guid_order_time_and_payload_protected(self):
        baseline = dict(length_s=2, rate_scale=1, segments=[], sections=[],
            existing_notifies=[dict(Guid='user', LinkValue='.4', Notify='collision'), dict(Guid='old')])
        current = copy.deepcopy(baseline); current['existing_notifies'][1] = dict(Guid='new')
        guard_scope['verify_author_data'](baseline,current,{'old'},{'new'})
        current['existing_notifies'][0]['LinkValue'] = '.5'
        with self.assertRaises(RuntimeError): guard_scope['verify_author_data'](baseline,current,{'old'},{'new'})

    def expansion_fixture(self):
        point = dict(event_id=20, sequence='seq', slot=0, segment_index=0, loop=0,
            montage_time_estimate_s=.6, cluster_id='source:20:B1:0', branch_id='B1',
            layer='武器层', observations=3, cut_boundary_ambiguous=False, calibration=dict(ue_fps=60))
        cluster = dict(native_frame_estimate=36, native_frame_range=[35.9,36.1])
        montage = dict(path='/Game/AM_IG_Test.AM_IG_Test', sections=[dict(time_s=0)],
                       segments=[dict(slot=0,index=0,effective_rate=2)])
        return point, {point['cluster_id']:cluster}, montage, [dict(intervals=[[0,1]])]

    def test_expansion_reuses_saved_identity_and_does_not_reauthor(self):
        p,c,m,r = self.expansion_fixture()
        self.assertEqual(prepare.expansion_candidates([p],r,[p],c,m),([],[]))

    def test_expansion_new_weapon_point_has_stable_distinct_tag(self):
        p,c,m,r = self.expansion_fixture()
        added, skipped = prepare.expansion_candidates([p],r,[],c,m)
        self.assertFalse(skipped); self.assertIn('.Current.S0.L0.E20.B1.C0',added[0]['event_tag'])

    def test_expansion_requires_repeated_observation_not_semantic_name(self):
        p,c,m,r = self.expansion_fixture(); p['observations']=1
        added,skipped=prepare.expansion_candidates([p],r,[],c,m)
        self.assertFalse(added); self.assertIn('single_observation',skipped[0]['reason'])

    def test_expansion_nearby_occurrences_are_not_double_authored(self):
        p,c,m,r = self.expansion_fixture(); other=dict(p,cluster_id='other',montage_time_estimate_s=.607)
        added,skipped=prepare.expansion_candidates([p,other],r,[],c,m)
        self.assertFalse(added);self.assertEqual(len(skipped),2)

    def test_expansion_does_not_bake_runtime_layer(self):
        p,c,m,r = self.expansion_fixture();p['layer']='猎虫运行时层'
        self.assertEqual(prepare.expansion_candidates([p],r,[],c,m),([],[]))

    def test_expansion_rejects_section_boundary_clock_range(self):
        p,c,m,r = self.expansion_fixture();m['sections'].append(dict(time_s=.6))
        added,skipped=prepare.expansion_candidates([p],r,[],c,m)
        self.assertFalse(added);self.assertEqual(skipped[0]['reason'],'section_boundary_clock_range')

    def test_expansion_rejects_unreachable_actual_entry(self):
        p,c,m,r = self.expansion_fixture();r[0]['intervals']=[[.7,1]]
        added,skipped=prepare.expansion_candidates([p],r,[],c,m)
        self.assertFalse(added);self.assertEqual(skipped[0]['reason'],'outside_actual_ga_routes')

    def test_expansion_preserves_saved_sound_guid_and_recipe(self):
        previous=[dict(guid='old',sound='cue',time_s=.1)]
        guard_scope['verify_previous'](previous,previous+[dict(guid='new')])
        with self.assertRaises(RuntimeError):guard_scope['verify_previous'](previous,[dict(guid='old',sound='other',time_s=.1)])

    def test_awakened_runtime_send_route_not_lost_at_static_hold_cycle(self):
        path='/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_JueChongJi.AM_IG_JueChongJi'
        names=['Startup193','Hold193Loop','Send195','PreFlight196','HunterFlight199']
        nexts=['Hold193Loop','Hold193Loop','PreFlight196','HunterFlight199','']
        montage=dict(path=path,length_s=5,sections=[dict(name=n,next=x,time_s=i) for i,(n,x) in enumerate(zip(names,nexts))])
        ability=dict(path='ga',primary_montage=path,montages=[path],entry_config=dict(default_entry_section='Startup193'),**{'class':'cls'})
        routes=prepare.playback_routes(dict(montages=[montage],abilities=[ability],combo_transitions=[]))[path]
        self.assertTrue(any(prepare.positions.reachable(2.5,r) for r in routes))
        self.assertTrue(any(r['status']=='cycle_prefix_only' for r in routes))

    def test_action_layer_uses_same_routes_and_boundary_guards(self):
        p,c,m,r = self.expansion_fixture();p['layer']='地面/动作附加层'
        added,skipped=prepare.expansion_candidates([p],r,[],c,m,('地面/动作附加层',),'Audio.Action.IG')
        self.assertFalse(skipped);self.assertTrue(added[0]['event_tag'].startswith('Audio.Action.IG.'))
        p['cut_boundary_ambiguous']=True
        self.assertFalse(prepare.expansion_candidates([p],r,[],c,m,('地面/动作附加层',))[0])

    def test_action_layer_does_not_admit_runtime_or_equipment(self):
        p,c,m,r = self.expansion_fixture();p['layer']='装备动作层'
        self.assertEqual(prepare.expansion_candidates([p],r,[],c,m,('地面/动作附加层',)),([],[]))

    def test_combat_config_guard_allows_only_three_audio_fields(self):
        guard=guard_scope['non_runtime_audio_config']
        before='  KinsectLaunchSound=None\n  PowderMotionValue=50\n  KinsectMarkDuration=90\n'
        after=before.replace('KinsectLaunchSound=None','KinsectLaunchSound=somecue')+'  KinsectFlightLoopSound=loop\n'
        self.assertEqual(guard(before),guard(after))
        self.assertNotEqual(guard(before),guard(after.replace('PowderMotionValue=50','PowderMotionValue=51')))


if __name__ == '__main__':
    unittest.main()
