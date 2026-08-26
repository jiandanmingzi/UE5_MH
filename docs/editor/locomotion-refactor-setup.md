# 普通移动重构：UE5.6 编辑器具体操作指南

> **状态：历史备选，禁止按本文接线。** 2026-08-25 已改用 [纯 Motion Matching 普通移动实施指南](../design/pure-motion-matching-locomotion-guide.md)。本文只保留 CMC + 状态机回退步骤；除非用户明确撤销纯 MM 决策，否则不得执行下面的 L0～L5 编辑器操作。
>
> **最终目标：** 普通移动由 CMC 位移，AnimBP 状态机只表现 Idle/Start/Loop/Stop；全身动作继续走 `DefaultSlot`，送虫/收虫继续走一个固定的 `UpperBody_IGAction` 上半身层。不要重新加入按 `IsMoving` 切换两套骨骼遮罩的图。

## 1. 阶段顺序与职责

| 阶段 | 先完成的代码 | 本文编辑器任务 | 通过后才能做 |
|---|---|---|---|
| L0 | 无生产代码改动 | 固定旧系统复现与资产基线 | L1 |
| L1 | 启用插件并编译 | 盘点、复制、去根位移、曲线、Marker | L2 代码 |
| L2 | CMC 纵切已编译 | 只接 Idle + Loop，切换 Root Motion Mode | L3 代码 |
| L3 | Start/Stop 事件已编译 | 接 Start/Stop、Distance Matching 和动作交接 | L4 代码 |
| L4 | 完整 Gait/CMC 参数已编译 | 接 Walk/Run/Sprint/持刀变体并调脚相 | L5 代码 |
| L5 | 旧字段已清理并编译 | 删除断开的 MM/PoseHistory/Trajectory，最终验证 | M4-A 最终移动验收 |

任何阶段出现动作 Root Motion 回归，都回到该阶段修复；不要继续下一阶段后再一起排查。

## 2. L0：冻结现状和复现基线

### 2.1 保存阶段快照

1. 关闭 PIE，保存所有已修改资产。
2. 编译并保存 `ABP_MH_Character`、`BP_MHGZCharacter`、`BP_PlayerState`、当前 GA、Montage 和 Runtime DataAsset。
3. 在 Source Control 中记录 M4-A.5/E4-A 快照。不要在 Content Browser 外复制 `.uasset`。
4. 确认持刀向左翻滚 Montage 至少有 `DodgeCore` 和 `IdleExit`，并完成一次 PIE 回归；该问题不属于普通移动重构。

### 2.2 录制旧系统的四个基线

在同一测试地图、同一角色蓝图下分别录制：

1. 收刀持续直线走、跑、冲刺各 10 秒。
2. 持刀持续移动 10 秒并多次松开摇杆。
3. PIE 启动后的第一次拔刀，以及后续收刀/拔刀各 3 次。
4. 攻击、前翻滚 `MoveExit`、移动收刀、送虫和收虫结束后的移动交接。

同时打开 `showdebug animation` 或 Rewind Debugger，记录：

- 当前 Motion Matching 候选；
- Mesh 与 Capsule 是否分离；
- Character Velocity；
- 首次拔刀是否误进 Stop；
- 持续移动是否反复选中单脚几帧。

### 2.3 冻结旧 PSS

- 从此阶段起不再改 PSS 的 Pose/Trajectory 权重、正负采样时间、Search Throttle 或候选数据库。
- 为了稳定测试，可以临时从旧 PSD 移除 Stop 资产，但必须记录这是 L0 临时基线，不能作为最终验收。
- 不删除旧节点或资产；真正清理由 L5 完成。

L0 退出标准：旧问题有可对照录像/日志，非移动动作仍可触发，且能回退到该快照。

## 3. L1：插件与资产预处理

### 3.1 启用插件

1. 打开 Unreal Editor。
2. 选择 `编辑（Edit） -> 插件（Plugins）`。
3. 搜索 `Animation Locomotion Library`。
4. 勾选启用，接受 Beta 插件提示。
5. 关闭编辑器并重新启动项目。
6. 随便打开一条动画序列，在 `窗口（Window） -> Animation Data Modifiers` 中确认可以添加 `DistanceCurveModifier`。

