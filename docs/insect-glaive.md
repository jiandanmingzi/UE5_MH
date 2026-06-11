# 虫棍资源系统（操虫棍·Insect Glaive）

**设计原则：** 基于现有 GAS 架构，虫棍资源系统分为**四大子系统**——**猎虫实体**（独立 `AKinsect` Actor：骨骼模型+动画+碰撞+飞行移动）、**猎虫耐力**（`URes_InsectGlaive` 组件内管理）、**三灯萃取**（持续时间 GE + GameplayTag 状态机）、**消耗灯特殊技**（`FComboNode::RequiredTags` 分支 + `ShouldContinueAfterHit` 招内派生）。红灯改连招通过出招表 Tag 分支实现，协调器零改动。三灯特殊命中音效通过 `UMHGZInsectGlaiveAbility` 覆写向 DamageSpec 注入额外 GameplayCue Tag。

---

## 系统总览

```
AKinsect (独立 Actor, 由 URes_InsectGlaive 管理生命周期)
├── USkeletalMeshComponent (猎虫品种骨骼模型)
├── UFloatingPawnMovement (飞行移动——悬停时速度为 0)
├── UKinsectCollisionComponent (胶囊体: Weapon 通道 = Block)
├── AnimInstance (单套飞行动画——前进/返回/悬停共用，通过 PlayRate 区分)
└── 品种数据: UInsectGlaiveKinsectData (DataAsset)

URes_InsectGlaive (WeaponResourceComponent, 挂载到 PlayerState)
├── 猎虫生命周期: Spawn → AttachToArm → Deploy(FVector) → Hover → Recall → Destroy
├── 目标坐标管理: 瞄准射线→WorldLocation / 收刀RT直飞→Forward×Distance
├── 猎虫耐力管理 (KinsectStamina: 悬停扣减 / 休息回复 / 归零自动召回)
├── 萃取状态机 (悬停中接近怪物→自动萃取 → Apply Duration GE → 三灯检测)
├── 三灯激活与保护 (替换单独灯 GE、不可刷新、到期全部消失)
├── 灯消耗接口 (ConsumeExtract → 移除 GE)
└── 词条加成接收 (ApplyEntryModifier → ActiveModifiers Map)

UMHGZInsectGlaiveAbility (攻击基类, 继承 UMHGZAttackAbility)
├── 覆写 CanActivateAbility (检查指定灯是否存在)
├── 覆写 MakeDamageSpec 后处理 (三灯时注入 GameplayCue.Hit.IG.TripleUp)
├── 辅助方法: CheckExtractRequirement / ConsumeExtractAndApplyBurst
└── 持有 TWeakObjectPtr<URes_InsectGlaive> ResourceComponent

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
| Collision | TObjectPtr\<UKinsectCollisionComponent\> | "Kinsect\|Component" | CreateDefaultSubobject | 胶囊体碰撞——仅 Weapon 通道 Block，用于萃取判定 |
| Movement | TObjectPtr\<UFloatingPawnMovement\> | "Kinsect\|Component" | CreateDefaultSubobject | 飞行移动——无重力、可悬停 |
| State | EKinsectState | "Kinsect\|State" | Attached | 猎虫状态：Attached / Flying / Hovering / Returning / Recalled |
| OwnerActor | TWeakObjectPtr\<AActor\> | "Kinsect\|State" | nullptr | 玩家引用——收虫时每 Tick 读取实时坐标动态修正回归路径。`AttachToPlayer` 时设置 |
| bFollowRay | bool | "Kinsect\|State" | false | true=沿射线方向飞行（臂上放虫模式），false=直线飞向目标坐标（悬停放虫模式） |
| RayDirection | FVector | "Kinsect\|State" | ZeroVector | 臂上放虫时的飞行方向（相机准心射线方向）。bFollowRay==true 时有效 |
| FlyDestination | FVector | "Kinsect\|State" | ZeroVector | 悬停放虫时的目标坐标。bFollowRay==false 时有效 |
| PendingExtractColor | FGameplayTag | "Kinsect\|State" | 空 | 萃取暂存颜色——飞行中碰撞到怪物部位时记录，返回玩家后传递给 ResourceComponent |
| KinsectData | TObjectPtr\<UInsectGlaiveKinsectData\> | "Kinsect\|Data" | nullptr | 品种配置 DataAsset——运行时由 ResourceComponent 注入 |

### 关键方法

- `void StartFlightAlongRay(FVector RayDirection, float MaxDistance)`
  - 输入：相机准心射线方向、最大飞行距离。
  - 作用：Detach（若已 Attach）→ State = Flying, bFollowRay = true → Movement 设速度 = RayDirection × FlightSpeed → Collision Enable → 播放 FlyMontage（PlayRate=1.5）。猎虫沿射线方向直线飞出——像从枪口射出的曳光弹。超过 MaxDistance 未命中任何物体 → State = Hovering。飞行途中碰撞到怪物 → 萃取。

- `void StartFlightToPoint(FVector Destination)`
  - 输入：目标世界坐标。
  - 作用：State = Flying, bFollowRay = false → Movement 设速度朝向 Destination → FlyMontage（PlayRate=1.5）。猎虫沿当前位置到目标坐标的直线飞行。距离 < AcceptRadius → State = Hovering。

- `void StartReturn()`
  - 作用：State = Returning → 播放 FlyMontage（PlayRate=1.0）。**每 Tick 读取 `OwnerActor->GetActorLocation()` 动态修正飞行方向**——即使玩家移动，猎虫也能追踪回归。距离 < AcceptRadius → 自动调用 `AttachToPlayer`。

- `void ForceRecall()`
  - 作用：耐力归零强制召回。调用 `StartReturn()`，**不清除 `PendingExtractColor`**——已萃取到的灯保留，召回后正常 Apply。

- `void Interrupt()`
  - 作用：停止当前 Movement + 停止动画 + 重置 bFollowRay/FlyDestination。在 `DeployKinsect` 重新放虫前调用——实现放虫↔收虫互斥打断。不修改 `PendingExtractColor`（若已萃取则保留）。

- `void AttachToPlayer(USceneComponent* ArmSocket)`
  - 输入：玩家手臂 Socket 组件。
  - 作用：`AttachToComponent(ArmSocket)` → State = Attached, OwnerActor = ArmSocket→GetOwner() → Collision Disable → 停止动画。

- `void EnableCollision()` / `void DisableCollision()`
  - 作用：切换 `Collision` 组件的 Weapon 通道响应（Block / Ignore）。

- `void OnHitMonsterHitzone(UMonsterHitzoneComponent* Hitzone)`
  - 输入：被命中的怪物部位碰撞体。
  - 作用：Overlap 回调——**仅在 `State == Flying` 时处理**。读取 `Hitzone->HitzoneTag` → `PendingExtractColor = MapHitzoneToExtract(...)` → `StartReturn()`。若已有 `PendingExtractColor` 则跳过。萃取本质是猎虫飞行中"咬"到怪物表皮——视觉反馈由 `GameplayCue.IG.ExtractGained` 粒子+音效表现。

- `void OnWorldCollision(const FHitResult& Hit)`
  - 输入：碰撞命中结果。
  - 作用：Hit 回调——**仅在 `State == Flying` 时处理**。猎虫撞到世界静态几何体（墙壁/建筑/地面）→ `Movement->StopMovementImmediately()` → State = Hovering（就地悬停）。不会萃取（只有怪物部位触发萃取）。与 `OnHitMonsterHitzone` 互斥——若同一帧同时命中怪物和墙壁，怪物优先。

- `float GetFlightSpeed() const`
  - 输出：当前飞行速度（`KinsectData->FlightSpeed × ResourceComponent 词条修正`）。

- `float GetHoverDrainRate() const` / `float GetFlightDrainRate() const`
  - 输出：当前耐力消耗速率。供 ResourceComponent Tick 读取。

### 猎虫碰撞组件（UKinsectCollisionComponent）

继承 `UCapsuleComponent`，复用怪物系统的碰撞通道设计模式：

| 通道 | 常态（停手臂） | 飞行/悬停中 | 召回中 |
|------|:--:|:--:|:--:|
| Weapon | Ignore | **Block** ← 怪物部位萃取判定 | Ignore |
| WorldStatic | Ignore | **Block** ← 碰撞世界几何体（墙壁/建筑）| Ignore |
| MonsterAttack | Ignore | Ignore | Ignore |
| Pawn | Ignore | Ignore（穿透玩家和怪物身体） | Ignore |
| Visibility | Ignore | Ignore | Ignore |

> **设计理由：**
> - **Weapon = Block**：与怪物 `UMonsterHitzoneComponent`（Weapon=Block 常态）产生 Overlap → `OnHitMonsterHitzone` → 萃取。
> - **WorldStatic = Block**：飞行中撞到墙壁/建筑/地面时产生 Hit 事件 → `OnWorldCollision` → 立即停止飞行，就地悬停。防止猎虫穿透场景飞出地图。
> - **Pawn = Ignore**：猎虫太小、非物理实体，穿透玩家和怪物身体；猎虫不可受击。

#### 与 AnimNotifyState_AttackCollision 的区别

| | AnimNotifyState_AttackCollision（武器攻击） | UKinsectCollisionComponent（猎虫） |
|------|------|------|
| 碰撞体生命周期 | 仅在 NotifyBegin→NotifyEnd 窗口内存在，动态创建/销毁 | 常驻于 AKinsect，飞行期间 Enable，停手臂时 Disable |
| 适用场景 | 固定时长的 Montage 播放中的瞬时判定（0.1-0.5s 窗口） | 持续数秒飞行过程中的不定时命中 |
| 碰撞形状 | 可灵活配置（Sphere/Capsule/Box） | 固定胶囊体（猎虫形体近似） |
| 移动方式 | 跟随骨骼动画——碰撞体固定在 Socket 上随动画运动 | 独立飞行移动——`UFloatingPawnMovement` 驱动 |

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

### 飞行轨迹机制——双模式

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
猎虫到达目标坐标（Distance < AcceptRadius）
  → Movement->StopMovementImmediately()
  → State = Hovering
  → 飞行动画 PlayRate = 0.3（慢速浮空——视觉上停在原地微振翅膀）
  → Tick 中每帧 KinsectStamina -= HoverDrainRate × Δt
  → Collision 组件保持 Enable（Weapon=Block）
  → ★ 悬停中不会萃取——必须召回后重新送虫
```

