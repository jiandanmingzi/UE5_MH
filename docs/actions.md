# 动作系统

> **实施状态说明（以源码为准）：** 本文保留完整目标设计。除本节“当前实现”以及正文中明确标注为已实现的内容外，其余类、字段、资产、流程和验证项均是待实现或待接入的详细方案，不表示当前项目已经具备。

## 当前实现

| 模块 | 当前状态 |
|------|----------|
| 移动 | `AMHGZCharacter::DoMove` 只计算 `InputMagnitude`、`TargetCruiseSpeed` 和 `DesiredSpeed`，不调用 `AddMovementInput`；AnimBP 使用 Motion Matching/Root Motion。角色在 `Tick` 中按 `TurnRate` 限制最大转角，默认 `360°/s`，180° 不再瞬转。 |
| 冲刺与瞄准 | 冲刺是 Character 上的 `bSprintHeld`，瞄准由 Character 输入增删 `Combat.State.Aiming`；当前不是 `GA_Sprint`/`GA_Aim` 驱动。 |
| GAS 输入 | ASC 在初始化时把 InputAction 的 `Started`/`Completed` 绑定到 GameplayTag 路由；`BindInputAction` 只更新配置数组，不会在初始化完成后补绑 EnhancedInput。 |
| 攻击 | `UMHGZAttackAbility`、Montage Task、Socket Sweep、多段/多跳伤害和 `AnimNotifyState_AttackCollision` 已实现。当前 Sweep 直接查询，不创建临时碰撞组件。 |
| 连招 | `UGA_WeaponComboCoordinator` 使用 `UMHGZWeaponComboData::ComboTable` 和 `FComboNode::InputTag`。当前只匹配状态、输入、Required/Blocked Tags、耐力门槛和优先级；`DirectionalInput`、`bRequiresWindowOpen`、`bAutoTransition`、`bRequiresHitToGrantTags` 尚未接入匹配流程。 |
| 闪避 | `UMHGZDodgeAbility` 与 `AnimNotifyState_DodgeWindow` 已有骨架；方向仍读取 `GetLastMovementInputVector()`，而当前移动不写 CMC 输入向量，因此方向闪避尚未完成接入。 |
| 边缘跳越 | `UMHGZEdgeVaultComponent` 目前仅为关闭 Tick 的桩组件；检测链和 `GA_EdgeVault` 属于下文保留方案。 |
| 基础消耗/冷却 | 持续扣耐可用；单次扣耐被无效的 `MakeOutgoingSpec(nullptr)` 条件包住，当前可能不生效。`CooldownTag` 只添加不移除，`CooldownDuration` 尚未使用。 |

**设计原则：** GAS + EnhancedInput 驱动，通过 GameplayTag 桥接输入与 Ability。核心能力（移动/闪避）始终可用，武器能力（连招/资源技能）由装备系统动态授予/移除。**无独立跳跃键——边缘跳越（Edge Vault）替代。**

## 移动实现

- 移动物理壳、重力与落地检测：`UCharacterMovementComponent`；常规位移由 AnimBP Root Motion 驱动
- 移动输入：`DoMove` 记录输入方向和期望速度，当前不调用 `AddMovementInput`
- 移动动画：AnimBP Motion Matching，根据 `DesiredSpeed` 和双 Pose Search Database 驱动
- 奔跑：Character 的 `bSprintHeld` 切换巡航速度；持刀时 `Combat.State.Unsheathed` 阻止进入冲刺
- GAS 当前主要通过 `Combat.State.BlockMovement` 阻断移动；`MoveSpeedMultiplier` 已定义但尚未接入 `CalcCruiseSpeed`

### RootMotion——攻击/翻滚中如何覆盖 CMC 移动

攻击 Montage 配置 Root Motion 时，动画根骨骼位移可直接驱动角色位移/旋转。当前常规移动本身不调用 `AddMovementInput`；攻击期间通过 `Combat.State.BlockMovement` 把 Motion Matching 期望速度清零。摇杆方向可由 **MotionWarping** 用于旋转修正（见 `MaxCorrectionAngle`）。

| 场景 | RootMotion 作用 | bEnableRootMotion |
|------|-----------------|:--:|
| 攻击 Montage | 锁定角色按动画轨迹移动，摇杆仅控制方向修正 | ✅ true |
| 翻滚 Montage | 前跃/侧移距离由动画精确控制，不受 CMC 加速度/摩擦影响 | ✅ true |
| 见切后撤 | 段0 后撤位移完全动画驱动（配合 MotionWarping 修正方向） | ✅ true |
| 登龙下劈 | 空中轨迹动画控制——不是物理跳跃+下落 | ✅ true |
| 受击硬直 Montage | 击退距离由 Impulse + Montage RootMotion 共同决定 | ✅ true |
| 待机/收刀行走 | 摇杆+CMC 正常移动 | ❌ false |

### MotionWarping——FBX 无根骨骼位移数据时的补位方案

#### 问题背景：外部游戏提取的 FBX 通常缺少根骨骼位移

从商业游戏（如怪猎崛起 RE Engine）解包获得的 FBX 动画，其骨骼层级与 UE5 的 RootMotion 预期存在结构性差异：

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
3. **曲线全程水平线 = 根骨骼无位移数据 → 必须用 MotionWarping 补偿**

#### MotionWarping 核心机制

**MotionWarping 不是瞬移。** 工作方式：

```
原始动画根骨骼轨迹：起点 ════════ 终点（前移 50cm，动画师 K 的小幅位移）
设定 Warp Target：   起点 ════════════════════════ 目标点（前移 800cm）

逐帧处理：
  每帧读取原始动画根骨骼位移 → 乘以缩放因子（或重新映射到目标点）
  → 最终累积位移 = 起点到目标点的向量
  → 角色在 Warp Window 时长内平滑移动到目标点
```

#### 位移速度控制

MotionWarping 不提供类似 CMC 的加速度曲线，但可通过以下参数控制视觉速度：

| 控制方式 | 作用 | 配置位置 |
|----------|------|----------|
| **Warp Window 时长** | 位移被拉伸的时间段。窗越短→速度越快；窗越长→越平滑 | `AnimNotifyState_MotionWarping` 的 Start/End 时间 |
| **Warp Translation 轴开关** | 控制 X/Y/Z 各轴是否参与 Warp、最大缩放倍数 | NotifyState 的 `WarpTranslation` 属性 |
| **Montage Play Rate** | 与 Warp 叠加生效。Rate=2.0 + Warp=800cm → 视觉速度翻倍 | GA 的 `PlayMontageAndWait` 的 `Rate` 参数 |
| **自定义 Warp Curve** | 自定义缓入缓出曲线（Ease-In/Out） | `MotionWarpingComponent` 的 Warp Curves 资产 |
| **分段 Warp** | 同 Montage 内多个 Warp Notify 覆盖不同时间片段 | 动画师在 Montage 时间轴上分段拖拽 |

> **精细曲线方案：** 如需精确的"先加速后减速"曲线，可 C++ 派生 `UMotionWarpingModifier`，在 `OnWarpUpdate` 中实现自定义 easing 逻辑。

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

**设计原则：** 空中招式按位移来源分为 5 类，通过**惯性速度状态（AerialVelocity）**在 CMC 与 GA 之间交接动量，AnimBP 按 `Combat.State.Aerial.Falling.*` Tag 选择下落/收招 Pose。

### 招式分类与位移策略

| 分类 | 虫棍示例 | 位移特征 | 实现方案 |
|------|---------|----------|----------|
| **① FBX 自带位移** | 空中回避(137)、降龙上升(173)、撑杆前半 | FBX 已 K 帧，方向锁定（4向/8向） | `bEnableRootMotion=true` + `MaxCorrectionAngle` 方向修正 |
| **② 准心方向 + 固定距离** | 操虫斩(162)、铁虫丝跳跃(178) | 方向=按键时准心朝向，最大距离固定 | MotionWarping `AddOrUpdateWarpTargetFromLocation` |
| **③ 固定垂直 + 摇杆水平缩放** | 撑杆起跳(141-146)、舞踏(189)、猎虫滑翔(190)、操虫斩命中后(163) | 上升高度固定，水平受摇杆影响（前推增、后推减） | 垂直: FBX RootMotion 或 Task；水平: RootMotion Task + 摇杆缩放 |
| **④ 纯惯性下落** | 被动下坠(74)、持刀下坠(76)、起跳下坠(143) | 仅重力+空气摩擦 | **无 GA**——CMC 全权管理 |
| **⑤ 惯性 + 摇杆参与** | 跳跃斩(105)→下坠(106)、急袭突刺 | 继承动量 + 摇杆修正 + 重力 | RootMotion Task（Additive 模式合成） |

> ③ 的上升方向由动画类型决定（撑杆前跳=前上、舞踏=正上、猎虫滑翔=前上、操虫斩命中=后上），水平位移是"动画预设方向 × 摇杆缩放"。⑤ 的位移方向完全由当前惯性+摇杆实时计算。

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
                    ① RootMotion 直接
                    ② MotionWarping
                    ③ RootMotion Task(垂直) + 摇杆缩放(水平)
                    ⑤ RootMotion Task(惯性+摇杆, Additive)
                        ↓
  ↓ GA 结束              ↓
Velocity=最终合成值  ←  回灌               下落 Pose Tag 写入
  ↓                                          ↓
CMC 管理重力+摩擦
  ↓ 落地                   ↓                    ↓