如果插件列表没有该项，先确认正在使用 UE5.6 安装 `C:\apps\UE5\UE_5.6`。不要用 Pose Search 的 Distance Channel 替代该修改器。

### 3.2 建立资产工作区

不要移动或覆盖解包源资产。建议在 Content Browser 新建：

```text
/Game/Characters/Demo/Anims/LocomotionRefactor/
  SourceWithRM/
    Sheathed/
    Unsheathed_IG/
  RuntimeInPlace/
    Sheathed/
    Unsheathed_IG/
```

在编辑器中右键源资产 `Duplicate`，不要在 Windows 资源管理器中复制。

当前已知候选如下。名称中的 `Shth` 不足以证明实际姿态，必须逐条打开目测武器姿势后再填角色：

| 姿态 | 用途 | 当前候选源资产 |
|---|---|---|
| 收刀 | Idle | `/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/AS_Shth_Idle` |
| 收刀 | Walk Start/Loop/Stop | 同目录 `AS_Shth_Walk_Start/Loop/Stop` |
| 收刀 | Run Start/Loop/Stop | 同目录 `AS_Shth_Run_Start/Loop/Stop` |
| 收刀 | Sprint Start/Loop/Stop | 同目录 `AS_Shth_Sprint_Start/Loop/Stop` |
| 持刀 | Idle | `/Game/Weapons/InsectGlaive/Anims/Sequences/Locomotion/AS_Shth_Idle` |
| 持刀 | Start/Loop/Stop | 同目录 `AS_Shth_Walk_Start/Loop/Stop` |
| 候选 | 单独冲刺表现 | `AS_Shth_Dash_Walk`；L4 前只试听，不默认接入 |

`AS_Shth_ShouDao_Idle` 和 `AS_Shth_ShouDao_Walk` 是收刀动作/衔接候选，不直接当普通持刀 Idle/Loop。只有目测、根轨迹和循环边界都符合时才能改表。

### 3.3 为每个资产填写审计表

在项目笔记或本文件旁的阶段记录中为每条资产填写：

| 字段 | 填写内容 |
|---|---|
| Source Asset | 原始资产完整路径 |
| Stance/Gait/Phase | Sheathed/Unsheathed + Walk/Run/Sprint + Idle/Start/Loop/Stop |
| Length | 动画时长 |
| Root XY Distance | 根骨骼水平总位移，cm |
| AuthoredLoopSpeed | Loop 的 `Root XY Distance / Length`，cm/s |
| First Plant | 第一处明确落地脚：Left 或 Right |
| Cycle Markers | 每个 `LeftFootPlant` / `RightFootPlant` 的时间 |
| Runtime Asset | 对应 in-place 资产路径 |

Loop 的 `AuthoredLoopSpeed` 必须从保留根位移的源序列测量。真正的 in-place Loop 根位移为零，不能再从它自动计算原始速度。

推荐用导出该动画的 DCC/动画检查工具读取 Root Bone 第一帧和最后一帧的平移：

```text
RootXYDistance = sqrt((EndX-StartX)^2 + (EndY-StartY)^2)
AuthoredLoopSpeed = RootXYDistance / SequenceLengthSeconds
```

单位统一为 UE 的 cm 和 s。若循环跨过导出边界导致最后一帧与第一帧重合，改为读取“一整个有效步态周期”的累计根轨迹长度，不能把结果误记为 0。把计算过程和原始帧值写进审计表，L4 不凭目测重新猜速度。

### 3.4 生成 Distance 曲线

先在 `SourceWithRM` 副本上操作：

1. 双击 Start 或 Stop 动画。
2. 在 Asset Details 的 `Root Motion` 分类勾选 `Enable Root Motion`。`DistanceCurveModifier` 会拒绝没有 Root Motion 的资产。
3. 打开 `Window -> Animation Data Modifiers`。
4. 点击 `Add Modifier`，选择 `DistanceCurveModifier`。
5. 设置：
   - `Sample Rate = 30`；快速位移可改为 `60`，同一资产集保持一致。
   - `Curve Name = Distance`。
   - `Axis = XY`。
   - Start：`Stop At End = false`，`Stop Speed Threshold = 5`。
   - Stop：`Stop At End = true`。