> **为何悬停不能萃取：** 萃取是猎虫飞行中"咬"到怪物表皮——需要飞行速度带来的接触判定。悬停时猎虫原地微振翅膀，即使怪物主动靠近也不会触发萃取。这要求玩家主动管理猎虫——精准瞄准怪物部位送虫、飞行途中命中、未命中则召回重新尝试。

### 目标坐标管理

**计算方：`URes_InsectGlaive`**（非 GA，非 Kinsect）

| 模式 | 当前猎虫状态 | 计算方式 | 调用 AKinsect 方法 |
|------|:--:|------|------|
| 臂上放虫（LT+Y） | Attached | 取相机朝向 `CameraForward` + `MaxFlightRange`（品种 DataAsset） | `StartFlightAlongRay(CameraForward, MaxRange)` |
| 悬停放虫（LT+Y） | Hovering/Returning | 相机射线 → HitLocation；无命中→CameraMaxRange | `StartFlightToPoint(HitLocation)` |
| 收刀直飞（RT） | Attached（收刀态） | PlayerForward × `StraightFlightDistance`（品种 DataAsset） | `StartFlightAlongRay(PlayerForward, StraightFlightDistance)` |

**传递方式：** `URes_InsectGlaive::DeployKinsect()`（无参数——由 ResourceComponent 内部判断当前 State 后选择正确的 AKinsect 方法）

