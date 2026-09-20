# M5「空中/舞踏/终结」：接缝修复 + 收尾计划

## Context

项目是 UE 5.6 + GAS 的原创虫棍单机 Demo，走 M0～M7 / E0～E7 的里程碑门禁。每个阶段必须同时有代码、资产接线、PIE 与遥测证据才算签核。

**当前真实位置：M4.7 已签核，M5 实施中。接缝部分（原阶段 A）已全部收敛，一项都没签核。**

用户以 Monster Hunter Rise 实机录制为**唯一真值源**（`C:/apps/steam/.../reframework/data/MHGZ_AerialTrajectoryRecorder`）。

> **本文档 2026-09-18 重排为「已完成 / 未完成」两段。** 此前它按时间顺序堆积，`5.0`/`5.0b`/`5.0c`/`5.0d` 交错（`5.0d` 排在 `5.0c` 之前），已关闭项与未开工项混在一起。
> **重排只动位置，不动证据** —— 结论、实测数字、教训、被推翻的推断一条未删。真值数字仍以 §3 与 `docs/reference/真值表.md` 为准。

### 三批证据的可信度（冲突时以此为准）

| 批次 | 来源 | 可信度 |
|---|---|---|
| F1～F5（§2 已并入各条） | 纯源码阅读 | **未验证**，只作线索 |
| M1～M6（早期 PIE 录制） | `Saved/RuntimeTelemetry/` | 实测，但部分窗口口径已被后批修正 |
| **Rise 真值（§3）** | `MHGZ_AerialTrajectoryRecorder` 实机录制 | **唯一真值源** |

**前两批已经各被推翻过至少一条，不要再把源码推断当已验证事实。** 附录 A 列了全部已推翻项。

### 测试基线

| 日期 | 改动集 | `MHGZ` 全量 | `M5` | 失败 |
|---|---|---|---|---|
| 2026-09-15 | 6 个 C++、零资产 | 91 / 90 通过 / 1 失败 | 3/3 | `PoseSearchControlNotifies` |
| 2026-09-17（A7） | `InsectGlaiveCombatConfig.h/.cpp`、`MHGZAdvancingCounterAbility.cpp` | 91 / 90 / 1 | 3/3 | 同一个 |
| 2026-09-17（A7+B2） | ＋`MHGZAdvancingCounterAbility.h`、`MHGZWeaponRuntimeHostComponent.h/.cpp`、`MHGZM5MovementTests.cpp` | 93 / 92 / 1 | 5/5 | 同一个 |
| 2026-09-17（白灯常量+基类+遥测+舞踏资产） | ＋`MHGZBackVaultAbility.*`、`MHGZInsectGlaiveAbility.*`、`MHGZMotionMatchingAnimInstance.*`、`MHGZWuTaMontageSetupCommandlet.*` | 95 / 94 / 1 | 5/5 | 同一个 |
| 2026-09-18（架构重做＋出招蒙太奇禁根运动） | ＋`AnimNotify_IG_AerialHandoff.*`、`MHGZAttackAbility.*`、`AbilityTask_MHGZWeaponMovement.*`、`MHGZM5AerialHandoffTests.cpp`、`MHGZAerialHandoffSetupCommandlet.*` | **98 / 97 / 1** | 6/6 | 同一个 |

**六次都是同一个失败项，逐项未变** —— `MHGZ.PMM.Assets.PoseSearchControlNotifies`，无武装 locomotion 序列把 Notify 挂在 `PoseSearchBlock` / `PoseSearchCostBias` 轨上。**是既存资产问题，与 M5 无关；全量套件在任何改动之前就不是绿的**，所以 `milestone-gates.md` 里的 54/54 是过期数字。

**`DA_IG_Combat` 从头到尾没被修改过**（`git hash-object` 始终等于 HEAD blob）。所有配置值走的都是 `InsectGlaiveCombatConfig.h` 的**类默认值**。

命令（Git Bash 里 uproject 必须传**绝对路径**，否则 `Failed to open descriptor file`；不加 `-stdout` 没有输出）：

```
UnrealEditor-Cmd.exe <绝对路径>/MHGZ.uproject -unattended -nop4 -nosplash -NullRHI -stdout \
  -ExecCmds="Automation RunTests MHGZ;Quit" -log
```

---

# 第一部分：已完成

## 1. 已签核

| 项 | 内容 |
|---|---|
| **M4.7** | 地面虫印、四连印斩、突进回旋斩及其精确窗口；反击消费 `IncomingHit` 并加一层 `AdvancingCounter` 舞踏（`docs/editor/verification.md:36`、`docs/design/milestone-gates.md:78`） |
| **E5.1** | 木桩三色 Hitzone |
| **自动化（形态）** | 1 个：`MHGZ.M5.Movement.OwnershipAndCleanup`。它只驱动 Host 的所有权 API，**从未实例化过 Task 本体** —— 这个空缺仍未补（见 §7） |

## 2. 已修 **且** PIE 已验证

