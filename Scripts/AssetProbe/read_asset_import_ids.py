#!/usr/bin/env python3
# Copyright MHGZ Project. All Rights Reserved.
"""从 `.uasset` **直接**读出每个动画资产的**原始导入文件名**，还原出 MHRise 招式序号。

## 为什么需要它

MHRise 的动画导出文件是**按序号命名的**（`001.fbx` … `172.fbx`，序号就是
`player_motion_old_id`）。导入 UE 之后资产被改成了人类可读的名字
（`AS_Unsh_TuCi` 之类），**名字对不对没有机器可查的依据** —— 而真值表、
`build_*_curves.py` 全都按**序号**说话，两边对不上就只能靠手抄。

## 为什么**不用**开编辑器（这是本脚本存在的理由）

原始文件名就**明文躺在 `.uasset` 里**：`AssetImportData` / `InterchangeAssetImportData`
把导入来源存成一段 JSON ——

    [{ "RelativeFilename" : "../../../../../cache/study/MHR/export/CHONG_GUN/002.fbx", ... }]

`FName` 表与这段 JSON 都是纯文本，所以**宿主机上一段 `read_bytes` + 正则**就能全量还原，
不必启动 `UnrealEditor-Cmd.exe -run=pythonscript`。（走 UE Python API 读
`UAnimSequence::AssetImportData` 得到的是同一串，但那是「要开编辑器」的路子；
批量核对名字时开着编辑器只为读一个字符串不值得。）

⚠ **不要用资产名去反推序号**，也不要信「名字看起来对」—— 以本脚本读出的
`RelativeFilename` 的数字词干为准。

## 顺序对得上的证据

与真值表**独立**对上（真值表的 id 是从实机遥测 `player_motion_old_id` 得到的，
与本脚本没有任何共同来源）：

| 序号 | 本脚本读到的资产 | 遥测侧已证实的招式 |
|---|---|---|
| 104 | `AS_Unsh_TuCi` | 突刺 |
| 105 | `AS_Unsh_TiaoYue` | 跳跃斩 |
| 107 | `AS_Unsh_ChongYin` | 虫印（近战） |
| **186/187/189** | `CaoChongZhan` / `_Flying` / `_Hit` | 空中操虫斩 → 前冲 → 命中进入舞踏（整条链逐帧核对过） |
| **193/195/199/206** | `JueChongJi` / `_Hit` / `_Flying` / `LieChongHua_Hit` | 觉虫击起手 → 丢虫 → 飞行突进 → 命中落地（整条链逐帧核对过） |
| **200/201/203** | `CaoChongChuanCi` / `_Flying` / `_Over` | 操虫穿刺 → 强化猎虫穿刺 → 落地攻击（**不是**「绝虫击突进命中」，见表下 A 类） |
| **452/453** | `大硬直起身（趴地）` / `（坐地）` | 击飞链第四段（由「进入那一帧有瞬时速度跳变」判定） |

用法：
    python Scripts/AssetProbe/read_asset_import_ids.py            # 写 资产序号对照.md
    python Scripts/AssetProbe/read_asset_import_ids.py --print     # 只看，不写文件
"""

from __future__ import annotations

import argparse
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[2]
ASSET_ROOT = PROJECT / "Content" / "Weapons" / "InsectGlaive" / "Anims"
OUT_MD = PROJECT / "docs" / "reference" / "资产序号对照.md"

sys.path.insert(0, str(PROJECT / "Scripts" / "MHRise"))
try:
    import build_truth_table as BT  # noqa: E402

    name_of = BT.name_of
    TABLE_IDS = set(BT.TABLE_IDS) | set(BT.KNOWN_IDS)
except Exception:  # pragma: no cover
    def name_of(key: str) -> str:
        return "（未核对）"

    TABLE_IDS: set[str] = set()

# 招式表本体 —— 快照指纹要用（见 `table_snapshot()`）。
TABLE_DOC = PROJECT / "docs" / "reference" / "虫棍招式表.md"

# `AssetImportData` 把来源存成 JSON；`RelativeFilename` 是其中唯一需要的那一项。
RELATIVE = re.compile(rb'RelativeFilename"\s*:\s*"([^"]+)"')