```cpp
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
    bKinsectDeployed = true;
}
```

### 猎虫生命周期

```
装备虫棍
  → URes_InsectGlaive::OnWeaponEquipped()
    → SpawnActor<AKinsect>(KinsectClass)
    → Kinsect->AttachToPlayer(Character->GetMesh(), "Kinsect_Arm_Socket")
    → State = Attached（碰撞体 Disable，纯静态 Mesh 吸附——无闲置动画）

瞄准送虫（持刀 LT+Y，臂上）
  → GA_SendKinsect → ResourceComponent->DeployKinsect()
    → 若 bKinsectDeployed → Kinsect->Interrupt()（打断当前飞行/悬停/返回）
    → Kinsect->Detach → StartFlightAlongRay(CameraForward, MaxRange)
    → bFollowRay = true, State = Flying, Collision Enable
    → Anim: FlyMontage, PlayRate = 1.5

悬停重新放虫（持刀 LT+Y，悬停中）
  → GA_SendKinsect → ResourceComponent->DeployKinsect()
    → Kinsect->Interrupt()（打断当前悬停）
    → StartFlightToPoint(HitLocation)
    → bFollowRay = false, State = Flying

收刀直飞（收刀 RT）
  → GA_DrawAndSendKinsect → 先 Unsheathe → DeployKinsect()
    → StartFlightAlongRay(PlayerForward, StraightFlightDistance)

猎虫到达射线最大距离 / 目标坐标
  → State = Hovering
    → Movement->StopMovementImmediately()
    → Anim: PlayRate = 0.3
    → Tick: KinsectStamina -= HoverDrainRate × Δt
    → ★ 悬停中不会萃取——必须召回后重新送虫

飞行中碰到怪物→萃取（仅 State == Flying 时生效）
  → Collision Overlap 怪物 HitzoneComponent
    → OnHitMonsterHitzone → PendingExtractColor = MapHitzoneToExtract(...)
    → 播放 GameplayCue.IG.ExtractGained（粒子+音效——猎虫"咬"到怪物的视觉反馈）
    → State = Returning（自动开始返回——不需要额外操作）
    → Anim: PlayRate = 1.0

飞行中撞到世界几何体→停止（仅 State == Flying 时生效）
  → Collision Hit WorldStatic（墙壁/建筑/地面/空气墙）
    → OnWorldCollision → Movement->StopMovementImmediately()
    → State = Hovering（就地悬停——不会萃取）
    → Anim: PlayRate = 0.3

召回（B / 耐力归零）——可打断飞行/悬停，不可打断返回
  → 若 State == Returning → 静默跳过（已在返回中）
  → 否则 → State = Returning → StartReturn()
    → ★ 每 Tick 读取 OwnerActor->GetActorLocation() 动态追踪玩家位置
    → 到达 → AttachToPlayer → State = Attached, Collision Disable
    → 若 PendingExtractColor 有效 → ResourceComponent->ApplyExtract(PendingExtractColor)
    → ★ 耐力归零不会清空萃取——已萃取到的灯在召回后仍会 Apply
    → 若 PendingExtractColor 为空（飞行中未碰到怪物即召回）→ 萃取失败
    → bKinsectDeployed = false
    → Tick: KinsectStamina += StaminaRegenRate × Δt（回复）

卸下虫棍
  → URes_InsectGlaive::OnWeaponUnequipped()
    → Kinsect->Destroy()
```

