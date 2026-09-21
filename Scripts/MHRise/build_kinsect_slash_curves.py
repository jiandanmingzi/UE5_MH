#!/usr/bin/env python3
# Copyright MHGZ Project. All Rights Reserved.
"""从 MHRise 实录抽出**操虫斩链 + 觉虫击链**的真值与曲线。

**为什么单独一个脚本**（而不是并进 `build_truth_table.py` / `build_vault_curves.py`）：
每条链都有**七八个 id、多条分支**，而且真正的真值（突进的位移/速度曲线、瞄准范围、
命中后各段的时长）属于**整条链**，不属于任何单个 id。撑杆跳那份同理，但两者没有交集
（撑杆跳是 141/144/145→142→143）。

本脚本覆盖**两条没有公共 id、但汇合于 `189`**的链：操虫斩 `186→187→…` 与
觉虫击 `193→195→199→…`。两条链的**瞄准判据不同** —— 操虫斩看「突进是否精确水平」，
觉虫击看「相机横摆的累计行程」（见第七节）。

链形状（实测，见文档第一、七节）：

    （id 后的名字以 `docs/reference/虫棍招式表.md` 为准，本文件只写 id、不抄名字 ——
     抄一份就会在招式表改名后悄悄过期，实测踩过。具体名字看生成物。）

     193 → 195          地面蓄力 + 瞄准，合计 333 帧（2.78 s）
      → 199             恒 44 帧（0.367 s），方向 = 准心，垂直匀减 ~1200 cm/s²
          ├→ 206 → 77/79   未命中，飞满全程后落地
          └→ 189           猎虫撞上，199 提前结束（← 与操虫斩的汇合点）

    186                悬停瞄准段，**恒 86 帧、位移恒 0**（可转向，最大实测 +61.7°）
      → 187             两段式：帧 1–10「发射相」匀速直线 → 帧 11+ 「滑行相」速度掉到 ~1/4
          ├→ 188 → 182     （未命中分支）
          └→ 189           （命中分支）
                ├→ 200（**恒 46 帧**，位移恒 69.4/+46.0/83.3 cm，方位 = 段首朝向 **+180.00°**）
                │    → 201 → 203 → 落地
                ├→ 137（最早第 90 帧）
                ├→ 186（回悬停，最早第 90 帧）
                └→ 157 → 148

【口径（每一条都踩过，别改）】
- 真值源 `C:/apps/steam/.../MHGZ_AerialTrajectoryRecorder/`，**该目录不入库**。扫全部实录，
  老格式（没有 `player_motion_old_id` 列）跳过。
- 分段依据 `player_motion_old_id`（写成 "186.00000000"，需 `int(float())`）；`motion_l0_id` 无用。
- 帧数 = 样本数 − 1；采样率 **119.8 Hz**（1 帧 = 8.347 ms）。
- **向上轴是 Y**；位置单位米、速度 m/s ⇒ 一律 ×100 得厘米。
- **镜头符号自检**：录制器 1.7.0 的 `cam_forward_*` / `cam_pitch_deg` / `cam_yaw_deg` 反了 180°
  （`cam_q*` 原始四元数没问题）。判据是几何的 —— `dot(cam_forward, 相机→猎人)` 应当 ≈ +1
  （相机在猎人后方、看向猎人）；< 0 就是反的。**全库自动判**，不写死文件名。
- **突进方向只能从「发射相」取**（帧 1–10 的速度均值）。用整段弦向会系统性偏小：
  滑行相速度掉到 ~1/4，会把方向拽向末帧。
- **【最重要的一条，来自用户实测】操虫斩只有在玩家按住 RT 进入瞄准状态时才跟随准心。**
  没按 RT 时突进**恒为水平**（逐帧 `velocity_y` 精确 0.0）。**遥测里没有按键状态列**，
  所以只能靠「跟不跟随」反推。`20260919_194059_01` 那 11 条就是天然的「未瞄准」对照组。

用法：
    python Scripts/MHRise/build_kinsect_slash_curves.py            # 写曲线 + 文档
    python Scripts/MHRise/build_kinsect_slash_curves.py --print     # 只看，不写文件
"""

from __future__ import annotations

import argparse
import csv
import math
import statistics
import sys
import unicodedata
from collections import Counter, defaultdict
from pathlib import Path

RECORDER_DIR = Path(
    r"C:/apps/steam/steamapps/common/MonsterHunterRise/reframework/data/MHGZ_AerialTrajectoryRecorder"
)
PROJECT = Path(__file__).resolve().parents[2]
OUT_MD = PROJECT / "docs" / "reference" / "操虫斩曲线.md"
OUT_CSV = PROJECT / "Saved" / "_mhr_curves"

SAMPLE_HZ = 119.8
MIN_SAMPLES = 12
# 发射相长度：帧 1..10 是逐帧位完全相同的匀速直线（实测）。滑行相从帧 11 起。
EMIT_FRAMES = 10

# 名字一律不写在这里 —— 招式表是唯一权威，写第二份必然过期（实测踩过）。
# 要名字用 `name_of(ID)` 或 `id_label(ID)`。
HOVER = "186"      # 空中悬停瞄准段
DASH = "187"       # 前冲动作（两段式）
MISS = "188"       # 操虫斩未命中分支
HIT = "189"        # 操虫斩/觉虫击 命中后共用的舞踏
WYVERN = "200"     # 操虫穿刺起手
PIERCE = "201"     # 操虫穿刺过程
LAND_ATK = "203"   # 操虫穿刺落地攻击
DODGE = "137"      # 空中回避
FALL_W = "157"     # 起跳下坠（白灯）
LAND = "148"       # 起跳落地
GROUND_FALL = "182"  # 铁虫丝跳跃落地

# 觉虫击（绝虫击）链 —— **与操虫斩共用 `189` 这个汇合点**（第七、八节）。
# `193`→`195`→（`196` 瞬态）→`199`→ 命中则 `206`，若猎虫撞上目标则 `199` 提前结束进 `189`。
STRIKE_WINDUP = "193"   # 起手（恒 139 帧）
STRIKE_THROW = "195"    # 丢虫（恒 194 帧）
STRIKE_DASH = "199"     # 突进飞行（恒 44 帧 = 定速直线）
STRIKE_HIT = "206"      # 未命中后的坠落段（`199` 飞满全程才进这里）

# 「没按 RT」的判据（**觉虫击不能用 `velocity_y` 判**，它不按 RT 时也有非零仰角）：
# 没按 RT 时右摇杆能把镜头扫一整圈而猎人朝向冻住 → 用**相机横摆的累计行程**。
# 实测双峰且中间是空的（瞄准 ≤ 20°，未瞄准 ≥ 530°），500 不会踩边界。
UNAIMED_SPIN = 500.0
# 段内相邻采样间隔超过此值 ⇒ 录制卡帧（帧数会少于动画帧数）。与 build_truth_table 同一口径。
HITCH_GAP_SECONDS = 0.03
# 突进速率低于此值的算「慢变体」（正常 ≈4800，慢变体 ≈1420），不进速率统计。
STRIKE_SLOW = 3000.0
# 白灯变体的起跳 id —— 本工程里**唯一**能指示白灯的遥测证据（见 7.4）。
WHITE_LAMP_IDS = {"155", "156", "158", "159"}

# 只在 import 不到 `build_truth_table`（单独拷出去跑）时用。**平时不生效** ——
# 正常路径下 `name_of()` 会去读招式表。这里的条目也请以招式表为准。
KNOWN = {
    HOVER: "空中操虫斩", DASH: "操虫斩前冲动作", MISS: "操虫斩结束",
    HIT: "操虫斩/觉虫击命中进入舞踏", WYVERN: "强化操虫穿刺", PIERCE: "强化操虫穿刺过程",
    LAND_ATK: "强化操虫穿刺落地攻击", DODGE: "空中回避", FALL_W: "起跳下坠（白灯）",
    LAND: "起跳落地", GROUND_FALL: "铁虫丝跳跃/操虫斩落地",
    STRIKE_WINDUP: "绝虫击起手", STRIKE_THROW: "绝虫击丢虫",
    STRIKE_DASH: "绝虫击突进飞行", STRIKE_HIT: "猎虫滑翔命中以及觉虫击未命中的下坠段",
}

# 招式名的第二来源与「受击/失控排除集」都从 build_truth_table 借 —— **不要在这里另抄一份**，
# 否则真值表那边改了排除集，这份文档会悄悄用旧口径（`189` 的「最早 40 帧」就是
# 「取消进 `15` 击飞受击」造出来的假值，正是真值表里已经删掉的那个）。
try:
    import build_truth_table as BT

    def name_of(key: str) -> str:
        return BT.name_of(key)

    CONTROL_LOSS = set(BT.CONTROL_LOSS_IDS)
except Exception:  # pragma: no cover - 单独拷出去跑时的兜底
    def name_of(key: str) -> str:
        return KNOWN.get(key, "（未核对）")

    CONTROL_LOSS: set[str] = set()


def disp_width(text: str) -> int:
    """终端显示宽度：东亚全角算 2 列，其余算 1。

    给下面的链路图对齐用 —— `str.ljust` 数的是码点，中英混排会窄一半、图会散。
    """
    return sum(2 if unicodedata.east_asian_width(ch) in "WF" else 1 for ch in text)


def aligned(rows: list[tuple[str, str]], gap: int = 3) -> list[str]:
    """把 `(左边, 注释)` 渲染成 `左边   ← 注释`，**按显示宽度对齐**。

    链路图里左边名字长短不一（`189 操虫斩/觉虫击命中进入舞踏` vs `182 …落地`），
    `str.ljust` 数码点会让中文行窄一半、箭头参差；这里按真实列宽补空格。
    注释为空的条目原样输出，不留尾随空格。
    """
    width = max((disp_width(left) for left, note in rows if note), default=0)
    return [left if not note
            else left + " " * (width - disp_width(left) + gap) + "← " + note
            for left, note in rows]


