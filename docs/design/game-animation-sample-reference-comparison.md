# MHGZ 与 UE5.6 Game Animation Sample：移动与动画系统事实对照

> **文档性质：只读审计与后续设计依据，2026-08-29 重建并裁定门禁。** 本文先分别记录 MHGZ 和本机 UE5.6 Game Animation Sample（下称 **GASP**）的实际构成与证据，最后才做受限对比。本文不直接实施运行时或资产改动；已据此更新 [阶段门禁](milestone-gates.md)，将普通移动验收（M4.2）与动作退出验收（M4.5）分开。
>
> **当前项目位置：E4.2 动作退出资产。** M4.2 普通移动 / Stop 固定矩阵、历史“真实松杆后 Stop 不二次重选”专项、M4.3 输入释放补丁和 M4.4 根运动交接合同已经通过；动作结束后的异常 Loop 留给 E4.2 / M4.5。阶段真相以 [milestone-gates.md](milestone-gates.md) 为准。
>
> **本轮明确不采用的办法：** 不恢复或延长“动作结束后强制 Idle”的时间保持；它会用输入死区掩盖错误交接。代码中 `mhgz.MM.PostActionIdleHold` 默认关闭（0）。保留“锁定释放首帧重置位移测量”是另一件事：它只丢弃最后一个 Montage Root Motion 样本，不要求 AnimGraph 持续输出 Idle。

---

## 1. 证据范围、可信度与术语

### 1.1 本次实际读取的来源

| 来源 | 用途 | 能证明什么 | 不能证明什么 |
|---|---|---|---|
| `Source/MHGZ/MHGZCharacter.*` | 输入、速度档位、朝向与动作锁定 | C++ 每帧怎样产生普通移动意图 | AnimGraph 的具体引脚接法 |
| `Source/MHGZ/Animation/MHGZMotionMatchingAnimInstance.*`、`MHGZMotionMatchingMath.h` | 查询、Stop 生命周期、遥测 | 每帧输入如何成为 MM 查询；何时清空测量 | 引擎在某一帧为何生成 Pose History 的具体骨骼样本 |
| `Source/MHGZ/WeaponRuntime/*`、`ActionSystem/MHGZGameplayAbility.*` | ActionToken、Montage Root Motion 所有权 | GA 生命周期与根位移所有权的精确边界 | 某个 Montage 的艺术表现是否自然 |
| `Saved/RuntimeTelemetry/20260829-114927-BP_IG_Character_C_0-53275` | 最新 PIE 事实 | 输入、Tag、GA、Montage、查询、选帧、角色实际位移 | 没有导出的 AnimGraph 连线 |
| 同会话导出的 `PoseSearchCandidates.csv` / `PoseSearchChannelCosts.csv` | 选帧成本 | 某次搜索中哪些候选真实参与、谁胜出、各频道成本 | 该查询姿势样本的上游精确来源 |
| UE5.6 Editor 的只读资产清单 | ABP / PSD / PSS / GASP 包组织 | 节点类别、数据库、Schema、资产是否存在 | 图的接线、Chooser 行条件及每个蓝图函数的精确逻辑 |
| 用户此前提供的 AnimGraph 截图 | MHGZ 核心图可见布局 | 两个 Motion Matching、`Unsheathed` 切换、Pose History、Slot 的主干存在 | 被截图裁掉的所有分支 |

因此，本文把结论分为：

- **已证实**：源码、CSV 或资产可直接复核；
- **强推断**：证据能确定效果与边界，但不能定位图中某一个未导出的引脚；
- **待在编辑器核对**：二进制资产和无头脚本无法可靠读出的接线/Chooser 行。

### 1.2 本文中的三个“交接”不要混为一谈

| 名称 | 意义 | 当前 MHGZ 状态 |
|---|---|---|
| **Root Motion 所有权释放** | ActionToken 释放 `MontageRootMotionOwner`，允许普通 MM 再次成为唯一根位移来源。 | 已实现。 |
| **位移测量重置** | 释放所有权的首帧不把 Montage 尾帧位移计入 `MMActualSpeed2D`。 | 已实现，且不等于输入死区。 |
| **候选语境交接** | 在“刚结束功能动作”的姿势/脚相/根速度下，决定普通移动允许搜索哪一组候选。 | 未实现；当前直接把同一 Move PSD 全量开放。 |

后文发现的核心问题属于第三项，不能用第一项或第二项的时间保持代替。

---

## 2. MHGZ 当前移动与动画系统（先不与 GASP 比较）