### 猎虫动画

猎虫只有**一套飞行动画**——`Montage_Kinsect_Fly`（翅膀扇动循环）。前进/返回/悬停统一使用，通过 `PlayRate` 区分：

| 状态 | PlayRate | 视觉效果 |
|------|:--:|------|
| Flying（前进） | 1.5 | 翅膀高速扇动——快速飞行 |
| Returning（返回） | 1.0 | 正常扇动——衔光球飞回 |
| Hovering（悬停） | 0.3 | 慢速扇动——原地浮空微振 |
| Attached（停手臂） | —（不播放动画） | 静态 Mesh 吸附——昆虫停在手臂上几乎不动 |

> **不需要 AnimBP 状态机。** 在 `AKinsect::Tick` 中根据 `State` 直接控制 `USkeletalMeshComponent::PlayMontage` + `SetPlayRate`——通过 Flipbook/BlendSpace 在 Montage 内部即可实现转向倾斜效果。若后续需要不同品种翅膀扇动风格差异，使用不同 Montage 资产即可，AnimBP 始终只需一个 Slot 节点播放 Montage。

---

## 一、猎虫耐力

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
    → PendingExtractColor = MapHitzoneToExtract(HitzoneTag)
    → State = Returning（自动）
    → 回调 ResourceComponent->OnKinsectHitMonster(HitzoneTag)

召回（持刀 B / 耐力归零）
  → 若 Kinsect State == Returning → 跳过（已在返回中）
  → GA_RecallKinsect（或耐力归零自动 ForceRecall）
    → Kinsect->StartReturn() → 每 Tick 追踪 OwnerActor 实时位置
    → 到达 → AttachToPlayer(ArmSocket) → State = Attached
    → 若 PendingExtractColor 有效 → ApplyExtract(PendingExtractColor)
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
- **耐力归零检测——Tick 内联判断**：`URes_InsectGlaive::Tick` 中扣减耐力后直接 `if (KinsectStamina <= 0) → ForceRecall()`。不设独立 Timer 或事件——耐力只有一个修改源（Tick），一次 `<=0` 比较即可，零额外开销。单机 60Hz Tick 延迟最多 16ms，完全不可感知。
- **装备词条修改**：通过 `ApplyEntryModifier(WeaponResource.IG.HoverDrainRate, Value, Multiply)` 等路径（遵循决策 #78）
- **UI 显示**：`WBP_IG_KinsectStamina` 独立进度条，位于武器资源 UI 下方，订阅组件 Delegate 更新

