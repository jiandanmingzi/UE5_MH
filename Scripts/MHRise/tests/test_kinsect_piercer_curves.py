"""Pure-function tests for the Enhanced Kinsect Piercer curve extractor."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from build_kinsect_piercer_curves import basis_from_launch, classify


class PiercerCurveTests(unittest.TestCase):
    def test_local_basis_preserves_game_y_up_and_is_orthonormal(self):
        forward, right, up = basis_from_launch((0.0, 0.0, 1.0))
        self.assertEqual(forward, (0.0, 0.0, 1.0))
        self.assertEqual(right, (1.0, 0.0, 0.0))
        self.assertEqual(up, (0.0, 1.0, 0.0))

    def test_large_cross_track_deviation_is_flagged_not_discarded_silently(self):
        quality, include, reason = classify({
            "max_cross_track_cm": 57.7,
            "max_cross_track_speed_cm_s": 1831.0,
            "max_sample_gap_s": 0.009,
        })
        self.assertEqual(quality, "suspected_external_push")
        self.assertFalse(include)
        self.assertIn("cross-track", reason)

    def test_normal_clean_sample_is_retained(self):
        quality, include, reason = classify({
            "max_cross_track_cm": 6.6,
            "max_cross_track_speed_cm_s": 812.0,
            "max_sample_gap_s": 0.009,
        })
        self.assertEqual(quality, "clean_candidate")
        self.assertTrue(include)
        self.assertIn("within", reason)


if __name__ == "__main__":
    unittest.main()
