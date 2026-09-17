# M5「空中/舞踏/终结」：接缝修复 + 收尾计划

## Context

项目是 UE 5.6 + GAS 的原创虫棍单机 Demo，走 M0～M7 / E0～E7 的里程碑门禁。每个阶段必须同时有代码、资产接线、PIE 与遥测证据才算签核。

**当前真实位置：M4.7 已签核，M5 实施中但一项都没签核。** 用户当前卡在具体缺陷上，并在 2026-09-16 提供了一批 **Monster Hunter Rise 实机录制作为唯一真值源**，要求据此审计本项目的曲线设计。

**2026-09-16 的两个新 PIE 症状**：
1. **后撑杆跳坠落过程中模型会突然往右边移动一下** → **已定案（见 §5 A9 / A9-R）**：vault 蒙太奇的 JumpOver 段在**姿势**里累积了 26.7 cm（白灯 31.5 cm）根骨骼水平偏移，因为 `PushDisableRootMotion()` 有意不提取它；坠落片段是原地的（根骨骼为 0）；两者以约 **2 帧**交接，模型因此**向右跳 13.0 cm、向前 23.3 cm**。**与 Rise 逐点对齐后确认：这段横向在 MHRise 里真实存在（无白灯 6.6–9 cm、白灯 30–43 cm），动画是忠实还原，曲线的水平形状残差仅约弧长的 1.1–1.2%。所以不是数据问题，是「同一段位移被算了两次、又丢了一份」。** 此前怀疑的「曲线放大横向分量」「落地后转向过冲」均已降级。
2. **突进回旋斩反击舞踏上升到一半就掉下来，且落地动作时有时无** → 见 §4b：弹跳物理配成 3.5 m / 0.6 s，而 Rise 真值（动作 id 154，n=7）是 **6.14–6.37 m / 1.534–1.568 s**；表现蒙太奇 1.6167 s 反而接近真值，**动画对、物理错**。落地时有时无则来自 `EndReason == Landed` 不满足 `Completed`。

> **证据来源说明（重要）。** 本计划的缺陷结论分三批，可信度依次递增：
> - **§2 的 F1～F5**：纯源码阅读，**未经验证**。
> - **§2b 的 M1～M6**：来自 `Saved/RuntimeTelemetry/` 的 PIE 录制。
> - **§4b 的 Rise 真值**：来自 `MHGZ_AerialTrajectoryRecorder` 的实机录制，**是唯一真值源**。
>
> 三者冲突时以 §4b 为准。前两批已经各被推翻过至少一条（A3、A4b 的前提），**不要再把源码推断当已验证事实**。

---

## 1. 当前阶段（三档衡量）

| 档 | 内容 |
|---|---|
| **已签核** | M4.7：地面虫印、四连印斩、突进回旋斩及其精确窗口；反击消费 IncomingHit 并加一层 `AdvancingCounter` 舞踏（`docs/editor/verification.md:36`、`docs/design/milestone-gates.md:78`）。E5.1 木桩三色 Hitzone。 |
| **代码/资产已落地，未验证** | Host/Resource 层空中基座：`BeginAerialFalling`、`HandleLanded`、下落物理（重力 2.4246 / 白灯 2.5740）、pose token ledger、落地清舞踏（`MHGZWeaponRuntimeHostComponent.cpp:915-1001`、`:1158-1220`）。唯一位移执行层 `UAbilityTask_MHGZWeaponMovement`（四种 Source）。后撑杆跳 `UMHGZBackVaultAbility`（760 行）+ GA 蓝图 + 两个蒙太奇。 |
| **已签核（自动化）** | 仅 1 个：`MHGZ.M5.Movement.OwnershipAndCleanup`（`Source/MHGZ/ActionSystem/Tests/MHGZM5MovementTests.cpp:14-100`）。它只驱动 Host 的所有权 API，**从未实例化过 Task 本体**。 |
| **未开始** | 五个空中招式（操虫斩 / 强化操虫穿刺 / 强化跳跃斩 / 急袭突刺 / 降龙）的 C++ 类、GA 蓝图、蒙太奇、Combo 边、空中输入 chord —— **一个都不存在**。源动画序列已导入（CaoChongZhan / CaoChongChuanCi / TiaoYue / JiangLong 共 20 个），但序列 ≠ 蒙太奇。 |

只读审计无法确认的：`DA_IG_Combat` / `DA_IG_Combo` / `DA_IG_InputProfile` / `GA_IG_HouChengGanTiao` 内部的标量值与指派 —— 这些由阶段 0 的资产探针一次解掉，**不需要人工开编辑器**。

---

## 2. 已验证的结构性发现

**F1 — 舞踏「层数 → 伤害倍率」是一条断开的管道（已复核）。**
`URes_InsectGlaive::GetDanceDamageMultiplier()`（`Res_InsectGlaive.cpp:835`）**全树零调用方**；`MHGZDamageExecCalc.cpp:86-87` 读 `Damage.DanceMultiplier`，但 `MHGZAttackAbility.cpp:1282-1295` 的 `MakeDamageSpec` 只写 `Damage.MotionValue` 与 `Damage.BaseStagger`，全树无任何写入点。`MaxDanceStacks` 类默认 `0`（`InsectGlaiveCombatConfig.h:124`），`AddDanceStack` 被 clamp 到 0（`Res_InsectGlaive.cpp:810`），且二进制资产里这两个属性未序列化。→ **层数能加、能观测，但不影响任何伤害**；M5 退出条件「倍率封顶且按段快照」当前不可验证。

**F2 — 文档记三种 Source，代码有四种；真正接线的只有两种。**
`EWeaponMovementMode` = `BoundedDirectional / BallisticVault / CurvedVault / AdditiveInertia`（`MHGZWeaponRuntimeTypes.h`）。已接线：`BallisticVault`（突进回旋斩反击）、`CurvedVault`（后撑杆跳）。`BoundedDirectional` / `AdditiveInertia` 正好对应未开工的操虫斩与强化跳跃斩/急袭突刺 —— 是**按合同预置的基础设施，不是死代码**。

**F3 — 后撑杆跳 BackVault 是无主实现。**
`后撑杆跳`/`撑杆`/`BackVault`/`CurvedVault` 在 docs/ 全树零命中；唯一痕迹是 `docs/design/gameplay-tags.md:166` 一行 tag 预留。

**F4 — 签核所需的遥测已经存在，比预想的完整。**
`Saved/RuntimeTelemetry/` 下已有成套 CSV：`Animation/Playback.csv`（含 montage `Weight` / `RootMotionDisabled` / `ActiveMontageTime` / `EnableAutoBlendOut`）、`Animation/MontageInstances.csv`、`RootMotionSources.csv`（`Group` / `ActiveSourceCount` / `FinishVelocity*`）、`Spatial.csv`（`MovementMode` 原始 int）、`CapsuleHits.csv`、`State.csv`（gameplay tags）。缺的是 `GravityScale` 列与 `CustomMovementMode`。→ 阶段 A 的仪器化是**补列**，不是新建系统。

---

## 2b. 实测结论（来自 `Saved/RuntimeTelemetry/20260914-215505-BP_IG_Character_C_0-55221`）

本会话**未开编辑器、未改任何东西**，直接读已有录制。该会话含 **12 次完整的后撑杆跳**（6 次白灯 `_W` + 6 次普通），全部走 `AM_IG_HouChengGanTiao(_W)` → `AM_IG_AerialFall(_W)` → `AM_IG_AerialLanding`。

**M1 — 主缺陷：交棒瞬间垂直速度被腰斩（12 次里 10 次，掉 63%～81%）。**

| 运行 | last Flying 帧 vZ | first Falling 帧 vZ | 掉幅 |
|---|---|---|---|
| 普通（6 次） | −1009.3（每次完全相同） | −267 ～ −340 | 66%～72% |
| 白灯（6 次） | −961.1（每次完全相同） | −180 ～ −886 | 8%～81% |

用真实 `LocationZ` 差分复核过，不是 `Velocity` 字段的假象：f6094→6095 真实 vZ = −1014.7，f6095→6096 = −335.7，之后按重力（每帧约 +60 cm/s ≈ 2376 cm/s²×0.025s）重新加速。**表现就是交棒那一下身体被拽住。**

**机制（数据已定位到帧）**：
- f6095（最后一帧 Flying）：CurvedVault 源 `ct=1.0750 / dur=1.0833`、**`fin=0 marked=0`**、`FinishVelocityMode=0`、`FV=(0,0,0)` —— 即源码 `AbilityTask_MHGZWeaponMovement.cpp:529-530` 那次「改写活 source 的 `FinishVelocityParams`」**没有生效**。
- f6096：source 整行消失、模式变 Falling、vZ 已是 −339。
- 即：CMC 自己把源走到期并移除（`AccumulateMode=Override`，源在末帧贡献≈0，速度被覆盖成塌缩值），任务的 `TickTask` 在**下一帧**才发现，此时 `GetRootMotionSourceByID` 已取不到源，`FinishMovement` 的意图落空；而 `Result.FinalVelocity = CMC->Velocity`（`:497`）读到的**已经是塌缩后的值**。
- 这是**帧序竞争**，不是一帧抖动：它每次都发生，且把实录弧线的末速永久性丢掉 2/3。

**M2 — 边界墙钟脱钩存在，但幅度小，非主因（推翻了纯源码推断的排序）。**
实测 CurvedVault 源起于 montage time **0.6758 ～ 0.7249**，而 `BackJumpDuration = 0.650`。偏差约 **1～3 帧**，且总时长对齐（montage 1.7333 = `BackVaultDuration`，白灯 2.2333 = `WhiteBackVaultDuration`）。所以「整体弧线被重新锚定」的严重推论**不成立**；此项降级为「应消除的常量耦合」，不是当前症状来源。

**M3 — 落地期确实没有动作门禁（坐实 A3b）。**
`Combat.State.Aerial.CantDodge` 与 `Combat.State.Aerial.CantAttack` 在**整个会话中零出现**，确认两个 token 从未 acquire。`Combat.State.Aerial.Landing` 正常出现（与 `Combat.State.Grounded` 并存），但无任何消费者。

**M4 — 落地交叉淡入本身是正常的；但淡出期根运动被重新打开（坐实 A4 的一条）。**
f6107→f6110 是干净的 0.5/0.5 交叉淡入（fall 0.5045 / landing 0.4955）。但 **f6107 处 `AM_IG_AerialFall` 的 `RootMotionDisabled` 由 1 翻回 0**，正是 `StopAerialFallingVisual` 先 `PopDisableRootMotion` 再 `Montage_Stop` 的次序问题（`:1071-1078`）—— 数据里可见。

**M5 — 落地→起步的「抽搐两下」是步态相位断链，不是位移所有权问题（用户描述 + 数据坐实）。**

用户描述：落地蒙太奇尾帧是右脚在前；idle 是右脚前／左脚后；起步又从 idle 用右脚先迈 —— 于是落地后抽搐两下才走起来。且**在 ABP 的 PSD 节点开混合、在蒙太奇里开混合都压不掉**。原游戏目测是「播完落地直接混入 loop 准备迈出左脚的相位」。

数据（f4812 起，落地蒙太奇刚结束、摇杆推上）：

```
f4812  HasLocomotionInput=1   Selected=AS_UnSh_Idle@2.6833
f4813  HasLocomotionInput=1   StartQueryActive=1  MoveGaitQuery=0.6667
                              Selected=AS_UnSh_Idle@2.7083  MoveGaitDelta=-0.6667  IsContinuing=1
f4815  HasLocomotionInput=1   Selected=AS_UnSh_Walk_Start@0.0000  IsContinuing=0   ← 从第 0 帧重新起步
  ...  走完 Walk_Start 整段（0 → 0.8753 s）
f4851  Selected=AS_UnSh_Walk_Loop@0.7500
```

即实际链路是 **落地蒙太奇 → Idle（2～3 帧）→ Walk_Start@0（整段 0.875 s）→ Walk_Loop**。抽搐来自两个接缝：落地尾帧 → Idle（相位归零），Idle → `Walk_Start@0`（从站定重新迈步）。`MoveGaitDelta=-0.6667` 已经把错配写在数据里了（要的步态相位 0.6667，Idle 给 0），但系统在 Idle 上停留了 2～3 帧。

**为什么开混合没用（可解释的原理）**：混合消的是**姿势**差，消不掉**相位**差。两个都"看起来站着"的片段可以处在步态周期的不同点；交叉淡入把姿势插值了，但速度场里同时含两个周期的分量，读出来就是抽一下。所以加长 BlendTime 只会把抽搐变软、变长，不会消失 —— 必须匹配**相位**，不是匹配姿势。

**架构缺口**：落地按设计**不是 GA**（`actions.md:417-427`：不产生伤害、不消耗资源、不需要 Montage，走 `Coordinator→OnLanded()`），因此它**不发布 M4.4 的 `FWeaponMotionMatchingHandoff`**。AnimBP 于是走默认路径：释放到 Idle → 建 Start 查询 → `Walk_Start@0` → Loop。而项目**已经有**解决同形状问题三次的机制：`EMHGZMMActionExitRoute`（`Move/ActionIdle/AwaitExitEntry/ExitOnly/ExitAndMove`，注释明写"是路由状态而非输出姿势保持：每个值选择下一次 MM 搜索可用哪些 PSD"）+ `MM{Sheathed,Unsheathed}ActionExitSelectedTime`（选进入时机）+ `MM_MoveGait` 索引曲线（1/3、2/3、1 三条泳道 = 足相位量化）。**落地只是没被接到这套路由上。**

