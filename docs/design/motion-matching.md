# MHGZ Motion Matching 移动系统设计文档

> **当前执行路由（2026-09-03）：** 本文只保留架构快照与背景说明。普通移动的当前阶段、允许工作和退出条件以 [阶段门禁](milestone-gates.md) 为准；M4.2 普通移动 / Stop、M4.3 输入释放补丁、M4.4 动作退出 / 根运动交接、E4.2 动作退出资产、M4.5 验收与 M4.2.1 Run/Sprint Loop 候选分流均已签收，当前主线是 **M4.6 攻击 Entry Section**。不得从本文旧的 PMM 记录推断当前可开工事项，也不得恢复 CMC/状态机备选路线。

> **实施状态说明（以源码和当前 AnimBP 资产为准）：** Motion Matching 主链路已经接入；附录脚步 IK 和本文中仍以“修改/实施步骤”描述的扩展内容继续作为详细方案保留。若示例代码与当前源码不同，以 `MHGZCharacter` 为准。
>
> **纯 Motion Matching 路线：** 查询、曲线和数据库的已落地基线以源码与历史 Stop 生命周期专项为准；M4.2 → M4.3 → M4.4 → E4.2 → M4.5 → M4.2.1 已完成并签收。`locomotion-refactor*.md` 中的 CMC + 状态机方案保留为历史备选，不再是当前执行路线，也不能与纯 MM Root Motion 路线并行实施。

## 当前实现快照

| 项目 | 当前值/状态 |
|------|-------------|
| Pose Search | `PSS_MH_Move`、`PSD_MH_Shth_Move`、`PSD_MH_UnSh_Move` 已存在，AnimBP 使用收刀/拔刀双数据库。 |
| 巡航速度 | `WalkCruise_Sheathed=160`、`RunCruise_Sheathed=460`、`SprintCruise=575`、`RunCruise_Unsheathed=440`；与当前正式 Loop 根位移速度基本一致。 |
| 速度平滑 | `DesiredSpeedInterpSpeed=20`；`DoMove` 与无输入帧的 `Tick` 使用 `FMath::FInterpTo`。 |
| 转向 | CMC 的 `bOrientRotationToMovement=false`、`RotationRate=0`；Character `Tick` 使用最短角差并把每帧转角限制为 `TurnRate × DeltaTime`，默认 `TurnRate=360°/s`。 |
| CMC | `MaxWalkSpeed=1200`、`JumpZVelocity=500`、`AirControl=0.01`、`BrakingDecelerationFalling=80`、`GravityScale=1`。 |
| 移速倍率 | `MoveSpeedMultiplier` Attribute 已存在，但当前 `CalcCruiseSpeed` 尚未乘该倍率。 |
| 脚步 IK | 尚未实现；附录 A 保留为后续方案。 |

## 1. 概述

当前系统已使用 **UE5 Pose Search（Motion Matching）** 替代旧 BlendSpace + 状态机方案，服务于从《世界》《崛起》《荒野》选取并重定向的移动动画。来源游戏只说明资产出处，不决定项目动作规则。

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

不同来源和导出工具可能产生不同帧率、Root 结构与播放倍率。每个导入动画必须记录 SourceGame、SourceClip、SourceFPS、ImportedFPS、RateScale 和实测根位移速度；不能把《崛起》30fps 假设套用到全部三部曲资产。构建 Pose Search Database 前先统一实际播放时序。

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

/**
 * 强制 MM 输出零 Root Motion 的 Idle Pose。
 * 它由「BlockMovement 或当前 ActionToken 仍拥有 Montage Root Motion」计算，
 * 不能再只由 BlockMovement 推导；MoveExit 需要允许输入转向，但 Montage 仍独占位移。
 */
