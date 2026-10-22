"""Pure scoped-condition and exact-identity checks; no game or decoder."""
import unittest
from rise_audio_combat import audit_rows, pointer


class CombatAuditTests(unittest.TestCase):
    def setUp(self):
        self.nodes={('hit_pl.bnk',1932771116):{'type':'CAkEvent','id':1932771116,'actions':[2]},
            ('hit_pl.bnk',2):{'type':'CAkActionPlay','targets':[3]},
            ('hit_pl.bnk',3):{'type':'CAkSwitchCntr','switch_container':{'group_type':0,'group_id':3805839008,'default_value':1804647490}}}
        self.posts=[{'row':'1','kind':'post_requested','event_id':'1932771116','game_object_id':'9','qpc':'100'}]
        self.objects=[{'kind':'object_key_return','qpc':'90','game_object_id':'9','engine_gameobject_address':'123'}]
        self.contexts=[];self.hits=[]

    def audit(self):return audit_rows(self.hits,self.posts,self.contexts,self.objects,self.nodes)

    def test_default_is_from_played_root_not_earlier_audition(self):
        value=self.audit()['hit_audio'][0]['switches'][0]
        self.assertEqual(value['declared_bank_default'],1804647490)
        self.assertIsNone(value['requested_value'])
        self.assertFalse(value['runtime_effective_value_verified'])
        self.assertIn('candidate',value['provenance'])

    def test_switch_from_other_object_never_borrowed(self):
        self.contexts=[{'kind':'switch_requested','qpc':'80','game_object_id':'99','group_or_parameter_id':'3805839008','value_id':'3335887372'}]
        self.assertIsNone(self.audit()['hit_audio'][0]['switches'][0]['requested_value'])

    def test_actual_same_object_switch_and_unregister_invalidation(self):
        self.contexts=[{'kind':'switch_requested','qpc':'80','game_object_id':'9','group_or_parameter_id':'3805839008','value_id':'111'}]
        self.assertEqual(self.audit()['hit_audio'][0]['switches'][0]['requested_value'],111)
        self.assertFalse(self.audit()['hit_audio'][0]['switches'][0]['runtime_effective_value_verified'])
        self.contexts.append({'kind':'unregister_requested','qpc':'85','game_object_id':'9'})
        self.assertIsNone(self.audit()['hit_audio'][0]['switches'][0]['requested_value'])

    def test_native_identity_is_exact_not_nearest_time(self):
        edge={'kind':'container_trigger_context','qpc':'70','game_object_id':'','engine_gameobject_address':'123',
            'hit_info_address':'345','thread_context_address':'678','owner_gameobject_address':'999'}
        self.objects.append(edge)
        self.hits=[{'row':'7','kind':'damage_side_callback','hit_info_address':'345','thread_context_address':'userdata: 000002A6'}]
        result=self.audit()['hit_audio'][0]
        self.assertEqual(result['unique_hit_row'],'7')
        self.assertEqual(result['native_relationship'],edge)
        self.hits.append({**self.hits[0],'row':'8'})
        result=self.audit()['hit_audio'][0]
        self.assertIsNone(result['unique_hit_row'])
        self.assertIn('ambiguity',result['relationship_status'])

    def test_reused_emitter_relationships_do_not_select_nearest(self):
        edge={'kind':'container_trigger_context','qpc':'70','game_object_id':'','engine_gameobject_address':'123'}
        self.objects += [edge,{**edge,'qpc':'80'}]
        self.assertIsNone(self.audit()['hit_audio'][0]['native_relationship'])

    def test_conflicting_object_id_fails_closed_and_limits_visible(self):
        self.objects.append({**self.objects[0],'qpc':'95','engine_gameobject_address':'124'})
        self.hits=[{'kind':'hit_sound_context','flow_fields':'schema_error=scalar_field_limit'}]
        result=self.audit()
        self.assertIsNone(result['hit_audio'][0]['engine_gameobject_address'])
        self.assertEqual(result['summary']['incomplete_field_rows'],1)

    def test_no_bank_metadata_is_unknown_not_default_zero(self):
        self.nodes.clear()
        value=self.audit()['hit_audio'][0]['switches'][0]
        self.assertIsNone(value['declared_bank_default'])
        self.assertEqual(value['provenance'],'unknown')

    def test_pointer_namespaces_preserve_full_width(self):
        self.assertEqual(pointer('userdata: FEDCBA9876543210'),0xFEDCBA9876543210)
        self.assertEqual(pointer(str(0xFEDCBA9876543210)),0xFEDCBA9876543210)
        self.assertIsNone(pointer('unknown'))

    def test_untimed_object_gap_marker_invalidates_identity_claims(self):
        self.objects.append({'kind':'unknown','qpc':'','semantics':'queue_gap;no_safe_complete_join'})
        result=self.audit()
        self.assertIsNone(result['hit_audio'][0]['engine_gameobject_address'])
        self.assertEqual(result['summary']['object_gaps'],1)

    def test_unknown_condition_gap_and_unregister_all_do_not_keep_stale_switch(self):
        self.contexts=[{'kind':'switch_requested','qpc':'80','game_object_id':'9','group_or_parameter_id':'3805839008','value_id':'111'},
            {'kind':'unknown','qpc':'','phase':'queue_drop_history_unknown'}]
        # Context bridge names this queue_drop_history_unknown, which must also
        # be treated as loss even though there is no kind/value to replay.
        self.assertIsNone(self.audit()['hit_audio'][0]['switches'][0]['requested_value'])
        self.contexts[1]={'kind':'unregister_requested','qpc':'85','game_object_id':'18446744073709551615'}
        self.assertIsNone(self.audit()['hit_audio'][0]['switches'][0]['requested_value'])


if __name__=='__main__':unittest.main()
