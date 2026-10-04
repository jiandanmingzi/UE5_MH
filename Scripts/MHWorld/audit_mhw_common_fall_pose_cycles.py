"""Rank repeated-pose windows in an MHW LMT.

Blender-only, read-only audit. It samples the same MOD3 rig for both LMTs,
normalizes component poses to the root bone, and writes JSON under Saved.
It does not export FBX, save a .blend, or touch Unreal Content.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import sys

import bpy
import numpy as np


TOOL_VERSION = "mhw-lmt-cyclic-window-audit/3"
PROJECT = Path(__file__).resolve().parents[2]
INVENTORY = PROJECT / "Saved/AssetProbe/DescendingThrust/common_lmt_action_inventory.json"
OUT = PROJECT / "Saved/AssetProbe/DescendingThrust/common_lmt_cyclic_window_candidates.json"
BODY_EXCLUDES = ("finger", "thumb", "_ik", "_t", "_w", "lookat", "parts")
BODY_TOKENS = ("waist", "cog", "spine", "neck", "head", "clavicle", "shoulder",
               "upperarm", "forearm", "arm_", "hand", "leg", "thigh", "calf",
               "foot", "weapon_00")


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--addon-path", type=Path, required=True)
    parser.add_argument("--mod3", type=Path, required=True)
    parser.add_argument("--lmt", "--common-lmt", dest="lmt", type=Path, required=True)
    parser.add_argument("--inventory", type=Path, default=INVENTORY)
    parser.add_argument("--output", type=Path, default=OUT)
    parser.add_argument("--preview-output", type=Path,
                        help="Optional JSON of sampled pose points for offline contact-sheet review.")
    return parser.parse_args(sys.argv[sys.argv.index("--") + 1:])


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def distance(a, b):
    shared = a.keys() & b.keys()
    positions, rotations = [], []
    for name in shared:
        pa, qa = a[name]
        pb, qb = b[name]
        positions.append(math.sqrt(sum((x - y) ** 2 for x, y in zip(pa, pb))) * 100.0)
        dot = min(1.0, max(-1.0, abs(sum(x * y for x, y in zip(qa, qb)))))
        rotations.append(math.degrees(2.0 * math.acos(dot)))
    if not positions:
        return (float("inf"), float("inf"), float("inf"), float("inf"))
    return sum(positions) / len(positions), max(positions), sum(rotations) / len(rotations), max(rotations)


def sample_action(rig, scene, action, bone_rename):
    if not action.slots:
        raise RuntimeError(f"Action has no slot: {action.name}")
    rig.animation_data_create()
    rig.animation_data.action = action
    rig.animation_data.action_slot = action.slots[0]
    start = int(math.floor(action.frame_range[0]))
    end = int(math.ceil(action.frame_range[1]))
    scene.frame_start, scene.frame_end = start, end
    samples = []
    for frame in range(start, end + 1):
        scene.frame_set(frame)
        bpy.context.view_layer.update()
        evaluated = rig.evaluated_get(bpy.context.evaluated_depsgraph_get())
        pose_bones = evaluated.pose.bones
        root = next((bone for bone in pose_bones if bone.parent is None), None)
        root_inverse = root.matrix.inverted() if root else None
        bones = {}
        for bone in pose_bones:
            name = bone.name
            mapped_name = bone_rename.get(name, name)
            lowered = mapped_name.lower()
            if (bone.parent is None or not any(token in lowered for token in BODY_TOKENS)
                    or any(token in lowered for token in BODY_EXCLUDES)):
                continue
            transform = root_inverse @ bone.matrix if root_inverse else bone.matrix.copy()
            position = tuple(float(value) for value in transform.translation)
            rotation = transform.to_quaternion()
            quaternion = (float(rotation.x), float(rotation.y), float(rotation.z), float(rotation.w))
            bones[mapped_name] = (position, quaternion)
        samples.append({"frame": frame - start, "source_blender_frame": frame, "bones": bones})
    return samples


def rank_cycles(samples):
    count = len(samples)
    if count < 18:
        return [], 0
    bone_names = sorted(samples[0]["bones"])
    positions = np.asarray([[samples[frame]["bones"][name][0] for name in bone_names]
                            for frame in range(count)], dtype=np.float64) * 100.0
    rotations = np.asarray([[samples[frame]["bones"][name][1] for name in bone_names]
                            for frame in range(count)], dtype=np.float64)
    min_start = max(1, int(math.ceil(count * 0.08)))
    candidates = []
    for period in range(6, (count - min_start) // 2 + 1):
        position_delta = positions[period:] - positions[:-period]
        position_errors = np.linalg.norm(position_delta, axis=2)
        rotation_dot = np.clip(np.abs(np.sum(rotations[period:] * rotations[:-period], axis=2)), 0.0, 1.0)
        rotation_errors = np.degrees(2.0 * np.arccos(rotation_dot))
        pair_mean_position = np.mean(position_errors, axis=1)
        pair_max_position = np.max(position_errors, axis=1)
        pair_mean_rotation = np.mean(rotation_errors, axis=1)
        pair_max_rotation = np.max(rotation_errors, axis=1)
        for start in range(min_start, count - 2 * period + 1):
            repeat_count = 3 if start + 3 * period <= count else 2
            compared_ranges = [(start + cycle * period, start + (cycle + 1) * period)
                               for cycle in range(repeat_count - 1)]
            mean_pos = float(np.mean(np.concatenate(
                [pair_mean_position[a:b] for a, b in compared_ranges])))
            max_pos = float(max(np.max(pair_max_position[a:b]) for a, b in compared_ranges))
            mean_rot = float(np.mean(np.concatenate(
                [pair_mean_rotation[a:b] for a, b in compared_ranges])))
            max_rot = float(max(np.max(pair_max_rotation[a:b]) for a, b in compared_ranges))
            candidates.append({
                "advisory_rank_cm_plus_0p20_deg": mean_pos + 0.2 * mean_rot,
                "start_frame_inclusive": start,
                "end_frame_exclusive": start + period,
                "period_frames": period,
                "repeated_cycles_available": repeat_count,
                "mean_repeated_position_error_cm": mean_pos,
                "max_repeated_position_error_cm": max_pos,
                "mean_repeated_rotation_error_deg": mean_rot,
                "max_repeated_rotation_error_deg": max_rot,
            })
    candidates.sort(key=lambda row: row["advisory_rank_cm_plus_0p20_deg"])
    for candidate in candidates[:8]:
        seam = distance(
            samples[candidate["end_frame_exclusive"] - 1]["bones"],
            samples[candidate["start_frame_inclusive"]]["bones"],
        )
        candidate.update({
            "wrap_mean_position_error_cm": seam[0],
            "wrap_max_position_error_cm": seam[1],
            "wrap_mean_rotation_error_deg": seam[2],
            "wrap_max_rotation_error_deg": seam[3],
        })
    return candidates[:8], len(candidates)


def main():
    args = parse_args()
    for path in (args.mod3, args.lmt, args.inventory):
        if not path.is_file():
            raise FileNotFoundError(path)
    if args.output.exists() and not args.preview_output:
        raise FileExistsError(f"Refusing to overwrite audit output: {args.output}")
    if args.preview_output and args.preview_output.exists():
        raise FileExistsError(f"Refusing to overwrite pose-preview output: {args.preview_output}")
    inventory = json.loads(args.inventory.read_text(encoding="utf-8"))
    if inventory["sources"]["lmt"]["sha256"] != sha256(args.lmt):
        raise RuntimeError("LMT hash differs from the inventory; regenerate the inventory first.")
    sys.path.insert(0, str(args.addon_path))
    from mhworld_tools_suite.mod3.mod3_loader import load_mod3
    from mhworld_tools_suite.common.bone_rename import bone_rename
    from mhworld_tools_suite.lmt.lmt_loader import load_lmt_data
    from mhworld_tools_suite.lmt.lmt_parser import LmtParser

    imported = load_mod3(str(args.mod3), LOD=0, fix_rotation=True, fix_scale=True,
                         obj_name="MHW_LMT_PoseAudit", rename_bones=False,
                         connect_bones=False)
    rig = next((obj for obj in imported if obj.type == "ARMATURE"), None)
    if rig is None:
        raise RuntimeError("MOD3 import produced no armature.")

    lmt_actions = {int(item["name"].rsplit("_", 1)[-1]): item for item in LmtParser(str(args.lmt)).read()}

    selected = [row for row in inventory["actions"]
                if row["slot"] in lmt_actions
                and row["last_key_frame"] is not None
                and 35 <= row["last_key_frame"] <= 220
                and (row["root_position_raw_delta_max_component"] or 0.0) < 5.0
                and row["tracks_with_raw_component_change"] >= 15]
    results = []
    preview_actions = []
    edge_set = set()
    if args.preview_output:
        mapped_bones = {}
        for bone in rig.data.bones:
            mapped_name = bone_rename.get(bone.name, bone.name)
            lowered = mapped_name.lower()
            if (bone.parent is None or not any(token in lowered for token in BODY_TOKENS)
                    or any(token in lowered for token in BODY_EXCLUDES)):
                continue
            mapped_bones[bone.name] = mapped_name
        for bone in rig.data.bones:
            mapped_child = mapped_bones.get(bone.name)
            ancestor = bone.parent
            mapped_parent = None
            while ancestor and mapped_parent is None:
                mapped_parent = mapped_bones.get(ancestor.name)
                ancestor = ancestor.parent
            if mapped_child and mapped_parent and mapped_child != mapped_parent:
                edge_set.add((mapped_parent, mapped_child))
    edges = sorted([list(edge) for edge in edge_set])

    for metadata in selected:
        slot = metadata["slot"]
        action = load_lmt_data(lmt_actions[slot], {}, {}, rename_bones=False, set_fake_user=True)
        samples = sample_action(rig, bpy.context.scene, action, bone_rename)
        top_cycles, cycle_count = rank_cycles(samples)
        results.append({
            "slot": slot,
            "action_name": metadata["action_name"],
            "last_key_frame": metadata["last_key_frame"],
            "track_count": metadata["track_count"],
            "tracks_with_raw_component_change": metadata["tracks_with_raw_component_change"],
            "root_position_raw_delta_max_component": metadata["root_position_raw_delta_max_component"],
            "best_cycle_candidates": top_cycles,
            "best_candidate_count": cycle_count,
        })
        if args.preview_output:
            frame_indices = sorted(set(round(index * (len(samples) - 1) / 4) for index in range(5)))
            preview_actions.append({
                "slot": slot,
                "action_name": metadata["action_name"],
                "frame_count": len(samples),
                "best_cycle_candidates": top_cycles,
                "preview_frames": [
                    {
                        "frame": samples[index]["frame"],
                        "normalized_time": index / max(1, len(samples) - 1),
                        "points_cm": {
                            name: [float(component * 100.0) for component in transform[0]]
                            for name, transform in samples[index]["bones"].items()
                        },
                    }
                    for index in frame_indices
                ],
            })
        print(f"AUDITED_LMT_SLOT={slot} FRAMES={len(samples)} CYCLES={cycle_count}")

    results.sort(key=lambda row: (
        row["best_cycle_candidates"][0]["advisory_rank_cm_plus_0p20_deg"]
        if row["best_cycle_candidates"] else float("inf")))
    report = {
        "status": "COMPLETE_LMT_CYCLIC_WINDOW_SCREEN_REQUIRES_SEMANTIC_AND_VISUAL_REVIEW",
        "tool_version": TOOL_VERSION,
        "blender_version": bpy.app.version_string,
        "python_version": sys.version.split()[0],
        "sources": {
            "mod3": {"path": str(args.mod3), "sha256": sha256(args.mod3)},
            "lmt": {"path": str(args.lmt), "sha256": sha256(args.lmt)},
            "inventory": {"path": str(args.inventory), "sha256": sha256(args.inventory)},
        },
        "source_label": args.lmt.stem,
        "method": {
            "candidate_pool": len(selected),
            "pose_space": "Blender evaluated MOD3 component pose relative to the primary root; no Retargeter or game-motion curve.",
            "pose_bone_filter": {"included_mapped_name_tokens": list(BODY_TOKENS),
                                 "excluded_mapped_name_tokens": list(BODY_EXCLUDES)},
            "cycle_rule": "Periods >=6 frames with at least two repetitions; ranking advisory only, no accept threshold.",
            "limits": ["Action slot semantics are not inferred from numeric IDs; no candidate is asserted to be a falling action.",
                       "A repeated/static pose window is only an animation-cycle hint, not evidence of the in-game falling phase.",
                       "Root motion and game-side trajectory curves are not included in this pose-cycle score.",
                       "Semantic identification and visual review are still required; no FBX or Unreal asset was generated."],
        },
        "candidate_count": len(results),
        "candidates": results,
    }
    if not args.output.exists():
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
        print(f"MHW_LMT_CYCLIC_WINDOW_AUDIT={args.output}")
    if args.preview_output:
        preview_report = {
            "status": "POSE_SAMPLES_FOR_VISUAL_SEMANTIC_REVIEW_ONLY",
            "tool_version": TOOL_VERSION,
            "blender_version": bpy.app.version_string,
            "python_version": sys.version.split()[0],
            "sources": report["sources"],
            "source_label": report["source_label"],
            "pose_space": "MOD3 evaluated pose relative to primary root; point positions in cm.",
            "views": ["XZ front projection", "YZ side projection"],
            "limits": [
                "The numeric slot label does not identify gameplay semantics.",
                "A contact sheet is a visual shortlist aid, not an approval or proof of in-game falling use.",
                "Root motion and game-side movement curves are not included.",
                "No FBX, .blend, or Unreal asset was generated.",
            ],
            "bone_edges": edges,
            "actions": preview_actions,
        }
        args.preview_output.parent.mkdir(parents=True, exist_ok=True)
        args.preview_output.write_text(json.dumps(preview_report, ensure_ascii=False, indent=2), encoding="utf-8")
        print(f"MHW_LMT_POSE_PREVIEW_DATA={args.preview_output}")
    for row in results[:12]:
        cycle = row["best_cycle_candidates"][0] if row["best_cycle_candidates"] else {}
        print("TOP_LMT_CYCLE_CANDIDATE " + json.dumps({
            "slot": row["slot"], "action": row["action_name"],
            "start": cycle.get("start_frame_inclusive"), "end": cycle.get("end_frame_exclusive"),
            "repeat_error_cm": cycle.get("mean_repeated_position_error_cm"),
            "repeat_error_deg": cycle.get("mean_repeated_rotation_error_deg"),
            "wrap_cm": cycle.get("wrap_max_position_error_cm"),
            "wrap_deg": cycle.get("wrap_max_rotation_error_deg"),
        }, ensure_ascii=False))


if __name__ == "__main__":
    main()
