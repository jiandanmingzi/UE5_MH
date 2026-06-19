# GameplayTags 完整层级

## 物品类型
```
Item.Type.Weapon(.Sword/.Bow/.Staff/.Dagger)
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
Attribute.WeaponResource / MaxWeaponResource
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
Combat.Event.HitStagger    ← 受击硬直事件（ExecCalc→HandleGameplayEvent 触发）
Combat.Event.Death         ← 死亡事件（HP=0 → ExecCalc HandleGameplayEvent → GA_Death 激活）
Combat.Event.RequestSheatheAndUse ← 请求收刀并使用（QuickBarComponent 发出，GA_Sheathe 监听）
Combat.Event.SheatheComplete ← 收刀完成通知（GA_Sheathe EndAbility 广播，QuickBarComponent 订阅）
Combat.Event.ChargeReleased ← 蓄力释放事件（ASC::OnInputActionCompleted 检查 Charging Tag 后发送，蓄力 GA 通过 AbilityTrigger 监听）
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
Combat.State.Aerial.Falling.IG_JumpSlash     ← 跳跃斩后
Combat.State.Aerial.Falling.IG_PoleVault     ← 撑杆跳后
Combat.State.Aerial.Falling.IG_DanceJump     ← 舞踏后
Combat.State.Aerial.Falling.IG_KinsectSlide  ← 猎虫滑翔后
Combat.State.Aerial.Falling.IG_KinsectSlashHit← 操虫斩命中后
Combat.State.Aerial.CantDodge             ← 空中回避已用（默认无限制，舞踏重置清）
Combat.State.Aerial.CantAttack            ← 空中攻击已用（默认无限制，舞踏重置清）
Combat.State.Aerial.Landing               ← 落地瞬间过渡（AnimBP 内部用）
Combat.State.Sheathed      ← 拔刀前/纳刀态（GA_Sheathe/GA_Unsheathe 管理，非战斗默认持有）
Combat.State.Unsheathed    ← 拔刀后/持刀态（与 Sheathed 互斥）
Combat.State.Aiming        ← 瞄准态（LT Hold → GA_Aim 激活时持有，松开/被打断时移除）
Combat.State.ComboWindowOpen ← 连招输入窗口打开中
Combat.State.Dead          ← 死亡（HP=0 时添加，阻塞所有输入+伤害）
Combat.State.Attacking     ← 攻击中
Combat.State.DodgeAcceptOpen ← 翻滚接受窗口打开中
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
```

> **碰撞通道说明：** 项目需在 `DefaultEngine.ini` 中定义两个自定义碰撞通道——`Weapon`（玩家攻击检测）和 `MonsterAttack`（怪物攻击检测），两者均为 **Trace Channel**（仅用于 Sweep/Overlap 查询，不参与物理模拟）。Pawn 通道为 UE 默认 **Object Channel**（控制 CMC 推挤/穿透修正）。无敌帧期间仅切换 Weapon/MonsterAttack 通道响应，Pawn 始终 Block。

## 输入
```
Input.Weapon               ← 武器输入父标签
Input.Weapon.Y             ← △/Y — 轻/基础攻击
Input.Weapon.B             ← ○/B — 重/特殊攻击
Input.Weapon.RT            ← RT/R2 — 武器技能/防御
Input.Weapon.RTA           ← RT+△（Chord Trigger）
Input.Weapon.RTB           ← RT+○（Chord Trigger）
Input.Weapon.RTY           ← RT+Y（Chord Trigger）
Input.Weapon.YB            ← Y+B / △+○（Chord Trigger）— 超必/收尾技
Input.Modifier.Aiming      ← LT/L2 长按进入瞄准态
Input.Modifier.Charging    ← RT 长按蓄力中
Input.Modifier.Sheathed    ← RB/R1 按下进入纳刀态
Input.Sprint               ← LS/L3 — 奔跑
Input.Dodge                ← A/× — 闪避
Input.EdgeVault            ← 边缘跳越（由 EdgeVaultComponent 自动触发，不绑定按键）
Input.Interact             ← 交互
```

## 武器资源——虫棍
```
WeaponResource.IG.Extract.White       ← 白灯激活中
WeaponResource.IG.Extract.Yellow      ← 黄灯激活中
WeaponResource.IG.Extract.Red         ← 红灯激活中
WeaponResource.IG.TripleUp            ← 三灯齐聚中
WeaponResource.IG.Kinsect.Active      ← 猎虫放出中
WeaponResource.IG.KinsectRegenRate    ← 猎虫耐力回复速率（词条用）
WeaponResource.IG.KinsectDrainRate    ← 猎虫耐力消耗速率（词条用）
WeaponResource.IG.TripleUpDuration    ← 三灯时长倍率（词条用）
WeaponResource.IG.ExtractDuration     ← 萃取时长倍率（词条用）
```

## UI 相关
```
UI.Aim.Target.Monster          ← 准心对准怪物（任意部位）
UI.Aim.Target.World            ← 准心对准场景/空气
UI.Aim.Extract.Red             ← 准心对准红灯部位
UI.Aim.Extract.Yellow          ← 准心对准黄灯部位
UI.Aim.Extract.White           ← 准心对准白灯部位
```

## 仓库分类标签页

| 标签页 | TagQuery | | 标签页 | TagQuery |
|--------|----------|-|--------|----------|
| 全部 | 无 | | 消耗品 | `Item.Type.Consumable` |
| 武器 | `Item.Type.Weapon` | | 材料 | `Item.Type.Material` |
| 衣服 | `Item.Type.Armor` | | 任务 | `Item.Type.Quest` |
| 饰品 | `Item.Type.Accessory` |
