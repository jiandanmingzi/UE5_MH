"""Read-only asset probe for the M5 aerial/back-vault audit.

Run headless:

    UnrealEditor-Cmd.exe <abs>/MHGZ.uproject -run=pythonscript \
        -script="Scripts/AssetProbe/probe_m5_assets.py" \
        -unattended -nop4 -nosplash -stdout

It NEVER saves, renames or modifies an asset. Every section is wrapped so a
missing engine API degrades to a printed note instead of aborting the run, and
each section prints what it could not read.

What it answers
---------------
A8  why the back-vault apex reaches only 443.9 cm instead of the configured
    579.7: are the BackVaultTrajectory keys normalised to a peak below 1.0, or
    does the apex fall before StartProgress?  Also quantifies the recorded
    lateral (Y) amplitude, which Rise says should be near zero.
A9  whether AM_IG_AerialFall / the fall source sequences carry a root-bone
    offset in the pose.  The Host disables root-motion *extraction* only, so a
    non-zero root still displaces the mesh.
A7  the live values of every DanceVault* field, and whether this DataAsset
    overrides the C++ class defaults at all.
"""

from __future__ import annotations

import json
import math
import os
import traceback

import unreal


OUT_DIR = os.path.join(unreal.Paths.project_saved_dir(), "AssetProbe")
OUT_JSON = os.path.join(OUT_DIR, "m5_assets.json")

GA_BACK_VAULT = "/Game/Weapons/InsectGlaive/Abilities/GA_IG_HouChengGanTiao"
GA_COUNTER = "/Game/Blueprints/Ability/InsectGlaive/GA_IG_TuJinHuiXuanZhan"
DA_COMBAT = "/Game/Weapons/InsectGlaive/Data/DA_IG_Combat"
DA_COMBO = "/Game/Weapons/InsectGlaive/Data/DA_IG_Combo"
DA_INPUT = "/Game/Weapons/InsectGlaive/Data/DA_IG_InputProfile"

MONTAGE_DIR = "/Game/Weapons/InsectGlaive/Anims/Montage"
AERIAL_MONTAGES = ["AM_IG_HouChengGanTiao", "AM_IG_HouChengGanTiao_W",
                   "AM_IG_AerialFall", "AM_IG_AerialFall_W", "AM_IG_AerialLanding"]

SEQ_DIR = "/Game/Weapons/InsectGlaive/Anims/Sequences/Imported"
ROOT_TRACK_SEQUENCES = ["AS_Unsh_Fall", "AS_Unsh_Fall_Loop", "AS_Unsh_Fall_W_Jump",
                        "AS_Unsh_TuJinHuiXuanWuTa", "AS_Unsh_Fall_Higher", "AS_Unsh_Fall_Junp",
                        # The back-vault montage is rebuilt at runtime from these
                        # four, so their extraction settings -- not the montage's --
                        # decide whether the Jump segment lifts the capsule at all.
                        # The binary name-presence shortcut is useless here: the
                        # property name is serialised for every sequence regardless
                        # of value (AS_Unsh_Fall contains it and is False).
                        "AS_Unsh_Jump_Back", "AS_Unsh_Jump_Over_Back",
                        "AS_Unsh_W_Jump_Back", "AS_Unsh_W_Jump_Over_Back",
                        # 前/左/右撑杆跳要复用的六条（2026-09-20）。这四条**起手段**
                        # 必须 `enable_root_motion = True` —— Jump 段就是靠它们自己的
                        # 蒙太奇 root motion 驱动的，而 ValidateActionDependencies 对
                        # Jump 是故意不校验的（注释写着 Jump "owns montage root motion"）。
                        # 两条**弧段**则相反，必须锁根轨道（LockedNotExtracted）。
                        "AS_Unsh_Jump_Forward", "AS_Unsh_Jump_Left", "AS_Unsh_Jump_Right",
                        "AS_Unsh_Jump_Over", "AS_Unsh_W_Jump_Forward", "AS_Unsh_W_Jump_Over"]

REPORT = {"sections": {}, "notes": []}


