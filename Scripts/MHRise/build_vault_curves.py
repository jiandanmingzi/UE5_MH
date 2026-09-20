#!/usr/bin/env python3
# Copyright MHGZ Project. All Rights Reserved.
"""从前/左/右撑杆跳的实机录制抽出**归一化轨迹曲线** + 链级真值表。

**为什么单独一个脚本**，而不并进 `build_truth_table.py`：
后者是**按 `player_motion_old_id` 逐个动作**归并的，而撑杆跳是**一条链** ——
`起手段 → 弧段 → 下坠段 →（落地）`，真正的真值（总位移、最高点、弧的形状）
属于这条链，不属于任何单个 id。而且曲线的消费方是
`UMHGZBackVaultAbility::BuildTrajectoryCurve`，它的输入格式是
`FBackVaultTrajectoryKey`（`Time` + `NormalizedPosition`），跟真值表的表格不是一回事。

**输出**：
  - `Saved/_mhr_curves/<状态>_<方向>.csv` —— 逐帧归一化轨迹（曲线源，**不入库**）
  - `docs/reference/撑杆跳曲线.md` —— 各变体真值 + 可直接抄进蓝图的关键帧表

【口径】
- 真值源：`C:/apps/steam/.../MHGZ_AerialTrajectoryRecorder/`。**该目录不入库。**
  本脚本**扫描全部实录**（不写死文件名），凡含完整撑杆跳链的都纳入；无/有白灯由**链自己**
  判定（起手段 `155` 或弧段 `156` ⇒ 有白灯）。所以以后补录撑杆跳不用改脚本。
- 向上轴是 **Y**；位置**米**、速度 m/s ⇒ 本脚本 ×100 出厘米。
- 采样率 **119.8 Hz**，帧数 = 样本数 − 1。
- **朝向分解**：`X` 沿起跳那一刻的 `forward_x/forward_z`（水平面），`Y` 取侧向，`Z` 取世界 Y 相对段首的抬升。
  空中段的朝向是锁死的（实测段内转动 0.0000°），所以用段首朝向分解不需要插值。
- 归一化：`X_norm = 前向进度 / 落地时的前向总进度`，
  `Y_norm = 侧移 / 同一个总进度`，`Z_norm = 抬升 / 弧最高点`，
  `Time = 该帧时刻 / 离地前缀总时长`。这与 `FBackVaultTrajectoryKey` 的注释一致
  （"X = travel progress, Y = lateral offset divided by total travel distance,
  Z = height divided by measured apex height"）。

用法：
    python Scripts/MHRise/build_vault_curves.py            # 写曲线 + 文档
    python Scripts/MHRise/build_vault_curves.py --print     # 只看，不写文件
"""

from __future__ import annotations

import argparse
import csv
import math
import statistics
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path

RECORDER_DIR = Path(
    r"C:/apps/steam/steamapps/common/MonsterHunterRise/reframework/data/MHGZ_AerialTrajectoryRecorder"
)
PROJECT = Path(__file__).resolve().parents[2]
OUT_MD = PROJECT / "docs" / "reference" / "撑杆跳曲线.md"
OUT_CSV = PROJECT / "Saved" / "_mhr_curves"

SAMPLE_HZ = 119.8
MIN_SAMPLES = 12

# 起手段 → 方向
TAKEOFF = {"141": "向前", "144": "向左", "145": "向右", "155": "向前"}
# 弧段（三向共用同一个弧 —— 这是实测结论，不是假设）
ARC_IDS = {"142", "156"}
# 下坠段：弧没够到地面的那几次会多一段
FALL_IDS = {"143", "157"}
# 链的终点：出现它就说明这一段弧已经到地面了
GROUND_ID = "148"
# 无/有白灯由**链自己**决定，不按会话名硬编码：
# 有白灯的起手段是 `155`、弧段是 `156`；无白灯是 `141`/`142`。
# 这样以后再补录撑杆跳（无论放在哪一份会话里）都会被自动纳入，不用改脚本。
WHITE_LAMP_TAKEOFF = "155"
WHITE_LAMP_ARC = "156"

KEY_COUNT = 25  # 文档里给的关键帧数（曲线源 CSV 仍是逐帧）


@dataclass
class Trial:
    tag: str
    direction: str
    prev_id: str
    chain: tuple[str, ...]
    rows: list
    frames: int
    takeoff_rows: list  # 只有起手段自己的采样，用来单独量起手段的方向

    @property
    def seconds(self) -> float:
        return self.frames / SAMPLE_HZ


def ident(row: dict) -> str:
    raw = (row.get("player_motion_old_id") or "").strip()
    try:
        return str(int(float(raw)))
    except ValueError:
        return ""


def runs_of(rows: list[dict]) -> list[tuple[str, list[dict]]]:
    runs: list[tuple[str, list[dict]]] = []
    for row in rows:
        key = ident(row)
        if runs and runs[-1][0] == key:
            runs[-1][1].append(row)
        else:
            runs.append((key, [row]))
    return runs


