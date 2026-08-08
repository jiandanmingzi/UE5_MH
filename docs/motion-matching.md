# MHGZ Motion Matching 移动系统设计文档

> **实施状态说明（以源码和当前 AnimBP 资产为准）：** Motion Matching 主链路已经接入；附录脚步 IK 和本文中仍以“修改/实施步骤”描述的扩展内容继续作为详细方案保留。若示例代码与当前源码不同，以 `MHGZCharacter` 为准。

## 当前实现快照

| 项目 | 当前值/状态 |
|------|-------------|
| Pose Search | `PSS_MH_Move`、`PSD_MH_Shth_Move`、`PSD_MH_UnSh_Move` 已存在，AnimBP 使用收刀/拔刀双数据库。 |
| 巡航速度 | `WalkCruise_Sheathed=150`、`RunCruise_Sheathed=500`、`SprintCruise=650`、`RunCruise_Unsheathed=450`。 |
| 速度平滑 | `DesiredSpeedInterpSpeed=20`；`DoMove` 与无输入帧的 `Tick` 使用 `FMath::FInterpTo`。 |
| 转向 | CMC 的 `bOrientRotationToMovement=false`、`RotationRate=0`；Character `Tick` 使用最短角差并把每帧转角限制为 `TurnRate × DeltaTime`，默认 `TurnRate=360°/s`。 |
| CMC | `MaxWalkSpeed=1200`、`JumpZVelocity=500`、`AirControl=0.01`、`BrakingDecelerationFalling=80`、`GravityScale=1`。 |
| 移速倍率 | `MoveSpeedMultiplier` Attribute 已存在，但当前 `CalcCruiseSpeed` 尚未乘该倍率。 |
| 脚步 IK | 尚未实现；附录 A 保留为后续方案。 |

## 1. 概述

当前系统已使用 **UE5 Pose Search（Motion Matching）** 替代旧 BlendSpace + 状态机方案，适用于从怪猎崛起解包获得的**仅向前移动**动画资产。

**核心特性：**

- 角色位移完全由动画根骨骼 Root Motion 驱动，CMC 保持 `MOVE_Walking` 做碰撞+重力+落地检测
- Motion Matching 自动处理 idle → 起步 → 循环 → 停止的全流程无缝过渡，不需要手写状态机
- 摄像头自由旋转，角色朝向独立跟随摇杆方向平滑旋转
- 通过摇杆幅度映射 walk / run / sprint，配合按键冲刺自动选择起步类型和循环速度
- 无转向动画时依靠平滑旋转和脚步 IK 减少滑步感
- 与 GAS 配合：`Combat.State.BlockMovement` Tag 冻结移动；`MoveSpeedMultiplier` 缩放期望速度仍是待接入方案

---

## 2. 与旧移动系统的对应关系

| 当前系统 | Motion Matching 替代 |
|---------|---------------------|
| `ETransitionState` 状态机（Idle/Start/Stop/Moving） | **不需要**——MM 自动在数据库中找到过渡帧 |
| BlendSpace1D（Walk/Run/Sprint 速度轴） | **不需要**——MM 按 DesiredSpeed 匹配最合适的动画帧 |
| `bWalkStart`/`bRunStart`/`bWalkStop`/`bRunStop` bool 分支 | **不需要**——MM 自动处理起步/停步的类型选择 |
| `CurrentTheorySpeed` → BlendSpace | → `DesiredSpeed` → Trajectory → MM 查询 |
| `MaxTheorySpeed` → AnimBP 边沿检测 | → `TargetCruiseSpeed`（摇杆瞬时映射值），AnimBP 仍可读此值判断启停方向 |
| `SteerVelocity()` 等腰三角形法 | → 按 `TurnRate × DeltaTime` 限制最大 Yaw 步长，沿最短角差旋转 Actor |
| `bHasInput` / `InputMagnitude` | **保留**——C++ 仍暴露给 AnimBP 读取 |
| C++ `DoMove` / `Tick` 理论速度更新 | → 简化为只计算 DesiredSpeed + 旋转，不再需要 UpdateTheorySpeed |

---

## 3. 动画资产准备

### 3.1 资产清单

