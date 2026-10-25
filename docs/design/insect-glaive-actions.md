# 虫棍 Demo 动作与连招设计

> **文档范围：** 本文是虫棍 Demo 的动作规则真相源，只详细描述位移明显、具有反击/猎虫/粉尘/精华等特殊效果的动作。普通《世界》地面动作的攻击帧、动作值和动画段继续配置在各自 GA/Montage 与虫棍 ComboData 中，不在本文逐招重复。

> **实施状态：** 本文描述的是目标设计，不表示代码或蓝图已经完成。实现状态以源码、Content 和 [编辑器搭建指南](../editor/demo-setup.md) 为准。

> **实现约束：** 输入快照、连招转移、RuntimeHost、Hitzone/猎虫碰撞、伤害上下文与位移所有权以 [Demo 冻结实施计划](demo-implementation-plan.md) 为准；在计划中的未决玩法语义确认前不开始对应代码阶段。

## 1. 目标与边界

虫棍以《怪物猎人：世界》的地面招式、基础猎虫操作和三色精华为骨架，吸收《怪物猎人：崛起》的四连印斩、突进回旋斩、操虫斩、强化操虫穿刺、猎虫滑翔、觉虫击与粉尘集约等动作，并按本项目规则重新组织输入和派生。

Demo 明确不实现：

- 翔虫槽、翔虫恢复和翔虫受身；移植的原翔虫动作不消耗翔虫。
- 集中模式及集中弱点攻击。
- 钩爪及软化系统。
- 跳跃突进斩（空中“电风扇”）。
- 强化跳跃斩与降龙（2026-10-01移出当前Demo/M5范围，不再作为动作需求或M5门禁）。
- 完整斩味、元素、异常和怪物生态；Demo 伤害沿用简化公式。

