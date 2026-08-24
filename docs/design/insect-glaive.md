# 虫棍资源系统（操虫棍·Insect Glaive）

> **实施状态说明（以源码、配置和 Content 为准）：** 本文负责猎虫实体、耐力、三色精华、三灯、UI 与词条；具体地面/空中动作、舞踏、位移和粉尘规则以 [insect-glaive-actions.md](insect-glaive-actions.md) 为唯一真相源。M3 已完成 Resource、基础猎虫飞行/召回、单灯/三灯、基础瞄准、虫印弹与四个猎虫原生 GA 父类；E4 仍需创建和接线 DataAsset/GE/GA 蓝图资产，舞踏、粉尘和后续动作由 M4～M6 完成。

> **目标口径：** 项目以《世界》的猎虫与地面虫棍为基底，吸收并改造《崛起》的部分动作，形成原创连招。不是《崛起》逐项复刻；不使用翔虫资源、集中模式或钩爪。

## 当前实现

| 模块 | 当前状态 |
|------|----------|
| C++ 类型 | `AKinsect`、`UKinsectCollisionComponent`、`AIGMarkProjectile`、`UInsectGlaiveKinsectData`、`URes_InsectGlaive`、`UMHGZInsectGlaiveAbility` 及四个基础猎虫 GA 父类已存在；M3 自动化 10/10 通过（包含 ProjectileMovement 发射/召回实际推进回归）。 |
| 装备接入 | RuntimeHost 已按 `DA_WeaponRuntime_IG` 的 ResourceClass/CombatConfig 动态创建 `URes_InsectGlaive`，Resource 自动 Spawn 并挂载猎虫；装备更换和卸下清理已纳入 M2/M3 回归。 |
| GA/连招资产 | 原生 Send/Recall/DrawAndSend/Mark 父类已实现；M4-A.4 的 `UMHGZDrawAttackAbility` 与 `AnimNotify_DrawCommit` 也已实现。M4-A.5 令 Send、Recall 和 DrawAndSend 都以数据型 `ActionMontage` 播放无 Root Motion 的上半身动作：DrawAndSend 在 `DrawCommit` 才拔刀、在后续 `KinsectSendCommit` 才部署冻结请求，且不属于攻击链；Send/Recall 分别在 Send/Recall Commit 后执行 Resource。之后再在 E4 创建/回填 `GA_IG_Draw`、`GA_IG_DrawSlash`、其 Montage 与 Combo 转移。 |
| 猎虫与萃取资产 | 猎虫 Mesh 已存在；Kinsect DataAsset 与 White/Orange/Red/TripleUp GE 待 E4 创建。生产代码已删除硬编码 `/Game/...` 加载，猎虫伤害直接复用原生 `UMHGZDamageGameplayEffect`。 |
| UI/反馈 | AimComponent 已使用 `Aiming.Kinsect` + Visibility/Hitzone 验证并输出颜色 Delegate；Crosshair/三灯/耐力 Widget 与 GameplayCue 资产仍待 E6。 |
| 运行时接线缺口 | Character WeaponRuntimeHost 已统一持有 Resource，并清理召回、换装、UnPossess 和 EndPlay 的当前 M3 资源。未实现的粉尘/舞踏/位移对象待后续阶段纳入同一所有权模型。 |

**设计原则：** 虫棍专属规则由猎虫实体、猎虫耐力、三色精华/三灯、虫印/粉尘、舞踏与虫棍 CombatConfig 共同组成。通用 GA 和连招协调器只处理输入、状态转移、攻击窗口和通用位移，不包含虫棍类型判断。红灯动作模式由 `UInsectGlaiveCombatConfig::RedExtractMode` 决定，默认使用经典动作门控，也可切换为只提供数值 Buff。

---

## 系统总览（M3 基底 + 后续目标架构）

