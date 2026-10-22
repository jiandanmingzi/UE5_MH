import copy
import math
import unittest
from prepare_ground_audio import candidate_points, impact_point, descending_points, LANDING_EVENT


class GroundAudioRulesTests(unittest.TestCase):
    def fixture(self, name='AM_IG_ErLian', source_id=111, event=938078130, layer='武器层'):
        segment = dict(sequence='seq', index=1, slot=0, loops=1, source_start_s=1.,
                       source_end_s=2., effective_rate=2., track_start_s=3.)
        snapshot = dict(sequences={'seq': dict(import_numeric_ids=[source_id], frames=120,
            fps=60, import_file_fps=60)}, montages=[dict(path='/Game/' + name + '.' + name,
            segments=[segment], audio=[])])
        cluster = dict(id='c', source_id=source_id, event_id=event, branch_id='b', layer=layer,
            observations=4, native_end_frames=[61.], native_frame_estimate=45., native_frame_range=[44.9, 45.1])
        return snapshot, [cluster], {'b': dict(cue='/Game/Sound')}

    def points(self, args):
        return next(iter(candidate_points(*args).values()))

    def test_actual_segment_time_not_ga_timer(self):
        point = self.points(self.fixture())[0]
        self.assertAlmostEqual(point['time'], 3.25)
        self.assertEqual((point['kind'], point['source'], point['socket']),
                         ('AnimNotify_SwingSound', 'WEAPON_SOCKET', 'IG_FrontTip'))

    def test_landing_uses_observed_contact_not_generic_substitution(self):
        point = self.points(self.fixture('AM_IG_TuJinHuiXuan', 160, LANDING_EVENT))[0]
        self.assertEqual((point['kind'], point['source'], point['socket']),
                         ('AnimNotify_LandingEvent', 'GROUND_CONTACT', 'Root'))
        self.assertEqual(self.points(self.fixture('AM_IG_TuJinHuiXuan', 160, 2304525234)), [])

    def test_cloth_or_single_observation_not_autofilled(self):
        self.assertEqual(self.points(self.fixture(layer='装备动作层')), [])
        args = self.fixture()
        args[1][0]['observations'] = 1
        self.assertEqual(self.points(args), [])

    def test_wrong_source_or_cropped_frame_not_clamped(self):
        args = self.fixture()
        args[1][0]['source_id'] = 112
        self.assertEqual(self.points(args), [])
        args = self.fixture()
        args[1][0]['native_frame_estimate'] = 10
        args[1][0]['native_frame_range'] = [9.9, 10.1]
        self.assertEqual(self.points(args), [])

    def test_existing_same_sound_near_time_is_not_duplicated(self):
        args = self.fixture()
        args[0]['montages'][0]['audio'] = [dict(sound='/Game/Sound.Sound', time=3.25)]
        before = copy.deepcopy(args)
        self.assertEqual(self.points(args), [])
        self.assertEqual(args, before)

    def test_impact_uses_retained161_and_own_event(self):
        snapshot, clusters, _ = self.fixture('AM_IG_SiLianYinZhan', 161, LANDING_EVENT)
        point = impact_point(snapshot, clusters[0], '/Game/Contact')
        self.assertAlmostEqual(point['time'], 3.25)
        self.assertEqual(point['kind'], 'AnimNotify_GroundImpact')
        clusters[0]['source_id'] = 160
        with self.assertRaises(ValueError):
            impact_point(snapshot, clusters[0], '/Game/Contact')

    def test_impact_does_not_guess_clamp_or_duplicate(self):
        args = self.fixture('AM_IG_SiLianYinZhan', 161, LANDING_EVENT)
        for field, value in [('observations', 1), ('event_id', 2304525234), ('native_frame_estimate', 5.)]:
            snapshot, clusters, _ = copy.deepcopy(args)
            clusters[0][field] = value
            with self.assertRaises(ValueError):
                impact_point(snapshot, clusters[0], '/Game/Contact')
        snapshot, clusters, _ = args
        snapshot['montages'][0]['audio'] = [dict(time=3.25, sound='/Game/Contact.Contact')]
        with self.assertRaises(ValueError):
            impact_point(snapshot, clusters[0], '/Game/Contact')


