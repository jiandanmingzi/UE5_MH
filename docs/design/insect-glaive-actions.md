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

**2026-10-06 当前M5状态：** 空回、操虫斩、强化操虫穿刺（平地范围）与急袭突刺均已按各自范围签收，M5总验收也已关闭。真实落地清舞踏层数/来源/镜像 Tag；急袭突刺 Commit 先快照倍率再清层。收刀、空中受击和死亡不属于 Demo/M5 范围。完整验收记录见[M5总计划§10](../using/m5-plan.md#10-阶段-f--m5-总验收与签核)；急袭突刺本招范围见[独立计划](../using/急袭突刺实现计划.md)。

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
| 持刀地面 | `LT+Y+B` | 粉尘集约并引爆 |
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
| `Idle → GroundStarter` | `Input.Weapon.Y`、`Direction=Forward`、`Grounded+Sheathed` | `GA_IG_DrawSlash` | 具体方向优先于 None；播放拔刀攻击，AttackSegment 按普通攻击配置。 |

`GA_IG_Draw` 与 `GA_IG_DrawSlash` 都继承 `UMHGZDrawAttackAbility`（M4-A.4），并使用 Montage 中原生 `AnimNotify_DrawCommit`。Notify 只负责以 Mesh+MontageInstanceID 找到当前 ActionToken 并转发；Ability 才调用 `RuntimeHost->SetSheathed(false)` 与清 `bSprintHeld`。因此 Commit 前取消仍是 Sheathed，Commit 后取消仍是 Unsheathed，不能在 Blueprint Event Graph 或 Ability 激活时提前写姿态。收刀 RT 的 `GA_IG_DrawAndSendKinsect` 也复用这一视觉 Commit 合同，但它直接继承 `UMHGZGameplayAbility`：它使用无 Root Motion 的 `UpperBody_IGAction` `ActionMontage`，不获得 `Attacking`、`BlockMovement` 或 MontageRootMotionOwner；`DrawCommit` 只切姿态，后续 `KinsectSendCommit` 才部署冻结的猎虫请求。

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
AwakenedKinsectMotionValue / AwakenedPierceHitInterval
MaxDanceStacks
DanceDamageMultipliers[]       // 索引即层数，索引 0 必须为 1.0
KinsectSlashMaxDistance
KinsectGlideMarkMaxDistance
KinsectMarkMaxDistance
KinsectMarkDuration
KinsectMarkProjectileSpeed/Radius/Lifetime
KinsectGlideFallbackDistance
KinsectGlideFallbackLiftHeight
AwakenedKinsectMaxDistance
AwakenedHunterFlightMaxDistance
AwakenedHunterFlightStartTime
AwakenedAimCorrectionAngle = 60°
DescendingThrustAirControl
PowderGatherRadius/Duration/MaxCount
```

精华 GE 负责固定 GrantedTags/Modifier 形态，持续时间和倍率由 CombatConfig 通过 Spec/SetByCaller 注入；不在 GE 默认值和 CombatConfig 各保留一份真相。位移距离、高度、持续时间和增伤值也都来自该资产；动作状态机不得使用散落在 GA 蓝图中的同名常量。Data Validation 必须检查距离/时长为正、倍率非负、修正角在 `[0°, 180°]`、`DanceDamageMultipliers.Num == MaxDanceStacks + 1`。InputProfile 另行验证 GracePeriod、方向阈值和 Chord 冲突。

## 5. 地面新增动作

### 5.1 四连印斩——`Y+B`

- 可以从 Idle 起手。
- 可以在《世界》地面动作明确开放的 ComboWindow 内派生，但飞圆斩除外；“任意招式派生”不表示可在任意动画帧无条件打断。
- 四连印斩结束进入 `IG.Ground.StarterOnly`，不能直接接中段或终结动作。
- `IG.Ground.StarterOnly` 只允许派生：突刺、上捞斩、横扫、飞身跃入斩。
- 四段攻击各自拥有 AttackSegment/ConfigIndex；相邻攻击窗口可以重叠，但必须独立去重。
- 动作本身不产生舞踏层数。

### 5.2 突进回旋斩——`前+Y+B`

- 起手与派生范围和四连印斩一致：Idle 起手边不要求窗口，其他动作派生边要求 ComboWindowOpen 并排除飞圆斩；方向条件匹配时优先于无方向 `Y+B`。
- 激活时快照输入世界方向，并在允许修正角内调整动作朝向。
- Montage 中以 `AnimNotifyState_IG_AdvancingCounter` 标记反击窗口；Notify 先由 Mesh+MontageInstanceID 解析本 ActionToken，Begin 向 Character IncomingHitResolver 注册绑定该 ActionToken 的拦截 Token，End/Cancel 幂等注销。
- 反击窗口内收到可反击的怪物攻击时，该次攻击不结算玩家伤害和硬直，立即停止剩余地面段，触发舞踏弹跳并增加一层舞踏。
- 同一激活实例只允许成功反击一次；环境伤害、持续地面伤害以及标记为不可反击的攻击不能触发。
- 未触发反击时，动作结束与四连印斩相同：进入 `IG.Ground.StarterOnly`。

反击不是普通无敌帧。判定必须由 IncomingHitResolver 消费带攻击实例 ID 的 IncomingHitContext，避免同一个多段攻击在反击成功后再次命中；窗口外按正常受击规则处理。

### 5.3 粉尘集约——地面 `LT+Y+B`

Demo 只实现一种通用粉尘，不区分爆破、回复、麻痹等正式品种：

`AIGPowderActor` 至少保存 OwningPlayer、SourceAttackInstanceID、SpawnTime/Lifetime、状态（Available/Reserved/Consumed）和伤害贡献参数；它无物理阻挡。Demo 中普通武器攻击和猎虫接触都不会引爆粉尘，只有本动作能够消费并引爆。

1. 以当前猎虫位置为中心，快照 `PowderGatherRadius` 内由本玩家生成且未被预留的粉尘。
2. 没有可用粉尘或猎虫不存在时激活失败，不进入攻击状态。
3. 最多选择 `PowderGatherMaxCount` 个，标记为 Reserved，防止被其他命中或第二次集约重复消费。
4. 在 `PowderGatherDuration` 内将表现聚向猎虫；完成后消费全部预留粉尘，生成一次聚合范围爆炸。
5. 爆炸伤害由基础值和粉尘贡献数共同计算，具体曲线保存在 CombatConfig/Curve 中；同一目标每次集约只结算一次。
6. GA 被受击或换武器取消时解除 Reserved；粉尘不消失，也不产生爆炸。

### 5.4 虫印——持刀虫印斩与 `LT+RT` 虫印弹

**近战虫印斩：** 持刀地面短按 RT，若未形成 RT+A/B/Y、LT+RT 等获胜组合，且 RT 在 0.25 秒内松开，才派发 `Input.Weapon.RT` 并激活 `GA_IG_MarkSlash`。超过该最大按住时长后松开不派发虫印斩。它是带普通 AttackSegment 的近战攻击；首次有效 Hitzone 命中通过 `URes_InsectGlaive` 建立/替换唯一虫印。攻击落空、无有效 Hitzone 或被中断均不改变旧虫印。

**远程虫印弹：**

- `LT+RT` 按猎虫准心发射无伤害虫印弹；只在命中有效怪物 Hitzone 时创建虫印。
- 使用 LT+RT 输入事件冻结的 Kinsect AimSnapshot 目标点，再从武器虫印弹发射 Socket 朝该点生成 `AIGMarkProjectile`；速度、半径、Lifetime 和最大距离来自 CombatConfig，不在激活/飞行中重新采样准心。
- Projectile 以前后帧 Sweep 查询 Hitzone Object，并单独检查 WorldStatic 阻挡；按 Hit.Time 选择最早有效命中。先撞 WorldStatic/超距/超时均失败。
- 同一玩家同时只能有一个有效虫印。近战虫印斩与虫印弹共用同一 Resource 所有权：创建新印时先移除旧印，再把新印附着到命中 Hitzone 的组件/骨骼局部位置。
- 虫印持续 `KinsectMarkDuration`，时长和最大射程可调；目标死亡、Hitzone/Actor 失效、卸下虫棍或 Pawn 结束生命周期时立即清除。
- 收刀不会主动清除虫印；再次拔刀后，只要目标和时长仍有效，猎虫滑翔仍可使用。
- 虫印不存进 ASC 或 SaveGame。`URes_InsectGlaive` 保存弱引用和到期 Timer，并用 `WeaponResource.IG.Mark.Active` 作为可选镜像 Tag 供 UI/条件查询。
- 虫印弹命中 WorldStatic、超出距离、超时或被阻挡时失败，不替换当前仍有效的旧虫印。

### 5.5 猎虫滑翔——地面 `RT+Y`

激活时只选择一次目的地：

- 当前存在有效虫印，且虫印距离不超过 `KinsectGlideMarkMaxDistance`：飞向虫印位置。
- 否则：沿角色 Forward 飞行 `KinsectGlideFallbackDistance`，并增加少量 `KinsectGlideFallbackLiftHeight`。

移动必须做胶囊 Sweep，并以最大距离、最大时长和阻挡碰撞三重结束。命中有效怪物 Hitzone 后停止水平冲刺，执行一次小幅弹跳并进入空中状态；该弹跳不增加舞踏层数。未命中则在位移结束后按当前高度进入 Falling，不能强制吸附到目标。

### 5.6 觉虫击——地面 `RT+Y+B`

激活条件为三灯有效且猎虫可用：

1. 在 Commit 时消耗三灯并清空红、白、橙精华；激活前验证失败不得消耗。
2. 使用 RT+Y+B 输入事件中冻结的 Action AimSnapshot。相对角色 Forward 的水平和垂直修正均限制在正前方 `±60°`；超出时将方向 Clamp 到圆锥边界，而不是拒绝激活。
3. 猎虫沿修正后的方向进行有最大距离的贯通飞行。同一 Flight 对同一 Hitzone 的重复命中受可配置间隔限制。
4. 每个通过 Hitzone/间隔验证且成功提交伤害的 Hit，按同一真实 HitResult 立即调用普通 `ApplyExtract`，并在命中位置生成一团本玩家拥有的通用粉尘；伤害提交失败时两者都不发生。
5. 猎人朝激活瞬间准星射线得到的目标点飞行，距离截断到 `AwakenedHunterFlightMaxDistance`。位移在 Montage 的 `HunterFlightStart` Notify/配置时刻开始，可以与猎虫贯通阶段重叠，但不重新采样准心。
6. 猎人位移结束后根据高度进入空中或落地状态；碰墙、受击、死亡或换武器必须同时取消猎人位移，猎虫按自己的召回规则结束。

三灯消耗必须在资源组件内作为一个原子操作完成，不能依次移除三个 GE 后再判断状态。

> **已冻结：** 觉虫击 Commit 先原子消费旧三灯；之后每次有效贯通伤害都立即按命中 Hitzone 调用普通 `ApplyExtract`，并同时生成一团粉尘。贯通过程可重新取得三色并形成新三灯；新三灯形成后，后续萃取被吞且不刷新持续时间。伤害、萃取与粉尘共用同一个有效 Hit 判定，不允许三条链路各自重复查询产生次数分歧。

## 6. 舞踏与空中动作

### 6.1 舞踏层数

只有以下两种事件增加舞踏层数并触发舞踏弹跳：

- 操虫斩成功命中有效怪物 Hitzone。
- 突进回旋斩反击成功。

每次成功后：

```text
DanceStacks = min(DanceStacks + 1, MaxDanceStacks)
AerialDamageMultiplier = DanceDamageMultipliers[DanceStacks]
```

倍率只作用于猎人的后续空中攻击段，不作用于猎虫独立伤害、粉尘爆炸、地面攻击或环境伤害。每个 AttackSegment 创建伤害 Spec 时快照倍率。

当前 Demo 中，真实落地统一清空舞踏；急袭突刺 Commit 先把当前倍率快照进本次攻击，再清空层数，保证本次终结动作能使用已积累增伤。收刀、空中受击和死亡不属于当前 Demo 的舞踏战斗规则/验收范围；卸装与 RuntimeShutdown 仍可按资源生命周期合同清理状态。

### 6.2 操虫斩——空中 `B` / `RT+B`

- `RT+B`：输入只冻结瞄准路径；到 186 的 AimLock 帧才采样当前准心并冻结三维前冲方向。
- 直接 `B`：按角色面朝方向飞行。
- 两种路径使用同一 GA 和位移参数，只改变 AimSource。
- 方向受 `KinsectSlashMaxDistance` 和碰撞 Sweep 限制，不追踪激活后移动的准心或目标。
- 本招有三个独立伤害机会，均由 GA 的 `Attack Collision` 窗口结算：`[0]` 猎虫附着点判定、`[1]` 棍尾至Root的第一下武器判定、`[2]` Root至棍尖的第二下武器判定。三个判定各有一次伤害额度，不能用单个 Segment 的 `MultiHitCount=3` 代替；旧独立Probe不参与生产判定。
- **仅猎虫段[0]真实伤害GE成功提交**到有效 Hitzone 后按该部位取得红/白/橙精华，即使角色当前已经处于三灯状态；该次提取不要求猎虫先返回。武器的两次命中只造成武器伤害，不点灯。
- 猎虫命中后触发舞踏、增加层数，并将 Resource 的 `DanceSource` 写为 `KinsectSlash`；只有武器命中或完全未命中时不进入该舞踏分支，保留末速度进入 Falling。对应来源标签由 `URes_InsectGlaive::RefreshDanceStateTags()` 在 `AddDanceStack`/`ClearDanceStacks` 广播前镜像到 ASC（M2，2026-09-30）；真值始终是 Resource 的 `DanceSource` 字段，标签只是它的对外广播。
- 运动使用本招专用档。**现行范围为不减速版**：188继承真实前冲末速度、重力3096cm/s²、水平制动/摩擦0；用户不要求还原原作条件制动。189两来向共用源帧0与0.1s混合，可调0.35s准备后使用实录后退上抛轨迹，随后157延续1800cm/s²重力与后向360cm/s²加速度。准备不是骨架冻结/卡肉。当前不减速运动、点灯与真实空回接管已签本招E3（2026-09-30）；M5总验收已完成。数据和边界见[操虫斩曲线](../reference/操虫斩曲线.md)，当前验收见[实施计划§9.7](../using/操虫斩实现计划.md)。

操虫斩与普通猎虫回手调用同一个 `ApplyExtract(Color)`。三灯持续期间，玩家吸收的任何单色精华都直接被吞掉：不创建单灯 GE、不缓存待恢复颜色，也不刷新三灯持续时间；仍可播放一次“吸收但未改变状态”的轻量反馈。

### 6.3 强化操虫穿刺——操虫斩命中舞踏后的 `RT+Y`

**2026-10-01：强化操虫穿刺 M1/M2/M3/E1/M4完成，E2本轮平地可玩范围已签收。** 最新证据见[强化操虫穿刺实现计划§13.5](../using/强化操虫穿刺实现计划.md)，静态/回归证据见§13.3～13.4，§11～12保留历史。碰墙/封顶、慢放采样、全取消矩阵或五条 clone 全部实机通过均不在签收范围。强化跳跃斩与降龙已移出当前 Demo/M5 范围；急袭突刺也已完成，本招证据见[独立计划](../using/急袭突刺实现计划.md)。主 M5 总验收已关闭。

- 输入为现有 `Input.Weapon.RTY`。仅允许当前 Resource 来源为 `EIGDanceSource::KinsectSlash`，且 `Combat.State.Aerial.Falling` 在场时派生；与空回、操虫斩的可出手时机相同。舞踏保护前段、落空、仅武器命中、突进回旋斩舞踏均不能进入。
- Resource 来源是真值，M2 将它同步为 `Combat.State.Aerial.Dance.Source.KinsectSlash` 标签供 Combo 匹配，GA 激活时再核验真实来源；不要求层数必须大于零，也不用旧 `Aerial.Actionable`。
- 本次 RTY 输入解析冻结准心射线，入招只采用水平 Yaw。准心俯仰不改变位移、速度、高度或时长；之后不追踪镜头、摇杆或目标。
- 沿实录完整链执行 `200` 起手→`201` 空中穿刺→真实触地→`203` 落地攻击/后摇；`202` 只循环空中姿势，曲线时间不循环。不再沿用旧的“穿刺结束回 Falling”设计。
- 三个独立武器伤害段，`AttackSegments[0/1/2]` 各有一次对同目标的成功伤害额度；用户在主/落地 Montage 放现有 Attack Collision 窗口，不预设时间或强行拆为空中两击、落地一击。
- 空中段显式接入舞踏倍率，触地后的地面段按地面规则结算；本招不增舞踏、不点灯、不重置空中预算。触地由 Host 清舞踏，一份 Action 身份继续完成 203，`LandingPolicy=AbilityOwned`。
- 类继承、GA/AM/CV 命名、共享伤害 GE、MovementTask/CMC 所有权与幂等清理均按详细计划；不新增 ForceIdle 输入死区、不重建已签收的空回/操虫斩。

### 6.4 强化跳跃斩（已移出Demo）

2026-10-01移出当前Demo，不再作为动作需求或M5门禁。

### 6.5 急袭突刺——持刀 Falling 阶段 `RT` 限时松开

保留《世界》的急袭突刺；它是当前Demo范围内的新增空中招式，现已完成本招验收。规则为同当前操虫斩的 `Unsheathed + Aerial.Falling`、未被CantAttack锁住及Host尚未接地；不要求操虫斩舞踏来源。RT在配置上限内松开且未被获胜组合消费才派发（初始沿用0.25s）。空中按下单RT不再提前触发，地面虫印斩与空中本招共用一个release-fallback所有者，Combo按状态分流。

第一段380有一次性进入和待查找的循环区，采用校准的锁定水平向量/竖直曲线，姿势循环不重置物理时间。第二段381真实触地后直接播放原动画Montage Root Motion，不叠加空中惯性。三次修正最大角180°/180°/60°，依真值时钟渐进转向；前两次各取序列入口前一游戏帧、第三次取转向开始前一游戏帧的摇杆快照。四个独立武器伤害窗口由用户放置；地面接受窗可派生二连、后撤、飞圆，飞圆入口同二连来源。

成功Commit先快照舞踏终结倍率再清层，四段用冻结倍率；下落、着地攻击/后摇属于同一Ability，`LandingPolicy=AbilityOwned`。只在地面段完成、成功派生或取消后报告结束。两条Montage均为常驻资产，禁止运行时动态拼接。详细实现与签收范围见[急袭突刺实现计划](../using/急袭突刺实现计划.md)；本招M1/M2/M3/E1/M4/E2与主M5总验收均已于2026-10-06签收。

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
5. 操虫斩直接 `B` 按角色 Forward 飞行；`RT+B` 在186的AimLock帧采样当前准心并冻结三维前冲方向。仅猎虫段[0]向有效Hitzone成功提交真实伤害GE后，按部位颜色调用ApplyExtract并提交舞踏；仅武器命中、落空或伤害提交失败均不点灯、不提交舞踏。三灯期间ApplyExtract吞掉单色精华且不刷新三灯。
6. 舞踏倍率达到配置上限后不再增加；当前 Demo 在真实落地清层，急袭突刺则先快照倍率再于 Commit 清层。收刀、空中受击和死亡不列入当前 Demo 范围；卸装/RuntimeShutdown 属资源生命周期清理。
7. 觉虫击原子消耗旧三灯；猎虫每次成功贯通伤害立即按部位点灯并留下粉尘，途中可以重新形成三灯；猎人按受限准心方向位移。
8. 粉尘集约只消费范围内己方粉尘；取消时正确回滚预留。
9. 虫印斩首次有效近战 Hitzone 命中或 LT+RT 虫印弹命中后，均替换唯一虫印；虫印按时长、目标和卸装规则清理。
10. 猎虫滑翔有虫印时追虫印、无虫印时短距前飞；命中怪物后进入空战但不增加舞踏。
11. 跳跃突进斩不在Demo范围；强化跳跃斩与降龙已于2026-10-01移出Demo/M5门禁。急袭突刺的持刀Falling限时RT松开、380进入/循环及校准位移、381原根运动、三修正/四判定/三后续派生与所有权清理均按独立计划验收；M5收口与总验收已关闭。