| 动画 | 用途 | 帧数 | 备注 |
|------|------|------|------|
| `AM_UnSh_Idle` | 静止待机 | — | 混合空间 Idle 格点动画，无位移 |
| `AS_UnSh_Walk_Start` | walk 起步 | — | 从静止加速到 walk 巡航 |
| `AS_UnSh_Walk_Loop` | walk 循环 | — | 恒定低速循环 |
| `AS_UnSh_Run_Start` | run 起步 | — | 加速至跑巡航 |
| `AS_UnSh_Run_Loop` | run 循环 | — | 中高速循环 |
| `AS_UnSh_Sprint_Loop` | sprint 循环 | — | 最高速循环，A_Run 副本 × 1.5 速 |
| `AS_UnSh_Walk_Stop` | walk 停步 | — | 低速减速至静止 |
| `AS_UnSh_Run_Stop` | run 停步 | — | 高速减速至静止 |

**注意：** 当前数据库按 `Shth`（收刀）与 `UnSh`（拔刀）区分：`PSD_MH_Shth_Move`、`PSD_MH_UnSh_Move`。两套动画放入**不同的** Pose Search Database（见 §9）。

### 3.2 帧率修正

怪猎崛起原始帧率 30fps，导入 UE5 时为 60fps 采样。当前通过 `Rate Scale` 修正资产播放速度。构建 Pose Search Database 前应将动画统一到正确帧率，避免数据库内的姿态时序与实际播放不一致。

### 3.3 巡航速度基准值

根据动画实际速度测量（**需在编辑器中确认**，以下为基于当前 `WalkSpeed=150` / `RunSpeed=500` 的推测值）：

| 巡航类型 | 世界速度 (cm/s) | 说明 |
|---------|----------------|------|
| `WalkCruise` | ~150 | Walk_Loop 平均速度 |
| `RunCruise` | ~500 | Run_Loop 平均速度 |
| `SprintCruise` | ~650 | Sprint_Loop 平均速度（Run × 1.3） |

> 若使用半速量纲，以上值需除以 2（因 Rate Scale = 2.0）。精确值应在 Animation Sequence 视图中通过 `MotionExtractorModifier` 提取后确认。

### 3.4 Pose Search Database 构建

- **单一数据库**：所有 idle / walk_* / run_* / sprint_* / stop 动画放入**同一个** Pose Search Database
- **不分割数据库**：walk / run / sprint 共享同一个数据库，MM 自动跨速度段匹配

#### Schema 配置

使用 UE5 默认 `PoseSearchDatabaseSequence` Schema，包含关节位置和轨迹通道即可，不需要额外配置曲线通道。DesiredSpeed 通过轨迹采样点的位置间隔自然编码——采样点间距越大，系统能判断出该动画帧匹配的速度越高。

---

## 4. CMC 配置

**不需要 `MOVE_Flying`。** RootMotion 在 `MOVE_Walking` 下完全正常工作——UE5 从 AnimGraph 提取根骨骼位移、应用到 CMC 胶囊体，不受行走模式限制。

`MOVE_Walking` 保留以下关键能力：
- 重力（`GravityScale`）——滞空/下落正常
- 落地检测（`OnLanded()`）——着陆后重置协调器和 Tags
- 地面约束——斜坡、台阶步上、胶囊体推挤
- 空中速度衰减（`BrakingDecelerationFalling`）

```cpp
// MHGZCharacter 构造函数中（只需改两行）
UCharacterMovementComponent* CMC = GetCharacterMovement();
CMC->bOrientRotationToMovement = false;
CMC->bUseControllerDesiredRotation = false;
// MaxWalkSpeed 设到足够大，防止 CMC 钳制 RootMotion 速度
// SprintCruise(650) × MoveSpeedMultiplier(上限 ~1.5) ≈ 975 → 设 1200 有余量
CMC->MaxWalkSpeed = 1200.f;
```

### 4.1 当前 CMC 配置项

| 配置项 | 操作 | 原因 |
|-------|------|------|
| `MovementMode` | **保持 MOVE_Walking** | 重力、落地检测、地面物理全部依赖此模式 |
| `MaxWalkSpeed` | **设 1200** | 大于 RootMotion 最高可能速度，防止 CMC 锳制 |
| `MaxAcceleration` | 保留原值 | 影响 AddMovementInput 但不调它，无实际作用 |
| `BrakingDecelerationWalking` | 保留原值 | 同上 |
| `bOrientRotationToMovement` | `false` | 已有手动旋转 |
| `RotationRate` | `FRotator::ZeroRotator` | 手动旋转不用此值 |
| `JumpZVelocity` | 保留 | 跳跃仍可能用到 |
| `AirControl` | 保留 | 空中摇杆微调 |
| `GravityScale` | 保留 | 滞空/下落/落地检测全依赖 |
| `BrakingDecelerationFalling` | 保留 | 空中水平速度衰减 |

