# M5「空中/舞踏/终结」：接缝修复 + 收尾计划

## Context

项目是 UE 5.6 + GAS 的原创虫棍单机 Demo，走 M0～M7 / E0～E7 的里程碑门禁。每个阶段必须同时有代码、资产接线、PIE 与遥测证据才算签核。

**阅读状态（2026-09-24）**：M4.7 已签核，M5 实施中。本文件保留 M5 各阶段的实施记录和未完成招式计划；早期测试数量、失败试验与“待办”描述均按所标日期阅读，不代表当前状态。空回 `137→157` 接缝的现行结果和剩余验收只看[空回 P0/P1 方案](空回P0-P1完整修复方案.md)；已决定暂缓的观感问题看[低优先级问题单](项目已知的不重要问题.md)，下落物理的未完成配置看[下落现状与缺口](空中下落实现缺口.md)。

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
| 2026-09-21（接缝拆双蒙太奇 + `VaultSeamBlendTime` 0.1334 + 空气墙） | ＋`MHGZPoleVaultMontageSetupCommandlet.*`、`MHGZPoleVaultAbility.*`、`MHGZAerialHandoffSetupCommandlet.*`、16 条蒙太奇资产、`L_DemoArena.umap` | **未跑全量** | **11/11** | — |
| 2026-09-22（阶段 D 第 1 招：空中回避） | ＋`MHGZAirDodgeAbility.*`、`MHGZAirDodgeMontageSetupCommandlet.*`、`MHGZM5AirDodgeTests.cpp`、`MHGZWeaponInputRouterComponent.*`（新开关）、`MHgZWeaponInputProfile.h`、`MHGZWeaponRuntimeHostComponent.*`、`InsectGlaiveCombatConfig.*`、`AM_IG_AirDodge`、`GA_IG_AirDodge`、`DA_IG_InputProfile`、`BP_PlayerState` | **103 / 102 通过 / 1 失败** | **15/15** | 同一个 |
| 2026-09-22（同上 · PIE 第 1 轮「按不出来」的修复） | 只改 `MHGZAirDodgeAbility.cpp`（判据下移 + 补日志）与 `MHGZM5AirDodgeTests.cpp`（＋1 条回归） | **104 / 103 通过 / 1 失败** | **16/16** | 同一个 |
| 2026-09-22（同上 · PIE 第 2 轮三缺陷：可操作帧门 / 舞踏让位 / 自打断） | ＋`MHGZComboCoordinatorAbility.*`（闸门/预输入空中分支/让位）、`MHGZInsectGlaiveAbility.*`（latch 重放钩子）、`MHGZPoleVaultAbility.*`（lead 段标记）、`MHGZAirDodgeAbility.*`（策略枚举/Mark 提前/尾段落地）、`MHGZGameplayAbility.h`+`MHGZWeaponRuntimeHostComponent.h`（ForTest 缝）、Tests×3 | **109 / 108 通过 / 1 失败** | **21/21** | 同一个 |
| 2026-09-22（同上 · PIE 第 3 轮：最早可操作帧改锁 tag 形态） | `Combat.State.Aerial.InputLocked`（ini）；领/放锁 + 中止放锁 poke（`MHGZInsectGlaiveAbility.*`）；空回 `ActivationBlockedTags`（`MHGZAirDodgeAbility.cpp`）；`DA_IG_Combo` 空中 10 行 `BlockedTags`（数据）；旧实例谓词闸门/`InputPolicy` 枚举退役（Coordinator/PoleVault 清理）；Tests 两条改写 | **109 / 108 通过 / 1 失败** | **21/21** | 同一个 |
| 2026-09-22（同上 · 机制再校准：闸门翻转 Falling 正判据） | 裸 `Falling` 领放点（Host `Acquire/ReleaseAerialFallingState`、`SetGrounded`、`Detect`/`NotifyAerialHandoff`/`EndAbility`）；空回 `ActivationRequiredTags`；`DA_IG_Combo` 撑杆跳 10 行 `RequiredTags=[Grounded]`（数据）；`InputLocked` 全退役；`IsAerialFalling` 钉回表现会话；Tests 两条改写 + M1 台账断言随新语义更新 | **109 / 108 通过 / 1 失败** | **21/21** | 同一个（e2e **31/31**） |
| 2026-09-23（空回下坠/落地 + 取消预输入） | `GetAerialDodgeFallMontage`（157 clip 解耦）+ `BeginAerialFalling` `MontageOverride`；`AerialLandingHorizontalSpeed=337` + `PlayAerialLandingVisual` 头重设；空回预输入整套删除（Coordinator/InsectGlaive/AirDodge/Tests）；`AM_IG_AerialLanding` 0.5833 回归钉（用户重烘）；Tests ＋2（−1 预输入） | **110 / 109 通过 / 1 失败** | **22/22** | 同一个（e2e **37/37**） |
| 2026-09-23（缝上速度骤变：空中动画根盾 + 表现 clip 剥离） | Host `ShieldAerialAnimRootMotion`（`ProcessRootMotionPostConvertToWorld`：空中/表现窗口根位移换 `Velocity*Δt`）；命令列 `MHGZStripPresentationRootMotion`（4 源 clip：enable=false + force_root_lock + copied 封死回灌）；Tests ＋2 | **112 / 111 通过 / 1 失败** | **24/24** | 同一个（e2e **40/40**） |

**2026-09-15 到 09-18 的五次都是同一个失败项，逐项未变** —— `MHGZ.PMM.Assets.PoseSearchControlNotifies`，无武装 locomotion 序列把 Notify 挂在 `PoseSearchBlock` / `PoseSearchCostBias` 轨上。**是既存资产问题，与 M5 无关；全量套件在任何改动之前就不是绿的**，所以 `milestone-gates.md` 里的 54/54 是过期数字。

**2026-09-24 已修，套件首次全绿**：新增单一用途 commandlet `MHGZPMMNotifyTrackRepair`（只改轨名 / `TrackIndex`，不碰通知类别与时间），把 **17 条**序列（测试只点名落在 PSD 引用集里的 4 条）的轨名归位到 `PoseSearchControl`，并删掉被清空的冗余轨。全量 `Automation RunTests MHGZ` = **117 项 117 成功 / 0 失败、`EXIT CODE 0`**，`-run=DataValidation` 复跑仍是 0 error / 0 warning。⚠ 顺带记一条：**不要**改成重跑 `MHGZPMMAssetFixupCommandlet` —— 它的 Stop 块起点是固定 `0.12`，而测试按 PMM-7.1 从 `MM_DistanceToStop` 的提交键推导生成停步的起点，重跑会把那批时序改坏（该工具相对测试已过期，属独立待办）。

**2026-09-21 那一行只跑了 `MHGZ.M5`（11/11）**，全量未重跑 —— 所以「同一个失败项还在不在」这一行没有新证据，**签核（§10）时必须重跑全量**。

**历史测试口径**：当时那批改动没有修改 `DA_IG_Combat`，但资产已序列化普通/白灯下落重力与制动值；后续不能只改 `InsectGlaiveCombatConfig.h` 的类默认值就假定运行时配置随之改变，见[下落现状与缺口](空中下落实现缺口.md)。

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
| **接缝硬切** | 白灯左/右 Jump→JumpOver 姿势突跳 | 见 §2.3 | 2026-09-21，**遥测 29/29 逐值验证** |

> ⚠ **行号过期标记（2026-09-21）**：上表 A1/A4a/A5/A7/A8/A9 与「舞踏蒙太奇登记缺口」引用的
> `MHGZBackVaultAbility.cpp` **行号已失效** —— 撑杆跳改成「基类 + 四向薄子类」之后，机制整体
> 上移到了 `MHGZPoleVaultAbility.cpp`。**结论、实测数字、教训全部仍然有效，只有行号要重新定位**。
> 往后的引用请用 `MHGZPoleVaultAbility.*`（`UMHGZBackVaultAbility` 现在只剩构造 + profile）。

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

### 2.3 接缝硬切 → 拆成双蒙太奇 + 交叉淡入（2026-09-21，**遥测已逐值验证**）

**症状**：白灯左/右跳从 Jump 到 JumpOver「有点瞬移」。

**根因**：蒙太奇同一条 AnimTrack 上的段与段**永远硬切** —— `FAnimTrack::GetAnimationPose`（`AnimCompositeBase.cpp:535`）用 `GetSegmentAtTime` 只取**一个**段，`ValidateSegmentTimes` 又把 `StartPos` 重新首尾相接，**连手工做重叠窗口都不可能**。而白灯左/右**没有专用起手段**（原作白灯一族只有 `155/156/157/158/159`），复用为弧段 `142` 授权的 `144`/`145` —— 所以只有这一对落差特别大。

**修法**：八条资产全部拆成**两条单段单节蒙太奇**（起手段沿用原名，弧段新建 `_Over` 后缀），接缝靠「播弧段时把起手段交叉淡出」糊掉 —— **不需要任何新机制**，`UAnimInstance::Montage_PlayInternal`（`AnimInstance.cpp:2394-2398`）在 `bStopAllMontages` 为真时调 `StopAllMontagesByGroupName(Group, BlendInSettings)`，**入场蒙太奇的 blend-in 设置同时把旧蒙太奇按同样时长淡出**。

**`VaultSeamBlendTime = 0.1334 s` 是扫出来的，不是拍的** —— `probe_vault_seam_blend.py`（倒姿势网格）+ `Saved/_mhr_scratch/analyze_vault_seam_blend.py`（离线判据，改判据不用重开编辑器）。判据是**逐帧局部**的 `r(k) = 淡入第 k 帧增量 / 纯弧段第 k 帧增量`。拐点在 0.1334 s，**30/40/60/120 fps 下都是同一点**：

| B (s) | 弹跳 max r | 代价 min r | 总偏差（max abs） | 拖拽 deg·s |
|---|---:|---:|---:|---:|
| 硬切 | 2.24 | — | 1.24 | — |
| 0.10 | 1.98 | 0.40 | 0.98 | 0.9 |
| **0.1334** | **1.58** | 0.30 | **0.70** | 1.2 |
| 0.2668 | 1.88（**回升**） | 0.15 | 0.88 | 2.5 |

**⚠ 订正一个测量口径。** 本任务早期写下的「白灯右 4.28×」是拿**1/30 剪辑秒**当「正常一帧」的落差当分母，而剪辑是被段速率压缩播的、弧段在接缝那一帧本来就跑得很快（实测 4.64°/帧 @40fps）。**换成逐帧局部基线后硬切最差是 2.24×** —— 旧口径把「前」放大了约 1.9 倍。**绝对落差（13.55°/14.40°）没问题，错的只是分母。**