### 2.1 目标边界与位移所有权

MHGZ 当前普通移动是**前向、Root Motion 驱动、纯 Motion Matching**：

- 实际平移来自普通移动动画的 Root Motion；`CharacterMovementComponent` 保留为碰撞、重力和落地壳，不承担普通移动的速度驱动。
- 导入资源的前向根轨迹是角色本地 **+Y**；因此 PSS 轨迹必须使用完整 `Position`，预测前向也由 `ActorTransform.GetUnitAxis(EAxis::Y)` 构造。使用 `PositionXY` 曾造成前向轴不一致和选帧抽搐，已不再是可接受配置。
- 功能 GA 的 Root Motion 由 `UMHGZWeaponRuntimeHostComponent` 用精确 `FWeaponActionToken` 取得、释放和验证：`AcquireMontageRootMotion`、`ReleaseMontageRootMotion`、`IsMontageRootMotionOwned`。一个有效 ActionToken 同时只能有一个 Montage Root Motion 所有者。
- `Combat.State.BlockMovement` 与 Montage Root Motion 所有权不同：前者屏蔽普通 locomotion 输入，后者防止 MM 与 Montage 在同一帧双重贡献根位移。`UMHGZGameplayAbility::EndAbility` 会注销 Montage、Active Action 和能力持有的 Tag，再以同一 ActionToken 通知 Combo FSM 收束。

**结论（已证实）：** 当前项目不是 CMC 速度/状态机移动，也不是让 MM 决定 GA 入口；GA 先决定玩法动作，普通 MM 只应负责没有剩余玩法语义的移动阶段。

### 2.2 输入、速度与朝向数据流

```text
Enhanced Input 的 Move
  -> AMHGZCharacter::DoMove(Right, Forward)
  -> RawMoveInput + LastMovementInputDir          （即使 BlockMovement 也保留）
  -> bHasInput / InputMagnitude                   （BlockMovement 时强制为 0 / false）
  -> TargetCruiseSpeed                             （离散动画档位）
  -> DesiredSpeed                                  （仅供普通移动预测的平滑值）
  -> UMHGZMotionMatchingAnimInstance
  -> MMPredictedTrajectory + 语义曲线查询
  -> 两个 Motion Matching 节点中的当前姿态节点
```

`CalcCruiseSpeed` 只会发出 PSS 已有真实动画速度档位，防止不存在的中间速度使 Loop 摇摆：

| 姿态与输入 | `TargetCruiseSpeed` |
|---|---:|
| 输入小于 `MoveDeadzone=0.1` | `0` |
| 收刀、幅度 `< 0.5` | Walk `160` |
| 收刀、幅度 `>= 0.5` 且不满足冲刺 | Run `460` |
| 收刀、RB 按住且幅度 `> 0.9` | Sprint `575` |
| 拔刀、任何有效输入 | 单一 Unsh. lane `440` |

`DesiredSpeed` 以 `FInterpTo` 向目标速度靠近（当前默认插值速率 20）；它**不是实际根位移速度**。实际值 `MMActualSpeed2D` 来自相邻帧 Actor 位置差。有效摇杆存在时，`Tick` 以 `TurnRate=360°/s` 将 Actor 朝 `LastMovementInputDir` 转向；此项目没有横向/后向普通移动动画，不应从这一数据流推导出 360° locomotion。

### 2.3 当前查询与 Stop 合同

`UMHGZMotionMatchingAnimInstance::NativeUpdateAnimation` 是查询产生者，不直接播放或硬选动画；真正的最低成本搜索由 AnimGraph 的 Motion Matching 节点完成。

| PSS 语义 | 运行时字段 | 含义 |
|---|---|---|
| Pose | Pose History | 当前骨骼姿势、双脚位置/速度/相位。 |
| Trajectory | `MMPredictedTrajectory` | 当前实际速度到目标速度的前向预测距离。 |
| `MM_Intent` | `MMIntentQuery` | Start 为正；巡航 / Idle 为 0；真实松杆 Stop 为负并沿 Stop 曲线回 0。不是速度。 |
| `MM_DistanceToStop` | `MMDistanceToStopQuery` | Stop 剩余距离，负值向 0 收敛；非 Stop 为 0。 |
| `MM_StopGait` | `MMStopGaitQuery` | 松杆边沿锁存 Walk / Run / Sprint 的 Stop 家族。 |
| `MM_MoveGait` | `MMMoveGaitQuery` | 有输入的目标移动家族：Walk=1/3、Run=2/3、Sprint=1；无输入为 0。 |

