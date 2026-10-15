"""Execute a Python file in the matching open UE editor using Epic's client.

Use --probe for a read-only connection check. This launcher never opens an
editor, retries a write, or requires a feature-specific C++ bridge.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import uuid


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_ENGINE_ROOT = Path(r"C:\apps\UE5\UE_5.6")
REMOTE_MODULE_RELATIVE = Path(
    "Engine/Plugins/Experimental/PythonScriptPlugin/Content/Python/remote_execution.py"
)


def normalize_path(value):
    return os.path.abspath(os.path.normpath(str(value))).replace("\\", "/").rstrip("/").casefold()


def load_remote_execution(engine_root):
    path = Path(engine_root) / REMOTE_MODULE_RELATIVE
    if not path.is_file():
        raise FileNotFoundError(f"Epic Remote Execution client missing: {path}")
    spec = importlib.util.spec_from_file_location("mhgz_epic_remote_execution", path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def script_command(script, arguments):
    """Quote for Python, never for a shell; restore the editor's process state."""
    path = str(Path(script).resolve())
    return (
        "import runpy, sys\n"
        "_mhgz_saved_argv, _mhgz_saved_path = sys.argv, sys.path[:]\n"
        "try:\n"
        f"    sys.argv = {[path, *arguments]!r}\n"
        f"    sys.path.insert(0, {str(Path(path).parent)!r})\n"
        f"    runpy.run_path({path!r}, run_name='__main__')\n"
        "except SystemExit as _mhgz_exit:\n"
        "    if _mhgz_exit.code not in (None, 0):\n"
        "        raise\n"
        "finally:\n"
        "    sys.argv, sys.path[:] = _mhgz_saved_argv, _mhgz_saved_path\n"
    )


def run_session(args, remote, emit):
    client = remote.RemoteExecution(remote.RemoteExecutionConfig())
    stage = "discovery"
    emit({"stage": stage})
    try:
        client.start()
        deadline = time.monotonic() + args.discovery_timeout
        while True:
            nodes = list(client.remote_nodes)
            matches = [node for node in nodes if node.get("project_root") and
                       normalize_path(node["project_root"]) == normalize_path(args.project_root)]
            if len(matches) > 1:
                raise RuntimeError("Multiple editors match this project; close the extra instance")
            if matches:
                selected = matches[0]
                break
            if time.monotonic() >= deadline:
                return {"success": False, "stage": stage, "error_type": "EditorNotFound",
                        "error": "No matching editor answered discovery", "discovered": nodes}
            time.sleep(0.1)

        stage = "connection"
        emit({"stage": stage, "editor": selected})
        client.open_command_connection(selected["node_id"])
        stage = "execution"
        emit({"stage": stage})
        if args.probe:
            command = (
                "import json, unreal\n"
                "print(json.dumps({'project': unreal.Paths.project_dir(), "
                "'python_ready': True, 'ui_bridge': hasattr(unreal, 'MHGZUIAssetSetupLibrary')}))"
            )
        else:
            command = script_command(args.script, args.script_arg)
        # UE ExecuteStatement compiles with Python's 'single' mode. Its outer
        # statement must be one expression even when the payload is multi-line.
        wire_command = f"exec(compile({command!r}, '<mhgz_remote>', 'exec'))"
        result = client.run_command(wire_command, exec_mode=remote.MODE_EXEC_STATEMENT)
        return {"success": bool(result.get("success")), "stage": stage,
                "editor": selected, "command_result": result,
                "error_type": None if result.get("success") else "RemoteScriptError"}
    except Exception as exc:
        return {"success": False, "stage": stage, "error_type": type(exc).__name__,
                "error": str(exc), "outcome_unknown": stage == "execution"}
    finally:
        try:
            client.stop()
        except Exception as exc:
            emit({"stage": "cleanup", "warning": str(exc)})


def parse_events(output):
    if isinstance(output, bytes):
        output = output.decode("utf-8", errors="replace")
    events = []
    for line in (output or "").splitlines():
        try:
            value = json.loads(line)
            if isinstance(value, dict):
                events.append(value)
        except ValueError:
            continue
    return events