# 已知「资产名与真值表名对不上」的，人工登记在这里 —— 这类判断机器做不了
# （一边是中文招式名，一边是拼音资产名），但**发现了就必须写下来**，否则下次重新查。
# **两边名字都现读、不写死**：招式表 `虫棍招式表.md` 由人随时改（本轮就边写边改过），
# 资产名从 `.uasset` 现读 —— 所以这张表只在脚本里记**类别 + 理由**，名字由 render 填。
# 若你已改招式表使两边一致，把该条目从这里删掉（脚本判不了「一致」：一边拼音一边中文）。
# 逐条判过、**需要处理**的差异 —— 只存说明，名字由 render 现读。
# 检测不再靠这张表（那会漏），而是靠 `verdict()` 全量跑；这里只补「为什么」。
NAME_CONFLICTS: dict[str, tuple[str, str]] = {
    "188": ("A", "**已解决**：表名已从「未击中」改成「操虫斩结束」，与资产译名一致"
                  "（`Over` = 结束，不是未命中）。这条现在渲染不出来，留着是怕判定哪天翻回来时说明又对不上。"),
    "189": ("A", "**语义一致，只差状态词**：改名后资产已覆盖两个入口"
                  "（`AS_Unsh_CaoChongZhanJueChongJiWuTa`→「操虫斩觉虫击舞踏」），与录制相符"
                  "（前驱 `187` 操虫斩 86 段、`199` 觉虫击 7 段）；表名多的只是「命中 / 进入」这层动作状态词。"
                  "改名前它叫 `AS_Unsh_CaoChongZhan_Hit`，只覆盖操虫斩那一半 —— 那正是当时这条要指出的问题。"),
    "196": ("A", "**同义，非差异**（2026-09-20 本人确认）：资产用 `Pose` 是因为当时不知道「前摇」怎么拼，"
                  "指的就是表名那个「突进前摇」。保留 `Pose` 不动。"
                  "（这批资产里 `Pose` 一律等于「前摇 / 起手架势」，见下「通用限制」第 5 条。）"),
    "206": ("A", "**语义一致，只差状态词**：2026-09-20 把资产改名为 "
                  "`AS_Unsh_LieChongHuaJueChongJiFall`（→「猎虫滑翔觉虫击下坠」），已按 `189` 的范式"
                  "覆盖两个入口；表名多的只是「命中 / 未命中」这层进入条件。"
                  "改名前它叫 `AS_Unsh_LieChongHua_Hit`，只写了猎虫滑翔命中那一半 —— 那正是当时这条要指出的问题。"),
    "450": ("A", "**已解决**：表名已改成「小硬直起身（坐地）」，与资产一致。"
                  "（原先表里写「受伤坐下后的站起」，缺了「小」这一档。）"),
    "451": ("A", "**已解决**：表名已改成「小硬直起身（趴地）」，与资产一致。同上。"),
    "454": ("B", "**已解决**：表名已从「风压恢复」改成「风压硬直」，与资产一致。"),
    "455": ("B", "**已解决**：表名已从「吼叫恢复」改成「龙吼硬直」，与资产一致。"),
    "456": ("B", "**已解决**：表名已从「地震恢复」改成「地震硬直」，与资产一致。"),
    "154": ("B", "**语义一致，只差措辞**：表名「突进回旋斩进入舞踏」vs 资产译名「突进回旋斩舞踏」，"
                  "少的就是「进入」二字。2026-09-20 把资产改名为 `AS_Unsh_TuJinHuiXuanWuTa` 之后两边已对上"
                  "（改名前它叫 `AS_Unsh_WuTa`，译名只有「舞踏」，那时这条记的是「资产只写了动作、没写入口」）。"),
    "169": ("B", "**已解决**：表名与资产名现在都是「猎虫回归结束」，一致。"
                  "（原先两边一个写「起跳动作」、一个写「后段」。）"),
    "113": ("B", "资产译名「突进」，表名写「跳跃突进斩」（`124` 是它的强化版 `R_TuJin`，表里已写「强化跳跃突进斩」）。"),
    "108": ("B", "资产译名「后撤」，表名补了「斩」。"),
    "165": ("B", "资产译名「铁虫丝」，表名补了「跳跃」。"),
}