Velocity归零→Grounded    可激活新 GA          落地动画→Idle
```

### 各分类实现细节

#### ① FBX 自带位移

```cpp
void UGA_IG_AirDodge::EndAbility(...)
{
    // 从 Montage 末帧反算速度（不能用 CMC→Velocity——可能已衰减）
    UAnimInstance* Anim = Mesh->GetAnimInstance();
    FVector RootDelta = Anim->GetRootMotionDelta();
    FVector FinalVelocity = RootDelta / Anim->GetDeltaSeconds();
    CMC->Velocity = FinalVelocity;

    ASC->AddLooseGameplayTag(Combat.State.Aerial.Falling.IG_AirDodge);
    Super::EndAbility(...);
}
```

#### ② 准心方向 + 固定距离

```cpp
void UGA_IG_KinsectSlash::ActivateAbility(...)
{
    FVector Target = Avatar->GetActorLocation()
                   + GetAimComponent()->GetAimDirection() * MaxSlashDistance;
    MotionWarpingComponent->AddOrUpdateWarpTargetFromLocation(
        FName("KinsectSlashTarget"), Target);
    PlayMontageAndWait(KinsectSlashMontage);
}
// 命中后：结束当前 GA，激活 ③ 类 GA 处理命中后动画
// ③ 类 GA 的 Task(bIsAdditive=false) 覆盖 Warp 残留速度
```

#### ③ 固定垂直 + 摇杆水平缩放

```cpp
void UGA_IG_PoleVaultForward::ActivateAbility(...)
{
    FVector2D Stick = GetLastMovementInputVector();
    float StickScale = FMath::Clamp(1.0f + Stick.Y, 0.2f, 1.8f);  // 前推×1.8，后推×0.2
    float HDist = PoleVaultBaseForwardDistance * StickScale;

    FVector LaunchVel = Avatar->GetActorForwardVector() * HDist / Duration;
    LaunchVel.Z = PoleVaultBaseHeight / Duration;
    LaunchVel += CMC->Velocity * InertiaPreserveRatio;

    UAbilityTask_ApplyRootMotionConstantForce* Task =
        UAbilityTask_ApplyRootMotionConstantForce::ApplyRootMotionConstantForce(
            this, TEXT("PoleVault"), LaunchVel, Duration,
            false, nullptr, ERootMotionFinishVelocityMode::SetVelocity);
    Task->ReadyForActivation();
}
```

#### ⑤ 惯性 + 摇杆参与

```cpp
void UGA_IG_JumpSlash::ActivateAbility(...)
{
    FVector Inherited = CMC->Velocity;
    FVector2D Stick = GetLastMovementInputVector();
    FVector StickDir = Avatar->GetActorForwardVector() * Stick.Y
                     + Avatar->GetActorRightVector() * Stick.X;

    FVector Vel = Inherited * 0.7f + StickDir * StickForce + FVector(0,0,-DownwardForce);
    Vel = Vel.GetClampedToMaxSize(MaxAerialSpeed);

    UAbilityTask_ApplyRootMotionConstantForce* Task = ...;
    Task->ReadyForActivation();
}
```

### RootMotion Task vs MotionWarping — 惯性行为对照

| | RootMotion Task（SetVelocity） | MotionWarping |
|------|:--:|:--:|
| **运行时速度正确反映在 CMC？** | ✅ 每帧 | ⚠️ 反映，但 Warp 是"位移缩放"而非"速度控制" |
| **结束时自动保留惯性？** | ✅ `SetVelocity` 模式自动写 CMC | ❌ 需手动从 `GetRootMotionDelta` 反算回灌 |
| **能叠加惯性（Additive）？** | ✅ `bIsAdditive=true` | ❌ Warp 只修改动画内位移 |
| **能覆盖惯性（重置）？** | ✅ `bIsAdditive=false` | ❌ 需配合 Task 覆盖 |
| **适用分类** | ③ ⑤ | ② |

> **MotionWarping 的坑：** Montage 结束和 `EndAbility` 之间可能差 1~数帧，此时 CMC 已恢复自主物理（`BrakingDecelerationFalling` 生效），直接读 `CMC→Velocity` 得到的是已衰减的速度。正确做法：从 `UAnimInstance::GetRootMotionDelta()` 反算末帧瞬时速度。

### 统一 EndAbility — 空中 GA 速度回灌

```cpp
void UMHGZAttackAbility::EndAbility(...)
{
    // 仅空中 GA 需要惯性保留
    if (ASC->HasMatchingGameplayTag(Combat.State.Aerial))
    {
        UAnimInstance* Anim = Mesh->GetAnimInstance();
        if (Anim && CurrentMontage)
        {
            if (!bHasActiveRootMotionTask)
            {
                // ①② 类（FBX RootMotion / MotionWarping）：
                // Task 未接管位移 → 从 Montage 末帧反算速度
                FVector RootDelta = Anim->GetRootMotionDelta();
                if (!RootDelta.IsNearlyZero())
                {
                    CMC->Velocity = RootDelta / Anim->GetDeltaSeconds();
                }
            }
            // ③⑤ 类（RootMotion Task）：Task 已在结束时自动写 CMC→Velocity
        }
    }

    Super::EndAbility(...);
}
```

> `bHasActiveRootMotionTask` 在 GA 激活 RootMotion Task 时设为 true，Task 结束时（`SetVelocity` 模式自动写 CMC）或 `EndAbility` 清理时设回 false。

### 各分类惯性行为总览

| 分类 | 位移源 | 结束时机 | 惯性交接方式 |
|------|--------|---------|-------------|
| ① | Montage RootMotion | Montage 末帧 | `EndAbility` 手动：`GetRootMotionDelta() / Δt` → CMC |
| ② | MotionWarping | Warp 窗口结束 | `EndAbility` 手动：同上 |
| ② 命中 → ③ | — | GA 切换 | ③ 类 Task（`bIsAdditive=false`）覆盖残留速度 |
| ③ | RootMotion Task | Task 结束 | `SetVelocity` 自动 → CMC |
| ④ | CMC 物理 | 持续直到落地 | CMC 自身（重力+摩擦） |
| ⑤ | RootMotion Task（Additive） | Task 结束 | `SetVelocity` 自动 → CMC |

### 空中收招后的状态流向

```
空中回避(137) 结束 → ① → 末速度写入 CMC → ④下落
操虫斩(162) 命中 → ② → ③ 类 GA 重置惯性 → ③ 结束 → ④下落
操虫斩(162) 未命中 → ② → 末速度写入 CMC → ④下落
撑杆起跳(141) 结束 → ③ → SetVelocity 自动 → ④下落
跳跃斩(105) 结束 → ⑤ → SetVelocity 自动 → ④下落
```

### 空中动作次数限制

**设计思路：** 不维护数字计数器，用 Cant（禁止）Tag 做"空中体力槽"。默认无限制——用了才禁止。每轮滞空：空中回避 ×1 + 空中攻击 ×1。两者用完→两个 Cant 同时存在→各自 BlockedTags 命中→双阻塞。舞踏/操虫斩命中后清 Cant 重置。

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
    └── ★ GA_DanceJump 激活（命中触发/操虫斩命中后）
          → 移除 CantDodge + CantAttack（重置）

不需要 Exhausted——两个 Cant 各自独立，不需要"汇总"标签。
```

**与碰撞/伤害系统无关：** 次数限制仅影响 GA 的 `CanActivateAbility`（GAS 原生 `BlockedTags`，零覆写代码）——不阻塞连招表匹配、不涉及 AnimNotifyState、不改变位移逻辑。空中攻击未命中→`ShouldContinueAfterHit` 返回 false→`EndAbility` 写入 `Falling.*` Tag→AnimBP 维持下落 Pose，不会额外发放新的空中权限。Cant 的容错性优于 Can：加 Cant 失败→最多多用一次；加 Can 失败→本应可用的招式被误锁。

### 着陆重置——落地后恢复连招

**问题：** 落地链路绕过了协调器（CMC `OnLanded` 事件，不经过任何 GA）→ `CurrentState` 残留空中攻击的招式名 → 地面 `FComboNode` 无法从 `"Idle"` 匹配起手攻击。

**方案：** 在 `AMHGZCharacter::OnLanded()` 中：
1. ASC 移除所有 `Aerial`/`Falling.*`/`CantDodge`/`CantAttack` Tag → 添加 `Grounded`
2. `Coordinator→OnLanded()` → `CurrentState = "Idle"` + 清除 `Combo.Branch.*` Tag
3. AnimBP 自然检测 `Grounded` Tag → 播放 Landing→Idle 过渡

> 着陆不是 GA——不产生伤害、不消耗资源、不需要 Montage。只是状态复位。落地后的攻击由协调器正常匹配 `StateName="Idle"` 的 `FComboNode` 行触发。

### GameplayTag 扩展

```
Combat.State.Aerial                               ← 已有
Combat.State.Aerial.Falling                       ← 下落态
Combat.State.Aerial.Falling.Default               ← 兜底 Pose
Combat.State.Aerial.Falling.IG_AirDodge
Combat.State.Aerial.Falling.IG_JumpSlash
Combat.State.Aerial.Falling.IG_PoleVault
Combat.State.Aerial.Falling.IG_DanceJump
Combat.State.Aerial.Falling.IG_KinsectSlide
Combat.State.Aerial.Falling.IG_KinsectSlashHit
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

```
EnhancedInput → InputAction → Tag → ASC→OnInputActionTriggered(Tag)
  → 武器Tag? → Coordinator→HandleWeaponInput
  → 非武器Tag? → TryActivateAbilityByTag