UPROPERTY(BlueprintReadOnly, Category="Movement|MM")
bool bForceMMIdle = false;
```

`Combat.State.BlockMovement` 与 Montage 根位移所有权是两个不同概念，必须分别维护：

| 状态 | 所有者 | 作用 | 典型阶段 |
|---|---|---|---|
| `Combat.State.BlockMovement` | TagLedger 的精确 `FWeaponOwnedTagToken` | 禁止由摇杆更新巡航速度与 Actor 朝向 | 前向翻滚本体、站立收刀 |
| `MontageRootMotionOwner` | RuntimeHost 的精确 `FWeaponActionToken` | 令 `bForceMMIdle=true`，使 Slot 上的 Montage Root Motion 成为唯一位移来源 | 翻滚本体、翻滚 MoveExit、全部实际含 RM 的收刀段 |

RuntimeHost 需要提供按 ActionToken 获取/释放的根位移所有权（例如 `AcquireMontageRootMotion`、`ReleaseMontageRootMotion`、`IsMontageRootMotionOwned`）。它不是 Loose Tag，旧 Montage、取消回调或其他 Ability 均不得释放不属于自己的所有权。GA 结束、Montage 中断、换装、死亡和 Runtime Shutdown 必须统一回收。

#### 上半身送虫/收虫（M4.1.5）

持刀 `LT+Y` / `LT+B` 的送虫、收虫不是“动作尾段接移动”，而是从第一帧起就允许完整持刀移动。二者的 Montage 必须为 in-place、无 Root Motion，并只播放 `UpperBody_IGAction` Slot；AnimGraph 以 Armed Motion Matching 为 Base Pose，将该 Slot 作为 `Layered Blend per Bone` 的 Blend Pose，从骨盆上方第一根脊椎骨开始混合全部上半身。它们不获取 `MontageRootMotionOwner`，不持有 `BlockMovement`，因此 `bForceMMIdle=false`，下半身的起步、移动、停步和转向始终由正常持刀 MM 输出。该规则只影响视觉分层；GA 在 Montage 中仍以 ActionToken 注册实例，送虫/收虫仅在各自精确 Commit Notify 执行，不能因移动而重读或更改冻结的 Aim 请求。

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

    const bool bBlockMovement = ShouldBlockMovement();
    const bool bMontageOwnsRootMotion = WeaponRuntimeHost
        && WeaponRuntimeHost->IsMontageRootMotionOwned();

    // 3. 两个维度分别计算：有任一个时都不允许 MM 贡献 Root Motion。
    //    MoveExit 会是「false + true」：可读摇杆并转向，但仍由 Montage 位移。
    bForceMMIdle = bBlockMovement || bMontageOwnsRootMotion;

    // 4. BlockMovement 只冻结输入驱动的速度与朝向；它不再是根位移所有权的唯一真相。
    if (bBlockMovement)
    {
        TargetCruiseSpeed = 0.f;
        DesiredSpeed = 0.f;
        return;
    }

    // 5. 计算目标巡航速度
    TargetCruiseSpeed = CalcCruiseSpeed(InputMagnitude);

    // 6. 平滑期望速度
    const float DeltaTime = GetWorld()->GetDeltaSeconds();
    DesiredSpeed = FMath::FInterpTo(DesiredSpeed, TargetCruiseSpeed, DeltaTime, DesiredSpeedInterpSpeed);

    // 7. 记录帧号；旋转统一在 Tick 中按最大角速度执行
    LastTheoryUpdateFrame = GFrameCounter;
}
```

### 5.5 Tick 简化

```cpp
void AMHGZCharacter::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    const bool bBlockMovement = ShouldBlockMovement();
    const bool bMontageOwnsRootMotion = WeaponRuntimeHost
        && WeaponRuntimeHost->IsMontageRootMotionOwned();
    bForceMMIdle = bBlockMovement || bMontageOwnsRootMotion;
    if (bBlockMovement) return;

    // 本帧 DoMove 已跑 → 跳过
    const uint64 CurrentFrame = GFrameCounter;
    if (CurrentFrame == LastTheoryUpdateFrame) return;

    // IA 漏帧兜底：期望速度衰减
    TargetCruiseSpeed = 0.f;  // 没输入 → 速度为 0
    DesiredSpeed = FMath::FInterpTo(DesiredSpeed, 0.f, DeltaTime, DesiredSpeedInterpSpeed);

    if (bHasInput && !WeaponRuntimeHost->IsRotationOwnedByAction())
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

普通 locomotion 的角色 Yaw 由 C++ `Tick` 计算，不经过 AnimBP。执行前必须查询 RuntimeHost：若当前 Action/Movement Token 已拥有旋转，普通 Tick 不调用 `SetActorRotation`，只把输入交给动作的 SteeringPolicy；没有动作旋转所有者时才按最短角差转向。

```
TargetYaw = LastMovementInputDir.Rotation().Yaw
DeltaYaw = FMath::FindDeltaAngleDegrees(CurrentYaw, TargetYaw)
MaxYawStep = TurnRate * DeltaTime
NewYaw = CurrentYaw + Clamp(DeltaYaw, -MaxYawStep, MaxYawStep)
if (!ActionOwnsRotation)
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