**本录制未覆盖、因而仍未验证的**：A4a（突进回旋斩反击落地无落地表现）—— 该会话里没有舞踏弹跳/回旋斩。需要另一次录制或阶段 0 探针后的定向 PIE。

**M6 — A4b 的前提被数据否定，且我原先把两个 tag 的语义读错了。**

- `Combat.State.Aerial.CantAttack` / `CantDodge` 的 DevComment（`Config/DefaultGameplayTags.ini:57-58`）是「空中攻击**已用**」「空中回避**已用**（舞踏重置清除）」—— 它们是**空中动作预算**标记，不是「落地期间锁输入」。我原先按字面「Cant=禁止」理解成了落地门禁，读错了。
- A4b 声称的症状「第一个输入就把落地姿势截断」在数据里**从未发生**：全部录制里 **119 次独立落地事件，0 次被动作打断**。
- 而且「落地期间锁输入」与用户的实际目标**相反** —— 用户要的是落地后**顺畅接上**起步，不是多锁一段。
- 保留的合理疑点：119 次里用户可能只是没在落地时按攻击，所以「没发生」也可能是「没试过」。因此**不声称它不可能发生**，只声称**它不是你现在的症状，且修它方向相反**。

**对照表（源码推断 vs 实测）**

| 源码推断 | 优先级 | 实测结论 |
|---|---|---|
| A1 边界墙钟脱钩 → 整条弧线重锚 | 高 → | **降级**：偏差仅 1～3 帧，弧线未重锚（M2） |
| A2 交棒姿势跳变 / 终姿是落地姿 | 高 → | 部分成立；落地淡入正常，但淡出期根运动被重开（M4） |
| **交棒速度来源（原 D4/D5，中/低）** | 中低 → | **升为唯一主因**（M1） |
| A4a 突进回旋斩落地无表现 | 高 → | 本录制未覆盖，未验证 |
| A4b 落地无动作门禁 | 高 → | **前提被否定**（M6）：tag 语义读错；119 次落地 0 次被打断；且修它方向与用户目标相反 |
| A4c `AbilityOwned` 空实现 | 高 → | 未验证，仍成立（纯代码事实，阶段 D 依赖） |
| A5 淡出期根运动被重开 | 中 → | **坐实**（M4） |
| A5 `BeginAerialFalling` 返回值被忽略 / 落地实例按指针认领 / Handoff 不被落地清理 | 中 → | 未逐一验证；均为纯代码事实，按小改动处理 |
| A3 交棒处姿势跳变 / 混合被吃掉 | 中 → | **被数据否定**：f6096 是干净的 0.5/0.5 交叉淡入 |
| —（用户提出，我原先完全没覆盖） | — | **落地→起步的步态相位断链**是落地症状的真因（M5） |

### 2c. 处置裁定：哪些「源码推测的问题」还要修

数据只是**重排**了优先级，没有整体推翻。判定标准换成三条，而不是"代码看着不对"：**(a) 是否造成你看到的症状；(b) 是否会被阶段 D 的五招复用；(c) 是否违反已签的合同或已冻结的设计。**

| 项 | 处置 | 理由 |
|---|---|---|
| A1 交棒速度塌缩 | **必修** | 数据坐实的当前症状本身 |
| A4b 落地期无动作门禁 | **不修（改判）** | tag 语义读错 + 119 次落地 0 次被打断 + 方向与用户目标相反。改列为代码整洁项：`CantDodge``CantAttack` 的文档语义是「空中动作预算已用」，应在**阶段 D** 空中招式落地时按该语义接线，或直接删除死 token |
| A5 淡出期 `RootMotionDisabled` 1→0 | **必修**（小） | 数据坐实；调换 Pop/Stop 顺序即可 |
| A5 `BeginAerialFalling` 返回值被忽略 | **必修**（小） | 4 条静默失败出口会让角色在 Falling 却没有 `Aerial.Falling` tag，空中锁整体失效 —— 阶段 D 每招都走这条路径 |
| A5 `HandleAerialLandingMontageEnded` 按资产指针认领 | **必修**（小） | 落地重叠窗口内会释放**新** token；阶段 D 的 `AbilityOwned` 落地会加剧重叠 |
| A5 `PendingMotionMatchingHandoff` 不被落地清理 | **必修** | 与 A6 直接相干：落地现在要新增一个出口载荷，不清旧的就是两个载荷打架 |
| A4a 突进回旋斩落地无表现 | **修，但先花 5 分钟定向录一次确认** | 代码逻辑无歧义（`bBeginFreeFallAfterEnd` 要求 `Completed`，而该路径只产生 `Landed`），但数据未覆盖。**不要凭代码直接改一个已签核动作的落地行为** |
| A4c `AbilityOwned` 落地策略空实现 | **修，排在阶段 D 之前** | 急袭突刺与降龙设计上都要用；现在不急，但它是阶段 D 的硬前置 |
| A2 墙钟边界脱钩 | **只加校验，不重构** | 实测偏差仅 1～3 帧、未造成症状，全面改成 notify 驱动是阶段 D 的活。现在最小动作是：激活时把常量与已指派 Montage 的 `GetSectionEndTime` 对表，不一致就拒绝并报错 —— 把静默脱钩变成响亮可测的条件 |
| A3 交棒姿势跳变 | **不修** | **数据否定**。仅保留"白灯变体物理比表现长 116ms"这条观察，作为 PIE 目视时顺带看一眼的项，不开工作项 |
| F1 舞踏倍率断开 | **必修** | 与本次症状无关，但它是 M5 退出条件「倍率封顶且按段快照」的必要条件 |
| F4 文档过时点 | **必修**（小） | 本仓库的规则是文档即真相源，漂移会污染后续阶段的判据 |
| **A7 舞踏弹跳高度/时长** | **必修（本轮新增）** | Rise 真值：顶点低 44%、时长短 61%，直接就是「升到一半掉下来」 |
| **A8 后撑杆跳顶点** | **必修（本轮新增）** | 顶点低 26%（443.9 vs 真值 598 cm）；时长与距离都已对齐。**必须先跑资产探针**，改动点在关键帧归一化还是缩放系数尚未区分 |
| **A9 坠落中模型往右移动** | **必修** | **已定案**：JumpOver 段姿势里累积 26.7 cm（白灯 31.5 cm）根骨骼偏移、坠落片段为 0、2 帧交接 → 向右 13.0 cm + 向前 23.3 cm 的突跳。**与 Rise 逐点对齐确认该横向是真实的、曲线形状也忠实（残差 ~1.1% 弧长），所以修法是让这段位移在胶囊上恰好出现一次，不是删掉它** —— 我上一轮建议的「换原地版序列」会删掉一个真实位移，已作废 |
| **F1 舞踏倍率断开** | **必修** | 探针在资产层坐实：`DA_IG_Combat` 的 `max_dance_stacks = 0`、`dance_damage_multipliers = [1.0]` |
| **A4a 舞踏落地表现时有时无** | **必修（本轮新增）** | 14 次里 6 次缺失，机制已定位到 `EndReason == Landed` 不满足 `Completed` |

**要克制的一条**：A3 这类"代码看着不对但数据说没事"的项，**不要顺手修**。本次对话前段正是栽在这个失败模式上 —— 把代码结构推断当成了已验证事实，并据此排错了优先级。

### 2d. 重新计数的测试基线（2026-09-17 复测，与 09-15 一致 —— **A7 改动未引入回归**）

| 日期 | 改动集 | `MHGZ` 全量 | `M4` | `M5` | 失败 |
|---|---|---|---|---|---|
| 2026-09-15 | 6 个 C++ 文件、零资产 | 91 项 / 90 通过 / 1 失败 | — | 3/3 | `PoseSearchControlNotifies` |
| 2026-09-17（A7） | `InsectGlaiveCombatConfig.h/.cpp` + `MHGZAdvancingCounterAbility.cpp` | 91 项 / 90 通过 / 1 失败 | — | 3/3 | 同一个 |
| **2026-09-17（A7+B2）** | **另加 `MHGZAdvancingCounterAbility.h`、`MHGZWeaponRuntimeHostComponent.h/.cpp`、`MHGZM5MovementTests.cpp`** | **93 项 / 92 通过 / 1 失败** | **35/35** | **5/5** | **同一个** |

**三次都是同一个失败项，逐项未变** —— `MHGZ.PMM.Assets.PoseSearchControlNotifies`，无武装 locomotion 序列把 Notify 挂在 `PoseSearchBlock` / `PoseSearchCostBias` 轨上。B2 新增的两个测试（`DanceTouchdownClaimsLandingWithoutFreeFall` / `DanceTouchdownKeepsHostCleanup`）**均通过**，M5 从 3 项增至 5 项。

**注意：`DA_IG_Combat` 从头到尾没被修改过**（`git hash-object` 始终等于 HEAD blob）。A7 的三个值走的是 `InsectGlaiveCombatConfig.h` 的**类默认值**。

---

### 2d-orig. 重新计数的测试基线（2026-09-15 实测，取代文档里的过期数字）

| 套件 | 结果 | 命令 |
|---|---|---|
| `MHGZ.M5` | **3/3 通过**，退出码 0 | `UnrealEditor-Cmd.exe <绝对路径>/MHGZ.uproject -unattended -nop4 -nosplash -NullRHI -DDC-ForceMemoryCache -stdout -ExecCmds="Automation RunTests MHGZ.M5;Quit" -TestExit="Automation Test Queue Empty" -log` |
| `MHGZ`（全量） | **91 项，90 通过，1 失败**，退出码 255 | 同上，把 `MHGZ.M5` 换成 `MHGZ` |

- 唯一失败 `MHGZ.PMM.Assets.PoseSearchControlNotifies` 是**既存资产问题，与 M5 无关**：它的断言是动画序列的 Notify 轨道名必须是单一 `PoseSearchControl`，而 `/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/` 下的 Start/Stop 序列把 Notify 放在了 `PoseSearchBlock` / `PoseSearchCostBias` 两条轨道上。我的改动集是 6 个 C++ 文件、**零资产**，不可能影响它。
- 结论：`milestone-gates.md:117` 的 54/54 与 `:77` 的 M4 27/27 **都是过期数字**，全量套件在我改动之前就不是绿的。
- 注意：`UnrealEditor-Cmd` 在 Git Bash 里必须传 **uproject 的绝对路径**（相对路径会 `Failed to open descriptor file`），且加 `-stdout` 才有输出。

---

**F5 — 文档过时点（顺手修）**：`demo-implementation-plan.md:496` 与 `actions.md:338` 写 `UAbilityTask_MHGZMovement`，实为 `UAbilityTask_MHGZWeaponMovement`；`actions.md:351` 的 `EMovementCollisionPolicy` 词汇与代码不符（代码为 `StopOnBlockingHit/SlideAlongBlockingHits/IgnoreBlockingHits`）；`milestone-gates.md:117` 的 54/54 与 :77 的 M4 27/27 已过时（当前树 89 个测试）。

---

## 3. 已确认的决策

1. **先修接缝**（理由见 §4）。
2. **后撑杆跳正式纳入 M5**，先补设计文档：`insect-glaive-actions.md` §3.1 补 RT+A 行、写明 `DodgeAcceptWindow` 内可达、白灯变体与 MHR 实录轨迹来源。
3. **空中下落/落地表现归 M5**，随招式一起验。

---

## 4. 为什么先修接缝，而不是先搭完整系统

1. **技术依赖**：M5 剩余五个招式**每一个**都以「进入 Falling」或「落地」收尾。接缝是所有空中动作的公共基底，现在不修就是让五个新 GA 各继承一次同一个缺陷。
2. **项目自己的冻结规则明确禁止批量前提**：`demo-implementation-plan.md:767`「不批量先建完所有 GA 蓝图再回头修底层；每个里程碑必须先通过退出条件。」`milestone-gates.md:79` 的 M5 退出条件本身就是「任一取消路径都只剩一个 CMC 移动所有者且不存在残留 WarpTarget」—— 接缝**就是**验收项。
3. **缺陷已经渗到已签核的路径上**：根因 3 让突进回旋斩反击（M4.7 已签核的动作）的落地同样没有落地动画。这不是「新功能的问题」，是现存缺陷。
4. **调试成本**：现在只有一个招式、一条复现路径，变量最少。叠加五个新 GA 后，任何 PIE 异常都会变成「是新 GA 还是接缝」的二义性排查。

---

## 4b. Rise 真值审计（**唯一真值源**：`C:/apps/steam/.../MHGZ_AerialTrajectoryRecorder`，2026-09-16 四份新录制）

### 数据约定（读 Lua 源码确认）

- `Scripts/MHRise/MHGZ_AerialTrajectoryRecorder.lua` 的 `get_master_player_position` 读 `get_Transform → get_Position` 的**原始世界坐标**，逐渲染帧。
- `velocity_*` **不是游戏里的值**，是 Lua 用位置后向差分算的 —— 所以它与位置自洽，可以用。
- **MHRise 世界是 Y-up**，竖直 = `world_y`（README 明确）。
- 量级表明坐标单位是**米**：后撑杆跳升高 ≈ 6.0，而项目 `BackVaultApexHeight = 579.7 cm` —— 对得上。
- 动作判别字段是 **`player_motion_old_id`**（本批 17 个不同值），**不是** `motion_l0..l4`（L1～L4 全空，L0 恒为 `bank=0/id=1`）。
- **不得按离地高度切分**（详见下一节）—— 会把连续滞空的多个动作合并。**本批数据无需重录，按 id 切分即可。**

