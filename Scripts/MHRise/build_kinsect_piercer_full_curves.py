#!/usr/bin/env python3
"""Rebuild the Enhanced Kinsect Piercer (强化操虫穿刺) 200/201/203 chain curves.

Two products come out of one pass over the capture:

1. A per-attempt export of every complete ``200 → 201 → 203`` chain, so the raw
   seams and the anomalous trials stay reviewable.
2. The canonical curves the runtime will consume: one robust position curve per
   phase on a real-time 120 Hz grid, with sample coverage, quantile bands, the
   velocity taken from the same smooth model, and the 201 tail parameters.

Everything is expressed in a **horizontal** launch basis taken from 201
(plan §5.2): ``forward`` is the horizontal part of the 201 launch velocity,
``right`` is ``(forward.z, 0, -forward.x)`` and ``up`` is world ``+Y``. Camera
pitch therefore never selects, scales or rotates a curve — it is only an audit
column. The previous export used a tilted basis built from the full 3D launch
velocity; its ``right`` axis was identical to this one, but its ``up`` axis was
not world vertical, so it could not be read as height.

The canonical model is a local quadratic regression (Savitzky-Golay style) over
the pooled clean samples. Position, velocity and acceleration all come from that
one fit, and the reported residuals are measured against the 120 Hz keys the
game will interpolate — not against a continuous idealisation.

This writes CSV/JSON analysis sources for the M3 asset generator. It does not
create Unreal assets and it does not decide the production displacement owner.
"""

from __future__ import annotations

import argparse
import bisect
import csv
import hashlib
import json
import math
import statistics
from collections import defaultdict
from pathlib import Path

import build_kinsect_piercer_curves as curve_core
from recorder_path import recorder_dir


PROJECT = Path(__file__).resolve().parents[2]
OUTPUT_DIR = PROJECT / "Saved" / "_mhr_curves"
CHAIN_IDS = ("200", "201", "203")

# --- canonical curve contract (plan §5.3) -----------------------------------
GRID_HZ = 120
P95_GATE_CM = 5.0
# Window of the local quadratic. Swept 15/20/25/30/40 ms over all three phases:
# 20 ms wins on every one of them. 30 ms leaves the 203 opening 1.42 cm outside
# the cohort's own 5-95 % band and 9 cells out of band; 15 ms is too short to
# hold the 200 launch kick and the 201 tail (max residual 3.17 cm and 49.65 cm).
# 20 ms keeps the worst out-of-band cell at 0.17 cm.
FIT_HALF_WINDOW_S = 0.020
FIT_MIN_POINTS = 12
ROBUST_ITERATIONS = 2
ROBUST_MAD_MULTIPLIER = 4.0
# Pre-screen cut in units of the window's own median absolute deviation.
PRESCREEN_MAD_MULTIPLIER = 8.0
# A 201 key backed by fewer than this many trials is reported but is not used as
# the tail anchor: the end of the dive is recorded by very few attempts.
TAIL_MIN_COVERAGE = 4
# A 203 trial whose horizontal path leaves the cohort by more than this is
# reported as an outlier. Nothing is dropped from the export: the flag is
# computed after the fit and only annotates the audit.
OUTLIER_CM = 10.0

# The last sample of a 201 run is the frame the game has already resolved onto
# the ground, so its reading is contact, not flight. Measured on this capture,
# the 201 gate goes 2.398 cm -> 2.075 cm P95 and, more tellingly, 34.371 cm ->
# 3.535 cm max when that one sample is dropped: the P95 hides it, the max does
# not. Trimming two or three buys ~0.03 cm and shortens the usable domain by a
# frame each (0.3667 -> 0.3583 -> 0.3500 s), so exactly one it is. 200 ends in a
# pure phase transition and 203 runs on the ground, so neither is trimmed.
CONTACT_TRIM_SAMPLES = {"200": 0, "201": 1, "203": 0}

CURVE_FIELDS = [
    "capture_id", "attempt_index", "sequence_frame_index", "phase_order",
    "motion_id", "phase_frame_index", "capture_time_s", "sequence_elapsed_s",
    "sequence_elapsed_norm", "phase_elapsed_s", "phase_elapsed_norm",
    "sequence_duration_s", "phase_duration_s", "phase_duration_frames",
    "previous_motion_id", "next_motion_id", "camera_pitch_deg",
    "launch_x", "launch_y", "launch_z",
    "position_x_cm", "position_y_cm", "position_z_cm",
    "forward_cm", "right_cm", "up_cm", "world_up_cm",
    "speed_cm_s", "forward_speed_cm_s", "right_speed_cm_s", "up_speed_cm_s",
    "quality", "include_in_clean_curve",
]

AUDIT_FIELDS = [
    "capture_id", "attempt_index", "motion_run_index", "motion_chain",
    "next_motion_after_203", "phase_200_frames", "phase_201_frames",
    "phase_203_frames", "phase_200_duration_s", "phase_201_duration_s",
    "phase_203_duration_s", "phase_200_boundary_s", "phase_201_boundary_s",
    "phase_203_boundary_s", "sequence_duration_s", "camera_pitch_201_median_deg",
    "max_cross_track_cm_201", "max_cross_track_speed_cm_s_201",
    "max_yaw_step_deg_201", "max_sample_gap_s", "quality",
    "include_in_clean_curve", "reason",
]

CANONICAL_FIELDS = [
    "phase", "sample_index", "time_s", "coverage_count", "coverage_fraction",
    "forward_cm", "right_cm", "up_cm",
    "forward_p05_cm", "forward_p50_cm", "forward_p95_cm",
    "right_p05_cm", "right_p50_cm", "right_p95_cm",
    "up_p05_cm", "up_p50_cm", "up_p95_cm",
    "up_measured_p50_cm", "up_measured_p95_cm",
    "forward_speed_cm_s", "right_speed_cm_s", "up_speed_cm_s", "speed_cm_s",
]

AXES = ("forward", "right", "up")


# ---------------------------------------------------------------------------
# geometry
# ---------------------------------------------------------------------------

def horizontal_basis(chunk: list[dict[str, str]]):
    """Plan §5.2: horizontal launch basis from the 201 start velocity.

    ``forward`` keeps only the horizontal part of the launch direction, so the
    aim pitch cannot tilt the curve frame; ``up`` is world ``+Y``. The returned
    ``right`` axis is numerically identical to the tilted basis's right axis,
    which is why the existing cross-track screen keeps its meaning.
    """
    source = chunk[1:6] or chunk[:1]
    velocity = tuple(
        statistics.mean(curve_core.vector(row, "velocity")[axis] for row in source)
        for axis in range(3)
    )
    horizontal = math.hypot(velocity[0], velocity[2])
    if horizontal < 1e-6:
        raise ValueError("201 launch direction is too close to vertical for a yaw basis")
    forward = (velocity[0] / horizontal, 0.0, velocity[2] / horizontal)
    right = (forward[2], 0.0, -forward[0])
    return forward, right


def phase_points(chunk, forward, right, drop_last: int = 0):
    """Local ``(t, forward_cm, right_cm, up_cm)`` for one phase of one attempt.

    ``up_cm`` is the world ``+Y`` displacement (plan §5.2 ``Local.Z = Delta.y``),
    not a projection onto a tilted axis. Each phase is relative to its own first
    sample so every curve starts at ``(0, 0, 0)``.
    """
    usable = chunk[:len(chunk) - drop_last] if drop_last and len(chunk) - drop_last >= 2 else chunk
    origin = curve_core.vector(chunk[0], "world")
    start = curve_core.number(chunk[0], "capture_time_s")
    points = []
    for row in usable:
        position = curve_core.vector(row, "world")
        delta_cm = tuple((position[axis] - origin[axis]) * 100.0 for axis in range(3))
        points.append((
            curve_core.number(row, "capture_time_s") - start,
            curve_core.dot(delta_cm, forward),
            curve_core.dot(delta_cm, right),
            delta_cm[1],
        ))
    return points


def phase_duration(chunk) -> float:
    return curve_core.number(chunk[-1], "capture_time_s") - curve_core.number(chunk[0], "capture_time_s")


def boundary_duration(previous, following) -> float:
    """Plan §2.1: a phase lasts until the first sample of the next phase.

    Using the next phase's first sample time instead of this phase's last sample
    time closes the one-sample gap at every boundary.
    """
    return curve_core.number(following[0], "capture_time_s") - curve_core.number(previous[0], "capture_time_s")


# ---------------------------------------------------------------------------
# robust local model
# ---------------------------------------------------------------------------

def solve_normal_equations(matrix, vector):
    size = len(matrix[0])
    a = [row[:] for row in matrix]
    b = vector[:]
    for column in range(size):
        pivot = max(range(column, size), key=lambda row: abs(a[row][column]))
        a[column], a[pivot] = a[pivot], a[column]
        b[column], b[pivot] = b[pivot], b[column]
        if abs(a[column][column]) < 1e-14:
            return None
        for row in range(column + 1, size):
            factor = a[row][column] / a[column][column]
            for index in range(column, size):
                a[row][index] -= factor * a[column][index]
            b[row] -= factor * b[column]
    solution = [0.0] * size
    for index in range(size - 1, -1, -1):
        total = b[index] - sum(a[index][other] * solution[other] for other in range(index + 1, size))
        solution[index] = total / a[index][index]
    return solution


def fit_quadratic(offsets, values, weights=None):
    """Least-squares ``c0 + c1*dt + c2*dt**2`` around ``dt = 0``."""
    weights = weights or [1.0] * len(offsets)
    matrix = [
        [sum(w * o ** i * o ** j for o, w in zip(offsets, weights)) for j in range(3)]
        for i in range(3)
    ]
    vector = [sum(w * v * o ** i for o, v, w in zip(offsets, values, weights)) for i in range(3)]
    return solve_normal_equations(matrix, vector)


def fit_linear(offsets, values, weights=None):
    """Least-squares ``c0 + c1*dt``; the fallback when a quadratic is singular."""
    weights = weights or [1.0] * len(offsets)
    matrix = [[sum(w * o ** i * o ** j for o, w in zip(offsets, weights)) for j in range(2)]
              for i in range(2)]
    vector = [sum(w * v * o ** i for o, v, w in zip(offsets, values, weights)) for i in range(2)]
    solution = solve_normal_equations(matrix, vector)
    return None if solution is None else [solution[0], solution[1], 0.0]


