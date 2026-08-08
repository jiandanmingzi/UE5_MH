"""Verify the Git-tracked Content scaffold required by the combat demo.

This script is filesystem-only and can run with the regular project Python.
Unreal package references are verified separately by ``organize_assets.py``.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parents[1]
CONTENT_ROOT = (PROJECT_ROOT / "Content").resolve()

REQUIRED_DIRECTORIES = [
    "Content/Audio/Combat",
    "Content/Audio/UI",
    "Content/GameplayCues/Hit",
    "Content/GameplayCues/InsectGlaive",
    "Content/GameplayEffects/Core",
    "Content/GameplayEffects/InsectGlaive",
    "Content/Monster/TrainingDummy/Anims",
    "Content/Monster/TrainingDummy/Materials",
    "Content/Monster/TrainingDummy/Meshes",
    "Content/Monster/TrainingDummy/Textures",
    "Content/UI/Common",
    "Content/UI/Feedback",
    "Content/UI/HUD",
    "Content/UI/InsectGlaive",
    "Content/VFX/Combat/Hit",
    "Content/Weapons/InsectGlaive/Anims/Blueprints",
    "Content/Weapons/InsectGlaive/Audio",
    "Content/Weapons/InsectGlaive/VFX",
]

ASSET_MOVES = [
    ("/Game/Data/DA_IG_Combo", "/Game/Weapons/InsectGlaive/Data/DA_IG_Combo"),
    ("/Game/Data/DA_IG_HuoLongGun", "/Game/Weapons/InsectGlaive/Data/DA_IG_HuoLongGun"),
    ("/Game/Data/DA_TrainingDummy", "/Game/Monster/TrainingDummy/Data/DA_TrainingDummy"),
]


def _package_file(package_path: str) -> Path:
    if not package_path.startswith("/Game/"):
        raise ValueError(f"Not a /Game package path: {package_path}")
    return CONTENT_ROOT / f"{package_path[len('/Game/'): ]}.uasset"


def main() -> int:
    errors: list[str] = []
    scaffold_count = 0
    placeholder_count = 0

    for path in REQUIRED_DIRECTORIES:
        relative = Path(path)
        directory = (PROJECT_ROOT / relative).resolve()
        try:
            directory.relative_to(CONTENT_ROOT)
        except ValueError:
            errors.append(f"Scaffold path is outside Content: {relative}")
            continue

        if not directory.is_dir():
            errors.append(f"Missing required directory: {relative.as_posix()}")
            continue

        scaffold_count += 1
        package_files = [
            child
            for child in directory.iterdir()
            if child.is_file() and child.suffix.lower() in {".uasset", ".umap"}
        ]
        placeholder = directory / ".gitkeep"
        if package_files and placeholder.exists():
            errors.append(
                f"Remove placeholder after adding assets: {placeholder.relative_to(PROJECT_ROOT)}"
            )
        elif not package_files:
            if not placeholder.is_file():
                errors.append(
                    f"Untracked empty scaffold needs .gitkeep: {relative.as_posix()}"
                )
            else:
                placeholder_count += 1

    for source_path, destination_path in ASSET_MOVES:
        source = _package_file(source_path)
        destination = _package_file(destination_path)
        if source.exists():
            errors.append(f"Old source package still exists: {source.relative_to(PROJECT_ROOT)}")
        if not destination.is_file():
            errors.append(
                f"Moved destination package is missing: {destination.relative_to(PROJECT_ROOT)}"
            )

    forbidden_roots = [
        CONTENT_ROOT / "Kinsect",
        CONTENT_ROOT / "LevelPrototyping",
        CONTENT_ROOT / "系统自带",
    ]
    for forbidden in forbidden_roots:
        if forbidden.exists():
            errors.append(
                f"Duplicate domain root must not exist: {forbidden.relative_to(PROJECT_ROOT)}"
            )

    result = {
        "required_directories": len(REQUIRED_DIRECTORIES),
        "verified_directories": scaffold_count,
        "active_placeholders": placeholder_count,
        "verified_asset_moves": len(ASSET_MOVES),
        "errors": errors,
    }
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
