"""Run the reviewed weapon-move asset rename manifest through AssetTools.

The migration remains read-only unless APPLY is explicitly changed to True.
Unreal's Python commandlet does not forward arbitrary switches to sys.argv.
"""

from __future__ import annotations

import os
import runpy
import sys

import unreal


PROJECT_DIR = unreal.Paths.project_dir()
ORGANIZER = os.path.join(
    PROJECT_DIR, "Scripts", "AssetOrganization", "organize_assets.py"
)
MANIFEST = os.path.join(
    PROJECT_DIR,
    "Scripts",
    "AssetOrganization",
    "Manifests",
    "weapon_move_asset_pinyin.json",
)
APPLY = False

sys.argv = [ORGANIZER, f"--manifest={MANIFEST}"]
if APPLY:
    sys.argv.append("--apply")

runpy.run_path(ORGANIZER, run_name="__main__")
