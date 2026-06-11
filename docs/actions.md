# 动作系统

**设计原则：** GAS + EnhancedInput 驱动，通过 GameplayTag 桥接输入与 Ability。核心能力（移动/闪避）始终可用，武器能力（连招/资源技能）由装备系统动态授予/移除。**无独立跳跃键——边缘跳越（Edge Vault）替代。**

## 移动实现

- 移动物理：`UCharacterMovementComponent`，**不用 GAS 实现**
- 移动输入：`AddMovementInput`（非 GAS 路径）
- 移动动画：AnimBP BlendSpace1D 基于 Speed 驱动
- 奔跑（GA_Sprint）：按下 LS→GE 提升 MoveSpeedMultiplier+持续扣耐；持刀时 Unsheathed Tag 阻塞
- GAS 只管两件事：**能不能动**（Tag 阻塞）、**有多快**（GE 修改 MoveSpeedMultiplier）

### RootMotion——攻击/翻滚中如何覆盖 CMC 移动

攻击 Montage 播放时，动画中的**根骨骼位移数据（RootMotion）**直接驱动角色位移/旋转，**覆盖** `AddMovementInput` 的移动输入。摇杆方向被 **MotionWarping** 读取用于旋转修正（见 `MaxCorrectionAngle`）。

| 场景 | RootMotion 作用 | bEnableRootMotion |
|------|-----------------|:--:|
| 攻击 Montage | 锁定角色按动画轨迹移动，摇杆仅控制方向修正 | ✅ true |
| 翻滚 Montage | 前跃/侧移距离由动画精确控制，不受 CMC 加速度/摩擦影响 | ✅ true |
| 见切后撤 | 段0 后撤位移完全动画驱动（配合 MotionWarping 修正方向） | ✅ true |
| 登龙下劈 | 空中轨迹动画控制——不是物理跳跃+下落 | ✅ true |
| 受击硬直 Montage | 击退距离由 Impulse + Montage RootMotion 共同决定 | ✅ true |
| 待机/收刀行走 | 摇杆+CMC 正常移动 | ❌ false |

## 边缘跳越（Vault）

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
  - 作用：BeginPlay 时调用。授予 CoreAbilities → Apply CoreAttributeEffects → 遍历 `InputBindings` 用 **lambda 捕获 `FGameplayTag`** 绑定 EnhancedInput 的 `Triggered` 和 `Completed` 事件。`OnInputActionTriggered` 按 Tag 分叉——武器 Tag → `Coordinator→HandleWeaponInput(Tag)`；非武器 Tag → `TryActivateAbilityByTag(Tag)`。`OnInputActionCompleted` 检查 `Combat.State.Charging` Tag → 若存在则发送 `Combat.Event.ChargeReleased` GameplayEvent（蓄力 GA 通过 AbilityTrigger 监听）。
    ```cpp
    for (auto& Binding : InputBindings)
    {
        FGameplayTag Tag = Binding.AbilityTag;
        EnhancedInput->BindAction(Binding.InputAction, ETriggerEvent::Triggered,
            [this, Tag](const FInputActionValue&) { OnInputActionTriggered(Tag); });
        EnhancedInput->BindAction(Binding.InputAction, ETriggerEvent::Completed,
            [this, Tag](const FInputActionValue&) { OnInputActionCompleted(Tag); });
    }
    ```

- `void OnInputActionTriggered(FGameplayTag AbilityTag)`
  - 输入：`AbilityTag`（已由 lambda 捕获）。
  - 作用：若 `AbilityTag.MatchesTag("Input.Weapon")` → 查找 Active 的 `GA_WeaponComboCoordinator` → `Coordinator→HandleWeaponInput(AbilityTag)`；否则 → `TryActivateAbilityByTag(AbilityTag)`。

- `void OnInputActionCompleted(FGameplayTag AbilityTag)`
  - 输入：直接传入 `AbilityTag`。
  - 作用：检查 ASC 是否持有 `Combat.State.Charging` Tag → 若是则 `HandleGameplayEvent(Combat.Event.ChargeReleased, InputTag=AbilityTag)`。蓄力 GA 通过 `AbilityTrigger` 监听此 Event 接收释放信号。若 ASC 无 `Charging` Tag，静默跳过（蓄力 GA 可能已被 Cancel，避免意外释放）。

- `void GrantWeaponAbilities(TArray<TSubclassOf<UGameplayAbility>> Abilities)`
  - 输入：武器授予的能力类列表。
  - 作用：授予并存储 Handle → `EquipmentComponent→OnEquipmentChanged` 时调用。

- `void RemoveWeaponAbilities()`
  - 作用：移除所有武器授予的能力（切换武器时调用）。

