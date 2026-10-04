"""Tests for full 200 → 201 → 203 curve-chain extraction and canonical curves.

Synthetic data only: the capture lives outside the repository, so the geometry,
the boundary rule, the local model and the tail contract are all pinned here
against values that can be checked by hand.
"""

import math
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import build_kinsect_piercer_full_curves as piercer
from build_kinsect_piercer_full_curves import (
    build_keys,
    chain_gap,
    coverage_at,
    find_chains,
    fit_quadratic,
    horizontal_basis,
    horizontal_residual_stats,
    is_complete_203,
    local_fit,
    phase_points,
    residual_stats,
    sample_keys,
    tail_parameters,
)


def sample_row(time_s, position, velocity=(0.0, 0.0, 0.0)):
    return {
        "capture_time_s": f"{time_s}",
        "world_x": f"{position[0]}",
        "world_y": f"{position[1]}",
        "world_z": f"{position[2]}",
        "velocity_x": f"{velocity[0]}",
        "velocity_y": f"{velocity[1]}",
        "velocity_z": f"{velocity[2]}",
        "cam_pitch_deg": "0",
    }


class BasisTests(unittest.TestCase):
    def test_basis_is_horizontal_and_right_handed(self):
        chunk = [
            sample_row(0.0, (0, 0, 0)),
            sample_row(0.01, (0, 0, 0), (0.0, -5.0, 3.0)),
            sample_row(0.02, (0, 0, 0), (0.0, -5.0, 3.0)),
            sample_row(0.03, (0, 0, 0), (0.0, -5.0, 3.0)),
            sample_row(0.04, (0, 0, 0), (0.0, -5.0, 3.0)),
            sample_row(0.05, (0, 0, 0), (0.0, -5.0, 3.0)),
        ]
        forward, right = horizontal_basis(chunk)
        # The dive points down; the basis must not inherit that tilt.
        self.assertAlmostEqual(forward[1], 0.0)
        self.assertAlmostEqual(forward[2], 1.0)
        self.assertEqual(right, (1.0, 0.0, 0.0))

    def test_pitch_does_not_change_the_horizontal_basis(self):
        def chunk_with_vertical(vertical):
            return [sample_row(0.0, (0, 0, 0))] + [
                sample_row(0.01 * index, (0, 0, 0), (4.0, vertical, 3.0))
                for index in range(1, 6)
            ]

        shallow = horizontal_basis(chunk_with_vertical(-1.0))
        steep = horizontal_basis(chunk_with_vertical(-40.0))
        self.assertEqual(shallow, steep)

    def test_vertical_launch_is_rejected_rather_than_guessed(self):
        chunk = [sample_row(0.0, (0, 0, 0))] + [
            sample_row(0.01 * index, (0, 0, 0), (0.0, -5.0, 0.0)) for index in range(1, 6)
        ]
        with self.assertRaises(ValueError):
            horizontal_basis(chunk)


class PhasePointTests(unittest.TestCase):
    def test_points_start_at_origin_and_use_world_up(self):
        chunk = [
            sample_row(10.0, (1.0, 2.0, 3.0)),
            sample_row(10.1, (1.0, 2.5, 3.0)),
            sample_row(10.2, (1.0, 3.0, 4.0)),
        ]
        points = phase_points(chunk, (1.0, 0.0, 0.0), (0.0, 0.0, -1.0))
        self.assertEqual(points[0], (0.0, 0.0, 0.0, 0.0))
        # up is world +Y in centimetres, independent of the basis's tilt
        self.assertAlmostEqual(points[1][3], 50.0)
        self.assertAlmostEqual(points[2][3], 100.0)
        # +Z world is the local right axis here
        self.assertAlmostEqual(points[2][2], -100.0)

    def test_drop_last_trims_only_the_contact_sample(self):
        chunk = [sample_row(0.0, (0, 0, 0)), sample_row(0.01, (0, 1, 0)), sample_row(0.02, (0, 9, 0))]
        self.assertEqual(len(phase_points(chunk, (1.0, 0.0, 0.0), (0.0, 0.0, -1.0), 1)), 2)

    def test_chain_gap_includes_motion_transition_boundaries(self):
        chunks = [
            [{"capture_time_s": "0.00"}, {"capture_time_s": "0.01"}],
            [{"capture_time_s": "0.03"}, {"capture_time_s": "0.04"}],
            [{"capture_time_s": "0.10"}, {"capture_time_s": "0.11"}],
        ]
        self.assertAlmostEqual(chain_gap(chunks), 0.06)


