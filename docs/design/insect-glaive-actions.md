# 虫棍 Demo 动作与连招设计

> **文档范围：** 本文是虫棍 Demo 的动作规则真相源，只详细描述位移明显、具有反击/猎虫/粉尘/精华等特殊效果的动作。普通《世界》地面动作的攻击帧、动作值和动画段继续配置在各自 GA/Montage 与虫棍 ComboData 中，不在本文逐招重复。

> **实施状态：** 本文描述的是目标设计，不表示代码或蓝图已经完成。实现状态以源码、Content 和 [编辑器搭建指南](../editor/demo-setup.md) 为准。

> **实现约束：** 输入快照、连招转移、RuntimeHost、Hitzone/猎虫碰撞、伤害上下文与位移所有权以 [Demo 冻结实施计划](demo-implementation-plan.md) 为准；在计划中的未决玩法语义确认前不开始对应代码阶段。

## 1. 目标与边界

虫棍以《怪物猎人：世界》的地面招式、基础猎虫操作和三色精华为骨架，吸收《怪物猎人：崛起》的四连印斩、突进回旋斩、操虫斩、强化操虫穿刺、猎虫滑翔、觉虫击、粉尘集约与降龙等动作，并按本项目规则重新组织输入和派生。

Demo 明确不实现：

- 翔虫槽、翔虫恢复和翔虫受身；移植的原翔虫动作不消耗翔虫。
- 集中模式及集中弱点攻击。
- 钩爪及软化系统。
- 跳跃突进斩（空中“电风扇”）。
- 完整斩味、元素、异常和怪物生态；Demo 伤害沿用简化公式。

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
| 收刀 | `Y` | 按《世界》地面基底执行拔刀攻击 |
| 收刀 | `RT` | 拔刀并让猎虫沿角色正前方飞出 |
| 收刀 | `LT` | 投射物瞄准；Demo 暂无消费方 |
| 持刀地面 | `LT` | 猎虫瞄准，显示猎虫准心 |
| 持刀地面 | `Y+B` | 四连印斩 |
| 持刀地面 | `前+Y+B` | 突进回旋斩；方向节点优先于无方向四连印斩 |
| 持刀地面 | `LT+Y+B` | 粉尘集约并引爆 |
| 持刀地面 | `LT+RT` | 发射虫印弹，命中 Hitzone 后建立/替换虫印 |
| 持刀地面 | `RT+Y` | 猎虫滑翔 |
| 持刀地面 | `RT+Y+B` | 觉虫击 |
| 空中 | `B` | 操虫斩，沿角色面朝方向 |
| 空中 | `LT+B` | 操虫斩，沿猎虫准心方向 |
| 操虫斩触发的舞踏中 | `LT+Y` | 强化操虫穿刺 |
| 空中 | `Y` | 强化跳跃斩，继承现有惯性 |
| 空中 | `RT` | 《世界》急袭突刺 |
| 空中 | `LT+Y+B` | 降龙 |

`LT` 是持刀状态的猎虫瞄准；`RT` 是动作瞄准/特殊动作修饰键。两者是不同的输入上下文，不能共用一个 `Combat.State.Aiming` 标签。建议使用：

```text
Combat.State.Aiming.Kinsect   // 持刀 LT
Combat.State.Aiming.Action    // 持刀 RT 特殊动作上下文
Combat.State.Aiming.Slinger   // 收刀 LT，Demo 无消费方
```

### 3.2 组合键与匹配优先级

`Y+B`、`LT+Y+B`、`RT+Y+B` 必须在输入层形成稳定的组合输入事件，不能依赖两个普通 Ability 在同一帧竞争激活。组合键允许一个可配置的 Chord Grace Period；在该时间内组成组合键后，应抑制对应的单键攻击。

同一帧有多个候选动作时按以下顺序选择：

1. InputProfile 先按 RequiredHeldModifiers 数量、TriggerControls 数量和 Chord Priority 解析最具体且修饰键完全匹配的组合 Tag，例如 LT 可提前长按，随后 Y+B 在 GracePeriod 内组成 `LTYB` 并阻止普通 `YB`。
2. 协调器按地面/空中、收刀/持刀等互斥状态筛选完全匹配的 InputTag。
3. 具体方向条件优先于 Direction=None。
4. ComboData 的显式 Priority。
5. 仍相同时视为数据错误，由 Data Validation 阻止运行。

### 3.3 “前+Y+B”的前方定义

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
DivingWyvernDistance/Height/Duration
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

### 5.4 虫印——持刀地面 `LT+RT`

- `LT+RT` 按猎虫准心发射无伤害虫印弹；只在命中有效怪物 Hitzone 时创建虫印。
- 使用 LT+RT 输入事件冻结的 Kinsect AimSnapshot 目标点，再从武器虫印弹发射 Socket 朝该点生成 `AIGMarkProjectile`；速度、半径、Lifetime 和最大距离来自 CombatConfig，不在激活/飞行中重新采样准心。
- Projectile 以前后帧 Sweep 查询 Hitzone Object，并单独检查 WorldStatic 阻挡；按 Hit.Time 选择最早有效命中。先撞 WorldStatic/超距/超时均失败。
- 同一玩家同时只能有一个有效虫印。创建新虫印时先移除旧虫印，再把新虫印附着到命中 Hitzone 的组件/骨骼局部位置。
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

