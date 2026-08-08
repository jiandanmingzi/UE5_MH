# 虫棍资源系统（操虫棍·Insect Glaive）

> **实施状态说明（以源码、配置和 Content 为准）：** 本文完整保留猎虫、耐力、三灯、特殊技、UI 与词条方案。当前只有 C++ 骨架和单次虫棍地面攻击资产；猎虫资源组件尚未由配置创建，送虫/召回 GA、萃取 GE、猎虫伤害 GE、GameplayCue 和 Widget 资产均未落地，因此完整猎虫流程当前不可运行。

## 当前实现

| 模块 | 当前状态 |
|------|----------|
| C++ 类型 | `AKinsect`、`UKinsectCollisionComponent`、`UInsectGlaiveKinsectData`、`URes_InsectGlaive`、`UMHGZInsectGlaiveAbility` 已存在。 |
| 装备接入 | `DT_WeaponResourceConfig` 资产及配置路径不存在；装备系统不会创建 `URes_InsectGlaive`。`OnWeaponEquipped` 没有调用者，所以猎虫不会自动 Spawn。 |
| GA/连招资产 | 当前虫棍只有 `GA_IG_BaDao`、`GA_IG_R_TuCI`，`DA_IG_Combo` 只配置了 Y→`GA_IG_R_TuCI` 的最小节点；没有 Send/Recall/特殊技 GA。 |
| 猎虫与萃取资产 | 猎虫 Mesh 已存在，但 Kinsect DataAsset、White/Yellow/Red/TripleUp GE、`GE_KinsectDamage` 均不存在；代码中的硬编码加载会失败。 |
| UI/反馈 | AimComponent C++ 射线检测已实现；Crosshair/三灯/耐力 Widget 与 GameplayCue 资产不存在。当前把 Cue Tag 加入 DynamicAssetTags，不会形成已接通的 GC 自动路由。 |
| 运行时接线缺口 | `URes_InsectGlaive` 挂载目标是 PlayerState，但 `DeployKinsect` 把 Owner 直接 Cast 为 Pawn；召回到达后未重新 Attach，PendingExtractColor 也未清空；这些问题需在启用猎虫前修复。 |

**设计原则：** 基于现有 GAS 架构，虫棍资源系统分为**四大子系统**——**猎虫实体**（独立 `AKinsect` Actor：骨骼模型+动画+碰撞+飞行移动）、**猎虫耐力**（`URes_InsectGlaive` 组件内管理）、**三灯萃取**（持续时间 GE + GameplayTag 状态机）、**消耗灯特殊技**（`FComboNode::RequiredTags` 分支 + `ShouldContinueAfterHit` 招内派生）。红灯改连招通过出招表 Tag 分支实现，协调器零改动。三灯特殊命中音效通过 `UMHGZInsectGlaiveAbility` 覆写向 DamageSpec 注入额外 GameplayCue Tag。

---

## 系统总览（目标架构；当前仅 C++ 骨架）

```
AKinsect (独立 Actor, 由 URes_InsectGlaive 管理生命周期)
├── USkeletalMeshComponent (猎虫品种骨骼模型)
├── UProjectileMovementComponent (飞行移动——bAutoActivate=false，手动控制 Velocity；悬停时 Velocity=0)
├── UKinsectCollisionComponent (胶囊体: 飞行时 Weapon 通道 = Overlap)
├── 动画预留 (当前只有 FlyPlayRate 字段，没有 AnimInstance 播放逻辑)
├── 伤害控制: EKinsectDamageMode + CurrentMotionValue + DamageInterval
├── 萃取控制: EKinsectExtractMode + PendingExtractColor
└── 品种数据: UInsectGlaiveKinsectData (DataAsset)

URes_InsectGlaive (WeaponResourceComponent, 挂载到 PlayerState)
├── 猎虫生命周期: Spawn → AttachToArm → Deploy(FVector) → Hover → Recall → Destroy
├── 目标坐标管理: 瞄准射线→WorldLocation / 收刀RT直飞→Forward×Distance
├── 猎虫耐力管理 (KinsectStamina: 悬停扣减 / 休息回复 / 归零自动召回)
├── 猎虫伤害管理 (ApplyKinsectDamage → 借玩家 ASC 走统一 GE 管道)
├── 萃取状态机 (召回时 Apply → Apply Duration GE → 三灯检测)
├── 三灯激活与保护 (替换单独灯 GE、不可刷新、到期全部消失)
├── 灯消耗接口 (ConsumeExtract → 移除 GE)
└── 词条加成接收 (ApplyEntryModifier → ActiveModifiers Map)

UMHGZInsectGlaiveAbility (攻击基类, 继承 UMHGZAttackAbility)
├── 覆写 CanActivateAbility (检查指定灯是否存在)
├── 覆写 MakeDamageSpec 后处理 (三灯时加入 GameplayCue.IG.TripleUpActivated Asset Tag)
├── 辅助方法: CheckExtractRequirement / ConsumeExtractAndApplyBurst
└── 通过 GetIGResourceComponent() 在 PlayerState 组件中查找 URes_InsectGlaive

FComboNode (出招表, 策划配置)
├── 红灯前节点: 无 RequiredTags → 标准连招
├── 红灯节点: RequiredTags={Combat.Branch.Extract.Red} → 强化连招
└── 三灯节点: RequiredTags={Combat.Branch.TripleUp} → 终结技入口

Duration GE (萃取 Buff)
├── GE_IG_WhiteExtract  → MoveSpeedMultiplier ×1.15
├── GE_IG_YellowExtract → Defense ×1.1 + Combat.Poise.Light
├── GE_IG_RedExtract    → AttackPower ×1.2 + Combat.Branch.Extract.Red
└── GE_IG_TripleUp      → 全属性 ×1.25 + Combat.Poise.Medium (不可刷新)
```

---

## 零、猎虫实体——独立 Actor 建模与碰撞

### 为什么猎虫需要独立建模和碰撞

虫棍与其他武器有一个本质区别：**猎虫是独立于玩家角色的可交互实体**。它的行为模式——从玩家手臂飞出、穿越空间追踪怪物部位、命中后携带萃取返回——跨越了三个 Actor（玩家→怪物→玩家），无法用简单的"投射物碰撞检测"概括：

| 需求 | 抽象方案的问题 | 独立 Actor 方案 |
|------|------|------|
| 视觉表现 | 无模型——只能画 UI 图标或粒子轨迹 | 完整骨骼模型+动画，不同品种有形体和颜色差异 |
| 飞行动画 | 无法表现——粒子/样条线无法做翅膀扇动、转向倾斜 | 单套飞行动画——前进/返回/悬停共用，通过 PlayRate 控制快慢 |
| 碰撞判定 | 依赖 `AnimNotifyState_AttackCollision` 逐帧创建/销毁临时碰撞体——猎虫飞行持续数秒，每帧 Create/Destroy 开销极高 | 碰撞体常驻于 Actor，飞行+悬停期间始终启用，性能友好 |
| 悬停萃取 | 无法表现"猎虫在怪物旁等待萃取时机"——投射物必须命中即返回 | 飞到目标坐标后悬停→碰撞体接近怪物→自动萃取→再返回。猎虫 Pawn=Ignore，不被怪物身体阻挡 |
| 装备可视化 | 猎虫停在手臂上时看不见 | AttachToComponent 吸附在玩家手臂 Socket，收刀状态下依然可见 |
| 品种差异化 | 不同品种只能改数值，外观无差异 | 不同品种→不同 SkeletalMesh + Material + 动画集 |

### AKinsect Actor 设计

```
UCLASS()
class AKinsect : public AActor
```

猎虫的独立 Actor 实体——拥有骨骼模型、飞行移动组件、碰撞体。由 `URes_InsectGlaive` 管理生命周期（Spawn / Attach / Deploy / Destroy），**不挂载 ASC**（猎虫不参与 GAS——萃取通过碰撞回调触发、耐力由 ResourceComponent Tick 管理）。

| 成员 | 类型 | Category | 默认值 | 说明 |
|------|------|----------|--------|------|
| Mesh | TObjectPtr\<USkeletalMeshComponent\> | "Kinsect\|Component" | CreateDefaultSubobject | 猎虫骨骼模型（品种 DataAsset 运行时注入） |
| Collision | TObjectPtr\<UKinsectCollisionComponent\> | "Kinsect\|Component" | CreateDefaultSubobject | 胶囊体碰撞——飞行时 Weapon=Overlap、WorldStatic=Block |
| Movement | TObjectPtr\<UProjectileMovementComponent\> | "Kinsect\|Component" | CreateDefaultSubobject | 飞行移动——`bAutoActivate=false`，手动控制 Velocity；无重力、可悬停 |
| State | EKinsectState | "Kinsect\|State" | Attached | 猎虫状态：Attached / Flying / Hovering / Returning / Recalled |
| OwnerActor | TWeakObjectPtr\<AActor\> | "Kinsect\|State" | nullptr | 玩家引用——收虫时每 Tick 读取实时坐标动态修正回归路径。`AttachToPlayer` 时设置 |
| bFollowRay | bool | "Kinsect\|State" | false | true=沿射线方向飞行（臂上放虫模式），false=直线飞向目标坐标（悬停放虫模式） |
| RayDirection | FVector | "Kinsect\|State" | ZeroVector | 臂上放虫时的飞行方向（相机准心射线方向）。bFollowRay==true 时有效 |
| FlyDestination | FVector | "Kinsect\|State" | ZeroVector | 悬停放虫时的目标坐标。bFollowRay==false 时有效 |
| PendingExtractColor | FGameplayTag | "Kinsect\|State" | 空 | 萃取暂存颜色——飞行中碰撞到怪物部位时记录，返回玩家后传递给 ResourceComponent |
| KinsectData | TObjectPtr\<UInsectGlaiveKinsectData\> | "Kinsect\|Data" | nullptr | 品种配置 DataAsset——运行时由 ResourceComponent 注入 |
| DamageMode | EKinsectDamageMode | "Kinsect\|Damage" | SingleHit | 伤害模式——SingleHit=命中 1 次即停；Piercing=按间隔持续伤害 |
| ExtractMode | EKinsectExtractMode | "Kinsect\|Damage" | FirstHitOnly | 萃取行为——NoExtract=不萃取；FirstHitOnly=仅首次命中记录；AlwaysOverwrite=高优先级覆盖 |
| CurrentMotionValue | float | "Kinsect\|Damage" | 1.0f | 当前送虫招式的动作值——由 GA 传入 |
| CurrentDamageInterval | float | "Kinsect\|Damage" | 0.12f | 贯穿伤害间隔（秒）——0 表示非贯穿。由 GA 传入 |
| TimeSinceLastDamage | float | "Kinsect\|Damage" | 999.0f | 距上次伤害的时间——初始极大值确保首帧可立即伤害 |
| bHasDealtDamage | bool | "Kinsect\|Damage" | false | 普通放虫是否已造成过伤害——SingleHit 模式命中后置 true，阻止后续伤害 |
| ResourceComponent | TWeakObjectPtr\<URes_InsectGlaive\> | "Kinsect\|Reference" | nullptr | 虫棍资源组件引用——用于 ApplyKinsectDamage 调用 |

### 关键方法

- `void StartFlightAlongRay(FVector RayDirection, float MaxDistance)`
  - 输入：相机准心射线方向、最大飞行距离。
  - 作用：Detach（若已 Attach）→ State = Flying, bFollowRay = true → `Movement->Velocity = RayDirection.GetSafeNormal() × FlightSpeed` → Collision Enable。猎虫沿射线方向直线飞出。
  - **终止条件：** 撞墙 → 悬停。普通放虫命中怪物 → 立即停止并悬停。贯穿放虫碰怪不停，撞墙或飞满 MaxDistance → 悬停。

