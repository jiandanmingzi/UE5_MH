"""Read-only audit of MHW armor-part MOD3 bones against WP10 LMT tracks.

Run from Blender's bundled Python, for example:

    blender.exe --background --factory-startup --python-exit-code 1 \
      --python Scripts/MHWorld/audit_mhw_rig_coverage.py -- \
      --addon-path <mhworld-tools-addon> --lmt <w10_00.lmt> \
      --mod3 <f_body001_0000.mod3> --mod3 <f_helm001_0000.mod3> ...

The report uses raw MOD3 remap IDs and LMT track IDs. Bone-name remapping is
deliberately excluded because that table is heuristic and may be inaccurate.
No source assets are modified.
"""

import argparse
import json
from collections import defaultdict
from pathlib import Path
import sys


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--addon-path", type=Path, required=True)
    parser.add_argument("--mod3", type=Path, action="append", required=True)
    parser.add_argument("--lmt", type=Path, required=True)
    parser.add_argument("--slots", default="380,381,360")
    parser.add_argument("--probe-importer-bind-poses", action="store_true")
    return parser.parse_args(sys.argv[sys.argv.index("--") + 1:])


def load_mod3(path, Mod3Parser):
    parsed = Mod3Parser(path=str(path))
    bones, meshes = parsed.read()
    by_remap = defaultdict(list)
    rows = []
    for bone in bones:
        parent_index = bone["parent"]
        parent_remap = None if parent_index == 255 else bones[parent_index]["remap"]
        row = {
            "index": int(bone["id"]),
            "remap": int(bone["remap"]),
            "parent_remap": None if parent_remap is None else int(parent_remap),
            "global_matrix": [[float(value) for value in matrix_row]
                              for matrix_row in bone["global_matrix"]],
        }
        rows.append(row)
        by_remap[row["remap"]].append(row)
    return {
        "path": str(path),
        "version": parsed.version,
        "mesh_count": len(meshes),
        "bone_count": len(rows),
        "roots": [row["remap"] for row in rows if row["parent_remap"] is None],
        "duplicate_remaps": {
            str(remap): entries for remap, entries in by_remap.items() if len(entries) > 1
        },
        "bones": rows,
        "remaps": set(by_remap),
    }