6. 点击 `Apply All Modifiers`，保存。
7. 在动画曲线面板检查：
   - Start 应从接近 `0` 开始，随前进总体递增为正值。
   - Stop 应从负的剩余距离开始，在动画末尾到 `0`。
   - 曲线不得在中间突然大幅反号或回跳。

若 Start 修改器把中途某个慢速点识别为起点，先检查源动画是否包含多个阶段。不要手工把复杂招式动画硬改成普通 Start；应选择只包含单次起步的序列，或在动画工具/DCC 中裁出干净片段后重做。

### 3.5 制作真正的 Runtime In-Place 副本

每个状态机播放资产必须满足：胶囊不动时，整段播放期间 Mesh 的根节点不会持续离开原点。

操作原则：

1. 从已生成 `Distance` 曲线的 `SourceWithRM` 副本再 Duplicate 到 `RuntimeInPlace`。
2. 使用你的动画处理/DCC 流程把 Root Bone 的水平平移烘焙为零，同时保留骨盆和四肢动作；重新导入到这个 Runtime 副本。
3. 重新导入后确认 `Distance` 曲线仍存在；若导入覆盖了曲线，从保留根位移源资产复制曲线数据或重新执行可靠的资产处理流程。
4. Runtime 副本关闭 `Enable Root Motion`，因为普通状态机不应申请根位移。
5. 保存后在动画编辑器循环播放，打开地面/根骨骼显示，确认 Mesh 不从预览原点漂走，也不会结尾突然弹回。

UE5.6 没有适用于所有导入动画的通用“一键删除水平 Root Translation”按钮。`Force Root Lock` 只能帮助验证某些资产；它不能作为已经制作出 in-place 数据的证明。若无法得到真正的 Runtime In-Place 副本，L1 尚未完成，不能进入 L2。

### 3.6 添加 Sync Marker

在每个会互相混合的 Start/Loop 序列中：

1. 打开 Runtime In-Place 动画。
2. 在 `Notifies` 轨道区域右键，选择 `Add Sync Marker`。
3. 在左脚完全承重、脚掌速度最低的帧放置 `LeftFootPlant`。
4. 在右脚完全承重、脚掌速度最低的帧放置 `RightFootPlant`。
5. Loop 至少各有一个 Left/Right Marker；循环首尾不得重复放一个会造成零长度区间的同名 Marker。
6. Start 末段至少要有一个与目标 Loop 同名、同一只脚的 Marker。
7. Stop 的 Marker 可帮助入 Stop 脚相，但 `Distance Match To Target` 本身不会自动保持上一 Loop 的相位；当前只有单方向 Stop 时，先保证停止距离正确，再在 L4 调脚相。

Marker 名必须逐字一致：

```text
LeftFootPlant
RightFootPlant
```

不要使用普通 AnimNotify 代替 Sync Marker。

### 3.7 L1 验收

- 每个将接线的 Start/Stop Runtime 资产都有名为 `Distance` 的曲线。
- SourceWithRM 保留真实根位移；RuntimeInPlace 不漂移。
- 每个 Loop 的 AuthoredLoopSpeed 已记录，不能留空或写 0。
- 可混合 Start/Loop 的 Marker 名和左右脚正确。
- 现有动作 Montage 未被改成 in-place。

## 4. L2：用 Idle + Loop 完成 CMC 纵切

本节只在 L2 C++ 编译成功、`LocomotionSnapshot` 可被 AnimBP Property Access 读取后执行。

### 4.1 修改 AnimBP Root Motion Mode

1. 打开 `ABP_MH_Character`。
2. 点击工具栏 `Class Defaults`。
3. 搜索 `Root Motion Mode`。
4. 设置为 `Root Motion From Montages Only`。
5. Compile、Save。

不要选择 `Root Motion from Everything`，否则普通状态机序列可能再次影响胶囊；也不要选择 `No Root Motion Extraction` 作为解决 Mesh 漂移的办法。

### 4.2 建立线程安全的变量读取

