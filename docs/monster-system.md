# 怪物与靶子系统

**设计原则：** 从木桩到怪物渐进式构建——当前版本只需木桩（能挨打、能显示命中反馈、形状/动画可配置），但整体骨架按完整怪物系统设计。木桩即怪物的最简子集。

> **当前范围：** 木桩（Training Dummy）——无 AI、不移动、不攻击、无血条。后续逐步扩展为完整怪物。

---

## 类层级

```
AMHGZMonsterBase (Character)
├── 持有 UMHGZAbilitySystemComponent     ← 最小 ASC（仅接收 GE）
├── 持有 UMonsterHitzoneCollection        ← 部位碰撞体集合
├── 持有 UMHGZDummyConfig (DataAsset)     ← 形状/动画/碰撞 可配置
├── 虚函数钩子：OnDamageReceived / OnDeath / OnStaggered
│
├── AMHGZTrainingDummy                    ← 当前实现：木桩
│   └── 无 AI、无移动、循环动画、配置驱动
│
└── AMHGZMonster_Xxx                      ← 未来：完整怪物
    └── 添加 BehaviorTree / 攻击 GA / 部位破坏 / 愤怒状态机
```

---

## ASC 策略——最小 ASC

木桩挂载 `UMHGZAbilitySystemComponent`，但**不注册 AttributeSet、不设 Health、不显示血条**。ASC 仅作为 GE 接收器存在——玩家攻击链路完全不动，命中后照常 Apply GE，ASC 接收但不产生属性变化。命中反馈（火花/音效/伤害数字）通过 GameplayCue 触发。

| 决策 | 理由 |
|------|------|
| 挂 ASC 而非纯 Actor | 攻击链路（`MakeDamageSpec` → `ApplyGameplayEffectToSelf`）零改动 |
| 不注册 AttributeSet | 木桩不需要血量，后续需要时再挂。**GE Apply 不会失败**——GameplayCue 标签始终触发（火花/音效/伤害数字正常）；ExecCalc 被调用但无属性可修改（空循环无害）；Attribute Modifiers 产生 Warning 日志并跳过（GAS 内置行为，不影响流程）。伤害数字值来自 GE Spec 的 `SetByCallerMagnitude`，不依赖 Attribute。后续挂载 AttributeSet 后 Modifiers 自动生效 |
| 命中反馈用 GameplayCue | 不与属性系统耦合，火花/音效/伤害数字独立触发 |

> **后续扩展：** 需要血条时，挂载 `UMHGZAttributeSet` → 注册 `Health` 属性 → 伤害 GE 开始扣血 → `Health≤0` 触发 `Combat.Event.Death`。

---

## 木桩配置（UMHGZDummyConfig）

`UPrimaryDataAsset`，策划可在编辑器中创建多个配置资产，同一木桩运行时切换形态。

| 字段 | 类型 | 说明 |
|------|------|------|
| DisplayMesh | TSoftObjectPtr\<USkeletalMesh\> | 主形体（人形靶） |
| FallbackMesh | TSoftObjectPtr\<UStaticMesh\> | 备选形体（木桩/石柱/桶） |
| MeshScale | FVector | 整体缩放（默认 1,1,1） |
| MaterialOverrides | TMap\<FName, TSoftObjectPtr\<UMaterialInstance\>\> | 按 SlotName 覆写材质 |
| LoopingMontage | TSoftObjectPtr\<UAnimMontage\> | 循环动画（呼吸/挑衅，留空则静止） |
| PlayRate | float | 动画播放速率（默认 1.0） |
| Hitzones | TArray\<FHitzoneSetup\> | 部位碰撞体配置 |

### FHitzoneSetup 结构体

```cpp
USTRUCT(BlueprintType)
struct FHitzoneSetup
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere) FName BoneName;                    // 挂载骨骼
    UPROPERTY(EditAnywhere) FGameplayTag HitzoneTag;           // Hitzone.Head / Hitzone.Torso ...
    UPROPERTY(EditAnywhere) EMonsterCollisionShape Shape;       // Sphere / Capsule / Box
    UPROPERTY(EditAnywhere) FVector HalfExtent;                // 半尺寸
    UPROPERTY(EditAnywhere) float DefenseMultiplier = 1.0f;    // 肉质（0.2=坚硬 / 1.0=弱点）
    UPROPERTY(EditAnywhere) float StaggerRate = 1.0f;          // 破坏值吸收率
};
```

---

## 形状变换

木桩运行时调用 `ApplyConfig(NewConfig)` 即可切换形态：

```
AMHGZTrainingDummy::ApplyConfig(UDummyConfig* Config)
  1. 切换网格体：MeshComponent->SetSkeletalMesh(Config->DisplayMesh)
     或 SetStaticMesh(Config->FallbackMesh)
  2. 遍历 MaterialOverrides → SetMaterial(slot, mat)
  3. 设置缩放：SetWorldScale3D(Config->MeshScale)
  4. 销毁旧 HitzoneComponent → 按 Config->Hitzones 重新生成碰撞体
  5. 切换动画：StopAllMontages → PlayMontage(Config->LoopingMontage, LoopCount=-1)
```