| 项 | 症状 | 根因与修法 | 证据 |
|---|---|---|---|
| **A1** | 下坠接缝卡顿 | 交棒速度取自已被塌缩的 `CMC->Velocity`。改用 `ComputeTrajectoryTangent` 解析末切线 | PIE 验证；M1 实测 12 次里 10 次掉 63%~81% |
| **A4a** | 舞踏落地表现时有时无 | 根因**不是 `IsFalling()` 竞态**，是 `Landed ≠ Completed`：`ProcessLanded` 先广播 `LandedDelegate`、之后才 `SetPostLandedPhysics`，而旧表达式第一个子句**确定性为 false 并短路**（11/11）。修法：`ResolveVaultExit` 分类器 | 已修（B2），2026-09-17 上移到基类并接上后撑杆跳 |
| **A5** | 淡出期 `RootMotionDisabled` 被重开 | 调换 Pop/Stop 次序 | 数据坐实 |
| **A7** | 舞踏升到一半掉下来 | 弹跳物理配成 3.5 m / 0.6 s，真值 564 cm / 1.6167 s。**动画对、物理错**。改类默认值 `564 / 1.6167 / 80` | **8/8 精确复现 564.0 cm** |
| **A8** | 后撑杆跳顶点低 26% | 曲线 Z 是**相对于 `StartProgress` 处高度**的偏移，不是绝对高度；`ApexHeight` 是缩放系数而非顶点。根因是**一个数被两用**（`BackVaultDuration` 同时当归一化域与窗口时长，漏掉下坠段）。解耦为 `ArcDuration` | 已修至 **−2.2%** |
| **A9** | 坠落中模型往右移动一下 | `PushDisableRootMotion()` 只门控**提取**、不门控**姿势**；`AS_Unsh_Jump_Over_Back` 两个标志都为假 → 26.7 cm 偏移留在姿势里，换坠落正片时 2 帧弹回。修法 `bForceRootLock = True`（**不**开提取 —— 开了会被 `HasAnimRootMotion()` 无条件抢占），落在 `Scripts/AssetProbe/fix_a9_root_lock.py:53`（对 `AS_Unsh_Jump_Over_Back` / `AS_Unsh_W_Jump_Over_Back`） | **`RootBoneRel` 全程 0.000** |
| **加速度遥测** | `Spatial.csv` 的 `AccelerationX/Y/Z` 恒为 0 | **结构性为零**：采的是 `CMC->GetCurrentAcceleration()`，而该值只由 `AddMovementInput`/`ConsumeInputVector` 写入，本项目**零调用**。改成速度有限差分 | 已修 |
| **碰撞 ×2** | 见下 | 见下 | 2026-09-18 PIE |
| **架构重做** | 反击舞踏完全无法起跳 | 见 §2.1 | 2026-09-18 PIE，22/22 |
| **出招蒙太奇抢根运动** | 舞踏起跳冻结一整个混入时长 | 见 §2.2 | 2026-09-18 PIE，34/34 |
| **舞踏蒙太奇登记缺口** | 舞踏的蒙太奇从来无法使用任何 notify | `StartAdvancingCounterVaultVisual` 用引擎原生 `UAbilityTask_PlayMontageAndWait`，**从未调 `RegisterMontageInstance`** —— 而项目里每个需要 notify 的能力都调（`MHGZBackVaultAbility.cpp:412`、`MHGZAttackAbility.cpp:415`、`MHGZDodgeAbility.cpp:262`、`MHGZInsectGlaiveKinsectAbilities.cpp:206/351/550`、`MHGZSheatheAbility.cpp:239`）。后果是 `MHGZ::AnimNotify::ResolveAction` 解析不出 ActionToken。实测坐实：窗口 tag 在后撑杆跳 9/9 出现、在舞踏 9/9 全无。已照 `MHGZBackVaultAbility.cpp:410-414` 补上 | 已修 |

### 2.1 架构重做：Flying → 点通知 → Falling（2026-09-18，**已 PIE 验证**）

**触发**：反击舞踏**完全无法起跳**。插桩 PIE 把因果链钉死了：

```
请求参数正确（apex 564 / dur 1.6167 / dist 80 / ApexHeightAndDuration）
源算出的力正确（force-Z = 1373.75 = 4×564/1.6167 减一帧重力）
但 Velocity.Z = −24.6（纯重力）、Velocity.XY = 上一招余速 ×0.66/帧
⇒ 力从没进 Velocity
根因：apply 那一刻 falling=1 —— 胶囊已是 MOVE_Falling，SetMovementMode 是 no-op，
      CMC 立刻找到脚下地面 → ProcessLanded → LandedDelegate → 任务 3 帧后收尾
```

**新架构**（`vault` = 代码对"一段由根运动源驱动的空中位移"的统称；舞踏 = `BallisticVault`，后撑杆跳 = `CurvedVault`）：

| 项 | 改动 |
|---|---|
| **模式** | 起飞进 **`MOVE_Flying`** —— `PhysFlying` 不跑重力 / `FindFloor` / `ProcessLanded`，override 源活跃时 `CalcVelocity` 也被跳过（无 `MaxFlySpeed` 钳制）。**这是后撑杆跳一直在用的手法** |
| **通知** | `UAnimNotifyState_IG_AerialWindow`（窗口）→ **`UAnimNotify_IG_AerialHandoff`（点）**。最早可操作帧是一个**帧**；窗口语义（begin/end 配对、窗口期持 tag、按 eventID 记账的 token 表）全部只服务于"窗口还开着吗"这个没人问的问题 |
| **释放** | `NotifyAerialHandoff()`：锁存 + 切 `MOVE_Falling` + 拿 `Combat.State.Aerial.Actionable`。**动画继续播完**；"空中 idle"只是比喻，切模式只为让落地动画与空中 GA 可触发 |
| **兜底** | `EnsureVaultFlightReleased()` —— 通知没走到时（蒙太奇没挂通知 / 释放点前被取消）不让胶囊**悬停**在 Flying。从 `FinishMovement` 与 `OnDestroy` 两条路各调一次 |
| **收尾** | `ResolveVaultExit` **删掉第二个参数**：`Landed → 落地姿势`，`Completed/BlockingHit → 自由落体`，其余 `None`。两道落地闸门删除 |
| **真值来源** | 唯一要问的是"弧走完时胶囊在半空吗"，UE 的直接回答是 **`LandedDelegate` 有没有响过**。旧代码拿"CMC 恰好处于什么模式"当代理指标，一直是错的（`ProcessLanded` 先广播再 `SetPostLandedPhysics`） |
| **tag** | `Combat.State.Aerial.ActionWindow` → **`Combat.State.Aerial.Actionable`**（acquire 在通知、release 在 `EndAbility`） |
| **碰撞** | 删掉 `EMovementCollisionPolicy`；`HandleCapsuleHit` 不再在胶囊碰撞上结束移动 —— **滑动是引擎自带的**（`PhysFlying`/`PhysFalling` 在任何 blocking hit 上都 `HandleImpact` + `SlideAlongSurface`，而源自己从不移动组件）。`ResolveVaultExit` 的 `BlockingHit` 从 `None` 改为按空中/地面分流 —— **"被打断"不再等于"身体无主"** |
| **下落** | `IsFalling()` 换成解析残余量 `(1 − HandoffProgress) × ArcDuration`（白灯 0.0 / 非白灯 0.2347 s）—— 修掉白灯的僵尸 `AM_IG_AerialFall_W`。另加坠落看门狗 `AerialFallMaxSeconds = 4.0`（`InsectGlaiveCombatConfig.h:268`，读点在 `MHGZWeaponRuntimeHostComponent.cpp:1005`）：坠落此前**没有任何生命周期管理**，实测会静默无限循环、掉到 Z = −1939 cm 仍在下降 |
| **清理** | 删掉 `BackVaultFreeFallHandoffProgress` / `WhiteBackVaultFreeFallHandoffProgress` —— 从不被读、且会撒谎（文件写 0.8734，运行时白灯是 1.0） |

