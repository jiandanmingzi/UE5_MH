"""Coverage-only scope/ownership/chain checks; no game, decoder or UE."""
import unittest
import rise_audio_coverage as coverage


class CoverageTests(unittest.TestCase):
    def setUp(self):
        self.snapshot = {'montages': [{'path':'montage','segments':[{'sequence':'seq','index':0,
            'track_start_s':0,'source_start_s':0,'source_end_s':2,'effective_rate':2,'loops':1}]}],
            'sequences':{'seq':{'import_numeric_ids':[118]}},'combo_transitions':[]}
        self.frozen = {'abilities':[{'path':'GA_IG_TuCi','primary_montage':'montage',
            'montages':['montage'],'combo_entries':[]}]}
        self.authored = {'montages':[{'path':'montage','authored':[{'time':0.5}]}]}

    def capture(self, source=118, hit=True, strong=True):
        observation = {'capture':'capture','source_id':source,'frame':2}
        event = {'event_id':coverage.positions.SWINGS[0],'kind':'挥击','source_id':source}
        hits = [{'source_id':source,'exact_audio_joins':[{'post_row':1,'switches':[]}] if strong else []}] if hit else []
        return ({'capture':'capture'},[observation],[event],hits)

    def report(self, captures):
        return coverage.build(self.snapshot,self.frozen,self.authored,captures)

    def test_observation_is_not_damage_evidence(self):
        action=self.report([self.capture(hit=False)])['actions'][0]
        self.assertEqual(action['last_sample_master_callbacks'],0)
        self.assertIn('仅补新版命中',action['task'])
        self.assertTrue(action['swing_event_counts'])

    def test_damage_without_sound_identity_is_not_strong(self):
        action=self.report([self.capture(strong=False)])['actions'][0]
        self.assertEqual(action['last_sample_master_callbacks'],1)
        self.assertEqual(action['unique_hit_sound_contexts'],0)

    def test_complete_strong_context_not_asked_to_repeat(self):
        report=self.report([self.capture()])
        self.assertEqual(report['actions'][0]['task'],'已有动作/候选，继续分析，不重录')
        self.assertEqual(report['summary']['exact_hit_audio_contexts'],1)

    def test_missing_animation_retained_without_empty_min_error(self):
        report=self.report([self.capture(135,hit=False)])
        self.assertEqual(report['actions'][0]['missing_source_ids'],[118])
        self.assertEqual(next(s for s in report['sources'] if s['source_id']==118)['native_frame_range'],[])
        self.assertIn('整招缺动作',report['actions'][0]['task'])

    def test_auxiliary_sound_does_not_turn_into_swing(self):
        h,o,e,d=self.capture(hit=False)
        e[:]=[{'event_id':3520842405,'kind':'空中推进','source_id':118}]
        action=self.report([(h,o,e,d)])['actions'][0]
        self.assertFalse(action['swing_event_counts'])
        self.assertEqual(action['other_event_counts'],{3520842405:1})

    def test_world_attack_not_a_rise_recording_request(self):
        self.frozen['abilities'][0]['path']='GA_IG_JiXiTuCi'
        self.snapshot['sequences']['seq']['import_numeric_ids']=[]
        action=self.report([])['actions'][0]
        self.assertIn('世界动作',action['task'])

    def test_shared_clips_flagged_not_blindly_assigned_to_GAs(self):
        other={**self.frozen['abilities'][0],'path':'GA_IG_FeiShenYueRu'}
        self.frozen['abilities'].append(other)
        report=self.report([self.capture()])
        self.assertTrue(all(a['shared_source_ids']==[118] for a in report['actions']))
        self.assertTrue(all(a['unique_hit_sound_contexts']==0 for a in report['actions']))
        self.assertTrue(any('shared' in s for s in report['limits']))

    def test_partial_missing_prefix_does_not_request_whole_air_swing_again(self):
        self.snapshot['sequences']['seq']['import_numeric_ids']=[118,306]
        report=self.report([self.capture(hit=False)])
        self.assertIn('主体已有',report['actions'][0]['task'])
        text=coverage.markdown(report)
        self.assertIn('主体/已录挥击保留',text)
        self.assertNotIn('| 突刺 | 空挥/完整执行',text)

    def test_source_parser_does_not_accept_nonplayer_or_fake_prefix(self):
        self.assertEqual(coverage.source_id('plw_InsectGlaive_100_118'),118)
        self.assertIsNone(coverage.source_id('weapon_118'))
        self.assertIsNone(coverage.source_id('plw_InsectGlaive_100_118_Loop'))

    def test_static_route_only_uses_intersecting_segments(self):
        route={'intervals':[[0.4,0.9]]}
        self.assertEqual(coverage.route_chain(self.snapshot['montages'][0],route,self.snapshot['sequences']),[118])
        self.assertEqual(coverage.route_chain(self.snapshot['montages'][0],{'intervals':[[2,3]]},self.snapshot['sequences']),[])

    def test_chain_requires_previous_source_and_no_gap_in_ids(self):
        episodes=[{'source_id':117},{'source_id':118,'episode':2,'start_s':1}]
        self.assertEqual(len(coverage.chain_candidates({'c':episodes},[118],{117})),1)
        self.assertFalse(coverage.chain_candidates({'c':episodes},[118],{120}))
        self.assertFalse(coverage.chain_candidates({'c':episodes},[None],{117}))

    def test_markdown_table_column_counts_consistent(self):
        text=coverage.markdown(self.report([self.capture()]))
        expected=None
        for line in text.splitlines():
            if not line.startswith('|'):
                expected=None
            elif expected is None: expected=line.count('|')
            else: self.assertEqual(line.count('|'),expected,line)


if __name__=='__main__': unittest.main()
