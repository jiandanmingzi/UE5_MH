"""Run the reviewed M6 Montage rename manifest through the shared UE asset mover.

The Unreal Python commandlet does not forward arbitrary command-line switches
into Python's ``sys.argv``, so this tiny project-local wrapper supplies them.
It is read-only by default; set APPLY=True only for the reviewed migration run.
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
    "m6_awakened_kinsect_montage_pinyin.json",
)
APPLY = False

sys.argv = [ORGANIZER, f"--manifest={MANIFEST}"]
if APPLY:
    sys.argv.append("--apply")

runpy.run_path(ORGANIZER, run_name="__main__")
