"""Compatibility entry point for the M6 UI setup and read-only probe."""

import argparse
from pathlib import Path
import sys

SCRIPTS_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS_ROOT))
import ue_remote


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("script", nargs="?")
    parser.add_argument("--probe", action="store_true", help="Read-only UI references/capability audit")
    parser.add_argument("--timeout", type=float, default=8.0, help="Legacy discovery timeout; default 8 seconds")
    parser.add_argument("--deadline", type=float, default=120.0, help="Total launcher deadline")
    parser.add_argument("--engine-root", type=Path)
    parser.add_argument("--project-root", type=Path)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--script-arg", action="append", default=[])
    args = parser.parse_args(argv)
    folder = Path(__file__).resolve().parent
    script = folder / "probe_m6_ui.py" if args.probe else args.script or folder / "setup_m6_ui_widgets.py"
    forwarded = [str(script), "--discovery-timeout", str(args.timeout), "--timeout", str(args.deadline)]
    for option in ("engine_root", "project_root", "report"):
        value = getattr(args, option)
        if value is not None:
            forwarded.extend(["--" + option.replace("_", "-"), str(value)])
    forwarded.extend(f"--script-arg={value}" for value in args.script_arg)
    return ue_remote.main(forwarded)


if __name__ == "__main__":
    raise SystemExit(main())
