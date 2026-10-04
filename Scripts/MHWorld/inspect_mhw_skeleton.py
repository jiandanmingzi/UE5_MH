"""Read-only dump of the MHW reference MOD3 skeleton hierarchy."""

import argparse
import json
from pathlib import Path
import sys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--mod3", type=Path, required=True)
    parser.add_argument("--addon-path", type=Path, required=True)
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:])
    sys.path.insert(0, str(args.addon_path))
    from mhworld_tools_suite.mod3.mod3_parser import Mod3Parser
    from mhworld_tools_suite.common.bone_rename import bone_rename

    parsed = Mod3Parser(path=str(args.mod3))
    bones, meshes = parsed.read()
    rows = []
    for bone in bones:
        raw_name = "bone_" + str(bone["remap"]).zfill(3)
        parent_index = bone["parent"]
        parent_raw = None
        if parent_index != 255:
            parent = bones[parent_index]
            parent_raw = "bone_" + str(parent["remap"]).zfill(3)
        rows.append({
            "index": bone["id"],
            "name": raw_name,
            "mapped_name": bone_rename.get(raw_name, raw_name),
            "parent_index": None if parent_index == 255 else parent_index,
            "parent_name": parent_raw,
            "parent_mapped_name": bone_rename.get(parent_raw, parent_raw),
            "remap": bone["remap"],
            "length": bone["length"],
        })
    print("MHW_SKELETON=" + json.dumps({
        "source": str(args.mod3),
        "version": parsed.version,
        "bone_count": len(bones),
        "mesh_count": len(meshes),
        "roots": [row["name"] for row in rows if row["parent_index"] is None],
        "bones": rows,
    }, ensure_ascii=False))


if __name__ == "__main__":
    main()
