import copy
import unittest
from spatial_audio_rules import emission, independent_points, verify_preserved, notify_category, SOUND_TRACKS, verify_sound_tracks, same_audio_recipes, plan_cloth_lanes


class SpatialRulesTests(unittest.TestCase):
    def test_coincident_cloth_uses_stable_parallel_lane_not_attack(self):
        rows = [dict(Guid='bad', TrackIndex=0, LinkValue='.5'),
                dict(Guid='keep', TrackIndex=1, LinkValue='.5'),
                dict(Guid='later', TrackIndex=0, LinkValue='.9')]
        assigned, tracks = plan_cloth_lanes(rows, {'bad', 'keep', 'later'}, ['Attack', 'ClothAudio'])
        self.assertEqual(assigned, {'bad': 2, 'later': 1})
        self.assertEqual(tracks, ['Attack', 'ClothAudio', 'ClothAudio_2'])
        for row in rows:
            row['TrackIndex'] = assigned.get(row['Guid'], row['TrackIndex'])
        self.assertEqual(plan_cloth_lanes(rows, {'bad', 'keep', 'later'}, tracks), ({}, tracks))

    def test_same_track_same_time_new_cloth_is_also_split(self):
        rows = [dict(Guid='a', TrackIndex=0, LinkValue='1'), dict(Guid='b', TrackIndex=0, LinkValue='1')]
        self.assertEqual(plan_cloth_lanes(rows, {'a', 'b'}, ['ClothAudio']),
                         ({'b': 1}, ['ClothAudio', 'ClothAudio_2']))

    def test_recipe_object_order_is_not_serialized_notify_order(self):
        rows = [dict(guid='a', sound=None, time=.1), dict(guid='b', sound='cue', time=.2)]
        self.assertTrue(same_audio_recipes(rows, list(reversed(rows))))
        changed = copy.deepcopy(rows); changed[1]['time'] = .3
        self.assertFalse(same_audio_recipes(rows, changed))
        self.assertFalse(same_audio_recipes(rows, [rows[0], rows[0]]))

    def test_six_categories_and_legacy_track_aliases(self):
        self.assertEqual(len(SOUND_TRACKS), 8)
        self.assertEqual(SOUND_TRACKS['AnimNotify_GroundDust'], SOUND_TRACKS['AnimNotify_DustSound'])
        self.assertEqual(SOUND_TRACKS['AnimNotify_LandingEvent'], SOUND_TRACKS['AnimNotify_LandingSound'])
        self.assertEqual(SOUND_TRACKS['AnimNotify_GroundImpact'], 'ImpactAudio')
        self.assertEqual(len(set(SOUND_TRACKS.values())), 6)

    def test_only_authorized_sound_track_change_is_normalized(self):
        before = dict(existing_notifies=[dict(Guid='sound', TrackIndex='1', LinkValue='.4'),
                                         dict(Guid='gameplay', TrackIndex='0')])
        after = copy.deepcopy(before)
        after['existing_notifies'][0]['TrackIndex'] = '3'
        self.assertEqual(verify_sound_tracks(before, after, {'sound': 3}), before)
        after['existing_notifies'][1]['TrackIndex'] = '2'
        self.assertNotEqual(verify_sound_tracks(before, after, {'sound': 3}), before)

    def test_wrong_track_and_unknown_guid_rejected(self):
        before = dict(existing_notifies=[dict(Guid='sound')])
        with self.assertRaises(RuntimeError):
            verify_sound_tracks(before, before, {'sound': 1})
        with self.assertRaises(RuntimeError):
            verify_sound_tracks(before, before, {'unknown': 0})

    def test_confirmed_categories_only(self):
        self.assertEqual(notify_category('/Game/Cues/SC_IG_MuGu_3777612407_ab'), 'AnimNotify_GroundDust')
        self.assertEqual(notify_category('/Game/Cues/SC_IG_MuGu_2304525234_cd'), 'AnimNotify_LandingEvent')
        self.assertEqual(notify_category('/Game/Cues/SC_IG_MuGu_1762027986_cd'), 'AnimNotify_LandingEvent')

    def test_equipment_or_weapon_not_assumed_cloth(self):
        for sound in ('/Game/Cues/SC_IG_MuGu_361976685_ab', '/Game/Cues/SC_IG_MuGu_2878059743_ab', None):
            self.assertIsNone(notify_category(sound))

    def test_explicit_weapon_endpoints(self):
        self.assertEqual(emission('B006', 'AM_IG_TuCi'), ('WEAPON_SOCKET', 'IG_FrontTip'))
        self.assertEqual(emission('B022', 'AM_IG_SendKinsect'), ('WEAPON_SOCKET', 'IG_RearTip'))
        self.assertEqual(emission('B010', 'AM_IG_ChongYin'), ('WEAPON_SOCKET', 'IG_RearTip'))

    def test_ground_does_not_attach_to_weapon(self):
        for branch in ('B008', 'B016'):
            self.assertEqual(emission(branch, 'AM_IG_TuCi'), ('GROUND_CONTACT', 'Root'))
        self.assertEqual(emission('B031', 'AM_IG_CaoChongZhan'), ('CHARACTER_SOCKET', 'Root'))

    def candidate(self, branch, time, observations=3, boundary=False):
        return dict(branch_id=branch, montage_time_estimate_s=time, observations=observations,
                    cut_boundary_ambiguous=boundary, cluster_id=branch + str(time), segment_index=0, layer='武器层')

    def test_b022_overlapping_hypotheses_authored_once(self):
        data = dict(montages=[dict(path='/Game/AM_IG_SendKinsect.AM_IG_SendKinsect', candidates=[
            self.candidate('B022', .0844, 18), self.candidate('B022', .0908, 12)])])
        points = independent_points(data)
        self.assertEqual(len(points), 1)
        self.assertEqual(points[0]['montage_time_estimate_s'], .0844)

    def test_independent_landing_can_be_mapped_without_ga_route(self):
        data = dict(montages=[dict(path='/Game/AM_IG_AerialLanding.AM_IG_AerialLanding', candidates=[
            self.candidate('B016', .05), self.candidate('B008', .22), self.candidate('B009', .01)])])
        self.assertEqual(len(independent_points(data)), 2)

    def test_unknown_or_cut_candidates_not_synthesized(self):
        data = dict(montages=[dict(path='/Game/Unknown.Unknown', candidates=[self.candidate('B008', .2)]),
            dict(path='/Game/AM_IG_ShouDao.AM_IG_ShouDao', candidates=[self.candidate('B005', .2, boundary=True)])])
        self.assertEqual(independent_points(data), [])

    def test_existing_guids_and_order_survive_append(self):
        before = dict(existing_notifies=[dict(Guid='a', time=1), dict(Guid='b', time=2)], length_s=3)
        after = copy.deepcopy(before)
        after['existing_notifies'].append(dict(Guid='new', time=.2))
        self.assertEqual(verify_preserved(before, after, ['a']), before)
        self.assertEqual(len(after['existing_notifies']), 3)

    def test_preservation_normalization_does_not_mutate_native_snapshot(self):
        before = dict(existing_notifies=[dict(Guid='dust', Notify='Old')])
        after = dict(existing_notifies=[dict(Guid='dust', Notify='New')])
        normalized = verify_preserved(before, after, [])
        normalized['existing_notifies'][0]['Notify'] = 'Old'
        self.assertEqual(after['existing_notifies'][0]['Notify'], 'New')

    def test_sheathe_does_not_accidentally_expand_equipment_layer(self):
        candidate = self.candidate('B001', .2)
        candidate['layer'] = '装备动作层'
        data = dict(montages=[dict(path='/Game/AM_IG_ShouDao.AM_IG_ShouDao', candidates=[candidate])])
        self.assertEqual(independent_points(data), [])


if __name__ == '__main__':
    unittest.main()