### 8.1 动作移动阶段、BlockMovement 与 Montage Root Motion

动作不能再只用“全程 BlockMovement / Ability End 解锁”表达。所有带位移且末段可接移动的动作都按下表分阶段；输入数据始终由 `DoMove` 更新，但只有允许的阶段才会影响角色。

| 阶段 | `BlockMovement` | `MontageRootMotionOwner` | 摇杆效果 | 位移来源 |
|---|---:|---:|---|---|
| `InPlaceLocked` | 有 | 无 | 只记录输入；不转向、不改速度 | 无根位移的动作表现 |
| `LockedRootMotion` | 有 | 有 | 只记录 `LastMovementInputDir`，不转向、不改速度 | Montage RM |
| `SteeringRootMotion` | 无 | 有 | 实时摇杆更新巡航速度与平滑朝向 | Montage RM；MM 零 RM |
| `MotionMatching` | 无 | 无 | 正常 | MM |

#### 拆分 Animation Sequence 的根运动阶段（M4.4 / M5 前置）

UE 的 Montage Section 只负责“从哪里开始、下一段跳到哪里”，**没有**“本 Section 开/关 Root Motion”的独立开关。当前帧是否有 Root Motion 由 Montage 正在播放的 AnimSequence 根骨骼数据及其资产设置决定。因此同一 Montage 可以线性播放 in-place 的 Entry、真实 Root Motion 的 Travel/Core、再播放 in-place 的 Recovery；但不能把一条本身有移动根轨迹的序列仅靠关闭某个 Section 的提取来变成安全的 in-place 片段——那会造成 Mesh 与 Capsule 漂移、回弹或脚底滑动。应使用根轨迹本来静止的序列，或重新处理/裁切为真正的 in-place 资产。

后续攻击的所有权以**实际贡献根位移的时间段**为准，而不是以整个 Montage 或 Section 为准：`AnimNotifyState_ActionRootMotionPhase` 覆盖每段真实 RM 的首帧至末帧，按精确 ActionToken 令 GA 获取/释放 `MontageRootMotionOwner`。典型组合是：`Entry_From_*` 为 `InPlaceLocked`，Travel/Core 的真实根位移段为 `LockedRootMotion` 或 `SteeringRootMotion`，尾部不再有根位移后进入 `MotionMatching`。若该招式仍要锁动作但尾部没有位移，则维持 `InPlaceLocked` 直到 Ability 结束；若应无缝回到移动，先释放动作自己的 `BlockMovement` Token 和 RootMotionOwner，再由 MM 的起步/停步序列接管。

“空中大位移序列看起来很乱、但根骨骼没有给出实际平移”属于 Gameplay 位移，不属于 Montage RM：GA 在相应 Notify/阶段启动有碰撞上限、终止与清理协议的 MovementTask/RootMotionSource；序列只作为表现。不得为让 MM 停止而虚假获取 MontageRootMotionOwner，也不得让 Task、Montage RM 和 MM 同时写角色位移。