```

## FAbilityInputBinding — 输入-技能绑定

```
USTRUCT(BlueprintType)
struct FAbilityInputBinding
```

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| InputAction | TObjectPtr\<UInputAction\> | nullptr | EnhancedInput 的 InputAction 资产 |
| AbilityTag | FGameplayTag | 空 | 触发时激活的 Ability Tag，也作为蓄力 GA 的 Completed 事件标识 |
| bConsumeInput | bool | true | 触发后是否消耗此次输入（防止一个按键触发多个 Ability） |

## UMHGZAbilitySystemComponent — 扩展 ASC

```
UCLASS()
class UMHGZAbilitySystemComponent : public UAbilitySystemComponent
```

扩展 UE 原生 ASC，增加输入绑定和批量授予能力。

| 成员 | 类型 | Category | 默认值 | 说明 |
|------|------|----------|--------|------|
| InputBindings | TArray\<FAbilityInputBinding\> | "Input" | 空 | 输入绑定列表（策划在蓝图中配置） |
| CoreAbilities | TArray\<TSubclassOf\<UGameplayAbility\>\> | "Ability\|Core" | 空 | 核心能力列表（BeginPlay 时自动授予） |
| CoreAttributeEffects | TArray\<TSubclassOf\<UGameplayEffect\>\> | "Ability\|Core" | 空 | 核心 GE 列表（BeginPlay 时自动 Apply） |

### 核心方法

- `void InitializeAbilitySystem()`
  - 作用：依次执行：(1) 设置初始 GameplayTag（`Combat.State.Sheathed`、`Combat.State.Grounded`），(2) 授予 CoreAbilities，(3) Apply CoreAttributeEffects，(4) 遍历 `InputBindings` 建立 `ActionToTag`，绑定 EnhancedInput 的 `Started` 和 `Completed`。回调从 `FInputActionInstance::GetSourceAction()` 查回 Tag。
  - `CoreAbilities` / `CoreAttributeEffects` 的具体内容由 PlayerState 蓝图配置；当前源码和 Content 中没有 `GA_Sprint`，冲刺由 Character 处理。
    ```cpp
    for (auto& Binding : InputBindings)
    {
        ActionToTag.Add(Binding.InputAction, Binding.AbilityTag);
        EnhancedInput->BindAction(Binding.InputAction.Get(), ETriggerEvent::Started,
            this, &UMHGZAbilitySystemComponent::OnInputActionTriggered);
        EnhancedInput->BindAction(Binding.InputAction.Get(), ETriggerEvent::Completed,
            this, &UMHGZAbilitySystemComponent::OnInputActionCompleted);
    }
    ```

- `void OnInputActionTriggered(const FInputActionInstance& Instance)`
  - 作用：从 SourceAction 查 `ActionToTag`。武器 Tag 转发给 Active Coordinator，非武器 Tag 调用 `TryActivateAbilitiesByTag`。

- `void OnInputActionCompleted(const FInputActionInstance& Instance)`
  - 作用：若 ASC 持有 `Combat.State.Charging`，发送 `Combat.Event.ChargeReleased`。当前 EventData 不携带 InputTag，且没有已实现的消费 Ability。

- `void GrantWeaponAbilities(TArray<TSubclassOf<UGameplayAbility>> Abilities)`
  - 输入：武器授予的能力类列表。
  - 作用：授予并存储 Handle → `EquipmentComponent→OnEquipmentChanged` 时调用。

- `void RemoveWeaponAbilities()`
  - 作用：移除所有武器授予的能力（切换武器时调用）。

- `void BindInputAction(UInputAction* Action, FGameplayTag AbilityTag)`
  - 输入：InputAction 资产、Ability Tag。
  - 当前作用：只更新 `InputBindings` 数组；若 ASC 已完成输入初始化，不会立即调用 EnhancedInput `BindAction` 或更新现有 `ActionToTag`，动态补绑仍需实现。
  - 注意：限制攻击/不可操作场景不通过解绑实现——GAS 的 `CanActivateAbility` 通过 GameplayTag 阻塞拦截激活。

### 统一派发流程

```
EnhancedInput (所有按键/摇杆)
  → FInputActionInstance.SourceAction → ActionToTag → AbilityTag
    → Tag.MatchesTag("Input.Weapon") ?
        ├── 是 → 查找 Active 的 GA_WeaponComboCoordinator
        │        → Coordinator→HandleWeaponInput(AbilityTag)
        │        → 协调器内部：同步收集候选 → 条件/优先级匹配 → TryActivateAbility(Handle)
        │
        └── 否 → ASC→TryActivateAbilityByTag(AbilityTag)
                 → GAS 标准路径
