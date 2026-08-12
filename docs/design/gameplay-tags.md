# GameplayTags 完整层级

> **实施状态说明：** `Config/DefaultGameplayTags.ini` 是实际注册表。M0 建立目标 Tag 与 Yellow→Orange 迁移 Redirect；具体 Tag 的运行时所有权仍按对应里程碑逐步接入。

## 物品类型

```
Item.Type.Weapon(.Sword/.Bow/.Staff/.Dagger/.InsectGlaive)
Item.Type.Armor(.Helmet/.Chest/.Legs/.Gloves/.Boots)
Item.Type.Accessory
Item.Type.Consumable(.Potion/.Food/.Throwable/.Scroll)
Item.Type.Material(.Ore/.Herb/.MonsterPart)
Item.Type.Quest
```

## 稀有度（RarityLevel 自动生成）

```
Item.Rarity.r1 ~ Item.Rarity.r12
```

## 装备槽位

```
Equipment.Slot.Weapon / Armor / Accessory
```

## 角色属性

```
Attribute.Health / MaxHealth
Attribute.Stamina / MaxStamina
Attribute.StaminaRegenRate / StaminaDeductionRate / StaminaConsumptionRate
Attribute.AttackPower / Defense / CriticalRate
Attribute.StaggerMultiplier
Attribute.WeaponResource / MaxWeaponResource  ← Tag 已注册，AttributeSet 尚无对应属性
Attribute.MoveSpeedMultiplier
```

## GE 来源（批量移除用）

```
Effect.Source.Equipment    ← 所有装备 GE 打此标签
```

## GameplayCue 标签（特效/音效触发）

```
GameplayCue.Hit.Slash      ← 斩击命中火花
GameplayCue.Hit.Blunt      ← 打击命中火花
GameplayCue.Hit.Fire       ← 火属性命中特效
GameplayCue.Hit.Crit       ← 暴击命中特效
GameplayCue.Hit.Kinsect    ← 猎虫命中反馈
GameplayCue.Hit.DamageNumber ← 伤害数字
GameplayCue.Hit.IG.DivingWyvern  ← 虫棍降龙命中特效
GameplayCue.Monster.Roar   ← 怪物咆哮
GameplayCue.Buff.Applied   ← Buff 叠加特效
GameplayCue.Weapon.Trail   ← 武器拖尾
GameplayCue.IG.ExtractGained     ← 虫棍萃取成功
GameplayCue.IG.TripleUpActivated ← 虫棍三灯齐聚瞬间
GameplayCue.IG.ExtractExpired    ← 虫棍灯到期消散
```

## 战斗

