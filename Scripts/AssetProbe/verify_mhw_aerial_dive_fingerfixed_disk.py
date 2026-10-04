"""Reload and verify the saved finger-fixed MHW dive review animations.

Run with UnrealEditor-Cmd and PythonScriptPlugin enabled. The root/body
verification and finger-motion audit are read-only for Content assets; their
reports are written separately under Saved/AssetProbe.
"""

from pathlib import Path
import runpy


SCRIPT_DIR = Path(__file__).parent
runpy.run_path(str(SCRIPT_DIR / "verify_mhw_aerial_dive_root_motion.py"),
               run_name="__main__")
runpy.run_path(str(SCRIPT_DIR / "audit_mhw_aerial_dive_finger_retarget.py"),
               run_name="__main__")