在现有 `Blueprint Thread Safe Update Animation` 函数中，或新建同名线程安全函数，使用 Property Access 从 Character/CMC 更新：

- `LocomotionSnapshot.Speed2D`
- `LocomotionSnapshot.bHasLocomotionInput`
- `LocomotionSnapshot.bStandardLocomotionEnabled`
- `LocomotionSnapshot.bUnsheathed`
- `LocomotionSnapshot.TargetGait`
- `LocomotionSnapshot.TargetCruiseSpeed`
- `CharacterMovement.Velocity`
- `CharacterMovement.CurrentAcceleration`

若当前 `getCharacterParams` 使用普通 Cast + Getter 且能稳定编译，可在 L2 暂留；但删过或改名的属性节点出现“已尝试访问缺失的属性”时，应删除旧 Getter 节点并从当前 Character 引脚重新拖出，不要只 Refresh Node。

### 4.3 创建状态机

1. 在 AnimGraph 空白处右键，创建 State Machine，命名 `SM_CharacterLocomotion`。
2. 打开状态机，创建 `Idle` 和 `Loop` 两个 State。
3. 将 Entry 连到 `Idle`。
4. `Idle -> Loop` 条件：

```text
bStandardLocomotionEnabled && bHasLocomotionInput
```

5. `Loop -> Idle` 条件：

```text
!bStandardLocomotionEnabled || !bHasLocomotionInput
```

6. 两个 Transition 暂设：
   - `Crossfade Duration = 0.05`。
   - `Blend Mode = Standard Blend`。
   - `Transition Type` 不使用 Inertialization；若选择 Inertialization，必须在后方添加 Inertialization 节点，否则会出现“未找到请求来自的惯性化节点”。

### 4.4 配置 Idle State

1. 打开 `Idle`。
2. 放置 `Blend Poses by Bool`，Active Value 接 `bUnsheathed`。
3. False Pose 接收刀 Runtime Idle；True Pose 接虫棍持刀 Runtime Idle。
4. True/False Blend Time 暂设 `0.0`，保持此前已验证的收刀/持刀硬切基线。
5. 输出到 State Result。

### 4.5 配置 Loop State

L2 先证明位移链，不做完整 Gait 混合：

1. 放置 `Blend Poses by Bool`，Active Value 接 `bUnsheathed`。
2. False Pose 暂接收刀 Run Runtime In-Place Loop。
3. True Pose 暂接虫棍持刀 Runtime In-Place Loop。
4. 两个 Sequence Player 勾选 Loop。
5. 暂时直接设置 Play Rate，使腿速大致匹配当前 CMC；L4 再按 AuthoredLoopSpeed 精确驱动。
6. 输出到 State Result。

### 4.6 接回现有动作层

最终 AnimGraph 主干改为：

```text
SM_CharacterLocomotion
  -> Save Cached Pose "LocomotionBase"

Use Cached Pose "LocomotionBase"
  -> Slot "UpperBody_IGAction"
  -> Save Cached Pose "KinsectActionPose"

Use "LocomotionBase" ---- Base Pose --------------------┐
Use "KinsectActionPose" - Blend Pose 0                  |
                     -> Layered Blend per Bone ----------┘
                     -> Slot "DefaultSlot"
                     -> Output Pose
```

`Layered Blend per Bone` 保持一个固定上半身分支：

- Blend Mode：`Branch Filter`。
- Branch Bone：当前已验证的 `spine_01`。
- Blend Depth：覆盖 `spine_01` 及其全部子骨骼；按节点版本使用足够深的正值或项目现有有效设置。
- Mesh Space Rotation Blend：保持当前已验证值，不在 L2 顺手改变。
- Blend Weight：`1.0`。

删除/绕过此前为了“静止全身、移动上半身”而建立的 `IsMoving -> Blend Poses by Bool -> 两个 Layered Blend per Bone` 分支。最终只有一个固定的上半身操虫遮罩：静止时下半身来自 Idle，移动时下半身来自 Loop，因此不需要切换遮罩。

`DefaultSlot` 必须在 Layered Blend 之后，确保攻击、翻滚、收刀和拔刀等全身 Montage 最后覆盖；不要重命名现有 Slot/Group。