```
AKinsect (独立 Actor, 由 URes_InsectGlaive 管理生命周期)
├── UKinsectCollisionComponent (Root/Projectile UpdatedComponent；只阻挡 WorldStatic)
├── USkeletalMeshComponent (挂在 Collision 下，仅表现)
├── UProjectileMovementComponent (飞行移动——bAutoActivate=false；悬停时停用)
├── 前后帧 Hitzone Object Capsule Sweep（部位查询，不用 Overlap）
├── 动画预留 (当前只有 FlyPlayRate 字段，没有 AnimInstance 播放逻辑)
├── 原子飞行请求: FKinsectFlightRequest + FlightInstanceID
├── 伤害/萃取: 请求内策略 + PendingExtractColor + 每 Hitzone 重击表
└── 品种数据: UInsectGlaiveKinsectData (DataAsset)

URes_InsectGlaive (WeaponResourceComponent, 由 Character WeaponRuntimeHost 持有)
├── 猎虫生命周期: Spawn → AttachToArm → Deploy(FKinsectFlightRequest) → Hover/Return → Destroy
├── 请求校验: RuntimeToken / 不可变 AimSnapshot / PostFlightPolicy
├── 猎虫耐力管理 (KinsectStamina: 悬停扣减 / 休息回复 / 归零自动召回)
├── 猎虫伤害管理 (ApplyKinsectDamage → 借玩家 ASC 走统一 GE 管道)
├── 萃取状态机 (召回时 Apply → Apply Duration GE → 三灯检测)
├── 三灯激活与保护 (替换单独灯 GE、不可刷新、到期全部消失)
├── 三灯原子消耗接口 (觉虫击 Commit 时使用)
├── 虫印、粉尘 Actor 所有权与回收
├── 舞踏层数与清空事件
└── CombatConfig 调参（WeaponResource 词条接收在 Demo 中禁用并延期）

UMHGZInsectGlaiveAbility (攻击基类, 继承 UMHGZAttackAbility)
├── 读取 UInsectGlaiveCombatConfig 与当前舞踏倍率
├── 在伤害 Spec 中快照舞踏/猎虫动作参数
├── 辅助方法: CheckExtractRequirement / ConsumeTripleUpAtomic
└── 通过武器资源接口取得 URes_InsectGlaive，不把虫棍 Cast 写进通用基类

UMHGZDrawAttackAbility（M4-A.4，继承 UMHGZInsectGlaiveAbility）
├── 只允许 Grounded + Sheathed 的 Y 拔刀起手
├── 接收 AnimNotify_DrawCommit 的精确 ActionToken 回调
├── 在实际取刀表现点 SetSheathed(false) + 清 bSprintHeld
└── GA_IG_Draw（零攻击段）与 GA_IG_DrawSlash（Forward 攻击段）共用

UInsectGlaiveCombatConfig (虫棍专属 DataAsset)
├── RedExtractMode: ClassicMovesetGate / NumericOnly
├── 舞踏层数与逐层倍率
├── 特殊动作距离、高度、时长和方向限制
└── 粉尘集约参数

Duration GE (萃取 Buff)
├── GE_IG_WhiteExtract  → MoveSpeedMultiplier ×1.15
├── GE_IG_OrangeExtract → Defense ×1.1 + Combat.Poise.Light
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
| 碰撞判定 | `AnimNotifyState_AttackCollision` 只适合短攻击窗口 | 碰撞体常驻于 Actor，但 Hitzone 伤害/萃取查询只在 Flying 开启；Hovering 不持续轮询伤害 |
| 悬停 | 投射物通常只能销毁或立即返回 | 未命中/到达距离后可原地悬停等待新命令，但悬停不会自动萃取或伤害 |
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
| Collision | TObjectPtr\<UKinsectCollisionComponent\> | "Kinsect\|Component" | CreateDefaultSubobject | Root/Projectile UpdatedComponent；只对 WorldStatic=Block，Hitzone/Pawn=Ignore；部位命中由独立的前后帧 Hitzone Object Sweep 负责 |
| Movement | TObjectPtr\<UProjectileMovementComponent\> | "Kinsect\|Component" | CreateDefaultSubobject | 飞行移动——`bAutoActivate=false`，手动控制 Velocity；无重力、可悬停 |
| State | EKinsectState | "Kinsect\|State" | Attached | 目标状态：Attached / Flying / Hovering / Returning；当前重复的 Recalled 在 M3 删除，到达手臂后直接 Attached |
| OwnerActor | TWeakObjectPtr\<AActor\> | "Kinsect\|State" | nullptr | 玩家引用——收虫时每 Tick 读取实时坐标动态修正回归路径。`AttachToPlayer` 时设置 |
| ActiveRequest | FKinsectFlightRequest | "Kinsect\|State" | 空 | 当前 Flight 的完整不可变请求；替代 bFollowRay/RayDirection/FlyDestination 和分散伤害字段 |
| FlightInstanceID | FGuid | "Kinsect\|State" | 无效 | 每次 BeginFlight 使用新 Guid；作为 Flight 请求身份，不直接作为目标侧逐击去重身份 |
| FlightStartLocation | FVector | "Kinsect\|State" | ZeroVector | 沿 Direction 模式按“本次起点→当前位置”判断 MaxDistance，不以会移动的玩家位置计算 |
| PreviousFlightTransform | FTransform | "Kinsect\|State" | Identity | ProjectileMovement 更新前的位置，用于连续 Sweep |
| PendingWorldHit | FHitResult | "Kinsect\|State" | 空 | 世界阻挡先暂存，Hitzone Sweep 后处理，防止同帧顺序不确定 |
| LastHitTimeByHitzone | TMap\<TWeakObjectPtr\<UPrimitiveComponent\>, double\> | "Kinsect\|State" | 空 | Piercing 每个部位独立重击间隔 |
| PendingExtractColor | FGameplayTag | "Kinsect\|State" | 空 | 萃取暂存颜色——返回到达时原子取出并立即清空，再向 Resource 交付一次；不得跨已完成回手残留 |
| KinsectData | TObjectPtr\<UInsectGlaiveKinsectData\> | "Kinsect\|Data" | nullptr | 品种配置 DataAsset——运行时由 ResourceComponent 注入 |
| ResourceComponent | TWeakObjectPtr\<URes_InsectGlaive\> | "Kinsect\|Reference" | nullptr | 虫棍资源组件引用——用于 ApplyKinsectDamage 调用 |

贯穿间隔按 Hitzone 组件分别记录，不使用一个全局 `TimeSinceLastDamage` 让不同部位互相阻塞。所有目标字段在 `BeginFlight` 一次提交；当前源码中的分散字段只作为迁移输入，不继续扩展。

### 关键方法

- `bool BeginFlight(const FKinsectFlightRequest& Request)`
  - 原子校验并提交整个请求：先生成 FlightInstanceID、清命中表、保存起点/PreviousTransform 和全部参数，再启动 ProjectileMovement/Sweep；失败不打断当前状态。
  - AlongDirection 以 `DirectionSnapshot` 与 FlightStartLocation 计算；ToPoint 以 `TargetPointSnapshot` 计算。Actor 不访问相机或 InputRouter。

- 已删除旧 `SetDamageParams` 与 `TryApplyKinsectDamage` 公开路径。轨迹、伤害、萃取和身份只能通过 `BeginFlight(const FKinsectFlightRequest&)` 原子提交，Piercing 使用每帧 `SweepHitzones` 与每 Hitzone 绝对时间间隔表。

- `void TryRecordExtract(const FHitResult& Hit)`
  - 输入：命中的怪物部位碰撞体。
  - 作用：直接读取 Hitzone 的 `ExtractColorTag`。None→跳过；FirstHitOnly→仅在 PendingExtractColor 为空时缓存；ApplyPerValidHit→只有对应伤害 Spec 成功提交后才立即调用 Resource 的普通 `ApplyExtract`。不采用“红色优先覆盖”的隐式规则。

- `bool ApplyKinsectDamage(const FHitResult& Hit, float MotionValue, const FGuid& HitInstanceID)`
  - 输入：真实 Hitzone 命中、当前动作值和攻击身份。
  - 作用：委托 Resource 走玩家 ASC 的原生通用 Damage GE/EffectContext/HitFeedbackRouter 管道。

- 飞行结束已集中在 `EndFlight(Reason)`：SingleHit 命中、撞墙、最大距离和 ToPoint 到达/越过目标都执行 Request 的 PostFlightPolicy。

- `void StopAndHover()`
  - 作用：`Movement->Velocity = FVector::ZeroVector`（立即停止）→ State=Hovering。有 PendingExtractColor 则保留，等待召回；无则等待玩家重新送虫。

- `void StartReturn()`
  - 目标作用：若已 Returning/Attached 则幂等跳过；否则 State=Returning，并每帧读取 OwnerActor 位置修正速度。到达半径来自 CombatConfig；到达时先停止移动/Sweep，再执行唯一 `CompleteReturn()`。

- `void ForceRecall()`
  - 作用：耐力归零强制召回。调用 `StartReturn()`，**不清除 `PendingExtractColor`**——已萃取到的灯保留，召回后正常 Apply。

- `void Interrupt()`
  - 已实现为停止移动/查询并转 Hovering，不修改 `PendingExtractColor`；新 Request 必须先完整校验再原子提交。

- `void AttachToPlayer(USceneComponent* ArmSocket)`
  - 输入：玩家手臂 Socket 组件。
  - 作用：只完成 `AttachToComponent`、State=Attached、OwnerActor 更新和 Collision Disable。`CompleteReturn` 在调用它之前已把 PendingExtractColor 原子移入局部变量并清空，Attach 后再用局部颜色调用一次 Resource `ApplyExtract`；回调重入也不能重复交付。

- `void EnableKinsectCollision()` / `void DisableKinsectCollision()`
  - 已实现：Collision 是猎虫 Root/ProjectileMovement UpdatedComponent，使用 `Kinsect` Preset 只处理 WorldStatic 阻挡；Hitzone 命中由前后帧显式 Capsule Sweep 负责。

- `void ProcessKinsectSweepHit(const FHitResult& Hit)`（目标）
  - 输入：前后帧 Capsule Sweep 得到的真实 Hitzone 命中。
  - 作用：只在 `State == Flying` 时处理。按 `Hit.Time` 排序并读取命中组件的 `ExtractColorTag`；SingleHit 取首个有效部位后结束，Piercing 按 Hitzone 组件与 `FlightInstanceID` 执行可配置命中间隔。每次伤害沿用原始 HitResult。

- `void OnFlightEnded()`
  - 作用：撞墙、极限距离、到点或 SingleHit 命中时结束飞行并执行 Request.PostFlightPolicy。普通送虫无论是否取得精华都固定 Hover；只有主动召回或耐力归零进入 Return。觉虫击达到终点也 Hover。

- `void SweepHitzones(const FTransform& Previous, const FTransform& Current)`（目标）
  - 作用：ObjectTypes 只查询 Hitzone；不缓存已离开飞行轨迹的 Actor 继续伤害。当前 `GetOverlappingHitzone()`/Overlap 路径在迁移后删除。

- `void OnWorldCollision(const FHitResult& Hit)`
  - 输入：碰撞命中结果。
  - 目标作用：ProjectileMovement 命中 WorldStatic 时先缓存 PendingWorldHit/停止速度，不立即把 State 改出 Flying。Actor Tick 在 Movement 之后对 Previous→Current 做 Hitzone Sweep，再处理缓存的 World Hit；由于 Current 已被世界阻挡截断，不会命中墙后的 Hitzone。

- `float GetFlightSpeed() const`
  - 输出：当前返回 `KinsectData->FlightSpeed`；生产 Resource 要求 KinsectData 必填，词条修正在 Demo 中明确禁用。

- `float GetHoverDrainRate() const` / `float GetFlightDrainRate() const`
  - 输出：当前耐力消耗速率。供 ResourceComponent Tick 读取。

### 猎虫碰撞组件（UKinsectCollisionComponent）

继承 `UCapsuleComponent`，并固定为 `AKinsect` Root 与 `UProjectileMovementComponent::UpdatedComponent`。目标方案不新增 Kinsect Object Channel，也不依赖 Hitzone Overlap：物理组件与命中查询彻底分工。

| 通道/查询 | 常态（停手臂） | 飞行/悬停中 | 召回中 |
|------|:--:|:--:|:--:|
| 显式 Hitzone Object Sweep | 关闭 | **前后帧 Capsule Sweep** ← 萃取/伤害 | 关闭 |
| WorldStatic | Ignore | **Block** ← 墙壁/建筑/地面 | Ignore |
| MonsterAttack | Ignore | Ignore | Ignore |
| Pawn | Ignore | Ignore（穿透玩家和怪物实体 Body） | Ignore |
| Visibility | Ignore | Ignore | Ignore |

> **设计理由：** Weapon 是攻击 Trace Channel，Hitzone 是对象身份。武器用 Weapon Trace；猎虫用 Hitzone Object Sweep；Aim 用 Visibility 并验证 Hitzone ObjectType，避免穿墙。WorldStatic Hit 只负责停止猎虫飞行，Hitzone 不让 ProjectileMovement 反弹；Pawn 始终 Ignore，猎虫不可受击。

#### 与 AnimNotifyState_AttackCollision 的区别

| | AnimNotifyState_AttackCollision（武器攻击） | UKinsectCollisionComponent（猎虫） |
|------|------|------|
| 检测生命周期 | NotifyBegin→NotifyEnd 之间启用每帧 Socket Sweep，不创建临时组件 | 胶囊组件常驻于 AKinsect，飞行期间 Enable，停手臂时 Disable |
| 适用场景 | 固定时长的 Montage 播放中的瞬时判定（0.1-0.5s 窗口） | 持续数秒飞行过程中的不定时命中 |
| 碰撞形状 | 新配置使用一个或多个球形采样 Region；旧版单区域仍兼容 Sphere/Capsule/Box | 固定胶囊体（猎虫形体近似） |
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
| MaxFlightRange | float | 最大飞行距离（cm）——臂上放虫用 |
| StraightFlightDistance | float | 收刀 RT 直飞距离（cm，默认 1500） |
| ReturnSpeed | float | 召回飞行速度（cm/s）——M3 新增字段 |
| StaminaPool | float | 基础耐力上限 |
| StaminaRegenRate | float | 基础耐力回复速率 |
| HoverDrainRate | float | 悬停耐力消耗速率（低于飞行耗耐——降低"飞过头"惩罚） |
| FlightDrainRate | float | 飞行耐力消耗速率 |
| KinsectAttackPower | float | 猎虫基础攻击力（默认 10.0）——当前品种未分化时所有猎虫共用。后续品种分化时可覆写 |

### 飞行请求与轨迹（M3 基础路径已实现）

所有送虫路径只接受一个完整请求，不提供无参 `DeployKinsect()`、方向重载或“启动后再补伤害参数”的接口：

```cpp
struct FKinsectFlightRequest
{
    FWeaponRuntimeToken RuntimeToken;
    EKinsectTrajectoryMode TrajectoryMode; // AlongDirection / ToPoint / ReturnToOwner
    FVector DirectionSnapshot;
    FVector TargetPointSnapshot;
    float MaxDistance;
    float FlightSpeed;
    EKinsectDamageMode DamageMode;         // None / SingleHit / Piercing
    EKinsectExtractMode ExtractMode;       // None / FirstHitOnly / PerValidHit
    EKinsectPostFlightPolicy PostFlightPolicy; // Hover / Return；普通命中项待用户冻结
    float MotionValue;
    float RehitInterval;
    FGuid FlightInstanceID; // 飞行身份；每次伤害另生成 HitInstanceID
};
```

Request 由 GA 根据不可变 ActivationContext 构造，Resource 负责校验和提交，Kinsect Actor 只执行：

1. 核对 RuntimeToken、状态、耐力与参数；拒绝时不改变旧 Flight。
2. 分配新的 FlightInstanceID，清空该 Flight 命中表，并一次写入全部轨迹/伤害/萃取/PostFlight 参数。
3. 必要时中断旧 Flight，Detach，设置速度，最后启用 ProjectileMovement 与 Hitzone Sweep。

输入映射如下：

| 路径 | 请求轨迹 | 数据快照 | 命中策略 |
|------|----------|----------|----------|
| 持刀 LT+Y，猎虫 Attached | AlongDirection | 最终触发 Y 时的 AimSnapshot.Direction | SingleHit + FirstHitOnly |
| 持刀 LT+Y，猎虫 Hovering/Returning | ToPoint | 最终触发 Y 时的 AimSnapshot.TargetPoint | SingleHit + FirstHitOnly；提交新请求可中断返回 |
| 收刀 RT 拔刀直飞 | AlongDirection | 最终触发 RT 时的角色 ActorForward | SingleHit + FirstHitOnly |
| 觉虫击 | AlongDirection | RT+Y+B 最终触发键的 Action Aim 修正方向 | Piercing + ApplyPerValidHit；每次有效伤害立即点灯并生成粉尘 |

臂上送虫沿准心方向而不是飞向射线 HitLocation，避免近距离从手臂向命中点产生横向拐弯；已在外部的猎虫则飞向快照 TargetPoint。GA 激活后不得重读当前相机、`CurrentAimExtractColor` 或当前摇杆。

悬停时停止 ProjectileMovement，保留 WorldStatic 物理组件但关闭 Hitzone Sweep；它不会因怪物主动靠近而造成伤害或萃取。HoverDrainRate 按实际 DeltaTime 消耗，耐力归零进入 Return。普通 SingleHit 命中并缓存颜色后固定 Hover，不会自动 Return；撞墙/到达无命中终点同样 Hover，觉虫击达到距离上限也 Hover。距离、速度与耐力仍为 CombatConfig 参数，但这三条 PostFlight 规则是 Demo 玩法合同，不作为任意调参项漂移。

`EKinsectState` 是部署状态唯一真相源，不再并行维护 `bKinsectDeployed`。`WeaponResource.IG.Kinsect.Active` 如需供分支/UI 查询，由 Resource 按所有权从 State 派生并在 Attached/Shutdown 时成对移除，不能由 GA 添加无所有者 Loose Tag。

### 猎虫生命周期（M3 已接通基础入口）

```
装备虫棍
  → URes_InsectGlaive::OnWeaponEquipped()
    → SpawnActor<AKinsect>(KinsectClass)
    → Kinsect->AttachToPlayer(Character->GetMesh(), "Kinsect_Arm_Socket")
    → State = Attached（碰撞体 Disable，纯静态 Mesh 吸附——无闲置动画）

