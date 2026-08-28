# 纯 Motion Matching 普通移动实施指南

> **状态（2026-08-28，须先读 [阶段门禁](milestone-gates.md)）：当前处于 PMM-7.1 Stop 候选生命周期修复，设计已冻结、尚未实施。** PMM-1～PMM-5 已接通；PMM-6 已进行调试但未签收；PMM-7 的运行时查询、九条生成 Stop、`MM_StopGait`/`MM_MoveGait` 原生频道、两套 PSD 重建和基础资产审计已完成，但最终 PIE Stop 生命周期验收失败。收刀 Extended Stop 在 PSD 尾部失去 Continuing 后会发生一次不合法的全局 Stop 重选。任何 AI/开发者都不得将“PMM-7 基础接入完成”表述为“普通移动完成”，也不得开始 M4-B.0、M4-B.1、M5 或它们的资产接线。
>
> **当前路线：** 本文采用“普通移动继续由动画 Root Motion 驱动”的纯 Motion Matching 路线，不执行 `locomotion-refactor*.md` 中的 CMC + 状态机备选方案，也不能把两套路线混装。
>
> **本文中的“纯 MM”定义：** Idle、Start、Loop、Stop 全部留在同一个 Pose Search 候选池，由加权成本决定结果；允许查询侧缓存输入意图和速度，但不建立 Idle/Starting/Looping/Stopping 动画状态机，不由代码直接指定要播放哪条动画。
>
> **状态解释：** PMM-3/4/7 的“自动审计通过”只证明曲线、Notify、候选集合和索引在当时符合脚本规则；它不能证明索引尾部仍存在 Continuing Pose。PMM-7.1 将修正这条不足，再重跑 PMM-3/4/6。

## 1. 项目不可违反的限制

1. 当前普通移动动画全部是**前向移动**资产，没有侧移、后退、转身起步或转身循环数据库。
2. 摇杆的世界方向只负责改变角色 Actor Yaw；动画始终沿角色当前前方贡献 Root Motion。
3. PSS 不配置 Facing Direction、Heading、角速度或“左/右/后”方向频道。
4. `AS_Shth_*_Stop_Left/Right` 中的 Left/Right 表示**由哪只脚完成停步**，不是向左/向右移动。
5. 角色方向继续由 `AMHGZCharacter::Tick` 中现有 `TurnRate=360°/s` 的最短角差旋转负责。纯 MM 改造不得重复旋转 Actor。
6. 普通移动继续使用 Animation Root Motion；不得同时调用 `AddMovementInput` 让 CMC 再产生一份位移。
7. 攻击、翻滚、收拔刀等动作仍遵守 `BlockMovement`、`MontageRootMotionOwner` 和动作 Slot 的现有合同；本方案只改普通 locomotion 查询。

## 2. 当前系统审计结果

### 2.1 C++ 当前数据流

`AMHGZCharacter::DoMove` 当前没有调用 `AddMovementInput`，而是计算以下数据：

```text
摇杆轴
  -> RawMoveInput / LastMovementInputDir
  -> InputMagnitude / bHasInput
  -> CalcCruiseSpeed
  -> TargetCruiseSpeed（当前帧瞬时目标）
  -> FInterpTo
  -> DesiredSpeed（平滑后的旧轨迹输入）
```

`TargetCruiseSpeed` 不是角色当前实际速度，也不是加速度。它表示“当前摇杆与姿态希望最终达到的巡航速度”，单位为 cm/s：

| 姿态/步态 | 当前 C++ 值 | 对应 Loop 根位移实测速率 |
|---|---:|---:|
| 收刀 Walk | 160 | `AS_Shth_Walk_Loop`：约 156.8 cm/s |
| 收刀 Run | 460 | `AS_Shth_Run_Loop`：约 458.1 cm/s |
| 收刀 Sprint | 575 | `AS_Shth_Sprint_Loop_125x`：约 572.6 cm/s |
| 持刀单速 | 440 | `AS_UnSh_Walk_Loop`：约 440.6 cm/s |

这四个**档位端点**已经与当前正式 Loop 资产匹配，后续不要改端点速度。但是当前 `CalcCruiseSpeed` 在死区到 0.5、0.5 到 0.9 之间做线性插值，会生成 37、245、380 cm/s 等数据库里没有对应 Loop 的中间速度；同时 MM 节点又固定 `Play Rate=[1,1]`，所以动画 Root Motion 无法真正兑现这些中间目标。这是当前搜索会在 Walk/Run 间摇摆的另一个原因。PMM-1 会把查询目标量化为现有的四个资产档位；仍由摇杆幅度决定 Walk/Run，只是不再伪造没有动画支撑的连续速度。

`DesiredSpeed` 是旧 AnimBP 用 `FInterpTo(..., InterpSpeed=20)` 得到的平滑值；它会非常快地逼近目标，并不能表达动画自身较长的起步和停步位移曲线，因此新查询不再用它生成未来轨迹。

### 2.2 当前 AnimBP

当前 `ABP_MH_Character` 的普通移动链仍是：

```text
PSD_MH_Shth_Move Motion Matching ┐
                                 ├─ Blend Poses by Bool(Unsheathed)
PSD_MH_UnSh_Move Motion Matching ┘
  -> Pose History（Trajectory 由旧 setTrajectory 提供）
  -> 动作 Slot/上半身分层
  -> Output Pose
```

资产读取到的两个 Motion Matching 节点当前设置为：

- `Blend Time=0`
- `Use Inertial Blend=false`
- `Pose Reselect History=0.3`
- `Search Throttle Time=0.12`
- `Play Rate=[1,1]`
- 收刀/持刀 Bool 分支当前 Blend Time 为 `0.1`

Pose History 已接收 C++ AnimInstance 生成的 `MM Predicted Trajectory`。它按当前实际 Root Motion 速度、量化后的 `TargetCruiseSpeed` 以及对应加/减速度生成前向位移；为匹配导入 Root 的坐标约定，查询仅沿 Actor 本地 `+Y` 写入距离。旧 `setTrajectory` 与 `DesiredSpeed × Time` 直线查询只能作为待删除的 Legacy，不得重新接回 Pose History。

### 2.3 当前 PSS

`PSS_MH_Move` 当前为 60 Hz、Normalize，包含五个频道：

| 频道 | 当前内容 |
|---|---|
| Pose | 权重 2；双脚；Position + Velocity + Phase |
| Trajectory | 权重 6；时间点 0.2、0.5、0.8、1.0；完整 Position |
| `BPSC_MMIntent` | 权重 10；资产曲线名 `MM_Intent` |
| `BPSC_MMDistanceToStop` | 权重 4；资产曲线名 `MM_DistanceToStop` |
| `UMHGZPoseSearchFeatureChannel_StopGait` | 权重 64；资产曲线名 `MM_StopGait`；`Use Continuing Pose` |

当前没有负时间轨迹样本。继续保持这一点；本项目之前已经验证，直接加入负 Offset 会导致循环动画在某只脚附近反复重选并抽搐。

### 2.4 Stop 查询频道已接通

`BPSC_MMIntent`、`BPSC_MMDistanceToStop` 与原生 `UMHGZPoseSearchFeatureChannel_StopGait` 都只读取 `UMHGZMotionMatchingAnimInstance` 的缓存查询值；它们不读取 World、Pawn、CharacterMovement 或 GA 状态。动画资产中的 `MM_Intent` / `MM_DistanceToStop` / `MM_StopGait` 因此与运行时的起停语义、停步剩余距离和实际步态族参与同一次成本搜索。

起步查询不是由 `Should Search` 门控：在输入从无到有、且实际速度仍低时激活。前 `1/30s` 的输入稳定期只让 AnimGraph 保持 Idle；第一条 Start 选择结果返回前，查询保持 `+1`，返回后每帧直接采样**实际选中 Start** 当前时间的 `MM_Intent`。Start 语义是有限窗口：收刀 Walk 为 1.25 s、收刀 Run/Sprint 为 0.80 s、持刀为 0.60 s，随后回到 0；它不再把完整 Start 资产当作持续的 `+1` 状态。

### 2.5 当前数据库和动画

`PSD_MH_Shth_Move` 当前正式引用 16 条候选：

- `AS_Shth_Idle`；
- Walk/Run/Sprint 的 Start 与 Loop；
- Walk/Run/Sprint 各自的 `Stop_Left_Extended`、`Stop_Right_Extended`、`FirstStepCommitStop`。

原始 `AS_Shth_*_Stop_*` 仅保留为生成输入，不再是正式 Pose Search 候选。

`PSD_MH_UnSh_Move` 当前引用持刀 Idle、Walk Start、Walk Loop、Walk Stop。两套数据库的 `Exclude From Database Parameters` 当前都是 `[0,-0.3]`；对 0.68～1.15 秒的 Stop 来说，统一裁掉末尾 0.3 秒过多。

当前正式资产均已启用 Root Motion，实测数据如下：

| 资产 | 时长 | 水平根位移/平均速度 |
|---|---:|---:|
| Sheathed Walk Start | 1.75 s | 266.7 cm / 152.4 cm/s |
| Sheathed Run Start | 1.15 s | 423.4 cm / 368.1 cm/s |
| Sheathed Sprint Start | 1.15 s | 423.4 cm / 368.1 cm/s |
| Unsheathed Start | 0.933 s | 355.8 cm / 381.2 cm/s |
| Sheathed Walk Stop Left/Right | 1.15 / 0.90 s | 64.4 / 37.3 cm |
| Sheathed Run Stop Left/Right | 0.683 / 0.90 s | 32.4 / 74.5 cm |
| Unsheathed Stop | 0.80 s | 51.0 cm |

左右脚 Stop 位移不同是有效数据；不要把它们烘焙成相同位移，也不要用方向条件硬选。Pose 频道负责判断当前脚相，停步距离频道负责判断哪条候选在当前速度下更合适。

## 3. 目标数据流

```text
MHGZCharacter
  ├─ bHasInput / TargetCruiseSpeed / bForceMMIdle / bUnsheathed
  ├─ ActorLocation（测量上一帧 Root Motion 实际位移）
  └─ ActorTransform 的本地 +Y（导入 locomotion Root 的查询前进轴）
        ↓
UMHGZMotionMatchingAnimInstance::NativeUpdateAnimation
  ├─ MMActualSpeed2D
  ├─ MMIntentQuery：Start=+1，Cruise/Idle=0，Stop=-1
  ├─ MMDistanceToStopQuery：Stop 时为负的预计剩余距离，其余为 0
  ├─ MMStopGaitQuery：收刀 Stop 锁存实际选中的 Walk=1/3、Run=2/3、Sprint=1
  └─ MMPredictedTrajectory：按加速/减速积分得到 0.2/0.5/0.8/1.0 s 位置
        ↓
PSS_MH_Move
  ├─ 双脚 Pose/Velocity/Phase
  ├─ 完整 Position 的前向 Trajectory（查询仅本地 Y 分量变化）
  ├─ MM_Intent 高权重语义频道
  └─ MM_DistanceToStop 停步距离频道
        ↓
PSD_MH_Shth_Move / PSD_MH_UnSh_Move
  ├─ Idle / Start / Loop / Stop 同池搜索
  ├─ Block Transition In 禁止直接跳进 Start/Stop 中后段
  └─ Continuing Pose Bias 让已选中的 Start/Stop 有机会继续播放
        ↓
Animation Root Motion 驱动胶囊
```

这里没有方向候选：摇杆向左时，Character 先按现有逻辑把 Actor 朝左转；MM 仍只查询“向角色前方移动”的 Start/Loop/Stop。

## 4. 运行时变量：名称、类型和职责

后续新建 `UMHGZMotionMatchingAnimInstance`，并让 `ABP_MH_Character` 改用它作为 Parent Class。变量全部在这个 C++ AnimInstance 中定义，避免 Curve Channel 直接依赖 `ABP_MH_Character.uasset` 形成资产循环。