def tricube_weight(offset: float, half_window: float) -> float:
    """LOESS-style distance kernel: 1 at the centre, 0 at the window edge."""
    ratio = abs(offset) / half_window
    if ratio >= 1.0:
        return 0.0
    return (1.0 - ratio ** 3) ** 3


def mad_scale(values) -> float:
    if len(values) < 4:
        return 0.0
    centre = statistics.median(values)
    return 1.4826 * statistics.median(abs(value - centre) for value in values)


def local_fit(pool, time_s: float, half_window: float = FIT_HALF_WINDOW_S,
              min_points: int = FIT_MIN_POINTS):
    """Per-axis local quadratic about ``time_s``; returns ``(position, velocity, acceleration)``.

    The fit is re-weighted with a tricube kernel and a MAD-based cut, so a single
    deviating trial cannot drag the canonical curve (plan §5.3 asks for a robust
    position curve). Position is the fitted value at ``time_s``; velocity and
    acceleration are the same polynomial's first and second derivatives, so the
    curve and its speed can never disagree.

    The kernel matters most where the motion bends hardest. A flat weight over a
    +/-30 ms window fits the 203 landing impact with a quadratic that undershoots
    the opening: the second key came out 3.92 cm below the whole cohort's 5-95 %
    band and 9 of 32 trials missed it by more than 5 cm. Distance weighting takes
    that to 0.18 cm and drops the per-trial-max P95 from 5.75 cm to 1.82 cm, while
    leaving 201 (whose window is rarely dominated by one bend) unchanged.

    When a window holds fewer than ``min_points`` samples — thin coverage at the
    end of 201 — the nearest samples are taken regardless of distance, so the
    curve keeps a defined value there. Those keys are the ones ``coverage_count``
    and the tail rules exist to expose.
    """
    near = [point for point in pool if abs(point[0] - time_s) <= half_window]
    if len(near) < min_points:
        ordered = sorted(pool, key=lambda point: abs(point[0] - time_s))
        near = ordered[:min_points]
    if len(near) < 3:
        raise ValueError(f"not enough samples to fit around t={time_s}")

    result = {}
    for index, axis in enumerate(AXES, start=1):
        values = [point[index] for point in near]
        offsets = [point[0] - time_s for point in near]
        # Widen the kernel to the fallback set when the window itself is thin,
        # otherwise every weight would be zero and the fit would degenerate to
        # the mean of samples far outside the window.
        radius = max(half_window, max(abs(offset) for offset in offsets))
        weights = [tricube_weight(offset, radius) for offset in offsets]
        # High-breakdown pre-screen: a plain first fit is dragged by a gross
        # outlier, which leaves MAD too large in the re-weighting below to cut
        # it. Median/MAD of the values tolerates a 50 % corrupt sample, so a
        # point far outside the window's own spread goes before the first fit.
        scale = mad_scale(values)
        if scale > 0.0:
            centre = statistics.median(values)
            cut = PRESCREEN_MAD_MULTIPLIER * scale
            if any(abs(value - centre) > cut for value in values):
                screened = [0.0 if abs(value - centre) > cut else weight
                            for value, weight in zip(values, weights)]
                if sum(1 for weight in screened if weight > 0.0) >= 3:
                    weights = screened
        coefficients = None
        for _ in range(ROBUST_ITERATIONS + 1):
            coefficients = fit_quadratic(offsets, values, weights)
            if coefficients is None:
                coefficients = fit_linear(offsets, values, weights)
                break
            residual = [value - (coefficients[0] + coefficients[1] * offset + coefficients[2] * offset ** 2)
                        for value, offset in zip(values, offsets)]
            scale = mad_scale(residual)
            if scale <= 0.0:
                break
            weights = [weight * (1.0 if abs(r) <= ROBUST_MAD_MULTIPLIER * scale
                                 else ROBUST_MAD_MULTIPLIER * scale / abs(r))
                       for r, weight in zip(residual, weights)]
        if coefficients is None:
            # Degenerate window (e.g. fewer distinct times than coefficients):
            # fall back to the mean rather than inventing a value at the origin.
            coefficients = [statistics.mean(values), 0.0, 0.0]
        result[axis] = (coefficients[0], coefficients[1], 2.0 * coefficients[2])
    return result


def build_keys(pool, domain_end: float, data_end: float | None = None,
               half_window: float = FIT_HALF_WINDOW_S, live_ends=None):
    """Position/velocity keys on the 120 Hz grid covering ``[0, domain_end]``.

    The grid is rounded **up** so the last key is at or past ``domain_end``:
    rounding down would leave the final samples past the last key, where the
    runtime's clamped interpolation turns a sub-centimetre fit error into a large
    apparent one.

    Position always comes from the local fit. Velocity does too, **except at keys
    whose fit window is not fully covered by recorded samples** — at the two ends
    the polynomial has data on one side only, so its derivative there is not
    identified by the data. On this capture that showed up as a 203 opening
    velocity of 3007 cm/s against an actually-recorded 2412 cm/s (25 % high, and
    that key is what §6.3 sets the ground slide from). Those keys take the slope
    of the position keys instead, which is exactly the velocity the runtime's
    linear interpolation will have.
    """
    key_count = int(math.ceil(domain_end * GRID_HZ - 1e-9))
    keys = []
    for index in range(key_count + 1):
        time_s = index / GRID_HZ
        fitted = local_fit(pool, time_s, half_window)
        fully_covered = (
            time_s - half_window >= -1e-9
            and (data_end is None or time_s + half_window <= data_end + 1e-9)
        )
        keys.append({
            "time_s": time_s,
            "position": {axis: fitted[axis][0] for axis in AXES},
            "velocity": {axis: fitted[axis][1] for axis in AXES} if fully_covered else None,
            "acceleration": {axis: fitted[axis][2] for axis in AXES},
        })
    # Every phase curve starts at its own first sample by construction, so the
    # first key is the exact origin (§5.2) rather than a fit that can miss it by
    # a fraction of a centimetre. Acceleration stays from the fit.
    for axis in AXES:
        keys[0]["position"][axis] = 0.0

    # Drop trailing keys no trial still backs: keeping one would be pure
    # extrapolation dressed up as data. This happens **before** the boundary
    # velocities are filled below, otherwise a kept key's velocity would be a
    # difference against a key that is no longer in the file.
    if live_ends is not None:
        while len(keys) > 1 and coverage_at(live_ends, keys[-1]["time_s"]) < 1:
            keys.pop()

    for index, key in enumerate(keys):
        if key["velocity"] is not None:
            continue
        low = max(0, index - 1)
        high = min(len(keys) - 1, index + 1)
        span = keys[high]["time_s"] - keys[low]["time_s"]
        key["velocity"] = {
            axis: (keys[high]["position"][axis] - keys[low]["position"][axis]) / span
            for axis in AXES
        } if span > 0 else {axis: 0.0 for axis in AXES}
    return keys


def sample_keys(keys, time_s: float):
    """Linear interpolation between keys — what the game will actually do.

    The lookup is searched rather than computed from ``time_s * GRID_HZ`` so the
    function stays correct if a caller ever builds keys on another grid.
    """
    if time_s <= keys[0]["time_s"]:
        return keys[0]["position"]
    if time_s >= keys[-1]["time_s"]:
        return keys[-1]["position"]
    times = [key["time_s"] for key in keys]
    high = bisect.bisect_left(times, time_s)
    low = high - 1
    first, second = keys[low], keys[high]
    span = second["time_s"] - first["time_s"]
    fraction = (time_s - first["time_s"]) / span if span > 0 else 0.0
    return {
        axis: first["position"][axis] + (second["position"][axis] - first["position"][axis]) * fraction
        for axis in AXES
    }


# ---------------------------------------------------------------------------
# statistics helpers
# ---------------------------------------------------------------------------

def percentile(values, quantile: float) -> float:
    ordered = sorted(values)
    if not ordered:
        return 0.0
    index = min(len(ordered) - 1, max(0, math.ceil(quantile * len(ordered)) - 1))
    return ordered[index]


def describe(values) -> dict:
    if not values:
        return {"count": 0, "p50": 0.0, "p95": 0.0, "max": 0.0}
    return {
        "count": len(values),
        "p50": round(percentile(values, 0.50), 3),
        "p95": round(percentile(values, 0.95), 3),
        "max": round(max(values), 3),
    }


def residual_stats(pool, keys) -> dict:
    """Sample deviation from the keyed curve, over the canonical domain only.

    Samples past the last key are counted separately: they are outside what the
    curve claims to cover, so folding them in would report a coverage gap as a
    fit error.
    """
    last_key_time = keys[-1]["time_s"]
    per_axis = {axis: [] for axis in AXES}
    euclidean = []
    beyond = 0
    for point in pool:
        if point[0] > last_key_time + 1e-9:
            beyond += 1
            continue
        model = sample_keys(keys, point[0])
        error = [point[index] - model[axis] for index, axis in enumerate(AXES, start=1)]
        for index, axis in enumerate(AXES):
            per_axis[axis].append(abs(error[index]))
        euclidean.append(math.sqrt(sum(value * value for value in error)))
    stats = {axis: describe(per_axis[axis]) for axis in AXES}
    stats["euclidean"] = describe(euclidean)
    stats["samples_beyond_domain"] = beyond
    return stats


def horizontal_residual_stats(pool, keys) -> dict:
    """P05/P50/P95/max of the in-plane ``(forward, right)`` deviation.

    This is the statistic that matters for 203: its runtime vertical is a
    declared zero, so the recorded capsule height above the landing surface is
    terrain tracking, not curve error.
    """
    last_key_time = keys[-1]["time_s"]
    distances = []
    for point in pool:
        if point[0] > last_key_time + 1e-9:
            continue
        model = sample_keys(keys, point[0])
        distances.append(math.hypot(point[1] - model["forward"], point[2] - model["right"]))
    return describe(distances)


