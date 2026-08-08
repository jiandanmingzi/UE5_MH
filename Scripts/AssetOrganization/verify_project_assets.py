"""Verify the current finalized Demo asset layout and compile project Blueprints."""

from __future__ import annotations

import json
import os
import traceback
from datetime import datetime, timezone

import unreal


EXPECTED_UASSET_COUNT = 443
EXPECTED_UMAP_COUNT = 1
EXPECTED_TEMPLATE_COUNT = 135
EXPECTED_ENVIRONMENT_ASSET_COUNT = 26
EXPECTED_CUSTOM_ANIMATION_COUNT = 168
EXPECTED_BLUEPRINT_COUNT = 8
EXPECTED_SKELETON = "/Game/Characters/Demo/Meshes/Body/SK_Demo_Body.SK_Demo_Body"

KEY_ASSETS = [
    "/Game/Maps/L_DemoArena",
    "/Game/Characters/Demo/Meshes/Body/SKM_Demo_Body",
    "/Game/Characters/Demo/Meshes/Body/SK_Demo_Body",
    "/Game/Weapons/InsectGlaive/Meshes/Glaive/SKM_IG_Glaive",
    "/Game/Weapons/InsectGlaive/Meshes/Kinsect/SKM_IG_Kinsect",
    "/Game/Weapons/InsectGlaive/Data/DA_IG_Combo",
    "/Game/Monster/TrainingDummy/Data/DA_TrainingDummy",
    "/Game/Blueprints/Characters/Demo/BP_IG_Character",
    "/Game/Blueprints/Characters/Demo/Animation/ABP_MH_Character",
    "/Game/Blueprints/GameModes/Demo/BP_Demo_GameMode",
    "/Game/Blueprints/Monster/TrainingDummy/BP_TrainingDummy",
    "/Game/Blueprints/PlayerController/Demo/BP_MHGZ_PlayerController",
    "/Game/Environment/DemoArena/Materials/MI_PrototypeGrid_Gray",
    "/Game/Environment/DemoArena/Materials/MI_PrototypeGrid_TopDark",
    "/Game/TemplateAssets/Characters/Mannequins/Meshes/SKM_Manny_Simple",
    "/Game/TemplateAssets/Characters/Mannequins/Meshes/SKM_Quinn_Simple",
]

EMPTY_ROOTS = [
    "/Game/Characters/Mannequins",
    "/Game/ThirdPerson",
    "/Game/Map",
    "/Game/__ExternalActors__",
    "/Game/__ExternalObjects__",
    "/Game/LevelPrototyping",
    "/Game/系统自带",
]

OLD_PACKAGES = [
    "/Game/Blueprints/Characters/BP_IG_Character",
    "/Game/Blueprints/Characters/BP_PlayerState",
    "/Game/Blueprints/Characters/ABP_MH_Character",
    "/Game/Blueprints/Characters/PSD_MH_Shth_Move",
    "/Game/Blueprints/Characters/PSD_MH_UnSh_Move",
    "/Game/Blueprints/Characters/PSS_MH_Move",
    "/Game/Blueprints/GameModes/BP_Demo_GameMode",
    "/Game/Blueprints/Monster/BP_TrainingDummy",
    "/Game/Blueprints/PlayerController/BP_MHGZ_PlayerController",
]


def _class_name(asset_data: unreal.AssetData) -> str:
    return str(asset_data.asset_class_path.asset_name)


def _filesystem_counts() -> dict:
    content_dir = os.path.abspath(unreal.Paths.project_content_dir())
    counts = {"uasset": 0, "umap": 0}
    for root, _, names in os.walk(content_dir):
        for name in names:
            if name.endswith(".uasset"):
                counts["uasset"] += 1
            elif name.endswith(".umap"):
                counts["umap"] += 1
    return counts


def _compile_project_blueprints(registry) -> list[dict]:
    records = []
    for asset_data in sorted(
        registry.get_assets_by_path("/Game/Blueprints", recursive=True),
        key=lambda item: str(item.package_name),
    ):
        if not _class_name(asset_data).endswith("Blueprint"):
            continue
        asset = asset_data.get_asset()
        if asset is None:
            raise RuntimeError(f"Failed to load Blueprint: {asset_data.package_name}")
        unreal.BlueprintEditorLibrary.compile_blueprint(asset)
        try:
            status = str(asset.get_editor_property("status"))
        except Exception:
            status = "unavailable"
        if "ERROR" in status.upper():
            raise RuntimeError(f"Blueprint compile status is error: {asset_data.package_name}")
        records.append(
            {
                "package": str(asset_data.package_name),
                "class": _class_name(asset_data),
                "status": status,
            }
        )
    return records