| 名称 | C++ 类型 | UPROPERTY | 默认值 | 作用 |
|---|---|---|---:|---|
| `MMActualSpeed2D` | `float` | `BlueprintReadOnly, Transient` | 0 | 用 Actor 本帧与上一帧 XY 位移除以 DeltaSeconds，表示 Root Motion 实际速度 |
| `MMIntentQuery` | `float` | `BlueprintReadOnly, Transient` | 0 | 起步接近 +1，巡航/Idle 为 0，停步接近 -1 |
| `MMDistanceToStopQuery` | `float` | `BlueprintReadOnly, Transient` | 0 | 无输入且仍在移动时为负的预计剩余停步距离，其他情况为 0 |
| `MMLastNonZeroCruiseSpeed` | `float` | `BlueprintReadOnly, Transient` | 0 | 保存松摇杆前最后一个有效 `TargetCruiseSpeed`，用于 Stop 归一化和选择步态参数 |
| `MMPredictedTrajectory` | `FTransformTrajectory` | `BlueprintReadOnly, Transient` | 空 | 传给 Pose History 的完整当前+未来轨迹 |
| `MMStartAccelerationWalk` | `float` | `EditDefaultsOnly` | 950 | Walk 起步预测加速度，cm/s²；按当前 Start 总位移与目标档位拟合的初值 |
| `MMStartAccelerationRun` | `float` | `EditDefaultsOnly` | 1000 | Run 起步预测加速度 |
| `MMStartAccelerationSprint` | `float` | `EditDefaultsOnly` | 700 | Sprint 起步预测加速度；当前 Sprint Start 与 Run Start 同源但目标速度更高 |
| `MMStartAccelerationUnsheathed` | `float` | `EditDefaultsOnly` | 1750 | 持刀起步预测加速度 |
| `MMStopDecelerationWalk` | `float` | `EditDefaultsOnly` | 250 | Walk 停步预测减速度，cm/s² |
| `MMStopDecelerationRun` | `float` | `EditDefaultsOnly` | 1900 | Run/持刀停步预测减速度 |
| `MMStopDecelerationSprint` | `float` | `EditDefaultsOnly` | 3000 | 从 Sprint 松摇杆时的预测减速度；仍使用 Run Stop 候选 |
| `MMStartEligibilitySpeed` | `float` | `EditDefaultsOnly` | 60 | 只有实际速度低于该值的新移动输入才启动 Start 语义，防止 Walk→Run 时重新挑 Idle Start |
| `MMIdleSpeedThreshold` | `float` | `EditDefaultsOnly` | 8 | 低于该速度视为已停稳 |
| `MMTeleportResetDistance` | `float` | `EditDefaultsOnly` | 200 | 单帧位移超过该值时重置历史，防止传送/动作退出被当作普通速度 |
| `PreviousActorLocation` | `FVector` | 非 UPROPERTY 私有字段 | Zero | 上一帧测量位置 |
| `bHasPreviousActorLocation` | `bool` | 非 UPROPERTY 私有字段 | false | 是否可以计算位移差 |
| `bStartQueryActive` | `bool` | 非 UPROPERTY 私有字段 | false | 仅表示 Start 查询语义仍在等待或跟随实际 Start 曲线；不指定动画 |
| `bStartQueryObservedStart` | `bool` | 非 UPROPERTY 私有字段 | false | 当前起步请求是否已收到实际 Start 选择；用于区分“尚未返回结果”和“已自然交接到 Loop” |
| `bHadMoveInput` | `bool` | 非 UPROPERTY 私有字段 | false | 只用于识别 `false→true` 的移动输入边沿 |
| `bWasForceMMIdle` | `bool` | 非 UPROPERTY 私有字段 | false | 记录上一帧是否由动作系统压住 MM；解除压制后的第一帧仍重置位移历史 |
| `MMForceIdleReleaseHoldRemaining` | `float` | 非 UPROPERTY 私有字段 | 0 | 外部动作锁解除后，仍保持 Idle bypass 的剩余秒数 |
| `MMStartQueryElapsed` | `float` | 非 UPROPERTY 私有字段 | 0 | 仅作 CSV 调试；已选 Start 时为实际选中时间 |
| `MMStartQueryDuration` | `float` | 非 UPROPERTY 私有字段 | 0 | 仅作 CSV 调试；已选 Start 时为该资产实际长度 |
| `CachedCharacter` | `TWeakObjectPtr<AMHGZCharacter>` | 非 UPROPERTY 私有字段 | null | `NativeInitializeAnimation` 缓存的拥有者 |

不要再创建含义不明的 `ActualSpeed2D`、`MMIntentQuery` 等散落 Blueprint 变量。表中前五个字段是 AnimBP 和 Curve Channel 的唯一查询真相源；后面的调参值全部在 `ABP_MH_Character` 的 Class Defaults 中可见。

## 5. PMM-0：建立可回退基线

### 5.1 操作

1. 保存当前所有 Content 资产并关闭 PIE。
2. 用 Git 提交当前可运行基线。项目已经有版本控制，不需要再复制一套 PSS/PSD 到其他目录。
3. 记录以下当前值：四个巡航速度、两个 PSD 的动画清单、PSS 五个频道、两个 MM 节点设置。
4. PIE 各录一次：Idle→Walk、Idle→Run、Idle→Sprint、移动→松摇杆、第一次拔刀、收刀后再拔刀。
5. 保留 Pose Search Debugger 截图或录屏，作为 PMM-6 前后对比。

### 5.2 退出条件

- 当前 Git 提交可独立恢复。
- 已有至少一段能复现“起步/停步被跳过或混播”的录屏。
- 不创建 `_Copy`、`_Backup` 或第二套并行运行 PSD。

## 6. PMM-1：代码侧建立查询生产者

> **实施状态（2026-08-26）：已完成。** 已新增 `UMHGZMotionMatchingAnimInstance`、纯数学辅助函数和 `MHGZ.PMM.Query.*` 自动化测试；后续 PMM-2 已完成 AnimBP Reparent 与两个 Curve Channel 接线。PSS、PSD 与动画资产的重配仍留给 PMM-3～PMM-5。

### 6.1 新建类

新增：

```text
Source/MHGZ/Animation/MHGZMotionMatchingAnimInstance.h
Source/MHGZ/Animation/MHGZMotionMatchingAnimInstance.cpp
```

类声明骨架：

```cpp
UCLASS(Blueprintable, Transient)
class MHGZ_API UMHGZMotionMatchingAnimInstance : public UAnimInstance
{
    GENERATED_BODY()

public:
    virtual void NativeInitializeAnimation() override;
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;

    UPROPERTY(BlueprintReadOnly, Transient, Category="Movement|MM|Query")
    float MMActualSpeed2D = 0.0f;

    UPROPERTY(BlueprintReadOnly, Transient, Category="Movement|MM|Query")
    float MMIntentQuery = 0.0f;

    UPROPERTY(BlueprintReadOnly, Transient, Category="Movement|MM|Query")
    float MMDistanceToStopQuery = 0.0f;

    UPROPERTY(BlueprintReadOnly, Transient, Category="Movement|MM|Query")
    float MMLastNonZeroCruiseSpeed = 0.0f;

    UPROPERTY(BlueprintReadOnly, Transient, Category="Movement|MM|Query")
    FTransformTrajectory MMPredictedTrajectory;

    // 其余 EditDefaultsOnly 调参字段按 §4 创建，类型均为 float。

private:
    TWeakObjectPtr<AMHGZCharacter> CachedCharacter;
    FVector PreviousActorLocation = FVector::ZeroVector;
    bool bHasPreviousActorLocation = false;
    bool bStartQueryActive = false;
    bool bHadMoveInput = false;
    bool bWasForceMMIdle = false;
    float MMStartQueryElapsed = 0.0f;
    float MMStartQueryDuration = 0.0f;

    void ResetMMTemporalState(const FVector& CurrentLocation);
    void BuildFlatTrajectory(const FTransform& ActorTransform);
    void BuildPredictedTrajectory(const FTransform& ActorTransform,
        float InitialSpeed, float TargetSpeed, float Acceleration);
};
```

头文件需要包含 `Animation/AnimInstance.h` 和 `Animation/TrajectoryTypes.h`，并前置声明 `AMHGZCharacter`。当前模块已经依赖 Engine/MotionTrajectory，不要引入新的第三方插件。

### 6.2 先把查询速度量化为现有资产档位

修改 `AMHGZCharacter::CalcCruiseSpeed`，保留当前死区、持刀单速和 Sprint 判定，但删除 Walk/Run 区间内的线性速度插值：

```text
StickMagnitude < MoveDeadzone：0
持刀：440
收刀且 StickMagnitude > 0.9 且 bSprintHeld：575
收刀且 StickMagnitude < 0.5：160
收刀其他情况：460
```

这不是把移动改成状态机，而是让 `TargetCruiseSpeed` 只描述数据库真实拥有的速度档位。摇杆方向仍连续控制 Actor Yaw；摇杆幅度仍选择 Walk 或 Run；Sprint 仍由当前 RB 长按逻辑选择。不要在本阶段开放 MM 节点 Play Rate 来模拟 0～160 的连续速度，否则会同时改变 Start、Stop 与脚步节奏，使后面的成本调试失去基准。

为 `CalcCruiseSpeed` 补纯函数测试，至少覆盖 `0、0.1 以下、0.3、0.5、0.9、1.0、SprintHeld、Unsheathed`。如果以后确实要模拟连续慢走，应在 PMM-6 稳定后单独设计 Play Rate/Stride Warping，不混进本轮起停修正。

### 6.3 初始化

`NativeInitializeAnimation`：

1. 调用 `Super`。
2. `CachedCharacter = Cast<AMHGZCharacter>(TryGetPawnOwner())`。
3. 把 `bHasPreviousActorLocation=false`、`bStartQueryActive=false`、`bHadMoveInput=false`、`bWasForceMMIdle=false`、两个 Start 计时 float 和三个查询 float 清零。
4. 不在构造函数中读取 Pawn、World 或 CharacterMovement。

### 6.4 每帧实际速度

`NativeUpdateAnimation` 首先调用 `Super`，然后：

```text
Character 无效
  -> 尝试重新 Cast TryGetPawnOwner
仍无效
  -> 清空查询并 return

CurrentLocation = Character.ActorLocation

ExternalForceMMIdle = Character.bForceMMIdle
如果 ExternalForceMMIdle=true：ForceIdleReleaseHoldRemaining = MMForceIdleReleaseHoldDuration（默认 0.05）
否则：ForceIdleReleaseHoldRemaining = Max(0, ForceIdleReleaseHoldRemaining - DeltaSeconds)
CurrentForceMMIdle = ExternalForceMMIdle 或 ForceIdleReleaseHoldRemaining > 0

如果 CurrentForceMMIdle=true、bWasForceMMIdle=true、不是 MovingOnGround、DeltaSeconds<=0，
或上一帧没有有效位置：
  MMActualSpeed2D = 0
  PreviousActorLocation = CurrentLocation
  bHasPreviousActorLocation = true
  bStartQueryActive = false
  bHadMoveInput = false
  MMIntentQuery = 0
  MMDistanceToStopQuery = 0
  BuildFlatTrajectory
  bWasForceMMIdle = CurrentForceMMIdle
  return

FrameDistance = Distance2D(CurrentLocation, PreviousActorLocation)
PreviousActorLocation = CurrentLocation

如果 FrameDistance > MMTeleportResetDistance：
  与上面的 Reset 分支相同，不把本帧当作速度
否则：
  MMActualSpeed2D = FrameDistance / DeltaSeconds

本帧正常计算结束时：
  bWasForceMMIdle = false
```

不要把 Motion Matching 节点的 `Should Search` 作为 Walk/Run/Sprint 的选择器或输入边沿门。
它只决定是否执行全库搜索，不能修正轨迹坐标或成本特征；第一次搜索如果因查询轴错误而选错 Start，
关闭后只会把错误结果维持得更久。速度 Start/Loop 的选择仍由同一候选池中的 Pose、Trajectory、
`MM_Intent`、`MM_DistanceToStop` 成本共同决定。

必须使用 Actor 实际位移，不使用 `CharacterMovement.Velocity.Size2D()`：当前普通移动由动画 Root Motion 驱动，Actor 位移才是最终事实。这里同时检查“本帧被压制”和“上一帧被压制”，并在外部锁定解除后保留默认 `0.05s` 的有效压制：这只吸收 Montage 的最后一个 Root Motion 采样，不可把动作结束后的可操作时间扩展成明显的 Idle 冻结。保持 C++ 查询和 AnimGraph 的 Idle bypass 同步，才能避免该位移被解释为玩家松开摇杆后的 Stop。

### 6.5 生成 `MMIntentQuery`

先读取：

```cpp
const bool bHasInput = Character->bHasInput;
const float TargetSpeed = Character->TargetCruiseSpeed;
```

规则：

```text
若 bHasInput && TargetSpeed > 1：
  MMLastNonZeroCruiseSpeed = TargetSpeed

若 !bHadMoveInput && bHasInput
   && MMActualSpeed2D <= MMStartEligibilitySpeed：
  bStartQueryActive = true
  bStartQueryObservedStart = false
  MMStartQueryElapsed = 0

若 !bHasInput：
  bStartQueryActive = false
  bStartQueryObservedStart = false
  MMStartQueryElapsed = 0

若 bStartQueryActive：
  若当前节点的实际选择是 Start：
      bStartQueryObservedStart = true
      MMIntentQuery = Sample(SelectedStart, SelectedTime, "MM_Intent")
      MMStartQueryElapsed = SelectedTime
      MMStartQueryDuration = SelectedStart.PlayLength
      当采样值为 0 时 bStartQueryActive=false
  否则若 bStartQueryObservedStart：
      bStartQueryActive=false
      MMIntentQuery=0
  否则：
      MMIntentQuery=+1   // 等待第一条 Start 选择回调
否则若 !bHasInput && MMActualSpeed2D > MMIdleSpeedThreshold：
  Ratio = Clamp(MMActualSpeed2D / Max(MMLastNonZeroCruiseSpeed, 1), 0, 1)
  MMIntentQuery = -Ratio
否则：
  MMIntentQuery = 0

本帧计算结束时：
  bHadMoveInput = bHasInput
```

