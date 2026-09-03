# 动作系统

> **实施状态说明（以源码为准）：** 本文保留完整目标设计。除本节“当前实现”以及正文中明确标注为已实现的内容外，其余类、字段、资产、流程和验证项均是待实现或待接入的详细方案，不表示当前项目已经具备。

> **项目动作口径：** 动画和机制可参考《世界》《崛起》《荒野》，但项目采用原创连招规则，不以逐项复刻某一作为目标。虫棍具体动作见 [insect-glaive-actions.md](insect-glaive-actions.md)；通用动作层不得包含翔虫、集中模式、钩爪或虫棍资源判断。

> **冻结实施口径：** 后续代码以 [Demo 冻结实施计划](demo-implementation-plan.md) 为公共接口真相源。M1 已删除 `FComboNode/ComboTable` 运行时、全局 `PreviousState`、`bIsContinuous` 成本语义和 ASC 物理输入职责；M2 已完成 RuntimeHost 装备重建、真实命中上下文、多跳策略和反馈基底，后续阶段不得恢复旧结构或旧运行时读取。
> **重构边界：** 具体保留、重写、删除与旧资产处理见 [Demo 重构范围与资产处置](demo-refactor-scope.md)。

## 当前实现

| 模块 | 当前状态 |
|------|----------|
| 移动 | `AMHGZCharacter::DoMove` 只计算 `InputMagnitude`、`TargetCruiseSpeed` 和 `DesiredSpeed`，不调用 `AddMovementInput`；AnimBP 使用 Motion Matching/Root Motion。角色在 `Tick` 中按 `TurnRate` 限制最大转角，默认 `360°/s`，180° 不再瞬转。 |
| 冲刺与瞄准 | 冲刺是 Character 上的 `bSprintHeld`，RB 收刀态持有 0.1s 后成立；M3 已将瞄准拆成 `Combat.State.Aiming.Kinsect/Action/Slinger`，由 InputRouter 通过 TagLedger Token 按 LT/RT 与收刀姿态派生。它们不是 `GA_Sprint`/`GA_Aim` 驱动。 |
| GAS 输入 | `UMHGZInputComponent` 独占 Enhanced Input 绑定，`UMHGZWeaponInputRouterComponent` 生成组合键、方向、姿态、Aim 与释放身份不可变快照；ASC 只接收已解析输入，不拥有物理键。 |
| 攻击 | `UMHGZAttackAbility` 使用 Montage Task、多 `TraceRegions` 自适应 Socket Sweep 和真实 `FHitResult`。同帧多 Region 选最早命中；默认接触式去重，只有显式 `LockedTargetTicks` 才离散复击并逐跳重验；旧 Socket/Shape 字段只剩序列化壳。普通攻击入口已使用 Confirm 后、Montage 前的冻结输入 Yaw 瞬转，不创建动态 WarpTarget；M4-A.5 的单帧招内修正沿用同一直接 Yaw 算法，但只在原生普通 Notify 的精确帧读取实时摇杆。 |
| 连招 | `UGA_WeaponComboCoordinator` 使用 `FComboTransition/Transitions`、不可变 ActivationContext 与 Pending/Confirm/Active 两阶段状态；方向、窗口、自动边、StateOnly、命中授予和 Superseded 实例隔离均已接通。最终虫棍 Transitions 仍待 E4 配置。 |
| 闪避 | M4-A.3 已实现前向 `LockedRootMotion → SteeringRootMotion → MotionMatching`，以及 `Dodging`、攻击侧 DodgeAcceptWindow、两阶段安全 Superseded 交接、GA 自身 Montage `SectionChanged` 出口处理与逐通道恢复。前向 `GA_Dodge`/Montage 与 M4-A.3.1 的持刀左/右/后翻滚选择、IdleExit/Section 校验均已纳入后续 E4.2/M4.5 PIE 固定矩阵并签收；现行工作项以 [阶段门禁](milestone-gates.md) 为准。 |
| 边缘跳越 | `UMHGZEdgeVaultComponent` 目前仅为关闭 Tick 的桩组件；检测链和 `GA_EdgeVault` 属于下文保留方案。 |
| 基础消耗/冷却 | None/Instant/PerSecond 已分别使用有效原生 GE/Drain Task；Cooldown 使用 HasDuration GE 与动态 GrantedTag；武器资源走 reservation→Commit→Consume/Release 事务。具体虫棍三灯消费由 M3/M6 完成。 |

**设计原则：** GAS + EnhancedInput 驱动，通过输入快照和 GameplayTag 桥接 Ability。协调器只处理武器无关的状态转移；武器能力、资源和特殊判定由装备时动态授予的派生层提供。**无独立跳跃键——边缘跳越（Edge Vault）替代。**

## 移动实现

- 移动物理壳、重力与落地检测：`UCharacterMovementComponent`；常规位移由 AnimBP Root Motion 驱动
- 移动输入：`DoMove` 记录输入方向和期望速度，当前不调用 `AddMovementInput`
- 移动动画：AnimBP Motion Matching，根据 `DesiredSpeed` 和双 Pose Search Database 驱动
- 奔跑：收刀态按住 RB ≥0.1s 后 `bSprintHeld` 切换巡航速度；仅持刀地面态才由 `Combat.State.Unsheathed+Grounded` 的 Chord 将 RB 转交 Router 解析为纳刀，空中 RB 不产生收刀输入
- GAS 当前主要通过 `Combat.State.BlockMovement` 阻断移动；`MoveSpeedMultiplier` 已定义但尚未接入 `CalcCruiseSpeed`

### RootMotion——攻击/翻滚中如何覆盖 CMC 移动

攻击 Montage 配置 Root Motion 时，动画根骨骼位移可直接驱动角色位移/旋转。当前常规移动本身不调用 `AddMovementInput`；攻击期间通过 `Combat.State.BlockMovement` 把 Motion Matching 期望速度清零。普通攻击在 Confirm 后、Montage 播放前按冻结输入做一次 Actor Yaw 瞬转（见 `MaxCorrectionAngle`）；它不依赖 MotionWarping。MotionWarping 只留给具有真实目标或位移/朝向对齐需求的特殊动作。

| 场景 | RootMotion 作用 | bEnableRootMotion |
|------|-----------------|:--:|
| 攻击 Montage | 锁定角色按动画轨迹移动；普通攻击仅在入口按冻结摇杆作一次方向修正 | ✅ true |
| 翻滚 Montage | 前跃/侧移距离由动画精确控制，不受 CMC 加速度/摩擦影响 | ✅ true |
| 见切后撤 | 段0 后撤位移完全动画驱动（配合 MotionWarping 修正方向） | ✅ true |
| 登龙下劈 | 空中轨迹动画控制——不是物理跳跃+下落 | ✅ true |
| 受击硬直 Montage | 击退距离由 Impulse + Montage RootMotion 共同决定 | ✅ true |
| 待机/收刀行走 | 摇杆+CMC 正常移动 | ❌ false |

### MotionWarping——有根位移动画的目标对齐工具

#### 问题背景：外部游戏提取的 FBX 通常缺少根骨骼位移

从三部曲提取并重定向的 FBX 动画，其骨骼层级与 UE5 RootMotion 预期可能不同：

```
UE5 RootMotion 预期结构：
  Root Bone（世界原点）→ 随角色移动逐帧改变坐标 → 帧间差值 = RootMotion 位移

RE Engine FBX 实际结构：
  Root Bone（始终锚定原点 0,0,0，不动）
    └─ Hips/Pelvis → 此骨骼随攻击向前移动，但 Root 不动
```

**典型现象：** 闪避/小幅攻击有位移生效，大幅位移攻击（虫棍飞天突刺、大剑蓄力斩等）无位移——因为 RE Engine 中大幅位移由游戏逻辑代码实时驱动，动画文件本身只存储原地姿态数据。

#### 验证方法

1. 打开动画序列 → 骨骼树面板 → 选中根骨骼
2. 曲线编辑器查看 Translation X/Y/Z 曲线
3. **曲线全程水平线 = 根骨骼无位移数据 → 不能依赖 MotionWarping 生成位移，应使用 RootMotionSource/AbilityTask/自定义轨迹**

#### MotionWarping 核心机制

MotionWarping 修改动画已经提供的根运动，使动作在 Warp Window 内对齐到目标位置/朝向。它不是通用运动组件，也不能把总平移为零的动画稳定拉成任意距离冲刺。

```
原始动画根骨骼轨迹：起点 ═════════════ 终点（已有明确前移）
设定 Warp Target：   起点 ═════════════════ 目标点（在允许缩放范围内校正）

逐帧处理：
  每帧读取原始动画根骨骼位移 → 在配置的平移/旋转限制内重映射
  → 保持动画相位并对齐到目标
```

#### 位移速度控制

MotionWarping 不提供类似 CMC 的加速度曲线，但可通过以下参数控制视觉速度：

| 控制方式 | 作用 | 配置位置 |
|----------|------|----------|
| **Warp Window 时长** | 位移被拉伸的时间段。窗越短→速度越快；窗越长→越平滑 | `AnimNotifyState_MotionWarping` 的 Start/End 时间 |
| **Warp Translation 轴开关** | 控制 X/Y/Z 各轴是否参与 Warp、最大缩放倍数 | NotifyState 的 `WarpTranslation` 属性 |
| **Montage Play Rate** | 改变动画与 Warp Window 的实际持续时间；必须与攻击帧一起校准 | GA 的 `PlayMontageAndWait` 的 `Rate` 参数 |
| **自定义 Warp Curve** | 自定义缓入缓出曲线（Ease-In/Out） | `MotionWarpingComponent` 的 Warp Curves 资产 |
| **分段 Warp** | 同 Montage 内多个 Warp Notify 覆盖不同时间片段 | 动画师在 Montage 时间轴上分段拖拽 |

> **选择规则：** 有可用根运动且只需有限目标对齐时用 MotionWarping；需要指定距离、最高点、贯通、惯性继承或零根位移动画时用通用位移任务。虫棍觉虫击、猎虫滑翔、操虫斩和降龙按 [虫棍动作设计](insect-glaive-actions.md) 选择后者。

#### 高速突刺 + 命中分支的 MotionWarping 配合

```
招式结构（单 Montage，三 Section）：
  Section "Approach"  ← MotionWarping Notify 驱动位移 + AttackCollision 检测命中
  Section "OnHit"     ← 命中后追击动画（可选新 Warp 或纯 RootMotion）
  Section "OnMiss"    ← 未命中收招动画

GA 流程：
  1. ActivateAbility → 计算 Warp Target（怪物位置/前方固定距离）
  2. PlayMontage("Approach") → Warp 逐帧驱动位移
  3. 碰撞命中 → Montage_JumpToSection("OnHit") → Warp Notify 自然结束
  4. Approach 段正常播完 → 自动进入 "OnMiss"
```

> ⚠️ **命中时不可 Stop Montage 再 Play 新 Montage**——Stop 会强制终止 MotionWarping，导致根骨骼弹回参考位置、角色瞬移。必须用 Section 切换（`JumpToSection`），Warp Notify 自然结束。

## 移动动画系统（旧方案，已由 Motion Matching 替代）

**设计原则：** 移动循环动画是纯视觉层——不开启 Root Motion、CMC 全权负责物理位移。AnimBP 通过 Speed（Velocity.Size()）驱动动画选择。GAS 仅通过 GE 修改 MoveSpeedMultiplier 间接影响 Speed。

### 动画资产配置

| 动画资产 | bLoop | bEnableRootMotion | Rate Scale | 说明 |
|----------|:-----:|:-----------------:|:----------:|------|
| A_Idle | ✅ | ❌ | 1.0 | 待机循环 |
| **A_Start_Walk** | ❌ | ❌ | 1.0 | 走路起步（一次性 ~0.3s） |
| **A_Start_Run** | ❌ | ❌ | 1.0 | 奔跑起步（一次性 ~0.25s） |
| A_Walk | ✅ | ❌ | 1.0 | 走路循环 |
| A_Run | ✅ | ❌ | 2.0 | 奔跑循环（FBX 源帧率不匹配时需 Scale 修正） |
| A_Sprint | ✅ | ❌ | 3.0 | 冲刺循环（A_Run 副本，×1.5 速） |
| **A_Walk_Stop** | ❌ | ❌ | 1.0 | 走路停步（一次性 ~0.3s） |
| **A_Run_Stop** | ❌ | ❌ | 1.0 | 奔跑停步（一次性 ~0.3s，含跑步→走路→站定全程） |

> **加粗 = 一次性过渡动画，** 其余 = 循环动画。A_Sprint 无需单独 FBX——复制 A_Run 资产后在 Asset Details 中设 `Rate Scale = 3.0` 即可（A_Run 自身 Rate Scale = 2.0 修正帧率后，Sprint 在此基础上再 ×1.5）。

### AnimBP 状态机结构

```
                    ┌──────────────────────┐
                    │       Idle           │
                    │    (A_Idle 循环)      │
                    └──────┬───────┬───────┘
              InputMag     │       │  Speed < 10 持续 0.1s
              0.2~0.7      │       │  ▶ 播放 Stop 动画
              ▶ Start_Walk │       │
                           │       │
              InputMag     ▼       ▲
              > 0.7    ┌───┴───────┴───┐
              ▶ Start_Run│   Moving    │
                         │ BlendSpace1D│
                         │ Walk⇄Run⇄   │
                         │ Sprint      │
                         └─────────────┘
```

| 转换 | 条件 | 过渡动画 | 播完后 |
|------|------|----------|--------|
| Idle → Moving | `InputMagnitude ∈ [0.2, 0.7)` | A_Start_Walk | 切入 Moving 状态（Blend Space） |
| Idle → Moving | `InputMagnitude ∈ [0.7, 1.0]` | A_Start_Run | 切入 Moving 状态（Blend Space） |
| Moving → Idle | `Speed < 10` 且 `InputMagnitude ≈ 0` 持续 0.1s | 见下方停步判定 | 切入 Idle 状态 |

### 起步判定：用 InputMagnitude 而非 Speed

**原因：** Speed 有 CMC 加速延迟——等 Speed 涨到能判断"玩家想跑"时，起步动画已经播完了。`GetPendingMovementInputVector().Size()` 返回摇杆幅度，0 帧瞬时响应。

```cpp
// AnimBP EventGraph 中每帧获取
APawn* Pawn = TryGetPawnOwner();
float InputMag = Pawn->GetPendingMovementInputVector().Size();
// 范围：0.0（未推摇杆）～ 1.0（摇杆推到底）
```

| 信号 | 延迟 | 适用场景 |
|------|:--:|----------|
| InputMagnitude（摇杆幅度） | 0 帧 | ✅ 判断起步类型（Walk/Run） |
| Speed（Velocity.Size()） | 有加速惯性 | 用于 Moving→Idle 停步判定 |

### 停步判定：松手瞬间 Speed 快照

**问题：** 松摇杆时角色还在高速移动，等 Speed 降到阈值时已不知道原本是 Walk 还是 Run。**解决：在 InputMagnitude 下降沿瞬间快照当前 Speed。**

```
每帧比较 InputMag 与上一帧：
  ┌─ 上一帧 InputMag > 0.2 且 当前帧 InputMag < 0.1
  │    → SnapSpeedAtRelease = 当前 Speed  ← 快照
  │
  └─ 等 Speed < 10 且 InputMag ≈ 0 持续 0.1s → 触发 Moving→Stop
       ├─ SnapSpeedAtRelease > RunSpeedThreshold（如 400）
       │    → 播放 A_Run_Stop（跑步收脚→走路减速→站定）
       └─ SnapSpeedAtRelease ≤ RunSpeedThreshold
            → 播放 A_Walk_Stop（走路收脚→站定）
```

> **为什么不需要 Walk_Stop 后再接 Run_Stop：** Run_Stop FBX 的时间线已覆盖"跑步姿势→过渡到走路→走路收脚→Idle"全程，一段动画完成所有减速阶段。

### 冲刺动画派生

冲刺和奔跑是同一运动模式，仅频率不同。无需额外 FBX：

1. 在编辑器中复制 `A_Run` 动画序列 → 重命名为 `A_Sprint`
2. 打开 `A_Sprint` → Asset Details → `Rate Scale = 3.0`
   - A_Run 已设 Rate Scale = 2.0（修正 FBX 帧率不匹配）
   - Sprint = Run 的 1.5 倍速 → 2.0 × 1.5 = 3.0
3. Blend Space 中 Sprint 格点引用 `A_Sprint`

| 格点 | 动画资产 | 资产 Rate Scale | 实际播放速度 |
|------|----------|:---:|:---:|
| Idle | A_Idle | 1.0 | 1.0 |
| Walk | A_Walk | 1.0 | 1.0 |
| Run | A_Run | 2.0 | 2.0 |
| Sprint | A_Sprint（A_Run 副本） | 3.0 | 3.0 |

### FBX 导入注意事项

#### 帧率不匹配