---

## 5. C++ 修改

### 5.1 移除项

- `UpdateTheorySpeed()` — MM 不再需要理论速度插值
- `SteerVelocity()` — 不再操作 CMC Velocity
- `MaxTheorySpeed` / `CurrentTheorySpeed` / `PendingTheorySpeed` — 全部移除
- `AccelRate` / `DecelRate` — 移除
- `InputResponseExponent` — 移除（摇杆到速度的映射简化，见 5.3）
- `WalkSpeed` / `RunSpeed` / `SprintSpeed` — 改为巡航速度常量（`WalkCruise` / `RunCruise` / `SprintCruise`）
- `TurnRate` — 保留（旋转仍需要）

### 5.2 新增项

```cpp
// ── Motion Matching 期望速度 ────────────────────────────────

/** 当前期望速度（平滑插值后的值）——AnimBP 拿此值喂 Trajectory */
UPROPERTY(BlueprintReadOnly, Category="Movement|MM")
float DesiredSpeed = 0.f;

/** 摇杆瞬时目标巡航速度（无平滑）——AnimBP 可读此值做启停方向判断 */
UPROPERTY(BlueprintReadOnly, Category="Movement|MM")
float TargetCruiseSpeed = 0.f;

// ── MM 巡航速度常量 ──────────────────────────────────────────

UPROPERTY(EditDefaultsOnly, Category="Movement|MM")
float WalkCruise_Sheathed = 150.f;

UPROPERTY(EditDefaultsOnly, Category="Movement|MM")
float RunCruise_Sheathed = 500.f;

UPROPERTY(EditDefaultsOnly, Category="Movement|MM")
float SprintCruise = 650.f;

UPROPERTY(EditDefaultsOnly, Category="Movement|MM|Unsheathed")
float RunCruise_Unsheathed = 450.f;

/** 期望速度平滑速率——DesiredSpeed 追踪 TargetCruiseSpeed 的 InterpSpeed */
UPROPERTY(EditDefaultsOnly, Category="Movement|MM")
float DesiredSpeedInterpSpeed = 20.f;

/** 强制 MM 输出 Idle——BlockMovement 时切断 MM，防止 RM 和蒙太奇 RM 叠加 */
UPROPERTY(BlueprintReadOnly, Category="Movement|MM")
bool bForceMMIdle = false;
```

### 5.3 摇杆 → 巡航速度映射（替代原 `CalcMaxSpeed`）

```
摇杆死区 Deadzone = 0.1（可下调，MM 不需起步预判窗口）

TargetCruiseSpeed:
  摇杆 < 0.1   → 0
  0.1 ≤ 摇杆 < 0.5 → Lerp(0, WalkCruise, (Stick - 0.1) / 0.4)
  0.5 ≤ 摇杆 ≤ 0.9 → Lerp(WalkCruise_Sheathed, RunCruise_Sheathed, (Stick - 0.5) / 0.4)
  Sprint 按下 且 摇杆 > 0.9 → SprintCruise
```

> 比原来的 `InputResponseExponent` 幂曲线更简单——MM 自己处理动画过渡，不需要 C++ 控制"手感"曲线。

### 5.4 DoMove 简化

```cpp
void AMHGZCharacter::DoMove(float Right, float Forward)
{
    if (!Controller) return;

    // 1. 世界方向 + LastMovementInputDir（不变）
    const FRotator YawRotation(0, Controller->GetControlRotation().Yaw, 0);
    const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
    const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

    const FVector InputVector = ForwardDirection * Forward + RightDirection * Right;
    if (!InputVector.IsNearlyZero())
        LastMovementInputDir = InputVector.GetSafeNormal();

    // 2. InputMagnitude + bHasInput（不变）
    const float Mag = FMath::Sqrt(Right * Right + Forward * Forward);
    bHasInput = Mag >= MoveDeadzone;
    InputMagnitude = bHasInput ? Mag : 0.f;

    // 3. BlockMovement → 全部归零，强制 MM 切 Idle
    if (ShouldBlockMovement())
    {
        TargetCruiseSpeed = 0.f;
        DesiredSpeed = 0.f;
        bForceMMIdle = true;
        return;
    }
    bForceMMIdle = false;  // 正常移动时 MM 正常输出

    // 4. 计算目标巡航速度
    TargetCruiseSpeed = CalcCruiseSpeed(InputMagnitude);

    // 5. 平滑期望速度
    const float DeltaTime = GetWorld()->GetDeltaSeconds();
    DesiredSpeed = FMath::FInterpTo(DesiredSpeed, TargetCruiseSpeed, DeltaTime, DesiredSpeedInterpSpeed);

    // 6. 记录帧号；旋转统一在 Tick 中按最大角速度执行
    LastTheoryUpdateFrame = GFrameCounter;
}
```