实现时应保存一个私有 `bHadMoveInput`，只在 `false→true` 的输入边沿且速度低于 `MMStartEligibilitySpeed` 时激活 `bStartQueryActive`。不能每帧用低速重新激活，否则 Stop 尾端突然推摇杆时可能反复重置 Start 查询。Start 不再由一条与资产长度脱节的线性计时器驱动：它在没有选择结果前输出 `+1`，收到 Start 后直接跟随该资产曲线。当前正式 Start 曲线从时间 0 保持约 `+1`，直到实际动画末尾前 `1/30s`，才在约两帧中降到 0；因此持刀 `AS_UnSh_Walk_Start` 不会再在旧的 0.60 秒提前变成 Loop 等价候选。**这仍只控制查询向量，不直接播放或绝对锁定 Start**：新的 `MM_MoveGait` 会在输入档位改变时强制同族候选参与成本比较，而 Start 的 Continuing Bias 仍只覆盖首个 0.10 s。

这不是动画状态机：`+1/0/-1` 只是搜索向量中的一个维度；代码没有播放动画，也没有禁止数据库搜索其他候选。

### 6.6 生成预计停步距离

无输入且仍在移动时：

```cpp
PredictedStopDistance = MMActualSpeed2D * MMActualSpeed2D
    / (2.0f * SelectedStopDeceleration);
MMDistanceToStopQuery = -PredictedStopDistance;
```

负号必须保留，因为当前 Stop 动画曲线从“负的剩余距离”走向 0。其他情况该值为 0。

`SelectedStopDeceleration` 初始选择：

- 持刀：1900。
- 收刀且 `MMLastNonZeroCruiseSpeed <= 310`：Walk 250。
- 收刀且速度 `< 520`：Run 1900。
- 收刀且速度 `>= 520`：Sprint 3000。

这些只是依据当前 Stop 总位移推导的初值，PMM-6 再用 Debugger 调整。项目没有单独 Sprint Stop；从 Sprint 完全松摇杆时仍从 Run Stop Left/Right 中选择。

### 6.7 生成加减速未来轨迹

`MMPredictedTrajectory.Samples` 每帧先清空，然后始终加入当前样本 `t=0`，再加入 `0.2、0.5、0.8、1.0` 四个未来样本。每个样本：

```cpp
Sample.TimeInSeconds = T;
Sample.Facing = Character->GetActorQuat();
Sample.Position = Character->GetActorLocation()
    + Character->GetActorTransform().GetUnitAxis(EAxis::Y) * PredictedDistance(T);
```

不要把摇杆 X/Y、控制器右向量、Facing 预测或角速度写进轨迹。角色方向已经由 `AMHGZCharacter::Tick`
修改 Actor Yaw；这里始终只写一条前向预测，但这批导入资源的 Root 骨骼前进轴映射到 Pose Search
查询坐标的本地 `+Y`，因此必须使用 `ActorTransform.GetUnitAxis(EAxis::Y)`，不能使用通常的
`GetActorForwardVector()`（本地 `+X`）。这会随 Actor Yaw 一起转到世界空间；不是把世界坐标固定写成 Y。

`Sample.Facing` 只是把 `FTransformTrajectorySample` 填完整；本方案的 PSS 不索引任何 Facing 字段，所以它不会参与方向选择或产生不存在的转向候选。

`PredictedDistance(T)` 使用分段匀加速公式：

```text
V0 = MMActualSpeed2D
V1 = bHasInput ? TargetCruiseSpeed : 0
A  = 当前步态的 StartAcceleration 或 StopDeceleration
Sign = V1 >= V0 ? +1 : -1
TimeToTarget = Abs(V1 - V0) / Max(A, 1)

若 T <= TimeToTarget：
  D = V0*T + 0.5*Sign*A*T*T
否则：
  DToTarget = V0*TimeToTarget
            + 0.5*Sign*A*TimeToTarget*TimeToTarget
  D = DToTarget + V1*(T-TimeToTarget)

D 最终 Clamp 到 >=0。
```

无输入时，速度到 0 后所有更远时间点保持在同一个停止位置；不能继续按指数衰减向前爬行。按下输入时，未来点间距逐渐增大，而不是第一帧就变成完整巡航速度。

### 6.8 PMM-1 验收

- Development Editor 全量编译成功，不用 Live Coding 作为最终证据。**已通过：** `Build.bat MHGZEditor Win64 Development ... -DisableUnity`。
- 新增的纯数学测试至少覆盖：速度档位量化、加速距离单调增加、减速达到 0 后位置固定、停步距离符号为负、ForceMMIdle 解除后的首帧与传送会重置测量。**已通过：** `MHGZ.PMM.Query.*` 共 3 项自动化测试。
- 尚未 Reparent AnimBP 时，游戏行为应与 PMM-0 完全相同。

## 7. PMM-2：让两个 Curve Channel 真正读取查询值

### 7.1 Reparent AnimBP

1. 打开 `ABP_MH_Character`。
2. 选择 `文件/File -> 重新设置父类/Reparent Blueprint`。
3. 选择 `MHGZMotionMatchingAnimInstance`。
4. Compile；先处理所有父类变更错误，再做后续接线。
5. 在 My Blueprint 的 Inherited Variables 中确认能看到：
   - `MM Actual Speed 2D`（Float）
   - `MM Intent Query`（Float）
   - `MM Distance To Stop Query`（Float）
   - `MM Last Non Zero Cruise Speed`（Float）
   - `MM Predicted Trajectory`（Transform Trajectory）

旧 AnimBP 中目前存在 `Trajectory`、`MM_IntentQuery`、`LastNonZeroTargetSpeed` 和旧 `setTrajectory` 计算。PMM-2 暂不删除；只把它们标记为 Legacy，等 PMM-5 切线通过后再删。

### 7.2 配置 `BPSC_MMIntent`

1. 打开 `/Game/Blueprints/Characters/Demo/Animation/MotionMatching/BPSC_MMIntent`。
2. 进入函数 `Get World Curve`；如果图中只有 Function Entry 和 Return，这是当前已审计到的半完成状态。
3. 从 Function Entry 的 `Anim Instance` 引脚拉线，添加 `Cast To MHGZMotionMatchingAnimInstance`。
4. 成功分支从 Cast Result 拉出 `Get MMIntentQuery`。
5. 把该 Float 接到 Return Node 的 `Return Value`。
6. Cast Failed 分支接另一个 Return Node，`Return Value=0.0`。
7. Compile、Save。

这个函数只做“对传入 AnimInstance 做 C++ 类型转换并读取已缓存 float”。它**不调用** `Get World`、`Try Get Pawn Owner`、`Get Character Movement` 或 Actor 节点；这些对象访问已经在 `NativeUpdateAnimation` 的游戏线程阶段完成。这样写也不会让 BPSC 依赖 `ABP_MH_Character.uasset`。

### 7.3 配置 `BPSC_MMDistanceToStop`

按相同步骤设置：

```text
Anim Instance
  -> Cast To MHGZMotionMatchingAnimInstance
  -> Get MMDistanceToStopQuery
  -> Return Value

Cast Failed -> Return 0.0
```

Compile、Save。用 Reference Viewer 检查两个 BPSC：应依赖 `/Script/PoseSearch` 和 `/Script/MHGZ`，不应依赖 `ABP_MH_Character`、PSS 或 PSD 资产。

### 7.4 PMM-2 验收

- PIE 时 AnimBP Debug 观察：静止为 0；从静止推摇杆时 `MMIntentQuery` 接近 +1；巡航时回到 0；松摇杆后接近 -1 并回到 0。
- `MMDistanceToStopQuery` 只在松摇杆且仍有速度时为负，停稳后为 0。
- 第一次拔刀/动作 Root Motion 结束后的第一帧两个查询都是 0，不产生负的 Stop 查询。

## 8. PMM-3：统一动画语义曲线

> **实施状态（2026-08-26）：当前阶段。** 本阶段只改正式动画资产中的 Float Curve 与 Pose Search Notify State；不改 C++ 查询生产者、PSS/PSD 权重或 AnimBP 轨迹输入。

### 8.1 `MM_Intent` 的含义

统一使用大小写完全一致的 `MM_Intent`：

| 资产类型 | 曲线键 |
|---|---|
| Idle | 开头 0，结尾 0 |
| Loop | 开头 0，结尾 0 |
| Start | 开头 +1，在动画进入稳定循环姿势前降到 0 |
| Stop | 开头 -1，结尾 0 |

正式 Start 使用两键、线性下降的 `MM_Intent` 曲线：时间 0 为 `1.0`，各自的有限语义窗口末端为 `0.0`。窗口为收刀 Walk 1.25 s、收刀 Run/Sprint 0.80 s、持刀 0.60 s。归零后当前 Start 可以由 Pose/Trajectory 自然继续，也可以交接到 Loop；新的 Start 前段不会再因持续匹配 `+1` 而被重复重选。

当前部分资产显示为 `mm_intent`，部分为 `MM_Intent`。FName 比较通常不区分大小写，但编辑器审计和批处理容易混乱；若编辑器不允许只改大小写，先改名为 `MM_Intent_Temp`，保存，再改成 `MM_Intent`。Loop 不需要每帧一个 0 键，只保留首尾两个 0 键。

### 8.2 `MM_DistanceToStop`

统一规则：

- Idle、Start、Loop：开头 0、结尾 0。
- Stop：每一帧记录“当前帧到 Stop 结尾还会产生的水平 Root Motion 位移”的负值，最后一帧为 0。

当前 Stop 中已有引擎生成的 `Distance` 曲线，它记录了真实的剩余根位移；当前手工 `MM_DistanceToStop` 多数只有首尾两三个线性键，信息不足。两条曲线的数据语义完全相同，因此不要维护副本。`Distance` 的原始键可以是稀疏的；Curve 的插值仍会在任意采样时刻给出连续距离值，关键是完整保留原曲线的键与切线，而不是人为按源动画帧重采样。对每条 Stop：

1. 打开动画序列底部 Curves 面板。
2. 展开现有 `Distance`，确认开头约等于本资产总停步距离的负值、结尾为 0。
3. 删除旧的手工 `MM_DistanceToStop`（如果存在）。
4. 将 `Distance` **直接重命名**为 `MM_DistanceToStop`，不要复制，也不要保留名为 `Distance` 的副本。
5. 保存后确认曲线保留全部原始键和切线：至少有首尾两个键，首键在 0 秒、末键在动画末尾，首值为负、末值为 0。不要强制键值单调；真实停步动画可能在重心调整时短暂向后位移，剩余位移曲线随之回升（更负），这正是原始 `Distance` 必须被原样保留的原因。

不要把中间轻微回摆或非单调段手工拉直；那是原动画真实根骨轨迹，Pose 频道会与距离频道共同消除多解。

### 8.3 逐资产审计表

| PSD | 资产 | `MM_Intent` | `MM_DistanceToStop` | 其他要求 |
|---|---|---|---|---|
| Shth | Idle | 0 | 0 | Looping |
| Shth | Walk Start | +1→0 | 0 | 非循环 |
| Shth | Walk Loop | 0 | 0 | Looping |
| Shth | Walk Stop L/R | -1→0 | 将各自 `Distance` 重命名 | 非循环；不保留 `Distance` 副本 |
| Shth | Run Start | +1→0 | 0 | 非循环 |
| Shth | Run Loop | 0 | 0 | Looping |
| Shth | Run Stop L/R | -1→0 | 将各自 `Distance` 重命名 | 非循环；不保留 `Distance` 副本 |
| Shth | Sprint Start | +1→0 | 0 | 非循环 |
| Shth | Sprint Loop 125x | 0 | 0 | Looping；不用旧 458 cm/s Sprint Loop |
| UnSh | Idle | 0 | 0 | Looping |
| UnSh | Start | +1→0 | 0 | 非循环 |
| UnSh | Loop | 0 | 0 | Looping |
| UnSh | Stop | -1→0 | 将 `Distance` 重命名 | 非循环；不保留 `Distance` 副本 |

### 8.4 Start/Stop 的 Pose Search Notify State

对每条 Start 和 Stop 添加独立 Notify Track：`PoseSearchControl`。

添加 `Pose Search: Block Transition In`：

- Start：从约 0.10 s 到动画末尾前 `1/30s`。
- Stop：从约 0.12 s 到动画末尾前 0.05 s。

它的含义是“搜索不能直接返回到这个区间，但已经从前 0.10/0.12 s 进入的动画可以自然播放进来”。因此 Idle 查询不能直接跳进 Stop 的尾部，Start/Stop 也不会每帧跳到自己中间的另一帧。

再添加 `Pose Search: Override Continuing Pose Cost Bias`：

- Start：仅从 0.0 s 到 0.10 s，`Modifier=0.00`。
- Stop：从 0.0 s 到末尾前约 0.08 s，`Modifier=-0.50`。