**PIE 实测（2026-09-18，`20260918-205813`，22 次舞踏）**：

| | 修前 | 修后 |
|---|---|---|
| 起跳冻结 | 0.150 s | **0.025 s（一帧）** |
| 首帧 `VZ` | 0.0 | **1330.6** |
| 源存活 | 0.150 s | **1.575 s** |
| 顶点 | 640.9 / 346.5 / 54.6 乱跳 | **563.9~564.0**，22 次极差 **1.4 cm** |

**后撑杆跳复测**（对抗"这次动了它的模式时序"这条风险）：

| 招式 | 项 | Rise 真值 | 本轮 UE | 偏差 |
|---|---|---|---|---|
| 白灯后撑杆跳 | 顶点 / 水平 / 窗口 | 750.3 / 712.6 / 2.141 | 748.9 / 703.3 / 2.175~2.202 | **−0.2% / −1.3% / +2.1%** |
| 无白灯后撑杆跳 | 顶点 | 580.5 | 567.5~568.0 | −2.2%（＝ A8 已记录） |
| 舞踏 | 顶点 / 时长 | 564 / 1.617 | 563.9~564.0 / 1.625 | ≤ +0.5% |

**顺带证掉一个担心**：两个模式的积分器确实不同（`PhysFlying:4350` 欧拉单步、`PhysFalling:4909` 中点法＋子步循环），但后撑杆跳跨 `5→3` 那一帧 Z 差值是 31.20 → 31.65 → 30.27、`VZ` 连续 —— **换模式在真实弧上测不出影响**，故不统一（统一需要换源码版引擎，而本机是 Launcher InstalledBuild）。

**顺带量到的**：43 个"弧 → 空中回避 `137`"边界显示 **`137` 不继承惯性**（`147` 自己散在 3.1–12.1 m/s，`137` 仍稳定 ~13）—— MHR 的空中位移是**速度设定式**而非位置播放式。项目现在"播放录下来的位置曲线"对固定轨迹等价，但表达不了惯性语义、超过弧尾的下落、任意起始状态复用。**那是另一件事，本轮不做。**

**实测的释放点**（新录 5 份 `mhrise_20260918_00*`，取被取消段的最短）：

| 招式 | 被取消段 | 最短 | 落到的蒙太奇时间 |
|---|---|---|---|
| 舞踏 | `154` | 0.316 s | `AM_IG_WuTa` **0.316** |
| 无白灯 后撑杆跳 | `147` | 0.133 s | `AM_IG_HouChengGanTiao` **0.783** |
| 白灯 后撑杆跳 | `159` | 0.125 s | `AM_IG_HouChengGanTiao_W` **0.775** |

后撑杆跳那个 0.650 从来不是测量值，命列源码里自己写着 `"PLACEHOLDER: Jump->JumpOver section boundary, not a measurement"`。

> **2026-09-18 复核（`docs/reference/真值表.md` 全量重算）**：三条分别复算为 **0.316 / 0.785 / 0.776** —— 后两条与已写入蒙太奇的值差 **≤2 ms**（120 Hz 下一帧是 8.3 ms，即不足一帧）。差在跳段常量：脚本测到 `146`=0.652 / `158`=0.651，写入时用的是 0.651/0.650。**不改**，但记下这个量级。

### 2.2 出招蒙太奇的根运动在混入窗口里吃掉了 vault 的根运动源（2026-09-18，**已 PIE 验证**）

**症状**：舞踏起跳后冻结整整一个混入时长（`AM_IG_WuTa` 的 `BlendIn = 0.15 s`），`srcForceZ` 全程正确却进不了 `Velocity`。

**根因链**（定位到具体引擎行）：

```
AM_IG_TuJinHuiXuan（MontageHasRootMotion=1, RootMotionDisabled=0）正在淡出
  → UAnimInstance::Montage_PlayInternal(AnimInstance.cpp:2412-2426) 只在
    「新蒙太奇自己带根运动」时才 Stop 上一个根运动实例 —— AM_IG_WuTa 没有根运动，
    那条分支根本不进，RootMotionMontageInstance 仍指着旧的那条
  → TickCharacterPose 里 ConsumeRootMotion() 仍有非零根运动
  → CMC->RootMotionParams.bHasRootMotion = true
  → ApplyRootMotionToVelocity(CharacterMovementComponent.cpp:4410) 走动画分支并提前 return
  → 本帧 override 源从未被应用
淡出结束(0.15s) → 无可提取 → 源立即生效
```

**修法**：`MHGZAttackAbility::SuspendAttackMontageForFollowup` 里对**正在淡出的** `ActiveAttackMontage` 实例 `PushDisableRootMotion()`，命中 `AnimInstance.cpp:1952` 的 `!MontageInstance->IsRootMotionDisabled()`。**只停提取、不动混合**，所以 0.15 的混入保住。不需要配对 `Pop`：`bMontageSuspendedForFollowup` 全项目没有恢复路径。

**一个曾经误导过的读数（值得记住）**：`CMC->HasAnimRootMotion()` 读的是 `CMC->RootMotionParams.bHasRootMotion`（`CharacterMovementComponent.h:2771-2776`），引擎注释明写 **"Not valid outside of the scope of that function"** —— 从能力任务 tick 里读它**永远是 0**，不构成证据。同一帧里 `IsPlayingRootMotion()`=1 与 `HasAnimRootMotion()`=0 **并不矛盾**，它们读的不是同一个东西。

## 3. 已改判 / 已关闭（**前提被否定，不需要修**）

