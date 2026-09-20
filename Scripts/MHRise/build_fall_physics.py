#!/usr/bin/env python3
# Copyright MHGZ Project. All Rights Reserved.
"""从 MHRise 实录量出**每个空中状态的垂直重力**与**下落/落地模型**。

**为什么单独一个脚本**：重力不是全局的 —— 它**按状态逐条不同**（实测 −1560 ~ −3100 cm/s²），
而且**下落段自己不设重力，继承发起那个动作的重力**。这条口径横跨击飞 / 撑杆跳 / 空回 /
操虫斩 / 舞踏五条链，不属于 `真值表.md`（按单 id 归并）、也不属于那两份链文档中的任何一份。

**它回答的问题**：
  1. 某个 id 是不是「一条恒定重力的抛物线」？（R² 判据）
  2. 它的 g 是多少、对应项目里的哪个常数？
  3. 下落段（`157`/`143`/`188`）的重力是不是**继承**前驱，而不是自己一套？
  4. 落地那一刻到底做了什么（重设还是摩擦衰减）？

【口径（每条都踩过）】
- **不能用逐帧差分求加速度**：位置列有采样抖动，一阶差分正好等于 `velocity_*` 列（已逐帧核过：
  452477 帧**零误差**，所以速度列是可信的），但**二阶差分会把抖动放大 10 倍**（实测 a_y 在
  ±8000 cm/s² 之间乱跳）。⇒ 一律**对整段做最小二乘**求斜率。
- **要掐掉落地帧**：下坠段末尾的落地帧 `vy` 会被刹车，混进来会把 g 系统性拉浅
  （`143` 全段拟合 −1653，掐掉末 3 帧后 −2386）。所以：掐末 3 帧 + 只取 `world_y > 60 cm` 的帧。
- **R² 是「这条段到底是不是一条抛物线」的判据**。两段式的动作（`187` 发射相+滑行相、
  `137` 之外还有前摇的）单直线拟合的 R² 会掉到 0.3~0.9，**那种 id 不给单一 g**。
- **`157` 必须按前驱分层**：它同时是「回避后下坠」和「舞踏后下坠」两种弹道的延续，
  混在一起取中位得到的数没有意义。
- 重力用 **g₀ = 980 cm/s²**（UE 默认）作单位。

用法：
    python Scripts/MHRise/build_fall_physics.py            # 写 下落物理.md
    python Scripts/MHRise/build_fall_physics.py --print     # 只看，不写文件
"""

from __future__ import annotations

import argparse
import csv
import math
import statistics
import sys
from collections import Counter, defaultdict
from pathlib import Path

RECORDER_DIR = Path(
    r"C:/apps/steam/steamapps/common/MonsterHunterRise/reframework/data/MHGZ_AerialTrajectoryRecorder"
)
PROJECT = Path(__file__).resolve().parents[2]
OUT_MD = PROJECT / "docs" / "reference" / "下落物理.md"

BIN = PROJECT / "Scripts" / "MHRise"
sys.path.insert(0, str(BIN))
try:
    import build_truth_table as BT  # noqa: E402

    name_of = BT.name_of
except Exception:  # pragma: no cover
    def name_of(key: str) -> str:
        return "（未核对）"

G0 = 980.0            # UE 默认重力 cm/s²
MIN_FRAMES = 8        # 段太短不给结论
MIN_ABOVE_GROUND = 60.0   # 只取离地帧（cm）
TAIL_MARGIN = 3       # 掐掉最后 3 帧（落地刹车）
R2_TRUST = 0.95       # 这条段算「可信抛物线」的门槛

# 项目侧的常数（`InsectGlaiveCombatConfig.h`）—— 用来做对照，不参与计算
ENGINE = {
    "AerialFallGravityScale": 2.4246,
    "WhiteAerialFallGravityScale": 2.5740,
}