**⚠ 再长就变差**：0.20/0.2668 的弹跳回升到 1.7~1.9。原因是引擎的旋转合成是 **nlerp 不是 slerp**（`AccumulateWithShortestRotation`，`TransformVectorized.h:1061-1072`），α≈0.5 处角速度不均匀，淡入越长中点越容易落进弧段的快速段。

**过程中的两个坑**（都已修，值得记住）：

1. **阻断性的**：`StopAllMontagesByGroupName` 传的是 `bInterrupt = true`（`AnimInstance.cpp:3280`），而 `UAbilityTask_PlayMontageAndWait::OnMontageBlendingOut` 在 `bInterrupted` 时直接广播 `OnInterrupted`（`:42-44`）⇒ 照最初写法**每一次撑杆跳都会在 0.650 s 终止**。修法是在接缝那一帧摘掉起手段任务的中断回调（不能干脆不绑 —— 起手段阶段的真中断仍必须结束动作）。
2. **Back-white 的时长陷阱**：弧段时长若按「窗口 − 起手段」推，白灯后撑杆跳的弧段姿势会从 1.5833 被压到 1.487（−6.1%），而那个「姿势刻意比窗口长、让落地砍断」正是 PIE 验证过的行为。新增 `FVaultProfile::JumpOverDuration` 把**姿势长度**与**移动窗口**分成两个量。

**验收（`Saved/RuntimeTelemetry/20260921-2044*`，29 次撑杆跳）**：

| 判据 | 实测 |
|---|---|
| 重叠帧数 / Σw | 5 帧 / **恒 1.0000** |
| α 序列 vs 引擎递推 | 误差 ≤ 0.013，**包括末帧那一跳** |
| 起手段位置 / `IsPlaying` | 冻结在 **0.6499** / 0 |
| 淡出期 `RootMotionDisabled` | 恒 1 |
| 弧段 `MontageHasRootMotion` | 恒 0 |
| 距离 / 顶点 vs 拆分前 | **逐组同值** |
| `MHGZ.M5` | **11/11**（当时）→ **28/28**（2026-09-23/24） |

复跑入口：`probe_vault_seam_pose.py`（硬切基线）+ `probe_vault_seam_blend.py`（姿势网格）+ 离线分析器。

## 3. 已改判 / 已关闭（**前提被否定，不需要修**）

| 项 | 原症状 | 结论 |
|---|---|---|
| **A3** | 交棒姿势跳变 | **被数据否定**（f6096 是干净的 0.5/0.5 交叉淡入） |
| **A4b** | 落地期无动作门禁 | **前提被否定**：`Aerial.CantAttack`/`CantDodge` 的 DevComment 是「空中攻击**已用**」「空中回避**已用**」—— 它们是**动作预算标记**，不是「落地期间锁输入」。且全部录制里 **119 次独立落地，0 次被动作打断**。改判为代码整洁项。详见**附录 A.2**（同一结论的逐条版，此处不重复展开） |
| **A2** | 边界墙钟与动画段脱钩 | 实测源起于 montage time **0.6758~0.7249**，而 `BackJumpDuration = 0.650`，偏差 **1~3 帧**、未造成症状。**只加校验不重构**（激活时与 `GetSectionEndTime` 对表）→ 降级为收口项，见 §6 |

**要克制的一条**：A3 这类"代码看着不对但数据说没事"的项，**不要顺手修**。对话前段正是栽在这个模式上 —— 把代码结构推断当成已验证事实，并据此排错优先级。

## 4. 已完成的基础设施（不是 bug 修复）

| 项 | 内容 |
|---|---|
| **阶段 0 资产探针** | `Scripts/AssetProbe/probe_m5_assets.py`（只读，写 JSON 到 `Saved/AssetProbe/`）。**Python 路线读不到骨骼轨道**（`UAnimSequence` 在 5.6 不暴露 data model），需要 DataModel 时必须走 C++ Commandlet |
| **阶段 C 后撑杆跳设计文档补录** | 已完成 |
| **遥测** | `Saved/RuntimeTelemetry/` 下的成套 CSV 覆盖 montage / RootMotionSource / MovementMode / 骨骼三个几何层次，2026-09-17 又补上加速度 |
| **唯一位移执行层** | `UAbilityTask_MHGZWeaponMovement`（四种 `EWeaponMovementMode`：`BoundedDirectional` / `BallisticVault` / `CurvedVault` / `AdditiveInertia`）。已接线：`BallisticVault`（突进回旋斩反击）、`CurvedVault`（后撑杆跳）。**其余两种是按合同预置的基础设施，不是死代码** —— 正好对应未开工的操虫斩与强化跳跃斩/急袭突刺 |
| **撑杆跳 / 舞踏 实现** | **2026-09-21 起撑杆跳是四向的**：机制全在基类 `UMHGZPoleVaultAbility`（`Abstract`），四个方向各一个薄子类（`Back`/`Forward`/`Left`/`Right`）只负责灌自己的 `FVaultProfile`；`UMHGZBackVaultAbility` **退化成薄子类**。资产：四向 × 两灯态 × 两半 = **16 条蒙太奇**（起手段沿用 `AM_IG_*ChengGanTiao{,_W}`，弧段 `_Over` 后缀）。舞踏仍是 `UMHGZAdvancingCounterAbility` + `AM_IG_WuTa`。**后撑杆跳曾是无主实现**（F3），设计文档补录后才纳入 M5 |
| **Rise 真值管线（2026-09-18 新增）** | `docs/reference/`（真值表 + 招式表 + 口径）+ `Scripts/MHRise/build_truth_table.py`（生成器）。逐帧轨迹写到已入库的 `Saved/_mhr_frames/`。详见 §3 |
| **命令列** | `MHGZAerialHandoffSetupCommandlet`（写点通知，报告 `Saved/_aerial_handoff.json`，**9 条 `validated: true`**）、`MHGZWuTaMontageSetupCommandlet`、**`MHGZPoleVaultMontageSetupCommandlet`**（2026-09-21 新增，烘 16 条单段单节蒙太奇，报告 `Saved/_pole_vault_montage.json`）。<br>**⚠ 2026-09-21 拆双蒙太奇之后，点通知从起手段搬到了弧段**：前/左/右与后 0.783 → **0.133**，白灯后 0.775 → **0.125**（都是减 `JumpDuration = 0.650`）。报告里另有 `takeoff_ordering[*]` 断言「起手段那半边不许带点通知」——**通知要是留在只有 0.650 s 长的起手段上就永远触发不了**，而"有没有通知"的判据却会为真。<br>**⚠ 别"顺手清理" `MHGZAerialHandoffSetupCommandlet.cpp:26` 的 `RetiredStateClassName = TEXT("AnimNotifyState_IG_AerialWindow")`** —— 旧类删掉之后，清理仍然靠**类名字符串**匹配（而不是 `Cast`），这行正是"幂等重跑仍能清掉旧通知"的实现方式，删了它旧蒙太奇就再也清不干净 |
| **怪物碰撞：不可站立** | `MonsterBody` 早就在用（`AMHGZMonsterBase` 构造函数）；「不可站立」不是碰撞预设能表达的 —— `FWalkableSlopeOverride` 是**组件属性**，`FCollisionProfileName` 里没有它，所以对胶囊写 `WalkableSlope_Unwalkable` |

> **订正一条我自己的错误结论。** 我曾据一次 grep 说「`MonsterBody` 在全树 `Source/` 零命中、只在蓝图里指派」。
> **那是错的** —— 那个 grep 被 `head -20` 截断，`MHGZMonsterHitzoneComponent` 里的 `MonsterHitzone` 子串把输出占满了，`MHGZMonsterBase.cpp` 排在字母序后面被截掉。
> **教训：`head` 截断过的 grep 不能用来下「零命中」这种全称判断。**

## 5. 已确认的决策

1. **先修接缝**（理由：M5 剩余五个招式（2026-09-22 起为六个 —— 空中回避也纳入了阶段 D）**每一个**都以「进入 Falling」或「落地」收尾，接缝是所有空中动作的公共基底；缺陷已渗到已签核路径上；现在只有一个招式、变量最少）。项目自己的冻结规则禁止批量前提（`demo-implementation-plan.md:767`），而 M5 退出条件本身就是「任一取消路径都只剩一个 CMC 移动所有者且不存在残留 WarpTarget」。
2. **后撑杆跳正式纳入 M5**，设计文档已补录。
3. **空中下落/落地表现归 M5**，随招式一起验。
4. **旋转维度不验证。** 用户按设计确认「锁死不转向绝对正确还原游戏设计的」，`RotationPolicy::Locked` 是**事实**而非假设。**M5 不产出朝向数据、不重录 Rise。**（11 份录制已带朝向列，实测整条链 yaw 变化恰好 0.0°，反过来佐证了这条。）
5. **不统一两个模式的积分器**（2026-09-18 定）。实测差异在真实弧上测不出；统一需要换源码版引擎。
6. **接缝用交叉淡入，`VaultSeamBlendTime = 0.1334 s`**（2026-09-21 定）。拐点由扫描给出，30/40/60/120 fps 下一致（见 §2.3）。**这是一个权衡不是最优点**：它把接缝跳跃从 2.24× 压到 1.58×，代价是弧段开头那一帧只跑到应有速度的 0.30 倍（短暂迟滞）。**若 PIE 目视觉得「起手发沉」比「接缝一跳」更难受，就往小调**（0.10 时是 1.98× / 0.40 倍）。
7. **不做逐变体的淡入时长**。硬切落差本来就小于一帧正常运动量的组合（白灯前/后，1.09/0.85）在淡入后升到 1.24 —— 是负收益，但 1.24 远低于最差的 1.58，不值得为它多一个常量。
8. **竞技场空气墙加高到 30 m**（2026-09-21）。四面墙原本 300 cm，而撑杆跳顶点 558~749 cm。只改 Z，内侧面与 X/Y 跨度不动（推墙会改可玩面积）。撑杆跳位移走 `SafeMoveUpdatedComponent → HandleImpact → SlideAlongSurface`（`AbilityTask_MHGZWeaponMovement.cpp:270-294`），**会撞墙**，所以加高即够。
9. **空中回避纳入 M5 阶段 D，且排第 1**（2026-09-22）。理由见 §8 开头：它是唯一「设计有、真值测完、代码为零、两头计划都没挂」的招。**落地重设水平速度那一条不跟着做**（§12 边界）。
10. **空中回避的位移走现成的显式初速弹道**，不新造位移层、不新增重力常数（`AerialFallGravityScale` 与真值差 0.08%）。**不能用「顶点 + 时长」那组参数**：那条路由引擎按 `4H/D` 反算 v0，代入会得到 989 而不是 1182 —— 入场速度直接错。
11. **输入侧两个决定**（2026-09-22，用户拍板）——这是本轮唯一动到**已签核输入层**的地方，两条都记在这里：
    - **`Input.Weapon.RTA` 加 `RequiredContextTags = {Combat.State.Grounded}`**：撑杆跳是从地面起跳的，空中 RT+A 不该出招。**纯数据改动**，效果是空中的 A 单独按下零延迟。
    - **新增 per-chord 开关 `bModifiersMustPrecedeTriggers`（默认 false）挂在 RTA 上**：让**地面**的 A 单独按下也零延迟。代价（已接受）：**「先按 A 再在 50 ms 内补 RT」不再出撑杆跳**，要出撑杆跳必须**先按住 RT**。默认 false ⇒ 其它 chord 语义逐字不变。
    - A 键改成**完全由 chord 中介**：`DA_IG_InputProfile` 里两条单成员 chord 各带上下文（Grounded→`Input.Dodge`、Aerial→`Input.AirDodge`）。因为 `RebuildChordCache` 会把单成员 chord 的 trigger 无条件塞进 `SingleChordOwnedTags`，而三处直出判据只查集合、不查上下文 —— 只加一条空中 chord 会**永久掐死地面的 A**。