def note(message):
    REPORT["notes"].append(message)
    unreal.log("[probe] {}".format(message))


def section(name, fn):
    try:
        REPORT["sections"][name] = fn()
    except Exception as exc:  # noqa: BLE001 - probe must survive any API gap
        REPORT["sections"][name] = {"error": str(exc)}
        note("{} failed: {}".format(name, exc))
        unreal.log_error("[probe] {} failed:\n{}".format(name, traceback.format_exc()))


def prop(obj, name, default=None):
    """Read a snake_case editor property, returning default when absent."""
    try:
        return obj.get_editor_property(name)
    except Exception:  # noqa: BLE001
        return default


def load(path):
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        return None
    return unreal.EditorAssetLibrary.load_asset(path)


def load_cdo(blueprint_path):
    """Class default object of a Blueprint's generated class, or None."""
    if not unreal.EditorAssetLibrary.does_asset_exist(blueprint_path):
        return None
    bp = unreal.EditorAssetLibrary.load_asset(blueprint_path)
    cls = unreal.EditorAssetLibrary.load_blueprint_class(blueprint_path) if bp else None
    if cls is None:
        return None
    return unreal.get_default_object(cls)


def vector_dict(value):
    if value is None:
        return None
    try:
        return {"x": float(value.x), "y": float(value.y), "z": float(value.z)}
    except Exception:  # noqa: BLE001
        return repr(value)


# --------------------------------------------------------------------------
# Back-vault trajectory keys  (A8)
# --------------------------------------------------------------------------
def probe_back_vault():
    out = {}
    for path, label in ((GA_BACK_VAULT, "normal"),):
        cdo = load_cdo(path)
        if cdo is None:
            out[label] = {"error": "blueprint or class not found"}
            continue
        entry = {
            "class": cdo.get_class().get_name(),
            "attack_montage": None,
            "white_attack_montage": None,
        }
        for field in ("attack_montage", "white_attack_montage"):
            value = prop(cdo, field)
            entry[field] = value.get_path_name() if value else None

        for field, holder in (("back_jump_duration", "scalars"),
                              ("back_jump_over_duration", "scalars"),
                              ("white_back_jump_duration", "scalars"),
                              ("white_back_jump_over_duration", "scalars")):
            entry[field] = prop(cdo, field)

        for field in ("back_vault_trajectory", "white_back_vault_trajectory"):
            keys = prop(cdo, field)
            if keys is None:
                entry[field] = {"error": "property not readable"}
                continue
            samples = []
            for key in keys:
                samples.append({
                    "time": float(prop(key, "time", 0.0)),
                    "pos": vector_dict(prop(key, "normalized_position")),
                })
            summary = summarize_keys(samples)
            entry[field] = {"count": len(samples), "summary": summary,
                            "samples": samples}
        out[label] = entry
    return out


def summarize_keys(samples):
    """Where the recorded path peaks, in the key frame's own normalised space."""
    if not samples:
        return {}
    summary = {}
    for axis, name in ((0, "x"), (1, "y"), (2, "z")):
        values = [(s["time"], s["pos"][name]) for s in samples
                  if s["pos"] is not None]
        if not values:
            continue
        peak_t, peak_v = max(values, key=lambda p: p[1])
        trough_t, trough_v = min(values, key=lambda p: p[1])
        summary[name] = {
            "min": trough_v, "min_time": trough_t,
            "max": peak_v, "max_time": peak_t,
        }
    return summary