---

## 二、三灯萃取

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
  1. ASC->ApplyGameplayEffectToSelf(GE_IG_{Color}Extract)
     → 记录 ActiveExtractHandles[Color]
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

## 三、红灯改连招

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

## 四、消耗灯特殊技

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

## 五、三灯攻击音效

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

## 六、UI 集成

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

### DT_WeaponResourceConfig 注册

| WeaponTypeTag | ResourceWidgetClass |
|------|------|
| `Weapon.InsectGlaive` | `WBP_IG_ResourcePanel`（包含猎虫耐力条 + 三灯圆盘） |

---

## 七、装备词条加成

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

| 操作 | 输入 Tag | 说明 |
|------|------|------|
| 瞄准 | `Input.Modifier.Aiming`（已有） | 持刀态 LT 长按 |
| 送虫（瞄准） | `Input.Weapon.Y`（已有） | 瞄准态下按 Y → GA_SendKinsect |
| 召回 | `Input.Weapon.B`（已有） | 持刀态按 B → GA_RecallKinsect |
| 收刀直飞 | `Input.Modifier.Sheathed`（已有） | 收刀态 RT → GA_DrawAndSendKinsect |
| 萃取爆发 | `Input.Weapon.RTB`（已有） | RT+B → GA_IG_ExtractSurge |
| 萃取终结 | `Input.Weapon.YB`（已有） | Y+B → GA_IG_TripleBurst |

> **设计理由：** 虫棍不新增专用输入标签——送虫复用轻攻击键（Y），召回复用重攻击键（B），瞄准复用现有 LT 瞄准。收刀 RT 直飞通过 `Input.Modifier.Sheathed` 路由到 GA_DrawAndSendKinsect。萃取爆发/终结复用和弦输入（RT+B / Y+B）。

### GameplayCue
```
GameplayCue.Hit.IG.DivingWyvern       ← 降龙命中特效
GameplayCue.IG.ExtractGained          ← 萃取成功视觉反馈（颜色=灯色）
GameplayCue.IG.TripleUpActivated      ← 三灯齐聚瞬间特效
GameplayCue.IG.ExtractExpired         ← 灯到期消散特效
```

---

## 九、目录结构

```
Source/MHGZ/
├── AttributeSystem/
│   └── Res_InsectGlaive.h/cpp              ← 虫棍资源组件（猎虫生命周期 + 耐力 + 三灯状态机）
├── ActionSystem/
│   └── MHGZInsectGlaiveAbility.h/cpp       ← 虫棍 GA 基类（萃取检查 + 三灯音效注入 + 消耗灯）
├── InsectGlaive/
│   └── Kinsect/
│       ├── Kinsect.h/cpp                   ← 猎虫 Actor（骨骼模型 + 碰撞 + 飞行移动）
│       ├── KinsectCollisionComponent.h/cpp ← 猎虫专用碰撞组件（胶囊体 + Weapon 通道管理）
│       └── InsectGlaiveKinsectData.h/cpp   ← 猎虫品种 DataAsset（模型/速度/耐力/飞行距离）

Content/
├── GameplayEffects/InsectGlaive/
│   ├── GE_IG_WhiteExtract.uasset            ← 白灯 Duration GE
│   ├── GE_IG_YellowExtract.uasset           ← 黄灯 Duration GE
│   ├── GE_IG_RedExtract.uasset              ← 红灯 Duration GE
│   ├── GE_IG_TripleUp.uasset                ← 三灯 Duration GE（不可刷新）
│   └── GE_IG_ExtractBurst_Any.uasset        ← 消耗灯爆发 Buff GE（短时高攻）
├── Blueprints/Ability/InsectGlaive/
│   ├── GA_IG_SendKinsect.uasset             ← 送虫（瞄准飞行）
│   ├── GA_IG_DrawAndSendKinsect.uasset      ← 收刀直飞（RT 拔刀+送虫）
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
├── GameplayCues/InsectGlaive/
│   ├── GC_Hit_IG_DivingWyvern.uasset        ← 降龙命中特效
│   ├── GC_IG_ExtractGained.uasset           ← 萃取成功
│   ├── GC_IG_TripleUpActivated.uasset       ← 三灯齐聚瞬间
│   └── GC_IG_ExtractExpired.uasset          ← 灯到期消散
├── Kinsect/
│   ├── SK_Kinsect_Speed.uasset              ← 速度型猎虫骨骼模型
│   ├── SK_Kinsect_Power.uasset              ← 力量型猎虫骨骼模型
│   ├── SK_Kinsect_Heal.uasset               ← 回复型猎虫骨骼模型
│   ├── ABP_Kinsect.uasset                   ← 猎虫动画蓝图（单 Slot 节点播放 FlyMontage）
│   ├── Montage_Kinsect_Fly.uasset           ← 唯一飞行动画（翅膀扇动循环，PlayRate 控制快慢）
│   ├── DA_Kinsect_Speed.uasset              ← 速度型品种 DataAsset
│   ├── DA_Kinsect_Power.uasset              ← 力量型品种 DataAsset
│   └── DA_Kinsect_Heal.uasset               ← 回复型品种 DataAsset
└── Data/
    ├── DA_IG_ComboData.uasset               ← 虫棍连招表（红灯/非红灯双分支）
    └── DT_WeaponResourceConfig (追加行)      ← Weapon.InsectGlaive → WBP_IG_ResourcePanel
```