def id_label(key: str, width: int = 0) -> str:
    """`<id> <招式表名>` —— 名字**每次现查招式表**，招式表改名后图自动跟上。

    传 `width` 时按显示宽度右侧补空格，用于链路图的竖线对齐。
    """
    text = f"{key} {name_of(key)}"
    return text + " " * max(0, width - disp_width(text))

# 定长判据与真值表**必须一致**：帧数按 6 帧分箱、最大箱占比 ≥30% 才算「定长」。
MODE_BIN_FRAMES = 6
MODE_SHARE = 0.30


def fixed_length(frames: list[int]) -> tuple[int | None, float]:
    """返回 (自然长度帧数 or None, 最大箱占比)。判据与 `build_truth_table.py` 完全相同。"""
    if not frames:
        return None, 0.0
    bins = Counter((f // MODE_BIN_FRAMES) * MODE_BIN_FRAMES for f in frames)
    _, count = bins.most_common(1)[0]
    share = count / len(frames)
    if share < MODE_SHARE:
        return None, share
    left = max(bins, key=lambda b: (bins[b], b))
    inside = sorted(f for f in frames if left <= f < left + MODE_BIN_FRAMES)
    return int(statistics.median(inside)), share


def fnum(row: dict, key: str) -> float | None:
    try:
        return float(row[key])
    except (KeyError, TypeError, ValueError):
        return None


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


def hdg(dx: float, dz: float) -> float:
    return math.degrees(math.atan2(dx, dz))


def wrap(deg: float) -> float:
    while deg > 180.0:
        deg -= 360.0
    while deg < -180.0:
        deg += 360.0
    return deg


def pos(row: dict):
    return (fnum(row, "world_x"), fnum(row, "world_y"), fnum(row, "world_z"))


def vel(row: dict):
    v = (fnum(row, "velocity_x"), fnum(row, "velocity_y"), fnum(row, "velocity_z"))
    return None if any(x is None for x in v) else v


def camera_sign(rows: list[dict]) -> int:
    """几何自检镜头朝向符号：`dot(cam_forward, 相机→猎人)` 应 ≈ +1。

    返回 +1（正常）或 −1（1.7.0 那次反了，需要取负）。**不靠文件名硬编码。**
    """
    dots = []
    for row in rows[::131]:
        c, p, f = pos(row), pos(row), None
        cv = (fnum(row, "cam_x"), fnum(row, "cam_y"), fnum(row, "cam_z"))
        fv = (fnum(row, "cam_forward_x"), fnum(row, "cam_forward_y"), fnum(row, "cam_forward_z"))
        if any(x is None for x in cv + fv):
            continue
        d = tuple(p[i] - cv[i] for i in range(3))
        n = math.sqrt(sum(x * x for x in d))
        if n < 1e-6:
            continue
        dots.append(sum(fv[i] * d[i] for i in range(3)) / n)
    if not dots:
        return 1
    return -1 if statistics.median(dots) < 0.0 else 1


def cam_pitch(row: dict, sign: int) -> float | None:
    v = fnum(row, "cam_pitch_deg")
    return None if v is None else v * sign


def seg_stats(chunk: list[dict]) -> dict:
    """一段 run 的位移 / 速度 / 高度统计（厘米、cm/s）。"""
    first, last = chunk[0], chunk[-1]
    p0, p1 = pos(first), pos(last)
    dx, dy, dz = p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]
    h = math.hypot(dx, dz) * 100.0
    vs = [v for v in (vel(r) for r in chunk) if v]
    speeds = [math.sqrt(sum(x * x for x in v)) * 100.0 for v in vs]
    vh = [math.hypot(v[0], v[2]) * 100.0 for v in vs]
    ys = [fnum(r, "world_y") for r in chunk]
    ys = [y for y in ys if y is not None]
    return dict(
        t0=fnum(first, "capture_time_s"),
        frames=len(chunk) - 1, h=h, v=dy * 100.0, d3=math.sqrt(h * h + (dy * 100.0) ** 2),
        peak=max(speeds) if speeds else None, last=speeds[-1] if speeds else None,
        first=speeds[0] if speeds else None,
        vh_first=vh[0] if vh else None,
        mean=statistics.mean(speeds) if speeds else None,
        vh_peak=max(vh) if vh else None, vh_last=vh[-1] if vh else None, vh_mean=statistics.mean(vh) if vh else None,
        y0=ys[0] * 100.0 if ys else None, y1=ys[-1] * 100.0 if ys else None,
        y_max=max(ys) * 100.0 if ys else None, y_min=min(ys) * 100.0 if ys else None,
    )


def dash_direction(chunk: list[dict]) -> tuple[float, float, float] | None:
    """发射相（帧 1..EMIT_FRAMES）的平均速度矢量 —— **突进方向的唯一正确定义**。

    用整段弦向会系统性偏小（滑行相速度掉到 ~1/4，会把方向拽向末帧）。
    """
    seg = [v for v in (vel(r) for r in chunk[1:EMIT_FRAMES + 1]) if v]
    if len(seg) < 4:
        return None
    vx = statistics.mean(v[0] for v in seg)
    vy = statistics.mean(v[1] for v in seg)
    vz = statistics.mean(v[2] for v in seg)
    n = math.sqrt(vx * vx + vy * vy + vz * vz)
    return None if n < 1e-6 else (vx / n, vy / n, vz / n)


def path_along(chunk: list[dict], d) -> list[dict]:
    """把一段轨迹投影到「突进方向」上：s = 沿突进方向的进度，lat = 垂直它的偏移。

    这样得到的曲线**与瞄准角无关**（发射相本来就是直线），运行时按 pitch/yaw 旋转即可。
    """
    p0 = pos(chunk[0])
    # 侧向的参考轴 = 水平面内垂直于突进方向水平投影的那个方向
    hx, hz = d[0], d[2]
    hn = math.hypot(hx, hz)
    if hn < 1e-6:
        side = (1.0, 0.0)
    else:
        side = (-hz / hn, hx / hn)
    out = []
    for k, row in enumerate(chunk):
        p = pos(row)
        dx, dy, dz = p[0] - p0[0], p[1] - p0[1], p[2] - p0[2]
        s = (dx * d[0] + dy * d[1] + dz * d[2]) * 100.0
        lat = (dx * side[0] + dz * side[1]) * 100.0
        v = vel(row)
        out.append(dict(
            frame=k, t=k / max(len(chunk) - 1, 1), s=s, lat=lat, y=dy * 100.0,
            speed=math.sqrt(sum(x * x for x in v)) * 100.0 if v else None,
            vy=v[1] * 100.0 if v else None,
            # 单位速度方向 —— 用来量「段内还算不算直线」（见 render_strike 的 _deviation）
            vdir=(lambda n: tuple(x / n for x in v))(math.sqrt(sum(x * x for x in v)))
            if v and math.sqrt(sum(x * x for x in v)) > 1e-9 else None,
        ))
    return out


def max_gap(chunk: list[dict]) -> float:
    """段内最大的相邻采样间隔（秒）。>0.03 就是录制卡帧 —— 帧数会因此比动画少，
    **这种偏差不是动画差异**（`193`/`195` 的少数 1 帧例外全是这么来的）。"""
    gaps = []
    for a, b in zip(chunk, chunk[1:]):
        t0, t1 = fnum(a, "capture_time_s"), fnum(b, "capture_time_s")
        if t0 is not None and t1 is not None:
            gaps.append(t1 - t0)
    return max(gaps) if gaps else 0.0


def cam_yaw_of(row: dict) -> float | None:
    fx, fz = fnum(row, "cam_forward_x"), fnum(row, "cam_forward_z")
    return None if fx is None or fz is None else hdg(fx, fz)


def facing_yaw_of(row: dict) -> float | None:
    fx, fz = fnum(row, "forward_x"), fnum(row, "forward_z")
    return None if fx is None or fz is None else hdg(fx, fz)


def _unwrap(vals: list[float]) -> list[float]:
    out = [vals[0]]
    for v in vals[1:]:
        out.append(out[-1] + wrap(v - out[-1]))
    return out


def cam_spin(rows: list[dict]) -> float | None:
    """一段时间里**相机横摆的累计行程**（度，带环绕解开）。

    **这就是「按没按 RT」的判据**（用户实测：按住 RT 时相机锁定、猎人不随镜头转；
    没按 RT 时右摇杆能把镜头扫一圈而猎人朝向**冻住不动**）。
    瞄准的段里相机几乎不动（< 20°），未瞄准的段里能扫 530–624°。
    """
    vals = [y for y in (cam_yaw_of(r) for r in rows) if y is not None]
    if len(vals) < 2:
        return None
    u = _unwrap(vals)
    return max(u) - min(u)


def facing_span(rows: list[dict]) -> float | None:
    """同一段时间里**猎人朝向的累计行程** —— 瞄准时它跟随相机（也在动），
    未瞄准时它被冻住（≈ 0）。与 `cam_spin` 一起看，两者**解耦**就是未瞄准。"""
    vals = [y for y in (facing_yaw_of(r) for r in rows) if y is not None]
    if len(vals) < 2:
        return None
    u = _unwrap(vals)
    return max(u) - min(u)


def facing_minus_cam(rows: list[dict]) -> float | None:
    """|猎人朝向 − 相机横摆| 的中位（度）。瞄准时 ≈ 0，未瞄准时是一个任意大数。"""
    ds = [wrap(a - b) for a, b in
          ((facing_yaw_of(r), cam_yaw_of(r)) for r in rows)
          if a is not None and b is not None]
    return None if not ds else statistics.median(ds)


def _fit_slope(series: list[float], hz: float = SAMPLE_HZ) -> tuple[float, float]:
    """对一段等间隔序列做最小二乘，返回 (斜率/秒, R²)。"""
    pts = [(i, v) for i, v in enumerate(series)]
    n = len(pts)
    if n < 3:
        return 0.0, 0.0
    mx = sum(p[0] for p in pts) / n
    my = sum(p[1] for p in pts) / n
    sxx = sum((p[0] - mx) ** 2 for p in pts)
    if sxx <= 0:
        return 0.0, 0.0
    a = sum((p[0] - mx) * (p[1] - my) for p in pts) / sxx
    b = my - a * mx
    ss = sum((p[1] - my) ** 2 for p in pts)
    sr = sum((p[1] - (a * p[0] + b)) ** 2 for p in pts)
    return a * hz, (1.0 - sr / ss if ss > 0 else 0.0)


def _dvy_dt(d: dict) -> float:
    """段内**垂直**分量的加速度（cm/s²）。跳过第 0 个样本。"""
    return _fit_slope([p["vy"] for p in d["path"][1:] if p["vy"] is not None])[0]


def _dvh_dt(d: dict) -> float:
    """段内**水平**分量的加速度（cm/s²）。重力不该影响它 —— 它是「水平是否匀速」的判据。"""
    return _fit_slope([math.sqrt(max((p["speed"] or 0) ** 2 - (p["vy"] or 0) ** 2, 0.0))
                       for p in d["path"][1:] if p["speed"] and p["vy"] is not None])[0]


def _speeds(d: dict) -> list[float]:
    """段内各样本的速率。**跳过第 0 个样本** —— 它的速度精确为 0（换 id 的过渡帧），
    算进去会把相对起伏虚报成 ~100%。"""
    return [p["speed"] for p in d["path"][1:] if p["speed"]]


def _spread(d: dict) -> float:
    """段内速度模长的相对起伏 (max−min)/中位 —— 用来判「定速」够不够格。"""
    v = _speeds(d)
    if len(v) < 3:
        return 0.0
    m = statistics.median(v)
    return 0.0 if m <= 0 else (max(v) - min(v)) / m


def _deviation(d: dict) -> float:
    """段内速度方向相对**段首方向**的最大偏角（度）。"""
    first = d["launch_dir"]
    worst = 0.0
    for p in d["path"][1:]:
        v = p.get("vdir")
        if not v:
            continue
        dot = max(-1.0, min(1.0, sum(a * b for a, b in zip(v, first))))
        worst = max(worst, math.degrees(math.acos(dot)))
    return worst


def launch_vector(chunk: list[dict], frames: int = 2):
    """突进的**发射速度** = 段内第 1..frames 个样本的速度均值，返回 (单位向量, 速率 cm/s)。

    第 0 个样本的速度**被重置为 0**（整条链的签名，别把它算进去），用整段均值则会被
    尾段拖偏。实测这条突进**逐帧恒速**（见 `render_strike` 的剖面），所以首帧即代表。
    """
    seg = [v for v in (vel(r) for r in chunk[1:frames + 1]) if v]
    if not seg:
        return None
    vx = statistics.mean(v[0] for v in seg)
    vy = statistics.mean(v[1] for v in seg)
    vz = statistics.mean(v[2] for v in seg)
    n = math.sqrt(vx * vx + vy * vy + vz * vz)
    return None if n < 1e-6 else ((vx / n, vy / n, vz / n), n * 100.0)


def load() -> tuple[dict, dict, dict]:
    """扫全部实录，返回 (id → [段], (前驱,id) → Counter, 逐次 186→187 明细)。"""
    by_id: dict[str, list[dict]] = defaultdict(list)
    transitions: dict[tuple[str, str], Counter] = defaultdict(Counter)
    dashes: list[dict] = []
    strikes: list[dict] = []
    lamp_marks: dict[str, float | None] = {}
    for path in sorted(RECORDER_DIR.glob("*_samples.csv")):
        with path.open(newline="", encoding="utf-8-sig") as handle:
            reader = csv.DictReader(handle)
            if "player_motion_old_id" not in (reader.fieldnames or []):
                continue
            rows = list(reader)
        sign = camera_sign(rows)
        runs = runs_of(rows)
        # 本份录制里白灯的**时序基准**：最后一个白灯变体 id（`155/156/158/159`）出现的时刻。
        # 之前没有白灯变体出现过 ⇒ 这份录制全程按「无白灯」记。
        lamp_marks[path.name[7:26]] = next(
            (fnum(runs[k][1][0], "capture_time_s") for k in range(len(runs) - 1, -1, -1)
             if runs[k][0] in WHITE_LAMP_IDS), None)
        for index, (key, chunk) in enumerate(runs):
            if not key or len(chunk) < MIN_SAMPLES:
                continue
            prev = runs[index - 1][0] if index else "-"
            transitions[(prev, key)][path.name] += 0
            transitions[(prev, key)]["n"] += 1
            st = seg_stats(chunk)
            st.update(recording=path.name[7:26], id=key, index=index,
                      prev=prev, next=runs[index + 1][0] if index + 1 < len(runs) else None,
                      cam_sign=sign)
            by_id[key].append(st)

            # 186→187：瞄准关系 + 曲线
            if key == DASH and prev == HOVER and index >= 1:
                d = dash_direction(chunk)
                if d is None:
                    continue
                hover = runs[index - 1][1]
                cam = cam_pitch(hover[-1], sign)
                st["pitch"] = math.degrees(math.atan2(d[1], math.hypot(d[0], d[2])))
                st["cam_pitch"] = cam
                st["path"] = path_along(chunk, d)
                # 碰撞朝向的偏差（瞄准打开时 ≈ 0；关掉时也可能 ≈ 0，两者不可区分）
                fx, fz = fnum(hover[-1], "forward_x"), fnum(hover[-1], "forward_z")
                st["dash_yaw_minus_facing"] = wrap(
                    hdg(d[0], d[2]) - hdg(fx, fz)) if fx is not None else None
                dashes.append(st)

            # 193 → 195 → 199：觉虫击链。`196` 是 <MIN_SAMPLES 的瞬态，`runs` 里
            # 它确实占一格，所以往后最多找 3 格就够（193,195,196,199）。
            if key == STRIKE_WINDUP:
                j = next((m for m in range(index + 1, min(index + 4, len(runs)))
                          if runs[m][0] == STRIKE_THROW), None)
                k = (next((m for m in range(j + 1, min(j + 4, len(runs)))
                           if runs[m][0] == STRIKE_DASH), None) if j is not None else None)
                if k is not None:
                    throw, dash = runs[j][1], runs[k][1]
                    lv = launch_vector(dash)
                    if lv:
                        _, speed = lv
                        strikes.append(dict(
                            recording=path.name[7:26], index=index, t0=fnum(chunk[0], "capture_time_s") or 0.0,
                            windup=len(chunk) - 1, throw=len(throw) - 1, dash=len(dash) - 1,
                            windup_gap=max_gap(chunk), throw_gap=max_gap(throw), dash_gap=max_gap(dash),
                            speed=speed, pitch=math.degrees(math.asin(lv[0][1])),
                            cam_pitch=statistics.median(
                                [c for c in (cam_pitch(r, sign) for r in dash) if c is not None]),
                            # 突进段内 |猎人朝向 − 相机横摆| 的中位（见 7.3）。**必须在 `199`
                            # 段内算**：相机在 `193`+`195` 里可能被扫了整圈，对解缠前的
                            # `cam_yaw` 取中位是垃圾值（未瞄准的那 5 条正是这种情况）。
                            az_minus_cam=statistics.median(
                                [abs(wrap(a - b)) for a, b in
                                 ((facing_yaw_of(r), cam_yaw_of(r)) for r in dash)
                                 if a is not None and b is not None]) if dash else None,
                            cam_span=cam_spin(chunk + throw),
                            face_span=facing_span(chunk + throw),
                            face_minus_cam=facing_minus_cam(chunk + throw),
                            st=seg_stats(dash), path=path_along(dash, lv[0]),
                            # 段首方向（单位向量）—— `_deviation` 的基准
                            launch_dir=lv[0],
                            # 后继要跳过 <MIN_SAMPLES 的瞬态：慢变体后面跟的是一个
                            # **1 样本**的 `206`，照抄会把它写成后继（真后继是 `77`）。
                            tail=next((runs[m][0] for m in range(k + 1, len(runs))
                                       if len(runs[m][1]) >= MIN_SAMPLES), None),
                            prev=prev,
                        ))
    return by_id, transitions, {"dashes": dashes, "strikes": strikes, "lamp": lamp_marks}


def median(values, digits: int = 1) -> str:
    vals = [v for v in values if v is not None]
    return "—" if not vals else f"{statistics.median(vals):.{digits}f}"


def rng(values, digits: int = 1) -> str:
    vals = sorted(v for v in values if v is not None)
    if not vals:
        return "—"
    if len(vals) == 1 or abs(vals[-1] - vals[0]) < 10 ** (-digits):
        return f"**{vals[0]:.{digits}f}**"
    return f"{vals[0]:.{digits}f}–{vals[-1]:.{digits}f}"


def pearson(xs: list[float], ys: list[float]) -> tuple[float, float] | None:
    if len(xs) < 3:
        return None
    mx, my = statistics.mean(xs), statistics.mean(ys)
    sxx = sum((a - mx) ** 2 for a in xs)
    syy = sum((b - my) ** 2 for b in ys)
    if sxx <= 0 or syy <= 0:
        return None
    sxy = sum((a - mx) * (b - my) for a, b in zip(xs, ys))
    return sxy / math.sqrt(sxx * syy), sxy / sxx


def render_strike(by_id: dict, trans: dict, strikes: list[dict], lamp_marks: dict) -> tuple[list[str], list[dict]]:
    """第七、八节：觉虫击（绝虫击）链 —— 与操虫斩**共用 `189` 这个汇合点**。"""
    if not strikes:
        return [], []
    good = [d for d in strikes if d["cam_span"] is not None and d["cam_pitch"] is not None]
    # 慢变体是**退化分支**（见 7.4），既不属于瞄准组也不属于未瞄准组。
    slow = [d for d in good if d["speed"] <= STRIKE_SLOW]
    fast = [d for d in good if d["speed"] > STRIKE_SLOW]
    # 「没按 RT」的判据在**方位**上：没按 RT 时能自由转镜头而猎人不跟着转，
    # 于是突进方位与相机横摆**脱钩**。判据量的是**相机横摆的累计行程**（见 7.3）。
    unaimed = [d for d in fast if d["cam_span"] >= UNAIMED_SPIN]
    aimed = [d for d in fast if d["cam_span"] < UNAIMED_SPIN]
    speeds = [d["speed"] for d in fast]
    fit = pearson([d["cam_pitch"] for d in aimed], [d["pitch"] for d in aimed])

    out = [
        "## 七、觉虫击（绝虫击）链：`193` → `195` → `199` → `206`",
        "",
        "> **这条链与操虫斩没有任何公共 id，但两者汇合于 `189`** —— 猎虫撞上目标那一刻，",
        f"> 无论你是从 `{DASH}` 还是从 `{STRIKE_DASH}` 进来，都会切到 `{HIT}`。",
        "> 两条入口的形态**不同**，见第八节。",
        "",
        "```",
        f"{id_label(STRIKE_WINDUP)}（恒 139 帧 / 1.160 s，猎人几乎静止）",
        f"  → {id_label(STRIKE_THROW)}（恒 194 帧 / 1.619 s，猎人缓慢挪动，**准心在这段里锁定**）",
        f"      → {id_label(STRIKE_DASH)}（恒 44 帧 / 0.367 s，方向由准心决定，见 7.2 / 7.3）",
        f"          ├→ {id_label(STRIKE_HIT)} → 77 {name_of('77')} / 79 {name_of('79')}"
        "   ← 未命中，飞满全程",
        f"          └→ {id_label(HIT)}   ← 猎虫撞上，`{STRIKE_DASH}` 提前结束",
        "```",
        "",
        "| 段 | n | 帧数 | 段内平均速率 | 段内平均水平速率 | 关键常数 |",
        "|---|---:|---|---:|---:|---|",
    ]
    note = {
        STRIKE_WINDUP: "猎人**几乎静止**（段内平均水平速率中位 ≈100 cm/s）",
        STRIKE_THROW: "猎人缓慢挪动；**段内弧长恒 479 cm**（极差 0.13%）",
        STRIKE_DASH: "水平匀速 + 垂直匀减 ~−1200 cm/s²（见 7.2）；第 0 样本速度 = 0",
        STRIKE_HIT: "**抬升峰值恒 122.5 cm**（28 段极差 1e-5 cm）、g ≈ −2875 cm/s²",
    }
    for key in (STRIKE_WINDUP, STRIKE_THROW, STRIKE_DASH, STRIKE_HIT):
        items = by_id.get(key, [])
        if not items:
            continue
        frames = [s["frames"] for s in items]
        natural, share = fixed_length(frames)
        fl = (f"**{natural}**（{min(frames)}–{max(frames)}，箱占 {share * 100:.0f}%）"
              if natural is not None else f"变长（{min(frames)}–{max(frames)}）")
        out.append(f"| `{key}` {name_of(key)} | {len(items)} | {fl} | "
                   f"{median([s['mean'] for s in items], 0)} cm/s | "
                   f"{median([s['vh_mean'] for s in items], 0)} cm/s | {note[key]} |")
    out += [
        "",
        "**`206` 是变长**（80–154 帧）：它的长度就是**从命中高度落回地面所需的时间**，",
        "所以不要给它定一个固定「时长」，要按抛物线驱动（重力 −2875 见 `下落物理.md` 口径）。",
        "",
        "> **本节数字经过一次独立复算**（另起脚本只读原始 CSV，不调用本生成器）：链数 37、",
        "> 段首速率 4834 cm/s（本脚本 4860，差 0.5%）、r = +0.985、未瞄准例索引 "
        "109.19 / 116.49 / 122.65 / 333.97 / 342.96 s、路径 1734.6 cm（本脚本 1734）—— 逐条吻合。",
        "> **对不上的两处已按复算结果改写**：① `199` **不是**定速直线（垂直是 −1200 的匀减速，见 7.2）；",
        "> ②「未瞄准 ⇒ 仰角固定 8.8°」是错的（见 7.3 最后那条警告）。",
        "",
        "### 7.1 三段时间长是**硬常数**",
        "",
        "`193` / `195` / `199` 的长度**不随瞄准、白灯、位置变化** —— 这是整条链最好用的一条：",
        "",
        "| 段 | 自然长度 | 覆盖 | 偏差来源 |",
        "|---|---|---|---|",
    ]
    # `193`/`195` 的 1 帧例外是否只是**录制卡帧**？不靠断言，直接量段内最大采样间隔，
    # 与全库的正常间隔比。（`199` 的短例完全是另一回事：玩家取消，见下。）
    NORM_GAP = statistics.median([d["windup_gap"] for d in strikes]) or 1.0 / SAMPLE_HZ
    for key, natural, field in ((STRIKE_WINDUP, 139, "windup_gap"),
                                (STRIKE_THROW, 194, "throw_gap"),
                                (STRIKE_DASH, 44, "dash_gap")):
        items = by_id.get(key, [])
        off = sorted(s["frames"] for s in items if s["frames"] != natural)
        if not off:
            out.append(f"| `{key}` | **{natural} 帧** | {len(items)}/{len(items)} | （无） |")
            continue
        tally = "、".join(f"{f} 帧 ×{off.count(f)}" for f in sorted(set(off)))
        dev = [d for d in strikes if d.get(field.replace("_gap", "")) != natural]
        if key == STRIKE_DASH:
            # `199` 的短例**不是掉帧而是玩家取消** —— 全部进了 `189`（猎虫撞上）。
            tails = Counter(d["tail"] for d in dev)
            why = ("**玩家取消，不是掉帧** —— 后继是 "
                   + "、".join(f"`{k}`×{v}" for k, v in tails.most_common()))
        else:
            g = max((d.get(field) or 0.0) for d in dev)
            why = (f"段内最大采样间隔 **{g * 1000:.1f} ms** = 正常间隔 {NORM_GAP * 1000:.1f} ms 的 "
                   f"**{g / NORM_GAP:.1f} 倍** ⇒ **录制卡帧**，不是动画差异")
        out.append(f"| `{key}` | **{natural} 帧** | "
                   f"{sum(1 for s in items if s['frames'] == natural)}/{len(items)} | {tally}（{why}） |")
    out += [
        "",
        "⇒ 做蒙太奇时这三段可以**当常数用**，不需要查表。`206` 不行（见上）。",
        "",
        "### 7.2 突进 `199` 的运动模型：**水平匀速 + 垂直匀减速 ~−1200 cm/s²**",
        "",
        "把满长 `199` 段的**水平与垂直分量分别做最小二乘**（跳过第 0 个样本 —— 它的速度",
        "**精确为 0**，是换 id 的过渡帧，37/37 无一例外）：",
        "",
        "| 分量 | 段内加速度 | n | 判读 |",
        "|---|---|---:|---|",
        f"| **水平** | **{statistics.median([_dvh_dt(d) for d in fast]):+.0f} cm/s²**"
        f"（IQR {statistics.quantiles([_dvh_dt(d) for d in fast], n=4)[2] - statistics.quantiles([_dvh_dt(d) for d in fast], n=4)[0]:.0f}）"
        f" | {len(fast)} | **严格匀速** —— 重力作用不到这里 |",
        f"| **垂直** | **{statistics.median([_dvy_dt(d) for d in fast]):+.0f} cm/s²**"
        f"（逐次 {min(_dvy_dt(d) for d in fast):.0f} … {max(_dvy_dt(d) for d in fast):.0f}，"
        f"R² 中位 {statistics.median([_fit_slope([p['vy'] for p in d['path'][1:] if p['vy'] is not None])[1] for d in fast]):.2f}）"
        f" | {len(fast)} | **匀减速** —— 但**不是**空中重力 |",
        "",
        "⇒ **既不是自由抛物线**（那该是 −2378，实测正好一半），**也不是匀速直线**（垂直确实在减速）。",
        "",
        "⚠ **上表垂直那一行是「全段、不掐尾、不设离地阈值」的口径**（−1201）。换成 `下落物理.md`",
        "的严格口径（**掐末 3 帧 + 只取 `world_y > 60 cm` 的样本 + 只取 R²>0.95 的段**）得到 "
        "**−1188 cm/s²** —— ",
        "而 `AerialFallGravityScale / 2 × 980 = 2.4246/2 × 980 = ` **1188.05**，**吻合到 0.004%**",
        "（对照：`137` 实测 −2378 vs 该常数整值 2376，差 0.08%）。",
        "",
        "**这不像巧合，但本节没有任何机制证据**说引擎真的取了半个重力 —— 也可能是原游戏",
        "的参数化恰好如此。**实现按实测值走，别自作主张去 × 0.5。**",
        "",
        "| 量 | 值 | n |",
        "|---|---|---:|",
        f"| 突进速率（段首，不含慢变体） | **{median(speeds, 0)} cm/s**"
        f"（范围 {min(speeds):.0f}–{max(speeds):.0f}） | {len(speeds)} |",
        f"| 三维路径长度 | **{median([d['st']['d3'] for d in fast], 0)} cm** | {len(fast)} |",
        f"| 段内方向相对段首的最大偏角 | 中位 **{median([_deviation(d) for d in fast], 1)}°**"
        f"（最大 {max(_deviation(d) for d in fast):.1f}°，出现在末尾撞停的链上） | {len(fast)} |",
        "",
        "> **取发射速度必须跳过第 0 个样本**：每条链 `199` 的**第 0 个样本速度精确为 0**",
        "> （37/37 无一例外）—— 那是换 id 的过渡帧，字面取「前两个样本」会把速率砍到 ≈2575 cm/s。",
        "",
        "### 7.3 方向：**方位跟随朝向、仰角跟随相机俯仰**",
        "",
        "两个分量来自**两个不同的东西**：",
        "",
        "| 分量 | 来源 | 实测 |",
        "|---|---|---|",
        "| **方位**（水平方向） | **猎人朝向**（`199` 段首的 `forward_*`） | "
        "与突进方位夹角中位 **0.15°**（37 条，最大 0.83°） |",
        "| **仰角**（俯仰） | **相机俯仰** | 见下面的拟合 |",
        "",
        "**瞄准起作用的链条是**：按住 RT → **猎人朝向被拽到相机横摆**（`193`+`195` 期间朝向跟随相机）",
        "→ 突进沿该朝向飞出。",
        "",
        "⇒ **「没按 RT」的签名在方位上，不在仰角上** —— 没按 RT 时可以自由转镜头而猎人**不跟着转**，",
        "于是突进方位与相机横摆**脱钩**。判据写死为：**`193`+`195` 期间 `cam_yaw` 的累计行程 ≥ 500°**",
        "（镜头被扫了整整一圈）。",
        "",
        "| 组 | 相机横摆累计行程 | 猎人朝向累计行程 | n |",
        "|---|---|---|---:|",
    ]
    if aimed:
        out.append(f"| **瞄准** | {min(d['cam_span'] for d in aimed):.1f}° – {max(d['cam_span'] for d in aimed):.1f}°"
                   f"（中位 {median([d['cam_span'] for d in aimed], 1)}°） | "
                   f"{median([d['face_span'] for d in aimed], 1)}°（跟随相机，同量级） | {len(aimed)} |")
    if unaimed:
        out.append(f"| **未瞄准** | **{min(d['cam_span'] for d in unaimed):.1f}° – "
                   f"{max(d['cam_span'] for d in unaimed):.1f}°** | **≈ 0°（冻住）** | {len(unaimed)} |")
    out += ["", "两组的相机行程是**双峰且中间是空的**，所以阈值取 500° 不会踩到边界。", ""]
    if fit:
        r, slope = fit
        intercept = statistics.mean([d["pitch"] - slope * d["cam_pitch"] for d in aimed])
        out.append(f"**仰角的拟合**（瞄准组）：`突进仰角 = {slope:.3f} × 相机俯仰 {intercept:+.2f}`，"
                   f"**r = {r:+.3f}**（n={len(aimed)}），两端夹紧在 "
                   f"[{min(d['pitch'] for d in aimed):.1f}°, {max(d['pitch'] for d in aimed):.1f}°]。")
        out.append("")
        out.append("斜率 ~0.7 而不是 1.0 —— 突进比准心「压得平」，**不能把 `camP` 直接当仰角用**。"
                   "（这个 0.985 有相当一部分来自相机的夹紧跨度 −66° ~ +56°：把 camP 限制在 ±10° 内，"
                   "r 会掉到 ~0.7 —— 说明小角度段只是近似线性，不是严格 1:1。）")
        out += ["", "**相机俯仰 → 突进仰角 全表**（实现时按这个插值，别用公式硬套）：", "",
                "| 相机俯仰 ° | 突进仰角 ° | n |", "|---:|---|---:|"]
        buckets: dict[float, list[float]] = {}
        for d in aimed:
            buckets.setdefault(round(d["cam_pitch"]), []).append(d["pitch"])
        for cam in sorted(buckets):
            vals = sorted(buckets[cam])
            span = f"**{vals[0]:.1f}**" if len(vals) == 1 else f"{vals[0]:.1f} – {vals[-1]:.1f}"
            out.append(f"| {cam:+.0f} | {span} | {len(vals)} |")
    if unaimed:
        out += ["", "**未瞄准组的逐条明细**（「没按 RT 的觉虫击」参照样本）：", "",
                "| 录制 | 相机横摆累计行程 | 猎人朝向累计行程 | **突进方位 − 相机横摆** | 相机俯仰 | 突进仰角 | 速率 cm/s |",
                "|---|---:|---:|---:|---:|---:|---:|"]
        for d in sorted(unaimed, key=lambda d: d["cam_span"]):
            out.append(f"| {d['recording']} | **{d['cam_span']:.1f}°** | {d['face_span']:.1f}° | "
                       f"**{d['az_minus_cam']:.0f}°** | "
                       f"{d['cam_pitch']:+.1f}° | {d['pitch']:+.1f}° | {d['speed']:.0f} |")
        out += [
            "",
            "> ⚠ **两条读法上的警告**（独立复算抓出来的）：",
            "> ① 这个判据**充分不必要** —— 它只能抓「不瞄准**并且**同时在甩镜头」的样本。",
            "> 7.4 那两例慢变体的相机行程只有 0.0°/5.4°，照样不是相机对准的突进，判据抓不到它们。",
            "> ② **不要**把「未瞄准组的仰角中位」读成「未瞄准时仰角固定在那一档」：那 5 条的相机俯仰",
            "> 只跨 5° 左右，仰角**仍然跟着它走**（`仰角 − camP` 与瞄准组是同一个偏移）。",
            "> **真正被冻住的是朝向，不是仰角。**",
            "> ③ `cam_yaw` 在 ±180° 处**折返**，累计行程必须先解缠（脚本已做）；不解缠永远算不出 >360°。",
        ]
    if slow:
        out += [
            "",
            f"### 7.4 慢变体：{len(slow)} 例突进只有 ≈{median([d['speed'] for d in slow], 0)} cm/s",
            "",
            "这几例的相机俯仰**恰好都停在夹紧值**（镜头压到底），突进速率掉到正常值的约 1/3.4、",
            "路径只剩 ~520 cm，**而且它们后面的 `206` 只有 1 个样本**（等于没有），随后直接进 `77`。",
            "**本节没能查清成因**，先按「瞄准退化分支」记下，实现时**不要照抄**：",
            "",
            "| 录制 | t0 s | 相机俯仰 | 突进仰角 | 速率 cm/s | 路径 cm | 后继 |",
            "|---|---:|---:|---:|---:|---:|---|",
        ]
        for d in sorted(slow, key=lambda d: d["t0"]):
            out.append(f"| {d['recording']} | {d['t0']:.1f} | {d['cam_pitch']:+.1f}° | "
                       f"{d['pitch']:+.1f}° | **{d['speed']:.0f}** | {d['st']['d3']:.0f} | `{d['tail']}` |")
        out += [
            "",
            "> 独立复算确认：它们**不是别的动作** —— id 序列、每段帧数（139/194/1/44）、动画层",
            "> （`motion_l0_id` / `motion_l0_end_frame`）与其余 35 条**逐项相同**，只是**根位移被压掉**。",
            "> 最可能是「准心压到地面（钳位）→ 上跃被抑制」，但**这是假设，遥测无法直接证明**。",
        ]

    # ---- 白灯对照 ----
    def after_lamp(item) -> bool:
        m = lamp_marks.get(item["recording"])
        return m is not None and item["t0"] > m

    wan = [d for d in fast if after_lamp(d)]
    wu = [d for d in fast if not after_lamp(d)]
    hit_rec = by_id.get(STRIKE_HIT, [])
    hit_wan = [d for d in hit_rec if after_lamp(d)]
    hit_wu = [d for d in hit_rec if not after_lamp(d)]
    out += [
        "",
        "### 7.5 白灯：**在这条链上找不到可测差异**",
        "",
        "⚠ **遥测里没有精华（buff）状态字段** —— 录制器不记录 extract。唯一的间接证据是",
        "**白灯变体的动作 id**（`155`/`156`/`158`/`159`）—— 它们的原始资产名是",
        "`AS_Unsh_W_Jump_Forward` / `_W_Jump_Over` / `AS_Unsh_Fall_W_Jump` / `_W_Jump_Back` / `_W_Jump_Over_Back`，",
        "是**全库唯一 5 个带 `W_` 的资产**；而 `193`/`195`/`199`/`206` **没有任何 `W_` 变体**。",
        "",
        "在这份录制里 `155`/`156` 只出现在 **t ≈ 249.3 s**（各 1 次），所以白灯状态**只在那一刻被直接证实**。",
        "下表按「t0 是否晚于该时刻」分组 —— **这不是物理判据，是时序判据**：",
        "",
        "| 参数 | 无白灯 中位 | 有白灯 中位 | n（无/有） |",
        "|---|---|---:|---|",
    ]
    for label, fn in (("`193` 帧数", lambda d: d["windup"]),
                      ("`195` 帧数", lambda d: d["throw"]),
                      ("`199` 帧数", lambda d: d["dash"]),
                      ("`199` 段首速率 cm/s", lambda d: d["speed"]),
                      ("`199` 路径 cm", lambda d: d["st"]["d3"]),
                      ("`199` 突进仰角 °", lambda d: d["pitch"])):
        out.append(f"| {label} | {median([fn(d) for d in wu], 1)} | {median([fn(d) for d in wan], 1)} | "
                   f"{len(wu)}/{len(wan)} |")
    out += [
        f"| `206` 帧数（命中后坠落） | {median([d['frames'] for d in hit_wu], 0)} | "
        f"{median([d['frames'] for d in hit_wan], 0)} | {len(hit_wu)}/{len(hit_wan)} |",
        f"| `206` 抬升峰值 cm | {median([d['y_max'] - d['y0'] for d in hit_wu if d['y0'] is not None], 3)} | "
        f"{median([d['y_max'] - d['y0'] for d in hit_wan if d['y0'] is not None], 3)} | "
        f"{len(hit_wu)}/{len(hit_wan)} |",
        "",
        "**但这条链的硬常数根本不吃分组** —— 7.1 那三行（`193` ≡ 139 / `195` ≡ 194 / `199` ≡ 44）",
        "加上 `195` 弧长 479 cm，**在全 37 次上一次都没动过**。白灯要在这条链上有效果，它们先得动。",
        "",
        "> ⚠ **上表 `206` 帧数那一行看起来有差，但它不是白灯效果**：`206` 的长度等于「从命中高度",
        "> 落回地面」的时间，而命中高度由**瞄准仰角**决定 —— 两个组的瞄准分布不同，加上后半段",
        "> 玩家取消 `199` 的频率也变了（前 249 s 取消 8/28、之后 1/9）。要检验白灯只能比**同仰角**的",
        "> 两条，本录制样本量不够。",
        "",
        "> **独立复算给出的另一种分法**（推断，**不是**遥测直读）：用「奔跑速度 4.4 ↔ 5.3 m/s 的阶跃」",
        "> 重建白灯窗口，得到白灯在 135–384 s 之间**断断续续亮了 8 段**：",
        "> `[135.3,140)`、`[168.9,177)`、`[188,197.6)`、`[215.5,219)`、`[226.5,229.7)`、`[246.5,273.8)`、",
        "> `[318,322)`、`[355.6,383.9)`（秒）。这样分组有几次觉虫击会被重新归类，但**结论不变**：",
        "> 置换检验 20000 次，`193`/`195`/`199` 帧数、`d193h`、`d206h`、`n206` 的 p 值全在 **0.14 ~ 1.00**，无一显著。",
        "> 顺带：那条窗口重建**解释了 `157`** —— 它在这份录制里出现的三个时刻（135.3 / 168.9 / 227.9 s）",
        "> 正是窗口的起点，**与它「起跳下坠（白灯）」的名字相符**，不是反例。",
        "",
        "**能说什么、不能说什么**：",
        "- 能说：白灯组（n≈9）与非白灯组在这条链的所有可测量上没有可测差异。",
        "- 不能说「效应严格为零」：n≈9 检测不出 **10–15% 以下**的小效应。",
        "- **原理限制**：若白灯只影响**伤害 / 动作值**，本项目遥测（不记录 HP、肉质、hitbox）",
        "  **从原理上看不见** —— 这一点只能靠游戏内伤害数据闭合。",
        "",
        f"## 八、三招链路：觉虫击 / 操虫斩 / {name_of(WYVERN)}",
        "",
        f"三者的 id 完全不重叠，**接口只有 `{HIT}`**（三条链唯一共用的那一段）：",
        "",
        "```",
        f"觉虫击  {id_label(STRIKE_WINDUP)} → {id_label(STRIKE_THROW)} → {id_label(STRIKE_DASH)}",
        *aligned([
            (f"          ├→ {id_label(HIT)}", f"命中（猎虫撞上，{STRIKE_DASH} 提前结束）"),
            (f"          └→ {id_label(STRIKE_HIT)} → 77 / 79", "未命中，飞满全程"),
        ]),
        "",
        f"操虫斩  {id_label(HOVER)} → {id_label(DASH)}",
        *aligned([
            (f"          ├→ {id_label(HIT)}", "命中"),
            (f"          ├→ {id_label(MISS)} → {id_label(GROUND_FALL)}", "未击中"),
            (f"          └→ {id_label(GROUND_FALL)}", "直接落地"),
        ]),
        "",
        f"{name_of(WYVERN)}只由 `{HIT}` 进入（28/28）：",
        f"  {id_label(WYVERN)} → {id_label(PIERCE)} → {id_label(LAND_ATK)} → 落地",
        "```",
        "",
        "**`189` 有三个入口**，形态各不相同（本节唯一需要注意的接口细节）：",
        "",
        "| 入口 | n | 帧数 | 抬升峰值 cm | 水平位移 cm | 段首高度 cm |",
        "|---|---:|---|---|---:|---|",
    ]
    # 循环变量原名 `label`，会遮蔽上面新加的 id_label() —— 已改名。
    for prev, row_label in ((STRIKE_DASH, f"`{STRIKE_DASH}` {name_of(STRIKE_DASH)}（**命中**）"),
                            (DASH, f"`{DASH}` {name_of(DASH)}（**命中**）"),
                            (MISS, f"`{MISS}` {name_of(MISS)} → `{HIT}`（**迟到的命中**）")):
        items = [s for s in by_id.get(HIT, []) if s["prev"] == prev]
        if not items:
            continue
        peaks = sorted({round(s["y_max"] - s["y0"], 1) for s in items if s["y0"] is not None})
        pdesc = (f"**恒 {peaks[0]}**" if len(peaks) == 1
                 else f"{len(peaks)} 个取值（{peaks[0]}–{peaks[-1]}）")
        out.append(f"| {row_label} | {len(items)} | {rng([s['frames'] for s in items], 0)} | {pdesc} | "
                   f"{median([s['h'] for s in items], 0)} cm | {median([s['y0'] for s in items], 0)} cm |")
    out += [
        "",
        "⇒ 由觉虫击进入的舞踏**抬升峰值是一个硬常数**（7 段全是同一个值），说明它带一个固定垂直冲量；",
        "另两个入口没有这个常数。**做蒙太奇时要按入口分开。**",
        "",
        "> ⚠ 独立复算的补充：那 **86** 段「由 `187` 进入」里，前驱 `187` run 只有 **3–12 样本**的占 49 段",
        "> —— 也就是**过半是「187 只闪了一下」的退化段**。把它们与「真打了一发前冲」的混在一个 86 里，",
        "> 会把「入口形态」讲糊；要精确建模得先按前驱 `187` 的长度分层。",
        "",
        f"> `{WYVERN}` / `{PIERCE}` / `{LAND_ATK}` 的真值见第一节表 —— 它们**只由 `{HIT}` 进入**，",
        "> 与你是用哪一招触发命中无关。",
        "",
    ]
    return out, fast


def render(by_id: dict, trans: dict, extra: dict) -> tuple[str, list[tuple[str, list[dict]]]]:
    dashes = extra["dashes"]
    lines = [
        "# 操虫斩 / 觉虫击链真值（生成物，勿手改）",
        "",
        "> 由 `Scripts/MHRise/build_kinsect_slash_curves.py` 从原始实录生成。",
        "> **改数请改脚本或原始录制。** 逐帧曲线源在 `Saved/_mhr_curves/`（**不入库**）。",
        "",
        f"**真值源**：`{RECORDER_DIR}` —— 全库扫描，老格式（无 `player_motion_old_id` 列）跳过。",
        "",
        "**为什么单独一份文档**：真值表是按 `player_motion_old_id` **逐个动作**归并的，",
        "而这两条链的真值（突进的瞄准范围与速度曲线、命中后各段时长、两条入口的接口差异）",
        "属于**整条链**，不属于任何单个 id。与 `撑杆跳曲线.md` 没有交集。",
        "",
        "本文件覆盖**两条没有公共 id、但汇合于 `189` 的链**：",
        "**操虫斩**（`186`→`187`→`188`/`189`，第一~六节）与",
        "**觉虫击（绝虫击）**（`193`→`195`→`199`→`206`/`189`，第七、八节）。",
        "",
        "## 一、链结构与逐段真值",
        "",
        "```",
        f"{id_label(HOVER)}（悬停瞄准，恒 86 帧、位移恒 0、可转向）",
        f"  → {id_label(DASH)}（两段式，见第三节）",
        *aligned([
            (f"      ├→ {id_label(HIT)}", "命中"),
            (f"      ├→ {id_label(MISS)} → {id_label(GROUND_FALL)}", "未命中"),
            (f"      └→ {id_label(GROUND_FALL)}", "直接落地"),
        ]),
        "",
        f"{id_label(STRIKE_WINDUP)} → {id_label(STRIKE_THROW)} → {id_label(STRIKE_DASH)}",
        *aligned([
            (f"      ├→ {id_label(HIT)}", f"猎虫撞上，{STRIKE_DASH} 提前结束"),
            (f"      └→ {id_label(STRIKE_HIT)} → 77 / 79", "未命中，飞满全程（见第七节）"),
        ]),
        "",
        f"**两条链在 {HIT} 汇合。** {HIT} 的后继：",
        f"      ├→ {id_label(WYVERN)} → {id_label(PIERCE)} → {id_label(LAND_ATK)}",
        f"      ├→ {id_label(DODGE)}（最早第 90 帧）",
        f"      ├→ {id_label(HOVER)}（回悬停，最早第 90 帧）",
        f"      └→ {id_label(FALL_W)} → {id_label(LAND)}",
        "```",
        "",
        "| id | 招式 | n | 帧数 | 水平位移 cm | 垂直位移 cm | 三维 cm | 高度 首→末 cm |",
        "|---|---|---:|---|---:|---:|---:|---|",
    ]
    for key in (HOVER, DASH, MISS, HIT, WYVERN, PIERCE, LAND_ATK,
                STRIKE_WINDUP, STRIKE_THROW, STRIKE_DASH, STRIKE_HIT):
        items = by_id.get(key, [])
        if not items:
            continue
        frames = [s["frames"] for s in items]
        # **定长判据必须与 `真值表.md` 完全一致**（6 帧分箱、占比 ≥30%），否则同一个招式
        # 在两份生成物里会给出不同说法（`187` 是「定长 29」还是「11–35」？）。
        natural, share = fixed_length(frames)
        fl = (f"**{natural}** 定长（{min(frames)}–{max(frames)}，箱占 {share * 100:.0f}%）"
              if natural is not None else f"变长（{min(frames)}–{max(frames)}）")
        lines.append(
            f"| `{key}` | {KNOWN[key]} | {len(items)} | {fl} | "
            f"{rng([s['h'] for s in items], 1)} | {rng([s['v'] for s in items], 1)} | "
            f"{rng([s['d3'] for s in items], 1)} | {median([s['y0'] for s in items], 0)} → "
            f"{median([s['y1'] for s in items], 0)} |"
        )
    lines += [
        "",
        "- `186` 悬停段**逐帧位完全冻结**（速度恒 0、位移恒 0），但**朝向可被玩家转动**",
        "  —— 它是**可瞄准阶段**，随后的 `187` 沿悬停结束那一刻的朝向飞出。",
        "- `200` 的位移**逐样本几乎相同**（样本间标准差 < 0.5 cm）⇒ 是脚本位移，不是物理。",
        "",
        "### 前驱 / 后继（含所有来源，便于判链）",
        "",
        "| id | 前驱 | 后继 |",
        "|---|---|---|",
    ]
    for key in (HOVER, DASH, MISS, HIT, WYVERN, PIERCE, LAND_ATK, DODGE,
                STRIKE_WINDUP, STRIKE_THROW, STRIKE_DASH, STRIKE_HIT):
        prevs = Counter()
        nexts = Counter()
        for (p, k), c in trans.items():
            if k == key:
                prevs[p] += c["n"]
            if p == key:
                nexts[k] += c["n"]
        if not prevs and not nexts:
            continue
        fmt = lambda c: "、".join(f"`{k}`×{v}" for k, v in c.most_common(6))
        lines.append(f"| `{key}` | {fmt(prevs)} | {fmt(nexts)} |")
    lines += ["", "## 二、做 GA / 蒙太奇时从哪儿取数", "",
              "| 要什么 | 从哪取 |", "|---|---|",
              "| 悬停段时长（`186`，固定） | 第一节表（恒 86 帧 / 0.718 s） |",
              "| 突进总位移 / 速度曲线 | **第四节**（曲线 + `Saved/_mhr_curves/操虫斩_187.csv`） |",
              "| 突进的方向口径（瞄准） | **第三节** |",
              "| 命中后各段时长与位移 | 第一节表（`189`/`200`/`201`/`203`） |",
              "| 舞踏 / 空回的最早可操作帧 | **第五节** |",
              "| 惯性是否延续到下落 | **第六节** |",
              "| **觉虫击**各段时长/突进速率/路径 | 第一节表 + **第七节** |",
              "| **觉虫击**的方向口径（不按 RT 怎么判） | **7.2** |",
              "| 觉虫击突进的逐帧曲线 | **第七节**（曲线源 `Saved/_mhr_curves/觉虫击_199.csv`） |",
              f"| 觉虫击/操虫斩/{name_of(WYVERN)}怎么衔接 | **第八节** |",
              ""]
    return "\n".join(lines), [(DASH, dashes)]


def render_aim(dashes: list[dict]) -> list[str]:
    good = [d for d in dashes if d["cam_pitch"] is not None]
    # 「没按 RT」的签名**不是「误差大」**（误差大的多是有瞄准但抖），而是
    # **突进俯仰精确为 0 而相机俯仰明显非零** —— 逐帧 velocity_y 就是 0.0。
    stray = [d for d in good if abs(d["pitch"]) <= 1.0 and abs(d["cam_pitch"]) >= 5.0]
    follow = [d for d in good if d not in stray]
    fit = pearson([d["cam_pitch"] for d in follow], [d["pitch"] for d in follow])
    out = [
        "## 三、瞄准口径：突进俯仰 ≈ 相机俯仰（**前提是按住 RT**）",
        "",
        "> **【用户实测，遥测里没有按键状态】** 操虫斩**只有在玩家按住 RT 进入瞄准状态时**才跟随准心；",
        "> 不按 RT 时突进**恒为水平**（逐帧 `velocity_y` 精确等于 0.0，不是≈0）。",
        "> 所以数据里只能看到「跟随 / 不跟随」两种结果，**看不到原因**。",
        "",
        f"全库 `186→187` 链 **{len(dashes)}** 条，其中能取到镜头俯仰的 **{len(good)}** 条：",
        f"**跟随准心 {len(follow)} 条**、**未瞄准 {len(stray)} 条**。",
        "",
        f"**未瞄准的判据写死为 `|突进俯仰| ≤ 1°` 且 `|相机俯仰| ≥ 5°`** —— 不是「误差大」，",
        "因为误差大的多半是**有瞄准但抖**（猎人转向的过渡帧）。真正的未瞄准是**精确水平**。",
        "",
    ]
    if fit:
        r, slope = fit
        intercept = statistics.mean([d["pitch"] - slope * d["cam_pitch"] for d in follow])
        out.append(f"**跟随组的拟合**：`突进俯仰 = {slope:.3f} × 相机俯仰 {intercept:+.2f}`，"
                   f"**r = {r:+.3f}**（n={len(follow)}）。")
        out.append("")
    cams = sorted(d["cam_pitch"] for d in good)
    pits = sorted(d["pitch"] for d in follow)
    out += [
        "| 量 | 值 | n |",
        "|---|---|---:|",
        f"| 相机俯仰实测范围 | **{cams[0]:.2f}° ~ {cams[-1]:.2f}°**（两端是硬夹紧，反复出现同一精确值） | {len(good)} |",
        f"| 突进俯仰（跟随组）范围 | **{pits[0]:.2f}° ~ {pits[-1]:.2f}°** | {len(follow)} |",
        "",
        "**未瞄准组的逐条明细**（这就是「没按 RT 的操虫斩」的参照样本）：",
        "",
        "| 录制 | 相机俯仰 | 突进俯仰 | 位移 水平/垂直/三维 cm | 187 帧 |",
        "|---|---:|---:|---|---:|",
    ]
    for d in sorted(stray, key=lambda d: d["cam_pitch"]):
        out.append(f"| {d['recording']} | {d['cam_pitch']:+.2f}° | **{d['pitch']:+.2f}°** | "
                   f"{d['h']:.0f} / {d['v']:+.0f} / {d['d3']:.0f} | {d['frames']} |")
    srcs = Counter(d["recording"] for d in stray)
    out += [
        "",
        f"⇒ 未瞄准样本集中在 **{'、'.join(f'`{k}`（{v} 条）' for k, v in srcs.most_common())}**。",
        "**这些样本不能当「瞄准样本」用**（它们的突进永远是水平的），但它们同时是**「未瞄准操虫斩」",
        "的唯一参照**：垂直位移恒 ≈ 0，水平位移因此比瞄准组短。",
        "",
        "**为什么不能靠 `velocity_y` 之外的办法分辨**：猎人在 `186` 悬停期间**可自由转向**，",
        "所以突进的水平方位 = 悬停末刻的猎人朝向（实测 `|突进方位 − 猎人朝向|` 中位 0.09°），",
        "而猎人朝向又跟随相机横摆。**只有俯仰能区分「瞄准开没开」。**",
        "",
        "**相机符号自检**（脚本自动做，见 `camera_sign()`）：判据是几何的 —— "
        "`dot(cam_forward, 相机→猎人)` 应当 ≈ +1。全库里 `20260919_005435_01` 是 **−0.968**"
        "（录制器 1.7.0 的已知缺陷），脚本已对它取负；其余录制全为 +0.96~+0.98。",
        "",
    ]
    return out


def render_curve(dashes: list[dict], num_points: int = 21) -> tuple[list[str], list[dict]]:
    """第三节之后是曲线：以**发射相跑满**的代表性样本，按突进方向归一化。"""
    full = [d for d in dashes if abs(d["frames"] - 29) <= 1 and "path" in d
            and d["cam_pitch"] is not None and abs(d["pitch"]) > 5.0]
    if not full:
        return [], []
    # 代表样本取**俯仰中位**那一条：不能取俯仰≈0 的（那是没按 RT 的），
    # 也不该取极值（上下限都被相机夹紧，形状被截断）。
    med = statistics.median(d["pitch"] for d in full)
    rep = min(full, key=lambda d: abs(d["pitch"] - med))
    path = rep["path"]
    total = path[-1]["s"]
    out = [
        "## 四、突进 `187` 的位移 / 速度曲线",
        "",
        "**两段式**（这条决定了「突进方向」只能从发射相取）：",
        "",
        "```",
        "帧 0      冻结（位移 0）",
        f"帧 1–{EMIT_FRAMES}   发射相：逐帧位移恒定、速度方向恒定到 0.1°（直线匀速）",
        f"帧 {EMIT_FRAMES + 1}–末   滑行相：合速度掉到约 1/4，方向开始漂",
        "```",
        "",
        f"**曲线坐标系**：先取发射相（帧 1–{EMIT_FRAMES}）的平均速度矢量作为**突进方向**，",
        "再把整段轨迹投影上去 —— `s` = 沿突进方向的进度、`lat` = 垂直它的偏移。",
        "这样得到的曲线**与瞄准角无关**（发射相本是直线），运行时按 pitch/yaw 旋转即可。",
        "**不要用整段弦向当突进方向**：滑行相会把方向拽向末帧，系统性偏小。",
        "",
        f"代表性样本：`{rep['recording']}`（相机俯仰 {rep['cam_pitch']:+.1f}°、突进 "
        f"{rep['pitch']:+.1f}°、{rep['frames']} 帧）。",
        "",
        "| 量 | 峰值 | 末帧 | 均值 | 范围 |",
        "|---|---:|---:|---:|---|",
        f"| 合速度 cm/s | **{max(s['speed'] for s in path if s['speed']):.0f}** | "
        f"{path[-1]['speed']:.0f} | "
        f"{statistics.mean([s['speed'] for s in path if s['speed']]):.0f} | "
        f"{rng([d['peak'] for d in dashes], 0)} |",
        f"| 突进总长 `s` cm | — | **{total:.1f}** | — | {rng([d['d3'] for d in dashes], 0)}（三维） |",
        f"| 侧向偏移 `|lat|` 最大 cm | — | — | — | "
        f"**{max(abs(s['lat']) for s in path):.4f}** |",
        "",
        f"**侧向偏移最大只有 {max(abs(s['lat']) for s in path):.4f} cm** ⇒ 突进是**严格平面**的",
        "（在由突进方向和竖直构成的平面内），可以直接用「俯仰角 + 水平方位」两个参数描述。",
        "",
        "### 关键帧（等归一化时间取样，可直接对照 `Saved/_mhr_curves/操虫斩_187.csv`）",
        "",
        "| Time | `s`/总长 | `lat`/总长 | 合速度 cm/s | 绝对 `s` cm | 绝对 `lat` cm | 绝对 y cm |",
        "|---:|---:|---:|---:|---:|---:|---:|",
    ]
    for i in range(num_points):
        t = i / (num_points - 1)
        s = min(path, key=lambda s: abs(s["t"] - t))
        out.append(f"| {t:.3f} | {s['s'] / total:.5f} | {s['lat'] / total:.5f} | "
                   f"{s['speed']:.0f} | {s['s']:.1f} | {s['lat']:.1f} | {s['y']:.1f} |")
    out.append("")
    return out, rep["path"]


def render_earliest(by_id: dict, target: str, label: str, total_hint: str) -> list[str]:
    items = by_id.get(target, [])
    if not items:
        return []
    nxt: dict[str, list[int]] = defaultdict(list)
    dropped: Counter = Counter()
    for s in items:
        if not s["next"]:
            continue
        # 受击/失控类后继**不作探针**（同 build_truth_table 的口径）——它们不是「玩家能操作了」，
        # 混进来会把最早帧压小。`189` 的假值 40 帧就是这么来的。
        if s["next"] in CONTROL_LOSS:
            dropped[s["next"]] += 1
            continue
        nxt[s["next"]].append(s["frames"])
    natural, share = fixed_length([s["frames"] for s in items])
    if natural is None:
        head = (f"全库 {len(items)} 段；**变长**（最大帧数箱只占 {share * 100:.1f}%，低于 {MODE_SHARE:.0%} 阈值）"
                f" —— 没有「自然长度」，下面的「最早帧」要连着样本数读。")
    else:
        head = (f"全库 {len(items)} 段；自身跑满 **{natural} 帧 / {natural / SAMPLE_HZ:.3f} s**"
                f"（帧数箱占 {share * 100:.1f}%）。")
    out = [f"### `{target}` {label}", "", head, "",
           "| 后继 id | 招式 | 最早帧 | 同帧次数 | 样本 | 全部取值 |",
           "|---|---|---:|---:|---:|---|"]
    for k, v in sorted(nxt.items(), key=lambda kv: min(kv[1]))[:6]:
        v = sorted(v)
        out.append(f"| `{k}` | {name_of(k)} | **{v[0]}** | {v.count(v[0])} | {len(v)} | "
                   + ", ".join(str(x) for x in v[:10]) + (" …" if len(v) > 10 else "") + " |")
    if dropped:
        out += ["", "> 已排除的受击/失控后继："
                + "、".join(f"`{k}`×{c}" for k, c in dropped.most_common())
                + " —— 它们的中断不是「玩家能操作了」。"]
    out += ["", f"> {total_hint}", ""]
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--print", dest="write", action="store_false", help="只看，不写文件")
    args = parser.parse_args()

    if not RECORDER_DIR.is_dir():
        print(f"找不到录制目录：{RECORDER_DIR}", file=sys.stderr)
        return 1
    by_id, trans, extra = load()
    if not extra["dashes"]:
        print("没有找到任何 186→187 链", file=sys.stderr)
        return 1

    head, _ = render(by_id, trans, extra)
    strike, strike_good = render_strike(by_id, trans, extra["strikes"], extra["lamp"])
    aim = render_aim(extra["dashes"])
    curve, path = render_curve(extra["dashes"])
    early = ["## 五、最早可操作帧", "",
             f"判据只有一个：**该段的帧数小于自身跑满长度**，说明玩家在它自然结束前接了下个动作。",
             "**除受击/失控外，任何后继都可以当探针**（见 `真值表.md` 的口径）。", ""]
    early += render_earliest(by_id, HIT, "命中进入舞踏",
                             "**主簇是 90 帧：12 次精确命中同一个帧号**（跨两个后继 `137`/`186`），是硬墙的签名。"
                             "表里那个 78 帧是**孤点**，只有 1 次，而且它的**上升相位比其它试次早约 17 帧**"
                             "（f28 起升 vs f45），**帧号与主簇不可直接比** —— 若以「相位」为口径，门可能更低。")
    early += render_earliest(by_id, DODGE, "空中回避",
                             "全库 0 段短于此帧号。此时回避还没跑完（位移 585 cm、仍在爬升）。"
                             "**空回本身的完整真值（跑满 142 帧 / 1065 cm / 方向恒为朝向）见 `撑杆跳曲线.md` 第三节。**")
    inertia = [
        "## 六、惯性是否继承到动作结束后的下落",
        "",
        "判据写死：**后继首帧速度 ÷ 前驱末帧速度**。≈1 就是「原样继承、没有新增推力也没有制动」。",
        "",
        "| 链 | n | 水平速度比（中位） | 说明 | 判定 |",
        "|---|---:|---|---|---|",
    ]
    for a, b, note in ((DODGE, FALL_W, "回避 → 下坠"),
                       (DASH, MISS, f"突进 → {name_of(MISS)}")):
        pairs = []
        items = by_id.get(a, [])
        for s in by_id.get(b, []):
            if s["prev"] != a:
                continue
            # 配对的必须是**紧邻**的那一段（同一份录制、下标差 1）。
            src = next((x for x in items if x["recording"] == s["recording"]
                        and x["index"] + 1 == s["index"]), None)
            # 比值 = **后继首帧** ÷ **前驱末帧**（不能拿后继峰值比 —— 那会把滑行/起跳的
            # 加速段算成"惯性放大"，比值平白多出 8%）。
            if src and src["vh_last"] and s["vh_first"] is not None:
                pairs.append((src["vh_last"], s["vh_first"]))
        if not pairs:
            continue
        ratios = sorted(b / max(a2, 1e-9) for a2, b in pairs)
        near = sum(1 for r in ratios if 0.9 <= r <= 1.1)
        inertia.append(f"| `{a}` → `{b}`（{note}） | {len(ratios)} | **{statistics.median(ratios):.3f}** | "
                       f"（{near}/{len(ratios)} 落在 0.9–1.1） | **继承** |")
    # 对照组：落地把水平速度砍到一个与入速无关的固定值。
    land_pairs = []
    for s2 in by_id.get(LAND, []):
        if s2["prev"] != DODGE:
            continue
        src = next((x for x in by_id.get(DODGE, []) if x["recording"] == s2["recording"]
                    and x["index"] + 1 == s2["index"]), None)
        if src and src["vh_last"] and s2["vh_first"] is not None:
            land_pairs.append((src["vh_last"], s2["vh_first"]))
    if land_pairs:
        lr = sorted(b / max(a2, 1e-9) for a2, b in land_pairs)
        lands = sorted(b for _, b in land_pairs)
        inertia.append(
            f"| `{DODGE}` → `{LAND}`（**对照组：落地**） | {len(lr)} | "
            f"**{statistics.median(lr):.3f}** | 末帧 {statistics.median(a2 for a2, _ in land_pairs):.0f} → "
            f"首帧 **{statistics.median(lands):.0f} cm/s**（0/{len(lr)} 落在 0.9–1.1） | **清零** |")
    inertia += [
        "",
        "⇒ **两个动作的下落都原样继承惯性**：没有新增推力，也没有制动。",
        "唯一的重置点是**触地**（`148` 落地把水平速度砍到一个与入速无关的固定值）。",
        "",
        "> ⚠ `157` 段本身很短（中位 9 帧），所以「下落形状」不能用短段画曲线；",
        "> 但速度比值不受段长影响，结论成立。详见 `撑杆跳曲线.md` 第三节对 `137` 本身的真值。",
        "",
    ]
    text = "\n".join([head, *aim, *curve, "", *early, *inertia, "", *strike])

    if args.write:
        OUT_MD.parent.mkdir(parents=True, exist_ok=True)
        OUT_MD.write_text(text, encoding="utf-8")
        print(f"写入 {OUT_MD}")
        if path:
            OUT_CSV.mkdir(parents=True, exist_ok=True)
            target = OUT_CSV / "操虫斩_187.csv"
            total = path[-1]["s"] or 1.0
            with target.open("w", newline="", encoding="utf-8") as handle:
                writer = csv.writer(handle)
                writer.writerow(["t_norm", "s_norm", "lat_norm", "s_cm", "lat_cm", "y_cm",
                                 "speed_cm_s", "vy_cm_s"])
                for s in path:
                    writer.writerow([f"{s['t']:.6f}", f"{s['s']/total:.6f}", f"{s['lat']/total:.6f}",
                                     f"{s['s']:.2f}", f"{s['lat']:.2f}", f"{s['y']:.2f}",
                                     f"{s['speed']:.1f}", f"{s['vy']:.1f}"])
            print(f"写入曲线源 → {target}")
        if strike_good:
            starget = OUT_CSV / "觉虫击_199.csv"
            # 曲线与瞄准角无关（发射相是直线），所以按**各自的实际突进方向**投影，
            # 再除以各自的路径长做归一 —— 运行时按准心方向 × 总长 旋转即可。
            with starget.open("w", newline="", encoding="utf-8") as handle:
                writer = csv.writer(handle)
                writer.writerow(["row", "t0_s", "frame", "s_cm", "s_norm",
                                 "lat_cm", "y_cm", "speed_cm_s", "vy_cm_s", "total_cm"])
                row = 0
                for d in strike_good:
                    total = d["st"]["d3"] or 1.0
                    for pt in d["path"]:
                        writer.writerow([row, f"{d['t0']:.3f}", pt["frame"], f"{pt['s']:.2f}",
                                         f"{pt['s'] / total:.6f}", f"{pt['lat']:.2f}", f"{pt['y']:.2f}",
                                         f"{pt['speed']:.1f}" if pt["speed"] else "",
                                         f"{pt['vy']:.1f}" if pt["vy"] is not None else "",
                                         f"{total:.2f}"])
                        row += 1
            print(f"写入曲线源 → {starget}")
    else:
        print(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
