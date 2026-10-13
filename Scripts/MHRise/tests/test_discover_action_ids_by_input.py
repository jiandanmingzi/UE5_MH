"""Synthetic tests for controller-input to motion-ID discovery."""

from __future__ import annotations

import io
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from discover_action_ids_by_input import discover_stream, format_report


FIELDS = [
    "sample_index", "capture_time_s", "player_motion_old_id",
    "input_gamepad_button_RTrigger_down", "input_gamepad_button_RTrigger_pressed",
    "input_gamepad_button_RTrigger_released", "input_gamepad_button_Y_down",
    "input_gamepad_button_Y_pressed", "input_gamepad_button_Y_released",
]


def csv_text(rows: list[list[str]]) -> str:
    return ",".join(FIELDS) + "\n" + "\n".join(",".join(row) for row in rows) + "\n"


class DiscoverActionIdsByInputTests(unittest.TestCase):
    def test_correlates_edges_and_held_input_with_contiguous_motion_ids(self):
        text = csv_text([
            ["1", "0.000", "10", "0", "0", "0", "0", "0", "0"],
            ["2", "0.016", "10", "1", "1", "0", "1", "1", "0"],
            ["3", "0.032", "42", "1", "0", "0", "1", "0", "0"],
            ["4", "0.048", "42", "0", "0", "1", "0", "0", "1"],
            ["5", "0.064", "10", "0", "0", "0", "0", "0", "0"],
        ])
        report = discover_stream(io.StringIO(text))
        self.assertEqual(report["motion_id_row_counts"], {10: 3, 42: 2})
        self.assertEqual(report["button_edge_counts"], {
            "RTrigger:pressed": 1,
            "RTrigger:released": 1,
            "Y:pressed": 1,
            "Y:released": 1,
        })
        transitions = report["motion_transitions"]
        self.assertEqual(len(transitions), 2)
        self.assertEqual(transitions[0]["nearby_input"]["edge_counts"], {
            "RTrigger:pressed": 1,
            "RTrigger:released": 1,
            "Y:pressed": 1,
            "Y:released": 1,
        })
        self.assertEqual(transitions[0]["nearby_input"]["held_row_counts"], {
            "RTrigger": 1,
            "Y": 1,
        })
        episode = report["episodes"][1]
        self.assertEqual(episode["motion_id"], 42)
        self.assertEqual(episode["transition_to_id"], 10)
        self.assertEqual(episode["button_edge_counts"], {
            "RTrigger:released": 1,
            "Y:released": 1,
        })
        self.assertAlmostEqual(episode["sampled_span_s"], 0.016)
        self.assertAlmostEqual(episode["time_until_transition_s"], 0.032)
        self.assertIn("RTrigger:pressed=1", format_report(Path("capture.csv"), report))

    def test_rejects_capture_without_controller_edge_fields(self):
        with self.assertRaisesRegex(ValueError, "controller button transition"):
            discover_stream(io.StringIO(
                "sample_index,capture_time_s,player_motion_old_id\n1,0.0,1\n"
            ))


if __name__ == "__main__":
    unittest.main()