### 缺口：录制本来不含朝向 —— 脚本已补（1.6.0），但**本期不需要**（见本节末「代价与时机」）

**问题**：脚本只读 `get_Transform → get_Position`，**不记录猎人朝向**。后果不是「精度差一点」，而是**横向位移在原理上无定义**：

- 「横向」需要一个参考系。Rise 不记录朝向，就只能自己造一个 —— 用首尾弦当轴得到「中段鼓起后回落」，用起始方向当轴得到「单调上升」，**两者结论相反**。§A9-R 因此只能用**与朝向无关的刚体配准**，并附带一条方法论免责声明。
- 更关键的是：**「侧滑」和「转身」在只有位置的数据里完全同形**。同样一段 13 cm 的世界横向，可能是猎人在原地朝向下向左侧移，也可能是猎人一边前进一边偏航。前者该进轨迹关键帧的 Y 分量，后者该进**根旋转**。**这个二选一决定了 A9 的修法落在哪个通道，而现有数据答不了。**

**改动**（`C:/apps/.../reframework/autorun/MHGZ_AerialTrajectoryRecorder.lua`，1.5.0 → 1.6.0，纯增量、只读不变）：

- 位置与朝向在**同一次** `get_Transform` 里读出，两者不可能失步。
- 新增 8 列：`rot_qx/qy/qz/qw`（**原始四元数**）、`forward_x/y/z`（局部 +Z 旋转后的朝向向量）、`facing_yaw_deg`（XZ 平面偏航角，Y-up）。
- **原始四元数一定要记**：万一我的轴约定猜错，可以离线重算，不必再进一次游戏。`forward_*` 才是真值，`facing_yaw_deg` 只是便利量。
- 朝向读取失败**不会**中断录制 —— 该列留空，位置照常。失败会写一条 `facing_unavailable` 事件。

**一个必须说明的不确定性**：`get_Rotation` 是**原生**访问器，名字**无法从托管方法表枚举**（`get_methods()` 不列原生方法；而且 F8 探针的 `MOTION_KEYWORDS` 过滤里根本没有 rotation/yaw/angle，所以现有 dump 证明不了它存在与否）。因此脚本改为**按候选名依次探测**：`get_Rotation → get_WorldRotation → get_LocalRotation`，每轮录制只探测一次，命中后记进 `state`，成功时写一条 `rotation_accessor_resolved` 事件。**第一次重录后请先看这条事件确认命中了哪个名字。** 同时给 `MOTION_KEYWORDS` 补了 `rot/quat/yaw/facing/forward`，下次 F8 能把朝向类访问器一并 dump 出来。

**代价与时机（2026-09-16 修订 —— 原先写的理由是错的）**

原先写的是「等 A9 的修法选定、需要再次验证时一并重录」。**这个理由是错的**：Rise 录制是**基准**，它不会因为 UE 项目改了而变。**验证一个 UE 修复靠重跑 PIE 跟现有录制比，不靠重录 MHRise。** 所以不存在「下次重录」可搭 —— **除非录朝向本身就是那个理由。**

原先给的另一条理由（回答「侧滑还是转身」）也已作废：A9 的机制是 `RootMotionRootLock = REF_POSE` 只提取增量，恒定偏移留在姿势里 —— **无论那段横向是真侧移还是真转身，都在 JumpOver 段的根骨骼姿势里、都没被提取。Rise 的朝向答不了这个问题，也不需要答。**

**现在剩下的唯一理由：旋转是最后一个未验证的维度。**

已验证：弧长 ✓、水平形状 ✓（§A9-R）、上升高度 ✓。**未验证：旋转。**

`Request.RotationPolicy = EActionRotationPolicy::Locked`（`MHGZBackVaultAbility.cpp:587`）—— 后撑杆跳全程锁死不转向。**这是未经验证的假设，不是已确认的事实。** 白灯配准的最优旋转 −97.2° 只是两个世界坐标系之间的任意常量偏置（常量偏置本就是任意的），**对「旋转量随时间怎么变」一个字都没说**。若 Rise 的猎人在这段里确实转了而 UE 锁死不转，那是肉眼可见的保真缺口，而现有数据看不出来。

朝向列一录，`Δyaw` 曲线直接跟 UE 遥测里已有的 `ActorYaw` 对齐比即可。

**裁定（2026-09-16 用户确认后）：不录。脚本改动保留，本期不用。**

用户明确：「**锁死不转向绝对正确还原游戏设计的**」。`RotationPolicy::Locked` 由设计知识确认为**事实**而非假设，旋转维度不需要数据验证。四份录制不重录。

**直接后果：§A9-R 的两难被消掉了，而且是靠设计知识不是靠数据。** 猎人全程不偏航 ⇒ **身体坐标系恒定** ⇒ 「用弦长当轴 vs 用起始方向当轴」这个二选一**根本不存在** —— **「按起始方向分解」就是物理上正确的那一个**。所以「无白灯中段 6.6–9 cm 鼓起、白灯 30–43 cm 持续」是**直接成立的读数**，刚体配准只是旁证，不再是唯一论据。

**脚本 1.6.0 的朝向列保留**（已验证语法、只读、失败不中断），理由：阶段 D 的五个空中招式**各自的 `RotationPolicy` 是逐招决定的**，届时若要核某一招转不转，录制侧已经就绪。**本期不产出任何朝向数据。**

### 必须按动作 id 切分，不能按离地高度切分

`motion_l0..l4` 在本批录制里**全部无效**（L1～L4 为空，L0 恒为 `bank=0/id=1`，`motion_l0_frame` 只是归一化播放位置）。**唯一的动作判别字段是 `player_motion_old_id`。**

按离地高度（`world_y > 0.8`）切分会把**连续滞空的多个动作合并成一段**，这正是先前把「舞踏弹跳」误判成 6.14–9.36 m 的原因 —— 实际上那是 `154` 之后**接着转 137 → 157**（用户判断为空中回避）造成的。

| actions id 序列 | 升高 (m) | 滞空 (s) | 水平 (m) | 次数 | 归属 |
|---|---|---|---|---|---|
| `146 > 147 > 143` | 5.90 – 6.01 | 1.458 – 1.492 | 4.32 – 4.47 | 4 | 无白灯 后撑杆跳 |
| `158 > 159` | 7.68 – 7.74 | 1.604 – 1.618 | 5.40 – 5.47 | 4 | 白灯 后撑杆跳 |
| **`154`（纯）** | **6.14 – 6.37** | **1.534 – 1.568** | **0.33 – 1.19** | **7** | **舞踏弹跳（唯一有效样本）** |
| `154 > 137 > 157` | 8.03 – 9.36 | 1.685 – 2.068 | 11.07 – 11.94 | 4 | ❌ 舞踏弹跳 + 后续空中动作，**不得单独归给弹跳** |
| `15` | 1.70 – 1.74 | 0.659 – 0.676 | 3.16 – 3.24 | 6 | 其它（前向跳） |
| `131` | 1.56 | 0.459 | 5.51 | 1 | 其它 |

**结论：舞踏弹跳是原地的。** 水平仅 0.33–1.19 m，项目配的 0 m 只差约 0.7 m —— **这是个小项，不是主项**。此前的「水平位移完全缺失」结论已作废。

后撑杆跳的高度峰值出现在**滞空归一化时间 u ≈ 0.55**（两条都是），起跳段 u=0.15 附近 vY 最大。

### 项目曲线 vs 真值

**后撑杆跳**（UE 达成值取自本轮 PIE 录制 `20260916-153520` 的 f11509–f11571 实测；注意 UE 的 montage 时长含起跳前的 Jump 段，故只能与「UE 实际离地窗口」比，不能直接与配置的 `Duration` 比）：

| 量 | 项目配置 | Rise 真值 | UE 实测达成 | 判定 |
|---|---|---|---|---|
| 无白 滞空 | — | 1.475 s | **1.508 s** | ✅ +2.2% |
| 无白 沿航向 | `BackVaultDistance` 583.87 cm | 440 cm | **456.2 cm** | ✅ +3.7% |
| 无白 **顶点** | `BackVaultApexHeight` 579.7 cm | **598 cm** | **443.9 cm** | ❌ **−25.8%** |
| 白灯 滞空 | — | 1.618 s | — | 需另测 |
| 白灯 沿航向 | `WhiteBackVaultDistance` 706.05 cm | **547 cm** | — | ❌ 配置高出 **+29%** |
| 白灯 顶点 | `WhiteBackVaultApexHeight` 748.9 cm | 774 cm | — | ⚠️ −3.2%（配置本身尚可） |
| 白灯 动作时长 | `WhiteBackVaultDuration` 2.2333 s | 滞空 1.618 s | — | ❌ 配置高出 **+38%** |

**结论（后撑杆跳）：时长和距离是对的，唯一真正错的是顶点高度 —— 只达成 443.9 cm，比配置的 579.7 低 26%，比 Rise 真值 598 低 26%。** 因为 `OffsetZ(u) = (Pos(u).Z − StartPos.Z) × ApexHeight`（`MHGZBackVaultAbility.cpp` 的 `AddTrajectoryKey`），达成顶点 = 579.7 × 0.766，说明**曲线的 Z 归一化只覆盖到 0.766，没有到 1.0** —— 要么 `BackVaultTrajectory` 关键帧的 Z 归一化基准不对（例如把「整段录制含落地」当分母，而动作窗口只取了前 87.34%），要么 `StartProgress=0.375` 之前已经过了真实顶点。**这两者只能靠资产探针读 `BackVaultTrajectory` 才能区分。**

**横向偏移**：Rise 真值 ≤0.16 m（占位移 ≤3%），即**这个动作本质是平面的**。而 UE 实测在 f11494–f11563 里 Y 从 470.6 涨到 484.5 再回落到 476.5，是一次 **+14 cm → −8 cm 的横向摆动**；且 `OffsetY(u) = (Pos(u).Y − StartPos.Y) × TotalDistance` 会把录制里的横向分量**按 583.87 放大**。这就是「模型突然往右边移动一下」的来源量级。需在探针后确认关键帧的 Y 是否只是录制噪声。

**舞踏弹跳（突进回旋斩反击）—— 高度与时长严重偏小：**

| 量 | 项目配置 | Rise 真值（id 154，n=7） | 判定 |
|---|---|---|---|
| 顶点 | `DanceVaultApexHeight` 350 cm | **614 – 637 cm**（均值 627） | ❌ **低 44%** |
| 时长 | `DanceVaultDuration` 0.6 s | **1.534 – 1.568 s** | ❌ **短 61%** |
| 水平位移 | 未设 `MaxDistance`（=0） | 0.33 – 1.19 m | ⚠️ 只差 ~0.7 m，**小项** |

UE 实测 14 次弹跳**全部**：升高恒 350.0 cm、时长恒 0.600 s、水平位移恒为 0（`dx=dy=0.0`）。代码路径：`MHGZAdvancingCounterAbility::StartAdvancingCounterVault`（`:216-260`）构造 `BallisticVault` 请求时**从不设置 `MaxDistance`**，于是 `ApplyBallisticVaultSource` 的 `ApexHeightAndDuration` 分支取 `Distance = 0` → `TargetLocation = StartLocation` → 原地垂直跳。**注意：原地这件事本身与真值相符**，缺的只是那 ~0.7 m。

**这直接解释「上升到一半就会掉下来」**：物理弧 0.6 s / 3.5 m，表现用的 `AS_Unsh_WuTa` 动态蒙太奇实测 **1.6167 s**（`AnimMontage_NN`，len=1.6167）。身体 0.3 s 到顶并下落，动画此时还在上升段。

**而且动画是对的、物理是错的**：1.6167 s 落在真值 1.534–1.568 s 附近（+4%），而物理 0.6 s 差 61%。所以修法应是**拉长物理去对齐动画**，不需要重做序列。代码注释（`:249-251`）自述「视觉播出可能早于或晚于可编辑的弹道时长」，即这处不一致是作者已知并接受的 —— 但它正是症状来源。

**探针补充**：`AS_Unsh_WuTa` 的 `GetPlayLength()` = 3.2333 s，但 `rate_scale = 2.0` → **有效时长 1.6167 s**，与动态蒙太奇长度精确吻合。本批多条导入序列都是 `rate_scale = 2.0`（`AS_Unsh_Fall`、`AS_Unsh_Fall_W_Jump`、`AS_Unsh_Fall_Higher`、`AS_Unsh_Fall_Junp`、`AS_Unsh_WuTa`），只有 `AS_Unsh_Fall_Loop` 是 1.0 —— 判读这些序列时**必须换算有效时长**，不能直接用 `play_length`。`dance_vault_animation_play_rate` 的 CDO 值是 **1.0**，所以动态蒙太奇没有额外缩放。

**「可能触发落地动作可能不会」也解释了**：`HandleAdvancingCounterVaultFinished`（`:296-300`）把 `bBeginFreeFallAfterEnd` 设为 `EndReason == Completed && IsFalling()`。BallisticVault 若在弧线走完前触地，`EndReason` 是 `Landed` 而不是 `Completed` → **不调 `BeginAerialFalling`** → `HandleLanded` 里 `bHadSystemOwnedFreeFall` 为假 → **不播落地蒙太奇**。14 次里 8 次走 `Completed`（ct≈0.600）、6 次提前结束（ct≈0.575，Z≈154 而非 98）—— 后者正是落地表现时有时无的那一半。**这就是 A4a，现在有证据了。**

