# 纯 Motion Matching 普通移动实施指南

> **状态：2026-08-26：PMM-1 的 C++ 查询生产者与自动化测试已实现并通过 Development Editor 编译。** 新 `AnimInstance` 尚未被 `ABP_MH_Character` 继承，因此尚未改变 PIE 中的普通移动；下一步仍是 PMM-2 的编辑器接线。本状态仅说明技术实现进度，不表示 E4-A 等阶段门禁已自动通过。本文以当前 `MHGZCharacter`、`ABP_MH_Character`、`PSS_MH_Move`、两套 PSD 和现有动画资产为依据，给出从当前半完成状态继续实施的唯一顺序。本文采用“普通移动继续由动画 Root Motion 驱动”的纯 Motion Matching 路线，不执行 `locomotion-refactor*.md` 中的 CMC + 状态机备选方案，也不能把两套路线混装。
>
> **本文中的“纯 MM”定义：** Idle、Start、Loop、Stop 全部留在同一个 Pose Search 候选池，由加权成本决定结果；允许查询侧缓存输入意图和速度，但不建立 Idle/Starting/Looping/Stopping 动画状态机，不由代码直接指定要播放哪条动画。
>
> **本轮已完成 PMM-1 的代码部分，尚未执行 PMM-2～PMM-6。** 后续必须依次完成 PMM-0～PMM-6；任何一步未通过，不得跳到后面的权重微调或批量增加动作资产。

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
- `Search Throttle Time=0`
- `Play Rate=[1,1]`
- 收刀/持刀 Bool 分支当前 Blend Time 为 `0.1`

旧 `setTrajectory` 仍按 `DesiredSpeed × Time` 生成近似直线轨迹。这个查询在按下摇杆后很快变成匀速直线，在松开摇杆后又只按指数速度衰减，所以 Start/Stop 的真实加减速形状在成本中不占优势。

### 2.3 当前 PSS

`PSS_MH_Move` 当前为 60 Hz、Normalize，包含四个频道：

| 频道 | 当前内容 |
|---|---|
| Pose | 权重 2；2 个骨骼；仅 Position |
| Trajectory | 权重 7；时间点 0.2、0.5、0.8、1.0；仅 Position |
| `BPSC_MMIntent` | 权重 1；资产曲线名 `MM_Intent` |
| `BPSC_MMDistanceToStop` | 权重 1；资产曲线名 `MM_DistanceToStop` |

当前没有负时间轨迹样本。继续保持这一点；本项目之前已经验证，直接加入负 Offset 会导致循环动画在某只脚附近反复重选并抽搐。

### 2.4 两个 Curve Channel 当前并未接通

两个 Blueprint Curve Channel 已经存在，但资产依赖中只有 `/Script/PoseSearch`，`Get World Curve` 没有读取 `ABP_MH_Character` 或 MHGZ C++ 类型。以当前 AnimBP 类默认对象调用，两者都返回 `0.0`。

因此现在的真实状态是：

- 动画资产中的 `MM_Intent` / `MM_DistanceToStop` 已经参与数据库索引；
- 运行时查询侧始终给这两个维度输入 0；
- Start 的 `+1`、Stop 的 `-1` 因而会被额外惩罚，而不是被输入意图选中；
- 原回答中“只读 AnimBP 缓存 float”并没有在资产中真正实现。

### 2.5 当前数据库和动画

`PSD_MH_Shth_Move` 当前引用：

- `AS_Shth_Idle`
- Walk Start/Loop/Stop Right
- Run Start/Loop/Stop Left/Stop Right
- Sprint Start/Loop 125x

文件已存在但当前数据库依赖中缺少 `AS_Shth_Walk_Stop_Left`，后续必须补入。

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
  └─ ActorForwardVector（唯一移动方向）
        ↓