# 问号条目已按资产名填上（2026-09-20）—— 记录在案，免得下次又去查。
RENAMED_FROM_QUESTION = [
    ("205", "？几乎同191", "猎虫滑翔飞行结束（几乎同191）", "`AS_Unsh_LieChongHua_Flying_Over`"),
    ("301", "未知动作衔接", "横扫接上捞衔接", "`AS_Unsh_HengSaoShangLao`"),
    ("302", "未知动作衔接", "横扫接虫印衔接", "`AS_Unsh_HengSaoChongYin`"),
    ("303", "未知动作开头", "横扫接敲打起手", "`AS_Unsh_HengSaoQiaoDa`"),
    ("304", "未知动作", "强化横扫接飞远斩", "`AS_Unsh_R_HengSaoFeiYuan`"),
    ("305", "未知动作", "强化横扫接上捞", "`AS_Unsh_R_HengSaoShangLao`"),
    ("306", "未知动作", "待机接突进回旋斩", "`AS_Unsh_IdleTuJinHuiXuan`"),
    ("307", "未知动作", "强化上捞接袈裟", "`AS_Unsh_R_ShangLaoJiaSha`"),
    ("308", "未知动作", "飞身跃入斩接上捞", "`AS_Unsh_FeiShenYueRuShangLao`"),
    ("309", "未知动作", "强化横扫", "`AS_Unsh_R_HengSao`（**只有一段**，与 304/305/312 不同）"),
    ("310", "未知动作", "强化二连接飞远斩", "`AS_Unsh_R_ErLianFeiYuan`"),
    ("312", "未知动作", "强化横扫接印斩", "`AS_Unsh_R_HengSaoYinZhan`"),
    ("313", "未知动作", "降龙接飞身跃入斩", "`AS_Unsh_JiangLongFeiShenYueRu`"),
    ("314", "未知衔接", "降龙接突进斩", "`AS_Unsh_JiangLongTuJin`"),
]

# 整段疑似错位的（逐 id 说不清，但整块看很可疑）—— 写下来免得下次重新怀疑。
RANGE_NOTES = [
    "",
    "## 五条通用限制（别把它们当成「已经查过了」）",
    "",
    "1. **录制只能命名「你真的做过的动作」。** 没做过的 id 在录制侧**永远不可证伪**"
    "（`75`/`76`/`78`、`400`–`404`、`411`/`412`、`450`–`463` 都是），它们的名字只能靠资产名。"
    "所以「用户说名字是对的」与「录制证明名字是对的」是两件事 —— 后者只覆盖录到过的 id。",
    "2. **资产的拼音名不等于官方中文名。** 有一批是「资产是动作描述、表是官方译名」"
    "（`117 飞远斩` / `R_FeiYuan`、`161 四连印斩` / `R_YinZhan`），也有反过来的。"
    "**两者都要留**：讲中文时用表名，对导出文件时用资产名。",
    "3. **`R_` 前缀不是统一含义。** 在 `118`–`122` 上是「强化」（`R_TuCi` = 强化突刺），"
    "在 `124 R_TuJin` 上也是（表里已写「强化跳跃突进斩」），"
    "但 `117 R_FeiYuan` 表里叫「飞远斩」、`161 R_YinZhan` 叫「四连印斩」—— 那两个是官方名。"
    "所以**别看到 `R_` 就机械翻成「强化」**，要看它在哪一支里。",
    "4. **表里没有、但有资产的 id**：`619`（资产 `QieShu`）。反过来，表里 `311`/`459`–`462` "
    "**既没资产也没录到过**，只能保持「未知」。",
    "5. **`Pose` = 「前摇 / 起手架势」，是同义不是差异。** 拼资产名时不知道「前摇」怎么拼，"
    "就用了 `Pose`（`196 JueChongJi_Pose` = 绝虫击突进前摇）。见到「表名说前摇、资产说 `Pose`」"
    "**不要当成待解问题**。",
    "",
]


CJK = re.compile(r"[\u4e00-\u9fff]")


def asset_cn(asset_name: str) -> str | None:
    """资产名去掉 `AS_Unsh_` / `AS_UnSh_` 前缀后**若含中文**，就返回那段中文；否则 None。

    中文资产名可以**直接跟招式表名比**（`AS_Unsh_铁虫丝跳跃落地` ↔ 表「铁虫丝跳跃落地」），
    拼音资产名不行（要人译）。所以自动判「已一致」只对中文资产名做得到。
    """
    for prefix in ("AS_Unsh_", "AS_UnSh_"):
        if asset_name.startswith(prefix):
            tail = asset_name[len(prefix):]
            return tail if CJK.search(tail) else None
    return None