---

## 5. 执行顺序

### 阶段 0 — 资产探针（先做，解决所有「需要开编辑器确认」）

Epic 没有官方 MCP；UE 的官方第一方自动化接口是 **Python（`PythonScriptPlugin`）+ Commandlet**。本项目已具备全部条件：插件已启用（`MHGZ.uproject`）、`bRemoteExecution=True`（`Config/DefaultEngine.ini:150-154`）、引擎在 `C:/apps/UE_5.6`（`UnrealEditor-Cmd.exe` 在位）、且仓库里已有 `Scripts/AssetOrganization/*.py` 与约 20 个 `Source/MHGZ/Editor/*Commandlet.h` 作为既有模式。

> 仓库根的 `setup_unreal_mcp.ps1` 装的是**第三方** `groscy/unreal-mcp`，且写的是 **Codex Desktop** 的配置；当前会话未注册任何 MCP（无 `.mcp.json`，`~/.claude.json` 无 unreal 项），故不可用。不要依赖它。

**做法**：新增一个**只读**的 Python 探针脚本（建议 `Scripts/AssetProbe/probe_m5_assets.py`，与既有 `Scripts/AssetOrganization/` 同风格），用

```
UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="Scripts/AssetProbe/probe_m5_assets.py" -unattended -nop4 -nosplash -stdout
```

headless 运行。**不调用任何 Save/SavePackage，不修改任何资产。** 需要 dump 的内容：

| 目标 | 要读出的字段 | 用途 |
|---|---|---|
| **`GA_IG_HouChengGanTiao`(+`_W`) 的 `BackVaultTrajectory` / `WhiteBackVaultTrajectory`** | **每个 `FBackVaultTrajectoryKey` 的 `Time` / `NormalizedPosition(X,Y,Z)` 全量 dump；并算 X/Y/Z 各自的 min/max 与峰值出现的 Time** | **A8 的唯一前置**：判定顶点低 26% 是「关键帧 Z 只到 0.766」还是「顶点落在 `StartProgress=0.375` 之前」；并量化 Y 的幅度是否只是录制噪声 |
| `GA_IG_HouChengGanTiao`(+`_W`) | `AttackMontage` / `WhiteAttackMontage` 是否被指派 | 判定 A2 根因是否成立（指派 → `BuildBackVaultMontage` 被绕过） |
| `GA_IG_...AdvancingCounter` | `DanceVaultSequence`、`DanceVaultAnimationPlayRate`、`DanceVaultBlendIn/OutTime`、`DanceVaultMontageSlot` | A7 的表现/物理对齐 |
| **`AM_IG_AerialFall` / `_W` / `AM_IG_AerialLanding` 及其源序列 `AS_Unsh_Fall*`** | **根骨骼位置轨道（X/Y/Z 随时间的 min/max 与漂移量）** | **A9 的唯一免重录定位手段**：根骨骼不在原点或随时间漂移，就能解释「坠落中模型往右移动」 |
| `AM_IG_HouChengGanTiao`(+`_W`) | 全部 CompositeSection 名与起止时刻、总长、`JumpOver` 段边界 | 与 `BackJumpDuration=0.650f` 对表，量化边界脱钩 |
| `AM_IG_HouChengGanTiao` 的 Slot 名 | 与 `AerialFall` / `AerialLanding` 的 Slot 是否相同 | 判定 A2 的姿态跳变机制 |
| `DA_IG_Combat` | `MaxDanceStacks`、`DanceDamageMultipliers[]` | F1 的配置侧 |
| `DA_IG_Combo` | 每条边的 `SourceState` / `ActivationAbility` / `TargetState` / `LandingPolicy` / `bAutoTransition`；特别是 `IG.Aerial.BackVault` 那条 | 阶段 D 的边清单 + A3c 是否已被配置触发 |
| `DA_IG_InputProfile` | 每个 chord 的 `RequiredContextTags` / 触发时机 / 消耗键；是否存在 `Combat.State.Aerial` context | 阶段 D 的输入接线 |
| 三个 Aerial Montage | `bEnableAutoBlendOut`、BlendIn/BlendOut、Slot | A2/A3b |
| `DA_TrainingDummy` / `DA_WeaponRuntime_IG` | 所有权与引用的资产 | 核对 F2/F5 |

**产出**：一份 `docs/editor/m5-asset-probe.md` 或直接写进 `docs/design/milestone-gates.md` 的验证证据行，作为后续每个阶段的可复核基线。

**注意**：headless 运行会写 `Intermediate/` 与 `Saved/Logs/`，但**不碰 `Content/`**；若编辑器 DLL 未编译，需要先跑一次 Development Editor 编译。这一步是只读的，因此可以在动代码之前独立完成并签核。

### 阶段 A — 修复下坠与落地接缝（先做）

#### A0. 仪器化（小改动，先于修复）

实测证明**现有遥测已经够定位接缝**（`Playback.csv` 的 montage/实例字段 + `Character/RootMotionSources.csv` 的 `CurrentTime/Duration/Finished/MarkedForRemoval/FinishVelocity*` + `Spatial.csv` 的 `MovementMode/VelocityZ/LocationZ`）。只需补两样：

1. `Spatial.csv` 加 `GravityScale` 与 `CustomMovementMode`（现有 `MovementMode` 只记原始 int，`MOVE_Flying=5` / `MOVE_Falling=3` 可分辨，但 Custom 子模式读不出）。
2. 记录 `CurvedVaultHandoffVelocity` 与**弧线解析末切线**两个值，供 A1 的回归护栏比对。

证据：一次后撑杆跳录出 `Saved/RuntimeTelemetry/<timestamp>`，A1 的验收判据（交棒前后 vZ 差 ≤5%）可直接从 CSV 判定，不需要目视。

#### A1. 下坠衔接主因：交棒速度取自已被塌缩的 `CMC->Velocity`（**已修复并在 PIE 验证通过**）

> **2026-09-16 复验：通过。** 新录制 `20260916-153520` 的 f11563（最后一帧 Flying）`vZ = −1009.3`，f11564（第一帧 Falling）`vZ = **−1009.3**` —— 完整保留，修复前是掉到 −339（−66%）。落地窗口内也不再出现速度腰斩。

**机制（见 M1）**：`AbilityTask_MHGZWeaponMovement.cpp` 的 `FinishMovement` 在 CurvedVault 分支里想用「改写活 source 的 `FinishVelocityParams`」把弧线末端切线交给 CMC（`:521-531`），但**时机晚了**：CMC 已经在自己的 movement update 里把走完的源自然移除并把速度覆盖成末帧的塌缩值，`TickTask` 下一帧才发现，此时 `GetRootMotionSourceByID` 取不到源、意图落空（数据里 f6095 的 `fin=0 marked=0 / FV=(0,0,0)` 与 f6096 源整行消失即此）。随后 `:549` 又把 `Result.FinalVelocity` 赋回 CMC，而它读自 `:497` 的 `CMC->Velocity` —— **已经是塌缩值**。于是每帧丢 2/3 垂直速度。

**修法（按推荐度排序）**：
1. **把 `FinishVelocityParams` 在源创建时就设好**（`ApplyCurvedVaultSource`，`:379-416`），值由轨迹曲线在 `HandoffProgress` 处的**末切线解析求出**，而不是等到 finish 时从 `CMC->Velocity` 反读。这样 CMC 自然移除源时会自己装上作者意图的末速，**彻底消除帧序依赖**。
2. `FinishMovement` 的 `Result.FinalVelocity` 不再取 `CMC->Velocity`，改为取同一条解析切线（或 `StartLocation`→当前位移的差分），保证交棒值与弧线一致。
3. `bRestrictSpeedToExpected=true` 需要在末端一段内关闭，或改用能给出真实切线的源类型 —— 当前它把 `CMC->Velocity` 压成了平台值（数据里 −810.76、−575.23 连续数帧完全不动，而真实差分在 −568 ～ −885 之间跳）。
4. 加断言/日志：`|CurvedVaultHandoffVelocity − 弧线解析末切线|` 超过阈值即报警 —— 这是本条缺陷的回归护栏。

**验收判据**：同一次后撑杆跳的录制里，last Flying 帧 vZ 与 first Falling 帧 vZ 之差 ≤ 5%；`RootMotionSources.csv` 在交棒帧的 `FinishVelocityMode=1(SetVelocity)` 且 `FinishVelocityZ` 等于弧线末切线。

#### A2. 下坠衔接次要项：位移边界与动画段边界脱钩（**已降级**）

**机制（已复核）**：`ScheduleJumpOverMovementHandoff` 用 **`UAbilityTask_WaitDelay(JumpDuration)`** 这个**墙钟**驱动交棒（`MHGZBackVaultAbility.cpp:479`），`JumpDuration` 是手写常量 `BackJumpDuration = 0.650f`（`.h:86`）。该常量只在 `BuildBackVaultMontage` 里用于切分段落（`.cpp:442-450`），而 **`PrepareAttackMontage` 一旦发现已指派的 `AttackMontage` 就在 `:288-291` 直接返回，完全绕过 `BuildBackVaultMontage`**。`Content/Weapons/InsectGlaive/Anims/Montage/AM_IG_HouChengGanTiao(_W)` 已存在，若 `GA_IG_HouChengGanTiao` 指派了它，则蒙太奇的 `Jump` 段边界由手工授权决定，与 0.650s **无任何强制关系**。`StartBackVaultMovement` 也只检查「存在 montage 实例」，从不检查当前 Section（`:520-527`）。

理论后果：边界早到 → `PushDisableRootMotion()`（`:532-533`）在起跳中途切断 Jump 根运动；晚到 → JumpOver 根运动在曲线本应接管后继续驱动胶囊。而任务在边界处采样 `StartLocation`（`AbilityTask_MHGZWeaponMovement.cpp:108`），边界一错会同时污染 `StartProgress = 0.375`（`:556`）、曲线时间窗 `[0.375, 0.8734]` 与由该窗口积出的 `CurveDistance`（默认约 318.8 cm）。

**实测把这条降级了**（M2）：12 次运行里源起于 montage time **0.6758～0.7249**，偏差仅 **1～3 帧**（其中大半还是采样量化），且总时长严格对齐。所以它是**应消除的常量耦合与阶段 D 的回归风险**，不是当前症状来源。修它是因为后面五招会复用同一条边界，不是因为它现在疼。

**前提已由录制确认**：`AM_IG_HouChengGanTiao` / `_W` 确实被指派为 `ActiveMontage`，且 `ActiveMontageLength` = 1.7333 / 2.2333，正好等于 `BackVaultDuration` / `WhiteBackVaultDuration` —— 即授权蒙太奇存在且是按配置时长做的，`BuildBackVaultMontage` 被绕过、手写常量 `0.650` 与蒙太奇段边界之间确实没有强制关系。

**修法**：
- 用**真实段边界**替代墙钟：在 Montage 的 Jump→JumpOver 边界挂 AnimNotify/NotifyState 触发交棒，或每帧比对 `MontageInstance->GetCurrentSection()`。
- 若保留常量，必须从**已指派的 Montage** 读段长（`GetSectionEndTime`）而非用手写值，并在两者不一致时拒绝激活并报错。
- `StartBackVaultMovement` 增加段校验：不在预期 Section 就拒绝。
- 阶段 0 探针顺带读出 `Jump` 段的真实结束时刻，量化当前偏差。

#### A3. 下坠姿态（**已被数据否定，仅保留一条观察**）

原推断「`EndAbility` 的 `Montage_Stop(0.05f)` 紧接着 `BeginAerialFalling` 的 0.0 淡出 → 一帧姿势跳变」**不成立**：`MontageInstances.csv` 在 f6096 显示 `HouChengGanTiao w=0.4965 / AerialFall w=0.5035`，是干净的 0.5/0.5 交叉淡入。

仍然成立的观察：BackVault 表现 Montage 的 `bEnableAutoBlendOut=false`（`:344`），以权重 1 停在终姿；而 `Jump_Over_Back` 的终姿在语义上是实录片段的触地姿。白灯变体的物理弧比表现长 116 ms（`CurveDuration` 1.58333 vs `WhiteBackJumpOverDuration` 1.467），这段里蒙太奇已冻结在终姿而胶囊仍在曲线上走。**但这需要目视确认是否构成可见问题，不要仅凭此改资产。**

#### A4. 落地衔接：三条独立缺陷

- **A4a 落地表现被跳过（高置信，且影响已签核路径；本录制未覆盖）**：`HandleLanded` 只在 `bHadSystemOwnedFreeFall`（`AerialFalling` token 有效 / `ActiveAerialFallingMontage` 有效）为真时才播落地蒙太奇（`MHGZWeaponRuntimeHostComponent.cpp:925-932`）。而突进回旋斩反击的 BallisticVault 走 `FinishMovement(Landed)` 分支（`AbilityTask_MHGZWeaponMovement.cpp:216-224`），其 `bBeginFreeFallAfterEnd` 要求 `EndReason == Completed`（`MHGZAdvancingCounterAbility.cpp:323`）—— **`Landed` 永远不满足**，于是从不调 `BeginAerialFalling`，落地**完全没有落地动画**，从舞踏姿势硬切到 MM Idle。
  **修法**：播落地表现的判据改为「是否处于空中态」（`!bGrounded` 或 CMC 的落地来源），而不是「下落蒙太奇是否还在」。
