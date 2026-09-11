# 怪物与靶子系统

> **实施状态说明（以源码与 Content 为准）：** E5.1 已将 `DA_TrainingDummy` / `BP_TrainingDummy` 接为直径 1m、高 2.25m 的三段训练柱：下白、中橙、上红，每段 0.75m；三枚 Hitzone 分别为 White/Orange/Red，中心在 root Z=-75/0/75cm、半径 37.5cm，彼此相切而不重叠。每段有对应 Point Light。白橙交界（Z=-37.5cm）另有一个直径 2m 的周期火圈，默认每 5 秒喷发、持续 1 秒、每 0.25 秒判定；所有频率、持续时间、半径、环带厚度、伤害、是否喷发、是否可反击和硬直 Tag 均在 `FDummyFireRingConfig` 以 Blueprint 可编辑字段公开。周期预警/正式怪物 AI、死亡和部位破坏仍未实现。

**设计原则：** 当前木桩负责可重复验证伤害、红/白/橙提取、贯通、多部位轨迹和突进回旋斩反击。反击测试攻击是训练设施，不是怪物 AI。

> **当前范围：** 木桩无 AI、不移动；火圈是可重复的训练环境攻击，每个判定创建独立 `AttackInstanceID` 并提交给玩家 `IncomingHitResolver`。它默认不可反击；为专门测试可在蓝图把 `bCounterable` 打开。无死亡、部位破坏和正式怪物行为。

---

## 类层级

```
AMHGZMonsterBase (Character)
├── 持有 UAbilitySystemComponent          ← 引擎基础 ASC
├── 持有 UMHGZAttributeSet                ← 当前含 Health 等通用属性
├── GenerateHitzonesFromConfig()          ← 按配置动态创建球形部位组件
│
├── AMHGZTrainingDummy                    ← 当前实现：木桩
│   ├── 持有 UMHGZDummyConfig
│   └── 无 AI、无移动、默认 1000 生命、配置驱动
│
└── AMHGZMonster_Xxx                      ← 未来：完整怪物
    └── 添加 BehaviorTree / 攻击 GA / 部位破坏 / 愤怒状态机 / 虚函数钩子
```

---

## ASC 策略——最小 ASC

木桩当前挂载引擎基础 `UAbilitySystemComponent` 和 `UMHGZAttributeSet`。`BeginPlay` 把 `Health/MaxHealth` 初始化为 `DummyMaxHealth`（默认 1000），订阅 Health 变化并广播 `OnHealthChanged`；伤害 GE 会实际扣血，但没有血条 UI、死亡逻辑或 GameplayCue 反馈。

| 决策 | 理由 |
|------|------|
| 挂 ASC 而非纯 Actor | 攻击链路（`MakeDamageSpec` → `ApplyGameplayEffectToSelf`）零改动 |
| 注册 AttributeSet | 当前需要验证真实扣血，因此木桩直接复用通用 AttributeSet |
| 命中反馈用 GameplayCue | 目标方案；当前只注册 Tag，没有 GC 资产与路由实现 |

> **后续扩展：** 创建血条 Widget，并在 `Health≤0` 时发送 `Combat.Event.Death`；死亡 Ability/事件当前尚未实现。

---

## 木桩配置（UMHGZDummyConfig）

`UPrimaryDataAsset`，策划可在编辑器中创建多个配置资产，同一木桩运行时切换形态。

| 字段 | 类型 | 说明 |
|------|------|------|
| DisplayMesh | TSoftObjectPtr\<USkeletalMesh\> | 主形体（人形靶） |
| LoopingMontage | TSoftObjectPtr\<UAnimMontage\> | 循环动画（呼吸/挑衅，留空则静止） |
| Hitzones | TArray\<FDummyHitzoneConfig\> | 球形部位碰撞体配置 |
| CounterTestAttack | FDummyCounterAttackConfig | 手动确定性反击测试入口；默认关闭 |
| FireRing | FDummyFireRingConfig | 白橙交界的周期环形火焰；默认开启 |

> `FallbackMesh`、`MeshScale`、`MaterialOverrides`、`PlayRate` 和多碰撞形状仍作为后续配置扩展方案保留，当前 `UMHGZDummyConfig` 没有这些字段。

规划扩展字段：

| 字段 | 类型 | 目标用途 |
|------|------|----------|
| FallbackMesh | TSoftObjectPtr\<UStaticMesh\> | 备选木桩/石柱/桶形体 |
| MeshScale | FVector | 配置整体缩放，默认 `(1,1,1)` |
| MaterialOverrides | TMap\<FName, TSoftObjectPtr\<UMaterialInstance\>\> | 按 SlotName 覆写材质 |
| PlayRate | float | 循环 Montage 播放速率，默认 1.0 |
| Shape | EMonsterCollisionShape | Hitzone 的 Sphere/Capsule/Box 形状选择 |

### FDummyHitzoneConfig 结构体

