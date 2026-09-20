#!/usr/bin/env python3
# Copyright MHGZ Project. All Rights Reserved.
"""从 MHRise 实录生成真值表（docs/reference/真值表.md）+ 每招式逐帧中间文件。

**为什么要有这个脚本**：真值源是原始 `*_samples.csv`（单份 17 MB / 四万行），每次要一个数
都得重读全表再算。本脚本把「招式真值」固化进仓库；每帧轨迹则写成中间文件（`Saved/`，不入库），
将来要烘成 `UCurveVector` 资产时直接读它。

**这个脚本取代的第一版错在哪**（别再犯）：
  1. 把 29 份录制**全部**当有效 —— 其中 **7 份根本没有 `player_motion_old_id` 列**（老格式），
     第一版把它们当成 id 为空的段写了 1600 余行噪声。
  2. 只做「逐段罗列」，没有做**招式级归并** —— 同一招式在不同录制里重复几十遍，
     真值被淹没在重复里。
  3. 没有区分**跑满段**与**候选段** —— 两者语义完全不同（前者给总时长/最高点，
     后者给最早可操作帧），混在一起取中位数得到的值没有意义。
  4. 向上轴用了 Z（应为 **Y**），单位当成厘米（应为**米**）。见下。

【口径（每一条都踩过）】
- MHRise 的**世界向上轴是 Y**，水平面是 XZ（`Scripts/MHRise/README.md` 明写）。
- 位置单位是**米**、速度是 **m/s**；项目用厘米 ⇒ 本脚本统一 ×100 输出。
- `world_x` / `world_z` 是地图世界坐标（−500 量级），只有**差值**有意义。
- 采样率 **119.8 Hz**，**帧数 = 样本数 − 1**。
- 分段依据是 **`player_motion_old_id`** 连续段（该列写成 "160.00000000"，需 `int(float())`）。
  **`motion_l0_id` 不能用**（该列长期恒为 1，不是招式 id）。

【术语】
- **跑满段**：该 id 里**帧数众数簇**（按 6 帧分箱、最大箱占比 ≥30%）内的段。它们带着**自然后继**。
- **候选段**：**严格短于任何跑满段帧数**的段 —— 玩家在它自然结束前就接了下个动作。
  **最早可操作帧只可能藏在候选段里。**
  （旧口径用「后继 id 不在自然后继集合里」判候选，有两个毛病：会把「玩家拖着没按」的长段
  当成取消 —— 实测 19 个有候选的招式里 7 个的最小候选比自己的众数还长；又会把「提前取消到
  同一个自然后继」的真取消静默丢掉。改成「只凭短判」两处一起修掉。）
- **探针**：玩家用来试探「这招能不能操作了」的那个后继动作。**除受击/失控外任何动作都可以当探针**
  （2026-09-19 订正）。旧口径只认空中回避 `137`，硬伤是用它当唯一探针就永远测不出
  **空中回避自己的**最早可操作帧。
- **可信度按后继动作 id 判，不按样本量判**：观测到「16 帧处取消进空中回避」哪怕只有一次也可信；
  「取消进受击」哪怕一百次也无意义。样本数只作信息展示。

【三个反复咬人的度量坑（都在 2026-09-19 修掉）】
- **录制首尾段**：末段的 `next_id` 恒为 None，而 `None not in natural` 恒真 ⇒ 它会被无条件判成
  「玩家主动取消」，可它的长度只是「按 F9 之前还剩多久」。26/26 份录制中招，最狠的是
  `148` 的 15 帧（真值 50 帧）。⇒ `Seg.is_boundary`，不作候选也不进众数簇。
- **壁钟秒 vs 帧数**：采样间隔在 0.0077–0.0093 抖动，全库还有 6 处 >50 ms 卡帧。
  实测一个 112 帧的 `147` 段因 0.178 s 卡帧，壁钟被抬到 1.093 s，按秒分箱时会和 131 帧的
  众数簇落进同一箱、把自己的后继并进「自然后继」。⇒ **判定一律用帧数**，`Seg.has_hitch` 的段
  既不进众数簇也不作候选。
- **后继穿过瞬态**：`next_id` 取的是紧邻的原始 run，可能是 1–2 帧的过渡（`456` 的前驱 18/18
  都是 1 帧瞬态 `58`），于是「经瞬态进入的状态永远不会出现在任何 next_id 里」。
  ⇒ `Seg.next_semantic`（其后第一个 ≥12 样本的动作）为报告口径，两列都出。

用法：
    python Scripts/MHRise/build_truth_table.py            # 写真值表 + Saved/_mhr_frames/
    python Scripts/MHRise/build_truth_table.py --no-frames  # 只写真值表
"""

from __future__ import annotations

import argparse
import csv
import math
import re
import statistics
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from pathlib import Path

RECORDER_DIR = Path(
    r"C:/apps/steam/steamapps/common/MonsterHunterRise/reframework/data/MHGZ_AerialTrajectoryRecorder"
)
PROJECT = Path(__file__).resolve().parents[2]
OUT_MD = PROJECT / "docs" / "reference" / "真值表.md"
OUT_FRAMES = PROJECT / "Saved" / "_mhr_frames"
# 人工维护的 id → 招式名对照表。它是**补充**来源：脚本内的 KNOWN_IDS 只收已核实的，
# 而这份表覆盖 200 多个 id（用户已确认其中那批通用动作的名字是对的）。
TABLE_DOC = PROJECT / "docs" / "reference" / "虫棍招式表.md"