class ChainDiscoveryTests(unittest.TestCase):
    def test_finds_only_complete_contiguous_action_chains(self):
        runs = [
            ("189", [{}]),
            ("200", [{}, {}]),
            ("201", [{}, {}, {}]),
            ("203", [{}, {}]),
            ("1", [{}]),
            ("200", [{}, {}]),
            ("7", [{}]),
            ("201", [{}, {}, {}]),
            ("203", [{}, {}]),
        ]
        chains = find_chains(runs)
        self.assertEqual(len(chains), 1)
        self.assertEqual(chains[0][0], 2)
        self.assertEqual([len(chunk) for chunk in chains[0][1]], [2, 3, 2])

    def test_only_long_203_trials_are_treated_as_complete(self):
        self.assertTrue(is_complete_203({"phase_durations": [0.39, 0.30, 1.5668]}))
        self.assertFalse(is_complete_203({"phase_durations": [0.39, 0.30, 0.9085]}))


class LocalModelTests(unittest.TestCase):
    def test_quadratic_fit_recovers_known_coefficients(self):
        offsets = [index * 0.01 - 0.05 for index in range(11)]
        values = [3.0 + 2.0 * offset - 4.0 * offset ** 2 for offset in offsets]
        coefficients = fit_quadratic(offsets, values)
        self.assertAlmostEqual(coefficients[0], 3.0, places=6)
        self.assertAlmostEqual(coefficients[1], 2.0, places=6)
        self.assertAlmostEqual(coefficients[2], -4.0, places=6)

    def test_local_fit_returns_position_velocity_and_acceleration_of_one_model(self):
        pool = []
        for index in range(121):
            time_s = index / 120.0
            pool.append((time_s, 100.0 * time_s, 0.0, -5.0 * time_s ** 2))
        fitted = local_fit(pool, 0.5)
        # value at t: 100*0.5 = 50; d/dt = 100; d2/dt2 = -10
        self.assertAlmostEqual(fitted["forward"][0], 50.0, places=3)
        self.assertAlmostEqual(fitted["forward"][1], 100.0, places=3)
        self.assertAlmostEqual(fitted["forward"][2], 0.0, places=3)
        self.assertAlmostEqual(fitted["up"][1], -5.0, places=3)
        self.assertAlmostEqual(fitted["up"][2], -10.0, places=3)

    def test_robust_reweighting_ignores_a_single_wild_trial(self):
        pool = [(index / 120.0, 2.0 * index / 120.0, 0.0, 0.0) for index in range(61)]
        outlier_time = 0.25
        pool.append((outlier_time, 900.0, 0.0, 0.0))
        fitted = local_fit(pool, outlier_time)
        self.assertAlmostEqual(fitted["forward"][0], 0.5, places=2)

    def test_robust_reweighting_survives_a_thin_window(self):
        # only the outlier's neighbours are recorded: the high-breakdown
        # pre-screen still has to reject it
        pool = [(0.22 + 0.01 * index, 2.0 * (0.22 + 0.01 * index), 0.0, 0.0) for index in range(6)]
        pool.append((0.25, 900.0, 0.0, 0.0))
        fitted = local_fit(pool, 0.25)
        self.assertLess(abs(fitted["forward"][0] - 0.5), 5.0)


class KeyTests(unittest.TestCase):
    def test_first_key_is_exactly_the_phase_origin(self):
        pool = [(index / 120.0, 1.0 + 2.0 * index / 120.0, 0.0, 0.0) for index in range(40)]
        keys = build_keys(pool, 0.3)
        for axis in piercer.AXES:
            self.assertEqual(keys[0]["position"][axis], 0.0)

    def test_keys_are_strictly_increasing_and_cover_the_domain_end(self):
        pool = [(index / 120.0, 2.0 * index / 120.0, 0.0, 0.0) for index in range(40)]
        keys = build_keys(pool, 0.3)
        times = [key["time_s"] for key in keys]
        self.assertEqual(times, sorted(times))
        self.assertEqual(len(set(times)), len(times))
        # rounding up keeps the last sample inside the curve's domain
        self.assertGreaterEqual(times[-1], 0.3)

    def test_boundary_keys_take_their_velocity_from_the_key_slope(self):
        # a sharp bend right at t=0 makes the one-sided fit's derivative useless;
        # the key must then agree with the position keys the runtime interpolates
        pool = [(index / 120.0, math.sqrt(index) * 20.0, 0.0, 0.0) for index in range(40)]
        keys = build_keys(pool, 0.3, data_end=pool[-1][0])
        slope = (keys[1]["position"]["forward"] - keys[0]["position"]["forward"]) / (
            keys[1]["time_s"] - keys[0]["time_s"])
        self.assertAlmostEqual(keys[0]["velocity"]["forward"], slope, places=6)

    def test_interior_keys_keep_the_fitted_derivative(self):
        pool = [(index / 120.0, 100.0 * index / 120.0, 0.0, 0.0) for index in range(61)]
        keys = build_keys(pool, 0.5, data_end=pool[-1][0])
        self.assertAlmostEqual(keys[30]["velocity"]["forward"], 100.0, places=3)

    def test_sample_keys_interpolates_and_clamps(self):
        keys = [
            {"time_s": 0.0, "position": {"forward": 0.0, "right": 0.0, "up": 0.0},
             "velocity": {}, "acceleration": {}},
            {"time_s": 1.0, "position": {"forward": 10.0, "right": 0.0, "up": 0.0},
             "velocity": {}, "acceleration": {}},
        ]
        self.assertAlmostEqual(sample_keys(keys, 0.5)["forward"], 5.0)
        self.assertAlmostEqual(sample_keys(keys, -1.0)["forward"], 0.0)
        self.assertAlmostEqual(sample_keys(keys, 9.0)["forward"], 10.0)