Stop 的现行合同：

1. **只有物理 locomotion 输入的下降沿** `bInputReleased` 能创建 Stop 请求；动作残余位移不能创建 Stop。
2. 已接受的 Stop 仅允许同一动画、同一时间轴向前的 `Continuing` 结果继续回读 `MM_Intent` / `MM_DistanceToStop`。
3. 新搜索回到 Stop 开头、不同 Stop 资产、或时间倒退，都必须清除本次请求，避免一个松杆无限重放 Stop。
4. 收刀 Extended Stop 的输入族由松杆时的活动选帧锁存，不能让“本帧偶然获胜”的 Walk Stop 把 Run / Sprint 自我确认成 Walk。

该合同有对应自动化测试：速度量化、预测距离、本地 +Y 轨迹、动作所有权释放后一帧测量重置、以及已消费 Stop 不能重新武装。它解决的是 **Stop 生命周期**，不是功能动作尾姿如何进入普通移动候选池。

### 2.4 AnimBP、PSS 与候选池的已知构成

无头资产清单和用户提供的图像共同确认：

| 项目 | 已证实构成 |
|---|---|
| `ABP_MH_Character` | 2 个 `AnimGraphNode_MotionMatching`、1 个 `PoseSearchHistoryCollector`、1 个 `DefaultSlot`、3 个 `BlendListByBool`、2 个 Sequence Player、Cache Pose / Blend Stack 节点。可见主干为两套 MM 经 `Unsheathed` 选择，经过 Pose History，再进入 Slot。 |
| 收刀库 | `PSD_MH_Shth_Move`，PSS 为 `PSS_MH_Move`，`normalization_set=None`。 |
| 拔刀库 | `PSD_MH_UnSh_Move`，同一 `PSS_MH_Move`，`normalization_set=None`。 |
| 动作层 | 全身功能动作走 Montage / Slot；持刀送虫、收虫等 in-place 上半身动作可在 `UpperBody_IGAction` 分层，不取得 Montage Root Motion 所有权。 |

**待编辑器核对，不可假定：** 当前三个 Bool 分支各自的条件与精确接线。无头扫描能数到两个 Sequence Player 和三个 Bool，但不能可靠导出 Pin 链。此前截图没有显示一条“动作结束后持续 N 秒的强制 Idle”分支；而 C++ 中的 `bMMForceIdle` 只说明字段和查询侧状态存在，不能单独证明它正在以何种方式控制最终 Pose。

这项不确定性不妨碍定位当前根因：最新问题发生在所有权已释放、查询全为 0、普通 MM 已重新搜索之后。即使存在一个短暂的 Idle 分支，它也不能解释为什么下一次全库搜索把 Run Loop 判为最低成本。

### 2.5 `bForceMMIdle` 的当前实际边界

这是容易混淆的历史字段，必须精确记录：

| 项目 | 当前代码事实 | 本轮结论 |
|---|---|---|
| `AMHGZCharacter::bForceMMIdle` | 每帧由 `BlockMovement || IsMontageRootMotionOwned()` 重算。 | 它表达“Montage Root Motion 所有权存在时，普通 MM 不应同时贡献 RM”的设计意图。 |
| `bMMForceIdle` | AnimInstance 内部的有效测量/图表状态；还会短暂用于两帧模拟摇杆起步稳定。 | 起步稳定与动作后死区不是同一机制。 |
| `mhgz.MM.PostActionIdleHold` | 默认 0；仅当 CVar 显式非 0 才填充 `MMForceIdleReleaseHoldRemaining`。代码注释也将其称为 legacy comparison aid。 | **保持关闭。** 不能拿它解决当前动作退出问题。 |
| `ShouldResetMotionMeasurement` | 所有权持有时、释放后的首帧、离地、无前帧位置或无效 delta 时，重置 `MMActualSpeed2D` 的时间状态。 | **保留。** 它不保留 Idle、不屏蔽新输入，只防止最后一帧 Montage RM 被误算为普通移动速度。 |

所以，“核对 `bMMForceIdle` 是否接到 Pose History 前”并不是当前要做的修复步骤。此前把它作为优先检查，是把“确认历史隔离分支”误当成“解释动作退出错误 Loop 的根因”。这条思路在本轮废止：重新接上或延长它会回到已否定的输入死区方案。

### 2.6 最新 PIE 中的问题、证据和已经排除的原因

