"""Write a read-only Unreal animation inventory to Saved/AssetOrganization.

Run from UnrealEditor-Cmd with PythonScriptPlugin enabled. The script never
saves or renames assets; it only loads animation assets and queries the Asset
Registry for reverse package references.
"""

from __future__ import annotations

import json
import os
import sys
import traceback
from datetime import datetime, timezone

import unreal


DEFAULT_ROOT = "/Game/Weapons/InsectGlaive/Anims"


def _argument_value(prefix: str, default: str) -> str:
    for argument in sys.argv[1:]:
        if argument.startswith(prefix):
            return argument[len(prefix) :]
    return default


ROOT_PATH = _argument_value("--root=", DEFAULT_ROOT).rstrip("/")
REPORT_NAME = _argument_value("--report=", "insect-glaive-animation-audit.json")


def _safe_property(asset: unreal.Object, property_name: str):
    try:
        value = asset.get_editor_property(property_name)
    except Exception:
        return None

    if isinstance(value, unreal.Object):
        return value.get_path_name()
    if isinstance(value, (str, int, float, bool)) or value is None:
        return value
    return str(value)


def _inspect_asset(asset_data: unreal.AssetData) -> dict:
    package_name = str(asset_data.package_name)
    asset = asset_data.get_asset()
    if asset is None:
        raise RuntimeError(f"Failed to load asset: {package_name}")

    asset_class = asset.get_class().get_name()
    relative_path = package_name[len(ROOT_PATH) :].lstrip("/")
    record = {
        "package": package_name,
        "relative_path": relative_path,
        "asset_name": str(asset_data.asset_name),
        "asset_class": asset_class,
        "referencers": sorted(
            str(path)
            for path in unreal.EditorAssetLibrary.find_package_referencers_for_asset(
                package_name, False
            )
        ),
    }

    if asset_class in ("AnimSequence", "AnimMontage", "PoseAsset"):
        skeleton = _safe_property(asset, "skeleton")
        record["skeleton"] = skeleton

    if asset_class == "AnimSequence":
        record.update(
            {
                "play_length": asset.get_play_length(),
                "rate_scale": _safe_property(asset, "rate_scale"),
                "enable_root_motion": _safe_property(asset, "enable_root_motion"),
                "root_motion_root_lock": _safe_property(
                    asset, "root_motion_root_lock"
                ),
                "additive_anim_type": _safe_property(asset, "additive_anim_type"),
                "ref_pose_type": _safe_property(asset, "ref_pose_type"),
            }
        )

    return record


def main() -> None:
    if not ROOT_PATH.startswith("/Game/"):
        raise RuntimeError(f"Audit root must be under /Game: {ROOT_PATH}")
    if os.path.basename(REPORT_NAME) != REPORT_NAME or not REPORT_NAME.endswith(
        ".json"
    ):
        raise RuntimeError("Report name must be a plain .json filename")

    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.wait_for_completion()
    asset_data = registry.get_assets_by_path(ROOT_PATH, recursive=True)
    records = sorted(
        (_inspect_asset(item) for item in asset_data),
        key=lambda item: item["package"],
    )

    class_counts: dict[str, int] = {}
    referenced_assets = 0
    for record in records:
        asset_class = record["asset_class"]
        class_counts[asset_class] = class_counts.get(asset_class, 0) + 1
        if record["referencers"]:
            referenced_assets += 1

    report = {
        "root": ROOT_PATH,
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "summary": {
            "asset_count": len(records),
            "referenced_asset_count": referenced_assets,
            "class_counts": class_counts,
        },
        "assets": records,
    }

    report_dir = os.path.join(
        os.path.abspath(unreal.Paths.project_dir()), "Saved", "AssetOrganization"
    )
    os.makedirs(report_dir, exist_ok=True)
    report_path = os.path.join(report_dir, REPORT_NAME)
    with open(report_path, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(report, handle, ensure_ascii=False, indent=2)
        handle.write("\n")

    unreal.log(
        "[AssetOrganization] Animation audit complete: "
        f"assets={len(records)}, referenced={referenced_assets}, report={report_path}"
    )


try:
    main()
except Exception:
    unreal.log_error("[AssetOrganization] Animation audit failed")
    unreal.log_error(traceback.format_exc())
    raise
