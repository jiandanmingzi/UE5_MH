"""**采样器**：把撑杆跳接缝两侧的姿势倒成网格，交给离线脚本分析。

判据不在这里算 —— 见 `Saved/_mhr_scratch/analyze_vault_seam_blend.py`。分成两步
是因为「淡入窗口内的帧」该拿什么当基线，第一版判错了**两次**，而每次改判据都要
重开一次编辑器（约 1 分钟）。姿势网格倒出来之后，判据可以随便改。

## 要解决的问题

拆成双蒙太奇之后运行时不再是硬切：弧段蒙太奇一播，
`UAnimInstance::StopAllMontagesByGroupName` 用同一个 blend-in 设置把起手段淡出
（`AnimInstance.cpp` 的 `Montage_PlayInternal`），两条姿势在 `VaultSeamBlendTime`
内同时有权重。于是 [probe_vault_seam_pose.py](probe_vault_seam_pose.py) 量到的
「起手段末帧 vs 弧段首帧」不再描述运行时的样子，它现在只是改动前的基线。

**为什么必须离线建模。** 遥测里 `MontageInstances.csv` 只有权重、`Spatial.csv`
只有胶囊，**没有逐骨姿势**。所以「淡入之后糊得怎么样」在 PIE 里量不到 —— 而计划
的验收判据恰恰是逐骨的那条（「落差从 4.0×/4.3× 降到正常帧量级」）。

## 倒出来的是什么

对每组（起手段剪辑, 弧段剪辑）：

- `pose_a_end`：起手段**末帧**姿势，75 根骨的局部变换。运行时它会被冻结 ——
  依据是项目给起手段实例设了 `bEnableAutoBlendOut = false`，播完不终止、保持
  终末姿势、权重仍为 1，正好被拿来做淡出。
- `arc_grid`：弧段姿势，均匀网格 `k / GRID_HZ` 秒（蒙太奇时间，从接缝起算）。
  **网格频率 240 能被 30 / 60 / 120 整除**，所以离线那边可以精确地按任意帧率取帧，
  不需要重跑。
- `hardcut_*`：与旧探针逐值对得上的接缝落差（自检用）。对不上就是采样口径错了。

网格用 5 位小数（平移 1e-5 m = 0.01 mm，远超声称精度），把 JSON 压到 ~5 MB。

Run headless:

    UnrealEditor-Cmd.exe <abs>/MHGZ.uproject -run=pythonscript \
        -script=".../probe_vault_seam_blend.py" -unattended -nop4 -nosplash -NullRHI -stdout

Writes `Saved/_vault_seam_blend_grid.json` —— pythonscript 命令列会吞掉 print()。
"""

from __future__ import annotations

import json
import math
import os
import traceback

import unreal

OUT = os.path.join(unreal.Paths.project_saved_dir(), "_vault_seam_blend_grid.json")
BASELINE = os.path.join(unreal.Paths.project_saved_dir(), "_vault_seam_pose.json")
SEQ_DIR = "/Game/Weapons/InsectGlaive/Anims/Sequences/Imported"
BP_DIR = "/Game/Weapons/InsectGlaive/Abilities"

# 姿势网格频率。必须能被 30 / 60 / 120 整除，否则离线取帧会有插值误差。
GRID_HZ = 240
# 倒多久。最长候淡入时长（0.2668 s）+ 基线窗口（0.15 s）之后还有富余。
GRID_SECONDS = 0.55

# (标签, 方向, 白灯, 起手段剪辑, 弧段剪辑)
#
# ⚠ 白灯左/右**复用无白灯的起跳剪辑**（原作白灯一族只有 155/156/157/158/159，
#   根本没有白灯左/右起跳），所以「白灯」这个标志**不能从剪辑名推**，只能写死
#   在这里。这是 `docs/reference/撑杆跳曲线.md` 里那条结论的直接后果。
PAIRS = [
    ("无白灯·前", "Forward", False, "AS_Unsh_Jump_Forward", "AS_Unsh_Jump_Over"),
    ("无白灯·左", "Left", False, "AS_Unsh_Jump_Left", "AS_Unsh_Jump_Over"),
    ("无白灯·右", "Right", False, "AS_Unsh_Jump_Right", "AS_Unsh_Jump_Over"),
    ("无白灯·后", "Back", False, "AS_Unsh_Jump_Back", "AS_Unsh_Jump_Over_Back"),
    ("白灯·前", "Forward", True, "AS_Unsh_W_Jump_Forward", "AS_Unsh_W_Jump_Over"),
    ("白灯·左", "Left", True, "AS_Unsh_Jump_Left", "AS_Unsh_W_Jump_Over"),
    ("白灯·右", "Right", True, "AS_Unsh_Jump_Right", "AS_Unsh_W_Jump_Over"),
    ("白灯·后", "Back", True, "AS_Unsh_W_Jump_Back", "AS_Unsh_W_Jump_Over_Back"),
]