**2026-10-06 当前M5状态：** 空回、操虫斩、强化操虫穿刺（平地范围）与急袭突刺均已按各自范围签收，M5总验收也已关闭。真实落地清舞踏层数/来源/镜像 Tag；急袭突刺 Commit 先快照倍率再清层。收刀、空中受击和死亡不属于 Demo/M5 范围。完整验收记录见[M5总计划§10](../archive/demo-2026-10-13/m5-plan.md#10-阶段-f--m5-总验收与签核)；急袭突刺本招范围见[独立计划](../archive/demo-2026-10-13/急袭突刺实现计划.md)。

## 2. 通用武器架构约束

虫棍规则不能写进通用 GA 或连招协调器的武器类型分支。通用层只提供：

- `UMHGZGameplayAbility`：激活、Commit、取消、输入释放和通用标签生命周期。
- `UMHGZAttackAbility`：Montage、攻击窗口、命中上下文和方向修正。
- `UMHGZWeaponComboData`：数据驱动的状态转移。
- 通用位移任务：定向位移、受限冲刺、弹跳、惯性继承和落地回收。
- 武器资源查询接口：GA 通过接口取得当前武器资源，不在基类 Cast 到虫棍类型。

虫棍派生层负责：精华、猎虫、粉尘、舞踏、虫印和本文所有动作特例。以后添加其他武器时，只新增该武器的 ComboData、派生 Ability 和资源组件，不修改协调器的匹配代码。

## 3. 输入与方向判定

### 3.1 输入语义

| 状态 | 输入 | 动作 |
|---|---|---|
| 收刀 | `Y`（无、左、右、后输入） | 仅拔刀，不攻击 |
| 收刀 | `前+Y` | 拔刀攻击 |
| 收刀 | `RT` | 拔刀并让猎虫沿角色正前方飞出 |
| 收刀 | `LT` | 投射物瞄准；Demo 暂无消费方 |
| 收刀 | `RB` 按住 ≥0.1s | 奔跑；点按不闪跑 |
| 持刀地面 | `RB` | 纳刀（按下立即触发；空中不派发；攻击/硬直/击倒/死亡/动作锁中无效） |
| 持刀地面 | `LT` | 猎虫瞄准，显示猎虫准心 |
| 持刀地面 | `Y+B` | 四连印斩 |
| 持刀地面 | `前+Y+B` | 突进回旋斩；方向节点优先于无方向四连印斩 |
| 持刀地面 | `LT+Y+B` | 猎虫飞向准心，吸粉刷新寿命，1s后两段粉尘聚爆；操虫/收虫可取消 |
| 持刀地面 | 按住 `RT` 后松开（期间未按 A/B/Y） | 虫印斩：近战命中后建立/替换虫印 |
| 持刀地面 | `LT+RT` | 发射虫印弹，命中 Hitzone 后建立/替换虫印 |
| 持刀地面 | `RT+Y` | 猎虫滑翔 |
| 持刀地面 | `RT+Y+B` | 觉虫击 |
| 空中 | `B` | 操虫斩，沿角色面朝方向 |
| 空中 | `RT+B` | 操虫斩，沿猎虫准心方向 |
| 操虫斩触发舞踏后的 Falling 可出手阶段 | `RT+Y` | 强化操虫穿刺 |
| 持刀、Falling且空中攻击预算可用 | `RT` 限定时间内松开，未被组合消费 | 《世界》急袭突刺；条件同当前操虫斩，按下不立即触发 |

`LT` 是持刀状态的猎虫瞄准；`RT` 是动作瞄准/特殊动作修饰键。两者是不同的输入上下文，不能共用一个 `Combat.State.Aiming` 标签。建议使用：

```text
Combat.State.Aiming.Kinsect   // 持刀 LT
Combat.State.Aiming.Action    // 持刀 RT 特殊动作上下文
Combat.State.Aiming.Slinger   // 收刀 LT，Demo 无消费方
```

`Input.Weapon.RT` 可以由三个互斥的 Chord 输出：收刀地面 `Sheathed+Grounded` 与空中 `Aerial` 路线在各自 0.05 秒 Chord 窗口结束后派发；持刀地面 `Unsheathed+Grounded` 路线只在 RT 未被组合消费、且物理按住时长不超过 0.25 秒时于松开派发虫印斩。每个包含物理 RT 的 Chord 都单独设置 `ChordGracePeriodOverrideSeconds=0.05`，不改变 Y+B 等其他组合的全局宽限期。虫印斩使用 `OnReleaseWithinMaxDurationIfUnconsumed`；不再使用无限时长的 `OnReleaseIfUnconsumed`。超时长按松开不产生虫印斩输入。

#### 收刀 Y 的拔刀分流与姿态 Commit

InputProfile 只配置一条普通 Y Chord，输出 `Input.Weapon.Y`；它不需要也不能在输入层创建“前+Y”的第二条 Chord。Router 将方向与按键一起冻结，Coordinator 再匹配两条 `DA_IG_Combo` 转移：

| 转移 | 条件 | GA | 结果 |
|---|---|---|---|
| `Idle → DrawOnly` | `Input.Weapon.Y`、`Direction=None`、`Grounded+Sheathed` | `GA_IG_Draw` | `None` 表示不要求方向，因而覆盖无、左、右、后输入；只播放拔刀，不配置 AttackSegment。 |
| `Idle → GroundStarter` | `Input.Weapon.Y`、`Direction=Forward`、`Grounded+Sheathed` | `GA_IG_BaDaoZhan` | 具体方向优先于 None；播放拔刀攻击，AttackSegment 按普通攻击配置。 |

`GA_IG_Draw` 与 `GA_IG_BaDaoZhan` 都继承 `UMHGZDrawAttackAbility`（M4-A.4），并使用 Montage 中原生 `AnimNotify_DrawCommit`。Notify 只负责以 Mesh+MontageInstanceID 找到当前 ActionToken 并转发；Ability 才调用 `RuntimeHost->SetSheathed(false)` 与清 `bSprintHeld`。因此 Commit 前取消仍是 Sheathed，Commit 后取消仍是 Unsheathed，不能在 Blueprint Event Graph 或 Ability 激活时提前写姿态。收刀 RT 的 `GA_IG_DrawAndSendKinsect` 也复用这一视觉 Commit 合同，但它直接继承 `UMHGZGameplayAbility`：它使用无 Root Motion 的 `UpperBody_IGAction` `ActionMontage`，不获得 `Attacking`、`BlockMovement` 或 MontageRootMotionOwner；`DrawCommit` 只切姿态，后续 `KinsectSendCommit` 才部署冻结的猎虫请求。

持刀 `LT+Y` 送虫和 `RT+B` 收虫同样需要角色动作，但它们不是近战攻击。M4-A.5 直接扩展现有的 `UMHGZSendKinsectAbility`、`UMHGZRecallKinsectAbility`：各自新增 `ActionMontage`，在 `AnimNotify_KinsectSendCommit`、`AnimNotify_KinsectRecallCommit` 才调用 Resource；不新建通用 Montage GA 父类。`AM_IG_SendKinsect` 也可以由收刀 RT 的 `UMHGZDrawAndSendKinsectAbility` 复用：同一 Montage 同时存在 `DrawCommit` 和 `KinsectSendCommit`，两个 Notify 都按精确 ActionToken 仅路由给匹配的 Ability 类型。共享资产中 DrawCommit 必须至少早于 KinsectSendCommit 一帧，保证 DrawAndSend 先提交姿态再允许出虫。三条路径仍使用 `UpperBody_IGAction` Slot、in-place 且无 Root Motion；AnimGraph 根据角色原始摇杆输入选择呈现范围：无移动输入时，Slot 动作全身覆盖；有移动输入时，以当前 MM 姿势为底，仅从 `Spine_00`（含其后代）混入 Slot 动作，让下半身继续由 MM 移动/转向。该选择只改变姿势呈现，不改变 Montage、GA、Commit 或输入逻辑。三种路径都不使用 `UMHGZAttackAbility`、`AttackSegment`、`AttackCollision`、`Attacking`、`BlockMovement`、攻击方向修正或 MontageRootMotionOwner；Notify 前中断不改变猎虫状态，Notify 后中断不撤销已开始的 Flight/Return。

### 3.2 虫棍连招到拆分动画片段的数据流（M4-B.1 起）

通用规则与完整时序见[动作系统设计](actions.md)中的“目标 GA 的 Montage 入口与片段数据流”。虫棍只在该通用链上填数据，不能在 Coordinator 中增加 `if InsectGlaive`、也不能让 ComboData 直接引用动画资源。

```text
当前虫棍招 A 的 ComboWindow
  → 输入快照
  → DA_IG_Combo: TransitionID=IG.A.To.B, A → B, AbilityClass=GA_IG_B
  → Coordinator 冻结 { TransitionID, SourceState=A, TargetState=B, InputSnapshot }
  → GA_IG_B Commit + Confirm
  → GA_IG_B 的 EntrySectionByTransitionID 选择 Entry_From_A
  → AM_IG_B: Entry_From_A → Core → Recovery_Common
  → A 以 Superseded 结束；未继续派生时 B 自然结束并回 Idle
```

例如某个二段斩的目标 GA 使用一个 `AM_IG_Slash2`：`Entry_From_Idle`、`Entry_From_Slash1` 都在 Montage 的 Next Section 中连接到 `Core`，而 `Core` 再连接 `Recovery_Common`。若 `Slash1 → Slash2` 有专门衔接序列，将该序列放在 **Slash2 的** `Entry_From_Slash1`；`Slash1` 本身的 Recovery 仍是“未输入下一招时”的收招。多个动作共有的后摇可让各 Montage 引用同一 AnimSequence，不必复制烘焙资产。

源序列被解包得零散不是问题：把“起手/衔接”“招式本体”“共用后摇”“仅给 MM 过渡的尾姿”按语义归类，再组进目标招式 Montage。Section 本身不能启用/关闭 Root Motion：真实有根位移的片段由 M4-B.1 的 `ActionRootMotionPhase` 精确取得所有权；in-place 片段不取得；看似有位移但根骨骼没有位移的空中片段由专用 MovementTask 驱动。不要仅为让一段连接动画播放而新建无碰撞的“中转 GA”。

M4-B.1 原生字段未编译前，可以先在编辑器建立这些 Section 和 Next Section 链；但不得填写不存在的入口映射、更不得通过 Event Graph `Jump to Section` 伪造该运行时选择。

### 3.3 组合键与匹配优先级

`Y+B`、`LT+Y+B`、`RT+Y+B` 必须在输入层形成稳定的组合输入事件，不能依赖两个普通 Ability 在同一帧竞争激活。组合键允许一个可配置的 Chord Grace Period；在该时间内组成组合键后，应抑制对应的单键攻击。

同一帧有多个候选动作时按以下顺序选择：

1. InputProfile 先按 RequiredHeldModifiers 数量、TriggerControls 数量和 Chord Priority 解析最具体且修饰键完全匹配的组合 Tag，例如 LT 可提前长按，随后 Y+B 在 GracePeriod 内组成 `LTYB` 并阻止普通 `YB`。
2. 协调器按地面/空中、收刀/持刀等互斥状态筛选完全匹配的 InputTag。
3. 具体方向条件优先于 Direction=None。
4. ComboData 的显式 Priority。
5. 仍相同时视为数据错误，由 Data Validation 阻止运行。

### 3.4 “前+Y+B”的前方定义

“前”相对角色朝向，不直接相对摄像机屏幕方向。移动输入先由 PlayerController 转换为水平世界方向，再与角色水平 Forward 比较：

```text
InputMagnitude >= DirectionInputThreshold
Dot(NormalizedInputWorld, CharacterForward2D)
    >= cos(ForwardConeHalfAngle)
```

例如角色面朝摄像机画面左侧时，摇杆向左产生的世界移动方向与角色 Forward 一致，因此触发 `前+Y+B`。方向、按键和修饰键必须在输入事件发生时一起快照，不能等 GA 激活后重新读取可能已经变化的摇杆。

## 4. 虫棍专属战斗配置

新增 `UInsectGlaiveCombatConfig : UPrimaryDataAsset`，由虫棍 WeaponDefinition 软引用。它是同一把虫棍动作规则与可调数值的唯一入口，不放入全局 GameInstance 设置，也不扩展通用武器基类字段。

### 4.1 红灯动作模式

```cpp
UENUM(BlueprintType)
enum class EIGRedExtractMode : uint8
{
    ClassicMovesetGate, // 默认：无红灯使用弱化动作组，有红灯使用完整动作组
    NumericOnly         // 红灯不改变动作，只保留可配置数值 Buff
};
```

`RedExtractMode` 默认 `ClassicMovesetGate`。Demo 只创建一个 `DA_IG_Combat` 和一个 `DA_IG_Combo`。RuntimeHost 根据模式拥有且只拥有一个配置 Tag：

```text
Combat.Config.IG.RedMode.Classic
Combat.Config.IG.RedMode.Numeric
```

同一张 ComboData 用 Required/BlockedTags 表达两种模式；所有 AbilityClass 在装备时只授予一次。RuntimeHost 的 `ActiveRedExtractMode` 初始化自 CombatConfig 默认值；调试切换只改这个运行时覆写，不修改共享 DataAsset，并且只允许在非攻击状态原子替换模式 Tag、`ResetCombo(ConfigChanged)`。

- `ClassicMovesetGate`：转移要求 `RedMode.Classic`；无红灯行 Block `Combat.Branch.Extract.Red` 并进入弱化动作，有红灯行 Require 红灯并进入正常动作。
- `NumericOnly`：正常动作行只要求 `RedMode.Numeric`，不检查红灯 Tag；有灯和无灯使用同一套动作，红灯只通过 GE 修改蓝图可调数值。
- 白灯、橙灯和三灯在两种模式中的动作与数值语义完全一致。

### 4.2 位移与舞踏参数

`DirectionInputThreshold`、`ForwardConeHalfAngle` 和 `ChordGracePeriod` 不属于虫棍 CombatConfig；它们由 `DA_WeaponRuntime_IG` 引用的通用 `UWeaponInputProfile` 唯一提供。两个红灯模式共用同一个 InputProfile，避免切换数值/动作模式时改变按键手感。

CombatConfig 至少暴露：

```text
RedExtractMode
ComboData
White/Red/Orange/TripleUpEffectClass
WhiteExtractDuration / RedExtractDuration / OrangeExtractDuration / TripleUpDuration
WhiteMoveSpeedMultiplier
RedAttackMultiplier
OrangeDefenseMultiplier
TripleAttack/MoveSpeed/DefenseMultipliers
SendKinsectMotionValue / DrawSendKinsectMotionValue
AwakenedKinsectMotionValue = 15 / AwakenedRedKinsectMotionValue = 30
AwakenedTripleKinsectMotionValue = 50 / AwakenedPierceHitInterval
AwakenedAimHorizontalCorrectionAngle = 90°
AwakenedAimCorrectionAngle = 60° pitch
MaxDanceStacks = 3
KinsectSlashDanceDamageMultipliers[]       // 0/1/2/3层：1/1.1/1.2/1.3
DescendingThrustDanceDamageMultipliers[]   // 0/1/2/3层：1/1.1/1.3/1.5
KinsectPiercerDanceDamageMultipliers[]     // 0/1/2/3层：1/1.3/1.5/2.0
KinsectSlashMaxDistance
KinsectGlideMarkMaxDistance
KinsectMarkMaxDistance
KinsectMarkDuration = 90s
KinsectMarkProjectileSpeed/Radius/Lifetime
KinsectGlideFallbackDistance
KinsectGlideFallbackLiftHeight
AwakenedHunterFlightMaxDistance
AwakenedHunterFlightStartTime
DescendingThrustAirControl
// 以下为M6.3目标字段，完整类型/迁移表见M6计划§6.7
KinsectAutoMarkSearchRadius / KinsectAutoMarkAttackInterval
MarkedHitPowderCooldown = 10s
PowderLifetime = 180s / PowderMaxCount = 100
PowderDespawnDistance / PowderMotionValue（原始百分数；默认100，伤害=猎虫攻击力×动作值/100）
PowderNaturalExplosionRadius = 150cm
PowderGatherRadius = ArenaPlayableSideLength / 2
PowderVortexDelay = 1s / PowderVortexExplosionRadius = 300cm
PowderVortexDamageAt1 / PowderVortexDamageAt5 / PowderVortexDamageAt10
```

精华 GE 负责固定 GrantedTags/Modifier 形态，持续时间和倍率由 CombatConfig 通过 Spec/SetByCaller 注入；不在 GE 默认值和 CombatConfig 各保留一份真相。位移距离、高度、持续时间和增伤值也都来自该资产；动作状态机不得使用散落在 GA 蓝图中的同名常量。Data Validation 必须检查距离/时长为正、修正角在 `[0°, 180°]`，三招舞踏数组各有 `MaxDanceStacks + 1` 项、0层为1、全部为有限且不小于1的数值。InputProfile 另行验证 GracePeriod、方向阈值和 Chord 冲突。

旧`PowderGatherDuration=0.5`、`PowderGatherMaxCount=5`和单基础伤害字段仅为待迁移源码基底，不再用作新合同配置。未给出的频率、距离和伤害数值保持待定；M6计划§6.7区分已存在字段与目标字段。

## 5. 地面新增动作

**Idle方向路由（2026-10-10当前接线）：** Idle的RT撑杆跳只保留一条无方向限制的`NoneRTA_QianChengGanTiao`边，仍使用前撑杆跳GA/动画/曲线，最大朝向修正为180°（覆盖完整360°输入方向）；Idle不再有独立的左、右、后撑杆跳边，AnyState左/右/后撑杆跳边也排除Idle。Idle的方向Y突刺显式配置前/后/左/右四条边，统一使用突刺GA并允许180°朝向修正；无方向Y仍走上捞斩。此处`Direction=None`表示方向通配，不表示摇杆中立。已做资产磁盘回读，PIE行为尚待验证。

### 5.1 四连印斩——`Y+B`

- 可以从 Idle 起手。
- 可以在《世界》地面动作明确开放的 ComboWindow 内派生，但飞圆斩除外；“任意招式派生”不表示可在任意动画帧无条件打断。
- 四连印斩结束进入 `IG.Ground.StarterOnly`，不能直接接中段或终结动作。
- `IG.Ground.StarterOnly` 只允许派生：突刺、上捞斩、横扫、飞身跃入斩。
- 四段攻击各自拥有 AttackSegment/ConfigIndex；相邻攻击窗口可以重叠，但必须独立去重。
- 第2个伤害段（`AttackSegments[1]`、约1.033～1.283s）成功结算武器伤害后施加虫印，每次动作最多成功建印一次；第1/3/4段和伤害失败不刷新。这里的“第二下”按GA伤害段编号，不是时间顺序第二个碰撞窗口（后者仍属ConfigIndex 0），不改原窗口。
- 动作本身不产生舞踏层数。

### 5.2 突进回旋斩——`前+Y+B`

- 起手与派生范围和四连印斩一致：Idle 起手边不要求窗口，其他动作派生边要求 ComboWindowOpen 并排除飞圆斩；方向条件匹配时优先于无方向 `Y+B`。
- 激活时快照输入世界方向，并在允许修正角内调整动作朝向。
- Montage 中以 `AnimNotifyState_IG_AdvancingCounter` 标记反击窗口；Notify 先由 Mesh+MontageInstanceID 解析本 ActionToken，Begin 向 Character IncomingHitResolver 注册绑定该 ActionToken 的拦截 Token，End/Cancel 幂等注销。
- 反击窗口内收到可反击的怪物攻击时，该次攻击不结算玩家伤害和硬直，立即停止剩余地面段，触发舞踏弹跳并增加一层舞踏。
- 同一激活实例只允许成功反击一次；环境伤害、持续地面伤害以及标记为不可反击的攻击不能触发。
- 未触发反击时，动作结束与四连印斩相同：进入 `IG.Ground.StarterOnly`。

反击不是普通无敌帧。判定必须由 IncomingHitResolver 消费带攻击实例 ID 的 IncomingHitContext，避免同一个多段攻击在反击成功后再次命中；窗口外按正常受击规则处理。

### 5.3 虫粉与粉尘聚爆——地面 `LT+Y+B`

**2026-10-08目标规则，尚未实现闭环。** 虫印/虫粉的实际生成手段、自动寻印和新版两段聚爆属于M6.3，旧单段聚爆验证不签收本节。详细数据、指令寿命、改动文件和验收见[M6计划§6](../archive/demo-2026-10-13/m6-plan.md)。

**虫粉生成和存在：**

- 一种红色、不发光的虫粉：不规则但接近直径15cm球状的粒子团，无碰撞/物理。各团属于生成它的玩家虫棍Runtime。
- 玩家操虫、自动虫印攻击、操虫斩等招式中的猎虫判定，只要成功伤害命中有效虫印的**局部覆盖**，进入产粉判定；这一来源每玩家Runtime共享10s冷却，首击可立即产粉，新虫印不会重置冷却。
- 觉虫击本次消耗黄灯（含完整三灯）时，每次通过既有命中/间隔验证的成功猎虫伤害产一团，单击和贯通均适用；不要求虫印且绕过10s产粉冷却。无黄灯保留普通虫印产粉。同一次Hit只生成一团，不能叠加两种来源。
- 寿命180s，上限暂定100团，预留团也计数；超上限计划拒绝新团。超过可配置的玩家距离阈值时只消失，不爆炸。
- 180s自然到期在当时位置产生一次**半径150cm（1.5m）球形伤害**，同一怪物多部位只一笔。单团伤害为生成时猎虫攻击力快照乘配置系数，不受肉质、猎人攻击力/灯/舞踏/暴击影响；当前倍率默认1并可在DA调节。
- 武器/猎虫直接接触虫粉不引爆；自然到期或聚爆各自消费一次。爆炸不再产粉、不点灯。

**粉尘聚爆：**

1. 持刀地面 `LT+Y+B`，用组合解析时冻结的Kinsect AimSnapshot，让猎虫飞向准心目标地点/部位；怪物目标保存组件/骨骼局部点，场景/空气保存世界目标点。
2. 猎虫到达后，选入中心球内全部本玩家Available虫粉，预留并刷新寿命至当前时间+180s。收集球**直径=Demo训练场可走地面的边长**，半径为其一半；不再使用猎虫原位置、600cm或最近5团。
3. 到达并开始吸粉后1s内将成员表现聚向目标，截止引爆；计划计时起点为到达/开始聚拢，旅途另计。这个1s内操虫或收虫会取消待引爆，立即执行新指令。抵达前也可取消；自动虫印行为不能抢占。
4. 取消时保留已发生的聚拢位置和寿命刷新、释放Available，不使粉尘消失/复活/瞬移回出生点。猎人Montage正常播完不等于取消已提交的猎虫事务。
5. 成功消费N团后产生**两段独立伤害，两段均为半径300cm（3m）球**。第一段仅N>0：1..4团取DamageAt1、5..9团取DamageAt5、≥10团取DamageAt10；默认10/50/100，阶梯跳变、不插值。第二段为所消费各团单独固定伤害之和ΣDi。
6. 两段共享本次ExplosionID，使用独立StageAttackID；每段对一个怪物只一笔，不能合成单笔或对每部位重复。两段共用半径300cm（3m）的聚爆球；第二段一次结算ΣDi，不调用N次半径150cm自然爆炸。
7. 0团不产生两段伤害。自然到期/聚爆/远距删除/运行期关闭必须原子互斥；旧寿命Timer或重复回调不能再次引爆。

### 5.4 局部虫印——虫印斩与急袭突刺地面第一击

本轮必做来源为既有 `GA_IG_ChongYin` 与 `GA_IG_JiXiTuCi`：

- **地面虫印斩：** 仍用项目已有单RT短按释放输入语义；指定MarkSegment首次成功武器伤害在真实ImpactPoint建立/替换虫印。
- **急袭突刺：** 空中单RT进入既有动作链；仅地面攻击第一下的指定武器段成功伤害建立虫印，空中下坠/其他伤害段不建立。实际ConfigIndex在实施前从已配置窗口审计，不猜索引。
- 两种动作一次Action各最多成功建印一次；空挥、伤害GE失败或无效Hitzone保留旧印。
- 每玩家至多一个局部虫印，持续90s；普通玩法只因怪物死亡、时间结束或成功产生新印而消失。收虫、收刀、操虫、离开寻印范围不会清印或刷新时间。
- 权威位置为目标组件/骨骼下的局部命中点及法线，随怪物移动；视觉为淡黄色、发光、不规则雾状粒子团，覆盖约20cm×20cm局部表面，不能把整部位标为都可产粉。
- Resource保存弱引用、MarkID/Serial和ExpireAt；`WeaponResource.IG.Mark.Active`只作镜像。目标销毁/Runtime结束的技术清理继续有效，不扩展Demo范围外的玩家死亡玩法。
- 猎虫离手、有配置范围内虫印、没有玩家指令/ActionAttached/Returning占用时，自动飞向虫印并以配置频率进行真实部位攻击。每次攻击参加产粉资格检查，虫印来源实际受10s产粉CD限制。
- 玩家操虫、收虫、操虫斩、觉虫击、聚爆等优先于自动虫印；原指令真正执行完成且猎虫仍离手时才重新评估虫印。回手后不因虫印自动离手；旧自动回调必须按CommandSerial拒绝。
- 旧LT+RT虫印弹接口兼容保留，不新增为本轮必做来源、不代替两个近战来源验收。其现有输入/投射物不在本次文档修订中删除；如仍启用，建立的印也必须共用90s局部虫印合同。

### 5.5 猎虫滑翔——地面 `RT+Y`

激活时只选择一次目的地：

- 当前存在有效虫印，且虫印距离不超过 `KinsectGlideMarkMaxDistance`：飞向虫印位置。
- 否则：沿角色 Forward 飞行 `KinsectGlideFallbackDistance`，并增加少量 `KinsectGlideFallbackLiftHeight`。

移动必须做胶囊 Sweep，并以最大距离、最大时长和阻挡碰撞三重结束。命中有效怪物 Hitzone 后停止水平冲刺，执行一次小幅弹跳并进入空中状态；该弹跳不增加舞踏层数。未命中则在位移结束后按当前高度进入 Falling，不能强制吸附到目标。

### 5.6 觉虫击——地面 `RT+Y+B`

猎虫可用即可发动；成功发动消耗当时的全部猎人灯，各颜色效果独立叠加。黄灯在代码中对应既有`Extract.Orange`，不新增第四种精华。

1. **2026-10-17规则：按本次实际消耗颜色冻结效果。** 无红灯时猎虫每次成功命中的动作值为15；有红灯且非三灯为30；完整三灯为50，不与红灯档叠乘。白灯令猎虫贯通／逐击采集；黄灯令每次成功伤害都产粉。零灯、仅白、仅黄、白黄的动作值仍为15；红白、红黄为30；三灯为50并具备全部效果。TripleUp仍在Commit阶段原子预留/消费；一灯/两灯在动作确认成功、Montage前由Resource移除，并将真正移除的颜色交回GA。预检/Commit失败不扣单灯，Triple失败不静默降级；消耗不清猎虫尚未交付的携带精华。蓄力、松键及丢虫不再重读后来获得的灯。
2. RTYB激活时立即读取输入快照的Action相机准心方向进行入口Yaw修正，GA `MaxCorrectionAngle=90°`（Combo边`-1`继承GA值）；不读左摇杆。Y/B全部松开时同步重采样相机准心并冻结当时ActorForward，以释放帧Forward为基准限制水平Yaw `±90°`、俯仰`±60°`，供猎虫和猎人199前冲共用。角色不在松键时立即转向，而由`AM_IG_JueChongJi`第230帧`ReleaseFacingCorrection`点通知启动共享方向任务，在`0.1s EaseOut`内转到锁定Yaw；后续不追踪准心。
3. 消耗白灯（含三灯）时猎虫沿修正后的方向贯通飞行，同一Flight对同一Hitzone的重复命中受`AwakenedPierceHitInterval`限制；无白灯时保持单次命中／单次取精华。**2026-10-25：首次成功伤害后，每只怪物至少结算6次**，由`GA_IG_JueChongJi → 招式参数 → 觉虫击 → 猎虫伤害 → AwakenedKinsectMinimumHitsPerTarget`配置。按本次SourceAttackID＋目标Actor计数，正常贯通已经打出的次数计入保底，不按每个部位分别补6次；超过6次的正常接触仍可继续伤害。补击沿用原间隔和最后成功命中的真实部位，随该部位移动；猎虫穿出、正常飞完或召回/回手后仍补足余数，不改变飞行/召回时机。各次均走原伤害GE、独立HitID及本次产粉策略，猎虫AP在首击冻结。实际接触仍按真实部位采集并正常回手交付；补击重复已采集部位，不虚构新颜色或在交付后重新填充携带UI。目标死亡/部位失效、伤害结算失败、源死亡或Runtime卸装/切换时清理剩余结算。普通操虫、无白灯觉虫击和急袭突刺不启用保底。贯通与三灯的飞行速度、终点召回策略彼此独立。
4. 消耗黄灯（含三灯）时，每个通过Hitzone/间隔验证且成功伤害的Hit产一团虫粉；使用独立的`AwakenedEachHit`策略，不要求虫印、不受普通10s冷却，单击和贯通均适用，同HitID最多一团。无黄灯仍保留§5.3普通MarkedContact产粉规则。白灯贯通的成功伤害把颜色加入猎虫携带容器（红白橙去重，不覆盖前色），回手后才逐色点灯；无白灯仍只携带一种。黄灯不改变粉尘本身的伤害。伤害失败不产粉；贯通伤害失败也不采集、不启动该部位的重复命中间隔。
5. `KinsectLaunch`与`HunterFlightStart`是常驻`AM_IG_JueChongJi`中的两处精确事件点。历史只读审计触发时刻为`3.95804286s`、`4.84202242s`，间隔`0.88397956s`；运行仍读取当前资产的实际通知间隔。本轮不改通知或飞行参数：非三灯固定`2074.573cm/s`、三灯固定`3111.8595cm/s`（1.5倍），用速度乘通知间隔派生终点距离，不按剩余起飞时间动态计算速度、不叠加怪物体长。非三灯无白灯的单击命中后悬停；含白灯的贯通命中继续飞行，直到碰地/世界阻挡或距离终点才悬停；均在猎人199冲刺结束（自然完成、落地或中断）时由GA召回。三灯仍保留请求自身终点回收。颜色效果不改变猎人199位移、伤害、舞踏或既有瞄准修正。
6. `HunterFlightStart`点通知负责启动199位移；GA还监听本次Montage实例进入`HunterFlight199`的SectionChanged并调用同一个幂等入口兜底，避免单个Notify漏触发导致整段无位移。猎人位移结束后根据高度进入空中或落地状态；碰墙、受击、死亡或换武器必须同时取消猎人位移，猎虫按自己的召回规则结束。
7. 猎人199冲刺的武器伤害段（`AttackSegments[0]`）成功结算后施加虫印，再排队进入既有189舞踏；普通与三灯分支均适用。猎虫自身飞行/贯通伤害不因此建印，起飞前、无效部位或伤害失败不建印；舞踏接管已排队后不重复刷新。使用同一个Resource局部建印入口，不改伤害窗、冲刺曲线、三灯点灯/产粉或舞踏规则。

三灯消耗必须在资源组件内作为一个原子操作完成，不能依次移除三个 GE 后再判断状态。

> **2026-10-17现行边界：** RTYB无三灯也能发动；白灯贯通、黄灯逐击产粉不再要求完整三灯。新采集的精华只能在猎虫回手交付后形成猎人灯，交付时已有三灯仍吞灯不刷新。采集与产粉沿用成功HitID；普通操虫和操虫斩的单色容量不扩展。自动寻印不得抢占觉虫击请求或其召回。旧`AwakenedPiercing`枚举值与请求校验保留供旧数据/调用兼容；当前觉虫击黄灯使用新增`AwakenedEachHit`，不能再把产粉权限硬绑到贯通／自动回手。

193 动画按住/松开规则：起手后播放序列后半段作为蓄力循环；RTYB 的 Y+B 控制保持按下时循环，最多播放3遍，第三遍结束后自动进入195；全部松开仍可提前退出。循环期间不结束 GA、不取消猎虫，也不再触发单独 RT 输入。

## 6. 舞踏与空中动作

### 6.1 舞踏层数

以下事件增加舞踏层数并触发舞踏弹跳：

- 操虫斩冲刺武器段 `[1]` 成功结算伤害。
- 突进回旋斩反击成功。
- 觉虫击猎人冲刺武器段 `[0]` 成功结算伤害；猎虫贯通本身不增加。

每次成功后：

```text
DanceStacks = min(DanceStacks + 1, MaxDanceStacks)
DamageMultiplier = 本次招式的专属倍率数组[出招时DanceStacks]
```

**2026-10-17 当前增伤合同：** 最多3层，0层均为1倍；只有下列三招消费增伤倍率，不给觉虫击或其他招式隐式加成。

| 招式 | 1层 | 2层 | 3层 | 作用范围 |
| --- | --- | --- | --- | --- |
| 操虫斩 | 1.1 | 1.2 | 1.3 | 两个武器段 `[1]/[2]`；猎虫段 `[0]` 不加成 |
| 急袭突刺 | 1.1 | 1.3 | 1.5 | 四段武器伤害，包含381地面段 |
| 强化操虫穿刺 | 1.3 | 1.5 | 2.0 | 整个GA的全部武器伤害段，包含203触地后的地面段 |

三招均在本次激活时快照对应倍率；操虫斩本次命中获得的新层数只加强后续出招，不反向加强本次189的后续伤害。创建各段伤害Spec时将配置动作值乘该快照，原`动作值/100`和`肉质/100`公式保留、不重复叠乘。加成只影响武器物理通道，不影响属性、猎虫独立伤害、粉尘爆炸或环境伤害。通用`DanceDamageMultipliers`仅作旧资产加载兼容，不再读取；默认/未指定招式查询返回1。

当前 Demo 中，真实落地统一清空舞踏；急袭突刺 Commit 先把当前倍率快照进本次攻击，再清空层数，保证本次终结动作能使用已积累增伤。收刀、空中受击和死亡不属于当前 Demo 的舞踏战斗规则/验收范围；卸装与 RuntimeShutdown 仍可按资源生命周期合同清理状态。

### 6.2 操虫斩——空中 `B` / `RT+B`

- `RT+B`：输入只冻结瞄准路径；到 186 的 AimLock 帧才采样当前准心并冻结三维前冲方向。
- 直接 `B`：按角色面朝方向飞行。
- 两种路径使用同一 GA 和位移参数，只改变 AimSource。
- 方向受 `KinsectSlashMaxDistance` 和碰撞 Sweep 限制，不追踪激活后移动的准心或目标。
- 本招有三个独立伤害机会，均由 GA 的 `Attack Collision` 窗口结算：`[0]` 猎虫附着点判定、`[1]` 棍尾至Root的第一下武器判定、`[2]` Root至棍尖的第二下武器判定。三个判定各有一次伤害额度，不能用单个 Segment 的 `MultiHitCount=3` 代替；旧独立Probe不参与生产判定。
- **仅猎虫段[0]真实伤害GE成功提交**到有效Hitzone后，按部位颜色写入真实猎虫的单精华携带槽，立即显示携带椭圆；返回猎人时共用普通操虫交付入口点灯。2026-10-12用户要求复用普通操虫链路，此条覆盖此前“命中即时ApplyExtract”的合同，不扩展多精华容量。武器两次命中只造成武器伤害、不采集。
- 冲刺武器段`[1]`成功结算伤害后触发舞踏、增加层数，并将 Resource 的 `DanceSource` 写为 `KinsectSlash`；猎虫段`[0]`只独立伤害/采集，189武器段`[2]`不重复触发舞踏。仅虫命中或完全未命中时，按现行188运动进入Falling。对应来源标签由 `URes_InsectGlaive::RefreshDanceStateTags()` 在 `AddDanceStack`/`ClearDanceStacks` 广播前镜像到 ASC；真值始终是 Resource 的 `DanceSource` 字段，标签只是它的对外广播。
- 空中起手攻击 GA（操虫斩、强化操虫穿刺、急袭突刺）在确认激活后同时消费 Host 所有的 `Combat.State.Aerial.CantAttack` 与 `Combat.State.Aerial.CantDodge`。因此操虫斩未命中进入 188 下坠后，即使 `Falling` 仍在场，空回也会被 `CantDodge` 阻止；GA 结束不退款，真实落地清理，成功进入舞踏时重置。
- 运动使用本招专用档。**现行范围为不减速版**：188继承真实前冲末速度、重力3096cm/s²、水平制动/摩擦0；用户不要求还原原作条件制动。189两来向共用源帧0与0.1s混合，可调0.35s准备后使用实录后退上抛轨迹，随后157延续1800cm/s²重力与后向360cm/s²加速度。准备不是骨架冻结/卡肉。当前不减速运动、点灯与真实空回接管已签本招E3（2026-09-30）；M5总验收已完成。数据和边界见[操虫斩曲线](../reference/操虫斩曲线.md)，当前验收见[实施计划§9.7](../archive/demo-2026-10-13/操虫斩实现计划.md)。

操虫斩与普通猎虫回手调用同一个 `ApplyExtract(Color)`。三灯持续期间，玩家吸收的任何单色精华都直接被吞掉：不创建单灯 GE、不缓存待恢复颜色，也不刷新三灯持续时间；仍可播放一次“吸收但未改变状态”的轻量反馈。

### 6.2.1 觉虫击199武器命中→共享189舞踏（M6.2新增合同，2026-10-07）

- 觉虫击猎人199冲刺另有一个武器伤害段 `AttackSegments[0]`，范围为 `IG_RearTip → Root`。只有该段成功应用伤害 GameplayEffect 后，才排队切换；Sweep接触或GE失败均不授予舞踏。碰撞窗口由用户配置在 `AM_IG_JueChongJi` 的 `HunterFlight199` 内，`ConfigIndex=0`；运行时和资产审计不更改其帧时刻。
- 命中切换复用操虫斩常驻 `AM_IG_CaoChongZhan_Dance / HitDance189`，连同 `DanceFromDashTuning`、准备时长和Montage BlendIn；不复制新Montage、不另造189运动曲线。舞踏来源登记为独立 `EIGDanceSource::AwakenedKinsect`，不伪装成 `KinsectSlash`。
- 命中后猎虫随猎人飞行结束而召回；切换在Sweep回调栈之外执行，退休旧199位移，再由共享189准备悬停和authored CMC舞踏运动接管。189动画与运动均结束后交回常规空中下落；未命中沿觉虫击原206下落流程。真实落地仍由RuntimeHost统一清除层数和来源Tag。

### 6.3 强化操虫穿刺——操虫斩命中舞踏后的 `RT+Y`

**2026-10-01：强化操虫穿刺 M1/M2/M3/E1/M4完成，E2本轮平地可玩范围已签收。** 最新证据见[强化操虫穿刺实现计划§13.5](../archive/demo-2026-10-13/强化操虫穿刺实现计划.md)，静态/回归证据见§13.3～13.4，§11～12保留历史。碰墙/封顶、慢放采样、全取消矩阵或五条 clone 全部实机通过均不在签收范围。强化跳跃斩与降龙已移出当前 Demo/M5 范围；急袭突刺也已完成，本招证据见[独立计划](../archive/demo-2026-10-13/急袭突刺实现计划.md)。主 M5 总验收已关闭。

- 输入为现有 `Input.Weapon.RTY`。仅允许当前 Resource 来源为 `EIGDanceSource::KinsectSlash`，且 `Combat.State.Aerial.Falling` 在场时派生；与空回、操虫斩的可出手时机相同。舞踏保护前段、落空、仅武器命中、突进回旋斩舞踏均不能进入。
- Resource 来源是真值，M2 将它同步为 `Combat.State.Aerial.Dance.Source.KinsectSlash` 标签供 Combo 匹配，GA 激活时再核验真实来源；不要求层数必须大于零，也不用旧 `Aerial.Actionable`。确认激活后消费 `CantAttack` 与 `CantDodge` 两项预算；本招命中不重置预算。
- 强化操虫穿刺资格仍只认 `EIGDanceSource::KinsectSlash`；M6.2 新增的 `AwakenedKinsect` 来源不会自动获得本招派生权限。
- 本次 RTY 输入解析冻结准心射线，入招只采用水平 Yaw。准心俯仰不改变位移、速度、高度或时长；之后不追踪镜头、摇杆或目标。
- 沿实录完整链执行 `200` 起手→`201` 空中穿刺→真实触地→`203` 落地攻击/后摇；`202` 只循环空中姿势，曲线时间不循环。不再沿用旧的“穿刺结束回 Falling”设计。
- 三个独立武器伤害段，`AttackSegments[0/1/2]` 各有一次对同目标的成功伤害额度；用户在主/落地 Montage 放现有 Attack Collision 窗口，不预设时间或强行拆为空中两击、落地一击。
- 整个GA的全部武器伤害段显式接入同一出招舞踏倍率快照，包含203地面段；触地由Host清除资源层数，但不改变本次GA的快照。本招不增舞踏、不点灯、不重置空中预算；一份Action身份继续完成203，`LandingPolicy=AbilityOwned`。
- 类继承、GA/AM/CV 命名、共享伤害 GE、MovementTask/CMC 所有权与幂等清理均按详细计划；不新增 ForceIdle 输入死区、不重建已签收的空回/操虫斩。

### 6.4 强化跳跃斩（已移出Demo）

2026-10-01移出当前Demo，不再作为动作需求或M5门禁。

### 6.5 急袭突刺——持刀 Falling 阶段 `RT` 限时松开

保留《世界》的急袭突刺；它是当前Demo范围内的新增空中招式，现已完成本招验收。规则为同当前操虫斩的 `Unsheathed + Aerial.Falling`、未被 `CantAttack` 锁住及 Host 尚未接地；不要求操虫斩舞踏来源。确认激活后同时消费 `CantAttack` 与 `CantDodge`，未命中下坠不能再空回；RT 在配置上限内松开且未被获胜组合消费才派发（初始沿用0.25s）。空中按下单RT不再提前触发，地面虫印斩与空中本招共用一个release-fallback所有者，Combo按状态分流。

第一段380有一次性进入和待查找的循环区，采用校准的锁定水平向量/竖直曲线，姿势循环不重置物理时间。第二段381真实触地后直接播放原动画Montage Root Motion，不叠加空中惯性。三次修正最大角180°/180°/60°，依真值时钟渐进转向；前两次各取序列入口前一游戏帧、第三次取转向开始前一游戏帧的摇杆快照。四个独立武器伤害窗口由用户放置；地面接受窗可派生二连、后撤、飞圆，飞圆入口同二连来源。

成功Commit先快照舞踏终结倍率再清层，四段用冻结倍率；下落、着地攻击/后摇属于同一Ability，`LandingPolicy=AbilityOwned`。只在地面段完成、成功派生或取消后报告结束。两条Montage均为常驻资产，禁止运行时动态拼接。详细实现与签收范围见[急袭突刺实现计划](../archive/demo-2026-10-13/急袭突刺实现计划.md)；本招M1/M2/M3/E1/M4/E2与主M5总验收均已于2026-10-06签收。

### 6.6 降龙（已移出Demo）

2026-10-01移出当前Demo，不再作为动作需求或M5门禁。

## 7. 连招状态约束

ComboData 的一行表示一条转移边，而不是“状态本身”：

```text
SourceState + Input/Direction/Tags
  -> ActivateAbility
  -> TargetState
```

自动派生必须引用确定的 Transition ID，不能只拿 TargetState 后随便激活该状态的第一条出边。

Demo 至少需要这些状态：

```text
Idle
IG.Ground.World.*
IG.Ground.Tetraseal
IG.Ground.AdvancingRoundslash
IG.Ground.StarterOnly
IG.Aerial.Free
IG.Aerial.Dance.KinsectSlash
IG.Aerial.Dance.AdvancingCounter
IG.Aerial.DescendingThrust
```

取消、受击、落地、死亡和换武器必须通过统一出口清除当前动作实例拥有的窗口 Tag，并把协调器恢复到与角色实际姿态一致的状态。

## 8. Demo 验收摘要

1. 《世界》地面基底连招可运行，经典红灯模式下无红灯/有红灯进入不同动作组。
2. 切换到 NumericOnly 配置后，红灯不再改变动作匹配，只改变可调数值。
3. `Y+B` 和 `前+Y+B` 按角色相对方向稳定分流；四连印斩/未反击回旋斩后只能接四种起手招。
4. 突进回旋斩窗口内受到可反击攻击时免除该次命中并触发舞踏；窗口外正常受击。
5. 操虫斩直接 `B` 按角色 Forward 飞行；`RT+B` 在186的AimLock帧采样当前准心并冻结三维前冲方向。猎虫段[0]成功提交真实伤害GE后按部位颜色捕获精华、回手交付；冲刺武器段[1]成功结算才提交舞踏，189武器段[2]不重复提交。三段伤害保持独立；仅虫命中不舞踏、仅武器命中不点灯。三灯期间按现有精华接收规则处理、不刷新三灯。
6. 舞踏倍率达到配置上限后不再增加；当前 Demo 在真实落地清层，急袭突刺则先快照倍率再于 Commit 清层。收刀、空中受击和死亡不列入当前 Demo 范围；卸装/RuntimeShutdown 属资源生命周期清理。
7. 觉虫击成功出招消耗所有现有灯并冻结效果：无红灯每击MV15，红灯且非三灯MV30，完整三灯MV50；白灯贯通并逐击收集不同颜色，黄灯令单击/贯通成功伤害逐击产粉，无黄灯保留普通虫印产粉。猎虫回手后才交付精华；非三灯终点悬停等待猎人冲刺结束召回、三灯终点回收及两档速度不变。猎人按受限准心方向位移；RTYB激活时按Action相机准心方向使用90°上限修正，丢虫面向修正从Montage第230帧开始；193后半段最多循环3遍，松键仍可提前进入送虫。
8. 粉尘聚爆飞向准心目标，收集直径等于训练场边长的球内己方虫粉、刷新180s，到达开始聚拢后1s以直径2m结算数量阶梯与ΣDi两段；操虫/收虫取消，释放预留但保留已发生的聚拢和寿命刷新。
9. 虫印斩与急袭突刺地面第一击的指定有效武器段建立/替换唯一90s局部虫印；玩法清印仅死亡、到期、新印替换。猎虫离手后在范围内自动追印真实攻击，玩家指令优先；普通/招式猎虫局部命中受10s产粉CD，贯通例外；虫粉180s自然单段直径1m爆炸，远距消失、上限100。
10. 猎虫滑翔有虫印时追虫印、无虫印时短距前飞；命中怪物后进入空战但不增加舞踏。
11. 跳跃突进斩不在Demo范围；强化跳跃斩与降龙已于2026-10-01移出Demo/M5门禁。急袭突刺的持刀Falling限时RT松开、380进入/循环及校准位移、381原根运动、三修正/四判定/三后续派生与所有权清理均按独立计划验收；M5收口与总验收已关闭。
