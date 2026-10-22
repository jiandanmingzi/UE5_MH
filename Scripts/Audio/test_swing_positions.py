"""Focused offline math/association checks; no UE/game/decoder launch."""
import unittest

import map_swing_positions as mapping


class SwingPositionTests(unittest.TestCase):
    def test_native_audio_ignores_delayed_ipc_clock(self):
        rows = [dict(self.sample(1, 1., 20), render_callback='100'),
                dict(self.sample(2, 1.01, 21), render_callback='101')]
        result = mapping.bracket_audio(rows, [1., 1.01], dict(native_present_frame='100', capture_time_estimate_s='.75'))
        self.assertEqual(result['native_frame_estimate'], 20)
        self.assertAlmostEqual(result['estimate_clock_offset_s'], .25)

    def test_missing_present_counter_does_not_silently_use_estimate(self):
        rows = [dict(self.sample(1, 1., 20), render_callback='100')]
        result = mapping.bracket_audio(rows, [1.], dict(capture_time_estimate_s='1.'))
        self.assertIsNone(result['native_frame_estimate'])

    def test_present_frame_cannot_cross_clip_boundary(self):
        rows = [dict(self.sample(1, 1., 20), render_callback='100'),
                dict(self.sample(2, 1.01, 0, 2), render_callback='101')]
        result = mapping.bracket_audio(rows, [1., 1.01], dict(native_present_frame='100', capture_time_estimate_s='1'))
        self.assertIsNone(result['native_frame_estimate'])

    def sample(self, sample, time, frame, episode=1):
        return {'sample_index': str(sample), 'capture_time_s': str(time),
                'node_frame': str(frame), 'node_end_frame': '91',
                'motion_name': 'plw_InsectGlaive_100_118', '_episode': episode}

    def cluster(self, frame=30):
        return {'source_id': 118, 'native_end_frames': [91], 'native_frame_estimate': frame,
                'native_frame_range': [frame - 0.2, frame + 0.2], 'id': 'test', 'event_id': 123}

    def sequence(self):
        return {'import_numeric_ids': [118], 'frames': 180, 'fps': 60, 'import_file_fps': 60}

    def segment(self):
        return {'source_start_s': 0.5, 'source_end_s': 2, 'effective_rate': 2,
                'track_start_s': 3, 'loops': 1, 'index': 0, 'slot': 0, 'sequence': 'seq'}

    def test_interpolate_same_clip(self):
        rows = [self.sample(1, 1, 20), self.sample(2, 1.01, 21)]
        result = mapping.bracket(rows, [1, 1.01], 1.005)
        self.assertEqual(result['status'], 'temporal_candidate')
        self.assertAlmostEqual(result['native_frame_estimate'], 20.5)

    def test_no_interpolation_across_clip_reset(self):
        rows = [self.sample(1, 1, 20), self.sample(2, 1.01, 0, 2)]
        result = mapping.bracket(rows, [1, 1.01], 1.005)
        self.assertEqual(result['status'], 'clip_or_reset_boundary')
        self.assertIsNone(result['native_frame_estimate'])

    def test_long_sample_gap_is_not_a_frame_estimate(self):
        rows = [self.sample(1, 1, 20), self.sample(2, 2, 80)]
        self.assertEqual(mapping.bracket(rows, [1, 2], 1.5)['status'], 'gap_or_reverse_clock')

    def test_before_and_after_capture_are_not_extrapolated(self):
        rows = [self.sample(1, 1, 20), self.sample(2, 1.01, 21)]
        for time in (0.99, 1.02):
            self.assertIsNone(mapping.bracket(rows, [1, 1.01], time)['native_frame_estimate'])

    def test_calibration_requires_source_identity(self):
        seq = dict(self.sequence(), import_numeric_ids=[119])
        self.assertIsNone(mapping.frame_calibration(seq, self.cluster()))

    def test_calibration_requires_matching_clip_extent(self):
        seq = dict(self.sequence(), frames=90)
        self.assertIsNone(mapping.frame_calibration(seq, self.cluster()))

    def test_calibration_does_not_invent_30fps_import(self):
        result = mapping.frame_calibration(self.sequence(), self.cluster())
        self.assertEqual(result['native_to_ue_frame_scale'], 2)
        self.assertEqual(result['import_file_fps'], 60)

    def test_effective_rate_conversion(self):
        result = mapping.segment_positions(self.segment(), self.sequence(), self.cluster())
        self.assertAlmostEqual(result[0]['source_time_estimate_s'], 1)
        self.assertAlmostEqual(result[0]['montage_time_estimate_s'], 3.25)

    def test_cut_start_inclusive_and_end_exclusive(self):
        self.assertEqual(len(mapping.segment_positions(self.segment(), self.sequence(), self.cluster(15))), 1)
        self.assertEqual(mapping.segment_positions(self.segment(), self.sequence(), self.cluster(60)), [])

    def test_cut_boundary_uncertainty_is_reported(self):
        result = mapping.segment_positions(self.segment(), self.sequence(), self.cluster(15))
        self.assertTrue(result[0]['cut_boundary_ambiguous'])

    def test_repeated_segment_repeats_event_positions(self):
        result = mapping.segment_positions(dict(self.segment(), loops=2), self.sequence(), self.cluster())
        self.assertEqual(len(result), 2)
        self.assertAlmostEqual(result[1]['montage_time_estimate_s'] - result[0]['montage_time_estimate_s'], 0.75)

    def test_reverse_segment(self):
        result = mapping.segment_positions(dict(self.segment(), effective_rate=-2), self.sequence(), self.cluster())
        self.assertAlmostEqual(result[0]['montage_time_estimate_s'], 3.5)

    def montage(self):
        return {'length_s': 3, 'sections': [{'name': 'EntryA', 'time_s': 0, 'next': 'Core'},
                {'name': 'EntryB', 'time_s': 1, 'next': 'Core'},
                {'name': 'Core', 'time_s': 2, 'next': 'None'}]}

    def test_section_jump_does_not_play_other_entry(self):
        result = mapping.route(self.montage(), 'EntryA')
        self.assertTrue(mapping.reachable(0.5, result))
        self.assertFalse(mapping.reachable(1.5, result))
        self.assertTrue(mapping.reachable(2.5, result))

    def test_entry_b_skips_entry_a(self):
        result = mapping.route(self.montage(), 'EntryB')
        self.assertFalse(mapping.reachable(0.5, result))

    def test_hold_cycle_is_bounded_and_explicit(self):
        montage = self.montage()
        montage['sections'][0]['next'] = 'EntryA'
        result = mapping.route(montage, 'EntryA')
        self.assertEqual(result['status'], 'cycle_prefix_only')
        self.assertEqual(len(result['sections']), 1)

    def test_invalid_entry_is_not_fallback(self):
        self.assertEqual(mapping.route(self.montage(), 'Unknown')['status'], 'invalid_entry')

    def test_entry_config_precedence(self):
        config = {'entry_section_by_transition_id': {'edge': 'A'},
                  'entry_section_by_source_state': {'source': 'B'}, 'default_entry_section': 'C'}
        self.assertEqual(mapping.entry_for(config, 'edge', 'source'), 'A')
        self.assertEqual(mapping.entry_for(config, 'other', 'source'), 'B')
        self.assertEqual(mapping.entry_for(config, 'other', 'other'), 'C')
        self.assertIsNone(mapping.entry_for({'default_entry_section': 'None'}, '', ''))

    def event(self, frame, episode):
        return {'motion': 'plw_InsectGlaive_100_118', 'status': 'temporal_candidate',
                'event_id': 123, 'capture': 'test', 'episode': episode, 'native_frame_estimate': frame,
                'native_end_frame': 91, 'audio_row': 1, 'native_frames': [frame, frame], 'sample_times_s': [0, 0.01]}

    def test_cluster_keeps_repeated_sound_in_same_episode(self):
        result = mapping.cluster_positions([self.event(10, 1), self.event(10.2, 1), self.event(10.1, 2)])
        self.assertEqual(len(result), 2)
        self.assertEqual(sorted(c['observations'] for c in result), [1, 2])

    def test_cluster_span_is_bounded_not_chained(self):
        result = mapping.cluster_positions([self.event(10, 1), self.event(12, 2), self.event(14, 3)])
        self.assertEqual(len(result), 2)


if __name__ == '__main__':
    unittest.main()
