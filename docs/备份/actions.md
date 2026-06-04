# 动作系统

**设计原则：** GAS + EnhancedInput 驱动，通过 GameplayTag 桥接输入与 Ability。核心能力（移动/闪避）始终可用，武器能力（连招/资源技能）由装备系统动态授予/移除。**无独立跳跃键——边缘跳越（Edge Vault）替代。**

## 移动实现

- 移动物理：`UCharacterMovementComponent`，**不用 GAS 实现**
- 移动输入：`AddMovementInput`（非 GAS 路径）
- 移动动画：AnimBP BlendSpace1D 基于 Speed 驱动
- 奔跑（GA_Sprint）：按下 LS→GE 提升 MoveSpeedMultiplier+持续扣耐；持刀时 Unsheathed Tag 阻塞
- GAS 只管两件事：**能不能动**（Tag 阻塞）、**有多快**（GE 修改 MoveSpeedMultiplier）

### RootMotion——攻击/翻滚中如何覆盖 CMC 移动

攻击 Montage 播放时，动画中的**根骨骼位移数据（RootMotion）**直接驱动角色位置/旋转，**覆盖** `AddMovementInput` 的移动输入。这是 UE5 引擎原生行为——不需要写代码来"禁止移动"。

```
同一帧内：
  AddMovementInput(Forward) → CMC 计算位移 = 10cm
  Montage RootMotion 数据  → 根骨骼偏移 = 2cm（动画师烘焙）
  → 最终角色位移 = 2cm（RootMotion 优先级更高，胜出）
```

摇杆输入在攻击期间**不被阻塞**——`AddMovementInput` 仍在运行——但 RootMotion 数据覆盖了 CMC 的位移结果。摇杆方向被 **MotionWarping** 读取用于旋转修正（见下方 UMHGZAttackAbility 的 `MaxCorrectionAngle`），而不是驱动移动。

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

## FAbilityInputBinding

| 字段 | 类型 | 说明 |
|------|------|------|
| InputAction | TObjectPtr\<UInputAction\> | EnhancedInput 资产 |
| AbilityTag | FGameplayTag | 触发时激活的 Ability Tag |
| bConsumeInput | bool | 触发后是否消耗输入 |

## UMHGZAbilitySystemComponent — 扩展 ASC

核心方法：
- `InitializeAbilitySystem()` — 授予 CoreAbilities + 绑定 EnhancedInput（lambda 捕获 FGameplayTag）
- `OnInputActionTriggered(Tag)` — 武器 Tag→Coordinator；非武器 Tag→TryActivateAbilityByTag
- `OnInputActionCompleted(Tag)` — 通知蓄力 GA 释放
- `GrantWeaponAbilities` / `RemoveWeaponAbilities` — 授予/移除武器能力

## UMHGZGameplayAbility — Ability 基类

统一处理耐力消耗、冷却、输入标签。

| 关键成员 | 说明 |
|------|------|
| StaminaCost（FScalableFloat） | 单次耐力扣除量 |
| StaminaCostRate（FScalableFloat） | 持续耐力消耗速率（每秒） |
| bIsContinuous | 是否持续型 Ability |
| CooldownDuration（FScalableFloat） | 冷却时长 |
| bRequiresWeaponResource | 是否需要武器资源 |
| MaxCorrectionAngle | 攻击激活瞬间最大方向修正角度（以角色朝向为基准，扭向摇杆方向） |
| AudioIdentityTag | FGameplayTag | 挥刀风声身份标签（如 `Audio.Swing.LS_VerticalSlash`）。GA 蓝图必配——`ActivateAbility` 时以此为 Key 查 `WeaponDef.SwingSoundOverrides`，命中则覆盖 `DamageConfig.SwingSound`。不同招式用不同 GA 蓝图→不同 Tag→不同音效，武器覆盖是可选增量 |

## UMHGZAttackAbility — 攻击 Ability 中间层