UMHGZMotionMatchingAnimInstance::NativeUpdateAnimation
  ├─ MMActualSpeed2D
  ├─ MMIntentQuery：Start=+1，Cruise/Idle=0，Stop=-1
  ├─ MMDistanceToStopQuery：Stop 时为负的预计剩余距离，其余为 0
  └─ MMPredictedTrajectory：按加速/减速积分得到 0.2/0.5/0.8/1.0 s 位置
        ↓
PSS_MH_Move
  ├─ 双脚 Pose/Velocity/Phase
  ├─ 只含 PositionXY 的前向 Trajectory
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
| `bStartQueryActive` | `bool` | 非 UPROPERTY 私有字段 | false | 仅表示 Start 查询语义是否仍在衰减；不指定动画 |
| `bHadMoveInput` | `bool` | 非 UPROPERTY 私有字段 | false | 只用于识别 `false→true` 的移动输入边沿 |
| `bWasForceMMIdle` | `bool` | 非 UPROPERTY 私有字段 | false | 记录上一帧是否由动作系统压住 MM；解除压制后的第一帧仍重置位移历史 |
| `MMStartQueryElapsed` | `float` | 非 UPROPERTY 私有字段 | 0 | 当前 Start 查询语义已经经过的秒数 |
| `MMStartQueryDuration` | `float` | 非 UPROPERTY 私有字段 | 0 | 本次 Start 查询衰减时长；Walk=1.25、Run/Sprint=0.80、持刀=0.60 |
| `CachedCharacter` | `TWeakObjectPtr<AMHGZCharacter>` | 非 UPROPERTY 私有字段 | null | `NativeInitializeAnimation` 缓存的拥有者 |

不要再创建含义不明的 `ActualSpeed2D`、`MMIntentQuery` 等散落 Blueprint 变量。表中前五个字段是 AnimBP 和 Curve Channel 的唯一查询真相源；后面的调参值全部在 `ABP_MH_Character` 的 Class Defaults 中可见。

## 5. PMM-0：建立可回退基线

### 5.1 操作

1. 保存当前所有 Content 资产并关闭 PIE。
2. 用 Git 提交当前可运行基线。项目已经有版本控制，不需要再复制一套 PSS/PSD 到其他目录。
3. 记录以下当前值：四个巡航速度、两个 PSD 的动画清单、PSS 四个频道、两个 MM 节点设置。
4. PIE 各录一次：Idle→Walk、Idle→Run、Idle→Sprint、移动→松摇杆、第一次拔刀、收刀后再拔刀。
5. 保留 Pose Search Debugger 截图或录屏，作为 PMM-6 前后对比。

### 5.2 退出条件

- 当前 Git 提交可独立恢复。
- 已有至少一段能复现“起步/停步被跳过或混播”的录屏。
- 不创建 `_Copy`、`_Backup` 或第二套并行运行 PSD。

## 6. PMM-1：代码侧建立查询生产者

> **实施状态（2026-08-26）：已完成。** 已新增 `UMHGZMotionMatchingAnimInstance`、纯数学辅助函数和 `MHGZ.PMM.Query.*` 自动化测试；尚未 Reparent 任何 AnimBP，也没有修改 PSS、PSD 或动画资产。

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

CurrentForceMMIdle = Character.bForceMMIdle

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

必须使用 Actor 实际位移，不使用 `CharacterMovement.Velocity.Size2D()`：当前普通移动由动画 Root Motion 驱动，Actor 位移才是最终事实。这里同时检查“本帧被压制”和“上一帧被压制”：Root Motion 常在动画更新之后才应用到 Actor，只在 `bForceMMIdle=true` 时重置仍可能把 Montage 的最后一帧位移带到解除压制后的第一帧。多跳过这一帧测量才能可靠消除第一次拔刀误触发 Stop。

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
  MMStartQueryElapsed = 0
  MMStartQueryDuration = 当前档位对应的 1.25 / 0.80 / 0.60

若 !bHasInput：
  bStartQueryActive = false
  MMStartQueryElapsed = 0

