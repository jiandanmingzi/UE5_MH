import copy
import unittest
from rise_audio_weapon_mapping import hunter_fx_posts, end_frame_hold_positions, transition_precursor_posts


class HunterFXOwnershipTests(unittest.TestCase):
    def rows(self):
        objects = [
            dict(kind='container_trigger_context', qpc='10', row='1', engine_gameobject_address='100',
                 owner_gameobject_address='100', container_address='200', native_present_frame='1'),
            dict(kind='container_trigger_context', qpc='20', row='2', engine_gameobject_address='300',
                 owner_gameobject_address='300', container_address='200', native_present_frame='5'),
            dict(kind='object_key_return', qpc='21', row='3', game_object_id='7', engine_gameobject_address='300', native_present_frame='5')]
        posts = [dict(kind='post_requested', qpc='22', row='4', event_id='3520842405',
                      game_object_id='7', native_present_frame='5')]
        return objects, posts

    def resolve(self, objects, posts, contexts=()):
        return hunter_fx_posts(contexts, objects, posts, 100, {3520842405})

    def test_exact_chain_is_accepted_with_provenance(self):
        linked, isolated = self.resolve(*self.rows())
        self.assertEqual(len(linked), 1)
        self.assertEqual(linked[0]['fx_evidence']['key_row'], '3')
        self.assertEqual(isolated, [])

    def test_other_frame_or_container_is_not_nearest_time_match(self):
        for field, value in [('native_present_frame', '4'), ('container_address', '999')]:
            objects, posts = self.rows(); objects[1][field] = value
            self.assertEqual(self.resolve(objects, posts)[0], [])

    def test_gap_and_object_lifecycle_invalidate_old_binding(self):
        for record in [dict(kind='gap', qpc='21', phase='gap'),
                       dict(kind='register_requested', qpc='21', game_object_id='7')]:
            self.assertEqual(self.resolve(*self.rows(), contexts=[record])[0], [])

    def test_trigger_can_create_registration_before_the_fresh_key(self):
        objects, posts = self.rows(); objects[-1]['qpc'] = '22'; posts[0]['qpc'] = '23'
        linked, _ = self.resolve(objects, posts, contexts=[
            dict(kind='register_requested', qpc='21', game_object_id='7')])
        self.assertEqual(len(linked), 1)

    def test_native_constructor_key_then_registration_in_same_frame(self):
        objects, posts = self.rows(); posts[0]['qpc'] = '24'
        contexts = [dict(kind='register_requested', qpc='23', game_object_id='7', native_present_frame='5')]
        self.assertEqual(len(self.resolve(objects, posts, contexts)[0]), 1)
        objects[-1]['native_present_frame'] = '4'
        self.assertEqual(self.resolve(objects, posts, contexts)[0], [])

    def test_foreign_owner_disqualifies_shared_container(self):
        objects, posts = self.rows()
        foreign = copy.deepcopy(objects[0]); foreign['owner_gameobject_address'] = '999'
        objects.append(foreign)
        self.assertEqual(self.resolve(objects, posts)[0], [])

    def test_later_different_trigger_is_not_bypassed(self):
        objects, posts = self.rows()
        later = copy.deepcopy(objects[1]); later.update(qpc='21', container_address='999')
        objects.append(later)
        self.assertEqual(self.resolve(objects, posts)[0], [])

    def test_other_events_are_never_promoted(self):
        objects, posts = self.rows(); posts[0]['event_id'] = '1932771116'
        self.assertEqual(self.resolve(objects, posts), ([], []))

    def boundary_fixture(self):
        seg = dict(source_start_s=0, source_end_s=1.3, effective_rate=2, track_start_s=0,
                   loops=1, index=0, slot=0, sequence='seq')
        seq = dict(import_numeric_ids=[155], frames=78, fps=60, import_file_fps=60, length_s=1.3)
        cluster = dict(id='source155', event_id=3520842405, source_id=155, observations=4,
                       native_end_frames=[40], native_frame_range=[39.01,39.9], branch_id='P001', layer='propulsion')
        return seg, seq, cluster

    def test_end_hold_maps_before_actual_part_exit(self):
        result = end_frame_hold_positions(*self.boundary_fixture())
        self.assertEqual(len(result), 1)
        self.assertAlmostEqual(result[0]['montage_time_estimate_s'], .65-1/60)
        self.assertIn('project_', result[0]['placement_policy'])

    def test_cropped_wrong_identity_and_mixed_frame_ranges_are_not_clamped(self):
        seg, seq, cluster = self.boundary_fixture()
        for variant in ('crop','identity','range','single'):
            a,b,c = copy.deepcopy(seg),copy.deepcopy(seq),copy.deepcopy(cluster)
            if variant=='crop':a['source_end_s']=1.2
            if variant=='identity':b['import_numeric_ids']=[144]
            if variant=='range':c['native_frame_range']=[38.9,39.9]
            if variant=='single':c['observations']=1
            self.assertEqual(end_frame_hold_positions(a,b,c), [])


class TransitionPrecursorTests(unittest.TestCase):
    def fixture(self):
        rows = [dict(sample_index=str(f), render_callback=str(f), capture_time_s=str(f/120),
                     motion_name='plw_InsectGlaive_100_'+('137' if f==100 else '156'),
                     node_frame='.5' if f==100 else '40', node_weight='1',
                     component='player00', layer='0', read_status='readable') for f in (98,99,100)]
        return rows, [dict(row='7', native_present_frame='98')]

    def test_request_before_entry_is_not_assigned_to_previous_animation(self):
        result = transition_precursor_posts(*self.fixture(), 137)
        self.assertEqual(len(result), 1)
        self.assertEqual(result[0]['lead_render_frames'], 2)
        self.assertEqual(result[0]['target_source_id'], 137)

    def test_gap_missing_entry_and_older_request_rejected(self):
        for variant in ('gap','late','older'):
            a,p = self.fixture()
            if variant=='gap':a.pop(1)
            if variant=='late':a[-1]['node_frame']='30'
            if variant=='older':p[0]['native_present_frame']='97'
            self.assertEqual(transition_precursor_posts(a,p,137), [])

    def test_competing_posts_are_not_nearest_time_selected(self):
        a,p = self.fixture();p.append(dict(row='8',native_present_frame='99'))
        self.assertEqual(transition_precursor_posts(a,p,137), [])

    def test_weapon_node_or_wrong_source_is_not_player_entry(self):
        a,p = self.fixture();a[-1]['component']='I_Gla059'
        self.assertEqual(transition_precursor_posts(a,p,137), [])
        self.assertEqual(transition_precursor_posts(*self.fixture(),156), [])


if __name__ == '__main__':
    unittest.main()