```

### Ability 分类与输入归属

| 类别 | 示例 | 授予方 | 输入绑定 |
|------|------|--------|----------|
| 核心能力 | 移动、闪避、边缘跳越、交互 | ASC→CoreAbilities（BeginPlay） | ASC→InputBindings（EdgeVault 例外——由 UMHGZEdgeVaultComponent 代码触发，不绑定按键） |
| 武器连招 | Y/B/RT/组合键 | 装备时 ASC→GrantWeaponAbilities | ASC→InputBindings（IA_Y→Input.Weapon.Y），由 `OnInputActionTriggered` 判别为武器 Tag 后转发给协调器 |
| 特殊动作 | 钩爪、探测、拍照 | 快捷栏手动分配 | 经快捷栏→UseAction→Ability |
| 消耗品 | 喝药、投掷 | 快捷栏自动登记 | 经快捷栏→UseAction→Ability |

## UMHGZGameplayAbility — Ability 基类

```
UCLASS(BlueprintType, Blueprintable, Abstract)
class UMHGZGameplayAbility : public UGameplayAbility
```

所有 Ability 的基类，统一处理耐力消耗、冷却、输入标签。

| 成员 | 类型 | Category | 默认值 | 说明 |
|------|------|----------|--------|------|
| InputTag | FGameplayTag | "Ability\|Input" | 空 | 绑定的输入标签（`Input.Weapon.Y`/`Input.Weapon.B`/`Input.Dodge`…） |
| StaminaCost | FScalableFloat | "Ability\|Cost" | 0 | 单次耐力扣除量（闪避/单次攻击）。`ActivateAbility` 时一次扣除：`Cost × StaminaDeductionRate` |
| StaminaCostRate | FScalableFloat | "Ability\|Cost" | 0 | 持续耐力消耗速率。当前用 0.1s Timer，每次扣 `Rate × ConsumptionRate × 0.1` |
| bIsContinuous | bool | "Ability\|Cost" | false | 是否持续型 Ability；当前协调器设为 true，GA_Dodge 为单次型。GA_Sprint/GA_Aim 不存在 |
| CooldownDuration | FScalableFloat | "Ability\|Cooldown" | 0 | 字段已定义，当前未使用 |
| CooldownTag | FGameplayTag | "Ability\|Cooldown" | 空 | 激活时作为 Loose Tag 添加，当前没有定时移除逻辑 |
| bRequiresWeaponResource | bool | "Ability\|Cost" | false | 是否需要武器专属资源 |
| WeaponResourceCost | FScalableFloat | "Ability\|Cost" | 0 | 消耗的资源量 |
| MaxCorrectionAngle | float | "Ability\|Correction" | 30.0 | 攻击激活瞬间最大方向修正角度（以角色朝向为基准，扭向摇杆方向）。0=禁止修正 |
| AudioIdentityTag | FGameplayTag | "Ability\|Audio" | 空 | 挥刀风声身份标签（如 `Audio.Swing.LS_VerticalSlash`）。GA 蓝图必配——`ActivateAbility` 时以此为 Key 查 `WeaponDef.SwingSoundOverrides`，命中则覆盖 `DamageConfig.SwingSound`。不同招式用不同 GA 蓝图→不同 Tag→不同音效，武器覆盖是可选增量 |

> **FScalableFloat：** 所有 `FScalableFloat` 字段统一关联全局 CurveTable `DT_AbilityScalars`，Ability 只需指定行名（RowName）。`StaminaCost` 是 GA 的实际耐力扣除量；`FComboNode::StaminaRequired` 是协调器的匹配门槛，不负责扣耐。

### 核心方法（覆写）

- `bool CanActivateAbility(...) const override`
  - 输出：是否可激活。
  - 作用：检查耐力是否够 → 检查武器资源是否够 → 检查冷却是否结束 → 任一不满足返回 false（GAS 自动处理 UI 提示）。

- `void ActivateAbility(...) override`
  - 作用：
    - **单次型（bIsContinuous=false）**：计划扣除 `StaminaCost × StaminaDeductionRate`；当前 `DeductStaminaOnce` 被无效的 `MakeOutgoingSpec(nullptr)` 条件包住，可能不会执行 ApplyMod。`WeaponResourceCost` 也尚未消费。
    - **持续型（bIsContinuous=true）**：启动 0.1s Timer，固定步长扣除 `Rate × StaminaConsumptionRate × 0.1`；耐力归零后取消 Ability。

- `void EndAbility(...) override`
  - 作用：清理动画状态、结束冷却计时器。

## UMHGZAttackAbility — 攻击 Ability 中间层

```
UCLASS(BlueprintType, Blueprintable, Abstract)
class UMHGZAttackAbility : public UMHGZGameplayAbility
```

继承 `UMHGZGameplayAbility`，统一封装所有攻击类 Ability 的**碰撞检测**、**命中过滤**、**伤害 GE 构造与 Apply**。蓝图子类只需配置参数，不写逻辑代码。攻击 Montage 统一启用 `bEnableRootMotion=true`——动画数据驱动位移，CMC 移动输入被 RootMotion 覆盖（见 §移动实现-RootMotion）。

### 核心配置结构

#### FAttackCollisionConfig — 单段碰撞配置

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| TraceMeshComponentTag | FName | WeaponTrace | 优先定位参与轨迹检测的武器 SkeletalMeshComponent |
| AttachSocketName | FName | 必填 | 碰撞体挂载的骨骼 Socket（如 "weapon_tip"、"hand_r"） |
| TraceStartSocketName | FName | 空 | 长武器轨迹起点；留空时只扫 AttachSocketName |
| Shape | EAttackCollisionShape | Sphere | 碰撞形状（Sphere / Capsule / Box） |
| ShapeExtent | FVector | (20,20,20) | 形状参数：Sphere→X=Radius；Capsule→X=Radius+Z=HalfHeight；Box→HalfExtent |
| CollisionChannel | TEnumAsByte\<ECollisionChannel\> | GameTraceChannel1 | 碰撞通道（默认 Weapon 通道） |
| HitzoneQueryTag | FGameplayTag | 空 | 限定碰撞仅检测带此 Tag 的组件。空=不限制（检测所有碰撞） |
| TraceSampleCount | int32 | 3 | 长武器根/中/尖采样数，范围 1~8 |
| bDrawDebug | bool | false | 绘制 Sweep 轨迹用于校准 |

#### FAttackDamageConfig — 单段伤害配置

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| DamageEffectClass | TSubclassOf\<UGameplayEffect\> | nullptr | 伤害 GE 蓝图（策划在编辑器中配置） |
| MotionValue | FScalableFloat | 1.0 | ★ 动作值（倍率），参与伤害计算：`Damage = AttackPower × MotionValue × HitzoneDefense`。轻击 0.8 / 重击 1.5 / 终结技 3.0 |
| BaseStaggerValue | FScalableFloat | 0 | ★ 基础破坏值。参与硬直计算：`Stagger = BaseStaggerValue × StaggerMultiplier × MonsterHitzoneStaggerRate`。为 0 则该段不造成硬直 |
| KnockbackAngle | float | 0 | 击退方向（相对攻击者朝向，0=前方，180=击飞） |
| KnockbackForce | FScalableFloat | 0 | 击退力度 |
| HitStaggerTag | FGameplayTag | 空 | 硬直等级（`Combat.Stagger.Light` / `Medium` / `Heavy`） |
| DamageSetByCallerTag | FGameplayTag | 空 | SetByCaller 伤害值 Tag（GE 中用此 Tag 读取动态伤害值） |
| bUseHitzoneDefense | bool | true | 是否按命中部位的 `DefenseMultiplier` 修正伤害。怪物侧每个 hitzone 碰撞体持有 `DefenseMultiplier`（肉质）和 `StaggerRate`（硬直肉质） |
| bRequiresHitToContinue | bool | false | 招式内空挥截断：为 true 时，本段碰撞窗口结束后检查 `HitTargets`——若该段空挥，提前 `EndAbility`。同时也是 `ShouldContinueAfterHit()` 的默认判断依据 |
| OnHitSelfEffect | TSubclassOf\<UGameplayEffect\> | nullptr | 命中时对自身施加的 GE（如虫棍三灯）。仅首次命中时 Apply 一次 |
| HitCueTag | FGameplayTag | 空 | 物理命中 Cue Tag。当前加入 DynamicAssetTags，GameplayCue 自动路由尚未接通 |
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
| Collision | FAttackCollisionConfig | — | 本段碰撞参数（形状/通道/过滤） |
| Damage | FAttackDamageConfig | — | 本段伤害参数（动作值/破坏值/击退） |
| MultiHitCount | int32 | 1 | ★ 单次碰撞产生的伤害跳数。默认 1=命中即造成 1 次伤害。>1=命中后每隔 `MultiHitInterval` 秒造成一次伤害，共 `MultiHitCount` 次（如登龙剑下劈：1 次碰撞判定 × 7 跳伤害） |
| MultiHitInterval | float | 0.1 | 多次伤害之间的间隔（秒）。仅 `MultiHitCount>1` 时有效 |
| MaxWarpAngle | float | 30.0 | ★ 本段 MotionWarping 允许的最大旋转修正角度（度）。与 GA 的 `MaxCorrectionAngle` 区分：GA 的管控"激活时第一段扭头"，段的 `MaxWarpAngle` 管控"段内 Montage 播放期间 MotionWarping 的旋转上限"。多段招式每段可不同（见切段0=180°后撤、段1=120°回砍）。0=该段不做旋转 Warp |

### 攻击 GA 成员

| 成员 | 类型 | Category | 默认值 | 说明 |
|------|------|----------|--------|------|
| AttackSegments | TArray\<FAttackSegmentConfig\> | "Attack" | 空 | ★ 多段攻击配置——每段独立配置碰撞 + 伤害 + 多跳。替代原来分离的 `CollisionConfigs` + `DamageConfig`，解决非数组成员（Damage/MotionValue/Stagger 等）无法随段变化的问题 |
| MaxCorrectionAngle | float | "Attack\|Correction" | 30.0 | ★ 攻击激活瞬间（第一段）的最大方向修正角度（度）。读摇杆方向，若偏离 ≤ 此值则设 MotionWarping RotationTarget 扭向目标。段内 MotionWarping 的修正上限由 `FAttackSegmentConfig::MaxWarpAngle` 控制，两者独立。0=禁止修正 |
| HitTargets | TMap\<AActor*, FName\> | — | 空 | 已命中的怪物→首个接触的 hitzone 骨骼名（Key=Actor, Value=BoneName）。**每段 `EnableCollision` 清空，各段独立记录——段0命中头部、段1命中尾部互不干扰。** 同一段内同怪物只记录首个接触部位 |
| CurrentSegmentIndex | int32 | — | 0 | 当前正在执行的段索引（运行时状态） |
| bHasHitThisActivation | bool | — | false | 本次 GA 激活后是否已有命中。用于首次命中时触发一次性逻辑（通知协调器 + Apply OnHitSelfEffect），避免多段/多怪重复触发 |

### 关键方法（覆写/新增）

- `void ActivateAbility(...) override`
  - 作用：基类扣耐力/资源 → `ASC→AddLooseGameplayTag(Combat.State.Attacking)` → **方向修正**（`GetLastMovementInputVector` → 若长度 ≥ 0.1 且与角色朝向夹角 ≤ `MaxCorrectionAngle` → `MotionWarpingComponent→AddOrUpdateWarpTarget` → 播放 Montage）→ 等待 `AnimNotifyState_AttackCollision` 控制碰撞窗口。

- `void EndAbility(...) override`
  - 作用：**幂等清理——正常结束、Cancel、异常销毁均经此路径**（GAS 保证 `Cancel→EndAbility` 调用链，无需在 `CancelAbility` 中重复）。`ASC→RemoveLooseGameplayTag(Combat.State.Attacking)` → 查找 `GA_WeaponComboCoordinator` → `Coordinator→OnAttackFinished()` → `DisableCollision`（内部 `ClearTimer(MultiHitTimer)`，幂等安全）→ 清理动画状态、结束冷却。

- `void EnableCollision(int32 SegmentIndex = 0)`
  - 输入：段索引。
  - 作用：设置段索引、清空 `HitTargets`，按组件 Tag/Socket 找到轨迹 Mesh，缓存前一帧起止点并立即执行首帧零距离 Sweep。当前不会创建临时碰撞组件。
  - 后续判定：`AnimNotifyState_AttackCollision::NotifyTick` 每帧调用 `TickCollision`，在前后帧 Socket 位置之间执行 `SweepMultiByChannel`；长武器按 `TraceSampleCount` 采样。
  - **多跳伤害（MultiHitCount>1）：** 首帧 Sweep 命中后启动 `MultiHitTimer`，每隔 `MultiHitInterval` 秒对 `HitTargets` 中所有怪物调用 `ApplyDamage(HitActor, BoneName, SegmentIndex)`，共 `MultiHitCount` 次。`DisableCollision` 或 GA 结束 → 清除 Timer。
  - 调用方：`UAnimNotifyState_AttackCollision→NotifyBegin`。

- `void DisableCollision()`
  - 作用：关闭 Sweep、清除 `MultiHitTimer`。若当前段要求命中但 `HitTargets` 为空，则调用 `ShouldContinueAfterHit()`，返回 false 时提前结束。
  - 调用方：`UAnimNotifyState_AttackCollision→NotifyEnd`。

- `void ProcessSweepHit(const FHitResult& Hit)`
  - 当前过滤：忽略自身和已命中 Actor；命中组件必须是 `UMHGZMonsterHitzoneComponent`；配置了 `HitzoneQueryTag` 时要求 Exact Match。当前没有队友、无敌或死亡过滤。

- `void ApplyDamage(AActor* Target, FName HitzoneBoneName, int32 SegmentIndex)`
  - 输入：目标 Actor、命中部位骨骼名、段索引。
  - 作用：
    1. 直接使用 `HitStopBase` 设置攻击者 `CustomTimeDilation=0.05`，Timer 到期恢复
    2. 调用 `MakeDamageSpec(Target, HitzoneBoneName, SegmentIndex)` 构造 GE Spec
    3. `SourceASC→ApplyGameplayEffectSpecToTarget(Spec, TargetASC)`；当前 GC 自动路由未接通
    4. 读 `CameraShakeClass`+`CameraShakeScale` → `ClientStartCameraShake`
    5. **首次命中时（`bHasHitThisActivation==false`）：** 设 `bHasHitThisActivation=true` → 通知协调器 `GA_WeaponComboCoordinator→OnAttackHit()`（触发 `PendingGrantedTags` 授予）→ 若段 `Damage.OnHitSelfEffect` 非空则 Apply 到自身 ASC
    6. 多段碰撞/多怪物场景下，后续命中跳过步骤 5。**多跳伤害（MultiHitCount>1）每次 Tick 都执行步骤 1-4（Apply 伤害 GE），但不重复触发首次命中逻辑。**
- `FGameplayEffectSpecHandle MakeDamageSpec(AActor* Target, FName HitzoneBoneName, int32 SegmentIndex)`
  - 输入：目标 Actor、命中部位骨骼名、段索引。
  - 输出：构造好的 GE Spec。
  - 作用：
    1. `ASC→MakeOutgoingSpec(AttackSegments[SegmentIndex].Damage.DamageEffectClass)`
    2. 写入 `Damage.MotionValue` 与 `Damage.BaseStagger` SetByCaller；最终伤害由 `UMHGZDamageExecCalc` 计算
    3. 若配置，向 DynamicAssetTags 加入 HitzoneTag、`HitStaggerTag`、`HitCueTag`、`ElementalCueTag`
    4. 始终把 `GameplayCue.Hit.DamageNumber` 加入 DynamicAssetTags
    5. 将 Hitzone 位置写入 GameplayEffectContext 的 HitResult
    6. `DamageSetByCallerTag`、Knockback、SwingSound 和 `MaxWarpAngle` 当前未被消费；暴击 GC 也未实现

- `bool ShouldContinueAfterHit() const` (BlueprintNativeEvent)
  - 输出：当前碰撞窗口命中后，是否继续下一段碰撞窗口。
  - 默认实现：若 `AttackSegments[CurrentSegmentIndex].Damage.bRequiresHitToContinue && HitTargets.IsEmpty()` → return false；否则 return true。
  - **蓝图覆写场景——登龙剑：** 覆写此函数 → 读 ASC 的武器资源（气刃槽色阶）→ 若色阶 < 白 → return false → `EndAbility` 提前，第二段不播放。
  - 调用时机：`DisableCollision` 内，下一段 `EnableCollision` 之前。

- `bool CheckWeaponResourceForAbility() const` (BlueprintNativeEvent)
  - 输出：当前武器资源是否满足此 Ability 的消耗要求。
  - 默认返回 true。**各武器 GA 子类自行覆写**——查询各自武器特有的资源系统（气刃槽/瓶计数/蓄力等级等）。

### Ability 继承层级

当前层级：`UGameplayAbility` → `UMHGZGameplayAbility` → `UMHGZAttackAbility` / `UMHGZDodgeAbility`，虫棍再由 `UMHGZInsectGlaiveAbility` 继承攻击基类。`UMHGZEdgeVaultAbility`、`GA_Sprint`、`GA_Heal` 为规划。

### GameplayCue 集成 — MakeDamageSpec

当前 `MakeDamageSpec` 把这些 Tag 放入 `DynamicAssetTags`，且没有自定义 `UMHGZGameplayCueManager`/GC 资产，因此下表是保留的 GameplayCue 接入方案，不是当前运行链路。

| 步骤 | Tag 来源 | 说明 |
|:--:|------|------|
| 1 | `FAttackDamageConfig::HitCueTag` | 物理命中类型（必设——`Slash` / `Blunt`） |
| 2 | `FAttackDamageConfig::ElementalCueTag` | 元素附魔（可选——留空则跳过） |
| 3 | ExecCalc 内部 `ASC→AddGameplayCue(Hit.Crit)` | 暴击——在 ExecCalc 判定暴击后单独触发 |
| 4 | `GameplayCue.Hit.DamageNumber` | 伤害数字——始终追加，值通过 `Parameters.RawMagnitude` 传递 |



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
  ├── UMHGZChargeBladeAbility              ← ★ 盾斧: 瓶计数+盾充能
  └── UMHGZSwitchAxeAbility                ← ★ 斩斧: 充能槽
```