Start 的短窗口只覆盖起步的首个搜索节流周期；它不使用负 Continuing Bias 锁住完整动作。`MM_Intent` 是有限的输入边沿语义，而不是完整动画状态：收刀 Walk 在 1.25 s、收刀 Run/Sprint 在 0.80 s、持刀在 0.60 s 从 `+1` 线性降至 `0`。归零后，当前 Start 可由 Pose/Trajectory 自然继续，或自然交接到 Loop；新的 Start 前段不再因持续匹配 `+1` 而重选。`MM_MoveGait` 则负责阻止手柄从死区推到 Run/Sprint 后继续使用错误 Walk Family。`Modifier=0` 覆盖窗口后恢复 PSD 的轻微全局 Continuing Bias，而 Trajectory 和 Pose 仍决定具体候选时间。Stop 则使用最大批准的 `-0.50`，使当前 Stop 优先于重新跳入另一条 Stop 或 Idle，直至作者制作的减速尾段完成。不要使用小于 `-0.50` 的值；那会实质锁死 Stop。

只有确实存在 T Pose/坏帧的区间才使用 `Pose Search: Exclude From Database`。Exclude 会让该段完全不能成为结果，不能拿它代替 Block Transition In。

### 8.5 PMM-3 验收

- 所有正式数据库动画都能在 Curves 面板看到两个精确同名 Float Curve。
- 非 Stop 的距离查询恒为 0。
- 每条 Stop 的 `MM_DistanceToStop` 是原始 `Distance` 的直接重命名：保留原有键和切线（无需强制每个动画采样点一个键，也不强制单调），首键为负的剩余距离，末键为 0，且资产中不再保留 `Distance` 副本。
- Start/Stop 只能从开头约 0.1 s 进入，但进入后能继续播放中后段。
- Start 的 Continuing Notify 只到 0.10 s、Modifier 精确为 0；Stop 到末尾前 0.08 s、Modifier 精确为 -0.50。

### 8.6 自动资产审计（先运行，再做编辑器修改）

`Source/MHGZ/ActionSystem/Tests/MHGZPMMAssetAuditTests.cpp` 提供三个**只读** Automation 测试；它只读取当前两套 PSD 已收录的资产，不会保存或修改任何 `.uasset`：

| 测试 | 检查内容 |
|---|---|
| `MHGZ.PMM.Assets.DatabaseMembership` | `PSD_MH_Shth_Move` 必须正好收录 16 条收刀候选（Idle、三组 Start/Loop、九条 PMM-7 Generated Stop），`PSD_MH_UnSh_Move` 必须正好收录 4 条持刀候选；旧的原始收刀 Stop、Sprint Loop 旧版和冻结的 `LocomotionRefactor` 资源不能混入。 |
| `MHGZ.PMM.Assets.CurveSemantics` | 两条曲线存在；Idle/Loop/非 Stop 距离恒为 0；Start 为 `+1→0`；原始 Stop 为 `-1→0`，PMM-7 Generated Stop 的前置段遵循 14.2、真实 Stop 段复制该语义；`MM_DistanceToStop` 保留原 `Distance` 的曲线语义（真实 Stop 首值为负、末值为 0、全程非正）；Stop 资产中不能再保留 `Distance` 副本；`bLoop` 与资产角色一致。 |
| `MHGZ.PMM.Assets.PoseSearchControlNotifies` | 每条 Start/Stop 有且仅有一个 `Block Transition In` 与一个 `Override Continuing Pose Cost Bias`；两者都在 `PoseSearchControl` 轨道；Start 仅覆盖 0～0.10 s 且 Modifier=0；原始 Stop 使用尾段窗口，PMM-7 Generated Stop 必须严格遵循 14.3 的真实 Stop 起点与末帧前 0.001 s 合同，Modifier 均为 -0.50。 |

在编辑器的 **Session Frontend → Automation** 中筛选 `MHGZ.PMM.Assets` 并运行；也可在已完成 C++ 编译后以无界面编辑器运行同一筛选。先运行一次会得到逐资产的缺口清单，再只打开失败资产修正。全部通过才算 PMM-3 的资产侧验收完成。

引擎的 `FName` 比较不区分大小写，因此自动测试不能可靠地区分存储名的 `mm_intent` 与 `MM_Intent`；仍要在第一次编辑该曲线时按 8.1 目视确认其最终显示为 `MM_Intent`。这只是命名大小写的最后一项手工审计，不影响曲线、Notify、数据库成员和数值形状的脚本检测。

## 9. PMM-4：重配 PSS 和 PSD

### 9.1 `PSS_MH_Move`

保持 `Sample Rate=60`、`Data Preprocessor=Normalize`。

Pose Channel：

1. 只保留当前左右脚两个采样骨骼；如果当前条目不是左右脚，则改为骨架中实际的 Left Foot/Right Foot 骨骼。
2. 每只脚启用 `Position + Velocity + Phase`。
3. 每只脚 Weight 先设 1；Pose Channel 总 Weight 保持 2。
4. `Input Query Pose=Use Continuing Pose`。
5. 不加入手、武器、头或脊柱；它们会把上半身动作差异误当成普通移动选择依据。

这里的 Phase 是脚步周期特征，用于区分左脚/右脚处于摆动还是支撑阶段；它与移动朝向无关。左右脚 Stop 会由这个姿势特征自然竞争。

Trajectory Channel：

| Offset | Flags | Sample Weight |
|---:|---|---:|
| 0.2 | Position | 1 |
| 0.5 | Position | 1 |
| 0.8 | Position | 1 |
| 1.0 | Position | 1 |

- Trajectory Channel 总 Weight 使用 `6`。在 `MM_Intent=10` 已区分 Start/Loop/Stop 语义的前提下，这个值让预测位移更明确地区分 Walk/Run 的根位移形状；它不是概率，也不能替代正确的运行时速度档位查询。
- 不添加负 Offset。
- 必须选完整 `Position`，不能选 `Position XY`：后者会剥离 Z 分量，而本项目导入 Root 的坐标约定会让前进信息在被剥离的维度上丢失。
- 查询轨迹始终只沿本地 `+Y` 写入距离；不要添加 Facing Direction、Velocity Direction 或 Heading。

`BPSC_MMIntent`：

- `Curve Name=MM_Intent`
- `Weight=10`
- `Sample Time Offset=0`

`BPSC_MMDistanceToStop`：

- `Curve Name=MM_DistanceToStop`
- `Weight=4`
- `Sample Time Offset=0`

权重不是概率。这里先让语义频道明显高于普通姿势成本，以便验证查询真的接通；PMM-6 再依据 Cost Breakdown 下调，而不是盲目靠观感猜。

### 9.2 `PSD_MH_Shth_Move`

1. 加入缺失的 `AS_Shth_Walk_Stop_Left`。
2. 确认正式 Sprint Loop 使用 `AS_Shth_Sprint_Loop_125x`，不使用速度约 458 的旧 Sprint Loop。
3. **所有** Idle、Start、Loop、Stop 条目设置 `Disable Reselection=true`。它只禁止全局搜索跳回当前 Source Asset；`Continuing Pose` 仍会首先评估并可继续推进当前动画。因此 Start/Stop 不会每次搜索又回到自身的 0 帧。
4. 不要为 Start/Stop 关闭该选项；该旧配置会使每个 Search Throttle 都有机会重启同一条过渡动画。
5. 所有条目 `Mirror Option=Original Only`；本项目不靠镜像制造方向动画。
6. `Exclude From Database Parameters` 从 `[0,-0.3]` 改为 `[0,-0.05]`。
7. `Continuing Pose Cost Bias=-0.05`。
8. `Looping Cost Bias=0.0`，先取消默认负值对 Loop 的额外偏爱。
9. 调试期间 `Pose Search Mode=Brute Force`；小型数据库优先保证成本结果完全可解释。

### 9.3 `PSD_MH_UnSh_Move`

使用同样的数据库级设置。只保留持刀前向 Idle、Start、Loop、Stop；不把左/右/后翻滚、攻击 Exit 或收刀 Montage 序列加入普通移动数据库。

### 9.4 PMM-4 验收

- 两个 PSD 都能完成索引，无缺失 Curve/Skeleton 报错。
- Sheathed PSD 同时包含 Walk Stop Left/Right 和 Run Stop Left/Right。
- Debugger 中被 Block Transition 标记的 Start/Stop 中后段不会作为新搜索结果返回。
- 连续移动时不再在同一 Loop 资产的相邻帧间反复重选。

### 9.5 非交互式配置与审计

本项目提供两个可重复运行的 Editor Commandlet，因此 PMM-4 的 PSS/PSD 配置不需要手动逐项编辑：

1. 关闭普通 Unreal Editor 后，运行 `MHGZPMM4AssetFixup`。它只改动 `PSS_MH_Move`、`PSD_MH_Shth_Move`、`PSD_MH_UnSh_Move`：设置本节 9.1～9.3 的 Channel、权重、候选 Reselection、Mirror、Bias、Brute Force，并同步重建两个数据库索引。
2. 再运行只读的 `MHGZPMM4AssetReport`。它会主动等待索引完成；任一 Schema、Channel、数据库参数、索引维度、Block Transition 元数据或候选策略不符合本节合同时，Commandlet 以非零退出。

若 CSV 已明确显示同一 Start 在每次 Search Throttle 后又跳回约 `0～0.1s`，使用
`MHGZPMM4AssetFixup -OnlyDisableReselection`：它只把两套 PSD 的全部候选改为
`Disable Reselection=true` 并重建索引，不改 PSS、AnimBP 或动画序列。

`PositionXY` 坐标错误修复后，`MHGZPMM4AssetReport` 已于 2026-08-26 验证 Schema Cardinality=`30`（四个样本各由 2 个分量增加到 3 个分量）；`PSD_MH_Shth_Move` 保持 801 个姿势、`PSD_MH_UnSh_Move` 保持 368 个姿势，两个 IndexDimensions 均为 `30`。Brute Force 的引擎搜索路径会将具有 Block Transition 元数据的姿势标记为 `DiscardedBy_BlockTransition`，所以 Start/Stop 的被阻塞区不会成为新搜索结果。

## 10. PMM-5：切换 AnimBP 到新查询

### 10.1 切轨迹输入

1. 打开 `ABP_MH_Character -> AnimGraph`。
2. 保持两个 Motion Matching 节点、`Unsheathed` Bool 分支、Pose History、`UpperBody_IGAction` 和 `DefaultSlot` 的层级关系不变。
3. 找到 Pose History 的 `Trajectory` 输入。
4. 断开旧 Blueprint 变量 `Trajectory`。
5. 从 Inherited Variables 拖入 `Get MMPredictedTrajectory`，接到 Pose History 的 `Trajectory`。
6. Compile、Save。

Motion Matching 节点的 Database 引脚仍分别使用当前 `PSD_MH_Shth_Move` 和 `PSD_MH_UnSh_Move`；不要为 Idle/Start/Stop 再创建不同 MM 节点。

### 10.2 调试阶段关闭混合

第一次 PIE 先保持两个 MM 节点：

- `Blend Time=0`
- `Use Inertial Blend=false`
- `Max Active Blends=0`
- `Search Throttle Time=0.12`

这样可以直接看见算法究竟选了哪一帧，不让混合掩盖错误选择。此阶段出现硬切是预期现象，不作为表现失败。

若记录已经确认“同一 Start 在数帧内反复从 0～0.1 秒重选”，关闭普通 Editor 后运行
`UnrealEditor-Cmd MHGZ.uproject -run=MHGZPMM5AnimGraphFixup`。该命令只修改
`ABP_MH_Character` 中已有的两个 Motion Matching 节点：`Search Throttle Time=0.12`、
`Pose Reselect History=0.30`、`Reset on Becoming Relevant=true`，并保持 `Blend Time=0`、
`Max Active Blends=0`。它不创建动画状态机、不新增候选，也不改 AnimGraph 接线；0.12 秒只是在
已选片段刚进入后暂缓下一次全库搜索，使 Continuing Pose 有机会赢得下一次比较。

若记录显示**不同** Start 资产（例如 Walk/Run/Sprint Start）交替从 0 帧进入，先检查 PSS
Trajectory Channel 是否为完整 `Position`，并确认查询轨迹沿导入约定的本地 `+Y` 写入。不得以连接
`Should Search` 引脚来掩盖该问题；它不会让错误的成本计算选择正确的 Start。

### 10.3 删除旧查询图

只有新轨迹 PIE 验收通过后才删除：

- 旧 `setTrajectory` 函数及其调用；
- 旧 Blueprint 变量 `Trajectory`；
- 旧 `MM_IntentQuery`；
- 旧 `LastNonZeroTargetSpeed`；
- 只为旧直线轨迹服务的 `DesiredSpeed` AnimBP 本地副本。

不要删除 `AMHGZCharacter::DesiredSpeed` 本身；它可能仍被旧文档/调试或其他动作读取，源码清理属于单独审计任务。

### 10.4 Root Motion 设置

普通 MM 仍依赖动画 Root Motion，所以：

- `ABP_MH_Character` 保持能从普通动画序列提取 Root Motion 的模式；不能改为 `Root Motion From Montages Only`。
- 正式 Idle/Start/Loop/Stop 保持 `Enable Root Motion=true`。
- 角色普通移动仍不调用 `AddMovementInput`。
- Montage 取得 `MontageRootMotionOwner` 或动作取得 `BlockMovement` 时，新 AnimInstance 必须输出平坦轨迹并重置速度历史。

