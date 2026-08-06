"""Audit explicitly listed Unreal ObjectRedirectors without modifying assets."""

from __future__ import annotations

import json
import os
import sys
import traceback
from datetime import datetime, timezone

import unreal


SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
DEFAULT_MANIFEST = os.path.join(SCRIPT_DIR, "phase5-redirectors.json")


def _argument_value(prefix: str, default: str) -> str:
    for argument in sys.argv[1:]:
        if argument.startswith(prefix):
            return argument[len(prefix) :]
    return default


MANIFEST_PATH = os.path.abspath(_argument_value("--manifest=", DEFAULT_MANIFEST))


def _load_manifest() -> dict:
    with open(MANIFEST_PATH, "r", encoding="utf-8") as handle:
        manifest = json.load(handle)

    if manifest.get("schema_version") != 1:
        raise RuntimeError("Unsupported redirector manifest schema_version")
    redirectors = manifest.get("redirectors")
    if not isinstance(redirectors, list) or not redirectors:
        raise RuntimeError("Redirector manifest contains no assets")

    seen: set[str] = set()
    for item in redirectors:
        path = item.get("path", "")
        destination = item.get("expected_destination", "")
        if not path.startswith("/Game/") or not destination.startswith("/Game/"):
            raise RuntimeError(f"Only /Game paths are allowed: {path}")
        if "." in path or "." in destination:
            raise RuntimeError("Manifest paths must omit object suffixes")
        if path in seen:
            raise RuntimeError(f"Duplicate redirector path: {path}")
        seen.add(path)
    return manifest


def _asset_class(asset_data: unreal.AssetData) -> str | None:
    if not asset_data.is_valid():
        return None
    try:
        return str(asset_data.asset_class_path.asset_name)
    except Exception:
        return str(asset_data.asset_class)


def _package_file(path: str) -> str:
    project_dir = os.path.abspath(unreal.Paths.project_dir())
    relative = path[len("/Game/") :].replace("/", os.sep)
    return os.path.join(project_dir, "Content", relative + ".uasset")


def _serialized_destination_match(path: str, destination: str) -> bool:
    package_file = _package_file(path)
    if not os.path.isfile(package_file):
        return False
    with open(package_file, "rb") as handle:
        data = handle.read()
    return destination.encode("utf-8") in data or destination.encode("utf-16-le") in data


def _inspect(item: dict) -> dict:
    path = item["path"]
    destination = item["expected_destination"]
    source_file_exists = os.path.isfile(_package_file(path))
    if source_file_exists:
        asset_data = unreal.EditorAssetLibrary.find_asset_data(path)
        asset_class = _asset_class(asset_data)
        registry_source_exists = asset_data.is_valid()
    else:
        asset_class = None
        registry_source_exists = False
    source_exists = registry_source_exists and source_file_exists
    destination_exists = unreal.EditorAssetLibrary.does_asset_exist(destination)
    referencers = (
        sorted(
            str(value)
            for value in unreal.EditorAssetLibrary.find_package_referencers_for_asset(
                path, False
            )
        )
        if source_exists
        else []
    )

    resolved_object = None
    if source_exists:
        loaded = unreal.EditorAssetLibrary.load_asset(path)
        if loaded is not None:
            resolved_object = loaded.get_path_name()
            if asset_class == "ObjectRedirector":
                try:
                    destination_object = loaded.get_editor_property(
                        "destination_object"
                    )
                    if destination_object is not None:
                        resolved_object = destination_object.get_path_name()
                except Exception:
                    pass

    resolved_destination = bool(
        resolved_object
        and (
            resolved_object == destination
            or resolved_object.startswith(destination + ".")
        )
    )
    serialized_destination_match = (
        _serialized_destination_match(path, destination) if source_exists else False
    )

    if not source_exists and destination_exists:
        status = "already_clean"
    elif (
        source_exists
        and destination_exists
        and asset_class == "ObjectRedirector"
        and not referencers
        and (resolved_destination or serialized_destination_match)
    ):
        status = "ready"
    else:
        status = "invalid"

    return {
        "path": path,
        "expected_destination": destination,
        "status": status,
        "asset_class": asset_class,
        "registry_source_exists": registry_source_exists,
        "source_file_exists": source_file_exists,
        "destination_exists": destination_exists,
        "resolved_object": resolved_object,
        "serialized_destination_match": serialized_destination_match,
        "referencers": referencers,
    }


def _write_report(manifest: dict, inspections: list[dict], outcome: str) -> str:
    project_dir = os.path.abspath(unreal.Paths.project_dir())
    report_dir = os.path.join(project_dir, "Saved", "AssetOrganization")
    os.makedirs(report_dir, exist_ok=True)
    report_path = os.path.join(report_dir, f"{manifest['batch']}-audit.json")
    report = {
        "batch": manifest["batch"],
        "mode": "audit",
        "outcome": outcome,
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "manifest": MANIFEST_PATH,
        "summary": {
            status: sum(1 for item in inspections if item["status"] == status)
            for status in ("ready", "already_clean", "invalid")
        },
        "redirectors": inspections,
    }
    with open(report_path, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(report, handle, ensure_ascii=False, indent=2)
        handle.write("\n")
    return report_path


def main() -> None:
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.wait_for_completion()
    manifest = _load_manifest()
    inspections = [_inspect(item) for item in manifest["redirectors"]]
    invalid = [item for item in inspections if item["status"] == "invalid"]
    if invalid:
        report_path = _write_report(manifest, inspections, "preflight_failed")
        raise RuntimeError(f"Redirector preflight failed. Report: {report_path}")

    outcome = (
        "verified_clean"
        if all(item["status"] == "already_clean" for item in inspections)
        else "ready"
    )

    report_path = _write_report(manifest, inspections, outcome)
    unreal.log(
        f"[AssetOrganization] Redirector result: {outcome}; report={report_path}"
    )


try:
    main()
except Exception:
    unreal.log_error("[AssetOrganization] Redirector audit failed")
    unreal.log_error(traceback.format_exc())
    raise