### 4.7 断开旧 MM，但暂不删除

- 断开两个 Motion Matching 节点、收刀/持刀 Bool 分支、Pose History 与 Trajectory 到最终输出的连接。
- 把旧节点移动到带注释框的隔离区，命名 `L5 DELETE - Legacy Locomotion MM`。
- 不删除相关变量和资产，保证 L2 可回退、也让 L5 能明确清理。

### 4.8 L2 PIE 清单

依次验证：

1. 收刀推摇杆：胶囊同帧开始移动，无等待 Loop 动画。
2. 松开摇杆：L2 直接回 Idle；没有 Stop 是预期。
3. 收刀走、跑、Sprint 的速度可不同，虽然暂时共用 Run 表现。
4. 持刀移动：使用持刀 Loop，角色不会触发旧 PSS Stop。
5. 第一次拔刀：不会播放持刀 Stop。
6. 前翻滚、左右后持刀翻滚、站立收刀、移动收刀、拔刀攻击：Montage 位移量与 L0 基线一致。
7. 送虫/收虫：上半身动作可见，下半身继续移动；静止时下半身保持 Idle。
8. 靠墙、上斜坡、走台阶：Capsule 与 Mesh 不分离。

任一 Montage 出现位移翻倍或归零，先检查普通 Runtime 序列是否真正 in-place、Root Motion Mode 是否正确、C++ 是否在 `IsMontageRootMotionOwned()` 时仍提交 CMC 输入。

## 5. L3：接入确定性 Start/Stop

本节只在 L3 C++ 快照已经提供 `bRequestStart`、`bRequestStop`、`bForceLocomotionIdle` 后执行。

### 5.1 扩展状态机

增加 `Start`、`Stop` 两个 State，最终结构：

```text
Idle -> Start -> Loop -> Stop -> Idle
          |        ^       |
          +-> Stop |       +-> Start（重新输入）
                   +--------- Start（Loop 中重新授权）
```

推荐 Transition：

| From -> To | 条件 |
|---|---|
| Idle -> Start | `bRequestStart && bStandardLocomotionEnabled` |
| Start -> Loop | `bHasLocomotionInput && StartRelevantTimeRemaining <= 0.05` |
| Start -> Stop | `bRequestStop && bStandardLocomotionEnabled` |
| Start -> Idle | `bForceLocomotionIdle || !bStandardLocomotionEnabled` |
| Loop -> Stop | `bRequestStop && bStandardLocomotionEnabled` |
| Loop -> Idle | `bForceLocomotionIdle || !bStandardLocomotionEnabled` |
| Stop -> Start | `bRequestStart && bStandardLocomotionEnabled` |
| Stop -> Idle | `bForceLocomotionIdle || Speed2D <= StopSpeedThreshold` |

`StopSpeedThreshold` 初值设 `5 cm/s`。Transition 的优先级确保同一帧有重新输入时 `Stop -> Start` 优先于 `Stop -> Idle`。

不要增加 `Speed2D > 0` 直接进入 Stop 的规则。动作 Montage 结束时残留速度不能成为 Stop 授权。

### 5.2 Start：Sequence Evaluator + 增量距离匹配

在 `Start` State 中，每个将使用的 Start 变体都用 `Sequence Evaluator`，不要使用普通 Sequence Player：

1. 把 Runtime In-Place Start 拖入 State。
2. 右键节点并确认它是 `Sequence Evaluator`；显式时间由节点函数控制。
3. 选中节点，在 Details 的 `Functions`/`On Update` 创建动画节点函数绑定。
4. 在函数中：
   - 从输入节点取得 `Update Context` 和 `Node`。
   - 使用 `Convert to Sequence Evaluator`。
   - 计算本次动画更新内胶囊实际水平位移：优先使用本帧 Character 平面位置减上一动画更新位置；若代码快照已提供该增量则直接读取。
   - 调用 `Advance Time By Distance Matching`。
   - `Distance Traveled` 接本次更新的非负 XY 位移增量，不接累计总距离，也不接 TargetCruiseSpeed。
   - `Distance Curve Name = Distance`。
   - `Play Rate Clamp` 初值 `(0.75, 1.25)`。