瞄准送虫（持刀 LT+Y，臂上）——普通单发型
  → GA_SendKinsect → ResourceComponent->DeployKinsect(Request)
    → 校验 Request.RuntimeToken 与当前 EKinsectState
    → Kinsect Detach
    → Request={AlongDirection, AimSnapshot.Direction, MaxRange, SingleHit, ConfigMotionValue, FirstHitOnly}
    → BeginFlight(Request)：先写参数/新 FlightInstanceID，再启动 AlongDirection
    → State=Flying, Hitzone Sweep Enable

悬停重新放虫（持刀 LT+Y，悬停中）——普通单发型
  → GA_SendKinsect → DeployKinsect(Request)
    → Kinsect->Interrupt()
    → Request={ToPoint, HitLocation, SingleHit, ConfigMotionValue, FirstHitOnly}
    → BeginFlight(Request)：先写参数/新 FlightInstanceID，再启动 ToPoint
    → State=Flying

收刀直飞（收刀 RT）——★ 普通单发型
  → GA_DrawAndSendKinsect → DrawCommit 后 Unsheathe → KinsectSendCommit 后 DeployKinsect(Request)
    → Request={AlongDirection, ActorForwardSnapshot, StraightFlightDistance, SingleHit, ConfigMotionValue, FirstHitOnly}
    → BeginFlight(Request)

飞行中每 Tick（仅 State == Flying）：
  ├─ Movement 已先更新 Collision Root；WorldStatic Hit 暂存为 PendingWorldHit
  │
  ├─ SweepHitzones(PreviousTransform, CurrentTransform)
  │     ├─ SingleHit → 最早 Hitzone：FirstHitOnly + ApplyDamageOnce + 停止伤害
  │     └─ Piercing（觉虫击）→ 每 Hitzone 间隔表 + ApplyDamageOnce + 继续飞行
  │
  └─ 若仍 Flying，再处理 PendingWorldHit / 极限距离 / 到点
        → WorldStatic/无萃取终止：StopAndHover
        → 普通 SingleHit 有萃取终止：StopAndHover，保留 PendingExtractColor 等待主动召回/耐力归零

召回（持刀 LT+B / 耐力归零）——可打断飞行/悬停，不可打断返回
  → 若 State == Returning → 静默跳过（已在返回中）
  → 否则 → State = Returning → StartReturn()
    → ★ 每 Tick 读取 OwnerActor->GetActorLocation() 动态追踪玩家位置
    → 到达 → 停止移动/Sweep → ExtractToDeliver = PendingExtractColor；立即清空 PendingExtractColor
    → AttachToPlayer → State = Attached, Collision Disable
    → 若 ExtractToDeliver 有效 → ResourceComponent->ApplyExtract(ExtractToDeliver) 一次
    → Resource 按自身 TagHandle/所有权移除 WeaponResource.IG.Kinsect.Active
    → ★ 耐力归零不会清空萃取——已萃取到的灯在召回后仍会 Apply
    → 若 PendingExtractColor 为空（飞行中未碰到怪物即召回）→ 萃取失败
    → Tick: KinsectStamina += StaminaRegenRate × Δt（回复）

卸下虫棍
  → URes_InsectGlaive::OnWeaponUnequipped()
    → Kinsect->Destroy()
```

### 猎虫动画

**现阶段不做猎虫动画**——纯 Mesh 飞行即可验证碰撞和萃取。后续需要时：单套飞行动画 `Montage_Kinsect_Fly`（翅膀扇动循环），前进/返回/悬停共用，通过 `PlayRate` 区分（Flying=1.5 / Returning=1.0 / Hovering=0.3），AnimBP 只需一个 Slot 节点播放 Montage。

---

## 零-A、猎虫伤害系统（M3 C++ 管道已接通；基础 GA 蓝图资产待 E4）

**设计原则：** 猎虫不挂载 ASC、不新增 GA——伤害走玩家 ASC 的统一 GE 管道。伤害参数（动作值、贯穿间隔、萃取行为）由送虫 GA 传入，不存 DataAsset。飞行结束条件按 `EKinsectDamageMode` 区分：普通放虫命中即停，贯穿放虫碰怪不停、撞墙或飞满距离才停。

### 伤害枚举

```cpp
// 伤害模式
UENUM(BlueprintType)
enum class EKinsectDamageMode : uint8
{
    None,           // 返回/纯位移，不产生伤害
    SingleHit,      // 普通放虫：飞行中只造成 1 次伤害，命中后立即停止飞行
    Piercing        // 贯穿放虫：按间隔持续造成伤害，碰怪不停止飞行
};

// 萃取行为
UENUM(BlueprintType)
enum class EKinsectExtractMode : uint8
{
    None,               // 不萃取
    FirstHitOnly,       // 仅首次命中时缓存一种颜色，回到玩家时交付
    ApplyPerValidHit,   // 每次有效伤害按 Hitzone 立即执行 ApplyExtract；觉虫击使用
};
```

Demo 不设“红＞橙＞白”的隐式覆盖优先级。普通送虫缓存首个有效部位；觉虫击每次有效贯通伤害立即 ApplyExtract，不缓存最后颜色。觉虫击 Commit 已先消费旧三灯，所以贯通过程可以重新取得三色并形成新三灯；形成后续三灯后，之后的萃取按统一规则被吞且不刷新。

### 飞行终止条件

| 条件 | 普通放虫 (SingleHit) | 贯穿放虫 (Piercing) |
|------|:--:|:--:|
| 撞墙 | ✅ 停止 | ✅ 停止 |
| 飞到极限距离 | ✅ 停止 | ✅ 停止 |
| 命中怪物 | ✅ 立即停止 | ❌ 继续飞行 |

停止后由 Request.PostFlightPolicy 执行明确合同：普通送虫命中/未命中都 Hover，觉虫击到达终点 Hover，召回请求才 ReturnToOwner。Actor 不根据“是否有颜色”自行选择隐藏分支。

### 伤害数据流

```
GA 送虫
  → 从 ActivationContext 构造完整 FKinsectFlightRequest
  → Resource->DeployKinsect(Request)
  → Kinsect->BeginFlight(Request) 原子写入后再启动移动/Sweep

飞行中 Tick（仅 State == Flying）：
  → SweepHitzones(PreviousTransform, CurrentTransform)
  → Hitzone Object Capsule Sweep，按 Hit.Time 排序
  ├─ SingleHit：取首个有效 Hit
  │     → TryRecordExtract(Hit.Component->ExtractColorTag)
  │     → ApplyKinsectDamage(Hit, MotionValue, NewHitInstanceID)
  │     → 结束本次飞行并按是否取得精华返回/悬停
  └─ Piercing：遍历有效 Hit
        → 按 Hitzone Component 检查本次 FlightInstanceID 的命中间隔
        → 按 ExtractMode 决定是否记录颜色
        → ApplyKinsectDamage(Hit, MotionValue, NewHitInstanceID)
        → 继续飞行；离开本次 Sweep 的目标不再由 Timer 继续受伤

ApplyDamageOnce 内部：
  → ResourceComponent->ApplyKinsectDamage(Hit, MotionValue, HitInstanceID)
    → 自定义 EffectContext 保存真实 HitResult、HitzoneTag、Kinsect 来源和攻击身份
    → PlayerASC->MakeOutgoingSpec(UMHGZDamageGameplayEffect)
    → SetByCaller: "Damage.MotionValue" = MotionValue
    → SetByCaller: "Damage.AttackPower" = 猎虫基础攻击力（当前无攻击力词条修正）
    → PlayerASC->ApplyGameplayEffectSpecToTarget(Spec, MonsterASC)
    → 统一 ExecCalc/AttributeSet 结算 → HitFeedbackRouter 显式执行反馈
```

### GA 调用对照

| GA | DamageMode | MotionValue | Interval | ExtractMode | 说明 |
|------|:--:|:--:|:--:|:--:|------|
| `GA_SendKinsect`（持刀瞄准送虫） | SingleHit | Config | — | FirstHitOnly | 《世界》基底的普通单发萃取 |
| `GA_DrawAndSendKinsect`（收刀直飞） | SingleHit | Config | — | FirstHitOnly | 拔刀同时正前方单发萃取 |
| `GA_IG_AwakenedKinsectAttack` | Piercing | Config | Config | ApplyPerValidHit | 觉虫击专属贯通；每次有效伤害立即按 Hitzone 点灯并生成粉尘 |

### URes_InsectGlaive 新增方法

- `bool ApplyKinsectDamage(const FHitResult& Hit, float MotionValue, const FGuid& HitInstanceID)`
  - 输入：真实部位命中、当前招式动作值和攻击身份。
  - 作用：借玩家 ASC 构造原生通用 Damage GE Spec；Context 保存 Hit/Cue/来源，SetByCaller 传 `MotionValue` 和 `KinsectAttackPower`，最终反馈由目标 HitFeedbackRouter 显式执行。

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

### 伤害 GE

猎虫不新增专用 GE 蓝图，复用原生 `UMHGZDamageGameplayEffect`。`Damage.AttackPower` 覆写猎虫攻击力，自定义 EffectContext 记录 Kinsect 来源、真实 Hitzone HitResult 与 CueTag。

---

## 一、猎虫耐力（M3 已接入 RuntimeHost 装备流程）

### 数据流

```
瞄准送虫（持刀 LT+Y，臂上）
  → WeaponInputRouter 冻结 AimSnapshot
  → GA_SendKinsect 构造 AlongDirection + SingleHit + FirstHitOnly Request
  → ResourceComponent->DeployKinsect(Request)
    → Request 通过后原子 BeginFlight
    → Tick: KinsectStamina -= FlightDrainRate × 实际 DeltaTime

