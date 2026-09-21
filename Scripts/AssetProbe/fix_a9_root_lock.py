"""A9: lock the JumpOver root tracks so their pose lead cannot snap back.

Both JumpOver sequences currently have `bEnableRootMotion = False` and
`bForceRootLock = False`.  Neither flag set means the root track is neither
reset to the ref pose nor extracted, so its ~23 cm forward / ~13 cm lateral
lead lives on in the pose, survives until the in-place fall clip replaces it,
and is then dropped in two frames -- the visible sideways snap.

Setting `bForceRootLock = True` resets the root bone to the ref pose without
enabling extraction, so the lead never exists and the CurvedVault source keeps
driving the capsule undisturbed.  Do NOT set `bEnableRootMotion`: anim root
motion unconditionally pre-empts root motion sources
(`CharacterMovementComponent.cpp` returns before accumulating them), which
would kill the whole analytic path.

The recovered forward distance is folded back into the trajectory curve by
`UMHGZBackVaultAbility::BackVaultClipDrift` -- see the ability constructor.

Run headless:

    UnrealEditor-Cmd.exe <abs>/MHGZ.uproject -run=pythonscript \
        -script=".../fix_a9_root_lock.py" -unattended -nop4 -nosplash -stdout

Writes its findings to Saved/_a9_rootlock.json, because the pythonscript
commandlet swallows every print()/unreal.log() the script makes.
"""

from __future__ import annotations

import json
import os

import unreal

SEQ_DIR = "/Game/Weapons/InsectGlaive/Anims/Sequences/Imported"
# 前两条是 A9 当时修的（后撑杆跳专用弧段）。后两条是 2026-09-20 加的 ——
# 前/左/右撑杆跳**共用** AS_Unsh_Jump_Over(142) / AS_Unsh_W_Jump_Over(156)，
# 而它们从没被处理过；`UMHGZBackVaultAbility::ValidateActionDependencies` 要求的
# 是 `LockedNotExtracted`，不满足会被**直接拒绝激活**（不出招，不是跑歪）。
# 实测这两个资产在 Content/ 里**零引用**，所以直接改不影响别处。
TARGETS = ["AS_Unsh_Jump_Over_Back", "AS_Unsh_W_Jump_Over_Back",
           "AS_Unsh_Jump_Over", "AS_Unsh_W_Jump_Over"]
OUT = os.path.join(unreal.Paths.project_saved_dir(), "_a9_rootlock.json")

result = {}
for name in TARGETS:
    path = "{}/{}".format(SEQ_DIR, name)
    entry = {}
    try:
        if not unreal.EditorAssetLibrary.does_asset_exist(path):
            entry["error"] = "not found"
            result[name] = entry
            continue
        asset = unreal.EditorAssetLibrary.load_asset(path)
        entry["before_enable_root_motion"] = asset.get_editor_property("enable_root_motion")
        entry["before_force_root_lock"] = asset.get_editor_property("force_root_lock")

        # Deliberately only the lock. See the module docstring.
        asset.set_editor_property("force_root_lock", True)
        entry["save_returned"] = unreal.EditorAssetLibrary.save_asset(
            path, only_if_is_dirty=False)

        reloaded = unreal.EditorAssetLibrary.load_asset(path)
        entry["after_enable_root_motion"] = reloaded.get_editor_property("enable_root_motion")
        entry["after_force_root_lock"] = reloaded.get_editor_property("force_root_lock")
        entry["ok"] = (entry["after_force_root_lock"] is True
                       and entry["after_enable_root_motion"] is False)
    except Exception as exc:  # noqa: BLE001 - always write the report
        entry["exception"] = "{}: {}".format(type(exc).__name__, exc)
    result[name] = entry

with open(OUT, "w", encoding="utf-8") as handle:
    json.dump(result, handle, indent=2, default=repr)