5. State 进入时重置“上一动画更新位置”，避免第一次更新把动作前位移算进 Start。

如果 Sequence Evaluator 报“无法设置时间/节点不是动态”，在节点 Details 把相关可动态属性设为 `Always Dynamic`，再编译。

### 5.3 Stop：预测位置 + Distance Match To Target

在 `Stop` State 中使用 Runtime In-Place Stop 的 `Sequence Evaluator`：

1. 从 Property Access 读取当前 CMC：
   - `Velocity`
   - `bUseSeparateBrakingFriction`
   - `BrakingFriction`
   - `GroundFriction`
   - `BrakingFrictionFactor`
   - `BrakingDecelerationWalking`
2. 调用 `Predict Ground Movement Stop Location`。
3. 对返回 FVector 调用 `Vector Length XY`，得到正的 `DistanceToStop`。
4. 在 Stop Sequence Evaluator 的 `On Update` 节点函数中调用 `Distance Match To Target`：
   - `Distance To Target = DistanceToStop`
   - `Distance Curve Name = Distance`
5. Stop State 只由 C++ 的 `bRequestStop` 进入；预测节点只决定播 Stop 的哪一帧，不决定是否应播放 Stop。

停止预测必须读取实际 CMC 参数，不要在 AnimBP 另建一套手填摩擦/制动常量。

### 5.4 Start/Loop 的 Sync Group

选中 Start/Loop 内所有会互相混合的资产播放器/评估器，在 Details 的 Sync 分类设置：

- Group Name：`Locomotion`
- Loop Player：通常设为 `Can Be Leader` 或主要分支设 `Always Leader`。
- Start：通常设为 `Can Be Leader`；确保它与 Loop 至少共享一个正确的脚落地 Marker。

只有 Marker 名与实际左右脚都正确的资产才能放入同一组。若混合后脚相反，先修 Marker，不要先把 Crossfade 拉到 0.3 秒掩盖。

### 5.5 动作交接验证

每项重复 20 次：

- 移动中攻击并松开摇杆：动作结束直接 Idle，不播 Stop。
- 移动中攻击并一直推摇杆：动作结束进入 Start/Loop，不播 Stop。
- 第一次拔刀：结束后无输入直接持刀 Idle。
- Dodge `MoveExit`：决定进入 MoveExit 的仍是 Exit 帧实时输入；Montage Root Motion 结束后再由普通 Start/Loop 接管。
- 移动收刀：动作期间允许转向但不叠加普通 CMC 位移；动作结束按当前输入进入收刀 Start/Idle。
- Stop 播放途中重新推摇杆：胶囊立即加速，动画立即转 Start，不等待 Stop 结束。

## 6. L4：完整步态、PlayRate 与脚相

### 6.1 Loop 资产选择

在 `Loop` State 使用两层选择：

```text
bUnsheathed ? Unsheathed_IG_Loop
             : Blend Poses by EMHGZLocomotionGait
                 Walk   -> Sheathed_Walk_Loop
                 Run    -> Sheathed_Run_Loop
                 Sprint -> Sheathed_Sprint_Loop
```

持刀当前只有一档普通移动资产，因此 `TargetGait=Run`，不为缺失资产复制三条一模一样的持刀分支。

Start/Stop 同样按“进入该阶段时冻结的 Gait + Stance”选择一次。不要在 Start 内随摇杆幅度每帧切换 Start 资产；最新 Gait 到 Loop 时再生效。

### 6.2 对真正 in-place Loop 设置 PlayRate

UE5.6 的 `Set Playrate To Match Speed` 会从当前 AnimSequence 的 Root Motion 总位移计算原始速度。真正去掉根平移的 Runtime In-Place 资产没有该数据，因此本项目默认使用手工测得值：

```text
DesiredPlayRate = Speed2D / AuthoredLoopSpeed
```

具体操作：