若 bStartQueryActive：
  MMIntentQuery = 1 - Clamp(MMStartQueryElapsed / Max(MMStartQueryDuration, 0.01), 0, 1)
  MMStartQueryElapsed += DeltaSeconds
  当 MMStartQueryElapsed >= MMStartQueryDuration 时 bStartQueryActive=false
否则若 !bHasInput && MMActualSpeed2D > MMIdleSpeedThreshold：
  Ratio = Clamp(MMActualSpeed2D / Max(MMLastNonZeroCruiseSpeed, 1), 0, 1)
  MMIntentQuery = -Ratio
否则：
  MMIntentQuery = 0

本帧计算结束时：
  bHadMoveInput = bHasInput
```

实现时应保存一个私有 `bHadMoveInput`，只在 `false→true` 的输入边沿且速度低于 `MMStartEligibilitySpeed` 时激活 `bStartQueryActive`。不能每帧用低速重新激活，否则 Stop 尾端突然推摇杆时可能反复重置 Start 查询。Start 查询按时间衰减而不是按实际速度衰减，是为了与当前动画中 `MM_Intent` 的 1.25/0.80/0.60 秒曲线保持同相；否则根位移速度较早升高时，查询会先变成 0，Start 仍在 +1→0 的中段，反而诱发提前跳 Loop。

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
    + Character->GetActorForwardVector() * PredictedDistance(T);
```

不要把摇杆 X/Y、控制器右向量、Facing 预测或角速度写进轨迹。角色方向已经由 `AMHGZCharacter::Tick` 修改 Actor Yaw；这里永远只沿 Actor 当前前方预测。

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

### 8.1 `MM_Intent` 的含义

统一使用大小写完全一致的 `MM_Intent`：

| 资产类型 | 曲线键 |
|---|---|
| Idle | 开头 0，结尾 0 |
| Loop | 开头 0，结尾 0 |
| Start | 开头 +1，在动画进入稳定循环姿势前降到 0 |
| Stop | 开头 -1，结尾 0 |

当前已审计到的 Start 过渡结束初值可以保留：

- Sheathed Walk Start：约 1.25 s 回到 0。
- Sheathed Run/Sprint Start：约 0.80 s 回到 0。
- Unsheathed Start：约 0.60 s 回到 0。

当前部分资产显示为 `mm_intent`，部分为 `MM_Intent`。FName 比较通常不区分大小写，但编辑器审计和批处理容易混乱；若编辑器不允许只改大小写，先改名为 `MM_Intent_Temp`，保存，再改成 `MM_Intent`。Loop 不需要每帧一个 0 键，只保留首尾两个 0 键。

### 8.2 `MM_DistanceToStop`

统一规则：

- Idle、Start、Loop：开头 0、结尾 0。
- Stop：每一帧记录“当前帧到 Stop 结尾还会产生的水平 Root Motion 位移”的负值，最后一帧为 0。

当前 Stop 中已有引擎生成的 `Distance` 曲线，它保留了真实每帧根位移；当前手工 `MM_DistanceToStop` 多数只有首尾两三个线性键，信息不足。对每条 Stop：

1. 打开动画序列底部 Curves 面板。
2. 展开现有 `Distance`，确认开头约等于本资产总停步距离的负值、结尾为 0。
3. 选中 `Distance` 的所有键并复制。
4. 清空 `MM_DistanceToStop` 的旧键。
5. 在 `MM_DistanceToStop` 的 0 秒位置粘贴全部键。
6. 确认键数量与 `Distance` 相同、首尾时间相同。

不要把中间轻微回摆或非单调段手工拉直；那是原动画真实根骨轨迹，Pose 频道会与距离频道共同消除多解。

### 8.3 逐资产审计表

