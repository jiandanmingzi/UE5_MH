"""Branch/identity/timing safety checks; no UE/game/decoder/full-bank launch."""
import unittest
import rise_audio_weapon_mapping as mapping


class CurrentWeaponMappingTests(unittest.TestCase):
    def review(self):
        return {'confirmed_exclusions':[{'event_id':1942118895}],
                'confirmed_branch_exclusions':[{'event_id':938078130,'capture_role':'Control',
                    'switch_conditions':{'1356166406':2033159194}}],
                'pending_event_ids':[1364862806,1816305690]}

    def variant(self, conditions=(), media=(1,), lines=('wem/1.wem',)):
        return {'conditions':list(conditions),'media_ids':list(media),'render_lines':list(lines)}

    def context(self,qpc,group='1',value='2',go='10',kind='switch_requested',phase='normal'):
        return {'qpc':str(qpc),'kind':kind,'game_object_id':go,'group_or_parameter_id':group,
                'value_id':value,'phase':phase}

    def obj(self,qpc,kind,go='10',address='100',owner='100'):
        return {'qpc':str(qpc),'kind':kind,'game_object_id':go,'engine_gameobject_address':address,
                'owner_gameobject_address':owner,'semantics':'normal'}

    def post(self,qpc,go='10'):
        return {'qpc':str(qpc),'kind':'post_requested','game_object_id':go,'event_id':'938078130'}

    def test_confirmed_blade_event_excluded(self):
        self.assertEqual(mapping.excluded(1942118895,{},'Primary',self.review()),'confirmed_event_exclusion')

    def test_primary938_not_excluded_by_control_review(self):
        self.assertIsNone(mapping.excluded(938078130,{'1356166406':'2033159193'},'Primary',self.review()))

    def test_control938_exclusion_requires_actual_condition(self):
        self.assertEqual(mapping.excluded(938078130,{'1356166406':'2033159194'},'Control',self.review()),'confirmed_branch_exclusion')
        self.assertIsNone(mapping.excluded(938078130,{},'Control',self.review()))

    def test_pending_semantics_is_not_an_exclusion_gate(self):
        for eid in (1364862806,1816305690):
            self.assertIsNone(mapping.excluded(eid,{},'Primary',self.review()))

    def test_same_event_two_conditions_keeps_source_graphs_separate(self):
        variants=[self.variant(['[1=2]'],media=(10,),lines=('wem/10.wem',)),
                  self.variant(['[1=3]'],media=(20,),lines=('wem/20.wem',))]
        a,_=mapping.choose_variant(variants,{'1':'2'},{})
        b,_=mapping.choose_variant(variants,{'1':'3'},{})
        self.assertNotEqual(mapping.graph_key(a),mapping.graph_key(b))

    def test_other_object_switch_never_borrowed(self):
        r=mapping.scoped_posts([self.context(1,go='11')],[self.obj(2,'object_key_return')],[self.post(3)],100)
        self.assertEqual(r[0]['switches'],{})

    def test_condition_gap_then_exact_setter_restores_only_its_key(self):
        ctx=[self.context(1),self.context(2,group='3',value='4'),
             self.context(4,kind='unknown',phase='queue_drop_history_unknown'),self.context(6)]
        r=mapping.scoped_posts(ctx,[self.obj(1,'object_key_return')],[self.post(3),self.post(5),self.post(7)],100)
        self.assertEqual([x['switches'] for x in r],[{'1':'2','3':'4'},{},{'1':'2'}])

    def test_unregister_invalidates_object_conditions(self):
        ctx=[self.context(1),self.context(4,kind='unregister_requested')]
        r=mapping.scoped_posts(ctx,[self.obj(2,'object_key_return'),self.obj(4,'unregister_requested')],
                               [self.post(3),self.post(5)],100)
        self.assertEqual(r[0]['owner_relation'],'master_emitter_exact')
        self.assertEqual(r[1]['switches'],{})
        self.assertEqual(r[1]['owner_relation'],'owner_unresolved_or_reused')

    def test_reused_emitter_different_owners_not_time_nearest(self):
        objs=[self.obj(1,'object_key_return',address='200'),
              self.obj(1,'container_trigger_context',address='200',owner='100'),
              self.obj(2,'container_trigger_context',address='200',owner='300')]
        r=mapping.scoped_posts([],objs,[self.post(3)],100)
        self.assertEqual(r[0]['owner_relation'],'owner_unresolved_or_reused')

    def test_unique_native_owner_can_be_master(self):
        objs=[self.obj(1,'object_key_return',address='200'),
              self.obj(1,'container_trigger_context',address='200',owner='100')]
        r=mapping.scoped_posts([],objs,[self.post(3)],100)
        self.assertEqual(r[0]['owner_relation'],'unique_native_owner_is_master')

    def test_observed_condition_selects_primary_not_first_variant(self):
        variants=[self.variant(['[1=3]'],media=(3,),lines=('wem/3.wem',)),self.variant(['[1=2]'])]
        v,_=mapping.choose_variant(variants,{'1':'2'},{})
        self.assertEqual(v['media_ids'],[1])

    def test_missing_condition_not_replaced_with_default(self):
        v,_=mapping.choose_variant([self.variant(['[1=2]'])],{},{})
        self.assertIsNone(v)

    def test_statechunks_match_observed_global_state(self):
        self.assertTrue(mapping.condition_match(self.variant(['{s}=(1=2)']),{}, {'1':'2'}))
        self.assertFalse(mapping.condition_match(self.variant(['{s}=(1=2)']),{'1':'2'}, {}))

    def test_distinct_possible_graphs_stay_ambiguous(self):
        v,reason=mapping.choose_variant([self.variant(),self.variant(media=(2,),lines=('wem/2.wem',))],{},{})
        self.assertIsNone(v)
        self.assertEqual(reason,'multiple_distinct_graphs')

    def test_no_media_graph_not_authored_as_audible_sound(self):
        v,_=mapping.choose_variant([self.variant(media=(),lines=('?.plugin-silence 1.0',))],{},{})
        self.assertIsNone(v)

    def test_loop_clusters_preserve_two_events_in_one_episode(self):
        def e(frame,episode):
            return {'motion':'plc_Field_000_011_Loop','event_id':1,'branch_id':'B1','layer':'装备动作层',
                    'native_frame_estimate':frame,'native_end_frame':61,'episode':episode,'audio_row':str(frame)}
        r=mapping.cluster_events([e(10,1),e(11,1),e(10.5,2)])
        self.assertEqual(len(r),2)
        self.assertEqual(sum(x['observations'] for x in r),3)
        self.assertTrue(all(x['source_id'] is None for x in r))

    def test_generated_conditions_use_observed_tuples_not_cross_product(self):
        events=[{'switches':{'1':'2','3':'4'},'states':{}},
                {'switches':{'1':'5','3':'6'},'states':{}}]
        result=mapping.observed_signatures(events,{'1','3'})
        self.assertEqual(len(result),2)
        self.assertNotIn((('sw','1','2'),('sw','3','6')),result)


if __name__=='__main__':
    unittest.main()