落地、受到有效攻击、急袭突刺 Commit、降龙 Commit、死亡、收刀或卸下虫棍时清空舞踏。急袭突刺和降龙应先把当前倍率快照进本次攻击，再清空层数，保证当前终结动作能吃到已积累增伤。

### 6.2 操虫斩——空中 `B` / `LT+B`

- `LT+B`：按本次输入快照中的猎虫准心方向飞行。
- 直接 `B`：按角色面朝方向飞行。
- 两种路径使用同一 GA 和位移参数，只改变 AimSource。
- 方向受 `KinsectSlashMaxDistance` 和碰撞 Sweep 限制，不追踪激活后移动的准心或目标。
- 命中有效 Hitzone 时立即按该部位取得红/白/橙精华，即使角色当前已经处于三灯状态；该次提取不要求猎虫先返回。
- 命中后触发舞踏、增加层数，并写入 `Combat.State.Aerial.Dance.Source.KinsectSlash`；未命中则保留末速度进入 Falling，不增加层数。

操虫斩与普通猎虫回手调用同一个 `ApplyExtract(Color)`。三灯持续期间，玩家吸收的任何单色精华都直接被吞掉：不创建单灯 GE、不缓存待恢复颜色，也不刷新三灯持续时间；仍可播放一次“吸收但未改变状态”的轻量反馈。

### 6.3 强化操虫穿刺——操虫斩舞踏中 `LT+Y`

- 只允许从 `Combat.State.Aerial.Dance.Source.KinsectSlash` 派生；突进回旋斩产生的舞踏不能进入该动作。
- 激活时快照 LT 准心方向，在修正角和最大距离内执行定向穿刺。
- 命中使用标准空中舞踏倍率，但不再次增加舞踏层数，避免自循环刷层。
- 结束后继承穿刺末速度进入 Falling；是否继续派生其他空中动作由 ComboData 决定。

### 6.4 强化跳跃斩——空中 `Y`

移除跳跃突进斩后，`Y` 保留强化跳跃斩。该动作不把水平速度重置为动画固定方向，而是：

1. 激活时快照当前 `AerialVelocity`。
2. 动画根运动以 Additive 方式叠加在惯性上。
3. 允许摇杆在 `AirControl` 上限内修正水平惯性。
4. 结束时把合成后的末速度交还 CMC。

强化跳跃斩不触发舞踏，只消费当前空中攻击许可。

### 6.5 急袭突刺——空中 `RT`

保留《世界》的急袭突刺。它继承进入动作时的水平惯性，并在 Commit 时快照、随后清空舞踏层数。下落、着地攻击和落地后摇属于同一个 Ability 的显式阶段；只有着地攻击完成或 Ability 被取消后才向协调器报告结束。

### 6.6 降龙——空中 `LT+Y+B`

降龙不消耗翔虫，也不消耗红灯或三灯。它使用当前舞踏倍率，Commit 后清空舞踏：

- 激活时快照方向、层数和当前位置。
- 按配置执行短暂上升/定向准备后进入下砸；不能依靠零位移动画的 Motion Warping 凭空生成全程位移。
- 下砸使用连续 Capsule/Weapon Sweep，按配置决定单次或多跳伤害。
- 碰到有效地面或怪物后进入落地段；碰墙、顶棚或超时走失败收尾。
- 整个动作只允许一个根运动/位移所有者，结束时清理 RootMotionSource、Falling 子状态和攻击窗口。

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
IG.Aerial.DivingWyvern
```

取消、受击、落地、死亡和换武器必须通过统一出口清除当前动作实例拥有的窗口 Tag，并把协调器恢复到与角色实际姿态一致的状态。

## 8. Demo 验收摘要

1. 《世界》地面基底连招可运行，经典红灯模式下无红灯/有红灯进入不同动作组。
2. 切换到 NumericOnly 配置后，红灯不再改变动作匹配，只改变可调数值。
3. `Y+B` 和 `前+Y+B` 按角色相对方向稳定分流；四连印斩/未反击回旋斩后只能接四种起手招。
4. 突进回旋斩窗口内受到可反击攻击时免除该次命中并触发舞踏；窗口外正常受击。
5. 操虫斩按 LT 准心或角色 Forward 飞行，命中任意颜色部位都能取得精华并增加舞踏。
6. 舞踏倍率达到配置上限后不再增加；落地、受击、急袭突刺和降龙按规则清空。
7. 觉虫击原子消耗旧三灯；猎虫每次成功贯通伤害立即按部位点灯并留下粉尘，途中可以重新形成三灯；猎人按受限准心方向位移。
8. 粉尘集约只消费范围内己方粉尘；取消时正确回滚预留。
9. LT+RT 只在命中有效 Hitzone 后替换唯一虫印；虫印按时长、目标和卸装规则清理。
10. 猎虫滑翔有虫印时追虫印、无虫印时短距前飞；命中怪物后进入空战但不增加舞踏。
11. 空中不存在跳跃突进斩；强化跳跃斩和急袭突刺正确继承惯性；降龙不依赖翔虫或精华消费。