- `void StartFlightToPoint(FVector Destination)`
  - 输入：目标世界坐标。
  - 作用：State = Flying, bFollowRay = false → `Movement->Velocity = (Destination - GetActorLocation()).GetSafeNormal() × FlightSpeed`。猎虫沿直线飞向目标坐标。
  - **终止条件：** 撞墙 → 悬停。普通放虫命中怪物 → 立即停止并悬停。贯穿放虫碰怪不停，撞墙或距离 < AcceptRadius → 悬停。

- `void SetDamageParams(EKinsectDamageMode InDamageMode, float InMotionValue, float InDamageInterval, EKinsectExtractMode InExtractMode = FirstHitOnly)`
  - 输入：伤害模式、动作值、贯穿间隔、萃取行为。
  - 作用：在 `DeployKinsect` 之后、`StartFlight*` 之前由 GA 调用，设置本次飞行的伤害与萃取参数。重置 `TimeSinceLastDamage=999`、`bHasDealtDamage=false`。

- `void TryApplyKinsectDamage(float DeltaTime)`
  - 作用：Tick 中调用——仅在 `State==Flying` + `DamageMode==Piercing` 时生效。检查 Overlap + 冷却 → 调用 `ApplyDamageOnce` + 重置冷却。普通放虫的伤害在 `OnHitMonsterHitzone` 回调中处理。

- `void TryRecordExtract(UMonsterHitzoneComponent* Hitzone)`
  - 输入：命中的怪物部位碰撞体。
  - 作用：根据 `ExtractMode` 决定是否记录/更新 `PendingExtractColor`。NoExtract→跳过；FirstHitOnly→仅首次记录；AlwaysOverwrite→高优先级覆盖（红 3 > 黄 2 > 白 1）。

- `void ApplyDamageOnce(UMonsterHitzoneComponent* Hitzone, float MotionValue)`
  - 输入：部位碰撞体、当前招式的动作值。
  - 作用：委托 `ResourceComponent->ApplyKinsectDamage(Hitzone, Monster, MotionValue)` 走玩家 ASC 的统一 GE 管道（复用现有 ExecCalc + GameplayCue）。

- `bool ShouldStopFlying() const`
  - 输出：本次飞行是否应立即终止。
  - 当前逻辑：仅 `SingleHit && bHasDealtDamage` 返回 true。撞墙由 `OnWorldCollision` 直接切悬停，射线模式的极限距离由 Tick 独立处理；点目标到达判定尚未实现。

- `void StopAndHover()`
  - 作用：`Movement->Velocity = FVector::ZeroVector`（立即停止）→ State=Hovering。有 PendingExtractColor 则保留，等待召回；无则等待玩家重新送虫。

- `void StartReturn()`
  - 当前作用：State = Returning；Returning Tick 每帧读取 `OwnerActor` 位置并手动更新 Velocity。距离 < 50cm 时回调 ResourceComponent，但当前回调尚未重新 `AttachToPlayer`。

- `void ForceRecall()`
  - 作用：耐力归零强制召回。调用 `StartReturn()`，**不清除 `PendingExtractColor`**——已萃取到的灯保留，召回后正常 Apply。

- `void Interrupt()`
  - 当前作用：`Movement->Velocity = FVector::ZeroVector`，重置 `bFollowRay`/`FlyDestination`。在重新放虫前调用；不修改 `PendingExtractColor`。

- `void AttachToPlayer(USceneComponent* ArmSocket)`
  - 输入：玩家手臂 Socket 组件。
  - 作用：`AttachToComponent(ArmSocket)` → State = Attached, OwnerActor = ArmSocket→GetOwner() → Collision Disable。

- `void EnableKinsectCollision()` / `void DisableKinsectCollision()`
  - 作用：飞行时启用 QueryOnly，并设 Weapon=Overlap、WorldStatic=Block；停用时设 NoCollision。

- `void OnHitMonsterHitzone(UMonsterHitzoneComponent* Hitzone)`
  - 输入：被命中的怪物部位碰撞体。
  - 作用：Overlap 回调——**仅在 `State == Flying` 时处理**。调用 `TryRecordExtract(Hitzone)` 按 `ExtractMode` 记录萃取颜色。若 `DamageMode==SingleHit` 且未伤害过→调用 `ApplyDamageOnce` + 设置 `bHasDealtDamage=true`（下一帧 Tick 中 `ShouldStopFlying` 会返回 true 停止飞行）。贯穿放虫的伤害由 `Tick→TryApplyKinsectDamage` 处理，此处不处理伤害。

- `void OnFlightEnded()`
  - 作用：飞行终止时调用——若 `PendingExtractColor` 有效→自动 `StartReturn()`（衔光球飞回）；无效→`StopAndHover()`（等待召回或重新送虫）。

- `UMonsterHitzoneComponent* GetOverlappingHitzone() const`
  - 输出：当前重叠的怪物部位碰撞体（若无返回 nullptr）。
  - 作用：Tick 中供 `TryApplyKinsectDamage` 查询当前 Overlap 状态。

- `void OnWorldCollision(const FHitResult& Hit)`
  - 输入：碰撞命中结果。
  - 作用：Hit 回调——**仅在 `State == Flying` 时处理**。猎虫撞到世界静态几何体（墙壁/建筑/地面）→ `Movement->Velocity = FVector::ZeroVector` → State = Hovering（就地悬停）。不会萃取（只有怪物部位触发萃取）。与 `OnHitMonsterHitzone` 互斥——若同一帧同时命中怪物和墙壁，怪物优先。

- `float GetFlightSpeed() const`
  - 输出：当前直接返回 `KinsectData->FlightSpeed`（无 Data 时回退 2000），尚未接入速度词条修正。

- `float GetHoverDrainRate() const` / `float GetFlightDrainRate() const`
  - 输出：当前耐力消耗速率。供 ResourceComponent Tick 读取。

### 猎虫碰撞组件（UKinsectCollisionComponent）

继承 `UCapsuleComponent`，复用怪物系统的碰撞通道设计模式：

| 通道 | 常态（停手臂） | 飞行/悬停中 | 召回中 |
|------|:--:|:--:|:--:|
| Weapon | Ignore | **Overlap** ← 怪物部位萃取判定 | Ignore |
| WorldStatic | Ignore | **Block** ← 碰撞世界几何体（墙壁/建筑）| Ignore |
| MonsterAttack | Ignore | Ignore | Ignore |
| Pawn | Ignore | Ignore（穿透玩家和怪物身体） | Ignore |
| Visibility | Ignore | Ignore | Ignore |

> **设计理由：**
> - **Weapon = Overlap**：怪物 `UMonsterHitzoneComponent` 设 Weapon=Block。**Overlap vs Block 产生 Overlap 事件** → `OnHitMonsterHitzone` → 萃取。若用 Block vs Block 则产生 Hit 事件且 `UProjectileMovementComponent` 会在怪物表面反弹——猎虫无法穿透怪物身体萃取。
> - **WorldStatic = Block**：飞行中撞到墙壁/建筑/地面时产生 Hit 事件 → `OnWorldCollision` → 立即停止飞行，就地悬停。防止猎虫穿透场景飞出地图。
> - **Pawn = Ignore**：猎虫太小、非物理实体，穿透玩家和怪物身体；猎虫不可受击。

#### 与 AnimNotifyState_AttackCollision 的区别

| | AnimNotifyState_AttackCollision（武器攻击） | UKinsectCollisionComponent（猎虫） |
|------|------|------|
| 检测生命周期 | NotifyBegin→NotifyEnd 之间启用每帧 Socket Sweep，不创建临时组件 | 胶囊组件常驻于 AKinsect，飞行期间 Enable，停手臂时 Disable |
| 适用场景 | 固定时长的 Montage 播放中的瞬时判定（0.1-0.5s 窗口） | 持续数秒飞行过程中的不定时命中 |
| 碰撞形状 | 可灵活配置（Sphere/Capsule/Box） | 固定胶囊体（猎虫形体近似） |
| 移动方式 | 跟随骨骼动画——碰撞体固定在 Socket 上随动画运动 | 独立飞行移动——`UProjectileMovementComponent` 驱动 |

### 猎虫品种系统（UInsectGlaiveKinsectData）

`UPrimaryDataAsset`，策划为每种猎虫品种创建一个资产：

| 字段 | 类型 | 说明 |
|------|------|------|
| KinsectDisplayName | FText | 品种名称（如"速度型猎虫·风牙"） |
| KinsectMesh | TSoftObjectPtr\<USkeletalMesh\> | 猎虫模型（不同品种不同外观——如甲虫型/蝴蝶型/蜻蜓型） |
| MaterialOverrides | TMap\<FName, TSoftObjectPtr\<UMaterialInstance\>\> | 按 SlotName 覆写材质（如属性颜色染色） |
| FlyMontage | TSoftObjectPtr\<UAnimMontage\> | 唯一飞行动画——前进/返回/悬停共用，通过 PlayRate 控制 |
| FlightSpeed | float | 基础飞行速度（cm/s） |
| StraightFlightDistance | float | 收刀 RT 直飞距离（cm，默认 1500） |
| StaminaPool | float | 基础耐力上限 |
| StaminaRegenRate | float | 基础耐力回复速率 |
| HoverDrainRate | float | 悬停耐力消耗速率（低于飞行耗耐——降低"飞过头"惩罚） |
| FlightDrainRate | float | 飞行耐力消耗速率 |
| KinsectAttackPower | float | 猎虫基础攻击力（默认 10.0）——当前品种未分化时所有猎虫共用。后续品种分化时可覆写 |

### 飞行轨迹机制——双模式（详细方案；GA 与装备接线未实现）

虫棍的猎虫飞行有两种输入模式：

**模式 A：臂上放虫——沿准心射线飞行（LT + Y）**

```
猎虫在手臂上（State == Attached）
  → 长按 LT → ASC 持有 Combat.State.Aiming
    → UMHGZAimComponent 每帧射线检测准心指向
    → 指向怪物部位 → WBP_Crosshair 显示对应萃取颜色（红/黄/白）+ 缩放动画
    → 指向空气/场景 → WBP_Crosshair 显示默认准心
  → 按 Y → GA_SendKinsect 激活
    → 读取 UMHGZAimComponent 当前相机朝向 + 萃取颜色
    → ResourceComponent->DeployKinsect()
      → Kinsect Actor Detach
      → ★ Kinsect->StartFlightAlongRay(CameraForward, MaxFlightRange)
        → 猎虫沿相机准心射线方向直线飞出（像曳光弹——轨迹=准心方向，而非猎虫→目标的连线）
        → 飞行途中碰撞到怪物 HitzoneComponent → 萃取
        → 超过 MaxRange 未命中 → Hovering
  → 按 B → GA_RecallKinsect → 收虫（可打断飞行，互斥中断）
```

> **为什么臂上放虫沿准心而非直线飞向目标：** 猎虫从手臂发射时应完美跟随玩家准心——这是玩家唯一能"瞄准"的方向。如果飞向射线 HitLocation，在近距离会出现猎虫"拐弯"的怪异轨迹。沿射线方向飞行确保轨迹始终从十字准心延伸出去，手感直接。

**模式 B：悬停放虫——直线飞向目标坐标（LT + Y）**

```
猎虫在悬停中（State == Hovering）或返回中（State == Returning）
  → LT 瞄准 → 按 Y → GA_SendKinsect 激活
    → 相机射线取 HitLocation（命中场景/怪物 → HitLocation；未命中 → CameraMaxRange）
    → ResourceComponent->DeployKinsect()
      → ★ 先调用 Kinsect->Interrupt()（中断当前悬停/返回）
      → ★ Kinsect->StartFlightToPoint(HitLocation)
        → 猎虫从当前悬停位置沿直线飞向目标坐标
        → 飞行途中碰撞到怪物 → 萃取
        → 到达 → Hovering
```

**模式 C：收刀直飞（RT）**