def gate_dispersion(cohort, keys, motion: str, horizontal_only: bool, outliers) -> dict:
    """Full dispersion of the cohort against the keyed curve, not just the P95.

    Plan §5.3 asks for the dispersion to be *reported* when it exceeds the fit
    threshold rather than for a pass to be claimed. A P95 alone would hide the
    tail: on this capture 203's horizontal P95 is 1.59 cm while its RMSE (6.3 cm)
    and P99 (38.5 cm) are carried by one flagged trial, and the number of trials
    whose worst sample misses by more than the limit is the honest companion.
    """
    last_key_time = keys[-1]["time_s"]
    signed = []
    for record in cohort:
        for point in record["points"][motion]:
            if point[0] > last_key_time + 1e-9:
                continue
            model = sample_keys(keys, point[0])
            error = [point[index] - model[axis] for index, axis in enumerate(AXES, start=1)]
            signed.append(
                math.hypot(error[0], error[1]) if horizontal_only
                else math.sqrt(sum(value * value for value in error))
            )
    per_trial_max = [entry["max_deviation_cm"] for entry in outliers]
    return {
        "statistic": ("horizontal (forward/right) sample deviation, linearly interpolated between "
                      "120 Hz keys" if horizontal_only else
                      "3D sample deviation, linearly interpolated between 120 Hz keys"),
        "limit_cm": P95_GATE_CM,
        "p95_cm": round(percentile(signed, 0.95), 3),
        "p99_cm": round(percentile(signed, 0.99), 3),
        "rmse_cm": round(math.sqrt(sum(value * value for value in signed) / len(signed)), 3) if signed else 0.0,
        "max_cm": round(max(signed), 3) if signed else 0.0,
        "samples": len(signed),
        "samples_over_limit": sum(1 for value in signed if value > P95_GATE_CM),
        "per_trial_max_p95_cm": round(percentile(per_trial_max, 0.95), 3) if per_trial_max else 0.0,
        "trials_over_limit": sum(1 for value in per_trial_max if value > P95_GATE_CM),
        "trials": len(per_trial_max),
        "p95_pass": percentile(signed, 0.95) <= P95_GATE_CM if signed else True,
    }


def coverage_at(live_ends, time_s: float) -> int:
    """How many cohort trials still have this phase running at ``time_s``.

    Counting raw samples within a tolerance would understate the tail: a trial
    whose phase runs to 0.39 s but whose last sample sits at 0.38 s is still
    live at 0.385 s. ``live_ends`` is per trial, so this answers "how many
    trials back this key".
    """
    return sum(1 for end in live_ends if end + 1e-9 >= time_s)


def interpolate_points(points, time_s: float):
    """Linear interpolation of one trial's ``(t, forward, right, up)`` series."""
    if time_s <= points[0][0]:
        return points[0][1:4]
    if time_s >= points[-1][0]:
        return points[-1][1:4]
    low, high = 0, len(points) - 1
    while low < high - 1:
        middle = (low + high) // 2
        if points[middle][0] <= time_s:
            low = middle
        else:
            high = middle
    first, second = points[low], points[high]
    span = second[0] - first[0]
    fraction = (time_s - first[0]) / span if span > 0 else 0.0
    return tuple(first[index] + (second[index] - first[index]) * fraction for index in (1, 2, 3))


def quantile_band(per_attempt, time_s: float, axis_index: int):
    """P05/P50/P95 across the attempts that still cover ``time_s``."""
    values = [
        interpolate_points(points, time_s)[axis_index - 1]
        for points in per_attempt
        if points[-1][0] + 1e-9 >= time_s
    ]
    return percentile(values, 0.05), percentile(values, 0.50), percentile(values, 0.95)


# ---------------------------------------------------------------------------
# chain discovery
# ---------------------------------------------------------------------------

def chain_gap(chunks: list[list[dict[str, str]]]) -> float:
    sequence = [row for chunk in chunks for row in chunk]
    gaps = [
        curve_core.number(b, "capture_time_s") - curve_core.number(a, "capture_time_s")
        for a, b in zip(sequence, sequence[1:])
    ]
    return max(gaps, default=0.0)


def find_chains(runs):
    chains = []
    for index, (motion, chunk) in enumerate(runs):
        if motion != "201" or len(chunk) < 3 or index == 0 or index + 1 >= len(runs):
            continue
        if runs[index - 1][0] == "200" and runs[index + 1][0] == "203":
            chains.append((index, [runs[index - 1][1], chunk, runs[index + 1][1]]))
    return chains


def chain_records(chains, runs):
    """One record per complete chain: quality screen, basis, per-phase points."""
    records = []
    for attempt, (run_index, chunks) in enumerate(chains, start=1):
        before, thrust, landing = chunks
        forward, right = horizontal_basis(thrust)
        metrics = curve_core.quality_metrics(thrust, right)
        metrics["max_sample_gap_s"] = chain_gap(chunks)
        quality, include, reason = curve_core.classify(metrics)
        frame_durations = [phase_duration(chunk) for chunk in chunks]
        pitches = [float(row["cam_pitch_deg"]) for row in thrust if row.get("cam_pitch_deg")]
        # 203 is the last phase of the chain, so its boundary duration is the gap
        # to whatever motion the player went into next (a combo, a roll, the
        # idle). Falling back to its own last sample keeps a truncated capture
        # from inventing one.
        following_run = runs[run_index + 2] if run_index + 2 < len(runs) else None
        landing_boundary = (
            curve_core.number(following_run[1][0], "capture_time_s")
            - curve_core.number(landing[0], "capture_time_s")
        ) if following_run else frame_durations[2]
        records.append({
            "attempt_index": attempt,
            "run_index": run_index,
            "chunks": chunks,
            "forward": forward,
            "right": right,
            "metrics": metrics,
            "quality": quality,
            "include": include,
            "reason": reason,
            "phase_durations": frame_durations,
            "phase_frames": [len(chunk) - 1 for chunk in chunks],
            "boundary_durations": [
                boundary_duration(before, thrust),
                boundary_duration(thrust, landing),
                landing_boundary,
            ],
            "pitch_median": statistics.median(pitches) if pitches else None,
            "next_after_landing": runs[run_index + 2][0] if run_index + 2 < len(runs) else "",
            "sequence_duration": curve_core.number(landing[-1], "capture_time_s")
                                 - curve_core.number(before[0], "capture_time_s"),
            "origin": curve_core.vector(before[0], "world"),
            "points": {
                motion: phase_points(chunk, forward, right, CONTACT_TRIM_SAMPLES[motion])
                for motion, chunk in zip(CHAIN_IDS, chunks)
            },
        })
    return records


# ---------------------------------------------------------------------------
# canonical per-phase reconstruction
# ---------------------------------------------------------------------------

def is_complete_203(record) -> bool:
    """Plan §2.1: the natural 203 recovery curve uses only the complete trials."""
    return record["phase_durations"][2] >= 1.5


def canonical_phase(motion: str, cohort, domain_end: float, live_ends):
    """Robust position keys, quantile bands and residual statistics for one phase."""
    pool = [point for record in cohort for point in record["points"][motion]]
    keys = build_keys(pool, domain_end, data_end=max(point[0] for point in pool), live_ends=live_ends)
    residuals = residual_stats(pool, keys)

    per_attempt = [record["points"][motion] for record in cohort]
    rows = []
    for index, key in enumerate(keys):
        time_s = key["time_s"]
        coverage = coverage_at(live_ends, time_s)
        row = {
            "phase": motion,
            "sample_index": index,
            "time_s": f"{time_s:.6f}",
            "coverage_count": coverage,
            "coverage_fraction": f"{coverage / len(cohort):.6f}",
            "forward_cm": f"{key['position']['forward']:.4f}",
            "right_cm": f"{key['position']['right']:.4f}",
            "up_cm": f"{key['position']['up']:.4f}",
            "forward_speed_cm_s": f"{key['velocity']['forward']:.4f}",
            "right_speed_cm_s": f"{key['velocity']['right']:.4f}",
            "up_speed_cm_s": f"{key['velocity']['up']:.4f}",
            "speed_cm_s": f"{math.sqrt(sum(key['velocity'][axis] ** 2 for axis in AXES)):.4f}",
        }
        for axis_index, axis in enumerate(AXES, start=1):
            low, median, high = quantile_band(per_attempt, time_s, axis_index)
            row[f"{axis}_p05_cm"] = f"{low:.4f}"
            row[f"{axis}_p50_cm"] = f"{median:.4f}"
            row[f"{axis}_p95_cm"] = f"{high:.4f}"
        # 203 runs on the ground: the runtime vertical is a declared zero and the
        # measured vertical is kept as audit data (plan §5.3).
        rows.append(row)

    return {"keys": keys, "rows": rows, "residuals": residuals, "pool": pool}


def apply_vertical_policy(rows, motion: str, per_attempt):
    """Flat-ground 203 uses ``Z = 0``; the measured vertical stays auditable."""
    for row in rows:
        time_s = float(row["time_s"])
        low, median, high = quantile_band(per_attempt, time_s, 3)
        row["up_measured_p50_cm"] = f"{median:.4f}"
        row["up_measured_p95_cm"] = f"{high:.4f}"
        if motion == "203":
            row["up_cm"] = "0.0000"
            row["up_speed_cm_s"] = "0.0000"
            row["speed_cm_s"] = f"{math.hypot(float(row['forward_speed_cm_s']), float(row['right_speed_cm_s'])):.4f}"
    if motion != "203":
        for row in rows:
            row["up_measured_p50_cm"] = row["up_p50_cm"]
            row["up_measured_p95_cm"] = row["up_p95_cm"]


def tail_parameters(keys, coverage, data_end: float, half_window: float = FIT_HALF_WINDOW_S,
                    min_coverage: int = TAIL_MIN_COVERAGE):
    """Plan §5.3 tail: the model's own terminal tangent and acceleration.

    The continuation past the recorded coverage integrates the same calibration,
    so both numbers are read off the canonical model rather than from a CMC frame
    or a raw per-frame difference. Two conditions pick the anchor key:

    * enough trials still back it (the last few keys of 201 rest on a handful of
      long dives), and
    * the fit window ``[t - h, t + h]`` is fully inside the recorded samples.

    The second condition matters more than it looks: at the very last keys the
    local quadratic has data on one side only, so it still interpolates position
    fine but its second derivative is unconstrained — the uncorrected anchor
    reported a forward acceleration of 6581 cm/s² against 2804 cm/s² from an
    independent per-trial fit at the same instant.
    """
    usable = [
        (key, count) for key, count in zip(keys, coverage)
        if count >= min_coverage and key["time_s"] + half_window <= data_end + 1e-9
    ]
    if not usable:
        usable = [(key, count) for key, count in zip(keys, coverage) if count >= min_coverage] or [(keys[-1], 0)]
    anchor, anchor_coverage = usable[-1]
    terminal_velocity = [anchor["velocity"][axis] for axis in AXES]
    terminal_acceleration = [anchor["acceleration"][axis] for axis in AXES]
    return {
        "time_s": round(anchor["time_s"], 6),
        "coverage_trials_at_anchor": anchor_coverage,
        "velocity_cm_s": [round(value, 3) for value in terminal_velocity],
        "acceleration_cm_s2": [round(value, 3) for value in terminal_acceleration],
        "speed_cm_s": round(math.hypot(terminal_velocity[0], terminal_velocity[1], terminal_velocity[2]), 3),
        "min_coverage_trials": min_coverage,
        "fit_window_fully_covered": True,
        "recorded_data_ends_s": round(data_end, 6),
        "model": "canonical local-quadratic fit evaluated at the last key that is both well covered "
                 "and has a fully recorded fit window",
    }