- **A4b 落地期没有动作门禁 —— 改判为不修（见 M6）**：原推断有两处错：`AerialCantDodge`/`AerialCantAttack` 的文档语义是「空中动作**预算**已用」（`DefaultGameplayTags.ini:57-58`），不是落地锁；且「第一个输入截断落地姿势」在 119 次落地里 0 次发生。更重要的是，**锁输入与用户目标相反** —— 要的是落地后顺畅接上起步。→ 改列 **阶段 D** 的代码整洁项：按「空中动作预算」的真实语义接线，或删除这两个死 token。
- **A4c `AbilityOwned` 落地策略是空实现（高置信，阻塞阶段 D）**：`MHGZComboCoordinatorAbility.cpp:569-575` 的 `AbilityOwned` 分支只调 `Ability->HandleLanded(Hit)` 就 return，而 `UMHGZGameplayAbility::HandleLanded` 是**空虚函数，全模块无任何 override**（`MHGZGameplayAbility.cpp:398-401`）。设计上急袭突刺与降龙都要用 `AbilityOwned`。
  **修法**：在阶段 A 实现该虚函数的基类语义（落地下沉/结束/清窗口），否则阶段 D 的两招无法落地。

#### A5. 次要项（同批修）

- **`StopAerialFallingVisual` 先 `PopDisableRootMotion` 再 `Montage_Stop`（`:1071-1078`）→ 淡出期根运动被重新打开（**数据坐实**，见 M4：f6107 处 `RootMotionDisabled` 由 1 翻回 0）。** → 调换顺序，或干脆不 Pop（实例随后即销毁）。
- `BeginAerialFalling` 的返回值被调用方忽略（`MHGZBackVaultAbility.cpp:214-221`），且它有 4 条静默失败出口（`MHGZWeaponRuntimeHostComponent.cpp:955-991`）。失败时角色在 `MOVE_Falling` 却**没有** `Combat.State.Aerial.Falling` tag，`IsAerialFalling()` 为 false，Character 里的空中锁整体失效。→ 记录返回值 + 失败时兜底。
- `HandleAerialLandingMontageEnded` 只用**资产指针**认领自己的实例（`:1196`）。第二次落地落在上一次的淡出窗口内时，旧实例的延迟结束事件会通过守卫、进而释放**新**的 token。→ 改用 `MontageInstanceID` 认领。
- `PendingMotionMatchingHandoff` 不被落地失效（`HandleLanded` 不触碰它）→ 空中闪避落地后可能额外播一段 M4.4 退场。→ 落地时清理与当前动作无关的存量 payload。
- 重力/刹车覆写的恢复只在 `HandleLanded` 与 `TeardownRuntime`：`InitializePoseState` 重置 `bAerialFallingPhysicsOverridden` 标志却**不恢复** `GravityScale`（`:1040-1048`）。→ 补对称恢复。

#### A6. 落地 → 起步的步态相位交接（**用户描述的真因**，见 M5）

这一条和 A1～A5 是**两个不同的问题**：A1～A5 是位移所有权，这一条是动画相位。

**建议的修法（复用已有机制，不新建系统）**：
1. 让落地发布一个「落地出口」载荷 —— `FWeaponMotionMatchingHandoff`（`MHGZWeaponRuntimeTypes.h:306-347`）新增 `LandingExit` 类型，携带**落地蒙太奇末帧的步态泳道**（`MM_MoveGait` 的 1/3、2/3、1 之一）。因为落地按设计不是 GA，发布点应在 `UMHGZWeaponRuntimeHostComponent::HandleLanded` 或 `HandleAerialLandingMontageEnded`，而不是某个 Ability。
2. 在 `EMHGZMMActionExitRoute`（`MHGZMotionMatchingAnimInstance.h:32-40`）旁加一条落地路由：落地且**有移动输入**时，直接把下一次搜索的候选集限定到 **Move loop 库**，并按第 1 步的泳道选**相位匹配的 loop 进入点** —— 即你说的「直接混入到 loop 准备迈出左脚的阶段」，跳过 Idle 与 `Walk_Start@0`。
3. **保留 `Walk_Start`** 给真正的站定起步（无落地前置的输入），不要为修这一个接缝把它删掉。
4. 顺带处理 f4813 的 2～3 帧 Idle 停留：`MoveGaitDelta=-0.6667` 已经在报错配，但系统仍停在 Idle 上；确认 `StartInputSettleRemaining` / `MMForceIdleReleaseHoldRemaining` 这两段保持是否应该在有落地出口时被跳过。

**关于「要不要把坠落和落地加入 MM 系统」——建议：都不要，但要给落地加路由。**
- **坠落不要进 PSD 搜索**：坠落的位移由 MHR 实录弧线 + CMC 拥有（`CurvedVault` 源 + 自由落体），而 MM 的职责是匹配意图轨迹。把坠落交给 MM 会让两套位移所有者并存，直接违反 `milestone-gates.md:131` 与 M5 退出条件里「唯一 CMC 移动所有者」。
- **落地不要进 PSD 搜索，但要进「路由」层**：落地是确定性反应，不需要被搜索；它缺的是**相位交接出口**，不是查询。而 `EMHGZMMActionExitRoute` 的设计注释已经明说它是「路由状态而非输出姿势保持」—— 落地正属于这一类。

**验收判据**：录制里落地后推杆应从落地蒙太奇直接进入 Move loop 库的相位匹配进入点，链路中**不出现** `Walk_Start@0`（有落地前置时），且落地尾帧与 loop 进入帧的 `CandidateMoveGait` 一致（`MoveGaitDelta ≈ 0`）；**用户 PIE 目视确认抽搐消失**。

**待你决策**：这一步会动 `ABP_MH_Character` 的图与 AnimInstance，属于 E 面资产 + M 面代码的混合改动，且会比 A1 大。建议**先做 A1/A4b 并录一次像**，确认位移侧的修复效果，再决定 A6 是并入 M5 还是开一个新面。

#### A7. 舞踏弹跳按 Rise 真值重配（**症状 2 的主因**，见 §4b）

语义已由动作 id 定案：**舞踏弹跳是原地的**（真值水平 0.33–1.19 m），不需要继承惯性档。所以只改标量，不动结构。

> **⚠️ 2026-09-16 重算：本节原先写的 625 cm / 1.55 s 两个数都是错的，已改正。** 见下方「数值重算」。

1. **[M 面 · 已完成] ✅ —— 改的是 C++ 类默认值，不是资产。** 记录时写成了 `[E 面] DA_IG_Combat.uasset`，**是错的**，已订正。

   `DA_IG_Combat` **从不覆盖** `DanceVault*` 这几个字段，生效值一直来自 `InsectGlaiveCombatConfig.h` 的类默认值。所以本次动的是：

   | 位置 | 改动 |
   |---|---|
   | `InsectGlaiveCombatConfig.h` | `DanceVaultApexHeight` 350 → **564**；`DanceVaultDuration` 0.6 → **1.6167**；新增 `DanceVaultDistance = 80` |
   | `InsectGlaiveCombatConfig.cpp` | DataValidation 请求补 `MaxDistance` 接线 |
   | `MHGZAdvancingCounterAbility.cpp` | `Request.MaxDistance = CombatConfig->DanceVaultDistance` |
   | `DA_IG_Combat.uasset` | **一个字都没改**（`git hash-object` == HEAD blob，4616 字节） |

   **`Scripts/AssetProbe/fix_a7_dance_vault.py` 因此是无效的** —— 它写的是几个本来就等于 CDO 的值，序列化时被跳过，文件不产生任何变化。保留作参考，但不再需要运行。

   **下一个人必读的三个坑（本轮逐个踩过）：**

   - **`get_editor_property` 读到值，不等于资产里存了这个值。** 属性未序列化时它返回**类默认值**。我据此误判"资产已写入"并写进了文档。**判据是「文件字节是否变化」，不是「读回来的数对不对」。** 决定性证据：把 apex 从 564 改成 777（≠ CDO）后文件从 4616 涨到 4696 字节 —— 涨了，说明 564 **此前根本没被序列化**；再改回 564（== CDO）文件精确回到 4616 / `c8edaa…`。
   - **`-run=pythonscript` commandlet 吞掉脚本的一切输出。** `unreal.log()` / `print()` 都不进日志，脚本抛 `SystemExit(1)` 也不改退出码（照样 `exit=0`、照样报 "executed successfully"）。**脚本要汇报结果就写文件，别依赖日志。**（探针 `probe_m5_assets.py` 之所以能用，是因为它写 JSON。）
   - **`probe_combat_config` 的字段列表是硬编码的。** 字段不在列表里时 `c.get(k)` 返回 `None` —— **和「属性真的读不到」长得一模一样**，本轮假阴性过一次。加字段必须同时改探针的 `fields`。
2. **[M 面 · 已完成] ✅** 新增配置字段 `DanceVaultDistance`（`InsectGlaiveCombatConfig.h`，默认 80，注释写明取值来源），并在三处接线：`MHGZAdvancingCounterAbility.cpp` 的 `Request.MaxDistance`、`InsectGlaiveCombatConfig.cpp` 的 DataValidation 请求、以及 DataAsset 本身。**做成配置字段而不是硬编码字面量**，与 `DanceVaultApexHeight` / `DanceVaultDuration` / `DanceVaultLaunchVelocity` 同级。顺带把 C++ 类默认值从 350 / 0.6 改成 564 / 1.6167 —— 否则新建的配置会退回错值。
   `DirectionSnapshot` 已设为 `GetActorForwardVector()`，未改。
3. **[M 面 · 已推迟 ⏸]** 删除 `ApplyBallisticVaultSource` 的零距离退化路径。**推迟原因：`MHGZM0FoundationTests.cpp:83` 构造了一个不设 `MaxDistance` 的弹道请求并断言「apex + duration 应被接受」（`:90`）。在 `HasValidBallisticParameters()` 里加 `MaxDistance > 0` 会直接打破它。** 要做得连测试一起改，是一个独立单元，不要混在 A7 里做。
4. **验证不用重录**：下次 PIE 后从遥测直接算 `LocationZ` 峰值 − 起跳 Z 应与 **564 ±5%** 相符、离地窗口应 ≈ **1.6167 s**、水平位移应 ≈ **80 cm**。

#### A7 数值重算（2026-09-16，从 `_02` / `_04` 两份录制按 `player_motion_old_id == 154` 连续段重切）

**原先的错因：把 `maxY`（绝对世界高度）当成了「升高」。** 6.14–6.37 是 `world_y` 的**绝对值**，而猎人起跳点本身就在 0.49–0.73 m。

| | 原写法 | 重算 |
|---|---|---|
| `DanceVaultApexHeight` | 625 cm（= `maxY` 6.14–6.37） | **564 cm**（= `maxY − startY`） |
| `DanceVaultDuration` | 1.55 s（真值区间下沿） | **1.6167 s** |

**8 个完整段（`n = 195` 帧）的升高全部是 5.64 m，一个不多一个不少** —— 这是本批数据里最稳的一个数。1.617 / 1.617 / 1.618 / 1.618 / 1.617 / 1.618 / 1.619 / 1.617 s，**并且与动画的有效时长 1.6167 s 吻合到毫秒**，所以「拉长物理对齐动画」和「对齐 Rise 真值」是同一件事，不存在取舍。

**1.55 这个数来自被截断的段污染出的区间。** 同一批里还有 `n = 88/74/57/53/39` 的短段（录制起止切在动作中间），它们的「升高」分别是 5.60/5.34/4.62/4.44/3.61 m —— **不是不同的动作，是没录完**。**取区间时必须是「时长完整的段」，不能对全部 id==154 的段取 min/max。** 这正是本计划反复出现的那类切分错误（见 §4b「必须按动作 id 切分」），所以写在这里备查。

**新得到的物理参数（可用于校验实现）：顶点在 `u = 0.49`（t = 0.792 s），基本对称。** 对称弹道下 `g_eff = 8h/T² = 8 × 5.64 / 1.6167² = **17.25 m/s² ≈ 1.76 g**`。**即舞踏这条弧比现实重力更「紧」，不是飘的** —— 若实现侧用 `ApexHeightAndDuration` 求解，得到的发射速度会自动匹配这对 (564 cm, 1.6167 s)，无需额外配重力。**若将来实测弧线比 Rise 更「飘」，先查这里。**

**A4a 一并在此关闭**：`HandleAdvancingCounterVaultFinished`（`:296-300`）的 `bBeginFreeFallAfterEnd` 要求 `EndReason == Completed`，而触地结束是 `Landed` → 不播落地表现。**14 次弹跳里 8 次 `Completed`（ct≈0.600）有落地表现、6 次提前结束（ct≈0.575，Z≈154 而非 98）没有** —— 这正是「落地动作时有时无」。修法见 A4a：把「是否播落地表现」的判据改为「是否处于空中态」，而不是「下落蒙太奇是否还在」。

#### A7 验收实测 + A4a 关闭（2026-09-17，B2）

**A7 数值正确，8/8 复现。** 改动后 PIE（`Saved/RuntimeTelemetry/20260917-010626`）共 11 次舞踏：

| | 升高 | 滞空 | 次数 |
|---|---|---|---|
| 正常 | **564.0 cm** | 1.600–1.614 s | **8** |
| 撞桩 | 946 / 971 / 997 cm | 2.775–2.851 s | 3 |

**11 次全部进入 `MOVE_Falling`**。正常组与配置（564 / 1.6167）精确吻合 —— `DanceVaultApexHeight` / `Duration` 生效，**A7 通过**。