#### 10.4.1 动作 Root Motion 期间必须旁路普通 MM（必做）

只在 C++ 里把速度、轨迹和语义查询清零还不够：两个 Motion Matching 节点若持续相关，仍会在
Montage 覆盖期间搜索并推进其内部 Asset Player；Montage 结束的第一帧便可能恢复一个 Walk Loop 的
Root Motion，随后被误判为“玩家松开摇杆的停步”。

`UMHGZMotionMatchingAnimInstance::bMMForceIdle` 已同步暴露当前的**有效** Force Idle（外部 `bForceMMIdle` 加默认 `0.05s` 的释放保持）。在
`ABP_MH_Character` 的 **Pose History 之前**按以下方式接线：

1. 保留现有两个 Motion Matching 节点及其 `Unsheathed` Bool 分支；该分支输出作为 `NormalLocomotion`。
2. 新建两个 `Sequence Player`，分别指定 `AS_Shth_Idle` 与 `AS_UnSh_Idle`，再用 `Unsheathed` Bool
   合成为 `ForcedIdle`。只使用这两个正式 Idle；不要使用 Montage 片段或 Stop 序列。
3. 新建 `Blend Poses by Bool`：`False Pose=NormalLocomotion`，`True Pose=ForcedIdle`，
   `Active Value=bMMForceIdle`，两个 Blend Time 都设为 `0`。
4. 将这个新节点的输出接入原 `Pose History -> Source`，替代旧 `Unsheathed` Bool 分支的直连；其余
   `Save Cached Pose`、Slot 和分层节点不改。

这样强制期间两个 MM 节点会失去相关性；外部动作锁解除后，`bMMForceIdle` 仍维持默认 `0.05s`，吸收
Montage 的尾帧 Root Motion。返回普通移动时节点的 `Reset on Becoming Relevant=true` 会重置旧选择，再用
平坦 Idle 历史重新搜索。该分支不是移动状态机：它只隔离动作 Montage 与普通 locomotion 的 Root Motion 所有权。

### 10.5 恢复最终混合

选择结果稳定后，再同时调整两个 MM 节点：

- `Blend Time=0.08` 起步，最多先试到 0.12。
- `Use Inertial Blend=false`。
- `Max Active Blends=2`，使用节点内部 Blend Stack。
- `Pose Reselect History=0.3`。
- `Play Rate=[1.0,1.0]` 先保持固定；速度资产已经与目标巡航速度匹配。

不添加 Inertialization 节点，因此不会再出现“未找到请求来自的惯性化节点”。Bool 姿态分支先用 0；确认第一次拔刀不误选 Stop 后，可尝试 `0.05`，但不得通过长 Blend 掩盖错误候选。

### 10.6 PMM-5 验收

- Idle 推摇杆，第一搜索结果来自对应 Start 的前 0.1 s 可进入区。
- Start 播放过程中不会每帧跳到 Loop；到稳定速度后能自然转入正确 Loop。
- 松摇杆时第一搜索结果来自 Stop 的前 0.12 s，而不是 Stop 尾帧或 Idle。
- Stop 结束后进入 Idle，不反复后撤或鬼畜。
- 第一次拔刀、后续拔刀和动作 Montage 结束均不触发完整 Stop。
- 左右输入只改变 Actor 朝向，数据库候选仍是前向动画；没有查找不存在的 Direction 资产。

## 11. PMM-6：按成本调试，不按概率猜参数

### 11.1 固定测试矩阵

每项至少重复 10 次：

1. Idle→小摇杆 Walk。
2. Idle→满摇杆 Run。
3. Idle→满摇杆并长按 Sprint。
4. Walk/Run/Sprint 各自松摇杆停步。
5. 连续 Walk↔Run 与 Run↔Sprint，不应重新进入 Idle Start。
6. 收刀/拔刀各 5 次，特别观察第一次姿态切换。
7. 上半身送虫/收虫时移动与停步。
8. 翻滚/攻击/收刀 Montage 结束后无输入回 Idle，有输入回正常前向 locomotion。

### 11.2 Debugger 观察顺序

每次错误选择都按以下顺序查看 Cost Breakdown：

1. `MM_Intent`：输入边沿的查询是否真的是 +1 或 -1。
2. `MM_DistanceToStop`：是否与 Stop 开头的剩余距离同号、同数量级。
3. Trajectory：起步点间距是否逐渐增大；停步后远期点是否重合。
4. Pose：左右脚的 Position/Velocity/Phase 是否让合适脚相的 Stop 更低成本。
5. Continuing Pose：Start/Stop 已经进入后，继续播放成本是否低于无意义的跳转。

### 11.2.1 可交给外部分析的运行时捕获

`UMHGZMotionMatchingAnimInstance` 提供一个默认关闭的**运行时遥测**捕获器。它不改变 Pose Search 的
候选、成本、播放或 Root Motion；因此可在 PMM-5、PMM-6 以及后续任何 PIE 复现中启用。每次启用创建
一个独立会话目录：`Saved/RuntimeTelemetry/<SessionId>/`。根目录的 `README.md` 由程序自动写入，专门
说明所有子目录、列含义、关联方式和不可从 CSV 推断的内容，供人或大模型读取；不要再把本项目遥测写入
UE 自带的 `Saved/Logs/`。

会话目录按领域拆分：

```text
Input/RawInput.csv                 # 实际手柄摇杆、持有物理按键、冲刺键
Input/ParsedInput.csv              # Router 解析后的 Input.* Tag 事件
Character/State.csv                # ASC Tag、收/拔刀、锁移动、Health/Stamina 等属性
Character/Spatial.csv              # 坐标、朝向、偏转、速度、加速度和移动目标
MotionMatching/Query.csv           # MM 查询向量、Stop/Start 模式、预测轨迹
MotionMatching/Selection.csv       # 每次 MM 选中结果及语义比较数据
MotionMatching/PoseSearchCandidates.csv   # 第二档：原生 Trace 的 Top-N 候选、标志与成本
MotionMatching/PoseSearchChannelCosts.csv # 第二档：候选的 PSS 频道特征成本
MotionMatching/PoseSearchDetail.utrace    # 第二档：可用 Unreal Insights 复核的原始 Trace
MotionMatching/PoseSearchDetail-Status.txt # 第二档是否成功导出的明确状态
Animation/Playback.csv             # Active GA、Montage、根运动所有权
```

状态采样文件默认 60 Hz；`ParsedInput` 和 `Selection` 保留每一个实际事件/节点更新，因此不会漏掉短按或
连续姿势时间。用 `Frame` 关联；输入事件同时保留捕获帧和 Router 原始时间戳，不能只按相邻行号推断。

在 PIE 控制台执行：

```text
mhgz.Telemetry.Enable 1
mhgz.Telemetry.SampleRateHz 60
```

复现一次问题后执行 `mhgz.Telemetry.Enable 0`，或直接停止 PIE。控制台和日志会以 `LogMHGZMM` 输出
完整会话目录；若需要更频繁落盘，`mhgz.Telemetry.FlushRows` 可设为 `1`～`600`（默认 `60`）。

`MotionMatching/Selection.csv` 除了最终候选和引擎 `SearchCost`，还记录：本次搜索使用的四个 Query 值、
候选资产在 `SelectedTime` 的四条曲线值、两者差值，以及按当前文档权重计算的
`*WeightedSquaredEstimate`。后者只是未归一化的标量诊断估算，**不是** Unreal 内部逐频道成本。

#### 第二档：原生候选/成本捕获（附加项）

只有当第一档 `Selection.csv` 还不能解释“为什么另一个候选胜出”时，才开启第二档。它不改动 PSS 的
搜索和权重；它只是让 UE 5.6 在短时间内输出原生 `PoseSearch` Trace，并在停止录制后立即转成 CSV。
必须在 `Enable 1` **之前**输入：

```text
mhgz.Telemetry.PoseSearchDetail 1
mhgz.Telemetry.PoseSearchTopN 24   # 可选；1~256，默认 24
mhgz.Telemetry.Enable 1
```

复现 5~10 秒内的单一问题后只需执行：

```text
mhgz.Telemetry.Enable 0
```

停止时系统会关闭**自己启动的** `PoseSearch,Object` Trace，并在当前会话的 `MotionMatching/` 下保留：

- `PoseSearchDetail.utrace`：UE 原始数据，可在 Unreal Insights/Pose Search Debugger 中交叉检查。
- `PoseSearchCandidates.csv`：每个搜索、每个数据库按引擎记录的 `TotalCost` 升序输出 Top-N。它包含
  `DissimilarityCost`、`NotifyCostAddend`、`ContinuingPoseCostAddend`、
  `ContinuingInteractionCostAddend` 以及 `Flags`；这些总成本和附加成本来自引擎 Trace，而非项目估算。
- `PoseSearchChannelCosts.csv`：对同一候选，以 Trace 内保存的 Query Vector 和当前 PSS 的
  `DynamicWeightsSqrt` 重建每个顶层/子频道的特征成本。父频道与子频道会同时出现，不能把两者相加。
- `PoseSearchDetail-Status.txt`：导出行数、无法解析的数据库数，或“已有其他 Unreal Trace 正在录制”时的
  跳过原因。

引擎每次搜索每个数据库至多保留 256 个低成本候选；`PoseSearchTopN` 只限制 CSV 导出行数，不改变这项
引擎上限。频道成本是导出时的重建结果，因此从开启第二档到录制停止期间不要编辑/重建 PSS 或 Schema。
它比第一档昂贵得多，不应用于普通 PIE 回归。若编辑器已经被 Unreal Insights、Rewind Debugger 或其他工具
占用 Trace，第二档会安全跳过，绝不会停止对方的 Trace；先结束已有录制，再重新开始本次遥测即可。

分类后可以直接区分：“实际手柄没有输入”“输入被 Router 解析但无 GA 激活”“GA/Montage 锁住移动”
“查询正确但 PSS 候选错误”与“选帧正确但 Blend/Root Motion 表现错误”。

特别地，若异常阶段同时满足 `HasRawMovementInput=1`、`HasLocomotionInput=1`、`BlockMovement=0`，且
`ActorToRawInputYawDelta` 正在缩小，则“角色仍可转向但没有移动”**不能**归因于 `BlockMovement`；应继续
检查 `Animation/Playback` 的根运动所有权/Active Montage 覆盖，以及 `MotionMatching/Selection` 是否
实际重新选择了 locomotion 候选。

为让 `MMSelection` 生效，`ABP_MH_Character` 的两个现有 Motion Matching 节点必须各创建一个
**On Update Motion Matching State** Node Function：把其 `Anim Node Reference` 输入连接到
`Queue Motion Matching Selection`，并分别将枚举设为 `Sheathed`、`Unsheathed`。这是节点运行时
搜索完成后的唯一安全回调点；不要从 Event Graph 或 Tick 调用该函数。接线后，把同一会话目录和相同
复现时段的 `Saved/Logs/MHGZ.log` 提供给代码协作方即可复盘，无需手工记录每一帧数值。

这里的“各创建一个”是硬性要求：不要让收刀和持刀两个节点共用同一个 `OnMmUpdate` 函数，因为
`Queue Motion Matching Selection` 的枚举参数必须在函数图中固定为对应节点的类型。推荐使用
`OnMmUpdate_Sheathed` 与 `OnMmUpdate_Unsheathed` 两个函数；每个函数均为 `Entry -> Queue Motion
Matching Selection -> Return`，只把本函数的 `Node` 引脚接入 Queue 的 `Anim Node Reference`，并在
Queue 节点的 `Motion Matching Node` 默认值中分别选 `Sheathed` 或 `Unsheathed`。若函数图仍是
`Entry -> Return`，C++ 虽然已经编译，但运行时不会产生 `MMSelection`，收刀 Stop 曲线跟随也不会
进入 `FollowingExtendedStopCurve`。

### 11.3 调参顺序

只按以下顺序改，一次只改一组：

1. 查询值错误：修 C++，不改 PSS 权重。
2. 轨迹形状错误：调对应步态 Acceleration/Deceleration。
3. Start/Stop 从中段进入：修 Block Transition In 范围。
4. Start/Stop 的 Continuing 行为不对：先确认 `MM_Intent`、`MM_DistanceToStop` 与轨迹查询正确，再调 Notify。Start 必须仅覆盖 `0～0.10s` 且默认 `0.00`，以允许死区后的满幅输入切换到正确 Start；Stop 默认 `-0.50` 直到尾段，以防链式重选或过早 Idle。关闭普通 Editor 后运行 `UnrealEditor-Cmd MHGZ.uproject -run=MHGZPMMAssetFixup` 使窗口和 Modifier 同时落盘；若只改数值可使用 `-OnlyContinuingModifier -StartContinuingBiasMagnitudeHundredths=0 -StopContinuingBiasMagnitudeHundredths=50`。Start 允许范围 `[-0.25,0]`，Stop 为 `[-0.50,-0.25]`。
5. Start/Stop 从不获胜，但查询正确：先把 `MM_Intent` 从 10 提到 12；确认后再回落。
6. 停步时间点错误：修 `MM_DistanceToStop` 曲线或把该频道权重从 4 调到 6。
7. 左右脚 Stop 选择不稳：先核对双脚 Phase/Velocity，再调 Pose 权重；不要写方向分支。
8. 最后才调 Blend Time；混合只修姿势过渡，不修错误候选。

