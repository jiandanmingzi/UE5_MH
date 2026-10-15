# 怪物与靶子系统

> **当前覆盖修复（2026-10-12；静态验证/PIE状态见编辑器指南§3.16）：** 训练柱实际为直径1.5m、高3.375m，下白、中橙、上红，各高1.125m。配置中心仍以基础尺寸Z=-75/0/75cm保存，运行时统一×1.5。三段受击区改为盒形查询，基础半尺寸51×51×38.5cm，运行时76.5×76.5×57.75cm，覆盖整个柱面/实体Body，并在接缝保留小幅外扩。旧37.5cm球形查询及“不重叠”验收已被[总规划§1.6](demo-implementation-plan.md#16-全项目命中覆盖硬约束2026-10-12)取代。三色精华、肉质与现有火圈玩法不变。

**设计原则：** 当前木桩负责可重复验证伤害、红/白/橙提取、贯通、多部位轨迹和突进回旋斩反击。反击测试攻击是训练设施，不是怪物 AI。

> **当前范围：** 木桩无 AI、不移动；火圈是可重复的训练环境攻击，每个判定创建独立 `AttackInstanceID` 并提交给玩家 `IncomingHitResolver`。它默认不可反击；为专门测试可在蓝图把 `bCounterable` 打开。无死亡、部位破坏和正式怪物行为。

---

## 类层级

```
AMHGZMonsterBase (Character)
├── 持有 UAbilitySystemComponent          ← 引擎基础 ASC
├── 持有 UMHGZAttributeSet                ← 当前含 Health 等通用属性
├── GenerateHitzonesFromConfig()          ← 按配置创建部位身份与Sphere/Box查询
│
├── AMHGZTrainingDummy                    ← 当前实现：木桩
│   ├── 持有 UMHGZDummyConfig
│   └── 无 AI、无移动、默认无限训练生命、配置驱动
│
└── AMHGZMonster_Xxx                      ← 未来：完整怪物
    └── 添加 BehaviorTree / 攻击 GA / 部位破坏 / 愤怒状态机 / 虚函数钩子
```

---

## ASC 策略——最小 ASC

木桩挂载引擎基础 `UAbilitySystemComponent` 和 `UMHGZAttributeSet` 以复用伤害与反馈。`DummyMaxHealth`（默认1000）是内部载体值，不代表有限木桩生命；`bInfiniteHealth=true`时伤害仍正常结算/反馈，但生命恢复满值，不会因测试攻击失去命中资格。当前无正式怪物死亡玩法。

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
| Hitzones | TArray\<FDummyHitzoneConfig\> | 球形/盒形查询配置及部位身份 |
| CounterTestAttack | FDummyCounterAttackConfig | 手动确定性反击测试入口；默认关闭 |
| FireRing | FDummyFireRingConfig | 白橙交界的周期环形火焰；默认开启 |

> `FallbackMesh`、`MeshScale`、`MaterialOverrides`、`PlayRate`仍为后续配置；当前已支持Sphere/Box查询形状，Capsule仍未实现。

规划扩展字段：

| 字段 | 类型 | 目标用途 |
|------|------|----------|
| FallbackMesh | TSoftObjectPtr\<UStaticMesh\> | 备选木桩/石柱/桶形体 |
| MeshScale | FVector | 配置整体缩放，默认 `(1,1,1)` |
| MaterialOverrides | TMap\<FName, TSoftObjectPtr\<UMaterialInstance\>\> | 按 SlotName 覆写材质 |
| PlayRate | float | 循环 Montage 播放速率，默认 1.0 |

### FDummyHitzoneConfig 结构体

```cpp
USTRUCT(BlueprintType)
struct FDummyHitzoneConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere) FName BoneName;                    // 挂载骨骼
    UPROPERTY(EditAnywhere) FGameplayTag HitzoneTag;           // Hitzone.Head / Hitzone.Torso ...
    UPROPERTY(EditAnywhere) FGameplayTag ExtractColorTag;      // Extract.Red / Orange / White
    UPROPERTY(EditAnywhere) float DefenseMultiplier = 100.0f;   // 物理肉质百分数，范围0-100
    UPROPERTY(EditAnywhere) float StaggerRate = 0.0f;          // 破坏值吸收率
    UPROPERTY(EditAnywhere) FVector RelativeLocation;           // 相对挂点位置
    UPROPERTY(EditAnywhere) float Radius = 30.0f;               // Sphere形状的半径
    UPROPERTY(EditAnywhere) EMHGZHitzoneShape Shape;            // Sphere / Box
    UPROPERTY(EditAnywhere) FVector BoxHalfExtent;              // 盒形半尺寸
};
```

> 部位元数据和挂载继续保存在原Hitzone组件。Box启用独立子查询组件，同时禁用原球的碰撞；攻击、猎虫、准心、虫印和粉尘经统一`Resolve`读取元数据，原始命中结果不改写。

---

## 形状变换（Mesh/Montage/Hitzone 已实现；其余字段为规划）

木桩运行时调用 `ApplyConfig(NewConfig)` 即可切换形态：

```
AMHGZTrainingDummy::ApplyConfig(UMHGZDummyConfig* Config)
  1. 切换网格体：MeshComponent->SetSkeletalMesh(Config->DisplayMesh)
  2. 销毁旧Hitzone及其查询子组件 → 按Config->Hitzones重建Sphere/Box
  3. PlayMontage(Config->LoopingMontage)
  4. [规划] FallbackMesh / MaterialOverrides / MeshScale / PlayRate
```

切换时机：BeginPlay 加载默认配置，或通过 GameMode / 蓝图事件触发切换（如训练场中更换靶子类型）。

---

## 部位碰撞（UMonsterHitzoneComponent）

元数据组件继续继承`USphereComponent`并挂载到骨骼。Sphere时自身QueryOnly；Box时自身NoCollision，唯一Box子查询为QueryOnly。有效查询组件的Object Type为`Hitzone`，Weapon/Visibility=Block、Pawn/WorldStatic=Ignore。所有消费者经`Resolve`读取统一部位元数据；最近点/探针外包半径使用当前查询形状。Body仍单独承担实体阻挡，不能被当作攻击命中。

| 成员 | 类型 | 说明 |
|------|------|------|
| HitzoneTag | FGameplayTag | 部位标签（Hitzone.Head / Hitzone.Torso ...） |
| ExtractColorTag | FGameplayTag | 该木桩部位提供的精华颜色；Demo 必须覆盖 Red/Orange/White |
| DefenseMultiplier | float | 物理肉质百分数0-100；物理类型表缺项时兼容回退 |
| PhysicalHitzoneMultipliers | TMap\<EMHGZPhysicalDamageType, float\> | 斩/打/弹物理肉质百分数，45=肉质45；显式0免疫 |
| ElementHitzoneMultipliers | TMap\<EMHGZElementType, float\> | 火/水/雷/冰/龙属性肉质仍用归一化倍率，0.20=肉质20；缺项0，不回退物理 |
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
  → ExecCalc计算物理/属性独立通道、分别取整后合并（详见[伤害计算](exec-calc.md)）；猎虫/粉尘保持专用规则
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