def load_table_names() -> dict[str, str]:
    """读 `虫棍招式表.md` 开头那段「一行一个 id」的清单，到第一个 `---` 或 `#` 为止。

    **只读那一段**：后面的散文里有大量 `| \\`146\\` 向后起跳 | 0.652 s |` 这种行，
    照单全收会把 id 和数字错配成名字。另外名字里若塞着 ≥2 个数字词，说明那行其实是
    别的 id 列表，直接跳过。
    """
    names: dict[str, str] = {}
    if not TABLE_DOC.exists():
        return names
    for raw in TABLE_DOC.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if line.startswith("---") or line.startswith("#"):
            break
        match = re.match(r"^(\d+)\s*(\S.*)$", line)
        if not match:
            continue
        name = match.group(2).strip()
        if sum(1 for token in name.split() if token.isdigit()) >= 2:
            continue
        names.setdefault(match.group(1), name)
    return names

# 已核对的 id → 招式名。来源：docs/reference/虫棍招式表.md + 实录逐段核对。
# **没核实过的不要往这里加**，宁可显示 id。
KNOWN_IDS = {
    # 空中动作
    "137": "空中回避",
    "141": "向前起跳",
    "142": "向前起跳后段",
    "143": "起跳下坠",
    "144": "向左起跳",
    "145": "向右起跳",
    "146": "向后起跳",
    "147": "向后起跳后段",
    "148": "起跳落地",
    "154": "突进回旋斩进入舞踏",
    "155": "向前起跳（白灯）",
    "156": "向前起跳后段（白灯）",
    "157": "起跳下坠（白灯）",
    "158": "向后起跳（白灯）",
    "159": "向后起跳后段（白灯）",
    # 地面招式
    "160": "突进回旋斩",
    "162": "猎虫回归(白灯)",
    "163": "猎虫回归结束(白灯)",
    "189": "操虫斩/觉虫击命中进入舞踏",
    # 空中操虫斩 / 猎虫滑翔
    # `186` 已由 20260919 的补录证实：全库 12 段、12/12 恒 86 帧、逐帧位完全冻结
    # （velocity 恒 0）、后继恒为 `187`；且悬停期间**动画在播**（43 帧 @60fps ⇒
    # `motion_l0_frame` 0.5→43.5）、**朝向可被玩家转动**（最多 43.9°，有一次 168°）。
    # 即 186 = 0.718 s 的可瞄准悬停，不是受击（受击不会是定长且不冻结原位）。
    "186": "空中操虫斩",
    "187": "操虫斩前冲动作",
    "188": "操虫斩未击中",
    "182": "铁虫丝跳跃/操虫斩落地",
    "190": "猎虫滑翔",
}

# 人工招式表的 id → 名字（只读开头那段清单）。见 load_table_names()。
TABLE_IDS = load_table_names()

# 受击 / 失控类动作：这些中断**不是**「玩家能操作了」，混进来会把最早可操作帧压小。
#
# **2026-09-19 补录（「故意挨打」三类会话）后定案。** 判据只有一条，而且不靠名字、不靠扇入：
# **受击在进入那一帧带瞬时速度跳变**，动画流是连续的，一帧内蹦起来只能是外力。
#
# 【击飞链】两次独立观测都指向同一个固定冲量：(水平 485 cm/s + 垂直 600 cm/s)，
# 方向 = **受击瞬间朝向的 ±1 倍**，所以只有两个变体、能 100% 二分开（不存在连续受击角度）：
#     朝后飞（被打的方向来自**正面**）：`15` → [`30`] → `16` → `452`
#     朝前飞（被打的方向来自**背后**）：`21` → [`33`] → `22` → `453`
# 全库比例：进入 `15`/`21` 的那一帧 Δvy 中位 **+587 / +602**；`15` 继任只有 `16`(53)/`30`(14)，
# `21` 只有 `22`(9)/`33`(1)，`452` 前驱 58/58 = `16`，`453` 前驱 9/9 = `22`。**全库零反例。**
# 中括号那两段（`30`/`33`）只在**空中被打中**时出现：地面受击高度 9.6–65.6 cm，空中 143.5–273.6 cm，
# 中间是干净断层。不是两套动作 —— 起手/翻滚/起身三段完全相同，区别只是空中坠落时间长，
# `15` 播完人还在空中，游戏切到「下坠」动画（`30` 与 `15` 末帧速度连续，不是新冲量）。
#
# 【硬直链】`57` → `58`（1 帧瞬态）→ `456`。与击飞**不是一回事**：进入 `57` 时 Δvy≈0、
# 速度被清零到 ~90 cm/s，恒 176 帧（1.469 s）、位移 41 cm、只在地面。
#
# 【为什么 `16` / `22` 判得比别的细】这两个 id **是重载的**：链上的 `16` 是 118 帧 / 668.4 cm
# 的受击翻滚，另有 6 段 `16` 是 30–90 帧 / 70–170 cm 的**起步过渡**（前驱 `4`/`6`，后继常是 `160`）。
# `22` 同理（链上 118 帧 / 616.6 cm，链外 1 段 56 帧）。⇒ 按 id 一刀切会误伤那几段，
# 所以**本集合不收 `16`/`22`**（见下面第 3 条的代价），受击排除靠其余 7 个无歧义 id 完成。
#
# 【三条踩过的错，别再回头】：
#   1. 招式表里那批名字（`400–404` 受伤小/大后退、`411/412` 踉跄、`414`、`450–463`）
#      在**整库中一次都没出现过** —— 加进去是不可证伪的空转，看着像防护其实没接线。
#      **不能反推「没挨过打」**：id 是按表分配的、不同表都从 0 开始，撞号的动作不会出现在虫棍表里。
#   2. 「扇入判据」**已被数据否掉**：受击应当扇入大，但实测 `137` 空中回避（合法可控）扇入
#      **6** / 最大前驱占比 53%，而 `57` 扇入只有 **2** / 占比 94%。扇入分不开这两者。
#      （`15` 的 15 种前驱、`21` 的 5 种**确实**是受击的签名，但它只是签名之一，不是判据。）
#   3. `452 受伤跪地后的站起` 当初被当成「命名可疑、扇入 1」而**差点被删** —— 实测它就是
#      击飞链的第四段（前驱 58/58 = `16`，且 `16` 本身带 668 cm 位移）。**照名单删会误删真数据。**
CONTROL_LOSS_IDS: set[str] = {"15", "21", "30", "33", "452", "453", "57", "58", "456"}