---

# 第二部分：未完成

## 6. 已知未修（按优先级）

| 项 | 症状 / 内容 | 为什么还没修 |
|---|---|---|
| **A4c `AbilityOwned` 落地策略空实现** | 急袭突刺与降龙都要用 | **阶段 D 的硬前置**，随那两招一起做 |
| **F1 舞踏倍率断开** | `URes_InsectGlaive::GetDanceDamageMultiplier()`（`Res_InsectGlaive.cpp:835`）**全树零调用方**；`MHGZDamageExecCalc.cpp:86-87` 读 `Damage.DanceMultiplier`，但 `MakeDamageSpec`（`MHGZAttackAbility.cpp:1282-1295`）只写 `MotionValue` 与 `BaseStagger`，全树无写入点。`MaxDanceStacks` 类默认 `0`，`AddDanceStack` 被 clamp 到 0。→ **层数能加、能观测，但不影响任何伤害**；M5 退出条件「倍率封顶且按段快照」当前不可验证 | 阶段 B 的三步：**B1[代码]** `MakeDamageSpec` 里写 `Damage.DanceMultiplier = Resource->GetDanceDamageMultiplier()`，**每个 AttackSegment 创建 Spec 时快照**；**B2[编辑器]** 设 `MaxDanceStacks > 0` 与 `DanceDamageMultipliers[]`（`Num == MaxDanceStacks + 1` 且 `[0] == 1.0f`）；**B3[决策]** `MHGZDamageExecCalc.cpp:107` 的 `FMath::Max(1.0f, DanceMultiplier)` 兜底会让 <1.0 的倍率被静默忽略，确定是否只允许 ≥1.0 |
| **「落到木桩上动不了」(wu13)** | 木桩圆顶的法线 Z 是 **0.715–0.756**，刚好高于 UE 的可行走坡度阈值（cos 44.765° ≈ 0.71），于是 `PhysFalling` 认定它可站立、CMC 自己落地、`HandleLanded` 报 `Landed`，而胶囊在那么小的曲面上无法稳定接触，陷入每 ~0.5 s 一次的穿透极限环，`MovementMode` 一直是 Falling。**这是关卡碰撞几何形状的问题**（圆顶通过了坡度测试却撑不住胶囊），不是纯逻辑缺陷 | 修法二选一：改木桩的碰撞（坡面覆盖 / 不可行走），或加"声称有地面但连续 N 帧无法稳定接触"的出口。**看门狗（4.0 s）也晚于那 3.2 s，不是它。**<br>**我曾写"窗口闸门直接修掉它"—— 那是错的**：假落地发生在 montage 时间 ~1.449 s，远在 0.317 s 窗口之后，闸门会放行 |
| **起手段阶段没有任何根运动源**（旧录制中的“空中顿一下”） | 接缝**之前**那 3~5 帧里水平速度从 13.7 cm/帧衰减到 0.68 cm/帧，个别帧正好 0.00。29 次撑杆跳里 26 次至少一帧 < 2 cm。**位置在接缝之前，所以交叉淡入改不了它** | **机制只查了一半**，见 §6.1。`VaultHandoffLeadSeconds` 不能在未查清来源前盲调；该旧录制不等于最新 PIE 已复现。 |
| **顶点/距离相对 Rise 的既有偏差**（**不是本次回归**） | 距离 `有白灯·向左` **−7.3%**；顶点 `有白灯·向前` **−5.6%**、`无白灯·向左` **−5.3%**、`有白灯·向左` −4.7%、两个「向右」−3.7%。计划判据是 ±4% | 拆分前后**逐组中位数完全相同**（`Saved/_mhr_scratch/apex_old_vs_new.py` 对拆分前两份录制跑同一判据）⇒ 与接缝工作无关。`有白灯·向左` 的 Rise 真值只有 **2 次可用试次**，偏差更可能在参照那一侧。**接受 / 补录 / 调表，待裁决** |
| **最早可操作帧晚约一帧** | 设计 0.783（链上），实测 `Combat.State.Aerial.Actionable` 出现在链上 **0.825**（弧段局部 0.175）。差 0.042 s ≈ 1.7 帧 | 可能只是 notify 要等下一个 tick 才反映的固有量化。**未确认是否算问题** |
| **三条 PIE 路径从未走到过** | ①空摇杆 RT+A → 前推的兜底边；②撑杆跳**中途被打断**；③**落地打断**。后两条会走 `EndAbility` 里「先停弧段再停起手段」那条清理，该路径只过了编译与单测 | 需要用户跑；`EndAbility` 的清理顺序是拆双蒙太奇时新写的 |
| **空气墙** | `L_DemoArena` 四面墙原本只有 **300 cm** 高，而撑杆跳顶点 558~749 cm ⇒ 一跳就越过去（用户「老是掉下去」） | **已修**：四面墙 `scale.z 3 → 30`（Z ∈ [0, 3000]），内侧面与 X/Y 跨度未动，`git diff` 只有 `L_DemoArena.umap`。**但未在 PIE 里真的跳一次撞上去验证** |
| **空中回避** | `20260922-224312` 的第 4 轮 PIE 中，22 次跑满弹道的标量进入真值允差；2026-09-23 23:57 的接缝录制也已通过。前几轮失败、输入门禁和看门狗修复过程在 §8.1 留作历史。 | 代码与资产已入库；P0/P1 的生产链路自动化和最终可视验收以[空回方案](空回P0-P1完整修复方案.md)为准。 |
| **A2 / 阶段 E 墙钟** | 交棒仍由 `UAbilityTask_WaitDelay(JumpDuration)` **墙钟**驱动（现 `MHGZPoleVaultAbility::ScheduleJumpOverMovementHandoff`，`:795`，`WaitDelay` 在 `:826`），不是由蒙太奇段边界驱动。**2026-09-21 拆成两条蒙太奇之后，蒙太奇里已经没有 `Jump → JumpOver` 边界了**，所以这条只剩「交棒调度仍是墙钟」这一半 | 收口项，见 §9 |

落地后步态衔接及撑杆跳弧尾一帧卡顿已按用户决定暂缓，其观察与证据边界集中在[低优先级问题单](项目已知的不重要问题.md)，不再作为 M5 当前修复项。早先针对落地专用 MM 路由的设想未经 PIE 证明，不应当作既定实现方案。

### 6.1 起手段阶段的「空中顿一下」（2026-09-21，**机制只查了一半**）

**位置**：接缝**之前**的 3~5 帧。水平位移（cm/帧，接缝为第 0 帧）：

```
-8     -6     -5     -4     -3     -2     -1      0     +1     +2     +3
13.7   5.4    3.2    1.6    1.4    0.7    1.8    1.8    5.6    9.3    9.3
```

29 次里 **26 次**至少一帧 < 2 cm；±8..+6 窗口里 **41/435 帧 < 1 cm**。

**已经坐实的**：

- `Character/RootMotionSources.csv` 在接缝前那 **10 帧一条源都没有**（`SourceType=Invalid`）。CurvedVault 源（`InstanceName=MHGZ_ActionMove_4_1`，`Duration=1.1029`）**在接缝那一帧才被创建**。
- 起手段**整段**水平位移 **252.7 cm**，而该剪辑（`AS_Unsh_Jump_Right`）实测根净位移是 **220.59 cm** —— **多出 32 cm**。位移是有的，只是**分布极陡**（19 cm/帧 → 0.68 cm/帧）。
- 同期 `AnimShouldExtractRootMotion=1`、`AnimRootMotionMode=2`（RootMotionFromEverything），但 `HasAnimRootMotion=0` **整段不变**；起手段蒙太奇自身 `MontageHasRootMotion=1` 且接缝前**未被禁用**。
- `BeginBackVaultInitialFlight`（`MHGZPoleVaultAbility.cpp:652`）**只切 `MOVE_Flying`，不赋速度**。

**还没查清的（动它之前必须先查）**：

1. 多出来的 32 cm 从哪来？若根运动是唯一驱动，应当恰好 220.59。
2. 为什么 CMC 级根运动整段是 0（`AnimShouldExtractRootMotion` 明说该提取）。
3. 速度为什么会衰减到近零 —— `MOVE_Flying` 的 `BrakingDecelerationFlying` 在 UE 默认是 **0**，项目也没设，所以「刹车」这个解释目前**不成立**。

> ⚠ `CMC->HasAnimRootMotion()` 出了 `PerformMovement` **恒为 0**（见 §2.2 与附录 A.1 第 11 条）—— 用它当「根运动没在起作用」的证据是**无效的**，上面第 2 条要用别的手段查。

**旋钮不要盲动**：`VaultHandoffLeadSeconds`（现 0）把 `WaitDelay` 的时长减掉它，让源提前起。
提前 1 帧填不上 3~5 帧的坑；提前 5 帧（0.125 s）虽然能把衰减段盖住，但**动画会甩在轨迹后面同样多** —— 那是把「顿一下」换成「姿势与位移错位」，不是修好。**先查清机制。**

复跑：`Saved/_mhr_scratch/check_vault_live_v2.py`（接缝+胶囊）、`apex_old_vs_new.py`（顶点/距离对照）。

> ⚠ **遥测单位**：`Character/Spatial.csv` 的 `Location*` 是**厘米**、`Velocity*` 是 **cm/s**。
> 再乘 100 会得到「726 m/s 竖直速度、飞到 656 m 高」这种荒唐读数（本节初稿踩过）。

## 7. 待外部输入