---

## 十、设计决策

| # | 决策 | 理由 |
|---|------|------|
| IG-0 | 猎虫为独立 AActor——含骨骼模型、碰撞体、飞行移动组件 | 猎虫需要独立视觉表现（品种差异化模型+动画）、常驻碰撞体（避免逐帧创建/销毁）、自主飞行移动（非骨骼跟随）。简单投射物/粒子方案无法满足怪猎猎虫的交互复杂度 |
| IG-0b | 猎虫碰撞复用怪物系统的通道设计模式——Weapon 通道飞行时 Block | 与怪物部位碰撞体（Weapon=Block 常态）自然产生 Overlap 事件，无需额外碰撞通道。猎虫碰撞体常驻于 Actor，飞行时 Enable、停手臂时 Disable——性能优于 AnimNotifyState 逐帧动态创建方案 |
| IG-0c | 猎虫品种用 UInsectGlaiveKinsectData（PrimaryDataAsset）配置 | 遵循决策 #18——策划编辑友好、异步加载。品种决定模型/材质/动画集 + 飞行速度/耐力/萃取倍率数值 |
| IG-0d | 猎虫对 Pawn 通道始终 Ignore——不参与物理阻挡且不可受击 | 猎虫太小、非物理实体，可穿透玩家和怪物身体。猎虫无受击机制——怪物攻击不命中猎虫 |
| IG-0e | 猎虫动画极简化——仅单套飞行动画，停手臂无动画 | 猎虫不是战斗核心（武器才是），无需复杂 Idle/Attack/Stagger 动画。停手臂时静态 Mesh 已足够——昆虫停在手上本来几乎不动，视觉差异不可感知 |
| IG-0f | 猎虫无受击——不可被怪物攻击命中 | 怪猎系列猎虫从未有受击机制。猎虫太小、飞行轨迹灵活，怪物攻击命中猎虫既不合理也无必要 |
| IG-0g | 萃取仅发生在飞行途中（State==Flying），悬停不会触发萃取 | 萃取是猎虫飞行中"咬"到怪物表皮——需要飞行的接触判定。悬停时猎虫原地微振翅膀，即使怪物主动靠近也不触发。这要求玩家精准瞄准怪物部位送虫——未命中则召回重新尝试。萃取无攻击动画，视觉反馈由 `GameplayCue.IG.ExtractGained` 粒子+音效表现"咬中"瞬间 |
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

---

## 十一、验证清单

