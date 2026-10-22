"""Focused branch audio checks; no UE, game, Content writes or decoder."""
import copy
import unittest

from section_audio_rules import handoff_patch, report
import map_swing_positions as positions


class SectionAudioRulesTests(unittest.TestCase):
    def fixture(self):
        m = dict(path='/Game/M.M', length_s=3., sections=[
            dict(name='A', time_s=0., next='Core'),
            dict(name='B', time_s=.5, next='Core'),
            dict(name='Core', time_s=1., next='')], segments=[
            dict(slot=0, index=0, sequence='alternative', track_start_s=0.,
                 source_start_s=0., source_end_s=1., effective_rate=2., loops=1),
            dict(slot=0, index=1, sequence='source', track_start_s=.5,
                 source_start_s=0., source_end_s=1., effective_rate=2., loops=1),
            dict(slot=0, index=2, sequence='source', track_start_s=1.,
                 source_start_s=1., source_end_s=5., effective_rate=2., loops=1)], audio=[])
        row = dict(time=.96, guid='existing', event_tag='(TagName="Audio.Test")', sound='/Game/Sound')
        anchor = dict(slot=0, segment_index=1, loop=0, sequence='source', source_time_estimate_s=.92)
        m['audio'] = [row]
        return m, row, anchor

    def test_source_lead_moves_to_skipping_entry_not_core(self):
        m, row, a = self.fixture()
        patch = handoff_patch(m, row, a, 'A', 'Core')
        self.assertAlmostEqual(patch['lead_s'], .04)
        self.assertAlmostEqual(patch['time'], .46)
        self.assertTrue(positions.reachable(patch['time'], positions.route(m, 'A')))
        self.assertFalse(positions.reachable(patch['time'], positions.route(m, 'B')))
        self.assertEqual(row['time'], .96)

    def test_non_skipping_path_must_not_duplicate(self):
        with self.assertRaisesRegex(ValueError, 'skipped'):
            handoff_patch(*self.fixture(), 'B', 'Core')

    def test_rate_changes_use_source_time_not_linear_montage_offset(self):
        m, row, a = self.fixture()
        m['segments'][2]['effective_rate'] = 4.
        patch = handoff_patch(m, row, a, 'A', 'Core')
        self.assertAlmostEqual(patch['time'], .48)

    def test_source_mismatch_rejected(self):
        m, row, a = self.fixture()
        a['sequence'] = 'other'
        with self.assertRaisesRegex(ValueError, 'same forward'):
            handoff_patch(m, row, a, 'A', 'Core')

    def test_runtime_cycle_not_guessed(self):
        m, row, a = self.fixture()
        m['sections'][2]['next'] = 'Core'
        with self.assertRaisesRegex(ValueError, 'cycle'):
            handoff_patch(m, row, a, 'A', 'Core')

    def test_insufficient_prefix_not_shifted_into_another_section(self):
        m, row, a = self.fixture()
        a['source_time_estimate_s'] = -.1
        with self.assertRaisesRegex(ValueError, 'fit'):
            handoff_patch(m, row, a, 'A', 'Core')

    def test_existing_copy_rejected(self):
        m, row, a = self.fixture()
        m['audio'].append(dict(row, time=.46, guid='copy'))
        with self.assertRaisesRegex(ValueError, 'already exists'):
            handoff_patch(m, row, a, 'A', 'Core')

    def test_branch_reroute_that_would_double_play_rejected(self):
        m, row, a = self.fixture()
        m['sections'][0]['next'] = 'B'
        with self.assertRaisesRegex(ValueError, 'direct Section'):
            handoff_patch(m, row, a, 'A', 'Core')

    def test_audit_distinguishes_source_drift_and_traversal_clock(self):
        m, row, a = self.fixture()
        snapshot = dict(montages=[m])
        anchors = {(m['path'], 'Audio.Test'): a}
        before = copy.deepcopy(snapshot)
        result = report(snapshot, anchors)
        self.assertEqual(result['checked_source_anchors'], 1)
        self.assertFalse(result['source_anchor_drift'])
        b_route = next(r for r in result['routes'] if r['entry'] == 'B')
        self.assertAlmostEqual(b_route['events'][0]['traversal_time'], .46)
        self.assertEqual(snapshot, before)
        row['time'] += .1
        self.assertEqual(len(report(snapshot, anchors)['source_anchor_drift']), 1)


if __name__ == '__main__':
    unittest.main()