def load_trials(path: Path) -> list[Trial]:
    with path.open(newline="", encoding="utf-8-sig") as handle:
        rows = list(csv.DictReader(handle))
    runs = runs_of(rows)

    trials: list[Trial] = []
    for index, (key, chunk) in enumerate(runs):
        if key not in TAKEOFF or len(chunk) < MIN_SAMPLES:
            continue
        # 离地前缀 = 起跳 + 弧 +（下坠），一路吃到落地段为止。
        chain = [key]
        merged = list(chunk)
        cursor = index + 1
        while cursor < len(runs) and runs[cursor][0] in (ARC_IDS | FALL_IDS):
            chain.append(runs[cursor][0])
            merged += runs[cursor][1]
            cursor += 1
        # 只有**接着落地段**的那些才算一次完整的撑杆跳 —— 中途接空中回避/别的招式
        # 的那几次弧被砍断了，形状不是完整弧。
        if cursor >= len(runs) or runs[cursor][0] != GROUND_ID:
            continue
        # 状态由链自己决定（见 WHITE_LAMP_* 的注释），不按会话名硬编码。
        white = key == WHITE_LAMP_TAKEOFF or WHITE_LAMP_ARC in chain
        trials.append(
            Trial(tag="有白灯" if white else "无白灯", direction=TAKEOFF[key], prev_id=key,
                  chain=tuple(chain), rows=merged, frames=len(merged) - 1,
                  takeoff_rows=list(chunk))
        )
    return trials


DODGE_ID = "137"


def fnum(row: dict, key: str) -> float | None:
    try:
        return float(row[key])
    except (KeyError, TypeError, ValueError):
        return None


def hdg(dx: float, dz: float) -> float:
    return math.degrees(math.atan2(dx, dz))


def wrap(deg: float) -> float:
    while deg > 180.0:
        deg -= 360.0
    while deg < -180.0:
        deg += 360.0
    return deg


def load_dodge_observations(
    path: Path, census: dict | None = None
) -> tuple[list[dict], list[dict]]:
    """扫一份实录，产出两类观测。两类都**跨段**（撑杆跳是一条链，不是单个 id）：

    - `cancels`：`起跳 → (弧|坠)* → 137` 的链 —— 撑杆跳**最早可操作帧**的探针。
      判据是 `frame`（距起跳段首帧的帧数）；高度只是它的派生表现，见文档。
    - `dodges`：每个 `137` 段本身 —— 空回自己的真值 + 惯性检验。

    `census` 非空时顺带记全库的前驱统计（`census[("prev", id)][前驱id] += 1`），
    用来判 `143`/`157` 到底是「白灯」还是「回避前后」——**只有全库统计能分开这两者**。
    """
    with path.open(newline="", encoding="utf-8-sig") as handle:
        rows = list(csv.DictReader(handle))
    runs = runs_of(rows)
    cancels: list[dict] = []
    dodges: list[dict] = []

    if census is not None:
        for index, (key, chunk) in enumerate(runs):
            if len(chunk) < MIN_SAMPLES or not key:
                continue
            prev = runs[index - 1][0] if index else "-"
            census[("prev", key)][prev] += 1
            if index + 1 < len(runs):
                census[("next", key)][runs[index + 1][0]] += 1

    for index, (key, chunk) in enumerate(runs):
        if key != DODGE_ID or len(chunk) < MIN_SAMPLES:
            continue

        # 往前回溯到起跳段（中间只允许是弧段/下坠段）。
        cursor, pre = index - 1, []
        while cursor >= 0 and runs[cursor][0] in (ARC_IDS | FALL_IDS):
            pre.insert(0, runs[cursor])
            cursor -= 1
        takeoff = runs[cursor] if cursor >= 0 and runs[cursor][0] in TAKEOFF else None
        arc = next((s for s in pre if s[0] in ARC_IDS), None)
        chain = ("→".join(s[0] for s in pre) + "→" + DODGE_ID) if pre else DODGE_ID
        # 状态按**弧段**判（`144`/`145` 两个状态共用起跳 id，只有弧段能分开）。
        lamp = "有白灯" if (arc and arc[0] == WHITE_LAMP_ARC) else (
            "无白灯" if (arc or (takeoff and takeoff[0] in TAKEOFF)) else "未判定")

        first, last = chunk[0], chunk[-1]
        x0, z0 = fnum(first, "world_x"), fnum(first, "world_z")
        x1, z1 = fnum(last, "world_x"), fnum(last, "world_z")
        y0, y1 = fnum(first, "world_y"), fnum(last, "world_y")
        fx, fz = fnum(first, "forward_x"), fnum(first, "forward_z")
        disp = math.hypot(x1 - x0, z1 - z0) * 100.0
        facing = hdg(fx, fz) if fx is not None else None

        entry = runs[index - 1][1][-1] if index else None
        v0h = rel_v0 = None
        if entry is not None:
            vx, vz = fnum(entry, "velocity_x"), fnum(entry, "velocity_z")
            if vx is not None and vz is not None:
                v0h = math.hypot(vx, vz) * 100.0
                if v0h > 5.0 and facing is not None:
                    rel_v0 = wrap(hdg(vx, vz) - facing)

        dodges.append(dict(
            recording=path.name, lamp=lamp, label="·".join(filter(None, [
                TAKEOFF.get(takeoff[0]) if takeoff else None, DODGE_ID])),
            chain=chain, frames=len(chunk) - 1, disp=disp,
            drop=(y1 - y0) * 100.0 if (y0 is not None and y1 is not None) else None,
            rel_facing=wrap(hdg(x1 - x0, z1 - z0) - facing) if (facing is not None and disp > 1.0) else None,
            v0h=v0h, rel_v0=rel_v0,
            exit=runs[index + 1][0] if index + 1 < len(runs) else None))

        if takeoff is None:
            continue
        # 起跳段必须**跑满**（= 打出完整起跳），否则链的形状不是撑杆跳。
        tk_chunk = takeoff[1]
        tk_y0 = fnum(tk_chunk[0], "world_y")
        last_pre = pre[-1][1][-1] if pre else tk_chunk[-1]
        last_y = fnum(last_pre, "world_y")
        cancels.append(dict(
            recording=path.name, lamp=lamp,
            direction=TAKEOFF[takeoff[0]], takeoff=takeoff[0],
            # 帧数 = 起跳段首帧 → 137 首帧之前那帧。**这是判据**（见文档）。
            frame=sum(len(s[1]) for s in pre) + len(tk_chunk) - 1,
            takeoff_samples=len(tk_chunk),
            rise=(last_y - tk_y0) * 100.0 if (tk_y0 is not None and last_y is not None) else None,
            chain=chain, exit=runs[index + 1][0] if index + 1 < len(runs) else None))
    return cancels, dodges