**「时高时低」的根因不是调参，是撞训练木桩。** 那 3 次全部在根运动源启动后 **1–3 帧内**命中 `BP_TrainingDummy_C_0`（t=81.480→撞于 81.530；91.933→91.959；101.547→101.547）。`CollisionPolicy = StopOnBlockingHit` 中止移动并摘除源，角色带着瞬时速度转纯世界重力弹道：源在 f≈0.03 处上升速度约 1309 cm/s，脱手后按 g=9.8 再升 `v²/2g ≈ 874 cm` + 已得 68 cm ≈ **942 cm（实测 946）**。对照组 t=71.628 那次撞击在 **71.604**，比源启动早一帧，没杀掉源，正常跑满 65 帧。**用户裁定：碰撞策略不动，验证时离桩远点。**

**A4a 的根因订正 —— 不是 `IsFalling()` 竞态，是 `Landed ≠ Completed`。**

先前我在分析中说「弹道弧与地面同帧 → `IsFalling()` 为假 → 判据失败」。**这个说法是错的**，引擎源码核实如下：

| 事实 | 出处 |
|---|---|
| `ProcessLanded` 先 `CharacterOwner->Landed(Hit)`，**之后**才 `SetPostLandedPhysics` | `CharacterMovementComponent.cpp:6203` / `:6224` |
| `ACharacter::Landed` = `OnLanded` + `LandedDelegate.Broadcast` | `Character.cpp:290-295` |
| 任务侧 `HandleLanded` **无条件**发 `FinishMovement(Landed)` | `AbilityTask_MHGZWeaponMovement.cpp:217-225` |
| `HandleCapsuleHit` 对 `ImpactNormal.Z > 0.5` 提前返回（注释：「Ground contact has a dedicated Landed result」） | `:238-243` |

**所以回调那一刻 CMC 仍在 `MOVE_Falling`，`IsFalling()` 是 true** —— 但表达式 `(EndReason == Completed) && IsFalling()` 的第一个子句**确定性为 false 并短路**。触地永远走 `Landed`，`Completed` 在空中跑完才可能。**这是确定性失败（11/11），不是掷硬币。** 遥测里那帧 `Falling → Walking` 是真的，但那是帧末采样，不是因。

**B2 修法（已实现）**：

- **`MHGZAdvancingCounterAbility`** 新增纯分类器 `ResolveVaultExit(Reason, bCmcIsFalling)` → `{None, FreeFall, LandedPresentation}`。**`Landed` 忽略 `bCmcIsFalling`** —— 这条是让结论与回调落在帧内哪一步无关的关键；`Completed && falling` 仍返回 `FreeFall`（后撑杆跳依赖的形状，一字未动）；其余全部 `None`。`HandleAdvancingCounterVaultFinished` 用它写成两个互斥标志，`EndAbility` 里 `bPlayLanding` 排在 `bStartFreeFall` 之前。
- **`MHGZWeaponRuntimeHostComponent`** 只加一个薄包装 `PlayAerialLandingPresentation()`（早退 `!bInitialized || bShuttingDown`，否则转调现有 `PlayAerialLandingVisual`）。**刻意不检查 `bGrounded`** —— 调用点上它合法地为 false。**`HandleLanded` 一字未动**，于是后撑杆跳、普通跳跃、闪避，以及「落地是清层数唯一真理」的 M5 不变式全部不受影响。

**为什么 B2 而不是「补一段真滞空」**：MHRise 实录里舞踏是**单一动作 id 154、1.617–1.619 s**，覆盖升起 + 下坠 + 触地；解包动画 `AS_Unsh_WuTa` = `3.2333 s ÷ rate_scale 2.0 = 1.6167 s`，`DanceVaultAnimationPlayRate = 1.0`。**动画与移动同长同终**，落地后直接接地面恢复动作 id 148（0.584 s，与后撑杆跳共用）。升降配比实录 **49 / 51**，项目的 `JumpForce` 抛物线 **50 / 50**，差 16 ms（不足一帧）。**要求自由落体等于要求一段移动没有、动画也没有的时间。**

**顺带澄清一个 Python API 的边界**：舞踏的 `UAnimSequence` 同样读不到 data model（和 `AS_Unsh_Fall` 一样），所以上面只验证了**总时长**，「动画内部的升/降配比也是 49/51」**没有证据**。

**范围有意限缩**：后撑杆跳确有真实自由落体（`BackVaultFreeFallHandoffProgress = 0.8734` 在触地前交回重力），其链条完全保留。

#### A9. 「坠落中模型往右移动一下」——**已定案：vault 蒙太奇的根骨骼水平偏移未被提取，在坠落交班时弹回**

> **2026-09-16 录制 `20260916-213240`（29 次后撑杆跳）定案。** 下面是实测数据，不是推断。

**测得的量（`Spatial.csv` 新增的 `RootBoneRel*`，根骨骼在 mesh 组件空间的位置）：**

| 变体 | vault 期间 `RootBoneRel` | 偏移量 | 坠落帧 | 次数 |
|---|---|---|---|---|
| 无白灯 | **(13.01, −23.34)** | **26.72 cm** | **(0, 0)** | 17 |
| 白灯 | **(26.65, 16.88)** | **31.55 cm** | **(0, 0)** | 12 |

每个变体内**精确恒定**，跨 29 次运行零抖动 —— 这是动画烤进去的值，不是噪声。

**偏移是怎么长出来的（无白灯 run f1274）—— 全部发生在 JumpOver 段：**

```
f1274  mt=0.025  (Jump 段)      RootRel=( 0.00,   0.00)
f1300  mt=0.679  (JumpOver 起)   RootRel=( 0.17,  -0.02)   ← CurvedVault 源同时出现
f1310  mt=0.929                 RootRel=(12.47,  -6.43)
f1330  mt=1.429                 RootRel=(13.03, -23.25)   ← 累积到 26.7 cm
f1344  mt=1.679                 RootRel=(13.01, -23.34)
f1345  AerialFall 混合第 2 帧     RootRel=( 6.52, -11.60)   ← 单帧平移 13.41 cm
f1346  AerialFall               RootRel=( 0.24,  -0.07)   ← 再 13.13 cm，归零
```

**世界坐标解算**（该次运行胶囊 yaw = −87.39°，`MeshRelativeToCapsuleYaw` = −90°）：

- vault 期间模型相对胶囊：**沿行进轴落后 23.3 cm、侧向偏左 13.0 cm**
- 切到坠落片段时（约 **2 帧**）模型**向前 23.3 cm、向右 13.0 cm**

**那 13.0 cm 的右向跳动就是「往右边移动一下」**；伴随的 23.3 cm 向前在 vault 语境里不显眼。

**根因**：`PushDisableRootMotion()`（`MHGZBackVaultAbility.cpp:532-533`）在 JumpOver 段边界**有意关闭根运动提取**，好让 CurvedVault 的程序化路径独占胶囊。代码注释只说了「防止动画根运动与程序化路径竞争」，**但没处理一个后果：不提取 ≠ 姿势里没有**。JumpOver 的源序列带着 MHRise 实录的位移（玩家确实在后退），根骨骼因此停在偏移位置；坠落片段是原地的（根骨骼为 0）。两者以约 2 帧交接 → 几何体在 2 帧内平移 26.7 / 31.5 cm。

**修法（见 A9-R 的逐点对齐结论后已修订）**：目标是让这段**真实**的横向位移在胶囊上**恰好出现一次**。

1. **首选：把 JumpOver 段换成原地版序列，同时把它的位移并入 CurvedVault 的关键帧**。这样胶囊只走一次、姿势不重复计数、坠落时没有偏移可丢。**注意：单纯换成原地版而不补进曲线，等于删掉一段真实位移 —— 方向错。**
2. **备选：让 JumpOver 段的根运动真正被提取**（而不是 `PushDisableRootMotion` 关掉），并把 CurvedVault 的 `StartProgress`/份额相应减小。改动更大，但更贴近「动画位移由动画负责」的常规做法，也顺带解决 A8 的份额问题。
3. **不推荐**：运行期按根骨骼偏移反向补偿 mesh 变换 —— 把动画数据问题变成代码补丁，且会与 A8 的顶点缺口互相掩盖。

#### A9-R. 与 Rise 逐点对齐：横向位移是**真实的**，曲线形状也**是忠实的**

问题：那 26.7 / 31.6 cm 的横向到底是动画烤坏了，还是 MHRise 本来就有？

**做法**：Rise 不记录猎人朝向，所以「横向」没有唯一定义 —— 用弦长当轴得到「中段峰值后回落」，用起始方向当轴得到「单调上升」，结论完全相反。因此改用**不依赖朝向**的判据（**这个缺口已由设计知识消掉，不是靠录数据：用户确认本招式 `RotationPolicy::Locked` 锁死不转向，见 §4b「代价与时机」——身体坐标系恒定，故「按起始方向分解」就是物理上正确的那个，下面的刚体配准只是旁证**）：把 UE 的**可见模型轨迹**（新列 `RootBoneWorld*`，它就是玩家看到的那个点）对 Rise 的 `world_x/z` 做**二维刚体配准**（只允许旋转+平移、不允许镜像），看残差；再按**弧长**（而非时间）参数化，消除两端窗口口径差（Rise 从「离地 0.8 m」起算，UE 从「离地 1 cm」起算，两者差 80 cm 弧）。

| | 无白灯 | 白灯 |
|---|---|---|
| 弧长（Rise / UE） | 11.76 / 10.52 m（**−10.6%**） | 15.69 / 16.97 m（**+8.2%**） |
| 最佳刚体旋转 | −21.7° | −97.2° |
| **水平形状残差 RMS** | **13.2 cm（弧长的 1.13%）** | **18.7 cm（1.19%）** |
| 残差按弧长 u=0,0.1,…,1.0 | 24 7 8 8 11 8 8 9 8 21 20 | 36 37 6 8 6 12 18 17 17 15 15 |
| 上升高度（可见模型） | Rise 5.18 m / UE 4.33 m（**−16.4%**） | Rise 6.64 m / UE 7.07 m（**+6.5%**） |

**结论：**

1. **MHRise 确实有这段横向，不是烤坏的。** 按起始方向分解：无白灯中段约 **6.6–9 cm**，白灯 **30–43 cm 且持续到结束**。UE 姿势里那份（侧向 13.0 / 26.7 cm）与真值同量级 —— **动画是对实录的忠实还原。**
2. **曲线的水平形状也是忠实的。** 去掉朝向假设后，**运动主体段的残差只有 7–11 cm**，整体 RMS 约弧长的 **1.1–1.2%**。两端 20–37 cm 的残差主要来自**窗口口径差**（Rise 起点已在 80 cm 空中），不是曲线错。**所以 `BackVaultTrajectory` 关键帧不需要重做。**
3. **因此 A9 不是数据问题，是「位移算了两遍、又丢了一份」的归属问题**：真实横向同时存在于（a）姿势里的一份（因为 `PushDisableRootMotion()` 不提取）和（b）CurvedVault 在胶囊上走的一份；坠落片段是原地的，于是姿势那份被丢掉 → 模型弹回。**修法应让横向在胶囊上恰好出现一次，而不是删掉它。**

#### A8/A9 所有权调查（2026-09-16，代码确认）—— **上一轮的「统一根因」被推翻**

上一轮我提出：「JumpOver 段的根运动既没被提取、也没被消除，而是留在了姿势里，所以 A8 和 A9 同源」。**查完了，这个假设是错的。** 逐条：

**1. `MontageRootMotionOwned = 0` 已完全解释，而且它对本招式无害。**

所有权**只有一个授予入口**：`UAnimNotifyState_ActionRootMotionPhase::NotifyBegin`（`AnimNotifyState_ActionRootMotionPhase.cpp:18`）→ `BeginActionRootMotionPhase` → `Host->AcquireMontageRootMotion`。`UMHGZBackVaultAbility` 自己**从不 Acquire**，只会在 `:539-542` Release 自己的令牌。所以对这个招式，那个 Notify 是**唯一可能**的写入者 —— 而它**结构上不可能触发**：

- `BuildBackVaultMontage`（`MHGZBackVaultAbility.cpp:418`）用 `NewObject<UAnimMontage>(this, NAME_None, RF_Transient)` 重建蒙太奇，**全程没有拷贝 `Notifies`**。运行时蒙太奇的通知列表是**空的**。
- `AM_IG_HouChengGanTiao.uasset` / `_W` 自己也没有这个 Notify（已核）。
- **全项目没有任何 Sequence 带这个 Notify** —— 15 个带它的资产是 13 个蒙太奇 + `GA_Dodge` / `GA_Sheathe`。（所以即使拷贝 Notifies 也救不了。）

**但它的消费方只有两处，两处都不解释 A8/A9：**

| 消费方 | 作用 | 对本招式 |
|---|---|---|
| `MHGZCharacter.cpp:400` / `:653` → `bForceMMIdle` | 强制 MM 节点待机、屏蔽摇杆 | 该招式已由 `IsAerialFalling() \|\| IsAerialLanding()` 强制，**无差别** |
| `MHGZWeaponRuntimeHostComponent.cpp:699` | `AcquireActionMovement` 的前置条件，要求 `!IsMontageRootMotionOwned()` | **从不占有反而让移动任务更容易拿到**；`MHGZBackVaultAbility.cpp:539-542` 那段「先释放再交接」对本招式是**死代码** |

**结论：`MontageRootMotionOwned` 不是根运动提取的仲裁者，它只驱动 MM 待机与移动所有权守卫。** 上一轮的因果推断作废。

