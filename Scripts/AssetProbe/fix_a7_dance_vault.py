"""A7: retune the dance vault to the MHRise ground truth.

Run headless:

    UnrealEditor-Cmd.exe <abs>/MHGZ.uproject -run=pythonscript \
        -script="C:/cache/study/UE5/UE5_WH/Scripts/AssetProbe/fix_a7_dance_vault.py" \
        -unattended -nop4 -nosplash -stdout

Values, re-derived 2026-09-16 from the `_02` / `_04` recordings restricted to
*complete* `player_motion_old_id == 154` runs (n = 195 frames each):

    apex height  564 cm   -- `maxY - startY`, identical in all 8 complete runs
    duration     1.6167 s -- matches the animation's own effective duration to
                             the millisecond, so "match the animation" and
                             "match Rise" are the same number

The previously recorded 625 cm came from reading `maxY` (absolute world height
6.14-6.37) as a rise; the hunter launches from 0.49-0.73 m.  The previously
recorded 1.55 s was the low end of a range polluted by truncated captures.

Idempotent: re-running writes the same values.  Verifies by re-reading.
"""

from __future__ import annotations

import unreal

DA_COMBAT = "/Game/Weapons/InsectGlaive/Data/DA_IG_Combat"

TARGET_APEX_CM = 564.0
TARGET_DURATION_S = 1.6167
# Horizontal travel of the arc.  ApexHeightAndDuration mode derives nothing
# here, so the ability now reads CombatConfig->DanceVaultDistance; without it
# the JumpForce Distance is 0 and the landing sits under the feet.
TARGET_DISTANCE_CM = 80.0


def main():
    if not unreal.EditorAssetLibrary.does_asset_exist(DA_COMBAT):
        unreal.log_error("[A7] {} not found".format(DA_COMBAT))
        raise SystemExit(1)

    asset = unreal.EditorAssetLibrary.load_asset(DA_COMBAT)
    if asset is None:
        unreal.log_error("[A7] failed to load {}".format(DA_COMBAT))
        raise SystemExit(1)

    fields = ("dance_vault_apex_height", "dance_vault_duration", "dance_vault_distance")
    targets = {
        "dance_vault_apex_height": TARGET_APEX_CM,
        "dance_vault_duration": TARGET_DURATION_S,
        "dance_vault_distance": TARGET_DISTANCE_CM,
    }
    before = {f: asset.get_editor_property(f) for f in fields}
    unreal.log("[A7] before: {}".format(before))

    for name, value in targets.items():
        asset.set_editor_property(name, value)

    if not unreal.EditorAssetLibrary.save_asset(DA_COMBAT, only_if_is_dirty=False):
        unreal.log_error("[A7] failed to save {}".format(DA_COMBAT))
        raise SystemExit(1)

    # Re-read through the asset library, not the object we just mutated, so the
    # check reflects what actually landed on disk.
    reloaded = unreal.EditorAssetLibrary.load_asset(DA_COMBAT)
    after = {f: reloaded.get_editor_property(f) for f in fields}
    unreal.log("[A7] after:  {}".format(after))

    wrong = {f: (after[f], targets[f]) for f in fields
             if abs(after[f] - targets[f]) > 1e-3}
    if wrong:
        unreal.log_error("[A7] verification FAILED (got, want): {}".format(wrong))
        raise SystemExit(1)

    unreal.log("[A7] OK: {}".format(after))
    print("A7_DONE")


main()