### 11.4 最终退出条件

- 固定矩阵中不再出现 Loop 只播某只脚几帧、Idle 误入 Stop 尾部或首次拔刀触发 Stop。
- Start/Stop 在 Debugger 中由正确查询维度获胜，不是靠极端负 Cost Bias 强行锁住。
- 正常移动只有一个位移所有者：Animation Root Motion。
- `ABP_MH_Character` 不再运行旧 `DesiredSpeed × Time` 直线轨迹。
- PSS 不包含方向、Heading、Facing 或负 Offset；项目只有前向动画的限制在资产和代码中保持一致。

## 12. 禁止事项

- 不要同时实施 CMC + in-place 状态机文档和本文 Root Motion MM。
- 不要给每个方向复制同一条前向动画冒充方向库。
- 不要用 Stop Left/Right 判断摇杆左右；它们是脚相差异。
- 不要在 BPSC 的 `Get World Curve` 中读取 World、Pawn、CharacterMovement 或 Gameplay Ability 状态。
- 不要继续让 BPSC 保持空实现；查询恒 0 时调权重没有意义。
- 不要只给 Stop 距离曲线首尾两个线性键；使用真实逐帧剩余 Root Motion 距离。
- 不要用长 Blend Time、Inertialization 或高 Continuing Bias 掩盖错误选帧。
- 不要把动作 Montage 的单帧位移计入普通移动实际速度；`bForceMMIdle` 边沿必须重置历史，并且
  AnimGraph 必须在 Pose History **之前**用 `bMMForceIdle` 切到相应 Idle Pose。仅在 C++ 中清零查询，
  不能阻止失活前的 MM Loop 在 Montage 结束时重新输出 Root Motion。
- 不要在选择稳定前把 Brute Force 改回 PCA KD Tree。

## 13. 实施顺序摘要

```text
PMM-0  Git 基线 + 复现录屏
  -> PMM-1  C++ AnimInstance 产生实际速度、语义查询、停步距离和加减速轨迹
  -> PMM-2  Reparent ABP；两个 BPSC 只读 C++ AnimInstance 缓存值
  -> PMM-3  统一曲线；Start/Stop 加 Block Transition 与 Continuing Bias
  -> PMM-4  重配 PSS/PSD；补 Walk Stop Left；Brute Force 调试
  -> PMM-5  Pose History 切到新轨迹；先零混合验证，再恢复短 Blend Stack
  -> PMM-6  用 Pose Search Debugger 分频道调参并完成固定测试矩阵
```

只有 PMM-6 与 PMM-7 全部通过，才把普通移动视为可供后续大量 GA、动作 Exit 和白灯加速表现依赖的稳定基础。

## 14. PMM-7：相位化 Extended Stop（替代速度驱动的 Stop 意图衰减）

### 14.1 要解决的问题与边界

本节只解决普通移动中“松开摇杆后，应从松杆前最后接地脚对应的步态相位自然衔接到正确 Stop”的问题。它针对已在 CSV 中出现的两类现象：

1. 小步幅 Stop 尚未结束时又被另一个 Stop 接走；
2. 左右 Stop 时长不同，而旧 `MMIntentQuery=-ActualSpeed2D/LastNonZeroCruiseSpeed` 与动画相位无关，导致同一查询进度对两个脚相位有不同语义。

以下事项不属于本节：

- 不创建 `Idle/Starting/Looping/Stopping` 动画状态机；
- 不用脚本在运行时指定“播放 Left Stop 或 Right Stop”；
- 不改变 Actor 转向、输入速度档位、CMC、Root Motion 所有权或动作 Montage 旁路；
- 不为目前只有一个 Stop 的持刀移动猜造左右脚版本；`AS_UnSh_Walk_Stop` 保持原状，直到获得可验证的第二相位资源；
- 生成器本身不直接改 PSS/PSD；只有紧随其后的 `MHGZPMM7AssetFixup` 通过审计并重建索引后，才提升为正式候选。

这里的“最后接地脚”严格指**松开摇杆前，循环步态中最后完成接地的脚**。它不是 Stop 动画最终落地的脚，也不是摇杆的左右方向。运行时不需要显式存储这个脚；PSS 应通过当前双脚 Pose/Velocity/Phase 在对应的 Extended Stop 前置段中自行命中正确时刻。

### 14.2 候选资产结构

以收刀 Run 为例，令：

- `tL`：`AS_Shth_Run_Loop` 中与 `AS_Shth_Run_Stop_Left` 第 0 帧最适合衔接的 Loop 帧；
- `tR`：`AS_Shth_Run_Loop` 中与 `AS_Shth_Run_Stop_Right` 第 0 帧最适合衔接的 Loop 帧。

`tL/tR` 不由文件名或人工估算决定。生成器在一个完整 Loop 周期内对每一帧评分，评分使用共同骨骼的局部姿势、局部平移速度和旋转差；Root 不参与姿势评分。生成器输出分数、帧号、秒数和接缝误差供审计。若原动画包含多个重复周期，必须先显式指定单个基础周期；不得把多个周期误当作一个相位环。

生成两个**新的、不覆盖原始资产**的非循环 `UAnimSequence`：

```text
AS_Shth_Run_Stop_Left_Extended
    = Loop[tR 的下一帧 -> tL] + AS_Shth_Run_Stop_Left[0 -> End]

AS_Shth_Run_Stop_Right_Extended
    = Loop[tL 的下一帧 -> tR] + AS_Shth_Run_Stop_Right[0 -> End]
```

区间沿 Loop 正向前进并允许在尾部回绕。两个前置段不留空帧，合起来恰好覆盖一个完整步态周期：任意松杆相位都能在其中一个前置段找到原样的 Loop Pose。生成器对跨越 Loop 尾部的 Root Transform 做连续重基，且在 Loop -> Stop 接缝将 Stop Root 相对其第 0 帧重基到前置段末帧；因此不会因源动画从原点重置而额外制造位移跳变。

不要仅创建 Animation Composite 作为最终资产。Pose Search 可以索引 Composite，但子资产曲线会被合并，无法可靠地让“同一段 Loop 在普通 Loop 中为 `0`，而在 Extended Stop 前置段中为 `-1`”。最终资产必须烘焙为独立 `UAnimSequence`，以拥有独立的曲线和 Notify。

### 14.3 曲线与 Pose Search Notify 合同

`MM_Intent` 的含义改为“当前候选的 Stop 请求/进度”，不再由实际速度倒推：

| 区段 | `MM_Intent` | `MM_DistanceToStop` | `MM_StopGait` |
|---|---:|---:|---:|
| 普通 Idle/Start/Loop、持刀候选 | 既有合同或 `0` | `0` | `0` |
| Extended Stop 的 Loop 前置段 | 恒为 `-1` | `0` | Walk=`1/3`、Run=`2/3`、Sprint=`1` |
| Extended Stop 的真实 Stop 段 | 复制源 Stop 的 `-1 -> 0` 曲线 | 复制源 Stop 的曲线 | 保持本资产的固定步态族值直到最后可索引帧；StopMode 仅由前两列归零结束 |
| FirstStepCommitStop 的 Start 前缀 | 恒为 `-1` | `0` | 与所属 Family 相同 |

不要把 Extended Stop 前置段的 `MM_Intent=-1` 回填到普通 Loop；普通 Loop 必须继续为 `0`。松杆首帧主动提交 `-1`，正是它把搜索从普通 Loop 候选切换到 Extended Stop 前置段。

Extended Stop 的 `PoseSearchControl` 合同与旧 Stop 不同。以下时间边界均以 **PSS 的实际采样帧** 定义；本项目当前为 60Hz，不能使用 `0.001s` 之类不对应索引帧的时间魔数：

1. 前置 Loop 段只开放至真实 Stop 前的最后一个合法 Prefix 索引帧；真实 Stop 的第一个索引帧不得作为新的全局入口；
2. `Block Transition In` 从真实 Stop 的**第一个 PSS 索引帧**开始，覆盖真实 Stop、语义归零尾段以及最后一个可索引帧；这些帧只能由已选中的动画 Continuing 经过；
3. `Override Continuing Pose Cost Bias=-0.50` 覆盖从资产开头到最后一个可索引帧，使已选中的 Extended Stop 能自然走完；
4. 每个 Extended Stop 禁止同源全库重选，但保留 Continuing Pose 评估。

`BlockTransition` 是“全局搜索能否新进入”的黑名单，不会阻断已在同一资产上推进的 Continuing。第 2、3 条必须覆盖同一条真实 Stop 曲线仍可能输出负值的索引区间，并额外覆盖曲线已归零但仍需让 StopMode 安全清理的索引尾段。旧的 `+0.05s` / `-0.05s` / `-0.08s` 通用窗口以及生成器中的 `0.001s` 边界均为历史方案；它们与数据库级尾部裁剪叠加后会造成“曲线仍是 Stop、当前资产已不能 Continuing、另一条 Stop 又有可进入边界样本”的空档。其完整修复合同见 §14.10。

### 14.4 运行时查询合同

这不是“代码指定播放某个 Stop”。运行时只管理查询模式；最终 Left/Right 和候选内部时间仍由 PSS 成本选择。

新增内部枚举：

```text
None
AwaitingExtendedStopCandidate
FollowingExtendedStopCurve
```

数据流必须如下：

```text
持续有移动输入：
    MMIntentQuery = Start(+1 -> 0) 或 Loop(0)

检测到移动输入下降沿：
    StopMode = AwaitingExtendedStopCandidate
    MMIntentQuery = -1
    MMDistanceToStopQuery = 0
    MMStopGaitQuery = 当前实际选中的收刀 Walk/Run/Sprint Family
                      （尚无选择结果时才回退 TargetCruiseSpeed）

Motion Matching 全库搜索：
    通过当前双脚 Pose/Velocity/Phase
    命中 Left_Extended 或 Right_Extended 的正确前置 Loop 时刻

下一帧 NativeUpdate：
    若上一帧的实际 MM 结果属于 Extended Stop：
        StopMode = FollowingExtendedStopCurve
        MMIntentQuery = Sample(SelectedAnim, SelectedTime, "MM_Intent")
        MMDistanceToStopQuery = Sample(SelectedAnim, SelectedTime, "MM_DistanceToStop")
        MMStopGaitQuery = Sample(SelectedAnim, SelectedTime, "MM_StopGait")

随后每帧：
    仅继续采样“本次松杆已经接受”的同一条 Extended Stop。
    新结果必须是 Continuing，且选中时间不得倒退；否则说明 PSS 已重新进行全局搜索，
    当前 Stop 请求立即失效，输出 0 查询，不得从新 Stop 的前置段重新采样 -1。
    前置段自然保持 -1/0/所属步态族；进入真实 Stop 后，查询自动跟随资产曲线。

正常终止：
    已接受 Stop 的 `MM_Intent` 与 `MM_DistanceToStop` 都达到 0 时清除 StopMode，
    输出 0 查询，让 Idle 正常参与后续搜索。
    这两个 0 必须在当前资产仍有至少一个后继的有效 Continuing 索引帧时被读取；
    不使用尾段定时强制交给 Idle，已接受的 Stop 可以完整播放。

防回环终止：
    若出现非 Continuing 地重选另一条 Stop，或把同一条 Stop 跳回较早时间，立即清除 StopMode。
    此操作只撤销下一帧的 Stop 查询，不会强制截断当前已经输出的动画；它是异常防御，
    不得成为正常 Stop 结束路径。
```

实现时不能直接用 `UAnimInstance::GetCurveValue("MM_Intent")` 作为唯一来源：它是整个 AnimGraph 的混合结果，可能混入 Action Slot/Montage。应由两个 Motion Matching 节点的 **On Update Motion Matching State** 回调每帧安全排队 `SelectedAnim`、`SelectedTime`、节点类型和 Continuing 状态；在游戏线程的 `NativeUpdateAnimation` 中消费最新结果，并对该 `UAnimSequenceBase` 在 `SelectedTime` 直接调用 `EvaluateCurveData`。

修复前的 `QueueMotionMatchingSelection` 只用于可选 CSV 录制，且跳过 Continuing 结果，不能承担这个运行时职责；这是 PMM-7 必须消除的旧行为。运行时结果队列必须始终启用，不能为了复用遥测逻辑而让功能依赖 `mhgz.Telemetry.Enable`。