### 5.5 Tick 简化

```cpp
void AMHGZCharacter::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    if (ShouldBlockMovement()) return;

    // 本帧 DoMove 已跑 → 跳过
    const uint64 CurrentFrame = GFrameCounter;
    if (CurrentFrame == LastTheoryUpdateFrame) return;

    // IA 漏帧兜底：期望速度衰减
    TargetCruiseSpeed = 0.f;  // 没输入 → 速度为 0
    DesiredSpeed = FMath::FInterpTo(DesiredSpeed, 0.f, DeltaTime, DesiredSpeedInterpSpeed);

    if (bHasInput)
    {
        const float CurrentYaw = GetActorRotation().Yaw;
        const float TargetYaw = LastMovementInputDir.Rotation().Yaw;
        const float DeltaYaw = FMath::FindDeltaAngleDegrees(CurrentYaw, TargetYaw);
        const float MaxYawStep = FMath::Max(0.f, TurnRate) * DeltaTime;
        const float NewYaw = FMath::UnwindDegrees(
            CurrentYaw + FMath::Clamp(DeltaYaw, -MaxYawStep, MaxYawStep));
        SetActorRotation(FRotator(0.f, NewYaw, 0.f));
    }
}
```

---

## 6. AnimBP 修改

### 6.1 移除项

- **整个状态机**（Idle / Start / Stop / Walk-Run-Sprint 状态 + Transition + BlendSpace）——被 Motion Matching 节点替代
- EventGraph 中所有 `bWalkStart` / `bRunStart` / `bWalkStop` / `bRunStop` 相关逻辑
- `PlayMontage` 过渡动画节点
- `Slot "DefaultSlot"` 节点（如果只为过渡动画而设）
- `CurrentTheorySpeed` 读取 → `BlendSpace` 的链路

### 6.2 EventGraph 新增——手动构造 Trajectory

直接在 AnimBP 中构建 `FTransformTrajectory`，不依赖 C++ 或 `Pose Search Generate Trajectory` 节点。

#### 蓝图实现

在 EventGraph 的 `BlueprintUpdateAnimation` 中：

```
1. 读 C++ 数据：
     DesiredSpeed  = Character→DesiredSpeed
     ForwardVector = Character→GetActorForwardVector()
     ActorLocation = Character→GetActorLocation()
     ActorRotation = Character→GetActorRotation()

2. 创建局部变量：
     Trajectory (类型: FTransformTrajectory)
     Sample    (类型: Transform Trajectory Sample)

3. 生成 5 个采样点（对应 Schema 时刻: 0, 0, 0.35, 0.7, 1.0）：
     对每个时间 Time：
       Sample.Rotation      = ActorRotation
       Sample.Position.X    = ActorLocation.X + ForwardVector.X × DesiredSpeed × Time
       Sample.Position.Y    = ActorLocation.Y + ForwardVector.Y × DesiredSpeed × Time
       Sample.Position.Z    = ActorLocation.Z + ForwardVector.Z × DesiredSpeed × Time
       Sample.Time In Seconds = Time
       Trajectory.Samples.Add(Sample)

4. 连到 Motion Matching 节点的 Trajectory 引脚
```

#### 为什么手动构建

- `FTransformTrajectory` 是 Pose History / Motion Matching 节点需要的原生类型
- 蓝图可以直接创建 `FTransformTrajectory` 和 `Transform Trajectory Sample` 局部变量
- 没有类型转换问题，逻辑透明——5 个采样点沿 Actor 当前朝向直线排列
- `DesiredSpeed = 0` 时所有采样点位置相同 → MM 自动匹配 Stop 帧
- 只有向前动画 → 所有采样点同一朝向，不预测转弯

### 6.3 AnimGraph 新结构