```
收刀态（Sheathed）
  → 按 RT → GA_DrawAndSendKinsect 激活
    → 先触发拔刀（若已持刀则跳过）
    → 目标坐标 = 玩家位置 + 玩家朝向 × StraightFlightDistance（品种 DataAsset 配置）
    → ResourceComponent->DeployKinsect(TargetLocation)
    → 猎虫直线飞出——不经过瞄准判定
```

**悬停机制：**

```
猎虫停止飞行（撞墙 / 飞满距离 / 普通放虫命中）
  → Movement->StopMovementImmediately()
  → State = Hovering
  → FlyMontage PlayRate = 0.3（慢速浮空——视觉上停在原地微振翅膀）
  → Tick 中每帧 KinsectStamina -= HoverDrainRate × Δt
  → Collision 组件保持 Enable（Weapon=Overlap）
  → ★ 若 PendingExtractColor 有效（命中过怪物且已记录萃取）→ 等待玩家召回（按 B 或耐力归零）
  → ★ 若 PendingExtractColor 无效（未命中任何怪物）→ 等待玩家重新送虫（按 Y）
  → ★ 悬停中不会造成伤害、不会触发新的萃取——萃取仅在飞行中发生
```

> **为何悬停不能萃取/伤害：** 萃取和伤害是猎虫飞行中"咬"到怪物表皮——需要飞行速度带来的接触判定。悬停时猎虫原地微振翅膀，即使怪物主动靠近也不会触发萃取或伤害。这要求玩家主动管理猎虫——精准瞄准怪物部位送虫、飞行途中命中、未命中则召回重新尝试。

### 目标坐标管理

**计算方：`URes_InsectGlaive`**（非 GA，非 Kinsect）

| 模式 | 当前猎虫状态 | 计算方式 | 调用 AKinsect 方法 |
|------|:--:|------|------|
| 臂上放虫（LT+Y） | Attached | 取相机朝向 `CameraForward` + `MaxFlightRange`（品种 DataAsset） | `StartFlightAlongRay(CameraForward, MaxRange)` |
| 悬停放虫（LT+Y） | Hovering/Returning | 相机射线 → HitLocation；无命中→CameraMaxRange | `StartFlightToPoint(HitLocation)` |
| 收刀直飞（RT） | Attached（收刀态） | PlayerForward × `StraightFlightDistance`（品种 DataAsset） | `StartFlightAlongRay(PlayerForward, StraightFlightDistance)` |

**传递方式：** `URes_InsectGlaive::DeployKinsect()` — ResourceComponent 内部判断当前 State 后选择正确的 AKinsect 方法。提供两个重载：无参版本（瞄准送虫——沿相机方向）和带方向/距离参数版本（收刀 RT 直飞——沿玩家朝向）。

```cpp
// ── 无参版本：瞄准送虫（LT+Y）——沿相机准心方向 ──
void URes_InsectGlaive::DeployKinsect()
{
    if (!KinsectActor) return;

    // ★ 互斥打断：若已在飞行/悬停/返回中，先中断
    if (bKinsectDeployed)
        KinsectActor->Interrupt();

    KinsectActor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

    EKinsectState CurrentState = KinsectActor->GetState();
    if (CurrentState == EKinsectState::Attached)
    {
        // 臂上放虫——沿准心射线飞出
        FVector CameraLoc, CameraDir;
        PlayerController->GetPlayerViewPoint(CameraLoc, CameraDir);
        KinsectActor->StartFlightAlongRay(CameraDir, KinsectData->MaxFlightRange);
    }
    else
    {
        // 悬停放虫——直线飞向目标坐标
        FVector Target = GetAimTargetLocation();  // 相机射线 HitLocation
        KinsectActor->StartFlightToPoint(Target);
    }

    KinsectActor->EnableCollision();

    // ★ I-2 修复：添加 Kinsect.Active Tag——供连招表匹配 GA_RecallKinsect
    if (UAbilitySystemComponent* ASC = GetPlayerASC())
    {
        ASC->AddLooseGameplayTag(
            FGameplayTag::RequestGameplayTag("WeaponResource.IG.Kinsect.Active"));
    }

    bKinsectDeployed = true;
}

// ── 带方向参数版本：收刀直飞（RT）——沿玩家朝向，不读相机 ──
void URes_InsectGlaive::DeployKinsectAlongDirection(FVector Direction, float Distance)
{
    if (!KinsectActor) return;

    if (bKinsectDeployed)
        KinsectActor->Interrupt();

    KinsectActor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
    KinsectActor->StartFlightAlongRay(Direction.GetSafeNormal(), Distance);
    KinsectActor->EnableCollision();

    if (UAbilitySystemComponent* ASC = GetPlayerASC())
    {
        ASC->AddLooseGameplayTag(
            FGameplayTag::RequestGameplayTag("WeaponResource.IG.Kinsect.Active"));
    }

    bKinsectDeployed = true;
}
```

> **★ GA 调用约定（I-5 修复）：** `GA_SendKinsect` / `GA_DrawAndSendKinsect` 在 `DeployKinsect()` 返回后、`EndAbility` 之前，**必须**调用 `Kinsect->SetDamageParams(DamageMode, MotionValue, Interval, ExtractMode)` 设置本次飞行的伤害与萃取参数。`DeployKinsect` 不负责设置伤害参数——伤害参数属于 GA 招式设计范畴（见 IG-12）。

### 猎虫生命周期（详细方案；当前没有调用入口）

```
装备虫棍
  → URes_InsectGlaive::OnWeaponEquipped()
    → SpawnActor<AKinsect>(KinsectClass)
    → Kinsect->AttachToPlayer(Character->GetMesh(), "Kinsect_Arm_Socket")
    → State = Attached（碰撞体 Disable，纯静态 Mesh 吸附——无闲置动画）

瞄准送虫（持刀 LT+Y，臂上）——★ 贯穿型
  → GA_SendKinsect → ResourceComponent->DeployKinsect()
    → 若 bKinsectDeployed → Kinsect->Interrupt()
    → Kinsect Detach
    → ★ Kinsect->SetDamageParams(Piercing, 1.0, 0.12s, AlwaysOverwrite)
    → StartFlightAlongRay(CameraForward, MaxRange)
    → State=Flying, bFollowRay=true, Collision Enable

悬停重新放虫（持刀 LT+Y，悬停中）——★ 贯穿型
  → GA_SendKinsect → DeployKinsect()
    → Kinsect->Interrupt()
    → ★ Kinsect->SetDamageParams(Piercing, 1.0, 0.12s, AlwaysOverwrite)
    → StartFlightToPoint(HitLocation)
    → bFollowRay=false, State=Flying

收刀直飞（收刀 RT）——★ 普通单发型
  → GA_DrawAndSendKinsect → 先 Unsheathe → DeployKinsect()
    → ★ Kinsect->SetDamageParams(SingleHit, 0.8, 0.0, FirstHitOnly)
    → StartFlightAlongRay(PlayerForward, StraightFlightDistance)

飞行中每 Tick（仅 State == Flying）：
  ├─ ★ 贯穿伤害：TryApplyKinsectDamage(DeltaTime)
  │     ├─ 检查 Overlap？→ 否 → 跳过
  │     ├─ 检查冷却？→ 未到 → 跳过
  │     └─ Overlap + 冷却到 → TryRecordExtract(Hitzone) + ApplyDamageOnce
  │
  ├─ 撞墙（WorldStatic Hit）→ StopAndHover()
  │
  ├─ 飞到极限距离？
  │     └─ ★ OnFlightEnded()
  │           ├─ 有 PendingExtractColor → StartReturn()（衔光球飞回）
  │           └─ 无 PendingExtractColor → StopAndHover()（等待召回）
  │
  └─ 普通放虫命中怪物（Overlap 回调）
        ├─ TryRecordExtract(Hitzone) → 按 ExtractMode 记录
        ├─ ApplyDamageOnce（1 次伤害）
        └─ bHasDealtDamage=true → 下帧 Tick 中 ShouldStopFlying→StopAndHover

召回（B / 耐力归零）——可打断飞行/悬停，不可打断返回
  → 若 State == Returning → 静默跳过（已在返回中）
  → 否则 → State = Returning → StartReturn()
    → ★ 每 Tick 读取 OwnerActor->GetActorLocation() 动态追踪玩家位置
    → 到达 → AttachToPlayer → State = Attached, Collision Disable
    → 若 PendingExtractColor 有效 → ResourceComponent->ApplyExtract(PendingExtractColor)
    → ★ I-2 修复：移除 Kinsect.Active Tag
      → ASC->RemoveLooseGameplayTag(WeaponResource.IG.Kinsect.Active)
    → ★ 耐力归零不会清空萃取——已萃取到的灯在召回后仍会 Apply
    → 若 PendingExtractColor 为空（飞行中未碰到怪物即召回）→ 萃取失败
    → bKinsectDeployed = false
    → Tick: KinsectStamina += StaminaRegenRate × Δt（回复）

卸下虫棍
  → URes_InsectGlaive::OnWeaponUnequipped()
    → Kinsect->Destroy()
```

### 猎虫动画

**现阶段不做猎虫动画**——纯 Mesh 飞行即可验证碰撞和萃取。后续需要时：单套飞行动画 `Montage_Kinsect_Fly`（翅膀扇动循环），前进/返回/悬停共用，通过 `PlayRate` 区分（Flying=1.5 / Returning=1.0 / Hovering=0.3），AnimBP 只需一个 Slot 节点播放 Montage。

---

## 零-A、猎虫伤害系统（C++ 骨架已存在；GE 资产与入口未实现）

**设计原则：** 猎虫不挂载 ASC、不新增 GA——伤害走玩家 ASC 的统一 GE 管道。伤害参数（动作值、贯穿间隔、萃取行为）由送虫 GA 传入，不存 DataAsset。飞行结束条件按 `EKinsectDamageMode` 区分：普通放虫命中即停，贯穿放虫碰怪不停、撞墙或飞满距离才停。

### 伤害枚举

```cpp
// 伤害模式
UENUM(BlueprintType)
enum class EKinsectDamageMode : uint8
{
    SingleHit,      // 普通放虫：飞行中只造成 1 次伤害，命中后立即停止飞行
    Piercing        // 贯穿放虫：按间隔持续造成伤害，碰怪不停止飞行
};

// 萃取行为
UENUM(BlueprintType)
enum class EKinsectExtractMode : uint8
{
    NoExtract,          // 不萃取（纯伤害贯穿）
    FirstHitOnly,       // 仅首次命中时记录萃取颜色（贯穿途中多次伤害但不更新）
    AlwaysOverwrite,    // 每次命中都记录，高优先级颜色覆盖低优先级（标准贯穿行为）
};
```

### 萃取颜色优先级

```
红(3) > 黄(2) > 白(1)
```

> 红灯同时提供最大 Buff 和连招分支——`AlwaysOverwrite` 模式下贯穿大型怪物时自然优先锁定红色。

### 飞行终止条件

| 条件 | 普通放虫 (SingleHit) | 贯穿放虫 (Piercing) |
|------|:--:|:--:|
| 撞墙 | ✅ 停止 | ✅ 停止 |
| 飞到极限距离 | ✅ 停止 | ✅ 停止 |
| 命中怪物 | ✅ 立即停止 | ❌ 继续飞行 |

停止后：有萃取→自动 `StartReturn()`（衔光球飞回）；无萃取→`StopAndHover()`（等待召回）。

### 伤害数据流