采样会话：`Saved/RuntimeTelemetry/20260829-114927-BP_IG_Character_C_0-53275`。

#### P-01：GA 结束后无输入却选到普通 Loop，角色产生自动位移

**已证实。** 这是最新主问题，出现在收刀、拔刀、突刺等动作后，但不是每次必现。

收刀 `GA_Sheathe_C#6` 的可复核时间线：

| 帧 | 事实 |
|---:|---|
| 14629–14631 | `GA_Sheathe_C#6` 仍是 Montage Root Motion owner。原始摇杆为 0。 |
| 14632 | 所有权已释放；收刀 MM 首次正常选中 `AS_Shth_Idle`，查询的 Intent / Distance / StopGait / MoveGait 均为 0。 |
| 14634 | 同样无原始输入、无 locomotion 输入、所有语义查询仍为 0，但收刀 MM 非 Continuing 地选中 `AS_Shth_Run_Loop@0`。 |
| 14635 | 角色实际根位移速度约 `395.6 cm/s`；这不是 `DesiredSpeed` 或摇杆要求的速度，而是错误 Loop 自己输出的 Root Motion。 |

导出的 Pose Search 候选与频道成本说明这是正常的“最低成本选择”，不是系统无视条件的硬切：

| 141.743430 trace 时刻、收刀节点 | 总成本 | 关键原因 |
|---|---:|---|
| `AS_Shth_Run_Loop@0`，Rank 1 | `6.623489` | Pose 约 `5.408359`；轨迹约 `0.941237`；`MM_MoveGait` 不匹配约 `0.273894`，但不足以抵消脚部速度 / 姿势优势。 |
| 最佳 `AS_Shth_Idle`，Rank 114 | `8.183671` | Trajectory 与语义成本为 0，但 Pose 约 `8.183670`，其中双脚速度成本明显更高。 |

因此根因不是“0 查询还不够接近 Idle”，而是：**功能动作释放后，Pose History 中当前双脚运动状态在全量 `PSD_MH_Shth_Move` 里更像 Run Loop 的起点，而该库没有“这是功能动作刚退出、目前无移动意图”的候选语境约束。**

**强推断、尚不可从 CSV 单独定位的部分：** 这份 Pose History 样本具体由 Montage 尾姿、Slot / Cache / Pose History 图顺序、还是释放帧前后的混合共同造成。需要在编辑器中检查图接线或在引擎调试器中逐帧捕获 Pose History，才能把它缩小到一个节点；当前证据不允许把锅直接归给某一段 Montage 或某一根线。

同类例子也出现在 `GA_IG_BaDao_C#1`、`GA_IG_TuCi_C#2` 等：动作结束后先有 Idle，再在无输入、目标速度 0 的后续搜索中跳入 `AS_UnSh_Walk_Loop`，由后者产生数百 cm/s 的实际根位移。有些同类动作正常保持 Idle，故“动作资产天生带不可避免尾位移”也被排除。

#### P-02：视觉上像“动作后又 Stop”，但最新记录中的真正 Stop 是合法的吗？

**已证实：当前采样中的 Stop 与 P-01 要分开看。**

- 当前记录中，持刀 `AS_UnSh_Walk_Stop` 的一次例子发生在帧 20339；此前原始输入在 20163–20323 为满幅，20325 变为 0，随后约 0.12 秒由输入下降沿合法选择 Stop。它与更早结束的 `GA_IG_TuCi_C#17` 相隔 256 帧，不是该攻击制造的 Stop。
- 旧问题“没有松杆、仅因动作残余位移而得到 `MMIntent=-1`、然后进 Stop”已经由 `UpdateLegacyIntentQueries` 修掉：现在只有 `bInputReleased` 能武装拔刀 Stop。
- 动作结束后若看见额外的“停一下 / 走一步”，最新数据首先应辨认是否其实是 P-01 的错误 Walk/Run Loop 起步，而不是 Stop 资产。两者修复路径不同。

#### P-03：收刀后不推杆却自动 Walk，碰墙才停

这是 P-01 的连续后果，而不是“摇杆飘移”或合法 Stop 合同。

一旦错误 Loop 已输出位移：

```text
错误 Loop 的 Root Motion
  -> Actor 位置继续改变
  -> 下一帧 MMActualSpeed2D 非零
  -> 预测轨迹以该实际速度为初始速度
  -> 全库更容易继续选移动 Loop
```