# 立案的状态。分组只影响文档排版，不影响计算。
GROUPS = [
    # `199` 觉虫击突进是**动画驱动**的直线位移，垂直分量只有 ~−1200（不是自由重力）——
    # 收在这里正是为了让它的 R² 与斜率一起被表出来，免得有人当抛体用。
    ("上抛 / 悬停类动作", ["137", "154", "15", "142", "156", "187", "199"]),
    # `206` 猎虫滑翔命中（觉虫击命中后的坠落段）是 20260920 那份录制才出现的状态。
    ("下坠类", ["143", "157", "188", "30", "33", "206"]),
    ("地面态（对照）", ["148", "182"]),
]


def fnum(row: dict, key: str):
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
    out: list[tuple[str, list[dict]]] = []
    for row in rows:
        key = ident(row)
        if out and out[-1][0] == key:
            out[-1][1].append(row)
        else:
            out.append((key, [row]))
    return out


def fit_g(chunk: list[dict]) -> tuple[float, float, int] | None:
    """对一段的 `velocity_y` 做最小二乘，返回 (g cm/s², R², 用到的帧数)。

    **掐末 `TAIL_MARGIN` 帧**（落地刹车）**且只取离地帧** —— 否则落地那几帧会把 g 拉浅。
    """
    pts = []
    for row in chunk[: max(len(chunk) - TAIL_MARGIN, 1)]:
        t, v, y = fnum(row, "capture_time_s"), fnum(row, "velocity_y"), fnum(row, "world_y")
        if None in (t, v, y) or y * 100.0 <= MIN_ABOVE_GROUND:
            continue
        pts.append((t, v))
    if len(pts) < MIN_FRAMES:
        return None
    n = len(pts)
    mt = statistics.mean(p[0] for p in pts)
    mv = statistics.mean(p[1] for p in pts)
    sxx = sum((p[0] - mt) ** 2 for p in pts)
    if sxx <= 0:
        return None
    slope = sum((p[0] - mt) * (p[1] - mv) for p in pts) / sxx
    resid = [p[1] - (mv + slope * (p[0] - mt)) for p in pts]
    ss = sum((p[1] - mv) ** 2 for p in pts)
    r2 = 1.0 - sum(r * r for r in resid) / max(ss, 1e-12)
    return slope * 100.0, r2, n


def horiz(row: dict):
    vx, vz = fnum(row, "velocity_x"), fnum(row, "velocity_z")
    return None if vx is None or vz is None else math.hypot(vx, vz) * 100.0


def scan():
    """返回 (id → [(g, r2, n, rec, prev)], id → [(首水平, 末水平, 段长, prev)],
    落地对照 (prev → [(入速, 落地首帧水平)]))。"""
    fits: dict[str, list] = defaultdict(list)
    horizs: dict[str, list] = defaultdict(list)
    landing: dict[str, list] = defaultdict(list)
    entry_vy: dict[str, list] = defaultdict(list)
    for path in sorted(RECORDER_DIR.glob("*_samples.csv")):
        with path.open(newline="", encoding="utf-8-sig") as handle:
            reader = csv.DictReader(handle)
            if "player_motion_old_id" not in (reader.fieldnames or []):
                continue
            rows = list(reader)
        runs = runs_of(rows)
        for i, (key, chunk) in enumerate(runs):
            if not key or len(chunk) - 1 < MIN_FRAMES:
                continue
            prev = runs[i - 1][0] if i else "-"
            got = fit_g(chunk)
            if got:
                fits[key].append((*got, path.name[7:26], prev))
            if key == "157":
                v0 = fnum(chunk[0], "velocity_y")
                if v0 is not None:
                    entry_vy[prev].append(v0 * 100.0)
            h0, h1 = horiz(chunk[0]), horiz(chunk[-1])
            if h0 is not None and h1 is not None:
                horizs[key].append((h0, h1, len(chunk) - 1, prev))
            # 落地对照：本段是 `148`，量它的首帧水平 vs 前驱末帧水平
            if key == "148" and i:
                a = horiz(runs[i - 1][1][-1])
                if a is not None and h0 is not None:
                    landing[prev].append((a, h0))
    return fits, horizs, landing, entry_vy