每个武器基类（~50 行 C++）持有 `UMHGZWeaponResourceComponent*` 子类引用，覆写 `CheckWeaponResourceForAbility()`（资源门控）和 `ShouldContinueAfterHit()`（招内击中派生）。武器基类管 GA **激活后**的内部逻辑，FComboNode 管**激活前**的匹配条件。

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
| **跨 GA 派生**（一个 GA 结束→下一个 GA） | `FComboNode::GrantedTags` + 协调器 | GA 之间 |

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

**设计原则：** 不改变"每个招式 = 一个 GA + 一个 Montage"的架构。衔接动画放在**下一个招式的 Montage 开头**作为 Entry Section，GA 激活时根据协调器的 `PreviousState` 选择对应入口。

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
    // 查询协调器：上一个招式是什么？
    FName PreviousState = Coordinator->GetPreviousState();

    // 根据来源选择入口 Section
    FName EntrySection;
    static const TMap<FName, FName> EntryMap = {
        { "Idle",       "Entry_From_Idle"       },
        { "Slash_101",  "Entry_From_Slash101"   },
        { "Dodge",      "Entry_From_Dodge"      },
    };
    EntrySection = EntryMap.FindRef(PreviousState, FName("Entry_Default"));

    // 从入口 Section 播放，播完自动进入 "Attack"
    PlayMontageAndWait(Montage, EntrySection);
}
```

> **Entry_Default：** 当 `PreviousState` 无匹配时走此兜底 Section——通常是一个极短的过渡段或直接空 Section，依赖 UE5 内置 Inertialization 做骨骼惯性混合。

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
| **Entry Section**（本节） | 每个 Montage 多个入口 Section | GA 激活时选 Section（基于 `PreviousState`） | 招式间的衔接过渡 |
| **Inertialization** | 不需要额外 Section | UE5 自动 | Pose 接近的招式间过渡 |

## 蓄力式攻击（规划）

蓄力不进连招表路由——全程在一个 GA 内部闭环。`bIsContinuous=true`，按住累积 `ChargeLevel`（通过曲线/参数控制递增速率），ASC 持有 `Input.Modifier.Charging` Tag。松开（Completed 事件）→ ASC 的 `OnInputActionCompleted` 检查 `Combat.State.Charging` Tag → 若存在则 `HandleGameplayEvent(Combat.Event.ChargeReleased, InputTag=AbilityTag)`。蓄力 GA 通过 `AbilityTrigger` 监听此 Event → 根据 `ChargeLevel` 分支选 Montage 和 `DamageConfig` → 方向修正（`MaxCorrectionAngle` 通常设 60°）→ 播放释放 Montage。不同等级使用不同 `AttackSegments` 配置，不创建多个 GA 蓝图子类。

> **优势：** 蓄力 GA 被 Cancel（受击/死亡）时 `Charging` Tag 已移除 → Completed 事件检查 Tag 不存在 → 不发送 `ChargeReleased` 事件 → 蓄力 GA 不会在被打断后意外释放。不再需要遍历 ActiveAbilities 查找蓄力 GA。

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

**UMHGZWeaponResourceComponent（基类，挂载到 PlayerState）** — 动态创建/销毁，切换武器时旧状态全部清空。与 ASC 同宿主，零跨 Actor 引用。

| 成员/接口 | 类型 | 说明 |
|------|------|------|
| WeaponTypeTag | FGameplayTag | 关联武器种类 |
| GetCurrentValue() | virtual float | 当前资源量（各子类覆写） |
| GetMaxValue() | virtual float | 资源上限 |
| Consume(float Amount) | virtual bool | 消耗资源，返回是否足够 |
| Restore(float Amount) | virtual void | 回复资源 |
| GetNormalizedValue() | float | 0~1 归一化值（UI 绑定用） |
| ApplyEntryModifier(FGameplayTag AttributeTag, float Value, EGameplayModOp Op) | virtual void | 词条修饰器——按 AttributeTag 前缀路由到内部倍率参数 |
| ClearAllEntryModifiers() | virtual void | 全量重建时清空所有修饰器 |
| GetModifiedParam(FName ParamName) | virtual float | 求值活跃修饰器——对基础参数应用所有已注册修饰器 |
| PlayResourceSound(USoundBase* Sound) | void | 工具方法——子类在资源变化时调用，统一走 `UGameplayStatics::PlaySound2D`（UI 反馈用 2D 音效） |

> 基类不预设音效槽位——子类各自持有 `UPROPERTY` 音效成员，通过 `PlayResourceSound()` 播放。词条/装备对资源的加成走 GE 修改资源组件的倍率参数（非 AttributeSet）。

### 各武器子类

| 子类 | 特有字段 | 特殊逻辑 | 音效成员 |
|------|----------|----------|----------|
| `URes_LongSword`（规划） | `ESpiritLevel Level`（无/白/黄/红）、`float Amount`、`FTimerHandle DecayTimer` | 击中回复量不同、等级随时间和命中升降、衰减 Timer | `GaugeFillSound` / `LevelUpSound`（白→黄→红） / `LevelDownSound` / `DepleteSound` |
| `URes_InsectGlaive`（部分实现） | `KinsectActor/KinsectData`、`KinsectStamina/MaxKinsectStamina`、Regen/Hover/Flight 三个倍率、白90/黄120/红60常量、`TripleUpDuration=90`、`TMap<FGameplayTag,FActiveGameplayEffectHandle> ActiveExtractHandles`、`TripleUpHandle`、`bTripleUpActive` | 萃取、三灯、消耗与耐力逻辑已有 C++；资源组件创建、猎虫 Spawn、GA/GE 资产仍未接线 | `ExtractCollectedSound` / `TripleUpActivatedSound` / `TripleUpExpiredSound` / `KinsectDepletedSound` |
| `URes_ChargeBlade`（规划） | `int32 PhialCount`(0~6)、`bool ShieldCharged`、`FTimerHandle RedShieldTimer` | 瓶被动不消耗、部分招式主动消耗、红盾有时限 | `PhialLoadSound` / `ShieldChargeSound` / `PhialBurstSound` / `OverheatSound` |
| `URes_SwitchAxe`（规划） | `float ChargeGauge`(0~1) | 连续值充能 | `GaugeChargedSound`（充能就绪） / `SwordModeActivateSound` / `SwordModeDeactivateSound` |

> `FWeaponResourceConfigRow` 同时桥接 ResourceComponentClass 与 ResourceWidgetClass，但当前 `DT_WeaponResourceConfig` 资产不存在。虫棍已声明 `OnKinsectStaminaChanged`、`OnExtractTimeUpdated`、`OnTripleUpChanged`；其中 ExtractTimeUpdated 尚未广播，UI 也未接线。

## GA_Dodge — 翻滚/闪避 Ability（不进连招表）

```
UCLASS(BlueprintType, Blueprintable)
class UMHGZDodgeAbility : public UMHGZGameplayAbility
```

继承 `UMHGZGameplayAbility`（非 `UMHGZAttackAbility`——翻滚不涉及攻击碰撞和伤害）。通过 `TryActivateAbilityByTag(Input.Dodge)` 激活，**不进连招协调器，不占 FComboNode 行**。



### GA_Dodge 自身配置（蓝图可编辑）

| 成员 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| SheathedDodgeMontages | TMap\<EComboDirection, TSoftObjectPtr\<UAnimMontage\>\> | 空 | 收刀态各方向翻滚 Montage（所有武器共用，Key=None=无方向翻滚，Forward/Back/Left/Right=方向翻滚）。无敌帧区间由动画师在 Montage 上拖拽 `AnimNotifyState_DodgeWindow` 控制 |

### FWeaponDodgeConfig — 武器翻滚配置

存于 `DT_WeaponDodgeConfig` DataTable。

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| WeaponTypeTag | FGameplayTag | 必填 | 武器种类（主键） |
| UnsheathedMontages | TMap\<EComboDirection, TSoftObjectPtr\<UAnimMontage\>\> | 空 | 拔刀态各方向翻滚 Montage（Key=None=无方向翻滚，Forward/Back/Left/Right=方向翻滚）。无敌帧区间由动画师在 Montage 上拖拽 `AnimNotifyState_DodgeWindow` 控制，不在此处配数字 |

> 耐力消耗使用基类 `UMHGZGameplayAbility::StaminaCost`。

### 执行流程

```
GA_Dodge::CanActivateAbility：
  → 检查 ASC 不含 Combat.State.Hitstun / Knockdown
  → 若 ASC 有 Combat.State.Attacking 但无 Combat.State.DodgeAcceptOpen → 阻塞（攻击中但不在翻滚窗口）
  → 检查 Stamina ≥ StaminaCost（基类字段）
  → ✅ 通过 → 激活
  // 纯 Tag 检查：无武器时 Attacking 不存在 → 翻滚始终可用

