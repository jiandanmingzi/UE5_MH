"""量撑杆跳蒙太奇 Jump→JumpOver 接缝处的**姿势落差**。

为什么这是个可离线测量的问题：`MHGZPoleVaultMontageSetupCommandlet` 的 `AddSegment`
给每个段设的是 `AnimStartTime = 0`、`AnimEndTime = 序列的 GetPlayLength()`、
`AnimPlayRate = GetPlayLength() / (DesiredDuration * |RateScale|)`。也就是**每个段都
把整条剪辑播完**，只是被压进 `DesiredDuration` 秒。于是段边界 = 「起手段剪辑的
**最后一帧**姿势」紧接「弧段剪辑的**第一帧**姿势」，中间**没有任何混合** ——
两条 `FAnimSegment` 在同一个 slot track 上硬切。

所以「接缝处会不会看到角色一顿/一跳」在这里是：把两条剪辑在各自边界时刻的姿势
逐骨相减。

对照基线（否则数字没法解释）：**同一族剪辑里相邻一帧**的姿势落差。接缝落差与它
同量级 ⇒ 接缝平滑；大一个数量级 ⇒ 可见的姿势突跳。

本次关心的组合（来自 `Saved/_pole_vault_montage.json` 与计划的路由表）：

    白灯·左  144 -> 156   ← 无人为该组合授权过
    白灯·右  145 -> 156   ← 同上
    白灯·前  155 -> 156   ← 同一个白灯链，原生配对
    无白灯   141/144/145 -> 142，146 -> 147，158 -> 159

Run headless:

    UnrealEditor-Cmd.exe <abs>/MHGZ.uproject -run=pythonscript \
        -script=".../probe_vault_seam_pose.py" -unattended -nop4 -nosplash -stdout

Writes Saved/_vault_seam_pose.json —— pythonscript 命令列会吞掉 print()。

 ⚠ 不要去读蒙太奇的 `slot_anim_tracks`：`UAnimMontage::SlotAnimTracks` 没暴露给
 Python，`get_editor_property` 会抛 `Failed to find property`。剪辑配对直接照
 命令列报告写死。
"""

from __future__ import annotations

import json
import math
import os
import traceback

import unreal

OUT = os.path.join(unreal.Paths.project_saved_dir(), "_vault_seam_pose.json")
SEQ_DIR = "/Game/Weapons/InsectGlaive/Anims/Sequences/Imported"

# (标签, 起手段, 弧段)
PAIRS = [
    ("无白灯·前", "AS_Unsh_Jump_Forward", "AS_Unsh_Jump_Over"),
    ("无白灯·左", "AS_Unsh_Jump_Left", "AS_Unsh_Jump_Over"),
    ("无白灯·右", "AS_Unsh_Jump_Right", "AS_Unsh_Jump_Over"),
    ("无白灯·后", "AS_Unsh_Jump_Back", "AS_Unsh_Jump_Over_Back"),
    ("白灯·前", "AS_Unsh_W_Jump_Forward", "AS_Unsh_W_Jump_Over"),
    ("白灯·左", "AS_Unsh_Jump_Left", "AS_Unsh_W_Jump_Over"),
    ("白灯·右", "AS_Unsh_Jump_Right", "AS_Unsh_W_Jump_Over"),
    ("白灯·后", "AS_Unsh_W_Jump_Back", "AS_Unsh_W_Jump_Over_Back"),
]

lib = unreal.AnimationLibrary
result = {"pairs": {}, "bones": 0}

_clips = {}
_names = None


def clip(name):
    if name not in _clips:
        asset = unreal.EditorAssetLibrary.load_asset("{}/{}".format(SEQ_DIR, name))
        _clips[name] = asset
    return _clips[name]


def bone_names(seq):
    global _names
    if _names is None:
        raw = seq.data_model_interface.get_bone_track_names()
        _names = [unreal.Name(str(n)) for n in raw]
    return _names


def pose(seq, t):
    return [lib.get_bone_pose_for_time(seq, n, t, False) for n in bone_names(seq)]


def d_trans(a, b):
    return math.sqrt((a.translation.x - b.translation.x) ** 2
                     + (a.translation.y - b.translation.y) ** 2
                     + (a.translation.z - b.translation.z) ** 2)


def d_rot(a, b):
    qa, qb = a.rotation, b.rotation
    dot = abs(qa.x * qb.x + qa.y * qb.y + qa.z * qb.z + qa.w * qb.w)
    return math.degrees(2.0 * math.acos(max(-1.0, min(1.0, dot))))