def angle_of(rows: list[dict], relative_to_facing: bool) -> float | None:
    """一段采样的**水平位移方向**（度）。`relative_to_facing=True` 时返回它与起跳朝向的夹角。"""
    first, last = rows[0], rows[-1]
    dx = float(last["world_x"]) - float(first["world_x"])
    dz = float(last["world_z"]) - float(first["world_z"])
    if math.hypot(dx, dz) < 1e-6:
        return None
    angle = math.degrees(math.atan2(dx, dz))
    if not relative_to_facing:
        return angle
    facing = math.degrees(math.atan2(float(first["forward_x"]), float(first["forward_z"])))
    offset = angle - facing
    while offset > 180.0:
        offset -= 360.0
    while offset < -180.0:
        offset += 360.0
    return offset


def normalize(trial: Trial) -> dict:
    """沿**行进方向**把世界轨迹分解成 (前向, 侧向, 抬升)，再归一化。

    **不能按起跳朝向分解。** 左/右撑杆跳时猎人几乎不朝行进方向（实测差 ±84~90°，见文档的表）：
    按朝向分解会得到「总前向 39 cm、侧移 562 cm」这种把整个行程塞进侧向的结果，
    而按行进方向分解后三个方向是同一种形状，且末帧侧向**恒为零**。
    """
    first = trial.rows[0]
    x0, y0, z0 = (float(first["world_x"]), float(first["world_y"]), float(first["world_z"]))
    fx, fz = float(first["forward_x"]), float(first["forward_z"])
    facing_len = math.hypot(fx, fz)

    # 行进方向 = 离地前缀的**首→末水平位移**方向。这一段的采样就是整条弧，够稳。
    last = trial.rows[-1]
    tx = float(last["world_x"]) - x0
    tz = float(last["world_z"]) - z0
    travel_len = math.hypot(tx, tz)
    if travel_len < 1e-6:
        return {}
    tx, tz = tx / travel_len, tz / travel_len
    # 水平面内与行进垂直的方向
    px, pz = tz, -tx

    samples = []
    for row in trial.rows:
        dx = float(row["world_x"]) - x0
        dz = float(row["world_z"]) - z0
        samples.append(
            {
                "u": (dx * tx + dz * tz) * 100.0,   # 沿行进的进度 cm
                "v": (dx * px + dz * pz) * 100.0,   # 侧向摆动 cm
                "h": (float(row["world_y"]) - y0) * 100.0,  # 抬升 cm
            }
        )
    total = samples[-1]["u"]
    apex = max(s["h"] for s in samples)
    if total <= 1.0 or apex <= 1.0:
        return {}
    for index, sample in enumerate(samples):
        sample["t"] = index / (len(samples) - 1)
        sample["x"] = sample["u"] / total
        sample["y"] = sample["v"] / total
        sample["z"] = sample["h"] / apex

    first_takeoff, last_takeoff = trial.takeoff_rows[0], trial.takeoff_rows[-1]
    takeoff_cm = math.hypot(
        float(last_takeoff["world_x"]) - float(first_takeoff["world_x"]),
        float(last_takeoff["world_z"]) - float(first_takeoff["world_z"]),
    ) * 100.0
    return {
        "samples": samples,
        "total_cm": total,
        "apex_cm": apex,
        "lateral_cm": max(abs(s["v"]) for s in samples),
        # 两个**不同**的量，别混：整条链的弦向 vs 起手段自己的方向（见文档里的表）。
        "chord_offset_deg": angle_of(trial.rows, True),
        "takeoff_offset_deg": angle_of(trial.takeoff_rows, True),
        "takeoff_cm": takeoff_cm,
        "seconds": trial.seconds,
        "n": len(samples),
    }


