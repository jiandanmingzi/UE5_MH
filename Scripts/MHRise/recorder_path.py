"""Locate the installed MHRise recorder data across development machines.

The repository does not contain raw captures. An explicit environment override
takes precedence; otherwise find the installed game through Steam's registry
entry and library list. Returning the game's expected data path is useful even
before the first F9 capture creates that directory.
"""

from __future__ import annotations

import os
import re
from pathlib import Path


def recorder_dir() -> Path:
    override = os.environ.get("MHGZ_RISE_RECORDER_DIR")
    if override:
        return Path(override).expanduser()

    steam_roots: list[Path] = []
    try:
        import winreg

        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam") as key:
            steam_roots.append(Path(winreg.QueryValueEx(key, "SteamPath")[0]))
    except (ImportError, OSError):
        pass

    steam_roots.extend((
        Path(r"C:\apps\steam"),
        Path(r"C:\Program Files (x86)\Steam"),
    ))
    libraries: list[Path] = []
    for steam_root in steam_roots:
        libraries.append(steam_root)
        library_file = steam_root / "steamapps" / "libraryfolders.vdf"
        if library_file.is_file():
            for raw in re.findall(r'"path"\s+"([^"]+)"',
                                  library_file.read_text(encoding="utf-8-sig")):
                libraries.append(Path(raw.replace("\\\\", "\\")))

    suffix = Path("reframework/data/MHGZ_AerialTrajectoryRecorder")
    for library in libraries:
        game = library / "steamapps" / "common" / "MonsterHunterRise"
        if game.is_dir():
            return game / suffix
    return steam_roots[0] / "steamapps" / "common" / "MonsterHunterRise" / suffix