统一封装碰撞检测、命中过滤、伤害 GE 构造。蓝图子类只需配置参数。攻击 Montage 统一启用 `bEnableRootMotion=true`——动画数据驱动位移，CMC 移动输入被 RootMotion 覆盖（见 §移动实现-RootMotion）。

### 核心配置结构

- **FAttackCollisionConfig：** Socket名、形状（Sphere/Capsule/Box）、通道、HitzoneQueryTag

**FAttackDamageConfig：**

| 字段 | 类型 | 说明 |
|------|------|------|
| MotionValue | FScalableFloat | 动作值（伤害倍率） |
| BaseStaggerValue | FScalableFloat | 基础破坏值 |
| KnockbackAngle | float | 击退角度 |
| KnockbackForce | FScalableFloat | 击退力度 |
| HitStaggerTag | FGameplayTag | 命中硬直等级（`Combat.Stagger.Light/Medium/Heavy`） |
| bRequiresHitToContinue | bool | 命中后是否允许招内派生（`ShouldContinueAfterHit` 钩子） |
| OnHitSelfEffect | TSubclassOf\<UGameplayEffect\> | 命中自身施加的 GE（如虫棍三灯） |
| HitCueTag | FGameplayTag | 物理命中 GameplayCue 标签（如 `GameplayCue.Hit.Slash`） |
| ElementalCueTag | FGameplayTag | 元素附魔命中 GC 标签（留空则无元素特效） |
| CameraShakeClass | TSubclassOf\<UCameraShakeBase\> | 震屏类（按武器种类选不同类；留空则无震屏） |
| CameraShakeScale | float | 震屏强度倍率（0.0~1.0）。同武器不同招式改此值，不产生新蓝图 |
| HitStopBase | FScalableFloat | 卡肉基础时长（秒）。0=无卡肉。实际卡肉 = HitStopBase × MotionValue × HitzoneDefense，仅在 `ApplyDamage` 执行 |
| SwingSound | TObjectPtr\<USoundBase\> | 招式挥刀风声（必配——每段攻击的默认风声）。武器可通过 `SwingSoundOverrides` 按 `AudioIdentityTag` 覆盖 |

> **震屏/卡肉归 Ability 层（非 GameplayCue）：** `CameraShakeClass`/`CameraShakeScale`/`HitStopBase` 在 `ApplyDamage` 中读取并执行。实际卡肉 = HitStopBase × MotionValue × HitzoneDefense——弱点（Defense=1.0）卡肉重，坚硬部位（0.2）几乎不停顿。GameplayCue 只管粒子+音效。