| 项 | 卡在哪 |
|---|---|
| **`186` 的 id 语义** | `147`/`159` 被取消的 4 条（各 0.425 s）后继是 `186`。**用户 2026-09-18：长时间无位移肯定不是受击**（受击不会长时间无位移），但**不记得何时录过操虫斩** ⇒ 等补录操虫斩后定案。真值表里**单列为「存疑」，不静默剔除**。注意 **id 是按表分配的、不同表都从 0 开始**，所以「不在虫棍招式表里」**不能**作为剔除依据 |
| **`CantAttack` / `CantDodge` 预算接线**（**一半已接，2026-09-22**） | 这两个 tag 的语义是「空中动作**已用**」。**`CantDodge` 已接**：`Host->MarkAerialDodgeUsed()` 在空中回避激活时领、**只在落地（`HandleLanded`）释放** —— 中途取消进别的空中招式**不退款**，所以 Token 由 Host 持有而不是能力持有。**仍待**：`CantAttack` 那一半（空中攻击招式还不存在），以及**舞踏重置两个 Cant**（操虫斩命中/突进回旋斩反击是舞踏入口，留到操虫斩那轮） |
| **顶点/距离既有偏差怎么处置**（2026-09-21 新增） | 三组超计划的 ±4%（距离 `有白灯·向左` −7.3%；顶点 `有白灯·向前` −5.6% / `无白灯·向左` −5.3%）。**拆分前后逐组同值 ⇒ 不是接缝工作引入的。** 接受 / 补录 Rise 白灯左 / 调表，三选一，**要用户定**。见 §6 |
| **三条 PIE 路径需要用户跑**（2026-09-21 新增） | ①**空摇杆 RT+A → 前推**的兜底边；②撑杆跳**中途被打断**；③**落地打断**。后两条会走 `EndAbility` 里「先停弧段再停起手段」这条清理 —— 拆双蒙太奇时新写的，实跑里一次都没触发过。另外空气墙的实际拦截（跳一次撞上去）也一并看。见 §6 |
| **接缝的观感**（2026-09-21 新增） | 遥测里**没有逐骨姿势**，所以「淡入之后还顿不顿」只有用户的眼睛能判。数字面：硬切 2.24× → 0.1334 s 后 1.58×。见 §2.3、§5 第 6 条 |

已暂缓的受击、反击观感和移动接缝问题集中在[低优先级问题单](项目已知的不重要问题.md)，不作为本表的 M5 当前阻塞项。

## 8. 阶段 D — 六个空中招式（第 1 招已实现；最终验收见 P0/P1 方案）

2026-09-22 用户确认将空中回避（Rise `137`）纳入阶段 D 并排在第一招；旧计划的“五招”计数不再适用。空回的实现与下落现状见[空中下落实现缺口](空中下落实现缺口.md)，交棒的现行验收见[空回 P0/P1 方案](空回P0-P1完整修复方案.md)。

### 8.1 空中回避 —— 已实现（2026-09-22；交棒仍待最终可视验收）

| 要素 | 落点 |
|---|---|
| C++ | `UMHGZAirDodgeAbility`（`MHGZAirDodgeAbility.{h,cpp}`，基类 `UMHGZInsectGlaiveAbility`） |
| 位移 | 复用现成的 `BallisticVault` + `EBallisticParameterMode::ExplicitLaunchVelocity`，**没有新造位移层** |
| 资产 | `AM_IG_AirDodge`（由 `AS_UnSh_Dash_Air` 烘，单段单节 1.18333 s）、`GA_IG_AirDodge`（指派蒙太奇） |
| 授予 | `BP_PlayerState` 的 `CoreAbilities`（闪避族是输入直驱，**不进** `DA_IG_Combo`） |
| 点通知 | 第 10 条路由：`AM_IG_AirDodge` @ **0.650 s** |
| 输入 | `Input.AirDodge` + `DA_IG_InputProfile` 两条单成员 chord（Grounded→`Input.Dodge`、Aerial→`Input.AirDodge`） |
| 预算 | `CantDodge` 由 `Host->MarkAerialDodgeUsed()` 领、**只在落地释放**（能力结束不退款） |

**四条与直觉相反、必须记住的实现事实**（都写进了代码注释）：

1. **没有起手段。** 142 帧本身就是一条抛物线，初速从第 0 帧生效 —— 与撑杆跳「Jump + JumpOver」结构本质不同，
   所以不拆双蒙太奇、不做段速率压缩（剪辑 `rate_scale = 2.0`，回算速率正好 1.0）。
2. **「142 帧」不是源时长。** 显式初速模式下源的时长由重力反算（`2·vz/g = 0.995 s`），余下 0.188 s 交给
   CMC 自由落体；两段相加才是 142 帧。这与真值「`137 → 157 → 落地` 是同一条抛物线、无速度重置」一致。
3. **头号风险是定序**：`CMC->GetGravityZ()` 决定反算出的时长与顶点，而 `GravityScale` 只在
   `BeginAerialFalling` 里被设成 2.4246。空中回避能在撑杆跳**弧段中途**触发，那一刻它还是 1.0
   ⇒ 套进去会算出 **2.4 倍**的弧。所以新增 `Host->ApplyAerialFallingProfile(false)`，必须在建源**之前**调。
4. **它是八向的**（用户指出、实测坐实）：方向取输入快照的世界系摇杆方向（**连续角，不量化**），
   激活那一帧**瞬转**（基类 `ApplyDirectionCorrection`，本 GA 的 `MaxCorrectionAngle = 180`），
   之后整段锁死（段内 213/213 段朝向变化恰好 0.0°）。⚠ 真值表那列 `|方向−朝向| = 0.0°` **分不开**
   「不转向」与「一按就瞬转」—— 我照它设计错过一次，详见附录 A 新增的方法论条。

**离线验收**：`Saved/_mhr_scratch/verify_air_dodge_end_to_end.py` 只读盘上资产，**26/26 通过**；
`MHGZ.M5` **28/28**（2026-09-23/24；空中一组：路由四态 / 请求形状 / CantDodge 预算 / tag 已声明 /
激活闸门不读激活上下文 / **闸门 latch 开合** / **让位** / **尾段落地** / **预算 commit 即消费** / **收尾判据纯函数**（`MontageEndCompletionWindow`）/ **收尾任务会 tick 且实例保持终态姿势**（`AirDodgeVisualTaskTicksAndHoldsTerminalPose`）/ **落地真触地两档入速**（`LandingRealTouchdownTwoEntrySpeeds`）。⚠ 原列的「**预输入策略**」已随预输入退役删除 —— 锁定期按下直接作废。

#### PIE 第 2 轮三缺陷与修复（2026-09-22，行为表在此）

**根因**（录制 `RuntimeTelemetry/20260922-141253-*`，逐条有证据）：

1. **可操作帧设计没生效** —— 通知本身工作正常（Actionable 精确出现在激活 +0.651 s、0.650 处 Flying→Falling），但该 tag **零消费方**、锁存读口 `IsAerialHandoffReached()` 全树零调用 ⇒ 只是记账不是门。
2. **舞踏按空回没位移** —— `Host->AcquireActionMovement` 互斥把空回的源启动**静默**拒绝（任务侧零日志）⇒ `FinishMovement(Failed)` ⇒ 能力自杀；舞踏的视觉任务刻意不绑中断回调（抢蒙太奇槽杀不死它）。同一机制会打掉撑杆跳弧段入口（真值 137 前驱弧段×117）。
3. **自己打断自己** —— `MarkAerialDodgeUsed()` 在位移启动**之后** ⇒ 失败的激活从不记预算 ⇒ 无 CantDodge 自锁 ⇒ 每次按下重新激活、新蒙太奇顶掉旧的（burst 闪断）。

**修复**（机制与分工见计划文件「已定决策」）：

- **可操作帧成为真门**：闸门谓词 =「有无 blocking 动作」（live + 已 Commit + 挂点通知 + 未 latch，**含自己**）；闸在 `CanActivateAbility`（预输入钩子挂在 `!TryActivateDirectInput` 上，Validate 拒绝进不了缓冲）。被闸住的按下**当时复用既有预输入系统**（独立单槽 `BufferedAerialDodgeInput`、TTL `AerialPreInputLifetime=0.9s`、latch 时经 `OnAerialHandoffReached` → `TryConsumeBufferedAerialDodgeInput` 重放，镜像地面 `OnDodgeAcceptWindowOpened` 那条腿；重放重查 CanActivate）。⚠ **2026-09-23 该预输入已退役** —— 锁定期按下直接作废，不入槽、放锁点不补发；见本节末的退役记录。地面分支一字未动（分派用精确 `==`）。
- **让位**：空回 commit 后立刻把其它活跃 IG 动作以 `Superseded` 结束（`RequestEndAction`，与连招确认同语义）⇒ 同步摘源、释放位移所有权 ⇒ 再建本招的源。舞踏与弧段两个入口都由它兜住。
- **预算 commit 即消费**：失败路径不退款；位移启动失败补了指名日志（`ActionMovementOwned / MontageRootMotionOwned`）；失败/取消收尾**显式停蒙太奇**（引擎语义：`EndTask` 不停蒙太奇）。
- **尾段落地**：源以 FreeFall 结束后由本 GA 接棒绑 `LandedDelegate`，尾段内触地当帧认领落地表现（与自由落体旗标互斥）。

#### PIE 第 3 轮校准（2026-09-22）：锁改 tag 形态

**用户定义校准**（问答原文归纳）：最早可操作帧 = 空中动作**锁定期的终点**；锁定期锁且只锁 **空回 + 攻击**（空中的动作只有这两种）；位置就在那几个空中蒙太奇的 **`IG_AerialHandoff` 通知**；机制 = **「用 tag（handoff 和被打断、蒙太奇/ga 中止时释放），在空中招式的 combo 表里加上 block tag 即可」**。

（第 3 轮落过一版 `Combat.State.Aerial.InputLocked` 锁 tag —— **未等 PIE 就被第 4 轮的机制再校准取代**，见下。定义不变，极性翻转。）

#### PIE 第 4 轮前的机制再校准（2026-09-22）：闸门翻转为 **Falling 正判据**

**用户校准**（原话）：「锁感觉可以复用现有的 flying 和 falling」→「空中动作的 ga 的要求由 block 某些 tag 改为**检查是否有 falling tag**」→「falling 难道不是在那个 handoff 通知之后就给吗？」。事实核查后按其心智模型重做：**falling 复用的是 tag 体系，不是现成的领取时机** —— 今天 `Combat.State.Aerial.Falling` 是托管下落**整包**的 Pose token（`BeginAerialFalling`：tag + 2.42g + 下落 montage + 看门狗），领在 **GA 结束**（EndAbility 的 `bStartFreeFall` 分支），`bWasCancelled` 一律跳过 ⇒ 直接翻转会误锁四个窗口（让位窗、舞踏触地类全程、移动离地、中止滞空）。故拆两层：

**闸门层**（裸 `Combat.State.Aerial.Falling`，单槽 `PoseTokens.AerialFalling`）：