# 短于此的 run 是过渡/混叠，不进真值。
# 但**后继**会穿过它们 —— 见 Seg.next_semantic（`456` 的前驱 18/18 都是 1 帧瞬态 `58`）。
MIN_SAMPLES = 12

# 实测 119.8977 Hz。**帧数 = 样本数 − 1** 是主口径，秒只作派生显示值（frames / SAMPLE_HZ）。
# 1 帧 = 8.347 ms，就是本方法的分辨率（所以 `159` 的 15 帧与 `147` 的 16 帧不是两个有效数字）。
SAMPLE_HZ = 119.8

# 「跑满」怎么判 —— 这里踩过一个大坑，别改回「时长 ≥ 最大值 × 0.98」：
# 那条规则对**时长由玩家决定**的招式（`160` 突进回旋斩 0.69~1.13 s、`4` 持续奔跑）
# 会把一个孤例当成自然长度，于是 `160` 被报成「2.393 s 恒定」，进而把舞踏的
# 蒙太奇时间算成 2.709 s（真值 0.316，因为舞踏自己就是蒙太奇的 0 偏移）。
#
# 正确判据是**众数簇**，且**按帧数分箱**（不是按秒）：把帧数按 MODE_BIN_FRAMES 分箱，
# 最大那箱占比 ≥ MODE_SHARE 才算「定长招式」，此时跑满段 = 该箱内的段、
# 自然后继 = 这些段的后继。占比不足 ⇒ 长度本来就可变，**没有「自然长度」可言**，如实标注。
#
# 为什么必须按帧数分箱：原来是 0.05 s 一箱，而采样间隔在 0.0077–0.0093 之间抖动，
# 全库还有 6 处 >50 ms 的卡帧（最大 0.178 s）。实测 `20260917_123650` 里一个 **112 帧**
# 的 `147` 段因为 0.178 s 卡帧，壁钟被抬到 **1.093 s**，正好和 **131 帧**的众数簇落进同一箱
# ⇒ 它加入了众数簇、把它的后继并进了「自然后继」。按帧数分箱则完全免疫卡帧与抖动。
# 0.05 s 换算成帧是 5.99，取 **6 帧**，正好对齐采样网格（顺带去掉 round() 的银行家舍入
# 与「箱边界落在样本之间」导致的不稳）。
MODE_BIN_FRAMES = 6
MODE_SHARE = 0.30

# 段内有相邻样本间隔超过「中位间隔的 3 倍」且 >30 ms ⇒ 录制卡帧。
# 卡帧会让帧数与壁钟脱钩，这种段不参与众数簇判定，也不作候选。
HITCH_GAP_SECONDS = 0.03
HITCH_GAP_RATIO = 3.0


@dataclass
class Seg:
    recording: str
    motion_id: str
    t0: float
    duration: float  # 壁钟秒（仅供显示；判定一律用 frames）
    samples: int
    apex: float  # 相对段首的最大抬升 (cm)
    end_rise: float  # 末帧相对段首的抬升 (cm) —— 取消那一刻的高度
    horiz: float  # 段首到段末的水平位移 (cm)
    rise_start: float
    prev_id: str | None
    next_id: str | None  # 紧邻的**原始** run，可能是 1–2 帧的过渡瞬态
    next_semantic: str | None = None  # 其后第一个 ≥MIN_SAMPLES 的动作 —— 报告以此为准
    frames: int = 0  # samples − 1
    is_boundary: bool = False  # 录制的首个/末个 run：时长被录制起止截断，不是完整观测
    has_hitch: bool = False  # 段内含卡帧，帧数与壁钟脱钩
    altitude: float = 0.0  # 段内 world_y 中位数 (cm)，训练场地面 ≈ 0 ⇒ 可当离地高度
    rows: list = field(repr=False, default_factory=list)

    @property
    def seconds(self) -> float:
        return self.frames / SAMPLE_HZ


def load_rows(recording: Path) -> list[dict] | None:
    with recording.open(newline="", encoding="utf-8-sig") as handle:
        rows = list(csv.DictReader(handle))
    if not rows or "player_motion_old_id" not in rows[0]:
        return None  # 老格式，无招式 id 列
    return rows


def ident(row: dict) -> str:
    raw = (row.get("player_motion_old_id") or "").strip()
    try:
        return str(int(float(raw)))
    except ValueError:
        return ""


def num(row: dict, key: str) -> float:
    try:
        return float(row[key])
    except (KeyError, TypeError, ValueError):
        return float("nan")