| 项 | 原症状 | 结论 |
|---|---|---|
| **A3** | 交棒姿势跳变 | **被数据否定**（f6096 是干净的 0.5/0.5 交叉淡入） |
| **A4b** | 落地期无动作门禁 | **前提被否定**：`Aerial.CantAttack`/`CantDodge` 的 DevComment 是「空中攻击**已用**」「空中回避**已用**」—— 它们是**动作预算标记**，不是「落地期间锁输入」。且全部录制里 **119 次独立落地，0 次被动作打断**。改判为代码整洁项 |
| **A2** | 边界墙钟与动画段脱钩 | 实测源起于 montage time **0.6758~0.7249**，而 `BackJumpDuration = 0.650`，偏差 **1~3 帧**、未造成症状。**只加校验不重构**（激活时与 `GetSectionEndTime` 对表）→ 降级为收口项，见 §6 |

**要克制的一条**：A3 这类"代码看着不对但数据说没事"的项，**不要顺手修**。对话前段正是栽在这个模式上 —— 把代码结构推断当成已验证事实，并据此排错优先级。

## 4. 已完成的基础设施（不是 bug 修复）

| 项 | 内容 |
|---|---|
| **阶段 0 资产探针** | `Scripts/AssetProbe/probe_m5_assets.py`（只读，写 JSON 到 `Saved/AssetProbe/`）。**Python 路线读不到骨骼轨道**（`UAnimSequence` 在 5.6 不暴露 data model），需要 DataModel 时必须走 C++ Commandlet |
| **阶段 C 后撑杆跳设计文档补录** | 已完成 |
| **遥测** | `Saved/RuntimeTelemetry/` 下的成套 CSV 覆盖 montage / RootMotionSource / MovementMode / 骨骼三个几何层次，2026-09-17 又补上加速度 |
| **唯一位移执行层** | `UAbilityTask_MHGZWeaponMovement`（四种 `EWeaponMovementMode`：`BoundedDirectional` / `BallisticVault` / `CurvedVault` / `AdditiveInertia`）。已接线：`BallisticVault`（突进回旋斩反击）、`CurvedVault`（后撑杆跳）。**其余两种是按合同预置的基础设施，不是死代码** —— 正好对应未开工的操虫斩与强化跳跃斩/急袭突刺 |
| **后撑杆跳 / 舞踏 实现** | `UMHGZBackVaultAbility` + `AM_IG_HouChengGanTiao(_W)`、`AM_IG_WuTa`。**后撑杆跳曾是无主实现**（F3），设计文档补录后才纳入 M5 |
| **Rise 真值管线（2026-09-18 新增）** | `docs/reference/`（真值表 + 招式表 + 口径）+ `Scripts/MHRise/build_truth_table.py`（生成器）。逐帧轨迹写到 `Saved/_mhr_frames/`（不入库）。详见 §3 |
| **命令列** | `MHGZAerialHandoffSetupCommandlet`（写点通知，报告 `Saved/_aerial_handoff.json`，三条 `validated: true`）、`MHGZWuTaMontageSetupCommandlet`。<br>**⚠ 别"顺手清理" `MHGZAerialHandoffSetupCommandlet.cpp:26` 的 `RetiredStateClassName = TEXT("AnimNotifyState_IG_AerialWindow")`** —— 旧类删掉之后，清理仍然靠**类名字符串**匹配（而不是 `Cast`），这行正是"幂等重跑仍能清掉旧通知"的实现方式，删了它旧蒙太奇就再也清不干净 |
| **怪物碰撞：不可站立** | `MonsterBody` 早就在用（`AMHGZMonsterBase` 构造函数）；「不可站立」不是碰撞预设能表达的 —— `FWalkableSlopeOverride` 是**组件属性**，`FCollisionProfileName` 里没有它，所以对胶囊写 `WalkableSlope_Unwalkable` |

> **订正一条我自己的错误结论。** 我曾据一次 grep 说「`MonsterBody` 在全树 `Source/` 零命中、只在蓝图里指派」。
> **那是错的** —— 那个 grep 被 `head -20` 截断，`MHGZMonsterHitzoneComponent` 里的 `MonsterHitzone` 子串把输出占满了，`MHGZMonsterBase.cpp` 排在字母序后面被截掉。
> **教训：`head` 截断过的 grep 不能用来下「零命中」这种全称判断。**

## 5. 已确认的决策

1. **先修接缝**（理由：M5 剩余五个招式**每一个**都以「进入 Falling」或「落地」收尾，接缝是所有空中动作的公共基底；缺陷已渗到已签核路径上；现在只有一个招式、变量最少）。项目自己的冻结规则禁止批量前提（`demo-implementation-plan.md:767`），而 M5 退出条件本身就是「任一取消路径都只剩一个 CMC 移动所有者且不存在残留 WarpTarget」。
2. **后撑杆跳正式纳入 M5**，设计文档已补录。
3. **空中下落/落地表现归 M5**，随招式一起验。
4. **旋转维度不验证。** 用户按设计确认「锁死不转向绝对正确还原游戏设计的」，`RotationPolicy::Locked` 是**事实**而非假设。**M5 不产出朝向数据、不重录 Rise。**（11 份录制已带朝向列，实测整条链 yaw 变化恰好 0.0°，反过来佐证了这条。）
5. **不统一两个模式的积分器**（2026-09-18 定）。实测差异在真实弧上测不出；统一需要换源码版引擎。

---

# 第二部分：未完成

## 6. 已知未修（按优先级）