```
Combat.Event.HitStagger    ← AttributeSet::PostGameplayEffectExecute 可广播；当前 DamageSpec 未自动注入该 Event Tag
Combat.Event.Death         ← Tag 已注册，死亡广播与 GA_Death 尚未实现
Combat.Event.RequestSheatheAndUse ← Tag 已注册，QuickBar/GA_Sheathe 流程尚未实现
Combat.Event.SheatheComplete ← Tag 已注册，GA_Sheathe/QuickBar 流程尚未实现
Combat.Event.ChargeReleased ← ASC::OnInputActionCompleted 可发送；当前无消费 Ability
Combat.Event.IncomingHit    ← 怪物/测试器提交的命中上下文；突进回旋斩反击窗口消费
Combat.Stagger.Light       ← 小硬直
Combat.Stagger.Medium      ← 中硬直
Combat.Stagger.Heavy       ← 大硬直
Combat.State.Invincible    ← 无敌状态
Combat.State.Hitstun       ← 受击硬直中
Combat.State.Knockdown     ← 击倒/击飞中
Combat.State.Grounded      ← 地面态（MovementComponent 管理）
Combat.State.Aerial        ← 空中态（与 Grounded 互斥）
Combat.State.Aerial.Falling               ← 下落态（GA 结束但未落地，AnimBP 读此 Tag 选下落 Pose）
Combat.State.Aerial.Falling.Default       ← 兜底下落 Pose
Combat.State.Aerial.Falling.IG_AirDodge      ← 空中回避后
Combat.State.Aerial.Falling.IG_StrongJumpingSlash ← 强化跳跃斩后
Combat.State.Aerial.Falling.IG_DanceVault    ← 舞踏后
Combat.State.Aerial.Falling.IG_KinsectGlide  ← 猎虫滑翔后
Combat.State.Aerial.Falling.IG_KinsectSlash  ← 操虫斩后
Combat.State.Aerial.Falling.IG_DescendingThrust ← 急袭突刺阶段
Combat.State.Aerial.Falling.IG_DivingWyvern  ← 降龙阶段
Combat.State.Aerial.Dance.Source.KinsectSlash ← 操虫斩触发的舞踏，可派生强化操虫穿刺
Combat.State.Aerial.Dance.Source.AdvancingCounter ← 突进回旋斩反击舞踏，不可派生强化操虫穿刺
Combat.State.Aerial.CantDodge             ← 空中回避已用（默认无限制，舞踏重置清）
Combat.State.Aerial.CantAttack            ← 空中攻击已用（默认无限制，舞踏重置清）
Combat.State.Aerial.Landing               ← 落地瞬间过渡（AnimBP 内部用）
Combat.State.Sheathed      ← ASC 初始化时默认添加；完整收/拔刀互斥流程尚未实现
Combat.State.Unsheathed    ← 拔刀后/持刀态（与 Sheathed 互斥）
Combat.State.Aiming                ← 瞄准父标签，不直接作为具体动作条件
Combat.State.Aiming.Kinsect        ← 持刀 LT 猎虫瞄准；AimComponent 订阅
Combat.State.Aiming.Action         ← 持刀 RT 动作瞄准/特殊动作上下文
Combat.State.Aiming.Slinger        ← 收刀 LT 投射物瞄准；Demo 暂无消费方
Combat.State.ComboWindowOpen ← 连招输入窗口打开中
Combat.State.BlockMovement ← 阻断 Motion Matching 输入与期望速度
Combat.State.Movement.Starting ← 起步过渡 Tag（已注册，是否由当前 AnimBP 消费以资产为准）
Combat.State.Movement.Stopping ← 停步过渡 Tag（已注册，是否由当前 AnimBP 消费以资产为准）
Combat.State.Dead          ← 死亡（HP=0 时添加，阻塞所有输入+伤害）
Combat.State.Attacking     ← 攻击中
Combat.State.DodgeAcceptOpen ← 翻滚接受窗口打开中
Combat.State.IG.AdvancingCounterOpen ← 突进回旋斩可反击窗口
Combat.State.Charging      ← 蓄力中
Combat.State.Sprinting     ← 奔跑中
Combat.Buff.IaiSpiritRegen ← 小居合剑气回复 Buff
Combat.Buff.AttackUp       ← 攻击力提升 Buff
Combat.Buff.DefenseUp      ← 防御力提升 Buff
Combat.Poise.Light         ← 轻霸体
Combat.Poise.Medium        ← 中霸体
Combat.Poise.Heavy         ← 重霸体
Combat.Poise.Super         ← 超霸体
Combat.Branch.SpiritBlade  ← 太刀气刃分支标记
Combat.Branch.PostRoundslash ← 太刀大回旋后标记
Combat.Branch.Heavy        ← 重击分支标记
Combat.Branch.Special      ← 特殊攻击分支标记
Combat.Branch.Aerial       ← 空中连段分支标记
Combat.Branch.Extract.Red  ← 虫棍红灯连招分支标记
Combat.Branch.TripleUp     ← 虫棍三灯连招分支标记
Combat.Config.IG.RedMode.Classic  ← RuntimeHost 拥有；经典红灯动作门控
Combat.Config.IG.RedMode.Numeric  ← RuntimeHost 拥有；红灯只影响数值

Hitzone.Head               ← 怪物部位标签（头部）
Hitzone.Neck               ← 颈部
Hitzone.Back               ← 背部
Hitzone.Torso              ← 躯干
Hitzone.Tail               ← 尾部
Hitzone.TailTip            ← 尾尖（弱点）
Hitzone.LeftWing           ← 左翼
Hitzone.RightWing          ← 右翼
Hitzone.LeftClaw           ← 左爪
Hitzone.RightClaw          ← 右爪
Hitzone.LeftLeg            ← 左腿
Hitzone.RightLeg           ← 右腿

Hitzone子属性（每个部位碰撞体持有）：
  DefenseMultiplier (float)  ← 肉质（伤害吸收率，0.2=坚硬/1.0=弱点）
  StaggerRate (float)        ← 硬直肉质（破坏值吸收率）
  ExtractColorTag            ← 该怪物该部位的 Red/White/Orange；不由部位名称全局推断
```