def collect_segments(recording: Path) -> list[Seg] | None:
    rows = load_rows(recording)
    if rows is None:
        return None

    runs: list[tuple[str, list[dict]]] = []
    for row in rows:
        key = ident(row)
        if runs and runs[-1][0] == key:
            runs[-1][1].append(row)
        else:
            runs.append((key, [row]))

    segs: list[Seg] = []
    for index, (key, chunk) in enumerate(runs):
        if not key or len(chunk) < MIN_SAMPLES:
            continue
        y0 = num(chunk[0], "world_y")
        x0, z0 = num(chunk[0], "world_x"), num(chunk[0], "world_z")
        last = chunk[-1]
        rise = next(
            (num(r, "velocity_y") * 100.0 for r in chunk if num(r, "velocity_y") > 0.0),
            float("nan"),
        )
        gaps = [
            num(chunk[i], "capture_time_s") - num(chunk[i - 1], "capture_time_s")
            for i in range(1, len(chunk))
        ]
        median_gap = statistics.median(gaps) if gaps else 0.0
        # next_semantic：往后找第一个够长的 run，跳过 1–2 帧的过渡瞬态。
        # `456` 的前驱 18/18 都是 1 帧瞬态 `58`，用 next_id 会把它的真实来路整个丢掉。
        semantic = next(
            (runs[j][0] for j in range(index + 1, len(runs)) if runs[j][0] and len(runs[j][1]) >= MIN_SAMPLES),
            None,
        )
        segs.append(
            Seg(
                recording=recording.name,
                motion_id=key,
                t0=num(chunk[0], "capture_time_s"),
                duration=num(last, "capture_time_s") - num(chunk[0], "capture_time_s"),
                samples=len(chunk),
                apex=(max(num(r, "world_y") for r in chunk) - y0) * 100.0,
                end_rise=(num(last, "world_y") - y0) * 100.0,
                horiz=math.hypot(num(last, "world_x") - x0, num(last, "world_z") - z0)
                * 100.0,
                rise_start=rise,
                prev_id=runs[index - 1][0] if index > 0 else None,
                next_id=runs[index + 1][0] if index + 1 < len(runs) else None,
                next_semantic=semantic,
                frames=len(chunk) - 1,
                altitude=statistics.median([num(r, "world_y") for r in chunk]) * 100.0,
                # 录制的首个/末个 run 时长被录制起止截断：末段的 next_id 恒为 None，
                # 而 `None not in natural` 恒真 ⇒ 它会被无条件判成「玩家主动取消」，
                # 可它的长度只是「按 F9 之前还剩多久」。26/26 份录制都会中招，
                # 其中最狠的一次是 `148` 的 15 帧（真值 50 帧）。
                is_boundary=index == 0 or index == len(runs) - 1,
                has_hitch=bool(gaps)
                and max(gaps) > HITCH_GAP_SECONDS
                and max(gaps) > HITCH_GAP_RATIO * median_gap,
                rows=chunk,
            )
        )
    return segs


def median(values: list[float]) -> float:
    clean = [v for v in values if not math.isnan(v)]
    return statistics.median(clean) if clean else float("nan")


def fmt(value: float, digits: int = 1) -> str:
    return "—" if math.isnan(value) else f"{value:.{digits}f}"


def name_of(motion_id: str | None) -> str:
    if motion_id is None:
        return "—"
    # **人工招式表是权威来源**（2026-09-20 改）：它是人维护的、随时在改，而 KNOWN_IDS 是
    # 脚本里的历史快照 —— 原先 KNOWN_IDS 优先，导致 `162`/`163` 在生成物里一直显示旧名
    # 「操虫斩 / 操虫斩命中」，而表里早就改成「猎虫回归(白灯) / 猎虫回归结束(白灯)」了。
    # 现在表优先，KNOWN_IDS 只作兜底（当前它与表 100% 重合，等于冗余，留着防表被删行）。
    return TABLE_IDS.get(motion_id) or KNOWN_IDS.get(motion_id) or "（未核对）"