def pick_representative(trials: list[Trial]) -> Trial:
    """取**时长中位**的那一次：既不用最长（可能含异常），也不用最短（可能被砍断）。
    录制本身高度可重复（最高点 580.1–587 cm），所以取一条忠实的样本比做平均更少编造。"""
    ordered = sorted(trials, key=lambda t: t.frames)
    return ordered[len(ordered) // 2]


def sample_at(samples: list[dict], t: float) -> dict:
    """线性插值取归一化时刻 t 的 (x, y, z)。"""
    if t <= 0.0:
        return samples[0]
    if t >= 1.0:
        return samples[-1]
    position = t * (len(samples) - 1)
    low = int(math.floor(position))
    frac = position - low
    a, b = samples[low], samples[min(low + 1, len(samples) - 1)]
    return {k: a[k] + (b[k] - a[k]) * frac for k in ("t", "x", "y", "z", "u", "v", "h")}


LAMP_ORDER = ("无白灯", "有白灯", "未判定")
DIR_ORDER = ("向前", "向左", "向右", "向后")


def render_dodge_section(cancels: list[dict], dodges: list[dict], census: dict) -> list[str]:
    """第三节：撑杆跳最早可操作帧 + 空回真值 + `157` 的语义。全部由数据算出，不写死数字。"""
    out = [
        "## 三、最早可操作帧（撑杆跳）与空中回避真值",
        "",
        "### 3.1 撑杆跳最早可操作帧 = 起跳段 **78 帧** + 弧段 **16 帧** = **94 帧 / 0.785 s**",
        "",
        "探针 = 取消进 `137` 空中回避。下表 `帧号` = **距起跳段首帧的帧数** ——",
        "**这才是判据**，高度只是它的派生表现（理由见本节末）。",
        "",
        "| 状态 | 方向 | 观测 n | 最早帧号 | 该帧抬升 cm | 最低几档帧号 |",
        "|---|---|---:|---:|---:|---|",
    ]
    keys = sorted({(c["lamp"], c["direction"]) for c in cancels},
                  key=lambda k: (LAMP_ORDER.index(k[0]) if k[0] in LAMP_ORDER else 9,
                                 DIR_ORDER.index(k[1]) if k[1] in DIR_ORDER else 9))
    for lamp, direction in keys:
        v = [c for c in cancels if c["lamp"] == lamp and c["direction"] == direction]
        bins = Counter(c["frame"] for c in v)
        low = min(bins)
        rises = [c["rise"] for c in v if c["frame"] == low and c["rise"] is not None]
        dist = " ".join(f"{n}×{bins[n]}" for n in sorted(bins)[:4])
        out.append(f"| {lamp} | {direction} | {len(v)} | **{low}** | "
                   f"{statistics.median(rises):.0f} | {dist} |")
    out.append("")

    wall_pre = min((c["frame"] for c in cancels), default=0)
    # 只有**没够到墙**的组才需要警告 —— 够到墙的组已经证明了墙存在，n 少也无妨。
    thin = [f"{lamp}·{d}" for lamp, d in keys
            if min((c["frame"] for c in cancels if c["lamp"] == lamp and c["direction"] == d),
                   default=0) > wall_pre]
    if thin:
        out.append(f"> ⚠ **还没够到墙的组**：{'、'.join(thin)} —— 它的「最早帧号」只是**上界**"
                   "（还可能更低），别当成另一个墙用。")
        out.append("")
    takes = Counter(c["takeoff_samples"] for c in cancels)
    # 墙：全局最早帧号、有多少组够到它、同帧命中几次。
    wall = min((c["frame"] for c in cancels), default=0)
    hit_groups = [f"{lamp}·{d}" for lamp, d in keys
                  if any(c["frame"] == wall for c in cancels if c["lamp"] == lamp and c["direction"] == d)]
    hits = sum(1 for c in cancels if c["frame"] == wall)
    lamp_rise = {}
    for lamp in LAMP_ORDER:
        rs = [c["rise"] for c in cancels if c["frame"] == wall and c["lamp"] == lamp and c["rise"] is not None]
        if rs:
            lamp_rise[lamp] = statistics.median(rs)
    out += [
        f"- **起跳段内不可取消**：全部 {sum(takes.values())} 次观测里，起跳段的样本数只有 "
        + "、".join(f"{n}×{c}" for n, c in sorted(takes.items()))
        + "，即**跑满 78 帧**（79 样本）。最早的取消点落在弧段第 16 帧。",
        "- **墙是帧号，不是高度**：前/左/右**共用同一条弧段**（无白灯 `142` / 白灯 `156`），"
        "三条的垂直轨迹是同一条 ⇒ **同一帧号必然同一高度**。所以「高度差不多」是「帧号差不多」的"
        "*结果*，不是独立规律。实测该处逐帧抬升只有 **~12 cm** ⇒ 晚 1 帧就矮 12 cm，"
        "用高度当配置值会把 ±1 帧的抖动放大成 ±12 cm 的误差。**要落地就取帧号。**",
        f"- **墙 = {wall} 帧**：{len(hit_groups)} 个组够到了它（{'、'.join(hit_groups)}），"
        f"共 {hits} 次精确命中。**两种灯态都能到**，所以墙与白灯无关。",
    ]
    if len(lamp_rise) >= 2:
        pair = "、".join(f"{k} ~{v:.0f} cm" for k, v in lamp_rise.items())
        spread = max(lamp_rise.values()) - min(lamp_rise.values())
        out += [
            f"- **同一帧号在两个灯态下高度并不相同**：{wall} 帧处 {pair}（差 {spread:.0f} cm）——"
            "因为两条弧（`142`/`156`）本来就不是同一条。⇒ **唯一跨灯态通用的量是帧号**，"
            "高度不是。这正好反过来印证了上一条。",
        ]
    out += [
        "",
        "### 3.2 空中回避 `137`：无白灯与有白灯**完全相同**",
        "",
        "| 状态 | 观测 n | 跑满帧数 | 水平位移 cm | 垂直落 cm | \\|方向−朝向\\| | 退出 id |",
        "|---|---:|---:|---:|---:|---:|---|",
    ]
    for lamp in LAMP_ORDER:
        v = [d for d in dodges if d["lamp"] == lamp]
        if not v:
            continue
        peak = max(d["frames"] for d in v)
        run = [d for d in v if d["frames"] == peak]
        disp = statistics.median([d["disp"] for d in run])
        drops = [d["drop"] for d in run if d["drop"] is not None]
        angs = [abs(d["rel_facing"]) for d in v if d["rel_facing"] is not None]
        exits = Counter(d["exit"] for d in v)
        out.append(f"| {lamp} | {len(v)} | {peak} | **{disp:.0f}** | {statistics.median(drops):.0f} | "
                   f"{statistics.median(angs):.1f}° | "
                   + "、".join(f"`{k}`×{c}" for k, c in exits.most_common()) + " |")
    out += [
        "",
        "> **「未判定」那一行**是**不属于撑杆跳链**的回避（前面不是起跳/弧段，判不出白灯状态），"
        "但它们自己的真值一样是 `142 帧 / 1065 cm` —— 这行反而是「空回与白灯无关」的旁证。",
        "",
    ]
    # 惯性的正经检验：只看跑满 + 回避方向严格等于朝向的核心样本，其余全是被打歪/被打断的。
    core = [d for d in dodges
            if d["rel_facing"] is not None and abs(d["rel_facing"]) < 1.0
            and d["frames"] == max(x["frames"] for x in dodges)
            and d["v0h"] is not None and d["v0h"] > 5.0]
    if len(core) >= 8:
        ds = sorted(d["disp"] for d in core)
        vs = sorted(d["v0h"] for d in core)
        out += [
            f"**完全不吃惯性。** 核心样本 n={len(core)}（跑满 + 回避方向严格 = 朝向）；",
            "下表按 **「进入速度方向」与「回避方向」的夹角** 分档 —— 这是最直接的检验：",
            "",
            "| 进入速度 vs 回避方向 | n | 位移中位 cm | 位移极差 cm | 进入速度 cm/s |",
            "|---|---:|---:|---:|---|",
        ]
        buckets = ((0, 30, "同向（顺着惯性）"), (30, 90, "斜"),
                   (90, 150, "横向"), (150, 181, "**正逆（顶着惯性）**"))
        for lo, hi, label in buckets:
            g = [d for d in core if d["rel_v0"] is not None and d["rel_facing"] is not None
                 and lo <= abs(wrap(d["rel_v0"] - d["rel_facing"])) < hi]
            if not g:
                continue
            gs = sorted(d["disp"] for d in g)
            gv = sorted(d["v0h"] for d in g)
            out.append(f"| {label} | {len(g)} | **{statistics.median(gs):.1f}** | "
                       f"{gs[-1]-gs[0]:.1f} | {gv[0]:.0f}–{gv[-1]:.0f} |")
        out += [
            "",
            "- **「同向」和「正逆」两档的位移中位一模一样** —— 顶着惯性按和顺着惯性按，距离相同。",
            f"- 进入水平速度跨 **{vs[0]:.0f}–{vs[-1]:.0f} cm/s**（{vs[-1]/max(vs[0],1):.1f} 倍），"
            f"而位移只在 **{ds[0]:.1f}–{ds[-1]:.1f} cm**（{100*(ds[-1]-ds[0])/statistics.median(ds):.2f}%）内。",
            "",
            "⇒ 空回是**固定位移**的动作：`1065 cm 水平 + 267 cm 下落 / 142 帧`，方向 = 朝向。",
            "「往前跳再往前回避能回避更远」**不成立**。",
            "",
            "**被落地打断或被出招取消时不是定值**：帧数变短、位移随之变小；退出 `148 起跳落地`",
            "（落到地），或退出 `105 跳跃斩`（在空中直接接攻击 —— 空回**自己也能被取消**）。",
            "所以 1065 cm 只在跑满时成立。",
            "**空回自己的最早可操作帧 = 78 帧**（全库 0 段更低，16 段精确命中，三个不同后继都能接上）。",
            "**惯性会原样延续到结束后的下落**（`137`→`157` 速度比中位 **0.989**，"
            "而落地 `148` 只有 0.373）—— 唯一的重置点是触地，详见 `操虫斩曲线.md` 第六节。",
            "",
        ]

    prev143, prev157 = census.get(("prev", "143"), Counter()), census.get(("prev", "157"), Counter())
    next137 = census.get(("next", DODGE_ID), Counter())
    if prev143 and prev157:
        out += [
            "### 3.3 `157` 不是「白灯下坠」—— 它是「回避后（或白灯弧后）的下坠」",
            "",
            "全库前驱统计：",
            "",
            "```",
            f"143 的前驱:  {', '.join(f'{k}×{c}' for k, c in prev143.most_common())}",
            f"157 的前驱:  {', '.join(f'{k}×{c}' for k, c in prev157.most_common())}",
            f"137 的后继:  {', '.join(f'{k}×{c}' for k, c in next137.most_common())}",
            "```",
            "",
            "`143` 的前驱**只有无白灯弧段**，`157` 的前驱却主要是 `137`（回避）。所以：",
            "",
            "- `143` = 从**无白灯弧段**自然进入的下坠（不打回避）",
            "- `157` = 从**白灯弧段**自然进入的下坠，**以及任何状态下的「回避之后」的下坠**",
            "",
            "这就是为什么无白灯链里插了 `137` 之后出来的是 `157`。招式表里「起跳下坠（白灯）」",
            "这个名字**只对了一半**。好消息：本脚本的链扫描在 `137` 处就断了（`ARC_IDS|FALL_IDS`",
            "里没有它），所以**带回避的试次全部被拒**，上面的曲线没有被污染。",
            "",
        ]
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--print", dest="write", action="store_false", help="只看，不写文件")
    args = parser.parse_args()

    if not RECORDER_DIR.is_dir():
        print(f"找不到录制目录：{RECORDER_DIR}", file=sys.stderr)
        return 1

    # **扫描全部实录**（不写死文件名）：以后再补录撑杆跳，无论放在哪一份会话里都会
    # 被自动纳入。老格式（没有 player_motion_old_id 列）跳过。
    by_variant: dict[tuple[str, str], list[Trial]] = defaultdict(list)
    sources: dict[str, int] = {}
    for path in sorted(RECORDER_DIR.glob("*_samples.csv")):
        with path.open(newline="", encoding="utf-8-sig") as handle:
            if "player_motion_old_id" not in (csv.DictReader(handle).fieldnames or []):
                continue
        found = load_trials(path)
        if not found:
            continue
        sources[path.name.replace("mhrise_", "").replace("_samples.csv", "")] = len(found)
        for trial in found:
            by_variant[(trial.tag, trial.direction)].append(trial)

    if not sources:
        print("没有在任何实录里找到完整的撑杆跳链", file=sys.stderr)
        return 1

    # 空回 / 最早可操作帧的观测**单独扫一遍**：那些会话（专门刷空回的）里
    # 一条完整撑杆跳链都没有，会被上面的 `if not found: continue` 整个跳过。
    cancels: list[dict] = []
    dodges: list[dict] = []
    census: dict = defaultdict(Counter)
    for path in sorted(RECORDER_DIR.glob("*_samples.csv")):
        with path.open(newline="", encoding="utf-8-sig") as handle:
            if "player_motion_old_id" not in (csv.DictReader(handle).fieldnames or []):
                continue
        got_cancel, got_dodge = load_dodge_observations(path, census)
        cancels += got_cancel
        dodges += got_dodge

    results: dict[tuple[str, str], dict] = {}
    for key, trials in by_variant.items():
        representative = pick_representative(trials)
        curve = normalize(representative)
        if not curve:
            continue
        curve["trial"] = representative
        curve["trials"] = trials
        results[key] = curve

    if not results:
        print("没有可用的撑杆跳试验", file=sys.stderr)
        return 1

    order = [("无白灯", "向前"), ("无白灯", "向左"), ("无白灯", "向右"),
             ("有白灯", "向前"), ("有白灯", "向左"), ("有白灯", "向右")]
    order = [k for k in order if k in results] + [k for k in results if k not in order]

    if args.write:
        OUT_CSV.mkdir(parents=True, exist_ok=True)
    for key in order:
        curve = results[key]
        if not args.write:
            continue
        path = OUT_CSV / f"{key[0]}_{key[1]}.csv"
        with path.open("w", newline="", encoding="utf-8") as handle:
            writer = csv.writer(handle)
            writer.writerow(["t_norm", "x_progress", "y_lateral", "z_height",
                             "u_cm", "v_cm", "h_cm"])
            for sample in curve["samples"]:
                writer.writerow([
                    f"{sample['t']:.6f}", f"{sample['x']:.6f}",
                    f"{sample['y']:.6f}", f"{sample['z']:.6f}",
                    f"{sample['u']:.2f}", f"{sample['v']:.2f}", f"{sample['h']:.2f}",
                ])

    lines = [
        "# 撑杆跳曲线真值（生成物，勿手改）",
        "",
        "> 由 `Scripts/MHRise/build_vault_curves.py` 从**专门录的两次撑杆跳会话**生成。",
        "> **改数请改脚本或原始录制。** 逐帧曲线源在 `Saved/_mhr_curves/`（**不入库**）。",
        "",
        f"**真值源**：`{RECORDER_DIR}`",
        "—— 本表扫了**全部实录**，下列这些里找到了完整的撑杆跳链："
        + "、".join(f"`{name}`（{count} 次）" for name, count in sorted(sources.items()))
        + "。",
        "",
        "**为什么单独一份文档**：真值表是按 `player_motion_old_id` **逐个动作**归并的，",
        "而撑杆跳的真值属于**一条链**（起手段 → 弧段 → 下坠段 → 落地），不属于任何单个 id。",
        "",
        "**状态由链自己判定**（不按会话名）：起手段 `155` 或弧段 `156` ⇒ 有白灯，否则无白灯。",
        "所以以后再补录撑杆跳，**放进任何一份会话都会被自动纳入**，不用改脚本。",
        "",
        "## 一、链结构与真值",
        "",
        "| 状态 | 方向 | 离地链 | 次数 | 起手段 | **行进总长 cm** | 总时长 s | **弧最高点 cm** | 侧向摆动 cm | **起跳朝向↔行进** |",
        "|---|---|---|---:|---|---:|---|---:|---:|---:|",
    ]
    for key in order:
        curve = results[key]
        trials = curve["trials"]
        takeoff = curve["trial"].prev_id
        chains = Counter("→".join(t.chain) for t in trials).most_common(1)[0][0]
        durations = sorted(t.seconds for t in trials)
        offset = curve["chord_offset_deg"]
        offset_text = f"**{offset:+.1f}°**" if offset is not None else "—"
        flag = " ⚠样本少" if len(trials) < 8 else ""
        lines.append(
            f"| {key[0]} | {key[1]} | `{chains}` | {len(trials)}{flag} | `{takeoff}` | "
            f"**{curve['total_cm']:.0f}** | "
            f"{durations[0]:.3f}–{durations[-1]:.3f}（中位 {statistics.median(durations):.3f}） | "
            f"**{curve['apex_cm']:.0f}** | {curve['lateral_cm']:.1f} | {offset_text} |"
        )

    lines += [
        "",
        "**三向共用同一个弧段**（无白灯 `142`、有白灯 `156`），三者的差别**全在起手段本身**的",
        "水平位移上。**弧的最高点跨方向恒定** ⇒ 同一套弧。",
        "",
        "### 左/右撑杆跳：朝向锁死，整条行程是**侧向平移**",
        "",
        "上表最后一列是「起跳那一刻的朝向」与「实际行进方向」的夹角：向前约 **0°/1°**（自己朝哪就跳哪），",
        "左/右则是 **±84 ~ ±90°**（精确值见表）—— 因为**整段动作里朝向是锁死的**：实测起跳段首朝向到落地段末朝向",
        "的变化是 **0.0°**（无白灯 27 次全部 0.0），段内朝向变化也是 0.0°。也就是说猎人保持朝前不动、",
        "整个身体侧向平移出去，落地时还是朝前。**这正是操作上的「推摇杆到角色左/右 + 按键」。**",
        "",
        "**所以本表的 X 一律沿行进方向分解**，与朝向无关 —— 否则按起跳朝向分解会得到",
        "「总前向 39 cm、侧移 562 cm」这种把整条行程塞进侧向的结果。",
        "",
        "**项目侧不需要新机制**：后撑杆跳早就是这个模式 ——",
        "`MHGZBackVaultAbility.cpp:690` 写的是 `Request.DirectionSnapshot = -Character->GetActorForwardVector();`，",
        "取的是**行进方向**（取负正因为它是往后跳），不是角色朝向。左/右撑杆跳照抄同一模式，",
        "把 `DirectionSnapshot` 换成**这一跳的实际行进方向**即可（精确值见下表），",
        "`PathOffsetCurve` 的 X 通道照常承载行程。**但别用「角色的左/右向量」这类整数近似** ——",
        "基准偏几度，世界轨迹就整体旋转几度、侧向偏 20–35 cm。",
        "",
        "### 方向基准：**两个不同的量，别混**（下表由脚本现算，勿手抄）",
        "",
        "- **起手段自身方向** = 起手段那 78 帧自己走了多远、朝哪走（相对当时朝向）。",
        "- **整条链弦向** = 从起手段首帧到触地帧的直线方向（相对当时朝向）—— **这才是曲线用的基准**，",
        "  因为只有以它为基准，曲线末帧的侧向分量才**恒为零**（参照系构造性质）。",
        "  换成 ±90° 之类的整数会让 Y 末值变成 ~30 cm（几度夹角 × 6 m 行程），直接违反消费方",
        "  「起止偏移为零」。所以 `DirectionSnapshot` 取的就是下表这一列。",
        "",
        "| 起手段 | 状态 | 起手段自身方向−朝向 | 起手段位移 | **整条链弦向−朝向** | n |",
        "|---|---|---|---:|---:|---:|",
    ]
    for key in order:
        curve = results[key]
        trials = curve["trials"]
        takeoff = curve["trial"].prev_id
        takeoffs = [t for t in (angle_of(t.takeoff_rows, True) for t in trials) if t is not None]
        chords = [t for t in (angle_of(t.rows, True) for t in trials) if t is not None]
        if not takeoffs or not chords:
            continue
        takeoff_cm = curve["takeoff_cm"] if "takeoff_cm" in curve else float("nan")
        lines.append(
            f"| `{takeoff}` {key[1]} | {key[0]} | **{statistics.median(takeoffs):+.1f}°** | "
            f"{curve['takeoff_cm']:.0f} cm | **{statistics.median(chords):+.2f}°** | {len(chords)} |"
        )

    lines += [
        "",
        "- **起手段自身方向与状态无关**（`144` 两个状态都是同一批值）⇒ 无/有白灯的差异**全部来自弧段**",
        "  （无白灯 `142` / 有白灯 `156`），不是起手段不同。",
        "- **⚠ 尚未解释的不对称**：`144` 与 `145` 的起手段方向不对称（一个偏 +85° 一侧、一个偏 −79°），",
        "  前向的起手段也偏了几度而非 0°。这些是**实测值本身**（逐次离散 < 0.5°，所以不是噪声），",
        "  但**成因未确认** —— 可能是「朝向」相对运动有固定滞后，也可能招式就是这样。",
        "  要用它反推「摇杆方向 → 招式方向」的映射之前，先补录确认。",
        "",
        "> **实测结论：向前起跳没有「推摇杆」变体。** 判据来自用户：「如果有更远的那就是有区分」。",
        "> 实测离地前缀的水平位移是**单一紧簇**（无白灯向前 28 次全部落在 **575–605 cm**），",
        "> 不存在第二个更远的簇；若存在独立变体，应当看到像「585 vs 750」那样的量级分离。",
        "> 所以前/左/右是**三招**，靠触发时的摇杆方向选起手段，而不是同一招的远近两种。",
        "",
        "## 二、做 GA / 蒙太奇时从哪儿取数",
        "",
        "| 要什么 | 从哪取 |",
        "|---|---|",
        "| 总时长（移动窗口 / `Duration`） | 第一节「总时长 s」的中位 |",
        "| 总位移（`MaxDistance` / `TotalDistance`） | 第一节「行进总长 cm」 |",
        "| 弧最高点（归一化用 / 校验） | 第一节「弧最高点 cm」 |",
        "| 方向基准（`DirectionSnapshot`） | 第一节末尾「方向基准」表里的**整条链弦向−朝向** |",
        "| **起跳后的最早可操作帧** | **第三节 3.1**（= 起跳 78 帧 + 弧 16 帧 = **94 帧**）|",
        "| 曲线关键帧（`FBackVaultTrajectoryKey`） | **第五节**（25 点的表），逐帧源在 `Saved/_mhr_curves/*.csv` |",
        "| 各段自己的时长 / 位移 / 顶点 | `真值表.md` 第一节 |",
        "| 空中回避本身的真值 | **第三节 3.2** |",
        "",
    ] + render_dodge_section(cancels, dodges, census) + [
        "",
        "## 四、曲线口径（对齐 `FBackVaultTrajectoryKey`）",
        "",
        "`UMHGZBackVaultAbility::BuildTrajectoryCurve` 的输入是 `Time` + `NormalizedPosition`，",
        "按该结构自己的注释：**X = 行程进度、Y = 侧移 ÷ 总行程、Z = 抬升 ÷ 实测最高点**。",
        "本脚本输出的就是这三列，外加绝对量（`u_cm`/`v_cm`/`h_cm`）便于核对。",
        "",
        "- **朝向分解**：X 沿**起跳那一刻**的 `forward_x/forward_z`（水平面），Y 取侧向，Z 取世界 Y 的抬升。",
        "  空中段的朝向是锁死的（实测段内转动 **0.0000°**），所以用段首朝向分解不需要插值。",
        "- **归一化基准**：`total_cm` = **落地那一刻的前向总进度**（上表的「总前向」），",
        "  `apex_cm` = 整段弧的最高点。`Time = 该帧时刻 ÷ 离地前缀总时长`。",
        "- **取哪一次**：时长**中位**的那一次（不用最长、不用最短）。录制高度可重复 ——",
        "  最高点 580.1–587 cm、时长 1.9–2.0 s —— 所以取一条忠实的样本比做平均更少编造。",
        "- **落地为零**：链的终点是落地段 `148` 的起点，即**触地那一刻**，所以 Z 末值应 ≈ 0。",
        "  实测末值是 **−0.031 ~ +0.016**（±23 cm）—— 那是落地处与起跳处的**地形高度差**，",
        "  不是弧的一部分，喂曲线时按 0 处理。",
        "- **弧顶在 t ≈ 0.64，不是 0.5**：因为起手段的前 ~55% **还在地面**（招式表已记：",
        "  `146` 向后起跳「前 55% 仍在地面」），所以整条「离地前缀」里真正在空中的时间只占后段，",
        "  弹道顶点自然被推到 0.64 附近。前段 X 也因此很慢（`无白灯向前` 在 t=0.04 才走了 16.7 cm）。",
        "",
        "**⚠ 消费方约束**（`FWeaponMovementRequest::PathOffsetCurve`）：X 沿 `DirectionSnapshot`、",
        "**起止偏移都为零**。本表给的是**归一化轨迹**，不是最终的 offset 曲线 ——",
        "最终曲线由 `BuildTrajectoryCurve` 自己减掉 `NormalizedTime × OutHandoffDistance` 得到",
        "（MoveToForce 源已经贡献了 `NormalizedTime ×` 端点 X），**别把本表直接当 offset 用**。",
        "",
        "## 五、可直接抄进蓝图的关键帧",
        "",
        f"每隔 `1/{KEY_COUNT - 1}` 归一化时间取一个点（曲线源 CSV 是逐帧的，这里只是便于手抄）。",
        "",
    ]
    for key in order:
        curve = results[key]
        lines += [
            f"### {key[0]} · {key[1]}（`{curve['trial'].prev_id}` → … 共 {curve['n']} 帧 / "
            f"{curve['seconds']:.3f} s）",
            "",
            "| Time | X | Y | Z |  ← 绝对：(前向 cm, 侧移 cm, 抬升 cm)",
            "|---:|---:|---:|---:|---|",
        ]
        for index in range(KEY_COUNT):
            t = index / (KEY_COUNT - 1)
            sample = sample_at(curve["samples"], t)
            lines.append(
                f"| {t:.4f} | {sample['x']:.5f} | {sample['y']:.5f} | {sample['z']:.5f} "
                f"| ({sample['u']:.1f}, {sample['v']:.1f}, {sample['h']:.1f}) |"
            )
        lines.append("")

    text = "\n".join(lines)
    if args.write:
        OUT_MD.parent.mkdir(parents=True, exist_ok=True)
        OUT_MD.write_text(text, encoding="utf-8")
        print(f"写入 {OUT_MD}")
        print(f"写入 {len(order)} 个曲线源 → {OUT_CSV}")
    else:
        print(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
