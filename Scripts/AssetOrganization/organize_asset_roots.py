"""Move complete Unreal asset roots with count and backup safeguards.

The manifest records root-to-root rules rather than hundreds of repetitive
package entries. Dry-run is the default. Pass ``--apply`` only after the
generated report and backup have been reviewed.
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
DEFAULT_MANIFEST = os.path.join(SCRIPT_DIR, "phase10-template-system-assets.json")
APPLY = "--apply" in sys.argv


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
        raise RuntimeError("Unsupported root migration manifest schema_version")
    rules = manifest.get("root_moves")
    if not isinstance(rules, list) or not rules:
        raise RuntimeError("Root migration manifest contains no root_moves")
    for rule in rules:
        source = rule.get("source_root", "").rstrip("/")
        destination = rule.get("destination_root", "").rstrip("/")
        expected = rule.get("expected_asset_count")
        if not source.startswith("/Game/") or not destination.startswith("/Game/"):
            raise RuntimeError(f"Only /Game roots may be migrated: {source} -> {destination}")
        if source == destination or source.startswith(destination + "/") or destination.startswith(source + "/"):
            raise RuntimeError(f"Root migration paths overlap: {source} -> {destination}")
        if not isinstance(expected, int) or expected <= 0:
            raise RuntimeError(f"Invalid expected_asset_count for {source}")
        rule["source_root"] = source
        rule["destination_root"] = destination
    return manifest


def _assets_under(registry, root: str, excluded: set[str]) -> list[unreal.AssetData]:
    return sorted(
        (
            item
            for item in registry.get_assets_by_path(root, recursive=True)
            if str(item.package_name) not in excluded
            and str(item.asset_class_path.asset_name) != "ObjectRedirector"
        ),
        key=lambda item: str(item.package_name),
    )


def _expand_rules(manifest: dict, registry) -> tuple[list[dict], list[dict]]:
    moves: list[dict] = []
    rule_reports: list[dict] = []
    destinations: set[str] = set()
    for rule in manifest["root_moves"]:
        source_root = rule["source_root"]
        destination_root = rule["destination_root"]
        expected = rule["expected_asset_count"]
        excluded = set(rule.get("exclude_packages", []))
        source_assets = _assets_under(registry, source_root, excluded)
        destination_assets = _assets_under(registry, destination_root, set())

        if len(source_assets) == expected:
            state = "pending"
            package_pairs = [
                (
                    str(item.package_name),
                    destination_root + str(item.package_name)[len(source_root) :],
                    str(item.asset_class_path.asset_name),
                )
                for item in source_assets
            ]
        elif len(source_assets) == 0 and len(destination_assets) == expected:
            state = "already_moved"
            package_pairs = [
                (
                    source_root + str(item.package_name)[len(destination_root) :],
                    str(item.package_name),
                    str(item.asset_class_path.asset_name),
                )
                for item in destination_assets
            ]
        else:
            raise RuntimeError(
                f"Root inventory mismatch for {source_root}: "
                f"source={len(source_assets)}, destination={len(destination_assets)}, "
                f"expected={expected}"
            )

        class_counts: dict[str, int] = {}
        for source, destination, asset_class in package_pairs:
            if destination in destinations:
                raise RuntimeError(f"Duplicate destination package: {destination}")
            destinations.add(destination)
            class_counts[asset_class] = class_counts.get(asset_class, 0) + 1
            moves.append(
                {
                    "source": source,
                    "destination": destination,
                    "asset_class": asset_class,
                    "status": state,
                }
            )

        rule_reports.append(
            {
                "source_root": source_root,
                "destination_root": destination_root,
                "expected_asset_count": expected,
                "state": state,
                "class_counts": class_counts,
                "exclude_packages": sorted(excluded),
            }
        )
    return moves, rule_reports


def _sha256(path: str) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _verify_backups(manifest: dict, moves: list[dict]) -> None:
    pending = [item for item in moves if item["status"] == "pending"]
    if not pending:
        return
    project_dir = os.path.abspath(unreal.Paths.project_dir())
    backup_root = os.path.abspath(
        os.path.join(project_dir, manifest.get("backup_directory", ""))
    )
    if not os.path.isdir(backup_root):
        raise RuntimeError(f"Backup directory does not exist: {backup_root}")
    failures = []
    for item in pending:
        relative = item["source"][len("/Game/") :].replace("/", os.sep) + ".uasset"
        source_file = os.path.join(project_dir, "Content", relative)
        backup_file = os.path.join(backup_root, "Content", relative)
        if not os.path.isfile(source_file):
            failures.append(f"Missing source package: {item['source']}")
        elif not os.path.isfile(backup_file):
            failures.append(f"Missing backup package: {item['source']}")
        elif _sha256(source_file) != _sha256(backup_file):
            failures.append(f"Backup hash mismatch: {item['source']}")
    if failures:
        raise RuntimeError("Backup verification failed:\n" + "\n".join(failures))


def _preflight_destinations(moves: list[dict]) -> None:
    collisions = [
        item["destination"]
        for item in moves
        if item["status"] == "pending"
        and unreal.EditorAssetLibrary.does_asset_exist(item["destination"])
    ]
    if collisions:
        raise RuntimeError("Destination collisions:\n" + "\n".join(collisions))


def _apply_moves(moves: list[dict]) -> None:
    pending = [item for item in moves if item["status"] == "pending"]
    if not pending:
        unreal.log("[AssetOrganization] Root manifest is already applied.")
        return
    rename_data = []
    loaded_assets = []
    for item in pending:
        asset = unreal.EditorAssetLibrary.load_asset(item["source"])
        if asset is None:
            raise RuntimeError(f"Failed to load source asset: {item['source']}")
        loaded_assets.append(asset)
        destination_path, destination_name = item["destination"].rsplit("/", 1)
        data = unreal.AssetRenameData()
        data.set_editor_property("asset", asset)
        data.set_editor_property("new_package_path", destination_path)
        data.set_editor_property("new_name", destination_name)
        rename_data.append(data)
    if not unreal.AssetToolsHelpers.get_asset_tools().rename_assets(rename_data):
        raise RuntimeError("AssetTools.RenameAssets reported failure")


def _write_report(manifest: dict, moves: list[dict], rules: list[dict], outcome: str) -> str:
    report_dir = os.path.join(
        os.path.abspath(unreal.Paths.project_dir()), "Saved", "AssetOrganization"
    )
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
            "asset_count": len(moves),
            "pending": sum(1 for item in moves if item["status"] == "pending"),
            "already_moved": sum(
                1 for item in moves if item["status"] == "already_moved"
            ),
        },
        "rules": rules,
        "moves": moves,
    }
    with open(report_path, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(report, handle, ensure_ascii=False, indent=2)
        handle.write("\n")
    return report_path


def main() -> None:
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.wait_for_completion()
    manifest = _load_manifest()
    moves, rules = _expand_rules(manifest, registry)
    _preflight_destinations(moves)
    outcome = "ready"
    if APPLY:
        had_pending = any(item["status"] == "pending" for item in moves)
        _verify_backups(manifest, moves)
        _apply_moves(moves)
        registry.scan_paths_synchronous(
            [rule["destination_root"] for rule in manifest["root_moves"]], True
        )
        moves, rules = _expand_rules(manifest, registry)
        if any(item["status"] != "already_moved" for item in moves):
            raise RuntimeError("Post-move root verification failed")
        outcome = "applied" if had_pending else "verified"
    report_path = _write_report(manifest, moves, rules, outcome)
    unreal.log(
        f"[AssetOrganization] Root migration {outcome}: "
        f"assets={len(moves)}, report={report_path}"
    )


try:
    main()
except Exception:
    unreal.log_error("[AssetOrganization] Root migration failed")
    unreal.log_error(traceback.format_exc())
    raise