因为玩家从未真的推杆再松杆，`bInputReleased` 不发生，系统也**不应伪造一个 Stop** 把它刹住。撞墙后 Actor 位置差变小，`MMActualSpeed2D` 才下降，搜索才可能回到 Idle；这解释了“碰墙才停”。正确方向是阻止无意图的 Loop 获得候选资格，而不是在事后凭速度强制 Stop。

### 2.7 MHGZ 当前已确认的非问题与未完成项

| 项目 | 状态 |
|---|---|
| 前向轴 / `Position` 轨迹选择 | 已修正；不可退回 `PositionXY`。 |
| 一次松杆只消费一个 Stop、Stop 尾段连续性 | 历史 Stop 生命周期专项已验收。 |
| 由残余动作位移伪造拔刀 Stop | 已从运行时合同移除。 |
| 普通移动内部 Walk / Run / Sprint 直接换 Loop 的观感 | 仍可能存在；当前只有短 Blend 方向，未完成正式调优/验收。 |
| 功能动作末段 → 普通移动 | 没有通用候选语境接口；P-01 表明直接全库交接不可靠。 |
| 动作期间 `bMMForceIdle` 在 AnimGraph 的精确连线 | 仍需可视化核对，但不是重新启用时间保持的理由。 |

---

## 3. 本机 UE5.6 Game Animation Sample 的移动与动画系统（先不作比较）

### 3.1 本次实际检查的位置与项目轮廓

本机样例位置为 `D:\study\MH\游戏动画示例`，项目为 UE 5.6。它不是一个可直接复制到 MHGZ 的单一“移动蓝图”，而是一套面向通用第三人称、360° locomotion、跳跃、落地、Traversal、姿态/朝向修正的完整堆栈。

已发现的主要资产：

| 领域 | 资产 / 路径模式 |
|---|---|
| 角色与动画蓝图 | `Content/Blueprints/CBP_SandboxCharacter.uasset`、`ABP_SandboxCharacter.uasset`、`PreCMCTick.uasset`。 |
| 输入 / 运动状态 | `CharacterInputState`、`E_Gait`、`E_MovementMode`、`E_MovementState`、`E_RotationMode`、`E_Stance`、方向阈值和 Blend Stack 输入结构。 |
| Pose Search Schema | `PSS_Default`、`PSS_Idle`、`PSS_Stop`、`PSS_Jump`、`PSS_Traversal`。 |
| 数据库路由 | `CHT_PoseSearchDatabases`、`CHT_PoseSearchDatabases_Dense`、`CHT_PoseSearchDatabases_Sparse`。 |
| Dense locomotion | Stand 的 Idle、Walk/Run/Sprint 的 Starts、Loops、Stops、Pivots、Lands、部分 `SpinTransition`、以及 `*_FromTraversal`。 |
| Sparse locomotion | Stand / Crouch 的 Walk/Run/Sprint Starts、Loops、Stops、Pivots。 |
| Traversal | `PSD_Traversal`、Traversal 相关输入/输出结构、EarlyTransition 和 Montage BlendOut NotifyState。 |
| 动画数据处理 | Move speed、左右脚速度、Phase、距离、Orientation/Rate Warping Alpha 等 AnimModifier。 |

### 3.2 `ABP_SandboxCharacter` 的可证实节点构成

无头只读扫描识别出以下节点类别：

| 类别 | 数量 | 能确定的作用范围 |
|---|---:|---|
| Motion Matching | 1 | 样例确实使用一个 MM 节点作为系统的一部分。 |
| Pose Search History Collector | 1 | 当前姿势和轨迹历史参与查询。 |
| State Machine / State Result / Transition Result | 1 / 9 / 20 | 样例中存在状态机子图；仅凭节点清单不能断言普通 locomotion 的 Start/Stop 完全由此状态机播放。 |
| Blend Stack / Two Way Blend / Dead Blending | 各存在 | 该项目还有显式的姿势交接与死亡混合层。 |
| Offset Root Bone / Reset Root | 1 / 2 | 根骨骼偏移与复位被纳入动画图。 |
| Orientation Warping / Steering | 2 / 4 | 方向扭曲与转向校正在图中存在。 |
| Foot Placement / Leg IK | 各 1 | 足部接地与腿部 IK 存在。 |
| Slot / Mesh-space additive | 1 / 2 | Montage 和叠加表现层存在。 |

Pose History 的资产默认参数也与 MHGZ 不同：样例记录 `pose_count=2`、`trajectory_history_count=10`、`trajectory_prediction_count=8`、预测间隔 `0.4`，并配置 `root_bone_recovery_time=0.3`；MHGZ 的扫描值为相同的样本数量，但根恢复时间为 `0`，采样间隔显示为 `0.04`。这些默认值只说明两套图的根恢复策略不同，**不能**据此直接复制数值。

