"""Offline extraction tests; no Unreal Editor or live game required."""

import csv
import io
import math
import sys
import unittest
from unittest.mock import patch
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from analyze_kinsect_slash_outcomes import (
    fit_polynomial, load_rows, local_curve, preserved_supplement,
    preserved_tail_supplement, summarize,
)
from analyze_kinsect_slash_outcomes import SUPPLEMENT_BEGIN, SUPPLEMENT_END
from analyze_kinsect_slash_outcomes import MHW_TURN_TRUTH_BEGIN, MHW_TURN_TRUTH_END


class OutcomeTests(unittest.TestCase):
    def test_capture_without_hit_does_not_invent_dance_parameters(self):
        result = summarize([{"source": "no_hits.csv", "sample_count": 1, "runs": []}])
        self.assertEqual(result["dance_runs"], 0)
        self.assertIsNone(result["first_positive_height_s"])
        self.assertIsNone(result["launch_model"]["launch_vertical_speed_cm_s"])

    def test_position_fit_recovers_delayed_launch(self):
        points = [(i / 120, 1080 * (i / 120 - 0.35) - 900 * (i / 120 - 0.35) ** 2)
                  for i in range(54, 184)]
        fit = fit_polynomial(points)
        a, b, c = fit["coefficients"]
        roots = sorted(((-b + math.sqrt(b * b - 4 * a * c)) / (2 * c),
                        (-b - math.sqrt(b * b - 4 * a * c)) / (2 * c)))
        self.assertAlmostEqual(roots[0], 0.35, places=9)
        self.assertAlmostEqual(b + 2 * c * roots[0], 1080, places=7)
        self.assertAlmostEqual(fit["acceleration_cm_s2"], -1800, places=7)
        self.assertAlmostEqual(fit["r2"], 1, places=10)

    def test_local_basis_uses_world_y_up_not_bone_axes(self):
        anchor = dict(world_x="10", world_y="20", world_z="30", capture_time_s="5",
                      forward_x="0", forward_z="2", player_motion_old_id="189")
        next_row = {**anchor, "world_z": "27", "world_y": "23.24", "capture_time_s": "5.6",
                    "velocity_z": "-3", "velocity_y": "10.8"}
        result = local_curve([anchor, next_row])[1]
        self.assertAlmostEqual(result["forward_cm"], -300)
        self.assertAlmostEqual(result["up_cm"], 324)
        self.assertAlmostEqual(result["up_speed_cm_s"], 1080)

    def test_sparse_button_edges_do_not_shift_transform_or_layer_tail(self):
        fields = [f"base{i}" for i in range(42)] + ["player_motion_old_id"]
        fields += [f"edge{i}" for i in range(74)] + ["motion_l0_bank_id"]
        fields += [f"layer{i}" for i in range(34)]
        prefix = [str(i) for i in range(42)] + ["189"]
        tail = [str(1000 + i) for i in range(35)]
        stream = io.StringIO()
        csv.writer(stream).writerows([fields, prefix + ["1"] + tail, prefix + ["0"] * 74 + tail])
        stream.seek(0)
        with patch.object(Path, "open", return_value=stream):
            rows, _, schema = load_rows(Path("capture.csv"))
        self.assertEqual(rows[0]["player_motion_old_id"], "189")
        self.assertEqual(rows[0]["motion_l0_bank_id"], "1000")
        self.assertEqual(rows[0]["layer33"], "1034")
        self.assertEqual(rows[0]["edge0"], "")
        self.assertEqual(rows[1]["edge0"], "0")
        self.assertEqual(schema["short_rows"], 1)
        self.assertFalse(schema["button_edge_cells_recoverable"])

    def test_legacy_generator_retains_only_scoped_supplement(self):
        block = SUPPLEMENT_BEGIN + "\nmeasured evidence\n" + SUPPLEMENT_END
        with patch.object(Path, "is_file", return_value=True), patch.object(
                Path, "read_text", return_value="old history\n" + block + "\nother history"):
            path = Path("truth.md")
            self.assertEqual(preserved_supplement(path), block)
        with patch.object(Path, "is_file", return_value=False):
            self.assertEqual(preserved_supplement(Path("absent.md")), "")

    def test_truth_generator_retains_mhw_tail_block(self):
        block = MHW_TURN_TRUTH_BEGIN + "\nmeasured MHW evidence\n" + MHW_TURN_TRUTH_END
        with patch.object(Path, "is_file", return_value=True), patch.object(
                Path, "read_text", return_value="Rise history\n" + block + "\nignored tail"):
            self.assertEqual(preserved_tail_supplement(Path("truth.md")), block)
        with patch.object(Path, "is_file", return_value=False):
            self.assertEqual(preserved_tail_supplement(Path("absent.md")), "")


if __name__ == "__main__":
    unittest.main()
