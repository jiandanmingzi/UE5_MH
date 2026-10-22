import copy
import unittest
from spatial_audio_rules import transfer_vault_stage_audio


class VaultStageTransferTests(unittest.TestCase):
    def fixture(self):
        segment = dict(track_start_s=0, source_start_s=0, source_end_s=3, effective_rate=2)
        source = dict(path='white', segments=[segment], audio=[
            dict(guid='kept', time=.6, sound='cue', kind='AnimNotify_SwingSound'),
            dict(guid='silent', time=.1, sound=None, kind='AnimNotify_ClothSound')])
        target = dict(segments=[dict(segment,source_end_s=2,effective_rate=1.5)], length_s=2/1.5)
        return source,target

    def test_real_source_frame_and_rate_not_whole_length_stretch(self):
        a,b = self.fixture();original=copy.deepcopy(a)
        r = transfer_vault_stage_audio(a,b,156,142)
        self.assertEqual(len(r),1)
        self.assertAlmostEqual(r[0]['time'],.8)
        self.assertEqual(r[0]['source_guid'],'kept')
        self.assertEqual(a,original)

    def test_unrelated_sources_and_outside_crop_do_not_get_guessed(self):
        a,b = self.fixture()
        with self.assertRaises(ValueError):transfer_vault_stage_audio(a,b,137,141)
        b['segments'][0]['source_end_s']=1
        with self.assertRaises(ValueError):transfer_vault_stage_audio(a,b,156,142)

    def test_unknown_multi_segment_or_invalid_rate_is_rejected(self):
        for variant in ('multi','rate'):
            a,b = self.fixture()
            if variant=='multi':b['segments'].append(b['segments'][0].copy())
            if variant=='rate':b['segments'][0]['effective_rate']=0
            with self.assertRaises(ValueError):transfer_vault_stage_audio(a,b,155,141)


if __name__=='__main__':unittest.main()