| 项 | 症状 / 内容 | 为什么还没修 |
|---|---|---|
| **A6 落地→起步的步态相位断链**（**用户主诉**） | 实际链路是 `落地蒙太奇 → Idle（2~3 帧）→ Walk_Start@0（整段 0.875 s）→ Walk_Loop`。**混合消的是姿势差，消不掉相位差** —— 加长 BlendTime 只会把抽搐变软变长 | **等所有招式导入后再统一修**（2026-09-18 用户决定）：地面攻击动作收尾也有同款问题，故与下面那条同批处理。<br>**修法与验收见 §6.1** |
| **后撑杆跳弧尾一帧卡顿**（用户 2026-09-18 指出，指**下坠**而非上升） | CurvedVault 源**末帧之后那一帧**，下坠量从约 −28 cm 掉到约 −9 cm，再恢复。一帧约丢 17~19 cm，所以"不明显"。**机制已钉死**（见 §6.2） | 同上：用户已指出**地面攻击收尾也有同款** ⇒ 这是**所有** movement task 收尾的通用帧量化效应，不该为 CurvedVault 单独打补丁。**建议与 A6 同批做通用修复** |
| **A4c `AbilityOwned` 落地策略空实现** | 急袭突刺与降龙都要用 | **阶段 D 的硬前置**，随那两招一起做 |
| **F1 舞踏倍率断开** | `URes_InsectGlaive::GetDanceDamageMultiplier()`（`Res_InsectGlaive.cpp:835`）**全树零调用方**；`MHGZDamageExecCalc.cpp:86-87` 读 `Damage.DanceMultiplier`，但 `MakeDamageSpec`（`MHGZAttackAbility.cpp:1282-1295`）只写 `MotionValue` 与 `BaseStagger`，全树无写入点。`MaxDanceStacks` 类默认 `0`，`AddDanceStack` 被 clamp 到 0。→ **层数能加、能观测，但不影响任何伤害**；M5 退出条件「倍率封顶且按段快照」当前不可验证 | 阶段 B 的三步：**B1[代码]** `MakeDamageSpec` 里写 `Damage.DanceMultiplier = Resource->GetDanceDamageMultiplier()`，**每个 AttackSegment 创建 Spec 时快照**；**B2[编辑器]** 设 `MaxDanceStacks > 0` 与 `DanceDamageMultipliers[]`（`Num == MaxDanceStacks + 1` 且 `[0] == 1.0f`）；**B3[决策]** `MHGZDamageExecCalc.cpp:107` 的 `FMath::Max(1.0f, DanceMultiplier)` 兜底会让 <1.0 的倍率被静默忽略，确定是否只允许 ≥1.0 |
| **「落到木桩上动不了」(wu13)** | 木桩圆顶的法线 Z 是 **0.715–0.756**，刚好高于 UE 的可行走坡度阈值（cos 44.765° ≈ 0.71），于是 `PhysFalling` 认定它可站立、CMC 自己落地、`HandleLanded` 报 `Landed`，而胶囊在那么小的曲面上无法稳定接触，陷入每 ~0.5 s 一次的穿透极限环，`MovementMode` 一直是 Falling。**这是关卡碰撞几何形状的问题**（圆顶通过了坡度测试却撑不住胶囊），不是纯逻辑缺陷 | 修法二选一：改木桩的碰撞（坡面覆盖 / 不可行走），或加"声称有地面但连续 N 帧无法稳定接触"的出口。**看门狗（4.0 s）也晚于那 3.2 s，不是它。**<br>**我曾写"窗口闸门直接修掉它"—— 那是错的**：假落地发生在 montage 时间 ~1.449 s，远在 0.317 s 窗口之后，闸门会放行 |
| **A2 / 阶段 E 墙钟** | `AM_IG_HouChengGanTiao` 的 `Jump → JumpOver` 边界仍是 `UAbilityTask_WaitDelay` **墙钟定时器**（`MHGZBackVaultAbility.cpp:534-548`），而资产里两段已 `NextSection` 链接。**常驻资产的收益目前没兑现** | 收口项，见 §8 |

### 6.1 A6 的修法

1. **让落地发布「落地出口」载荷** —— `FWeaponMotionMatchingHandoff` 新增 `LandingExit` 类型，携带**落地蒙太奇末帧的步态泳道**（`MM_MoveGait` 的 1/3、2/3、1）。落地**按设计不是 GA**，所以发布点在 `HandleLanded` / `HandleAerialLandingMontageEnded`，不在某个 Ability。
2. **加一条落地路由**：在 `EMHGZMMActionExitRoute` 旁，落地且**有移动输入**时，直接把下一次搜索的候选集限定到 **Move loop 库**，并按泳道选**相位匹配的进入点** —— 即「直接混入到 loop 准备迈出左脚的阶段」，跳过 Idle 与 `Walk_Start@0`。
3. **保留 `Walk_Start`** 给真正的站定起步，不要为修这一个接缝删掉它。
4. 顺带处理那 2~3 帧 Idle 停留：`MoveGaitDelta=-0.6667` 已经在报错配，确认 `StartInputSettleRemaining` / `MMForceIdleReleaseHoldRemaining` 是否应在有落地出口时跳过。

**坠落不要进 PSD 搜索**（位移归 CurvedVault + CMC，两套所有者会违反唯一移动所有者的退出条件）；**落地不要进搜索、但要进路由层**。

**验收**：落地后推杆应直接进入 Move loop 的相位匹配点，链路中**不出现** `Walk_Start@0`，`MoveGaitDelta ≈ 0`，且**用户目视确认抽搐消失**。

### 6.2 弧尾一帧卡顿的机制（已定位到引擎行，未修）

`FinishVelocityParams` 的安装点是 `FRootMotionSourceGroup::CleanUpInvalidRootMotion`（`RootMotionSource.cpp:1346`，装速度在 `:1381-1401`），由 `UCharacterMovementComponent::PerformMovement` 在开头调用（`CharacterMovementComponent.cpp:2743-2753`）。**帧内顺序是 `CleanUp → PrepareRootMotion → StartNewPhysics`。** 于是卡顿那一帧：

1. 帧开始时源**还没**被判完成（遥测末帧 `cT=1.075 < Duration=1.083`），`CleanUp` 不动它 ⇒ 切线速度**没装上**；
2. 紧接着 `PrepareRootMotion` 把时间推过 `Duration`、标 `Finished`，而它只产出路径**剩下的那 0.4%**（进度 0.8767 → 0.8808）⇒ 这一帧的位移就是这点残量；
3. 到**下一帧**的 `CleanUp` 才移除源并装上 `SetVelocity` 切线 ⇒ 从下一帧起恢复。

**`ApplyCurvedVaultSource` 已经做对了**（切线在建源时就装进 `FinishVelocityParams`，注释写着「removes the frame-order dependency entirely」）—— 那句话在**值**上成立，在**帧**上不成立。

