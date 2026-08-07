"""Read-only audit for specific Unreal package paths."""

from __future__ import annotations

import json
import os
import sys
import traceback
from datetime import datetime, timezone

import unreal


def _argument_value(prefix: str, default: str) -> str:
    for argument in sys.argv[1:]:
        if argument.startswith(prefix):
            return argument[len(prefix) :]
    return default


PATHS = [
    value
    for value in _argument_value("--paths=", "").split(";")
    if value.startswith("/Game/")
]
REPORT_NAME = _argument_value("--report=", "asset-path-audit.json")


def main() -> None:
    if not PATHS:
        raise RuntimeError("Pass one or more semicolon-separated --paths=/Game/... packages")
    if os.path.basename(REPORT_NAME) != REPORT_NAME or not REPORT_NAME.endswith(".json"):
        raise RuntimeError("--report must be a plain .json filename")

    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.wait_for_completion()
    records = []
    for package_path in PATHS:
        asset_data_items = registry.get_assets_by_package_name(package_path)
        records.append(
            {
                "package": package_path,
                "file_exists": os.path.isfile(
                    os.path.join(
                        os.path.abspath(unreal.Paths.project_content_dir()),
                        package_path[len("/Game/") :].replace("/", os.sep) + ".uasset",
                    )
                ),
                "asset_data": [
                    {
                        "asset_name": str(item.asset_name),
                        "asset_class": str(item.asset_class_path.asset_name),
                        "object_path": f"{item.package_name}.{item.asset_name}",
                    }
                    for item in asset_data_items
                ],
                "referencers": sorted(
                    str(value)
                    for value in unreal.EditorAssetLibrary.find_package_referencers_for_asset(
                        package_path, False
                    )
                ),
            }
        )

    report_dir = os.path.join(
        os.path.abspath(unreal.Paths.project_dir()), "Saved", "AssetOrganization"
    )
    os.makedirs(report_dir, exist_ok=True)
    report_path = os.path.join(report_dir, REPORT_NAME)
    report = {
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "paths": records,
    }
    with open(report_path, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(report, handle, ensure_ascii=False, indent=2)
        handle.write("\n")
    unreal.log(f"[AssetOrganization] Asset path audit complete: {report_path}")


try:
    main()
except Exception:
    unreal.log_error("[AssetOrganization] Asset path audit failed")
    unreal.log_error(traceback.format_exc())
    raise