# ---------------------------------------------------------------------------
# 资产名 → 中文译名
#
# **为什么需要它**：这张表的目的就是「两边名字哪里不一样」。但一边是拼音、一边是中文，
# 机器没法直接比。之前只靠人工登记「我注意到的」条目 —— 于是 `189` 这种明显的差异
# （表「操虫斩/觉虫击命中进入舞踏」vs 当时的资产名 `CaoChongZhan_Hit`=操虫斩命中）**一直没被列出来**。
# 现在改成：**把每个资产名逐词素译成中文，再和表名严格比**，保证不漏。
#
# `R` 译成空串（它在 `117`/`124`/`161` 上是官方名、在 `118`–`122` 上是「强化」，
# 机械翻成「强化」会制造假差异）；`W` 译成「白灯」。
LEXICON: dict[str, str] = {
    "R": "", "W": "白灯", "Idle": "待机", "Air": "空中",
    "Forward": "前", "Back": "后", "Left": "左", "Right": "右",
    # 资产侧的大小写变体：`180` 是 `YinDan_Over_ForWard`、`618` 是 `yHui_back`。
    # 不收这两个整词，它们就会退化成 `?ForWard`/`?back`，判定降级成「需人工」而不是「有差异」。
    "ForWard": "前", "back": "后",
    "Up": "上", "up": "上", "Down": "下", "Shoot": "发射",
    "Over": "结束", "Flying": "飞行", "Fall": "下坠", "FallDown": "落地",
    "Fall_Loop": "下坠循环", "Loop": "循环", "Pose": "姿势", "Hit": "命中",
    "Send": "丢出", "Hold": "持续", "Walk": "走", "Dash": "翻滚",
    "High": "高", "Low": "低", "Medium": "中", "Jump": "跳跃", "Junp": "跳跃",
    "BaDao": "拔刀", "ShouDao": "收刀", "MoDao": "磨刀", "yHui": "y回", "QieShu": "切书",
    "TuCi": "突刺", "TiaoYue": "跳跃斩", "JiaSha": "袈裟", "ShangLao": "上捞",
    "ErLian": "二连斩", "HengSao": "横扫", "QiaoDa": "敲打", "ChongYin": "虫印",
    "HouChe": "后撤", "TuJin": "突进", "TuJinHuiXuan": "突进回旋斩",
    "FeiYuan": "飞远斩", "YinZhan": "印斩", "YinDan": "印弹",
    "FeiShenYueRu": "飞身跃入斩",
    "WuTa": "舞踏", "CaoChong": "操虫", "CaoChongZhan": "操虫斩",
    "JueChongJi": "绝虫击", "LieChongHua": "猎虫滑翔",
    "CaoChongChuanCi": "操虫穿刺", "CaoChongChuanci": "操虫穿刺",
    "CaoChongChuanCI": "操虫穿刺",
    "TieChongSi": "铁虫丝", "JiangLong": "降龙",
    # `300` 系是**衔接动作**（两段动作名拼的），译成「A+B」而不是「A 接 B」，方便严格比
    "HengSaoShangLao": "横扫上捞", "HengSaoChongYin": "横扫虫印",
    "HengSaoQiaoDa": "横扫敲打", "HengSaoFeiYuan": "横扫飞远斩",
    "ShangLaoJiaSha": "上捞袈裟", "FeiShenYueRuShangLao": "飞身跃入斩上捞",
    "ErLianFeiYuan": "二连飞远斩", "HengSaoYinZhan": "横扫印斩",
    "JiangLongFeiShenYueRu": "降龙飞身跃入斩", "JiangLongTuJin": "降龙突进",
    "IdleTuJinHuiXuan": "待机突进回旋斩", "BaDaoFeiShenYueRu": "拔刀飞身跃入斩",
    # 两个「动作+舞踏」拼起来的名字（2026-09-20 改名后出现）。词典是**按 `_` 切词**的，
    # 不收录这两个整词就会退化成 `?TuJinHuiXuanWuTa` / `?CaoChongZhanJueChongJiWuTa`，
    # 判定降级成「需人工」，把 `154`/`189` 两行从「可判」变成「没法判」。
    "TuJinHuiXuanWuTa": "突进回旋斩舞踏", "CaoChongZhanJueChongJiWuTa": "操虫斩觉虫击舞踏",
    "LieChongHuaJueChongJiFall": "猎虫滑翔觉虫击下坠",
    # 受伤族
    "Hited": "受伤", "Hitted": "受伤", "Little": "小", "LIttle": "小",
    # 无信息
    "UnKnown": "", "115": "", "116": "",
}