悬停重新放虫（持刀 LT+Y，悬停中）
  → GA_SendKinsect 构造 ToPoint(AimSnapshot.TargetPoint) Request
  → 新请求校验成功后再中断旧状态并 BeginFlight；失败保持悬停

收刀直飞（收刀 RT）
  → Router 冻结 ActorForward，GA 的 DrawCommit 成功后拔刀
  → DeployKinsect(AlongDirection + ActorForwardSnapshot + StraightFlightDistance)

飞行中（仅 State == Flying）
  → Movement 先更新 Collision Root；WorldStatic Hit 只缓存
  → Actor Tick: SweepHitzones(PreviousTransform, CurrentTransform)
    → 按 Hit.Time 处理真实 Hitzone HitResult
    → SingleHit：记录 FirstHitOnly 精华 + 伤害一次 + 停止并 Hover
    → Piercing：按每 Hitzone 间隔伤害；觉虫击每次有效伤害立即 ApplyExtract 并继续飞行
  → 若仍 Flying，再处理 PendingWorldHit/距离/到点终止

召回（持刀 LT+B / 耐力归零）
  → 若 Kinsect State == Returning → 跳过（已在返回中）
  → GA_RecallKinsect（或耐力归零自动 ForceRecall）
    → Kinsect->StartReturn() → 每 Tick 追踪 OwnerActor 实时位置
    → 到达 → 原子取出并清空 PendingExtractColor
    → AttachToPlayer(ArmSocket) → State = Attached；若取出的颜色有效 → ApplyExtract 一次
    → Resource 按自身所有权移除 WeaponResource.IG.Kinsect.Active
    → Tick: KinsectStamina += StaminaRegenRate × DeltaTime

猎虫耐力归零（放出/悬停中）
  → URes_InsectGlaive::Tick 中判断上一帧 > 0 且本帧 <= 0，并确认 State 不是 Returning/Attached
    → 播放 KinsectDepletedSound
    → Kinsect->ForceRecall()
    → ★ 耐力归零不清空萃取——已萃取到的灯保留
```

### 管理方式

- **归属**：`URes_InsectGlaive` 内部 `float KinsectStamina` / `float MaxKinsectStamina`，非 GAS Attribute（遵循决策 #43）
- **消耗速率**：飞行期间 `FlightDrainRate`、悬停期间 `HoverDrainRate`（< FlightDrainRate）
- **回复速率**：`StaminaRegenRate`（休息时——Attached 状态——每秒回复量）
- **Tick 启用**：由 Character WeaponRuntimeHost 注册运行时组件并驱动 Tick；卸装、死亡或 Pawn 销毁时先停止 Tick，再召回/销毁猎虫和粉尘并移除精华/舞踏状态。
- **耐力归零检测——阈值边沿**：Tick 保存扣减前数值，只在 `Previous>0 && Current<=0` 且 State 为 Flying/Hovering 时触发一次 ForceRecall 和一次音效。Current 持续为 0、已经 Returning 或 Attached 时不得重复触发。
- **装备词条修改**：本轮禁用。当前 `ApplyEntryModifier/GetModifiedParam` 不能正确表达同 Tag 多来源和按参数过滤，Demo CombatConfig 不依赖该路径；修复延后到完整装备词条阶段。
- **UI 显示**：`WBP_IG_KinsectStamina` 独立进度条，位于武器资源 UI 下方，订阅组件 Delegate 更新

---

## 二、三色精华与三灯（C++ 状态机需按本节修订，依赖的 GE 资产未创建）

### 萃取颜色映射

颜色为红、白、橙。颜色属于怪物具体 Hitzone 数据，不能由 Head/Torso/Leg 的全局硬编码决定；下表仅是训练木桩 Demo 的默认配置：

| 怪物部位 | 萃取颜色 | Tag |
|----------|:--:|------|
| Head | 红灯 | `WeaponResource.IG.Extract.Red` |
| Torso | 橙灯 | `WeaponResource.IG.Extract.Orange` |
| Legs | 白灯 | `WeaponResource.IG.Extract.White` |

`UMHGZMonsterHitzoneComponent`/Hitzone 配置直接保存 `ExtractColorTag`。AimComponent、猎虫命中和操虫斩点灯都读取同一个字段；未配置时返回无颜色并输出数据警告。

### 单灯 Buff GE

| GE 蓝图 | 类型 | 基础时长 | GrantedTags | Modifiers |
|------|:--:|:--:|------|------|
| `GE_IG_WhiteExtract` | Duration | 90s | `WeaponResource.IG.Extract.White` | MoveSpeedMultiplier ×1.15 |
| `GE_IG_OrangeExtract` | Duration | 120s | `WeaponResource.IG.Extract.Orange` | Defense ×1.1, 额外 `Combat.Poise.Light` |
| `GE_IG_RedExtract` | Duration | 60s | `WeaponResource.IG.Extract.Red`, `Combat.Branch.Extract.Red` | AttackPower ×1.2 |

> **数值可调：** 上述倍率和时长只是 Demo 初始值，唯一真相源为 `DA_IG_Combat`。GE 只固定 GrantedTags 和 Modifier 形态，具体持续时间/倍率由 Resource 创建 Spec 时从 CombatConfig 注入。白、橙、红各自倒计时；获得第三色时转入统一三灯状态。

倍率统一使用 `Data.IG.Buff.AttackMultiplier/MoveSpeedMultiplier/DefenseMultiplier` SetByCaller；Duration 用 `Spec.SetDuration`。每个 GE 只读取自己需要的键，未使用的键不写入 Spec。

### 三灯触发机制

```
ApplyExtract(Color)
  0. 若 IsTripleUpActive() → 吞掉本次颜色，不 Apply、不缓存、不刷新 TripleUp → return
     IsTripleUpActive = TripleUpHandle 有值且 ASC->GetActiveGameplayEffect(Handle) != nullptr
  1. 保存同色旧 Handle；Apply 新单灯 GE
     → 只有新 Handle 在 ASC 中确实 Active 才替换 ActiveExtractHandles[Color]
     → 再移除旧 Handle；Apply 失败则保留旧灯和旧剩余时间
  3. CheckAndActivateTripleUp()
     → 若三个 ActiveExtractHandles 都仍对应 ASC 中的 Active GE，且三灯当前无效
       → 开启内部 ExtractTransitionGuard，暂缓 UI/属性派生广播
       → Apply GE_IG_TripleUp
       → 只有返回 Handle 在 ASC 中确实 Active，才记录 TripleUpHandle 并移除三个单灯
       → Apply 失败时保留三个单灯，不产生部分状态
       → 绑定 Triple Handle 的移除回调，关闭 Guard 后只广播一次最终状态