| PSD | 资产 | `MM_Intent` | `MM_DistanceToStop` | 其他要求 |
|---|---|---|---|---|
| Shth | Idle | 0 | 0 | Looping |
| Shth | Walk Start | +1→0 | 0 | 非循环 |
| Shth | Walk Loop | 0 | 0 | Looping |
| Shth | Walk Stop L/R | -1→0 | 复制各自 Distance | 非循环，两条都保留 |
| Shth | Run Start | +1→0 | 0 | 非循环 |
| Shth | Run Loop | 0 | 0 | Looping |
| Shth | Run Stop L/R | -1→0 | 复制各自 Distance | 非循环，两条都保留 |
| Shth | Sprint Start | +1→0 | 0 | 非循环 |
| Shth | Sprint Loop 125x | 0 | 0 | Looping；不用旧 458 cm/s Sprint Loop |
| UnSh | Idle | 0 | 0 | Looping |
| UnSh | Start | +1→0 | 0 | 非循环 |
| UnSh | Loop | 0 | 0 | Looping |
| UnSh | Stop | -1→0 | 复制 Distance | 非循环 |

### 8.4 Start/Stop 的 Pose Search Notify State

对每条 Start 和 Stop 添加独立 Notify Track：`PoseSearchControl`。

添加 `Pose Search: Block Transition In`：

- Start：从约 0.10 s 到动画末尾前 0.05 s。
- Stop：从约 0.12 s 到动画末尾前 0.05 s。

它的含义是“搜索不能直接返回到这个区间，但已经从前 0.10/0.12 s 进入的动画可以自然播放进来”。因此 Idle 查询不能直接跳进 Stop 的尾部，Start/Stop 也不会每帧跳到自己中间的另一帧。

再添加 `Pose Search: Override Continuing Pose Cost Bias`：

- Start：从 0.0 s 到 `MM_Intent` 回到 0 的位置。
- Stop：从 0.0 s 到末尾前约 0.08 s。
- `Modifier` 初值设为 `-0.25`。

负值只降低“继续播放当前候选”的成本，不是锁死动画。输入反向变化后，新的语义与轨迹仍能让其他候选获胜。不要一开始使用默认 `-1.0`；它可能过强，导致松摇杆后 Start 仍拒绝切到 Stop。

只有确实存在 T Pose/坏帧的区间才使用 `Pose Search: Exclude From Database`。Exclude 会让该段完全不能成为结果，不能拿它代替 Block Transition In。

### 8.5 PMM-3 验收

- 所有正式数据库动画都能在 Curves 面板看到两个精确同名 Float Curve。
- 非 Stop 的距离查询恒为 0。
- 每条 Stop 的 `MM_DistanceToStop` 与 `Distance` 键数和形状一致。
- Start/Stop 只能从开头约 0.1 s 进入，但进入后能继续播放中后段。

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
| 0.2 | Position XY | 1 |
| 0.5 | Position XY | 1 |
| 0.8 | Position XY | 1 |
| 1.0 | Position XY | 1 |

- Trajectory Channel 总 Weight 从 7 降到 `4` 作为初值。
- 不添加负 Offset。
- 不添加 Facing Direction、Velocity Direction、Heading 或 Z Position。

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
3. Idle、Walk/Run/Sprint Loop 条目设置 `Disable Reselection=true`，避免同一循环资产中反复跳帧。
4. Start/Stop 条目保持 `Disable Reselection=false`。
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
- `Search Throttle Time=0`

这样可以直接看见算法究竟选了哪一帧，不让混合掩盖错误选择。此阶段出现硬切是预期现象，不作为表现失败。

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

### 11.3 调参顺序

只按以下顺序改，一次只改一组：

1. 查询值错误：修 C++，不改 PSS 权重。
2. 轨迹形状错误：调对应步态 Acceleration/Deceleration。
3. Start/Stop 从中段进入：修 Block Transition In 范围。
4. Start/Stop 被过早打断：把 Continuing Modifier 从 -0.25 逐步调到 -0.35、-0.5。
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
- 不要把动作 Montage 的单帧位移计入普通移动实际速度；`bForceMMIdle` 边沿必须重置历史。
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

只有 PMM-6 全部通过，才把普通移动视为可供后续大量 GA、动作 Exit 和白灯加速表现依赖的稳定基础。