# --------------------------------------------------------------------------
# Montage structure and blend settings
# --------------------------------------------------------------------------
def probe_montage(name):
    path = "{}/{}".format(MONTAGE_DIR, name)
    montage = load(path)
    if montage is None:
        return {"error": "not found"}
    entry = {
        "play_length": float(montage.get_play_length()),
        "enable_auto_blend_out": prop(montage, "enable_auto_blend_out"),
        "blend_in": prop(montage, "blend_in", None),
        "blend_out": prop(montage, "blend_out", None),
        "sections": [],
        "slots": [],
    }
    try:
        for section in montage.get_editor_property("composite_sections"):
            entry["sections"].append({
                "name": str(section.get_editor_property("section_name")),
                "start_time": float(section.get_editor_property("start_time")),
                "next_section": str(section.get_editor_property("next_section_name")),
            })
    except Exception as exc:  # noqa: BLE001
        entry["sections"] = {"error": str(exc)}
    try:
        for track in montage.get_editor_property("slot_anim_tracks"):
            entry["slots"].append(str(track.get_editor_property("slot_name")))
    except Exception as exc:  # noqa: BLE001
        entry["slots"] = {"error": str(exc)}
    return entry


# --------------------------------------------------------------------------
# Root bone track of the fall clips  (A9)
# --------------------------------------------------------------------------
def probe_sequence_root(seq_name):
    path = "{}/{}".format(SEQ_DIR, seq_name)
    seq = load(path)
    if seq is None:
        return {"error": "not found"}
    force_lock = prop(seq, "force_root_lock")
    enable_rm = prop(seq, "enable_root_motion")
    entry = {
        "play_length": float(seq.get_play_length()),
        # A non-1.0 rate scale shortens the *effective* duration, which is why a
        # 3.233 s sequence can become a 1.617 s dynamic montage.
        "rate_scale": prop(seq, "rate_scale"),
        "enable_root_motion": enable_rm,
        "force_root_lock": force_lock,
        "root_motion_root_lock": str(prop(seq, "root_motion_root_lock")),
        # `UMHGZBackVaultAbility::ValidateActionDependencies` 判的就是这个 policy：
        # JumpOver clip 必须是 LockedNotExtracted（= ForceRootLock 且未提取）。
        # 直接报出来，省得下次还要手推 ResolveRootTrackPolicy。
        "root_track_policy": (
            "Conflicting" if (force_lock and enable_rm)
            else "LockedNotExtracted" if force_lock
            else "Extracted" if enable_rm
            else "Unaccounted"),
    }

    # --- 根轨道读数：**必须用 `get_bone_pose_for_time`** ---
    # ⚠ 三条踩过的坑，别再走回头路：
    #  ① 原先走 `seq.data_model`，实测**恒为 None**（5.6 的绑定里要用
    #     `seq.data_model_interface`），于是下面整段从没跑过。
    #  ② `AnimationBlueprintLibrary.GetRawTrack*` 那族从 5.2 起**已废弃且是空实现**
    #     （头文件里函数体就是 `{}`），不能用。
    #  ③ **`extract_root_track_transform` 会骗人** —— 它对 AS_Unsh_Jump_Forward/Back
    #     给出正确值，却对 AS_Unsh_Jump_Left/Right 恒返回 0，于是把「向左/向右起跳
    #     没有 root 位移」这个假结论喂给了我。同一次运行里用 `get_bone_pose_for_time`
    #     复测，四条起手段的相对误差是 −0.8% / −0.4% / +0.3% / −1.2%（对 MHR 实测
    #     161/173/220/303 cm）—— 它才是可信的那个。
    # `get_bone_pose_for_time` 虽然标了废弃，但 .cpp 里是**真实现**，且不需要组件。
    lib = getattr(unreal, "AnimationLibrary", None)
    if lib is not None:
        try:
            play_length = float(seq.get_play_length())
            samples = {}
            for label, t in (("t0", 0.0), ("t1", max(play_length - 1e-4, 0.0))):
                pose = lib.get_bone_pose_for_time(
                    seq, unreal.Name("root"), t, False)
                try:
                    yaw = round(math.degrees(pose.rotation.rotator().yaw), 3)
                except Exception:  # noqa: BLE001
                    yaw = None
                samples[label] = (pose.translation.x, pose.translation.y,
                                  pose.translation.z, yaw)
            (x0, y0, z0, yaw0), (x1, y1, z1, yaw1) = samples["t0"], samples["t1"]
            entry["root_transform"] = {
                "t0": [round(x0, 3), round(y0, 3), round(z0, 3), yaw0],
                "t1": [round(x1, 3), round(y1, 3), round(z1, 3), yaw1],
                "net_x_cm": round(x1 - x0, 3),
                "net_y_cm": round(y1 - y0, 3),
                "net_z_cm": round(z1 - z0, 3),
                "net_planar_cm": round(math.hypot(x1 - x0, y1 - y0), 3),
                # 走向 = 根骨局部 `atan2(x, y)`，**朝左为正**。
                #
                # ⚠ **这句话的作用域仅限于「它与 撑杆跳曲线.md 的那一列怎么比」**
                # （141 +6.44 vs +6.5、144 +84.59 vs +84.8、145 −78.93 vs −79.3、
                # 155 +3.32 vs +3.3，同号且两位小数级吻合 ⇒ 两者**是同一个约定**，
                # 相互比对时不要取反）。**它不是「UE 侧也不用取负」的授权** ——
                # 这个字段本身没有任何 UE 消费方（只写进 m5_assets.json）。
                #
                # 标定锚点（不依赖招式命名）：后撑杆跳的 Jump 段 PIE 已确认送**后**，
                # 而 `AS_Unsh_Jump_Back` 的局部净位移是 (−13.2, −156.1) ⇒ 局部 `+Y` =
                # 角色的前；`AS_Unsh_Jump_Left` 的 (+171.6, +16.3) 沿角色的**左**走
                # 172.4 cm（MHR 实测 173 吻合）⇒ 局部 `+X` = 角色的左。所以这里的
                # 「朝左为正」是**测出来的**，与文档列一致是真的一致。
                #
                # UE 侧要取负的原因在别处且只有一条：UE 的 yaw「朝右为正」。
                # 见 `UMHGZPoleVaultAbility::ComputeDirectionSnapshot`。
                "heading_deg": round(math.degrees(math.atan2(x1 - x0, y1 - y0)), 2),
                "yaw_span_deg": (None if yaw0 is None or yaw1 is None
                                 else round(yaw1 - yaw0, 3)),
            }
        except Exception as exc:  # noqa: BLE001
            entry["root_transform"] = {"error": "{}: {}".format(type(exc).__name__, exc)}
    else:
        entry["root_transform"] = {"error": "unreal.AnimationLibrary 不存在"}

    model = prop(seq, "data_model_interface")
    if model is None:
        entry["data_model_interface"] = None
        entry["sequence_api"] = sorted(
            n for n in dir(seq) if not n.startswith("_"))[:80]
        return entry
    entry["data_model_accessor"] = "data_model_interface"

    entry["num_frames"] = prop(model, "number_of_frames")
    entry["data_model_api"] = sorted(
        n for n in dir(model) if not n.startswith("_"))[:80]

    root_name = None
    try:
        names = model.get_bone_track_names()
        entry["bone_track_count"] = len(names)
        if names:
            # The root is the first track in the skeleton's track order.
            root_name = str(names[0])
            entry["root_bone"] = root_name
            entry["first_tracks"] = [str(n) for n in list(names)[:4]]
    except Exception as exc:  # noqa: BLE001
        entry["bone_track_names"] = {"error": str(exc)}
        return entry

    if root_name is None:
        return entry
    try:
        # `get_bone_track` 在这个绑定里不存在，叫 `get_bone_track_by_name`。
        track = model.get_bone_track_by_name(root_name)
        positions = track.get_editor_property("pos_keys") if track else None
        if positions is None:
            entry["root_positions"] = {"error": "pos_keys not readable"}
            return entry
        values = [(float(p.time), float(p.x), float(p.y), float(p.z))
                  for p in positions]
        entry["root_position_key_count"] = len(values)
        if values:
            first, last = values[0], values[-1]
            entry["root_positions"] = {
                "x": {"min": min(v[1] for v in values), "max": max(v[1] for v in values)},
                "y": {"min": min(v[2] for v in values), "max": max(v[2] for v in values)},
                "z": {"min": min(v[3] for v in values), "max": max(v[3] for v in values)},
                "first": first, "last": last,
                "max_abs_xy": max(max(abs(v[1]), abs(v[2])) for v in values),
            }
            # 根轨道的**净位移**。骨骼轨道单位就是 cm，可以直接和 MHR 侧的实测
            # 起手段位移（161 / 173 / 220 / 303 cm，见 撑杆跳曲线.md §一）对账 ——
            # 文件名对不代表内容对，这一条抓的是「导错 clip / 错行导入」。
            dx, dy, dz = last[1] - first[1], last[2] - first[2], last[3] - first[3]
            entry["root_net_displacement_cm"] = {
                "x": round(dx, 3), "y": round(dy, 3), "z": round(dz, 3),
                "planar": round(math.hypot(dx, dy), 3),
            }
    except Exception as exc:  # noqa: BLE001
        entry["root_positions"] = {"error": str(exc)}

    # 根轨道的**旋转**。若 root 带 yaw，Jump 段的蒙太奇 root motion 会让角色转向，
    # 那么按「起跳朝向 + 弦向角」算的 DirectionSnapshot 基准就全错。
    try:
        track = model.get_bone_track_by_name(root_name)
        rotations = track.get_editor_property("rot_keys") if track else None
        if rotations:
            yaws = [math.degrees(math.atan2(
                2.0 * (float(q.w) * float(q.z) + float(q.x) * float(q.y)),
                1.0 - 2.0 * (float(q.y) ** 2 + float(q.z) ** 2))) for q in rotations]
            base = yaws[0]
            worst = max(abs(((y - base + 180.0) % 360.0) - 180.0) for y in yaws)
            entry["root_rotation_key_count"] = len(yaws)
            entry["root_rotation_yaw_deg"] = {
                "first": round(base, 3), "last": round(yaws[-1], 3),
                "max_deviation_from_first": round(worst, 3),
            }
    except Exception as exc:  # noqa: BLE001
        entry["root_rotation_yaw_deg"] = {"error": str(exc)}
    return entry