```cpp
USTRUCT(BlueprintType)
struct FDummyHitzoneConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere) FName BoneName;                    // 挂载骨骼
    UPROPERTY(EditAnywhere) FGameplayTag HitzoneTag;           // Hitzone.Head / Hitzone.Torso ...
    UPROPERTY(EditAnywhere) FGameplayTag ExtractColorTag;      // Extract.Red / Orange / White
    UPROPERTY(EditAnywhere) float DefenseMultiplier = 1.0f;    // 肉质（0.2=坚硬 / 1.0=弱点）
    UPROPERTY(EditAnywhere) float StaggerRate = 0.0f;          // 破坏值吸收率
    UPROPERTY(EditAnywhere) FVector RelativeLocation;           // 相对挂点位置
    UPROPERTY(EditAnywhere) float Radius = 30.0f;               // 当前统一使用球形 Hitzone
};
```

> Sphere/Capsule/Box 的 `EMonsterCollisionShape` 分支是保留方案；当前统一创建 `USphereComponent`。

---

## 形状变换（Mesh/Montage/Hitzone 已实现；其余字段为规划）

木桩运行时调用 `ApplyConfig(NewConfig)` 即可切换形态：

```
AMHGZTrainingDummy::ApplyConfig(UMHGZDummyConfig* Config)
  1. 切换网格体：MeshComponent->SetSkeletalMesh(Config->DisplayMesh)
  2. 销毁旧 HitzoneComponent → 按 Config->Hitzones 重新生成球体
  3. PlayMontage(Config->LoopingMontage)
  4. [规划] FallbackMesh / MaterialOverrides / MeshScale / PlayRate
```

切换时机：BeginPlay 加载默认配置，或通过 GameMode / 蓝图事件触发切换（如训练场中更换靶子类型）。

---

## 部位碰撞（UMonsterHitzoneComponent）

继承 `USphereComponent`，额外持有 Hitzone 元数据。挂载到骨骼上，Object Type 固定为 `Hitzone`、Collision Enabled 为 QueryOnly、Weapon/Visibility Trace 响应为 Block、Pawn/WorldStatic 为 Ignore。木桩实体 Body 对 Visibility Ignore，WorldStatic 仍 Block，所以准心不会被 Body 抢占，也不会穿墙。

| 成员 | 类型 | 说明 |
|------|------|------|
| HitzoneTag | FGameplayTag | 部位标签（Hitzone.Head / Hitzone.Torso ...） |
| ExtractColorTag | FGameplayTag | 该木桩部位提供的精华颜色；Demo 必须覆盖 Red/Orange/White |
| DefenseMultiplier | float | 肉质（伤害吸收率） |
| StaggerRate | float | 破坏值吸收率 |

武器攻击通过 Weapon Trace 查询 Hitzone；Aim 使用 Visibility 并验证命中组件 ObjectType=Hitzone；猎虫以前后帧 Capsule Sweep 显式查询 Hitzone Object。猎虫自身 Collision Root 只处理 WorldStatic 阻挡，不订阅 Hitzone Overlap。木桩的实体阻挡组件与 Hitzone 分离，Hitzone 不承担 Pawn 物理阻挡。

## 突进回旋斩反击测试器（Demo 规划）

`FDummyCounterAttackConfig` 保留为可手动调用的确定性入口。E5.1 的实际可视入站攻击为 `FDummyFireRingConfig`，字段如下：

| 字段 | 说明 |
|---|---|
| bEnabled | 是否喷发；Blueprint 可在运行时切换，不修改共享 DataAsset |
| InitialDelay / Interval | PIE 后首次攻击延迟和循环间隔 |
| ActiveDuration | 火圈可见与有效的持续时间 |
| HitInterval | 有效期内的判定频率 |
| RelativeLocation / Radius / RingHalfThickness / VerticalHalfHeight | 火圈中心、半径、环带厚度和高度；默认中心为 White/Orange 交界、半径 100cm |
| VisualSegmentCount | 粒子火圈下方的水平发光引导环分段数；火焰本体由 `PS_TD_FireRing` 发射 |
| Damage / StaggerTag | 未被反击时对玩家结算的 Demo 伤害与硬直 |
| bCounterable | 是否可被突进回旋斩反击；火圈默认 false，可为专项验证打开 |

每个火圈判定生成唯一 `AttackInstanceID`。同一判定不会重复提交；不同 `HitInterval` 是独立伤害跳。Resolver 是每个入站攻击实例的最终去重与反击消费权威。处理顺序固定为：

1. Resolver 先检查该 ID 是否已处理；重复提交直接返回 Duplicate。
2. 按 Priority 调用带完整 ActionToken 所有权的反击 Token，并检查 Context 的 `bCounterable`。
3. 成功反击时将该 ID 标记 Consumed，不结算伤害/硬直，通知回旋斩触发确定的舞踏自动转移。
4. 未消费时由 Resolver 标记 Applied，再统一 Apply 玩家伤害和受击事件。
5. 已处理 ID 缓存按配置 TTL/容量回收；同一 ID 的迟到提交不得再次结算。

测试器不得调用怪物决策、追踪玩家或改变朝向；它只提供固定时序、可复现的命中载荷。