FBX 导出帧率 ≠ UE5 导入帧率会导致动画变慢/变快。表现为导入后动捕速度明显不对。

- **查：** 打开动画序列 → Asset Details → `Number of Keys` 和 `Sequence Length` 是否匹配预期
- **改（推荐）：** 动画序列 → Asset Details → `Rate Scale` 设修正倍数
- **改（导入时）：** FBX Import Options → `Import Uniform Sampling = 0` → `Sample Rate` = 源帧率

#### Z 轴偏移

部分解包动画的根骨骼不在地面高度，导入后动画序列中角色浮空。表现为需在预览中手动偏移 Z 才能贴合地面。

- **根骨骼在腰部时的常见问题：** 角色脚底 Z < 0，Animation Sequence 预览浮空
- **方案 A（推荐——导入时）：** FBX Import → `Translation Offset` 填入偏移值（如 `(0, 0, -111)`）
- **方案 B（AnimBP 修正）：** AnimGraph 末尾加 `Transform (Modify) Bone` 节点 → Root Bone → Translation Z = 偏移值
- **方案 C（动画序列内修正）：** 打开动画序列 → 选中根骨骼 Translation Z 曲线 → 全选关键帧整体偏移

> ⚠️ 移动循环动画务必 **关闭 Root Motion**（`bEnableRootMotion = false`）。若误开 Root Motion，根骨骼在腰部时其 Z 轴上下起伏（Walk/Run Bob）会被 CMC 累积应用，导致角色逐帧上浮。此问题在 Blend Space 中尤其明显——不同 Speed 格点的 Z 增量混合后加速上浮。

#### Root Motion Root Lock

开启 Root Motion 时（仅攻击/翻滚 Montage），`Root Motion Root Lock` 控制根骨骼旋转处理：

| 枚举值 | 行为 | 适用 |
|--------|------|------|
| `RefPose` | 锁定到参考姿势 | ✅ 推荐——根骨骼在腰部时也安全 |
| `AnimFirstFrame` | 锁定到动画第一帧朝向 | ✅ 备选 |
| `Zero` | 所有旋转归零 | ❌ 根骨骼在腰部时会导致模型面朝上等异常朝向 |

### 八方向移动的转向策略

项目仅使用正向 Walk/Run 动画循环（无侧面/后退动画），八方向移动靠 CMC 旋转角色实现：

| 玩家操作 | CMC 行为 | AnimBP 行为 |
|----------|----------|-------------|
| 摇杆前推 | 直走 | 播正向 Walk/Run |
| 摇杆左推 | `bOrientRotationToMovement = true` → 自动旋转角色 -90° | 播正向 Walk/Run |
| 摇杆后推 | 自动旋转角色 180° | 播正向 Walk/Run |
| 摇杆右推 | 自动旋转角色 +90° | 播正向 Walk/Run |

**关键 CMC 配置：**

| 属性 | 值 | 效果 |
|------|:--:|------|
| `bOrientRotationToMovement` | `true` | 角色自动朝向移动方向 |
| `RotationRate.Yaw` | 540°/s | 转向速度 |
| `bUseControllerDesiredRotation` | `false` | 不跟随摄像机朝向 |

> **结论：** 正向动画 + CMC 旋转 = 完整的八方向移动表现。不需要侧面或后退动画。注意区分"摇杆后推→CMC 转身 180°"（移动循环，无 Root Motion）和"战斗回避后撤"（Dodge/见切 Montage，Root Motion 驱动）——两者是不同的系统路径。

### 系统数据流总览

```
摇杆输入 → AddMovementInput → CMC（物理）→ Velocity → Speed
                                                          │
                                          ┌───────────────┘
                                          ▼
                                    AnimBP 状态机
                                    ├─ Idle 状态: A_Idle
                                    ├─ Start 转换: A_Start_Walk / A_Start_Run
                                    ├─ Moving 状态: BlendSpace1D (Walk ⇄ Run ⇄ Sprint)
                                    └─ Stop 转换: A_Walk_Stop / A_Run_Stop
                                                          │
                                                          ▼
                                                     输出 Pose

攻击/翻滚（并行路径，RootMotion=ON）：
EnhancedInput → Tag → ASC → GA → Montage → RootMotion 覆盖 CMC 位移
```

## 空中动作系统（规划，当前无空中 GA）