**翻滚方向与阶段：** 翻滚的**动画变体**由激活时冻结的姿态和 `InputSnapshot.Direction` 一次选择；退出分支只在允许的前向变体中读取实时输入。收刀或持刀的 `Forward/None` 使用对应前向 Montage：`DodgeCore` 为 `LockedRootMotion`，GA 为自己这一次 `FAnimMontageInstance` 绑定 `OnMontageSectionChanged`，并先固定 `DodgeCore -> IdleExit`；实际进入 `IdleExit` 时才读原始摇杆，持续有输入便立即跳到 `MoveExit`。确认进入 `MoveExit` 后，GA 自己释放本 Action 的 `BlockMovement`，而 `MontageRootMotionOwner` 仍保留到 Montage Root Motion 结束/BlendOut。这样摇杆不会改变翻滚本体方向，却能从移动恢复段开始以 `TurnRate` 平滑转向。持刀的 Left/Right/Back 在激活时选择各自专用 Montage，但 `FDodgeSelection.bAllowMoveExit=false`，完整动作强制 IdleExit 并一直保持 `LockedRootMotion` 到结束；收刀 Left/Right/Back 直接拒绝，不播动画也不扣耐。

| 变体 | 选择依据 | `SteeringRootMotion` | Montage 结束后的移动 |
|---|---|:---:|---|
| 收刀/持刀前向（含 None） | 冻结姿态 + Forward/None | 有 | 前向 Exit 后可无缝交给 MM |
| 持刀左/右/后 | 冻结姿态 + Left/Right/Back | 无 | 先回 Idle；若仍有摇杆，随后由正常 MM 起步动画接管 |
| 收刀左/右/后 | 冻结姿态 + Left/Right/Back | — | Reject |

**移动收刀：** `Idle`/`Walk` 仅由激活快照决定，之后不允许切换 Section。`Idle` 持有 `BlockMovement`；`Walk` 从第一帧起不持有它，因此实时摇杆可转向。两段只要仍含 Root Motion 就都持有 `MontageRootMotionOwner`；动画应只有前向 Root 位移、没有与代码转向竞争的 Root Yaw。转向后的 Root Motion 在 Actor 新朝向上形成平滑弯曲轨迹；这不是瞬时 180° 改轨，若要瞬时大幅重定向，必须另做方向资产或使用经验证的 Motion Warping。

#### 收刀输入互斥（M4.1 必需）

`BlockMovement` 和 `MontageRootMotionOwner` 都**不是输入锁**：前者只冻结移动/转向，后者只禁止 MM 与 Montage 同时贡献根位移。尤其 `Walk` 收刀故意不持有 `BlockMovement`，因此不能把“可转向”误解为“可打断”。

本 Demo 的纳刀资格固定为：`Grounded + Unsheathed`，且当前不处于攻击、硬直、击倒、死亡、收刀或翻滚动作锁中。InputProfile 的 `Input.Sheathe` Chord 必须同时要求 `Combat.State.Grounded` 与 `Combat.State.Unsheathed`，从源头不产生空中收刀快照；`UMHGZSheatheAbility::CanActivateAbility` 仍必须以当前 RuntimeHost/ASC 状态二次确认 Grounded（并拒绝 Aerial）与上述阻塞状态。这样即使输入快照后的极短时间内起跳，也不会把旧的地面输入误激活成空中收刀。空中收刀不属于 M4.1，留给 M5 的空中动作设计明确接入。

`GA_Sheathe` 在成功提交 Action 后必须以自己的 `FWeaponOwnedTagToken` 获取 `Combat.State.Sheathing`，并从激活开始一直持有到 `EndAbility`；不得在 `SheatheCommit` 时释放。该 Tag 与姿态 Tag 正交：Commit 前为 `Unsheathed + Sheathing`，Commit 后为 `Sheathed + Sheathing`。这样武器已归位但 Montage 尾帧/Root Motion 尚未结束时，普通玩家按键仍不能插入新动作。

实现合同如下：