# --------------------------------------------------------------------------
# DataAssets
# --------------------------------------------------------------------------
def probe_combat_config():
    asset = load(DA_COMBAT)
    if asset is None:
        return {"error": "not found"}
    fields = ["dance_vault_ballistic_mode", "dance_vault_apex_height",
              "dance_vault_duration", "dance_vault_distance",
              "dance_vault_launch_velocity",
              "back_vault_duration", "back_vault_distance", "back_vault_apex_height",
              "white_back_vault_duration", "white_back_vault_distance",
              "white_back_vault_apex_height", "back_vault_free_fall_handoff_progress",
              "white_back_vault_free_fall_handoff_progress",
              "max_dance_stacks", "dance_damage_multipliers",
              "aerial_fall_gravity_scale", "white_aerial_fall_gravity_scale",
              "aerial_fall_braking_deceleration"]
    return {f: normalise(prop(asset, f, "<absent>")) for f in fields}


def normalise(value):
    if value is None or isinstance(value, (bool, int, float, str)):
        return value
    try:
        return [normalise(v) for v in value]
    except TypeError:
        return vector_dict(value) or repr(value)


def probe_serialized_props():
    """Which properties this DataAsset actually serialises (vs class default)."""
    asset = load(DA_COMBAT)
    if asset is None:
        return {"error": "not found"}
    try:
        export = unreal.EditorAssetLibrary.find_asset_data(DA_COMBAT)
        return {"package": export.package_name if export else None}
    except Exception as exc:  # noqa: BLE001
        return {"error": str(exc)}