| 事件 | 动作 | 落点 |
|---|---|---|
| **handoff** | **领** | `NotifyAerialHandoff`（与 CMC 的 Flying→Falling 同拍）+ poke 补发 |
| **离地且无已注册 Action** | **领**（含上升段 —— 用户拍板） | `SetGrounded(false)`；有 Action 在场（起手段）**跳过** |
| **中止滞空且无已注册 Action** | **领** + poke 补发 | `InsectGlaiveAbility::EndAbility`（`bWasCancelled` 且非地面，Super 之后） |
| **新动作起播**（蒙太奇挂 handoff 通知） | **放**（lead = 锁） | `DetectAerialHandoffNotify` authored 分支 → `Host->ReleaseAerialFallingState()`（停落下落表现、拆看门狗、**不 Restore 物理**） |
| 落地 / 看门狗 | **放** | `HandleLanded` / `HandleAerialFallingWatchdog`（既有） |

**表现层**（整包）：`BeginAerialFalling` 时机一字不动（GA 结束的 `bStartFreeFall` 分支），单槽 re-acquire `[Falling, Style]`。`IsAerialFalling()` 语义**保持**「托管下落表现会话」（改看 `ActiveAerialFallingMontage`）—— 摇杆屏蔽（`MHGZCharacter` 的 `bAerialPresentationLocked`）与落地收尾判据不受裸 tag 影响。

**查闸两处（全正向）**：空回 GA **`ActivationRequiredTags` = [Falling]**（`ActivationBlockedTags` 只留 `CantDodge` 预算）；`DA_IG_Combo` 撑杆跳 10 行 **`RequiredTags` = [Grounded]**（原 `[Unsheathed]` 保留合并，回读 10/10；撑杆跳只能地面起手）——**未来空中攻击行**进来时 `RequiredTags` 带 `Combat.State.Aerial.Falling`。`InputLocked` 整套退役（ini / token / 行 block tag / e2e 项）。

**输入行为表**（统一语义，无策略开关。**空回没有预输入** —— 2026-09-23 用户拍板取消，锁定期按下直接作废）：

| 场景 | 行为 |
|---|---|
| 舞踏 0–0.316 按 A | **作废**（无缓冲、无补发）—— 0.316 后要再按才出 |
| 撑杆跳 **0–0.783** 按 A（含起手段） | **作废** —— 0.783 后要再按才出 |
| 撑杆跳 0–0.783 按 Y 等攻击 | **不出**（lead 里 Falling 缺席；撑杆跳行 require Grounded 也拒） |
| 0.783 后按 A | 立即出空回 + `Superseded` 收撑杆跳（Falling 已在场） |
| 空回自身 0–0.65 再按 A | `CantDodge` 在场 ⇒ 吞 |
| 锁定期里动作被打断 | **中止即领** Falling，**之后的按下**立即可用 |
| **走落台阶 / 起跳离地（含上升段）** | 无 Action ⇒ 离地即领 ⇒ 按 A **立即出**（用户拍板） |
| 143 系统托管下落按 A | 整包在场 ⇒ **立即出** |

（旧 `InputPolicy` 三案枚举、`InputLocked` 锁 tag、空回预输入槽均已退役。）

**⚠ 第 5 条是 PIE 第 1 轮踩出来的**：`CanActivateAbility` 早于 `ActivateAbility`，此刻
`GetWeaponActivationContext()` 还是默认构造的空上下文 —— 在那里读快照判据＝恒 false＝静默拒绝一切。
五个入口的**分工**：ASC 活 tag 判据留 `CanActivateAbility`，快照判据放 `ValidateActionDependencies`。
完整教训见附录 A.1 第 17 条。

#### 空回下坠/落地（157/148）+ 取消预输入（2026-09-23）

**下坠表现**：空回后坠按真值走 **157 的 clip**（`AS_Unsh_Fall_W_Jump` = `WhiteAerialFallMontage`
= `AM_IG_AerialFall_W`，二进制名表核实归属：`AM_IG_AerialFall`←143、`_W`←157、
`AM_IG_AerialLanding`←148）。新增 `GetAerialDodgeFallMontage()` 固定返回 157 ——
**刻意不借 `bEnhancedVariant` 选槽**（那个旗标一手挑 clip 一手挑重力，空回要 157 的 clip
但重力仍是非白灯档 2.4246，真值无/有白灯逐值相同）。`BeginAerialFalling` 加
`MontageOverride` 参数承载。下坠**运动**不做（= 弹道的延续，遥测 R² 0.9999 已验证）。

**落地**：动作 clip = 148 `AS_Unsh_FallDown_Jump` —— **用户已重烘** `AM_IG_AerialLanding`
（0.5833 s = 1.1667 @rate2.0，e2e 钉长度）。**水平重设 337**（缺口 §2）落在
`PlayAerialLandingVisual` 开头 = 认领落地表现那一刻（= 148 首帧）：方向保持、近零速取朝向；
走落台阶这类不认领落地表现的触地**不重设**（真值里 148 只覆盖真正的落地动作）。
自动化 `MHGZ.M5.Aerial.LandingHorizontalReset`（900/300 入速 + 朝向回退）。

**取消空回预输入**（用户拍板）：拆掉 `BufferedAerialDodgeInput` 整套（入槽/重放/poke×2/
`AerialPreInputLifetime`/`FindAirDodgeTemplate`），`TryBufferDirectInput` 的空中分派臂改为
直接 `false`（地面 DodgeAccept 腿不动）。行为表已改「按下作废」。

**缝上速度骤变的根修 —— 状态：⚠ 症状已消、写点未修（2026-09-23 24:00）；下列「窄门控」已于 22:50 按用户要求整体删除，仅存历史**：
来源 = `AnimSync.cpp:98/102`（同步组各资产播放器按根运动权重往代理累加，权重不满时幅度 ≈w² ⇒「标志为真、位移近零」）；
实测分离干净：空中根位移 29/29 ≤0.0072 cm/帧，合法根运动 1–7 cm/帧全在地面帧。
当时实现的窄门 = 项目 CMC 的 `TickCharacterPose` 里对 `|Δroot| < ε` 的帧 `RootMotionParams.Clear()`
（⇒ 动画根分支整段不执行；不碰旋转、不依赖会失效的 Host 委托绑定），开关
（⚠ 已删）曾用开关 `mhgz.Aerial.NeutralizeNegligibleAnimRoot`（默认 0）、`mhgz.Aerial.NeutralizeEpsilonCm`（默认 0.05）。
**现行空回接缝做法是表现交棒配方**（`bEnableAutoBlendOut=false` + `EndAbility` 去掉守卫 + 任务在播到长度时上报，详见[空回 P0/P1 方案](空回P0-P1完整修复方案.md)）：缝那帧槽被填满，`20260923-235744` 缝上 7/7 保持约 900 cm/s。共享 CMC 写点未动，不把它写成通用根运动机制已修复。下面是历史（第一版已回退）：
137 蒙太奇结束那一帧，水平 900 被抹成 ~0、竖直照走重力（遥测逐帧：vX 899→−0.14、vZ 残差 0.3）。
**不是本轮回归**：旧录制（`20260922-224312`）每条自由落体缝同样抹零，此前比对窗口截在 1.185 s
没照到缝外。**机制**（子阶段记录逐位证实，见附录 A.1 第 25 条）：淡出帧 `RootMotionParams.bHasRootMotion`
翻真、根位移近零，`PerformMovement:2870` 把 XY 换成 `AnimRootMotionVelocity≈0`（Falling 保留 Z）。
**修的第一版（根盾通电）** 实测止住了缝，但根盾作用面过宽（空中一律丢掉动画根位移与旋转），
用户 PIE 判定「没修好还把原来好的弄坏了」⇒ **已回退**（`RebuildRuntime` 重绑与 M2 断言删除；
`Unbind` 日志与 P1-3 记录层保留）。**当时的下一版（窄门控）已实现并在 22:50 删除**。`MHGZ.M5.Aerial.AnimRootMotionShield` 与 `PresentationClipsHaveNoRootMotion` 只能作辅助钉，不能代替真实交棒回归。
取证、排除与回退记录见附录 A.1 第 25/26/27/28 条，以及[空回 P0/P1 方案](空回P0-P1完整修复方案.md)的历史执行记录。

### 8.2 其余五招（未开始）

除空中回避外，另五招的 C++ 类、GA 蓝图、蒙太奇、Combo 边、空中输入 chord —— **一个都不存在**。
源序列已导入，但序列 ≠ 蒙太奇。按项目规则**逐个纵切**，不要一次写完：

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
- **交棒仍是墙钟**：`UAbilityTask_WaitDelay(JumpDuration)`（`MHGZPoleVaultAbility.cpp:795/:826`）。拆成两条蒙太奇之后蒙太奇里已无段边界，所以只剩这半条（见 §6）。
- **舞踏回退蒙太奇（可选整洁项，2026-09-21 复核后判定「不必做」）**：`StartAdvancingCounterVaultVisual`（`MHGZAdvancingCounterAbility.cpp:342`）在 `DanceVaultMontage` 加载失败时走 `UAnimMontage::CreateSlotAnimationAsDynamicMontage`，而该函数在 UE 5.6 **声明了 `InPlayRate` 却从不引用**，所以回退路径**无法改速率**、姿势会与 `DanceVaultDuration` 脱钩。后撑杆跳已示范正确写法（`NewObject<UAnimMontage>(this, NAME_None, RF_Transient)` + 手工填 `FAnimSegment`，`MHGZPoleVaultAbility.cpp:746`）。**结论：不照抄。** ①这条回退只在资产丢失时才会走，而 `/Game/Weapons/InsectGlaive` 已在 `DirectoriesToAlwaysCook`（`Config/DefaultGame.ini:42`），录制里一次都没走过；②头文件注释（`MHGZAdvancingCounterAbility.h:66-84`）已经把「这条路不能改速率」写明并引了引擎行号，陷阱已被文档堵住；③照抄等于把一份手工建蒙太奇的代码复制成第二份。**真要做，该做的是删掉回退、让缺资产变成硬报错**（连只喂它的 `DanceVaultSequence` / `DanceVaultAnimationPlayRate` 一起删）。
- **自动化空缺**：`MHGZ.M5.Movement.OwnershipAndCleanup` 从未实例化过 Task 本体。

## 10. 阶段 F — M5 签核

Development Editor 全量编译（新增反射字段**不得用 Live Coding 验证**）→ 命名自动化套件带硬计数 → commandlet 资产审计带计数 → DataValidation 冷启动资产数 → `Saved/RuntimeTelemetry/<timestamp>` 录制 → **用户 PIE 目视确认** → 文档状态行回写。
**必须重新计数 `MHGZ.M4` / `MHGZ.M5`，不得复用过期数字** —— 全量最近一次是 **2026-09-18（98 / 97 通过 / 1 失败）**；**2026-09-21 只重跑过 `MHGZ.M5`（11/11）；2026-09-23/24 又重跑过 `MHGZ.M5`（28/28）**，全量仍未跑，所以签核时必须重跑。