1. `Input.Sheathe` 的 Chord 同时要求 `Grounded+Unsheathed`；`UMHGZSheatheAbility::CanActivateAbility` 重新读取当前状态，必须拒绝 `Aerial`、`Dead`、`Attacking`、`Hitstun`、`Knockdown`、`Sheathing` 与 `Dodging`，并保留 Mesh/AnimInstance/Montage/Section 有效性检查。不得把 InputProfile 视为唯一门槛。
2. 在 `DefaultGameplayTags.ini` 注册 `Combat.State.Sheathing`；`UMHGZSheatheAbility` 获取它失败时必须取消本次 Action，所有正常结束、中断、换武器、死亡和 Runtime Shutdown 都依赖 ActionToken/TagLedger 精确回收。
3. `UMHGZGameplayAbility::CanActivateAbility` 为普通玩家动作默认拒绝 `Sheathing/Dodging`；正在收刀的 `GA_Sheathe` 不豁免，因此重复激活也会被自己的 `Sheathing` 拒绝。常驻 Coordinator 只豁免自身初始化，其 Transition 仍拒绝动作锁；未来强制反应必须由原生类显式豁免。不能靠把多个同 `InputTag` 的 Blueprint 堆在 `CoreAbilities` 来决定优先级。
4. `UGA_WeaponComboCoordinator::HandleWeaponInput`/Transition 匹配在该 Tag 存在时直接拒绝，不能建立 PendingTransition 或预输入；`Input.Dodge`、猎虫等通用路由最终也必须被 GA 激活检查拒绝。
5. 仅硬直、死亡等明确的强制取消路径可打断收刀；若未来要设计“收刀后摇可翻滚取消”，必须另做精确 `SheatheCancelWindow` 和明确的取消规则，不能复用 `DodgeWindow` 或提前清 `Sheathing`。

#### 翻滚取消与翻滚动作锁（M4.1 必需）

最终 `GA_Dodge` 与 `GA_Sheathe` 同属 `CoreAbilities`，且 `Input.Dodge` 只能有一个已授予的最终 Spec；它不进入任何武器 ComboData。CoreAbilities 只保证通用输入始终有目标，**不**提供取消优先级或动作互斥。

攻击 Montage 上的 `AnimNotifyState_DodgeAcceptWindow` 才是“攻击允许被翻滚打断”的短窗口：它由当前攻击的精确 ActionToken 持有 `Combat.State.DodgeAcceptOpen`，窗口关闭/攻击结束即回收。它与翻滚 Montage 上的 `AnimNotifyState_DodgeWindow` 完全不同；后者仅在无敌帧持有 `Invincible` 并暂时调整碰撞，不能允许攻击取消，也不能充当输入锁。

`GA_Dodge` 成功 Commit 后以自己的 Token 持有 `Combat.State.Dodging` 直到 `EndAbility`。普通玩家动作（攻击、猎虫、收刀、再次翻滚）默认拒绝此 Tag，Coordinator 也不得建立 PendingTransition；硬直、死亡、换装等强制路径可中断并统一清理。攻击中按 Dodge 使用两阶段交接：新 Dodge 先完成预检/Commit/动作登记，Coordinator 对仍有精确 DodgeAcceptWindow 的旧攻击 Prepare；随后启动并登记 Dodge Montage，成功后 Commit `RequestEndAction(Superseded)`，失败则 Cancel Prepare。Prepare 期间旧攻击会忽略由新 Montage 引发的瞬时 Interrupted 回调，保证最终结束原因仍为 Superseded；不得在 Dodge 失败前先杀死旧攻击。

Notify 不直接操作 CharacterMovementComponent，也不直接清全局 Tag。它使用 `(Mesh, MontageInstanceID) → ActionToken` 注册表定位当前 Ability；GA 再通过 `FWeaponOwnedTagToken` 释放自己的锁。`UMHGZGameplayAbility` 应新增受保护的单 Token 释放接口（例如 `ReleaseActionTag(FWeaponOwnedTagToken&)`），同时从其结束时清理列表移除该 Token；禁止 `RemoveLooseGameplayTag` 或“按 Tag 全删”。

**交接点：** `MontageRootMotionOwner` 只能在 Montage 不再贡献根位移、Slot 正要/已经 Blend Out 时释放。之后 `bForceMMIdle=false`，MM 根据在 `SteeringRootMotion` 阶段已更新的 `DesiredSpeed`、Actor Yaw 与 Trajectory 接管。若通用数据库不能稳定匹配动作尾姿，将尾段裁为非循环 transition sequence，加入目标 Database；仍不足时才用短暂的专用 Exit Database/Chooser 约束，绝不让该序列同时被 Montage 与 MM 驱动。