def tail_cross_check(cohort, tail_time_s: float, half_window: float = FIT_HALF_WINDOW_S,
                     points_of=None):
    """Independent per-trial fit **at the canonical tail time**, as a cross-check.

    Each trial that still has samples around ``tail_time_s`` gets its own local
    quadratic there, so the comparison is like-for-like with the canonical
    model's tangent and acceleration. Fitting each trial at *its own* end time
    instead would average over trials that stop earlier, where the dive is
    shallower, and would look like a disagreement when both numbers are right.
    """
    points_of = points_of or (lambda record: record["points"]["201"])
    tangents = {axis: [] for axis in AXES}
    accelerations = {axis: [] for axis in AXES}
    contributors = []
    for record in cohort:
        near = [point for point in points_of(record) if abs(point[0] - tail_time_s) <= half_window]
        if len(near) < 4:
            continue
        contributors.append(record["attempt_index"])
        offsets = [point[0] - tail_time_s for point in near]
        for index, axis in enumerate(AXES, start=1):
            coefficients = fit_quadratic(offsets, [point[index] for point in near])
            if coefficients is None:
                continue
            tangents[axis].append(coefficients[1])
            accelerations[axis].append(2.0 * coefficients[2])
    return {
        "at_time_s": round(tail_time_s, 6),
        "half_window_s": half_window,
        "contributing_trials": contributors,
        "velocity_cm_s": {axis: round(statistics.median(tangents[axis]), 3) for axis in AXES if tangents[axis]},
        "acceleration_cm_s2": {axis: round(statistics.median(accelerations[axis]), 3) for axis in AXES if accelerations[axis]},
        "note": "per-trial median at the canonical tail time; the production tail is the model's own "
                "tangent and acceleration, this only checks that they are reachable",
    }


def velocity_cross_check(cohort, phase_index: int, keys) -> dict:
    """Compare the model's velocity with the capture's own measured velocity.

    Plan §5.3 keeps the raw velocity columns as cross-validation for the
    derivative of the smoothed model: the model is the controller, but if it
    disagreed with what the game actually moved at, the fit would be wrong.
    Raw velocity is only ever compared here — never differentiated again.
    """
    per_axis = {axis: [] for axis in AXES}
    for key in keys:
        time_s = key["time_s"]
        measured = {axis: [] for axis in AXES}
        for record in cohort:
            basis = (record["forward"], record["right"])
            for row in record["chunks"][phase_index]:
                if abs(curve_core.number(row, "capture_time_s")
                       - curve_core.number(record["chunks"][phase_index][0], "capture_time_s")
                       - time_s) > FIT_HALF_WINDOW_S:
                    continue
                velocity = curve_core.vector(row, "velocity")
                velocity_cm_s = tuple(component * 100.0 for component in velocity)
                measured["forward"].append(curve_core.dot(velocity_cm_s, basis[0]))
                measured["right"].append(curve_core.dot(velocity_cm_s, basis[1]))
                measured["up"].append(velocity_cm_s[1])
        for axis in AXES:
            if measured[axis]:
                per_axis[axis].append(abs(statistics.median(measured[axis]) - key["velocity"][axis]))
    return {
        "per_axis": {
            axis: {
                "median_abs_difference_cm_s": round(statistics.median(values), 1) if values else None,
                "p95_abs_difference_cm_s": round(percentile(values, 0.95), 1) if values else None,
                "max_abs_difference_cm_s": round(max(values), 1) if values else None,
            }
            for axis, values in per_axis.items()
        },
        "note": "per-key |median of the window's raw velocity samples - the model's velocity|; the raw "
                "column is noisy at the launch kick and at the landing impact, so the median and P95 "
                "describe the fit and the max marks a transient, not a curve error",
    }


def contact_frame_report(cohort) -> dict:
    """Classify each trial's last 201 sample instead of asserting what it is.

    The last sample of a 201 run is the frame the motion id changes to 203. It
    is *not* always an already-resolved contact frame: measured on this capture
    21 of 36 trials break sharply (median 19.70 cm from the trajectory, mostly
    vertical) while 15 keep descending at >=80 % of the previous frame's step and
    recorded vertical speed and sit a median 2.82 cm from the trajectory.

    The blanket trim is still the right call, and this is the measurement that
    says so: keeping those samples makes an independent per-trial fit at the tail
    anchor disagree with the canonical model by 11 % on the vertical velocity and
    by 5.7e4 cm/s^2 on its acceleration, i.e. the boundary frames poison any fit
    that includes them. Classifying them here keeps the rule honest and gives
    M2/E1 the list.
    """
    provisional = {
        record["attempt_index"]: phase_points(record["chunks"][1], record["forward"], record["right"], 0)
        for record in cohort
    }
    pool = [point for points in provisional.values() for point in points]
    live_ends = [points[-1][0] for points in provisional.values()]
    keys = build_keys(pool, max(live_ends), data_end=max(live_ends), live_ends=live_ends)

    entries = []
    for record in cohort:
        points = provisional[record["attempt_index"]]
        if len(points) < 3:
            continue
        last, previous, before = points[-1], points[-2], points[-3]
        model = sample_keys(keys, last[0])
        error = [last[index] - model[axis] for index, axis in enumerate(AXES, start=1)]
        step = abs(last[3] - previous[3])
        previous_step = abs(previous[3] - before[3])
        recorded_velocity = abs(curve_core.number(record["chunks"][1][-1], "velocity_y") * 100.0)
        previous_velocity = abs(curve_core.number(record["chunks"][1][-2], "velocity_y") * 100.0)
        keeps_descending = (
            previous_step > 1e-9 and previous_velocity > 1e-9
            and step / previous_step >= 0.8 and recorded_velocity / previous_velocity >= 0.8
        )
        entries.append({
            "attempt_index": record["attempt_index"],
            "time_s": round(last[0], 6),
            "deviation_cm": round(math.sqrt(sum(value * value for value in error)), 3),
            "deviation_vertical_cm": round(abs(error[2]), 3),
            "still_descending": keeps_descending,
        })
    broken = [entry for entry in entries if not entry["still_descending"]]
    returning = [entry for entry in entries if entry["still_descending"]]
    return {
        "trials": len(entries),
        "resolved_contact_frames": len(broken),
        "still_descending_frames": len(returning),
        "deviation_cm": {
            "resolved_contact": describe([entry["deviation_cm"] for entry in broken]),
            "still_descending": describe([entry["deviation_cm"] for entry in returning]),
        },
        "measurements": entries,
        "note": "all of these are 201->203 transition frames and all are excluded from the fit and the "
                "residual statistics; the split only records how abruptly each one breaks",
    }