> **与玩家 Hitzone 的区别：** 怪物 Hitzone 是独立的碰撞组件挂在骨骼上；玩家没有独立 Hitzone（受击由攻击方的碰撞检测直击 CapsuleComponent）。两者共用同一套 `Hitzone.*` GameplayTag 和肉质计算逻辑。

---

## 命中流程（玩家 → 木桩）

沿用现有攻击链路，基本无改动：

```
玩家 AttackAbility
  → AnimNotifyState_AttackCollision: EnableCollision(Weapon通道 Sweep)
  → 命中木桩 HitzoneComponent（Weapon=Block）
  → 读 HitzoneTag + DefenseMultiplier
  → MakeDamageSpec(Target, HitzoneBoneName, SegmentIndex)
  → Source ASC::ApplyGameplayEffectSpecToTarget(UMHGZDamageGameplayEffect)
  → ExecCalc 计算最终伤害（AttackPower × MotionValue × 肉质）
  → 木桩 Health 实际减少并广播 OnHealthChanged
  → [规划] HandleGameplayEvent(HitStagger) + GameplayCue + 伤害数字
```

### 命中反馈三层（规划；当前只有伤害和可选 CameraShake/HitStop）

| 层 | 机制 | 内容 |
|----|------|------|
| 帧级同步 | GameplayCue（Burst） | 命中火花、斩击/打击音效——GE Spec 携带 `HitCueTag`+`ElementalCueTag`，ASC Apply 时路由 GC |
| 数值显示 | GameplayCue → DamageNumberPool | 伤害数字 Widget 浮空文字——`GameplayCue.Hit.DamageNumber` 触发，WorldSubsystem 对象池管理，1.5s 淡出回收 |
| 可选 | 受击 Montage | 木桩轻微抖动（`GA_HitReaction`），当前版本可略 |

---

## 伤害数字（规划，当前未实现）

伤害数字统一走 **GameplayCue** 系统（详见 [gameplay-cue.md](gameplay-cue.md)）：
- 攻击方 `MakeDamageSpec` 始终向 GE Spec 注入 `GameplayCue.Hit.DamageNumber` Tag
- `UMHGZDamageNumberPool`（WorldSubsystem）管理 Widget 对象池（预分配 30 个，1.5s 上浮淡出回收）
- `GC_Hit_DamageNumber`（Burst 蓝图）读取 `Parameters.RawMagnitude` 显示数值，暴击时放大 1.5x + 黄色

---

## 目录结构

```
Source/MHGZ/Monster/
├── MHGZMonsterBase.h/cpp               ← 怪物基类（基础 ASC + AttributeSet + Hitzone 生成）
├── MHGZTrainingDummy.h/cpp             ← 木桩子类
├── MHGZDummyConfig.h                   ← DataAsset 配置
├── MHGZMonsterHitzoneComponent.h/cpp   ← 部位碰撞体

Content/Blueprints/Monster/TrainingDummy/
└── BP_TrainingDummy.uasset

Content/Monster/TrainingDummy/
├── Anims/                              ← 木桩 AnimSequence、Montage、AnimBP
├── Data/
│   ├── DA_TrainingDummy.uasset         ← 当前 Demo 配置
│   ├── DA_DummyPillar.uasset           ← 后续配置：木桩/石柱
│   └── DA_DummyBarrel.uasset           ← 后续配置：桶/壶
├── Materials/
├── Meshes/
└── Textures/

Content/GameplayCues/Hit/               ← 以下均为规划，当前目录为空
├── GC_Hit_Slash.uasset                 ← 斩击命中火花
├── GC_Hit_Blunt.uasset                 ← 打击命中火花
└── GC_Hit_DamageNumber.uasset          ← 伤害数字
```

---

## 扩展路径（木桩 → 完整怪物）

| 阶段 | 添加内容 | 改动范围 |
|------|----------|----------|
| **当前** | 木桩：ASC + AttributeSet Health + 球形 Hitzone + Config + 生命变化委托 | 已实现；无伤害数字/UI/死亡 |
| **+1** | 扩充怪物属性：StaggerThreshold/PartBreakHP + 血条/死亡 | MonsterBase/AttributeSet/UI |
| **+2** | 怪物 AI：`AMHGZMonsterAIController` + BehaviorTree（巡逻/索敌/追击） | 新增 AI 模块，不影响木桩 |
| **+3** | 怪物攻击：`UMHGZMonsterAttackAbility`，AI 通过 `TryActivateAbility` 触发 | 新增 GA，MonsterAttack 通道 |
| **+4** | 部位破坏 / 硬直 / 愤怒：GAS Tag+GE 驱动状态机 | MonsterBase 虚函数钩子 |
| **+5** | 生成管理：`UMHGZMonsterSpawner`（GameModeSubsystem） | 新增子系统 |

> **关键约束：** 木桩始终是 `AMHGZMonsterBase` 的子类。任何添加到 MonsterBase 的功能（如 AttributeSet）不应破坏木桩行为——木桩通过覆写虚函数或配置开关保持静止/无 AI。