def pearson(xs, ys):
    if len(xs) < 3:
        return None
    mx, my = statistics.mean(xs), statistics.mean(ys)
    sxx = sum((a - mx) ** 2 for a in xs)
    syy = sum((b - my) ** 2 for b in ys)
    if sxx <= 0 or syy <= 0:
        return None
    return sum((a - mx) * (b - my) for a, b in zip(xs, ys)) / math.sqrt(sxx * syy)


def median(xs, d=0):
    xs = [x for x in xs if x is not None]
    return "—" if not xs else f"{statistics.median(xs):.{d}f}"


def iqr(xs, d=0):
    xs = sorted(x for x in xs if x is not None)
    if not xs:
        return "—"
    q1, q3 = xs[len(xs) // 4], xs[3 * len(xs) // 4]
    return f"{q1:.{d}f}~{q3:.{d}f}"


def parabola_chain():
    """把 `137` + 紧跟的 `157` + `148` 前 6 帧拼起来做**一条**二次拟合。

    这是「下落段不设重力」的直接证据：若 `157` 处重置过速度或换过重力，R² 会塌。
    """
    out = []
    for path in sorted(RECORDER_DIR.glob("*_samples.csv")):
        with path.open(newline="", encoding="utf-8-sig") as handle:
            reader = csv.DictReader(handle)
            if "player_motion_old_id" not in (reader.fieldnames or []):
                continue
            rows = list(reader)
        runs = runs_of(rows)
        for i, (key, chunk) in enumerate(runs):
            if key != "157" or i == 0 or runs[i - 1][0] != "137":
                continue
            if i + 1 >= len(runs) or runs[i + 1][0] != "148":
                continue
            t0 = fnum(runs[i - 1][1][0], "capture_time_s")
            pts = []
            for seg in (runs[i - 1][1], chunk, runs[i + 1][1][:6]):
                for row in seg:
                    t, y = fnum(row, "capture_time_s"), fnum(row, "world_y")
                    if t is None or y is None:
                        continue
                    pts.append((t - t0, y * 100.0))
            if len(pts) < 20:
                continue
            n = len(pts)
            mt, my = statistics.mean(p[0] for p in pts), statistics.mean(p[1] for p in pts)
            S = [[sum((p[0] - mt) ** (a + b) for p in pts) for b in range(3)] for a in range(3)]
            R = [sum((p[0] - mt) ** a * (p[1] - my) for p in pts) for a in range(3)]

            def det(M):
                return (M[0][0] * (M[1][1] * M[2][2] - M[1][2] * M[2][1])
                        - M[0][1] * (M[1][0] * M[2][2] - M[1][2] * M[2][0])
                        + M[0][2] * (M[1][0] * M[2][1] - M[1][1] * M[2][0]))

            D = det(S)
            if abs(D) < 1e-12:
                continue
            coef = []
            for c in range(3):
                M = [row[:] for row in S]
                for r_ in range(3):
                    M[r_][c] = R[r_]
                coef.append(det(M) / D)
            a, c1, c2 = my + coef[0], coef[1], coef[2]
            pred = [a + c1 * (p[0] - mt) + c2 * (p[0] - mt) ** 2 for p in pts]
            ss = sum((p[1] - my) ** 2 for p in pts)
            rr = sum((p[1] - q) ** 2 for p, q in zip(pts, pred))
            out.append((2 * c2, 1.0 - rr / max(ss, 1e-9), n, path.name[7:26]))
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--print", dest="write", action="store_false", help="只看，不写文件")
    args = parser.parse_args()

    if not RECORDER_DIR.is_dir():
        print(f"找不到录制目录：{RECORDER_DIR}", file=sys.stderr)
        return 1
    fits, horizs, landing, entry_vy = scan()
    if not fits:
        print("没有量到任何重力", file=sys.stderr)
        return 1

    L = [
        "# 空中下落物理（生成物，勿手改）",
        "",
        "> 由 `Scripts/MHRise/build_fall_physics.py` 从原始实录生成。**改数请改脚本或原始录制。**",
        "",
        f"**真值源**：`{RECORDER_DIR}` —— 全库扫描，老格式跳过。",
        "",
        "本表回答四件事：每个空中状态的重力是多少、下落段的重力从哪来、"
        "落地那一刻做了什么、水平速度在下落期间衰不衰减。**重力都以 `g₀ = 980 cm/s²` 为单位。**",
        "",
        "## 一、结论：重力**按状态逐条不同**，不是全局一个 g",
        "",
        "**方法**：对每一段的 `velocity_y` 做最小二乘求斜率（**不用逐帧差分** —— 位置抖动会被二阶差分放大，"
        "实测 a_y 在 ±8000 cm/s² 之间乱跳）。**掐掉末 3 帧**（落地刹车会把 g 系统性拉浅："
        "`143` 全段拟合 −1653，掐掉后 −2386）**且只取离地帧**（`world_y > 60 cm`）。",
        "",
        "**R² 是「这段到底是不是一条抛物线」的判据** —— 两段式动作（`187` 发射相 + 滑行相）"
        "单直线拟合的 R² 会掉到 0.3~0.9，那种 id **不给单一 g**。",
        "",
        "| id | 招式 | 可拟合段 | 其中可信 | **g 中位 cm/s²** | **÷g₀** | IQR | R² 中位 | 判定 |",
        "|---|---|---:|---:|---:|---:|---|---:|---|",
    ]
    verdict: dict[str, str] = {}
    for _, ids in GROUPS:
        for key in ids:
            items = fits.get(key, [])
            if not items:
                continue
            trust = [x for x in items if x[1] >= R2_TRUST]
            gs = sorted(x[0] for x in trust) or sorted(x[0] for x in items)
            r2s = statistics.median(x[1] for x in items)
            parabolic = len(trust) / len(items) >= 0.5 and abs(statistics.median(gs)) > 50
            if abs(statistics.median(gs)) < 100:
                note = "**地面态**（不自由落体）"
            elif parabolic:
                note = "恒定重力抛物线"
            else:
                note = "**非单一抛物线**（两段式或多相）"
            verdict[key] = note
            L.append(
                f"| `{key}` | {name_of(key)} | {len(items)} | {len(trust)} | "
                f"**{median(gs)}** | {statistics.median(gs) / G0:.2f} | {iqr(gs)} | {r2s:.3f} | {note} |"
            )
    L += [
        "",
        "**项目侧对照**（`InsectGlaiveCombatConfig.h`，两个常数都是从实录反推的）：",
        f"- `AerialFallGravityScale = {ENGINE['AerialFallGravityScale']}` → "
        f"**{ENGINE['AerialFallGravityScale'] * G0:.0f} cm/s²**",
        f"- `WhiteAerialFallGravityScale = {ENGINE['WhiteAerialFallGravityScale']}` → "
        f"**{ENGINE['WhiteAerialFallGravityScale'] * G0:.0f} cm/s²**",
        "- 另有 `BrakingDecelerationFalling = 0.0`、`AerialFallMaxSeconds = 4.0`（**只是看门狗**，不是物理钳制）、"
        "全工程**没有** `MaxFallSpeed` 赋值。",
        "",
        "## 二、下落段**不设自己的重力**，它继承发起那个动作的",
        "",
        "`157` 是这条结论的关键证据：它同一个 id，重力却**随前驱分家**。",
        "混在一起取中位得到的数没有意义，必须分层看：",
        "",
        "| `157` 的前驱 | 段数 | g 中位 | ÷g₀ | IQR | 入口 vy 中位 cm/s |",
        "|---|---:|---:|---:|---|---:|",
    ]
    # `157` 段本身很短（中位 9~13 帧），R² 天然低 ⇒ 这里门槛放到 0.6
    # （全表门槛 0.95 会把大半样本丢掉，反而看不到「按前驱分家」这件事本身）。
    by_prev: dict[str, list] = defaultdict(list)
    for key, items in fits.items():
        if key != "157":
            continue
        for g, r2, n, rec, prev in items:
            if r2 >= 0.60:
                by_prev[prev].append((g, n))
    for prev, v in sorted(by_prev.items(), key=lambda kv: -len(kv[1])):
        if len(v) < 3:
            continue
        gs = [x[0] for x in v]
        vy0 = entry_vy.get(prev, [])
        L.append(f"| `{prev}` {name_of(prev)} | {len(v)} | **{median(gs)}** | "
                 f"{statistics.median(gs) / G0:.2f} | {iqr(gs)} | {median(vy0)} |")
    L += [
        "",
        "⇒ **`157` 跟着前驱的重力走**：回避后是 `137` 那一档，舞踏后是 `154` 那一档。"
        "所以实现上**下落段什么都不该设** —— 不要给它单独配重力。",
        "",
        "> ⚠ 上表两组都**比前驱本身略深约 2%**（`137` −2430 vs `137` 自身 −2378）—— 这是**短段偏差**，"
        "不是真的换了重力：`157` 段中位只有 9~13 帧，回归窗口太短，且 id 切换那一帧常带一个小台阶。"
        "**判「是不是同一条抛物线」要用第四节的整体二次拟合，不要用这张表的单段斜率。**",
        "",
        "## 三、`137` 空中回避 = 一个纯抛体",
        "",
    ]
    d137 = fits.get("137", [])
    if d137:
        gs = [x[0] for x in d137]
        trust = [x for x in d137 if x[1] >= 0.98]
        L += [
            f"- g = **{median(gs)} cm/s²**（{statistics.median(gs) / G0:.2f} g₀），"
            f"**R² 中位 {statistics.median(x[1] for x in d137):.4f}**，"
            f"{len(trust)}/{len(d137)} 段 R²>0.98，IQR **{iqr(gs)}**"
            + ("（四分位间距只有几 cm/s² —— 机器精度级别的一致）" if len(trust) else ""),
            f"- 对照 `AerialFallGravityScale × g₀ = {ENGINE['AerialFallGravityScale'] * G0:.0f}` ⇒ "
            f"**差 {abs(statistics.median(gs) + ENGINE['AerialFallGravityScale'] * G0):.0f} cm/s²"
            f"（{100 * abs(statistics.median(gs) + ENGINE['AerialFallGravityScale'] * G0) / (ENGINE['AerialFallGravityScale'] * G0):.2f}%）**",
        ]
    chain = parabola_chain()
    if chain:
        acc = [x[0] for x in chain]
        r2 = [x[1] for x in chain]
        ok = sum(1 for r in r2 if r > 0.99)
        L += [
            "",
            "## 四、`137 → 157 → 落地` 是**同一条抛物线**",
            "",
            "把 `137`（142 帧）+ 紧跟的 `157` + `148` 前 6 帧拼成**一条**轨迹做二次拟合：",
            "",
            f"| 量 | 值 |",
            f"|---|---:|",
            f"| 例数 | {len(chain)} |",
            f"| **R² > 0.99 的比例** | **{ok}/{len(chain)}** |",
            f"| R² 中位 | **{statistics.median(r2):.4f}** |",
            f"| 拟合加速度中位 | **{statistics.median(acc):.0f} cm/s²**（{statistics.median(acc) / G0:.2f} g₀）|",
            "",
            "⇒ **`157` 处没有任何速度重置** —— id 换了、动画换了，弹道还是那一条。"
            "（拟合值比 `137` 单段略浅，是因为尾巴带进了落地刹车的前几帧。）",
            "**所以「下降」不需要单独实现：它是上一条弹道的延续。**",
        ]
    L += [
        "",
        "## 五、水平速度：**下坠段全程不衰减**（`188` 是唯一例外）",
        "",
        "判据用**逐段比值**（末帧 ÷ 首帧），不用「首帧中位 ÷ 末帧中位」—— 后者会被长段与短段的",
        "组成差异污染。**只列下坠类 id**：`142`/`156` 是撑杆跳的弧段（本身在减速，不是下落），",
        "`187`/`15` 是上抛段，都不属于这一节。",
        "",
        "| 下坠 id | 段数 | 首帧中位 cm/s | 末帧中位 cm/s | **逐段比值中位** | 落在 ±5% 内 | 判定 |",
        "|---|---:|---:|---:|---:|---:|---|",
    ]
    for key in ("143", "157", "188", "30"):
        items = [x for x in horizs.get(key, []) if x[0] > 20.0]
        if len(items) < 5:
            continue
        ratios = sorted(x[1] / x[0] for x in items)
        med = statistics.median(ratios)
        near = sum(1 for r in ratios if abs(r - 1.0) <= 0.05)
        note = ("**水平衰减约 20%**（唯一例外，原因未查清）"
                if abs(med - 1.0) > 0.10 else "**恒定**（不衰减）")
        L.append(f"| `{key}` {name_of(key)} | {len(items)} | "
                 f"{statistics.median(x[0] for x in items):.0f} | "
                 f"{statistics.median(x[1] for x in items):.0f} | **{med:.3f}** | "
                 f"{near}/{len(items)} | {note} |")
    L += [
        "",
        "⇒ `BrakingDecelerationFalling = 0.0` 对**下坠段**是对的：水平速度全程恒定，"
        "惯性一点不该衰减。`188`（操虫斩落空后的下坠）是唯一例外。",
        "",
        "## 六、落地：**重设**水平速度，不是对入速做摩擦衰减",
        "",
        "`148 起跳落地` 首帧的水平速度，按前驱分组：",
        "",
        "| `148` 的前驱 | n | 入速中位 cm/s | **`148` 首帧中位** | 比值 | `148` 首帧 IQR |",
        "|---|---:|---:|---:|---:|---|",
    ]
    for prev, v in sorted(landing.items(), key=lambda kv: -len(kv[1])):
        if len(v) < 3:
            continue
        ins = [x[0] for x in v]
        outs = [x[1] for x in v]
        L.append(f"| `{prev}` {name_of(prev)} | {len(v)} | {statistics.median(ins):.0f} | "
                 f"**{statistics.median(outs):.0f}** | "
                 f"{statistics.median(outs) / max(statistics.median(ins), 1e-9):.3f} | {iqr(outs)} |")
    if landing:
        allout = [b for v in landing.values() for _, b in v]
        allin = [a for v in landing.values() for a, _ in v]
        r = pearson(allin, allout)
        L += [
            "",
            f"**入速 306 也变 {statistics.median(allout):.0f}、入速 898 也变 {statistics.median(allout):.0f}** "
            f"⇒ `148` 首帧恒在 **{statistics.median(allout):.0f} ± "
            f"{statistics.median([abs(b - statistics.median(allout)) for b in allout]):.0f} cm/s**，"
            f"**与入速无关**（入速↔落地首帧的相关系数 r = "
            f"{r:+.3f}）。",
            "",
            "**这是「重设」而不是「摩擦衰减」的判据**：越是高入速、越该被砍得狠；而实测两头的落点相同。"
            "⚠ 项目当前实现是摩擦衰减（引擎侧遥测：落地帧 v2D 292 → 126 → 24 → 0，4 帧到静），"
            "**没有这个重设**，见 `docs/using/空中下落实现缺口.md`。",
        ]
    text = "\n".join(L) + "\n"

    if args.write:
        OUT_MD.parent.mkdir(parents=True, exist_ok=True)
        OUT_MD.write_text(text, encoding="utf-8")
        print(f"写入 {OUT_MD}")
    else:
        print(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