> **碰撞通道说明：** 当前 `DefaultEngine.ini` 只有 `Weapon` 和 `MonsterAttack` 两条 Trace Channel。目标 Demo 固定新增 `Hitzone` Object Channel（`bTraceType=false`）：怪物实体 Body 负责物理阻挡，Hitzone 只做 Query；武器走 Weapon Trace，猎虫按 Hitzone Object Sweep。Aim 走 Visibility，WorldStatic/Hitzone Block、训练木桩 Body Ignore，命中后再验证 ObjectType=Hitzone，防止穿墙。详见 [冻结实施计划](demo-implementation-plan.md#35-hitzone武器与猎虫碰撞)。

## 伤害参数

```text
Damage.MotionValue / Damage.BaseStagger / Damage.DanceMultiplier
Damage.AttackPower / Damage.CritOverride / Damage.DisplayValue
```

## 输入

```
Input.Weapon               ← 武器输入父标签
Input.Weapon.Y             ← △/Y — 轻/基础攻击
Input.Weapon.B             ← ○/B — 重/特殊攻击
Input.Weapon.LT            ← 输出层预留：暂未使用（物理层为 Input.Modifier.LT）
Input.Weapon.RT            ← 收刀 RT 拔刀直飞（未来 RT 单键 Chord 输出；物理层为 Input.Modifier.RT）
Input.Weapon.RTA           ← RT+A（Chord Resolver）：起跳
Input.Weapon.RTB           ← RT+○（Chord Trigger）
Input.Weapon.RTY           ← RT+Y（Chord Trigger）
Input.Weapon.YB            ← Y+B / △+○（Chord Resolver）
Input.Weapon.LTY           ← LT+Y（持刀送虫/空中特殊派生）
Input.Weapon.LTB           ← LT+B（持刀召回/空中操虫斩）
Input.Weapon.LTRT          ← LT+RT（持刀发射虫印弹）
Input.Weapon.LTYB          ← LT+Y+B（地面粉尘集约/空中降龙）
Input.Weapon.RTYB          ← RT+Y+B（觉虫击）
Input.Modifier.LT          ← LT/L2 原始修饰输入，按姿态产生 Kinsect/Slinger 上下文
Input.Modifier.RT          ← RT/R2 原始修饰输入，按姿态产生 Action 上下文或拔刀直飞
Input.Modifier.Charging    ← RT 长按蓄力中
Input.Modifier.Sheathed    ← RB/R1 原始输入 Tag：持刀按下=纳刀；收刀按住=奔跑
Input.Sprint               ← RB/R1 — 收刀态按住奔跑（0.1s 阈值）
Input.Sheathe              ← RB/R1 持刀按下=纳刀（通用 GA 路由，同 Input.Dodge）
Input.Dodge                ← A/× — 闪避
Input.EdgeVault            ← 边缘跳越（由 EdgeVaultComponent 自动触发，不绑定按键）
Input.Interact             ← 交互
```

## 武器资源——虫棍

```
Weapon.InsectGlaive                    ← 虫棍武器类型
WeaponResource.IG.Extract.White       ← 白灯激活中
WeaponResource.IG.Extract.Orange      ← 橙灯激活中
WeaponResource.IG.Extract.Red         ← 红灯激活中
WeaponResource.IG.TripleUp            ← 三灯齐聚中
WeaponResource.IG.Kinsect.Active      ← 猎虫放出中
WeaponResource.IG.Mark.Active         ← 当前玩家存在有效虫印；运行时弱引用仍在 ResourceComponent
WeaponResource.IG.KinsectRegenRate    ← 猎虫耐力回复速率（词条用）
WeaponResource.IG.HoverDrainRate      ← 悬停耐力消耗倍率（词条用）
WeaponResource.IG.FlightDrainRate     ← 飞行耐力消耗倍率（词条用）
WeaponResource.IG.KinsectDrainRate    ← 猎虫耐力消耗速率（词条用）
WeaponResource.IG.TripleUpDuration    ← 三灯时长倍率（词条用）
WeaponResource.IG.ExtractDuration     ← 萃取时长倍率（词条用）

Cost.IG.TripleUp                     ← 觉虫击的离散武器成本类型；由 URes_InsectGlaive 解释
Data.IG.Buff.AttackMultiplier        ← 精华/三灯 GE 的 SetByCaller 倍率
Data.IG.Buff.MoveSpeedMultiplier     ← 精华/三灯 GE 的 SetByCaller 倍率
Data.IG.Buff.DefenseMultiplier       ← 精华/三灯 GE 的 SetByCaller 倍率
Data.Cost.Stamina                    ← 通用 Stamina Cost GE 的 SetByCaller 负值
```

## UI 相关

准心目标、颜色、猎虫耐力和舞踏层数通过组件 Delegate/Widget ViewModel 传递，不写 `UI.Aim.*` Loose Tag 到 ASC。ASC 只保存会影响玩法判定的 `Combat.State.Aiming.*` 与 `WeaponResource.IG.*`。

## 仓库分类标签页

| 标签页 | TagQuery | | 标签页 | TagQuery |
|--------|----------|-|--------|----------|
| 全部 | 无 | | 消耗品 | `Item.Type.Consumable` |
| 武器 | `Item.Type.Weapon` | | 材料 | `Item.Type.Material` |
| 衣服 | `Item.Type.Armor` | | 任务 | `Item.Type.Quest` |
| 饰品 | `Item.Type.Accessory` | | — | — |