### 3.3 Pose Search 数据库和 Schema 的实际组织

GASP 明确不是“所有动画放进一个普通 Move PSD”：

| 可见资产组 | 对应语境 |
|---|---|
| `PSD_Dense_Stand_Idles` | Stand Idle；使用 `PSS_Idle` 和 `PSN_Dense_All` normalization set。 |
| `PSD_Dense_Stand_{Walk,Run,Sprint}_{Starts,Loops,Stops,Pivots,Lands_*}` | 普通站立 locomotion 按速度档和动作功能细分。 |
| `PSD_Dense_Stand_{Walk,Run}_FromTraversal` | Traversal 完成后回到普通移动的专用候选。 |
| `PSD_Dense_Stand_{Walk,Run}_SpinTransition` | 特定转向 / 旋转语境。 |
| `PSD_Sparse_*` | 相同大类的稀疏版本，含 Stand 与 Crouch。 |
| `PSD_Traversal` | 独立 Traversal 库；使用 `PSS_Traversal`，该资产本身未见 normalization set。 |

`CHT_PoseSearchDatabases*` 与 `S_ChooserOutputs`、`S_TraversalChooserInputs/Outputs` 同时存在，足以证实样例会先用 Chooser / Gameplay Context 组织可搜索的数据库集合，再由 Pose Search 在返回集合中决定帧。无头方式不能可靠展开 Chooser 的每一行条件；例如“某个具体 enum 值必然返回某一 PSD”仍是**待编辑器核对**，不能写成事实。

### 3.4 Traversal 与功能动画的可见机制

样例有独立 `PSD_Traversal`、`E_TraversalActionType`、Traversal 输入/输出结构、`BP_NotifyState_EarlyTransition` 与 `BP_NotifyState_MontageBlendOut`。这说明样例为 Traversal 这样的功能移动准备了：

1. 单独的动作类型 / 上下文数据；
2. 独立的可搜索动画集合；
3. 能在指定动画阶段提前转移或 Blend Out 的 NotifyState；
4. 从 Traversal 返回各 gait 的 `FromTraversal` 数据库。

这里能得出的事实是“功能上下文没有和所有普通循环动画无条件混在一个库里”。不能从资产名称推断它的 NotifyState 精确何时释放 CMC、是否有 GAS、是否采用与 MHGZ 相同的 Token 所有权模型；样例的运行时蓝图连线仍需在编辑器打开后再作逐图验证。

### 3.5 GASP 的方向、根运动和视觉校正栈

GASP 同时拥有：

- Dense / Sparse 资源；
- 360°方向相关的 `OrientationWarping`、`Steering`、`OffsetRootBone`、`ResetRoot`；
- Foot Placement 与 Leg IK；
- Blend Stack、State Machine、Dead Blending；
- 以多个 Schema 和 Normalization Set 管理的 Pose Search 数据。

这套栈适配的是通用角色移动，包含比 MHGZ 更多方向、姿态和 Traversal 情形。它不能自动说明“官方样例是 CMC 位移”或“官方样例所有 Start/Stop 都由状态机播放”：本次资产证据只足够确认上述组件共存。对 MHGZ 最重要的是它的**候选集按语境组织**，不是复制它的 360° / Root Offset / Warping 方案。

---

## 4. 两个项目的受限对比

### 4.1 架构差异表

| 维度 | MHGZ 当前 | GASP 已证实构成 | 可否直接照搬 |
|---|---|---|---|
| 普通移动位移 | 前向 Root Motion；CMC 是碰撞壳。 | 存在 MM、Root Offset、Warping、Steering 等通用栈。 | 否；根位移和坐标约定不同。 |
| 普通候选库 | 收刀 / 拔刀各一个 Move PSD，共用一个 PSS。 | Dense / Sparse，且 Idle/Start/Loop/Stop/Pivot/Land/Traversal 分库。 | 不能原样复制规模，但可借鉴“先限定语境”。 |
| Stop | 同一 PSS 内用输入下降沿 + 曲线 + Continuing 维护一次性生命周期。 | 有独立 `PSS_Stop` 和对应数据库组。 | 不复制独立 PSS_Stop；MHGZ 的 Stop 合同已是稳定基础。 |
| 功能动作退出 | Root Motion owner 释放后直接恢复全量 Move PSD 搜索。 | 存在 `*_FromTraversal` 与 Traversal 专用数据库 / Chooser。 | **可借鉴概念。** |
| GA / 玩法动作 | GAS、ActionToken、精确 Commit 与所有权。 | 本次无法从资产证明同构 GAS 模型。 | 不借鉴运行时所有权模型。 |
| 转向 | 仅前向资源；C++ 转 Actor，Root +Y。 | 360°资源与 Warping/Steering。 | 不照搬。 |
| 视觉补偿 | 当前不依赖 Offset Root/方向扭曲。 | Root Offset、OrientationWarping、IK 等完整层。 | 不作为 P-01 修复手段。 |