```
GA 送虫
  → Kinsect->SetDamageParams(DamageMode, MotionValue, Interval, ExtractMode)
  → Kinsect->StartFlight*()

飞行中 Tick（仅 State == Flying）：
  ├─ Piercing 模式：
  │     IsOverlappingMonsterHitzone()?
  │       ├─ 否 → 跳过
  │       └─ 是 → TimeSinceLastDamage >= DamageInterval?
  │               ├─ 否 → 跳过
  │               └─ 是 → TryRecordExtract(Hitzone)  ← 按 ExtractMode
  │                     → ApplyDamageOnce(Hitzone, MotionValue)
  │                     → TimeSinceLastDamage = 0
  │
  └─ SingleHit 模式（Overlap 回调触发，不在 Tick 中轮询）：
        OnHitMonsterHitzone(Hitzone)
          → TryRecordExtract(Hitzone)  ← 按 ExtractMode
          → ApplyDamageOnce(Hitzone, MotionValue)
          → bHasDealtDamage = true
          → 下帧 Tick 中 ShouldStopFlying() → true → StopAndHover()

ApplyDamageOnce 内部：
  → ResourceComponent->ApplyKinsectDamage(Hitzone, Monster, MotionValue)
    → PlayerASC->MakeOutgoingSpec(GE_KinsectDamage)
    → SetByCaller: "Damage.MotionValue" = MotionValue
    → SetByCaller: "Damage.AttackPower" = 猎虫基础攻击力（当前无攻击力词条修正）
    → AddDynamicAssetTag: HitzoneTag（部位信息）
    → PlayerASC->ApplyGameplayEffectSpecToTarget(Spec, MonsterASC)
    → ★ 走统一 ExecCalc → GameplayCue 自动触发（火花/音效/伤害数字）
```

### GA 调用对照

| GA | DamageMode | MotionValue | Interval | ExtractMode | 说明 |
|------|:--:|:--:|:--:|:--:|------|
| `GA_SendKinsect`（瞄准送虫） | Piercing | 1.0 | 0.12s | AlwaysOverwrite | 标准贯穿萃取 |
| `GA_DrawAndSendKinsect`（收刀直飞） | SingleHit | 0.8 | — | FirstHitOnly | 普通单发萃取 |
| 未来：贯穿强袭 | Piercing | 1.5 | 0.08s | NoExtract | 纯伤害无萃取 |
| 未来：速攻送虫 | Piercing | 0.7 | 0.15s | FirstHitOnly | 贯穿仅首录 |

### URes_InsectGlaive 新增方法

- `void ApplyKinsectDamage(UMonsterHitzoneComponent* Hitzone, AActor* Monster, float MotionValue)`
  - 输入：部位碰撞体、怪物 Actor、当前招式的动作值。
  - 作用：**借玩家 ASC** 构造 `GE_KinsectDamage` Spec → 通过 `SetByCaller` 传递 `MotionValue` 和 `KinsectAttackPower` → `ApplyGameplayEffectSpecToTarget` → 走统一 `ExecCalc` 计算最终伤害。GameplayCue 自动触发（火花/音效/伤害数字）。

- `float GetModifiedKinsectAttackPower() const`
  - 输出：猎虫基础攻击力 × 词条修正后的最终攻击力。

### KinsectData 新增字段（可选，暂不分化）

| 字段 | 类型 | 默认值 | 说明 |
|------|------|------|------|
| KinsectAttackPower | float | 10.0f | 猎虫基础攻击力——当前品种未分化时所有猎虫共用 |

### 新增 GameplayTag

```
Damage.MotionValue                   ← 猎虫伤害动作值（SetByCaller）
Damage.AttackPower                   ← 猎虫攻击力覆写（SetByCaller）
GameplayCue.Hit.Kinsect              ← 猎虫命中反馈（小号火花+音效）
```

### 新增 GE 蓝图

| GE | 类型 | 说明 |
|------|:--:|------|
| `GE_KinsectDamage` | Instant | 猎虫伤害 GE——ExecCalc 复用现有伤害计算，`SetByCaller` 接收 `MotionValue` 和 `AttackPower` |

---

## 一、猎虫耐力（逻辑已写入资源组件，尚未接入装备流程）

### 数据流

```
瞄准送虫（持刀 LT+Y，臂上）
  → GA_SendKinsect::ActivateAbility
    → ResourceComponent->DeployKinsect()
      → 若 bKinsectDeployed → Kinsect->Interrupt()
      → Kinsect Detach → StartFlightAlongRay(CameraForward, MaxRange)
      → bFollowRay = true
      → Tick: KinsectStamina -= FlightDrainRate × DeltaTime

悬停重新放虫（持刀 LT+Y，悬停中）
  → GA_SendKinsect → DeployKinsect()
    → Kinsect->Interrupt() → StartFlightToPoint(GetAimTargetLocation())
    → bFollowRay = false

收刀直飞（收刀 RT）
  → GA_DrawAndSendKinsect::ActivateAbility
    → 先 Unsheathe → DeployKinsect() → StartFlightAlongRay(PlayerForward, StraightFlightDistance)

飞行中猎虫碰撞体 Overlap 怪物 HitzoneComponent（仅 State == Flying 时）
  → Kinsect::OnHitMonsterHitzone(HitzoneComponent)
    → TryRecordExtract(Hitzone)  ← 按 ExtractMode 记录
    → 若 SingleHit 模式：ApplyDamageOnce + bHasDealtDamage=true → 下帧自动悬停
    → 若 Piercing 模式：不处理伤害（由 Tick 中 TryApplyKinsectDamage 处理），不停止飞行

召回（持刀 B / 耐力归零）
  → 若 Kinsect State == Returning → 跳过（已在返回中）
  → GA_RecallKinsect（或耐力归零自动 ForceRecall）
    → Kinsect->StartReturn() → 每 Tick 追踪 OwnerActor 实时位置
    → 到达 → AttachToPlayer(ArmSocket) → State = Attached
    → 若 PendingExtractColor 有效 → ApplyExtract(PendingExtractColor)
    → ★ ASC->RemoveLooseGameplayTag(WeaponResource.IG.Kinsect.Active)
    → bKinsectDeployed = false
    → Tick: KinsectStamina += StaminaRegenRate × DeltaTime

猎虫耐力归零（放出/悬停中）
  → URes_InsectGlaive::Tick 中内联判断 KinsectStamina <= 0
    → 播放 KinsectDepletedSound
    → Kinsect->ForceRecall()
    → ★ 耐力归零不清空萃取——已萃取到的灯保留
```

### 管理方式

- **归属**：`URes_InsectGlaive` 内部 `float KinsectStamina` / `float MaxKinsectStamina`，非 GAS Attribute（遵循决策 #43）
- **消耗速率**：飞行期间 `FlightDrainRate`、悬停期间 `HoverDrainRate`（< FlightDrainRate）
- **回复速率**：`StaminaRegenRate`（休息时——Attached 状态——每秒回复量）
- **Tick 启用**：构造函数设置 `PrimaryComponentTick.bCanEverTick = true`，`BeginPlay` 中兜底检查 `IsTickFunctionEnabled`。挂载在 PlayerState 上，单机下 Tick 正常执行（PlayerState 为 AActor 子类，其 Actor Tick 驱动所有已启用 Tick 的子组件）。
- **耐力归零检测——Tick 内联判断**：`URes_InsectGlaive::TickComponent` 中扣减耐力后直接 `if (KinsectStamina <= 0) → ForceRecall()`。不设独立 Timer 或事件——耐力只有一个修改源（Tick），一次 `<=0` 比较即可，零额外开销。
- **装备词条修改**：通过 `ApplyEntryModifier(WeaponResource.IG.HoverDrainRate, Value, Multiply)` 等路径（遵循决策 #78）
- **UI 显示**：`WBP_IG_KinsectStamina` 独立进度条，位于武器资源 UI 下方，订阅组件 Delegate 更新

---

## 二、三灯萃取（C++ 状态机已写，依赖的 GE 资产未创建）

### 萃取颜色映射

`MapHitzoneToExtract(FGameplayTag HitzoneTag) → FGameplayTag ExtractColor`（虚函数，支持猎虫品种覆写）：

| 怪物部位 | 萃取颜色 | Tag |
|----------|:--:|------|
| Head / TailTip | 红灯 | `WeaponResource.IG.Extract.Red` |
| Torso / Wings / Back / Neck | 黄灯 | `WeaponResource.IG.Extract.Yellow` |
| Legs / Tail / Claws | 白灯 | `WeaponResource.IG.Extract.White` |

### 单灯 Buff GE

| GE 蓝图 | 类型 | 基础时长 | GrantedTags | Modifiers |
|------|:--:|:--:|------|------|
| `GE_IG_WhiteExtract` | Duration | 90s | `WeaponResource.IG.Extract.White` | MoveSpeedMultiplier ×1.15 |
| `GE_IG_YellowExtract` | Duration | 120s | `WeaponResource.IG.Extract.Yellow` | Defense ×1.1, 额外 `Combat.Poise.Light` |
| `GE_IG_RedExtract` | Duration | 60s | `WeaponResource.IG.Extract.Red`, `Combat.Branch.Extract.Red` | AttackPower ×1.2 |

> **灯时长可叠加**：白 90s + 黄 120s + 红 60s → 获得第三灯时前两灯各自剩余独立倒计时，三灯触发后全部移除。

### 三灯触发机制

```
ApplyExtract(Color)
  0. ★ H-8 修复——若 ActiveExtractHandles[Color] 有效（同色灯已存在）→ ASC->RemoveActiveGameplayEffect(旧Handle)
     → 防止旧 GE 到期时误删新 GE 正在维护的 Extract Tag（GE 泄露 + 标签提前消失 bug）
  1. ASC->ApplyGameplayEffectToSelf(GE_IG_{Color}Extract)
     → 更新 ActiveExtractHandles[Color] = 新 Handle
  2. CheckAndActivateTripleUp()
     → 若 ASC 同时持有 White + Yellow + Red 三个 Extract Tag
       → 且 bTripleUpActive == false
         → 移除三个单独灯 GE（通过 Handle）
         → ASC->ApplyGameplayEffectToSelf(GE_IG_TripleUp)
         → bTripleUpActive = true
         → 记录 TripleUpHandle
         → 广播 Delegate（UI 三灯合一动画）
```

### 三灯 GE（GE_IG_TripleUp）

| 属性 | 值 | 说明 |
|------|------|------|
| DurationPolicy | HasDuration | 固定时长，不可刷新 |
| Duration | `TripleUpDuration`（默认 90s） | 由 ResourceComponent 在 Apply 前通过 `SetDuration` 设置 |
| GrantedTags | `WeaponResource.IG.TripleUp`, `Combat.Branch.TripleUp` | 三灯状态 + 连招分支标记 |
| Modifiers | AttackPower ×1.25, MoveSpeedMultiplier ×1.15, Defense ×1.15 | 全面强化 |
| 额外 GrantedTags | `Combat.Poise.Medium` | 中霸体 |
| StackingPolicy | 不允许叠加 | 三灯期间再次萃取不刷新 |

> **不可刷新保证**：由 `CheckAndActivateTripleUp()` 逻辑实现——`bTripleUpActive==true` 时直接 return，不重新 Apply GE。三灯到期后 `bTripleUpActive` 复位，可重新触发。

### 三灯到期

三灯 GE 到期 → `OnRemove` → 所有灯 Tag 移除 → UI 三个灯图标同时暗灭 → 玩家回归零灯状态。**单灯不会"残留"**——三灯 GE 不带任何单独灯 Tag（仅 `TripleUp` + `Branch.TripleUp`）。

---

## 三、红灯改连招（规划；当前 ComboData 未配置该分支）

### 出招表分支（策划配置）

同一输入在 `DA_IG_ComboData` 中配两条 `FComboNode`，由 ASC Tag 状态自然分流：

| 状态 | Priority | RequiredTags | 目标 GA |
|------|:--:|------|------|
| 无红灯 | 0 | —（空） | `GA_IG_Slash_01`（标准纵斩） |
| 有红灯 | 10 | `Combat.Branch.Extract.Red` | `GA_IG_RedSlash_01`（强化多段斩） |

