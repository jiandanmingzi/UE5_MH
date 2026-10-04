"""Read-only inventory check for candidate MHW hunter MOD3/LMT files.

Run in Blender's bundled Python, without opening or saving a scene:
blender --background --factory-startup --python-exit-code 1 --python <this file> -- --root <extracted MHW folder>
"""

import argparse
import json
from pathlib import Path
import sys

import addon_utils


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:])
    root = args.root.resolve()
    if not root.is_dir():
        raise FileNotFoundError(root)

    addon_utils.enable("mhworld_tools_suite", default_set=False, persistent=False)
    from mhworld_tools_suite.mod3.mod3_parser import Mod3Parser
    from mhworld_tools_suite.lmt.lmt_parser import LmtParser

    report = {"root": str(root), "models": [], "motions": []}
    model_remaps = {}
    for relative in (
        "chunkG0/pl/m_equip/pl001_0000/body/mod/m_body001_0000.mod3",
        "chunkG0/pl/f_equip/pl001_0000/body/mod/f_body001_0000.mod3",
    ):
        path = root / Path(relative)
        if not path.is_file():
            report["models"].append({"path": relative, "exists": False})
            continue
        parser = Mod3Parser(path=str(path))
        bones, meshes = parser.read()
        model_remaps["male" if "/m_equip/" in relative else "female"] = {
            bone["remap"] for bone in bones
        }
        report["models"].append({
            "path": relative,
            "exists": True,
            "version": parser.version,
            "bone_count": len(bones),
            "mesh_count": len(meshes),
            "mapped_bone_count": len({bone["remap"] for bone in bones}),
            "remap_id_min_max": [
                min(bone["remap"] for bone in bones),
                max(bone["remap"] for bone in bones),
            ],
        })

    for relative in (
        "chunkG3/hm/wp/wp10/mot/w10_00/w10_00.lmt",
        "chunkG0/hm/wp/wp10/mot/w10_00_fs/w10_00_fs.lmt",
        "chunkG0/hm/wp/wp10/mot/wp10_tree/wp10_tree.lmt",
        "chunkG0/hm/common/mot/co00_00/co00_00.lmt",
    ):
        path = root / Path(relative)
        if not path.is_file():
            report["motions"].append({"path": relative, "exists": False})
            continue
        parser = LmtParser(path=str(path))
        actions = parser.read()
        track_ids = set().union(
            *(set(action["bone_action"]) for action in actions)
        ) if actions else set()
        indexed_names = [action["name"] for action in actions]
        track_count_frequencies = {}
        for action in actions:
            count = len(action["bone_action"])
            track_count_frequencies[count] = track_count_frequencies.get(count, 0) + 1
        report["motions"].append({
            "path": relative,
            "exists": True,
            "version": parser.version,
            "entry_count": len(actions),
            "entry_name_samples": indexed_names[:6] + indexed_names[-3:],
            "track_count_frequencies": track_count_frequencies,
            "track_ids": sorted(track_ids),
            "unmapped_tracks_by_model": {
                sex: sorted(track_ids - remaps - {-1})
                for sex, remaps in model_remaps.items()
            },
        })

    print("MHGZ_MHW_ASSET_CANDIDATES=" + json.dumps(report, ensure_ascii=False))


if __name__ == "__main__":
    main()