> **历史实现说明：** 本节描述改造前的 Motion Matching 路径及其动作交接合同；其中的日期型实现进度不再作为阶段依据。当前状态、M4.1.5 的 E4.1 接线门禁、M4.2～M4.5 的顺序以及 M4.6/M4.7/M5 禁入条件，以 [阶段门禁](milestone-gates.md) 和 [纯 Motion Matching 普通移动实施指南](pure-motion-matching-locomotion-guide.md) 为准。`locomotion-refactor*.md` 中的 CMC + 状态机路线只保留为历史备选。

### 8.2 MoveSpeedMultiplier

GAS `MoveSpeedMultiplier` 属性已存在，但以下消费方式尚未接入：

- 在 `CalcCruiseSpeed` 中乘以 `Multiplier`
- 例如减速 debuff 30% → Multiplier = 0.7 → WalkCruise = 150×0.7 = 105

### 8.3 冲刺

- `SprintAction` 绑定 RB/R1（2026-08-11 双语义：收刀态按住奔跑；仅持刀地面态由 Router 解析为纳刀）。`SprintPressed` 改为收刀态按住 ≥0.1s 才置 `bSprintHeld=true`（避免点按闪跑），拔刀态直接 return；`CalcCruiseSpeed` 拔刀分支不返回 SprintCruise
- `bSprintHeld = true` 时 `CalcCruiseSpeed` 对摇杆 > 0.9 返回 `SprintCruise`
- 拔刀态禁止冲刺（`Combat.State.Unsheathed` Tag）；奔跑中拔刀（Y/RT）必须清 `bSprintHeld`

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

**切换时机**：收刀/拔刀 GA 播放 Montage 期间，Montage 通过 Slot 节点覆盖 MM 输出。收刀必须在武器实际挂回背部的 `AnimNotify_SheatheCommit` 切换 `Sheathed`；该 Notify 后的短尾帧属于 `Database_Unarmed` 的姿势家族，Slot 继续遮盖数据库切换。若 Notify 前中断，保持 Unsheathed；若 Notify 后中断，保持 Sheathed。所有拔刀路径反向遵守同一合同：`GA_IG_Draw`、`GA_IG_DrawSlash` 与 `GA_IG_DrawAndSendKinsect` 都只在自己的 `AnimNotify_DrawCommit` 解析到当前 ActionToken 后切 `Unsheathed` 并清 `bSprintHeld`；Commit 前中断保持 Sheathed，Commit 后中断保持 Unsheathed。真实全身根位移动作的 Montage 根位移结束并 Blend Out 后才释放 `MontageRootMotionOwner`，MM 依据实时输入接续新数据库的第一匹配帧；`GA_IG_DrawAndSendKinsect` 是 in-place `UpperBody_IGAction`，从不取得该所有权，且其后续 `KinsectSendCommit` 才放虫。不能再笼统地在 Montage 正常结束或 Ability 激活时切换姿态。

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
  ├─ BlockMovement? → DesiredSpeed=0，禁止角色按摇杆转向
  └─ BlockMovement 或 MontageRootMotionOwner? → bForceMMIdle=true
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
8. **动作阶段运行时**：实现 ActionToken 所有的 Montage Root Motion 所有权、单 Token 提前释放，以及 Dodge 自身 Montage `SectionChanged` 出口处理、`SheatheCommit` Notify 的精确路由。
9. **GAS 验证**：测试前向翻滚锁定段不受摇杆影响、只有前向变体可进入 MoveExit、持刀左/右/后强制 IdleExit、收刀左/右/后拒绝且不耗耐、移动收刀可转向且不换 Section、收刀与所有拔刀路径的 Commit 前后中断姿态、Montage 与 MM 从不同时拥有根位移。
10. **脚步 IK**：添加 Foot Placement IK（可选，先看滑步严不严重）

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
