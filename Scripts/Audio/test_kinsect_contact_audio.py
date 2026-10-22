import copy
import unittest
from prepare_kinsect_contact_audio import evidence


class KinsectContactEvidenceTests(unittest.TestCase):
    def fixture(self):
        hits = [dict(kind='damage_side_callback', attacker_name='IG_Ins012', attacker_address='100',
                     row=str(i), capture_time_last_sample_s=str(i)) for i in (1, 2)]
        objects = [dict(kind='object_key_return', qpc='1', game_object_id='7', engine_gameobject_address='100')]
        posts = [dict(kind='post_requested', qpc=str(i+1), game_object_id='7', event_id='603272525',
                      row=str(i), capture_time_estimate_s=str(i+.01)) for i in (1, 2)]
        return hits, [], objects, posts

    def test_exact_emitter_and_repeated_contact_is_labelled_inference(self):
        result = evidence(*self.fixture())
        self.assertEqual(result['exact_emitter_posts'], 2)
        self.assertTrue(all(r['temporal_only'] for r in result['observations']))
        self.assertFalse(result['universal_original_hit_coverage_proven'])
        self.assertFalse(result['unknown_539819764_or_powder_assigned'])

    def test_hunter_or_other_bug_cannot_be_borrowed(self):
        for name in ('player00', 'companion', 'IG_Ins012_extra'):
            h,c,o,p = self.fixture()
            for row in h: row['attacker_name'] = name
            with self.assertRaises(ValueError): evidence(h,c,o,p)

    def test_multiple_insect_actors_are_not_coalesced(self):
        h,c,o,p = self.fixture(); h[-1]['attacker_address'] = '101'
        with self.assertRaises(ValueError): evidence(h,c,o,p)

    def test_pointer_reuse_or_wrong_emitter_is_not_temporal_ownership(self):
        h,c,o,p = self.fixture()
        other = copy.deepcopy(o[0]); other.update(qpc='2', engine_gameobject_address='200')
        with self.assertRaises(ValueError): evidence(h,c,o+[other],p)

    def test_single_sparse_or_ambiguous_contact_rejected(self):
        for variant in ('single','missing','ambiguous'):
            h,c,o,p = self.fixture()
            if variant=='single': p=p[:1]
            if variant=='missing': h[1]['capture_time_last_sample_s']='4'
            if variant=='ambiguous': h.append(copy.deepcopy(h[0]))
            with self.assertRaises(ValueError): evidence(h,c,o,p)

    def test_unknown_event_not_promoted(self):
        h,c,o,p = self.fixture()
        for row in p: row['event_id']='539819764'
        with self.assertRaises(ValueError): evidence(h,c,o,p)


if __name__ == '__main__':
    unittest.main()