```

`ExtractTransitionGuard` 只抑制同一同步事务中的中间广播，不是三灯状态真相源。三灯有效性始终查询 ASC 中对应 Handle；`FActiveGameplayEffectHandle::IsValid()` 只说明 Handle 结构有效，不能单独证明 GE 仍在运行。

### 三灯 GE（GE_IG_TripleUp）

| 属性 | 值 | 说明 |
|------|------|------|
| DurationPolicy | HasDuration | 固定时长，不可刷新 |
| Duration | `TripleUpDuration`（默认 90s） | 由 ResourceComponent 在 Apply 前通过 `SetDuration` 设置 |
| GrantedTags | `WeaponResource.IG.TripleUp`, `Combat.Branch.TripleUp`, `Combat.Branch.Extract.Red` | 三灯状态；额外保留 Classic 完整动作权限，但不伪造三个单灯状态 Tag |
| Modifiers | AttackPower ×1.25, MoveSpeedMultiplier ×1.15, Defense ×1.15 | 全面强化 |
| 额外 GrantedTags | `Combat.Poise.Medium` | 中霸体 |
| StackingPolicy | 不允许叠加 | 三灯期间再次萃取不刷新 |

> **不可刷新保证**：`ApplyExtract` 必须在创建任何单灯 GE 前先调用 `IsTripleUpActive()`。三灯期间普通猎虫、操虫斩等所有吸收路径都调用同一函数，颜色直接被吞，不缓存也不延长 TripleUp。

### 三灯到期

三灯 GE 到期/被觉虫击消费 → Handle 对应的移除回调核对身份后清空 `TripleUpHandle` → Triple/Branch Tag 移除 → UI 三个灯图标同时暗灭 → 玩家回归零灯状态。旧 Handle 的迟到回调不得清除后来新建的三灯。**单灯不会“残留”**——三灯 GE 不带 `WeaponResource.IG.Extract.Red/White/Orange`；`Combat.Branch.Extract.Red` 只是 Classic 动作权限，随 Triple GE 一起移除。

---

## 三、两套红灯模式（规划；当前 ComboData 未配置）

红灯模式存放在虫棍专属 `UInsectGlaiveCombatConfig`，默认 `ClassicMovesetGate`。完整字段与切换规则见 [虫棍 Demo 动作设计 §4](insect-glaive-actions.md#4-虫棍专属战斗配置)。

### ClassicMovesetGate（默认）

同一输入在唯一 `DA_IG_Combo` 中按模式和红灯状态配置转移，由 ASC Tag 自然分流：

| 状态 | Priority | RequiredTags | 目标 GA |
|------|:--:|------|------|
| Classic 无红灯 | 0 | Require `Combat.Config.IG.RedMode.Classic`；Block `Combat.Branch.Extract.Red` | 弱化动作组 |
| Classic 有红灯 | 10 | Require `Combat.Config.IG.RedMode.Classic` + `Combat.Branch.Extract.Red` | 正常完整动作组 |
| Numeric 任意红灯状态 | 10 | Require `Combat.Config.IG.RedMode.Numeric`；不检查红灯 | 正常完整动作组 |

匹配逻辑（协调器现有机制，零改动）：
1. `HandleWeaponInput(Input.Weapon.Y)` → 查找 `StateIndex` 中所有候选节点
2. 遍历候选 → `RequiredTags` 全部满足 + `BlockedTags` 无一满足 → 取 `Priority` 最高
3. 红灯存在时 ASC 持有 `Branch.Extract.Red` → 红灯版匹配成功（Priority 10 > 0）
4. 红灯到期 → ASC 失去 `Branch.Extract.Red` → 完整动作节点不再满足 → 回到弱化动作组

### NumericOnly

该模式仍使用同一 ComboData，只把 RuntimeHost 拥有的互斥模式 Tag 切换为 `Combat.Config.IG.RedMode.Numeric`。Numeric 行不检查 `Combat.Branch.Extract.Red`，所以有灯和无灯都使用同一完整动作组；`GE_IG_RedExtract` 仍提供 CombatConfig 注入的数值 Buff。白、橙及三灯规则不变。

---

## 四、动作系统接口

具体动作规则已移至 [insect-glaive-actions.md](insect-glaive-actions.md)。资源系统在 Demo 中只向动作层提供以下稳定接口：

- `GetRedExtractMode()`：返回当前 CombatConfig 的红灯模式。
- `TryConsumeTripleUpAtomic()`：觉虫击 Commit 时一次性验证并清空三灯。
- `ApplyExtractFromHitzone(Hitzone)`：普通猎虫召回或操虫斩命中时按 Hitzone 配置点灯。
- `ApplyExtract(Color)`：所有吸收路径共用；三灯期间直接吞灯且不刷新。
- `SetKinsectMark(Hitzone, LocalImpactPoint)` / `ClearKinsectMark(Reason)`：维护唯一虫印弱引用和到期 Timer。
- `AddDanceStack(Source)` / `ClearDanceStacks(Reason)`：仅接受本文定义的来源和清空原因。
- `SpawnPowder(HitResult)` / `ReservePowdersInRadius(...)`：觉虫击和粉尘集约共享的粉尘所有权接口。

降龙不消耗精华；Demo 唯一消耗三灯的动作是觉虫击。旧的 `Extract Surge`、`Triple Burst` 和“降龙消耗红灯”方案不再属于目标设计。

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

- 三个圆形灯图标：白（左）· 橙（中）· 红（右）
- 灯激活/到期/消耗——Tag 事件驱动动画（非 Tick）
- 三灯齐聚→三灯外圈金色光环合一动画
- 数据源：ASC GameplayTag `WeaponResource.IG.Extract.*` + `WeaponResource.IG.TripleUp`

### 准心（WBP_Crosshair）

- LT 瞄准时可见——准心样式随瞄准目标变化
- 对准怪物部位→显示对应萃取颜色（红/橙/白）+ 缩放动画
- 对准空气/场景→默认样式
- 数据源：`UMHGZAimComponent::OnAimTargetChanged` Delegate

### WeaponRuntimeDefinition 注册（目标）

`DA_WeaponRuntime_IG` 在 M4-A/E4-A 统一引用 `URes_InsectGlaive`、`DA_IG_InputProfile` 和 `DA_IG_Combat`；`ResourceWidgetClass` 保持 `None`。M6/E6 创建 `WBP_IG_ResourcePanel` 后才补入同一 RuntimeDefinition。虫棍物品 WeaponDefinition 始终只引用这一个资产；不再额外维护 DT_WeaponResourceConfig 行。阶段顺序见 [阶段门禁](milestone-gates.md)。

---

## 七、装备词条加成（Demo 明确延期且不可依赖）

当前基类以单 Tag Map 保存 Modifier，无法保留同参数的多个来源；`GetModifiedParam` 的目标参数过滤也没有形成可靠合同。为避免 Demo 表面可调但实际把错误倍率应用到其他参数，本轮不接通 `ApplyEntryModifier`，以下目录仅保留为未来需求草案，不属于 M0～M7 验收。

### 词条目录新增（DT_EntryCatalog）

| EntryID | EffectType | Modifiers | 说明 |
|------|:--:|------|------|
| IG_KinsectRegenUp | WeaponResource | `{Attr=WeaponResource.IG.KinsectRegenRate, Op=Multiply, Curve=Curve_IG_Regen}` | 猎虫耐力回复速度 UP |
| IG_TripleUpExtend | WeaponResource | `{Attr=WeaponResource.IG.TripleUpDuration, Op=Multiply, Curve=Curve_IG_TripleUp}` | 三灯时间延长 |
| IG_ExtractExtend | WeaponResource | `{Attr=WeaponResource.IG.ExtractDuration, Op=Multiply, Curve=Curve_IG_Extract}` | 萃取有效时间延长 |
| IG_HoverDrainReduce | WeaponResource | `{Attr=WeaponResource.IG.HoverDrainRate, Op=Multiply, Curve=Curve_IG_HoverDrain}` | 猎虫悬停耐力消耗减少 |
| IG_FlightDrainReduce | WeaponResource | `{Attr=WeaponResource.IG.FlightDrainRate, Op=Multiply, Curve=Curve_IG_FlightDrain}` | 猎虫飞行耐力消耗减少 |

### 未来生效路径（未冻结）

未来必须先把来源身份、目标参数、叠加顺序和移除句柄设计成可验证的多来源模型，再接入装备。当前 `ActiveModifiers Map` 方案不得作为实现指导；Demo 直接使用 `UInsectGlaiveCombatConfig` 的基础参数。

---

## 八、GameplayTag 完整层级

### 武器资源——虫棍

```
WeaponResource.IG.Extract.White       ← 白灯激活中
WeaponResource.IG.Extract.Orange      ← 橙灯激活中
WeaponResource.IG.Extract.Red         ← 红灯激活中
WeaponResource.IG.TripleUp            ← 三灯齐聚中
WeaponResource.IG.Kinsect.Active      ← 猎虫放出中
WeaponResource.IG.Mark.Active         ← 存在有效虫印（运行时目标弱引用仍由 ResourceComponent 保存）

WeaponResource.IG.KinsectRegenRate    ← 猎虫耐力回复速率（词条用）
WeaponResource.IG.HoverDrainRate     ← 悬停耐力消耗速率（词条用）
WeaponResource.IG.FlightDrainRate    ← 飞行耐力消耗速率（词条用）
WeaponResource.IG.TripleUpDuration    ← 三灯时长（词条用）
WeaponResource.IG.ExtractDuration     ← 萃取时长（词条用）
```

### 战斗分支

```
Combat.Branch.Extract.Red             ← 红灯连招分支（FComboTransition::RequiredTags 用）
Combat.Branch.TripleUp                ← 三灯连招分支
```

### 输入

| 操作 | 输入 Tag | 连招表节点 | 说明 |
|------|------|------|------|
| 拔刀（仅拔刀） | `Input.Weapon.Y` | `Idle → DrawOnly`，`Direction=None`，`RequiredTags={Grounded,Sheathed}` | ★ `None` 是方向通配：收刀无、左、右、后 Y → `GA_IG_Draw`，只拔刀；不配置 AttackSegment。 |
| 拔刀（攻击） | `Input.Weapon.Y` + `Forward` | `Idle → GroundStarter`，`Direction=Forward`，`RequiredTags={Grounded,Sheathed}` | ★ 具体 Forward 优先于 None：收刀前+Y → `GA_IG_DrawSlash`，拔刀攻击；奔跑中均在 DrawCommit 时清奔跑并切姿态。 |
| 猎虫瞄准 | `Input.Modifier.LT` | — | 持刀态 LT 长按→ASC 持有 `Combat.State.Aiming.Kinsect` |
| 送虫（瞄准） | `Input.Weapon.LTY` | `RequiredTags={Unsheathed,Aiming.Kinsect}` | 持刀地面 LT+Y；沿准心射线飞出 |
| 召回（瞄准） | `Input.Weapon.LTB` | `RequiredTags={Unsheathed,Kinsect.Active}` | 持刀地面 LT+B；空中同一输入由状态分流为操虫斩 |
| 虫印斩 | `Input.Weapon.RT` | `RequiredTags={Unsheathed,Grounded}` | 持刀地面 RT 未被 A/B/Y/LT 等获胜组合消费时，松开 RT 后近战命中 Hitzone 建立/替换唯一虫印 |
| 虫印弹 | `Input.Weapon.LTRT` | `RequiredTags={Unsheathed,Aiming.Kinsect}` | 持刀 LT+RT；命中 Hitzone 后建立/替换唯一虫印 |
| 拔刀直飞 | `Input.Weapon.RT` | `RequiredTags={Combat.State.Sheathed,Grounded}` | 收刀地面按下 RT → 拔刀并沿角色 Forward 放虫；姿态/奔跑只在 DrawCommit 时改变 |
| 急袭突刺 | `Input.Weapon.RT` | `RequiredTags={Aerial}` | 空中无论收/拔刀，按下 RT 立即发动；不使用松开回退 |
| 纳刀 | `Input.Sheathe` | 通用路由（非连招表） | 仅持刀地面态按 RB 立即触发；空中不派发，攻击/硬直/击倒/死亡/动作锁中无效；只在 `SheatheCommit` 切 `Combat.State.Sheathed`，动作尾帧仍持有 `Sheathing` |
| 奔跑 | `Input.Sprint` | — | 收刀态按住 RB ≥0.1s 进入奔跑；点按不闪跑；拔刀/攻击中不产生奔跑 |
| 四连印斩 | `Input.Weapon.YB` | 地面动作节点 | 无方向 Y+B |
| 突进回旋斩 | `Input.Weapon.YB` + `Forward` | 地面方向节点，Priority 高于四连印斩 | 前+Y+B |
| 粉尘集约/降龙 | `Input.Weapon.LTYB` | 地面/空中状态分流 | 地面为粉尘集约，空中为降龙 |
| 猎虫滑翔 | `Input.Weapon.RTY` | 地面动作节点 | 有虫印追虫印，否则短距前飞 |
| 觉虫击 | `Input.Weapon.RTYB` | 地面且 TripleUp | 原子消耗三灯 |

> **输入规则：** 收刀状态一般不能进行猎虫瞄准送/收虫，唯一例外是地面 RT 的“拔刀并正前方放虫”；空中 RT 始终为急袭突刺。持刀地面单 RT 在松开时才成为虫印斩，且只在不存在已消费 RT 的获胜组合时成立。收刀态按住 RB 奔跑；仅持刀地面按 RB 才立即纳刀，空中 RB 不派发 `Input.Sheathe`，攻击/硬直/击倒/死亡/动作锁中无效。所有组合键先形成唯一 InputTag，再由状态、修饰键、方向和 Priority 分流；完整动作表见 [insect-glaive-actions.md §3](insect-glaive-actions.md#3-输入与方向判定)。

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
│   └── MHGZInsectGlaiveAbility.h/cpp       ← 虫棍 GA 基类（资源接口 + 舞踏倍率快照 + 三灯原子消费）
├── InsectGlaive/
│   └── Kinsect/
│       ├── Kinsect.h/cpp                   ← 猎虫 Actor（骨骼模型 + 碰撞 + 飞行移动 + ★ 伤害/萃取枚举与控制）
│       ├── KinsectCollisionComponent.h/cpp ← 猎虫 Root 胶囊（WorldStatic 阻挡；Hitzone 由显式 Sweep 查询）
│       └── InsectGlaiveKinsectData.h/cpp   ← 猎虫品种 DataAsset（模型/速度/耐力/飞行距离 + ★ KinsectAttackPower）

Content/
├── GameplayEffects/InsectGlaive/
│   ├── GE_IG_WhiteExtract.uasset            ← 白灯 Duration GE
│   ├── GE_IG_OrangeExtract.uasset           ← 橙灯 Duration GE
│   ├── GE_IG_RedExtract.uasset              ← 红灯 Duration GE
│   └── GE_IG_TripleUp.uasset                ← 三灯 Duration GE（不可刷新）
├── Blueprints/Ability/InsectGlaive/
│   ├── GA_IG_Draw.uasset                   ← 收刀 Y 非 Forward：仅拔刀（M4-A.4）
│   ├── GA_IG_DrawSlash.uasset              ← 收刀前+Y：拔刀攻击（M4-A.4）
│   ├── GA_IG_SendKinsect.uasset             ← 持刀瞄准送虫（单发：SingleHit / FirstHitOnly）
│   ├── GA_IG_DrawAndSendKinsect.uasset      ← 收刀直飞（单发：SingleHit / FirstHitOnly）
│   ├── GA_IG_RecallKinsect.uasset           ← 召回
│   ├── GA_IG_MarkSlash.uasset                ← 持刀地面单 RT 松开后的近战虫印斩
│   ├── GA_IG_MarkTarget.uasset              ← LT+RT 虫印弹
│   ├── GA_IG_TetrasealSlash.uasset          ← 四连印斩
│   ├── GA_IG_AdvancingRoundslash.uasset     ← 突进回旋斩/反击舞踏
│   ├── GA_IG_PowderVortex.uasset            ← 粉尘集约
│   ├── GA_IG_KinsectGlide.uasset            ← 猎虫滑翔
│   ├── GA_IG_AwakenedKinsectAttack.uasset   ← 觉虫击（消耗三灯）
│   ├── GA_IG_KinsectSlash.uasset            ← 操虫斩
│   ├── GA_IG_EnhancedKinsectSpiker.uasset   ← 强化操虫穿刺
│   ├── GA_IG_StrongJumpingSlash.uasset      ← 强化跳跃斩
│   ├── GA_IG_DescendingThrust.uasset        ← 急袭突刺
│   └── GA_IG_DivingWyvern.uasset            ← 降龙（无翔虫/精华消费）
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
└── Weapons/InsectGlaive/Data/
    └── DA_WeaponRuntime_IG.uasset            ← Resource/Input/Combat/Widget 的唯一运行时接线
```