匹配逻辑（协调器现有机制，零改动）：
1. `HandleWeaponInput(Input.Weapon.Y)` → 查找 `StateIndex` 中所有候选节点
2. 遍历候选 → `RequiredTags` 全部满足 + `BlockedTags` 无一满足 → 取 `Priority` 最高
3. 红灯存在时 ASC 持有 `Branch.Extract.Red` → 红灯版匹配成功（Priority 10 > 0）
4. 红灯到期 → ASC 失去 `Branch.Extract.Red` → 红灯版 RequiredTags 不满足 → 自动回退标准版

### 招内派生（ShouldContinueAfterHit）

红灯版"强化叩击"（`GA_IG_RedSlam`）：
- 段 0 命中 → `ShouldContinueAfterHit()` 检查 `ResourceComponent->HasExtract(Red)`
  - 是 → 段 1 多跳派生（MultiHitCount=5）
  - 否（红灯在攻击中途到期）→ 标准收尾

---

## 四、消耗灯特殊技（规划；GA/GE 未创建）

三种消耗模式，均通过 `UMHGZInsectGlaiveAbility` 基类的辅助方法实现：

### 4.1 消耗红灯——降龙（Diving Wyvern）

| 项目 | 说明 |
|------|------|
| GA 蓝图 | `GA_IG_DivingWyvern`（继承 `UMHGZInsectGlaiveAbility`） |
| 激活条件 | `ResourceComponent->HasExtract(Red)` — 红灯必须存在 |
| 消耗逻辑 | `ResourceComponent->ConsumeExtract(Red)` → 移除红灯 GE |
| 若原为三灯 | 同时移除三灯 GE → `bTripleUpActive = false` → 白+黄继续各自倒计时（重新 Apply 剩余时长的单灯 GE） |
| 攻击参数 | MotionValue=3.0, BaseStaggerValue=20, HitCueTag=Slash |
| 特殊反馈 | 命中时注入 `GameplayCue.Hit.IG.DivingWyvern`（爆发特效+重音效） |

### 4.2 消耗任意灯——萃取爆发（Extract Surge）

| 项目 | 说明 |
|------|------|
| GA 蓝图 | `GA_IG_ExtractSurge` |
| 激活条件 | 任意灯激活 |
| 消耗优先级 | 白 > 黄 > 红（保留红灯维持连招能力） |
| 消耗逻辑 | 消耗优先级最高的灯 → Apply `GE_IG_ExtractBurst_Any` |
| Buff GE | Duration=10s, AttackPower ×1.4, 不可叠加 |
| 若原为三灯 | 消耗一个灯后三灯自然解除 → 剩余两个灯继续各自计时 |

### 4.3 消耗三灯——萃取终结（Triple Burst）

| 项目 | 说明 |
|------|------|
| GA 蓝图 | `GA_IG_TripleBurst` |
| 激活条件 | `ASC->HasMatchingGameplayTag(TripleUp)` |
| 消耗逻辑 | `ResourceComponent->ConsumeExtract(TripleUp)` → 移除三灯 GE → 所有灯归零 |
| 攻击参数 | MotionValue=4.0, BaseStaggerValue=30, MultiHitCount=3 |
| 特殊反馈 | 巨大 GameplayCue 爆发 + 全屏震屏 |
| 冷却 | CooldownDuration=60s（防连发） |

### 消耗灯通用流程

```cpp
// UMHGZInsectGlaiveAbility
bool ConsumeExtractAndApplyBurst(FGameplayTag ExtractType, TSubclassOf<UGameplayEffect> BurstGE)
{
    URes_InsectGlaive* RC = GetResourceComponent();
    if (!RC || !RC->HasExtract(ExtractType)) return false;

    // 1. 消耗灯
    RC->ConsumeExtract(ExtractType);

    // 2. Apply 爆发 Buff
    if (BurstGE)
    {
        GetAbilitySystemComponentFromActorInfo()->ApplyGameplayEffectToSelf(
            BurstGE->GetDefaultObject<UGameplayEffect>(), 1.0f, MakeEffectContext());
    }
    return true;
}
```

---

## 五、三灯攻击音效（C++ 钩子已存在，资源 Tag/GE 尚未形成可用链路）

### 机制

三灯齐聚时，**每个武器攻击 GA 激活时**播放特殊音效（清脆虫鸣/金属混响）——不是命中时触发，而是**挥刀动作本身**伴随音效。猎虫操虫 GA（GA_SendKinsect / GA_RecallKinsect）不触发此音效。

**实现：** `UMHGZInsectGlaiveAbility::ActivateAbility` 覆写中，检查 ASC 是否持有 `Combat.Branch.TripleUp` Tag → 若是则 `UGameplayStatics::PlaySound2D(TripleUpSwingSound)`。

```cpp
void UMHGZInsectGlaiveAbility::ActivateAbility(...)
{
    Super::ActivateAbility(...);  // 父类扣耐力、方向修正、播 Montage

    if (ASC->HasMatchingGameplayTag(Combat.Branch.TripleUp))
    {
        UGameplayStatics::PlaySound2D(this, TripleUpSwingSound);
    }
}
```

| 成员 | 类型 | Category | 默认值 | 说明 |
|------|------|----------|--------|------|
| TripleUpSwingSound | TObjectPtr\<USoundBase\> | "Ability\|Audio" | nullptr | 三灯攻击音效——每个攻击 GA 激活时播放。由 `UMHGZInsectGlaiveAbility` 基类持有，所有虫棍攻击 GA 蓝图继承 |

> **与命中音效的区别：** 命中音效（`HitCueTag` → GameplayCue）在武器碰到怪物时产生——可能命中、可能空挥。三灯攻击音效在**每次挥刀**时无条件播放——无论是否命中。这是怪猎虫棍的标志性反馈：三灯后每一刀都有清脆的虫鸣混响，让玩家明显感知到"我处于三灯状态"。
>
> **猎虫 GA 不触发：** `GA_SendKinsect` 和 `GA_RecallKinsect` 不继承 `UMHGZInsectGlaiveAbility`（它们继承 `UMHGZGameplayAbility`），自然不包含此逻辑。

---

## 六、UI 集成（规划；Widget 资产未创建）