代码侧现状：`UMHGZMotionMatchingAnimInstance` 已将 Queue 改为始终记录有效的 `SelectedAnim`、
`SelectedTime`、Continuing 和节点类型；`NativeUpdateAnimation` 在游戏线程消费最新结果。收刀路径
只在松杆下降沿输出 `MMIntentQuery=-1`，并锁存**实际选中**的 Walk/Run/Sprint Family；实际选中当前
命名的 `FirstStepCommitStop` 或 `*_Stop_*_Extended` 后，才从该 `UAnimSequenceBase` 的当前时间采样
`MM_Intent`、`MM_DistanceToStop` 与 `MM_StopGait`，并在曲线回到 0 时清除 StopMode；若 PSS 在
Stop 尾段非 Continuing 地重搜 Stop 或回跳到较早时间，也会撤销本次已消费的 Stop 请求，防止把
新候选开头的 `-1` 再次写回查询。它不指定
Left/Right 或某条动画，只给 PSS 一个步态族约束；双脚 Pose/Phase 仍决定真正命中的 Stop。持刀路径
在首次选中 `AS_UnSh_Walk_Stop` 后同样跟随该资产的 `MM_Intent` 与 `MM_DistanceToStop` 至零，修复了
旧速度推导导致持刀 Stop 中途回 Idle 的问题。

若玩家在真实 Stop 开始前重新推杆：清除 StopMode 并恢复常规查询，允许 PSS 从前置段重新找正常 Loop/Start。若真实 Stop 已进入承诺区，是否允许重新输入打断属于后续动作手感决策；本阶段默认保持 Continuing 直到尾段，以便先验证无链式 Stop。

### 14.5 自动生成、正式接入与重建（基础接入已完成，最终验收未完成）

生成器的正式范围是 Walk、Run、Sprint 三个 Family，每个 Family 生成两个 `ExtendedStop` 和一个
`FirstStepCommitStop`，共九条独立 `UAnimSequence`，统一写入：

```text
/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/Generated/
```

重建顺序固定如下：

```text
UnrealEditor-Cmd MHGZ.uproject -run=MHGZPMMExtendedStop -ReplaceGenerated
UnrealEditor-Cmd MHGZ.uproject -run=MHGZPMMAssetFixup
UnrealEditor-Cmd MHGZ.uproject -run=MHGZPMM7AssetFixup
```

第一条命令仅覆盖 `Generated/` 下由生成器命名的九条资产，绝不改动源动画；它写入
`Saved/MotionMatching/ExtendedStopBuild-*.csv`。第二条命令重新写正式 Idle/Start/Loop/原始 Stop 的
语义曲线和 PoseSearchControl Notify；第三条命令才是正式接入步骤：它为全部候选写入
`MM_StopGait`（普通候选为 `0`，生成候选为所属 Family 值直至最后可索引帧），在 PSS 安装原生 StopGait
频道，并把 PSD 收敛为收刀 16 条、持刀 4 条候选后重建两个索引。它写入/更新同一审计输出。

当前接入后的收刀 PSD 由 Idle、Walk/Run/Sprint 的 Start/Loop，以及九条 Generated Stop 组成；旧的
原始收刀 Stop 不再作为正式候选。持刀仍是 Idle、Walk Start/Loop/Stop，未错误混入收刀生成资产。

### 14.6 PMM-7 当前验收与剩余人工项

代码、曲线、候选集合和索引的**基础接入**已由 `MHGZ.PMM` 自动化测试以及生成器审计验证；这不代表
PMM-7 已验收。最新 PIE Telemetry 已复现 Stop 尾部失去 Continuing 后的一次全局 Stop 重选，因此下面的
人工项尚未全部通过，PMM-7 当前状态是“基础接入完成，转入 PMM-7.1 修复”。

1. 每个 Family 从任意 Loop 相位松杆至少 10 次，必须选到同 Family 的 Generated Stop，不能因
   目标速度恰好落到 Walk 而把 Run/Sprint 停步换成 Walk；
2. 从 Start 第一次落脚前松杆，必须选择同 Family 的 `FirstStepCommitStop` 并先迈出第一步；
3. Stop 开始后不得再链到第二条 Stop；曲线归零时当前资产仍为 Continuing，下一次全局搜索才可稳定回 Idle；
4. 预览所有九条资产，确认 Loop -> Stop 接缝、Root Motion、脚和武器均无跳变；
5. 持刀 `AS_UnSh_Walk_Stop` 必须完整播放至其曲线归零，而非由旧速度推导在中途切 Idle。

先完成 §14.10 的 PMM-7.1 并重建/复验资产，以上五项才可重新执行。五项和 PMM-6 完整固定矩阵均通过后，普通移动才可供后续大量 GA 依赖。

### 14.7 三档位生成范围与 Root Motion 硬验收（2026-08-27 更新）

`MHGZPMMExtendedStop` 不再只生成 Run。每次执行会在不改动源动画、PSS、PSD 或
AnimGraph 的前提下，生成以下九条 `UAnimSequence`：每个速度族两条
`ExtendedStop`，以及一条各自 Start 来源的 `FirstStepCommitStop`。

| Family | Loop 来源 | Stop 来源 | 输出 |
|---|---|---|---|
| Walk | `AS_Shth_Walk_Loop` | `AS_Shth_Walk_Stop_Left/Right` | `AS_Shth_Walk_Stop_Left/Right_Extended`；`AS_Shth_Walk_FirstStepCommitStop` |
| Run | `AS_Shth_Run_Loop` | `AS_Shth_Run_Stop_Left/Right` | `AS_Shth_Run_Stop_Left/Right_Extended`；`AS_Shth_Run_FirstStepCommitStop` |
| Sprint | `AS_Shth_Sprint_Loop_125x` | 临时复用 `AS_Shth_Run_Stop_Left/Right` | `AS_Shth_Sprint_Stop_Left/Right_Extended`；`AS_Shth_Sprint_FirstStepCommitStop` |

Sprint 没有独立的作者制作 Stop，因此该组的“前置段相位覆盖”是有效的，但真实 Stop
段仍是 Run 的减速动作。这使它成为可测试的临时候选，不得误称为已拥有 Sprint 专用
减速表现；Sprint 必须单独做人工预览和 PIE 验收，未来若获得正式 Sprint Stop，应以
该资产替换此组的 Stop 来源后重新生成。

所有 Extended Stop 都必须满足下列 Root Motion 合同：

1. `Enable Root Motion=true`；
2. `Force Root Lock=false`。这是相对于旧源资产的有意例外，避免新烘焙资产在预览或
   非 Montage 普通移动中退化成只有姿势、没有可观察根位移的序列；
3. 命令行使用 `ExtractRootMotion(0, PlayLength, false)` 计算总平移，必须大于 `1 cm`；
   否则命令失败且不应接入 PSD；
4. CSV 必须记录 `ExtractedRootMotionCm`、`EnableRootMotion`、`ForceRootLock`，以及
   既有的接缝 Root step。接缝为零并不表示整段没有位移：它只表示 Loop 到真实 Stop
   的那一帧没有源坐标系重置造成的瞬移。

生成器还必须把 Skeleton 的 Root bone 作为输出动画的**第一个骨骼轨道**。这不是排序
偏好：UE 压缩动画的 `ExtractRootMotion` 会以第 0 个压缩轨道检查 Root；若把所有骨骼
按字母排序而使 Hips 等骨骼排在 Root 前，即使原始 Root keys 已被复制，运行时仍会回退
到参考姿势并表现为“没有实际位移”。其余轨道才按名称稳定排序。

重建已有的九条生成资产时使用：

```text
UnrealEditor-Cmd MHGZ.uproject -run=MHGZPMMExtendedStop -ReplaceGenerated
```

`-ReplaceGenerated` 仅允许覆盖 `Generated/` 下由此工具命名的九条 Extended Stop /
FirstStepCommitStop，绝不
覆盖任何源动画。每个 Family 都应先在动画编辑器中检查：前置段持续前进、通过
Loop -> Stop 接缝后仍持续按原 Stop 减速，并且角色胶囊/Root Motion 预览没有停在原地。
随后必须按 14.5 依次执行 `MHGZPMMAssetFixup` 和 `MHGZPMM7AssetFixup`：前者重建正式源资产的
语义曲线与 Notify，后者写入步态频道、更新 PSD 成员并重建索引。生成器本身不得绕过这些步骤直接
修改数据库。

### 14.8 FirstStepCommitStop：松杆仍必须迈出第一步

Extended Stop 的前缀只来自 Loop，不能承担当前姿势仍在 Start **第一次落脚之前**时的松杆。
本 demo 的交互要求是“第一步必须迈出”，因此不能在这段直接回 Idle，也不做第一步之前的
提前刹停。生成器改为在第一次落脚后拼接真实 Stop：

```text
AS_Shth_<Family>_FirstStepCommitStop
    = AS_Shth_<Family>_Start[0 -> sFirst] + StopFirst[0 -> End]
```

生成器分别扫描 Start -> StopLeft、Start -> StopRight 的非 Root 局部 Pose、局部速度与旋转
接缝成本，取得到的两个**第一个局部最小值中时间更早者**。它就是 `sFirst`，即第一步完成的
可用落脚；与其匹配的 Stop 作为 `StopFirst`。前缀至少保留 3 帧。Walk 保持当前早期接缝搜索：
Walk Start + Walk Stop。Run 与 Sprint 必须先完整通过第一步：两者均从各自 Start 的**第 20 帧
（含）**开始寻找接缝，分别生成 Run Start + Run Stop 与 Sprint Start + Run Stop；Sprint 复用
Run Stop 源，但不复用 Run 的合并资产。

`FirstStepCommitStop` 是完整独立 `UAnimSequence`，不是 Composite：

- Start 前缀：`MM_Intent=-1`、`MM_DistanceToStop=0`；
- 真实 Stop：复制 Stop 的 `MM_Intent` 与 `MM_DistanceToStop` 曲线；
- `PoseSearchControl`、Root 重基、Root 第一轨道、`Enable Root Motion=true`、
  `Force Root Lock=false` 与 Extended Stop 完全相同；
- 与 Extended Stop 一样，必须先通过生成器 Root Motion 审计，再由 `MHGZPMM7AssetFixup` 统一接入 PSD。

PMM-7 的最终输入下降沿合同更新为：

```text
松杆前实际选中 Start，且当前选中时间位于 sFirst 之前
    -> MM_Intent=-1，MMDistanceToStop=0
    -> PSS 以当前 Start Pose 命中 FirstStepCommitStop 的对应前缀时刻
    -> 无论松杆发生在第几帧，序列继续到 sFirst，因此第一步一定迈出

松杆前实际选中 Loop / 第一次落脚之后的 loop-like Start 尾段
    -> 同一 -1/0 查询；PSS 直接命中 Extended Stop
```

这里的 “loop-like” 只表示 Pose Search 可以将其作为候选重选区间，**不表示 Start 后半段与
Loop 是同一段动画或 Root 轨迹**。接入 PSD 前必须预览 Start 尾段 -> ExtendedStop：若仍有无法
命中的姿势空洞，才增加受限候选；不重新采用全 Start 的长前缀拼接。

这里的“实际选中 Start”和累计 Root Motion 必须由 14.4 所要求的每帧 Motion Matching
结果队列得出，不能继续用 `bStartQueryActive` 或单帧 `ActualSpeed2D` 猜测。它们只表示
查询曾经提交过 `+1`，不能可靠表示当前播放资产和已走过的起步距离。代码仍只提交查询，
不得指定 Left/Right 或直接播放任何 FirstStepCommitStop。

### 14.9 收刀步态语义与快速 Sprint（2026-08-27）

最新 CSV 证明 `DesiredSpeed` 不是 Run -> Sprint 迟迟不切换的原因：RB 成立后
`TargetCruiseSpeed` 已立即为 `575`，`DesiredSpeed` 也在约 `0.1s` 内达到 `575`，但 PSS 仍可继续
选择 `AS_Shth_Run_Loop`。根因是原 PSS 只有轨迹和 Start/Stop 语义，没有“当前移动目标属于
Walk、Run 还是 Sprint”的正式成本维度；同一姿势相近、但速度族不同的候选可以长期被 Continuing
Pose 和 Pose 成本保留。

因此正式 Schema 现为六个频道，而不是旧文档前文所述的五个：

| 频道 | 权重 | 资产曲线 | 运行时查询 |
|---|---:|---|---|
| Pose | 2 | 姿势/速度/相位 | Continuing Pose |
| Trajectory | 6 | 完整 Position | `MMPredictedTrajectory` |
| `MM_Intent` | 10 | `MM_Intent` | Start / Cruise / Stop 语义 |
| `MM_DistanceToStop` | 4 | `MM_DistanceToStop` | 已选 Stop 的距离进度 |
| `MM_StopGait` | 64 | `MM_StopGait` | 松杆边沿锁存的 Stop Family；作为离散族约束，必须压过姿势相近但族错误的 Stop |
| `MM_MoveGait` | 8 | `MM_MoveGait` | 当前有输入时的目标移动 Family |

`MM_MoveGait` 的值域固定：Walk=`1/3`、Run=`2/3`、Sprint=`1`。收刀 Start/Loop 的曲线整段保持
所属值；Idle 与所有 Stop 为 `0`。持刀只有单一正式移动族，持刀 Start/Loop 使用 `2/3`。运行时
只要 `bHasInput=true`，就由当前帧 `TargetCruiseSpeed` 生成该查询；没有输入时为 `0`。这不是代码
直接播放动画：PSS 仍通过姿势、轨迹和相位选择具体资产及时间，但 Run 输入不再允许 Sprint Start
或 Walk Loop 作为长期等价候选，按住 RB 后也不再被 Run Loop 持续占用。

