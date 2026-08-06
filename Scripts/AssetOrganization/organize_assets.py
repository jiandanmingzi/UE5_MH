"""Dry-run or apply an Unreal asset migration manifest.

Run from UnrealEditor-Cmd with PythonScriptPlugin and
EditorScriptingUtilities enabled. The default mode is read-only. Pass
``--apply`` to invoke AssetTools.RenameAssets after all preflight checks pass.
"""

from __future__ import annotations

import hashlib
import json
import os
import sys
import traceback
from datetime import datetime, timezone

import unreal


SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
DEFAULT_MANIFEST = os.path.join(SCRIPT_DIR, "phase1.json")
APPLY = "--apply" in sys.argv


def _argument_value(prefix: str, default: str) -> str:
    for argument in sys.argv[1:]:
        if argument.startswith(prefix):
            return argument[len(prefix) :]
    return default


MANIFEST_PATH = os.path.abspath(_argument_value("--manifest=", DEFAULT_MANIFEST))


def _log(message: str) -> None:
    unreal.log(f"[AssetOrganization] {message}")


def _load_manifest() -> dict:
    with open(MANIFEST_PATH, "r", encoding="utf-8") as handle:
        manifest = json.load(handle)

    if manifest.get("schema_version") != 1:
        raise RuntimeError("Unsupported migration manifest schema_version")

    moves = manifest.get("moves")
    if not isinstance(moves, list) or not moves:
        raise RuntimeError("Migration manifest contains no moves")

    sources: set[str] = set()
    destinations: set[str] = set()
    protected_paths = tuple(manifest.get("protected_paths", []))

    for move in moves:
        source = move.get("source", "")
        destination = move.get("destination", "")
        if not source.startswith("/Game/") or not destination.startswith("/Game/"):
            raise RuntimeError(f"Only /Game assets may be migrated: {source} -> {destination}")
        if "." in source or "." in destination:
            raise RuntimeError(f"Manifest paths must be package paths without object suffixes: {source}")
        if source == destination:
            raise RuntimeError(f"Source and destination are identical: {source}")
        if source in sources or destination in destinations:
            raise RuntimeError(f"Duplicate source or destination in manifest: {source} -> {destination}")
        if any(source == path or source.startswith(path + "/") for path in protected_paths):
            raise RuntimeError(f"Manifest attempts to move protected path: {source}")
        sources.add(source)
        destinations.add(destination)

    return manifest


def _asset_class(asset_path: str) -> str | None:
    asset = unreal.EditorAssetLibrary.load_asset(asset_path)
    if asset is None:
        return None
    return asset.get_class().get_name()


def _inspect_move(move: dict) -> dict:
    source = move["source"]
    destination = move["destination"]
    source_exists = unreal.EditorAssetLibrary.does_asset_exist(source)
    destination_exists = unreal.EditorAssetLibrary.does_asset_exist(destination)

    if source_exists and destination_exists:
        status = "collision"
        inspected_path = source
    elif source_exists:
        status = "pending"
        inspected_path = source
    elif destination_exists:
        status = "already_moved"
        inspected_path = destination
    else:
        status = "missing"
        inspected_path = source

    referencers: list[str] = []
    if source_exists or destination_exists:
        referencers = sorted(
            str(path)
            for path in unreal.EditorAssetLibrary.find_package_referencers_for_asset(
                inspected_path, False
            )
        )

    return {
        "source": source,
        "destination": destination,
        "reason": move.get("reason", ""),
        "status": status,
        "asset_class": _asset_class(inspected_path),
        "referencers": referencers,
        "expected_referencers_after_move": move.get(
            "expected_referencers_after_move", []
        ),
    }


def _verify_expected_referencers(inspections: list[dict]) -> None:
    failures = []
    for item in inspections:
        expected = set(item["expected_referencers_after_move"])
        actual = set(item["referencers"])
        missing = sorted(expected - actual)
        if missing:
            failures.append(
                f"{item['destination']} is missing referencers: {', '.join(missing)}"
            )
    if failures:
        raise RuntimeError(
            "Post-move referencer verification failed:\n" + "\n".join(failures)
        )