> **详细设计见 [§十二·瞄准与 UI 集成](#十二瞄准与-ui-集成) 和 [ui-system.md](ui-system.md)。** 本节仅列出虫棍 UI 的视觉规格——数据绑定/Delegate/Widget 工厂见 §十二。

### 猎虫耐力条（WBP_IG_KinsectStamina）

- 细长进度条，位于武器资源条下方
- 颜色渐变：绿（>60%）→ 黄（30-60%）→ 红（<30%）→ 归零时闪烁 + 自动召回提示
- 数据源：`URes_InsectGlaive::OnKinsectStaminaChanged` Delegate

### 三灯圆盘（WBP_IG_ExtractDisplay）

- 三个圆形灯图标：白（左）· 黄（中）· 红（右）
- 灯激活/到期/消耗——Tag 事件驱动动画（非 Tick）
- 三灯齐聚→三灯外圈金色光环合一动画
- 数据源：ASC GameplayTag `WeaponResource.IG.Extract.*` + `WeaponResource.IG.TripleUp`

### 准心（WBP_Crosshair）

- LT 瞄准时可见——准心样式随瞄准目标变化
- 对准怪物部位→显示对应萃取颜色（红/黄/白）+ 缩放动画
- 对准空气/场景→默认样式
- 数据源：`UMHGZAimComponent::OnAimTargetChanged` Delegate

### DT_WeaponResourceConfig 注册（规划；资产与配置路径均不存在）

| WeaponTypeTag | ResourceWidgetClass |
|------|------|
| `Weapon.InsectGlaive` | `WBP_IG_ResourcePanel`（包含猎虫耐力条 + 三灯圆盘） |

---

## 七、装备词条加成（规划；ApplyEntryGEs 当前为空）

### 词条目录新增（DT_EntryCatalog）

| EntryID | EffectType | Modifiers | 说明 |
|------|:--:|------|------|
| IG_KinsectRegenUp | WeaponResource | `{Attr=WeaponResource.IG.KinsectRegenRate, Op=Multiply, Curve=Curve_IG_Regen}` | 猎虫耐力回复速度 UP |
| IG_TripleUpExtend | WeaponResource | `{Attr=WeaponResource.IG.TripleUpDuration, Op=Multiply, Curve=Curve_IG_TripleUp}` | 三灯时间延长 |
| IG_ExtractExtend | WeaponResource | `{Attr=WeaponResource.IG.ExtractDuration, Op=Multiply, Curve=Curve_IG_Extract}` | 萃取有效时间延长 |
| IG_HoverDrainReduce | WeaponResource | `{Attr=WeaponResource.IG.HoverDrainRate, Op=Multiply, Curve=Curve_IG_HoverDrain}` | 猎虫悬停耐力消耗减少 |
| IG_FlightDrainReduce | WeaponResource | `{Attr=WeaponResource.IG.FlightDrainRate, Op=Multiply, Curve=Curve_IG_FlightDrain}` | 猎虫飞行耐力消耗减少 |

### 生效路径

遵循决策 #78——`URes_InsectGlaive::ApplyEntryModifier(Tag, Value, Op)`：
1. 按 Tag 路由到内部倍率参数（如 `KinsectRegenRateMultiplier`）
2. 存入 `ActiveModifiers` Map
3. Tick 中 `GetModifiedParam("KinsectRegenRate")` 返回 `BaseRegenRate × Modifiers 累计倍率`
4. 切换装备 → `ClearAllEntryModifiers` → 倍率复位

---

## 八、GameplayTag 完整层级

### 武器资源——虫棍

```
WeaponResource.IG.Extract.White       ← 白灯激活中
WeaponResource.IG.Extract.Yellow      ← 黄灯激活中
WeaponResource.IG.Extract.Red         ← 红灯激活中
WeaponResource.IG.TripleUp            ← 三灯齐聚中
WeaponResource.IG.Kinsect.Active      ← 猎虫放出中

WeaponResource.IG.KinsectRegenRate    ← 猎虫耐力回复速率（词条用）
WeaponResource.IG.HoverDrainRate     ← 悬停耐力消耗速率（词条用）
WeaponResource.IG.FlightDrainRate    ← 飞行耐力消耗速率（词条用）
WeaponResource.IG.TripleUpDuration    ← 三灯时长（词条用）
WeaponResource.IG.ExtractDuration     ← 萃取时长（词条用）
```

### 战斗分支

```
Combat.Branch.Extract.Red             ← 红灯连招分支（FComboNode::RequiredTags 用）
Combat.Branch.TripleUp                ← 三灯连招分支
```

### 输入（虫棍复用现有标签，不新增专用输入 Tag）

| 操作 | 输入 Tag | 连招表节点 | 说明 |
|------|------|------|------|
| 拔刀 | `Input.Weapon.Y`（已有） | `bMatchAnyState=true`, `RequiredTags={Combat.State.Sheathed}`, `Priority=30` | ★ 收刀态按 Y 优先拔刀——最高的 Priority 确保拔刀不被送虫/攻击抢占 |
| 瞄准 | `Input.Modifier.Aiming`（已有） | — | 持刀态 LT 长按→ASC 持有 `Combat.State.Aiming` |
| 送虫（瞄准） | `Input.Weapon.Y`（已有） | `bMatchAnyState=true`, `RequiredTags={Combat.State.Aiming}`, `Priority=20` | ★ 瞄准态下按 Y 优先匹配送虫 |
| 召回 | `Input.Weapon.B`（已有） | `bMatchAnyState=true`, `RequiredTags={WeaponResource.IG.Kinsect.Active}`, `Priority=15` | ★ 有虫放出时按 B 召回 |
| 收刀直飞 | `Input.Modifier.Sheathed`（已有） | `bMatchAnyState=true`, `RequiredTags={Combat.State.Sheathed}`, `Priority=10` | 收刀态 RT → `GA_DrawAndSendKinsect`（单发：SingleHit / FirstHitOnly） |
| 萃取爆发 | `Input.Weapon.RTB`（已有） | 按武器连招表匹配 | RT+B → GA_IG_ExtractSurge |
| 萃取终结 | `Input.Weapon.YB`（已有） | 按武器连招表匹配 | Y+B → GA_IG_TripleBurst |

> **设计理由：** 送虫和收虫不是独立的"武器操作"——它们与拔刀和攻击动作共享输入键（Y/B），通过 GameplayTag 状态（收刀态/瞄准态/虫已放出）在连招表的同一级匹配中自然分流。`bMatchAnyState=true` 使送虫/收虫/拔刀可在连招任意节点触发（允许连招期间放虫/收虫），遵循决策 #28 和 #45。所有 Y/B/RT 键行为在一张出招表中可视化，单一代码路径——协调器的 `HandleWeaponInput` 是唯一入口。
>
> **冲突解决：** 收刀态下 Y 键同时匹配 `GA_Unsheathe`（Priority=30）和 `GA_SendKinsect`（Priority=20）——两者 RequiredTags 都满足（`Sheathed`），但拔刀 Priority 更高，自然选择拔刀。瞄准态下 Y 键：`GA_SendKinsect`（Priority=20）> `GA_IG_Slash_01`（Priority=0），自然选择送虫。`GA_RecallKinsect` 同理。

### 猎虫伤害

```
Damage.MotionValue                   ← 猎虫伤害动作值（SetByCaller）
Damage.AttackPower                   ← 猎虫攻击力覆写（SetByCaller）
```

### GameplayCue

```
GameplayCue.Hit.Kinsect              ← 猎虫命中反馈（小号火花+音效——与武器攻击共用 GC 管道）
GameplayCue.Hit.IG.DivingWyvern       ← 降龙命中特效
GameplayCue.IG.ExtractGained          ← 萃取成功视觉反馈（颜色=灯色）
GameplayCue.IG.TripleUpActivated      ← 三灯齐聚瞬间特效
GameplayCue.IG.ExtractExpired         ← 灯到期消散特效
```

---

## 九、目标目录结构（未标注“当前存在”的资产均为规划）

```
Source/MHGZ/
├── AttributeSystem/
│   └── Res_InsectGlaive.h/cpp              ← 虫棍资源组件（猎虫生命周期 + 耐力 + 三灯状态机 + ★ ApplyKinsectDamage）
├── ActionSystem/
│   └── MHGZInsectGlaiveAbility.h/cpp       ← 虫棍 GA 基类（萃取检查 + 三灯音效注入 + 消耗灯）
├── InsectGlaive/
│   └── Kinsect/
│       ├── Kinsect.h/cpp                   ← 猎虫 Actor（骨骼模型 + 碰撞 + 飞行移动 + ★ 伤害/萃取枚举与控制）
│       ├── KinsectCollisionComponent.h/cpp ← 猎虫专用碰撞组件（胶囊体 + Weapon 通道管理）
│       └── InsectGlaiveKinsectData.h/cpp   ← 猎虫品种 DataAsset（模型/速度/耐力/飞行距离 + ★ KinsectAttackPower）

Content/
├── GameplayEffects/Core/
│   └── GE_KinsectDamage.uasset              ← 猎虫伤害 GE（Instant——SetByCaller 接收 MotionValue + AttackPower）
├── GameplayEffects/InsectGlaive/
│   ├── GE_IG_WhiteExtract.uasset            ← 白灯 Duration GE
│   ├── GE_IG_YellowExtract.uasset           ← 黄灯 Duration GE
│   ├── GE_IG_RedExtract.uasset              ← 红灯 Duration GE
│   ├── GE_IG_TripleUp.uasset                ← 三灯 Duration GE（不可刷新）
│   └── GE_IG_ExtractBurst_Any.uasset        ← 消耗灯爆发 Buff GE（短时高攻）
├── Blueprints/Ability/InsectGlaive/
│   ├── GA_IG_SendKinsect.uasset             ← 送虫（瞄准贯穿：Piercing / AlwaysOverwrite）
│   ├── GA_IG_DrawAndSendKinsect.uasset      ← 收刀直飞（单发：SingleHit / FirstHitOnly）
│   ├── GA_IG_RecallKinsect.uasset           ← 召回
│   ├── GA_IG_Slash_01.uasset                ← 标准纵斩（无红灯版）
│   ├── GA_IG_RedSlash_01.uasset             ← 强化纵斩（红灯版）
│   ├── GA_IG_RedSlam.uasset                 ← 强化叩击（招内派生）
│   ├── GA_IG_DivingWyvern.uasset            ← 降龙（消耗红灯）
│   ├── GA_IG_ExtractSurge.uasset            ← 萃取爆发（消耗任意灯）
│   └── GA_IG_TripleBurst.uasset             ← 萃取终结（消耗三灯）
├── UI/InsectGlaive/
│   ├── WBP_IG_ResourcePanel.uasset          ← 虫棍资源面板（耐力条+三灯圆盘）
│   ├── WBP_IG_KinsectStamina.uasset         ← 猎虫耐力条
│   └── WBP_IG_ExtractDisplay.uasset         ← 三灯圆盘
├── GameplayCues/Hit/
│   ├── GC_Hit_Kinsect.uasset               ← 猎虫命中反馈（小号火花+音效——复用 GC_HitBase 基类）
│   └── GC_Hit_IG_DivingWyvern.uasset        ← 降龙命中特效
├── GameplayCues/InsectGlaive/
│   ├── GC_IG_ExtractGained.uasset           ← 萃取成功
│   ├── GC_IG_TripleUpActivated.uasset       ← 三灯齐聚瞬间
│   └── GC_IG_ExtractExpired.uasset          ← 灯到期消散
├── Weapons/InsectGlaive/
│   ├── Anims/Blueprints/
│   │   └── ABP_IG_Kinsect.uasset            ← 猎虫动画蓝图（单 Slot 节点播放 FlyMontage）
│   ├── Anims/Montage/
│   │   └── AM_IG_Kinsect_Fly.uasset         ← 唯一飞行动画（PlayRate 控制快慢）
│   ├── Data/
│   │   ├── DA_IG_Combo.uasset               ← 虫棍连招表（红灯/非红灯双分支）
│   │   ├── DA_IG_HuoLongGun.uasset          ← Demo 虫棍定义
│   │   └── DA_IG_Kinsect_Speed.uasset       ← 速度型猎虫品种
│   ├── Meshes/Kinsect/
│   │   └── SKM_IG_Kinsect.uasset            ← Demo 猎虫骨骼模型
│   ├── Audio/                               ← 挥棍、猎虫、萃取与三灯音效
│   └── VFX/                                 ← 武器拖尾、猎虫和萃取特效
└── Data/
    └── DT_WeaponComboConfig.uasset           ← 跨武器映射表；保持全局路径
```

---

## 十、设计决策

| # | 决策 | 理由 |
|---|------|------|
| IG-0 | 猎虫为独立 AActor——含骨骼模型、碰撞体、飞行移动组件 | 猎虫需要独立视觉表现（品种差异化模型+动画）、常驻碰撞体（避免逐帧创建/销毁）、自主飞行移动（非骨骼跟随）。简单投射物/粒子方案无法满足怪猎猎虫的交互复杂度 |
| IG-0b | 猎虫碰撞复用怪物系统的通道设计模式——Weapon 通道飞行时 Overlap | 与怪物部位碰撞体（Weapon=Block 常态）产生 Overlap 事件，无需额外碰撞通道。猎虫碰撞体常驻于 Actor，飞行时 Enable、停手臂时 Disable |
| IG-0c | 猎虫品种用 UInsectGlaiveKinsectData（PrimaryDataAsset）配置 | 遵循决策 #18——策划编辑友好、异步加载。品种决定模型/材质/动画集 + 飞行速度/耐力/萃取倍率数值 |
| IG-0d | 猎虫对 Pawn 通道始终 Ignore——不参与物理阻挡且不可受击 | 猎虫太小、非物理实体，可穿透玩家和怪物身体。猎虫无受击机制——怪物攻击不命中猎虫 |
| IG-0e | 猎虫动画极简化——仅单套飞行动画，停手臂无动画 | 猎虫不是战斗核心（武器才是），无需复杂 Idle/Attack/Stagger 动画。停手臂时静态 Mesh 已足够——昆虫停在手上本来几乎不动，视觉差异不可感知 |
| IG-0f | 猎虫无受击——不可被怪物攻击命中 | 怪猎系列猎虫从未有受击机制。猎虫太小、飞行轨迹灵活，怪物攻击命中猎虫既不合理也无必要 |
| IG-0g | 萃取和伤害仅发生在飞行途中（State==Flying），悬停不会触发萃取或伤害 | 萃取和伤害是猎虫飞行中"咬"到怪物表皮——需要飞行的接触判定。悬停时猎虫原地微振翅膀，即使怪物主动靠近也不触发。这要求玩家精准瞄准怪物部位送虫——未命中则召回重新尝试。萃取/伤害无攻击动画，视觉反馈由 `GameplayCue.*` 粒子+音效表现 |
| IG-0h | 目标坐标由 URes_InsectGlaive 计算、以 FVector 传给 AKinsect | 目标坐标计算逻辑属于资源系统（知道玩家状态、相机方向），不属于猎虫 Actor（只知道"飞去哪"）。用纯 FVector 解耦——猎虫零依赖怪物 Actor |
| IG-0i | 飞行双模式——瞄准飞行（LT+Y/B）和收刀直飞（RT） | 两种使用场景：战斗中的精准萃取（瞄准）+ 快速接近/应急（直飞）。共用同一套飞行和悬停逻辑 |
| IG-0j | 悬停消耗耐力小于飞行消耗——鼓励玩家大胆送虫，但悬停不能萃取 | 悬停是"飞行未命中目标"后的等待状态——玩家应召回重新瞄准，而非悬停等待怪物经过。低耗耐降低"飞过头"的惩罚，但不提供萃取捷径 |
| IG-0k | 耐力归零不清空已萃取到的灯 | 萃取发生在飞行中碰撞怪物的瞬间——一旦"咬"到颜色就已获得。耐力归零只影响猎虫能否飞回，不影响已获得的萃取结果 |
| IG-0l | 收虫时猎虫动态追踪玩家实时位置（非锁死召回瞬间坐标） | 收虫期间玩家可以移动——锁定召回瞬间坐标会导致猎虫飞向"空气"。每 Tick 读取 OwnerActor->GetActorLocation() 动态修正方向 |
| IG-0m | 放虫与收虫互斥打断——任何状态下按 Y/B 均可中断当前动作 | 流畅操作的核心——玩家不应等待"当前动画播完"。DeployKinsect 先调 Interrupt() 清空旧飞行状态；RecallKinsect 在 State!=Returning 时立即切换 |
| IG-0n | 耐力归零由 Tick 内联判断触发，不设独立 Timer/事件 | 耐力只有一个修改源（Tick 中扣减），内联 `if (<=0)` 检查零额外开销。单机 60Hz Tick 延迟 ~16ms |
| IG-1 | 猎虫耐力在 ResourceComponent 内用纯 float 管理（非 GAS Attribute） | 遵循决策 #43——武器专属资源不在 AttributeSet；词条通过 ApplyEntryModifier 修改倍率参数 |
| IG-2 | 萃取 Buff 用 Duration GE + GrantedTag（纯 Tag 方案） | 遵循决策 #83——限时 Buff 用 Duration GE；ASC Tag 供连招表/其他系统查询 |
| IG-3 | 三灯不可刷新——由 CheckAndActivateTripleUp 逻辑保证 | UE GE 的 DurationPolicy 无法原生阻止刷新；代码层通过 bTripleUpActive 标志位拒绝重复 Apply |
| IG-4 | 三灯到期全部灯消失——GE_IG_TripleUp 不携带单独灯 Tag | 符合 MHW/MHR 核心机制；到期后玩家需重新萃取 |
| IG-5 | 红灯改连招用 FComboNode::RequiredTags + Priority | 协调器零改动；同一输入两条节点由 Tag 状态分流；策划在一张出招表中可视化全部红灯/非红灯分支 |
| IG-6 | 三灯攻击音效在 GA 激活时播放（非命中时） | 挥刀音效跟随招式而非命中判定——无论空挥还是命中都播放。通过 `UMHGZInsectGlaiveAbility::ActivateAbility` 覆写中 `PlaySound2D` 实现，不经过 GameplayCue |
| IG-7 | 萃取颜色映射为虚函数 MapHitzoneToExtract | 支持不同猎虫品种覆写部位颜色规则——速度型/力量型/回复型猎虫可自定义 |
| IG-8 | 消耗灯→Apply 短时 Buff GE（非永久修改属性） | 消耗灯是战术爆发行为，Buff 应为临时增益。与装备 GE 走不同路径——不触发 OnEquipmentChanged |
| IG-9 | 三灯后被消耗单个灯→解除三灯、剩余灯继续各自计时 | 遵循 MHW/MHR 逻辑——三灯被破后回归单灯状态（白+黄继续有效），不全部清空 |
| IG-10 | 猎虫回收时萃取——不是命中瞬间立即萃取 | MHW/MHR 核心机制——猎虫必须在放出后回到猎人身上才传递萃取。飞行中途收回=萃取失败 |
| IG-11 | 猎虫飞行结束条件统一为三种（撞墙/极限距离/普通命中）——贯穿放虫碰怪不终止 | 贯穿伤害需要猎虫完整穿过怪物身体。普通放虫命中即停（符合"咬一口就回"的直觉），贯穿放虫只有墙或距离能让它停下 |
| IG-12 | 猎虫伤害参数（动作值、贯穿间隔、萃取行为）由送虫 GA 通过 `SetDamageParams` 传入，不存 DataAsset | 动作值属于招式不属于品种；萃取策略属于招式设计（伤害贯穿 vs 萃取贯穿 vs 混合）。当前阶段暂不做品种数值分化 |
| IG-12b | 猎虫伤害走玩家 ASC 的统一 GE 管道——不新增 GA、不给猎虫加 ASC | 复用现有伤害公式（AttackPower × MotionValue × HitzoneDefense）、GameplayCue 火花/音效/伤害数字管道。猎虫无"出招"概念，不需要 Ability 激活/取消生命周期 |
| IG-13 | 猎虫击中怪物后的行为由 `EKinsectDamageMode` 控制——SingleHit 立刻停止，Piercing 继续飞行 | 普通放虫咬一口就原地悬停——玩家回收取得萃取。贯穿放虫穿过去才停——最大化贯穿伤害次数 |
| IG-14 | 猎虫萃取行为通过 `EKinsectExtractMode` 枚举控制，由送虫 GA 传入 | 萃取策略属于招式设计范畴（伤害贯穿 vs 萃取贯穿 vs 混合），不硬编码在猎虫内部。一个 GA 可以送"纯伤害无萃取"的虫，另一个可以送"标准萃取贯穿"的虫 |
| IG-15 | 萃取颜色优先级硬编码为红(3) > 黄(2) > 白(1)，`AlwaysOverwrite` 模式下自动覆盖 | 红灯同时提供最大 Buff 和连招分支——玩家自然希望贯穿时优先锁定红灯。优先级顺序与游戏机制一致 |
| IG-16 | 普通放虫的伤害在 `OnHitMonsterHitzone` Overlap 回调中处理，贯穿放虫的伤害在 Tick 中按 DamageInterval 轮询处理 | SingleHit 只需判断一次命中——Overlap 回调语义正好匹配。Piercing 需要持续检测 Overlap + 冷却计时——Tick 是唯一合适的位置。两者互不干扰 |
| IG-17 | 猎虫移动用 `UProjectileMovementComponent`（`bAutoActivate=false`），不继承 APawn | 当前回归由 Returning Tick 手动追踪 OwnerActor 并更新 Velocity；不使用 HomingTargetComponent |
| IG-18 | 送虫/收虫 GA 进连招表（`bMatchAnyState=true`），不设独立激活路径 | 单一输入路由（协调器 `HandleWeaponInput` 唯一入口）消除双路径竞态。`bMatchAnyState` 使送虫/收虫可在连招任意节点触发。瞄准态下 Y 键分流由 `RequiredTags={Combat.State.Aiming}` + `Priority` 在出招表中可视化控制——不依赖 ASC 内部执行顺序 |

---

## 十一、规划验收清单（完整链路接通后执行）

| # | 测试项 | 预期结果 |
|:--:|------|------|
| 0a | 装备虫棍→猎虫 Spawn 并吸附手臂 | Kinsect Actor 存在；State=Attached；碰撞体 Disable；无动画 |
| 0b | 送虫→猎虫 Detach 并沿准心飞出 | State=Flying, bFollowRay=true；碰撞体 Enable（Weapon=Overlap）；耐力开始扣减；伤害参数已设置；动画仍待实现 |
| 0c | 普通放虫（收刀RT）命中怪物→造成 1 次伤害→立即悬停 | OnHitMonsterHitzone 触发→TryRecordExtract 按 FirstHitOnly 记录颜色→ApplyDamageOnce 1 次伤害→下帧 State=Hovering。萃取保留在 PendingExtractColor，不自动返回 |
| 0c2 | 贯穿放虫（瞄准送虫）命中怪物→持续伤害→继续飞行 | Piercing 模式：每 DamageInterval 秒 ApplyDamageOnce 1 次；TryRecordExtract 按 AlwaysOverwrite 更新；飞行不停止 |
| 0c3 | 贯穿放虫穿过怪物后→无 Overlap→停止伤害但继续飞行 | 伤害冷却自动重置，Overlap 结束后不再触发新伤害。萃取颜色保留最后覆盖的结果 |
| 0c4 | 贯穿放虫贯穿大型怪物（如 5 次 Overlap）→造成 5 次伤害 | 每次伤害独立走 GE 管道→GameplayCue.Hit.Kinsect 触发小火花；伤害数字各自弹出 |
| 0d | 有萃取的猎虫悬停→召回→灯 Apply | 召回→到达→PendingExtractColor 有效→ApplyExtract→UI 灯亮起 |
| 0d2 | 无萃取的猎虫悬停→召回→萃取失败 | 召回→到达→PendingExtractColor 空→无灯 Apply→UI 无变化 |
| 0e | 猎虫飞行/悬停中穿透怪物身体 | Pawn 通道 Ignore→猎虫不被怪物物理阻挡，可在怪物身体附近悬停 |
| 0f | 不同品种猎虫→不同外观 | 切换 DA_Kinsect_Speed → Mesh + Material + FlyMontage 正确加载 |
| 0g | 臂上 LT+Y→猎虫沿准心射线贯穿飞出 | SetDamageParams(Piercing,1.0,0.12s,AlwaysOverwrite)→State=Flying→轨迹=相机方向直线 |
| 0h | 收刀 RT→猎虫单发飞出+拔刀 | SetDamageParams(SingleHit,0.8,0,FirstHitOnly)→StartFlightAlongRay(PlayerForward,StraightFlightDistance) |
| 0i | 悬停中 LT+Y→猎虫贯穿飞向新目标坐标 | Kinsect->Interrupt() → StartFlightToPoint(HitLocation); 贯穿参数不变 |
| 0j | 飞行中按 B→立即中断飞行→开始返回 | Interrupt 被调用; State 切换 Returning; 上次飞行萃取结果保留 |
| 0k | 返回中按 Y→中断返回→飞向新目标 | Interrupt 被调用; 若有 PendingExtractColor 则保留; 重新放虫 |
| 0l | 收虫时玩家移动→猎虫实时修正回归方向 | 每 Tick 读 OwnerActor->GetActorLocation(); 回归路径始终指向玩家当前位置 |
| 0m | 飞行中耐力归零→强制召回→不清空萃取 | Tick 内联检测 KinsectStamina<=0 → ForceRecall; PendingExtractColor 保留 |
| 0n | 飞行中撞到墙壁/建筑→立即停止→就地悬停 | OnWorldCollision 触发; Movement 停止; State=Hovering; 不萃取 |
| 0o | 同一帧命中怪物+墙壁→怪物优先 | OnHitMonsterHitzone 先生效→TryRecordExtract + 伤害（或标记普通命中）；WorldStatic Hit 被忽略 |
| 0p | ExtractMode=NoExtract 贯穿→多次伤害但无萃取 | 贯穿命中 3 次→3 次伤害→召回后 PendingExtractColor 为空→无灯 |
| 0q | ExtractMode=FirstHitOnly 贯穿→首次命中记录，后续不更新 | 首次命中头部→PendingExtractColor=Red；后续 Overlap 躯干→TryRecordExtract 跳过（已有颜色）→不覆盖 |
| 0r | ExtractMode=AlwaysOverwrite 贯穿→从黄灯部位穿到红灯部位 | 首次命中躯干→Yellow；后续命中头部→优先级 3>2→覆盖为 Red |
| 1 | 猎虫放出→耐力持续下降 | Tick 中每帧扣减正确；UI 条同步更新 |
| 2 | 猎虫耐力归零→自动强制召回 | 播放警告音效；萃取失败；耐力条开始回复 |
| 3 | 猎虫命中 Head→召回→红灯 Apply | ASC 持有 `Extract.Red` + `Branch.Extract.Red`；UI 红灯亮起+倒计时 |
| 4 | 猎虫命中 Torso→召回→黄灯 Apply | ASC 持有 `Extract.Yellow`；Defense 提升 |
| 5 | 猎虫命中 Leg→召回→白灯 Apply | ASC 持有 `Extract.White`；MoveSpeed 提升 |
| 6 | 依次获得白→黄→红→三灯自动触发 | 三个单独灯 GE 被移除→三灯 GE Apply→ASC 持有 `TripleUp` + `Branch.TripleUp` |
| 7 | 三灯期间再次萃取→不刷新 | 三灯 GE 剩余时间不变；bTripleUpActive 阻止重复 Apply |
| 8 | 三灯 GE 到期→全部灯消失 | ASC 失去所有 IG Tag；UI 三灯同时暗灭；bTripleUpActive 复位 |
| 9 | 有红灯按 Y→红灯版连招触发 | 协调器匹配 Priority 更高的红灯版 FComboNode |
| 10 | 无红灯按 Y→标准版连招触发 | 红灯版 RequiredTags 不满足→回退标准版 |
| 11 | 三灯状态下发动攻击→特殊挥刀音效播放 | `UMHGZInsectGlaiveAbility::ActivateAbility` 检测 TripleUp Tag → `PlaySound2D(TripleUpSwingSound)`；空挥也播放 |
| 12 | 降龙：红灯存在→可激活→消耗红灯 | 红灯 GE 被移除；高伤下劈；若原三灯→解除三灯、白+黄继续 |
| 13 | 降龙：无红灯→激活被拒 | CanActivateAbility 返回 false |
| 14 | 萃取爆发：任意灯存在→消耗→10s buff | 消耗优先级最低的灯；AttackPower ×1.4 持续 10s 后移除 |
| 15 | 萃取终结：三灯存在→消耗→终结一击 | 所有灯归零；极高伤害+全屏特效；60s 冷却 |
| 16 | 装备词条"三灯时间+20%"→三灯时长=108s | ApplyEntryModifier → GetModifiedParam → 实际时长 = 90 × 1.2 |
| 17 | 卸下虫棍→ResourceComponent 销毁→所有灯清除 | ASC 失去所有 IG Tag；UI 复位 |
| 18 | 虫棍连招表 DA_IG_ComboData 异步加载→加载期间输入静默忽略 | 遵循决策 #94——加载期间 StateIndex 为空 |

---

## 十二、瞄准与 UI 集成

**设计原则：** 遵循决策 #36——UI 由 GameplayTag/Attribute/Delegate 驱动，Ability 不直接操作 UI。虫棍独有的瞄准准心变色+萃取预览由 `UMHGZAimComponent`（Character 端）提供数据，`WBP_Crosshair`（UI 端）订阅 Delegate 表现。武器资源 UI 通过 `UMHGZUISubsystem`（GameInstanceSubsystem）工厂模式动态创建/销毁。

### 架构总览

```
数据层
  ├── GAS ASC Tag（WeaponResource.IG.Extract.* / TripleUp / Kinsect.Active）
  │     → UI 订阅 RegisterGameplayTagEvent
  ├── URes_InsectGlaive Delegate（OnKinsectStaminaChanged / OnExtractTimeUpdated）
  │     → UI 直接绑定
  └── UMHGZAimComponent（每帧射线检测 → OnAimTargetChanged Delegate）
        → WBP_Crosshair 订阅
        │
        ▼
表现层（UMG Widget）
  ├── WBP_HUD（主 HUD——血条/耐力条/武器资源插槽/准心容器）
  ├── WBP_Crosshair（准心——随瞄准目标变色）
  └── WBP_IG_ResourcePanel（虫棍资源——猎虫耐力条 + 三灯圆盘）
```

### UMHGZAimComponent — 瞄准检测组件

```
UCLASS(ClassGroup=(Aim), BlueprintType)
class UMHGZAimComponent : public UActorComponent
```

挂载到 **Character**（非 PlayerController——Character 有相机访问、随 Character 销毁自然清理）。负责瞄准射线检测、准心目标识别、萃取颜色预览。

| 成员 | 类型 | Category | 默认值 | 说明 |
|------|------|----------|--------|------|
| bIsAiming | bool | "Aim\|State" | false | 当前是否瞄准中（订阅 `Combat.State.Aiming` Tag 变化） |
| CurrentAimTarget | TWeakObjectPtr\<AActor\> | "Aim\|State" | nullptr | 准心当前指向的 Actor（怪物/场景物体/nullptr） |
| CurrentAimHitzoneTag | FGameplayTag | "Aim\|State" | 空 | 准心指向的怪物部位 Tag（Hitzone.Head / .Torso / 空） |
| CurrentAimExtractColor | FGameplayTag | "Aim\|State" | 空 | 映射后的萃取颜色（Red/Yellow/White/空——供 GA_SendKinsect 读取） |
| AimMaxDistance | float | "Aim\|Config" | 3000 | 瞄准射线最大距离（cm） |
| AimChannel | TEnumAsByte\<ECollisionChannel\> | "Aim\|Config" | GameTraceChannel1 | 瞄准射线碰撞通道（Weapon 通道——检测怪物 HitzoneComponent） |

| Delegate | 签名 | 说明 |
|------|------|------|
| OnAimTargetChanged | `(AActor* Target, FGameplayTag HitzoneTag, FGameplayTag ExtractColor)` | 准心指向变化时广播——UI 准心订阅。Target 为 nullptr 表示瞄空/场景 |

### 关键方法

- `void BeginPlay() override`
  - 作用：获取 ASC → 订阅 `RegisterGameplayTagEvent(Combat.State.Aiming)` → `OnAimingTagChanged`。LT 的 Started/Completed/Canceled 实际由 `AMHGZCharacter` 绑定，并在 `AimPressed/AimReleased` 中维护 Aiming Tag；AimComponent 不直接绑定 EnhancedInput。

- `void TickComponent(float DeltaTime) override`
  - 作用：若 `bIsAiming==true` → 从 `PlayerCameraManager` 做 `LineTraceSingleByChannel(AimChannel)`。命中 `UMonsterHitzoneComponent` → 读 HitzoneTag → `MapHitzoneToExtract` → 若与上一帧不同 → 广播 `OnAimTargetChanged(Monster, HitzoneTag, ExtractColor)`。命中 WorldStatic → 广播 `OnAimTargetChanged(nullptr, 空, 空)`。**仅在目标变化时广播**——避免每帧重复触发 UI 动画。

- `void OnAimingTagChanged(const FGameplayTag Tag, int32 NewCount)`
  - 作用：`NewCount>0` → `bIsAiming=true`；`NewCount==0` → `bIsAiming=false` → 广播 `OnAimTargetChanged(nullptr, 空, 空)`。
  - **Tag 来源：** 不由 GA 添加——由 Character 的 EnhancedInput 回调调用 `ASC->AddLooseGameplayTag` / `RemoveLooseGameplayTag`。受击/击倒时 AimComponent 主动移除 Aiming。

- `FGameplayTag MapHitzoneToExtract(FGameplayTag HitzoneTag) const`
  - 作用：部位→萃取颜色映射。当前 AimComponent 调用 `URes_InsectGlaive::StaticMapHitzoneToExtract(FGameplayTag)`，与实际萃取共用映射。

### 瞄准→送虫数据流

```
玩家按下 LT
  → AMHGZCharacter 绑定 EnhancedInput IA_LT Started
    → ASC->AddLooseGameplayTag(Combat.State.Aiming)
  → OnAimingTagChanged → bIsAiming = true
  → Tick 启动射线检测

每帧（仅当 bIsAiming==true）：
  → LineTraceSingleByChannel(Weapon)
    → 命中 UMonsterHitzoneComponent（Hitzone.Head）
      → MapHitzoneToExtract → ExtractColor = Red
      → 广播 OnAimTargetChanged(Monster, Hitzone.Head, Red)
        → WBP_Crosshair 收到 → 显示红色准心 + 缩放动画（Scale 1.0→1.2→1.0, 0.1s）
    → 命中 WorldStatic（墙壁/地面）
      → 广播 OnAimTargetChanged(nullptr, 空, 空)
        → WBP_Crosshair 收到 → 显示默认灰色准心

玩家按下 Y（送虫）：
  → ASC::OnInputActionTriggered(Input.Weapon.Y)
    → ComboCoordinator::HandleWeaponInput（★ 唯一入口——不在 ASC 直接激活）
      → 匹配连招表中 `bMatchAnyState=true` 的 GA_SendKinsect 节点
        → RequiredTags={Combat.State.Aiming} 满足（Priority=20 高于攻击节点）
  → GA_SendKinsect::ActivateAbility
    → 读取 UMHGZAimComponent::CurrentAimExtractColor（当前准心颜色）
    → 读取 UMHGZAimComponent 当前相机朝向
    → DeployKinsect() → StartFlightAlongRay(CameraForward, MaxRange)

玩家松开 LT：
  → AMHGZCharacter 绑定 EnhancedInput IA_LT Completed/Canceled
    → ASC->RemoveLooseGameplayTag(Combat.State.Aiming)
  → OnAimingTagChanged → bIsAiming = false
  → 广播 OnAimTargetChanged(nullptr, 空, 空) → 准心隐藏
```

### 准心 Widget（WBP_Crosshair）

| 瞄准目标 | 准心样式 | 动画 |
|------|------|------|
| 怪物·红灯部位 | 红色准心 + 微光晕 | 缩放（1.0→1.2→1.0, 0.1s） |
| 怪物·黄灯部位 | 黄色准心 | 同上 |
| 怪物·白灯部位 | 白色准心 | 同上 |
| 怪物·无萃取部位 | 灰色准心 + "×"标记 | 无 |
| 场景/空气 | 默认灰色准心（小点） | 无 |

### 三灯 UI 数据绑定

| UI 元素 | 数据源 | 驱动方式 |
|------|------|------|
| 白/黄/红灯图标亮/灭 | ASC Tag `WeaponResource.IG.Extract.White/Yellow/Red` | `RegisterGameplayTagEvent` — Tag 添加→亮起动画，Tag 移除→暗灭动画 |
| 三灯合一光环 | ASC Tag `WeaponResource.IG.TripleUp` | 同上 |
| 灯环形倒计时 | `URes_InsectGlaive::OnExtractTimeUpdated(Color, 0~1 Ratio)` Delegate | Delegate 已声明但当前未广播；剩余时间读取与 UI 更新为规划 |
| 猎虫状态图标 | ASC Tag `WeaponResource.IG.Kinsect.Active` | Tag 添加→"虫已放出"图标，Tag 移除→"虫已归"图标 |

### 猎虫耐力条数据绑定

| UI 元素 | 数据源 | 驱动方式 |
|------|------|------|
| 进度条填充 | `URes_InsectGlaive::OnKinsectStaminaChanged(Current, Max)` Delegate | ResourceComponent Tick 中耐力变化时广播——UI 更新百分比 |
| 颜色变化 | 同上（Current/Max 比值） | 蓝图中绑定：>0.6 绿 / 0.3~0.6 黄 / <0.3 红+闪烁 |
| 归零提示 | `OnKinsectStaminaChanged` 中 Current==0 | 播放闪烁+文字提示动画 |

### Widget 生命周期

```
装备虫棍 → EquipmentComponent::OnEquipmentChanged 广播
  → UMHGZUISubsystem（GameInstanceSubsystem）收到 WeaponTypeTag=Weapon.InsectGlaive
  → 查 DT_WeaponResourceConfig → ResourceWidgetClass=WBP_IG_ResourcePanel
  → 获取 URes_InsectGlaive 组件引用
  → CreateWidget → AddToViewport（放入 WBP_HUD 的武器资源插槽）
  → 绑定所有 Delegate（StaminaChanged / ExtractTimeUpdated）
  → 订阅 ASC Tag 事件（Extract.* / TripleUp / Kinsect.Active）

卸下虫棍 → 同样流程 → RemoveFromParent → 解绑所有事件
```

### 新增 GameplayTag

用于 UI 驱动（非虫棍独占——其他武器如有需要可复用）：

```
UI.Aim.Target.Monster          ← 准心对准怪物（任意部位）
UI.Aim.Target.World            ← 准心对准场景/空气
UI.Aim.Extract.Red             ← 准心对准红灯部位
UI.Aim.Extract.Yellow          ← 准心对准黄灯部位
UI.Aim.Extract.White           ← 准心对准白灯部位
```

> **注意：** `UI.Aim.*` 是 UI 专用 Tag——由 `UMHGZAimComponent` 广播时设置到 ASC，WBP_Crosshair 订阅。虫棍之外的其他武器（如太刀见切瞄准、弓箭瞄准）也可以复用 `UI.Aim.Target.Monster`。

### 验证清单（新增）

| # | 测试项 | 预期结果 |
|:--:|------|------|
| UI-1 | LT 瞄准怪物头部→准心变红 | OnAimTargetChanged(Monster, Head, Red) → WBP_Crosshair 显示红色+缩放 |
| UI-2 | LT 瞄准怪物躯干→准心变黄 | OnAimTargetChanged(Monster, Torso, Yellow) |
| UI-3 | LT 瞄准墙壁→准心恢复默认 | OnAimTargetChanged(nullptr, 空, 空) → 灰色小点 |
| UI-4 | 送虫→萃取→召回→三灯图标亮起 | ASC Tag 变化 → RegisterGameplayTagEvent → UI 更新 |
| UI-5 | 猎虫耐力下降→耐力条实时更新 | OnKinsectStaminaChanged Delegate → ProgressBar 填充 |
| UI-6 | 瞄准中移动准心扫过多个部位→准心颜色实时切换 | AimComponent Tick 检测目标变化→仅变化时广播 |
| UI-7 | 装备虫棍→资源 Widget 创建→卸下→资源 Widget 销毁 | UMHGZUISubsystem 管理生命周期 |
| UI-8 | 三灯到期→三灯图标同时暗灭 | TripleUp Tag 移除→UI 播放 FadeOut |