**2. 顺带查出的真缺陷（真实，但不是 A8/A9 的因）：**

`PushDisableRootMotion()` 在 `MHGZBackVaultAbility.cpp:532`（`StartBackVaultMovement`，注释「Disable only the remaining JumpOver root track」）**在正常路径上永远不会被 Pop** —— 唯一的 Pop 在 `:182`，被 `if (!bVisualFinished)` 圈住；而正常路径下 `bVisualFinished` 已在 `:794` 置位，`:170` 的守卫直接跳过。**每次后撑杆跳都会在这个蒙太奇实例上留下一次未配对的 disable。** 蒙太奇结束通常能兜住，但这是一处未配对的 Push。

**3. 仍然悬空的（A8 的真正症结）：Jump 段那 80.8 cm 是哪来的？**

`ApplyCurvedVaultSource`（`AbilityTask_MHGZWeaponMovement.cpp:380-440`）确认：窗口 `[StartProgress, HandoffProgress]` 内 `AccumulateMode = Override` 让轨迹源成为**唯一**位移来源，而该源在 Jump→JumpOver 边界（`:555-580`）才建立。**所以 Jump 段的抬升不是它给的** —— 只能是蒙太奇根运动，或纯姿势。**引擎源码不在本工作区，这一条无法从 C++ 判定。**

**一个被自己推翻的捷径（记下来免得再犯）：** 我试过用二进制 grep 判断序列的 `bEnableRootMotion`（uasset 里出现该属性名 ⇒ true）。**四个 Jump 序列全都「命中」，但同一个测试对 `AS_Unsh_Fall` 也命中，而探针实测它是 `False`。** 属性名无论取值都会序列化，这个捷径无效，读数作废。正路是跑 Python 探针（`Scripts/AssetProbe/probe_m5_assets.py`，已把四个 Jump 序列加进 `ROOT_TRACK_SEQUENCES`）。

**尚未解释**：Jump 段（mt 0 → 0.65）胶囊只升 **80.8 cm**，而关键帧要求该点达到 `0.4030 × 579.7 = 233.6 cm`。**这是 A8 剩下唯一的缺口，且它是数据问题不是所有权问题。**

#### A8 定案：曲线是**相对于边界高度**的，`ApexHeight` 不是顶点

探针（`Saved/AssetProbe/m5_assets.json`）与 `BuildTrajectoryCurve` 一起把 A8 算平了。

**决定性的一行**（`MHGZBackVaultAbility.cpp`，`AddTrajectoryKey` 内）：

```cpp
const FVector RelativePosition = Position - StartPosition;   // StartPosition 在 StartProgress 处采样
AddLinearKey(Curve->FloatCurves[2], NormalizedTime, RelativePosition.Z * ApexHeight);
```

**Z 曲线是「相对于 `StartProgress` 处高度」的偏移，不是绝对高度。** 所以：

```
顶点 = 边界实际高度 + (曲线峰值 z − StartPosition.z) × ApexHeight
```

非白灯：`StartProgress = 0.65 / 1.7333 = 0.3750`（探针实测 `back_vault_duration = 1.73333`，正好 = 0.65 + 1.08333）。曲线在 t=0.375 处 `z ≈ 0.4030`（t=0.353→0.309、t=0.404→0.527 线性内插），峰值 1.0 在 t=0.655。

```
顶点 = 80.8 + (1.0 − 0.4030) × 579.7 = 80.8 + 346.1 = 426.9 cm
```

**实测 443.9 cm。** 差的 17 cm 与激活帧时刻、`StartPosition` 的采样相位一致 —— **结构对上了。**

**所以 `ApexHeight = 579.7` 不是顶点，而是「相对曲线的缩放系数」，而这条相对曲线的峰值只有 0.597。** 配置成 579.7 只有在「边界高度恰好是 `0.4030 × 579.7 = 233.6 cm`」时才成立 —— **这正是曲线自己在 `StartProgress` 处采样所隐含的假设。而 Jump 段实际只交付了 80.8 cm，缺口 152.8 cm，一分不少地全部变成顶点损失。**

**这解释了 A8 的每一个数字，并且它不是曲线写错了** —— 曲线相对化是对的（否则窗口起点会有一次 233.6 cm 的瞬移）。**错的是「Jump 段会交付 233.6 cm」这个契约。**

白灯同理但不同量级：`StartProgress = 0.65 / 2.2333 = 0.2910`，曲线该处 `z ≈ 0.1477`，相对峰值 `0.8523` → 曲线自认边界高度仅 `0.1477 × 748.9 = 110.6 cm`。**曲线对白灯的假设比非白灯保守得多（110.6 vs 233.6），所以白灯的顶点缺口按比例小得多** —— 与实测「非白灯 −16.4%、白灯 +6.5%」的方向一致。白灯那 +6.5% 的正超需要另一个解释（`AS_Unsh_W_Jump_Back` 是另一条 clip，根运动量不同），本轮到此为止。

#### A8 与 A9 确实同源 —— 但机制是 REF_POSE，不是所有权

我上一轮猜「同源」的方向对了，理由错了。真实的共同机制是 **`RootMotionRootLock = REF_POSE`**（四条相关序列探针实测全为 `REF_POSE`）：

| | 段 | 表现 |
|---|---|---|
| **A8（Z 少）** | Jump 段 | 只提取**增量**。clip 根骨骼的实际抬升只有 80.8 cm，而曲线按 233.6 cm 记账 → 顶点少 152.8 cm |
| **A9（XY 多）** | Jump 段 | 只提取**增量**。根骨骼相对 ref pose 有 26.7 cm 的**恒定**偏移，恒定偏移的增量恒为 0 → 一个字节都提取不到，它**留在姿势里**跟着 mesh；坠落换成正片（根骨骼为 0）时被丢掉 → 模型弹回 |

**同一把刀的两面：REF_POSE 只认增量。该动的动不了（Z 不足），不该留的留下来了（XY 偏移）。**

**顺带推翻两个我自己的猜测（写下来免得再犯）：**

1. `RequiredSegmentPlayRate` **没有**重复施加 `RateScale` —— 它写的是 `PlayLength / (DesiredDuration × abs(RateScale))`，已经除掉了。`AS_Unsh_Jump_Back` 的 `rate_scale = 2.0` 是安全的。此路不通。
2. `MHGZBackVaultAbility.cpp:532` 的 `PushDisableRootMotion()` 是**冗余**的，不是缺陷：`AS_Unsh_Jump_Over_Back` 探针实测 `enable_root_motion = False`，那条 clip 本来就不提取。（它仍然没被 Pop —— 见上面第 2 条 —— 但那是一处无配对的 Push，不影响本症状。）
3. 我试过的二进制 grep 捷径无效（`bEnableRootMotion` 的**属性名**对 true/false 都会序列化；`AS_Unsh_Fall` 命中而实测为 False）。**序列的提取开关只能靠探针读，不能 grep。**

**四条序列的探针实测（`enable_root_motion`）：**

| 序列 | rm | rate_scale | play_length | 有效时长 |
|---|---|---|---|---|
| `AS_Unsh_Jump_Back` | **True** | 2.0 | 1.3000 | 0.65 |
| `AS_Unsh_Jump_Over_Back` | **False** | 2.0 | 2.1667 | 1.0833 |
| `AS_Unsh_W_Jump_Back` | **True** | 2.0 | 1.3000 | 0.65 |
| `AS_Unsh_W_Jump_Over_Back` | **False** | 2.0 | 3.1667 | 1.5833 |

**注意 `AS_Unsh_W_Jump_Over_Back` 有效时长 1.5833 s，恰好等于 `WhiteBackJumpOverDuration`；而非白灯 1.0833 也恰好等于 `BackJumpOverDuration`。所以分段时长配置本身是对的 —— 契约破裂只发生在 Jump 段的「高度」维度上，不在「时间」维度上。**

**注意 A8 的两个顶点口径**：以**胶囊**算，无白灯达成 443.9 cm、缺口 −25.8%；以**可见模型**算，上升 4.33 m vs 真值 5.18 m、缺口 −16.4%。两者差在模型恒定比胶囊低约 101 cm 的原点偏移与姿势的 Z 分量。**与 Rise 对比时应以可见模型为准**，因为 Rise 记的就是可见的猎人。

**已排除（同批实测）**：胶囊横向位移（坠落段每帧 ≈0.01 cm）、胶囊翻滚（`ΔYaw/ΔRoll/ΔPitch` 五次全 0）、mesh **组件**偏移（`MeshRelativeToCapsule` 恒定）、图根运动（`ProxyHasRootMotion = 0`）。上一轮怀疑的「落地后转向过冲」经本次 29 次验证**不是主因** —— 它在场，但幅度只有 2–9°，与 26.7 cm 的位移不是一个量级。

**已用 2026-09-16 录制 `20260916-153520` 排除的（五个后撑杆跳实例，全部实测）：**

| 假设 | 实测 | 结论 |
|---|---|---|
| 胶囊横向位移 | 坠落段每帧 `\|dY\|` ≈ 0.01 cm，全程平滑 | ❌ 不是胶囊 |
| 胶囊旋转 | `ΔYaw` / `ΔRoll` / `ΔPitch` **五次全部 = 0.00**（`Locked` 策略生效） | ❌ 不是旋转 |
| Mesh 相对胶囊偏移 | `MeshRelativeToCapsule` 恒为 `(0, 0, 10)`，小数位全零 | ❌ 不是组件偏移 |
| AnimGraph 根运动 | `ProxyHasRootMotion = 0`，`Raw/ScaledLocalTranslation` 全程 0 | ❌ 不是图根运动 |
| 曲线横向放大 | vault 段确有横向摆动（+14 → −8 cm），但**发生在 vault，不在坠落段** | ⚠️ 只解释 vault 段，不解释坠落 |

**因此剩下的唯一可能是「姿势层面」的偏移** —— 遥测记录的是组件变换，看不见骨骼姿势。两个候选：

1. **`AM_IG_AerialFall` / `AM_IG_AerialFall_W` 的根骨骼在姿势里带有横向偏移**。Host 用 `Instance->PushDisableRootMotion()`（`MHGZWeaponRuntimeHostComponent.cpp:996`）只关闭**提取**，不改变**姿势**——根骨骼偏离原点时，几何体会真的歪出去。坠落蒙太奇以 0.5 s 交叉淡入，期间就会看到一次横滑。
2. **vault 的表现蒙太奇定格终姿 → 坠落蒙太奇的过渡**。vault 的 `bEnableAutoBlendOut=false` 让它以权重 1 停在终姿，而该终姿是实录片段的**触地姿势**；与坠落姿的横向差在 0.5 s 混合里表现成一次横滑。

**阶段 0 探针的结果（2026-09-16）——骨骼路线走不通：**
`UAnimSequence` 在 5.6 的 Python 里**没有**暴露 data model：`get_editor_property('data_model')` 与 `get_data_model()` 都返回 `None`，`sequence_api` 里也没有任何骨骼/轨道访问器。所以**读不到根骨骼关键帧**。探针改读到的有用信息：`AM_IG_AerialFall` / `_W` 均为 0.85 s、`AM_IG_AerialLanding` 0.3889 s、全部 `enable_root_motion = False`、`root_motion_root_lock = REF_POSE`。注意 `enable_root_motion=False` 只说明不**提取**，不能证明姿势里根骨骼没动。

**但同一轮扫描找到了一个新的、可测量的候选（此前漏掉，因为上一轮的旋转扫描只到坠落开始）：**

落地蒙太奇结束、玩家推杆的瞬间，角色开始转向输入方向，**带 2 帧滞后、过冲约 6°、再阻尼收敛**：

```
f11590  Yaw=  0.09   RawYaw=-34.9   ActorToRawInputYawDelta=-34.99   LocomotionInputMag=1.00
f11591  Yaw= -8.96   ← 单帧 ΔYaw = -9.05
f11592  Yaw=-17.97   ← 又 -9.01，冲过目标
f11593  Yaw=-12.74   ← 回摆 +5.23
f11594  Yaw= -6.82   ← +5.93
f11595  Yaw= -2.91   ← +3.91，收敛
```

五次运行的单帧最大 `ΔYaw` 分别为 **9.05° / 8.67° / 4.67° / 3.64° / 2.05°**，全部出现在**落地蒙太奇结束之后**。

**关键推论**：vault 的 `EActionRotationPolicy::Locked` 随动作结束即失效，**坠落途中旋转是自由的**（`MHGZCharacter.cpp:431` 的门是 `bHasInput && !bActionMovementOwned`，而动作已 End）。所以**若玩家在坠落途中推杆，角色会在半空中就发生这段过冲旋转** —— 与「坠落过程中模型往右甩一下」吻合。

**决定性仪器已装（2026-09-16）：`Spatial.csv` 新增 10 列。**

`UpdateRuntimeTelemetry`（`MHGZMotionMatchingAnimInstance.cpp`）现在把几何记在**三个层次**上，原先只有前两层：

| 层次 | 列 | 能否反映姿势位移 |
|---|---|---|
| 胶囊 | `Location*` / `ActorPitch/Yaw/Roll` | ❌ |
| Mesh **组件**变换 | `MeshRelativeToCapsule*` | ❌（组件重挂前恒定） |
| **动画骨架**（新增） | `HasRootBoneSample`、`RootBoneWorldX/Y/Z`、`RootBoneRelX/Y/Z`、`MeshBoundsX/Y/Z` | ✅ |