`MM_StopGait` 与 `MM_MoveGait` 必须分离。Stop 的 lane 在输入下降沿锁存，生成 Stop 的
`MM_StopGait` 从第 0 帧到最后可索引帧保持常量，`FollowingExtendedStopCurve` 也只输出该锁存值。
不得再从“刚刚错误选中的 Stop”采样其 lane，也不得把 lane 线性衰减到零；否则一次错误的 Walk
Stop 会在下一帧自我确认。当前值域相邻档位只相差 `1/3`，因此 StopGait 权重为 `64`；`8` 已由 CSV
证实会让 `MMStopGaitQuery=Run` 时仍被 Pose/Trajectory 更近的 Walk Stop 抢走。Stop 的结束仍唯一由
`MM_Intent` 和 `MM_DistanceToStop` 到零决定；非 Continuing 的 Stop 重搜仅作为防回环保护，
不会使用定时窗口提前截断已接受的 Stop。

为避免手柄刚越过死区时的单帧 Walk Start 随即跳到 Run Start，新增
`MMStartInputSettleDuration=1/30s`。这段时间仅让 AnimGraph 维持 Idle，不清位移测量、不增加
`MMStartQueryElapsed`；随后才用已稳定的目标档位提交 Start 查询。它不是 GA 后输入死区：动作
释放后的 `MMForceIdleReleaseHoldDuration` 仍为 `0.05s`，且只吸收 Montage 的最后一个 Root Motion
采样。

RB 在**已经有 locomotion 输入**时不再等待 `SprintHoldThreshold`，当帧成为 Sprint 请求；静止时
仍保留原有的短按住判定。由于 Sprint Start 和 Run Start 当前 Root 位移相同，暂不生成
`Sprint_Start_1.25x`：从静止按 RB 仍先由 Sprint Start 的前段承接，开始移动后由
`MM_MoveGait` 立即收敛到 `Sprint_Loop_125x`。只有在该链路稳定后、且 PIE 仍证明起步位移不足时，
才单独烘焙 Sprint Start 并重新标定其曲线。

自动化落盘和验收顺序：

```text
UnrealEditor-Cmd MHGZ.uproject -run=MHGZPMMAssetFixup
UnrealEditor-Cmd MHGZ.uproject -run=MHGZPMM7AssetFixup
UnrealEditor-Cmd MHGZ.uproject -ExecCmds="Automation RunTests MHGZ.PMM; Quit"
```

第一条负责正式 Idle/Start/Loop/原始 Stop 的 `MM_Intent`、距离曲线与 PoseSearchControl Notify；第二条才负责 `MM_StopGait` / `MM_MoveGait`、候选集合和两个 Pose Search 索引。两者不能互相替代：只运行 PMM-7 Fixup 不会更新 Start 曲线，索引也不能包含尚未写入资产的新语义。

PIE 验收必须至少确认：满推 Run 松杆十次不再选 Walk Stop；Run 中按住 RB 在一个 PSS 搜索周期内
切到 Sprint Loop；起步不再出现 Walk Start 后立即跳 Run Start；攻击蒙太奇结束后，CSV 中
`ExternalForceMMIdle=0` 后额外 bypass 仅为 `0.05s` 加上最多 `1/30s` 的新起步稳定期。

### 14.10 PMM-7.1：Stop 候选生命周期收束（当前阶段）

> **状态：设计已冻结，尚未实施。** 本节是 PMM-7 的唯一后续工作。完成前，PMM-6 不得签收，M4-A 不得标记为最终完成，M4-B.0、M4-B.1、M5 和相关编辑器批量接线均不得开始。阶段状态以 [阶段门禁](milestone-gates.md) 为唯一真相源。

#### 14.10.1 已复现事实，而非待猜测假设

最近一次可解析的 Pose Search Telemetry：

```text
Saved/RuntimeTelemetry/20260828-130012-BP_IG_Character_C_0-53227/
```

收刀 `Run_Stop_Right_Extended` 的关键帧如下：

```text
Frame 10484：Run_Stop_Right_Extended @ 1.2667s，IsContinuing=1
Frame 10485：当前动画在 PSD 中已无可索引 Pose，IsContinuing=0，发生全局搜索
             Query: MM_Intent≈-0.056, MM_DistanceToStop≈-0.054,
                    MM_StopGait=Run(2/3), MM_MoveGait=0
             Run_Stop_Right @ 1.2667s 的全局候选成本仅 0.004764，
             但被 PoseReselectHistory + BlockTransition 正确丢弃；
             随后进入另一条边界未完整覆盖的 Run Stop。
```

因此根因不是“`MM_Intent` 权重不够高”、不是攻击 GA 后的 `BlockMovement`，也不是 `BlockTransition` 错误阻断 Continuing。UE 的顺序是：

```text
用当前实际播放资产和实际播放时间重建 PoseIdx
    -> PoseIdx 有效：才调用 SearchContinuingPose，并以当前 PoseIdx 计算 Continuing 成本
    -> PoseIdx 无效：bCanAdvance=false，根本不生成 Continuing 候选，只能全局搜索

全局搜索时：
    BlockTransition 只禁止“新进入”被标记帧；
    它不禁止已经在同一资产上推进的 Continuing。
```

`ExcludeFromDatabaseParameters=[0,-0.05]` 的含义是“数据库只索引到每条动画结束前 0.05 秒”。它不是只排除全局候选，而是会令尾部根本不存在可供 Continuing 重建的 `PoseIdx`。该 PMM-4 通用策略被 PMM-7 直接继承，正好与 Extended Stop 需要在曲线归零后继续推进的合同冲突。

#### 14.10.2 机制职责：保留、替换与禁止误用

| 机制 | 当前正确职责 | PMM-7.1 决策 |
|---|---|---|
| `bForceMMIdle` / `MMForceIdleReleaseHoldDuration` | GA/Montage 与普通移动的 Root Motion 所有权边界；吸收最后一次动作 Root Motion 采样 | 保留；它不负责普通 Stop 候选重选。 |
| `BlockTransition` | 禁止全局搜索从 Start/真实 Stop/Stop 尾段重新进入 | 保留，并改为按 PSS 索引帧覆盖。不可删除。 |
| `SetDisableReselection(true)` | 禁止同一源动画跳回较早帧 | 保留；它不能防止 Right Stop 跳到 Left Stop。 |
| Continuing Cost Bias | 已选中的当前 Pose 与全局候选竞争时的稳定性偏置 | 保留；它只有在当前时间仍有有效 PoseIdx 时才有意义。 |
| `MM_StopGait` | 输入下降沿锁存 Stop Family，防止 Run/Sprint 被 Walk Stop 抢走 | 保留为离散约束；不得线性衰减。 |
| `FollowingExtendedStopCurve` | 已接受 Stop 后从该资产采样语义曲线，而非由速度反推 | 保留；正常结束必须发生在 Continuing 仍有效时。 |
| 数据库级 `[0,-0.05]` 尾裁剪 | PMM-4 的通用尾部保护 | 停用为全库规则；改为逐候选采样范围策略。 |
| `0.001s` Notify/曲线边界 | 旧版防边界漏选尝试 | 删除；全部改为 60Hz 索引帧边界。 |
| “非 Continuing 后清除 StopMode” | 防止意外 Stop 回跳再次把 `-1` 写入查询 | 仅保留为异常防御；不得承担正常结束。 |

#### 14.10.3 冻结后的资产与索引合同

设 PSS 采样率为 `S`，当前为 `60Hz`，采样步长为 `Δ=1/S`。生成器、Fixup 和审计一律以整数索引帧计算时间；不得再用 `0.001s`、`0.01s` 容差或“接近动画末尾”表达生命周期边界。

对每条 Generated Extended Stop / FirstStepCommitStop，定义：

```text
Prefix：可由全局搜索进入的 Loop / Start 前置段
CommitIndex：真实 Stop 的第一个 PSS 索引帧
ZeroIndex：MM_Intent 与 MM_DistanceToStop 首次同时为 0 的索引帧
LastIndex：该条资产在 PSD 中最后一个有效 PoseSearch 索引帧
```

必须同时满足：

1. `Prefix` 的最后一个可进入样本严格早于 `CommitIndex`；`CommitIndex` 及之后的每个有效索引帧均处于 `BlockTransition` 内。
2. Continuing Bias 覆盖从资产开头至 `LastIndex`；`BlockTransition` 覆盖 `[CommitIndex, LastIndex]`，包括曲线已归零的尾段。
3. `ZeroIndex <= LastIndex - 1`：曲线第一次归零后，至少还存在一个后继有效索引帧。曲线从 `ZeroIndex` 到动画播放末尾保持 0，不能只在实际 `PlayLength` 最后一键归零。
4. `MM_StopGait` 在资产曲线上保持所属 Family 常量直至 `LastIndex`；但运行时在 `ZeroIndex` 消费到两个 0 后立即清除 StopMode，下一帧输出的查询 StopGait 必须为 0。
5. `Database.ExcludeFromDatabaseParameters` 不再统一将所有候选裁掉尾部。Fixup 必须将其恢复为全范围，并为确实需要裁尾的**普通非生成候选**逐条设置 `FPoseSearchDatabaseSequence::SamplingRange`；Generated Stop 必须保留满足第 3 条的完整索引尾部。

这不是状态机：运行时代码仍只提交“松杆请求”和已接受候选的曲线值，PSS 仍依 Pose、Trajectory、Phase 与频道成本决定 Left/Right 和具体时间。代码不允许直接选择某条 Stop。

#### 14.10.4 实施范围与顺序

1. 修改 `MHGZPMMExtendedStopCommandlet`：从 PSS/Schema 读取采样率；所有 Prefix、Block、Continuing 和曲线末尾键由索引帧换算。删除 `EndPadding=0.001f`、`StopBlockCommitLead=0.001f` 一类时间魔数。
2. 修改 `MHGZPMM7AssetFixupCommandlet`：取消数据库级 `ExcludeFromDatabaseParameters=[0,-0.05]`；配置每个 `FPoseSearchDatabaseSequence` 的明确 SamplingRange。Generated Stop 的有效范围必须包含 `LastIndex` 所需尾段。
3. 修改 `MHGZPMMAssetFixupCommandlet`：普通 Start/旧 Stop 的尾部裁剪也改为逐候选、可审计的 SamplingRange，不能重新覆盖 Generated Stop 的合同。
4. 修改生成器/资产审计：以实际构建的 SearchIndex/PoseMetadata 或等价 `GetPoseIndex` 查询验证每一个索引帧，而不是只检查 Notify 的可视时间。审计至少验证“真实 Stop 无全局入口”“ZeroIndex 后仍有 Continuing 索引”“最后有效索引前曲线已归零”。
5. 仅在上述资产合同通过后，审查 `UMHGZMotionMatchingAnimInstance` 的 StopMode 清理：正常路径只在 Continuing 中读到两个 0 时退出；非 Continuing 分支只记录防回环事件并清空下一帧查询，不得强制截断动画或新增定时 Idle。
6. 重建顺序固定为：

```text
UnrealEditor-Cmd MHGZ.uproject -run=MHGZPMMExtendedStop -ReplaceGenerated
UnrealEditor-Cmd MHGZ.uproject -run=MHGZPMMAssetFixup
UnrealEditor-Cmd MHGZ.uproject -run=MHGZPMM7AssetFixup
UnrealEditor-Cmd MHGZ.uproject -ExecCmds="Automation RunTests MHGZ.PMM; Quit"
```

任何一项失败均不得进入 PIE 调权重。`MM_Intent`、Pose、Trajectory、StopGait 的权重不是本阶段的修复手段。

#### 14.10.5 PMM-7.1 退出条件

代码、资产和 PIE 必须全部满足：

1. 收刀 Walk/Run/Sprint 从多个 Loop 相位松杆各至少 10 次；每次只出现本次输入锁存 Family 的一个 Stop，不出现 Stop → Stop，也不出现 Run/Sprint → Walk Stop。
2. Telemetry 显示每次正常 Stop 的 `MM_Intent` 与 `MM_DistanceToStop` 归零时，当前选择仍为同一资产、`IsContinuing=1`；之后若发生全局搜索，`MM_StopGait=0`，不能重新选择任何 Stop 前置段。
3. `BlockTransition` 不再产生真实 Stop 首帧漏网候选；它仍不会阻断正常 Continuing。
4. 持刀旧 `AS_UnSh_Walk_Stop` 完整播放；攻击、翻滚、收拔刀退出不因 Montage 尾帧伪造 Stop。
5. 重跑 PMM-3、PMM-4 和 PMM-6 固定矩阵全绿，且最新记录可由 Telemetry/PoseSearch 导出工具复核。

满足后才将 PMM-7.1 标记为已完成、解除 G-003，并回到 M4-A 最终 PIE/阶段验证。
