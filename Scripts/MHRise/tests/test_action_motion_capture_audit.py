"""Synthetic tests for MHRise action-ID episode and frame-field auditing."""

from __future__ import annotations

import io
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from audit_action_motion_captures import audit_stream, parse_action_id


def row(index: int, time_s: float, action_id: str, frame: str = "") -> dict[str, str]:
    result = {
        "sample_index": str(index),
        "capture_time_s": f"{time_s:.3f}",
        "player_motion_old_id": action_id,
        "world_x": f"{index * 0.1:.1f}",
        "world_y": "0.0",
        "world_z": "0.0",
        "velocity_x": "1.0",
        "velocity_y": "0.0",
        "velocity_z": "0.0",
        "cam_pitch_deg": "10.0",
        "cam_forward_x": "0.0",
        "cam_forward_y": "0.0",
        "cam_forward_z": "1.0",
        "input_gamepad_axis_l_x": "0.25",
        "input_gamepad_axis_l_y": "1.0",
    }
    for layer in range(5):
        result[f"motion_l{layer}_id"] = ""
        result[f"motion_l{layer}_frame"] = ""
    result["motion_l0_frame"] = frame
    return result


def csv_text(rows: list[dict[str, str]]) -> str:
    columns = list(rows[0])
    return ",".join(columns) + "\n" + "\n".join(
        ",".join(row[column] for column in columns) for row in rows
    ) + "\n"


class ActionMotionAuditTests(unittest.TestCase):
    def test_parses_decimal_id_without_accepting_fraction(self):
        self.assertEqual(parse_action_id("199.00000000"), 199)
        self.assertIsNone(parse_action_id("199.5"))
        self.assertIsNone(parse_action_id(""))

    def test_splits_non_contiguous_action_episodes_and_counts_frames(self):
        rows = [
            row(1, 0.0, "199.00000000", "12"),
            row(2, 0.016, "199.00000000", "13"),
            row(3, 0.032, "206.00000000"),
            row(4, 0.048, "199.00000000"),
        ]
        report = audit_stream(io.StringIO(csv_text(rows)), (199, 206))
        self.assertEqual(report["rows"], 4)
        self.assertEqual(report["target_id_rows"], {199: 3, 206: 1})
        self.assertEqual([item["samples"] for item in report["episodes"]], [2, 1, 1])
        self.assertEqual(report["episodes"][0]["frame_columns_with_data"], {"motion_l0_frame": 2})
        self.assertEqual(report["episodes"][1]["frame_columns_with_data"], {})
        self.assertEqual(report["episodes"][2]["frame_columns_with_data"], {})
        self.assertEqual(report["episodes"][0]["sampled_span_s"], 0.016)
        self.assertAlmostEqual(report["episodes"][0]["interval_until_transition_s"], 0.032)
        self.assertEqual(report["episodes"][0]["transition_to_id"], 206)
        self.assertAlmostEqual(report["episodes"][0]["displacement_cm"]["x"], 10.0)
        self.assertAlmostEqual(report["episodes"][0]["velocity_mean_cm_s"]["x"], 100.0)
        self.assertAlmostEqual(report["episodes"][0]["speed_mean_cm_s"], 100.0)
        self.assertAlmostEqual(report["episodes"][0]["camera_start_alignment_error_deg"], 90.0)
        self.assertAlmostEqual(report["episodes"][0]["camera_pitch_mean_deg"], 10.0)

    def test_missing_action_id_column_is_an_explicit_error(self):
        with self.assertRaisesRegex(ValueError, "player_motion_old_id"):
            audit_stream(io.StringIO("sample_index,capture_time_s\n1,0.0\n"), (199,))

    def test_reports_post_entry_velocity_fit_without_transition_sample(self):
        rows = [row(i + 1, i * 0.1, "199") for i in range(4)]
        for item in rows:
            t = float(item["capture_time_s"])
            item["velocity_y"] = f"{1.0 + 3.0 * t:.3f}"
        report = audit_stream(io.StringIO(csv_text(rows)), (199,))
        episode = report["episodes"][0]
        fit = episode["velocity_linear_fit_after_entry"]["y"]
        self.assertEqual(episode["velocity_linear_fit_after_entry"]["sample_count"], 3)
        self.assertAlmostEqual(fit["velocity_at_episode_start_cm_s"], 100.0, places=4)
        self.assertAlmostEqual(fit["acceleration_cm_s2"], 300.0, places=4)
        self.assertAlmostEqual(fit["r_squared"], 1.0, places=8)


if __name__ == "__main__":
    unittest.main()