---

# 第三部分：真值源与口径

## 11. Rise 真值审计（**唯一真值源**）

> **2026-09-18**：逐段/逐招式的原始真值已迁到 **`docs/reference/真值表.md`**（由 `Scripts/MHRise/build_truth_table.py` 从 22 份可用录制生成），完整口径见 `docs/reference/README.md`。**本节只留「配置对照」这张验收表与几条判据性的结论。**

### 数据约定

> **权威版在 `docs/reference/README.md`**（192 行，含生成器口径与完整字段说明），本节只是摘要。
> **冲突时以那边为准** —— 两份抄同一件事迟早会漂移，改口径请改那边并回来同步这里。

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
- **不做空中回避的无敌帧**：真值源里没有任何 i-frame 数据，无从对齐，不能用「地面翻滚有、所以空中也有」去补。
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
16. **"速度方向 = 朝向"恒成立，分不开"不转向"与"一按就瞬转"。** 真值表那列 `|方向−朝向| = 0.0°` 两种机制都会给出这个值（若角色在动作起始那一帧就转到目标方向，之后每帧的速度方向自然等于每帧朝向）。我照它把**八向的空中回避**设计成了「方向取 `ActorForward`、`RotationPolicy = Locked`」，被用户一句「空中回避是 8 向的」打回。**要判转向必须看 `facing_yaw_deg` 的帧间差**：实测段内 `facing[段尾]−facing[段首]` **213/213 段恰好 0.0°**，而段首相对前一帧 **中位 82.3°、最大 178.7°、69% >30°** ⇒ 一帧内瞬转。**还要排掉相机滞后**：这些时刻 `cam_yaw_deg` 的逐帧变化中位/90 分位/最大**全是 0.0°/帧**，所以那 47% 的偏轴是真的（方向是**连续**的原始摇杆角，不量化到 8 扇区：偏离最近 45° 方位 >11.25° 的占 47%，最多 ±22.4°）。
17. **激活闸门里不许读激活上下文。** `GetWeaponActivationContext()` 返回的成员 `ActivationContext` 是在 `ActivateAbility` 里才从 `PendingActivationContexts` **消费**进来的（`MHGZGameplayAbility.cpp:187`），而 `CanActivateAbility` 被 GAS 调用的时机比它**早一步** ⇒ 读到的永远是默认构造的**空上下文**，`ContextTags` 为空 ⇒ 每个快照判据恒 false ⇒ **静默拒绝一切**（连 `ValidateActionDependencies` 都跑不到，所以一行日志都没有）。2026-09-22 PIE 的「怎么都按不出来」就是这个：输入侧 65 次 `Input.AirDodge` 全部解析正确、`AM_IG_AirDodge` 零出现、`[AirDodge]` 零命中。**分工**：ASC 上的活 tag 判据留 `CanActivateAbility`，快照判据放 `ValidateActionDependencies` —— 照 `UMHGZDodgeAbility`（它只读 `ASC->HasMatchingGameplayTag`）。**并且**：每个静默 `return false` 都值得一行 `UE_LOG`，这次是靠录像反推根因的，成本远高于一行日志。回归测试 `MHGZ.M5.Aerial.AirDodgeCanActivateIgnoresActivationContext` 把这条钉住（没有待消费上下文时也必须放行）。
18. **「MontageInstances 里一串 0.15~0.22 s 的闪断实例」= 能力自杀但蒙太奇未停。** 2026-09-22 PIE 第 2 轮的 4 个 burst（31 个实例）是这么读出来的：每个实例活到**下一次按压**（被 PlayMontage 抢槽顶掉）⇒ 每次按下都重新激活 ⇒ 能力每次都当场死（位移所有权互斥 `AcquireActionMovement` **静默**拒绝 → `FinishMovement(Failed)` → `RequestEndAction`）；而 `EndAbility` 的显式 `EndTask` **不会**停蒙太奇（引擎：`OnDestroy(false)` 不走 `StopPlayingMontage`，先 `EndTask` 再 `Super` 连 `TaskOwnerEnded` 都轮不到它）⇒ 闪断残留。burst 终点与持权动作的源时长逐个对齐（舞踏 1.6167 s，4/4）就是「谁占着位移所有权」的指纹。两条硬结论：**① 位移失败必须打日志**（现已补 `!DidStartMovement()` 分支）；**② 失败/取消收尾必须显式 `Montage_Stop`**。而「预算 acquire 排在位移启动之后」会让失败的激活不记 CantDodge ⇒ 无限重按 —— 预算必须 **commit 即消费**。
19. **CDO 运行期改写不传实例；惰性对象不能跑收尾链。** 自动化里给 GA 装配资产，别用 `GetDefaultObject()->AttackMontage = X` —— 实测激活出的实例读不到该改写（改授予生产蓝图类 `GA_IG_AirDodge_C` 后一切正常）。另外 `NewObject` 一个从未激活的能力去调 `HandleXxxForTest` 这类会走 `RequestEndAction → EndAbility` 的路径，会踩引擎 `ensure(CurrentActorInfo)`（`GameplayAbility.cpp:1257`）—— 要测收尾链就**真实激活**（`MHGZM5AirDodgeTests.cpp` 的 `ActivateRealAirDodge` 是现成脚手架）。
20. **「哪些输入被锁」是数据问题，别在单个 GA 里手写判定。** 2026-09-22 PIE 第 3 轮「撑杆跳的最早可操作帧没生效」的根因是形态：我把锁做成了空回 `CanActivateAbility` 里的一段实例扫描谓词 —— 攻击侧零闸、且判定逻辑长在错误的层。正解（用户一句话给出）：**锁是 tag**，查锁用现成机制（GA 的 tag 要求 + `DA_IG_Combo` 行的 tag 要求）。范围判据（「空中的动作只有空回和攻击」）落在**数据行**上，零判定代码。（当时落的是 block 正向锁 `InputLocked`；同日第 22 条翻成了 Falling 正判据 —— 「查锁落在数据侧 tag」这半条教训不变。）
21. **UE Python 改结构体数组：`get_editor_property` 的数组代理写回是 identity-no-op，改动静默丢失。** 必须 `list()` 包一层再改元素、`obj.modify()`、`set_editor_property("xxx", list)`、`save_asset(only_if_is_dirty=False)` —— 照 `setup_air_dodge_input.py` 的配方。诊断特征：**同一元素对象上 set 立即可见，数组写回后 re-get 不可见**（`Saved/_mhr_scratch/debug_tag_write.py` 就是为此写的）。丢起来无声无息，**每次改完必须独立 re-load 回读计数**。
22. **状态 tag 与表现整包要分层；用户说「X 之后就给 tag」时，先查实现的领取点是不是真的在 X。** 2026-09-22 机制再校准：`Combat.State.Aerial.Falling` 的领取被绑在 `BeginAerialFalling`（整包：tag + 重力 + 下落 montage + 看门狗）里、时点在 **GA 结束** —— 于是「handoff 后就能按空回」在实现上根本不成立（让位窗/舞踏/离地/中止四个窗口全被误锁），而用户的心智模型（**handoff 之后就给**）才是对的。正解：拆两层 —— 裸 tag（=「空中可操作」闸门，handoff/离地/中止领、新动作起播放）+ 整包（表现，时机不动）；闸门用**正判据**（`RequiredTags` 查在场）而不是 block 反判据，锁 = 领取点缺席。顺带两条硬事实：`RequestEndAction(Superseded)` ⇒ `bWasCancelled=true`（让位者不抢发整包）；UE 的 loose tag **是计数的**（`GetTagCount` 还会把子 tag 聚合进父计数 —— M1 的台账断言就是这么被裸 Falling 的子计数顶翻的），`FWeaponRuntimeTagLedger` 的 entry 级 Release 由引擎计数兜住，**单槽 re-acquire 是为了让槽永远单一属主、放点可推理**，不是引擎必需。
23. **UE RuntimeTelemetry 比对三坑（全部会静默给出「看似精确、实则错误」的数）。** 2026-09-22 遥测比对实测：① `Velocity*` 列的**段首样本是动作生效前的旧值**（空回发射帧 vz=−1725、次样本 1152）——整列线性拟合会把 v0z 从 1182 拖到 954、R² 掉到 0.7，**主口径用位置二次拟合**（实测 R² 0.99998），速度列掐首样本只做旁证；② 落地窗口判 `"Combat.State.Aerial" in OwnedGameplayTags` 是**子串误配**（`Aerial.Landing/CantDodge/Falling.*` 都命中），落地表现的 ~0.45 s 会并进下落窗口、整段二次拟合 g 从 −2355 漂到 −1072 —— 必须 split 后**精确集合**判定（`extract_vault_takes.py` 与 `compare_air_dodge.py` 都修过）；③ 试次要按真值侧同款验收**剔污染**：坠崖（|hEnd|>30）、转身（yaw_span>5）、**空回让位串窗**（撑杆跳 0.783 被空回接管 ⇒ 顶点 285+293≈578、距离翻倍，伪装成「干净但翻倍」）。另：`FTimerHandle::Invalidate()` **不撤定时器**只丢句柄，disarm 必须 `ClearTimer`（看门狗 `elapsed=-1` 假报警就是它）。
24. **表现参数别让一个旗标双挑；预输入是手感决策不是技术决策。** 2026-09-23：① `BeginAerialFalling(bEnhancedVariant)` 一手挑下落 clip、一手挑重力档 —— 空回后坠要 157 的 clip（`AS_Unsh_Fall_W_Jump`）却要非白灯重力 2.4246（真值无/有白灯逐值相同），一个布尔表达不了 ⇒ **clip 与物理变体解耦**（`GetAerialDodgeFallMontage()` + `MontageOverride`）。归属判据别信长度（143/157 的 clip 同长 1.7 s），**二进制 grep uasset 名表**一查一个准。② 空回预输入整套被用户取消（锁定期按下作废、放锁点不补发）——它当初是「复用项目预输入」的顺手决定，真值手感是**到可操作帧再按才出**；预输入这类缓冲层动手前先问手感，别当免费的容错。
25. **动画根运动是速度小偷：恒定根也会抹掉 CMC 的 XY —— 空中要把「运动权」从动画根手里收走。** 2026-09-23 PIE 第 5 轮「空回下坠途中速度骤变」：137 蒙太奇结束那一帧 vX 899→−0.14、vZ 匀加速照走、胶囊与 RootBone 水平同步冻住（位置二次差分交叉定位）。机制（引擎源码逐环核实，且与项目旧案互证 —— `MHGZAttackAbility.cpp` 里 `AM_IG_TuJinHuiXuan` 淡出期提取根运动、点亮 `CMC->RootMotionParams.bHasRootMotion` 盖掉 RMS 的事故）：任何 `bEnableRootMotion=true` 的 clip 在激活的资产玩家/蒙太奇里被 `Accumulate`（**`FRootMotionMovementParams::Set` 对单位变换不设零检查** —— 恒定 (0,0,−111) 根每帧 `Accumulate(identity)` 也置真 `bHasRootMotion`），CMC 的 `ConstrainAnimRootMotionVelocity`（`CharacterMovementComponent.cpp:2115`，Falling：XY 换成动画根速度、Z 保留）就把水平抹成 ~0。**修（架构 + 卫生双层）**：① Host 的 `ShieldAerialAnimRootMotion` 挂 `CMC->ProcessRootMotionPostConvertToWorld`（该委托只包动画根的 local→world 转换，RMS 源不经过）—— 非地面或托管表现窗口内把根位移换成 `Velocity*Δt`、旋转换成单位，Constrain 的替换变**恒等**，不再对「谁点亮 bHasRootMotion」设任何假设；地面 locomotion（Walk/Dash 一族 `bEnableRootMotion=true`）是合法驱动，放行。② 命令列 `MHGZStripPresentationRootMotion` 对表现 clip（143/157/148 ∪ 四条蒙太奇段引用）写 `bEnableRootMotion=false` + `bForceRootLock=true`（根骨钉首帧，A9 观感不变 —— 撑杆跳 `AS_Unsh_Jump_Over*` 早有同型配方）+ `bRootMotionSettingsCopiedFromMontage=true` 并清蒙太奇 DEPRECATED 旗标（`EnableRootMotionSettingFromMontage` 仅在 copied 标记 false 时写入，双保险断 PostLoad 强制回灌）。**归因未钉死的坦白（经对抗校验 + 散布反证后仍成立）**：852 缝的确切触发点没抓到。对抗校验提出的备择「正面挡墙 + PhysFalling 阻塞命中分支的速度重写」被**散布反证否掉**：33/33 个 take 全在「蒙太奇结束 +1 帧」塌缩（时间确定 = 代码在收尾链），坐标却散布全场（新录制 (603,166)/(−643,404)/(191,−180)…旧录制 (957,−839)/(−26,−275)…），且塌缩后仍**自由下落 5 帧**（Z 一路 −1568→−1865）——贴着任何表面都不可能。⚠ ~~反方向，Constrain 说也被打伤~~**（该反证已作废 —— 见本条末与附录 A.2：`ConstrainAnimRootMotionVelocity` 后被 19:35 的子阶段记录逐位证实就是写手；「段级闸门」那几句只说明蒙太奇轨不供能，不构成对 Constrain 的反驳）**：四条 clip `enable=false`（段级闸门连 identity 都不 Accumulate），`MontageHasRootMotion`/`ProxyHasRootMotion` 全程 0，下坠窗 ForceMMIdle。**「残差 = ΔLocation/dt」不构成任何机制的证据** —— 模式尾部 `Velocity = (Location−OldLocation)/dt` 重推导使它恒成立。已钉的硬事实：148 修复前 `enable=true` 是**落地缝**的真实漏点（337 重设被吃出 301）；全库 169 条 clip 115 条 `enable=true`。**2026-09-23 19:35：机制**确认**（本条成立），但**当时的根盾从没通电** —— 真因是绑定生命周期。**
子阶段记录（下一条）在断点帧给出了无歧义的读数（录制 `20260923-193250-…-55480`，断点 820）：
`CMC.HandlePendingLaunch.Post v=(716.3779, -544.8013, -1507.9955)`（vXY 900，`HasAnimRootMotion=0`）
→ **`CMC.ConstrainAnimRootMotionVelocity`：`callSite=None`（即 `PerformMovement:2870`），
`RootMotionParamsTranslation=(-0.0077, 0.0059, -0.0000)`、`AnimRootMotionVelocity=(-0.3026, 0.2301, -0.0000)`，
`ConstrainInput=(716.3779, …) → ConstrainOutput=(-0.3026, …)`**
→ `CMC.StartNewPhysics.Pre v=(-0.3026, 0.2301, -1507.9955)`（XY 已被换掉、Z 保留）。
即 **`RootMotionParams.bHasRootMotion` 恰在淡出帧翻真、位移近零 ⇒ `Constrain` 把 XY 换成 ~0**，
与 `A.25` 原判一致（`AnimRootMotionVelocity = RMPtrans/Δt` 与断点后速度逐位吻合）。