切换时机：BeginPlay 加载默认配置，或通过 GameMode / 蓝图事件触发切换（如训练场中更换靶子类型）。

---

## 部位碰撞（UMonsterHitzoneComponent）

继承 `UCapsuleComponent`，额外持有 Hitzone 元数据。挂载到骨骼上，碰撞通道设 Weapon=Block。

| 成员 | 类型 | 说明 |
|------|------|------|
| HitzoneTag | FGameplayTag | 部位标签（Hitzone.Head / Hitzone.Torso ...） |
| DefenseMultiplier | float | 肉质（伤害吸收率） |
| StaggerRate | float | 破坏值吸收率 |

碰撞预设：`Custom`，Weapon 通道 = Block，其余 Ignore。Pawn 通道不参与（木桩不作为 Pawn 推挤源）。

> **与玩家 Hitzone 的区别：** 怪物 Hitzone 是独立的碰撞组件挂在骨骼上；玩家没有独立 Hitzone（受击由攻击方的碰撞检测直击 CapsuleComponent）。两者共用同一套 `Hitzone.*` GameplayTag 和肉质计算逻辑。

---

## 命中流程（玩家 → 木桩）

沿用现有攻击链路，基本无改动：

```
玩家 AttackAbility
  → AnimNotifyState_AttackCollision: EnableCollision(Weapon通道 Sweep)
  → 命中木桩 HitzoneComponent（Weapon=Block）
  → 读 HitzoneTag + DefenseMultiplier
  → MakeDamageSpec(AttackPower, MotionValue, HitStaggerTag, HitzoneTag)
  → 木桩 ASC::ApplyGameplayEffectToSelf(GE_Damage)
  → ExecCalc 计算最终伤害（AttackPower × MotionValue × 肉质）
  → HandleGameplayEvent(Combat.Event.HitStagger)
  → GameplayCue 触发：命中火花 + 伤害数字
```

### 命中反馈三层

| 层 | 机制 | 内容 |
|----|------|------|
| 帧级同步 | GameplayCue（Burst） | 命中火花、斩击/打击音效——GE Spec 携带 `HitCueTag`+`ElementalCueTag`，ASC Apply 时路由 GC |
| 数值显示 | GameplayCue → DamageNumberPool | 伤害数字 Widget 浮空文字——`GameplayCue.Hit.DamageNumber` 触发，WorldSubsystem 对象池管理，1.5s 淡出回收 |
| 可选 | 受击 Montage | 木桩轻微抖动（`GA_HitReaction`），当前版本可略 |

---

## 伤害数字

伤害数字统一走 **GameplayCue** 系统（详见 [gameplay-cue.md](gameplay-cue.md)）：
- 攻击方 `MakeDamageSpec` 始终向 GE Spec 注入 `GameplayCue.Hit.DamageNumber` Tag
- `UMHGZDamageNumberPool`（WorldSubsystem）管理 Widget 对象池（预分配 30 个，1.5s 上浮淡出回收）
- `GC_Hit_DamageNumber`（Burst 蓝图）读取 `Parameters.RawMagnitude` 显示数值，暴击时放大 1.5x + 黄色

---

## 目录结构

```
Source/MHGZ/Monster/
├── MHGZMonsterBase.h/cpp               ← 怪物基类（ASC + Hitzone集合 + Config）
├── MHGZTrainingDummy.h/cpp             ← 木桩子类
├── MHGZDummyConfig.h/cpp               ← DataAsset 配置
├── MHGZMonsterHitzoneComponent.h/cpp   ← 部位碰撞体

Content/Blueprints/Monster/
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

Content/GameplayCues/Hit/
├── GC_Hit_Slash.uasset                 ← 斩击命中火花
├── GC_Hit_Blunt.uasset                 ← 打击命中火花
└── GC_Hit_DamageNumber.uasset          ← 伤害数字
```

---

## 扩展路径（木桩 → 完整怪物）

| 阶段 | 添加内容 | 改动范围 |
|------|----------|----------|
| **当前** | 木桩：ASC + Hitzone + Config + 伤害数字 | 全新文件，不碰现有代码 |
| **+1** | 怪物 AttributeSet：挂载 Health/StaggerThreshold/PartBreakHP | MonsterBase 添加 ASC 注册 |
| **+2** | 怪物 AI：`AMHGZMonsterAIController` + BehaviorTree（巡逻/索敌/追击） | 新增 AI 模块，不影响木桩 |
| **+3** | 怪物攻击：`UMHGZMonsterAttackAbility`，AI 通过 `TryActivateAbility` 触发 | 新增 GA，MonsterAttack 通道 |
| **+4** | 部位破坏 / 硬直 / 愤怒：GAS Tag+GE 驱动状态机 | MonsterBase 虚函数钩子 |
| **+5** | 生成管理：`UMHGZMonsterSpawner`（GameModeSubsystem） | 新增子系统 |

> **关键约束：** 木桩始终是 `AMHGZMonsterBase` 的子类。任何添加到 MonsterBase 的功能（如 AttributeSet）不应破坏木桩行为——木桩通过覆写虚函数或配置开关保持静止/无 AI。