**已排除**：碰撞（整段飞行只有落地那一次胶囊命中，法线 Z=1）；采样假象（那段 0.1 s 内 Z 实降 −100.9 cm，而 `VZ` 积分预测 −118.2 cm）；模式切换（两版逐帧数值相同，旧版 `MovementMode` 到末帧才由 5 变 3、新版早在 0.80 就变 3）。

## 7. 待外部输入

| 项 | 卡在哪 |
|---|---|
| **`186` 的 id 语义** | `147`/`159` 被取消的 4 条（各 0.425 s）后继是 `186`。**用户 2026-09-18：长时间无位移肯定不是受击**（受击不会长时间无位移），但**不记得何时录过操虫斩** ⇒ 等补录操虫斩后定案。真值表里**单列为「存疑」，不静默剔除**。注意 **id 是按表分配的、不同表都从 0 开始**，所以「不在虫棍招式表里」**不能**作为剔除依据 |
| **`CantAttack` / `CantDodge` 预算接线** | 这两个 tag 的语义是「空中动作**已用**」——**由招式消费时 acquire**，不是窗口一开就 acquire。空中招式归阶段 D，现在 acquire 反而会把语义写错 |
| **空中受击表现** | 起跳后不允许收刀（设计如此）；被空中命中时播的是**地面**受击动画。`UMHGZHitReactionAbility` 目前只有 `Combat.Stagger.Light` 一条表现路径（`:162-166`）。**尚未制作** |
| **反击触发延迟** | 反击链内**没有任何计时器或段边界**（`HandleIncomingHit` 到 `ApplyBallisticVaultSource` 同一帧同步）。延迟来自训练桩火环 `HitInterval = 0.25 s`（`MHGZDummyConfig.h:121`）才采样一次命中 —— **是测试夹具问题，不是战斗逻辑问题** |

> 上表后三项的实测证据见 `docs/using/空中动作已知问题.md`。

## 8. 阶段 D — 五个空中招式（**未开始**）

五个招式的 C++ 类、GA 蓝图、蒙太奇、Combo 边、空中输入 chord —— **一个都不存在**。源序列已导入，但序列 ≠ 蒙太奇。

按项目规则**逐个纵切**，不要一次写完五个 GA：

1. **操虫斩**（空中 `B` / `LT+B`，两种 AimSource、命中点灯、`AddDanceStack(KinsectSlash)`、走 `BoundedDirectional`）。
2. **强化操虫穿刺**（`LT+Y`，**只**允许从 `Combat.State.Aerial.Dance.Source.KinsectSlash` 派生）。
3. **强化跳跃斩**（空中 `Y`，`AdditiveInertia`）+ **急袭突刺**（空中 `RT`，`LandingPolicy = AbilityOwned`，依赖 A4c）。
4. **降龙**（`LT+Y+B`，`AbilityOwned` Landing，Commit 时先快照倍率再清层）。

每招之后立刻做对应 **[E] 编辑器装配**。

**遗留待确认**：急袭突刺的源序列归属（`AS_Unsh_Fall_TuJin` / `AS_Unsh_Fall_R_TuJin` 命名上像空中突进，需确认根运动与归属）。

## 9. 阶段 E — 收口项（未完成）

- 补 `EIGDanceClearReason`（`AttributeSystem/Res_InsectGlaive.h:38-47`，现有枚举 `Landed / Hit / Sheathed / Unequipped / DescendingThrust / DivingWyvern / RuntimeShutdown`）的 `Hit` / `Sheathed` / `DescendingThrust` / `DivingWyvern` 调用点；该枚举**缺 `Death`**。
- AnimBP 读取 `Combat.State.Aerial.Falling.*` / `.Landing` 选 pose。
- 修 F5 的文档过时点：`demo-implementation-plan.md:496` 与 `actions.md:338` 写 `UAbilityTask_MHGZMovement`，实为 `UAbilityTask_MHGZWeaponMovement`；`actions.md:351` 的 `EMovementCollisionPolicy` 词汇与代码不符（该类已删）；`milestone-gates.md:117` 的 54/54 与 `:77` 的 M4 27/27 是过期数字。
- `AM_IG_HouChengGanTiao` 的 `Jump → JumpOver` 墙钟定时器（见 §6）。
- **自动化空缺**：`MHGZ.M5.Movement.OwnershipAndCleanup` 从未实例化过 Task 本体。

## 10. 阶段 F — M5 签核

Development Editor 全量编译（新增反射字段**不得用 Live Coding 验证**）→ 命名自动化套件带硬计数 → commandlet 资产审计带计数 → DataValidation 冷启动资产数 → `Saved/RuntimeTelemetry/<timestamp>` 录制 → **用户 PIE 目视确认** → 文档状态行回写。
**必须重新计数 `MHGZ.M4` / `MHGZ.M5`，不得复用过期数字**（当前基线 **98 / 97 通过 / 1 失败**）。

---

# 第三部分：真值源与口径

## 11. Rise 真值审计（**唯一真值源**）

> **2026-09-18**：逐段/逐招式的原始真值已迁到 **`docs/reference/真值表.md`**（由 `Scripts/MHRise/build_truth_table.py` 从 22 份可用录制生成），完整口径见 `docs/reference/README.md`。**本节只留「配置对照」这张验收表与几条判据性的结论。**

### 数据约定

- `get_master_player_position` 读 `get_Transform → get_Position` 的**原始世界坐标**，逐渲染帧。
- `velocity_*` **不是游戏里的值**，是 Lua 用位置后向差分算的 —— 与位置自洽，可以用。
- 动作判别字段是 **`player_motion_old_id`**，**不是** `motion_l0..l4`（L1～L4 全空，L0 恒为 `bank=0/id=1`）。
- **MHRise 世界是 Y-up**，竖直 = `world_y`。坐标单位是**米**。
- 采样率 **119.8 Hz**，**帧数 = 样本数 − 1**。
- **不得按离地高度切分** —— 会把连续滞空的多个动作合并。
- **id 是按表分配的，不同表都从 0 开始** ⇒ **编号不全局唯一**；「不在虫棍招式表里」不能推断它不是虫棍动作。小硬直（`400–404`/`411/412`）**空中不能触发**；大硬直是**武器无关**的，不在虫棍表里。