GA_BP = {
    "Forward": "GA_IG_QianChengGanTiao",
    "Left": "GA_IG_ZuoChengGanTiao",
    "Right": "GA_IG_YouChengGanTiao",
    "Back": "GA_IG_HouChengGanTiao",
}

lib = unreal.AnimationLibrary
report = {"grid_hz": GRID_HZ, "grid_seconds": GRID_SECONDS, "pairs": {}}

_clips = {}
_bones = {}


def clip(name):
    if name not in _clips:
        _clips[name] = unreal.EditorAssetLibrary.load_asset("{}/{}".format(SEQ_DIR, name))
    return _clips[name]


def bones_of(seq, name):
    if name not in _bones:
        raw = seq.data_model_interface.get_bone_track_names()
        _bones[name] = [unreal.Name(str(n)) for n in raw]
    return _bones[name]


def q(v):
    # 7 位小数 = IEEE float32 的有效位数，所以这不丢精度，反而顺手把 float32
    # 的表示噪声（`0.1f` → `0.10000000149…`）抹掉。
    # ⚠ 别降到 5 位：旋转角对四元数点积的导数是 `2/sqrt(1-dot²)`（11° 处 ≈ 21），
    #   5 位舍入（≈4e-5）会把每个角度系统性抬高 ≈0.07°，自检因此失败。
    return round(float(v), 7)


def pose(seq, names, t):
    """一根骨 7 个数：平移 xyz + 四元数 xyzw。全程用纯 Python 浮点，
    离线那边不需要 unreal 也能重算。"""
    out = []
    for n in names:
        p = lib.get_bone_pose_for_time(seq, n, t, False)
        tr, qt = p.translation, p.rotation
        out.append([q(tr.x), q(tr.y), q(tr.z), q(qt.x), q(qt.y), q(qt.z), q(qt.w)])
    return out


def d_rot(a, b):
    dot = abs(sum(a[3 + i] * b[3 + i] for i in range(4)))
    return math.degrees(2.0 * math.acos(max(-1.0, min(1.0, dot))))


def read_profiles():
    """从四个 GA 蓝图 CDO 读每个变体的 `JumpDuration` / `JumpOverDuration`。

    **不从窗口推、也不在脚本里抄一份** —— 后撑杆跳白灯的弧段姿势刻意比窗口长
    （`FVaultProfile::JumpOverDuration` 就是为了分开这两个量才加的），照窗口推
    会得到错的段速率，进而把整条时间轴算错。
    """
    out = {}
    for direction, bp_name in GA_BP.items():
        path = "{}/{}.{}".format(BP_DIR, bp_name, bp_name)
        bp = unreal.EditorAssetLibrary.load_asset(path)
        if bp is None:
            out[direction] = {"error": "找不到蓝图 " + path}
            continue
        cdo = unreal.get_default_object(bp.generated_class())
        for p in cdo.get_editor_property("vault_profiles"):
            out["{}/{}".format(direction, bool(p.get_editor_property("white")))] = {
                "jump": float(p.get_editor_property("jump_duration")),
                "arc": float(p.get_editor_property("jump_over_duration")),
            }
    return out