- `RootBoneRel*` = 根骨骼在**组件空间**的位置，即纯粹由动画贡献的部分。
- `MeshBounds*` = **已摆姿**的几何原点，所以子骨骼把整个外形带偏时它也会动 —— 覆盖「根骨骼没动但整体看起来歪了」这一路。
- 判读方法：**用 `RootBoneWorld*` 减去胶囊位置**。坠落段里它若恒定 → 是姿势在动；若跟着变 → 是胶囊在动。

**仍需一次定向 PIE 定案：**
1. **[PIE]** 录一次「**后撑杆跳升空后立刻推杆并保持**」。若横移随推杆出现 → 是转向追踪（含过冲）；若不出现 → 查 `RootBoneRel*` / `MeshBounds*` 是否变化，即姿势层面。
2. **[阶段 0 探针，仅当要读关键帧]** Python 路线已确认读不到骨骼轨道（`UAnimSequence` 在 5.6 不暴露 data model），需改用 Commandlet（C++ 可访问 `UAnimDataModel`）。

**修法取决于定位结果**：旋转追踪 → 调转向速率/阻尼（或确认坠落期是否应继续锁旋转）；姿势偏移 → 把源序列根骨骼归零/原地化，或显式消除根偏移。**定位前不要改。**

#### A8. 后撑杆跳顶点：**根因已由探针定位，且不在曲线代码里**（见 §4b）

**探针读出的关键帧（`back_vault_trajectory`，22 点；`white_back_vault_trajectory`，24 点）证明曲线算术是自洽的：**

| | StartProgress | Pos(StartProgress) | Z 峰值 | 窗口内 Z 份额 | × Apex | ＋边界份额 | = 合计 |
|---|---|---|---|---|---|---|---|
| 无白灯 | 0.3750 | x=0.3247 y=−0.0138 **z=0.4030** | 1.0000 @ t=0.655 | 0.5970 | 346.1 cm | 0.4030 → **233.6 cm** | **579.7 ✓** |
| 白灯 | 0.2910 | x=0.3647 y=−0.0357 **z=0.1477** | 1.0000 @ t=0.662 | 0.8523 | 638.3 cm | 0.1477 → **110.6 cm** | **748.9 ✓** |

份额之和**精确等于**配置的 `ApexHeight`，说明设计意图是：**Jump 段（montage 根运动）负责把角色升到 `Pos(StartProgress).Z × ApexHeight`，CurvedVault 负责剩下那一段。**

**而实测边界高度只有 87.1 cm**（Z 98.2 → 185.3，mt 0.6506），设计要求 233.6 cm —— **Jump 段只交付了它份额的 37%**。验算：579.7 − 233.6 + 87.1 = **433.2 ≈ 实测 443.9** ✓。

**所以修法在蒙太奇/动画侧，不在 `BuildTrajectoryCurve`。** 之前「关键帧 Z 归一化错」的假设**已被证伪**（Z 确实到 1.0，且峰值 t=0.655 落在窗口 [0.375, 0.8734] 内）。下一步要查的是：
- `AM_IG_HouChengGanTiao` 的 `Jump` 段根运动是否被完整提取（PIE 里 `Playback.csv` 的 `MontageRootMotionOwned` 全程为 0，`Spatial.csv` 的 `HasAnimRootMotion` 也全程为 0 —— 但角色确实升了 87 cm，所以有东西在驱动，需查清是谁）；
- `ActionRootMotionPhase` Notify 覆盖的区间是否就是整个 Jump 段；
- 授权蒙太奇的 `Jump` 段实际根运动位移，与 `Pos(0.375).Z × 579.7 = 233.6 cm` 差多少。

**横向**：关键帧 Y 幅度 0.0320（无白）× 583.87 = **18.7 cm**、0.0480（白）× 706.05 = **33.9 cm**。Rise 真值横向 ≤8 cm（无白）/ ≤16 cm（白）。所以**关键帧的 Y 是 Rise 的约 2 倍，不是纯噪声** —— 我原先把「曲线放大录制横向分量」说成主因是**过度归因**，实测幅度只差 2 倍，且它发生在 vault 段而非坠落段。**降级为小项**，等 A9 定位后再决定是否阻尼。

**白灯配置**：`WhiteBackVaultDuration` 2.2333 vs 真值 1.618（+38%）、`WhiteBackVaultDistance` 706.05 vs 547（+29%）—— 按你的选择，按真值改（E 面）。

**已顺带解决的旧疑点**：CDO 的 `back_jump_over_duration = 1.08333`、`white_back_jump_over_duration = 1.58333`，**正好等于各自的 `CurveDuration`**。之前「白灯物理比表现长 116 ms」是拿头文件默认值（1.467）比的，**已被蓝图 CDO 覆盖，不是问题**。另外探针确认 `attack_montage` / `white_attack_montage` **确已指派** → `BuildBackVaultMontage` 被绕过，**A2 的前提成立**。

**验收判据（阶段 A）—— 全部可在 CSV 上判定，不必靠目视：**

| 判据 | 阈值 | 数据来源 |
|---|---|---|
| 交棒前后垂直速度差 | ≤ 5%（当前 63%～81%） | `Spatial.csv` `VelocityZ` + `LocationZ` 差分 |
| 交棒帧的源结束速度 | `FinishVelocityMode=1` 且 `FinishVelocityZ` = 弧线解析末切线 | `Character/RootMotionSources.csv` |
| 边界与段对齐 | RMS 起始帧的 montage time 与 `Jump` 段真实结束时刻差 ≤ 1 帧 | `Playback.csv` + 阶段 0 探针 |
| 落地期动作门禁 | `Combat.State.Aerial.CantDodge` / `CantAttack` 在落地窗口内出现 | `Character/State.csv` |
| 淡出期根运动 | 落地窗口内不得出现 `RootMotionDisabled` 由 1 翻 0 | `Animation/MontageInstances.csv` |
| 落地表现 | 每次落地都有 `AM_IG_AerialLanding` 实例 | `Playback.csv` |

外加：**用户 PIE 目视确认**下坠无顿挫、落地无硬切；新增接缝命名自动化套件（须**真正实例化 `UAbilityTask_MHGZWeaponMovement`**，补上现有测试的空缺）。

**Rise 真值对比判据（A7/A8 专用）—— 每条都能从 PIE 遥测自动算出：**

| 判据 | 阈值（对齐 Rise） | 当前实测 | 数据来源 |
|---|---|---|---|
| 后撑杆跳 顶点 | 598 cm ±5%（无白）/ 774 cm ±5%（白） | **443.9 cm（−26%）** | `Spatial.csv` `LocationZ` 峰值 − 起跳 Z |
| 后撑杆跳 滞空 | 1.475 s ±5%（无白）/ 1.618 s ±5%（白） | 1.508 s ✅ | 离地→落地帧数 × dt |
| 后撑杆跳 沿航向 | 440 cm ±5%（无白）/ 547 cm ±5%（白） | 456.2 cm ✅ | 起跳→落地的水平弦长 |
| 后撑杆跳 **横向** | **≤ 3% 位移（Rise ≤0.16 m）** | **+14 cm → −8 cm 摆动** | 航向系分解后取 cross-track 极值 |
| 舞踏弹跳 顶点 | **625 cm ±5%**（id 154 真值 614–637） | **350.0 cm（−44%）** | `Spatial.csv` 峰值 − 起跳 Z |
| 舞踏弹跳 时长 | **1.55 s ±5%**（真值 1.534–1.568） | **0.600 s（−61%）** | RMS `Duration` |
| 舞踏弹跳 水平 | **0.3–1.2 m**（接近原地） | 0.0 m（⚠️ 只差 ~0.7 m） | `LocationX/Y` 位移 |
| 舞踏弹跳 落地表现 | 每次都出现 `AM_IG_AerialLanding` | **14 次里 6 次缺失** | `Playback.csv` |

**A4a 需补一次定向录制**：本次 12 次运行全是后撑杆跳，未覆盖突进回旋斩反击的 BallisticVault 落地。修完 A1/A4b 后，专门录一次"突进回旋斩反击 → 舞踏弹跳 → 落地"，核对落地帧是否有 `AM_IG_AerialLanding`。

### 阶段 B — 打通舞踏观测链路

- **B1 [代码]** 在 `MHGZAttackAbility.cpp:1282-1295` 的 `MakeDamageSpec` 路径写入 `Damage.DanceMultiplier = Resource->GetDanceDamageMultiplier()`，**在每个 AttackSegment 创建 Spec 时快照**（对应 `insect-glaive-actions.md:283`）。
- **B2 [编辑器]** 依据阶段 0 探针读出的现值，在 `Content/Weapons/InsectGlaive/Data/DA_IG_Combat.uasset` 设定 `MaxDanceStacks > 0` 与 `DanceDamageMultipliers[]`（`Num == MaxDanceStacks + 1` 且 `[0] == 1.0f`，校验在 `InsectGlaiveCombatConfig.cpp:167-200`）。这一步**必须人工在编辑器改并保存**——探针只读不写。
- **B3 [决策]** `MHGZDamageExecCalc.cpp:107` 的 `FMath::Max(1.0f, DanceMultiplier)` 兜底会让配置里 <1.0 的倍率被静默忽略、且永远无法产生减伤。确定是否只允许 ≥1.0。

证据：新套件 `MHGZ.M5.Dance.*` 通过并报硬计数；PIE 一次反击后层数增加且伤害随分层变化。

### 阶段 C — 后撑杆跳设计文档补录（并行，不占代码路径）

在 `docs/design/insect-glaive-actions.md` §3.1 补 RT+A 行与玩法规则（`DodgeAcceptWindow` 内可达、白灯变体、轨迹来自 MHR 实录），在 `docs/design/milestone-gates.md` §3 M5 行与 §7 记录归属与验收项。

### 阶段 D — 五个空中招式（接缝修好之后）

按项目「每个里程碑必须先通过退出条件」的规则**逐个纵切**，不要一次写完五个 GA：

1. **操虫斩**（空中 `B` / `LT+B`，两种 AimSource、命中点灯、`AddDanceStack(KinsectSlash)`、走 `BoundedDirectional`）。新建 `Source/MHGZ/ActionSystem/MHGZInsectGlaiveAerialAbilities.h/.cpp`。
2. **强化操虫穿刺**（`LT+Y`，**只**允许从 `Combat.State.Aerial.Dance.Source.KinsectSlash` 派生，命中不加层）。
3. **强化跳跃斩**（空中 `Y`，`AdditiveInertia`，结束把合成末速度交还 CMC）+ **急袭突刺**（空中 `RT`，`LandingPolicy = AbilityOwned`，依赖 A3c）。
4. **降龙**（`LT+Y+B`，`AbilityOwned` Landing，Commit 时先快照倍率再清层）。

每招之后立刻做对应 **[E] 编辑器装配**：新蒙太奇、GA 蓝图、`DA_IG_Combo` 空中边（`RequiredTags=[Combat.State.Aerial]`、`bAutoTransition`）、`DA_IG_InputProfile` 的空中 chord。

**遗留待确认**：急袭突刺的源序列归属 —— `AS_Unsh_Fall_TuJin` / `AS_Unsh_Fall_R_TuJin` 命名上像空中突进（对应急袭突刺），但需确认根运动与归属（`AS_Unsh_TuJinHuiXuan` 才是地面突进回旋斩）。由阶段 0 探针扩展读出这两条序列的时长与根运动通道即可判定。

### 阶段 E — 收口项

- 补 `EIGDanceClearReason` 的 `Hit` / `Sheathed` / `DescendingThrust` / `DivingWyvern` 调用点；该枚举**缺 `Death`**，而设计文档要求死亡清空。
- AnimBP 读取 `Combat.State.Aerial.Falling.*` / `.Landing` 选 pose。
- 修 F5 的文档过时点。

### 阶段 F — M5 签核

Development Editor 全量编译（新增反射字段**不得用 Live Coding 验证**）→ 命名自动化套件带硬计数 → commandlet 资产审计带计数 → DataValidation 冷启动资产数 → `Saved/RuntimeTelemetry/<timestamp>` 录制 → **用户 PIE 目视确认** → 文档状态行回写（`milestone-gates.md` §3 行、§6 清单、§7 当前位置）。
**必须重新计数 `MHGZ.M4` / `MHGZ.M5`，不得复用已过时的 34/34。**

**旋转维度不需要验证**：`RotationPolicy::Locked` 已由用户按设计确认为事实（见 §4b「代价与时机」）。**M5 不产出朝向数据、不重录 Rise。**

---

## 6. 边界（本阶段不做）

- 不认定「已有空中 ability 代码或资产」—— 一个都没有。
- 不断言舞踏目前影响伤害（F1 已证断开）；也不反过来说「舞踏完全不可观察」—— vault 运动、`OnDanceStacksChanged`、`GetDanceStacks` 都是活的。
- 不在反击 GA / 蓝图 Tick / `MHGZCharacter` 里直接写 CMC 或 `LaunchCharacter` 绕过位移所有权（`milestone-gates.md:131`）。
- 不删 `BoundedDirectional` / `AdditiveInertia` / `DistanceCurve` / `AirControlScale` / `DescendingThrustAirControl` / `DivingWyvern*` —— 它们是阶段 D 要消费的合同。
- 不做 M6 的觉虫击、逐击点灯、粉尘、HUD（`verification.md` 28-30 属 M6）。
- 不把 `ResourceWidgetClass`、HUD 资源面板提前接进来（M6/E6）。
- 不用自动化测试替代 PIE 视觉验收。