> **旧的「11 份录制 / 动作 id 分段表」已删除** —— 它被 `docs/reference/真值表.md` 的 22 份全量分析取代（`146>147>143>148` = 无白灯、`158>159>148` = 白灯、`160>154>148` = 舞踏，三条链在新表里都还在）。

### ⚠️ 窗口口径（踩着的最重要一条）

**MHR 的起跳段有很长一段地面起手**：`146`（0.652 s）的前 55% 里 `dUp < 1 cm`，人还站在地上；项目侧对应 montage `Jump` 段的前 0.375 s。**按动作 id 分段时这段被算在段内，用「首次 Z 上升」当起点则会被切掉。** 两者混用会得出「项目空中短了 0.38 s」的假结论 —— 同口径后误差只有 0.3%。**判据：窗口 = montage 起 → 落地，不是首次 Z 上升。**

### 配置对照（`InsectGlaiveCombatConfig` 类默认值）

| 量 | 配置 | Rise 真值 | 判定 |
|---|---|---|---|
| `BackVaultDistance` | 583.87 cm | 581.3 | ✅ −0.44% |
| `WhiteBackVaultDistance` | 706.05 cm | 708.4 | ✅ +0.33% |
| `BackVaultApexHeight` | 579.7 cm | 580.0 | ✅ −0.05% |
| `WhiteBackVaultApexHeight` | 748.9 cm | 751.5 | ✅ −0.35% |
| **`WhiteBackVaultDuration`** | ~~2.2333~~ → **2.137** | **2.1325** | ✅ 2026-09-17 修正 |
| `DanceVaultApexHeight` | 564 cm | 564.1 | ✅ |
| `DanceVaultDuration` | 1.6167 s | 1.6221 | ✅ +0.33% |
| `DanceVaultDistance` | 80 cm | 49.8–97.0 | ⚠️ 量级正确（MHR 侧受残余速度影响，散布大） |
| `BackVaultArcDuration` | 1.968 s | 1.9811 | ⚠️ −0.7%（偏短，未改） |

### ★ 关键发现：`159` 的自然长度是 1.596 s，众数 1.486 s 是**被触地砍断的**

`159` 众数长度 179 帧 ≈ 1.486 s，但 **2/34 次跑到 192 帧 ≈ 1.596 s 且停在离地 537–553 cm 的半空**，随后交给真正的下坠 id `157`。**一条「播到落地为止」的动画不可能停在半空** —— 所以它有自然长度，众数那个是被触地砍掉的。

项目侧实测（`MontageInstances.csv`）白灯 montage 反而**播满了全长 2.2333**。**两边机制相反：MHR 是落地先砍动画，项目是动画先播完。**

项目之所以把窗口设成 clip 长度，是因为 `WhiteBackVaultArcDuration` 从 2.14 改成 2.2333 是**为了满足 `HandoffProgress ≤ 1.0` 那道守卫**，不是依据 MHR —— 守卫逼出了一条把路径拉伸 4.7% 的值。**2026-09-17 已改为飞行时长 2.137。**

> 2026-09-18：生成器独立复现了这条 —— `159` 众数簇 1.488 s、**最长观测 1.597 s 且那次后继是 `157`**，真值表用 ⚠ 自动标出这类「众数 ≠ 自然」。

### 下坠段是否需要：**是条件性的**

- **舞踏（154）**：`endY − startY` 中位数 **−11.2 cm，24/24 全为负** —— **它落到地面了，不需要下坠段。**
- **白灯（158→159）**：**32/34 直接进 148**；**2/34 在 159 后插入真正的下坠段 `157`**，而那两次恰是 159 跑得更长、下落更深的两次。

**⇒ 是否需要有下坠段取决于弧线有没有够到地面** —— 这正是 `ResolveVaultExit(Reason)` 的判据，而它**只有一个参数**：真值来源是 `LandedDelegate` 有没有响过，不是"CMC 恰好处于什么模式"。

### 已经排除的量（附：不要再试）

- **加速度不可用。** MHR 的 `acceleration_*` 列**就是速度列的逐帧差分**（逐行数值相同），而速度列自身帧间抖 ±340 cm/s，于是加速度峰值达 160000 cm/s²（160 倍重力）。**它是噪声的数值导数，不是独立测量。** 要加速度必须自己对速度做滑窗拟合。
- **可靠轴只有位移与速度。** 位移曲线最稳；速度列趋势可用、逐点不可用。

### 订正：旧版本里的两个错数字

| 旧值 | 真值 | 错因 |
|---|---|---|
| 白灯 沿航向 **547 cm** | **708.4 cm** | 段落被截断污染（「短段不是不同动作，是没录完」那类错误） |
| 白灯 顶点 **774 cm** | **751.5 cm** | 同上 |

**旧版据此得出的「白灯配置高出 +29%」是错的** —— 配置 706.05 与真值 708.4 只差 0.33%。

## 12. 边界（本阶段不做）

- 不断言舞踏影响伤害（F1 已证断开）；也不反过来说「舞踏完全不可观察」。
- 不在反击 GA / 蓝图 Tick / `MHGZCharacter` 里直接写 CMC 或 `LaunchCharacter` 绕过位移所有权。
- 不删 `BoundedDirectional` / `AdditiveInertia` / `DistanceCurve` / `AirControlScale` / `DescendingThrustAirControl` / `DivingWyvern*` —— 它们是阶段 D 要消费的合同。
- 不做 M6 的觉虫击、逐击点灯、粉尘、HUD。
- 不用自动化测试替代 PIE 视觉验收。
- **不做加速度曲线对照**（见 §11：两边都不是可用测量）。
- **不统一 `PhysFlying` / `PhysFalling` 的积分器**（见 §5 第 5 条）。

---

# 附录 A：已推翻的假设（**别再重试**）

每条都是踩过的坑，留着是为了不重复付学费。

## A.1 方法论