GA_Dodge::ActivateAbility：
  → 基类扣耐力（StaminaCost × StaminaDeductionRate）
  → 读 ASC 的 Sheathed/Unsheathed 标签：
      Sheathed → 使用 `SheathedDodgeMontages`（GA_Dodge 自身配置，所有武器共用）
      Unsheathed → 通过 `UMHGZDataManager::FindWeaponDodgeConfig(WeaponTypeTag)` 获取 FWeaponDodgeConfig → 使用 UnsheathedMontages
  → 读摇杆方向 → 从 Montages 中选对应 Montage
     （无方向 → None 键；有方向 → 按象限匹配 Forward/Back/Left/Right；无匹配 → 回退 None）
  → 播放 Montage（含 AnimNotifyState_DodgeWindow 控制无敌帧）
  → Montage 播完 → EndAbility
```

### 翻滚与连招的关系

- 翻滚激活时 GAS 自动取消当前攻击 GA → 协调器的 `SafetyTimer`（GlobalComboTimeout）兜底回 Idle
- 翻滚无敌帧由独立的 `AnimNotifyState_DodgeWindow` 控制

### DT_WeaponDodgeConfig — 武器翻滚配置表

| 列名 | 类型 | 说明 |
|------|------|------|
| WeaponTypeTag | FGameplayTag | 武器种类（主键） |
| DodgeConfig | FWeaponDodgeConfig | 翻滚参数（各方向 Montage + 无敌帧区间）。注意：耐力消耗由基类 `UMHGZGameplayAbility::StaminaCost` 管理，不在此结构中 |

### 翻滚窗口与取消

`AnimNotifyState_DodgeAcceptWindow` 控制翻滚可用性：窗口内 ASC 持有 `Combat.State.DodgeAcceptOpen` Tag，`GA_Dodge::CanActivateAbility` 检查 `Attacking` 有 + `DodgeAcceptOpen` 无 → 阻塞。取消动作（虫棍收虫等）设 `bRequiresWindowOpen=false` + `RequiredTags` 含 `Combat.State.DodgeAcceptOpen`，与翻滚共用取消窗口。

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

挂载到 PlayerController 或 Character。**仅负责 IMC（InputMappingContext）生命周期管理**——添加/移除/切换映射上下文。EnhancedInput 与 Ability 的绑定由 ASC 的 `InputBindings` 统一管理，InputComponent 不持有绑定数据。

| 成员 | 类型 | Category | 默认值 | 说明 |
|------|------|----------|--------|------|
| InputMappingContext | TArray\<UInputMappingContext\> | "Input" | 空 | 默认 IMC 列表（BeginPlay 时添加到 EnhancedInputSubsystem） |

### 核心方法

- `void InitializeInput(APlayerController* PC)`
  - 输入：PlayerController。
  - 作用：将 `InputMappingContext` 添加到 EnhancedInputSubsystem → 通知 ASC 调用 `InitializeAbilitySystem()`（ASC 内部遍历自己的 `InputBindings` 完成 EnhancedInput 绑定）。仅调用一次。

- `void PushInputMappingContext(UInputMappingContext* IMC, int32 Priority = 0)`
  - 输入：映射上下文、优先级。
  - 作用：运行时叠加额外的 IMC（如进入载具后切换一套按键映射）。不影响 ASC 已有绑定。

- `void PopInputMappingContext(UInputMappingContext* IMC)`
  - 输入：映射上下文。
  - 作用：移除之前 Push 的 IMC，恢复默认映射。

> InputComponent 只管 IMC 生命周期（"哪些按键可用"），ASC 的 `InputBindings` 管 IA→Tag→Ability 映射（"按键触发什么技能"）。限制攻击/不可操作场景通过 GAS 的 `CanActivateAbility` GameplayTag 阻塞，不解绑输入。

## UMHGZWeaponComboData — 连招表 DataAsset

```
UCLASS(BlueprintType)
class UMHGZWeaponComboData : public UPrimaryDataAsset
```

每武器种类一个，策划在编辑器中配置完整连招图（有向图，允许环）。

| 成员 | 类型 | 说明 |
|------|------|------|
| WeaponTypeTag | FGameplayTag | 关联武器种类 |
| ComboTable | TArray\<FComboNode\> | 连招节点列表 |
| GlobalComboTimeout | float | 全局兜底超时，默认 10 秒 |

### FComboNode 结构体

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| StateName | FName | 必填 | 当前所处的具体招式名（"Idle" / "RisingSlash" / "DoubleSlash" / "TornadoSlash"…），非抽象段位编号。`"Idle"` 为起手待机态 |
| bMatchAnyState | bool | false | 为 true 时忽略 StateName 匹配——匹配任意招式状态（含 Idle）。用于纳刀、起跳等通用招式。若需排除特定招式状态，使用 `BlockedStateNames`；若需排除受击/击倒等，使用 `BlockedTags` |
| BlockedStateNames | TArray\<FName\> | 空 | **仅 `bMatchAnyState==true` 时生效**——排除这些源状态名。空数组=匹配任意状态（原行为）。用于"任意派生但不可从 Idle/收刀态起手"（如太刀特殊纳刀）或"排除特定招式"的场景。黑名单模式——只需列出不允许的少数状态 |
| InputTag | FGameplayTag | 必填 | 触发条件（`Input.Weapon.Y` / `Input.Weapon.B` / `Input.Weapon.RT` 等） |
| DirectionalInput | EDirectionalInput | None | 字段已定义，当前协调器尚未消费；方向匹配为后续方案 |
| NextState | FName | 必填 | 命中后跳转到的招式名（可指向自身或前序招式，有向图允许环） |
| AbilityClass | TSubclassOf\<UGameplayAbility\> | nullptr | 触发的 GA 类；当前不限于 AttackAbility |
| StaminaRequired | float | 0 | 耐力门槛——连招匹配时协调器检查 `CurrentStamina ≥ Required`。**不负责扣耐**——实际扣除由 GA 的 `UMHGZGameplayAbility::StaminaCost` 在 ActivateAbility 中执行 |
| RequiredTags | FGameplayTagContainer | 空 | 激活前提——ASC **必须持有全部**这些 Tag（AND）。含状态标签：`Grounded/Unsheathed`（地面招式）、`Aerial/Unsheathed`（空中招式）、`Sheathed`（拔刀攻击）。Buff/PowerUp 也在此列 |
| BlockedTags | FGameplayTagContainer | 空 | 激活阻止——ASC **必须不持有任一**这些 Tag（NOR）。用于排除特定状态：登龙剑设 `BlockedTags={Combo.Branch.PostRoundslash}`，大回旋 `GrantedTags` 含此 Tag → 登龙无法从大回旋后派生 |
| GrantedTags | FGameplayTagContainer | 空 | **GA 首次命中后**由协调器授予的临时 Tag，供后续节点 RequiredTags/BlockedTags 判断（非激活时立即授予）。空挥则 GrantedTags 不生效 → 依赖此 Tag 的后续节点匹配失败 → 空挥断连 |
| bRequiresHitToGrantTags | bool | false | 字段尚未被读取；当前 PendingGrantedTags 总是在 `OnAttackHit` 时授予 |
| bRequiresWindowOpen | bool | false | 字段尚未被读取；当前协调器不检查 ComboWindowOpen |
| Priority | int32 | 0 | 显式匹配优先级。同层（精确招式/通用招式 + DirectionalInput）内有多个候选行满足 InputAction 条件时，Priority 高的优先匹配 |
| bAutoTransition | bool | false | 字段已定义但 `OnAutoTransition` 尚未实现；以下 ε 转移流程作为后续方案保留 |

#### 尚未接入字段的目标行为（保留方案）

- `DirectionalInput`：在玩家按下输入时快照方向，以角色前向为基准划分 Forward/Back/Left/Right；具体方向节点优先于 None。
- `bRequiresWindowOpen`：为 true 时仅在 `Combat.State.ComboWindowOpen` 存在时匹配；为 false 时允许收虫、纳刀等取消动作绕过连招窗口，但仍受 RequiredTags 约束。
- `bRequiresHitToGrantTags`：为 true 时仅首次命中后授予 GrantedTags；为 false 时计划允许激活即授予，从而支持空挥派生。
- `bAutoTransition`：目标是允许 InputTag 为空的 ε 转移；GA 命中或播放完成后通过 `OnAutoTransition(NextState)` 请求协调器激活下一节点。

### 出招表数据模型

`ComboTable` 定义了 FSM 的有向转移图。`StateName` 是状态，`NextState` 是转移目标。当前输入字段名为 `InputTag`；`bAutoTransition` 对应的 ε 转移仍是后续方案。

`ComboTable` 是平面数组。当前 `StateIndex` 只索引精确 StateName；`bMatchAnyState` 节点在每次输入时遍历数组并检查 `BlockedStateNames`。`"*"` 桶是可选的后续优化方案。

### 与装备系统的对接

`EquipmentComponent→ApplyItemEffects` 当前从 `DT_WeaponComboConfig` 同步 `LoadSynchronous` ComboData，收集 `ComboTable` 中 AbilityClass 并授予，再 `GiveAbilityAndActivateOnce` 协调器，最后 `InjectComboData` 构建 StateIndex。异步 RequestID 方案保留在 [entries.md](entries.md)。

### 复合输入与修饰态

| 模式 | 示例 | 实现 | FComboNode 如何区分 |
|------|------|------|---------------------|
| 长按修饰+点按 | 按住 LT 瞄准时按 B | LT→Hold trigger→设 `Input.Modifier.Aiming` Tag；B 照常触发 | 同一 InputAction（B）+ 不同 RequiredTags（空 vs `Aiming`）匹配不同行 |
| 同时按 | Y+B | `IA_YB` + `UInputTriggerChordAction` 引用 IA_Y 和 IA_B | 独立 InputTag `Input.Weapon.YB`，与 Y、B 不冲突 |
| 嵌套长按 | 按住 RT 蓄力 + 按住 LT 瞄准 | 两个独立 Hold trigger，各管各的 Tag | `RequiredTags={Charging, Aiming}` 匹配蓄力瞄准态招式 |

> **核心原则：** 不创建组合爆炸的 InputAction——修饰态走 GameplayTag（`Input.Modifier.*`），真正的同时按键走 Chord Trigger。

### EDirectionalInput 象限规则（规划，协调器尚未消费）

以角色前向为基准 ±45° 分 4 象限。Forward/Back 优先级高于 Left/Right——对角线（45°）归 Forward。无输入或向量长度 < 0.1 视为 None。翻滚使用相同规则。

| 值 | 角度范围 |
|----|------|
| None | 不检测方向（长度 < 0.1） |
| Forward | [-45°, +45°] |
| Back | [135°, 180°] ∪ [-180°, -135°] |
| Left | (45°, 135°) |
| Right | (-135°, -45°) |

## GA_WeaponComboCoordinator — 连招协调器（基础匹配已实现，扩展流程为规划）

```
UCLASS(BlueprintType, Blueprintable)
class UGA_WeaponComboCoordinator : public UMHGZGameplayAbility
```

协调器是 `InstancedPerActor`、`LocalOnly`、`bIsContinuous=true` 的持续 Ability。装备时通过 `GiveAbilityAndActivateOnce` 激活，`InjectComboData` 后构建索引；卸装时取消。它不直接绑定 EnhancedInput，只接收 ASC 转发的 `HandleWeaponInput`。

### 运行时状态（非 UPERTY，协调器内部维护）

| 成员 | 类型 | 说明 |
|------|------|------|
| CurrentState | FName | 当前所处的招式名（初始 "Idle"） |
| PreviousState | FName | 上一个成功激活的招式名（初始 `"Idle"`）。GA 在 `ActivateAbility` 中通过 `Coordinator→GetPreviousState()` 读取，用于选择 Montage 的入口 Section（Entry Section） |
| ComboData | TObjectPtr\<UMHGZWeaponComboData\> | 当前武器的连招表 |
| StateIndex | TMap\<FName, TArray\<int32\>\> | 当前只索引 `bMatchAnyState=false` 的 StateName；通用节点输入时线性遍历 |
| ComboTimeoutTimer | FTimerHandle | 使用 ComboData 的 `GlobalComboTimeout`；到期回 Idle 并清 PendingGrantedTags |
| PendingGrantedTags | FGameplayTagContainer | 当前激活的 GA 待授予的 GrantedTags。GA 激活时存入（非立即应用），GA 首次命中时由 `OnAttackHit()` 写入 ASC；若 GA 结束仍未命中则丢弃 |

> `PendingInputs`、帧批处理、PreInput 缓冲、ActiveLoadRequestID 均为保留方案，当前类没有这些成员。

规划成员明细：

| 成员 | 类型 | 目标语义 |
|------|------|----------|
| PendingInputs | TArray\<FGameplayTag\> | 累积当前帧武器输入，下一帧统一按 Chord/单键优先级处理 |
| InputBatchTimer | FTimerHandle | 0 秒延迟到下一帧执行批处理 |
| PreInputTag | FGameplayTag | ComboWindow 打开前的单槽预输入，后输入覆盖前输入 |
| PreInputTimestamp | float | 预输入捕获时刻，用于判定有效期 |
| PreInputLifetime | float | 预输入有效窗口，原方案默认 0.15 秒 |
| ActiveLoadRequestID | FGuid | 异步加载令牌，只接受最新武器的 ComboData 回调 |

### 公共方法

- `void InjectComboData(UMHGZWeaponComboData* Data)`
  - 当前作用：保存同步加载的 ComboData，构建精确状态索引并重置超时。

- `void SetComboData(UMHGZWeaponComboData* InData, FGuid RequestID)`（规划）
  - 与 `DataManager::RequestWeaponComboData` 配套：校验 RequestID，丢弃过期换装请求，再构建 StateIndex。完整竞态方案见 [entries.md](entries.md) 的 RequestID 章节。

- `void HandleWeaponInput(FGameplayTag AbilityTag)`
  - 输入：武器输入 Tag（`Input.Weapon.Y` / `Input.Weapon.B` 等）。
  - 作用：由 ASC 的 `OnInputActionTriggered` 在判别为武器 Tag 时调用。若 ComboData 未注入（StateIndex 为空）→ 忽略。帧批处理收集 → 排序 → 匹配 → 激活 GA。

- `FName GetPreviousState() const`
  - 输出：上一个成功激活的招式名。
  - 作用：GA 在 `ActivateAbility` 中调用——根据来源招式选择 Montage 的入口 Section（Entry Section）。

- `void OnAttackHit()`
  - 作用：由攻击 GA 的 `ApplyDamage` 在首次命中时调用。将 `PendingGrantedTags` 写入 ASC。

- `void OnAttackFinished()`
  - 当前作用：清空 ActiveAttackHandle；仅当 CurrentState 不在 StateIndex 且不是 Idle 时回 Idle，不清 Branch Tag。

- `void OnAutoTransition(FName NextStateName)`（规划，当前无此方法）
  - 输入：自动转移目标的招式名。
  - 作用：GA 命中/播完时主动调用——不经过玩家输入，直接按 `StateName=NextStateName` 从 `StateIndex` 查找对应 GA 类并激活。内部流程：`PreviousState = CurrentState` → 查 `FindAbilityClassForState(NextStateName)` → `ASC→TryActivateAbilityByClass(NextGA)` → `CurrentState = NextStateName` → 重置 `SafetyTimer`。调用方为 GA 子类的命中回调或 `ShouldContinueAfterHit` 覆写。
  - 前提：`NextStateName` 对应的连招表行必须设 `bAutoTransition=true`，否则不激活（防止非法自动转移）。

- `TSubclassOf<UGameplayAbility> FindAbilityClassForState(FName StateName) const`
  - 输出：指定招式名对应的 Ability 类。
  - 作用：从 `StateIndex[StateName]` 中取出首个节点的 `AbilityClass`（所有同名节点共享同一个 AbilityClass）。若 `StateIndex` 中无此键或 AbilityClass 为空 → 返回 nullptr。

### 运行时工作流

**阶段 A（当前装备流程）：** `EquipmentComponent→OnEquipmentChangedInternal()` → 从 DT 同步加载 ComboData → `GrantWeaponAbilities` → `GiveAbilityAndActivateOnce(UGA_WeaponComboCoordinator)` → `InjectComboData` 构建 StateIndex。

**阶段 B（起手攻击）：** `HandleWeaponInput` → 候选行 = `StateIndex["Idle"] ∪ StateIndex["*"]` → 四级排序（`bMatchAnyState=false > true`；`DirectionalInput` 具体 > None；`Priority` 降序）→ 遍历检查 6 条件（`InputAction`/`DirectionalInput`/`RequiredTags`/窗口或起手/`BlockedTags`/`StaminaRequired`）→ 匹配成功则 `PreviousState = CurrentState` → `ActivateAbility` + 更新 `CurrentState`（新招式名）+ 存入 `PendingGrantedTags` + 重置 `SafetyTimer`（时长为 `GlobalComboTimeout`）。

**阶段 C（GA 执行）：** GA `ActivateAbility` 扣耐力/播 Montage → `AttackCollision→NotifyBegin` 调 `EnableCollision` → Sweep 命中 → `ApplyDamage` → `NotifyEnd` 调 `DisableCollision` → `ComboWindow→NotifyBegin` 加 `ComboWindowOpen` Tag。

**阶段 D（连招下一段）：** 窗口内 `HandleWeaponInput` → `StateIndex[CurrentState] ∪ StateIndex["*"]`（`"*"` 桶中过滤 `BlockedStateNames.Contains(CurrentState)` 的行）→ 匹配成功则取消旧 `SafetyTimer` → `PreviousState = CurrentState` → `ActivateAbility` → 更新 `CurrentState` → 重启 `SafetyTimer`（时长为 `GlobalComboTimeout`）。GA 首次命中时 `OnAttackHit()` 将 `PendingGrantedTags` 写入 ASC。

**阶段 E（回 Idle）：** `ComboWindow→NotifyEnd` 移除 Tag → Montage 播完 → GA `EndAbility` → `Coordinator→OnAttackFinished()` → 若 `CurrentState` 未变更（未被 `HandleWeaponInput` 或 `OnAutoTransition` 改变）则回 `"Idle"` + 清除 `Combo.Branch.*` Tag。

**阶段 F（异常兜底）：** `SafetyTimer` 到期（`GlobalComboTimeout` 秒，仅 Montage 卡死等极端情况）→ 强制 `CurrentState="Idle"` + 清除所有 `Combo.Branch.*` Tag。武器卸下：清除所有 `Combo.Branch.*` Tag + StateIndex 清空。

**阶段 G（自动转移）：** GA 命中/播完 → `Coordinator→OnAutoTransition(NextStateName)` → 检查 `StateIndex` 中是否含此状态 → 目标行 `bAutoTransition==true` 才激活 → `PreviousState=CurrentState` → 查 `FindAbilityClassForState` → `ActivateAbility` → `CurrentState=NextStateName` → 重置 `SafetyTimer`。与阶段 B/D 共享同一状态变更路径，不绕过协调器。

### 两种转移路径对照

| | **输入转移**（阶段 B/D） | **自动转移**（阶段 G） |
|------|------|------|
| 触发源 | 玩家按键 | GA 事件（命中/播完） |
| 协调器方法 | `HandleWeaponInput` | `OnAutoTransition` |
| InputAction | 必填 | 可为空或忽略 |
| 匹配方式 | 四级排序匹配 | 直接按 StateName 查找 |
| 出招表标记 | — | `bAutoTransition=true` |
| 示例 | 袈裟斩→二连斩（按 Y） | 操虫斩命中→命中后动画（自动） |

### 关键设计要点

- **帧级输入批处理：** `PendingInputs` 收集当前帧所有武器输入，下一帧统一按 Chord > 单键排序匹配
- **预输入缓冲：** `PreInputTag`（单槽，`PreInputLifetime=0.15s`），窗口打开时消费
- **节点匹配四级排序 + 匹配即停**
- **死亡处理：** `Combat.Event.Death` → `GA_Death` Cancel 所有 GA。猫车 = `SetActorLocation` + 设 Grounded+Sheathed
- **打断后自动恢复：** 监听 Hitstun/Knockdown Removed → 检查 `Input.Modifier.*` → 自动 `TryActivateAbilityByTag`
- **着陆重置——`Coordinator→OnLanded()`：** CMC 落地事件触发，将 `CurrentState` 强制复位 `"Idle"` + 清除 Branch Tag。确保地面连招表正确匹配起手招式
- **空中次数限制——Cant 权限模型（零交叉逻辑）：** `CantDodge`/`CantAttack` 两个 Tag 各自独立。`GA_AirDodge` 添加 `CantDodge`，`GA_AirAttack_*` 添加 `CantAttack`。无需 `Exhausted` 汇总——两个 Cant 同时存在 = 双阻塞。GAS 原生 `BlockedTags` 处理，GA 零覆写代码
- **FSM 两条转移路径不冲突：** 输入转移（`HandleWeaponInput`）和自动转移（`OnAutoTransition`）共享同一状态变更路径——都经过 `PreviousState=CurrentState` → 激活新 GA → `CurrentState=NextState`。GA 的 `EndAbility` 中 `OnAttackFinished()` 仅当 `CurrentState` 未被两者修改时才回 Idle
- **GA→GA 自动派生不走 `TryActivateAbilityByTag`：** 必须通过 `OnAutoTransition`——确保协调器感知状态变更、`PreviousState` 正确传递、`SafetyTimer` 更新。绕过协调器直接激活 GA 会导致状态不同步、Entry Section 选错、`OnAttackFinished` 误回 Idle

## AnimNotifyState 系列（当前仅 AttackCollision 与 DodgeWindow；其余为规划）

> 均为 C++ 类（非蓝图）。`UAnimNotifyState` 有 Begin/Tick/End 三阶段回调，适合需要"持续一段帧"的逻辑。

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
  → MeshComp→GetOwner()→GetAbilitySystemComponent()
  → ASC→AddLooseGameplayTag(Combat.State.ComboWindowOpen)
  → （可选）按 HapticLeadTime 启动震动计时器

NotifyEnd（窗口关闭）：
  → ASC→RemoveLooseGameplayTag(Combat.State.ComboWindowOpen)
  → 输入不再触发连招
  → 攻击 Montage 自然播放至 BlendOut → 动画回到 Idle 待机
```