1. 为每条 Loop 在 AnimBP 增加清晰命名的 `Authored...Speed` 默认变量，例如 `ShthWalkAuthoredSpeed`。
2. 值填写 L1 审计表中从 SourceWithRM 测得的 cm/s。
3. 在对应 Sequence Player 的 Play Rate 动态引脚接 `Speed2D / AuthoredLoopSpeed`。
4. Clamp 初值为 `0.75～1.25`；超出范围时应由 Gait 切换或后续 Stride Warping承担，不让动画极慢/极快。
5. `AuthoredLoopSpeed <= 1` 时使用 `1.0` 兜底并报阶段错误，不能除零。

只有运行时资产仍保留可提取 Root Motion、且已经证明在 `Root Motion From Montages Only` 下不会导致 Mesh 漂移时，才改用 `Set Playrate To Match Speed`；不能同时手工除速和调用该节点。

### 6.3 Transition 混合

初始建议：

- Start -> Loop：`0.05～0.12s`。
- Gait 间：`0.08～0.15s`。
- Stop -> Start：`0.05～0.10s`。
- Stop -> Idle：`0.05～0.10s`。

先确认 Marker/脚相，再调时间。若使用 Inertialization：

1. Transition Blend Logic 选 Inertialization。
2. 在状态机输出之后、进入动作 Slot 之前添加一个 `Inertialization` 节点。
3. Compile 后确认不再出现“未找到请求来自的惯性化节点”。

不想使用时，把所有相关 Transition 改回 Standard Blend；仅删除 Inertialization 节点但保留请求会在 PIE 报错。

### 6.4 CMC 参数调校

在 `BP_MHGZCharacter` 的 Character Movement 组件中调校：

- `Max Acceleration`
- `Ground Friction`
- `Braking Deceleration Walking`
- `Use Separate Braking Friction`
- `Braking Friction`
- `Braking Friction Factor`

每改一组就记录：从 Run 松杆的实际停止距离、Stop 曲线起点范围和是否滑脚。`Predict Ground Movement Stop Location` 使用同一组值，因此不要在 AnimBP 再填副本。

### 6.5 Stride Warping（可选，L4 后半）

只有 PlayRate、脚相和 CMC 停止距离已稳定后才加入 Stride Warping。它只修正腿部步幅，不改变 CMC 速度，也不是修复错误 Marker、错误 AuthoredSpeed 或双位移的工具。Demo 第一轮可以不做，不阻塞 L4 的基础退出条件。

### 6.6 L4 验收

- 收刀 Walk/Run/Sprint 各持续 30 秒，无重复某只脚几帧、无 Mesh 回弹。
- 在 Walk/Run 阈值附近缓慢推拉摇杆，不反复重播 Start。
- Sprint 按住/松开时速度和腿速一致，没有明显滑脚。
- 持刀移动使用固定持刀资产，收刀/拔刀切换后不误播 Stop。
- Start、Stop、重新起步各重复 20 次，胶囊响应不受动画 Blend 延迟。
- 所有全身 Montage 与操虫上半身 Layer 回归通过。

## 7. L5：删除旧 Motion Matching 路径

L4 全部通过后才执行。

### 7.1 AnimGraph 清理

从 `ABP_MH_Character` 删除：

- 两个旧 Motion Matching 节点及其收刀/持刀 Bool 分支；
- 只服务旧 MM 的 Pose History；
- 旧 Trajectory 变量输入与手工 `setTrajectory` 调用；
- `DesiredSpeed`、`bForceMMIdle` 的蓝图 Getter；
- `L5 DELETE - Legacy Locomotion MM` 注释框内所有断开节点。

保留：

- `SM_CharacterLocomotion` 与 `LocomotionBase` Cached Pose；
- 单个固定 `UpperBody_IGAction` 上半身 Layer；
- 最后的 `DefaultSlot`；
- 动作系统需要的 Montage、Notify 与 Root Motion 设置。

### 7.2 清理资产引用

1. 对旧 PSD/PSS、Motion Matching 数据库和 Trajectory 资产执行 `Reference Viewer`。
2. 只有确认没有非普通移动系统引用时才从运行时目录移除；不确定时保留资产但标记 Legacy，不要在 L5 扩大到资产销毁。
3. 在 Content Browser 对本轮移动/重命名产生的目录执行 `Fix Up Redirectors in Folder`。
4. 保存全部，重启编辑器，重新编译 AnimBP，确认没有“缺失属性”错误。