---

## 十、设计决策

| # | 决策 | 理由 |
|---|------|------|
| IG-0 | 猎虫为独立 AActor——含骨骼模型、碰撞体、飞行移动组件 | 猎虫需要独立视觉表现（品种差异化模型+动画）、常驻碰撞体（避免逐帧创建/销毁）、自主飞行移动（非骨骼跟随）。简单投射物/粒子方案无法满足怪猎猎虫的交互复杂度 |
| IG-0b | 新增 Hitzone Object Channel；猎虫以前后帧 Capsule Sweep 显式查询，不使用 Kinsect Object Channel/Overlap | Weapon 仍是攻击 Trace Channel；Hitzone 提供对象身份。猎虫 Collision Root 只阻挡 WorldStatic，命中与物理移动互不污染 |
| IG-0c | 猎虫品种用 UInsectGlaiveKinsectData（PrimaryDataAsset）配置 | 遵循决策 #18——策划编辑友好、异步加载。品种决定模型/材质/动画集 + 飞行速度/耐力/萃取倍率数值 |
| IG-0d | 猎虫对 Pawn 通道始终 Ignore——不参与物理阻挡且不可受击 | 猎虫太小、非物理实体，可穿透玩家和怪物身体。猎虫无受击机制——怪物攻击不命中猎虫 |
| IG-0e | 猎虫动画极简化——仅单套飞行动画，停手臂无动画 | 猎虫不是战斗核心（武器才是），无需复杂 Idle/Attack/Stagger 动画。停手臂时静态 Mesh 已足够——昆虫停在手上本来几乎不动，视觉差异不可感知 |
| IG-0f | 猎虫无受击——不可被怪物攻击命中 | 怪猎系列猎虫从未有受击机制。猎虫太小、飞行轨迹灵活，怪物攻击命中猎虫既不合理也无必要 |
| IG-0g | 萃取和伤害仅发生在飞行途中（State==Flying），悬停不会触发萃取或伤害 | 萃取和伤害是猎虫飞行中"咬"到怪物表皮——需要飞行的接触判定。悬停时猎虫原地微振翅膀，即使怪物主动靠近也不触发。这要求玩家精准瞄准怪物部位送虫——未命中则召回重新尝试。萃取/伤害无攻击动画，视觉反馈由 `GameplayCue.*` 粒子+音效表现 |
| IG-0h | 目标坐标由 URes_InsectGlaive 计算、以 FVector 传给 AKinsect | 目标坐标计算逻辑属于资源系统（知道玩家状态、相机方向），不属于猎虫 Actor（只知道"飞去哪"）。用纯 FVector 解耦——猎虫零依赖怪物 Actor |
| IG-0i | 飞行双模式——瞄准飞行（LT+Y/B）和收刀直飞（RT） | 两种使用场景：战斗中的精准萃取（瞄准）+ 快速接近/应急（直飞）。共用同一套飞行和悬停逻辑 |
| IG-0j | 悬停消耗耐力小于飞行消耗——鼓励玩家大胆送虫，但悬停不能萃取 | 悬停是"飞行未命中目标"后的等待状态——玩家应召回重新瞄准，而非悬停等待怪物经过。低耗耐降低"飞过头"的惩罚，但不提供萃取捷径 |
| IG-0k | 耐力归零不清空 PendingExtract | 飞行命中只记录待交付颜色；耐力归零会强制召回，猎虫到达玩家时仍正常交付 |
| IG-0l | 收虫时猎虫动态追踪玩家实时位置（非锁死召回瞬间坐标） | 收虫期间玩家可以移动——锁定召回瞬间坐标会导致猎虫飞向"空气"。每 Tick 读取 OwnerActor->GetActorLocation() 动态修正方向 |
| IG-0m | 持刀地面的 LT+Y/LT+B 负责放虫与收虫；是否能取消当前攻击由该动作显式窗口决定 | 普通 Y/B 始终保留给武器连招，猎虫操作不能依赖任意状态全局抢占；允许取消的窗口在 ComboData/Notify 中可见 |
| IG-0n | 耐力归零由 Tick 的正值→零阈值边沿触发一次，不设独立 Timer | 避免 Current=0 时每帧重复音效和 ForceRecall；Returning/Attached 不再触发 |
| IG-1 | 猎虫耐力在 ResourceComponent 内用纯 float 管理（非 GAS Attribute） | 遵循决策 #43；Demo 数值直接来自 CombatConfig，资源词条链路延期 |
| IG-2 | 萃取 Buff 用 Duration GE + GrantedTag（纯 Tag 方案） | 遵循决策 #83——限时 Buff 用 Duration GE；ASC Tag 供连招表/其他系统查询 |
| IG-3 | 三灯不可刷新——以 ASC 中仍 Active 的 TripleUpHandle 为真相源 | `IsValid()` 不能单独证明 GE 存活；ApplyExtract 入口查询 ActiveEffect，移除回调按 Handle 身份清空，不维护第二个 bool |
| IG-4 | 三灯到期全部灯消失；Triple GE 不携带三个单灯状态 Tag，但携带红灯动作分支 Tag | Classic 三灯期间仍使用完整动作；到期后动作权限和三灯一起消失，玩家重新萃取 |
| IG-5 | 红灯支持 ClassicMovesetGate 与 NumericOnly，默认 Classic；两者共用一个 CombatConfig/ComboData | RuntimeHost 只拥有一个互斥 Mode Tag；经典行再按红灯分流，Numeric 正常行不检查红灯，不污染通用协调器 |
| IG-6 | 三灯攻击音效在 GA 激活时播放（非命中时） | 挥刀音效跟随招式而非命中判定——无论空挥还是命中都播放。通过 `UMHGZInsectGlaiveAbility::ActivateAbility` 覆写中 `PlaySound2D` 实现，不经过 GameplayCue |
| IG-7 | 萃取颜色直接配置在怪物 Hitzone 数据上 | 不同怪物同名部位可以有不同颜色；Aim、猎虫和操虫斩共享同一数据源，猎虫品种不能改写怪物颜色 |
| IG-8 | Demo 只有觉虫击原子消耗三灯；降龙不消耗精华 | 移植动作不继承翔虫成本，也不沿用旧的原创消耗灯技能 |
| IG-9 | 三灯期间所有吸收路径统一吞掉单色精华 | `ApplyExtract` 在入口直接返回；不创建单灯 GE、不缓存颜色、不刷新 TripleUp |
| IG-10 | 普通猎虫回手时原子取出、清空并交付一次 PendingExtract | 主动召回和耐力归零不使已取得颜色失败；清空后下一次 Flight 能获取新颜色，迟到回调不能重复交付 |
| IG-11 | 猎虫飞行结束条件统一为三种（撞墙/极限距离/普通命中）——贯穿放虫碰怪不终止 | 贯穿伤害需要猎虫完整穿过怪物身体；普通放虫命中后停止并 Hover，只有主动召回或耐力归零才回手；贯穿放虫只有墙或距离能让它停下 |
| IG-12 | 送虫 GA 从 CombatConfig/ActivationContext 构造完整 `FKinsectFlightRequest`，Resource 原子提交 | 动作值属于招式不属于品种；删除 SetDamageParams+StartFlight 两步竞态，当前阶段暂不做品种数值分化 |
| IG-12b | 猎虫伤害走玩家 ASC 的统一 GE 管道——不新增 GA、不给猎虫加 ASC | 复用现有伤害公式（AttackPower × MotionValue × HitzoneDefense）、GameplayCue 火花/音效/伤害数字管道。猎虫无"出招"概念，不需要 Ability 激活/取消生命周期 |
| IG-13 | 猎虫击中怪物后的行为由 `EKinsectDamageMode` 控制——SingleHit 立刻停止，Piercing 继续飞行 | 普通放虫咬一口就原地悬停——玩家回收取得萃取。贯穿放虫穿过去才停——最大化贯穿伤害次数 |
| IG-14 | 猎虫萃取行为通过 `EKinsectExtractMode` 枚举控制，由送虫 GA 传入 | 萃取策略属于招式设计范畴（伤害贯穿 vs 萃取贯穿 vs 混合），不硬编码在猎虫内部。一个 GA 可以送"纯伤害无萃取"的虫，另一个可以送"标准萃取贯穿"的虫 |
| IG-15 | Demo 不设贯穿颜色优先级；普通送虫固定 FirstHitOnly，觉虫击固定 ApplyPerValidHit | 每次觉虫击有效贯通伤害立即走普通点灯；Actor 不按颜色排序或缓存最后颜色 |
| IG-16 | SingleHit/Piercing 统一使用前后帧 Hitzone Object Capsule Sweep | Overlap 在高速飞行下可能漏判，且全局冷却会让不同部位互相阻塞；Sweep 保留真实 HitResult，Piercing 按 Hitzone 组件记录间隔 |
| IG-17 | 猎虫移动用 `UProjectileMovementComponent`（`bAutoActivate=false`），不继承 APawn | 当前回归由 Returning Tick 手动追踪 OwnerActor 并更新 Velocity；不使用 HomingTargetComponent |
| IG-18 | 猎虫输入与攻击输入都经同一协调器，但使用 LT/RT 组合 Tag 和显式取消窗口 | 收刀 RT 是唯一收刀放虫入口；持刀 LT+Y/LT+B 不与普通 Y/B 争抢。地面/空中上下文解决 LT+B 和 LT+Y+B 的动作复用 |
| IG-19 | 虫棍动作详情独立维护在 insect-glaive-actions.md | 资源文档只定义稳定接口；位移、反击、舞踏和粉尘不再散落于多个章节 |