def gloss(asset_name: str) -> str:
    """把资产名译成中文。**已经是中文的原样返回**；未收录的词素保留成 `?Tok`。

    `?` 出现 ⇒ 译名不可信，判定会降级成「需人工」而不是硬判。
    """
    tail = asset_name
    for prefix in ("AS_Unsh_", "AS_UnSh_"):
        if tail.startswith(prefix):
            tail = tail[len(prefix):]
            break
    if CJK.search(tail):
        return tail
    out = []
    for tok in tail.split("_"):
        if tok in LEXICON:
            out.append(LEXICON[tok])
        elif tok:
            out.append("?" + tok)
    return "".join(out)


def _norm(name: str) -> list[str]:
    """归一化成一串候选名：去掉括号注解、按 `/` 拆、去掉「的」与空白。"""
    name = re.sub(r"[（(].*?[)）]", "", name)
    parts = re.split(r"[/／、]", name)
    return [re.sub(r"[\s的]", "", p) for p in parts if re.sub(r"[\s的]", "", p)]


# 「强化」在表里是修饰，`R_` 前缀译不出来；比名字时忽略它，免得 `120 强化上捞 / 上捞`
# 这种被算成差异。
IGNORE_IN_NAME = re.compile(r"[\s的]|强化")


def verdict(table_name: str, asset_name: str) -> str:
    """返回四档之一：

    - `一致` —— 去掉括号注解 / `强化` / `的` / 空白后**完全相同**
    - `表名更详细` —— 表名的某一段**包含**资产译名（如 `189`：表「操虫斩/觉虫击命中进入舞踏」）
    - `资产更详细` —— 资产译名**包含**表名的某一段（如 `168`：资产「猎虫回归前摇」）
    - `两者不同` —— 互不包含（这才是**真的对不上**）
    - `需人工` —— 词典没覆盖那个词素，译名不可信

    **为什么要四档而不是一致/不同**：只报「不同」的话，`189` 这种「表名比资产更详细」
    会被包含判定吞掉、看不见（这正是本轮用户指出的漏报）；只报严格不同又会把
    `120 强化上捞 / 上捞` 这种噪音全倒出来。分级之后**全都列出来，但一眼能看出轻重**。
    """
    g = gloss(asset_name)
    if "?" in g:
        return "需人工"
    g2 = IGNORE_IN_NAME.sub("", re.sub(r"[（(].*?[)）]", "", g))
    cands = [IGNORE_IN_NAME.sub("", c) for c in _norm(table_name)]
    if g2 in cands:
        return "一致"
    if any(g2 and c and c in g2 for c in cands):
        return "资产更详细"
    if any(g2 and c and g2 in c for c in cands):
        return "表名更详细"
    return "两者不同"


def table_snapshot() -> str:
    """招式表当前的 (mtime, sha1 前 8 位)。

    **为什么要印出来**：招式表由人随时改，生成物可能在编辑中途被重跑 —— 那样读到的是
    **中间态**（本轮就发生过：一次读到「猎虫滑翔命中以及」，几分钟后同一行变成
    「…以及觉虫击未命中的下坠段」）。把快照写进文档，读的人一眼能看出这份生成物对应哪一版。
    """
    if not TABLE_DOC.exists():
        return "（招式表不存在）"
    import datetime
    import hashlib
    mtime = datetime.datetime.fromtimestamp(TABLE_DOC.stat().st_mtime).strftime("%Y-%m-%d %H:%M:%S")
    return f"{mtime} · sha1 `{hashlib.sha1(TABLE_DOC.read_bytes()).hexdigest()[:8]}`"


def looks_resolved(table_name: str, asset_name: str) -> bool:
    """只在**完全一致**时算「没有差异」。保留别名是为了少改调用点。"""
    return verdict(table_name, asset_name) == "一致"