def _sha256(path: str) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _verify_pending_backups(manifest: dict, inspections: list[dict]) -> None:
    pending = [item for item in inspections if item["status"] == "pending"]
    if not pending:
        return

    backup_directory = manifest.get("backup_directory", "")
    if not backup_directory:
        raise RuntimeError("Apply requires manifest.backup_directory")

    project_dir = os.path.abspath(unreal.Paths.project_dir())
    backup_root = os.path.abspath(os.path.join(project_dir, backup_directory))
    if not os.path.isdir(backup_root):
        raise RuntimeError(f"Backup directory does not exist: {backup_root}")

    failures = []
    for item in pending:
        relative_package = item["source"][len("/Game/") :].replace("/", os.sep)
        matched = False
        for extension in (".uasset", ".umap"):
            source_file = os.path.join(
                project_dir, "Content", relative_package + extension
            )
            backup_file = os.path.join(
                backup_root, "Content", relative_package + extension
            )
            if os.path.isfile(source_file):
                matched = True
                if not os.path.isfile(backup_file):
                    failures.append(f"Missing backup: {backup_file}")
                elif _sha256(source_file) != _sha256(backup_file):
                    failures.append(f"Backup hash mismatch: {item['source']}")
                break
        if not matched:
            failures.append(f"Source package file not found: {item['source']}")

    if failures:
        raise RuntimeError("Backup verification failed:\n" + "\n".join(failures))


def _rename_pending(inspections: list[dict]) -> None:
    pending = [item for item in inspections if item["status"] == "pending"]
    if not pending:
        _log("No pending assets; manifest is already applied.")
        return

    rename_data = []
    loaded_assets = []
    for item in pending:
        asset = unreal.EditorAssetLibrary.load_asset(item["source"])
        if asset is None:
            raise RuntimeError(f"Failed to load source asset: {item['source']}")
        loaded_assets.append(asset)
        new_package_path, new_name = item["destination"].rsplit("/", 1)
        data = unreal.AssetRenameData()
        data.set_editor_property("asset", asset)
        data.set_editor_property("new_package_path", new_package_path)
        data.set_editor_property("new_name", new_name)
        rename_data.append(data)

    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    if not asset_tools.rename_assets(rename_data):
        raise RuntimeError("AssetTools.RenameAssets reported failure")

    failures = []
    for item in pending:
        source_exists = unreal.EditorAssetLibrary.does_asset_exist(item["source"])
        destination_exists = unreal.EditorAssetLibrary.does_asset_exist(item["destination"])
        if source_exists or not destination_exists:
            failures.append(
                f"{item['source']} -> {item['destination']} "
                f"(source_exists={source_exists}, destination_exists={destination_exists})"
            )
    if failures:
        raise RuntimeError("Post-move verification failed:\n" + "\n".join(failures))


def _write_report(manifest: dict, inspections: list[dict], outcome: str) -> str:
    project_dir = os.path.abspath(unreal.Paths.project_dir())
    report_dir = os.path.join(project_dir, "Saved", "AssetOrganization")
    os.makedirs(report_dir, exist_ok=True)
    mode = "apply" if APPLY else "dry-run"
    report_path = os.path.join(report_dir, f"{manifest['batch']}-{mode}.json")
    report = {
        "batch": manifest["batch"],
        "mode": mode,
        "outcome": outcome,
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "manifest": MANIFEST_PATH,
        "summary": {
            status: sum(1 for item in inspections if item["status"] == status)
            for status in ("pending", "already_moved", "collision", "missing")
        },
        "moves": inspections,
    }
    with open(report_path, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(report, handle, ensure_ascii=False, indent=2)
        handle.write("\n")
    return report_path


def main() -> None:
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.wait_for_completion()

    manifest = _load_manifest()
    inspections = [_inspect_move(move) for move in manifest["moves"]]
    invalid = [
        item for item in inspections if item["status"] in ("collision", "missing")
    ]
    if invalid:
        report_path = _write_report(manifest, inspections, "preflight_failed")
        details = ", ".join(f"{item['status']}: {item['source']}" for item in invalid)
        raise RuntimeError(f"Preflight failed ({details}). Report: {report_path}")

    if APPLY:
        had_pending_moves = any(item["status"] == "pending" for item in inspections)
        _verify_pending_backups(manifest, inspections)
        _rename_pending(inspections)
        inspections = [_inspect_move(move) for move in manifest["moves"]]
        # Asset Registry reverse-dependency data can remain stale in the same
        # editor process that performed a batch rename. Verify expected
        # referencers only in a fresh, idempotent --apply run.
        if had_pending_moves:
            outcome = "applied_needs_fresh_referencer_verification"
        else:
            _verify_expected_referencers(inspections)
            outcome = "verified"
    else:
        outcome = "ready"

    report_path = _write_report(manifest, inspections, outcome)
    summary = {
        status: sum(1 for item in inspections if item["status"] == status)
        for status in ("pending", "already_moved", "collision", "missing")
    }
    _log(f"Result: {outcome}; summary={summary}; report={report_path}")


try:
    main()
except Exception:
    unreal.log_error("[AssetOrganization] Migration failed")
    unreal.log_error(traceback.format_exc())
    raise
