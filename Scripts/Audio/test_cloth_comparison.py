import copy
import unittest
from rise_audio_cloth_comparison import (plugin_silence, paired_windows,
    aerial_montage, powder_hit_neighbors)


class ClothComparisonTests(unittest.TestCase):
    def window(self, **extra):
        return dict(motion='plw_InsectGlaive_100_120', event_id=361976685,
                    native_end_frames=[44.0], native_frame_estimate=12.0, observations=3, **extra)

    def test_plugin_source_id_is_not_audio_media_proof(self):
        self.assertTrue(plugin_silence(dict(media_ids=[511255348], render_lines=['?.plugin-silence #B 1.0 #i'])))

    def test_audible_or_mixed_graph_is_not_silent(self):
        self.assertFalse(plugin_silence(dict(render_lines=['wem/123.wem #i'])))
        self.assertFalse(plugin_silence(dict(render_lines=['?.plugin-silence #B 1.0 #i', 'wem/123.wem #i'])))
        self.assertFalse(plugin_silence(dict(render_lines=[])))

    def test_unique_repeated_same_source_pair(self):
        a, b = self.window(), self.window()
        b['native_frame_estimate'] = 13
        self.assertEqual(len(paired_windows([a], [b])), 1)
        self.assertEqual(a['native_frame_estimate'], 12)

    def test_ambiguous_either_direction_is_rejected(self):
        a, b = self.window(), self.window()
        self.assertEqual(paired_windows([a], [b, copy.deepcopy(b)]), [])
        self.assertEqual(paired_windows([a, copy.deepcopy(a)], [b]), [])

    def test_sparse_different_clip_end_or_far_frame_rejected(self):
        for field, value in [('observations', 1), ('event_id', 1), ('motion', 'different'),
                             ('native_end_frames', [45]), ('native_frame_estimate', 20)]:
            b = self.window(); b[field] = value
            self.assertEqual(paired_windows([self.window()], [b]), [])

    def test_aerial_write_scope_excludes_ground_and_world_only_motion(self):
        for name in ('AM_IG_AirDodge', 'AM_IG_CaoChongZhan_Dance',
                     'AM_IG_HouChengGanTiao_W_Over', 'AM_IG_AerialLanding',
                     'AM_IG_JueChongJi', 'AM_IG_QiangHuaCaoChongChuanCi_Over'):
            self.assertTrue(aerial_montage(name), name)
        for name in ('AM_IG_ErLian', 'AM_IG_PowderVortex', 'AM_IG_JiXiTuCi',
                     'AM_IG_Dodge_Forward', 'AM_IG_AirDodge_Unexpected'):
            self.assertFalse(aerial_montage(name), name)

    def test_powder_neighbors_use_native_counter_not_delayed_estimate(self):
        hit = dict(row='1', kind='damage_side_callback', attacker_name='bomb',
                   attacker_address='123', frame_callback='300.000000',
                   capture_time_last_sample_s='10')
        posts = [dict(row='2', event_id='615339279', native_present_frame='299',
                      capture_time_estimate_s='1'),
                 dict(row='3', event_id='647565313', native_present_frame='303',
                      capture_time_estimate_s='10'),
                 dict(row='4', event_id='539819764', native_present_frame='300')]
        result = powder_hit_neighbors([hit], posts)
        self.assertEqual(result[0]['audio'], [dict(audio_row='2', event_id=615339279, frame_delta=-1)])
        self.assertNotIn('exact_owner', result[0])

    def test_kinsect_hit_cannot_be_mislabeled_powder_explosion(self):
        hit = dict(kind='damage_side_callback', attacker_name='IG_Ins008')
        self.assertEqual(powder_hit_neighbors([hit], []), [])


if __name__ == '__main__':
    unittest.main()