def main():
    args = parse_args()
    if not (args.addon_path / "mhworld_tools_suite" / "__init__.py").is_file():
        raise FileNotFoundError(args.addon_path / "mhworld_tools_suite" / "__init__.py")
    for path in [*args.mod3, args.lmt]:
        if not path.is_file():
            raise FileNotFoundError(path)

    sys.path.insert(0, str(args.addon_path))
    from mhworld_tools_suite.mod3.mod3_parser import Mod3Parser
    from mhworld_tools_suite.lmt.lmt_parser import LmtParser

    parts = [load_mod3(path, Mod3Parser) for path in args.mod3]
    union = set().union(*(part["remaps"] for part in parts))
    memberships = defaultdict(list)
    parent_claims = defaultdict(lambda: defaultdict(set))
    transforms_by_remap = defaultdict(dict)
    for part in parts:
        label = Path(part["path"]).name
        for row in part["bones"]:
            memberships[row["remap"]].append(label)
            transforms_by_remap[row["remap"]][label] = row["global_matrix"]
            if row["parent_remap"] is not None:
                parent_claims[row["remap"]][label].add(row["parent_remap"])

    conflicts = {}
    transform_deltas = {}
    for remap, per_part in parent_claims.items():
        all_parents = set().union(*per_part.values())
        if len(all_parents) > 1:
            conflicts[str(remap)] = {
                "parents_by_part": {part: sorted(values) for part, values in per_part.items()},
                "distinct_non_root_parents": sorted(all_parents),
            }
    for remap, per_part in transforms_by_remap.items():
        if len(per_part) < 2:
            continue
        labels = sorted(per_part)
        reference = per_part[labels[0]]
        deltas = {}
        for label in labels[1:]:
            candidate = per_part[label]
            if len(candidate) != len(reference):
                deltas[label] = "matrix shape differs"
                continue
            deltas[label] = max(
                abs(reference[row][column] - candidate[row][column])
                for row in range(len(reference)) for column in range(len(reference[row]))
            )
        differing = {label: delta for label, delta in deltas.items()
                     if delta == "matrix shape differs" or delta > 1e-5}
        if differing:
            transform_deltas[str(remap)] = {
                "reference_part": labels[0],
                "max_abs_global_matrix_delta": differing,
            }

    parsed_actions = LmtParser(path=str(args.lmt)).read()
    by_slot = {int(action["name"].rsplit("_", 1)[-1]): action for action in parsed_actions}
    requested_slots = [int(value.strip()) for value in args.slots.split(",") if value.strip()]
    action_reports = []
    for slot in requested_slots:
        action = by_slot.get(slot)
        if action is None:
            action_reports.append({"slot": slot, "present": False})
            continue
        track_ids = sorted(int(track_id) for track_id in action["bone_action"] if int(track_id) >= 0)
        track_set = set(track_ids)
        action_reports.append({
            "slot": slot,
            "name": action["name"],
            "present": True,
            "track_count_including_root": len(action["bone_action"]),
            "non_root_track_count": len(track_ids),
            "non_root_track_ids": track_ids,
            "tracks_covered_by_part_union": len(track_set & union),
            "tracks_missing_from_part_union": sorted(track_set - union),
        })

    report = {
        "lmt": str(args.lmt),
        "parts": [{key: value for key, value in part.items() if key not in ("bones", "remaps")}
                  | {"unique_remaps": sorted(part["remaps"])} for part in parts],
        "union_bone_count": len(union),
        "union_remaps": sorted(union),
        "unique_remaps_by_part": {
            Path(part["path"]).name: sorted(part["remaps"] - set().union(*(
                other["remaps"] for other in parts if other is not part
            ))) for part in parts
        },
        "overlapping_remaps": {
            str(remap): sorted(set(part_names))
            for remap, part_names in memberships.items() if len(set(part_names)) > 1
        },
        "parent_conflicts": conflicts,
        "global_transform_deltas_over_1e-5": transform_deltas,
        "actions": action_reports,
        "interpretation_limit": (
            "Track-ID coverage proves only that these MOD3 parts contain matching numeric IDs. "
            "Matching global matrices are parser-space comparisons, not a substitute for "
            "checking the imported bind pose. This does not prove semantic bone mapping, "
            "full-body bind-pose correctness, "
            "or that the selected LMT slots are the intended runtime actions."
        ),
    }
    if args.probe_importer_bind_poses:
        import bpy
        from mhworld_tools_suite.mod3.mod3_loader import load_mod3 as load_mod3_asset

        imported = {}
        for path in args.mod3:
            part_name = path.stem
            objects = load_mod3_asset(
                str(path), LOD=0, fix_rotation=True, fix_scale=True,
                obj_name="Audit_" + part_name, rename_bones=False,
                connect_bones=False,
            )
            armature = next((obj for obj in objects if obj.type == "ARMATURE"), None)
            if armature is None:
                raise RuntimeError(f"No armature imported from {path}")
            imported[part_name] = armature

        body_name = "f_body001_0000"
        if body_name not in imported:
            raise RuntimeError("Bind-pose probe requires f_body001_0000.mod3 as a reference")
        reference = imported[body_name]
        ref_bones = {bone.name: bone for bone in reference.data.bones}
        bind_comparison = {}
        for part_name, armature in imported.items():
            if part_name == body_name:
                continue
            differences = []
            for bone in armature.data.bones:
                reference_bone = ref_bones.get(bone.name)
                if reference_bone is None:
                    continue
                same_parent = (
                    (bone.parent.name if bone.parent else None)
                    == (reference_bone.parent.name if reference_bone.parent else None)
                )
                delta = max(
                    abs(bone.matrix_local[row][column] - reference_bone.matrix_local[row][column])
                    for row in range(4) for column in range(4)
                )
                if not same_parent or delta > 1e-5:
                    differences.append({
                        "bone": bone.name,
                        "part_parent": bone.parent.name if bone.parent else None,
                        "body_parent": reference_bone.parent.name if reference_bone.parent else None,
                        "max_abs_armature_space_matrix_delta": delta,
                    })
            bind_comparison[part_name] = {
                "shared_bone_count": sum(1 for bone in armature.data.bones if bone.name in ref_bones),
                "nonmatching_rest_bones": differences,
            }
        report["blender_importer_bind_pose_comparison_to_body"] = bind_comparison
    print("MHW_RIG_COVERAGE=" + json.dumps(report, ensure_ascii=False))


if __name__ == "__main__":
    main()