def scan() -> list[dict]:
    out = []
    for path in sorted(ASSET_ROOT.rglob("*.uasset")):
        raw = path.read_bytes()
        hits = sorted({m.decode("utf-8", "replace") for m in RELATIVE.findall(raw)})
        ids = sorted({int(Path(h).stem) for h in hits if Path(h).stem.isdigit()})
        out.append(dict(
            name=path.stem,
            folder=path.parent.name,
            asset=str(path.relative_to(PROJECT)).replace("\\", "/"),
            ids=ids,
            sources=hits,
        ))
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--print", dest="write", action="store_false", help="只看，不写文件")
    args = parser.parse_args()

    if not ASSET_ROOT.is_dir():
        print(f"找不到资产目录：{ASSET_ROOT}", file=sys.stderr)
        return 1
    assets = scan()
    with_id = [a for a in assets if a["ids"]]
    without = [a for a in assets if not a["ids"]]

    by_id: dict[int, list[dict]] = defaultdict(list)
    for a in with_id:
        for i in a["ids"]:
            by_id[i].append(a)

    L = [
        "# 动画资产 ↔ MHRise 招式序号 对照（生成物，勿手改）",
        "",
        "> 由 `Scripts/AssetProbe/read_asset_import_ids.py` 从 `.uasset` 直接读出。",
        "> **改数请改脚本或重导资产。**",
        "",
        "**序号从哪来**：MHRise 的动画导出文件**按序号命名**（`001.fbx` … ），序号就是遥测里的",
        "`player_motion_old_id`。导入 UE 后资产被改成人类可读的名字，**原始名字仍明文存在资产里**：",
        "`AssetImportData` → `SourceData[].RelativeFilename`。",
        "",
        "**⚠ 不要用资产名反推序号** —— 以本表第 1 列（从资产里读出的数字词干）为准。",
        "",
        f"扫到 **{len(assets)}** 个资产：带序号的 **{len(with_id)}**，"
        f"不带序号的 **{len(without)}**（UE 原生资产：montage、混合、手搓的），"
        f"覆盖 **{len(by_id)}** 个序号。",
        "",
        f"**招式表快照**：`虫棍招式表.md` @ {table_snapshot()}",
        "",
        "> ⚠ 招式表由人随时改。**若上表时间早于招式表最后一次编辑，本文件的「招式表名」列就是旧的**"
        "—— 重跑本脚本即可。",
        "",
        "## 一、序号 ↔ 招式表名 ↔ 资产",
        "",
        "| 序号 | 招式表名 | 资产 | 目录 |",
        "|---:|---|---|---|",
    ]
    for i in sorted(by_id):
        nm = name_of(str(i))
        mark = "" if str(i) in TABLE_IDS else " ｜⚠ 招式表里没有"
        for a in sorted(by_id[i], key=lambda a: a["name"]):
            L.append(f"| `{i}` | {nm}{mark} | `{a['name']}` | {a['folder']} |")

    missing = sorted(int(x) for x in TABLE_IDS
                     if x.isdigit() and int(x) < 700 and int(x) not in by_id)
    L += [
        "",
        f"## 二、招式表里有、但**没有对应资产**的序号（{len(missing)} 个）",
        "",
        "```",
        ", ".join(str(i) for i in missing) or "（无）",
        "```",
        "",
        "多半是**共用基础动作库**（站立 / 跑 / 起跑 / 转向 / 击飞翻滚）—— 它们不在虫棍这一批导出里，",
        "所以不能因为「找不到资产」就断定这些 id 没用。",
        "",
        "## 三、没有原始序号的资产",
        "",
        f"共 {len(without)} 个，按目录分布：" +
        "、".join(f"`{k}`×{v}" for k, v in Counter(a["folder"] for a in without).most_common()) + "。",
        "",
        "它们是 **UE 原生**的（montage 由序列拼出来、混合空间、手搓的），本来就没有导入来源，",
        "所以读不到 `RelativeFilename` 是**正常**的，不是缺数据。",
        "",
        "## 四、招式表名 ↔ 资产译名 **全量对照**（生成物，逐条过）",
        "",
        "机器做不了「名字对不对」的判断（一边中文招式名、一边拼音资产名），所以这里做的是：",
        "**把每个资产名逐词素译成中文，再和表名比**（词典在脚本的 `LEXICON`）。",
        "**只要不「完全一致」就列出来** —— 上一版只列人工登记过的条目，结果 `189` 这种明显的差异",
        "（表「操虫斩/觉虫击命中进入舞踏」vs 资产译名「操虫斩命中」）**一直没被列出来**。",
        "",
        "**性质**（自动判，见 `verdict()`）：",
        "",
        "| 性质 | 含义 |",
        "|---|---|",
        "| **两者不同** | 互不包含 —— 要么是词序差异，要么是真的对不上，**要逐条看** |",
        "| **表名更详细** | 表名的某一段包含资产译名（表里多写了入口/后段之类） |",
        "| **资产更详细** | 资产译名包含表名的某一段（表里漏了后缀） |",
        "| **需人工** | 词典没覆盖那个词素，译名不可信 |",
        "",
        "**说明**列只有我逐条判过、觉得要处理的才有；空着 = 看着像纯词序差异。",
        "",
        "| 序号 | 性质 | 招式表名 | 资产译名 | 说明 |",
        "|---:|---|---|---|---|",
    ]
    asset_of = {str(i): a["name"] for a in with_id for i in a["ids"]}
    rows = []
    for key in sorted(asset_of, key=int):
        cur = asset_of[key]
        v = verdict(name_of(key), cur)
        if v == "一致":
            continue
        note = NAME_CONFLICTS.get(key)
        rows.append((int(key), v, name_of(key), gloss(cur), note[1] if note else ""))
    order = {"两者不同": 0, "需人工": 1, "资产更详细": 2, "表名更详细": 3}
    for _, v, tn, gl, note in sorted(rows, key=lambda r: (order[r[1]], r[0])):
        L.append(f"| `{_}` | {v} | {tn} | {gl} | {note} |")
    L += [
        "",
        f"（共 **{len(rows)}** 条与表名不完全一致；另有 {sum(1 for k in asset_of if verdict(name_of(k), asset_of[k]) == '一致')} 条完全一致。）",
        "",
        "> ⚠ **译名是机器逐词素拼的，不是官方中文名** —— 它只用来看「两边说的是不是同一件事」。",
        "> 词序不同（`翻滚前` vs `正向翻滚`）在这张表里会显示成「两者不同」，那是**噪音**，不用管。",
        "> 真正要处理的是**语义对不上**的那些（例如表名写了一个资产里没有的动作）。",
        "",
        "## 四之二、从「?/未知」按资产名补上名字的条目（2026-09-20）",
        "",
        "这几行的资产名是**两段动作名拼起来的**（如 `IdleTuJinHuiXuan` = 待机 + 突进回旋斩，"
        "而待机确实在前）⇒ 它们是**衔接动作**，按 **「A 接 B」** 译：",
        "",
        "| 序号 | 原表名 | 现表名 | 依据资产 |",
        "|---:|---|---|---|",
    ]
    for k, before, after, asset in RENAMED_FROM_QUESTION:
        L.append(f"| `{k}` | {before} | **{after}** | {asset} |")
    L += [
        "",
        "**没改的**：`112`（资产 `AS_Unsh_UnKnown` —— **资产自己也是「未知」**）、"
        "`311`/`459`/`460`/`461`/`462`（既无资产也无录制）。",
        "",
    ]
    L += RANGE_NOTES
    L += [
        "",
        "## 五、为什么这张表可靠（与遥测侧交叉验证）",
        "",
        "真值表的 id 来自**实机遥测**（`player_motion_old_id`），与本脚本**没有任何共同来源**。",
        "两边在这些点上逐条吻合：`104` 突刺 · `105` 跳跃斩 · `107` 虫印 · "
        "`186/187/189` 操虫斩整条链 · `200/201/203` 操虫穿刺整条链 · `452/453` 大硬直起身。",
        "**尤其 `452/453`**：资产名直接写着「大硬直起身（趴地/坐地）」，而遥测侧是从"
        "「进入那一帧有没有瞬时速度跳变」独立判出它们是击飞链的第四段的。",
        "",
    ]
    text = "\n".join(L) + "\n"
    if args.write:
        OUT_MD.parent.mkdir(parents=True, exist_ok=True)
        OUT_MD.write_text(text, encoding="utf-8")
        print(f"写入 {OUT_MD}")
        print(f"  {len(assets)} 个资产，带序号 {len(with_id)}，覆盖 {len(by_id)} 个序号，"
              f"招式表缺资产 {len(missing)} 个")
    else:
        print(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
