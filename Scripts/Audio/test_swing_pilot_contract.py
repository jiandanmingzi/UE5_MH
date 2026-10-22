"""Execute the actual pure preservation guards, without importing Unreal."""
import ast
import copy
from pathlib import Path
import re
import unittest


SOURCE = Path(__file__).with_name('setup_swing_pilot.py')
tree = ast.parse(SOURCE.read_text(encoding='utf-8'))
scope = {'re': re}
for node in tree.body:
    if isinstance(node, ast.FunctionDef) and node.name in ('verify_originals', 'non_audio_cdo'):
        exec(compile(ast.Module(body=[node], type_ignores=[]), str(SOURCE), 'exec'), scope)


class PilotContractTests(unittest.TestCase):
    def before(self):
        return {'length_s': 2, 'rate_scale': 1, 'segments': [{'cut': [0, 2], 'rate': 2}],
                'sections': [{'name': 'Core', 'next': ''}],
                'existing_notifies': [{'Guid': 'a', 'TrackIndex': '0', 'LinkValue': '0.2'},
                                      {'Guid': 'b', 'TrackIndex': '1', 'LinkValue': '0.3'}]}

    def test_new_notify_does_not_mutate_originals(self):
        before = self.before()
        after = copy.deepcopy(before)
        after['existing_notifies'].insert(1, {'Guid': 'new', 'TrackIndex': '2', 'LinkValue': '0.25'})
        scope['verify_originals'](before, after)

    def test_original_time_or_track_change_is_rejected(self):
        for field in ('TrackIndex', 'LinkValue', 'Guid'):
            before = self.before()
            after = copy.deepcopy(before)
            after['existing_notifies'][0][field] = 'changed'
            with self.assertRaises(RuntimeError): scope['verify_originals'](before, after)

    def test_original_relative_order_is_preserved(self):
        before = self.before()
        after = copy.deepcopy(before)
        after['existing_notifies'].reverse()
        with self.assertRaises(RuntimeError): scope['verify_originals'](before, after)

    def test_animation_or_section_change_is_rejected(self):
        for field in ('length_s', 'rate_scale', 'segments', 'sections'):
            before = self.before()
            after = copy.deepcopy(before)
            after[field] = 'changed'
            with self.assertRaises(RuntimeError): scope['verify_originals'](before, after)

    def test_cdo_guard_only_allows_two_audio_fields(self):
        original = '  Damage=10\n  AudioIdentityTag=()\n'
        audio = '  Damage=10\n  AudioIdentityTag=(TagName="Audio.X")\n  bUseAuthoredSwingAudio=True\n'
        self.assertEqual(scope['non_audio_cdo'](original), scope['non_audio_cdo'](audio))
        self.assertNotEqual(scope['non_audio_cdo'](original), scope['non_audio_cdo'](audio.replace('Damage=10', 'Damage=20')))


if __name__ == '__main__': unittest.main()