**上一轮我写的「动画根被证伪」是错的，两条「证据」都无效**：① 「根盾 0 次回调」——**盾当时根本没绑上**
（`RebuildRuntime` 漏了重绑，见 A.1 第 28 条）：PIE 起了一次 `bound`，第一次武器身份变化走
`TeardownRuntime`（`Unbind`）→ `RebuildRuntime`（**不重绑**）⇒ 整局盾都是死的，日志因此只有
`bound` 没有 `shielding anim root`。新遥测里 `RootMotionShieldBound` 列在**每一个**样本上都是 0，
这才是它没通电的直接证据（旧证据只说明「回调没被调」，推不出「分支没进」）。②
「`ProxyHasRootMotion=0`」——**正是我自己记录过的采样陷阱**（代理值在 `PostUpdateAnimation` 里被
`Clear` 之后才采样，恒 0），却被我当成反证用了。⇒ 教训写进第 28 条。
**修的第一版（19:35）**：`RebuildRuntime` 补 `BindAerialRootMotionShield()`（幂等）＋ `Unbind` 补一条日志；
回归钉 `MHGZ.M2.Equipment.SameWeaponArmorSocketAreNoOp` 增加「换武器重建后盾仍绑在本 CMC 上」断言。
**19:55 用户 PIE 判定：缝止住了（19:42 会话无 `900→0.3`，唯一空中塌缩是 `BlockingImpact=1` 的真实撞墙），
但「把原来好的弄坏了」⇒ 该版已回退**（`RebuildRuntime` 的重绑与 M2 断言删除；`Unbind` 日志与 P1-3 记录层保留）。
**根盾的作用面过宽**：它挂在 `ConvertLocalRootMotionToWorld`，只要「非地面或托管表现窗口」就把动画根的
位移换成 `Velocity*Δt`、旋转换成单位 —— 日志实测它**丢掉了真实根位移**
（`in=V(6.06,-3.55,0) out=V(5.43,-7.03) v=V(215,-278)`，~351 cm/s 空中运动时每帧替换）。
**当时的下一版候选（已实现为窄门控，随后于 22:50 删除；现行空回交棒见 P0/P1 方案）**：只对**近零**根位移替换（本例噪声 ~0.3 cm/s vs 合法 50~240 cm/s，差三个数量级，ε 可分），
门用「空回/托管窗口」这类窄条件而非「只要在空中」，并保留旋转与合法根位移；
⚠ **不可直接返回单位变换**（`Constrain` 会拿到 `0/Δt = 0`，等于亲手抹掉 XY）。

26. **帧级遥测不够用时，把 CMC 的子阶段本身变成记录点 —— 先让证据指名函数，再改行为。** 2026-09-23 空回缝（137→157 交接帧 XY 900→0.3，Z 照走重力，无 `HandleImpact`、无有效源）用帧边界 `PerformMovement.Pre/Post` 只能给定「在这一个调用里」，反推了半轮都在猜。做法：在项目 CMC 子类里 `override` 全部**可改写 Velocity 的子阶段**并各记 Pre/Post —— `RestorePreAdditiveRootMotionVelocity`、`CalcVelocity`（带实际 `Friction`/`BrakingDeceleration`）、`ApplyRootMotionToVelocity`、`PhysFalling`、`ApplyAccumulatedForces`、`HandlePendingLaunch`、`UpdateCharacterStateBeforeMovement`、`StartNewPhysics`；再 `override` **const** 的 `ConstrainAnimRootMotionVelocity`（用 `mutable` 缓冲 + 用一个 `ActiveMovementSubPhase` 成员标记「当前在哪层 override 里」，于是每次 Constrain 投票都自带调用点）。每条样本带 `RootMotionParams.bHasRootMotion`（**唯一合法的采样时点就在帧内**）、`AnimRootMotionVelocity`、`IsPlayingRootMotion()`、`CurrentRootMotion.bIsAdditiveVelocityApplied`/`LastPreAdditiveVelocity`、`ProcessRootMotionPostConvertToWorld.IsBound()`。若相邻两条的 vXY 不同，写手就被夹在一个具名函数里。配套三件：① 只在遥测开 + `IsFalling()/IsFlying()`/托管表现窗口内采样（别全程逐帧）；② `PerformMovement` 里那条既有的「空中平面速度骤降」告警**把本次调用内所有子阶段逐行 dump 到 MHGZ.log**（就地读，不必等 CSV）；③ 三条记录点必然踩到的引擎事实 —— **`RootMotionMode=RootMotionFromEverything` 时 `ACharacter::IsPlayingRootMotion()` 恒真**（项目 AnimInstance 正是 2），于是 `HasRootMotionSources()`（`IsPlayingRootMotion() && GetMesh()`）也恒真、`PerformMovement` 每帧都进根运动块并调 `TickCharacterPose`（`CharacterMovementComponent.cpp:2747/2819/2824`），但**这不等于每帧真有根运动** —— 真闸门是 `ConsumeRootMotion().bHasRootMotion`（源自代理的 `GetExtractedRootMotion()`，`AnimInstance.cpp:735`）。别把「模式允许」当「本帧提取了」。
27. **同一份数据可以先把「不是谁」钉死 —— 排除项也是交付物。** 2026-09-23：断点帧的 `MovementPhases.csv` 已直接否掉三类候选（`Acceleration=0 && BrakingDecelerationFalling=0 && FallingLateralFriction=0` ⇒ `CalcVelocity` 是空操作；`ActiveRootMotionSourceCount=0 && HasOverrideRootMotionVelocity=0` ⇒ 源覆盖没参与；盾回调 0 次 ⇒ 动画根分支没进），`RootMotionSources.csv` 的 `FinishVelocityMode=0` 又否掉源结束写速。这些**在改代码之前**就能从既有录制里读出来，代价是几个 `awk`。教训：排查「谁改了 X」时，先枚举所有**能**改 X 的语句，再逐条用已落盘的列去否 —— 比接着收集新证据快得多；剩下的候选集才是要上仪表的。

