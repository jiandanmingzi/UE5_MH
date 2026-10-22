import copy
import unittest
from rise_audio_cloth_comparison import plugin_silence, paired_windows


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


if __name__ == '__main__':
    unittest.main()