def pitch_independence(cohort):
    """Plan §9.2: aim pitch must not move the trajectory.

    Two checks: the 201 launch direction's world-up component must be constant
    across the recorded pitch range, and its correlation with camera pitch must
    be ~0. Both are computed, never assumed.
    """
    pitches, dive_components, speeds = [], [], []
    for record in cohort:
        chunk = record["chunks"][1]
        pitch_values = [float(row["cam_pitch_deg"]) for row in chunk if row.get("cam_pitch_deg")]
        if not pitch_values:
            continue
        velocity = tuple(
            statistics.mean(curve_core.vector(row, "velocity")[axis] for row in chunk[1:6])
            for axis in range(3)
        )
        magnitude = math.sqrt(sum(value * value for value in velocity))
        if magnitude < 1e-9:
            continue
        pitches.append(statistics.median(pitch_values))
        dive_components.append(velocity[1] / magnitude)
        speeds.append(magnitude * 100.0)

    def pearson(first, second):
        if len(first) < 3:
            return None
        mean_first, mean_second = statistics.mean(first), statistics.mean(second)
        numerator = sum((a - mean_first) * (b - mean_second) for a, b in zip(first, second))
        left = math.sqrt(sum((a - mean_first) ** 2 for a in first))
        right = math.sqrt(sum((b - mean_second) ** 2 for b in second))
        return round(numerator / (left * right), 4) if left > 0 and right > 0 else None

    median_dive = statistics.median(dive_components)

    # The plan's §9.2 states the claim as "same real seconds => same trajectory at
    # every pitch", and names the rise as well as the dive. Sample each trial's
    # own 201 and 200 curves at fixed times and correlate the local components
    # with that trial's camera pitch.
    def matched_time(phase: str, phase_index: int, time_s: float, pitches_out) -> dict:
        triples = {axis: [] for axis in AXES}
        for record in cohort:
            points = record["points"][phase]
            if points[-1][0] < time_s:
                continue
            pitch_values = [float(row["cam_pitch_deg"]) for row in record["chunks"][phase_index]
                            if row.get("cam_pitch_deg")]
            if not pitch_values:
                continue
            sample = interpolate_points(points, time_s)
            pitches_out.append(statistics.median(pitch_values))
            for index, axis in enumerate(AXES):
                triples[axis].append(sample[index])
        return {
            "trials": len(pitches_out),
            **{f"correlation_pitch_vs_{axis}_cm": pearson(pitches_out, triples[axis]) for axis in AXES},
            **{f"{axis}_cm_spread": round(max(values) - min(values), 3) for axis, values in triples.items()},
        }

    matched = {}
    all_pitches: list[float] = []
    all_correlations = [pearson(pitches, dive_components), pearson(pitches, speeds)]
    for phase, phase_index, times in (("201", 1, (0.10, 0.20)), ("200", 0, (0.10, 0.20, 0.30))):
        for time_s in times:
            pitch_values: list[float] = []
            entry = matched_time(phase, phase_index, time_s, pitch_values)
            matched[f"{phase}_t_{time_s:.2f}s"] = entry
            all_pitches.extend(pitch_values)
            all_correlations.extend(value for key, value in entry.items() if key.startswith("correlation_"))

    # The plan's §9.2 acceptance is stated on named quantities — the rise, the
    # distance and the fixed dive angle — not on a position sampled at an
    # arbitrary instant. Measure those directly; a pointwise channel can carry a
    # small correlation from sub-frame timing jitter even when the integral is
    # flat, so the pointwise numbers are diagnostics, not the verdict.
    acceptance = {}
    trials_with_pitch = [
        (record, statistics.median([float(row["cam_pitch_deg"]) for row in record["chunks"][1]
                                    if row.get("cam_pitch_deg")]))
        for record in cohort
        if any(row.get("cam_pitch_deg") for row in record["chunks"][1])
    ]
    for index, (name, extract) in enumerate((
        ("rise_200_cm", lambda record: max(point[3] for point in record["points"]["200"])),
        ("distance_201_forward_cm", lambda record: record["points"]["201"][-1][1]),
        ("dive_angle_deg", lambda record: math.degrees(math.asin(
            -statistics.mean(curve_core.vector(row, "velocity")[1] for row in record["chunks"][1][1:6])
            / max(1e-9, math.sqrt(sum(
                statistics.mean(curve_core.vector(row, "velocity")[axis] for row in record["chunks"][1][1:6]) ** 2
                for axis in range(3))))))),
    )):
        values = [extract(record) for record, _ in trials_with_pitch]
        acceptance[name] = {
            "correlation_pitch": pearson([pitch for _, pitch in trials_with_pitch], values),
            "min": round(min(values), 3),
            "max": round(max(values), 3),
            "spread": round(max(values) - min(values), 3),
        }

    strongest = max((abs(value) for value in all_correlations if value is not None), default=0.0)
    strongest_acceptance = max(
        (abs(entry["correlation_pitch"]) for entry in acceptance.values()
         if entry["correlation_pitch"] is not None), default=0.0
    )
    above_zero = sum(1 for pitch in pitches if pitch > 0.0)
    sorted_pitches = sorted(pitches)
    gap = round(sorted_pitches[-1] - sorted_pitches[-2], 3) if len(sorted_pitches) > 1 else None
    # The verdict is scoped on purpose. 35 of 36 trials sit at or below -0.7 deg
    # and exactly one is above zero, so "no pitch dependence" is established for
    # the recorded band only; an upward-aim regime is simply untested.
    return {
        "trials": len(pitches),
        "camera_pitch_deg": {
            "min": round(min(pitches), 3),
            "max": round(max(pitches), 3),
            "trials_above_zero": above_zero,
            "gap_from_second_highest_deg": gap,
        },
        "correlation_pitch_vs_dive_up_component": pearson(pitches, dive_components),
        "correlation_pitch_vs_launch_speed": pearson(pitches, speeds),
        "acceptance_quantities": acceptance,
        "strongest_absolute_correlation_acceptance": round(strongest_acceptance, 4),
        "matched_time_trajectory": matched,
        "strongest_absolute_correlation_pointwise": round(strongest, 4),
        "pointwise_note": "position sampled at a fixed instant; small values here can come from sub-frame timing "
                          "jitter in the phase boundary and are not by themselves a pitch dependence — compare "
                          "with acceptance_quantities, where the same motion integrated over the phase is flat",
        "dive_up_component": {
            "min": round(min(dive_components), 5),
            "max": round(max(dive_components), 5),
            "spread": round(max(dive_components) - min(dive_components), 5),
        },
        "dive_angle_deg": round(math.degrees(math.asin(-median_dive)), 3),
        "dive_angle_spread_deg": round(
            math.degrees(math.asin(-min(dive_components))) - math.degrees(math.asin(-max(dive_components))), 4
        ),
        "launch_speed_cm_s": {
            "min": round(min(speeds), 1),
            "max": round(max(speeds), 1),
        },
        "verdict": ("pitch independent within the recorded band: rise, distance and dive angle all have "
                    f"|corr| <= {strongest_acceptance:.3f}"
                    if strongest_acceptance < 0.3 else "pitch correlated"),
        "scope": f"recorded camera pitch is {min(pitches):.2f} to {max(pitches):.2f} deg with only "
                 f"{above_zero} of {len(pitches)} trials above 0 deg, so the recorded data cannot test an "
                 f"upward-aim regime; the runtime contract comes from MHRise gameplay, not from this script",
    }


def phase_outliers(cohort, keys, motion: str, horizontal_only: bool):
    """Per-trial deviation from the canonical curve, reported never dropped.

    Samples past the last key are skipped for the same reason the residual
    statistics skip them: interpolation clamps there, and a clamped lookup turns
    "this trial is still moving after the curve ends" into a fake 45 cm outlier.
    """
    last_key_time = keys[-1]["time_s"]
    outliers = []
    for record in cohort:
        points = record["points"][motion]
        worst, worst_time = 0.0, 0.0
        for point in points:
            if point[0] > last_key_time + 1e-9:
                continue
            model = sample_keys(keys, point[0])
            error = [point[index] - model[axis] for index, axis in enumerate(AXES, start=1)]
            distance = math.hypot(error[0], error[1]) if horizontal_only else math.sqrt(sum(v * v for v in error))
            if distance > worst:
                worst, worst_time = distance, point[0]
        outliers.append({
            "attempt_index": record["attempt_index"],
            "max_deviation_cm": round(worst, 3),
            "at_time_s": round(worst_time, 4),
            "over_threshold": worst > OUTLIER_CM,
        })
    outliers.sort(key=lambda entry: -entry["max_deviation_cm"])
    return outliers


# ---------------------------------------------------------------------------
# writers
# ---------------------------------------------------------------------------