| # | 测试项 | 预期结果 |
|:--:|------|------|
| 0a | 装备虫棍→猎虫 Spawn 并吸附手臂 | Kinsect Actor 存在；State=Attached；碰撞体 Disable；无动画 |
| 0b | 送虫→猎虫 Detach 并沿准心飞出 | State=Flying, bFollowRay=true；碰撞体 Enable（Weapon=Block）；Anim PlayRate=1.5；耐力开始扣减 |
| 0c | 猎虫飞行中碰到怪物 HitzoneComponent→命中回调 | OnHitMonsterHitzone 触发；PendingExtractColor 记录正确颜色；State 切换为 Returning；碰撞体 Disable |
| 0d | 猎虫飞回→到达玩家→Attach 吸附 | State=IdleOnArm；PendingExtractColor 传递给 ResourceComponent→ApplyExtract；UI 灯亮起 |
| 0e | 猎虫飞行/悬停中穿透怪物身体 | Pawn 通道 Ignore→猎虫不被怪物物理阻挡，可在怪物身体附近悬停 |
| 0f | 不同品种猎虫→不同外观 | 切换 DA_Kinsect_Speed → Mesh + Material + FlyMontage 正确加载 |
| 0g | 臂上 LT+Y→猎虫沿准心射线飞出 | State=Flying, bFollowRay=true; 轨迹=相机方向直线; Anim PlayRate=1.5 |
| 0h | 收刀 RT→猎虫沿玩家前方直飞+拔刀 | StartFlightAlongRay(PlayerForward, StraightFlightDistance); 同时 Unsheathe |
| 0i | 悬停中 LT+Y→猎虫直线飞向新目标坐标 | Kinsect->Interrupt() → StartFlightToPoint(HitLocation); bFollowRay=false |
| 0j | 飞行中按 B→立即中断飞行→开始返回 | Interrupt 被调用; State 切换 Returning; 上次飞行萃取结果保留 |
| 0k | 返回中按 Y→中断返回→飞向新目标 | Interrupt 被调用; 若有 PendingExtractColor 则保留; 重新放虫 |
| 0l | 收虫时玩家移动→猎虫实时修正回归方向 | 每 Tick 读 OwnerActor->GetActorLocation(); 回归路径始终指向玩家当前位置 |
| 0m | 飞行中耐力归零→强制召回→不清空萃取 | Tick 内联检测 KinsectStamina<=0 → ForceRecall; PendingExtractColor 保留 |
| 0n | 飞行中撞到墙壁/建筑→立即停止→就地悬停 | OnWorldCollision 触发; Movement 停止; State=Hovering; 不萃取 |
| 0o | 同一帧命中怪物+墙壁→怪物优先 | OnHitMonsterHitzone 先生效→萃取+返回；WorldStatic Hit 被忽略 |
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

- `void TickComponent(float DeltaTime) override`
  - 作用：若 `bIsAiming==true` → 从 `PlayerCameraManager` 做 `LineTraceSingleByChannel(AimChannel)`。命中 `UMonsterHitzoneComponent` → 读 HitzoneTag → `MapHitzoneToExtract` → 若与上一帧不同 → 广播 `OnAimTargetChanged(Monster, HitzoneTag, ExtractColor)`。命中 WorldStatic → 广播 `OnAimTargetChanged(nullptr, 空, 空)`。**仅在目标变化时广播**——避免每帧重复触发 UI 动画。

- `void OnAimingTagChanged(const FGameplayTag Tag, int32 NewCount)`
  - 作用：BeginPlay 时订阅 ASC 的 `RegisterGameplayTagEvent(Combat.State.Aiming)`。NewCount>0 → `bIsAiming=true`；NewCount==0 → `bIsAiming=false` → 广播 `OnAimTargetChanged(nullptr, 空, 空)`。

- `FGameplayTag MapHitzoneToExtract(FGameplayTag HitzoneTag) const`
  - 作用：部位→萃取颜色映射。与 `URes_InsectGlaive::MapHitzoneToExtract` 相同逻辑——共用静态工具函数或通过 ResourceComponent 代理调用，保证映射一致。

### 瞄准→送虫数据流

```
玩家按下 LT
  → ASC 添加 Combat.State.Aiming Tag
  → UMHGZAimComponent::OnAimingTagChanged → bIsAiming = true
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
  → GA_SendKinsect::ActivateAbility
    → 读取 UMHGZAimComponent::CurrentAimExtractColor（当前准心颜色）
    → 读取 UMHGZAimComponent 当前相机朝向
    → DeployKinsect() → StartFlightAlongRay(CameraForward, MaxRange)

玩家松开 LT：
  → ASC 移除 Combat.State.Aiming Tag
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
| 灯环形倒计时 | `URes_InsectGlaive::OnExtractTimeUpdated(Color, 0~1 Ratio)` Delegate | ResourceComponent Tick 中读取剩余时间广播——UI 更新材质参数 |
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