1. **`get_editor_property` 读到值 ≠ 资产里存了这个值。** 属性未序列化时它返回**类默认值**。判据是**「文件字节是否变化」**，不是「读回来的数对不对」。
2. **`-run=pythonscript` commandlet 吞掉脚本的一切输出。** `unreal.log()` / `print()` 都不进日志，脚本抛 `SystemExit(1)` 也不改退出码。**要汇报结果就写文件。**（C++ Commandlet 同样，`Saved/_wuta_montage.json` 就是这么来的。）
3. **`probe_combat_config` 的字段列表是硬编码的。** 字段不在列表里时返回 `None` —— **和「属性真的读不到」长得一模一样**，假阴性过一次。
4. **二进制 grep 判断 `bEnableRootMotion` 无效。** 属性名无论取值都会序列化（`AS_Unsh_Fall` 命中而实测为 `False`）。**序列的提取开关只能靠探针读。**
5. **`UAnimSequence` 在 5.6 的 Python 里没有 data model**（`get_data_model()` 返回 `None`）。读骨骼轨道必须走 C++ Commandlet。
6. **按离地高度切分动作是错的**，必须按 `player_motion_old_id`。
7. **取区间时只能用「时长完整」的段。** 短段不是不同动作，是**录制起止切在动作中间**。
8. **别把 `maxY`（绝对世界高度）当「升高」。** 起跳点本身就有高度（0.49–0.73 m）。舞踏的 625 cm 就是这么错出来的，真值是 564 cm。
9. **窗口口径**：见 §11 —— 「首次 Z 上升」会切掉 MHR 起跳段共有的地面起手。
10. **`head` 截断过的 grep 不能用来下「零命中」这种全称判断。**（`MonsterBody` 那次就是被截断骗了。）
11. **一个"看着像反证"的读数要先问它在这个帧相位是否有效。** `CMC->HasAnimRootMotion()` 出了 `PerformMovement` **恒为 0**，我差点因此放过真凶（见 §2.2）。
12. **别从"编号不在表里"推出"不是这个武器的动作"。** id 按表分配、不同表从 0 开始，编号会撞。
13. **测最早可操作帧时，短段必须看后继 id。** 后继不是空中回避的短段另有语义（可能不是可操作帧）；但**也不能用"后继不在表里"来判**（见 12）—— 用「后继段自身是否长时间无位移」这类**结构特征**判，它不依赖命名。
14. **「众数」不等于「自然」。** `159` 众数 1.488 s（被触地砍断）、自然 1.596 s。被外部事件砍断的招式，众数会给错答案。
15. **别把"接近最大值"当"跑满"。** 对**长度由玩家决定**的招式（`160` 突进回旋斩 0.33~2.39 s），这条会把一个孤例当自然长度 —— 曾据此把舞踏的蒙太奇时间算成 2.709 s（真值 0.316）。

## A.2 具体推断

| 曾经的推断 | 结论 |
|---|---|
| `MontageRootMotionOwned` 是根运动提取的仲裁者，A8/A9 同源 | **错。** 它只驱动 MM 待机与移动所有权守卫。A8/A9 的真实共同机制是 `RootMotionRootLock = REF_POSE` 只认增量 |
| `PushDisableRootMotion()` 从未被 Pop | **错**（后半段订正）：`MontageInstances.csv` 显示同一实例 `RMDisabled 1→0`，Pop 执行了。原因是 `bEnableAutoBlendOut=false` 让蒙太奇永不结束 |
| 后撑杆跳的蒙太奇是运行时用 `BuildBackVaultMontage` 建的 | **错。** 跑的是资产里的 `AM_IG_HouChengGanTiao`；builder 只是兜底 |
| `PushDisableRootMotion()` 是冗余的、不是缺陷 | **错**，它就是 A9 的成因（只关提取不关姿势） |
| 「关键帧 Z 归一化只到 0.766」 | **错。** Z 确实到 1.0；曲线是**相对** `StartProgress` 高度的偏移 |
| 「换原地版序列」修 A9 | **错，方向反了** —— 会删掉一段真实位移（Rise 实测横向确实存在） |
| 「曲线放大了录制横向分量」是主因 | **过度归因。** 幅度只差约 2 倍，且发生在 vault 段而非坠落段 |
| `RequiredSegmentPlayRate` 重复施加了 `RateScale` | **错。** 它写的是 `PlayLength / (DesiredDuration × abs(RateScale))`，已经除掉了 |
| 换掉 `AS_Unsh_W_Jump_Over_Back` 让白灯对齐 MHR 的 1.486 s | **错，方向反了。** 1.486 s 是**被触地砍断**的值，不是自然长度。应改窗口、让落地去砍动画（§11） |
| `CreateSlotAnimationAsDynamicMontage` 的 `InPlayRate` 参数有效 | **错。** UE 5.6 的 `_WithBlendSettings` 声明了它却从不引用（`AnimMontage.cpp:3093-3154`），段速率恒为 `FAnimSegment::AnimPlayRate` 的默认值 1.0 |
| 在蒙太奇里插混合帧会让总时长对不上 | **错。** 同 slot 的交叉淡化**不消耗墙钟时间**，两个 clip 各自继续推进，由 slot 节点按权重平均。唯一会改时长的是真的往 clip 里加帧 |
| 「落地期无动作门禁」（A4b）是真问题 | **前提错**：tag 语义读错了；且 119 次落地 0 次被打断；修它方向与用户目标相反 |
| 「第一个输入就截断落地姿势」 | **从未发生**。119 次独立落地，0 次被打断 |
| 「删掉惰性的 `CollisionPolicy` 会引入回归」（2026-09-18 某轮结论） | **错。** `EMovementCollisionPolicy` 在 HEAD 上**只有一个读者** —— `HandleCapsuleHit` 里的前置守卫，而两个调用方都传 `StopOnBlockingHit` ⇒ **守卫从不短路 ⇒ 该策略本来就惰性**，删它没改变行为。反证：舞踏 22 次里 13 次撞到训练桩（最多 11 次命中），顶点仍精确是 564 |
| 「先播受击动画再起飞」是反击逻辑缺陷 | **错。** 反击链内没有任何计时器或段边界；成因是训练桩火环 0.25 s 的采样节奏（测试夹具） |
| 「两个模式的积分器差异会造成可见跳变」 | **错（幅度上）。** 后撑杆跳跨 `5→3` 时 ΔZ 31.20→31.65→30.27、`VZ` 连续，测不出影响 |