try:
    frame = 1.0 / 30.0
    for label, takeoff_name, arc_name in PAIRS:
        try:
            a, b = clip(takeoff_name), clip(arc_name)
            if a is None or b is None:
                result["pairs"][label] = {"error": "asset missing"}
                continue
            names = bone_names(b)
            result["bones"] = len(names)

            a_end = max(float(a.get_play_length()) - 1e-4, 0.0)
            b_len = float(b.get_play_length())
            b_start = 0.0

            seam_a, seam_b = pose(a, a_end), pose(b, b_start)
            root_index = next((i for i, bn in enumerate(names) if str(bn) == "root"), None)
            seam_a_root_x = 0.0 if root_index is None else seam_a[root_index].translation.x
            a_prev = pose(a, max(a_end - frame, 0.0))
            b_next = pose(b, min(b_start + frame, b_len - 1e-4))

            rows = []
            for i, bone in enumerate(names):
                # `root` 必须排除：带上它的姿势位移就是剪辑自己的根位移（起手段
                # 最大到 ~300 cm，弧段被锁成 ref pose = 0），引擎对根轨道另有
                # 一套处理（提取 / 锁 ref pose）。把它算进「姿势落差」会得到
                # 200~300 cm 的假警报，掩盖真正的骨骼落差。
                if str(bone) == "root":
                    result.setdefault("root_note", {})[label] = {
                        "takeoff_end_cm": round(seam_a[i].translation.x, 2),
                        "arc_start_cm": round(seam_b[i].translation.x, 2),
                    }
                    continue
                rows.append((
                    str(bone),
                    (d_trans(seam_a[i], seam_b[i]), d_rot(seam_a[i], seam_b[i])),
                    (d_trans(seam_a[i], a_prev[i]), d_rot(seam_a[i], a_prev[i])),
                    (d_trans(seam_b[i], b_next[i]), d_rot(seam_b[i], b_next[i])),
                ))

            def agg(which, comp):
                vals = [r[which][comp] for r in rows]
                return {"max": round(max(vals), 3), "mean": round(sum(vals) / len(vals), 3)}

            worst = sorted(rows, key=lambda r: -(r[1][0] + 0.5 * r[1][1]))[:6]

            # 另一个切点会不会好一些：起手段的**每一帧**与弧段首帧比一遍。
            # 这不是学术兴趣 —— 如果存在一个落差只有现切点几分之一的帧，
            # 那么「裁掉起手段尾部 + 重算段速率」就是一条纯数据的修法。
            # 代价也要一起量：被裁掉的那一段里根骨还带着多少位移。
            scan = []
            n_frames = int(round(float(a.get_play_length()) * 30.0))
            for fi in range(n_frames + 1):
                t = min(fi / 30.0, a_end)
                p = pose(a, t)
                vals = [d_rot(p[i], seam_b[i]) for i, bn in enumerate(names) if str(bn) != "root"]
                scan.append((round(t, 4), round(sum(vals) / len(vals), 3)))
            best_t, best_v = min(scan, key=lambda s: s[1])
            root_trail = abs(seam_a_root_x - pose(a, best_t)[root_index].translation.x) \
                if root_index is not None else None
            cut_scan = {
                "at_clip_end_t": round(a_end, 4), "at_clip_end_rot_mean": scan[-1][1],
                "best_t": best_t, "best_rot_mean": best_v,
                "root_tail_cm": None if root_trail is None else round(root_trail, 2),
            }
            result["pairs"][label] = {
                "takeoff": takeoff_name, "arc": arc_name,
                "takeoff_length": round(float(a.get_play_length()), 4),
                "arc_length": round(b_len, 4),
                "seam_trans_cm": agg(1, 0), "seam_rot_deg": agg(1, 1),
                "ctrl_prev_trans_cm": agg(2, 0), "ctrl_prev_rot_deg": agg(2, 1),
                "ctrl_next_trans_cm": agg(3, 0), "ctrl_next_rot_deg": agg(3, 1),
                "cut_scan": cut_scan,
                "worst_bones": [
                    {"bone": r[0],
                     "seam_t": round(r[1][0], 2), "seam_r": round(r[1][1], 2),
                     "prev_t": round(r[2][0], 2), "prev_r": round(r[2][1], 2),
                     "next_t": round(r[3][0], 2), "next_r": round(r[3][1], 2)}
                    for r in worst],
            }
        except Exception:  # noqa: BLE001
            result["pairs"][label] = {"error": traceback.format_exc()}
except Exception:  # noqa: BLE001
    result["fatal"] = traceback.format_exc()

with open(OUT, "w", encoding="utf-8") as handle:
    json.dump(result, handle, ensure_ascii=False, indent=1)