def build(all_segs: list[Seg]):
    """按 id 归并成招式级真值 + 最早可操作帧。"""
    by_id: dict[str, list[Seg]] = defaultdict(list)
    for seg in all_segs:
        by_id[seg.motion_id].append(seg)

    moves = {}
    for motion_id, segs in by_id.items():
        # 只有**完整观测**参与判定：录制首尾段被录制起止截断，卡帧段的帧数与壁钟脱钩。
        observed = [s for s in segs if not s.is_boundary and not s.has_hitch]
        # 众数簇：定长招式的跑满段会聚在一箱里；变长招式不会。
        bins = Counter(s.frames // MODE_BIN_FRAMES for s in observed)
        threshold = MODE_SHARE * len(observed) if observed else float("inf")
        # **自然长度取「占比够的箱里最靠右的那个」，不是「最靠右的那一箱占比最高」。**
        # 因为玩家**越认真探一个招式，它的提前取消样本就越多**，取消簇迟早会盖过自然簇，
        # 于是「最常见箱」把自然长度定成取消长度、候选集变空、数字消失。实测 `154` 舞踏：
        # 194 帧(自然玩完)28 次 vs 38 帧(最早取消)24 次 —— 取消簇以 32:31 一票之差赢下众数，
        # 舞踏就从第二节彻底消失了（而它的真值 1.622 s 是招式表手核过的）。
        # 取消只会比自然长度**更短**，所以自然长度必在分布右端 ⇒ 取最靠右的合格箱。
        qualifying = [b for b, c in bins.items() if c >= threshold]
        fixed_length = bool(qualifying)
        if fixed_length:
            peak = max(qualifying)
            full = [s for s in observed if s.frames // MODE_BIN_FRAMES == peak]
            natural = {s.next_semantic for s in full}
            natural_frames_min = min(s.frames for s in full)
            # 候选 = **严格短于任何自然观测**的段。判据只凭「短」，不看后继 id 在不在
            # natural 集合里 —— 后者有两个毛病：会把「玩家拖着没按」的长段当成取消
            # （旧口径下 19 个有候选的招式里 7 个的最小候选比自己的众数还长），
            # 又会把「提前取消到同一个自然后继」的真取消静默丢掉。
            cand = [
                s
                for s in observed
                if s.next_semantic is not None
                and s.next_semantic not in CONTROL_LOSS_IDS
                and s.frames < natural_frames_min
            ]
        else:
            # 长度由玩家决定 ⇒ 没有「自然长度」，也就分不出「被取消」。
            # 但不该因此让这类招式在表里彻底消失（旧版就是）：如实报左尾分布，
            # 让「有没有输入门」这件事可以被看见。实测 `160`/`4` 的左尾是平滑斜坡、没有墙。
            full = list(segs)
            natural = (
                {Counter(s.next_semantic for s in segs).most_common(1)[0][0]} if segs else set()
            )
            natural_frames_min = None
            cand = []
        moves[motion_id] = {
            "id": motion_id,
            "name": name_of(motion_id),
            "trials": len(segs),
            "observed": len(observed),
            "recordings": len({s.recording for s in segs}),
            "fixed_length": fixed_length,
            "duration": median([s.seconds for s in full]),
            "frames": median([float(s.frames) for s in full]),
            "frames_range": (
                (min(s.frames for s in full), max(s.frames for s in full)) if full else (0, 0)
            ),
            # 众数簇之外的最长观测。**对「被外部事件砍断」的招式，这才是自然长度** ——
            # 典型是 `159`：1.488 s 是被触地砍断的，自然长度约 1.596（招式表已记）。
            # 众数给不出这个，所以要单独报，不能只看中位数。
            "longest": max((s.seconds for s in segs), default=float("nan")),
            "longest_frames": max((s.frames for s in segs), default=0),
            "longest_next": max(segs, key=lambda s: s.frames).next_semantic if segs else None,
            "samples": median([float(s.samples) for s in full]),
            "apex": median([s.apex for s in full]),
            "altitude": median([s.altitude for s in full]),
            "apex_range": (
                (min(s.apex for s in full), max(s.apex for s in full)) if full else (0, 0)
            ),
            "horiz": median([s.horiz for s in full]),
            "rise": median([s.rise_start for s in full]),
            "natural": natural,
            "full": full,
            "cand": cand,
            # 前置段能不能当蒙太奇时间的锚，要看它**全部**试验是否恒定 ——
            # 只看众数簇会得出「恒定」的假象（160 就是被这样坑过）。
            "all_frames": [s.frames for s in segs],
            # 变长招式的左尾（帧数 → 次数），只到中位数为止。
            "tail": sorted(Counter(s.frames for s in observed).items()),
        }
        tail = moves[motion_id]["tail"]
        if tail and not fixed_length:
            mid = statistics.median([s.frames for s in observed])
            moves[motion_id]["tail"] = [t for t in tail if t[0] <= mid][:12]
    return moves


def state_of(moves: dict, motion_id: str | None) -> str:
    """该动作是空中还是地面动作。跨状态比较后继的「最早」没有意义（地面动作接不了
    空中动作，反之亦然），所以第三节要带着这个标签读。

    判据要**两个信号取或**，单看 apex 会错判：`186 空中操虫斩` 悬停期间逐帧位冻结、
    apex 恒为 0，只看 apex 会把它标成「地」，而它实际在 1.87–8.64 m 的高度上。
    训练场地面 `world_y ≈ 0`，所以段内 world_y 中位数可以当离地高度用。
    """
    if motion_id is None:
        return "?"
    m = moves.get(motion_id)
    if not m:
        return "?"
    if m["altitude"] > 100.0:
        return "空"
    return "空" if (not math.isnan(m["apex"]) and m["apex"] > 50.0) else "地"


def earliest_frames(moves: dict):
    """每招式：候选段里取**帧数最少**的那条。

    探针口径（2026-09-19 用户订正）：**除受击/失控外，任何后继都可以当探针**。
    旧口径只认后继为 `137` 空中回避的段，有两个硬伤：
      (1) 用空中回避当唯一探针，就**永远测不出空中回避自己的最早可操作帧**；
      (2) 游戏里的「可操作」不该依赖某一个特定后继动作 —— 应当存在通用的最早可操作帧。
    受击集合 2026-09-19 补录后已定案（`CONTROL_LOSS_IDS`，判据是进入那一帧的瞬时速度跳变）。
    它之外，**结构性的排除才是主力**：录制首尾段（`is_boundary`）与卡帧段（`has_hitch`）都不作候选。

    报告纪律（2026-09-19 用户订正）：**可信度按「取消进了哪个动作 id」判，不按样本量判** ——
    一个「16 帧处取消进空中回避」的观测哪怕只有一次也可信（探针动作无歧义）；
    「取消进受击」的观测哪怕一百次也无意义。所以这里**不设**「样本不足 ⇒ 未测定」这类
    按样本量设卡的门槛，样本数与「次早值差几帧」只作信息展示。
    """
    out = []
    for motion_id, m in moves.items():
        if not m["cand"]:
            continue
        best = min(m["cand"], key=lambda s: s.frames)
        higher = sorted({s.frames for s in m["cand"] if s.frames > best.frames})
        by_succ: dict[str, list[int]] = defaultdict(list)
        for s in m["cand"]:
            by_succ[s.next_semantic].append(s.frames)
        # 前置段帧数：只有**全部试验**都恒定时才拿来当蒙太奇时间的锚（±1 帧是分辨率）。
        prefix = moves.get(best.prev_id or "", {}).get("all_frames", [])
        prefix_const = (
            int(statistics.median(prefix)) if prefix and (max(prefix) - min(prefix)) <= 1 else None
        )
        out.append(
            {
                "id": motion_id,
                "name": m["name"],
                "natural": m["natural"],
                "cand_total": len(m["cand"]),
                "earliest": best,
                "earliest_count": sum(1 for s in m["cand"] if s.frames == best.frames),
                "next_gap": (higher[0] - best.frames) if higher else None,
                "prefix_id": best.prev_id,
                "prefix_const": prefix_const,
                "by_successor": {
                    k: (min(v), len(v)) for k, v in sorted(by_succ.items(), key=lambda kv: min(kv[1]))
                },
            }
        )
    return out


def render(moves: dict, earliest: list, skipped: list[str], recordings: int) -> str:
    lines = [
        "# MHRise 真值表（生成物，勿手改）",
        "",
        "> 由 `Scripts/MHRise/build_truth_table.py` 从原始实录生成。**改数请改脚本或原始录制。**",
        "",
        f"**真值源**：`{RECORDER_DIR}` —— 本次统计 **{recordings} 份**录制"
        f"（另有 **{len(skipped)} 份是 14/45 列的老格式，没有 `player_motion_old_id` 列，已跳过**："
        + "、".join(s.replace("mhrise_", "").replace("_samples.csv", "") for s in skipped)
        + "）。",
        "",
        "**口径**：向上轴 **Y**（水平面 XZ）；位置**米**、速度 **m/s**，本表已 ×100 为**厘米 / cm·s⁻¹**。"
        "**帧数是主口径**（帧数 = 样本数 − 1；119.8 Hz ⇒ **1 帧 = 8.35 ms，这就是本方法的分辨率**），"
        "秒一律是派生显示值（帧数 ÷ 119.8）。`world_x`/`world_z` 是地图世界坐标，只有差值有意义。",
        "",
        "**招式名来源**：脚本内已核实的 `KNOWN_IDS` 优先，其次人工维护的 `虫棍招式表.md`"
        "（用户已确认其中那批通用动作的名字是对的）；两个来源都没有才写「（未核对）」。",
        "",
        "**三个词**：**跑满段** = 该动作**帧数众数簇**里的那些（按 **6 帧**分箱；"
        "取**占比 ≥30% 的箱里最靠右**的那个 —— 原因见下面「定长/变长」的说明）；"
        "**候选段** = **严格短于任何跑满段帧数**的段，即玩家在它自然结束前就接了下个动作；"
        "**最早可操作帧** = 候选段里帧数最少的那个 —— **除受击/失控外，后继是什么动作都可以当探针**。"
        "（旧口径只认后继为空中回避 `137`，那会导致**空中回避自己永远测不出最早可操作帧**。）",
        "",
        "## 一、招式真值（**定长招式**取跑满段的中位数；变长招式没有自然长度，见第四节）",
        "",
        "> **定长**：帧数聚成一个众数簇（占 ≥30%）—— 跑满段就是那一簇，它给出**自然长度**，"
        "表里的「自然长度」列才是可直接用的配置值。"
        "**变长**：长度由玩家按键时机决定（`160`、`4` 这类），**不存在「自然长度」**，"
        "那一格一律留 `—`，不要拿中位数冒充。",
        "> **为什么取「合格箱里最靠右」而不是「占比最高的那一箱」**：玩家**越认真探一个招式，"
        "它的提前取消样本就越多**，取消簇迟早盖过自然簇，众数会把自然长度定成取消长度、"
        "候选集变空、数字凭空消失。实测 `154` 舞踏：194 帧（自然玩完）28 次 vs 38 帧（最早取消）24 次，"
        "取消簇以 32:31 一票之差赢下众数，舞踏就从本表第二节彻底消失了。"
        "取消只会比自然长度**更短** ⇒ 自然长度必在分布右端。",
        "> **众数簇按帧数分箱（6 帧一箱），且排除录制首尾段与卡帧段** —— 理由是实测出来的："
        "一个 112 帧的 `147` 段因 0.178 s 卡帧，壁钟被抬到 1.093 s，按秒分箱时会和 131 帧的众数簇"
        "落进同一箱、把自己的后继并进「自然后继」。帧数分箱对此完全免疫。",
        "",
        "| id | 招式 | 长度 | 试验 | 完整观测 | 录制数 | **自然长度 帧** | ≈秒 | 帧范围 | **最长观测 帧** | 最长那次的下一段(语义) | 最高点 cm | 最高点范围 | 水平位移 cm | 起跳竖直速度 cm/s | 自然后继(语义) |",
        "|---|---|---|---:|---:|---:|---:|---:|---|---:|---|---|---:|---|---:|---:|---|",
    ]
    for motion_id in sorted(moves, key=lambda k: -moves[k]["trials"]):
        m = moves[motion_id]
        if m["trials"] < 3 or not m["full"]:
            continue
        natural = ", ".join(
            f"{n}" + (f"({name_of(n)})" if n in KNOWN_IDS else "")
            for n in sorted(m["natural"], key=lambda x: (x is None, x))
        )
        longest_next = (
            f"{m['longest_next']}({name_of(m['longest_next'])})" if m["longest_next"] else "—"
        )
        flag = "" if m["longest_frames"] - m["frames_range"][1] <= 1 else " **⚠**"
        # 变长招式**没有自然长度**：这里必须留空，不能拿中位数冒充（旧版的毛病就是给了个
        # 中位数、看着像个可直接用的配置值）。它的分布见第四节左尾表。
        if m["fixed_length"]:
            natural_frames = f"{m['frames']:.0f}"
            natural_seconds = f"{m['duration']:.3f}"
        else:
            natural_frames = "—"
            natural_seconds = "—"
        lines.append(
            f"| {motion_id} | {m['name']} | {'定长' if m['fixed_length'] else '**变长**'} "
            f"| {m['trials']} | {m['observed']} | {m['recordings']} "
            f"| {natural_frames} | {natural_seconds} "
            f"| {m['frames_range'][0]}–{m['frames_range'][1]} "
            f"| {m['longest_frames']}{flag} | {longest_next} "
            f"| {m['apex']:.1f} | {m['apex_range'][0]:.1f}–{m['apex_range'][1]:.1f} "
            f"| {m['horiz']:.1f} | {fmt(m['rise'])} | {natural} |"
        )

    lines += [
        "",
        "## 二、最早可操作帧（**除受击/失控外，后继是什么动作都可以当探针**）",
        "",
        "> ⚠ **本节的「最早帧」只在「后继是玩家按出来的」时才等于可操作帧。** "
        "2026-09-20 的觉虫击录制暴露了一类反例：",
        "> **自动转移**（由游戏判定触发，不是玩家输入）—— 它们的「最早帧」只反映**判定什么时候成立**，"
        "不是操作墙。已知的有：",
        "> `199 → 189`（觉虫击突进的猎虫**撞上目标**那一刻；实测 23–36 帧，飞满时 44 帧）、"
        "`187 → 189`、`188 → 189`（同上，操虫斩的猎虫命中）、"
        "`200 → 201 → 203`（穿刺命中后自动接）。**做 GA 时别把这些当操作窗口用。**",
        "",
        "> **探针口径**（2026-09-19 订正）：旧版只接受后继为 `137` 空中回避的段。那条规则有个硬伤 ——"
        "**用空中回避当唯一探针，就永远测不出空中回避自己的最早可操作帧**；而且游戏里的「可操作」"
        "不该依赖某一个特定后继动作。现在改成：候选段的后继**只要不是受击/失控类动作**就都算。",
        ">",
        "> **可信度按「取消进了哪个动作 id」判，不按样本量判**：「16 帧处取消进空中回避」哪怕只观测到"
        "一次也可信（探针动作无歧义）；「取消进受击」哪怕一百次也无意义。所以下面的样本数只是信息，"
        "不设「样本不足」门槛，另给「次早差几帧」让你判读这个孤点离其它观测有多远。",
        ">",
        "> **受击/失控排除集合**（2026-09-19 补录后定案）：`15/21/30/33/452/453`（击飞）+ `57/58/456`（硬直）。"
        "判据是**进入那一帧的瞬时速度跳变**，不是名字也不是扇入。击飞链全库零反例 —— 朝后飞"
        "`15 → [30] → 16 → 452`、朝前飞 `21 → [33] → 22 → 453`，中括号两段只在**空中被打中**时出现"
        "（地面受击高度 9.6–65.6 cm / 空中 143.5–273.6 cm，中间干净断层）。"
        "**`16`/`22` 没有收进来**：这两个 id 重载，链上的是 118 帧/668.4 cm 的受击翻滚，"
        "另有几段 30–90 帧/70–170 cm 的起步过渡，按 id 一刀切会误伤。详见脚本 `CONTROL_LOSS_IDS` 的注释。",
        "",
        "| id | 招式 | 候选段 | **最早 帧** | ≈秒 | 同帧次数 | 次早差 帧 | 前置段 | 前置段帧数 | **蒙太奇 帧** | ≈秒 | 该刻抬升 cm | 该刻水平 cm |",
        "|---|---|---:|---:|---:|---:|---:|---|---:|---:|---:|---:|---:|",
    ]
    for e in sorted(earliest, key=lambda e: e["id"]):
        s = e["earliest"]
        prefix_txt = f"`{e['prefix_id']}` {name_of(e['prefix_id'])}" if e["prefix_id"] else "—"
        prefix_txt2 = f"{e['prefix_const']}" if e.get("prefix_const") is not None else "不恒定"
        gap_txt = f"{e['next_gap']}" if e["next_gap"] is not None else "—"
        # 蒙太奇时间 = 前置段帧数 + 该段最早帧数，**但仅当前置段属于同一个蒙太奇时**。
        # 后撑杆跳的蒙太奇从「跳段」146/158 开始（它们恒定），所以要加；
        # 舞踏的蒙太奇从 154 自己开始，前置的 160 是另一段招式（且长度不恒定），
        # 所以不加 —— 但「是否同一蒙太奇」是项目知识，数据里推不出来，故加 `*` 标记。
        if e.get("prefix_const") is not None:
            montage_frames = e["prefix_const"] + s.frames
            montage = f"{montage_frames}"
            montage_s = f"{montage_frames / SAMPLE_HZ:.3f}"
        else:
            montage = f"{s.frames} \\*"
            montage_s = f"{s.seconds:.3f} \\*"
        lines.append(
            f"| {e['id']} | {e['name']} | {e['cand_total']} "
            f"| **{s.frames}** | {s.seconds:.3f} | {e['earliest_count']} | {gap_txt} "
            f"| {prefix_txt} | {prefix_txt2} "
            f"| **{montage}** | {montage_s} | {s.end_rise:.1f} | {s.horiz:.1f} |"
        )

    lines += [
        "",
        "**「该刻抬升/水平」= 候选段**末帧**相对段首的位移** —— 段在被取消那一帧结束，"
        "所以末帧就是可操作那一刻。（注意：不是该段的 apex。）",
        "",
        "## 三、各后继各自的最早（**通用可操作帧的检验**）",
        "",
        "> 每个后继动作**各自**给出一个「最早帧数」（= 取消进该动作的候选段里最短的那个）。"
        "若游戏里存在**通用**的最早可操作帧，那么**同一状态层内**所有合法后继的最小帧数应当"
        "**完全相等**（先写死判据再看数据，避免事后合理化）。不一致 ⇒ 该招式是**逐后继门槛**。",
        ">",
        "> 实测两例：`148 起跳落地` 的 **六个**后继（`104`/`118`/`141`/`145`/`155` 等）最早**全是 50 帧**"
        "⇒ 强烈支持通用帧（另有 `160` 一次 47 帧的孤点）；而 `147 向后起跳后段` 是 `137` 16 帧"
        "vs `186` 51 帧 ⇒ 逐后继门槛。",
        ">",
        "> **必须按状态分层比较**：地面动作接不了空中回避、空中动作接不了地面动作，跨层比较没有意义。"
        "「状态」取自该后继动作自己的段中位 apex（>50 cm 记「空」）。",
        ">",
        "> 另注意 `min` 对样本量单调（样本多的一方永远不输），所以「谁最早」有一半是样本量的产物 ——"
        "看结论请连**候选数**一起看。",
        "",
        "| id | 招式 | 后继 | 状态 | **该后继的最早 帧** | 该后继的候选数 |",
        "|---|---|---|---|---:|---:|",
    ]
    for e in sorted(earliest, key=lambda e: e["id"]):
        for succ, (frames, count) in e["by_successor"].items():
            lines.append(
                f"| {e['id']} | {e['name']} | {succ}({name_of(succ)}) "
                f"| {state_of(moves, succ)} | {frames} | {count} |"
            )

    lines += [
        "",
        "## 四、变长招式的左尾（**没有自然长度，就没有最早可操作帧**）",
        "",
        "> 变长招式（长度由玩家按键时机决定）分不出「被取消」，所以第二节里没有它们。"
        "但**不该因此让它们在表里彻底消失**（旧版就是这样，静默的）：左尾分布能回答"
        "「有没有输入门」—— 真正的输入窗口会形成**堆积（墙）**，强制转移则是**平滑斜坡**。"
        "左尾平滑 ⇒ 该招式随时可被打断，没有可测的最早帧。",
        "",
        "| id | 招式 | 完整观测 | 左尾（帧数 × 次数）|",
        "|---|---|---:|---|",
    ]
    for motion_id in sorted(moves, key=lambda k: -moves[k]["trials"]):
        m = moves[motion_id]
        if m["fixed_length"] or m["trials"] < 3 or not m["tail"]:
            continue
        tail = " ".join(f"{f}×{c}" for f, c in m["tail"])
        lines.append(f"| {motion_id} | {m['name']} | {m['observed']} | {tail} |")

    lines += [
        "",
        "## 五、每帧轨迹",
        "",
        "逐帧数据**不放在本文档**（120 Hz × 2 s 就是 240 帧，塞进表格会撑爆）。"
        "生成器把它们写到 `Saved/_mhr_frames/<id>_<招式名>.csv`（**不入库**），"
        "每行一帧：`trial, frame, t_s, x_cm, y_cm, z_cm, vx, vy, vz`，坐标为**相对该段首帧**。",
        "",
        "将来要烘成 `UCurveVector` 资产时读它。**注意消费方约束**："
        "`FWeaponMovementRequest::PathOffsetCurve` 要求 X 沿 `DirectionSnapshot`、"
        "**起止偏移都为零**，所以不能把录制原样烘进去，得先减掉线性分量做归一化。",
        "",
    ]
    return "\n".join(lines)


def write_frames(moves: dict) -> int:
    OUT_FRAMES.mkdir(parents=True, exist_ok=True)
    written = 0
    kept: set[str] = set()
    for motion_id, m in moves.items():
        if not m["full"]:
            continue
        safe = m["name"].replace("（", "_").replace("）", "").replace("/", "_")
        path = OUT_FRAMES / f"{motion_id}_{safe}.csv"
        kept.add(path.name)
        with path.open("w", newline="", encoding="utf-8") as handle:
            writer = csv.writer(handle)
            writer.writerow(["trial", "frame", "t_s", "x_cm", "y_cm", "z_cm", "vx", "vy", "vz"])
            for trial, seg in enumerate(m["full"]):
                x0 = num(seg.rows[0], "world_x")
                y0 = num(seg.rows[0], "world_y")
                z0 = num(seg.rows[0], "world_z")
                t0 = num(seg.rows[0], "capture_time_s")
                for frame, row in enumerate(seg.rows):
                    writer.writerow(
                        [
                            trial,
                            frame,
                            f"{num(row, 'capture_time_s') - t0:.5f}",
                            f"{(num(row, 'world_x') - x0) * 100.0:.2f}",
                            f"{(num(row, 'world_y') - y0) * 100.0:.2f}",
                            f"{(num(row, 'world_z') - z0) * 100.0:.2f}",
                            f"{num(row, 'velocity_x') * 100.0:.2f}",
                            f"{num(row, 'velocity_y') * 100.0:.2f}",
                            f"{num(row, 'velocity_z') * 100.0:.2f}",
                        ]
                    )
        written += 1
    # 招式改名会让上一轮的旧文件变成孤儿（例如 `186` 从「空中操虫斩（存疑）」改名）。
    # 这是生成目录，清掉不认识的 *.csv，保持「目录内容 == 本轮写出」。
    for stale in OUT_FRAMES.glob("*.csv"):
        if stale.name not in kept:
            stale.unlink()
    return written


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--no-frames", dest="frames", action="store_false", help="不写逐帧中间文件")
    args = parser.parse_args()

    if not RECORDER_DIR.is_dir():
        print(f"找不到录制目录：{RECORDER_DIR}", file=sys.stderr)
        return 1

    recordings = sorted(RECORDER_DIR.glob("*_samples.csv"))
    all_segs: list[Seg] = []
    skipped: list[str] = []
    used = 0
    for recording in recordings:
        segs = collect_segments(recording)
        if segs is None:
            skipped.append(recording.name)
            continue
        used += 1
        all_segs.extend(segs)

    moves = build(all_segs)
    earliest = earliest_frames(moves)

    OUT_MD.parent.mkdir(parents=True, exist_ok=True)
    OUT_MD.write_text(render(moves, earliest, skipped, used), encoding="utf-8")
    print(f"写入 {OUT_MD}（{used} 份录制 / {len(all_segs)} 段 / {len(moves)} 个 id；跳过 {len(skipped)} 份老格式）")

    if args.frames:
        count = write_frames(moves)
        print(f"写入 {count} 个逐帧文件 → {OUT_FRAMES}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