try:
    profiles = read_profiles()
    try:
        with open(BASELINE, encoding="utf-8") as handle:
            old = json.load(handle)
    except Exception as exc:  # noqa: BLE001
        old = {"error": "{}: {}".format(type(exc).__name__, exc)}
    report["profiles"] = profiles

    n_grid = int(math.ceil(GRID_SECONDS * GRID_HZ)) + 1

    for label, direction, white, takeoff_name, arc_name in PAIRS:
        entry = {"takeoff": takeoff_name, "arc": arc_name,
                 "direction": direction, "white": white}
        try:
            a, b = clip(takeoff_name), clip(arc_name)
            if a is None or b is None:
                report["pairs"][label] = {"error": "asset missing"}
                continue

            prof = profiles.get("{}/{}".format(direction, white))
            if not isinstance(prof, dict) or "jump" not in prof:
                report["pairs"][label] = {"error": "读不到 profile：{}".format(prof)}
                continue

            t_a = float(a.get_play_length())
            t_b = float(b.get_play_length())
            d_jump, d_arc = prof["jump"], prof["arc"]
            # 段的播速率 = 剪辑长度 / 期望秒数（命令列的 RequiredSegmentPlayRate，
            # RateScale 为 1）。蒙太奇时间 t 对应的剪辑时间 = t * rate。
            rate_a = t_a / d_jump if d_jump > 0 else 0.0
            rate_b = t_b / d_arc if d_arc > 0 else 0.0

            names = bones_of(b, arc_name)
            names_a = bones_of(a, takeoff_name)
            # 两条剪辑的骨序**按名字对齐**，不假设一致 —— 顺序一旦不同，
            # 后面所有下标都会串骨，而结果看上去仍像个正常数字。
            order_a = [str(n) for n in names_a]
            order_b = [str(n) for n in names]
            if order_a != order_b:
                entry["bone_set_mismatch"] = True
                try:
                    remap = [order_a.index(nm) for nm in order_b]
                except ValueError:
                    report["pairs"][label] = {"error": "两条剪辑骨集合不同且无法对齐"}
                    continue
            else:
                remap = None

            raw_a_end = pose(a, names_a, max(t_a - 1e-4, 0.0))
            pose_a_end = raw_a_end if remap is None else [raw_a_end[j] for j in remap]
            arc_zero = pose(b, names, 0.0)

            root_i = next((i for i, n in enumerate(names) if str(n) == "root"), None)
            # 除 `root` 之外全部参与。`root` 的位移是剪辑自己的根位移（起手段最大
            # ~300 cm，弧段被锁成 ref pose = 0），引擎对根轨道另有一套处理，算进
            # 「姿势落差」会得到几百厘米的假警报。
            idx = [i for i in range(len(names)) if i != root_i]
            entry.update({
                "takeoff_clip_length": round(t_a, 6), "arc_clip_length": round(t_b, 6),
                "jump_duration": round(d_jump, 6), "arc_duration": round(d_arc, 6),
                "takeoff_rate": round(rate_a, 6), "arc_rate": round(rate_b, 6),
                "bones": len(names), "root_index": root_i,
                "bone_names": [str(n) for n in names],
                # `root` 的 X 是项目验证过的那条「根位移」轴向（起手段 171.6 对应
                # 向左的 173 cm），留两个数方便和 `docs/reference` 对账。
                "takeoff_end_root_x_cm": None if root_i is None else round(100.0 * pose_a_end[root_i][0], 3),
                "arc_start_root_x_cm": None if root_i is None else round(100.0 * arc_zero[root_i][0], 3),
            })

            # 自检：与 `probe_vault_seam_pose.py` 的 `seam_rot_deg.mean` 逐值比对。
            hard_all = sum(d_rot(pose_a_end[i], arc_zero[i]) for i in idx) / len(idx)
            old_mean = None
            try:
                old_mean = old["pairs"][label]["seam_rot_deg"]["mean"]
            except Exception:  # noqa: BLE001
                pass
            entry["hardcut_deg_all"] = round(hard_all, 4)
            entry["hardcut_deg_old_probe"] = old_mean
            entry["hardcut_ok"] = old_mean is not None and abs(hard_all - old_mean) < 0.01

            grid = []
            for k in range(n_grid):
                ct = min((k / float(GRID_HZ)) * rate_b, max(t_b - 1e-4, 0.0))
                grid.append(pose(b, names, ct))
            entry["pose_a_end"] = pose_a_end
            entry["arc_grid"] = grid
            report["pairs"][label] = entry
        except Exception:  # noqa: BLE001
            report["pairs"][label] = {"error": traceback.format_exc()}
except Exception:  # noqa: BLE001
    report["fatal"] = traceback.format_exc()

with open(OUT, "w", encoding="utf-8") as handle:
    json.dump(report, handle, ensure_ascii=False, default=repr)