class ResidualTests(unittest.TestCase):
    def test_samples_past_the_last_key_are_counted_not_scored(self):
        pool = [(0.0, 0.0, 0.0, 0.0), (0.5, 50.0, 0.0, 0.0), (0.9, 900.0, 0.0, 0.0)]
        keys = build_keys([(time, value, 0.0, 0.0) for time, value, _, _ in pool], 0.5)
        stats = residual_stats(pool, keys)
        # the 0.9 s sample is outside the domain: it must not become the max error
        self.assertEqual(stats["samples_beyond_domain"], 1)
        self.assertLess(stats["euclidean"]["max"], 5.0)

    def test_horizontal_residual_ignores_vertical_separation(self):
        # two trials on the same ground path, one recorded 4 m higher
        pool = [(index / 120.0, 100.0 * index / 120.0, 0.0, 0.0) for index in range(61)]
        pool += [(index / 120.0, 100.0 * index / 120.0, 0.0, 400.0) for index in range(61)]
        keys = build_keys(pool, 0.5)
        self.assertLess(horizontal_residual_stats(pool, keys)["max"], 1.0)
        self.assertGreater(residual_stats(pool, keys)["up"]["max"], 100.0)


class OutlierAndInterpolationTests(unittest.TestCase):
    def test_interpolate_points_spans_and_clamps(self):
        points = [(0.0, 0.0, 0.0, 0.0), (0.5, 50.0, 5.0, -5.0), (1.0, 100.0, 10.0, -10.0)]
        self.assertEqual(piercer.interpolate_points(points, -1.0), (0.0, 0.0, 0.0))
        self.assertEqual(piercer.interpolate_points(points, 2.0), (100.0, 10.0, -10.0))
        # 0.25 sits between the 0.0 and 0.5 samples
        self.assertAlmostEqual(piercer.interpolate_points(points, 0.25)[0], 25.0)

    def test_outlier_scan_ignores_samples_past_the_last_key(self):
        # a trial still moving after the curve ends must not be reported as an
        # outlier: interpolation clamps there and the clamp looks like a jump
        straight = [(index / 120.0, 100.0 * index / 120.0, 0.0, 0.0) for index in range(61)]
        keys = build_keys(straight, 0.5)
        beyond = list(straight) + [(0.51, 900.0, 0.0, 0.0)]
        cohort = [
            {"attempt_index": 1, "points": {"201": straight}},
            {"attempt_index": 2, "points": {"201": beyond}},
        ]
        flagged = piercer.phase_outliers(cohort, keys, "201", False)
        by_index = {entry["attempt_index"]: entry for entry in flagged}
        self.assertLess(by_index[2]["max_deviation_cm"], 1.0)
        self.assertEqual([entry["attempt_index"] for entry in flagged if entry["over_threshold"]], [])


class CoverageAndTailTests(unittest.TestCase):
    def test_coverage_counts_live_trials_not_raw_samples(self):
        # a trial whose phase runs to 0.5 s is still live at 0.45 s even though
        # its last recorded sample is at 0.4 s
        self.assertEqual(coverage_at([0.5, 0.5, 0.2], 0.45), 2)
        self.assertEqual(coverage_at([0.5, 0.5, 0.2], 0.5), 2)
        self.assertEqual(coverage_at([0.5, 0.5, 0.2], 0.6), 0)

    def test_tail_anchor_requires_coverage_and_a_fully_recorded_window(self):
        pool = [(index / 120.0, 100.0 * index / 120.0, 0.0, -400.0 * index / 120.0) for index in range(61)]
        keys = build_keys(pool, 0.5)
        coverage = [8] * len(keys)
        tail = tail_parameters(keys, coverage, data_end=0.5)
        self.assertTrue(tail["fit_window_fully_covered"])
        # the window is 30 ms each side, so the anchor cannot sit within 30 ms of the data end
        self.assertLessEqual(tail["time_s"] + piercer.FIT_HALF_WINDOW_S, 0.5 + 1e-9)

    def test_tail_anchor_skips_under_covered_keys(self):
        pool = [(index / 120.0, 100.0 * index / 120.0, 0.0, 0.0) for index in range(61)]
        keys = build_keys(pool, 0.5)
        coverage = [1 if index >= len(keys) - 6 else 8 for index in range(len(keys))]
        tail = tail_parameters(keys, coverage, data_end=0.9, min_coverage=4)
        self.assertLess(tail["time_s"], keys[-1]["time_s"])


if __name__ == "__main__":
    unittest.main()