### 4.2 可以借鉴的不是“更多节点”，而是候选资格

官方样例给 MHGZ 的可用原则是：

```text
先根据当前玩法/移动语境，决定“哪些数据库有资格参与本次搜索”
    -> 再在这个受限集合中让 Pose Search 按姿势、轨迹、曲线选最低成本帧
```

这与以下错误做法不同：

- 不是让 Chooser 或状态机直接播放某条动画；
- 不是把完整 GA Montage 动态塞进 PSD（Pose Search 索引是编辑器构建的，运行时不能把正在播放的 Section 加入）；
- 不是长 Blend、降低 `MM_Intent` 权重、强制 Idle 或输入死区；
- 不是把 GASP 的 `PSS_Stop`、Offset Root 或 360° Warping 复制进前向虫棍项目。

### 4.3 现有问题能否参考官方项目获得解决？

| MHGZ 问题 | 官方样例是否提供可借鉴方向 | 正确映射 | 不能误解成 |
|---|---|---|---|
| P-01：动作后无输入仍被 Loop 胜出 | **可以，原则层面。** `FromTraversal`、Traversal PSD 和 Chooser 说明“功能动作退出”应是独立候选语境。 | 未来为移动收刀、前向翻滚 MoveExit 等有无功能尾段的动作建立 `ExitTransition` 候选库，并在 Handoff 后优先路由至该库。 | 立即把攻击 Montage 加入普通 Move PSD，或恢复强制 Idle。 |
| P-03：收刀后自动 Walk，碰墙才停 | **可以，和 P-01 同一方向。** | 在错误 Loop 获得资格前消除交接失配；无输入时 Exit 末段自然回 Idle。 | 由非玩家松杆制造 Stop 来“刹住”。 |
| 动作期间曾推杆、退出前松杆 | **可以，概念上。** | ExitTransition 先消化必须播放的无功能尾段；在安全交接点消费一次 PendingStop，而非在功能 Montage 尾帧直接搜索全量库。 | 把 Stop 在任何 Montage 结束时强行设为 -1。 |
| Walk/Run/Sprint 的直接换 Loop 违和 | **部分可以。** 样例按 gait / transition 组织资源。 | 对已证实同相的 Run/Sprint，先路由到仅含目标 Loop 的候选库，再用现有短 Blend 平滑正确选帧；Walk/Run 仍先审计资源或相位兼容性。 | 退回由状态机强制完整播放 Start / Loop。 |
| 真实松杆后 Stop 重选 | 不需要引用官方解决。 | 继续维持已验收的一次性 Stop + Continuing 合同。 | 为了“像官方”删除 `BlockTransition` 或拆 `PSS_Stop`。 |

### 4.4 已裁定的门禁：分离 M4.2 与 M4.5，而非用 Idle 掩盖矛盾

旧门禁把普通移动、Stop 与功能动作退出塞在同一个阶段，却要求普通移动全绿后才允许实现 `MMHandoff` / ExitTransition。新遥测反证了“直接全库交接足够”的前提，故已按以下顺序裁定并写入 [阶段门禁](milestone-gates.md)：

1. **M4.2：** 只签收普通 Idle/Start/Loop/Stop、左右脚 Stop、首次 PIE；不测试 GA 结束后的交接。
2. **M4.3 → M4.4：** 先完成隔离的输入释放基础设施，再实现唯一通用的 Root Motion Phase、`MMHandoff`、Exit PSD 与 Chooser 路由。
3. **E4.2：** 只为已有且已由 Telemetry 证明失败的收刀 Walk、前向 Dodge MoveExit、拔刀/突刺路径接线退出资产。
4. **M4.5：** 用持续推杆、Handoff 前后松杆、无输入和中断矩阵签收 Exit→Loop/Stop/Idle；随后才签收 M4.1，并开始 M4.6 / E4.3。

