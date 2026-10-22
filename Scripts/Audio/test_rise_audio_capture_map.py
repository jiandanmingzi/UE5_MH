"""Focused offline checks; no game, decoder, wwiser launch or UE."""
from pathlib import Path
import struct
import tempfile
import unittest

import rise_audio_capture_map as mapping
import rise_audio_m1 as m1


class CaptureMapTests(unittest.TestCase):
    def fixtures(self):
        root = m1.PROJECT / 'Saved/Audio/RiseIG/Tests'
        root.mkdir(parents=True, exist_ok=True)
        return tempfile.TemporaryDirectory(dir=root)

    def event(self, action_type, role_bank='pl_wp_ig_ins_com.bnk'):
        return {(role_bank, 1): {'type': 'CAkEvent', 'actions': [2]},
                (role_bank, 2): {'type': action_type, 'action_type': action_type}}

    def test_stop_is_not_a_play_candidate(self):
        result = mapping.describe_event(1, self.event('CAkActionStop'))
        self.assertFalse(result['has_play_action'])
        self.assertFalse(result['in_scope_candidate'])

    def test_voice_play_is_excluded_from_attack_previews(self):
        result = mapping.describe_event(1, self.event('CAkActionPlay', 'pl_voice.bnk'))
        self.assertTrue(result['has_play_action'])
        self.assertFalse(result['in_scope_candidate'])

    def test_empty_event_is_not_an_audio_source(self):
        result = mapping.describe_event(1, {('bank', 1): {'type': 'CAkEvent', 'actions': []}})
        self.assertEqual(len(result['sources']), 1)
        self.assertFalse(result['has_play_action'])

    def test_byte_hit_is_not_automatically_an_event(self):
        self.assertFalse(mapping.describe_event(1, {('bank', 1): {'type': 'CAkSound'}})['sources'])

    def test_missing_action_is_not_silently_accepted(self):
        with self.assertRaises(ValueError):
            mapping.describe_event(1, {('bank', 1): {'type': 'CAkEvent', 'actions': [2]}})

    def test_same_clip_reset_creates_a_new_episode(self):
        rows = [{'sample_index': str(i), 'capture_time_s': str(i / 120), 'component': 'player00',
                 'layer': '0', 'read_status': 'readable', 'node_weight': '1', 'motion_name': 'clip',
                 'action_state_id': '117', 'node_frame': str(frame)}
                for i, frame in enumerate((10, 11, 10.5, 0.1), 1)]
        result = mapping.motion_episodes(rows, {'117': '飞圆斩'})
        self.assertEqual(len(result), 2)
        self.assertEqual(result[0]['last_observed_frame'], 10.5)
        self.assertEqual(result[1]['action_name'], '飞圆斩')

    def test_output_outside_saved_rejected(self):
        with self.assertRaises(ValueError):
            mapping.checked_output(m1.PROJECT / 'Content/Audio')

    def test_existing_m1_root_and_input_overlap_rejected(self):
        with self.assertRaises(ValueError):
            mapping.checked_output(mapping.WORK)
        with self.assertRaises(ValueError):
            mapping.checked_inputs(Path('D:/source'), Path('D:/capture'), Path('D:/capture/derived'))

    def test_persistent_metadata_rejects_mixed_captures(self):
        with self.assertRaises(ValueError):
            mapping.persistent_summary({'capture': 'first'}, {'capture': 'second'}, Path('D:/output'))

    def test_persistent_metadata_preserves_bounded_user_acceptance(self):
        report = {'capture': 'test', 'input_files': {}, 'source_banks': [], 'scan': {},
                  'audio_rows': 0, 'unique_events': 0, 'located_events': 0, 'play_events': 0,
                  'scope_candidates': 0, 'events': [], 'episodes': [], 'limits': []}
        review = {'scope': 'E1_minimum', 'all_candidates_classified': False}
        audition = {'capture': 'test', 'previews': [], 'unresolved': [],
                    'listening_accepted': True, 'user_review': review}
        result = mapping.persistent_summary(report, audition, Path('D:/output'))
        self.assertTrue(result['listening_accepted'])
        self.assertEqual(result['user_review'], review)
        self.assertFalse(result['ue_imported'])

    def test_differing_bank_copy_is_preserved(self):
        with self.fixtures() as fixture:
            original, copy = Path(fixture) / 'source.bnk', Path(fixture) / 'copy.bnk'
            original.write_bytes(b'original'); copy.write_bytes(b'user data')
            with self.assertRaises(ValueError):
                mapping.copy_verified(original, copy, {})
            self.assertEqual(copy.read_bytes(), b'user data')

    def test_shared_txtp_reader_preserves_identity_and_conditions(self):
        with self.fixtures() as fixture:
            path = Path(fixture) / 'bank-001-event [12=34].txtp'
            path.write_text(' # CAkEvent[1] 123\n # Source 456\n 456.wem #i\n', encoding='utf-8')
            result = m1.describe_txtp(path, {'456': [{'source': 'source'}]})
            self.assertEqual((result['event_id'], result['media_ids'], result['conditions']), (123, [456], ['[12=34]']))
            self.assertEqual(result['media_locations']['456'][0]['source'], 'source')

    def test_media_slice_freshly_checks_didx_and_never_overwrites(self):
        def chunk(tag, payload): return tag + struct.pack('<I', len(payload)) + payload
        data = chunk(b'BKHD', struct.pack('<II', 140, 1))
        data += chunk(b'DIDX', struct.pack('<III', 123, 0, 4)) + chunk(b'DATA', b'RIFF')
        with self.fixtures() as fixture:
            original, target = Path(fixture) / 'source.bnk', Path(fixture) / '123.wem'
            original.write_bytes(data)
            location = {'source': str(original), 'source_sha256': m1.sha256(original), **m1.bank_index(data)['media'][0]}
            result = m1.export_media_slice(location, 123, target)
            self.assertEqual(target.read_bytes(), b'RIFF')
            self.assertEqual(result['wem_sha256'], m1.sha256(target))
            with self.assertRaises(ValueError):
                m1.export_media_slice({**location, 'offset': location['offset'] + 1}, 123, target)
            target.write_bytes(b'user data')
            with self.assertRaises(ValueError):
                m1.export_media_slice(location, 123, target)
            self.assertEqual(target.read_bytes(), b'user data')

    def test_truncated_filename_recovers_complete_header_conditions(self):
        body = ('# * gamesyncs: [12=34] [56=78]\n# * statechunks: (90=12)\n'
                '# PATH\n# ~ [12=999]\n# STATECHUNKS\n# - ulStateGroupID: (90=999)\n')
        result = m1.txtp_conditions(body, 'event [12=3~0004.txtp')
        self.assertEqual(result['conditions'], ['[12=34]', '[56=78]', '{s}=(90=12)'])
        self.assertEqual(set(result['condition_sources'].values()), {'txtp_header'})

    def test_header_overrides_misleading_filename_and_empty_is_authoritative(self):
        result = m1.txtp_conditions('# * gamesyncs: [1=2]\n# * statechunks:\n', 'event [1=9] {s}=(3=4).txtp')
        self.assertEqual(result['conditions'], ['[1=2]'])
        self.assertEqual(result['state_conditions'], [])

    def test_legacy_filename_conditions_remain_supported(self):
        result = m1.txtp_conditions('# PATH\n', 'event [1=2] {s}=(3=4).txtp')
        self.assertEqual(result['conditions'], ['[1=2]', '{s}=(3=4)'])
        self.assertEqual(set(result['condition_sources'].values()), {'filename_fallback'})

    def test_refresh_preserves_wave_and_rejects_changed_identity(self):
        with self.fixtures() as fixture:
            text, wave = Path(fixture) / 'truncated.txtp', Path(fixture) / 'test.wav'
            text.write_text('# CAkEvent[1] 123\n# Source 456\n# * gamesyncs: [1=2]\n', encoding='utf-8')
            wave.write_bytes(b'existing wave bytes')
            item = {'event_id': 123, 'media_ids': [456], 'file': str(text), 'wav': str(wave),
                    'sha256': m1.sha256(wave), 'conditions': [], 'user_field': 'preserved'}
            result = m1.refreshed_preview(item)
            self.assertEqual(result['conditions'], ['[1=2]'])
            self.assertEqual(result['user_field'], 'preserved')
            self.assertEqual(result['sha256'], m1.sha256(wave))
            with self.assertRaises(ValueError):
                m1.refreshed_preview({**item, 'event_id': 999})

    def test_random_comparison_only_changes_selector(self):
        with self.fixtures() as fixture:
            path = Path(fixture) / 'event.txtp'
            body = (' a.bnk #s1 #i ##123.wem\n a.bnk #s2 #i ##456.wem\n'
                    'group = -R2>1\n\n# CAkEvent[1] 123\n# Source 123\n# Source 456\n')
            path.write_text(body, encoding='utf-8')
            choices = mapping.random_comparison_bodies(m1.describe_txtp(path, {}))
            self.assertEqual([v['selected_media_id'] for v in choices], [123, 456])
            self.assertEqual(choices[0]['body'], body)
            self.assertEqual(choices[1]['body'], body.replace('-R2>1', '-R2>2'))
            with self.assertRaises(ValueError):
                mapping.random_comparison_bodies({'file': str(path), 'render_lines': ['group = -L2']})

    def test_comparison_groups_identical_graphs_not_just_matching_media(self):
        with self.fixtures() as fixture:
            paths = []
            for index, (condition, gain) in enumerate(((1, 2), (2, 2), (2, 3))):
                path = Path(fixture) / f'event-{index}.txtp'
                path.write_text(f' a.bnk #s1 #v {gain}dB\n# CAkEvent[1] 123\n# * gamesyncs: [4={condition}]\n', encoding='utf-8')
                paths.append(path)
            self.assertEqual([len(group) for group in mapping.comparison_groups(paths)], [2, 1])


if __name__ == '__main__':
    unittest.main()