def probe_combo():
    asset = load(DA_COMBO)
    if asset is None:
        return {"error": "not found"}
    out = {"fields": [n for n in dir(asset) if not n.startswith("_")][:60]}
    for field in ("transitions", "edges", "entries"):
        value = prop(asset, field, "<absent>")
        if value != "<absent>":
            try:
                out[field] = [describe_row(r, COMBO_FIELDS) for r in value]
            except Exception as exc:  # noqa: BLE001
                out[field] = {"error": str(exc)}
    return out


# `FComboTransition` 的**全部**字段（逐字对照 MHGZWeaponComboData.h）。
#
# 加这一份是因为照抄一条连招边时必须**逐字段**复制：漏掉 direction / priority /
# bMatchAnyState / blocked_source_states / bRequiresDodgeAcceptWindow 里的任何一个，
# 新边都会以看不出差别的方式匹配错（比如少了 blocked_source_states，any-state 边
# 就会在它本该让开的来源状态里也生效，抢掉方向边）。
COMBO_FIELDS = [
    "transition_id", "source_state", "b_match_any_state", "blocked_source_states",
    "input_tag", "direction", "execution_policy", "ability_class",
    "montage_blend_in_time", "max_correction_angle", "target_state", "state_policy",
    "landing_policy", "required_tags", "blocked_tags", "stamina_required",
    # ⚠ 布尔字段的 Python 名**去掉了前导 b**：`bMatchAnyState` → `match_any_state`。
    # 写成 `b_match_any_state` 会静默得到 `<absent>`，看着像「这条边没这个字段」。
    "match_any_state", "requires_combo_window", "requires_dodge_accept_window",
    "grant_timing", "granted_tags", "auto_transition", "priority",
]