- `void BindInputAction(UInputAction* Action, FGameplayTag AbilityTag)`
  - 输入：InputAction 资产、Ability Tag。
  - 作用：运行时动态绑定/替换单个 IA→Tag 映射。常见场景：进入载具后换一套按键映射、特殊状态（攀爬/游泳）覆盖默认绑定。
  - 注意：限制攻击/不可操作场景不通过解绑实现——GAS 的 `CanActivateAbility` 通过 GameplayTag 阻塞拦截激活。

### 统一派发流程

```
EnhancedInput (所有按键/摇杆)
  → Lambda 捕获的 FGameplayTag → ASC→OnInputActionTriggered(AbilityTag)
    → Tag.MatchesTag("Input.Weapon") ?
        ├── 是 → 查找 Active 的 GA_WeaponComboCoordinator
        │        → Coordinator→HandleWeaponInput(AbilityTag)
        │        → 协调器内部：帧级批处理 → StateIndex 匹配 → ActivateAbility(Class)
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
| StaminaCostRate | FScalableFloat | "Ability\|Cost" | 0 | 持续耐力消耗速率（每秒）。用于奔跑/蓄力/瞄准。仅 `bIsContinuous==true` 的 Ability 使用：每 Tick 扣除 `Rate × StaminaConsumptionRate × Δt` |
| bIsContinuous | bool | "Ability\|Cost" | false | 是否持续型 Ability（true=GA_Sprint/GA_Aim，false=单次型如 GA_Dodge） |
| CooldownDuration | FScalableFloat | "Ability\|Cooldown" | 0 | 冷却时长 |
| CooldownTag | FGameplayTag | "Ability\|Cooldown" | 空 | 冷却标签（用于 UI 显示冷却） |
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
    - **单次型（bIsContinuous=false）**：基类在 Activate 时扣除耐力 `StaminaCost × StaminaDeductionRate`、扣除武器资源、启动冷却。非攻击类 Ability（闪避/喝药）子类覆写实现具体逻辑；攻击类 Ability 由 `UMHGZAttackAbility` 接管。
    - **持续型（bIsContinuous=true）**：基类不一次性扣耐力——改为在 `OnTick` 中每帧扣除。**用 Tick × DeltaTime 保证帧率无关**，不同帧率下 1 秒总扣除量一致（30/60/120 FPS 均扣除 `CostRate × ConsumptionRate`）。API：`ASC→ApplyModToAttribute(StaminaAttribute, Add, -CostRate × ConsumptionRate × DeltaTime)`——走 ApplyMod 触发 AttributeSet 的 `PreAttributeChange` Clamp，防止扣到负数。耐力归零后 `EndAbility`。

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
| AttachSocketName | FName | 必填 | 碰撞体挂载的骨骼 Socket（如 "weapon_tip"、"hand_r"） |
| Shape | ECollisionShape | Sphere | 碰撞形状（Sphere / Capsule / Box） |
| ShapeExtent | FVector | (20,20,20) | 形状参数：Sphere→X=Radius；Capsule→X=Radius+Z=HalfHeight；Box→HalfExtent |
| CollisionChannel | TEnumAsByte\<ECollisionChannel\> | GameTraceChannel1 | 碰撞通道（默认 Weapon 通道） |
| HitzoneQueryTag | FGameplayTag | 空 | 限定碰撞仅检测带此 Tag 的组件。空=不限制（检测所有碰撞） |

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
| HitCueTag | FGameplayTag | 空 | 物理命中 GameplayCue 标签（必设——如 `GameplayCue.Hit.Slash` / `GameplayCue.Hit.Blunt`）。`MakeDamageSpec` 注入到 GE Spec 的 DynamicGameplayCueTags |
| ElementalCueTag | FGameplayTag | 空 | 元素附魔命中 GC 标签（可选——留空则无元素特效）。如 `GameplayCue.Hit.Fire` |
| CameraShakeClass | TSubclassOf\<UCameraShakeBase\> | nullptr | 震屏类（按武器种类选不同类；留空则无震屏）。在 `ApplyDamage` 中通过 `ClientStartCameraShake` 执行 |
| CameraShakeScale | float | 0.0 | 震屏强度倍率（0.0~1.0）。同武器不同招式改此值，不产生新蓝图 |
| HitStopBase | FScalableFloat | 0 | 卡肉基础时长（秒）。0=无卡肉。实际卡肉 = `HitStopBase × MotionValue × HitzoneDefense`，仅在 `ApplyDamage` 执行——弱点卡肉重，坚硬部位几乎不停顿 |
| SwingSound | TObjectPtr\<USoundBase\> | nullptr | 招式挥刀风声（必配——每段攻击的默认风声）。武器可通过 `SwingSoundOverrides` 按 `AudioIdentityTag` 覆盖 |

> **震屏/卡肉归 Ability 层（非 GameplayCue）：** `CameraShakeClass`/`CameraShakeScale`/`HitStopBase` 在 `ApplyDamage` 中读取并执行。实际卡肉 = HitStopBase × MotionValue × HitzoneDefense——弱点（Defense=1.0）卡肉重，坚硬部位（0.2）几乎不停顿。GameplayCue 只管粒子+音效。

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
  - 作用：`CurrentSegmentIndex = SegmentIndex` → 在 `AttackSegments[SegmentIndex].Collision.AttachSocketName` 处按配置形状创建碰撞体 → 清空 `HitTargets`。
  - 首帧判定：创建碰撞体后下一 Tick 执行一次 `SweepMultiByChannel`——从武器上一帧位置扫到当前帧位置，按 `FHitResult.Time` 升序取首个带 `HitzoneQueryTag` 的命中（若配置了该 Tag），记录到 `HitTargets` 后调用 `ApplyDamage(HitActor, BoneName, SegmentIndex)`。若 Sweep 无命中则注册 `OnComponentBeginOverlap` 持续检测后续新进入的怪物。
  - **多跳伤害（MultiHitCount>1）：** 首帧 Sweep 命中后启动 `MultiHitTimer`，每隔 `MultiHitInterval` 秒对 `HitTargets` 中所有怪物调用 `ApplyDamage(HitActor, BoneName, SegmentIndex)`，共 `MultiHitCount` 次。`DisableCollision` 或 GA 结束 → 清除 Timer。
  - 调用方：`UAnimNotifyState_AttackCollision→NotifyBegin`。
  - **性能：** 单次 RegisterComponent ~0.05ms，4 段 0.2ms（帧预算 1.25%），保持动态创建方案。

- `void DisableCollision()`
  - 作用：清除 `MultiHitTimer`（若存在）→ 销毁碰撞体 → 停止检测。若 `AttackSegments[CurrentSegmentIndex].Damage.bRequiresHitToContinue && HitTargets.IsEmpty()` → 调用 `ShouldContinueAfterHit()` → 若返回 false → `EndAbility` 提前。
  - 调用方：`UAnimNotifyState_AttackCollision→NotifyEnd`。

- `void OnAttackOverlap(AActor* HitActor, FName HitzoneBoneName)`
  - 输入：被命中的 Actor、接触的 hitzone 骨骼名。
  - 作用：过滤链 — 自身 → 队友 → 已在 `HitTargets` 中（同怪物已命中）→ 无敌 → 已死亡 → 命中组件不含 `HitzoneQueryTag`（若配置）→ 任一命中则 return。通过后 `HitTargets.Add(HitActor, HitzoneBoneName)` → 调用 `ApplyDamage(HitActor, HitzoneBoneName)`。
  - 设计思路：每怪物只记录首次接触的 hitzone。若首帧 Sweep 已命中怪物，后续 Overlap 事件中同怪物直接跳过。多怪物场景下各自独立记录。

- `void ApplyDamage(AActor* Target, FName HitzoneBoneName, int32 SegmentIndex)`
  - 输入：目标 Actor、命中部位骨骼名、段索引。
  - 作用：
    1. 计算 `ActualHitStop = HitStopBase × MotionValue × HitzoneDefense` → 冻结时间（`CustomTimeDilation`），FTimer 恢复
    2. 调用 `MakeDamageSpec(Target, HitzoneBoneName, SegmentIndex)` 构造 GE Spec + 注入 GC Tag
    3. `SourceASC→ApplyGameplayEffectSpecToTarget(Spec, TargetASC)`（ASC 自动路由 GC 粒子+音效）
    4. 读 `CameraShakeClass`+`CameraShakeScale` → `ClientStartCameraShake`
    5. **首次命中时（`bHasHitThisActivation==false`）：** 设 `bHasHitThisActivation=true` → 通知协调器 `GA_WeaponComboCoordinator→OnAttackHit()`（触发 `PendingGrantedTags` 授予）→ 若段 `Damage.OnHitSelfEffect` 非空则 Apply 到自身 ASC
    6. 多段碰撞/多怪物场景下，后续命中跳过步骤 5。**多跳伤害（MultiHitCount>1）每次 Tick 都执行步骤 1-4（Apply 伤害 GE），但不重复触发首次命中逻辑。**
- `FGameplayEffectSpecHandle MakeDamageSpec(AActor* Target, FName HitzoneBoneName, int32 SegmentIndex)`
  - 输入：目标 Actor、命中部位骨骼名、段索引。
  - 输出：构造好的 GE Spec。
  - 作用：
    1. `ASC→MakeOutgoingSpec(AttackSegments[SegmentIndex].Damage.DamageEffectClass)`
    2. **伤害计算：** `Damage = AttackPower(ASC Attribute) × Seg.Damage.MotionValue × HitzoneDefenseMultiplier` → `Spec→SetSetByCallerMagnitude(DamageSetByCallerTag, Damage)`
    3. **硬直值计算（若 Seg.Damage.BaseStaggerValue > 0）：** `Stagger = Seg.Damage.BaseStaggerValue × StaggerMultiplier(ASC Attribute) × HitzoneStaggerRate` → 写入 Spec 供目标 ExecCalc 处理
    4. `Spec→AddDynamicAssetTag(Seg.Damage.HitStaggerTag)`
    5. 若 `bUseHitzoneDefense`：`Spec→SetSetByCallerMagnitude("Hitzone.DefenseMultiplier", MonsterHitzoneComp→DefenseMultiplier)` + `Spec→SetSetByCallerMagnitude("Hitzone.StaggerRate", MonsterHitzoneComp→StaggerRate)`
    6. `Spec→AddDynamicAssetTag(HitzoneTag)` — 命中部位标签写入 Spec
    7. `Spec→GetContext()→AddHitResult(Hit)` — 碰撞检测的 `FHitResult` 写入 GameplayEffectContext
    8. **GC 标签注入（4 类）：** 向 `DynamicGameplayCueTags` 注入：① `HitCueTag`（物理命中类型）；② `ElementalCueTag`（元素附魔）；③ 暴击由 ExecCalc 内部 `ASC→AddGameplayCue(Hit.Crit)`；④ `GameplayCue.Hit.DamageNumber`（始终追加）。

- `bool ShouldContinueAfterHit() const` (BlueprintNativeEvent)
  - 输出：当前碰撞窗口命中后，是否继续下一段碰撞窗口。
  - 默认实现：若 `AttackSegments[CurrentSegmentIndex].Damage.bRequiresHitToContinue && HitTargets.IsEmpty()` → return false；否则 return true。
  - **蓝图覆写场景——登龙剑：** 覆写此函数 → 读 ASC 的武器资源（气刃槽色阶）→ 若色阶 < 白 → return false → `EndAbility` 提前，第二段不播放。
  - 调用时机：`DisableCollision` 内，下一段 `EnableCollision` 之前。

- `bool CheckWeaponResourceForAbility() const` (BlueprintNativeEvent)
  - 输出：当前武器资源是否满足此 Ability 的消耗要求。
  - 默认返回 true。**各武器 GA 子类自行覆写**——查询各自武器特有的资源系统（气刃槽/瓶计数/蓄力等级等）。

### Ability 继承层级

`UGameplayAbility` → `UMHGZGameplayAbility`（耐力/冷却/资源）→ `UMHGZAttackAbility`（碰撞+伤害+部位判定）、`UMHGZDodgeAbility`（翻滚）、`UMHGZEdgeVaultAbility`（边缘跳越）。蓝图子类：`GA_Sprint`、`GA_Heal` 等。

### GameplayCue 集成 — MakeDamageSpec

`MakeDamageSpec` 构造 GE Spec 时向 `DynamicGameplayCueTags` 注入 4 类 GC Tag。命中后 `ASC::ApplyGameplayEffectToSelf` 内部自动读取并路由到 `UMHGZGameplayCueManager`，所有匹配的 `OnBurst` 依次触发。

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


## 武器 Ability 基类分化

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

## 蓄力式攻击

蓄力不进连招表路由——全程在一个 GA 内部闭环。`bIsContinuous=true`，按住累积 `ChargeLevel`（通过曲线/参数控制递增速率），ASC 持有 `Input.Modifier.Charging` Tag。松开（Completed 事件）→ ASC 的 `OnInputActionCompleted` 检查 `Combat.State.Charging` Tag → 若存在则 `HandleGameplayEvent(Combat.Event.ChargeReleased, InputTag=AbilityTag)`。蓄力 GA 通过 `AbilityTrigger` 监听此 Event → 根据 `ChargeLevel` 分支选 Montage 和 `DamageConfig` → 方向修正（`MaxCorrectionAngle` 通常设 60°）→ 播放释放 Montage。不同等级使用不同 `AttackSegments` 配置，不创建多个 GA 蓝图子类。

> **优势：** 蓄力 GA 被 Cancel（受击/死亡）时 `Charging` Tag 已移除 → Completed 事件检查 Tag 不存在 → 不发送 `ChargeReleased` 事件 → 蓄力 GA 不会在被打断后意外释放。不再需要遍历 ActiveAbilities 查找蓄力 GA。

## 怪物攻击碰撞——部位胶囊体复用 + 通道切换

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

## 武器资源子系统

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
| `URes_LongSword` | `ESpiritLevel Level`（无/白/黄/红）、`float Amount`、`FTimerHandle DecayTimer` | 击中回复量不同、等级随时间和命中升降、衰减 Timer | `GaugeFillSound` / `LevelUpSound`（白→黄→红） / `LevelDownSound` / `DepleteSound` |
| `URes_InsectGlaive` | `float KinsectStamina`/`MaxKinsectStamina`（猎虫耐力）、`float KinsectStaminaRegenRate`/`DrainRate`（回复/消耗速率）、`TMap<FGameplayTag, float> ExtractDurations`（白/黄/红单灯基础时长）、`float TripleUpDuration`（三灯固定时长）、`TArray<FActiveGameplayEffectHandle> ActiveExtractHandles`（当前激活的单灯 GE Handle）、`FActiveGameplayEffectHandle TripleUpHandle`（三灯 GE Handle）、`bool bTripleUpActive`（三灯激活标志位）、`TMap<FGameplayTag, FActiveModifier> ActiveModifiers`（词条修饰器） | **萃取状态机**：`MapHitzoneToExtract(HitzoneTag)→ExtractColor`（部位→颜色映射，虚函数可覆写）→ `ApplyExtract(Color)`（Apply Duration GE 到 ASC）→ `CheckAndActivateTripleUp()`（三灯齐全时移除单灯 GE → Apply 三灯 GE，不可刷新）。**猎虫耐力**：Tick 放出时扣耐力（`DrainRate × Δt`）→ 归零自动强制召回；休息时回复（`RegenRate × Δt`）。**灯消耗**：`ConsumeExtract(Color)`移除对应 GE → 若原为三灯则解除、剩余灯继续各自计时。**词条**：`ApplyEntryModifier(Tag, Value, Op)`接收装备词条修改倍率参数 | `ExtractCollectedSound`（红/白/黄各不同） / `TripleUpActivatedSound`（三灯齐聚） / `TripleUpExpiredSound`（三灯到期） / `ExtractExpirySound`（单灯到期） / `KinsectDepletedSound`（猎虫耐力归零） |
| `URes_ChargeBlade` | `int32 PhialCount`(0~6)、`bool ShieldCharged`、`FTimerHandle RedShieldTimer` | 瓶被动不消耗、部分招式主动消耗、红盾有时限 | `PhialLoadSound` / `ShieldChargeSound` / `PhialBurstSound` / `OverheatSound` |
| `URes_SwitchAxe` | `float ChargeGauge`(0~1) | 连续值充能 | `GaugeChargedSound`（充能就绪） / `SwordModeActivateSound` / `SwordModeDeactivateSound` |

> 资源可视化：`DT_WeaponResourceConfig` 桥接 `WeaponTypeTag → TSoftClassPtr<UUserWidget>`。资源组件 Tick 中广播 `OnValueChanged` 委托，UI 订阅刷新。

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

## UMHGZEdgeVaultComponent — 边缘跳越组件

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
| ComboEntries | TArray\<FComboNode\> | 连招节点列表 |

### FComboNode 结构体

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| StateName | FName | 必填 | 当前所处的具体招式名（"Idle" / "RisingSlash" / "DoubleSlash" / "TornadoSlash"…），非抽象段位编号。`"Idle"` 为起手待机态 |
| bMatchAnyState | bool | false | 为 true 时忽略 StateName 匹配——匹配任意招式状态（含 Idle）。用于纳刀、起跳等通用招式。**不自动排除任何状态**——若需排除受击/击倒/Idle，使用 `RequiredTags`+`BlockedTags` 显式声明 |
| InputAction | FGameplayTag | 必填 | 触发条件（`Input.Weapon.Y` / `Input.Weapon.B` / `Input.Weapon.RT` 为基础按键；`Input.Weapon.YB` / `Input.Weapon.RTA` 等为 Chord Trigger 同时按键）。修饰态（如按住 LT 瞄准）不走单独 InputAction——通过 `RequiredTags={Input.Modifier.Aiming}` 区分 |
| DirectionalInput | EComboDirection | None | 方向条件：None=不判方向 / Forward / Back / Left / Right。不匹配则跳过该节点 |
| NextState | FName | 必填 | 命中后跳转到的招式名（可指向自身或前序招式，有向图允许环） |
| AbilityClass | TSubclassOf\<UMHGZAttackAbility\> | nullptr | 触发的攻击 GA 蓝图（Montage 由 GA 蓝图内部指定，连招表不持有动画引用） |
| StaminaRequired | float | 0 | 耐力门槛——连招匹配时协调器检查 `CurrentStamina ≥ Required`。**不负责扣耐**——实际扣除由 GA 的 `UMHGZGameplayAbility::StaminaCost` 在 ActivateAbility 中执行 |
| RequiredTags | FGameplayTagContainer | 空 | 激活前提——ASC **必须持有全部**这些 Tag（AND）。含状态标签：`Grounded/Unsheathed`（地面招式）、`Aerial/Unsheathed`（空中招式）、`Sheathed`（拔刀攻击）。Buff/PowerUp 也在此列 |
| BlockedTags | FGameplayTagContainer | 空 | 激活阻止——ASC **必须不持有任一**这些 Tag（NOR）。用于排除特定状态：登龙剑设 `BlockedTags={Combo.Branch.PostRoundslash}`，大回旋 `GrantedTags` 含此 Tag → 登龙无法从大回旋后派生 |
| GrantedTags | FGameplayTagContainer | 空 | **GA 首次命中后**由协调器授予的临时 Tag，供后续节点 RequiredTags/BlockedTags 判断（非激活时立即授予）。空挥则 GrantedTags 不生效 → 依赖此 Tag 的后续节点匹配失败 → 空挥断连 |
| bRequiresHitToGrantTags | bool | false | 为 true 时本节点必须命中才能接下一段（协调器仅收到 GA 命中通知后才应用 GrantedTags）。false=激活即授予，允许空挥接下一段 |
| bRequiresWindowOpen | bool | true | 为 false 时本节点匹配**不受 `Combat.State.ComboWindowOpen` Tag 限制**——即使连招窗口关闭也能触发。实际可用性仍受 `RequiredTags` 约束（收虫/纳刀需 `DodgeAcceptOpen`）。默认 true |
| Priority | int32 | 0 | 显式匹配优先级。同层（精确招式/通用招式 + DirectionalInput）内有多个候选行满足 InputAction 条件时，Priority 高的优先匹配 |

### 出招表数据模型

`ComboEntries` 是平面数组，`NextState` 是字符串键（非指针）。协调器构建 `TMap<FName, TArray<int32>> StateIndex`，按 `StateName` 分组（`bMatchAnyState=true` 的行放入 `"*"` 桶），运行时 O(1) 查候选行。匹配时查询 `StateIndex[CurrentState]` 和 `StateIndex["*"]` 两个桶。`StateName` 用具体招式名（如 `"RisingSlash"`）而非抽象编号——多路径收敛和派生差异在出招表中显式可见。

### 与装备系统的对接

`EquipmentComponent→ApplyItemEffects`：`ASC→RemoveWeaponAbilities()` → `ASC→GrantWeaponAbilities(ComboAbilities)` → `ASC→GiveAbility + TryActivateAbility(GA_WeaponComboCoordinator)`（先激活协调器，空状态）→ `UMHGZDataManager::FindWeaponComboData` 异步加载 ComboData → `Coordinator→SetComboData(ComboData)` 构建 StateIndex。所有武器共用 `Input.Weapon.Y/B/RT`，切换武器时只换 ComboData。

### 复合输入与修饰态

| 模式 | 示例 | 实现 | FComboNode 如何区分 |
|------|------|------|---------------------|
| 长按修饰+点按 | 按住 LT 瞄准时按 B | LT→Hold trigger→设 `Input.Modifier.Aiming` Tag；B 照常触发 | 同一 InputAction（B）+ 不同 RequiredTags（空 vs `Aiming`）匹配不同行 |
| 同时按 | Y+B | `IA_YB` + `UInputTriggerChordAction` 引用 IA_Y 和 IA_B | 独立 InputTag `Input.Weapon.YB`，与 Y、B 不冲突 |
| 嵌套长按 | 按住 RT 蓄力 + 按住 LT 瞄准 | 两个独立 Hold trigger，各管各的 Tag | `RequiredTags={Charging, Aiming}` 匹配蓄力瞄准态招式 |

> **核心原则：** 不创建组合爆炸的 InputAction——修饰态走 GameplayTag（`Input.Modifier.*`），真正的同时按键走 Chord Trigger。

### EComboDirection 象限规则

以角色前向为基准 ±45° 分 4 象限。Forward/Back 优先级高于 Left/Right——对角线（45°）归 Forward。无输入或向量长度 < 0.1 视为 None。翻滚使用相同规则。

| 值 | 角度范围 |
|----|------|
| None | 不检测方向（长度 < 0.1） |
| Forward | [-45°, +45°] |
| Back | [135°, 180°] ∪ [-180°, -135°] |
| Left | (45°, 135°) |
| Right | (-135°, -45°) |

## GA_WeaponComboCoordinator — 连招协调器

```
UCLASS(BlueprintType, Blueprintable)
class UMHGZWeaponComboCoordinatorAbility : public UGameplayAbility
```

**Infinite 持续型 Ability**——装备武器时授予并激活，卸下时结束。整个装备期间协调器保持 Active，不结束时不影响其他 Ability 同时运行。在 Activate 中构建 `StateIndex`，在 EndAbility 中清理解绑。**协调器不绑定 EnhancedInput**——只暴露公共方法 `HandleWeaponInput(FGameplayTag)`，由 ASC 的 `OnInputActionTriggered` 在判别为武器 Tag 时调用。

### 运行时状态（非 UPERTY，协调器内部维护）

| 成员 | 类型 | 说明 |
|------|------|------|
| CurrentState | FName | 当前所处的招式名（初始 "Idle"） |
| ComboData | TObjectPtr\<UMHGZWeaponComboData\> | 当前武器的连招表 |
| StateIndex | TMap\<FName, TArray\<int32\>\> | 按 StateName 分组的行号索引。含 `"*"` 桶存放所有 bMatchAnyState=true 的行。O(1) 查找候选行 |
| ComboTimeoutTimer | FTimerHandle | 绝对安全兜底计时器。每次新 GA 激活时重置，时长=`ComboTimeout`(10s)。到期时若仍未回 Idle→强切+清临时 Tag。正常流程 Montage 完成先触发，此计时器不介入 |
| PendingInputs | TArray\<FGameplayTag\> | 当前帧累积的待处理武器输入（帧级批处理——收集后统一排序匹配） |
| InputBatchTimer | FTimerHandle | 批处理延迟 Timer（0 秒延迟，下一帧触发排序匹配） |
| PendingGrantedTags | FGameplayTagContainer | 当前激活的 GA 待授予的 GrantedTags。GA 激活时存入（非立即应用），GA 首次命中时由 `OnAttackHit()` 写入 ASC；若 GA 结束仍未命中则丢弃 |
| PreInputTag | FGameplayTag | 预输入缓冲区——ComboWindow 打开前景早按键的 Tag（后覆盖前，仅存一个）。在 `HandleWeaponInput` 中窗口关闭时写入，`ComboWindow→NotifyBegin` 中刷新消费 |
| PreInputTimestamp | float | 预输入捕获时刻（`GetWorld()->GetTimeSeconds()`）。用于判定是否在 `PreInputLifetime` 内 |
| PreInputLifetime | float | 预输入有效窗口（秒）。默认 0.15（~9帧@60fps），策划可在蓝图中调整。超时未等到窗口打开则清空缓冲 |
| ActiveLoadRequestID | FGuid | 异步加载令牌（竞态保护——SetComboData 时检查令牌，过期则丢弃）。`DataManager::RequestWeaponComboData` 返回 `FGuid`，两者类型一致 |

### 公共方法

- `void SetComboData(UMHGZWeaponComboData* InData, FGuid RequestID)`
  - 输入：武器连招表 DataAsset、加载请求令牌（`FGuid`，与 `DataManager::RequestWeaponComboData` 返回类型一致）。
  - 作用：检查令牌是否匹配当前 ActiveLoadRequestID → 若匹配则构建 StateIndex（遍历 ComboEntries 按 StateName 分组），完成后协调器开始接收输入。仅在 Activate 后、EndAbility 前有效。

- `void HandleWeaponInput(FGameplayTag AbilityTag)`
  - 输入：武器输入 Tag（`Input.Weapon.Y` / `Input.Weapon.B` 等）。
  - 作用：由 ASC 的 `OnInputActionTriggered` 在判别为武器 Tag 时调用。若 ComboData 未注入（StateIndex 为空）→ 忽略。帧批处理收集 → 排序 → 匹配 → 激活 GA。

- `void OnAttackHit()`
  - 作用：由攻击 GA 的 `ApplyDamage` 在首次命中时调用。将 `PendingGrantedTags` 写入 ASC。

- `void OnAttackFinished()`
  - 作用：由攻击 GA 的 `EndAbility` 调用。若 `CurrentState` 在此期间未被新 GA 激活变更 → `CurrentState = "Idle"` + 清除 `Combo.Branch.*` 临时 Tag。

### 运行时工作流

**阶段 A（装备武器）：** `EquipmentComponent→OnEquipmentChanged()` → `GrantWeaponAbilities` → `GiveAbility + TryActivateAbility(GA_WeaponComboCoordinator)`（先激活空状态）→ 异步加载 ComboData → `SetComboData` 构建 StateIndex。

**阶段 B（起手攻击）：** `HandleWeaponInput` → 候选行 = `StateIndex["Idle"] ∪ StateIndex["*"]` → 四级排序（`bMatchAnyState=false > true`；`DirectionalInput` 具体 > None；`Priority` 降序）→ 遍历检查 6 条件（`InputAction`/`DirectionalInput`/`RequiredTags`/窗口或起手/`BlockedTags`/`StaminaRequired`）→ 匹配成功则 `ActivateAbility` + 更新 `CurrentState` + 存入 `PendingGrantedTags` + 重置 `SafetyTimer`（时长为 `GlobalComboTimeout`）。

**阶段 C（GA 执行）：** GA `ActivateAbility` 扣耐力/播 Montage → `AttackCollision→NotifyBegin` 调 `EnableCollision` → Sweep 命中 → `ApplyDamage` → `NotifyEnd` 调 `DisableCollision` → `ComboWindow→NotifyBegin` 加 `ComboWindowOpen` Tag。

**阶段 D（连招下一段）：** 窗口内 `HandleWeaponInput` → `StateIndex[CurrentState] ∪ StateIndex["*"]` → 匹配成功则取消旧 `SafetyTimer` → `ActivateAbility` → 更新 `CurrentState` → 重启 `SafetyTimer`（时长为 `GlobalComboTimeout`）。GA 首次命中时 `OnAttackHit()` 将 `PendingGrantedTags` 写入 ASC。

**阶段 E（回 Idle）：** `ComboWindow→NotifyEnd` 移除 Tag → Montage 播完 → GA `EndAbility` → `Coordinator→OnAttackFinished()` → 若 `CurrentState` 未变更则回 `"Idle"` + 清除 `Combo.Branch.*` Tag。

**阶段 F（异常兜底）：** `SafetyTimer` 到期（`GlobalComboTimeout` 秒，仅 Montage 卡死等极端情况）→ 强制 `CurrentState="Idle"` + 清除所有 `Combo.Branch.*` Tag。武器卸下：清除所有 `Combo.Branch.*` Tag + StateIndex 清空。

### 关键设计要点

- **帧级输入批处理：** `PendingInputs` 收集当前帧所有武器输入，下一帧统一按 Chord > 单键排序匹配
- **预输入缓冲：** `PreInputTag`（单槽，`PreInputLifetime=0.15s`），窗口打开时消费
- **节点匹配四级排序 + 匹配即停**
- **死亡处理：** `Combat.Event.Death` → `GA_Death` Cancel 所有 GA。猫车 = `SetActorLocation` + 设 Grounded+Sheathed
- **打断后自动恢复：** 监听 Hitstun/Knockdown Removed → 检查 `Input.Modifier.*` → 自动 `TryActivateAbilityByTag`

## AnimNotifyState 系列

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

无敌帧期间只关闭 Weapon 通道响应，Pawn 通道保持不变：

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

> 两层保障：(1) 碰撞层 Weapon 通道 Ignore；(2) GAS 层 `Combat.State.Invincible` Tag。

**生命周期：**

```
NotifyBegin：
  → MeshComp→GetOwner()→GetAbilitySystemComponent()
  → ASC→AddLooseGameplayTag(Combat.State.Invincible)
  → ★ 将角色胶囊体对 Weapon 通道设为 Ignore（保存原始响应，NotifyEnd 恢复）
  → 可选：同时设 MonsterAttack 通道 = Ignore（若怪物攻击用独立通道）