这保留了动作退出的完整验收，只把其前置条件放在正确位置；`bMMForceIdle` 时间保持仍不是解法。

---

## 5. 后续设计决策的安全边界（不等于立即开工）

### 5.1 若决定采用官方思路，MHGZ 的最小正确形态

以下是未来设计目标，不是本轮实现指令：

```text
Gameplay GA / Montage（功能段）
  ├─ ActionToken 持有 Montage Root Motion
  ├─ 所有 Commit、伤害、窗口已完成
  └─ 到达明确的无功能 Handoff 点
        ├─ 精确释放该 Token 的 Montage Root Motion owner
        ├─ 根据 HandoffType + 当前姿态 + 当前输入路由候选库
        │    ├─ 有输入：ExitTransition -> Base Move 的正确 gait
        │    ├─ 曾有输入后松杆：ExitTransition -> 一次合法 Stop
        │    └─ 全程无输入：ExitTransition -> Idle
        └─ ExitTransition 自然进入可搜索尾段后，再开放 Base Move
```

必须保持的约束：

1. 仅处理移动收刀、前向翻滚 `MoveExit` 等“功能已经完成、后半段确实无玩法 Notify”的路径；站立收刀、翻滚 Core、左右后翻滚、攻击伤害段、硬直、死亡不应获得这种自由交接。
2. ExitTransition 必须是已在编辑器索引的独立、无功能候选资产；不得含 AttackCollision、Combo/Dodge Window、Draw/Sheathe/Kinsect Commit、GameplayCue 或资源结算 Notify。
3. 仍使用 `PSS_MH_Move` 的前向 +Y 约定；不需要创建 `PSS_Exit`，更不采用 `PSS_Stop`。
4. Chooser 只返回可搜索的 PSD 集合，不能绕开 Pose Search 直接指定动画。
5. 新输入必须立即覆盖旧 PendingStop；不能形成“动作结束后 N 秒按不动”的输入死区。

### 5.2 现在不应做的事情

- 不重新开启 `mhgz.MM.PostActionIdleHold`，不增加时长，也不把它伪装成 Exit 方案；
- 不在没有资产审计前创建/索引未知解包动作；
- 不把 `GA_IG_TuCi`、拔刀、攻击等完整 Montage 或带玩法 Notify 的 Section 放入普通 Move PSD；
- 不删 Stop 的 `BlockTransition` / Continuing 保护，不因一次 Loop 误选而盲改全部 PSS 权重；
- 不复制 GASP 的 Capsule / Offset Root / 方向扭曲栈；
- 不宣称 GASP 用状态机替代了 MM。当前证据只能说明两者在该项目中共同存在。

### 5.3 已选择的路线与当前允许工作

已选择“分离 M4.2/M4.5 + 提前唯一通用 Exit 基础设施”路线。它不改变 M4.7 批量地面招式、M5 或 E4.3 的前置；唯一提前的是为验证已有 M4.1 动作退出而不可缺少的 M4.4 / E4.2 小纵切。

M4.3 与 M4.4 已完成。当前只应按 4.4 的顺序实施并验证 E4.2；不得创建批量地面动作资产或修改具体 GA 的 Entry/Combo 行为。

---

## 6. 复核索引

- 当前运行时实现：[MHGZMotionMatchingAnimInstance.cpp](../../Source/MHGZ/Animation/MHGZMotionMatchingAnimInstance.cpp)、[MHGZMotionMatchingMath.h](../../Source/MHGZ/Animation/MHGZMotionMatchingMath.h)、[MHGZCharacter.cpp](../../Source/MHGZ/MHGZCharacter.cpp)、[MHGZWeaponRuntimeHostComponent.cpp](../../Source/MHGZ/WeaponRuntime/MHGZWeaponRuntimeHostComponent.cpp)。
- 现行门禁：[milestone-gates.md](milestone-gates.md)。
- 当前纯 MM 路线与未来 Exit 设计：[pure-motion-matching-locomotion-guide.md](pure-motion-matching-locomotion-guide.md)。
- 最新录制：`Saved/RuntimeTelemetry/20260829-114927-BP_IG_Character_C_0-53275`；其中 `MotionMatching/PoseSearchCandidates.csv`、`PoseSearchChannelCosts.csv` 是为本次根因确认导出的诊断文件。
- GASP 资产根目录：`D:\study\MH\游戏动画示例\Content\Blueprints` 与 `...\Characters\UEFN_Mannequin\Animations\MotionMatchingData`。
