# 普通移动系统重构冻结方案

> **状态：已冻结，尚未实施。** 本文是普通地面移动的下一版目标设计，不代表当前源码或 `ABP_MH_Character` 已完成这些改动；当前阶段与已知阻塞项以 [阶段门禁](milestone-gates.md#5-l普通移动重构门禁) 为准。
>
> **实施门槛：** 先完成 M4-A.5 的非移动核心代码与 E4-A 阶段资产接线；随后执行本文 L0～L5，再做 M4-A 最终移动 PIE/阶段验证。不得在完成 L5 前进入依赖基础移动接管的 M4-B.1/M5 批量动作接线。全项目 Data Validation 仍属于 M7/E7，而不是要求用未来 E5/E6 资产阻塞 E4-A。
>
> **当前系统真相源：** 重构实施前，实际行为仍以 [`MHGZCharacter`](../../Source/MHGZ/MHGZCharacter.cpp) 和 [motion-matching.md](motion-matching.md) 为准。本文实施完成并通过验收后，普通 locomotion 章节才取代旧 MM Root Motion 主链路。
>
> **执行文档：** AI/程序员按 [代码侧设计与实施指南](locomotion-refactor-code-guide.md) 逐阶段改代码；代码编译后按 [UE5.6 编辑器具体操作指南](../editor/locomotion-refactor-setup.md) 制作资产、接 AnimBP 并执行 PIE。本文只维护架构和不可违反的合同。

## 1. 冻结结论

只重构**普通地面 locomotion**，不推倒 GAS 动作系统：

- 普通走、跑、冲刺的胶囊位移改由 `UCharacterMovementComponent`（下称 CMC）负责。
- Idle、Start、Loop、Stop 动画只负责表现，不再决定普通移动的实际位移。
- 普通启停使用 Distance Matching；走/跑/冲刺循环使用 Sync Marker/Sync Group 保持脚相。
- 攻击、翻滚、收刀、拔刀及特殊招式继续使用现有 Montage Root Motion、RootMotionSource 或 MovementTask。
- `Combat.State.BlockMovement`、精确 ActionToken、`MontageRootMotionOwner`、动作旋转所有权和 Commit Notify 合同保持有效。
- 不保留“旧 MM Root Motion 与新 CMC locomotion 可长期切换”的双运行时架构。迁移期可使用短暂测试基线，验收后删除旧普通移动路径。

### 1.1 位移所有权矩阵

| 场景 | 位移所有者 | 动画/表现所有者 | 约束 |
|---|---|---|---|
| 普通走/跑/冲刺 | CMC | Locomotion 状态机 | Locomotion 序列不得向胶囊贡献 Root Motion |
| 普通 Start/Stop | CMC | Distance Matching 序列 | 动画时间按实际移动/剩余停止距离推进 |
| 攻击的动画位移 | Montage Root Motion | 对应攻击 GA/Montage | 继续使用 ActionToken 和 RootMotionOwner |
| 无动画根位移的特殊招式 | RootMotionSource/MovementTask | 对应 GA/Montage | 由 GA 明确创建、取消和清理 |
| 前向翻滚 | Montage Root Motion | `GA_Dodge` | 现有方向冻结、Exit 分支和无敌合同保留 |
| 移动收刀 | Montage Root Motion | `GA_Sheathe` | 现有选段和实时转向合同保留 |
| 拔刀小位移 | Montage Root Motion | Draw GA | 不能据此触发普通 Stop |
| 送虫/收虫 | CMC | `UpperBody_IGAction` 上半身层 | 不持有 BlockMovement/RootMotionOwner |

## 2. 重构原因与已确认故障

当前普通移动使用“手工直线预测 `FTransformTrajectory` → Motion Matching 选帧 → AnimBP Root Motion 驱动胶囊”的闭环。现有动画只有正向的收刀走/跑启停循环、持刀单套启停循环和单独冲刺循环，候选覆盖不足以稳定表达动作语义。

PIE 已确认以下行为：

1. PSS 加入负 Offset，并阻止进入 Stop 尾段，能显著降低原地误选 Stop 的频率，但持续移动会反复重选某只脚附近几帧并抽搐。
2. 删除负 Offset 后持续移动恢复，但正常 Stop 几乎不再触发。
3. PIE 启动后的第一次拔刀会完整触发一次持刀 Stop；后续收刀/拔刀通常正常。拔刀 Montage 自身含小幅根位移。
4. 减少远期 PSS 样本、增加速度 Flags 或调整权重只能改变发生概率，不能表达“动作位移不是普通停步”“第一次进入持刀态必须回 Idle”等语义。

因此冻结以下判断：

- 旧 PSS 的动画时长型 Offset 配置有缺陷，但不是 Stop 误触的唯一原因。
- 运行时没有可靠的真实负时间轨迹时，单纯在 PSS 增加负 Offset 会使移动候选与查询历史不一致。
- Stop 是否可用属于 locomotion phase 规则，不能只由 Pose Search 成本决定。
- 第一次拔刀问题是新持刀候选池无稳定 Continuing Pose、动作尾姿/小位移与 Stop 候选混淆的结果；继续调 PSS 不能给出确定性保证。

## 3. 目标运行时数据流

```text
Enhanced Input Move
  -> DoMove 保存 RawMoveInput / LastMovementInputDir
  -> CalcCruiseSpeed(InputMagnitude, Stance, SprintHeld, MoveSpeedMultiplier)
  -> TargetCruiseSpeed / TargetGait
  -> CMC.MaxWalkSpeed + AddMovementInput
  -> CMC Velocity / Acceleration / PredictedStopLocation
  -> AnimBP Locomotion Snapshot
  -> Idle / Start / Loop / Stop 表现
  -> DefaultSlot / UpperBody_IGAction 等动作层
```

普通移动的游戏响应和动画选择彻底解耦：摇杆有效的同一游戏帧即可向 CMC 提交输入；动画的 Blend Time、Start 长度和 Stop 选帧不再延迟胶囊移动。

## 4. C++ 目标合同

### 4.1 保留的输入数据

`AMHGZCharacter` 继续提供：

- `RawMoveInput`
- `LastMovementInputDir`
- `InputMagnitude`
- `bHasInput`
- `TargetCruiseSpeed`
- `bUnsheathed`
- `TurnRate` 和现有最短角差转向

`DoMove` 在 `BlockMovement` 期间仍更新 `RawMoveInput` 和 `LastMovementInputDir`，供 InputSnapshot、动作入口方向和 MoveExit 使用；但不得向 CMC 提交普通移动输入。

### 4.2 普通移动提交

目标逻辑为：

```cpp
TargetCruiseSpeed = CalcCruiseSpeed(InputMagnitude) * MoveSpeedMultiplier;
CMC->MaxWalkSpeed = TargetCruiseSpeed;

if (bHasInput && !ShouldBlockMovement())
{
    AddMovementInput(LastMovementInputDir, 1.0f);
}
```

`CalcCruiseSpeed` 已经使用摇杆幅度映射目标速度，因此 `AddMovementInput` 不再重复乘 `InputMagnitude`。若实施时改为固定 `MaxWalkSpeed + AnalogInputModifier`，必须作为显式替代决策更新本文，不能同时使用两次幅度缩放。

CMC 的 `MaxAcceleration`、`GroundFriction`、`BrakingDecelerationWalking` 和可选 Separate Braking Friction 由手感调校决定；本文不冻结具体数值，但要求全部为可配置字段，并由 Distance Matching 使用同一组实际参数预测停止位置。

### 4.3 动画读取的权威量

AnimBP 不再把 `DesiredSpeed` 当作实际移动速度。目标输入为：

- `Speed2D = CharacterMovement.Velocity.Size2D()`
- 当前 Acceleration/LastUpdateVelocity
- `bHasInput`
- `InputMagnitude`
- `TargetCruiseSpeed`
- 冻结的 `TargetGait`
- `bUnsheathed`
- `bActionMovementLocked`/等价动作状态
- 预测停止位置和当前剩余停止距离

`DesiredSpeed` 和 `bForceMMIdle` 在迁移期保留以避免旧 AnimBP 立即断引用；新 AnimBP 验收后删除或明确重命名，不把它们作为永久兼容字段。

### 4.4 BlockMovement 边沿

- `ShouldBlockMovement()==true` 时不调用 `AddMovementInput`，但继续记录原始摇杆。
- 不得在 Character Tick 中每帧调用 `StopMovementImmediately()`；这可能清除动作 Root Motion 转换出的速度或破坏交接。
- 若某动作进入时必须清除普通移动惯性，应在该动作取得移动锁的边沿清理一次，并由动作策略明确请求。
- 动作中允许移动的能力不得取得 `BlockMovement`；送虫/收虫继续属于这一类。

## 5. AnimBP 目标结构

### 5.1 Root Motion 模式

`ABP_MH_Character` 的目标设置：

```text
Root Motion Mode = Root Motion From Montages Only
```

所有普通 locomotion 播放资产必须是安全的 in-place 表现：

- 原始 Root Motion 资产保留，供 Distance Curve 生成、速度测量和审计。
- 建议复制运行时 locomotion 版本，避免破坏解包源资源。
- 运行时版本可先验证 `Enable Root Motion=true`、`Force Root Lock=true`、`Root Motion Root Lock=Anim First Frame`；若 Mesh 仍相对 Capsule 漂移或回弹，必须烘焙真正移除根平移的 in-place 副本。
- 不得用 `No Root Motion Extraction` 直接播放仍含移动根轨迹的序列，否则 Mesh 会离开 Capsule 后回弹。

### 5.2 AnimGraph

目标骨架：

```text
[Locomotion State Machine]
  -> [可选 Stride Warping / Foot IK]
  -> [UpperBody_IGAction Layered Blend per Bone]
  -> [DefaultSlot：全身攻击/翻滚/收拔刀]
  -> Output Pose
```

`UpperBody_IGAction` 继续从骨盆上方第一根脊椎骨开始混合；Send/Recall 下半身始终来自普通 locomotion。

## 6. Locomotion 状态与确定性规则

### 6.1 状态和步态

目标表现状态：

```text
ELocomotionPhase
  Idle
  Starting
  Looping
  Stopping

ELocomotionGait
  Walk
  Run
  Sprint
```

收刀/拔刀仍由 `bUnsheathed` 或等价 Stance 维度选择不同资产集；不把 Armed 混入 Gait 枚举。持刀态当前只有一个普通移动速度，可映射到固定 Gait 但使用持刀资产集。

### 6.2 状态转换

```text
Idle --有效输入--> Starting --启动完成/同步点--> Looping
Looping --普通移动输入释放--> Stopping --实际速度归零--> Idle
Stopping --重新输入--> Starting 或 Looping
Starting --输入释放--> Stopping 或 Idle（按实际速度）
```

规则：

1. Start 变体在进入 `Starting` 时按 Stance 和目标 Gait 冻结一次；不得因摇杆阈值轻微变化每帧切换 Start 资产。
2. Walk/Run 在 Start 期间变化时，默认继续当前 Start，在进入 Loop 时选择最新 Gait；如需中途切换，只能在共同 Sync Marker 处执行。
3. Stop 变体在普通 locomotion 的 `Looping/Starting -> 无输入` 边沿冻结；不得仅凭一次 `Speed2D > 0 -> 0` 推导 Stop。
4. Stop 可被新输入立即打断；胶囊由 CMC 当帧响应，动画以短 Inertialization 进入 Start/Loop。

### 6.3 动作进入与退出

动作位移不得伪装成普通 locomotion 历史。确定性规则：

```text
动作开始：取消 Pending Stop；基础 locomotion 按动作策略维持 Idle 或继续移动。

动作结束且无有效摇杆：进入 Idle。
动作结束且有有效摇杆：进入 Starting/Looping。
动作结束：禁止仅因 Montage Root Motion Velocity 进入 Stopping。
```

Stop 的授权至少要求：

```text
bWasStandardLocomoting
&& bMoveInputReleased
&& !ShouldBlockMovement()
&& !ActionOwnsMovement
```

这条合同必须消除“PIE 第一次拔刀完整播放持刀 Stop”。拔刀的小幅根位移不属于 `bWasStandardLocomoting`。

## 7. 动画同步方案

### 7.1 Distance Curve

启用 UE `Animation Locomotion Library`。从保留根轨迹的源动画生成 XY Distance Curve：

- Start：使用 `Advance Time By Distance Matching`，按胶囊每帧实际移动距离推进。
- Stop：使用 `Predict Ground Movement Stop Location` 与 `Distance Match To Target`，按剩余停止距离选姿势。
- Loop：从保留根位移的源序列测量 `AuthoredLoopSpeed`；真正去根平移的运行时序列使用 `PlayRate = Speed2D / AuthoredLoopSpeed`，再按需要增加 Stride Warping。只有运行时序列仍保留可提取 Root Motion 且已证明不会造成 Mesh 漂移时，才直接使用 `Set Playrate To Match Speed`。

Distance Curve 的压缩设置必须满足运行时索引要求；具体点击步骤见 [普通移动重构编辑器指南](../editor/locomotion-refactor-setup.md#34-生成-distance-曲线)。

### 7.2 Sync Marker

所有可相互混合的 Walk/Run/Sprint Start/Loop 至少统一：

```text
Sync Group: Locomotion
Markers: LeftFootPlant, RightFootPlant
```

- Start 末段与对应 Loop 必须有共同落脚 Marker。
- Walk/Run/Sprint Loop 只有在共同 Marker 名称和脚相正确后才能加入同一 Sync Group。
- 建议 Start -> Loop 使用同步点加 `0.05~0.12s` 的 Inertialization；最终数值由 PIE 调校，不冻结为硬编码。
- 不用大 Blend Time 掩盖相反脚相或根骨漂移。

### 7.3 冲刺资产

单独收刀冲刺动画重新纳入目标系统。其原始根位移速度与 Run 相同不再构成淘汰理由，因为胶囊速度由 CMC 决定。Sprint 的表现速度通过 PlayRate、Stride Warping 及 CMC `SprintCruise` 对齐。

## 8. 与现有动作系统的兼容边界

### 8.1 必须保留

- `Combat.State.BlockMovement` 的单 Token 所有权和结束清理。
- `(Mesh, MontageInstanceID) -> ActionToken` 精确 Notify 路由。
- `MontageRootMotionOwner` 的唯一所有权。
- 动作旋转 Token；动作拥有旋转时，普通 Character Tick 不得抢写 Yaw。
- `AnimNotify_DrawCommit`、`AnimNotify_SheatheCommit` 的姿态提交时机。
- Dodge 方向冻结、GA 自身 `SectionChanged` 出口选择、`DodgeWindow` 与 `DodgeAcceptWindow` 的职责分离。

### 8.2 动作 Root Motion 与 CMC

- Montage Root Motion 或 RootMotionSource 生效时继续优先于普通 CMC 移动。
- 普通 locomotion 序列已 in-place，因此不再需要 `bForceMMIdle` 来防止“MM RM + Montage RM”叠加；但删除字段前必须完成 AnimBP 迁移。
- 前向 Dodge 的 GA 在自身 Montage 实际进入 `MoveExit` 时、移动收刀则在其对应阶段，释放本 Action 的 `BlockMovement` 以读取实时输入和转向；只要 Montage 仍贡献根位移，普通 CMC 位移不会成为动作位移所有者。
- 若要求 CMC 在某个 Notify 帧立即接管位移，该 Notify 必须位于 Montage 不再贡献根平移的首帧；否则只能在 Montage RM 结束后接管。
- 不得让 CMC、Montage RM、RootMotionSource 在同一阶段被设计为三个可叠加的位移写者。

### 8.3 M4-B.1 文档术语迁移

旧文档中的：

```text
LockedRootMotion -> SteeringRootMotion -> MotionMatching
```

实施本文后改为：

```text
LockedRootMotion -> SteeringRootMotion -> CharacterLocomotion
```

`ActionRootMotionPhase` 的精确所有权不变；改变的是最后由 CMC+Locomotion State Machine 接管，而不是 MM Root Motion。本文实施前不得批量按旧术语完成 M4-B.1/M5 资产验收。

## 9. 迁移顺序

### L0：冻结与临时基线（当前）

- 完成 M4-A.5 非移动核心代码和资产接线。
- 停止继续扩展旧 PSS/Trajectory 方案。
- 为稳定测试可临时从 PSD 移除 Stop，并使用无负 Offset 的可行基线；该状态不满足最终移动验收。
- 不因临时无 Stop 宣称 M4-A 完成。

### L1：资产预处理

- 盘点收刀/持刀 Idle、Start、Loop、Stop、Sprint 源资源。
- 保留 Root Motion Source；创建或验证 in-place 运行时版本。
- 测量每条 Loop 的原始速度、周期和左右脚落点。
- 为 Start/Stop 生成 Distance Curve；为可混合序列布置统一 Sync Marker。

### L2：CMC 普通移动纵切

- `DoMove` 接入 CMC，`MoveSpeedMultiplier` 接入目标速度。
- 暂时只接 Idle + Loop，不接 Start/Stop。
- AnimBP 改为 `Root Motion From Montages Only`。
- 验证普通移动、碰撞、斜坡、转向和所有现有动作 Root Motion。

退出条件：无 PSS 抽搐、无普通移动双位移、第一次拔刀不触发 Stop、动作 Montage 位移未回归。

### L3：确定性 Start/Stop

- 加入 Locomotion Phase/Gait。
- Start 使用距离推进或速度匹配。
- Stop 使用预测停止位置和 Distance Matching。
- 加入动作进入/退出规则，确保动作位移不生成 Stop 请求。

### L4：走/跑/冲刺同步与表现

- 接入 Walk/Run/Sprint 和持刀资产集。
- 验证 Sync Group、脚相、PlayRate/Stride Warping。
- 调校 CMC 加速度、制动和停止距离。

### L5：清理旧路径与更新文档

- 删除手工 `FTransformTrajectory` 和普通 locomotion MM 节点/资产引用。
- 删除或重命名 `DesiredSpeed`、`bForceMMIdle` 等旧字段。
- 将 `motion-matching.md` 标记为历史实现或重写为现行 Locomotion 文档。
- 更新 M4-B.1/M5 的最终接管术语和编辑器验收。
- 不保留永久 Legacy/New 运行时开关。

## 10. 当前任务边界

本文实施前仍可完成：

- M4-A.5 普通攻击入口瞬转代码。
- `GA_IG_SendKinsect` / `GA_IG_RecallKinsect` 的上半身 Montage、精确 Commit 和猎虫生命周期。
- 两个 Y 拔刀 GA、Draw Montage/Commit 和 Combo 边。
- Tag、ActionToken、Commit 前后中断、资源与伤害等非移动自动化测试。

必须延后到本文实施后最终验收：

- 第一次拔刀后的 Idle/Move 接管。
- Send/Recall 期间下半身移动最终平滑度。
- Dodge MoveExit、移动收刀与基础 locomotion 的无缝接管。
- 正常 Walk/Run/Sprint Start/Stop 和急速反向输入。
- 所有依赖 `MotionMatching` 最终接管的 M4-B.1/M5 项目。

## 11. 验收合同

### 11.1 普通移动

1. 摇杆越过死区的同一游戏帧向 CMC 提交移动输入；动画 Blend 不延迟胶囊响应。
2. 持续 Walk/Run/Sprint 30 秒无单脚帧反复重选、Mesh 回弹或 Capsule 双位移。
3. Walk/Run/Sprint 切换保持正确脚相；冲刺使用独立表现资产。
4. 松开摇杆只播放一次正确 Stop，并按实际停止距离结束到 Idle。
5. Stop 中重新输入可立即恢复移动，不等待 Stop 动画完整播放。
6. 收刀与持刀使用各自资产集，不串姿势。

### 11.2 动作交接

1. PIE 第一次及后续拔刀，无输入均回持刀 Idle，有输入进入持刀 Start/Loop，绝不触发普通 Stop。
2. 拔刀/收刀 Commit 前后中断姿态合同不变，武器 Socket 不回归。
3. 前向 Dodge 的 Core 方向冻结；允许的 MoveExit 可读取实时摇杆并最终无缝交给 CMC。
4. 持刀左右后 Dodge 仍强制 IdleExit；收刀左右后仍拒绝且不耗耐。
5. 移动收刀继续可转向，Montage RM 与 CMC 不发生叠加位移。
6. Send/Recall 全程可移动、转向，下半身 locomotion 不被上半身 Montage 重置。
7. 攻击、翻滚和特殊 MovementTask 的取消/中断不残留 CMC 锁、RootMotionSource 或所有权 Token。

### 11.3 自动化与人工验证

- 为普通移动锁边沿、动作退出不生成 Stop、Gait 冻结和 MoveSpeedMultiplier 增加 C++ 自动化测试。
- Development Editor 构建和既有 `MHGZ`/`MHGZ.M4` 测试必须继续通过。
- PIE 使用 Rewind Debugger/Anim Insights 验证 State、Sequence Time、Root Motion 来源和 Capsule Velocity。
- Data Validation 检查 Locomotion 资产引用、Curve、Marker、Loop/InPlace 设置和缺失 Stance 变体。

## 12. 非目标

本轮重构不包含：

- 完整 360° 多方向 locomotion 动画生产。
- 锁定怪物后的独立 strafing 资产集。
- 高级 Pivot/急转动画、Motion Warping 自动补齐。
- 正式脚步 IK、坡度 Warping 和网络专项优化；它们可在基础纵切稳定后追加。
- 修改攻击、猎虫、精华、伤害、输入组合键或 ComboData 的玩法规则。

这些内容不得以“移动重构顺手处理”为由扩大 L1～L5 的实施范围。