class DescendingAudioAdaptationTests(unittest.TestCase):
    def fixture(self):
        def montage(name, length, attacks=(), audio=()):
            return dict(path='/Game/' + name + '.' + name, length_s=length, audio=list(audio), sections=[],
                existing_notifies=[dict(NotifyStateClass='AnimNotifyState_AttackCollision', LinkValue=t, Duration=d) for t,d in attacks])
        def sound(event, time, dust=False):
            return dict(sound=f'/Game/SC_IG_MuGu_{event}_exact.Exact', time=time, guid=str(event)+str(time),
                kind='AnimNotify_GroundDust' if dust else 'AnimNotify_SwingSound', socket='Root' if dust else 'IG_FrontTip')
        main = montage('AM_IG_JiXiTuCi', 1.15)
        main['sections'] = [dict(name='FallingLoop', time_s=.65)]
        over = montage('AM_IG_JiXiTuCi_Over', 2.7, ((.583,.117),(1.2,.166)))
        sweep = montage('AM_IG_HengSao', 2.24, ((.1,.166),(.49,.166)),
            (sound(4196403743,.01),sound(2231624382,.05),sound(3777612407,.25,True)))
        double = montage('AM_IG_ErLian', 3., ((.333,.166),(.756,.166),(1.166,.166)),
            (sound(2702726931,1.1),sound(1392529433,1.24),sound(3777612407,.24,True),sound(3777612407,1.44,True),sound(938078130,2.59)))
        air = montage('AM_IG_AirDodge', 1.18, audio=[dict(sound(3520842405,.016667),kind='AnimNotify_ActionSound',socket='Root')])
        def pose(t, angle):
            return dict(time=t,bones={'R_Weapon_00':dict(q=[0,0,math.sin(math.radians(angle/2)),math.cos(math.radians(angle/2))])})
        result = dict(montages=[main,over,sweep,double,air],
            curve_rows=[[i/120,0,0,-800*(i/120)**2-5000*max(0,i/120-.60833)**2] for i in range(100)],
            poses={over['path']:[pose(1.8,0),pose(1.9,90),pose(2.,100)],
                   double['path']:[pose(2.4,0),pose(2.5,50),pose(2.6,160),pose(2.7,170)]})
        impact = dict(sound(1762027986,2.293,True),kind='AnimNotify_GroundImpact')
        return result, impact

    def test_phase_scope_and_reference_sound_layers(self):
        args = self.fixture()
        before = copy.deepcopy(args)
        rows = descending_points(*args)
        main, over = list(rows.values())
        self.assertEqual((len(main),len(over)),(1,8))
        self.assertEqual(main[0]['kind'],'AnimNotify_ActionSound')
        self.assertLess(main[0]['time'],.65)
        self.assertAlmostEqual(main[0]['time'],.608333,places=5)  # fixture onset, not the live curve's .616667
        self.assertEqual((over[0]['time'],over[0]['kind']),(0.,'AnimNotify_GroundImpact'))
        self.assertEqual(sum(r['kind']=='AnimNotify_GroundDust' for r in over),2)
        self.assertEqual(sum(r['kind']=='AnimNotify_SwingSound' for r in over),5)
        self.assertEqual(args,before)

    def test_no_reference_or_no_acceleration_is_not_guessed(self):
        snapshot, impact = self.fixture()
        snapshot['curve_rows'] = [[i/120,0,0,0] for i in range(100)]
        with self.assertRaises(ValueError):
            descending_points(snapshot,impact)
        snapshot, impact = self.fixture()
        snapshot['montages'][3]['audio'].clear()
        with self.assertRaises(ValueError):
            descending_points(snapshot,impact)

    def test_existing_audio_or_changed_damage_topology_requires_review(self):
        for change in ('audio','topology'):
            snapshot, impact = self.fixture()
            if change=='audio': snapshot['montages'][0]['audio']=[impact]
            else: snapshot['montages'][1]['existing_notifies'].pop()
            with self.assertRaises(ValueError):
                descending_points(snapshot,impact)


if __name__ == '__main__':
    unittest.main()