INPUT_FIELDS = ["source_state", "target_state", "transition_id", "activation_ability",
                "landing_policy", "auto_transition", "required_tags", "blocked_tags"]


def describe_row(row, fields=None):
    """列出字段值。`fields` 默认给输入档用；连招边传 `COMBO_FIELDS`。

    只列**存在**的字段名，其余不动 —— 共用一份 descriptor 时把 combo 的 22 个字段
    全打给输入档，会得到一屏 `<absent>`，把真正的差异淹掉。
    """
    names = fields or INPUT_FIELDS
    out = {}
    for name in names:
        value = prop(row, name, "<absent>")
        if value == "<absent>":
            continue
        if isinstance(value, (list, tuple)):
            # `repr()` 只会给一个 Array 对象，看不出内容；逐项 repr 才有用。
            out[name] = ["{}".format(v) for v in value]
        elif isinstance(value, (bool, int, float, str)):
            out[name] = value
        else:
            out[name] = repr(value)[:200]
    return out


def probe_input_profile():
    asset = load(DA_INPUT)
    if asset is None:
        return {"error": "not found"}
    out = {}
    for field in ("chords", "entries", "bindings"):
        value = prop(asset, field, "<absent>")
        if value != "<absent>":
            try:
                out[field] = [describe_row(r) for r in value]
            except Exception as exc:  # noqa: BLE001
                out[field] = {"error": str(exc)}
    if not out:
        out["fields"] = [n for n in dir(asset) if not n.startswith("_")][:60]
    return out


def probe_counter_ability():
    cdo = load_cdo(GA_COUNTER)
    if cdo is None:
        return {"error": "not found"}
    fields = ["dance_vault_sequence", "dance_vault_animation_play_rate",
              "dance_vault_blend_in_time", "dance_vault_blend_out_time",
              "dance_vault_montage_slot"]
    return {f: repr(prop(cdo, f, "<absent>"))[:200] for f in fields}


def main():
    os.makedirs(OUT_DIR, exist_ok=True)

    section("back_vault_ability", probe_back_vault)
    section("counter_ability", probe_counter_ability)
    section("combat_config", probe_combat_config)
    section("combo", probe_combo)
    section("input_profile", probe_input_profile)

    montages = {}
    for name in AERIAL_MONTAGES:
        section("montage:{}".format(name), lambda n=name: probe_montage(n))
        montages[name] = REPORT["sections"]["montage:{}".format(name)]

    for name in ROOT_TRACK_SEQUENCES:
        section("sequence:{}".format(name), lambda n=name: probe_sequence_root(n))

    with open(OUT_JSON, "w", encoding="utf-8") as handle:
        json.dump(REPORT, handle, indent=2, ensure_ascii=False, default=repr)

    unreal.log("[probe] wrote {}".format(OUT_JSON))
    print("PROBE_DONE {}".format(OUT_JSON))


main()