> ComboWindow（AnimNotifyState, 0.1–0.5s）精确控制可接下一招的帧；GlobalComboTimeout（协调器字段, ~10s）为异常兜底。

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
  → MeshComp→GetOwner()→GetPlayerState()→FindComponentByClass<UAbilitySystemComponent>()
  → ASC→AddLooseGameplayTag(Combat.State.Invincible)
  → 将角色胶囊体对 Weapon、MonsterAttack 通道设为 Ignore

NotifyEnd：
  → ASC→RemoveLooseGameplayTag(Combat.State.Invincible)
  → 将胶囊体 Weapon、MonsterAttack 通道恢复为 Block（当前不是恢复缓存的原始值）
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
  → MeshComp→GetOwner()→GetAbilitySystemComponent()
  → ASC→AddLooseGameplayTag(Combat.State.DodgeAcceptOpen)

NotifyEnd：
  → ASC→RemoveLooseGameplayTag(Combat.State.DodgeAcceptOpen)
```

> ComboWindow 接受连招输入（0.1–0.5s）；DodgeAcceptWindow 接受翻滚输入（0.1–0.7s，延伸到收招帧）。DodgeAcceptWindow 在攻击 Montage，DodgeWindow 在翻滚 Montage。

### UAnimNotifyState_PoiseWindow — 霸体窗口

```
UCLASS(BlueprintType, Blueprintable)
class UAnimNotifyState_PoiseWindow : public UAnimNotifyState
```

挂载在攻击 Montage 中。`NotifyBegin` → ASC 添加 Poise Tag（如 `Combat.Poise.Heavy`）；`NotifyEnd` → 移除。攻击动画师在此区间放置，覆盖招式的大开大合期。

### UAnimNotifyState_AttackCollision — 攻击碰撞窗口

```
UCLASS(BlueprintType, Blueprintable)
class UAnimNotifyState_AttackCollision : public UAnimNotifyState
```

挂载在攻击 Montage 中。`NotifyBegin` → 通过 `MeshComp→GetOwner()→GetAbilitySystemComponent()` 获取当前 Active Ability → Cast 到 `UMHGZAttackAbility` → `EnableCollision(ConfigIndex)`；`NotifyEnd` → `DisableCollision()`。判定窗口可短至 1-2 帧；多段攻击在 Montage 中放多个独立 NotifyState 各自负责一段判定。

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