**设计原则：** 通用层通过**惯性速度状态（AerialVelocity）**在 CMC 与 GA 之间交接动量，并保证任一时刻只有一个位移所有者。虫棍动作选择和派生以 [insect-glaive-actions.md §6](insect-glaive-actions.md#6-舞踏与空中动作) 为准。

### 招式分类与位移策略

| 分类 | 虫棍示例 | 位移特征 | 目标实现 |
|------|---------|----------|----------|
| **① 动画根运动** | 具有可靠 Root 曲线的空中回避/短动作 | 轨迹主要由动画给出 | Montage RootMotion；MotionWarping 只做有限朝向/落点校正 |
| **② 受限定向位移** | 操虫斩、猎虫滑翔、觉虫击猎人位移 | 激活时快照方向，具有最大距离和碰撞终止 | RootMotionSource/AbilityTask + Capsule Sweep + 距离/时长上限 |
| **③ 弹跳/舞踏** | 操虫斩命中、突进回旋斩反击后的舞踏 | 明确初速度或最高点，随后交给重力 | 由统一 Launch/Ballistic Task 计算速度；结束写回 CMC |
| **④ 惯性叠加攻击** | 强化跳跃斩、急袭突刺 | 保留进入动作时水平惯性并允许受限空中修正 | Additive RootMotionSource/Task；动画位移与惯性合成 |
| **⑤ 自由下落** | 空中动作结束后的 Falling | 仅重力、空气控制和衰减 | 无 GA 位移，CMC 全权管理 |

任何运行时轨迹都必须输出明确的 Start、EndReason、FinalVelocity，并在碰墙、受击、死亡、落地和换武器时清理。不能在 `EndAbility` 临时读取“最后一帧根位移”猜测整段动作末速度。

### 惯性系统——AerialVelocity 状态

**核心问题：** 空中 GA 期间 RootMotion/MotionWarping 覆盖 CMC Velocity，GA 结束时若不做交接，惯性丢失——角色像"撞墙"一样失去空中动量。

```
AerialVelocity（逻辑概念，物理载体是 CMC→Velocity）：
  生命周期：
    起跳瞬间：CMC→Velocity 初始化
    GA 激活时：快照 Velocity → 传递给 GA（作为惯性力的输入）
    GA 执行时：GA 读取+修改 → RootMotion Task 驱动合成
    GA 结束时：最终合成值写回 CMC→Velocity
    无 GA 时：CMC 自主管理重力+空气摩擦
```

### 速度交接协议

```
CMC（物理层）          GA（逻辑层）               AnimBP（表现层）
═══════════          ═══════════               ═══════════════
地面移动             （无空中 GA）              地面状态机
  ↓ 起跳/击飞
Velocity=(vx,vy,vz)
  ↓ GA 激活              ↓
暂停 Velocity 更新  →  快照 Velocity
                        ↓
                    根据分类处理：
                    ① Montage RootMotion
                    ② Bounded Directional Task
                    ③ Ballistic/Vault Task
                    ④ Additive Inertia Task
                        ↓
  ↓ GA 结束              ↓
Velocity=最终合成值  ←  回灌               下落 Pose Tag 写入
  ↓                                          ↓
CMC 管理重力+摩擦
  ↓ 落地                   ↓                    ↓
Velocity归零→Grounded    可激活新 GA          落地动画→Idle
```

### 通用位移任务协议

所有武器共享 `UAbilityTask_MHGZMovement`，字段与冻结计划保持一致：

```text
FWeaponMovementRequest:
  OwnerAction, Mode, DirectionSnapshot, MaxDistance, Duration, DistanceCurve,
  InheritedVelocity, InheritedVelocityRatio,
  BallisticMode, ApexHeight, LaunchVelocity, AirControlScale,
  RotationPolicy, MaxTurnRateDegrees, SteeringConeHalfAngle, WarpTargetName,
  CollisionPolicy, CancelVelocityPolicy
FWeaponMovementResult:
  EndReason, TravelledDistance, FinalVelocity, BlockingHit
```

`Mode` 只允许 BoundedDirectional/BallisticVault/AdditiveInertia。`DirectionSnapshot` 在输入发生时确定；任务执行中不能持续追踪准心。`CollisionPolicy` 至少区分 WorldBlock、MonsterHit、Landing、IgnoreHitzoneForMovement。任务按 ActionToken 拥有唯一 Movement Token/RootMotionSource ID、旋转/steering 权限和唯一 WarpTargetName；Character 普通移动在该 Token 有效时不得另外写 ActorRotation。所有 Ability 结束路径精确回收本动作对象。

弹跳任务不能用“距离/Duration”冒充三维初速度。若配置为最高点 `ApexHeight` 与时长 `Duration`，应由重力求解初始 Z 速度；若配置为显式 LaunchVelocity，则最高点只作为验证结果。

### RootMotion Task vs MotionWarping — 选择边界

| | RootMotion Task（SetVelocity） | MotionWarping |
|------|:--:|:--:|
| **运行时速度正确反映在 CMC？** | ✅ 每帧 | ⚠️ 反映，但 Warp 是"位移缩放"而非"速度控制" |
| **结束时保留惯性？** | ✅ 任务显式返回 FinalVelocity | 仅在动画根运动连续且已验证时可取动画提取速度 |
| **能叠加惯性（Additive）？** | ✅ `bIsAdditive=true` | ❌ Warp 只修改动画内位移 |
| **能覆盖惯性（重置）？** | ✅ `bIsAdditive=false` | ❌ 需配合 Task 覆盖 |
| **适用分类** | ② ③ ④ | ① |

> `GetRootMotionDelta()` 只代表被查询帧的动画根位移，不是可靠的整段动作末速度合同。需要惯性的动作必须让位移任务或经验证的动画提取器显式产出 FinalVelocity。

### 统一 EndAbility — 空中 GA 速度交接

Ability 保存当前位移任务句柄和最近一次有效 `FinalVelocity`。正常完成使用任务输出；取消时按动作的 `CancelVelocityPolicy` 选择保留当前速度、清零指定轴或交给 CMC。清理顺序固定为：停止攻击窗口 → 结束位移任务 → 写回速度 → 更新 Aerial/Falling 状态 → 通知协调器。

### 各分类惯性行为总览

| 分类 | 位移源 | 结束时机 | 惯性交接方式 |
|------|--------|---------|-------------|
| ① | Montage RootMotion | Montage/Warp 窗口结束 | 经验证的动画速度提取器输出，不在 EndAbility 猜测最后一帧 |
| ② | Bounded Directional Task | 命中/阻挡/距离/超时 | Task FinalVelocity → CMC |
| ③ | Ballistic/Vault Task | 时长/顶棚/落地/取消 | Task FinalVelocity → CMC |
| ④ | Additive Inertia Task | 动作段完成/取消 | 合成 FinalVelocity → CMC |
| ⑤ | CMC 物理 | 持续直到落地 | CMC 自身管理 |

### 空中收招后的状态流向

```
操虫斩命中 → ② 定向位移结束 → ③ 舞踏弹跳 → ⑤ 下落/继续派生
操虫斩未命中 → ② 输出末速度 → ⑤ 下落
猎虫滑翔命中 → ② 停止水平位移 → ③ 小弹跳（不加舞踏层）→ 空战
强化跳跃斩/急袭突刺 → ④ 合成惯性 → ⑤ 下落或显式落地段
降龙 → ②/③ 组合任务 → 落地段 → Grounded
```

### 空中动作次数限制

空中回避和普通空中攻击许可仍可使用 `CantDodge`/`CantAttack` Tag；舞踏层数则必须是虫棍资源组件中的显式计数，不能用 Cant Tag 代替。只有操虫斩命中和突进回旋斩反击能触发舞踏并按动作配置重置空中许可。

```
权限模型（Tag 驱动，零交叉逻辑）：

  起跳 → ASC 无任何 Cant Tag（默认全部可用）
    │
    ├── GA_AirDodge: BlockedTags={CantDodge}
    │     → 激活时添加 CantDodge（锁自己）
    │
    ├── GA_AirAttack_*: BlockedTags={CantAttack}
    │     → 激活时添加 CantAttack（锁自己）
    │
    ├── 两者都用了 → CantDodge + CantAttack 都存在
    │     → 各自 BlockedTags 各自命中 → 双阻塞
    │
    └── ★ IG DanceVault（仅操虫斩命中/突进回旋斩反击）
          → 移除 CantDodge + CantAttack（重置）

不需要 Exhausted——两个 Cant 各自独立，不需要"汇总"标签。
```

**与碰撞/伤害系统无关：** 次数限制仅影响 GA 的 `CanActivateAbility`（GAS 原生 `BlockedTags`，零覆写代码）——不阻塞连招表匹配、不涉及 AnimNotifyState、不改变位移逻辑。空中攻击未命中→`ShouldContinueAfterHit` 返回 false→`EndAbility` 写入 `Falling.*` Tag→AnimBP 维持下落 Pose，不会额外发放新的空中权限。Cant 的容错性优于 Can：加 Cant 失败→最多多用一次；加 Can 失败→本应可用的招式被误锁。

### 着陆重置——落地后恢复连招

**当前问题：** 落地链路绕过了协调器（CMC `OnLanded` 事件，不经过任何 GA）→ `CurrentState` 残留空中攻击的招式名 → 旧 `FComboNode` 无法从 `"Idle"` 匹配起手攻击。

**方案：** 在 `AMHGZCharacter::OnLanded()` 中：
1. 通过句柄移除当前空中动作实例拥有的 `Aerial`/`Falling.*`/`CantDodge`/`CantAttack` Tag → 添加 `Grounded`
2. 当前武器资源收到 `OnLanded`；虫棍清空舞踏层数
3. `Coordinator→OnLanded()` → `CurrentState = "Idle"` + 清除本次连招拥有的 Branch Tag
4. AnimBP 自然检测 `Grounded` Tag → 播放 Landing→Idle 过渡

> 着陆不是 GA——不产生伤害、不消耗资源、不需要 Montage。它向协调器提交 `ResetCombo(Landed)`；落地后的攻击从 `SourceState="Idle"` 的 `FComboTransition` 匹配。

### GameplayTag 扩展

```
Combat.State.Aerial                               ← 已有
Combat.State.Aerial.Falling                       ← 下落态
Combat.State.Aerial.Falling.Default               ← 兜底 Pose
Combat.State.Aerial.Falling.IG_AirDodge
Combat.State.Aerial.Falling.IG_StrongJumpingSlash
Combat.State.Aerial.Falling.IG_DanceVault
Combat.State.Aerial.Falling.IG_KinsectGlide
Combat.State.Aerial.Falling.IG_KinsectSlash
Combat.State.Aerial.Falling.IG_DescendingThrust
Combat.State.Aerial.Falling.IG_DivingWyvern
Combat.State.Aerial.CantDodge                  ← 空中回避已用
Combat.State.Aerial.CantAttack                 ← 空中攻击已用
Combat.State.Aerial.Landing                       ← 落地瞬间过渡
```

### CMC 空中物理配置

| 属性 | 值 | 说明 |
|------|:--:|------|
| `GravityScale` | 1.8 | 空中重力倍率 |
| `AirControl` | 0.15 | 摇杆微调（低值=惯性主导，接近怪猎手感） |
| `BrakingDecelerationFalling` | 80 | 水平速度空中衰减 |
| `MaxAerialSpeed` | 2000 | 终端速度上限（cm/s），覆写 `CalcVelocity` 钳制 XY 分量 |

## 边缘跳越（规划，当前仅有桩组件）

CMC 边缘检测 + 自定义组件触发 + GA 播动画。推荐方案 A（组件轮询）：`UMHGZEdgeVaultComponent` Tick 中检测 → `TryActivateAbilityByTag(Input.EdgeVault)`。

## 输入流

**M1 当前源码：**

```
EnhancedInput → UMHGZInputComponent（唯一 Binding/IMC 所有者）
  → UMHGZWeaponInputRouterComponent
  → FWeaponInputSnapshot
     → 武器 Tag：Coordinator→HandleWeaponInput
     → 通用 Tag：ASC→HandleResolvedInputSnapshot
```

**后续阶段保持的冻结边界：**

```text
EnhancedInput
  -> UMHGZWeaponInputRouterComponent
     -> UWeaponInputProfile 解析组合、修饰键、姿态和世界方向
     -> FWeaponInputSnapshot(InputTag, HeldModifiers, Direction, Phase, SequenceID)
        -> 武器输入：Coordinator->HandleWeaponInput(Snapshot)
        -> 非武器输入：显式通用输入路由
```

ASC 保留 Ability Spec、GE 和激活能力，但不再直接解释物理按键或把任意 Completed 转成统一 ChargeReleased。

## FAbilityInputBinding — 已删除的旧输入结构

M1 已从源码删除 `FAbilityInputBinding`、`InputBindings`、`ActionToTag`、`bInputBound` 和 ASC 的 Enhanced Input 回调。旧蓝图在 E2 Compile 后不应再显示该属性；不得建立替代数组或第二套并行输入系统。

## UMHGZAbilitySystemComponent — 扩展 ASC

```
UCLASS()
class UMHGZAbilitySystemComponent : public UAbilitySystemComponent
```

扩展 UE 原生 ASC。M1 当前 ASC 只保留 GAS 身份、Ability 授予/移除、SpecHandle 查找、RuntimeHost 弱引用和带 ActivationContext/InputSnapshot 的激活接口。ASC 不拥有 IMC、UInputAction、物理键状态或 Pawn 初始姿态。

| 成员 | 类型 | Category | 默认值 | 说明 |
|------|------|----------|--------|------|
| CoreAbilities | TArray\<TSubclassOf\<UGameplayAbility\>\> | "Ability\|Core" | 空 | 核心能力列表（BeginPlay 时自动授予） |
| CoreAttributeEffects | TArray\<TSubclassOf\<UGameplayEffect\>\> | "Ability\|Core" | 空 | 核心 GE 列表（BeginPlay 时自动 Apply） |

### M1 当前方法

- `void InitializeAbilitySystem()`
  - 作用：幂等授予 CoreAbilities 并 Apply CoreAttributeEffects；不写 Pawn 姿态、不绑定输入。

- `void HandleResolvedInputSnapshot(const FWeaponInputSnapshot& Snapshot)`
  - 作用：武器 Tag 交给当前 Coordinator；通用 Tag 按 `UMHGZGameplayAbility::InputTag` 查 SpecHandle，注册一次性 ActivationContext 后激活。

- `void HandleResolvedInputRelease(const FWeaponInputSnapshot& Snapshot)`
  - 作用：交给当前 RuntimeHost，按 `SourceControlTag + SequenceID` 只通知匹配的 Active Action；默认 Ability 释放回调为 No-Op。

- `void GrantWeaponAbilities(TArray<TSubclassOf<UGameplayAbility>> Abilities)`
  - 输入：武器授予的能力类列表。
  - 作用：授予并存储 Handle → `EquipmentComponent→OnEquipmentChanged` 时调用。

- `void RemoveWeaponAbilities()`
  - 作用：移除所有武器授予的能力（切换武器时调用）。

- `PrepareWeaponAbilityActivation` / `ConsumePendingActivationContext`
  - 作用：以 SpecHandle 保存并精确一次消费不可变 ActivationContext；TryActivate 直接失败时必须立即丢弃，禁止后续激活读到陈旧快照。

### 目标统一派发流程

```
EnhancedInput (所有按键/摇杆)
  → WeaponInputRouter + 当前 UWeaponInputProfile
  → 组合键消费 / Aim 上下文 / 世界方向分类
  → FWeaponInputSnapshot
    → Weapon Tag → Coordinator→HandleWeaponInput(Snapshot)
      → ExecuteTransition → ASC→TryActivateWeaponAbility(Handle, ActivationContext)
    → 通用 Tag → 显式通用路由（闪避/交互/快捷栏）
```

所有分支都从同一 `HandleResolvedInputSnapshot` 进入：武器 Tag 交给 Coordinator，`Input.Dodge` 等通用 Tag 按明确 SpecHandle 激活并注册快照。任何 GA 都不能回退读取原始 InputAction 或 `GetLastMovementInputVector()`。

### Ability 分类与输入归属

| 类别 | 示例 | 授予方 | 输入绑定 |
|------|------|--------|----------|
| 核心能力 | 闪避、边缘跳越、交互 | ASC→CoreAbilities | InputComponent/Router→Resolved Snapshot→显式通用路由（EdgeVault 由组件事件触发） |
| 武器连招 | Y/B/RT/组合键 | 装备时 ASC→GrantWeaponAbilities | WeaponInputRouter→InputSnapshot→Coordinator |
| 特殊动作 | 探测、拍照等非武器操作 | 快捷栏手动分配 | 经快捷栏→UseAction→Ability；项目不设计钩爪 |
| 消耗品 | 喝药、投掷 | 快捷栏自动登记 | 经快捷栏→UseAction→Ability |

## UMHGZGameplayAbility — Ability 基类

```
UCLASS(BlueprintType, Blueprintable, Abstract)
class UMHGZGameplayAbility : public UGameplayAbility
```

所有动作 Ability 的 M1 基类，统一处理耐力消耗、冷却、输入标签、ActionToken、reservation 和幂等清理。

| 成员 | 类型 | Category | 默认值 | 说明 |
|------|------|----------|--------|------|
| InputTag | FGameplayTag | "Ability\|Input" | 空 | 绑定的输入标签（`Input.Weapon.Y`/`Input.Weapon.B`/`Input.Dodge`…） |
| StaminaCostPolicy | EAbilityStaminaCostPolicy | "Ability\|Cost" | None | None / Instant / PerSecond；不表达 Ability 生命周期 |
| StaminaCost | FScalableFloat | "Ability\|Cost" | 0 | 单次耐力扣除量（闪避/单次攻击）。`ActivateAbility` 时一次扣除：`Cost × StaminaDeductionRate` |
| StaminaCostRate | FScalableFloat | "Ability\|Cost" | 0 | PerSecond 速率；Task 按 `Rate × ConsumptionRate × 实际经过时间` 结算 |
| WeaponResourceCosts | TArray\<FWeaponResourceCostSpec\> | "Ability\|Cost" | 空 | 由当前 ResourceProvider 解释并预留的离散/数值武器资源成本 |
| CooldownDuration | FScalableFloat | "Ability\|Cooldown" | 0 | 大于 0 且 CooldownTag 有效时写入 HasDuration GE Spec |
| CooldownTag | FGameplayTag | "Ability\|Cooldown" | 空 | 由 Cooldown GE 的 DynamicGrantedTags 持有，不使用 Loose Tag |
| MaxCorrectionAngle | float | "Ability\|Correction" | 30.0 | 攻击已 Confirm、Montage 播放前的最大入口修正角度（以角色朝向为基准，直接转向冻结摇杆方向）。0=禁止修正 |
| AudioIdentityTag | FGameplayTag | "Ability\|Audio" | 空 | 挥刀风声身份标签（如 `Audio.Swing.LS_VerticalSlash`）。GA 蓝图必配——`ActivateAbility` 时以此为 Key 查 `WeaponDef.SwingSoundOverrides`，命中则覆盖 `DamageConfig.SwingSound`。不同招式用不同 GA 蓝图→不同 Tag→不同音效，武器覆盖是可选增量 |

> **FScalableFloat：** 所有 `FScalableFloat` 字段统一关联全局 CurveTable `DT_AbilityScalars`，Ability 只需指定行名（RowName）。`StaminaCost` 是 GA 的实际耐力扣除量；`FComboTransition::StaminaRequired` 是协调器的匹配门槛，不负责扣耐。

### 冻结后的成本合同

`EAbilityStaminaCostPolicy = None / Instant / PerSecond`。Ability 是否长期存在只由其 EndAbility 时机决定，与成本策略无关。

- `Instant`：`CommitAbility` 使用有效的 Cost GE 和 Cooldown GE；Commit 失败不播放 Montage、不提交连招状态。
- `PerSecond`：Commit 后启动带 Ability 所有权的 `UAbilityTask_MHGZStaminaDrain`，按真实经过时间用有效 Instant Cost GE 扣耐；不足支付下一跳时取消 Ability。
- `None`：协调器等长期 Ability 使用；不扣耐、不伪造 Cooldown Loose Tag。
- 武器专属资源使用 reservation：`TryReserveCosts(ActionToken)` → `CommitAbility` → 失败 `ReleaseReservation` / 成功 `ConsumeReservedCosts` → Confirm。成功后的 Consume 保证不失败，reservation 期间禁止资源重入改变被锁身份。所有 reservation、任务、GE Handle、Timer 和临时 Tag 由同一个幂等清理出口回收。

M1 当前 Ability 已使用 `TArray<FWeaponResourceCostSpec>`：`CostType` 是资源组件解释的 GameplayTag，`Amount` 是可缩放数值；空数组表示无武器成本。旧 `bRequiresWeaponResource/WeaponResourceCost` 运行时路径已删除。通用层只调用当前 Resource 的 Reserve/Release/Consume 接口，不假设资源一定是单个 float。

### 核心方法（覆写）

- `bool CanActivateAbility(...) const override`
  - 输出：是否可激活。
  - 作用：只做无副作用的耐力、Cooldown 和 Tag 预检；等级通过 Handle+ActorInfo 查询，兼容 GAS 在 CDO 上执行 CanActivate。武器资源最终在动作实例中 reservation。

- `void ActivateAbility(...) override`
  - 作用：验证当前 RuntimeToken/依赖，依次完成 Resource reservation + GAS Commit；成功后消费 reservation、Confirm、注册 Action，并根据 CostPolicy 启动持续任务。无效 Cost Spec 与 Loose Cooldown 路径已删除。

- `void EndAbility(...) override`
  - 作用：幂等停止持续任务、释放未消费 reservation、注销 Montage/Action、释放本 Ability 的 Ledger Token，并以完整 ActionToken 通知 Coordinator；Cooldown GE 不在这里手工移除。

## UMHGZAttackAbility — 攻击 Ability 中间层

```
UCLASS(BlueprintType, Blueprintable, Abstract)
class UMHGZAttackAbility : public UMHGZGameplayAbility
```

继承 `UMHGZGameplayAbility`，统一封装所有攻击类 Ability 的**碰撞检测**、**命中过滤**、**伤害 GE 构造与 Apply**。蓝图子类只需配置参数，不写逻辑代码。攻击 Montage 统一启用 `bEnableRootMotion=true`——动画数据驱动位移，CMC 移动输入被 RootMotion 覆盖（见 §移动实现-RootMotion）。

### 核心配置结构

#### FWeaponTraceRegion — 单个有效攻击区域

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| StartSocketName | FName | 空 | 有效攻击区域起点 Socket 或骨骼；留空时退化为 EndSocketName 的单点 Sweep |
| EndSocketName | FName | 必填 | 有效攻击区域终点 Socket 或骨骼 |
| Radius | float | 14 | 球形 Sweep 半径（厘米） |
| MaxSampleSpacing | float | 20 | 棍身相邻采样点允许的最大间距；运行时还会限制为不超过 `2 × Radius`，避免采样球之间出现空洞 |
| MaxSampleCount | int32 | 16 | 单个区域最多使用的空间采样点数，范围 1~32；不足以覆盖区域时输出警告 |
| MaxAngularStepDegrees | float | 15 | 一帧内旋转超过该角度时增加时间子步，降低高速旋转沿弧线漏判 |

同一个碰撞窗口可以配置多个 `TraceRegions`。虫棍按握持点把棍身拆为前、后两个区域：只用前半段的招式启用 Front，只用后半段的招式启用 Rear，整棍横扫则同时配置两个区域。当前虫棍的 `Root` 骨骼就在模型中点/握持点，因此它可直接作为两个区域的共同 Start，只需在前后棍尖各建一个 Tip Socket；这些名称只描述有效伤害区边界，不承担武器附着功能。

#### FAttackCollisionConfig — 单段碰撞窗口配置

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| TraceMeshComponentTag | FName | WeaponTrace | 优先定位参与轨迹检测的武器 SkeletalMeshComponent |
| TraceRegions | TArray\<FWeaponTraceRegion\> | 空 | 最终轨迹区域数组，可按招式选择前段、后段或两者 |
| CollisionChannel | TEnumAsByte\<ECollisionChannel\> | GameTraceChannel1 | 碰撞通道（默认 Weapon 通道） |
| HitzoneQueryTag | FGameplayTag | 空 | 限定碰撞仅检测带此 Tag 的组件。空=不限制（检测所有碰撞） |
| bDrawDebug | bool | false | 绘制 Sweep 轨迹用于校准 |

`TraceEndSocketName`、`AttachSocketName`、旧 `TraceStartSocketName/Shape/ShapeExtent/TraceSampleCount` 只属于旧原型。2026-08-11 审计决定不转存两个旧攻击 GA；M2 删除兼容读取，E3 删除旧 GA，M4 移除序列化壳，E4 在全新 GA 的 `TraceRegions` 中按最终动作重新配置。运行时不保留“TraceRegions 为空就读旧字段”的兼容分支。

#### FAttackDamageConfig — 单段伤害配置

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| DamageEffectClass | TSubclassOf\<UGameplayEffect\> | UMHGZDamageGameplayEffect | Demo 通用原生伤害 GE；特殊来源仍复用同一 ExecCalc，不以空类静默跳过 |
| MotionValue | FScalableFloat | 0 | ★ 动作值（倍率）。伤害段必须显式设置为正值；0 表示合法的无伤害命中，缺失不回退到 1 |
| BaseStaggerValue | FScalableFloat | 0 | ★ 基础破坏值。参与硬直计算：`Stagger = BaseStaggerValue × StaggerMultiplier × MonsterHitzoneStaggerRate`。为 0 则该段不造成硬直 |
| KnockbackAngle | float | 0 | 击退方向（相对攻击者朝向，0=前方，180=击飞） |
| KnockbackForce | FScalableFloat | 0 | 击退力度 |
| HitStaggerTag | FGameplayTag | 空 | 硬直等级（`Combat.Stagger.Light` / `Medium` / `Heavy`） |
| bUseHitzoneDefense | bool | true | 是否按命中部位的 `DefenseMultiplier` 修正伤害。怪物侧每个 hitzone 碰撞体持有 `DefenseMultiplier`（肉质）和 `StaggerRate`（硬直肉质） |
| bRequiresHitToContinue | bool | false | 招式内空挥截断：为 true 时，本段碰撞窗口结束后检查该窗口自己的 `HitTargets`——若该段空挥，提前 `EndAbility`。同时也是 `ShouldContinueAfterHit()` 的默认判断依据 |
| OnHitSelfEffect | TSubclassOf\<UGameplayEffect\> | nullptr | 命中时对自身施加的 GE（如虫棍三灯）。仅首次命中时 Apply 一次 |
| HitCueTag | FGameplayTag | 空 | 物理命中 Cue Tag。目标链路把它写入 HitFeedbackResult，由 Router 显式执行；DynamicAssetTags 只保留调试镜像 |
| ElementalCueTag | FGameplayTag | 空 | 元素附魔命中 GC 标签（可选——留空则无元素特效）。如 `GameplayCue.Hit.Fire` |
| CameraShakeClass | TSubclassOf\<UCameraShakeBase\> | nullptr | 震屏类（按武器种类选不同类；留空则无震屏）。在 `ApplyDamage` 中通过 `ClientStartCameraShake` 执行 |
| CameraShakeScale | float | 0.0 | 震屏强度倍率（0.0~1.0）。同武器不同招式改此值，不产生新蓝图 |
| HitStopBase | FScalableFloat | 0 | 卡肉时长（秒）。当前直接作为 Timer 时长，不乘 MotionValue 或肉质 |
| SwingSound | TObjectPtr\<USoundBase\> | nullptr | 招式挥刀风声（必配——每段攻击的默认风声）。武器可通过 `SwingSoundOverrides` 按 `AudioIdentityTag` 覆盖 |

> **震屏/卡肉归 Ability 层：** `CameraShakeClass`/`CameraShakeScale`/`HitStopBase` 已在 `ApplyDamage` 中执行；按 MotionValue/肉质缩放卡肉是保留方案，当前未实现。GameplayCue 尚未接通。

#### FAttackSegmentConfig — 单段攻击配置（碰撞 + 伤害 + 多跳）

```
USTRUCT(BlueprintType)
struct FAttackSegmentConfig
```

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| Collision | FAttackCollisionConfig | — | 本段碰撞窗口参数（轨迹区域/通道/过滤） |
| Damage | FAttackDamageConfig | — | 本段伤害参数（动作值/破坏值/击退） |
| MultiHitPolicy | EMultiHitPolicy | PerContactTrace | `PerContactTrace` 只在实际 Sweep/Overlap 仍接触时重复伤害；`LockedTargetTicks` 才允许命中后锁定目标定时跳伤 |
| MultiHitCount | int32 | 1 | 每目标在该窗口内最多结算次数；1=普通单次命中 |
| MultiHitInterval | float | 0.1 | 同目标两次结算的最小间隔；不能只靠 Timer 对已离开区域的缓存目标继续伤害 |
| LockedTargetMaxDistance | float | 0 | 仅 LockedTargetTicks 使用；每跳验证攻击者到原 Hitzone 的距离，0 禁止配置该策略 |
| MaxWarpAngle | float | 30.0 | 为将来**特殊动作**的段内 MotionWarping 预留的旋转上限；与入口 `MaxCorrectionAngle` 独立。当前通用 `UMHGZAttackAbility` 没有读取它，也没有为普通攻击建立 WarpTarget，因此编辑器填写它不会产生运行时效果。待某个特殊 GA 显式实现其目标、Warp Notify 与消费逻辑后，才可为该 GA 启用；0 表示该特殊段不做旋转 Warp。 |

### 攻击 GA 成员

所有离散武器动作 GA 固定为 `InstancedPerExecution`。激活实例由 `FWeaponActionToken{RuntimeToken, AbilityHandle, ActivationSequenceID, AbilityInstance}` 唯一标识；Montage 开始后另以 `FWeaponMontageRegistration{ActionToken, Mesh, MontageInstanceID}` 注册。这样 Confirm 不依赖尚未创建的 Montage 实例，同一个 AbilityClass/Spec 仍可在旧实例 BlendOut 前启动下一实例而不共享运行时窗口。

项目结束原因不修改 UE 原生 `EndAbility` 签名。通用动作基类以 `RequestEndAction(Reason)` 首次保存原因并调用原生 Cancel/End；`EndAbility` 统一读取该原因。后续重复终止请求不覆盖首个原因，Superseded 因而可以被精确记录和验证。

| 成员 | 类型 | Category | 默认值 | 说明 |
|------|------|----------|--------|------|
| AttackSegments | TArray\<FAttackSegmentConfig\> | "Attack" | 空 | ★ 多段攻击配置——每段独立配置碰撞 + 伤害 + 多跳。替代原来分离的 `CollisionConfigs` + `DamageConfig`，解决非数组成员（Damage/MotionValue/Stagger 等）无法随段变化的问题 |
| MaxCorrectionAngle | float | "Attack\|Correction" | 30.0 | ★ 攻击已 Confirm、Montage 开始前的最大入口修正角度（度）。只读冻结 `Input.WorldDirection`；偏离 ≤ 此值时直接设置 Actor Yaw。它不读取实时摇杆、不创建普通攻击 WarpTarget。特殊段内 MotionWarping 若未来实现，由该特殊 GA 自己消费 `FAttackSegmentConfig::MaxWarpAngle`。0=禁止修正 |
| ActiveCollisionWindows | TMap\<int32, RuntimeState\> | — | 空 | 每个 ConfigIndex 独立保存轨迹区域、已命中目标和多跳 Timer；不同段的 Notify 窗口可以重叠运行 |
| CurrentSegmentIndex | int32 | — | 0 | 最近开始或结束判定的段索引，仅用于兼容 `ShouldContinueAfterHit()` 覆写 |
| bHasHitThisActivation | bool | — | false | 本次 GA 激活后是否已有命中。用于首次命中时触发一次性逻辑（通知协调器 + Apply OnHitSelfEffect），避免多段/多怪重复触发 |

### 关键方法（覆写/新增）

- `void ActivateAbility(...) override`
  - 目标作用：从 `FWeaponAbilityActivationContext` 取得 SourceState/InputSnapshot/RuntimeToken/ActivationSequence → 建立 ActionToken 候选 → Resource reservation + `CommitAbility` → 消费 reservation → 回执 Coordinator Confirm。只有 Confirm 接受后才申请 `Combat.State.Attacking` 所有权 Token，并在播放 Montage 前以冻结 `Input.WorldDirection` 执行一次入口方向修正：水平输入有效、`MaxCorrectionAngle>0` 且当前 Actor Yaw 与目标 Yaw 差值不超过阈值时，直接 `SetActorRotation`。普通攻击不创建 `AttackDirection_<...>` WarpTarget，也不读取实时摇杆；Montage 启动后把 Mesh+MontageInstanceID 注册到 RuntimeHost。只有某个特殊 GA 显式拥有目标对齐需求时，才在其自己的合同中创建并清理唯一 WarpTarget。Commit/Confirm 前不改状态、不授予 Tag、不播放动画。

- `void EndAbility(...) override`
  - 目标作用：**按本次 ActionToken 幂等清理**：注销 Montage/Notify Registry → 关闭本实例的 Collision/Combo/DodgeAccept/Counter/Movement Token → 若本动作属于显式 MotionWarp 特殊动作则移除其自己拥有的 WarpTarget → 释放自己拥有的 Attacking/临时 Tag → 释放未消费 reservation → 携带 ActionToken 和 EndReason 通知 Coordinator → 清理 Montage/任务。普通攻击没有 WarpTarget 可移除。`Superseded` 只结束精确旧实例；迟到 End 不能重置新状态。

- `void EnableCollision(int32 SegmentIndex = 0)`
  - 输入：段索引。
  - 作用：为该段建立独立运行时窗口，按组件 Tag/Socket 找到轨迹 Mesh，缓存每个 Region 的前一帧起止点并立即执行首帧零距离 Sweep。当前不会创建临时碰撞组件。
  - 后续判定：`AnimNotifyState_AttackCollision::NotifyTick` 每帧调用 `TickCollision(SegmentIndex, DeltaSeconds)`；每个 Region 根据长度和半径自动布置空间采样点，并根据帧间旋转角增加时间子步，在上一帧到当前帧的旋转弧线上执行球形 `SweepMultiByChannel`。
  - **同帧部位选择：** 汇总该窗口所有 Region、空间采样和时间子步的命中；同一怪物只结算归一化帧时间最早的 Hitzone，随后写入该窗口自己的去重表。
  - **多跳伤害（MultiHitCount>1）：** 默认 `PerContactTrace` 由后续真实 Sweep/Overlap 触发并按目标记录 LastHitTime/Count。只有显式 `LockedTargetTicks` 才启动 Timer，且每跳必须验证弱引用、存活、Hitzone 和最大距离。当前代码仍是无条件伤害缓存目标，接入完整 Demo 前需按此目标修订。
  - 调用方：`UAnimNotifyState_AttackCollision→NotifyBegin` 先由 Mesh+MontageInstanceID 解析本 ActionToken，再调用该 AbilityInstance；不得遍历全部 Active Ability。

- `void DisableCollision(int32 SegmentIndex = INDEX_NONE)`
  - 作用：只关闭指定段并清除该窗口的 `MultiHitTimer`；`INDEX_NONE` 关闭全部窗口。若指定段要求命中但其 `HitTargets` 为空，则调用 `ShouldContinueAfterHit()`，返回 false 时提前结束。
  - 调用方：`UAnimNotifyState_AttackCollision→NotifyEnd` 只调用相同 ActionToken；旧 Montage 的 NotifyEnd 不得关闭新动作窗口。

- `void ProcessSweepHit(const FHitResult& Hit, int32 SegmentIndex)`
  - 当前过滤：忽略自身和已命中 Actor；命中组件必须是 `UMHGZMonsterHitzoneComponent`；配置了 `HitzoneQueryTag` 时要求 Exact Match。当前没有队友、无敌或死亡过滤。

- `void ApplyDamage(const FHitResult& Hit, int32 SegmentIndex)`
  - 输入：真实 Sweep HitResult 和段索引；不得只保存 BoneName 后重建命中点。
  - 作用：
    1. 调用 `MakeDamageSpec(Hit, SegmentIndex, AttackInstanceID)` 构造 GE Spec
    2. `SourceASC→ApplyGameplayEffectSpecToTarget(Spec, TargetASC)`；AttributeSet 结算后由 HitFeedbackRouter 请求 Cue、伤害数字、镜头和可叠加卡肉 Token
    3. **首次命中时（`bHasHitThisActivation==false`）：** 设 `bHasHitThisActivation=true` → 携带完整 ActionToken 通知协调器 `OnAttackHit`；段 `Damage.OnHitSelfEffect` 非空时 Apply 到自身 ASC
    4. 多段碰撞/多怪物场景下，后续命中跳过步骤 3。一次 GA 激活的全部段、目标和跳伤共享稳定 `AttackInstanceID`，但每次结算携带各自真实 HitResult；逐目标/逐跳去重由 Attack 窗口状态负责，不重复触发首次命中逻辑
  - M2 已删除 Attack 直接写 `CustomTimeDilation`/自行震屏的路径；结算后统一交给 FeedbackRouter 与 HitStopController。
- `FGameplayEffectSpecHandle MakeDamageSpec(const FHitResult& Hit, int32 SegmentIndex)`
  - 输入：真实 HitResult 和段索引。
  - 输出：构造好的 GE Spec。
  - 作用：
    1. `ASC→MakeOutgoingSpec(AttackSegments[SegmentIndex].Damage.DamageEffectClass)`
    2. 写入 `Damage.MotionValue` 与 `Damage.BaseStagger` SetByCaller；最终伤害由 `UMHGZDamageExecCalc` 计算
    3. 将原始 HitResult、HitStaggerTag、HitCueTag、ElementalCueTag 和 AttackInstanceID 写入项目自定义 GameplayEffectContext
    4. 虫棍空中攻击写入激活时快照的 `Damage.DanceMultiplier`；其他来源默认为 1
    5. DynamicAssetTags 可保留用于调试/查询，但不负责触发 GameplayCue
    6. 旧 `DamageSetByCallerTag` 在资产迁移后删除；Knockback、SwingSound 和 `MaxWarpAngle` 若不进入 Demo 最终数据流则不得作为假可用字段保留

- `bool ShouldContinueAfterHit() const` (BlueprintNativeEvent)
  - 输出：当前碰撞窗口命中后，是否继续下一段碰撞窗口。
  - 默认实现：若 `AttackSegments[CurrentSegmentIndex].Damage.bRequiresHitToContinue` 且当前 Index 对应窗口的 `HitTargets` 为空 → return false；否则 return true。
  - **蓝图覆写场景——登龙剑：** 覆写此函数 → 读 ASC 的武器资源（气刃槽色阶）→ 若色阶 < 白 → return false → `EndAbility` 提前，第二段不播放。
  - 调用时机：`DisableCollision` 内，下一段 `EnableCollision` 之前。

- `TryReserveWeaponResourceCosts` / `ReleaseReservation` / `ConsumeReservedCosts`（目标）
  - 输入：本 Ability 的 `FWeaponResourceCostSpec` 数组。
  - 作用：通过 `IWeaponResourceProvider` 交给当前 Resource 解释 CostType，锁定精确资源身份并按 GAS Commit 结果释放或消费。旧 `CheckWeaponResourceForAbility + float WeaponResourceCost` 在 M1 删除。

### Ability 继承层级

当前层级：`UGameplayAbility` → `UMHGZGameplayAbility` → `UMHGZAttackAbility` / `UMHGZDodgeAbility`，虫棍再由 `UMHGZInsectGlaiveAbility` 继承攻击基类。M4-A.4 已在虫棍分支新增 `UMHGZDrawAttackAbility`，收刀 Y 与收刀 RT 都通过它的精确 `DrawCommit` 收敛“实际拔刀”姿态入口；`UMHGZEdgeVaultAbility`、`GA_Sprint`、`GA_Heal` 为规划。

### GameplayCue 集成 — HitFeedbackResult

`MakeDamageSpec` 同时把 Cue 写入自定义 Context 与调试 DynamicAssetTags；运行时表现只由结算结果显式路由：

| 步骤 | Tag 来源 | 说明 |
|:--:|------|------|
| 1 | GameplayEffectContext | 保存真实 HitResult、物理/元素 CueTag、来源动作和 AttackInstanceID |
| 2 | ExecCalc/AttributeSet | 得到 FinalDamage、bCrit 和硬直结果，组成 `FMHGZHitFeedbackResult` |
| 3 | Target `UMHGZHitFeedbackRouter` | 显式 Execute 物理/元素/Crit Cue；用真实 ImpactPoint/Normal |
| 4 | `GameplayCue.Hit.DamageNumber` | Router 显式设置 `Parameters.RawMagnitude=FinalDamage` 后执行 |



### 挥刀风声 — GA 注入 Notify 模式

GA 激活时解析最终 `SwingSound`，直接写入 Montage 上的 `AnimNotify_SwingSound` 实例——Notify 自持数据，不经过 Character。

| 步骤 | 位置 | 说明 |
|:--:|------|------|
| 1 | `UMHGZAttackAbility::ActivateAbility` | `FinalSound = WeaponDef.SwingSoundOverrides.Find(AudioIdentityTag) ?? DamageConfig.SwingSound`——武器覆盖优先 → 招式默认兜底 |
| 2 | 同上 | `Montage→AnimNotifies` 中查找首个 `UAnimNotify_SwingSound` 实例 |
| 3 | 同上 | `NotifyInstance→Sound = FinalSound`——GA 直接注入。AnimNotify 不查任何外部对象 |
| 4 | `UAnimNotify_SwingSound::Notify` | `PlaySoundAtLocation(this→Sound)`——读自己，零查找 |

> **单人假设：** 当前版本仅单机。Montage 资产按武器种类创建（如太刀所有 GA 引用太刀专属 Montage 集），不存在不同 GA 共享同一 Montage 实例的竞态。多人化时需评估 Montage 实例化策略。


## 武器 Ability 基类分化（规划；当前仅有虫棍基类）

怪猎武器资源系统极其多样，无法用统一基类概括。每种武器从 `UMHGZAttackAbility` 派生一个**武器专属中间类**，持有该武器的资源组件引用并覆写关键钩子：

```
UMHGZAttackAbility                         ← 碰撞+伤害+方向修正（通用）
  ├── UMHGZLongSwordAbility                ← ★ 太刀: 气刃槽Level+Amount+衰减
  │     ├── GA_LS_Slash_01                 ← 蓝图: 配AttackSegments
  │     ├── GA_LS_ForesightSlash           ← 见切(两段MotionWarping+判定)
  │     └── GA_LS_HelmBreaker              ← 登龙(三段: 突刺+起跳+下劈, 多跳)
  │
  ├── UMHGZInsectGlaiveAbility             ← ★ 虫棍: 三灯+猎虫耐力
  │     └── UMHGZDrawAttackAbility          ← M4-A.4: 拔刀 Commit+姿态切换
  │           ├── GA_IG_Draw                ← 仅拔刀，无 AttackSegment
  │           └── GA_IG_DrawSlash           ← 前+Y 拔刀攻击
  ├── UMHGZChargeBladeAbility              ← ★ 盾斧: 瓶计数+盾充能
  └── UMHGZSwitchAxeAbility                ← ★ 斩斧: 充能槽
```

每个武器派生 Ability 通过 `IWeaponResourceProvider` 取得当前 `UMHGZWeaponResourceComponent`，覆写资源门控和招内命中派生。武器派生层管 GA **激活后**的内部逻辑，`FComboTransition` 管**激活前**的匹配条件；通用基类不持有虫棍类型指针。

> **见切（ForesightSlash）：** 段0 `AttackSegments[0].Damage` 全为 0（不产生伤害），闪避判定采用**碰撞通道 Overlap 检测**——`DodgeWindow` 将玩家对 MonsterAttack 通道从 Block 切换为 Overlap（攻击穿透无伤害），`ForesightJudge` 监听玩家 CapsuleComponent 的 `OnComponentBeginOverlap`（与怪物攻击碰撞体重叠即 `bDodgeSuccessful=true`）。Pawn 通道始终 Block——后撤时被怪物身体挡住即失败。段1 走标准 `AttackCollision → ApplyDamage`。

### 见切（ForesightSlash）流程

`ActivateAbility`（`MaxCorrectionAngle=180°`）：段0 后撤（Warp Target=反方向×500, `MaxWarpAngle=180°`）。Montage 内并行两个 AnimNotifyState：

| NotifyState | 作用 | 细节 |
|------|------|------|
| `DodgeWindow` | 碰撞通道切换 | 玩家 Capsule 对 **Weapon** 通道 = Ignore（免疫伤害），对 **MonsterAttack** 通道 = **Overlap**（穿透但产生重叠事件），**Pawn** 始终 Block（被怪物身体挡住） |
| `ForesightJudge` | 见切成功判定 | 订阅玩家 CapsuleComponent→`OnComponentBeginOverlap`，过滤重叠组件是否带 `MonsterAttack` 相关 Tag → 是则 `bDodgeSuccessful=true` |

**Montage 时间轴约束（关键）：**

```
见切 Montage 时间轴（动画师必须遵守的区间约束）：
├─ 0.00s ─ 0.40s: DodgeWindow + ForesightJudge（段0, 碰撞通道切换+重叠监听）
│   └─ 0.40s: DodgeWindow.NotifyEnd → 恢复 Weapon 通道 = Block
│       ★ 必须在此之后至少留 1 帧间隙（~0.016s@60fps）
├─ 0.42s ─ 0.70s: AttackCollision(ConfigIndex=1)（段1, 标准攻击碰撞）
│   └─ 此时 Weapon 通道已恢复 Block，Sweep 正常工作
└─ 0.70s ─ 1.00s: 收招 Blend
```

> ⚠️ **DodgeWindow.NotifyEnd 必须在 AttackCollision.NotifyBegin 之前至少 1 帧**，确保段 1 的 Sweep 碰撞检测不受段 0 通道切换影响。此约束由动画师在 Montage 中拖拽区间保证。

**段方向修正——取最晚输入：**

- **段 0（后撤）**：方向在 `ActivateAbility` 时确定——取按键瞬间的摇杆反向作为后撤方向。后撤是防御性动作，方向不应中途改变。
- **段 1（回砍）**：方向在 `EnableCollision(SegmentIndex=1)`（即 `AttackCollision::NotifyBegin`）时读取——此时是段 1 动画实际开始前的最后一刻，玩家可在后撤期间调整摇杆方向，实现"后撤向左、回砍向右"的灵活操作。

> **通用规则（多段 MotionWarping）：** 段 0 方向取 `ActivateAbility` 时的摇杆输入；后续每段的方向取该段 `EnableCollision` 时的摇杆输入（若该段无碰撞则取最近一次 `EnableCollision` 的缓存）。这保证每段都使用"尽可能晚"的输入方向。

→ 段 1 回砍命中后：若 `bDodgeSuccessful`：**恢复固定量气刃槽**（如 +50% `Amount`，GA 内部直接调用 `URes_LongSword::Restore(FixedAmount)`，不经过 ExecCalc——与 AttributeSet 无关）+ `GrantedTags={Combo.Branch.ForesightSuccess}`（大回旋可派生）；否则仅伤害。

> **设计理由：** 不再依赖"受击→HitStagger 事件"来判定见切成功——改为几何重叠检测。只要玩家闪避路径与怪物攻击碰撞体有空间交集即成功，攻击本身穿透不造成伤害。这避免了"无敌帧阻止受击事件→见切永远无法成功"的悖论。

### 登龙（HelmBreaker）流程

段0 突刺（`AttackCollision, ConfigIndex=0`）→ 首次命中时 `ShouldContinueAfterHit()` 检查气刃槽 ≥ 白 → 是则播段1 起跳，否则播后摇 `EndAbility`。段1 起跳（MotionWarping 上跳，无碰撞）。段2 下劈（`AttackCollision, ConfigIndex=1, MultiHitCount=7, MultiHitInterval=0.1s`）→ 每 0.1s `ApplyDamage` 共 7 次。`ShouldContinueAfterHit()` 是**招内派生**（同一 GA 内），不同于连招表的**跨 GA 派生**（`GrantedTags`）。

### 招内分支：命中/未命中派生——GA 与 Montage 的职责划分

#### 架构原则

```
┌─────────────────────────────────────────────────────────┐
│                    Montage 负责                          │
│  动画时间线编排（Section 分段、Blend 曲线、Notify 位置）  │
│  不包含任何条件判断逻辑                                  │
├─────────────────────────────────────────────────────────┤
│                    GA 负责                               │
│  条件判断（命中？未命中？资源够？）、分支决策、           │
│  JumpToSection 指令、Warp Target 设置                    │
└─────────────────────────────────────────────────────────┘
```

#### 为什么不能放 Montage？

| 原因 | 说明 |
|------|------|
| Montage Section 链接是**静态**的 | 只能在编辑器中预设"Section A 播完→切 Section B"，无法执行时判断"这一帧有没有命中怪物" |
| AnimNotify 只能"通知"，不能"决策" | Notify 上报命中事件，但命中后需查 ASC Tag、资源组件状态、连招表——只有 GA 能访问这些 |
| 项目已有反例 | 见切 `ForesightJudge` Notify 只负责**记录** `bDodgeSuccessful=true`，实际分支决策在 GA 的回调中读取该标记 |

#### 命中分支的标准实现模式

```
Montage 结构（纯动画分段，不含逻辑）：
  Section "Approach"  ← 含 MotionWarping Notify + AttackCollision Notify
  Section "OnHit"     ← 命中追击动画
  Section "OnMiss"    ← 未命中收招动画

GA 中的分支逻辑：
  ActivateAbility()
    ├── 设置 Warp Target
    ├── PlayMontageAndWait("Approach")
    └── 注册回调

  命中回调（碰撞系统检测到命中时）：
    ├── 查 ASC Tag / 资源组件 → 判断是否满足派生条件
    ├── 是 → Montage_JumpToSection("OnHit")
    └── 否 → 继续等待

  Montage 播完回调（Approach 自然结束）：
    └── 中途未 JumpToSection → 自动进入 "OnMiss"
```

#### 与已有机制的关系

项目已有两套分支机制，高速突刺招式是 `ShouldContinueAfterHit()` 的扩展用法：

| 分支类型 | 对应方法 | 作用层级 |
|----------|---------|----------|
| **招内派生**（同一 GA 内，命中→继续/命中→切段） | `ShouldContinueAfterHit()` + `JumpToSection` | 同一 GA 内部 |
| **跨 GA 派生**（一个 GA 结束→下一个 GA） | `FComboTransition::GrantedTags` + 协调器 | GA 之间 |

### Montage 资产策略——单 Montage（多 Section）vs 多 Montage

#### 决策对照

| 维度 | 单 Montage 多 Section | 多 Montage |
|------|:--:|:--:|
| **Section 间 Blend** | ✅ 内置，动画师拖拽 Blend 曲线 | ❌ 需代码 Stop→Play，有 1 帧空白或硬切 |
| **MotionWarping 连续性** | ✅ Warp Notify 跨 Section 自然结束 | ❌ Stop Montage 强制终止 Warp，角色可能瞬移 |
| **RootMotion 连续性** | ✅ 根骨骼位移跨 Section 累积 | ❌ 切 Montage 时根骨骼回参考位置 |
| **Notify 管理** | ⚠️ 所有 Notify 在同一轨道，需注意区间不重叠 | ✅ 各自独立，互不干扰 |
| **独立设置（RateScale/RootMotion 等）** | ❌ 所有 Section 共享同一 Montage 属性 | ✅ 每个 Montage 可独立配置 |
| **资产复用** | ❌ Section 不能跨 Montage 引用 | ✅ 收招动画可被多个 GA 共用 |
| **版本控制** | ⚠️ 单文件多人编辑易冲突 | ✅ 各文件独立 |

#### 选择规则

| 场景 | 推荐 | 理由 |
|------|:--:|------|
| 同一 GA 内，各段共享 RootMotion/MotionWarping 设置 | **单 Montage** | Section Blend 保证命中瞬间过渡流畅 |
| 需要 MotionWarping 驱动的位移跨段连续 | **单 Montage** | Stop Montage 会破坏 Warp 连续性 |
| 命中动画需跨招式/跨武器复用 | **多 Montage** | 避免资产冗余 |
| 不同段需要不同 `bEnableRootMotion` 设置 | **多 Montage** | 单 Montage 所有 Section 共享该属性 |
| 多人协作，不同动画师负责不同段 | **多 Montage** | 二进制文件合并冲突严重 |

> **项目推荐：** 高速突刺→命中/未命中分支、登龙剑、见切等招式均用**单 Montage 多 Section**——这些场景共享 RootMotion 设置、依赖 MotionWarping 连续性、命中瞬间需要平滑 Blend。

### 招式衔接动画——Montage Entry Section 模式

**设计原则：** 不改变“每个招式 = 一个 GA + 一个 Montage”的架构。衔接动画放在**下一个招式的 Montage 开头**作为 Entry Section，GA 激活时根据本次不可变 `FWeaponAbilityActivationContext.SourceState` 选择对应入口，不读取协调器随后可能变化的全局状态。

#### Montage Entry Section 结构

```
Montage_Slash_103（连续上捞）：
  ┌─────────────────────────────────────────────────────┐
  │ Section "Entry_From_Idle"       ← 从站立起手         │
  │ Section "Entry_From_Slash101"   ← 从袈裟斩衔接       │
  │ Section "Entry_From_Dodge"      ← 从回避恢复         │
  │ Section "Entry_Default"         ← 兜底（走 Inertialization 硬混）│
  │ Section "Attack"                ← 攻击本体（含碰撞窗口）│
  └─────────────────────────────────────────────────────┘
```

各 Section 之间用 Montage 内置的 `Automatic Section Transition` 或 GA 在 Entry 播完后 `JumpToSection("Attack")` 衔接。

#### GA 激活时选择入口

```cpp
// 在武器 GA（如 UGA_IG_Slash_103）的 ActivateAbility 中
void UGA_IG_Slash_103::ActivateAbility(...)
{
    // 由本次激活上下文取得来源状态；激活后保持不变
    const FName SourceState = GetWeaponActivationContext().SourceState;

    // 根据来源选择入口 Section
    FName EntrySection;
    static const TMap<FName, FName> EntryMap = {
        { "Idle",       "Entry_From_Idle"       },
        { "Slash_101",  "Entry_From_Slash101"   },
        { "Dodge",      "Entry_From_Dodge"      },
    };
    EntrySection = EntryMap.FindRef(SourceState, FName("Entry_Default"));

    // 从入口 Section 播放，播完自动进入 "Attack"
    PlayMontageAndWait(Montage, EntrySection);
}
```

> **Entry_Default：** 当 `SourceState` 无匹配时走此兜底 Section——通常是一个极短的过渡段或直接空 Section，依赖 UE5 内置 Inertialization 做骨骼惯性混合。

#### 哪些招式对需要专属衔接动画？

**判定标准：** 两个动画的首尾帧骨骼 Pose 差异是否大到 Inertialization 混合也会出现肉眼可见的跳帧。

| 条件 | 需要专属 Entry Section？ |
|------|:--:|
| 上一个招式的结束 Pose 和下一个招式的起始 Pose 差异大（如袈裟斩收刀在右上→上捞从左下起手） | ✅ 需要 |
| 空中动作落地→地面攻击（下落惯性姿势和站姿差异大） | ✅ 需要 |
| 快速连招之间（如横扫→二连斩），Pose 接近 | ❌ 走 Entry_Default + Inertialization |
| Idle 起手（已有专门的起手式动画） | ❌ Entry_From_Idle 本身就是起手动画 |

#### 与已有机制的关系

| 机制 | Montage 结构 | 触发方式 | 适用 |
|------|-------------|----------|------|
| **招内分支**（上节） | 单 Montage 多 Section | GA 内部 `JumpToSection`（命中/未命中） | 同一招式内的分支 |
| **Entry Section**（本节） | 每个 Montage 多个入口 Section | GA 激活时按 ActivationContext.SourceState 选 Section | 招式间的衔接过渡 |
| **Inertialization** | 不需要额外 Section | UE5 自动 | Pose 接近的招式间过渡 |

## 蓄力式攻击（规划）

蓄力全程在一个 GA 内闭环。激活输入的 `SourceControlTag + SequenceID` 写入 Ability 激活上下文；WeaponInputRouter 收到同一物理输入的 Completed 后，只把释放事件投递给持有该身份的 Ability。按住期间按曲线累积 `ChargeLevel`，释放后在同一 GA 内选择 Montage 和 DamageConfig。蓄力本身是否持续扣耐由 `EAbilityStaminaCostPolicy` 决定，不能再借用 `bIsContinuous` 表示“正在蓄力”。

> **取消规则：** 蓄力 GA 被受击/死亡取消时注销其输入身份；之后到达的 Completed 因 SequenceID 无有效订阅者而被丢弃，不会误释放其他正在运行的 Ability。

## 怪物攻击碰撞——部位胶囊体复用 + 通道切换（规划）

怪物身体各部位骨骼上始终挂着胶囊体，动画驱动跟随，无需临时创建碰撞体。

**部位胶囊体通道配置：**

```
每个部位 (Head/Tail/LeftClaw/RightWing...) 挂 UPrimitiveComponent：
┌──────────────────────────┬──────────────┬──────────────┐
│  通道                     │  常态(Hitzone)│  攻击窗口     │
├──────────────────────────┼──────────────┼──────────────┤
│  Weapon (玩家攻击检测)     │  Block ← 始终 │  Block       │
│  MonsterAttack (怪物攻击)  │  Ignore      │  Block ★     │
│  Pawn (物理阻挡)          │  Block ← 始终 │  Block       │
│  Visibility (射线/UI)      │  Ignore      │  Ignore      │
└──────────────────────────┴──────────────┴──────────────┘
★ 唯一在 NotifyBegin/NotifyEnd 之间切换的通道
```

- **Weapon 始终 Block**：玩家 Sweep 任何时候都能检测到部位
- **MonsterAttack 窗口内 Block**：仅攻击帧可命中玩家，收招自动 Ignore
- **Pawn 始终 Block**：物理推挤不变

**UAnimNotifyState_MonsterAttackCollision：**

| 步骤 | 操作 |
|------|------|
| NotifyBegin | 遍历本怪物所有参与此攻击的部位 Tag（策划在 NotifyState 上配置 `TArray<FGameplayTag>`）→ 每个部位设 MonsterAttack 通道 = Block → 可选：根据攻击类型临时叠加一个更大的胶囊体（如龙车全身判定） |
| NotifyTick | `SweepMultiByChannel(MonsterAttack)` 从各部位上一帧位置扫到当前帧 → 命中玩家 → 构造怪物伤害 GE Spec → Apply 到玩家 ASC |
| NotifyEnd | 恢复所有部位 MonsterAttack 通道 = Ignore → 移除临时叠加的碰撞体（若有） |



**边界情况：**

| 边界 | 处理 |
|------|------|
| 高速攻击穿透（如龙车） | 首帧 Sweep 从上一帧位置扫到当前帧位置，`FHitResult.Time` 升序取首个命中——高速也不会穿透 |
| 攻击范围大于部位胶囊体 | 部分招式（如翻滚碾压）临时叠加一个更大的 Box/Sphere 碰撞体，NotifyEnd 时移除 |
| 多部位同时攻击 | NotifyState 的 `AttackPartTags` 数组可配多个部位 Tag——如龙扫尾同时涉及 Tail1 + Tail2 + TailTip |
| 攻击被硬直/死亡打断 | **强制恢复机制**——怪物 GA EndAbility（被打断/取消/死亡）时遍历所有部位，强制设 MonsterAttack 通道 = Ignore。不依赖 NotifyEnd 被正常调用。实现：在 `AMHGZMonsterBase` 中提供 `ForceRestoreAllChannels()`，由 GA EndAbility、死亡流程、`BeginDestroy()` 三重调用——确保无论对象以何种方式销毁，通道都能恢复 |

## 武器资源子系统（部分实现；当前仅虫棍 C++ 骨架）

**UMHGZWeaponResourceComponent（基类，由 Character 的 WeaponRuntimeHost 持有）** — 装备时动态创建，切换武器/重生时通过统一生命周期清理。它可访问当前 Pawn、动画和世界 Actor，并通过接口取得 PlayerState ASC；持久装备数据仍留在 PlayerState。

| 成员/接口 | 类型 | 说明 |
|------|------|------|
| WeaponTypeTag | FGameplayTag | 关联武器种类 |
| TryReserveCosts(const FWeaponActionToken&, TConstArrayView\<FWeaponResourceCostSpec\>, OutReservation) | virtual bool | 无表现地锁定精确资源身份；CostType 由具体武器解释 |
| ReleaseReservation(const FWeaponResourceCostReservation&) | virtual void | GAS Commit/激活失败时幂等释放，不改变资源 |
| ConsumeReservedCosts(const FWeaponResourceCostReservation&) | virtual void | 成功 reservation 的同步、保证成功消费；完成后统一广播 |
| BuildResourceViewData() | virtual/接口 | 返回该武器 UI DTO；不假设所有武器只有 Current/Max float |
| ApplyEntryModifier / GetModifiedParam | 延期 | 当前多来源与目标参数过滤有缺陷，Demo 不得调用；完整装备词条阶段重新设计来源句柄 |
| PlayResourceSound(USoundBase* Sound) | void | 工具方法——子类在资源变化时调用，统一走 `UGameplayStatics::PlaySound2D`（UI 反馈用 2D 音效） |

> 基类不预设音效槽位——子类各自持有 `UPROPERTY` 音效成员，通过 `PlayResourceSound()` 播放。武器资源词条本轮明确延期；Demo 参数直接来自 CombatConfig。

### 各武器子类

| 子类 | 特有字段 | 特殊逻辑 | 音效成员 |
|------|----------|----------|----------|
| `URes_LongSword`（规划） | `ESpiritLevel Level`（无/白/黄/红）、`float Amount`、`FTimerHandle DecayTimer` | 击中回复量不同、等级随时间和命中升降、衰减 Timer | `GaugeFillSound` / `LevelUpSound`（白→黄→红） / `LevelDownSound` / `DepleteSound` |
| `URes_InsectGlaive`（部分实现） | 当前源码仍使用白/Yellow/红常量、TripleUp Handle 和猎虫耐力；目标设计改为白/Orange/红、CombatConfig 双红灯模式、舞踏和粉尘接口 | 当前状态机需按 [insect-glaive.md](insect-glaive.md) 修订；资源组件创建、猎虫 Spawn、GA/GE 资产仍未接线 | `ExtractCollectedSound` / `TripleUpActivatedSound` / `TripleUpExpiredSound` / `KinsectDepletedSound` |
| `URes_ChargeBlade`（规划） | `int32 PhialCount`(0~6)、`bool ShieldCharged`、`FTimerHandle RedShieldTimer` | 瓶被动不消耗、部分招式主动消耗、红盾有时限 | `PhialLoadSound` / `ShieldChargeSound` / `PhialBurstSound` / `OverheatSound` |
| `URes_SwitchAxe`（规划） | `float ChargeGauge`(0~1) | 连续值充能 | `GaugeChargedSound`（充能就绪） / `SwordModeActivateSound` / `SwordModeDeactivateSound` |

> 当前 `FWeaponResourceConfigRow/DT_WeaponResourceConfig` 尚未形成资产闭环。目标由 WeaponDefinition 引用 `UWeaponRuntimeDefinition`，统一提供 ResourceClass/InputProfile/CombatConfig/ResourceWidget；RuntimeHost 是唯一消费方。虫棍现有 Delegate 仍需在 M3/M6 接线。

## GA_Dodge — 翻滚/闪避 Ability（不进连招表）

```
UCLASS(BlueprintType, Blueprintable)
class UMHGZDodgeAbility : public UMHGZGameplayAbility
```

继承 `UMHGZGameplayAbility`，通过通用输入路由激活，**不进武器连招表**。它仍使用 ActionToken/Notify Registry 和 TagLedger，因此与攻击共享可靠的动作生命周期，但不共享 Combo State。

### 最终配置

- 最终数据型 `GA_Dodge` 的 `SheathedDodgeMontage`、`UnsheathedDodgeMontage` 表示各姿态的**前向**翻滚；`InputSnapshot.Direction=Forward` 或 `None` 都使用它们。两者可以引用同一 Montage。
- M4-A.3.1 在 `UMHGZDodgeAbility` 新增 `UnsheathedLeftDodgeMontage`、`UnsheathedRightDodgeMontage`、`UnsheathedBackDodgeMontage`（或等价的最终直接配置）。它们只允许持刀 Left/Right/Back 使用；收刀 Left/Right/Back 直接拒绝。
- 激活时先由冻结姿态和冻结方向得到 `FDodgeSelection{Montage,bAllowMoveExit}`，而不是在 Montage 播放中重新读取方向。规则如下：

| 激活姿态 | 冻结方向 | 选择结果 | 退出规则 |
|---|---|---|---|
| Sheathed | Forward / None | `SheathedDodgeMontage` | 可按实时输入走 IdleExit 或 MoveExit |
| Unsheathed | Forward / None | `UnsheathedDodgeMontage` | 可按实时输入走 IdleExit 或 MoveExit |
| Unsheathed | Left / Right / Back | 对应的持刀方向 Montage | 始终 IdleExit；不允许 MoveExit |
| Sheathed | Left / Right / Back | Reject | 不播动画、不扣耐 |

- 所有成功行复用同一 `Instant` 耐力成本。没有所需 Montage、AnimInstance/Section 无效、Commit 失败或表中 Reject 的行都不得产生耐力消耗。
- 旧方向 Montage Map 仅为资产迁移壳；运行时最多回退读取 `Forward/None`，不能承担 Left/Right/Back 的正式选择。
- 旧 `DT_WeaponDodgeConfig` 在 M0 清点、迁移后停止运行时读取，不保留 DataTable 与 RuntimeDefinition 两条路径。
- 耐力使用 `Instant` 成本策略；无敌帧只由 Montage 中 `AnimNotifyState_DodgeWindow` 定义。

### 执行流程

```text
HandleResolvedInputSnapshot(Input.Dodge)
  → 以 live RuntimeHost/ASC 与冻结 Snapshot 双重检查 Grounded、姿态、Dead/Hitstun/Knockdown
  → Attacking 时要求 Coordinator 当前精确 Attack ActionToken 自己持有 DodgeAcceptWindow
  → TryActivate 明确的 Dodge Spec，并注册完整 InputSnapshot
  → GA 取得 RuntimeToken/Sequence，建立 ActionToken；以冻结姿态+Input.Direction 选择一次 FDodgeSelection
  → 前向选择验证 DodgeCore/IdleExit/MoveExit；左右后选择只验证 DodgeCore/IdleExit，并标记禁止 MoveExit
  → 依赖、Montage、AnimInstance 任一无效：Reject/End，不修改 BlockMovement 或碰撞
  → Commit/动作登记成功后持有 Dodging、BlockMovement、MontageRootMotionOwner
  → 攻击取消路径 Prepare 精确旧攻击，再播放并登记新 Montage，成功后 Commit Superseded
  → Dodge GA 监听自身 MontageInstance 的 SectionChanged：先固定 Core -> IdleExit，再在实际进入 IdleExit 时读实时原始输入；仅前向变体有输入时立即跳 MoveExit 并释放自身 BlockMovement
  → BlendOut 释放 MontageRootMotionOwner；EndAbility 最后释放 Dodging
  → DodgeWindow 只解析本 ActionToken，缓存各通道原响应、取得 Invincible Token
  → 完成/BlendOut/Interrupted/Cancelled/Superseded 均走同一幂等 End
```

前向翻滚本体不再读取实时方向，也不由实时方向改轨；它只使用激活时已选出的前向 Montage。持刀左右后同样固定为激活时选出的方向 Montage，且完整动作保持移动锁直到结束。Character 在 BlockMovement 期间仍更新实时 `bHasInput/LastMovementInputDir`；只有允许 MoveExit 的前向选择在进入该阶段后才允许这些值影响角色朝向。

### 翻滚与连招的关系

- Idle 时可直接闪避；攻击中只有动作的 DodgeAcceptWindow Token 有效时可激活。
- 翻滚接受后由协调器精确结束旧 ActionToken，并以明确原因 Reset/交接，不依赖 SafetyTimer 才回 Idle。
- DodgeWindow 缓存并恢复 NotifyBegin 前每个被修改通道的 CollisionResponse；不得统一恢复成 Block。
- Ability End 兜底释放仍存活的 Invincible/DodgeAccept Token 和碰撞快照，即使 Montage 中断未触发 NotifyEnd。

取消动作（如虫棍收虫）仍可配置 `bRequiresComboWindow=false` 并要求 `Combat.State.DodgeAcceptOpen`，但它们必须通过自己的 Transition/ActionToken 执行，不能直接篡改 Dodge 实例。

## UMHGZEdgeVaultComponent — 边缘跳越组件（规划；当前关闭 Tick）

```
UCLASS(ClassGroup=(Movement), BlueprintType, meta=(BlueprintSpawnableComponent))
class UMHGZEdgeVaultComponent : public UActorComponent
```

挂载到 Character。Tick 中轮询 CMC 边缘状态 + 角色移动状态，满足条件时触发 `GA_EdgeVault`。

### 配置参数（BlueprintReadWrite，策划可在蓝图中调整）

| 成员 | 类型 | Category | 默认值 | 说明 |
|------|------|----------|--------|------|
| ForwardTraceDistance | float | "EdgeVault\|Trace" | 100 | 前向 Trace 距离（cm），从角色胯部高度向前探测边缘 |
| ForwardTraceHeight | float | "EdgeVault\|Trace" | 60 | 前向 Trace 起始高度（cm），约为胶囊体半高 |
| DownwardTraceDepth | float | "EdgeVault\|Trace" | 500 | 向下 Trace 深度（cm），验证下方有可落脚平台 |
| MinLandingDepth | float | "EdgeVault\|Trace" | 50 | 最小下落深度（cm）。低于此值视为平地/台阶，CMC 自动步下，不触发跳越 |
| MaxLandingDepth | float | "EdgeVault\|Trace" | 500 | 最大下落深度（cm）。超出此值视为悬崖，不触发跳越（防止摔死） |
| VaultCooldown | float | "EdgeVault\|Cooldown" | 0.5 | 触发冷却（秒），防止同一边缘反复触发 |
| BlockingTags | FGameplayTagContainer | "EdgeVault\|State" | `Combat.State.Hitstun, Combat.State.Knockdown, Combat.State.Dead` | ASC 中存在任一 Tag 时禁止触发 |

### 核心方法

- `virtual void TickComponent(float DeltaTime, ELevelTick, FActorComponentTickFunction*) override`
  - 作用：每帧执行检测链（全部通过才触发 `GA_EdgeVault`）：
    1. **冷却检查**：`VaultCooldown` 计时器是否到期。未到期 → return。
    2. **状态阻塞**：`ASC→HasAnyMatchingGameplayTags(BlockingTags)` → return。
    3. **移动状态**：角色是否正在奔跑（`ASC→HasMatchingGameplayTag(Combat.State.Sprinting)`）**或**正在闪避（`ASC→GetActiveAbilities` 中包含 `UMHGGZDodgeAbility`）。两者都不满足 → return。
    4. **边缘检测**：调用 `DetectLedge()`。未检测到边缘 → return。
    5. **触发**：`ASC→TryActivateAbilityByTag(Input.EdgeVault)` → 启动 `VaultCooldown` 计时器。

- `bool DetectLedge(FVector& OutLandingPoint) const`
  - 输出：通过 `OutLandingPoint` 返回预估着陆点。返回值为是否检测到有效跳越边缘。
  - 作用：
    1. 从角色位置 + `ForwardTraceHeight` 高度，沿 **角色当前水平速度方向**（`CMC→Velocity` 归一化后的水平分量）做前向 LineTrace（距离=`ForwardTraceDistance`，通道=`ECC_Visibility`，忽略自身）。
    2. 若前向 Trace 命中（前方有墙/地面）→ 未到边缘，return false。
    3. 若前向 Trace 未命中（前方无阻挡，已在边缘外）→ 从前向 Trace 终点向下做 LineTrace（距离=`DownwardTraceDepth`）。
    4. 若向下 Trace 命中，且命中点 Z 坐标与当前地面 Z 的差值在 `[MinLandingDepth, MaxLandingDepth]` 范围内 → 有效跳越边缘，`OutLandingPoint = HitResult.Location`，return true。
    5. 若向下 Trace 未命中（深渊）或深度超出范围 → return false。

- `bool IsSprintingOrDodging() const`
  - 输出：当前是否处于奔跑或闪避状态。
  - 作用：读 ASC 的 `Combat.State.Sprinting` Tag + 检查 ActiveAbilities 中是否有 Dodge Ability。

### 边缘检测可视化示意

```
角色 → 奔跑方向 →
  ┌──────┐
  │ 角色 │──前向Trace(100cm)──→ ✗ (无命中，已出边缘)
  └──┬───┘                        │
     │ 地面                        │ 向下Trace(500cm)
─────┘                             ↓
═════════════════════════════    ──── 平台
                               ✓ 命中！深度在 [50,500]cm → 触发跳越
                                    │
                                    │ 若深度 > 500cm → 悬崖，不触发
                                    ↓
                                ═══════════════════
```

### GA_EdgeVault — 边缘跳越 Ability

```
UCLASS(BlueprintType, Blueprintable)
class UMHGZEdgeVaultAbility : public UMHGZGameplayAbility
```

继承 `UMHGZGameplayAbility`。由 `UMHGZEdgeVaultComponent` 通过 `TryActivateAbilityByTag(Input.EdgeVault)` 触发。

| 成员 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| SprintVaultMontage | TSoftObjectPtr\<UAnimMontage\> | nullptr | 奔跑边缘跳 Montage（RootMotion 驱动前跃位移） |
| DodgeVaultMontage | TSoftObjectPtr\<UAnimMontage\> | nullptr | 翻滚边缘跳 Montage（RootMotion 驱动前跃位移） |

- `ActivateAbility`：判断触发来源（Sprint 或 Dodge）→ 选择对应 Montage → 播放 → Montage 播完 → EndAbility。
- `InputTag` 设为 `Input.EdgeVault`，供 `TryActivateAbilityByTag` 匹配。

### 翻滚边缘跳的特殊处理

`GA_Dodge` 执行中若 `UMHGZEdgeVaultComponent` 检测到边缘，Dodge 尚未结束时 `GA_EdgeVault` 被激活。由于 GAS 默认行为是激活新 Ability 时取消当前 Ability（取决于 `InstancingPolicy` 和 `ActivationGroup`），需配置 `GA_EdgeVault` 与 `GA_Dodge` 在同一 `ActivationGroup` 或使用 `bAutoCancelAbilities=false`，使 Dodge Montage 自然 Blend Out 到 Vault Montage。

## UMHGZInputComponent — 输入组件

```
UCLASS(ClassGroup=(Input), BlueprintType)
class UMHGZInputComponent : public UActorComponent
```

固定挂载到 PlayerController。它是 **IMC 与 Enhanced Input Binding 的唯一所有者**：添加/移除/切换 MappingContext，建立 Router 所需的原始 Action 绑定并保存 Handle。PlayerController 和 ASC 不再添加 Default IMC 或绑定 UInputAction；InputComponent 不持有连招、Ability 或虫棍规则。

| 成员 | 类型 | Category | 默认值 | 说明 |
|------|------|----------|--------|------|
| InputMappingContext | TArray\<UInputMappingContext\> | "Input" | 空 | 默认 IMC 列表（BeginPlay 时添加到 EnhancedInputSubsystem） |

### 核心方法

- `void InitializeInput(APlayerController* PC)`
  - 输入：PlayerController。
  - 目标作用：先解除本组件保存的旧 Binding/Mapping Handle，再将默认 InputMappingContext 添加到 EnhancedInputSubsystem，并把原始 Action 绑定到 WeaponInputRouter。重复 Setup 不得叠加。

- `void PushInputMappingContext(UInputMappingContext* IMC, int32 Priority = 0)`
  - 输入：映射上下文、优先级。
  - 作用：运行时叠加额外的 IMC（如进入载具后切换一套按键映射）。不影响 ASC 已有绑定。

- `void PopInputMappingContext(UInputMappingContext* IMC)`
  - 输入：映射上下文。
  - 作用：移除之前 Push 的 IMC，恢复默认映射。

> InputComponent 只管 IMC 与原始 Enhanced Input Binding 的生命周期/所有权；回调立即交给 WeaponInputRouter 解析为 InputSnapshot，ComboData/通用路由再决定“触发什么”。限制攻击/不可操作场景通过 GAS Tag/CanActivate 阻塞，不反复解绑输入。

`UnPossess`、Controller/Pawn 替换和 `EndPlay` 必须调用同一幂等 `ShutdownInput`，按 Handle 解除所有绑定并移除本组件添加的 MappingContext。

## UMHGZWeaponInputRouterComponent — 武器输入路由（目标）

挂载到本地 PlayerController，读取当前武器的 `UWeaponInputProfile`。它只生成输入事实，不选择具体 GA。

| 运行时状态 | 说明 |
|---|---|
| CurrentProfile | 当前武器的物理键、Chord、GracePeriod 和方向阈值 |
| HeldControls | 当前按下的 LT/RT/Y/B 等物理输入及 Started SequenceID |
| PendingChordInputs | 仅保存可能参与组合的输入；超时后再派发单键 |
| NextSequenceID | 单调递增的输入身份；Completed 必须引用对应 Started |
| OwnedAimTags | 本路由器添加的 Kinsect/Action/Slinger Tag 计数，失效时精确清理 |

`ResolveInput` 输出完整 `FWeaponInputSnapshot`。任意 Chord 必需成员 Started 都重算候选；TriggerControls 必须在 GracePeriod 内，RequiredHeldModifier 可预先持有或在候选超时前最后补齐。组合完整的最后成员决定方向、姿态、HeldModifier、Aim 和时间快照。组合键优先级和消费规则由 Profile Data Validation 保证；详细字段与释放身份合同见 [冻结实施计划 §3.2](demo-implementation-plan.md#32-输入路由与组合键)。

## UMHGZWeaponComboData — 连招转移表 DataAsset

```
UCLASS(BlueprintType)
class UMHGZWeaponComboData : public UPrimaryDataAsset
```

每武器种类一个，策划在编辑器中配置完整连招图（有向图，允许环）。目标结构正式使用“转移边”命名；源码已使用 `FComboTransition/Transitions/SourceState/TargetState`。旧 `DA_IG_Combo` 不转存，E3 从最终类型新建空壳，E4 一次填写完整图。

| 成员 | 类型 | 说明 |
|------|------|------|
| WeaponTypeTag | FGameplayTag | 关联武器种类 |
| Transitions | TArray\<FComboTransition\> | 连招转移边列表；旧 ComboTable/FComboNode 只保留精确 Redirect，不参与运行时 |
| GlobalComboTimeout | float | 全局兜底超时，默认 10 秒 |

### FComboTransition 结构体

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| TransitionID | FName | 必填 | 转移边的唯一稳定 ID；自动派生、调试和运行时所有权都引用它。Data Validation 禁止重复 |
| SourceState | FName | 必填 | 输入发生前所处状态（"Idle" / "RisingSlash" / "DoubleSlash"…） |
| bMatchAnyState | bool | false | 为 true 时忽略 SourceState 并匹配任意招式状态；使用 `BlockedSourceStates` 排除少数状态 |
| BlockedSourceStates | TArray\<FName\> | 空 | 仅 bMatchAnyState 时生效；空数组表示匹配任意状态 |
| InputTag | FGameplayTag | 输入边必填 | ChordResolver 已解析的 `Input.Weapon.*`；自动转移可为空 |
| Direction | EDirectionalInput | None | None 表示不要求方向；具体方向优先于 None |
| ExecutionPolicy | EComboExecutionPolicy | ActivateAbility | ActivateAbility 激活新 GA；StateOnly 只允许同一 GA 内自动阶段/收尾边 |
| TargetState | FName | Replace 时必填 | 转移成功后状态，可指向自身或前序状态 |
| StatePolicy | EComboStatePolicy | Replace | Replace 改变状态；Preserve 只用于不改变连招状态的侧向命令，且不得授予跨招式 Branch Tag |
| LandingPolicy | EComboLandingPolicy | ResetToIdle | 普通落地由协调器 Reset；AbilityOwned 把 Landing Hit 交给当前 GA 完成显式落地段 |
| AbilityClass | TSubclassOf\<UGameplayAbility\> | nullptr | ActivateAbility 时必填；StateOnly 必须为空并继续使用 SourceActionToken |
| StaminaRequired | float | 0 | 耐力门槛——连招匹配时协调器检查 `CurrentStamina ≥ Required`。**不负责扣耐**——实际扣除由 GA 的 `UMHGZGameplayAbility::StaminaCost` 在 ActivateAbility 中执行 |
| RequiredTags | FGameplayTagContainer | 空 | 激活前提——ASC **必须持有全部**这些 Tag（AND）。含状态标签：`Grounded/Unsheathed`（地面招式）、`Aerial/Unsheathed`（空中招式）、`Sheathed`（拔刀攻击）。Buff/PowerUp 也在此列 |
| BlockedTags | FGameplayTagContainer | 空 | 激活阻止——ASC **必须不持有任一**这些 Tag（NOR）。用于排除特定状态：登龙剑设 `BlockedTags={Combo.Branch.PostRoundslash}`，大回旋 `GrantedTags` 含此 Tag → 登龙无法从大回旋后派生 |
| GrantedTags | FGameplayTagContainer | 空 | 由本次 Transition ActionToken 拥有，进入下一转移或 Reset 时精确释放 |
| GrantTiming | ETransitionGrantTiming | OnActivation | OnActivation 或 OnFirstHit；旧布尔 `bRequiresHitToGrantTags` 在旧包删除门槛满足后删除 |
| bRequiresComboWindow | bool | false | true 时要求 ComboWindowOpen；旧名 `bRequiresWindowOpen` 只用于历史包加载审计 |
| Priority | int32 | 0 | 显式匹配优先级。同层（精确招式/通用招式 + DirectionalInput）内有多个候选行满足 InputAction 条件时，Priority 高的优先匹配 |
| bAutoTransition | bool | false | 自动 ε 转移由唯一 TransitionID + SourceActionToken 请求 |

#### M1 已接入的冻结行为

- `Direction`：输入层先把摇杆转换为世界方向，再与角色 Forward/Right 比较。方向、组合键和修饰态在同一个 InputSnapshot 中冻结；具体方向节点优先于 None。
- `bRequiresComboWindow`：为 true 时仅在 `Combat.State.ComboWindowOpen` 存在时匹配；为 false 时允许收虫、纳刀等取消动作绕过连招窗口，但仍受 RequiredTags 约束。
- `GrantTiming`：OnFirstHit 只接受当前 ActionToken 的首次命中；OnActivation 在 Ability 激活成功后授予。
- `bAutoTransition`：允许 InputTag 为空的 ε 转移；GA 命中或阶段完成后通过确定的 `TransitionID` 请求协调器执行该边，不能只传 TargetState。
- `ExecutionPolicy=StateOnly`：只用于当前 GA 内的命中/落地/收尾阶段变更；协调器验证 SourceActionToken 后更新同一 ActiveTransition，不为纯状态变化创建空 GA。

### 出招表数据模型

`Transitions` 定义 FSM 的有向转移图。`SourceState` 是来源，`TargetState` 是目标，`AbilityClass` 是执行这次状态转移的动作。多个出边可以共享 SourceState，但不要求共享 AbilityClass。

`Transitions` 是平面数组。目标 `StateIndex` 索引精确 SourceState，并另建 AnyState 索引；不为 Demo 保留第二套 `"*"` 数据表示。

### 与装备系统的对接

`EquipmentComponent→ApplyItemEffects` 当前从 `DT_WeaponComboConfig` 同步 `LoadSynchronous` ComboData，收集旧 `ComboTable` 中 AbilityClass 并授予，再激活协调器。目标由 RuntimeHost/装备变更流程注入 `Transitions`；异步 RequestID 不属于本次 Demo 必需范围。

### 复合输入与修饰态

| 模式 | 示例 | 实现 | FComboTransition 如何区分 |
|------|------|------|---------------------|
| 长按修饰+点按 | 持刀 LT+B | LT→Kinsect Aiming 状态；ChordResolver 输出 `Input.Weapon.LTB` | 地面为召回，空中为操虫斩；由 Grounded/Aerial 分流 |
| 同时按 | Y+B | ChordResolver 在 Grace Period 内合并并抑制 Y、B 单键 | 独立 `Input.Weapon.YB`，可再叠加 DirectionalInput |
| 三键组合 | LT+Y+B / RT+Y+B | 快照修饰键后输出 `LTYB` / `RTYB` | 不等待三个普通 GA 竞争；状态再区分粉尘集约/降龙 |

> **核心原则：** EnhancedInput 只采集原始键；通用 ChordResolver 根据当前武器声明的组合表产生稳定 InputTag，并在可配置 Grace Period 内抑制已被组合消费的单键。协调器不硬编码虫棍组合。

### EDirectionalInput 象限规则（M1 已接入）

摇杆先按当前控制方案转换为水平世界方向，再以角色 Forward/Right 分象限。虫棍 `前+Y+B` 因此要求输入世界方向与角色面朝方向一致：角色朝屏幕左时，摇杆左才是 Forward。默认半角 45°、最小输入 0.1，具体阈值只由当前 `UWeaponInputProfile` 提供。

| 值 | 角度范围 |
|----|------|
| None | 不检测方向（长度 < 0.1） |
| Forward | [-45°, +45°] |
| Back | [135°, 180°] ∪ [-180°, -135°] |
| Left | (45°, 135°) |
| Right | (-135°, -45°) |

## GA_WeaponComboCoordinator — 连招协调器（M1 已实现）

```
UCLASS(BlueprintType, Blueprintable)
class UGA_WeaponComboCoordinator : public UMHGZGameplayAbility
```

协调器是 `InstancedPerActor`、`LocalOnly` 的 maintained/no-cost Ability。它长期存在是因为装备期间不主动 End，与耐力成本策略无关。装备时激活并注入 ComboData，卸装时取消；只接收 WeaponInputRouter 生成的 `FWeaponInputSnapshot`。

### 运行时状态（非 UPERTY，协调器内部维护）

| 成员 | 类型 | 说明 |
|------|------|------|
| CurrentState | FName | 当前所处的招式名（初始 "Idle"） |
| ComboData | TObjectPtr\<UMHGZWeaponComboData\> | 当前武器的连招表 |
| StateIndex / AnyStateIndices | 索引 | 目标分别索引精确 SourceState 与 bMatchAnyState 转移；不在每次输入全表扫描 |
| ComboTimeoutTimer | FTimerHandle | 使用 ComboData 的 `GlobalComboTimeout`；到期只清理当前 ActiveTransition 拥有的状态和标签 |
| PendingTransition | FPendingComboTransition | 已通过数据匹配但尚未收到 GA Commit 成功回执的转移；不拥有状态或标签 |
| ActiveTransition | FActiveComboTransition | 当前转移的 TransitionID、完整 ActionToken、Source/TargetState、OwnedTags 与命中状态；不存在跨 Ability 共用的全局 PendingGrantedTags |

M1 已删除运行时 `PreviousState`；协调器会把不可变的 `TransitionID`、`SourceState`、`TargetState` 和输入快照写入 `FWeaponAbilityActivationContext`。**当前 `UMHGZAttackAbility` 仍从 `AttackMontage` 的开头播放，尚未按该上下文选择 Start Section。** 入口选择属于 M4-B.1 的原生前置实现；在它编译并通过验证前，编辑器只能预建 Section，不能假定 GA 已经会按 SourceState 跳到其中一个。协调器已维护 PendingTransition，但不维护 Chord Pending/Timer；组合缓冲只属于 WeaponInputRouter。

### 目标 GA 的 Montage 入口与片段数据流（M4-B.1 前置）

`FComboTransition` 只描述玩法转移，**不**引用 Montage 或 Section；动画细节始终由目标 GA 拥有。这样同一套 Coordinator 能服务不同武器，也不会因为重排动画片段而修改 ComboData。

```text
DA_IG_Combo 的 FComboTransition
  (TransitionID, SourceState, TargetState, AbilityClass)
        │  匹配并冻结
        ▼
FWeaponAbilityActivationContext
  (TransitionID, SourceState, TargetState, InputSnapshot, RuntimeToken)
        │  Pending → GAS Commit → Confirm
        ▼
目标 Ability 实例（InstancedPerExecution）
  选择 Entry Section → 从该 Section 启动自己的 Montage
        │
        ├─ Entry_From_<Source>：衔接动画片段
        ├─ Core：招式本体、命中/窗口/Commit Notify
        └─ Recovery_Common：未派生时的收招片段
                 │
                 ├─ 下一招 Confirm 成功：下一招从自己的 Entry_From_<本招> 开始，旧招 Superseded
                 └─ 未派生：本招播完 → EndAbility → Coordinator 精确回 Idle
```

M4-B.1 在 `UMHGZAttackAbility` 增加以下**目标 GA 私有配置**和选择钩子；命名可随最终 C++ 风格微调，但职责、优先级和时机不得改变：

```cpp
FName DefaultEntrySection = NAME_None;
TMap<FName, FName> EntrySectionByTransitionID;
TMap<FName, FName> EntrySectionBySourceState;
virtual FName SelectAttackMontageStartSection() const;
```

`SelectAttackMontageStartSection()` 只读取本实例已经冻结的 ActivationContext，并按以下顺序返回：

1. `EntrySectionByTransitionID[TransitionID]`；同一目标 GA 的两条边需要不同衔接时使用它。
2. `EntrySectionBySourceState[SourceState]`；多个边来自同一状态且共用衔接时使用它。
3. `DefaultEntrySection`；该 GA 的通用起手。
4. `NAME_None`；没有入口差异时从 Montage 默认开头播放。

必须在 Action Confirm 后、`UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy` **创建时**把结果作为 Start Section 传入；不得先从开头播放、再 `Montage_JumpToSection`，否则会泄漏开头帧的 Root Motion/Notify，也可能先打断旧 Montage。任何已填写的映射值、或非 `NAME_None` 的 Default Section，只要为空/不存在于 `AttackMontage`，数据验证必须报错；运行时也必须在开始播放前 Cancel，不能静默回退到错误片段。Data Validation 还要验证某 GA 的 `TransitionID` 映射只指向以该 GA 为 `AbilityClass` 的正式转移。

Montage 内的实现约定如下：

- 一个**逻辑招式**通常有一个主 Montage；不要把整条连段做成一条超长 Montage，也不要为纯视觉连接动画新建空 GA。
- 同一目标招式的多个入口 Section 均在 Montage 的 `Next Section` 中指向同一个 `Core`，例如 `Entry_From_Idle → Core`、`Entry_From_Slash1 → Core`；`Core → Recovery_Common`。多个入口片段可使用不同 AnimSequence，但共享的 Core/Recovery 可以复用同一个 Sequence 资产。
- `Link_Slash1_To_Slash2` 归入 **Slash2** 的 `Entry_From_Slash1`，而不是 Slash1 的尾部。这样只有 Slash2 完成 Commit 后才会出现这段连接动画；若新动作启动失败，Slash1 仍保持原状。
- 若不同入口连碰撞段、资源语义、Slot、位移所有者或取消规则都不同，则拆为不同 GA/Montage；Section 只解决“同一玩法招式的不同进入/表现片段”，不把异质动作塞进巨型 Montage。
- `ComboWindow` 只授权下一条 Combo 边，并不跳 Section。窗口内输入仍先走 Coordinator 的 Pending→Commit→Confirm；Confirm 成功后的**目标** GA 才依据自己的上下文选择入口。

动画实际位移与 Section 是正交的：Section 只决定播放路径，Root Motion 来自当前播放的 AnimSequence。M4-B.1 另提供原生 `AnimNotifyState_ActionRootMotionPhase`（名称可按最终代码规范调整）：它从精确 `(Mesh, MontageInstanceID)` 解析 ActionToken，再调用所属 Ability 的 Begin/End 接口；Ability 才能按自己的 Token 获取/释放 `MontageRootMotionOwner`。这个 NotifyState 覆盖每段**真实贡献 Root Motion** 的首帧到末帧；in-place 的 Entry/Recovery 不持有所有权。动画看似大位移但根骨骼没有真实位移时，必须由对应 GA 的 `MovementTask`/RootMotionSource 执行，不能假装它是 Montage Root Motion。

规划成员明细：

| 成员 | 类型 | 目标语义 |
|------|------|----------|
| PreInputSnapshot | TOptional\<FWeaponInputSnapshot\> | ComboWindow 打开前的单槽预输入；后输入覆盖前输入，但保留原方向/时间/SequenceID |
| PreInputLifetime | float | 预输入有效窗口，原方案默认 0.15 秒 |

### 公共方法

- `void InjectComboData(UMHGZWeaponComboData* Data)`
  - 当前作用：Reset 当前状态，保存最终 Transitions，构建 State/AnyState/TransitionID 索引。M2 完整 RuntimeHost 装备流程负责同步注入。

- 异步 ComboData 的 RequestID 方案延期到完整装备/流送系统；本 Demo 不同时实现同步与异步两条加载路径。其他延迟回调使用完整 `FWeaponRuntimeToken{Host, Generation}` 拒绝旧 Pawn/旧世代。

- `void HandleWeaponInput(const FWeaponInputSnapshot& Input)`
  - 输入：WeaponInputRouter 已解析的组合 Tag、HeldModifiers、姿态 ContextTags、世界方向、Phase、时间与 SequenceID。
  - 作用：若 ComboData 未注入则忽略；按状态/修饰/方向/Priority 匹配后进入统一 `ExecuteTransition`。

- `bool ConfirmTransitionActivation(const FWeaponActionToken& ActionToken)`
  - 只接受 RuntimeToken、SpecHandle、ActivationSequenceID 和 AbilityInstance 与 PendingTransition 全部匹配的成功回执；此时才提交 CurrentState、ActiveTransition 和 OnActivation Tags。

- `void RejectTransitionActivation(const FWeaponActionToken& ActionToken)`
  - Commit 失败、激活期间取消或上下文无效时清除 Pending；不得修改 CurrentState 或释放当前旧转移。

- `void OnAttackHit(const FWeaponActionToken& ActionToken)`
  - 作用：只更新完整匹配 ActionToken 的 ActiveTransition，并按该转移配置从 TagLedger 取得有所有权 Token；同一 Spec 的旧激活迟到回调不能改写新转移。

- `void OnActionFinished(const FWeaponActionToken& ActionToken, EWeaponActionEndReason Reason)`
  - 仅当完整 ActionToken 仍拥有 ActiveTransition 时清理其 Token 并决定回 Idle；被后续动作以 `Superseded` 取代的旧实例不能重置新状态。

- `bool OnAutoTransition(FName TransitionID, const FWeaponActionToken& SourceAction)`
  - 输入：确定的自动转移边 ID 和发起它的完整 ActionToken。
  - 作用：验证 SourceAction 仍拥有当前状态、目标边 `bAutoTransition=true` 且 SourceState 匹配，然后执行该边的 AbilityClass 并进入它的 TargetState。
  - 不提供 `FindAbilityClassForState`；同一 SourceState 的不同出边本来就可以激活不同 Ability。

### 运行时工作流

**阶段 A（目标装备流程）：** EquipmentComponent 只在武器身份变化时广播 WeaponSnapshot → Character RuntimeHost 比较身份并创建 Resource/请求 ComboData → ASC 授予 Transitions 引用的 Ability → 激活 Coordinator → 注入 ComboData 并构建索引。护甲、饰品和重复 Snapshot 不重建。

**阶段 B（起手攻击）：** InputProfile 已将修饰键解析为唯一 InputTag；`HandleWeaponInput(InputSnapshot)` → 候选边 = `StateIndex["Idle"] ∪ AnyState` → 按 InputTag、状态精确度、方向、Priority 排序与预检 → 建立 PendingTransition/ActivationContext → TryActivate 已授予 Spec。GA 开头 Commit 成功并回执 Confirm 后，协调器才提交 ActiveTransition 和 TargetState；TryActivate 或 Commit 失败都清 Pending 且不改变状态。

**阶段 C（GA 执行）：** GA 读取 ActivationContext → Resource reservation → GAS Commit → Consume → Confirm；Confirm 接受后，目标 Attack GA 按 `TransitionID → SourceState → Default → Montage 开头` 选择并验证 Start Section，随后以该 Section 创建 Montage Task 并注册 `Mesh+MontageInstanceID→ActionToken`。当前代码在 M4-B.1 前仍只有“从开头播放”的最后一步，不能提前把该目标行为视为已实现。`AttackCollision/ComboWindow/RootMotionPhase` 先从 RuntimeHost Registry 解析所属 ActionToken，再只调用该实例；Coordinator 通过 TagLedger 管理重叠 Window Token。

**阶段 D（连招下一段）：** 窗口内按同一流程执行下一条边；新实例 Commit+Confirm 后接管 ActiveTransition，协调器再对旧实例调用 `RequestEndAction(Superseded)`。GA 首次命中只更新完整 ActionToken 匹配的转移。

**阶段 E（回 Idle）：** `ComboWindow→NotifyEnd` 释放匹配 Window Token → Montage 播完 → GA `EndAbility` → `Coordinator→OnActionFinished(ActionToken, Reason)`；只有该完整身份仍拥有 ActiveTransition 时才回 `Idle` 并释放它持有的分支 Token，绝不按 `Combo.Branch.*` 父标签批量清理。

**阶段 F（异常兜底）：** `SafetyTimer` 到期（`GlobalComboTimeout` 秒，仅 Montage 卡死等极端情况）→ `ResetCombo(SafetyTimeout)`，只释放 Pending/ActiveTransition 自己拥有的 Tag，并按真实姿态回 Idle/Aerial.Free。武器卸下由 RuntimeHost 先 Reset/Cancel，再清索引；禁止按父标签无差别删除其他系统拥有的 Branch Tag。

**阶段 G（自动转移）：** GA 命中/阶段完成 → `Coordinator→OnAutoTransition(TransitionID, SourceActionToken)` → 精确找到一条 `bAutoTransition` 边 → 验证所有权和 SourceState。ActivateAbility 边走 Pending/Commit/Confirm；StateOnly 边继续由同一 ActionToken 持有并直接完成状态/标签交接。

### 两种转移路径对照

| | **输入转移**（阶段 B/D） | **自动转移**（阶段 G） |
|------|------|------|
| 触发源 | 玩家按键 | GA 事件（命中/播完） |
| 协调器方法 | `HandleWeaponInput` | `OnAutoTransition` |
| InputTag | 必填 | 必须为空 |
| 匹配方式 | 输入快照排序匹配 | 直接按唯一 TransitionID 查找 |
| 出招表标记 | — | `bAutoTransition=true` |
| ExecutionPolicy | ActivateAbility | ActivateAbility 或 StateOnly |
| 示例 | 袈裟斩→二连斩（按 Y） | 操虫斩命中→StateOnly 舞踏阶段 |

### 关键设计要点

- **组合输入解析：** ChordResolver 只延迟可能组成组合键的单键；形成组合后立即消费，普通无组合输入不增加一帧延迟
- **预输入缓冲：** 保存完整 `FWeaponInputSnapshot`（单槽，`PreInputLifetime=0.15s`），窗口打开时消费；不能只存 Tag 丢失方向和 SequenceID
- **转移匹配排序 + 匹配即停：** InputTag 已精确解析；再按精确 SourceState、具体方向、Priority 排序
- **死亡处理：** `Combat.Event.Death` 取消武器 GA并 RuntimeHost Shutdown；复用同一 Character 时递增 Generation，新建 Pawn 时更换 Host，二者都生成新 `FWeaponRuntimeToken` 并从当前装备重建，不能复用死亡前运行时对象
- **打断后瞄准恢复：** Hitstun/Knockdown 移除后由 WeaponInputRouter 根据仍按住的物理键重新计算 Aim Context；不自动激活攻击 GA
- **着陆分流——`Coordinator→OnLanded(Hit)`：** 默认 `ResetCombo(Landed)`；当前转移为 AbilityOwned 时，只把 Hit 交给匹配 ActionToken 的 GA，急袭突刺/降龙完成落地段后再 StateOnly Auto→Idle
- **空中次数限制——Cant 权限模型（零交叉逻辑）：** `CantDodge`/`CantAttack` 两个 Tag 各自独立。`GA_AirDodge` 添加 `CantDodge`，`GA_AirAttack_*` 添加 `CantAttack`。无需 `Exhausted` 汇总——两个 Cant 同时存在 = 双阻塞。GAS 原生 `BlockedTags` 处理，GA 零覆写代码
- **FSM 两条转移路径不冲突：** 输入转移和自动转移共享 `ExecuteTransition(TransitionID)`；自动转移必须引用唯一边 ID，EndAbility 只有在 AbilityHandle 仍拥有 ActiveTransition 时才能回 Idle
- **GA→GA 自动派生不走 `TryActivateAbilityByTag`：** 必须通过 `OnAutoTransition`——确保协调器感知状态变更、ActivationContext 来源正确、SafetyTimer 更新。绕过协调器直接激活 GA 会导致状态不同步、Entry Section 选错、旧 End 回调误回 Idle

## AnimNotifyState 系列（AttackCollision、ComboWindow、DodgeWindow、DodgeAcceptWindow 已实现；其余为规划）

> 均为 C++ 类（非蓝图）。`UAnimNotifyState` 有 Begin/Tick/End 三阶段回调，适合需要"持续一段帧"的逻辑。

所有玩家动作 Notify 覆写带 `FAnimNotifyEventReference` 的 Begin/Tick/End，并从 `EventReference.GetContextData<UE::Anim::FAnimNotifyMontageInstanceContext>()` 取得 UE5.6 的 MontageInstanceID（包含 `Animation/ActiveMontageInstanceScope.h`）。再以 `MeshComp + MontageInstanceID` 查询 RuntimeHost Registry，取得完整 ActionToken 和弱 AbilityInstance。`Kinsect Send/Recall Commit` 是 Layered Slot 兼容的例外：若 UE Context 缺失，Notify 仅把本次回调的源 `UAnimMontage` 传入 Resolver；Resolver 从**同一 Mesh** 的 AnimInstance 取得该 Montage 的当前 InstanceID，再走同一 Registry。它仍不是扫描 Ability，也不是按 Class 猜实例。Context/回退均无法解析、Token 无效、世代不符或实例已结束时不执行 Commit；Kinsect Commit 会记录 Warning 以便 PIE 定位。禁止 `GetActivatableAbilities()` 全表扫描，禁止按 AbilityClass 猜实例，禁止 Notify 资产对象保存运行时 Token。

### UAnimNotifyState_ActionRootMotionPhase — 动作根运动阶段（M4-B.1 规划）

该 NotifyState 不是根运动开关，也不直接操作 CharacterMovementComponent。它只把当前 Montage 实例路由到所属 Action；Ability 再以自己的 `FWeaponActionToken` 调用 RuntimeHost 的精确所有权接口。

```text
NotifyBegin
  → Mesh + MontageInstanceID → ActionToken
  → UMHGZAttackAbility::BeginMontageRootMotionPhase(ActionToken)
  → RuntimeHost::AcquireMontageRootMotion(ActionToken)

NotifyEnd / Ability End / Montage Interrupted
  → 同一精确 Token
  → UMHGZAttackAbility::EndMontageRootMotionPhase(ActionToken)
  → RuntimeHost::ReleaseMontageRootMotion(ActionToken)
```

旧实例的 NotifyEnd、已被 Superseded 的 Ability、或不匹配的 RuntimeToken 必须静默忽略，绝不能释放新实例的所有权。已完成的 `GA_Dodge`、`GA_Sheathe` 和 `UMHGZDrawAttackAbility` 各自已有经验证的根运动生命周期；M4-B.1 先为后续普通攻击/特殊动作提供按阶段的通用路径，不重写这三条已接线动作，除非逐条资产审计和回归测试通过。

### UAnimNotifyState_ComboWindow — 连招窗口通知

```
UCLASS(BlueprintType, Blueprintable)
class UAnimNotifyState_ComboWindow : public UAnimNotifyState
```

挂载在攻击 Montage 中，标记"接受下一招输入"的精确帧区间。替代纯代码计时器。

| 成员 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| bHapticFeedback | bool | false | 窗口结束前是否手柄震动提示 |
| HapticLeadTime | float | 0.05 | 震动提前量（秒） |

**生命周期：**

```
NotifyBegin（窗口打开）：
  → 从 MeshComp/MontageInstanceID 精确取得 ActionToken
  → Coordinator→OpenComboWindow(ActionToken, NotifyEventID)
  → Coordinator 为该身份创建 Window Token；首次 Token 才使 ComboWindowOpen 生效
  → （可选）按 HapticLeadTime 启动震动计时器

NotifyEnd（窗口关闭）：
  → Coordinator→CloseComboWindow(ActionToken, NotifyEventID)
  → 只释放匹配 Token；同一 Ability 的其他重叠窗口仍保持开放
  → 输入不再触发连招
  → 攻击 Montage 自然播放至 BlendOut → 动画回到 Idle 待机
```

> NotifyState 资产对象可能被多个角色/实例共享，不能保存运行时 Handle。Window Token 保存于 Coordinator 的 `(ActionToken, NotifyEventID)` Map；Ability End/ResetCombo 会兜底回收，即使 Montage 中断没有可靠到达 NotifyEnd 也不残留。

### UAnimNotifyState_DodgeWindow — 翻滚无敌帧通知

```
UCLASS(BlueprintType, Blueprintable)
class UAnimNotifyState_DodgeWindow : public UAnimNotifyState
```

挂载在翻滚 Montage 中，标记无敌帧的精确区间。与 `ComboWindow` 完全独立——翻滚不在连招系统内，无需访问协调器。

无敌帧期间当前同时关闭 Weapon 与 MonsterAttack 两个 Trace 通道响应，Pawn 通道保持不变：

```
┌──────────────────────────────────────────┐
│  Weapon 通道（玩家攻击检测用）            │  ← NotifyBegin: 玩家胶囊体设为 Ignore
│  作用: 怪物武器 Sweep 检测玩家             │     NotifyEnd:   恢复原始响应
│                                           │     效果: 攻击 Sweep 物理上穿过玩家
├──────────────────────────────────────────┤
│  Pawn 通道（物理阻挡用）                  │  ← 始终 Block，不变!
│  作用: 角色胶囊体 ↔ 怪物胶囊体             │     效果: 玩家仍被怪物身体阻挡
│         CMC 推挤/重叠修正                  │     不会出现"翻滚穿过整个怪物"
└──────────────────────────────────────────┘
```

> 两层保障：(1) 碰撞层 Weapon/MonsterAttack 通道 Ignore；(2) GAS 层 `Combat.State.Invincible` Tag。

**生命周期：**

```
NotifyBegin：
  → 由 Registry 取得当前 Dodge ActionToken
  → 该实例申请 Invincible Window Token，并缓存角色胶囊体对 Weapon、MonsterAttack 的原响应
  → 将两个通道设为 Ignore

NotifyEnd：
  → 释放匹配 Token
  → 恢复 NotifyBegin 前缓存的各通道原始响应；不能一律写成 Block
```

**卡模/穿模风险：**

| 风险 | 对策 |
|------|------|
| 翻滚结束时卡在怪物体内 | CMC 自带 `ResolvePenetration`——两个 Pawn 胶囊体重叠时自动推挤分离。极端情况：`NotifyEnd` 中做一次重叠检测，若仍重叠则强制推离 |
| 大型怪物 vs 小型怪物 | 策划在怪物蓝图上配置 Pawn 通道响应：大型（Block，不可穿过） vs 小型（Ignore，可穿过）——翻滚手感由策划按怪物体型决定 |
| 两段无敌帧之间频繁切换 | `SetCollisionResponseToChannel` 是修改一个枚举值，每帧开销可忽略。每个翻滚只执行两次（Begin+End） |

### UAnimNotifyState_DodgeAcceptWindow — 翻滚接受窗口通知

```
UCLASS(BlueprintType, Blueprintable)
class UAnimNotifyState_DodgeAcceptWindow : public UAnimNotifyState
```

挂载在**攻击 Montage** 中，标记"允许翻滚"的精确帧区间。与 `DodgeWindow`（翻滚动画自身的无敌帧）不同——这是攻击侧的接受窗口。**不操作协调器，直接操作 ASC Tag**——与 GAS 的 `ActivationRequiredTags` 机制天然契合。

**生命周期：**

```
NotifyBegin：
  → 当前攻击 Ability 以 ActionToken+NotifyEventID 申请 DodgeAccept Window Token

NotifyEnd：
  → 释放匹配 Token；Ability End/Reset 兜底回收
```

> ComboWindow 接受连招输入（0.1–0.5s）；DodgeAcceptWindow 接受翻滚输入（0.1–0.7s，延伸到收招帧）。DodgeAcceptWindow 在攻击 Montage，DodgeWindow 在翻滚 Montage。

### UAnimNotifyState_PoiseWindow — 霸体窗口

```
UCLASS(BlueprintType, Blueprintable)
class UAnimNotifyState_PoiseWindow : public UAnimNotifyState
```

挂载在攻击 Montage 中。`NotifyBegin` 以当前 ActionToken+NotifyEventID 申请 Poise Token（如 `Combat.Poise.Heavy`）；`NotifyEnd` 只释放匹配 Token，Ability End/Reset 兜底回收。Notify 对象不保存运行时状态。攻击动画师在此区间放置，覆盖招式的大开大合期。

### UAnimNotifyState_AttackCollision — 攻击碰撞窗口

```
UCLASS(BlueprintType, Blueprintable)
class UAnimNotifyState_AttackCollision : public UAnimNotifyState
```

挂载在攻击 Montage 中。Begin/Tick/End 每次都由 `MeshComp + MontageInstanceID` 解析同一 ActionToken，并只调用该 Token 的 `UMHGZAttackAbility` 实例执行 `EnableCollision/TickCollision/DisableCollision(ConfigIndex)`；解析失败即忽略，绝不遍历 ASC 中全部活动攻击。ConfigIndex 在完整生命周期内保持一致，因此不同段可以重叠，各自维护去重表、轨迹历史与多跳 Timer。旧 Montage 的 End 因 ActionToken 不匹配不能关闭新实例窗口。判定窗口可短至 1-2 帧；同一招式前后半棍同时有效时，优先在一个 Segment 的 `TraceRegions` 中配置两个 Region，而不是复制相同 Index 的 Notify。

### UAnimNotifyState_MonsterAttackCollision — 怪物攻击碰撞窗口

挂载在怪物攻击 Montage 中（详见上文"怪物攻击碰撞"章节）。

### UAnimNotifyState_ForesightJudge — 见切判定窗口

挂载在见切 Montage 段0 中。`NotifyBegin` → 遍历 `ActiveAbilities` 找到见切 GA → 设其 `bIsInForesightWindow=true`；`NotifyEnd` → 恢复 false。GA 的 `OnGameplayEvent(Combat.Event.HitStagger)` 回调检查此标记决定是否成功。

### AnimNotify 类

> 均为 C++ 类（非蓝图）。`UAnimNotify` 仅单帧触发，适合瞬时事件。

#### UAnimNotify_SwingSound — 挥刀风声

持有 `USoundBase* Sound` 成员。`Notify` 中 `PlaySoundAtLocation(this→Sound)`——GA 在 `ActivateAbility` 时从 Montage 查找此实例并注入音效引用，Notify 只读自己，不查任何外部对象。

## 特效/音效/镜头——三层分工（规划；震屏/基础卡肉除外）

| 层 | 机制 | 适用场景 |
|----|------|----------|
| 帧级同步 | Montage AnimNotify / AnimNotifyState | 武器拖尾、脚步声、挥空音效 |
| 状态驱动 | GAS GameplayCue（`ASC→ExecuteGameplayCue`） | 命中火花 VFX、命中碰撞音效（按物理材质选）、伤害数字、Buff 光环 |
| 镜头 | Ability 内 `UCameraModifier` / `PlayerCameraManager→StartCameraShake` | 震屏、FOV 变化、瞄准拉近 |

调用链：`GA_Slash_01::Montage` → `AnimNotify_SlashWhoosh`（挥空音效）+ `AnimNotifyState_WeaponTrail`（刀光拖尾）→ `AttackCollision::Sweep` 命中 → `ApplyDamage` → GE Spec Apply → `GameplayCue.Hit.Slash` 触发（命中火花 VFX + 按物理材质选命中音效 + 伤害数字 UI）。震屏/卡肉在 `ApplyDamage` 中直接执行（`CameraShakeClass`/`HitStopBase`），Ability 结束后自动清理。