def _verify_custom_animations(registry) -> dict:
    roots = [
        "/Game/Characters/Demo/Anims",
        "/Game/Weapons/InsectGlaive/Anims",
    ]
    records = []
    for root in roots:
        for asset_data in registry.get_assets_by_path(root, recursive=True):
            if _class_name(asset_data) not in ("AnimSequence", "AnimMontage"):
                continue
            asset = asset_data.get_asset()
            skeleton = asset.get_editor_property("skeleton")
            skeleton_path = skeleton.get_path_name() if skeleton is not None else None
            records.append(
                {
                    "package": str(asset_data.package_name),
                    "class": _class_name(asset_data),
                    "skeleton": skeleton_path,
                }
            )
    invalid = [item for item in records if item["skeleton"] != EXPECTED_SKELETON]
    if len(records) != EXPECTED_CUSTOM_ANIMATION_COUNT or invalid:
        raise RuntimeError(
            f"Custom animation verification failed: count={len(records)}, invalid={len(invalid)}"
        )
    return {"count": len(records), "invalid": invalid}


def main() -> None:
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.wait_for_completion()

    content_dir = os.path.abspath(unreal.Paths.project_content_dir())
    package_files = sorted(
        os.path.join(root, name)
        for root, _, names in os.walk(content_dir)
        for name in names
        if name.endswith((".uasset", ".umap"))
    )
    registry.scan_files_synchronous(package_files, True)
    registry.wait_for_completion()

    counts = _filesystem_counts()
    if counts != {"uasset": EXPECTED_UASSET_COUNT, "umap": EXPECTED_UMAP_COUNT}:
        raise RuntimeError(f"Unexpected Content package counts: {counts}")

    missing_key_assets = [
        path for path in KEY_ASSETS if not unreal.EditorAssetLibrary.does_asset_exist(path)
    ]
    nonempty_roots = {
        root: len(registry.get_assets_by_path(root, recursive=True))
        for root in EMPTY_ROOTS
        if registry.get_assets_by_path(root, recursive=True)
    }
    all_assets = registry.get_assets_by_path("/Game", recursive=True)
    redirectors = sorted(
        str(item.package_name)
        for item in all_assets
        if _class_name(item) == "ObjectRedirector"
    )
    template_count = len(
        registry.get_assets_by_path("/Game/TemplateAssets", recursive=True)
    )
    environment_count = len(
        registry.get_assets_by_path("/Game/Environment/DemoArena", recursive=True)
    )
    remaining_old_packages = [
        path for path in OLD_PACKAGES if unreal.EditorAssetLibrary.does_asset_exist(path)
    ]
    if missing_key_assets or nonempty_roots or redirectors or remaining_old_packages:
        raise RuntimeError(
            f"Layout verification failed: missing={missing_key_assets}, "
            f"nonempty={nonempty_roots}, redirectors={redirectors}, "
            f"old_packages={remaining_old_packages}"
        )
    if template_count != EXPECTED_TEMPLATE_COUNT:
        raise RuntimeError(f"Unexpected template asset count: {template_count}")
    if environment_count != EXPECTED_ENVIRONMENT_ASSET_COUNT:
        raise RuntimeError(f"Unexpected environment asset count: {environment_count}")

    animation_result = _verify_custom_animations(registry)
    blueprint_records = _compile_project_blueprints(registry)
    if len(blueprint_records) != EXPECTED_BLUEPRINT_COUNT:
        raise RuntimeError(f"Unexpected project Blueprint count: {len(blueprint_records)}")
    report = {
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "outcome": "verified",
        "package_counts": counts,
        "template_asset_count": template_count,
        "environment_asset_count": environment_count,
        "custom_animation_count": animation_result["count"],
        "redirectors": redirectors,
        "empty_roots": EMPTY_ROOTS,
        "key_assets": KEY_ASSETS,
        "compiled_blueprints": blueprint_records,
    }
    report_dir = os.path.join(
        os.path.abspath(unreal.Paths.project_dir()), "Saved", "AssetOrganization"
    )
    os.makedirs(report_dir, exist_ok=True)
    report_path = os.path.join(report_dir, "project-final-verification.json")
    with open(report_path, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(report, handle, ensure_ascii=False, indent=2)
        handle.write("\n")
    unreal.log(
        f"[AssetOrganization] Project layout verified: assets={counts}, "
        f"animations={animation_result['count']}, blueprints={len(blueprint_records)}"
    )


try:
    main()
except Exception:
    unreal.log_error("[AssetOrganization] Project asset verification failed")
    unreal.log_error(traceback.format_exc())
    raise