```
[Idle Animation Sequence]          [Motion Matching Node]
  (A_Idle, bLoop=true,                - Database: MM_Database
   bEnableRootMotion=false)           - Trajectory: ← EventGraph
       │                              - Pose History: UE5 自动
       │                              - Blend Time: 0.1s
       │                              - Process Root Motion: ✓
       │                              - Enable Inertialization: ✓
       │                              │
       └──────────┬───────────────────┘
                  │
        [Blend by Bool]
          Bool: bForceMMIdle
          true → Idle Anim（零 RM）
          false → MM 输出（正常 RM）
                  │
                  ▼
        [L_TwoBoneIK]  ← 左脚 IK
                  │
                  ▼
        [R_TwoBoneIK]  ← 右脚 IK
                  │
                  ▼
        [Slot "DefaultSlot"]  ← 攻击/翻滚/收刀拔刀 Montage
                  │
                  ▼
             Output Pose
```

> **`bForceMMIdle = true`** 时（BlockMovement）：MM 输出被切断，Idle 动画零位移往下走 → Slot 层蒙太奇 RM 独占 → 无叠加。**`bForceMMIdle = false`** 时：MM 正常输出，Idle 分支不生效。Idle 动画序列必须关闭 RootMotion（`bEnableRootMotion = false`），只提供静止 Pose。

---

## 7. 旋转系统

### 7.1 核心逻辑

角色 Yaw 由 C++ `Tick` 每帧计算，不经过 AnimBP，不依赖动画根骨骼旋转。实现按最短角差转向，并限制每帧最大转角，避免 180° 瞬转。

```
TargetYaw = LastMovementInputDir.Rotation().Yaw
DeltaYaw = FMath::FindDeltaAngleDegrees(CurrentYaw, TargetYaw)
MaxYawStep = TurnRate * DeltaTime
NewYaw = CurrentYaw + Clamp(DeltaYaw, -MaxYawStep, MaxYawStep)
SetActorRotation(FRotator(0, UnwindDegrees(NewYaw), 0))
```

### 7.2 角速度记录

AnimBP 通过 Actor Yaw 的帧间差计算 `AngularVelocity`（度/秒），填入 Trajectory 的 `FacingRotation` 字段。Motion Matching 会选择与当前旋转趋势最匹配的动画帧。

### 7.3 原地转向

摇杆 = 0 时 `DoMove` 不更新朝向。如需原地转身（如视觉聚焦怪物），由其他系统（右摇杆/锁定系统）控制，不在本移动系统处理。

### 7.4 转向滑步处理

由于所有动画都是向前位移，转向时可能脚底滑动。缓解措施（按优先级排序）：