NotifyEnd：
  → ASC→RemoveLooseGameplayTag(Combat.State.Invincible)
  → ★ 恢复胶囊体 Weapon 通道的原始碰撞响应
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

## 特效/音效/镜头——三层分工

| 层 | 机制 | 适用场景 |
|----|------|----------|
| 帧级同步 | Montage AnimNotify / AnimNotifyState | 武器拖尾、脚步声、挥空音效 |
| 状态驱动 | GAS GameplayCue（`ASC→ExecuteGameplayCue`） | 命中火花 VFX、命中碰撞音效（按物理材质选）、伤害数字、Buff 光环 |
| 镜头 | Ability 内 `UCameraModifier` / `PlayerCameraManager→StartCameraShake` | 震屏、FOV 变化、瞄准拉近 |

调用链：`GA_Slash_01::Montage` → `AnimNotify_SlashWhoosh`（挥空音效）+ `AnimNotifyState_WeaponTrail`（刀光拖尾）→ `AttackCollision::Sweep` 命中 → `ApplyDamage` → GE Spec Apply → `GameplayCue.Hit.Slash` 触发（命中火花 VFX + 按物理材质选命中音效 + 伤害数字 UI）。震屏/卡肉在 `ApplyDamage` 中直接执行（`CameraShakeClass`/`HitStopBase`），Ability 结束后自动清理。