---

## 十一、规划验收清单（完整链路接通后执行）

| # | 测试项 | 预期结果 |
|:--:|------|------|
| 0a | 装备虫棍→猎虫 Spawn 并吸附手臂 | Kinsect Actor 存在；State=Attached；碰撞体 Disable；无动画 |
| 0b | 送虫→猎虫 Detach 并沿准心飞出 | State=Flying；Collision Root/Projectile UpdatedComponent 正确；Hitzone Sweep 启用；耐力开始扣减 |
| 0c | 普通放虫命中怪物→造成 1 次伤害并记录首次颜色 | SingleHit Sweep 只结算最早有效 Hitzone；随后立即停止伤害/Sweep 并 Hover，不自动回手；主动召回或耐力归零后交付 |
| 0c2 | 觉虫击贯穿多个 Hitzone | Piercing 按每 Hitzone 间隔结算并继续飞行；每次有效伤害保留真实 HitResult、立即 ApplyExtract 并生成粉尘 |
| 0c3 | 觉虫击猎虫离开 Hitzone | 后续 Sweep 不再命中时立即停止对该部位伤害，不存在缓存目标 Timer |
| 0c4 | 觉虫击同一部位多次有效命中 | 每次成功提交伤害后恰好 ApplyExtract 一次、生成粉尘一次并走 HitFeedbackRouter；命中次数受每 Hitzone Config 间隔限制，伤害提交失败时三者均不发生 |
| 0d | 有 PendingExtract 的猎虫返回玩家（主动召回或耐力归零） | 到达→Attached→原子取出并清 Pending→ApplyExtract 一次→UI 灯亮起，Active Tag 清理 |
| 0d2 | 无 PendingExtract 的猎虫被主动召回 | 到达→AttachToPlayer→无灯 Apply→UI 无变化，耐力进入附着恢复逻辑 |
| 0e | 猎虫飞行/悬停中穿透怪物身体 | Pawn 通道 Ignore→猎虫不被怪物物理阻挡，可在怪物身体附近悬停 |
| 0f | 不同品种猎虫→不同外观 | 切换 DA_Kinsect_Speed → Mesh + Material + FlyMontage 正确加载 |
| 0g | 臂上 LT+Y→猎虫沿准心射线单发飞出 | 完整 Request 原子提交；SingleHit/FirstHitOnly；轨迹=最终 Y 触发时 AimSnapshot.Direction |
| 0h | 收刀 RT→猎虫单发飞出+拔刀 | AlongDirection 使用最终 RT 触发时 ActorForwardSnapshot 与 StraightFlightDistance |
| 0i | 悬停中 LT+Y→猎虫单发飞向新目标坐标 | ToPoint 使用最终 Y 触发时 AimSnapshot.TargetPoint；校验成功后才 Interrupt；新 FlightInstanceID/命中表重置 |
| 0j | 飞行中按 LT+B→立即中断飞行→开始返回 | Interrupt 被调用; State 切换 Returning; 上次飞行萃取结果保留 |
| 0k | 返回中按 LT+Y→中断返回→飞向新目标 | Interrupt 被调用; 若有 PendingExtractColor 则保留; 重新放虫 |
| 0l | 收虫时玩家移动→猎虫实时修正回归方向 | 每 Tick 读 OwnerActor->GetActorLocation(); 回归路径始终指向玩家当前位置 |
| 0m | 飞行中耐力跨到 0→强制召回→不清空萃取 | 阈值边沿调用一次 ForceRecall；PendingExtractColor 保留；后续 0 耐力 Tick 不重复调用或播音效 |
| 0n | 飞行中撞到墙壁/建筑→立即停止→就地悬停 | OnWorldCollision 触发; Movement 停止; State=Hovering; 不萃取 |
| 0o | 同帧遇到 Hitzone 与墙壁 | Projectile 世界阻挡先把 Current 截在墙面；Actor Tick 先处理 Previous→Current 的 Hitzone Sweep，再处理 PendingWorldHit，不命中墙后部位 |
| 0q | 普通放虫 FirstHitOnly | 首个有效 Hitzone 写入 PendingExtract；同一 Flight 后续结果不覆盖 |
| 0r | 第一次回手交付红色后再送虫命中白色 | 第一次到达后 Pending 为空；第二次可记录并交付 White，不残留或重复 Red |
| 1 | 猎虫放出→耐力持续下降 | Tick 中每帧扣减正确；UI 条同步更新 |
| 2 | 猎虫耐力归零→自动强制召回 | 警告音效和召回请求各一次；已有 PendingExtract 回手交付；Attached 后耐力开始回复 |
| 3 | 猎虫命中 Head→召回→红灯 Apply | ASC 持有 `Extract.Red` + `Branch.Extract.Red`；UI 红灯亮起+倒计时 |
| 4 | 猎虫命中 Torso→召回→橙灯 Apply | ASC 持有 `Extract.Orange`；Defense 提升 |
| 5 | 猎虫命中 Leg→召回→白灯 Apply | ASC 持有 `Extract.White`；MoveSpeed 提升 |
| 6 | 依次获得白→橙→红→三灯自动触发 | 三个单独灯 GE 被移除→三灯 GE Apply→ASC 持有 `TripleUp` + `Branch.TripleUp` + `Branch.Extract.Red`，但无三个单灯状态 Tag |
| 7 | 三灯期间再次萃取→不刷新 | 三灯 GE 剩余时间不变；ApplyExtract 查询 Active Triple Handle 后直接吞灯 |
| 8 | 三灯 GE 到期→全部灯消失 | ASC 失去所有 IG Tag；匹配 Handle 的移除回调清空 TripleUpHandle；UI 三灯同时暗灭 |
| 9 | Classic 模式有红灯→完整动作组 | 协调器匹配 RequiredTags 含红灯的正常动作转移 |
| 10 | Classic 无红灯 / NumericOnly 任意灯状态 | 前者匹配弱化动作组；后者始终使用同一完整动作组，只改变数值 |
| 11 | 三灯状态下发动攻击→特殊挥刀音效播放 | `UMHGZInsectGlaiveAbility::ActivateAbility` 检测 TripleUp Tag → `PlaySound2D(TripleUpSwingSound)`；空挥也播放 |
| 12 | 觉虫击：三灯存在→Commit→原子消耗 | 旧三灯与三个单灯状态一次性归零；猎虫每次成功贯通伤害立即 ApplyExtract 并留下粉尘，三色可在本次飞行中重新形成三灯 |
| 13 | 觉虫击：无三灯→激活被拒 | 资源不变化，猎虫和猎人位移都不启动 |
| 14 | 操虫斩在三灯期间命中橙色部位 | 走统一 ApplyExtract；Orange 被吞，三灯剩余时间和状态完全不变 |
| 15 | 降龙任意精华状态均可激活 | 不消耗红灯/三灯；只使用并清空当前舞踏层数 |
| 16 | Demo 配置引用 WeaponResource 词条 | Data Validation/内容审计拒绝；本轮不得依赖未修复的 ApplyEntryModifier |
| 17 | 卸下虫棍→ResourceComponent 销毁→所有灯清除 | ASC 失去所有 IG Tag；UI 复位 |
| 18 | 虫棍连招表 DA_IG_ComboData 异步加载→加载期间输入静默忽略 | 遵循决策 #94——加载期间 StateIndex 为空 |

---

## 十二、瞄准与 UI 集成

**设计原则：** UI 由 GameplayTag/Attribute/Delegate 驱动，Ability 不直接操作 UI。虫棍瞄准预览由 Character 的 `UMHGZAimComponent` 提供，`WBP_Crosshair` 订阅；`AMHGZHUD` 是本地 Widget 树的唯一所有者，按 RuntimeDefinition 在 WBP_HUD 的资源插槽创建/销毁虫棍面板并验证 RuntimeToken。当前空壳 UISubsystem 删除。

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

AimComponent 不在 BeginPlay 只尝试一次绑定 ASC。它在 RuntimeHost/ASC ActorInfo Ready 后按当前 RuntimeToken 绑定，在 Avatar 替换、UnPossess 和 RuntimeInvalidated 时按 DelegateHandle 解绑；随后可对新 Pawn 幂等重绑。