1. **限制最大转速**：当前 `TurnRate=360°/s`，可继续按动画观感调低
2. **惯性化混合**：MM 节点 Blend Time = 0.1s + 开启 Inertialization
3. **脚步 IK**：完整方案见 [附录 A](#附录-a脚步-ik-系统低优先级滑步严重时再实施)（低优先级，滑步严重时再实施）

## 8. GAS 集成

### 8.1 BlockMovement Tag

当前 C++ 代码已有 `ShouldBlockMovement()`，保持不变：

```
攻击 GA / 翻滚 GA / 受击 GA
  → AddLooseGameplayTag(Combat.State.BlockMovement)
  → DoMove 检测到 → DesiredSpeed = 0, bForceMMIdle = true
  → AnimGraph: Blend by Bool 切到 Idle（零 RM）→ Slot 层蒙太奇 RM 独占位移
  → Ability 结束 → RemoveLooseGameplayTag → DesiredSpeed 恢复，bForceMMIdle = false
```

### 8.2 MoveSpeedMultiplier

GAS `MoveSpeedMultiplier` 属性已存在，但以下消费方式尚未接入：

- 在 `CalcCruiseSpeed` 中乘以 `Multiplier`
- 例如减速 debuff 30% → Multiplier = 0.7 → WalkCruise = 150×0.7 = 105

### 8.3 冲刺

- `SprintAction` 绑定不变；拔刀态已有 `SprintPressed` 直接 return 逻辑，`CalcCruiseSpeed` 拔刀分支不返回 SprintCruise
- `bSprintHeld = true` 时 `CalcCruiseSpeed` 对摇杆 > 0.9 返回 `SprintCruise`
- 拔刀态禁止冲刺（`Combat.State.Unsheathed` Tag）——当前 `SprintPressed` 逻辑不变

---


## 9. 收刀/拔刀双数据库

### 9.1 问题

收刀态和拔刀态的手部/上身姿态完全不同，且速度体系不同（收刀有 Walk/Run/Sprint 三档，拔刀只有单速 ~450）。若混入同一数据库，MM 会在同速度下选错姿势。

### 9.2 方案：双 Pose Search Database + Tag 驱动切换

| | Database_Unarmed（收刀） | Database_Armed（拔刀） |
|---|---|---|
| 动画 | Idle, Walk_Start, Walk_Loop, Run_Start, Run_Loop, Sprint_Loop, Walk_Stop, Run_Stop | Idle_Weapon, Walk_Start, Walk_Loop, Walk_Stop（或仅单速 Run_Loop/Stop） |
| 巡航速度 | Walk=150, Run=500, Sprint=650 | 单速 ~450 |
| Sprint | 支持 | 不支持（拔刀态禁止奔跑） |

**切换时机**：收刀/拔刀 GA 播放 Montage 期间，Montage 通过 Slot 节点覆盖 MM 输出。Montage 结束后，AnimBP 已切到新数据库，MM 自然接续新数据库的第一匹配帧。动画序列之间的过渡差距由收刀/拔刀 Montage 本身的动作遮盖，无需额外处理。

### 9.3 C++ 巡航速度配置

```cpp
// 收刀态巡航速度
UPROPERTY(EditDefaultsOnly, Category="Movement|MM|Sheathed")
float WalkCruise_Sheathed = 150.f;
UPROPERTY(EditDefaultsOnly, Category="Movement|MM|Sheathed")
float RunCruise_Sheathed = 500.f;
UPROPERTY(EditDefaultsOnly, Category="Movement|MM|Sheathed")
float SprintCruise = 650.f;

// 拔刀态巡航速度（单速，走跑合一）
UPROPERTY(EditDefaultsOnly, Category="Movement|MM|Unsheathed")
float RunCruise_Unsheathed = 450.f;
```

### 9.4 CalcCruiseSpeed 逻辑

```
CalcCruiseSpeed(InputMagnitude):
  bUnsheathed = ASC->HasTag(Combat.State.Unsheathed)

  if bUnsheathed:
    // 拔刀态：单速
    return InputMagnitude >= Deadzone ? RunCruise_Unsheathed : 0.f
  else:
    // 收刀态：摇杆→WalkCruise_Sheathed/RunCruise_Sheathed/SprintCruise
    return 按摇杆幅度映射收刀三档速度
```

### 9.5 AnimBP 数据库切换

```
EventGraph:
  bUnsheathed = ASC->HasTag(Combat.State.Unsheathed)

  MM_Database = bUnsheathed ? Database_Armed : Database_Unarmed
```

AnimGraph 中 Motion Matching 节点的 `Database` 引脚绑定 `MM_Database` 变量。切换瞬间 Montage 仍通过 Slot 输出（遮盖切换），无视觉问题。


---

## 10. 数据流全景

```
摇杆输入
  │
  ▼
DoMove() / Tick()  ← 每帧必跑
  ├─ bHasInput, InputMagnitude, LastMovementInputDir（始终更新）
  ├─ TargetCruiseSpeed = CalcCruiseSpeed(InputMagnitude)（摇杆→巡航速度）
  ├─ DesiredSpeed = FInterpTo(DesiredSpeed, TargetCruiseSpeed, dt)（平滑）
  ├─ Tick: 按 TurnRate×dt 限制最大 Yaw 步长（平滑转向）
  └─ BlockMovement? → DesiredSpeed=0, bForceMMIdle=true
  │
  ▼
AnimBP EventGraph（TG_PostPhysics，读 C++ 数据）
  ├─ DesiredSpeed, ActorLocation, ActorYaw
  ├─ AngularVelocity = 帧间 Yaw 差 / dt
  └─ 填充 Trajectory（未来 1s 的位置、朝向、速度）
  │
  ▼
AnimGraph Motion Matching 节点
  ├─ Trajectory + Pose History → 查询 Pose Search Database
  ├─ 选择最佳匹配帧
  └─ 输出 Pose（含 Root Motion）→ CMC 执行位移
```

---

## 11. 实施步骤

1. **动画准备**：确认所有移动动画的帧率已修正为 60fps，RootMotion 已正确配置
2. **构建 PSD**：分别创建 Database_Unarmed 和 Database_Armed 两个 Pose Search Database
3. **创建 MM AnimBP**：新建或以 `ABP_MH_Character` 为基础，替换 AnimGraph 为 MM 节点
4. **C++ 重构**：按 §5 简化 DoMove/Tick，移除理论速度相关代码
5. **EventGraph 重写**：按 §6.2 实现 Trajectory 填充逻辑
6. **CMC 配置**：`MaxWalkSpeed = 1200`，保持 `MOVE_Walking`
7. **调参**：按 §10（设计文档原文参数调优指南）调整 InterpSpeed/TurnSpeed/死区
8. **GAS 验证**：测试攻击/翻滚/受击期间 BlockMovement 行为
9. **脚步 IK**：添加 Foot Placement IK（可选，先看滑步严不严重）

---

## 12. 参数配置

| 参数 | 位置 | 建议值 | 作用 |
|------|------|--------|------|
| `MoveDeadzone` | C++ | 0.1 | 摇杆死区（比之前 0.2 低，MM 不需预判窗口） |
| `WalkCruise` | C++ | 150 | walk 巡航速度 cm/s |
| `RunCruise` | C++ | 500 | run 巡航速度 cm/s |
| `SprintCruise` | C++ | 650 | 冲刺速度 cm/s |
| `DesiredSpeedInterpSpeed` | C++ | 5.0 | 期望速度平滑速率 |
| `TurnRate` | C++ | 360 | 角色旋转速度 度/秒 |
| Database_Unarmed | AnimBP | — | 收刀态 Pose Search Database |
| Database_Armed | AnimBP | — | 拔刀态 Pose Search Database |
| Blend Time | MM 节点 | 0.1s | 匹配帧间混合时间 |
| Trajectory 时长 | AnimBP | 1.0s | 未来轨迹预测跨度 |
| Trajectory 样本数 | AnimBP | 10 | 间隔 0.1s 的样本点 |


---

## 附录 A：脚步 IK 系统（低优先级，滑步严重时再实施）

> 此系统为可选扩展。优先用 TurnSpeed 调低（~360）+ Blend Time（0.1s）+ Inertialization 消滑步。
> 如果转向滑步仍然明显，再实施以下 IK 方案。

# 脚步 IK 系统——利用解包 IK 骨骼减少转向滑步

### A.1 问题

MM 输出的所有动画都是向前的，无转向动画。角色旋转时脚底会在地面上滑动——因为动画中的脚部位置是局部空间的，Actor 旋转后脚部世界位置也跟着转了，而原本脚踩的地面位置没变。

### A.2 已有资产：解包 IK Target 骨骼

RE Engine FBX 的骨骼中已经包含了 IK 参考数据：

```
Root
├── R_Foot_IK         ← 灰色（无蒙皮权重），IK Target
│   └── R_Foot_IK_end
├── L_Foot_IK         ← 灰色，IK Target
│   └── L_Foot_IK_end
└── ...
```

- **灰色** = 无蒙皮权重，不直接驱动顶点变形，只存 Transform
- **动画中的位置**：每帧跟随脚底在地面的投影位置。脚着地时 Z 接近 0（地面），抬脚时 Z 升高
- **用途**：它们记录了动画师/引擎运行时对"脚应该在哪"的意图，是天然的 IK 目标源

### A.3 方案：IK Target 驱动的 Two Bone IK

利用 UE5 内置的 `Two Bone IK` 节点，将实际脚部骨骼拉到 IK Target 骨骼记录的**上一帧世界位置**，使脚底在转向时粘住地面。

### A.4 AnimBP 实现

**EventGraph —— 记录 IK Target 世界位置**

```
BlueprintUpdateAnimation:
  // 获取 IK Target 骨骼的当前世界位置（本帧动画产出的）
  L_IK_WorldPos = Mesh->GetBoneTransform("L_Foot_IK").GetLocation()  // 世界空间
  R_IK_WorldPos = Mesh->GetBoneTransform("R_Foot_IK").GetLocation()

  // ★ 用上一帧记录的位置作为 IK 目标（滞后一帧=粘地）
  L_IK_Target = L_IK_LastWorldPos   // 上帧存的值，本帧用
  R_IK_Target = R_IK_LastWorldPos

  // ★ 关键判断：脚是否着地？
  //   IK Target Z 接近胶囊体底部（ActorLocation.Z - CapsuleHalfHeight = 脚底基准面）
  //   因为 SkeletalMesh 原点在胶囊体底部，脚底 IK Target Z ≈ 胶囊体底部 Z
  FootBaseZ = ActorLocation.Z - CapsuleHalfHeight  // 胶囊体半高 96cm
  L_Grounded = FMath::Abs(L_IK_WorldPos.Z - FootBaseZ) < GroundThreshold  // 如 10cm
  R_Grounded = FMath::Abs(R_IK_WorldPos.Z - FootBaseZ) < GroundThreshold

  // 脚离地时不锁定（走步时需要自然迈步）
  if L_Grounded:
    // 只往下锁（不往上抬）：防止脚穿透地面，但允许动画抬脚迈步
    L_IK_Target.Z = FMath::Min(L_IK_Target.Z, L_IK_WorldPos.Z)

  if R_Grounded:
    R_IK_Target.Z = FMath::Min(R_IK_Target.Z, R_IK_WorldPos.Z)

  // Alpha = 着地程度，用于 IK 混合权重
  L_IK_Alpha = L_Grounded ? 1.0 : 0.0
  R_IK_Alpha = R_Grounded ? 1.0 : 0.0

  // 记录本帧位置，留给下帧用
  L_IK_LastWorldPos = L_IK_WorldPos
  R_IK_LastWorldPos = R_IK_WorldPos
```

**AnimGraph —— Two Bone IK 节点**

```
Motion Matching 输出
        │
        ▼
┌─────────────────────┐
│ L_TwoBoneIK         │ ← 左脚 IK
│   IKBone: foot_l    │   实际脚骨骼（蒙皮骨骼，在腿部链末端）
│   EffectorLocation: L_IK_Target (世界空间)  ← 来自 EventGraph
│   EffectorRotation: L_Foot_IK 的旋转
│   Alpha: L_IK_Alpha                        ← 着地=1，离地=0
└──────────┬──────────┘
           ▼
┌─────────────────────┐
│ R_TwoBoneIK         │ ← 右脚 IK
│   IKBone: foot_r    │
│   EffectorLocation: R_IK_Target
│   Alpha: R_IK_Alpha
└──────────┬──────────┘
           ▼
┌─────────────────────┐
│ Slot "DefaultSlot"  │
└──────────┬──────────┘
           ▼
       Output Pose
```

### A.5 关键参数

| 参数 | 位置 | 建议值 | 说明 |
|------|------|--------|------|
| `GroundThreshold` | AnimBP 变量 | 10 cm | IK Target Z 与胶囊体底部 Z 的差值绝对值低于此值视为着地 |
| `L_IK_Alpha` / `R_IK_Alpha` | AnimBP 变量 | 0~1 | IK 权重。可直接用 0/1 硬切，或用 `FInterpTo` 做淡入淡出 |
| `IK Blend` 过渡速度 | — | 5.0 | 若不用硬切，`FInterpTo` 的 InterpSpeed |
| EffectorLocation Space | IK 节点属性 | World Space | IK Target 已用世界坐标 |

### A.6 边缘情况处理

| 场景 | 行为 |
|------|------|
| **角色转弯** | 上一帧 IK Target 位置粘在地面 → 脚不被 Actor 旋转拖走 → 自然脚步 |
| **起步/停步（脚切换）** | `L_Grounded` 检测某只脚离地 → `Alpha=0` → IK 释放该脚 → 动画迈步自然 |
| **跳跃/空中** | 双脚 `Z > GroundThreshold` → 两脚 `Alpha=0` → IK 全释放 → 自由下落 |
| **斜面** | IK Target Z 已反映动画中的脚底高度（不是绝对 0），自然适应坡度 |
| **高速冲刺** | 步幅大、频率高 → IK 每帧追着上一帧位置走 → 仍有粘地效果 |
| **角色旋转 + 起步叠加** | 起步时前脚离地 → IK 释放 → 后脚保持粘地 → 视觉效果正确 |

### A.7 为什么用 IK Target 而不是手动射线检测

| 方法 | 优点 | 缺点 |
|------|------|------|
| **IK Target（本方案）** | 动画数据自带，零额外计算；脚底高度精准跟随动画 | 依赖 FBX 骨骼数据完整 |
| 手动射线检测地面 | 通用，不依赖特定骨骼 | 每帧射线检测开销；脚底 Z 和地面的关系需手动调参；坡度/台阶误判 |

### A.8 与 Motion Matching 的关系

此 IK 系统对 MM 完全透明——MM 选出动画帧 → AnimGraph 传递给 IK 节点 → IK 修正后输出。IK 不参与数据库查询决策，只是在输出层做后处理。配合以下消滑措施：

- **TurnSpeed**：540 → 360（转向更柔，减少 IK 需要修正的量）
- **惯性化混合**：MM 节点 Blend Time = 0.1s

---
