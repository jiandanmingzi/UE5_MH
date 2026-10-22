"""Focused frozen-recipe and actual preservation guards; no UE/game access."""
import ast
import copy
from pathlib import Path
import unittest
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from ue_asset_utils import export_fields

import plan_swing_coverage as planning

scope = {'copy': copy, 'export_fields': export_fields}
pilot_scope = {'re': re}
pilot_source = Path(__file__).with_name('setup_swing_pilot.py')
for node in ast.parse(pilot_source.read_text(encoding='utf-8')).body:
    if isinstance(node, ast.FunctionDef) and node.name == 'verify_originals':
        exec(compile(ast.Module(body=[node], type_ignores=[]), str(pilot_source), 'exec'), pilot_scope)
from types import SimpleNamespace
scope['pilot'] = SimpleNamespace(verify_originals=pilot_scope['verify_originals'])
source = Path(__file__).with_name('setup_swing_coverage.py')
for node in ast.parse(source.read_text(encoding='utf-8')).body:
    if isinstance(node, ast.FunctionDef) and node.name in ('verify_tracks', 'find_recipe', 'verify_montage'):
        exec(compile(ast.Module(body=[node], type_ignores=[]), str(source), 'exec'), scope)


class CoverageTests(unittest.TestCase):
    def fixture(self, name='ErLian'):
        path = '/Game/AM_IG_' + name + '.AM_IG_' + name
        candidates = [dict(event_id=e, cluster_id=f'motion:{e}:{i+1}', segment_index=0,
                           loop=0, montage_time_estimate_s=.1 + .1 * i, cut_boundary_ambiguous=False)
                      for i, e in enumerate((2702726931, 2231624382))]
        montage = dict(path=path, length_s=2, candidates=candidates,
                       routes=[dict(intervals=[[0, 2]])], missing_evidence=[])
        ability = dict(path='/Game/GA_IG_' + name + '.GA_IG_' + name, montages=[path])
        snapshot = dict(success=True, content_unchanged=True, montages=[montage], protected_package_hashes={})
        positions = dict(montages=[montage], abilities=[ability], captures=[], limits=['temporal_only'])
        audition = {'previews': [dict(event_id=e, sha256=str(e), wav='x.wav', seconds=.5,
                             channels=1, sample_rate=48000, semantic_review=dict(status='user_confirmed',
                             reviewed_wav_sha256=str(e))) for e in planning.EVENTS]}
        return snapshot, positions, audition

    def test_same_event_repeated_in_distinct_segment_is_retained(self):
        s, p, a = self.fixture()
        point = copy.deepcopy(p['montages'][0]['candidates'][0])
        point['segment_index'], point['montage_time_estimate_s'] = 1, 1.1
        p['montages'][0]['candidates'].append(point)
        result = planning.make_plan(s, p, a)
        self.assertEqual(len(result['montages'][0]['recipes']), 3)
        self.assertEqual(len(set(r['event_tag'] for r in result['montages'][0]['recipes'])), 3)

    def test_ambiguous_points_are_isolated_not_written(self):
        s, p, a = self.fixture()
        p['montages'][0]['candidates'][0]['cut_boundary_ambiguous'] = True
        result = planning.make_plan(s, p, a)
        self.assertEqual(len(result['skipped']), 1)
        self.assertEqual(len(result['montages'][0]['recipes']), 1)

    def test_missing_phase_is_preserved_not_invented(self):
        s, p, a = self.fixture()
        p['montages'][0]['missing_evidence'] = [{'source_ids': [312], 'reason': 'missing'}]
        result = planning.make_plan(s, p, a)
        self.assertEqual(result['missing_evidence'][0]['source_ids'], [312])
        self.assertFalse(result['complete_original_audio_coverage'])

    def test_pilot_tags_remain_unchanged(self):
        result = planning.make_plan(*self.fixture('TuCi'))
        self.assertEqual([r['event_tag'] for r in result['montages'][0]['recipes']],
                         ['Audio.Swing.IG.TuCi.First', 'Audio.Swing.IG.TuCi.Second'])

    def test_wrong_or_unconfirmed_source_rejected(self):
        for field, value in (('status', 'unknown'), ('reviewed_wav_sha256', 'changed')):
            s, p, a = self.fixture()
            a['previews'][0]['semantic_review'][field] = value
            with self.assertRaises(ValueError): planning.make_plan(s, p, a)

    def test_unreachable_point_rejected(self):
        s, p, a = self.fixture()
        p['montages'][0]['routes'][0]['intervals'] = [[1, 2]]
        with self.assertRaises(ValueError): planning.make_plan(s, p, a)

    def test_unowned_montage_is_not_authored(self):
        s, p, a = self.fixture()
        p['abilities'] = []
        with self.assertRaises(ValueError): planning.make_plan(s, p, a)

    def test_frozen_plan_fingerprint_is_sensitive(self):
        result = planning.make_plan(*self.fixture())
        sha = result.pop('plan_sha256')
        self.assertEqual(planning.fingerprint(result), sha)
        result['montages'][0]['recipes'][0]['montage_time_estimate_s'] += .01
        self.assertNotEqual(planning.fingerprint(result), sha)

    def test_track_append_only(self):
        before = [{'TrackName': '"Attack"'}, {'TrackName': '"Combo"'}]
        scope['verify_tracks'](before, before + [{'TrackName': '"SwingAudio"'}])
        with self.assertRaises(RuntimeError): scope['verify_tracks'](before, list(reversed(before)))
        with self.assertRaises(RuntimeError): scope['verify_tracks'](before, before + [{'TrackName': '"Other"'}])

    def test_existing_recipe_cannot_be_silently_moved_or_duplicated(self):
        recipe = dict(event_tag='Audio.Test', montage_time_estimate_s=.2, event_id=1)
        row = dict(event_tag='(TagName="Audio.Test")', time_s=.2, sound='/Game/SC.SC', legacy_index=-1)
        self.assertIs(scope['find_recipe']([row], recipe, {1: dict(cue='/Game/SC')}), row)
        with self.assertRaises(RuntimeError): scope['find_recipe']([row, row], recipe, {1: dict(cue='/Game/SC')})
        row['time_s'] = .4
        with self.assertRaises(RuntimeError): scope['find_recipe']([row], recipe, {1: dict(cue='/Game/SC')})

    def cache_fixture(self):
        return dict(length_s=2, rate_scale=1, segments=[], sections=[], existing_notifies=[
            dict(Guid='a', SegmentLength='1.783334', LinkValue='0.500000', Duration='0.300000',
                 EndLink='(SegmentLength=1.783334,LinkValue=0.800000)')])

    def test_measured_absolute_cache_roundoff_allowed_and_reported(self):
        before = self.cache_fixture()
        after = copy.deepcopy(before)
        after['existing_notifies'][0]['SegmentLength'] = '1.783333'
        after['existing_notifies'][0]['EndLink'] = '(SegmentLength=1.783333,LinkValue=0.800000)'
        self.assertEqual(len(scope['verify_montage'](before, after)), 2)
        self.assertEqual(after['existing_notifies'][0]['SegmentLength'], '1.783333')

    def test_window_times_and_duration_remain_exact(self):
        for field in ('LinkValue', 'Duration'):
            before = self.cache_fixture()
            after = copy.deepcopy(before)
            after['existing_notifies'][0][field] = str(float(after['existing_notifies'][0][field]) + .000001)
            with self.assertRaises(RuntimeError): scope['verify_montage'](before, after)

    def test_end_time_or_relative_link_cannot_hide_in_cache_exception(self):
        before = self.cache_fixture()
        after = copy.deepcopy(before)
        after['existing_notifies'][0]['EndLink'] = '(SegmentLength=1.783333,LinkValue=0.800001)'
        with self.assertRaises(RuntimeError): scope['verify_montage'](before, after)
        before['existing_notifies'][0]['LinkMethod'] = 'Proportional'
        after = copy.deepcopy(before)
        after['existing_notifies'][0]['SegmentLength'] = '1.783333'
        with self.assertRaises(RuntimeError): scope['verify_montage'](before, after)

    def test_larger_cache_change_rejected(self):
        before = self.cache_fixture()
        after = copy.deepcopy(before)
        after['existing_notifies'][0]['SegmentLength'] = '1.783300'
        with self.assertRaises(RuntimeError): scope['verify_montage'](before, after)

    def test_measured_single_clip_absolute_end_cache_clear(self):
        before = self.cache_fixture()
        before['length_s'] = 1.7833333
        before['segments'] = [dict(sequence='/Game/AS.AS')]
        before['existing_notifies'][0]['EndLink'] = '(LinkedMontage="M",SegmentIndex=0,SegmentLength=1.783334,LinkValue=1.783333,LinkedSequence="/Game/AS.AS")'
        after = copy.deepcopy(before)
        after['existing_notifies'][0]['EndLink'] = '(LinkedMontage="M",SegmentBeginTime=-1.000000,SegmentLength=-1.000000,LinkValue=1.783333)'
        self.assertEqual(scope['verify_montage'](before, after)[0]['kind'], 'native_absolute_single_clip_end_unlinked')
        after['existing_notifies'][0]['EndLink'] = after['existing_notifies'][0]['EndLink'].replace('LinkValue=1.783333', 'LinkValue=1.783332')
        with self.assertRaises(RuntimeError): scope['verify_montage'](before, after)

    def test_cached_relative_method_cannot_use_absolute_exception(self):
        before = self.cache_fixture()
        before['existing_notifies'][0]['CachedLinkMethod'] = 'Relative'
        after = copy.deepcopy(before)
        after['existing_notifies'][0]['SegmentLength'] = '1.783333'
        with self.assertRaises(RuntimeError): scope['verify_montage'](before, after)


if __name__ == '__main__':
    unittest.main()