| 成员 | 类型 | Category | 默认值 | 说明 |
|------|------|----------|--------|------|
| bIsAiming | bool | "Aim\|State" | false | 当前是否处于猎虫瞄准（订阅 `Combat.State.Aiming.Kinsect` Tag 变化） |
| CurrentAimTarget | TWeakObjectPtr\<AActor\> | "Aim\|State" | nullptr | 准心当前指向的 Actor（怪物/场景物体/nullptr） |
| CurrentAimHitzoneTag | FGameplayTag | "Aim\|State" | 空 | 准心指向的怪物部位 Tag（Hitzone.Head / .Torso / 空） |
| CurrentAimExtractColor | FGameplayTag | "Aim\|State" | 空 | Hitzone 配置的萃取颜色（Red/Orange/White/空），仅供准心预览；GA 使用最终触发时的 AimSnapshot |
| AimMaxDistance | float | "Aim\|Config" | 3000 | 瞄准射线最大距离（cm） |
| AimChannel | TEnumAsByte\<ECollisionChannel\> | "Aim\|Config" | Visibility | WorldStatic/Hitzone Block；木桩 Body Ignore；命中后验证组件 ObjectType=Hitzone |

| Delegate | 签名 | 说明 |
|------|------|------|
| OnAimTargetChanged | `(AActor* Target, FGameplayTag HitzoneTag, FGameplayTag ExtractColor)` | 准心指向变化时广播——UI 准心订阅。Target 为 nullptr 表示瞄空/场景 |

### 关键方法

- `FWeaponAimSnapshot CaptureAimSnapshot(EWeaponAimContext Context) const`（目标）
  - 作用：立即执行一次 Visibility 射线，返回 Origin/Direction/TargetPoint/HitResult/Timestamp；InputRouter 在需要 Aim 的最终触发键按下时调用，GA 只读取快照。

- `void BeginPlay() override`
  - 作用：获取 ASC → 订阅 `Combat.State.Aiming.Kinsect`。目标 LT 的 Started/Completed/Canceled 由 PlayerController WeaponInputRouter 维护有所有权 Aim Tag；AimComponent 不直接绑定 EnhancedInput。

- `void TickComponent(float DeltaTime) override`
  - 作用：若 `bIsAiming==true` → 从 PlayerCameraManager 做 Visibility Trace。只有命中组件 ObjectType=Hitzone 且 Cast 成功才读 `ExtractColorTag`；命中 WorldStatic/其他对象广播空目标。仅在目标变化时广播。

- `void OnAimingTagChanged(const FGameplayTag Tag, int32 NewCount)`
  - 作用：`NewCount>0` → `bIsAiming=true`；`NewCount==0` → `bIsAiming=false` → 广播 `OnAimTargetChanged(nullptr, 空, 空)`。
  - **Tag 来源：** 不由 GA 或 AimComponent 添加；由 WeaponInputRouter 按物理键/姿态拥有并清理。受击/击倒时 Router 暂停 Aim Context，恢复后按仍持有的键重算。

- `FGameplayTag ResolveExtractColor(const UMHGZMonsterHitzoneComponent* Hitzone) const`
  - 作用：返回 Hitzone 自身配置的 `ExtractColorTag`。Aim、猎虫和操虫斩必须调用同一解析函数，不能维护三份部位名称映射。

### 瞄准→送虫数据流

```
玩家按下 LT
  → PlayerController WeaponInputRouter 解析当前持刀姿态
    → 以自身所有权添加 Combat.State.Aiming.Kinsect
  → OnAimingTagChanged → bIsAiming = true
  → Tick 启动射线检测

每帧（仅当 bIsAiming==true）：
  → LineTraceSingleByChannel(Visibility)
    → 命中 UMonsterHitzoneComponent（Hitzone.Head）
      → Hitzone.ExtractColorTag = Red
      → 广播 OnAimTargetChanged(Monster, Hitzone.Head, Red)
        → WBP_Crosshair 收到 → 显示红色准心 + 缩放动画（Scale 1.0→1.2→1.0, 0.1s）
    → 命中 WorldStatic（墙壁/地面）
      → 广播 OnAimTargetChanged(nullptr, 空, 空)
        → WBP_Crosshair 收到 → 显示默认灰色准心

玩家按下 Y（送虫）：
  → PlayerController WeaponInputRouter 以 Y 为最终触发键解析 LT+Y
    → 在同一时刻调用 AimComponent::CaptureAimSnapshot(Kinsect)
    → 生成不可变 InputSnapshot（ResolvedInputTag、HeldModifiers、AimSnapshot、SequenceID）
  → ComboCoordinator::HandleWeaponInput（唯一入口）
    → 匹配 `Input.Weapon.IG.SendKinsect` 的转移边并建立 PendingTransition/ActivationContext
  → TryActivateAbility 成功；GA Commit 成功后回执 Confirm
  → GA_SendKinsect 只读取 ActivationContext 中的 AimSnapshot
    → 构造完整 FKinsectFlightRequest → DeployKinsect(Request)

> GA 不读取激活时刻的 `CurrentAimExtractColor`、当前相机朝向或当前摇杆方向；它们可能已与玩家按下最终触发键时不同。UI 的每帧准心状态只用于预览，出招语义以快照为准。

玩家松开 LT：
  → PlayerController WeaponInputRouter 收到 IA_LT Completed/Canceled
    → 按自身保存的 Aim Tag 所有权移除 Combat.State.Aiming.Kinsect
  → OnAimingTagChanged → bIsAiming = false
  → 广播 OnAimTargetChanged(nullptr, 空, 空) → 准心隐藏
```

### 准心 Widget（WBP_Crosshair）

| 瞄准目标 | 准心样式 | 动画 |
|------|------|------|
| 怪物·红灯部位 | 红色准心 + 微光晕 | 缩放（1.0→1.2→1.0, 0.1s） |
| 怪物·橙灯部位 | 橙色准心 | 同上 |
| 怪物·白灯部位 | 白色准心 | 同上 |
| 怪物·无萃取部位 | 灰色准心 + "×"标记 | 无 |
| 场景/空气 | 默认灰色准心（小点） | 无 |

### 三灯 UI 数据绑定

| UI 元素 | 数据源 | 驱动方式 |
|------|------|------|
| 白/橙/红灯图标亮/灭 | ASC Tag `WeaponResource.IG.Extract.White/Orange/Red` | `RegisterGameplayTagEvent` — Tag 添加→亮起动画，Tag 移除→暗灭动画 |
| 三灯合一光环 | ASC Tag `WeaponResource.IG.TripleUp` | 同上 |
| 灯环形倒计时 | `URes_InsectGlaive::OnExtractTimeUpdated(Color, 0~1 Ratio)` Delegate | Delegate 已声明但当前未广播；剩余时间读取与 UI 更新为规划 |
| 猎虫状态图标 | ASC Tag `WeaponResource.IG.Kinsect.Active` | Tag 添加→"虫已放出"图标，Tag 移除→"虫已归"图标 |

### 猎虫耐力条数据绑定

| UI 元素 | 数据源 | 驱动方式 |
|------|------|------|
| 进度条填充 | `URes_InsectGlaive::OnKinsectStaminaChanged(Current, Max)` Delegate | ResourceComponent Tick 中耐力变化时广播——UI 更新百分比 |
| 颜色变化 | 同上（Current/Max 比值） | 蓝图中绑定：>0.6 绿 / 0.3~0.6 黄 / <0.3 红+闪烁 |
| 归零提示 | `OnKinsectStaminaChanged` 中 Current==0 | 播放闪烁+文字提示动画 |

### Widget 生命周期（M6/E6 目标；当前 M4-A 不执行）

```
M6/E6 创建并填入 `WBP_IG_ResourcePanel` 后：

装备虫棍 → EquipmentComponent::OnEquippedWeaponChanged(Snapshot) 广播
  → Character RuntimeHost 完成 DA_WeaponRuntime_IG + URes_InsectGlaive 初始化
  → RuntimeHost 广播 Ready(RuntimeDefinition, Resource, RuntimeToken)
  → 本地 HUD（已绑定当前 Possessed Pawn 的 RuntimeHost）验证 RuntimeToken.Host == RuntimeSource
  → 直接读取 RuntimeDefinition.ResourceWidgetClass=WBP_IG_ResourcePanel
  → CreateWidget → Bind → AddChild 到 WBP_HUD 的武器资源插槽（资源面板不 AddToViewport）
  → 绑定所有 Delegate（StaminaChanged / ExtractTimeUpdated）
  → 订阅 ASC Tag 事件（Extract.* / TripleUp / Kinsect.Active）

卸下/死亡/换 Pawn → RuntimeHost 先广播 Invalidated(RuntimeToken)
  → Token 精确匹配当前 Widget 才执行 Unbind → RemoveFromParent
  → 旧 Pawn 的迟到回调因 Host 不匹配而忽略
```

### UI 状态传递

准心目标和颜色通过 `OnAimTargetChanged` Delegate 直接传给 Widget，不写入 ASC。`UI.Aim.*` 不属于战斗权威状态；只有 `Combat.State.Aiming.Kinsect` 进入 ASC，供输入和 Ability 条件查询。

### 验证清单（新增）

| # | 测试项 | 预期结果 |
|:--:|------|------|
| UI-1 | LT 瞄准怪物头部→准心变红 | OnAimTargetChanged(Monster, Head, Red) → WBP_Crosshair 显示红色+缩放 |
| UI-2 | LT 瞄准怪物躯干→准心变橙 | OnAimTargetChanged(Monster, Torso, Orange) |
| UI-3 | LT 瞄准墙壁→准心恢复默认 | OnAimTargetChanged(nullptr, 空, 空) → 灰色小点 |
| UI-4 | 送虫→萃取→召回→三灯图标亮起 | ASC Tag 变化 → RegisterGameplayTagEvent → UI 更新 |
| UI-5 | 猎虫耐力下降→耐力条实时更新 | OnKinsectStaminaChanged Delegate → ProgressBar 填充 |
| UI-6 | 瞄准中移动准心扫过多个部位→准心颜色实时切换 | AimComponent Tick 检测目标变化→仅变化时广播 |
| UI-7 | 装备虫棍→资源 Widget 创建→卸下→资源 Widget 销毁 | HUD 独占生命周期；面板只存在于 WBP_HUD 资源插槽，无第二个 Viewport 实例 |
| UI-8 | 三灯到期→三灯图标同时暗灭 | TripleUp Tag 移除→UI 播放 FadeOut |
| UI-9 | 死亡重建 Character 后旧 RuntimeHost 迟到广播 | Host+Generation Token 不匹配，不能删除或重绑新 Character 的资源 UI |