def write_csv(path: Path, rows, fields) -> None:
    with path.open("w", newline="", encoding="utf-8-sig") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def build_outputs(capture: Path, output_dir: Path, force: bool = False):
    rows = curve_core.read_rows(capture)
    runs = curve_core.contiguous_runs(rows)
    chains = find_chains(runs)
    if not chains:
        raise ValueError("No complete contiguous motion chain 200 → 201 → 203 found")

    capture_id = capture.stem.removesuffix("_samples")
    output_dir.mkdir(parents=True, exist_ok=True)
    all_path = output_dir / f"强化操虫穿刺_完整曲线_全样本_{capture_id}.csv"
    clean_path = output_dir / f"强化操虫穿刺_完整曲线_纯样本_{capture_id}.csv"
    audit_path = output_dir / f"强化操虫穿刺_完整曲线_样本审计_{capture_id}.csv"
    report_path = output_dir / f"强化操虫穿刺_完整曲线说明_{capture_id}.md"
    canonical_paths = {motion: output_dir / f"强化操虫穿刺_{motion}.csv" for motion in CHAIN_IDS}
    audit_json_path = output_dir / "强化操虫穿刺_生成审计.json"
    canonical_report_path = output_dir / "强化操虫穿刺_规范曲线说明.md"

    targets = [all_path, clean_path, audit_path, report_path, audit_json_path,
               canonical_report_path, *canonical_paths.values()]
    existing = [path for path in targets if path.exists()]
    if existing and not force:
        raise FileExistsError(
            "Refusing to overwrite existing output (pass --force to regenerate): "
            + ", ".join(path.name for path in existing)
        )

    records = chain_records(chains, runs)
    clean_cohort = [record for record in records if record["include"]]

    # ---- per-attempt export -------------------------------------------------
    all_rows, clean_rows, audit_rows = [], [], []
    for record in records:
        chunks = record["chunks"]
        forward, right = record["forward"], record["right"]
        origin = record["origin"]
        start_time = curve_core.number(chunks[0][0], "capture_time_s")
        sequence_duration = record["sequence_duration"]
        audit_rows.append({
            "capture_id": capture_id,
            "attempt_index": record["attempt_index"],
            "motion_run_index": record["run_index"],
            "motion_chain": "200>201>203",
            "next_motion_after_203": record["next_after_landing"],
            "phase_200_frames": record["phase_frames"][0],
            "phase_201_frames": record["phase_frames"][1],
            "phase_203_frames": record["phase_frames"][2],
            "phase_200_duration_s": f"{record['phase_durations'][0]:.6f}",
            "phase_201_duration_s": f"{record['phase_durations'][1]:.6f}",
            "phase_203_duration_s": f"{record['phase_durations'][2]:.6f}",
            "phase_200_boundary_s": f"{record['boundary_durations'][0]:.6f}",
            "phase_201_boundary_s": f"{record['boundary_durations'][1]:.6f}",
            "phase_203_boundary_s": f"{record['boundary_durations'][2]:.6f}",
            "sequence_duration_s": f"{sequence_duration:.6f}",
            "camera_pitch_201_median_deg": f"{record['pitch_median']:.3f}" if record["pitch_median"] is not None else "",
            "max_cross_track_cm_201": f"{record['metrics']['max_cross_track_cm']:.3f}",
            "max_cross_track_speed_cm_s_201": f"{record['metrics']['max_cross_track_speed_cm_s']:.3f}",
            "max_yaw_step_deg_201": f"{record['metrics']['max_yaw_step_deg']:.3f}",
            "max_sample_gap_s": f"{record['metrics']['max_sample_gap_s']:.6f}",
            "quality": record["quality"],
            "include_in_clean_curve": int(record["include"]),
            "reason": record["reason"],
        })

        sequence_frame = 0
        for phase_order, (motion_id, chunk) in enumerate(zip(CHAIN_IDS, chunks)):
            phase_start = curve_core.number(chunk[0], "capture_time_s")
            phase_end = curve_core.number(chunk[-1], "capture_time_s")
            duration = phase_end - phase_start
            next_motion = ("201" if motion_id == "200" else "203" if motion_id == "201"
                           else record["next_after_landing"])
            for phase_frame, row in enumerate(chunk):
                position = curve_core.vector(row, "world")
                velocity = curve_core.vector(row, "velocity")
                delta_cm = tuple((position[axis] - origin[axis]) * 100.0 for axis in range(3))
                velocity_cm_s = tuple(component * 100.0 for component in velocity)
                timestamp = curve_core.number(row, "capture_time_s")
                elapsed = timestamp - start_time
                row_out = {
                    "capture_id": capture_id,
                    "attempt_index": record["attempt_index"],
                    "sequence_frame_index": sequence_frame,
                    "phase_order": phase_order,
                    "motion_id": motion_id,
                    "phase_frame_index": phase_frame,
                    "capture_time_s": f"{timestamp:.6f}",
                    "sequence_elapsed_s": f"{elapsed:.6f}",
                    "sequence_elapsed_norm": f"{elapsed / sequence_duration:.6f}",
                    "phase_elapsed_s": f"{timestamp - phase_start:.6f}",
                    "phase_elapsed_norm": f"{(timestamp - phase_start) / duration:.6f}" if duration > 0 else "0",
                    "sequence_duration_s": f"{sequence_duration:.6f}",
                    "phase_duration_s": f"{duration:.6f}",
                    "phase_duration_frames": len(chunk) - 1,
                    "previous_motion_id": "-" if motion_id == "200" else CHAIN_IDS[phase_order - 1],
                    "next_motion_id": next_motion,
                    "camera_pitch_deg": f"{float(row['cam_pitch_deg']):.3f}" if row.get("cam_pitch_deg") else "",
                    "launch_x": f"{forward[0]:.7f}",
                    "launch_y": f"{forward[1]:.7f}",
                    "launch_z": f"{forward[2]:.7f}",
                    "position_x_cm": f"{(position[0] - origin[0]) * 100.0:.3f}",
                    "position_y_cm": f"{(position[1] - origin[1]) * 100.0:.3f}",
                    "position_z_cm": f"{(position[2] - origin[2]) * 100.0:.3f}",
                    "forward_cm": f"{curve_core.dot(delta_cm, forward):.3f}",
                    "right_cm": f"{curve_core.dot(delta_cm, right):.3f}",
                    "up_cm": f"{delta_cm[1]:.3f}",
                    "world_up_cm": f"{delta_cm[1]:.3f}",
                    "speed_cm_s": f"{curve_core.magnitude(velocity_cm_s):.3f}",
                    "forward_speed_cm_s": f"{curve_core.dot(velocity_cm_s, forward):.3f}",
                    "right_speed_cm_s": f"{curve_core.dot(velocity_cm_s, right):.3f}",
                    "up_speed_cm_s": f"{velocity_cm_s[1]:.3f}",
                    "quality": record["quality"],
                    "include_in_clean_curve": int(record["include"]),
                }
                all_rows.append(row_out)
                if record["include"]:
                    clean_rows.append(row_out)
                sequence_frame += 1

    # ---- canonical curves ---------------------------------------------------
    canonical = {}
    audit_phases = {}

    for phase_index, motion in enumerate(CHAIN_IDS):
        if motion == "203":
            cohort = [record for record in clean_cohort if is_complete_203(record)]
            # The curve covers 203's own samples; the extra frame up to the next
            # motion belongs to that motion, so it is timed but not modelled.
            domain_end = min(record["phase_durations"][2] for record in cohort)
            live_ends = [record["boundary_durations"][2] for record in cohort]
        elif motion == "200":
            cohort = list(clean_cohort)
            domain_end = min(record["boundary_durations"][0] for record in cohort)
            live_ends = [record["boundary_durations"][0] for record in cohort]
        else:
            cohort = list(clean_cohort)
            domain_end = max(record["points"]["201"][-1][0] for record in cohort)
            # 201 ends at real ground contact, so "still backed at t" means "has a
            # usable sample at or past t" — the boundary duration would claim
            # coverage the recording does not actually contain.
            live_ends = [record["points"]["201"][-1][0] for record in cohort]

        phase = canonical_phase(motion, cohort, domain_end, live_ends)
        apply_vertical_policy(phase["rows"], motion, [record["points"][motion] for record in cohort])
        canonical[motion] = phase

        horizontal_only = motion == "203"
        outliers = phase_outliers(cohort, phase["keys"], motion, horizontal_only)
        flagged = [entry for entry in outliers if entry["over_threshold"]]
        horizontal = horizontal_residual_stats(
            [point for record in cohort for point in record["points"][motion]], phase["keys"]
        )
        measured = horizontal["p95"] if horizontal_only else phase["residuals"]["euclidean"]["p95"]
        audit_phases[motion] = {
            "cohort_trials": len(cohort),
            "cohort_note": {
                "200": "all clean candidates",
                "201": "all clean candidates; each trial's last sample is the 201->203 transition frame and is "
                       "excluded from the fit and the residual statistics (see contact_frames for the measured "
                       "per-trial classification, and untrimmed_counterfactual for what keeping them would cost)",
                "203": "complete candidates only; early-exit trials are not used for the recovery curve",
            }[motion],
            "grid_hz": GRID_HZ,
            "key_count": len(phase["rows"]),
            "canonical_domain_s": [0.0, round(domain_end, 6)],
            "key_last_s": round(phase["keys"][-1]["time_s"], 6),
            "key_last_note": "the grid is rounded up to cover the recorded domain; trailing keys that no trial "
                             "still backs are then dropped, so the last key can sit one frame inside it",
            "source_coverage": {
                "boundary_duration_s": {
                    "min": round(min(record["boundary_durations"][phase_index] for record in cohort), 6),
                    "median": round(statistics.median(record["boundary_durations"][phase_index] for record in cohort), 6),
                    "max": round(max(record["boundary_durations"][phase_index] for record in cohort), 6),
                },
                "coverage_count_first": int(phase["rows"][0]["coverage_count"]),
                "coverage_count_last": int(phase["rows"][-1]["coverage_count"]),
                "coverage_count_min": min(int(row["coverage_count"]) for row in phase["rows"]),
                "coverage_note": "coverage_count is how many cohort trials still have this phase running at "
                                 "that key; the p05/p50/p95 columns come only from the trials that actually "
                                 "have a recorded sample at or past it, which is a smaller set at the tail "
                                 "(200's last key: 25 live but 9 sampled). Read the pair together.",
            },
            "model": {
                "kind": "local quadratic regression (Savitzky-Golay) with MAD re-weighting",
                "half_window_s": FIT_HALF_WINDOW_S,
                "min_points": FIT_MIN_POINTS,
                "robust_iterations": ROBUST_ITERATIONS,
                "vertical_policy": "flat ground: runtime up = 0, measured vertical kept as audit data"
                                   if motion == "203" else "model vertical (world +Y)",
            },
            "residuals_cm": phase["residuals"],
            "velocity_cross_check_cm_s": velocity_cross_check(cohort, phase_index, phase["keys"]),
            "gate": gate_dispersion(cohort, phase["keys"], motion, horizontal_only, outliers),
            "outliers": outliers[:6],
            "outlier_threshold_cm": OUTLIER_CM,
            "flagged_trials": [entry["attempt_index"] for entry in flagged],
        }
        if motion == "203":
            audit_phases[motion]["horizontal_residuals_cm"] = horizontal
            audit_phases[motion]["vertical_noise_cm"] = {
                "measured": phase["residuals"]["up"],
                "note": "recorded capsule height above the landing surface; flat-ground runtime uses up = 0, "
                        "so this is terrain tracking and is deliberately outside the gate",
            }
        if motion == "201":
            coverage_per_key = [int(row["coverage_count"]) for row in phase["rows"]]
            tail = tail_parameters(phase["keys"], coverage_per_key,
                                   max(record["points"]["201"][-1][0] for record in cohort))
            audit_phases[motion]["tail"] = tail
            audit_phases[motion]["tail_cross_check"] = tail_cross_check(cohort, tail["time_s"])
            # Agreement along the whole well-covered tail, not just at the anchor:
            # a single number at one key cannot tell a reader whether 20 % is an
            # anchor defect or simply how well acceleration is determined here.
            velocity_errors, acceleration_errors = [], []
            for key in phase["keys"]:
                if key["time_s"] + FIT_HALF_WINDOW_S > max(record["points"]["201"][-1][0] for record in cohort):
                    continue
                cross = tail_cross_check(cohort, key["time_s"])
                if not cross["velocity_cm_s"]:
                    continue
                # Normalise by the phase's own speed scale, not per axis: the
                # lateral axis is ~0, so a per-axis ratio would divide a
                # millimetre-per-second difference by one and report thousands
                # of percent.
                scale = max(1.0, math.sqrt(sum(key["velocity"][axis] ** 2 for axis in AXES)))
                velocity_errors.append(max(
                    100.0 * abs(key["velocity"][axis] - cross["velocity_cm_s"][axis])
                    / scale for axis in AXES if axis in cross["velocity_cm_s"]))
                acceleration_errors.append(max(
                    100.0 * abs(key["acceleration"][axis] - cross["acceleration_cm_s2"][axis])
                    / scale for axis in AXES if axis in cross["acceleration_cm_s2"]))
            audit_phases[motion]["tail_agreement_profile"] = {
                "fully_covered_keys": len(velocity_errors),
                "velocity_percent": describe(velocity_errors),
                "acceleration_percent": describe(acceleration_errors),
                "note": "model vs the per-trial median fit at the same instant, over every key whose fit window "
                        "is fully recorded; the velocity (the term the continuation is dominated by) holds to "
                        "well under a percent, while the acceleration is only determined to tens of percent "
                        "anywhere in this tail — which is why the runtime continuation needs a stop condition",
            }
            audit_phases[motion]["contact_frames"] = contact_frame_report(cohort)
            # What the alternative would cost, so the trim is a measurement and
            # not a preference.
            keep_all = {record["attempt_index"]: phase_points(
                record["chunks"][1], record["forward"], record["right"], 0) for record in cohort}
            keep_pool = [point for points in keep_all.values() for point in points]
            keep_live = [points[-1][0] for points in keep_all.values()]
            keep_keys = build_keys(keep_pool, max(keep_live), data_end=max(keep_live), live_ends=keep_live)
            keep_residuals = residual_stats(keep_pool, keep_keys)
            keep_tail = tail_parameters(
                keep_keys, [coverage_at(keep_live, key["time_s"]) for key in keep_keys], max(keep_live))
            keep_cross = tail_cross_check(
                cohort, keep_tail["time_s"],
                points_of=lambda record: keep_all[record["attempt_index"]],
            )
            audit_phases[motion]["untrimmed_counterfactual"] = {
                "last_key_s": round(keep_keys[-1]["time_s"], 6),
                "residuals_cm": keep_residuals["euclidean"],
                "tail": keep_tail,
                "tail_cross_check": keep_cross,
                "velocity_agreement_pct": {
                    axis: round(100.0 * abs(keep_tail["velocity_cm_s"][index] - keep_cross["velocity_cm_s"].get(axis, 0.0))
                                / max(1.0, abs(keep_cross["velocity_cm_s"].get(axis, 0.0))), 2)
                    for index, axis in enumerate(AXES) if axis in keep_cross["velocity_cm_s"]
                },
                "note": "what the 201 curve and its tail would look like with the transition frames kept; "
                        "the trim is chosen because this variant disagrees with an independent per-trial "
                        "fit at its own anchor",
            }

    excluded_trials = [
        {
            "attempt_index": record["attempt_index"],
            "reason": record["reason"],
            "max_cross_track_cm_201": round(record["metrics"]["max_cross_track_cm"], 3),
            "max_cross_track_speed_cm_s_201": round(record["metrics"]["max_cross_track_speed_cm_s"], 3),
        }
        for record in records if not record["include"]
    ]
    # Plan §2.1: the 4 trials that left 203 early are what E1 uses to place the
    # ground combo/dodge accept windows, so record what they left into and when,
    # not just that they were short.
    early_exit = [
        {
            "attempt_index": record["attempt_index"],
            "duration_s": round(record["phase_durations"][2], 6),
            "frames": record["phase_frames"][2],
            "left_into_motion": record["next_after_landing"],
            "boundary_duration_s": round(record["boundary_durations"][2], 6),
        }
        for record in clean_cohort if not is_complete_203(record)
    ]

    audit = {
        "generator": "Scripts/MHRise/build_kinsect_piercer_full_curves.py",
        "generated_from": {
            "capture": str(capture),
            "capture_id": capture_id,
            "sha256": hashlib.sha256(capture.read_bytes()).hexdigest(),
            "rows": len(rows),
            "nominal_hz": GRID_HZ,
        },
        "coordinate_contract": {
            "forward": "normalize(mean velocity 201 samples 1..5 projected to the horizontal plane)",
            "right": "(forward.z, 0, -forward.x)",
            "up": "world +Y (plan §5.2 Local.Z = Delta.y)",
            "units": "centimeters, centimeters per second, seconds",
            "origin": "each phase curve starts at its own first sample, i.e. (0, 0, 0)",
            "pitch_policy": "camera pitch never enters the basis, grouping, scaling or curve rotation",
            "runtime_transform": "WorldOffset(t) = D*Curve.X(t) + (WorldUp x D)*Curve.Y(t) + WorldUp*Curve.Z(t), D = frozen horizontal aim",
        },
        "candidate_selection": {
            "complete_chains": len(records),
            "clean_candidates": len(clean_cohort),
            "excluded": excluded_trials,
            "early_exit_203": early_exit,
            "early_exit_note": "early-exit 203 trials stay in the per-attempt export but are not used for the "
                               "recovery curve; they are listed with the motion they left into so E1 can place "
                               "the ground accept windows",
        },
        "phases": audit_phases,
        "pitch_independence": pitch_independence(clean_cohort),
        "validation_summary": {
            "p95_limit_cm": P95_GATE_CM,
            "phases": {motion: audit_phases[motion]["gate"] for motion in CHAIN_IDS},
        },
    }

    # ---- write --------------------------------------------------------------
    write_csv(all_path, all_rows, CURVE_FIELDS)
    write_csv(clean_path, clean_rows, CURVE_FIELDS)
    write_csv(audit_path, audit_rows, AUDIT_FIELDS)
    for motion, phase in canonical.items():
        ordered = sorted(phase["rows"], key=lambda row: int(row["sample_index"]))
        write_csv(canonical_paths[motion], ordered, CANONICAL_FIELDS)
    audit_json_path.write_text(json.dumps(audit, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    counts = defaultdict(int)
    for row in audit_rows:
        counts[row["quality"]] += 1
    excluded = [row for row in audit_rows if row["include_in_clean_curve"] == 0]
    included_pitches = [float(row["camera_pitch_201_median_deg"]) for row in audit_rows
                        if row["include_in_clean_curve"] == 1 and row["camera_pitch_201_median_deg"]]
    report = [
        "# 强化操虫穿刺完整动作曲线（200 → 201 → 203）",
        "",
        f"- 原始录制：`{capture}`",
        f"- 完整动作链：{len(chains)} 次；纯样本候选：{counts['clean_candidate']} 次；",
        f"  疑似外力扰动：{counts['suspected_external_push']} 次；采样卡帧：{counts['recording_hitch']} 次。",
        f"- 保留样本的相机俯仰范围：{min(included_pitches):.2f}° 至 {max(included_pitches):.2f}°。",
        "",
        "## 文件",
        "",
        f"- `{all_path.name}`：所有完整 200→201→203 链的逐帧数据及质量标记。",
        f"- `{clean_path.name}`：纯样本候选的逐帧数据。",
        f"- `{audit_path.name}`：每次链的各段帧数/边界时长、后继、扰动指标和筛选理由。",
        "",
        "## 曲线口径",
        "",
        "- **本次起改为水平建基**（计划 §5.2）：`forward` 取 201 起始速度的**水平分量**，",
        "  `right = (forward.z, 0, -forward.x)`，`up` 就是世界 `+Y`。",
        "  旧版用 201 的三维起始速度建基，其 `right` 与本版逐位相同（横偏筛选不受影响），",
        "  但 `up_cm` 不是世界竖直、不能当高度读取。",
        "- 每次曲线从 motion 200 首帧位置起算，到 motion 203 最后一帧；保留实际时间戳、",
        "  整招归一时间、各动作段 ID/段内帧和段内归一时间。",
        "- 位移单位 cm，速度单位 cm/s。瞄准俯仰只留在审计列，不参与建基、分组或缩放。",
        "- 该输出是逐次样本的中间曲线，不是 UE Curve 资产；规范曲线见同目录的",
        "  `强化操虫穿刺_200.csv` / `_201.csv` / `_203.csv` 与 `强化操虫穿刺_生成审计.json`。",
        "",
        "## 筛选",
        "",
        f"- 201 段横向最大偏移阈值 {curve_core.MAX_CROSS_TRACK_CM:g} cm；横向速度阈值 "
        f"{curve_core.MAX_CROSS_TRACK_SPEED_CM_S:g} cm/s；最大采样间隔 "
        f"{curve_core.MAX_SAMPLE_GAP_S:g} s。",
        "- 疑似外力样本完整保留于全样本 CSV，但不纳入纯样本 CSV；标签是轨迹异常推断，",
        "  不是游戏碰撞事件的直接读取。",
        "",
    ]
    if excluded:
        report.extend(["被排除的完整动作链：", ""])
        for row in excluded:
            report.append(
                f"- 第 {row['attempt_index']} 次：201 横偏 {float(row['max_cross_track_cm_201']):.1f} cm，"
                f"横向速度 {float(row['max_cross_track_speed_cm_s_201']):.0f} cm/s。"
            )
        report.append("")
    report_path.write_text("\n".join(report), encoding="utf-8")

    canonical_report = [
        "# 强化操虫穿刺规范曲线（生成物，勿手改）",
        "",
        "> 由 `Scripts/MHRise/build_kinsect_piercer_full_curves.py` 从实录重建。",
        "> **改数请改脚本或原始录制。** 逐次曲线源见同目录的 `强化操虫穿刺_完整曲线_*`。",
        "",
        f"**真值源**：`{capture}`（sha256 `{audit['generated_from']['sha256'][:16]}…`）",
        "",
        "## 相位合同",
        "",
        "| 相位 | 候选 | 实测域 | CSV 末键 | 覆盖数（首→末） | 统计量 | P95 | P99 | RMSE | 超限样本/候选 |",
        "|---|---|---:|---:|---:|---|---:|---:|---:|---|",
    ]
    for motion in CHAIN_IDS:
        phase = audit_phases[motion]
        gate = phase["gate"]
        statistic = "水平" if motion == "203" else "三维"
        canonical_report.append(
            f"| {motion} | {phase['cohort_trials']} | 0–{phase['canonical_domain_s'][1]:.4f} s | "
            f"{phase['key_last_s']:.4f} s | "
            f"{phase['source_coverage']['coverage_count_first']}→{phase['source_coverage']['coverage_count_last']} | "
            f"{statistic} | {gate['p95_cm']:.2f} | {gate['p99_cm']:.2f} | {gate['rmse_cm']:.2f} | "
            f"{gate['samples_over_limit']}/{gate['samples']} 样本、{gate['trials_over_limit']}/{gate['trials']} 候选 |"
        )
    canonical_report.extend([
        "",
        f"门槛是 **P95 ≤ {P95_GATE_CM:g} cm**；同表给出 P99、RMSE 与超限计数，因为 203 的尾部由单个候选"
        "（第 6 次）主导——只看 P95 会把 3% 的坏尾部完全藏起来，计划 §5.3 要求的是报告离散度而不是挑一个通过的数字。",
    ])
    tail = audit_phases["201"]["tail"]
    cross = audit_phases["201"]["tail_cross_check"]
    canonical_report.extend([
        "",
        "## 201 尾段参数（超出实录覆盖时的安全续行）",
        "",
        f"- 锚点：`{tail['time_s']:.4f} s`，该键仍有 `{tail['coverage_trials_at_anchor']}` 次候选覆盖，"
        f"且拟合半窗完整落在实录范围内（实录样本止于 `{tail['recorded_data_ends_s']:.4f} s`）。",
        f"- 终末速度：forward `{tail['velocity_cm_s'][0]:.1f}` / right `{tail['velocity_cm_s'][1]:.1f}` / "
        f"up `{tail['velocity_cm_s'][2]:.1f}` cm/s（合成 `{tail['speed_cm_s']:.0f}` cm/s）",
        f"- 终末加速度：forward `{tail['acceleration_cm_s2'][0]:.0f}` / right `{tail['acceleration_cm_s2'][1]:.0f}` / "
        f"up `{tail['acceleration_cm_s2'][2]:.0f}` cm/s²",
        f"- 独立复核（同一时刻的逐次拟合中位，{len(cross['contributing_trials'])} 次候选）："
        f"速度 forward `{cross['velocity_cm_s'].get('forward', 0):.0f}` / up `{cross['velocity_cm_s'].get('up', 0):.0f}`，"
        f"加速度 forward `{cross['acceleration_cm_s2'].get('forward', 0):.0f}` / up `{cross['acceleration_cm_s2'].get('up', 0):.0f}`。",
        f"- 整条可覆盖尾段（{audit_phases['201']['tail_agreement_profile']['fully_covered_keys']} 个键）上，"
        f"模型与独立逐次拟合的相对差：**速度中位 "
        f"{audit_phases['201']['tail_agreement_profile']['velocity_percent']['p50']:.2f}%、P95 "
        f"{audit_phases['201']['tail_agreement_profile']['velocity_percent']['p95']:.2f}%、最大 "
        f"{audit_phases['201']['tail_agreement_profile']['velocity_percent']['max']:.2f}%**；"
        f"加速度中位 {audit_phases['201']['tail_agreement_profile']['acceleration_percent']['p50']:.0f}%、最大 "
        f"{audit_phases['201']['tail_agreement_profile']['acceleration_percent']['max']:.0f}%。",
        "- 也就是说 **120 Hz 数据里的二阶导本身就只有几十个百分点的确定度**，这不是某个锚点选坏了，"
        "整条尾段都是这样。外推由速度主导（<1%）可用；加速度项只能在真实触地或独立看门狗到期时停下，",
        "不能把曲线默认常量当作可无限外推的物理。",
        "",
        "## 俯仰独立性",
        "",
        f"- 录制相机俯仰：{audit['pitch_independence']['camera_pitch_deg']['min']:.2f}° 至 "
        f"{audit['pitch_independence']['camera_pitch_deg']['max']:.2f}°（{audit['pitch_independence']['trials']} 次）",
        f"- 201 下冲方向的世界竖直分量：`{audit['pitch_independence']['dive_up_component']['min']:.4f}` 至 "
        f"`{audit['pitch_independence']['dive_up_component']['max']:.4f}`"
        f"（极差 `{audit['pitch_independence']['dive_up_component']['spread']:.4f}`）",
        f"- 与相机俯仰的相关系数：下冲分量 `{audit['pitch_independence']['correlation_pitch_vs_dive_up_component']}`、"
        f"起始速度 `{audit['pitch_independence']['correlation_pitch_vs_launch_speed']}`",
    ])
    for key, entry in sorted(audit["pitch_independence"]["matched_time_trajectory"].items()):
        phase, _, time_label = key.partition("_t_")
        canonical_report.append(
            f"- {phase} 内同一真实秒 `t={time_label}`（{entry['trials']} 次）：位移极差 forward "
            f"`{entry['forward_cm_spread']:.2f}` / right `{entry['right_cm_spread']:.3f}` / up "
            f"`{entry['up_cm_spread']:.2f}` cm；与俯仰相关系数最大 "
            f"`{max(abs(entry[k]) for k in entry if k.startswith('correlation_')):.3f}`"
        )
    canonical_report.extend([
        f"- 计划 §9.2 点名的三个量（升高 / 距离 / 下冲角）与俯仰的相关："
        + "；".join(
            f"{name} `{entry['correlation_pitch']:+.3f}`（{entry['min']:.2f}–{entry['max']:.2f}）"
            for name, entry in audit['pitch_independence']['acceptance_quantities'].items()
        ),
        f"- 逐点通道（固定 t 取位置）最强相关 `{audit['pitch_independence']['strongest_absolute_correlation_pointwise']}`："
        "相位边界的亚帧抖动也会在单点通道上留下小相关，整段积分（上一行）才是判据。",
        f"- 结论：**在实录俯仰带内与俯仰无关**（三个点名量 |corr| ≤ "
        f"{audit['pitch_independence']['strongest_absolute_correlation_acceptance']:.3f}）；"
        f"下冲角约 {audit['pitch_independence']['dive_angle_deg']:.2f}°"
        f"（极差 {audit['pitch_independence']['dive_angle_spread_deg']:.2f}°）。",
        f"- **范围限制**：实录俯仰 {audit['pitch_independence']['camera_pitch_deg']['min']:.2f}°～"
        f"{audit['pitch_independence']['camera_pitch_deg']['max']:.2f}°，其中高于 0° 的只有 "
        f"{audit['pitch_independence']['camera_pitch_deg']['trials_above_zero']} 次"
        f"（与次高相隔 {audit['pitch_independence']['camera_pitch_deg']['gap_from_second_highest_deg']:.2f}°）——"
        "**向上瞄准的情景本录制没有覆盖**，不能由这 36 次外推；运行时合同本身来自 MHRise 实操，不是本脚本的结论。",
        "",
        "## 曲线口径",
        "",
        f"- 120 Hz 真实秒网格；每相位每条曲线从 `(0, 0, 0)` 起，键时间严格递增。",
        f"- 位置来自局部二次回归（半窗 {FIT_HALF_WINDOW_S*1000:.0f} ms，MAD 重加权），",
        "  速度/加速度是同一拟合的导数；残差按 120 Hz 键的**线性插值**统计，与运行时一致。",
        "  例外：首末键的拟合半窗只有单侧数据、导数不可辨识，这两个键的速度改由位置键斜率给出"
        "（203 起始速度因此从虚高的 3007 回到实录的 2412 cm/s）。",
    ])
    contact = audit_phases["201"]["contact_frames"]
    counterfactual = audit_phases["201"]["untrimmed_counterfactual"]
    canonical_report.extend([
        f"- 201 各次末样本是 **201→203 的换段帧**，拟合与残差都裁掉该样本。它**不总是**已经落地的接触帧："
        f"实测 {contact['resolved_contact_frames']} 次急折（偏离轨迹中位 "
        f"{contact['deviation_cm']['resolved_contact']['p50']:.2f} cm，主要是竖直），"
        f"{contact['still_descending_frames']} 次仍在正常下坠（偏离中位 "
        f"{contact['deviation_cm']['still_descending']['p50']:.2f} cm）。逐次分类见审计 JSON 的 `contact_frames.measurements`。",
        f"  保留这些帧的反事实（审计 `untrimmed_counterfactual`）：末键退到 "
        f"{counterfactual['last_key_s']:.4f} s，残差 max 从 {audit_phases['201']['residuals_cm']['euclidean']['max']:.2f} 涨到 "
        f"{counterfactual['residuals_cm']['max']:.2f} cm，且**同一尾段锚点的独立逐次拟合与模型的分歧**在竖直速度上达到 "
        f"{counterfactual['velocity_agreement_pct'].get('up', 0):.1f}%——换段帧会污染任何包含它们的拟合，这才是裁剪的依据。",
        "- 203 平地运行时竖直 `up_cm` 约定为 0，实测竖直留在 `up_measured_*` 列与审计 JSON。",
        "- P95 残差是**逐样本**统计（含候选之间本身的离散度），不是拟合内差；",
        "  超出规范域的样本记在 `samples_beyond_domain`，不混进残差。",
        "- `coverage_count` 与分位区间回答**两个不同问题**：前者是该键时刻相位仍在场的候选数，"
        "后者只用在该键附近真的采到样本的候选。尾部两者会分开（200 末键：25 在场 / 9 采到），要一起读。",
        "- 模型速度与录制自带速度的逐键差异（中位 |Δ|）："
        + "；".join(
            f"{motion} forward `{audit_phases[motion]['velocity_cross_check_cm_s']['per_axis']['forward']['median_abs_difference_cm_s']}`"
            f" / up `{audit_phases[motion]['velocity_cross_check_cm_s']['per_axis']['up']['median_abs_difference_cm_s']}` cm/s"
            for motion in CHAIN_IDS
        )
        + "。起手蹬地与落地冲击处的原始速度本身跳变，峰值不代表模型误差。",
        "",
        "## 已知离散与标记样本",
        "",
    ])
    for motion in CHAIN_IDS:
        phase = audit_phases[motion]
        flags = phase["flagged_trials"]
        canonical_report.append(
            f"- **{motion}**：偏离规范曲线超过 {OUTLIER_CM:g} cm 的候选："
            + ("无。" if not flags else "、".join(f"第 {index} 次" for index in flags) + "。")
        )
        if flags:
            top = phase["outliers"][0]
            canonical_report.append(
                f"  最大偏离 `{top['max_deviation_cm']:.2f} cm`（t=`{top['at_time_s']:.3f} s`）。"
                "该候选仍留在候选集与全样本导出里，MAD 重加权只按残差降权、不整体剔除；"
                "录制没有碰撞/地形事件列，因此只按轨迹记录，不编造原因。"
            )
    canonical_report.extend([
        "",
        "## 待办（不属于本生成器）",
        "",
        "- 三份 CurveVector 资产、Montage、GA 与 RTY 路由由 M3 生成；本文件只给数据。",
        "- 三个武器判定窗由 E1 由用户按实际接触摆放，本生成器不写碰撞时机。",
        "",
    ])
    canonical_report_path.write_text("\n".join(canonical_report), encoding="utf-8")

    return {
        "capture_id": capture_id,
        "complete_chains": len(chains),
        "clean_candidate_chains": counts["clean_candidate"],
        "suspected_external_push_chains": counts["suspected_external_push"],
        "recording_hitch_chains": counts["recording_hitch"],
        "all_rows": len(all_rows),
        "clean_rows": len(clean_rows),
        "validation": {motion: audit_phases[motion]["gate"] for motion in CHAIN_IDS},
        "paths": {
            "all": all_path,
            "clean": clean_path,
            "audit": audit_path,
            "notes": report_path,
            "canonical": canonical_paths,
            "audit_json": audit_json_path,
            "canonical_notes": canonical_report_path,
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture", type=Path, help="Specific *_samples.csv; defaults to newest recorder capture")
    parser.add_argument("--output-dir", type=Path, default=OUTPUT_DIR)
    parser.add_argument("--force", action="store_true",
                        help="Regenerate and overwrite existing outputs (the per-attempt export changes basis)")
    args = parser.parse_args()
    capture = args.capture
    if capture is None:
        candidates = sorted(recorder_dir().glob("*_samples.csv"), key=lambda path: path.stat().st_mtime)
        if not candidates:
            parser.error(f"No *_samples.csv files in {recorder_dir()}")
        capture = candidates[-1]
    if not capture.is_file():
        parser.error(f"Capture file does not exist: {capture}")
    try:
        summary = build_outputs(capture, args.output_dir, force=args.force)
    except (OSError, ValueError, KeyError) as exc:
        parser.error(str(exc))
    print(f"Capture: {capture}")
    print("Summary:", ", ".join(
        f"{key}={value}" for key, value in summary.items() if key != "paths" and key != "validation"
    ))
    for motion, gate in summary["validation"].items():
        print(f"  phase {motion}: P95={gate['p95_cm']:.2f} P99={gate['p99_cm']:.2f} "
              f"RMSE={gate['rmse_cm']:.2f} cm gate={'PASS' if gate['p95_pass'] else 'FAIL'} "
              f"(over limit: {gate['samples_over_limit']}/{gate['samples']} samples, "
              f"{gate['trials_over_limit']}/{gate['trials']} trials)")
    for name, path in summary["paths"].items():
        if name == "canonical":
            for motion, target in path.items():
                print(f"Canonical {motion}: {target}")
        else:
            print(f"{name}: {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