> **GC 标签注入：** 详见下方 [GameplayCue 集成 — MakeDamageSpec](#gameplaycue-集成--makedamagespec)。

- **FAttackSegmentConfig：** Collision + Damage + MultiHitCount/Interval + MaxWarpAngle

### 关键方法

- `EnableCollision(SegmentIndex)` — 创建碰撞体 → 下一 Tick Sweep → ApplyDamage。**性能：** 单次 RegisterComponent ~0.05ms，4 段 0.2ms（帧预算 1.25%），保持动态创建方案
- `DisableCollision()` — 清除 MultiHitTimer → 销毁碰撞体
- `ApplyDamage(Target, BoneName, SegmentIndex)` — 1. 计算 `ActualHitStop = HitStopBase × MotionValue × HitzoneDefense`→冻结时间（`CustomTimeDilation`），FTimer 恢复；2. 构造 GE Spec + 注入 GC Tag；3. `ApplyGameplayEffectToSelf`（ASC 自动路由 GC 粒子+音效）；4. 读 `CameraShakeClass`+`CameraShakeScale`→`ClientStartCameraShake`；5. 首次命中通知协调器 `OnAttackHit()`
- `MakeDamageSpec(...)` — 伤害 = AttackPower × MotionValue × HitzoneDefense
- `ShouldContinueAfterHit()` — 招内击中派生钩子
- `CheckWeaponResourceForAbility()` — 资源门控钩子

### AnimNotifyState 类

> 均为 C++ 类（非蓝图）。`UAnimNotifyState` 有 Begin/Tick/End 三阶段回调，适合需要"持续一段帧"的逻辑。

| 类 | 作用 |
|----|------|
| AttackCollision | 攻击碰撞窗口：`NotifyBegin` 在武器骨骼处动态创建碰撞体并注册→下一 Tick Sweep 检测目标；`NotifyEnd` 销毁碰撞体+清除 MultiHitTimer |
| MonsterAttackCollision | 怪物攻击碰撞窗口：`NotifyBegin` 将部位胶囊体的 MonsterAttack 通道切为 Block→每 Tick Sweep 检测玩家；`NotifyEnd` 恢复 Ignore |
| PoiseWindow | 霸体窗口：`NotifyBegin`→ASC 添加 Poise Tag（如 `Combat.Poise.Heavy`）；`NotifyEnd`→移除。攻击动画师在此区间放置，覆盖招式的大开大合期 |
| ForesightJudge | 见切判定窗口：`NotifyBegin`→遍历 `ActiveAbilities` 找到见切 GA→设其 `bIsInForesightWindow=true`；`NotifyEnd`→恢复 false。GA 的 `OnGameplayEvent(Combat.Event.HitStagger)` 回调检查此标记决定是否成功 |

### AnimNotify 类

> 均为 C++ 类（非蓝图）。`UAnimNotify` 仅单帧触发，适合瞬时事件。

| 类 | 作用 |
|----|------|
| SwingSound | 挥刀风声：持有 `USoundBase* Sound` 成员。`Notify` 中 `PlaySoundAtLocation(this→Sound)`——GA 在 `ActivateAbility` 时从 Montage 查找此实例并注入音效引用，Notify 只读自己，不查任何外部对象 |

### GameplayCue 集成 — MakeDamageSpec

`MakeDamageSpec` 构造 GE Spec 时向 `DynamicGameplayCueTags` 注入 4 类 GC Tag。命中后 `ASC::ApplyGameplayEffectToSelf` 内部自动读取并路由到 `UMHGZGameplayCueManager`，所有匹配的 `OnBurst` 依次触发。

| 步骤 | Tag 来源 | 说明 |
|:--:|------|------|
| 1 | `FAttackDamageConfig::HitCueTag` | 物理命中类型（必设——`Slash` / `Blunt`） |
| 2 | `FAttackDamageConfig::ElementalCueTag` | 元素附魔（可选——留空则跳过） |
| 3 | ExecCalc 内部 `ASC→AddGameplayCue(Hit.Crit)` | 暴击——在 ExecCalc 判定暴击后单独触发 |
| 4 | `GameplayCue.Hit.DamageNumber` | 伤害数字——始终追加，值通过 `Parameters.RawMagnitude` 传递 |

> **ApplyDamage 不改动：** 现有链路 `ApplyDamage → ASC::ApplyGameplayEffectToSelf(Spec)` 零改动，GC 路由全由 ASC 内部自动完成。

### 挥刀风声 — GA 注入 Notify 模式

GA 激活时解析最终 `SwingSound`，直接写入 Montage 上的 `AnimNotify_SwingSound` 实例——Notify 自持数据，不经过 Character。

| 步骤 | 位置 | 说明 |
|:--:|------|------|
| 1 | `UMHGZAttackAbility::ActivateAbility` | `FinalSound = WeaponDef.SwingSoundOverrides.Find(AudioIdentityTag) ?? DamageConfig.SwingSound`——武器覆盖优先 → 招式默认兜底 |
| 2 | 同上 | `Montage→AnimNotifies` 中查找首个 `UAnimNotify_SwingSound` 实例 |
| 3 | 同上 | `NotifyInstance→Sound = FinalSound`——GA 直接注入。AnimNotify 不查任何外部对象 |
| 4 | `UAnimNotify_SwingSound::Notify` | `PlaySoundAtLocation(this→Sound)`——读自己，零查找 |

> **AnimNotify 只管时机（when），不管数据来源（what）。** GA 激活时一次性完成解析+注入，Notify 触发时只做一件事——播放自己持有的音效。

## 武器 Ability 基类分化

```
UMHGZAttackAbility
  ├── UMHGZLongSwordAbility    (太刀: 气刃槽+衰减)
  ├── UMHGZInsectGlaiveAbility (虫棍: 三灯+猎虫耐力)
  ├── UMHGZChargeBladeAbility  (盾斧: 瓶计数+盾充能)
  └── UMHGZSwitchAxeAbility    (斩斧: 充能槽)
```

每种武器基类（~50 行 C++）持有 ResourceComponent 引用 + 覆写资源门控钩子。

## 武器资源子系统

**UMHGZWeaponResourceComponent（基类）** — 动态创建/销毁，切换武器时旧状态全部清空。

| 接口 | 说明 |
|------|------|
| GetCurrentValue/GetMaxValue | 资源量 |
| Consume/Restore | 消耗/回复 |
| ApplyEntryModifier | 词条修饰器（按 AttributeTag 前缀路由） |
| ClearAllEntryModifiers | 全量重建时清空 |
| GetModifiedParam | 求值活跃修饰器 |

| 方法 | 说明 |
|------|------|
| `PlayResourceSound(USoundBase*)` | 工具方法——子类在资源变化时调用，统一走 `UGameplayStatics::PlaySound2D`（UI 反馈用 2D 音效） |

> **音效由各子类自行定义：** 太刀的"气刃升色叮"、虫棍的"精华采集嗡"、盾斧的"瓶装填咔嗒"语义完全不同——基类不预设任何音效槽位。子类各自持有自己需要的 `UPROPERTY` 音效成员，在资源变化回调中通过 `PlayResourceSound()` 播放。

子类及各自音效：

| 子类 | 资源机制 | 音效成员 |
|------|----------|----------|
| `URes_LongSword` | 气刃槽色阶+衰减 | `GaugeFillSound` / `LevelUpSound`（白→黄→红） / `LevelDownSound` / `DepleteSound` |
| `URes_InsectGlaive` | 三灯Timer | `ExtractCollectedSound`（红/白/橙各不同） / `TripleBuffSound`（三灯齐） / `ExtractExpirySound` |
| `URes_ChargeBlade` | 瓶计数+红盾 | `PhialLoadSound` / `ShieldChargeSound` / `PhialBurstSound` / `OverheatSound` |
| `URes_SwitchAxe` | 充能槽 | `GaugeChargedSound`（充能就绪） / `SwordModeActivateSound` / `SwordModeDeactivateSound` |

## 蓄力式攻击

蓄力在 GA 内部闭环——按住累积 ChargeLevel、松开释放对应等级。不进连招表路由。

## GA_Dodge — 翻滚/闪避

不进连招表。独立 GA_Dodge + DT_WeaponDodgeConfig 参数化。收刀态所有武器共用 Montage；拔刀态每武器独立配置。

```
GA_Dodge::CanActivateAbility：
  → 检查无 Hitstun/Knockdown
  → Attacking 有但 DodgeAcceptOpen 无 → 阻塞
  → Stamina ≥ Cost
```

## UMHGZEdgeVaultComponent

挂载到 Character。Tick 中检测：冷却→状态阻塞→Sprint/Dodge 激活中→边缘检测→触发 GA_EdgeVault。

## UMHGZWeaponComboData — 连招表 DataAsset

### FComboNode 结构体

| 关键字段 | 说明 |
|------|------|
| StateName | 当前招式名（"Idle"/"RisingSlash"/...），`"*"` 通配用 bMatchAnyState |
| InputAction | 触发条件（Input.Weapon.Y/B/RT/YB...） |
| DirectionalInput | None/Forward/Back/Left/Right |
| NextState | 命中后跳转招式名（有向图允许环） |
| AbilityClass | 触发的攻击 GA 蓝图 |
| RequiredTags / BlockedTags | AND/NOR 条件 |
| GrantedTags | 命中后授予的临时 Tag |
| bRequiresHitToContinue | 空挥断连 |
| Priority | 显式匹配优先级 |

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

**Infinite 持续型 Ability。** 装备武器时授予并激活，卸下时结束。

### 运行时状态

| 成员 | 说明 |
|------|------|
| CurrentState | 当前招式名（初始 "Idle"） |
| StateIndex | `TMap<FName, TArray<int32>>` 按 StateName 分组的行号索引 |
| PendingInputs | 帧级批处理缓冲区 |
| PendingGrantedTags | 等待 GA 命中后授予的 Tag |
| PreInputTag | 预输入缓冲（单槽，后覆盖前） |
| ActiveLoadRequestID | 异步加载令牌（竞态保护） |

### 核心方法

- `SetComboData(Data, RequestID)` — 检查令牌→构建 StateIndex
- `HandleWeaponInput(Tag)` — 帧批处理收集→排序→匹配→激活 GA
- `OnAttackHit()` — 授予 PendingGrantedTags
- `OnAttackFinished()` — 回 Idle 主路径

### 工作流（6 阶段）

**阶段 A：** 装备武器→协调器激活（空状态）→异步加载 ComboData→SetComboData
**阶段 B：** 按键→StateIndex 匹配→ActivateAbility
**阶段 C：** GA 执行→Montage 播放→AttackCollision→ComboWindow 打开
**阶段 D：** 窗口内按键→连招下一段→命中回调→GrantedTags 生效
**阶段 E：** 窗口关闭→Montage 播完→OnAttackFinished→回 Idle
**阶段 F：** ComboTimeout 兜底/武器卸下

### 关键设计要点

- **Montage 完成 = 主回 Idle 路径**（动画时长即精确计时）
- **ComboTimeout = 唯一安全兜底**（10s，仅极端异常触发）
- **帧级输入批处理：** Chord > 单键优先级排序，解决 UE EnhancedInput Chord 不消费单键的问题
- **预输入缓冲：** ComboWindow 开前按键被记住（单槽，PreInputLifetime=0.15s）
- **死亡处理：** HP=0→Combat.Event.Death→**GA_Death** 激活（Cancel 所有其他 GA）→播放死亡 Montage。猫车=同关卡 SetActorLocation → 手动设为 Grounded+Sheathed → 回满 HP/Stamina
- **打断后自动恢复：** Character 层监听 Hitstun/Knockdown 的 Removed 事件→检查 Input.Modifier.* 仍存在→自动 TryActivateAbilityByTag（如 LT 瞄准被打断后自动恢复）

## AnimNotifyState 系列

| 类 | 挂载位置 | 作用 |
|----|----------|------|
| ComboWindow | 攻击 Montage | 标记连招输入窗口（Add/Remove ComboWindowOpen Tag） |
| DodgeWindow | 翻滚 Montage | 标记无敌帧（Add/Remove Invincible Tag + 碰撞通道切换） |
| DodgeAcceptWindow | 攻击 Montage | 标记翻滚接受窗口（Add/Remove DodgeAcceptOpen Tag） |
| ForesightJudge | 见切 Montage 段0 | 判定窗口（遍历 ActiveAbilities 找 GA→设 bIsInForesightWindow） |

### 见切（ForesightSlash）完整流程

段0（后撤）：DodgeWindow（无敌帧）+ ForesightJudge（监听 HitStagger 事件）并行→轨迹重合判定
段1（回砍）：标准 AttackCollision→ApplyDamage→bDodgeSuccessful?回满气刃槽+授予 ForesightSuccess Tag:仅伤害

## 特效/音效/镜头——三层分工

| 层 | 机制 | 适用场景 |
|----|------|----------|
| 帧级同步 | Montage AnimNotify | 武器拖尾、脚步声、挥空音效 |
| 状态驱动 | GAS GameplayCue | 命中火花、音效（按物理材质选）、伤害数字、Buff 光环 |
| 镜头 | Ability 内 CameraModifier | 震屏、FOV 变化、瞄准拉近 |