28. **「某个回调没被调用」不能证明「那条分支没走」——先证明仪器通电。** 2026-09-23 我拿「根盾 0 次回调」当证据否掉了动画根，结论错了。真实情况：盾挂在 `CMC->ProcessRootMotionPostConvertToWorld`，而 `TeardownRuntime` 里 `Unbind()` 之后 `RebuildRuntime` **没有重绑** —— 所以只要发生过一次武器身份变化（`TeardownRuntime(WeaponChanged)` → `RebuildRuntime`），盾就整局失效：日志里只有初始化那一条 `bound`，此后再无任何回调。**这类「绑定类修复」的失效特征是静默的**，且它与「机制不成立」在日志上长得一模一样。做法：① 仪器要**自证在岗** —— 我把 `ProcessRootMotionPostConvertToWorld.IsBound()` 逐样本写进遥测（`RootMotionShieldBound` 列），一眼就看到它全程 0；② 会 Unbind 的地方**必须配对打日志**（`[AerialRootMotionShield] unbound from CMC …`），这样生命周期在日志里可审计；③ **凡是「TearDown → Rebuild」双段式生命周期，绑定/订阅/委托必须在两段里都建立**（`InitializePawnRuntime` 与 `RebuildRuntime` 是同一套初始化义务的两个入口，漏一处就是静默失效）。附带一条推论：**排除项要写清它依赖的前提**（「回调没被调」依赖「回调已绑」），否则前提一破，排除表就变成错误结论的背书。

29. **`LogRootMotion` 声明级别是 `Warning` —— 引擎自带的根运动叙述平时全静音，别把「日志里没有」当成「没发生」。** 2026-09-23 追「谁把 `RootMotionParams` 点亮」时，日志里搜 `WorldSpaceRootMotion` / `FAnimMontageInstance::Advance ExtractedRootMotion` **一条都没有**，一开始差点当成「动画根分支没走」的旁证（与第 28 条同型的误判）。真相：`DECLARE_LOG_CATEGORY_EXTERN(LogRootMotion, Warning, All)`（`EngineLogs.h:17`），而这些叙述是 `UE_LOG(LogRootMotion, Log, ...)` ⇒ 默认被过滤。做法：**判因期用 `log LogRootMotion Log`（或代码里 `FindConsoleVariable("LogRootMotion")->Set(TEXT("Log"))`）打开**，它是零代码的飞行记录仪，直接打印「哪个蒙太奇抽取了多少根运动」与「CMC 帧内应用了多少世界空间根运动」。配套两条本轮读清的源码闸门（用于判断哪条通道**可能**供能）：蒙太奇轨 = `AnimMontage.cpp:2486` `bExtractRootMotion = (OutRootMotionParams != nullptr) && Montage->HasRootMotion()` + `2580` `!IsRootMotionDisabled()` + 权重 `Blend.GetBlendedValue()`（⇒ `MontageHasRootMotion=0` 时该路不通）；动画图 = `AnimInstance.cpp:735-741` 把代理的 `ExtractedRootMotion` 并入 + `753-755` `MakeUpToFullWeight()`（⇒ **全引擎没有任何 writer 写代理那个成员**，grep 只命中读取端与 accessor）。

30. **默认关的开关，必须先证明它「开过」，再谈「有没有效」——A/B 习惯缺一条正向对照。** 2026-09-23 22:30 我按用户提议做了「交棒提前一帧」（开关 `mhgz.Aerial.HandoffOneFrameEarlier`，默认 0，理由：便于同一 PIE 内做对照；⚠ **该开关与其实现已于 2026-09-23 22:50 整体删除**），用户下一轮报「问题依旧存在」。判读录制时先查了 `Saved/Logs/MHGZ.log` 的 `Cmd:` 列表：**只有 `mhgz.Telemetry.Enable 1/0` 与遥测自己打的 `log LogRootMotion Log/Warning`**，开关从来没被置 1，`[AirDodgeHandoff]` 日志 0 行 ⇒ 两轮录制都是对照轮，**那版实现从未被测过**，「无效」这个判定当时并不成立（同一批数据里缝的形态与改动前逐帧同型，恰好也是对照轮应有的样子）。做法：① 录制入库时**先 dump `Cmd:` 列表**当正向对照，缺了就当没跑；② 默认关的开关**不值得为它多花一整轮 PIE** —— 要么默认开（靠既有的回归判据把关），要么在同一个 PIE 里当场 A/B 并把两条 `Cmd:` 都留档；③ 这与第 28 条同型（那次是「回调没被调」依赖「回调已绑」），**都是让一个未验证的前提给结论背书**。

31. **交棒类缺陷，先问「收尾那一帧实例还在不在」，再问「谁先调了谁」——结构性保证胜过时序提前量。** 2026-09-23 空回缝：两边的收尾代码同构（都在 `EndAbility` 里 `Montage_Stop(0.05f)` 再 `BeginAerialFalling`，都由同一条 `TryFinishXxx` 门控 `移动结束 && 视觉结束`），差别只在一条旗标 —— 撑杆跳 `PlayVaultMontage` 里 `MontageInstance->bEnableAutoBlendOut = false`（终末姿势保持住，等 `EndAbility` 来淡出），空回保留默认 `true`（到自身长度自动淡出 → `Terminate()` → 完成回调落在实例已死之后 ⇒ `Montage_Stop` 成空操作 ⇒ **一帧没有任何蒙太奇**，姿势掉回底层图再半权重混入）。引擎依据：`FAnimMontageInstance::Advance` 的整段自动淡出由 `bEnableAutoBlendOut` 把关，`Terminate()` 只在 `IsStopped() && Blend.IsComplete()` 时发生 ⇒ 关掉之后 `OnMontageEnded`/`OnCompleted` **永远不会来**（本项目 A.2 里那条「`PushDisableRootMotion` 从未被 Pop」的推断已经撞见过这个事实）。做法：① 判据用**位置事实**（`Position >= PlayLength - 1e-3`）而不是提前量 —— 提前量随帧率与采样相位漂、事实不漂，且早/晚一帧都不会破（实例还活着）；② **`Montage_IsPlaying` 不能当判据** —— 保持终末姿势的实例 `bPlaying=false`，它会把「播完了但还活着」判成「不在播」；③ 收尾那行 `Montage_Stop` 的守卫（撑杆跳的 `if (!bVisualFinished)`）**不可照抄** —— 在本招「视觉完成」正是实例还活着的那一刻，漏掉它实例会以权重 1.0 留在槽里抢姿势；④ 现场一眼可验：`MontageInstances.csv` 的 `EnableAutoBlendOut` 列读的就是那条旗标。

32. **重写一个虚函数不等于它会被调用 —— 先查「谁负责调它、什么时候登记」。** 2026-09-23 空回交棒：在项目任务 `UAbilityTask_MHGZPlayMontageAndWait` 里写了 `TickTask` override，两版修法（提前一帧 / 播到长度上报）都挂在它上面，**两版都无效、且都是静默无效**。真相：`TickTask` 由 `UGameplayTasksComponent`（不是 ASC）派发，而任务的 tick 登记**只有一次、只在激活那一刻** —— `UGameplayTask::PerformActivation()` 里 `Activate()` 返回后即调 `OnGameplayTaskActivated()`，内部 `if (Task.IsTickingTask()) TickingTasks.Add(...)`（`GameplayTasksComponent.cpp:86-89`）；`bTickingTask` 是 `UGameplayTask` 的 protected 位、构造函数里置 false。**之后再置 true 是静默 no-op**，而且这个旗标实际是单向的（移除那侧也被同一个 `IsTickingTask()` 挡着）。做法：① tick 类任务在**构造函数**里置 `bTickingTask`（引擎三个先例：`AbilityTask_ApplyRootMotion_Base.cpp:17`、`MoveToLocation.cpp:16`、`WaitVelocityChange.cpp:12`）；② 症状「这个 override 好像没跑」时，先分清是**没被调用**还是**逻辑不对** —— 本项目自己的 `AbilityTask_MHGZWeaponMovement.cpp:67` 就是构造函数里置真的正例，一 grep 就能看出谁置过、谁没置；③ 与第 28 条（回调没被调 ≠ 分支没走）、第 30 条（开关没开 ≠ 无效）同族：**先证明通路通电，再评价行为**。这一例的特别之处是「通路」和「配置」两层同时失效，所以同一处连续两轮 PIE 都拿到「无效」的假象。

33. **判据要吃「不变量」而不是「会漂的量」；但引用不变量时要把它的前提也钉住。** 2026-09-23 空回收尾判据最终定为 `Position >= PlayLength - 1e-3`（引擎在末尾把位置夹到 `SectionEnd − KINDA_SMALL_NUMBER/2`）—— 事实不随帧率漂，比「剩余 ≤ 1.25×dt」这种提前量稳。但这条判据暗含一个**资产前提**：单段蒙太奇、且末段末端 == `GetPlayLength()`（引擎夹的是「当前段末端」）；重切成多段或非连续段，位置就永远够不到长度 ⇒ 判据永假 ⇒ 动作卡住（比原来的视觉缺陷更严重）。做法：把前提写成断言进同一条自动化钉（本例第 ③ 组：`CompositeSections.Num()==1` 且末段末端 ≈ `GetPlayLength()`），**让「假设失效」红出来而不是静默退化** —— 与第 27 条「排除项要写清它依赖的前提」是同一条纪律的反面用法。

34. **没被变异过的钉子只是假设 —— 「绿」不证明它咬得住。** 2026-09-23 给死代码那条写的自动化钉，我先按「注释里最重的警告」配了断言，但读完引擎源码才发现：`bTickingTask` 那个位**粘性、移除时也不清**，所以「旗标为真」挡不住「把置真那行挪到激活之后」这种同类改法（那正是注释里警告的事）；同时我配的第三条「末段末端 == `GetPlayLength()`」是**恒真断言**（单段时 `GetSectionStartAndEndTime` 内部就取 `GetPlayLength()`，`AnimMontage.cpp:270-283`），只制造覆盖率的错觉；而第二条「实例 `bEnableAutoBlendOut == false`」也分不清「是 C++ 关的还是资产本来就关」（实例位从资产拷来，`AnimMontage.cpp:1506`）。改法：① 断**登记结果**而不是旗标（`UGameplayTasksComponent::GetTickingTaskIterator()` 里查得到它）② 关键位断言「资产为 true 且实例为 false」两半，才能证明是代码关的 ③ 恒真断言删掉，换成断前提（段数）④ 把「测试世界不 tick」那条零覆盖链路用 `DriveTickForTest` 手动驱动一次。最后**真的把变异打进去跑一遍**：注释掉那行 ⇒ 钉子红 + 生产侧 `ensure` 式 Error 日志同时响；恢复 ⇒ 27/27 绿。**「我以为它会红」和「我见它红了」之间差着一次编译**（与第 30 条同族：都是拿未验证的前提给结论背书）。

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