def run_worker(command, timeout):
    env = os.environ.copy()
    env["PYTHONIOENCODING"] = "utf-8"
    try:
        completed = subprocess.run(command, capture_output=True, text=True, encoding="utf-8",
                                   errors="replace", env=env, timeout=timeout, check=False)
        events = parse_events(completed.stdout)
        reports = [event["report"] for event in events if "report" in event]
        if reports:
            report = reports[-1]
        else:
            report = {"success": False, "stage": events[-1].get("stage", "startup") if events else "startup",
                      "error_type": "WorkerExited", "error": completed.stderr[-4000:]}
        report["worker_exit_code"] = completed.returncode
        if completed.returncode != 0:
            report["success"] = False
        return report
    except subprocess.TimeoutExpired as exc:
        events = parse_events(exc.stdout)
        stages = [event["stage"] for event in events if "stage" in event and event["stage"] != "cleanup"]
        stage = stages[-1] if stages else "startup"
        return {"success": False, "stage": stage, "error_type": "Timeout",
                "error": f"Launcher deadline exceeded ({timeout:g}s)",
                "outcome_unknown": stage == "execution",
                "note": "Editor-side execution is not cancelled. Check postconditions before retrying a write."}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("script", nargs="?", help="Python file to execute inside UE")
    parser.add_argument("--probe", action="store_true", help="Read-only project/Python capability check")
    parser.add_argument("--script-arg", action="append", default=[], help="Pass an argument to the script; use --script-arg=--flag")
    parser.add_argument("--project-root", type=Path, default=PROJECT_ROOT)
    parser.add_argument("--engine-root", type=Path, default=Path(os.environ.get("UE_ENGINE_ROOT", DEFAULT_ENGINE_ROOT)))
    parser.add_argument("--discovery-timeout", type=float, default=8.0)
    parser.add_argument("--timeout", type=float, default=120.0, help="Total launcher deadline, including connection and execution")
    parser.add_argument("--report", type=Path, help="JSON report; default Saved/Maintenance/ue_remote/<unique>.json")
    parser.add_argument("--worker", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    if bool(args.script) == args.probe:
        parser.error("Choose exactly one Python script or --probe")
    if args.timeout <= 0 or args.discovery_timeout <= 0:
        parser.error("Timeouts must be positive")
    args.project_root = args.project_root.resolve()
    if args.script:
        args.script = Path(args.script).resolve()
        if not args.script.is_file():
            parser.error(f"Script not found: {args.script}")
    emit = lambda event: print(json.dumps(event, ensure_ascii=False, default=str), flush=True)
    if args.worker:
        try:
            remote = load_remote_execution(args.engine_root)
            report = run_session(args, remote, emit)
        except Exception as exc:
            report = {"success": False, "stage": "startup", "error_type": type(exc).__name__, "error": str(exc)}
        emit({"report": report})
        return 0 if report["success"] else 1

    command = [sys.executable, str(Path(__file__).resolve()), "--worker",
               "--project-root", str(args.project_root), "--engine-root", str(args.engine_root),
               "--discovery-timeout", str(args.discovery_timeout)]
    command += ["--probe"] if args.probe else [str(args.script)]
    command += [f"--script-arg={value}" for value in args.script_arg]
    started = time.monotonic()
    report = run_worker(command, args.timeout)
    report.update({"schema_version": 1, "project_root": str(args.project_root),
                   "script": str(args.script) if args.script else None, "probe": args.probe,
                   "elapsed_seconds": round(time.monotonic() - started, 3)})
    report_path = args.report or args.project_root / "Saved/Maintenance/ue_remote" / (
        time.strftime("%Y%m%d-%H%M%S") + "-" + uuid.uuid4().hex[:8] + ".json")
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2, default=str), encoding="utf-8")
    print(f"UE remote: {'OK' if report['success'] else 'FAILED'}; stage={report.get('stage')}; "
          f"elapsed={report['elapsed_seconds']}s; report={report_path}")
    if report.get("error"):
        print(report["error"])
    result = report.get("command_result", {})
    for item in result.get("output", [])[-12:]:
        print(item.get("output", str(item)) if isinstance(item, dict) else item)
    if not report["success"] and result.get("result"):
        print(result["result"])
    if report.get("outcome_unknown"):
        print("Editor-side outcome unknown; check postconditions before retrying a write.")
    return 0 if report["success"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