### 7.3 最终图结构检查

最终主链只能是：

```text
SM_CharacterLocomotion
  -> LocomotionBase
  -> UpperBody_IGAction（固定 spine_01 以上覆盖）
  -> DefaultSlot（全身动作最终覆盖）
  -> Output Pose
```

不得存在：

- Motion Matching/Pose History 仍连到 Output；
- `IsMoving` 在两套操虫遮罩之间切换；
- 普通 locomotion 使用 Root Motion from Everything；
- 送虫/收虫 Montage 放到 `DefaultSlot` 作为最终方案；
- CMC 和普通动画 Root Motion 同时移动胶囊。

## 8. 最终验证顺序

### 8.1 编译与资产验证

1. 关闭编辑器，完成 Development Editor 构建。
2. 打开编辑器，Compile/Save `ABP_MH_Character` 与 `BP_MHGZCharacter`。
3. 对 `LocomotionRefactor` 目录运行 `Validate Assets in Folder`。
4. 对 `ABP_MH_Character`、角色蓝图、所有引用的 RuntimeInPlace 动画运行 Data Validation。
5. 运行 `Automation RunTests MHGZ`。

全 Content Validation 中尚未完成的未来 E5/E6 资产不应被误当成本阶段失败；但本轮修改资产不能新增错误。

### 8.2 PIE 矩阵

| 场景 | 期望 |
|---|---|
| 收刀 Walk/Run/Sprint | CMC 立即响应；循环稳定，无 Root Motion 双位移 |
| 持刀移动 | 持刀 Loop 稳定；速度由 CMC 决定 |
| 松杆 Stop | 只有真实普通输入释放进入；停止距离匹配 |
| Stop 中重新输入 | 立即由 CMC 起步并切 Start |
| 第一次拔刀 | 不触发持刀 Stop |
| 攻击结束无输入 | Idle，不播普通 Stop |
| 攻击结束有输入 | Start/Loop，不播普通 Stop |
| 前翻滚 MoveExit | Exit 帧实时输入选分支；Montage 结束后 CMC 接管 |
| 左/右/后持刀翻滚 | 只回 Idle，之后重新起步 |
| 移动收刀 | Montage 位移和转向保留，结束后平滑接普通移动 |
| Send/Recall | 上半身动作可见，下半身始终来自普通 locomotion |
| 墙、斜坡、台阶 | Capsule 与 Mesh 不分离，无穿墙/弹回 |

### 8.3 调试判断

- 胶囊移动正确、腿滑：检查 AuthoredLoopSpeed、PlayRate、Marker、曲线与 Blend。
- 胶囊位移翻倍：检查普通运行时资产仍有 Root Motion、CMC 是否在动作 Owner 期间提交。
- 动作结束误播 Stop：检查 `bRequestStop` 是否由真实普通输入 Release 产生，而不是速度下降。
- 第一次拔刀误播 Stop：检查动作退出是否发 `bForceLocomotionIdle`，以及 AnimBP 是否仍有速度驱动 Stop 的旧 Transition。
- 操虫无法移动：检查 GA 是否错误取得 `BlockMovement`/RootMotionOwner，以及上半身 Montage 的 Slot 是否为 `UpperBody_IGAction`。
- PIE 报惯性化节点缺失：把 Transition 改回 Standard，或在状态机输出后补唯一 Inertialization 节点。

## 9. 完成标志

只有同时满足以下条件才能宣布 L5 完成：

- 普通移动的唯一位移所有者是 CMC。
- 状态机播放的所有 locomotion 资产都是经验证的 in-place。
- Start/Stop 只由确定性事件授权，Distance Matching 只决定动画时间。
- 旧 MM/PSS/Trajectory 不再连接运行时主链，旧 Character 字段无蓝图引用。
- 全身动作和操虫上半身层的 Slot 顺序没有回归。
- 构建、MHGZ 自动化、本阶段 Data Validation 和完整 PIE 矩阵通过。

完成后回到 [阶段门禁](../design/milestone-gates.md)，执行 M4-A 最终移动验收；不要直接跳过门禁进入批量招式接线。
