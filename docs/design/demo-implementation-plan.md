# 虫棍木桩 Demo 冻结实施计划

> **阶段状态与顺序：** 本文描述冻结接口和详细工作，不维护会过期的“当前进行到哪里”结论。开始任何工作前先查 [阶段门禁、顺序与当前状态](milestone-gates.md)。其中区分代码完成、编辑器接线完成、阶段验收和全项目验收；M6 前不要求 Resource Widget，E5.1 的木桩三色配置也不会被误记为 E4.1 的新功能。

> **2026-08-11 追加冻结：** RB 双语义——仅持刀地面态按 RB=纳刀（`Input.Sheathe` 通用路由，按下立即触发；攻击/硬直/击倒/死亡/动作锁中无效）；收刀态按住 RB ≥0.1s=奔跑（`Input.Sprint` 键位由 LS 改为 RB，LS 释放）。空中不产生收刀输入，留待 M5 空中动作设计。文档已按此更新（§1.4、§3.2、M3/M4、E3/E4、验证清单），不要求回改已完成的 M0～M2 代码。

> **编号迁移（2026-08-29）：** E4.1 是最小纵切资产接线，E4.3 是其余动作资产接线。E4.1 只覆盖收刀/拔刀、放虫/收虫和一个由用户选定的地面起手动作；它不代表 E4 或 M4 完成。收刀与首招的实际运行时语义在 M4.1 接通，不能用 Blueprint Event Graph 临时绕过 RuntimeHost、Coordinator 或 ActionToken。

> **用途：** 当本文的公共接口、所有权和阶段退出条件确定后，再按里程碑逐步修改代码。实施时不得跨阶段顺手重构未列入范围的系统；阶段是否允许进入下一步由 [阶段门禁](milestone-gates.md) 的进出条件决定。

> **资产边界：** 现有实现允许按 [重构范围与资产处置](demo-refactor-scope.md) 完整重写。该文档的 Keep/Rewrite/Delete/Defer 表和一次性删除重建合同与本文同为开始改代码前的强制输入。

## 1. 本轮任务合同

### 1.1 目标

完成一个单机虫棍训练木桩 Demo：以《世界》的地面连招、猎虫和三色精华为基底，按 [虫棍动作设计](insect-glaive-actions.md) 接入选定的《崛起》动作，并保留未来添加其他武器的通用扩展点。

Demo 必须形成以下闭环：

```text
输入与组合键
  -> 连招转移与 Ability 激活
  -> Montage / 位移 / 攻击判定
  -> Hitzone / 精华 / 三灯 / 舞踏 / 虫印 / 粉尘
  -> 伤害、硬直与命中反馈
  -> 落地、受击、死亡、收刀、换装的统一清理
```

### 1.2 不在本轮实现的内容

- 翔虫、集中模式、钩爪。
- 完整斩味、属性、异常、眩晕、部位破坏与任务倍率。
- 正式怪物 AI、死亡剥取、任务结算、背包/仓库/存档闭环。
- 网络预测与联机复制；接口不得主动阻止未来复制，但本轮只验收本地单机。
- 为每个普通地面动作单独编写一份设计文档；普通动作继续由 ComboData、GA 配置和 Montage 窗口表达。

### 1.3 真相源优先级

出现冲突时按以下顺序处理：

1. [虫棍动作设计](insect-glaive-actions.md) 决定玩法语义。
2. 本文与 [重构范围与资产处置](demo-refactor-scope.md) 决定后续实现架构、公共接口、所有权、删除边界和实施顺序。
3. [编辑器搭建指南](../editor/demo-setup.md) 与 [验证清单](../editor/verification.md) 决定可观察验收。
4. 其他系统文档提供模块细节。
5. 当前源码只代表“现在有什么”，不能覆盖尚未实现的目标设计。

如果实施中发现必须改变本文的公共接口或玩法语义，应先停在当前里程碑并修改文档；不能一边写代码一边保留两套方案。

### 1.4 已冻结的补充玩法语义

- **觉虫击贯通萃取：** 每次产生有效贯通伤害时，立即读取该 Hitzone 的颜色并调用普通 `ApplyExtract`。觉虫击 Commit 已先消费旧三灯，因此贯通过程可以重新取得单灯，命中三种颜色时也可以重新形成三灯；一旦途中重新形成三灯，后续萃取按统一规则被吞且不刷新三灯时间。不使用颜色优先级，也不缓存“最后一种颜色”等待回手。
- **普通放虫命中后的猎虫状态：** SingleHit/FirstHitOnly 命中后立即停止伤害与 Hitzone Sweep，携带 `PendingExtractColor` 原地进入 Hovering；只有玩家主动召回或猎虫耐力归零才进入 Returning，到达玩家后交付精华。不会因取得颜色自动回手。
- **RB 双语义（纳刀/奔跑）：** 收刀态按住 RB ≥0.1s 进入奔跑（点按不闪跑）；仅持刀地面态按下 RB 立即输出 `Input.Sheathe`（通用路由，同 `Input.Dodge`），由 `GA_Sheathe` 播纳刀（静止/移动选段）。资格固定为 `Grounded + Unsheathed`，并且不得处于攻击、硬直、击倒、死亡、收刀或翻滚动作锁；InputProfile 与 `CanActivateAbility` 都要检查，空中不输出收刀输入。收刀状态在武器实际挂回背部的 `SheatheCommit` Notify 才 `SetSheathed(true)`。所有拔刀路径（收刀 Y 的仅拔刀、前+Y 拔刀攻击、收刀 RT 拔刀直飞、以及奔跑中进入这些路径）都在各自武器实际取出的 `DrawCommit` Notify 才 `SetSheathed(false)` 并清 `bSprintHeld`。`Input.Sprint` 键位由 LS 改为 RB，LS 释放。

### 1.5 E4.1：最小可玩纵切的边界

E4.1 的目的不是先做一套临时虫棍，而是用最终 DataAsset、输入路由、Resource、GA 父类和 Combo 架构形成最小闭环。允许先只接以下流程：

```text
收刀
  → RT 拔刀直飞放虫（Input.Weapon.RT）
  → 持刀 LT+Y 送虫 / LT+B 收虫
  → RB 纳刀（Input.Sheathe）
  → 收刀 Y：无/左/右/后仅拔刀；前+Y 拔刀攻击（同一 Input.Weapon.Y）
```

**E4.1 必须完成的资产与接线：**

1. 创建 `DA_IG_Kinsect_Speed`、四个精华 GE、`GA_IG_DrawAndSendKinsect`、`GA_IG_SendKinsect`、`GA_IG_RecallKinsect`、收刀 Montage `AM_Shth_ShouDao`。`GA_Sheathe` 的最终蓝图子类必须等待 M4.1 创建并编译其原生父类 `UMHGZSheatheAbility` 后再建立；`GA_IG_Draw`、`GA_IG_DrawSlash` 与其 Montage 必须等待 `UMHGZDrawAttackAbility`/`AnimNotify_DrawCommit` 编译后再建立；不得先用 `UMHGZGameplayAbility` 或 Blueprint Event Graph 制作临时收刀/拔刀路径。
2. 在 `DA_IG_Combat` 回填 KinsectData 和四个 GE；在角色 Skeleton 建立 `Kinsect_Arm_Socket` 并填入 CombatConfig。即使 E4.1 暂不测试萃取/三灯，这五个引用也是 Resource 运行与 Data Validation 的前置条件，不能留空或改为代码回退。
3. InputProfile 至少接通 Y、B、LT、RT、RB 的 RawAction 映射，以及 `Input.Weapon.RT`、`Input.Weapon.LTY`、`Input.Weapon.LTB`、`Input.Sheathe` 和**唯一** `Input.Weapon.Y` Chord。Y 不按方向创建第二条 Chord；`DA_IG_Combo` 用 `Direction=None` 的 DrawOnly 通配边和 `Direction=Forward` 的 DrawSlash 边分流。RT/Sheathe 的 ContextTags 仍按 §5.4 配置，尤其 Sheathe 必须为 `Unsheathed+Grounded`，不能为演示移除姿态限制。
4. `GA_Sheathe` 在 M4.1 原生父类编译后创建，且仍只属于 CoreAbilities；三个猎虫 GA 和两个 Y 拔刀 GA 只通过 `DA_IG_Combo` 的最终 Transition 获得。不得在 Character、PlayerController、PlayerState 或蓝图 Tick 直接 TryActivate 这些 Ability。

**E4.1 明确延后到 E4.3/M4.7/M5/M6 的内容：** 其他地面招式、四连/回旋、虫印、滑翔、粉尘、觉虫击、空中动作、舞踏、完整 Transition 表、木桩三色萃取闭环、HUD/反馈和正式猎虫外观动画。

**实施顺序：** KinsectData、四个精华 GE、三个猎虫 GA 与已完成的前向 Dodge 资产保留为当前 E4.1 基底；随后先做 M4.1.3.1～M4.1.5 原生代码：方向 Dodge 选择、DrawCommit/DrawAttack、普通攻击瞬转与测试。编译通过后再创建持刀左右后 Dodge Montage、`GA_IG_Draw`/`GA_IG_DrawSlash`、两条 Y Transition，并回归 `AM_Shth_ShouDao`、`GA_Sheathe` 与收刀 RT DrawCommit。最后完成最小 PIE 与 **E4.1 阶段范围** Data Validation；之后按 [阶段门禁](milestone-gates.md) 依次完成 M4.2～M4.5、M4.2.1，才继续 M4.6/E4.3/M4.7。E4.1 不允许在 Blueprint Event Graph 临时播放 Montage、直接写 Sheathed/Unsheathed Tag、直接 Spawn 猎虫或绕过 ComboCoordinator；这些做法会破坏后续动作接入。

## 2. 冻结后的模块边界

```text
PlayerController
  UMHGZInputComponent                 IMC/Enhanced Input Binding 唯一所有者
  UMHGZWeaponInputRouterComponent     原始按键、组合键、方向与释放身份
            |
            v  FWeaponInputSnapshot
PlayerState ASC
  UMHGZAbilitySystemComponent         GAS 身份、Ability Spec、GE
  UGA_WeaponComboCoordinator          数据驱动 FSM
            |
            v  FWeaponAbilityActivationContext
Character
  UMHGZWeaponRuntimeHostComponent     当前 Pawn 的武器运行时所有者
  FWeaponAction/MontageRegistry       Notify 精确解析到 ActionToken/实例
  FWeaponRuntimeTagLedger             临时 Loose Tag 的有所有权计数账本
  UMHGZWeaponResourceComponent        当前武器资源基类
  URes_InsectGlaive                   精华、三灯、猎虫、舞踏、虫印、粉尘
  UCharacterMovementComponent         唯一物理移动载体
            |
            v
Target ASC / Hitzone
  FMHGZGameplayEffectContext          真实 HitResult 与攻击身份
  UMHGZDamageExecCalc                 纯数值计算
  UMHGZAttributeSet::PostExecute      扣血、硬直事件、反馈结果
  UMHGZHitFeedbackRouterComponent     Cue、伤害数字、卡肉与镜头反馈

Local Player UI
  AMHGZHUD                            WBP_HUD 与资源插槽唯一所有者
```

每个武器类型使用一个 `UWeaponRuntimeDefinition : UPrimaryDataAsset` 作为运行时接线唯一入口：

```text
WeaponTypeTag
ResourceComponentClass
InputProfile
CombatConfig                 // UWeaponCombatConfigBase；虫棍为 UInsectGlaiveCombatConfig
ResourceWidgetClass
```

具体 `UMHGZWeaponDefinition`（物品攻击力、外观、词条等）只引用对应 RuntimeDefinition。`DT_WeaponResourceConfig` 资产不存在；`DT_WeaponComboConfig` 是待删除旧桥接。M2 删除其配置和代码读取，E3 删除资产，不保留 DataTable 与 RuntimeDefinition 两套并行查找。虫棍 ComboData 由其 CombatConfig 继续引用。

### 2.1 通用层可以知道什么

| 通用模块 | 允许知道 | 不允许知道 |
|---|---|---|
| InputRouter | 物理键、组合声明、姿态 Tag、输入方向 | 四连印斩、三灯、舞踏等虫棍名词 |
| ComboCoordinator | Source/TargetState、输入快照、Tag、资源门槛、AbilityClass | 当前武器是否虫棍；按武器类型写 `if/switch` |
| GameplayAbility 基类 | Commit、Cost、Cooldown、取消与输入释放身份 | 精华颜色、虫印、粉尘 |
| AttackAbility | Montage、攻击窗口、真实 HitResult、伤害上下文 | 部位名称到精华颜色的映射 |
| MovementTask | 方向、轨迹、碰撞、惯性、结束原因 | 猎虫滑翔或降龙的玩法名 |
| RuntimeHost | 创建/销毁当前武器 Resource、统一生命周期 | 具体资源内部数值规则 |
| HitFeedbackRouter | 已结算的反馈结果 | 重算伤害、决定是否反击 |

虫棍派生层独占红灯模式、三色精华、三灯原子消费、猎虫、粉尘、舞踏、虫印和特殊动作规则。

## 3. 必须一次冻结的公共合同

### 3.1 Ability 成本、冷却与持续生命周期

废弃 `bIsContinuous` 同时表示“Ability 长期存在”和“持续扣耐”的语义。目标字段为：

```cpp
enum class EAbilityStaminaCostPolicy : uint8
{
    None,
    Instant,
    PerSecond
};

struct FWeaponResourceCostSpec
{
    FGameplayTag CostType;       // 例如 Cost.IG.TripleUp；通用层不解释含义
    FScalableFloat Amount;
};

struct FWeaponResourceCostReservation
{
    FWeaponRuntimeToken RuntimeToken;
    uint32 ActivationSequenceID = 0;
    uint64 ReservationID = 0;
};
```

- Ability 是否持续存在由自身何时调用 `EndAbility` 决定，不再由成本字段推断。
- 所有可失败的一次性动作先建立武器资源 reservation，再调用 `CommitAbility`；任一步失败都立即结束，不播放 Montage、不改变连招状态，也不产生部分武器资源消费。
- `Instant` 使用有效的 Stamina Cost GE；不得再用 `MakeOutgoingSpec(nullptr)` 或以 Loose Tag 伪造冷却。
- `PerSecond` 在 Commit 成功后启动 `UAbilityTask_MHGZStaminaDrain`。任务按固定间隔记录真实经过时间，用有效的 Instant Cost GE 结算 `Rate × ElapsedTime`；不足以支付下一次消耗时取消拥有它的 Ability。
- Cooldown 使用标准 Cooldown GE 的 Duration 与 GrantedTag；取消、死亡或换装不手工写回 Loose Cooldown Tag。
- 连招协调器属于 maintained/no-cost Ability：不扣耐、不附带冷却，卸装或 Pawn 生命周期结束时显式取消。
- 旧 `bRequiresWeaponResource + WeaponResourceCost(float)` 不能表达三灯、瓶、色阶等离散资源，目标改为 `TArray<FWeaponResourceCostSpec>`。通用 GA 只透传 CostType/Amount，不按武器解释。
- Resource 必须提供 `TryReserveCosts(ActionToken, Specs, OutReservation)`、`ReleaseReservation(Reservation)` 与保证成功的 `ConsumeReservedCosts(Reservation)`。Reservation 锁定本次需要的精确资源身份，例如 Triple ActiveGE Handle；它不广播回调、不播放表现、不立刻移除资源。
- 固定事务顺序为：`TryReserveCosts` → `CommitAbility` → Commit 失败时 `ReleaseReservation`，成功时 `ConsumeReservedCosts` → `ConfirmTransitionActivation`。成功 reservation 在同一同步调用栈内消费，`ConsumeReservedCosts` 不允许再次失败；Resource 在 reservation 作用域内拒绝会改变被锁资源的重入操作。
- 觉虫击使用 `Cost.IG.TripleUp`，reservation 锁定当前 Triple Handle，不能拆成三个独立 `RemoveActiveGameplayEffect`。检查、GAS Commit、消费之间不得插入 Montage、Delegate 广播或延迟任务；消费结束后再统一广播资源变化。

M1 固定新增两个通用原生 GE：`UMHGZStaminaCostGameplayEffect`（Instant，Stamina Additive，读取 `Data.Cost.Stamina` 负值）和 `UMHGZCooldownGameplayEffect`（HasDuration）。Ability 的 `CheckCost/ApplyCost` 计算 Instant 的 `StaminaCost × StaminaDeductionRate`；Drain Task 计算 `StaminaCostRate × StaminaConsumptionRate × ActualElapsed`，两者都构造有效 Cost Spec。`ApplyCooldown` 给通用 Cooldown Spec 设置本次 Duration 和动态 Granted CooldownTag；CooldownDuration≤0 或 Tag 无效时不 Apply。`FComboTransition::StaminaRequired` 仍只负责匹配门槛，不参与实际扣除。

退出路径只有一个幂等清理函数，负责释放尚未消费的 reservation、停止 Drain Task、取消 Timer、释放临时 Tag/GE Handle；正常完成、取消、Montage 中断、死亡和卸装都必须调用。

### 3.2 输入路由与组合键

新增 `UMHGZWeaponInputRouterComponent`，挂在本地 PlayerController。`UMHGZInputComponent` 是 Enhanced Input 和 IMC 的唯一所有者：保存所有 MappingContext 与 Binding Handle，并在重复 Setup、UnPossess、EndPlay 前逐项解除。PlayerController 不再维护第二份 DefaultMappingContexts；ASC 删除 `FAbilityInputBinding`、`bInputBound` 和全部物理输入绑定，只接收已经解析的快照。

每种武器引用一个 `UWeaponInputProfile : UPrimaryDataAsset`，至少声明：

```text
RawAction -> PhysicalInputTag
FWeaponChordDefinition[]:
  OutputTag
  TriggerControls[]           // 需要在 GracePeriod 内组成的普通触发键；至少 1 个
  RequiredHeldModifiers[]     // LT/RT 可提前长按，也可在 Trigger 候选等待期内最后补齐
  bRequireExactModifiers      // 默认 true，额外 LT/RT 会阻止该 Chord
  Priority
  bConsumeTriggerControls
  DispatchPolicy              // OnPress（默认）/ OnReleaseIfUnconsumed
  ReleaseControlTag           // 需要释放身份的动作显式指定
  AimSnapshotContext          // None / Kinsect / Action / Slinger
ChordGracePeriod
DirectionInputThreshold
ForwardConeHalfAngle
```

路由器输出不可变快照：

```cpp
struct FWeaponInputSnapshot
{
    FGameplayTag ResolvedInputTag;
    FGameplayTag SourceControlTag;
    FGameplayTagContainer HeldModifierTags;
    FGameplayTagContainer ContextTags; // Grounded/Aerial、Sheathed/Unsheathed、Aim 子集
    FVector2D RawMoveInput;
    FVector WorldDirection;
    EDirectionalInput Direction;
    FWeaponAimSnapshot Aim;       // Context、Origin、Direction、TargetPoint、可选真实 HitResult
    EWeaponInputPhase Phase;       // Started / Triggered / Completed
    double Timestamp;
    uint32 SequenceID;
};
```

冻结规则：

- 只延迟“可能参与组合”的普通键；不参与组合的键零额外等待。
- 每个 Chord 的全部 `TriggerControls` 必须在 `ChordGracePeriod` 内 Started；`RequiredHeldModifiers` 可以在第一枚 Trigger 之前已持有，也可以在 Trigger 候选仍处于 GracePeriod 时最后按下。任意必需成员 Started 都重新评估候选，因此 `RT→Y→B`、`Y→B→RT` 和 `LT→RT`/`RT→LT` 都能在窗口内得到同一组合结果。
- 候选按 RequiredHeldModifiers 数量、TriggerControls 数量、Priority 排序；形成组合后只发一次并消费 TriggerControls。默认 ExactModifiers 保证额外 LT/RT 不会误落到较弱组合。Modifier 自身不因早已持有而受 GracePeriod 限制，但在最后补齐时必须发生在 Trigger 候选超时前。
- 每个物理 Started 先保存自己的方向快照。组合成功的解析时刻是“最后一个必需成员使组合变完整”的时刻，方向、姿态、HeldModifier、Aim 和 Timestamp 全部在此刻冻结；组合失败后派发单键时仍使用该单键最初 Started 的方向，不能在 GracePeriod 超时点重新采样。
- Chord 声明 AimSnapshotContext 时，Router 在组合解析时调用 Character AimComponent 的 `CaptureAimSnapshot(Context)` 执行新射线并冻结 Aim；预输入消费和 GA 激活时不得重新读取相机。`LT+B` 操虫斩/`LT+RT` 虫印使用 Kinsect Aim，`RT+Y+B` 觉虫击使用 Action Aim。
- 离散武器动作只由 Started/组合解析产生一次；EnhancedInput 的逐帧 Triggered 只更新模拟量/held 状态，不重复派发攻击，除非未来某个 InputProfile 明确增加 RepeatPolicy。默认 `OnPress` Chord 在形成时发出 Started，Completed 只用于释放身份和清 held 状态。
- `OnReleaseIfUnconsumed` 是 InputRouter 的显式例外：只能配置为一个 TriggerControl、无 RequiredHeldModifiers、无 ReleaseControlTag 的兜底 Chord。它在该物理键 Completed 时重新核对 ContextTags；若本次按压尚未被任一获胜 Chord 消费，才按该按压的 `SourceControlTag + SequenceID` 发出一次新的 Started 快照，不再为此离散动作补发 Completed。任何含该物理键的获胜组合都会消费兜底，松开后不得泄漏单键动作。默认 `OnPress` 保持全部既有资产的行为不变。
- `HeldModifierTags`、方向和姿态在事件产生时快照；GA 激活后不得重新读取摇杆决定是不是“前”。
- Coordinator 消费时仍以当前 Combo SourceState 处理预输入，但 Grounded/Aerial、Sheathed/Unsheathed 等姿态必须与 Input.ContextTags 兼容；GracePeriod 内发生落地/拔刀变化时不能把旧姿态输入解释成新姿态动作。
- 世界方向先由相机水平 Forward/Right 与摇杆合成，再相对角色 Forward/Right 分类。角色面朝画面左时，摇杆左可以得到 `Forward`。
- `Completed` 携带原 `SourceControlTag + SequenceID`。需要释放事件的 Ability 只订阅自己的激活输入身份；删除“任意 Completed 在 Charging 时发送统一 ChargeReleased”的全局逻辑。
- 收刀/持刀/空中语义由路由器结合 ASC 姿态 Tag 解析。路由器通过当前 RuntimeHost 的 TagLedger 持有 `Aiming.Kinsect/Action/Slinger` Token；失去控制器、换装、死亡时按 Token 释放。
- InputProfile 只描述键与通用上下文；虫棍的动作选择仍在 ComboData，不在路由器里写动作类。
- 所有解析后的离散输入共用 `HandleResolvedInputSnapshot` 入口。武器动作转交 Coordinator；`Input.Dodge` 等角色通用动作按明确 AbilityTag/SpecHandle 激活并携带同一快照，仍不得回退读取物理键或 `GetLastMovementInputVector()`。
- Grounded/Aerial、Sheathed/Unsheathed 等 Pawn 姿态由 RuntimeHost/CombatState 按当前 Avatar 初始化和维护，并通过 TagLedger/有身份事件修改。ASC 初始化不永久写入默认姿态 Tag；换 Pawn 后必须从新 Pawn 的真实状态重建。

### 3.3 连招 FSM

目标结构正式使用 `FComboTransition/Transitions`。`FComboNode/ComboTable` 只代表旧包的历史序列化名称；最终 `DA_IG_Combo` 从空壳新建，不转存旧最小节点。

```cpp
struct FComboTransition
{
    FName TransitionID;
    FName SourceState;
    bool bMatchAnyState;
    TArray<FName> BlockedSourceStates;
    FGameplayTag InputTag;              // 自动转移可为空
    EDirectionalInput Direction;
    EComboExecutionPolicy ExecutionPolicy; // ActivateAbility / StateOnly
    TSubclassOf<UGameplayAbility> AbilityClass;
    FName TargetState;
    EComboStatePolicy StatePolicy;       // Replace / Preserve
    EComboLandingPolicy LandingPolicy;   // ResetToIdle / AbilityOwned
    FGameplayTagContainer RequiredTags;
    FGameplayTagContainer BlockedTags;
    float StaminaRequired;
    bool bRequiresComboWindow;
    ETransitionGrantTiming GrantTiming; // OnActivation / OnFirstHit
    FGameplayTagContainer GrantedTags;
    bool bAutoTransition;
    int32 Priority;
};
```

`Direction=None` 表示“不要求方向”，不是“摇杆必须回中”。`StatePolicy=Preserve` 只用于不改变当前连招状态的侧向命令；它不得授予跨招式 Branch Tag。会打断当前动作的命令必须使用 `Replace` 并配置明确 TargetState。

Preserve 转移 Commit 成功后只消费 Pending/ActivationContext，不替换 ActiveTransition；其临时状态由该 GA 自己按 Ability 生命周期持有。若一个命令需要中断、改变派生或在结束时回 Idle，它就不是 Preserve，必须建明确的 Replace/Auto 转移。

运行时上下文：

```cpp
struct FWeaponAbilityActivationContext
{
    FWeaponRuntimeToken RuntimeToken;
    uint32 ActivationSequenceID;
    FName TransitionID;
    FName SourceState;
    FName TargetState;
    FWeaponInputSnapshot Input;
};

struct FWeaponActionToken
{
    FWeaponRuntimeToken RuntimeToken;
    FGameplayAbilitySpecHandle AbilityHandle;
    uint32 ActivationSequenceID;
    TWeakObjectPtr<UGameplayAbility> AbilityInstance;
};

struct FWeaponMontageRegistration
{
    FWeaponActionToken ActionToken;
    TWeakObjectPtr<USkeletalMeshComponent> Mesh;
    int32 MontageInstanceID = INDEX_NONE;
};

struct FActiveComboTransition
{
    FName TransitionID;
    FWeaponActionToken ActionToken;
    FName SourceState;
    FName TargetState;
    FGameplayTagContainer OwnedTags;
    bool bFirstHitReceived;
};
```

所有“窗口/动作态/临时分支”Loose Tag 统一经 RuntimeHost 内的 `FWeaponRuntimeTagLedger`：

```cpp
struct FWeaponTagOwnerID
{
    FWeaponRuntimeToken RuntimeToken;
    EWeaponTagOwnerKind Kind; // Input / Transition / Ability / NotifyWindow / Resource
    FGameplayAbilitySpecHandle AbilityHandle;
    uint32 ActivationSequenceID;
    FName LocalID;             // TransitionID 或 NotifyEventID
};

struct FWeaponOwnedTagToken
{
    FWeaponRuntimeToken RuntimeToken;
    uint64 TokenID;
};
```

`Acquire(OwnerID, Tags)` 对每个 Tag 增加一次 Loose Count 并返回唯一 Token；`Release(Token)` 只回退该 Token 增加的计数，重复释放无效果；`ReleaseOwner`/`ReleaseAll` 用于 Ability End 与 RuntimeHost Shutdown。精华/三灯/成本/冷却等有持续时间或数值的状态仍由 Active GE Handle 管理，不放进 Ledger。禁止 Notify 对象保存 Token，也禁止调用“按 Tag 全删”破坏其他拥有者。

协调器固定使用 `InstancedPerActor`；所有离散武器动作 GA 固定使用 `InstancedPerExecution`。协调器还维护一个短生命周期 `FPendingComboTransition`，保存候选 Transition、AbilityHandle、ActivationSequenceID、ActivationContext 和旧 ActionToken，但不授予 Tag、不修改 CurrentState。原因是 GAS 的 `TryActivateAbility` 返回 true 只表示开始激活，不保证 GA 内部 Commit 成功。

RuntimeHost 维护 Montage/Notify 注册表，将 `(SkeletalMeshComponent, MontageInstanceID)` 精确映射到 `FWeaponActionToken`。GA 在 Montage 真正开始后登记，在 Montage 结束或任意 Ability End 路径注销。UE5.6 Notify 的四参数覆写从 `FAnimNotifyEventReference::GetContextData<UE::Anim::FAnimNotifyMontageInstanceContext>()` 读取 `MontageInstanceID`（头文件 `Animation/ActiveMontageInstanceScope.h`）；Context 缺失的非 Montage 调用对玩家动作 Notify 视为无效。AttackCollision、ComboWindow、DodgeWindow、CounterWindow 等只用该 ID 和当前 Mesh 解析 ActionToken，再向该实例提交事件；禁止遍历 `GetActivatableAbilities()`，禁止 Notify 对象保存跨播放实例的可变状态。

`UGameplayAbility::EndAbility` 原生签名不携带项目结束原因，因此通用动作基类提供 `RequestEndAction(EWeaponActionEndReason Reason)`：首次调用原子保存 Normal/Cancelled/Interrupted/Superseded/Hit/Landed/Death/WeaponChanged/RuntimeShutdown，再走原生 Cancel/End；`EndAbility` 的统一清理读取已保存原因，未设置时默认为 Normal。重复 Request 不覆盖首个终止原因。

所有输入转移与自动转移共用一个 `ExecuteTransition`：

1. InputProfile 已把修饰键解析进唯一 ResolvedInputTag；协调器按 InputTag 精确匹配，再按精确状态优先、具体方向优先、Priority 高优先筛选；完全并列是 Data Validation 错误。
2. 检查窗口、Tag、耐力门槛和武器资源预条件。
3. `ActivateAbility` 边建立 PendingTransition，把包含 RuntimeToken/Sequence 的 ActivationContext 注册到 ASC，再调用已授予 Spec 的 `TryActivateAbility`；TryActivate 直接失败则清 Pending。
4. 武器 GA 在 `ActivateAbility` 开头读取 ActivationContext，建立 ActionToken 候选并执行 §3.1 reservation/Commit 事务。成功后调用 `ConfirmTransitionActivation(ActionToken)`；失败则 Release/Reject 并 EndAbility。
5. 协调器只在收到与 Pending、RuntimeToken、SpecHandle、Sequence、AbilityInstance 全部匹配的 Confirm 后提交 `CurrentState`、ActiveTransition 与 OnActivation Tags。Confirm 不重新检查已被合法消费的 RequiredTags；Reject、取消或身份不匹配时状态完全不变。
6. Replace 转移采用明确的两阶段交接：新动作先 Commit+Confirm 成为 ActiveTransition，随后协调器向旧 ActionToken 的精确 AbilityInstance 调用 `RequestEndAction(Superseded)`，再释放旧转移 Tag。新 GA 只有在 Confirm 被接受后才能启动 Montage；同一 AbilityClass/Spec 的连续派生依靠 `InstancedPerExecution` 分离实例。
7. 新状态转移接管后，旧 Ability 的迟到 Hit/End/Notify 因 ActionToken 不匹配而被忽略；旧实例只能清理自己登记的 Token、Task 与 WarpTarget。
8. `OnFirstHit` Tag 只能由拥有当前转移的 ActionToken 授予。进入下一转移时，先完成两阶段提交，再释放旧转移 Tag；若新状态仍需要该 Branch Tag，新转移必须显式重新授予。
9. `StateOnly` 只允许自动边使用：验证 Source ActionToken 仍拥有当前状态后，不激活新 GA，直接更新 ActiveTransition 到 TargetState 并执行标签交接。输入边或新的独立动作不得用 StateOnly。
10. 自动派生传 `TransitionID + SourceActionToken`；禁止按 TargetState 查“第一条”边，也禁止 GA 直接 `TryActivateAbilityByTag` 绕过 FSM。
11. CMC 落地时先检查 ActiveTransition.LandingPolicy：ResetToIdle 走统一 `ResetCombo(Landed)`；AbilityOwned 则把 Landing Hit 交给仍匹配 ActionToken 的当前 GA，不改状态，待它完成落地段后请求 StateOnly Auto→Idle。急袭突刺和降龙使用 AbilityOwned；其他 Demo 空中动作默认 ResetToIdle。
12. 受击、死亡、换装和 SafetyTimeout 使用统一 `ResetCombo(Reason)`；它清 Pending、按 ActionToken 结束实例、按所有权释放 Active Tag，然后根据真实姿态回 `Idle`/`Aerial.Free`。

GA 不再从协调器读取易变的全局 `PreviousState` 决定入口；从本次 `FWeaponAbilityActivationContext.SourceState` 选择 Montage Section。

ComboData Validation 还必须拒绝：重复/空 TransitionID、输入边缺 InputTag、自动边带普通 InputTag、Replace 缺 TargetState、Preserve 携带 GrantedTags、输入边使用 StateOnly、StateOnly 不是自动边或仍填写 AbilityClass、ActivateAbility 缺 AbilityClass、bMatchAnyState=false 却填写 BlockedSourceStates、地面转移配置 AbilityOwned Landing，以及同一条件下 Priority 完全并列。

### 3.4 武器资源宿主与清理顺序

ASC、AttributeSet 和持久装备数据留在 PlayerState。所有引用当前 Pawn、控制器、Montage、猎虫 Actor、粉尘 Actor、虫印 Hitzone、位移任务或 Timer 的武器运行时对象由 Character 的 `UMHGZWeaponRuntimeHostComponent` 持有。

```cpp
struct FWeaponRuntimeToken
{
    TWeakObjectPtr<UMHGZWeaponRuntimeHostComponent> Host;
    uint64 Generation = 0; // 只在同一 Host 内单调递增
};

struct FWeaponRuntimeContext
{
    FWeaponRuntimeToken RuntimeToken;
    TWeakObjectPtr<ACharacter> Character;
    TWeakObjectPtr<APlayerController> Controller;
    TWeakObjectPtr<UMHGZAbilitySystemComponent> ASC;
    TWeakObjectPtr<UMHGZEquipmentComponent> Equipment;
    TObjectPtr<const UMHGZWeaponDefinition> WeaponDefinition;
};

class UMHGZWeaponResourceComponent
{
    InitializeRuntime(const FWeaponRuntimeContext& Context);
    ShutdownRuntime(EWeaponRuntimeEndReason Reason);
};
```

EquipmentComponent 将变化拆成两条事件：`OnEquipmentStatsChanged` 只触发属性/词条重算；`OnEquippedWeaponChanged(FEquippedWeaponSnapshot)` 只描述武器槽身份。Snapshot 至少包含 EquipmentInstance 身份、WeaponDefinition、RuntimeDefinition 与单调递增的 WeaponRevision。它不直接创建 Resource、授予/移除武器 GA 或激活协调器。

RuntimeHost 只在 EquipmentInstance 或 RuntimeDefinition 身份真正变化时重建；护甲、饰品、镶嵌和同一武器快照的重复广播必须 no-op，不得清空精华、舞踏、猎虫或连招。RuntimeHost 从 RuntimeDefinition 取得 ResourceClass/InputProfile/CombatConfig/Widget，再从 CombatConfig 取得 ComboData；它请求 ASC 授予/移除 Ability、启停协调器并创建/销毁 Resource。`InitializeRuntime` 与 `ShutdownRuntime` 都必须幂等。

RuntimeHost 初始化时先订阅 Equipment Delegate，再主动读取一次当前武器，避免 Character 在装备事件之后才生成而漏接；保存 DelegateHandle，并在每次完整重建前递增 RuntimeGeneration。所有 ActivationContext、移动/命中/资源延迟回调都携带 `FWeaponRuntimeToken{Host, Generation}`，同时核对 Host 与 Generation；仅比较数值不足以区分两个不同 Character 上同为 1 的世代。Shutdown 时解除订阅。

冻结清理顺序：

1. RuntimeHost 标记 `bShuttingDown`，拒绝新武器输入和资源请求。
2. 取消当前武器 Ability 与协调器，等待其同步清理回调结束。
3. 停止位移所有者、攻击窗口、ProjectileMovement、Tick、Timer 和 Delegate。
4. 广播 `OnWeaponRuntimeInvalidated(CurrentToken)`；本地 HUD 先解绑该 Token 的 Resource Widget，AimComponent 解除该 Runtime 绑定。角色 Health/Stamina 的 ASC 绑定只在 Possess/Avatar 变化时切换，不因单纯换武器解除。RuntimeHost 不直接依赖 UI 模块。
5. 虫棍资源召回或销毁猎虫，解除粉尘预留并销毁所属粉尘，清除虫印和舞踏。
6. 按保存的 Handle 移除精华/三灯/临时 GE；TagLedger `ReleaseAll(CurrentToken)` 清全部临时 Loose Tag Token。
7. 广播资源失效，最后 `DestroyComponent`。Demo 未启用 WeaponResource EntryModifier，不把 `ClearAllEntryModifiers` 作为依赖；未来接通词条后只按来源句柄清理。

所有 Runtime/UI 的延迟回调都携带并核对完整 `FWeaponRuntimeToken`。`AMHGZHUD` 是本地 Widget 树的唯一所有者：Possess 时绑定当前 RuntimeHost，且只接受该 Host 的 Token；资源 Widget 由 HUD 创建并插入 WBP_HUD 的资源插槽，不单独 AddToViewport。Resource Widget 只持有 `TWeakObjectPtr<UMHGZWeaponResourceComponent>`；HUD 可以强持有 Widget，但不能因此延长旧 Character 或 ResourceComponent 的寿命。当前空壳 `UMHGZUISubsystem` 在迁移后删除；未来若引入 LocalPlayer ViewModel，Subsystem 也不得拥有 Widget。

PlayerState 不再只为虫棍 Runtime Tick；Resource 内不得 `Cast<APawn>(GetOwner())` 假设 Owner 是 Pawn。

AimComponent 不得只在 `BeginPlay` 尝试一次查找 PlayerState/ASC。它在 RuntimeHost/ASC ActorInfo Ready 时绑定当前 Avatar 与 RuntimeToken，在 UnPossess、Avatar 替换和 Runtime Invalidated 时按 DelegateHandle 解绑并重新建立；重复 Ready 必须幂等。

### 3.5 Hitzone、武器与猎虫碰撞

碰撞方案冻结为“**Hitzone Object Channel + 猎虫显式 Sweep**”，不再保留 Kinsect Object Channel/Overlap 二选一。

Project Settings 新增 `Hitzone` Object Channel（`bTraceType=false`）。合同如下：

| 组件/查询 | Object Type | 关键响应与用途 |
|---|---|---|
| 怪物/木桩实体 Body | Pawn 或 WorldDynamic | 负责玩家与怪物实体阻挡，不提供精华；Demo 对 Visibility=Ignore，让部位负责准心命中 |
| `UMHGZMonsterHitzoneComponent` | Hitzone | QueryOnly；对 Weapon/Visibility Trace=Block；Pawn/WorldStatic=Ignore |
| 玩家 Capsule/Hurtbox | Pawn | 接收 MonsterAttack 查询；与怪物 Body 的阻挡独立 |
| 猎虫 Collision Root | WorldDynamic | Mesh 无碰撞；只对 WorldStatic=Block，其余 Ignore |

武器攻击继续使用已有 Weapon Trace Channel 和 `FWeaponTraceRegion`，但只接受 `UMHGZMonsterHitzoneComponent` 命中。Aim 使用 Visibility 单射线：WorldStatic 与 Hitzone 都 Block，命中后还必须验证组件 ObjectType=Hitzone；因此墙会遮挡准心，木桩 Body 不会抢在部位前返回。

猎虫实现规则：

- `UKinsectCollisionComponent` 成为 Actor Root，`ProjectileMovement.UpdatedComponent` 明确指向它；Mesh 只负责表现。
- AKinsect Actor Tick 显式依赖 ProjectileMovement 的 Component Tick，确保 CurrentTransform/缓存 World Hit 已更新后再做 Previous→Current Hitzone Sweep；每次 Tick 结束才写回 PreviousTransform。
- 物理组件只处理 WorldStatic 阻挡，不订阅 Hitzone Overlap。
- 飞行中保存上一帧 Transform，从上一帧到当前帧执行与猎虫尺寸一致的 Capsule Sweep，ObjectTypes 只包含 Hitzone。
- Sweep 命中按 `Hit.Time` 排序；同一飞行实例使用 `FlightInstanceID`。SingleHit 取首个有效 Hitzone 后结束，Piercing 按 Hitzone 组件维护可配置命中间隔。
- 多跳只对本次真实 Sweep 命中的 Hitzone 结算，不缓存离开轨迹后的 Actor 继续 Timer 伤害。
- 每次伤害保留原始 `FHitResult`；萃取读取命中组件的 `ExtractColorTag`，不得再按 Hitzone 名字硬编码颜色。

这样 Weapon 仍是“攻击查询通道”，Hitzone 是“被查询对象身份”，两者语义不混用。

### 3.6 反击窗口与 IncomingHit 前置解析

所有怪物/木桩对玩家的命中先提交给 Character 的 `UMHGZIncomingHitResolverComponent`，不得由攻击碰撞直接 Apply 玩家伤害 GE：

```cpp
struct FIncomingHitContext
{
    FGuid AttackInstanceID;
    FHitResult Hit;
    TWeakObjectPtr<AActor> SourceActor;
    FGameplayTag SourceAttackTag;
    bool bCounterable;
    float Damage;
    FGameplayTag StaggerTag;
};

enum class EIncomingHitInterceptResult : uint8
{
    Pass,
    Consume
};
```

- Resolver 先按 AttackInstanceID 去重，再按 Priority 调用当前注册的拦截 Token；Token 绑定 AbilityHandle、ActivationSequence 和清理回调。
- 突进回旋斩的 Counter NotifyBegin 注册 Token，NotifyEnd/Ability Cancel/受击失败/死亡/卸装均幂等注销。
- 只有 `bCounterable=true` 的 Context 可以被 Consume。成功时把 ID 记录为已处理，不创建伤害 Spec，并通知拥有该 Token 的回旋斩请求确定的舞踏自动转移。
- Pass 后由 Resolver 统一构造玩家受击 Spec 并 Apply。碰撞方可减少重复 Submit，但最终“同一 AttackInstanceID 最多结算一次”的权威去重在目标 Resolver。
- 已处理 ID 使用有容量和过期时间的缓存；容量/TTL 至少覆盖木桩攻击的最长 ActiveDuration，不能无限增长。
- Counter 不是 Invincible Tag。环境伤害、不可反击攻击和窗口外命中正常进入伤害链。

### 3.7 伤害上下文、硬直与反馈

新增 `FMHGZGameplayEffectContext : FGameplayEffectContext`。Demo 至少保存：

```text
OriginalHitResult
AttackInstanceID
SourceActionTag
HitzoneTag
DamageSourceType           // Weapon / Kinsect / Powder / DummyAttack
HitCueTag / ElementCueTag
```

必须在 AbilitySystemGlobals 中注册自定义 EffectContext，并实现复制/序列化所需覆写；即使本轮单机，也不能把不可复制的裸组件指针作为唯一真相源。

最低实现合同包括 `UMHGZAbilitySystemGlobals::AllocGameplayEffectContext`、Context 的 `GetScriptStruct`、深拷贝 HitResult 的 `Duplicate`、`NetSerialize` 与对应 `TStructOpsTypeTraits`。项目启动自动化测试必须确认 `MakeEffectContext()` 实际分配的是 `FMHGZGameplayEffectContext`，并验证 Duplicate 后 HitResult/AttackInstanceID/Tag 不丢失。

职责固定为：

- 攻击方：创建 Context，写真实 HitResult/攻击身份；SetByCaller 只传 MotionValue、BaseStagger、AttackPowerOverride、CritOverride、DanceMultiplier 等标量。
- ExecCalc：读取 Context、Hitzone 与属性，依次输出 Meta Attribute `IncomingDamage`、`IncomingStagger`、`IncomingCriticalFlag`，最后输出 `IncomingHitSignal=1`；零/负 MotionValue 的 IncomingDamage 为 0，但需要反馈/反击的有效命中仍可提交 HitSignal。
- AttributeSet `PostGameplayEffectExecute`：只在处理最后的 `IncomingHitSignal` 时原子读取并清零上述 Meta 值，Clamp/扣 Health，生成一次 `FMHGZHitFeedbackResult`，再发送硬直事件。这样一次 GE 只产生一次反馈，也支持零伤害有效命中。
- UE5.6 当前按 `FGameplayEffectCustomExecutionOutput::OutputModifiers` 的插入顺序逐项 `InternalExecuteMod`，且每项后立即调用 PostGameplayEffectExecute，因此 HitSignal 必须最后 Add；M2 用自动化测试锁定这一引擎合同，未来升级引擎时先跑该测试。
- 目标的 `UMHGZHitFeedbackRouterComponent`：根据已结算结果显式 Execute GameplayCue、提交伤害数字、镜头/音效和卡肉请求。DynamicAssetTag 只作为数据标签，不被当作“自动触发 Cue”。
- ASC 位于 PlayerState 时，受击表现和 HitReaction 的物理目标必须从 TargetASC.ActorInfo 的 AvatarActor 取得 Character；只有无 Avatar 的木桩/怪物 ASC 才回退 OwnerActor。不得用 `AttributeSet::GetOwningActor()` 默认等同当前 Pawn。
- 卡肉使用可叠加、带请求 Token 的控制器；禁止 Ability 直接把 `CustomTimeDilation` 写成 0.05 后再无条件恢复 1.0。

Demo 公式保持 [伤害计算](exec-calc.md) 的简化基线；完整伤害模型只在 Context 与 ExecCalc 阶段扩展，不修改每个攻击 GA。

### 3.8 空中位移所有权

所有非纯动画位移通过一个通用任务族执行，建议入口为 `UAbilityTask_MHGZMovement`，内部按模式选择算法：

```cpp
enum class EWeaponMovementMode : uint8
{
    BoundedDirectional,
    BallisticVault,
    AdditiveInertia
};

struct FWeaponMovementRequest
{
    FWeaponActionToken OwnerAction;
    EWeaponMovementMode Mode;
    FVector DirectionSnapshot;
    float MaxDistance;
    float Duration;
    TObjectPtr<UCurveFloat> DistanceCurve;
    FVector InheritedVelocity;
    float InheritedVelocityRatio;
    EBallisticParameterMode BallisticMode; // ApexHeightAndDuration / ExplicitLaunchVelocity
    float ApexHeight;
    FVector LaunchVelocity;
    float AirControlScale;
    EActionRotationPolicy RotationPolicy; // Locked / FaceDirection / SteerWithinCone
    float MaxTurnRateDegrees;
    float SteeringConeHalfAngle;
    FName WarpTargetName;                 // 由 RuntimeHost 按 ActionToken 生成唯一名称
    EMovementCollisionPolicy CollisionPolicy;
    EMovementCancelVelocityPolicy CancelVelocityPolicy;
};

struct FWeaponMovementResult
{
    EWeaponMovementEndReason EndReason;
    float TravelledDistance;
    FVector FinalVelocity;
    FHitResult BlockingHit;
};
```

冻结规则：

- CharacterMovementComponent 是唯一物理移动载体；同一角色同一时刻只有一个由 ActionToken 标识的 WeaponMovement Token。该 Token 同时拥有本动作的平移、角色旋转、转向策略和 WarpTarget，不只拥有位移。
- 新位移启动前必须按动作规则拒绝或取消旧 Token，不能同时由 Timeline、LaunchCharacter、MotionWarping 和 RootMotionSource 写位置。Character 普通 locomotion 在动作拥有旋转时不得无条件 `SetActorRotation`，只能把转向输入交给 MovementTask 的 SteeringPolicy。
- 任务统一处理正常结束、命中 Hitzone、碰墙、落地、受击、Ability Cancel、死亡与卸装，并始终返回 Result。
- CMC `OnLanded` 是姿态落地的唯一权威事件。MovementTask 可以以 Landing 结束并返回 BlockingHit，但不得另行 ResetCombo；Character 只向 Coordinator/当前 Ability 分发一次带 Handle/Token 的落地通知。
- `FinalVelocity` 明确交还 CMC；强化跳跃斩、急袭突刺使用 AdditiveInertia，舞踏弹跳使用 BallisticVault，滑翔/操虫斩/降龙定向阶段使用 BoundedDirectional。
- BallisticVault 必须明确选择“最高点+时长求解初速度”或“显式 LaunchVelocity”之一；Data Validation 拒绝两者同时有效或都无效。AdditiveInertia 的 AirControlScale 只缩放本任务内输入修正，不永久改 CMC 配置。
- MotionWarping 只修正已经验证存在 Root Motion 的动画段；零根位移动画不得靠 Warp Target 凭空产生距离。每个动作按 RuntimeToken+Sequence 生成唯一 WarpTargetName，正常结束、Superseded、Montage 中断、取消、死亡、换装和失败早退都必须精确移除自己的 WarpTarget。

### 3.9 虫棍资源内部真相源

唯一 `UInsectGlaiveCombatConfig` 由 `DA_WeaponRuntime_IG` 引用，保存默认红灯模式、唯一 ComboData、四个精华 GE Class、精华/三灯时长与倍率、舞踏倍率和全部虫棍特殊位移参数。Resource 不再硬编码 `/Game/...` 类路径。方向/组合键阈值只存在于同一 RuntimeDefinition 引用的通用 `UWeaponInputProfile`。RuntimeHost 的 `ActiveRedExtractMode` 初始化自该默认值，并按模式拥有 `Combat.Config.IG.RedMode.Classic` 或 `Numeric` 中恰好一个；调试覆写不修改共享 DataAsset。同一张 ComboData 通过 Tag 条件分流，不复制调参或整张出招表。

`URes_InsectGlaive` 只保存运行时状态：

```text
ActiveExtractHandles[color]
TripleUpHandle
KinsectActor
DanceStacks
ActiveMark                    // 弱 Hitzone 引用、局部位置、到期时间
ActiveMarkProjectiles         // Resource 拥有的弱 Actor 集合，发射后可独立于 GA 飞行
OwnedPowders                  // 弱 Actor 集合
ActiveReservations            // 本资源发起的粉尘预留
```

所有放虫路径构造单个 `FKinsectFlightRequest`，包含轨迹、距离、Damage/Extract 模式、动作值、间隔和 `FlightInstanceID`。`DeployKinsect(Request)` 必须先更新全部参数与 FlightInstanceID/命中表，再启动 ProjectileMovement 和 Hitzone Sweep；每次实际命中才生成独立 `HitInstanceID` 写入 EffectContext 的 `AttackInstanceID`；禁止“先飞行、后 SetDamageParams”的两步接口。

- 三灯是否有效要求 `TripleUpHandle` 对应的 Active GE 仍可从 ASC 查询到；Handle 的结构 `IsValid()` 不能单独证明 GE 存活。不再同时维护可能失步的 `bTripleUpActive`。
- `ApplyExtract(Color)` 入口第一步检查三灯：有效则吞灯并只发轻量反馈；不创建单灯 GE、不缓存、不刷新。
- 三色单灯全部有效时，先成功 Apply Triple GE，再移除单灯；Apply 失败时保留原单灯状态。
- Triple GE 不授予三个 `WeaponResource.IG.Extract.*` 单灯状态 Tag，但授予 `Combat.Branch.Extract.Red`，保证 Classic 模式三灯期间仍使用完整动作；该动作权限随 Triple Handle 一起移除。
- `TryConsumeTripleUpAtomic()` 先验证 ASC 中仍存在对应 Active GE，再一次移除该 Handle；匹配身份的 GE 移除回调负责清状态并广播，失败不产生部分状态。
- GE 移除 Delegate 负责到期清理和 UI 广播；Timer 只做表现或非 GE Actor 生命周期。
- 舞踏只由操虫斩命中和突进回旋斩反击成功增加；所有清空原因使用同一个带枚举原因的 `ResetDance`。
- 虫印、粉尘不放进 ASC/PlayerState/SaveGame，Pawn/武器生命周期结束时清理。
- 普通 FirstHitOnly 飞行只在 `PendingExtractColor` 为空时写入首个有效颜色并进入 Hover。返回到达必须先原子取出并清空 Pending，再切到 Attached、调用一次 `ApplyExtract`；因此下一次 Flight 可以获取新颜色。返回中重新送虫保留尚未交付的 Pending，不允许后续命中覆盖它。
- 猎虫耐力归零按“上一帧 > 0 且本帧 <= 0，并且不在 Returning/Attached”这一阈值边沿只触发一次 ForceRecall 和一次警告音效；归零后的每帧 Tick 不得重复召回或播放。到达玩家后直接进入 Attached，开始附着恢复。
- `FKinsectFlightRequest` 明确保存起飞点、最大飞行距离、ToPoint 到达半径和 FlightInstanceID。距离从本次起飞点累计；超过距离、到达目标、碰 WorldStatic、被召回或被新合法 Request 中断都产生唯一结束原因，并关闭本 Flight 的 Sweep/伤害。

## 4. 实施里程碑

### M0：配置、标签与验证地基

**修改范围：** Project Settings、GameplayTags、最终 C++/DataAsset 结构、资产引用清点、审计用 Redirect 与 Data Validation/自动化测试骨架；不创建具体动作 GA。

**工作：**

1. 新增 Hitzone Object Channel 并统一木桩 Hitzone/Body 碰撞预设。
2. 按 [重构范围与资产处置](demo-refactor-scope.md) 清点目标蓝图/DataAsset/Montage 及用户工作树基线；建立 Keep/Rewrite/Delete/Defer 执行表，禁止覆盖已有改动。
3. 定义 WeaponRuntimeDefinition、RuntimeToken/ActionToken/TagLedger、InputProfile、CombatConfig、Transition、ActivationContext、CostReservation、EffectContext、MovementRequest/Result 的最终 C++ 结构。
4. 给 TransitionID 唯一性、并列匹配、ExecutionPolicy/AbilityClass 合法组合、CombatConfig 必需 GE/Combo 引用、方向阈值、Dance 数组长度、位移正值、Ballistic 参数互斥、伤害段显式 MotionValue，以及 LockedTargetTicks 的正距离/间隔添加 Data Validation。
5. 定义唯一 `DA_IG_Combat`、`DA_IG_Combo`、`DA_IG_InputProfile` 的最终类型与校验；为实际旧序列化名称添加精确 Redirect 以完成只读加载审计。E0 不重存旧包，最终数据壳由 E3 从最终类型新建。

**退出条件：** 项目编译；资产清点、保留/删除/重建映射和引用链完整；最终类型与精确 Redirect 可只读加载旧包且无 Missing Property；数据验证可以故意构造重复 TransitionID/错误舞踏数组并稳定报错；Hitzone 与 Body 碰撞互不承担对方职责。

### M1：Ability 生命周期、输入路由与 FSM

**修改范围：** `ActionSystem`、`InputSystem`、Character 上的 RuntimeHost/TagLedger 最小骨架及对应测试；不接入具体武器 Resource，不实现虫棍特殊状态。

**工作：**

1. 实现成本/冷却与 reservation 事务合同，删除空 GE Spec、永久 Loose Cooldown 和旧 float 武器成本。
2. 让 InputComponent 独占 IMC/Binding；实现 WeaponInputRouter、InputSnapshot、任意必需成员补齐组合、输入释放身份和重复 Possess 安全重绑；从 ASC 删除 Enhanced Input。
3. 以 `FComboTransition/Transitions` 实现统一 `ExecuteTransition`；不为旧最小 Combo 资产增加运行时兼容行为。
4. 建立 RuntimeHost/TagLedger/ActionToken/Notify Registry 最小生命周期；动作 GA 固定 InstancedPerExecution，协调器固定 InstancedPerActor，实现 Superseded 两阶段交接、精确 MontageInstance Notify、旧回调隔离、自动派生和落地重置。
5. 将 Pawn 姿态初始化从 ASC 一次性默认 Tag 移到 RuntimeHost/CombatState，并接通 `HandleResolvedInputSnapshot` 的武器与角色通用两条逻辑分发。
6. 重写基础 `GA_Dodge`：使用解析后的方向快照和 AbilityTask Montage；DodgeWindow 通过 ActionToken 获取实例，以 TagLedger 持有窗口，并缓存/恢复每个被修改碰撞通道的原响应。缺角色、Montage、AnimInstance 或 Commit 失败均走统一 End 清理；Demo 不读取旧 Dodge DataTable 并行配置。
7. 先用两到三个无虫棍资源的占位 GA 验证 Idle→A→B、同类 GA 连续重入、组合键、方向分流、TryActivate/Commit 失败和资源 reservation 回滚。

**退出条件：** Y+B 不泄漏 Y/B；LT/RT 先按或最后补齐都能在 GracePeriod 内解析同一组合；角色朝画面左+摇杆左得到 Forward；重复 Possess/Setup 不重复 IMC 或回调；TryActivate/Commit 任一失败都保持原状态、回滚 reservation 且不授予标签；两个重叠窗口关闭一个后 Tag 仍有效；同一 GA Class 连续重入时 Notify 只调用所属 Montage 实例；旧 Ability 以 Superseded 结束且迟到回调不重置新状态；Dodge 缺 Montage/中断后无残留 Tag 或碰撞响应；任意 Completed 不再误释放其他 Ability。

### M2：RuntimeHost、命中上下文与训练木桩

**修改范围：** Equipment、Character、AttributeSystem、Monster、AttackAbility、Feedback；不实现虫棍特殊动作。

**工作：**

1. 在 M1 RuntimeHost 骨架上实现 Equipment/Resource 完整生命周期；拆分 StatsChanged 与 WeaponChanged Snapshot，只有武器实例/Runtime 身份变化才重建；换装/死亡/PIE End 执行固定清理顺序。
2. 接入玩家 IncomingHitResolver 与反击 Token；武器攻击保留真实 HitResult，并接入自定义 EffectContext、四个 Incoming Meta 与 FeedbackRouter。
3. 多跳默认改为真实接触策略；只有显式 LockedTarget 策略允许离散复击并逐跳重验。
4. 木桩建立独立 Body、三个不重叠的 Red/White/Orange Hitzone 与确定性反击攻击器。
5. 接入可叠加卡肉 Token；Cue/数字可先使用临时表现资产。
6. 保留角色/木桩核心蓝图并按最终父类接线；删除 `DefaultGame.ini` 的 `WeaponComboConfig`、旧 DataManager Getter、Equipment 旧表读取，以及 Attack 旧字段的所有运行时读取。旧 Collision/Socket/成本字段只作为不参与决策的序列化壳保留到 E3 删除旧 GA；所有失败早退必须 End 并清 Action/Movement/Warp Token。

**退出条件：** 同帧多 Region 只结算最早 Hitzone；MotionValue=0 不扣血；护甲/饰品/镶嵌变化不重建武器 Runtime 或清空临时资源，同一武器重复 Snapshot no-op，真正换武器后无旧 Resource/Timer/Tag；不同 Character 上相同 Generation 数值的 Token 不相等；旧 Host 回调不能操作新运行时；旧 Attack 字段和 DataTable 的运行时读取归零；反击测试器的同一 AttackInstanceID 最多结算一次；两个卡肉请求不会互相提前恢复。

### M3：虫棍资源、基础猎虫、精华与瞄准

**修改范围：** `InsectGlaive`、`URes_InsectGlaive`、Aim/UI 基础绑定；不实现新增特殊攻击。

**进入条件：** §1.4 的普通放虫命中后 Hover、主动召回交付规则已写入动作/资源/验收文档。

**工作：**

1. 猎虫改为 Collision Root + Projectile UpdatedComponent + Hitzone 显式 Sweep。
2. Hitzone 直接提供 Red/White/Orange；删除 Yellow 与部位名映射。
3. 实现统一 ApplyExtract、Triple GE 到期、三灯吞灯和原子消费接口。
4. 接入持刀 LT 猎虫瞄准、收刀 RT 拔刀直飞、普通送虫/召回。
5. 实现 LT+RT 唯一虫印及其清理规则。
6. 修正 PendingExtract 到达时原子取出并清空、连续飞行取不同颜色、返回中保留规则，以及耐力归零阈值只触发一次召回/音效；冻结起飞点距离和 ToPoint 到达半径；`UInsectGlaiveKinsectData` 新增 `ReturnSpeed`（召回速度）字段并接入召回飞行。
7. Character 侧 RB 奔跑分流：`SprintAction` 键位改 RB；`SprintPressed` 收刀态按住 ≥0.1s 置 `bSprintHeld`，拔刀态 return。仅持刀地面态的 RB 由 Router 输出 `Input.Sheathe`（`Unsheathed+Grounded`，E3 InputProfile 配置），`GA_Sheathe` 在 M4/E4 实现前为 no-op。
8. 资源音效接线：`InsectGlaiveCombatConfig` 新增萃取成功、三灯激活/到期、猎虫耐力归零四个 `USoundBase` 字段；把 CombatConfig 传入 `URes_InsectGlaive::InitializeRuntime`，`PlayResourceSound` 改从配置读取，并删除 `URes_InsectGlaive` 遗留的硬编码资产路径依赖。

M3 输入接线补充：`FWeaponChordDefinition` 增加通用 `RequiredContextTags/BlockedContextTags`。`Input.Weapon.RT` 的收刀地面 Chord 使用单成员 RT 并要求 `Sheathed+Grounded`，因此按下立即冻结 ActorForward；持刀态该候选不成立，RT 可继续作为 modifier 与 LT 组成 `Input.Weapon.LTRT`。`Input.Sheathe` 使用单成员 RB 并要求 `Unsheathed+Grounded`，因此空中不产生纳刀快照。M4.3 再新增通用 `DispatchPolicy=OnReleaseIfUnconsumed`，供持刀地面单 RT 虫印斩使用；空中 RT 急袭突刺仍是独立的 `Aerial` + `OnPress` Chord。四个基础猎虫动作由原生 GA 父类构造完整 Flight/Aim 请求，E4 蓝图子类只做资产接线。

贯通伤害身份补充：`FKinsectFlightRequest.FlightInstanceID` 表示整次 Flight；每次实际命中再生成独立 HitInstanceID 写入 GameplayEffectContext 的 `AttackInstanceID`，避免 IncomingHitResolver 的“同 AttackInstanceID 只结算一次”规则把觉虫击后续贯通 Tick 误判为重复命中。

**退出条件：** 三部位都能正确点灯；三灯期间所有吸收路径只吞灯且不刷新；Triple GE Apply 失败不会丢单灯；猎虫高速穿过 Hitzone 仍可 Sweep 命中；回手后 Pending 为空且下一次能取得另一颜色；耐力持续为 0 时只播放一次警告并只请求一次召回；收刀保留虫印而卸装清除。

### M4.1：E4.1 最小纵切运行时接线

> **历史条目映射：** 本节冻结细节中的 `M4-A.2`、`M4-A.3.1`、`M4-A.4`、`M4-A.5` 分别读作 **M4.1.2、M4.1.3.1、M4.1.4、M4.1.5**；`E4-A` 读作 **E4.1**。这些是已完成历史条目，不是新的任务名。

> **状态入口：** M4.1.2～M4.1.5 的原生合同、动作数据和验收范围如下；实际完成度、已知阻塞项与下一步只查 [阶段门禁](milestone-gates.md#7-当前项目位置与下一步)。上述 M4.1/E4.1、M4.2～M4.5 与 M4.2.1 已于 2026-09-03 签收；当前允许进入 M4.6，E4.3/M4.7/M5 仍受其各自前置门禁限制。

**修改范围：** 通用 ActionToken 的 Montage Root Motion 所有权与单 Token 提前释放、收刀/翻滚期间的 `Combat.State.Sheathing`/`Combat.State.Dodging` 输入互斥、攻击→翻滚的精确取消窗口、`UMHGZDodgeAbility` 的锁定/MoveExit 阶段和方向选择、`UMHGZSheatheAbility` 与 `GA_Sheathe` 的最终原生运行时语义、Y 拔刀专属 Ability/Commit、收/拔刀 Commit 驱动的武器视觉 Socket 切换、普通攻击的激活瞬转、持刀送虫/收虫的无伤害 Montage/Commit，以及对应的 Combo/GA/Montage 接线与测试；不实现其余地面动作、特殊技或完整 Transition 表。

**进入条件：** E4.1 已创建并保存 KinsectData、四个精华 GE、三个基础猎虫 GA、收刀资产，以及已完成的前向 `GA_Dodge`/Montage；它们均使用最终父类和最终 DataAsset 引用，不存在临时输入/姿态/资源通路。持刀左右后 Dodge 资产必须等待 M4.1.3.1 的新直接字段编译后再创建；`GA_IG_Draw`/`GA_IG_DrawSlash` 必须等待 M4.1.4 的专用原生父类和 `DrawCommit` Notify 编译后再创建。不得以临时 Montage/蓝图子类或 Event Graph 写姿态来绕过这些依赖。

**工作：**

1. 在 RuntimeHost 建立按 `FWeaponActionToken` 归属的 `MontageRootMotionOwner`；在通用 GA 基类建立 `ReleaseActionTag(FWeaponOwnedTagToken&)`，只能释放本 Ability 已获得的一个 Tag Token，并从结束清理列表移除。Character 的 `BlockMovement` 与 `bForceMMIdle` 计算按 [Motion Matching 设计](motion-matching.md#81-动作移动阶段blockmovement-与-montage-root-motion) 分离；不得以清全局 Tag 或裸 bool 绕过 Token。
2. **M4.1.3.1：扩展最终 `GA_Dodge`。** 它与 `GA_Sheathe` 一样只加入一次 CoreAbilities、使用唯一 `Input.Dodge` Spec，不进入 ComboData。保留已完成的前向实现：收刀或持刀的 `Forward/None` 都选对应的前向 Montage，运行时先固定 `DodgeCore -> IdleExit`；`UMHGZDodgeAbility` 直接绑定本次 `FAnimMontageInstance::OnMontageSectionChanged`，仅在实际进入 `IdleExit` 的 Section 边界读取**实时原始**摇杆，并且只对允许移动衔接的前向变体立即跳进 `MoveExit`。原生类新增 `UnsheathedLeftDodgeMontage`、`UnsheathedRightDodgeMontage`、`UnsheathedBackDodgeMontage`（或等价最终直接字段），并在激活时根据冻结的姿态与 `InputSnapshot.Direction` 生成 `FDodgeSelection{Montage,bAllowMoveExit}`：持刀 Left/Right/Back 选对应 Montage 且强制 IdleExit；收刀 Left/Right/Back 直接拒绝。旧方向 Montage Map 仍只为迁移保留，运行时最多回退 `Forward/None`，不承担新方向选择。所有成功变体复用同一 `Instant` 耐力成本；无效方向、缺所需 Montage、依赖无效或 Commit 失败均不得扣耐。验证 Montage 时所有有效变体需要 `DodgeCore` 与 `IdleExit`，只有 `bAllowMoveExit=true` 的前向变体才要求/允许 `MoveExit`；左右后 Montage 不得进入 MoveExit。Action 成功 Commit 后获取 `Combat.State.Dodging` Token 并持有至 EndAbility，禁止普通玩家动作和再次 Dodge 激活；硬直、死亡、换装等强制路径可中断。攻击 Montage 的原生 `AnimNotifyState_DodgeAcceptWindow` 只在该攻击允许翻滚取消的后摇持有 `Combat.State.DodgeAcceptOpen`；它不是 Dodge Montage 的 `DodgeWindow` 无敌帧。交接先由 Coordinator 对精确旧攻击 Prepare，避免 UE 在播放新 Montage 时把旧动作误记为 Interrupted；新 Dodge 启动并登记成功后才 Commit `RequestEndAction(Superseded)`，启动失败则 Cancel Prepare 并保持旧攻击。`AttackCollision`、`ComboWindow`、`DodgeAcceptWindow` 等仍经 MontageInstanceID/ActionToken 精确解析；Dodge 出口与 MoveExit 移动锁切换不依赖 AnimNotify 或全局反查。
3. 完成 `UMHGZSheatheAbility`：InputProfile 的 `Input.Sheathe` 必须要求 `Unsheathed+Grounded`；`CanActivateAbility` 必须重新确认当前为 Grounded、拒绝 Aerial，并拒绝 Dead、Attacking、Hitstun、Knockdown、Sheathing、Dodging，随后才检查 Mesh/AnimInstance/Montage/选定 Section。读取激活快照选择 `AM_Shth_ShouDao` 的 `Idle`/`Walk` Section，二者之后均不切换 Section；Idle 持有 BlockMovement、Walk 不持有而允许实时摇杆转向；含 RM 期间均持有根位移所有权。`AnimNotify_SheatheCommit` 在武器实际挂回背部时切换 Sheathed，Commit 前中断保持 Unsheathed、Commit 后中断保持 Sheathed。Action 成功提交后还必须获取 `Combat.State.Sheathing` Token 并持有至 `EndAbility`：它不是姿态 Tag，也不是 BlockMovement，Commit 前后都存在；普通按键 GA 的 `CanActivateAbility` 和 ComboCoordinator 都必须拒绝它，不能建立 PendingTransition。只有硬直/死亡等显式强制取消可豁免。编译通过后创建最终数据型蓝图子类 `GA_Sheathe`，仅填写 Montage/Section/成本等数据并加入 `CoreAbilities`；Event Graph 不得手写 Tag。它不进入 ComboData，不直接写 Loose Tag。
4. **M4-A.4（原生完成；编辑器接线待后续）：Y 拔刀分流。** `UMHGZDrawAttackAbility : UMHGZInsectGlaiveAbility` 与原生 `AnimNotify_DrawCommit` 已新增。它在激活前确认 `Grounded+Sheathed`，但只在 Montage 中武器实际取出时按当前 ActionToken 调用该 Ability，由 Ability 统一 `RuntimeHost->SetSheathed(false)` 并清 `bSprintHeld`；Commit 前中断保持 Sheathed，Commit 后中断保持 Unsheathed。收刀 RT 的 `UMHGZDrawAndSendKinsectAbility` 直接继承 `UMHGZGameplayAbility`，播放无 Root Motion 的 `UpperBody_IGAction` `ActionMontage`，不持有 Attacking、BlockMovement 或 MontageRootMotionOwner；它在 DrawCommit 成功后才切姿态，随后在同一 Montage 的 KinsectSendCommit 才按冻结 ActorForward 部署。`DA_IG_Combo` 接线仍只使用一个 `Input.Weapon.Y` Chord，配置两条 `Idle` 起手边：`Direction=None`（通配，含无/左/右/后输入）→ `DrawOnly` → `GA_IG_Draw`，无 AttackSegment；`Direction=Forward`（具体方向优先于 None）→ `GroundStarter` → `GA_IG_DrawSlash`，有 AttackSegments。二者均要求 `Grounded+Sheathed`，均经 Coordinator 的 Pending→Commit→Confirm 与 ActionToken 路径。
5. **M4-A.5：收敛普通攻击方向修正。** `UMHGZAttackAbility::ApplyDirectionCorrection` 在 Action 已 Confirm、Montage 尚未播放时只读取冻结的 `ActivationContext.Input.WorldDirection`，把它投影到水平面；存在输入、`MaxCorrectionAngle>0` 且角色当前 Yaw 到目标 Yaw 的绝对差不超过该值时，直接 `SetActorRotation` 到目标 Yaw。它不读取实时摇杆，不生成 `AttackDirection_<...>` WarpTarget，也不要求普通攻击 Montage 配 MotionWarping Notify。普通攻击持有 BlockMovement 后由 GA 独占这一次瞬转，Root Motion 从修正后的 Actor 朝向开始执行。`MaxCorrectionAngle` 默认 30°，前推拔刀斩可配 45°，单纯拔刀配 0°；180° 只用于明确允许任意方向的动作。保留 `UMotionWarpingComponent`，但只供具有真实目标/平移或旋转对齐需求的特殊动作显式使用；当前 `FAttackSegmentConfig::MaxWarpAngle` 没有运行时消费者，不能宣称在普通多段攻击中已生效。

   **单帧招内方向修正（M4-A.5 增量，原生已实现）：** 对“某个精确帧可按摇杆补正一次朝向”的招式，原生普通 Notify `AnimNotify_ActionDirectionCorrection` 不使用 NotifyState，不创建 WarpTarget，也不使用 MotionWarping。Notify 先以 `(Mesh, MontageInstanceID)` 解析当前 `FWeaponActionToken`，再调用当前 `UMHGZAttackAbility::ApplyInActionDirectionCorrection(ActionToken, MaxCorrectionAngleOverride)`；任何旧 Montage、错误 Token、未 Commit 或已结束 Ability 均为无副作用 no-op。Ability 读取 `AMHGZCharacter::GetLastMovementInputDir()` 的**实时**方向（`BlockMovement` 时该原始方向仍会更新），复用入口相同的水平投影、夹角阈值和 `SetActorRotation` 算法。Notify 的 `MaxCorrectionAngleOverride` 默认 `-1`，表示回退到 GA 的 `MaxCorrectionAngle`；`>=0` 表示该修正帧自己的阈值，`0` 合法但不会产生转向。每个 Notify 只调用一次，绝不在 Tick 中持续读摇杆。若该帧之后的动画存在强 Root Yaw、需要重新定向根位移轨迹或对齐目标位置，则此直接修正不适用，必须由对应特殊 GA 单独实现 MotionWarping/MovementTask；不能把本 Notify 扩展为通用 WarpTarget 工具。
6. **M4-A.5：直接扩展现有持刀送虫/收虫 GA 的无伤害上半身 Montage。** 保持 `UMHGZSendKinsectAbility`、`UMHGZRecallKinsectAbility` 直接继承 `UMHGZGameplayAbility`；**不**新增 `UMHGZMontageActionAbility`，也不修改通用 `UMHGZGameplayAbility` 的公开数据字段。两个既有类各自新增 `ActionMontage`、播放速率、`UAbilityTask_PlayMontageAndWait`、Active Montage、幂等 `bCommandCommitted` 与完成/中断回调；送虫类额外以完整 `FKinsectFlightRequest PendingRequest` 保存 Confirm 后的冻结请求。两类在 Action 已 Confirm 后先校验 Montage/Mesh/AnimInstance；Send 构造并保存 PendingRequest，Recall 只验证冻结的 Unsheathed 输入与当前猎虫非 Attached；随后播放 Montage，并立刻将当前 `Mesh + MontageInstanceID → ActionToken` 注册。原生普通 Notify `AnimNotify_KinsectSendCommit` 与 `AnimNotify_KinsectRecallCommit` 必须先按 Montage 实例精确解析当前 ActionToken，再分别调用现有类的 `CommitSendKinsect` / `CommitRecallKinsect`。Send Commit 只能使用保存的 PendingRequest，重验 Resource、猎虫和 RuntimeToken 后 `DeployKinsect`，不得重读实时 Aim/相机/摇杆；Recall Commit 重验 Resource 与猎虫仍非 Attached 后 `RecallKinsect`。Commit 前中断不得改变猎虫 Flight/Return 状态；Commit 后中断保留已经开始的飞行/召回；Montage 正常播完但未发生 Commit 视为 Cancelled。`UMHGZGameplayAbility::EndAbility` 已按 ActionToken 注销 Montage/Action，因此子类只清自己的 Task/临时状态，不新增注册表。两类**不得**继承 `UMHGZAttackAbility`，不得添加 `Combat.State.Attacking`、`Combat.State.BlockMovement`、方向修正、`AttackSegments`、`AttackCollision` 或任何伤害/命中路径，也不得取得 `MontageRootMotionOwner`。两条正式 Montage 均为 in-place 的 `UpperBody_IGAction` Slot：上半身表现与武器挂点来自 Montage，下半身保持现有 Armed Motion Matching，动作途中实时摇杆可正常移动/转向。编辑器在实际出手/召回手势帧添加对应普通 Notify；两条 Montage 均不配置 AttackSegment、AttackCollision、ComboWindow 或 DodgeWindow。
7. 回归收刀 RT 拔刀直飞、持刀 LT+Y 送虫、LT+B 收虫与 RB 纳刀，确认它们都只使用 E4-A 的正式 InputProfile、Resource、GA 与姿态 Token。
8. 为上述语义增加最小自动化测试：收刀的 Idle/Walk、仅 Grounded+Unsheathed 可激活、Aerial/Dead/攻击/硬直/击倒/收刀中/翻滚中拒绝、Commit 前/后中断、缺 Commit 完成、旧/竞争 Token、输入锁、Dodge/攻击/猎虫输入拒绝、结束后按当前姿态恢复合法输入；前向的动态 Exit、持刀左右后强制 IdleExit、收刀左右后拒绝且不耗耐、所有有效 Dodge 同一耐力成本、Dodge 失败不吞旧攻击、Dodge 全程拒绝攻击/猎虫/收刀/再次 Dodge、强制硬直/死亡中断后无残留 Tag 或碰撞响应；两个 Y 拔刀边的方向优先级、DrawCommit 前后中断、DrawOnly 零碰撞、Forward DrawSlash 的 AttackSegment；收刀 RT DrawAndSend 的 DrawCommit 前后姿态、SendCommit 前后猎虫、错误/竞争 Token、无 Attacking/BlockMovement/RootMotionOwner；普通攻击入口无输入/角度阈值内/阈值外的瞬转与无 WarpTarget，及招内修正的精确 Token、实时方向、`-1` 回退、`0` no-op、超阈值/错误或结束 Token no-op；持刀送虫/收虫的 Montage 实例注册、各自 Commit 前后中断、Send 的 PendingRequest 冻结 Aim 与 Commit 重验、Recall 的非 Attached 重验、无伤害/无碰撞/无 Attacking/无 BlockMovement/无 RootMotionOwner，以及动作期间持刀 Motion Matching 仍可响应实时移动与转向。不得因未接入木桩而伪造精华/伤害通过。

**退出条件：** 收刀或持刀的前向/无方向翻滚本体不受摇杆改变轨迹；它们仅在自己的 MoveExit 阶段按实时摇杆进入移动衔接，无输入稳定走 IdleExit。持刀左/右/后翻滚使用各自 Montage 且强制回 Idle；收刀左/右/后拒绝且不扣耐。所有允许的翻滚消耗同一笔耐力。攻击只有精确 `DodgeAcceptWindow` 内才能启动 Dodge，Dodge 成功后才精确结束旧攻击，Dodge 失败保持旧攻击；翻滚全程持有 `Combat.State.Dodging`，攻击/猎虫/收刀/再次 Dodge 不激活，硬直/死亡/换装可中断且无残留 Tag/碰撞响应。收刀仅在 `Grounded+Unsheathed` 下由 RB 输入和原生激活检查共同允许，空中或 Dead/攻击/硬直/击倒/收刀中/翻滚中都不激活；收刀 RT 成功后仅一次切 Unsheathed 并停止奔跑；移动收刀选段不在中途切换但可实时转向，站立收刀全程锁移动；持刀 RB 在 `SheatheCommit` 前后中断分别保持 Unsheathed/Sheathed，且从收刀开始到 Montage 结束都持有 `Combat.State.Sheathing`：Y/B/RT/LT 组合与 Dodge 不激活、不创建 PendingTransition、不播放第二个 Montage，结束后才按当前姿态恢复合法输入。MM 与 Montage 从不同时拥有根位移。收刀 Y 的 None/非 Forward 输入只拔刀，Forward+Y 拔刀攻击；二者与收刀 RT 拔刀直飞都在各自视觉 Commit 前后正确保持姿态，且经 Coordinator/ActionToken 的路径不残留窗口或碰撞。普通攻击入口只在激活快照与角度阈值允许时瞬转一次，不创建普通攻击 WarpTarget；配置 `Action Direction Correction` 的招内帧只在精确当前 ActionToken 下读取一次实时摇杆，并在自己的阈值允许时直接转向一次。持刀 LT+Y/LT+B 仅在各自现有 GA 的 `UpperBody_IGAction` Montage 的精确 `KinsectSendCommit`/`KinsectRecallCommit` 后启动送虫/收虫，Commit 前中断不改变猎虫状态，Commit 后中断不回滚已开始的 Flight/Return。动作全程不持有 Attacking、BlockMovement 或 MontageRootMotionOwner，实时摇杆持续驱动持刀 Motion Matching 的下半身移动/转向，且全过程无攻击碰撞或伤害。动作退出的自然回到移动必须另经 M4-B.1A/E4-A.6/PMM-6B 验收；只有 M4-A 最终验收后，才可继续 M4-B.1B、E4-B 或 M4-B。

### M4.3～M4.7：输入补丁、动作退出、攻击入口与地面动作

> **历史条目映射：** 本节下方尚未拆分的冻结细节中，`M4-B.0`、`M4-B.1A`、`M4-B.1B`、`M4-B` 分别读作 **M4.3、M4.4、M4.6、M4.7**；其实际执行顺序与门禁以 [阶段门禁](milestone-gates.md) 的 `M4.2 → M4.3 → M4.4 → E4.2 → M4.5 → M4.2.1 → M4.6 → E4.3 → M4.7` 为准。后续新增内容不得再使用旧字母编号。

**修改范围：** `InputSystem` 的通用 ReleaseFallback、虫棍 ComboData、地面 GA/Montage/Notify 与虫印 Resource 接口；不实现完整空中动作。

> **历史执行记录：** 本节的进入条件已在后续 M4.3、M4.4、E4.2、M4.5 与 M4.2.1 中满足并完成验收。当前不得以本节的旧顺序推断工作项；现行门禁是 M4.6，E4.3/M4.7/M5 仍保持阻塞，详见 [阶段门禁](milestone-gates.md#7-当前项目位置与唯一允许的下一步)。

**工作：**

**本节的执行编号：**

1. **M4.3 输入释放补丁**：对应下方条目 1；字段、Router 状态机、测试与验收以 [M4.3 详细设计与实施](m4.3-input-release-implementation.md) 为准；完成后才允许 M4.4。
2. **M4.4 动作退出 / 根运动交接**：对应下方条目 2；字段、原子所有权顺序、Telemetry 和自动化证据以 [M4.4 详细设计及实施记录](m4.4-action-handoff-implementation.md) 为准；代码完成后由 E4.2 做资产接线。
3. **M4.5 动作退出固定矩阵**：不在下方批量动作条目中实现，按 [纯 Motion Matching 普通移动实施指南](pure-motion-matching-locomotion-guide.md#7-已裁定的实施顺序与门禁) 的矩阵验收。
4. **M4.2.1 普通移动档位重搜 / Blend**：按 [纯 Motion Matching 普通移动实施指南](pure-motion-matching-locomotion-guide.md#421-档位改变的一次性重搜合同) 实现并验收；前置为 M4.5。
5. **M4.6 攻击 Entry Section**：对应下方条目 3；前置为 M4.5 与 M4.2.1。
6. **M4.7 地面招式 / 虫印**：对应下方条目 4～12；前置为 M4.6 与 E4.3。

1. **M4-B.0：** 在 `FWeaponChordDefinition` 增加 `DispatchPolicy`，默认 `OnPress`；实现并测试 `OnReleaseIfUnconsumed`。它必须保持既有单键/组合键排序、快照身份、释放身份和多次 Possess 行为，不改 ASC、Coordinator 或任何具体虫棍动作判断。
2. **M4-B.1A：在任何批量攻击资产前，建立唯一的动作退出/根运动交接基础设施。** 新增原生 `AnimNotifyState_ActionRootMotionPhase` 与 `AnimNotify_MotionMatchingHandoff`（最终名称可微调）。二者只能按 `(Mesh, MontageInstanceID)` 解析 ActionToken 并转发给所属 Ability；Ability 才能用相同 Token 调 RuntimeHost 获取/释放 Root Motion owner。Root Motion Phase 覆盖每个真实根位移片段；in-place 片段无所有权；玩法需要位移但动画没有真实 Root Motion 时用 MovementTask。Handoff 只能发生在所有 Commit 已完成、后续没有玩法 Notify 和 Root Motion 的无功能安全帧；它记录 HandoffType、原始摇杆、松杆边沿和 Telemetry，并把搜索路由到 Exit PSD/Chooser。失败时保持原 Montage 与所有权。该阶段只用于 E4-A 已存在的收刀 Walk、前向 Dodge MoveExit 以及 Telemetry 已证明失败的拔刀/突刺，不实现新攻击、Section 映射或 Combo 分支。最小自动化覆盖旧 NotifyEnd 不能释放新 Token、in-place 片段无 Owner、Handoff 失败不提前释放、Root Motion 段结束后 Exit/MM 才能接管。
3. **M4-B.1B：在 PMM-6B 和 M4-A 最终验收后，接通目标攻击 GA 的入口 Section。** `UGA_WeaponComboCoordinator::ExecuteTransition` 已经把 `TransitionID`、`SourceState`、`TargetState` 和冻结 `InputSnapshot` 写入 `FWeaponAbilityActivationContext`；不得在 Coordinator 新增 Montage/Section 分支，`FComboTransition` 仍只引用 `AbilityClass`。扩展 `UMHGZAttackAbility` 的最终配置/钩子（字段最终命名可微调）：`DefaultEntrySection`、`EntrySectionByTransitionID`、`EntrySectionBySourceState` 与 `SelectAttackMontageStartSection()`。选择优先级固定为 **TransitionID → SourceState → Default → `NAME_None`（Montage 开头）**；在 Action 已 Confirm、`CreatePlayMontageAndWaitProxy` 尚未创建时验证并传入 StartSection，禁止先从开头播放再 `Montage_JumpToSection`。Data Validation 必须拒绝空/不存在的映射 Section，且校验映射的 TransitionID 确实以此 GA 为 `AbilityClass`；运行时配置失效时在开始 Montage 前 Cancel，不得播放错误开头。B.1A 的 Root Motion Phase 与 Handoff 是唯一的位移所有权接口，不得在 B.1B 重建第二条路径。最小自动化覆盖优先级、无入口映射、无效 Section 预播放取消、A→B 成功后 B 从 `Entry_From_A` 启动、B Commit 失败时 A 未被 Superseded。
4. 在 InputProfile 配置三条互斥的 `Input.Weapon.RT` Chord：收刀地面 `Sheathed+Grounded` + `OnPress` 为拔刀直飞；持刀地面 `Unsheathed+Grounded` + `OnReleaseIfUnconsumed` 为虫印斩；空中 `Aerial` + `OnPress` 为急袭突刺。空中条目可在 M5 接入对应 GA，但不要求 Sheathed/Unsheathed。
5. E3 已确认旧 GA/Montage/Combo 包删除后，移除只为这些包保留的 Attack 序列化旧字段与临时兼容代码，再接通《世界》地面基底与两个红灯模式。
6. 实现四连印斩及 `StarterOnly` 派生限制。
7. 实现突进回旋斩位移、反击窗口、AttackInstance 消费和反击舞踏弹跳入口。
8. 实现 `GA_IG_MarkSlash`：持刀单 RT 松开且未组合时激活，作为普通近战 AttackSegment 命中；首次有效 Hitzone 命中调用 Resource 的近战虫印入口建立/替换唯一虫印。它不得复用 `UMHGZMarkKinsectTargetAbility` 的虫印弹发射路径。
9. 将 Resource 的唯一虫印接口从“必须拥有 `AIGMarkProjectile`”扩展为同时支持 Projectile 与近战 Hitzone HitResult；两条来源共享替换、局部附着点、到期、目标失效和卸装清理规则。
10. 实现远程虫印弹与猎虫滑翔的地面激活部分。
11. 建立每条特殊转移的唯一 TransitionID 和自动收尾边。
12. 回归 M4.1 的 `GA_Sheathe`（`AM_Shth_ShouDao` 的 `Idle`/`Walk` Section，分别使用 `AS_Shth_ShouDao_Idle/Walk`；`SheatheCommit` 切姿态、Montage RM 结束后交给 MM）与拔刀姿态接线（Y 拔刀、收刀 RT 拔刀直飞、奔跑中拔刀：在实际取出 Commit 点 `SetSheathed(false)` + 清 `bSprintHeld`）；M4.7 不重复实现收刀原生语义。

**退出条件：** 每个有入口差异的目标 GA 都由已 Confirm 的冻结上下文从正确 `Entry_From_*` Section 启动；Coordinator/ComboData 不持有动画路径，A→B 的衔接只在 B 成功 Commit 后出现，B 失败时 A 不被提前结束。真实 Root Motion 片段与 MM 从不同时输出位移；in-place 片段不伪造所有权，无根位移的玩法移动由 MovementTask 接管。两种红灯模式行为符合设计；Y+B/前+Y+B 稳定分流；四连/未反击回旋斩后只能接四种起手；窗口内反击吞掉该次木桩攻击，窗口外正常受击。

### M5：舞踏、空中位移与终结动作

**修改范围：** MovementTask、虫棍空中 GA、Montage/Notify、ComboData。

**进入条件：** M4.7、M4.6 与 M4.5 已完成；不得以旧的直接全库动作退出路径接通空中资产。

**工作：**

1. 完成 BoundedDirectional/BallisticVault/AdditiveInertia；Movement Token 同时仲裁平移、旋转、转向与唯一 WarpTarget，Character locomotion 在 Token 存活时让出旋转所有权。
2. 实现操虫斩两种 AimSource、命中点灯和舞踏。
3. 实现强化操虫穿刺的来源限制。
4. 接入强化跳跃斩惯性、急袭突刺和降龙；删除跳跃突进斩转移。
5. 实现落地、受击、收刀、终结动作对舞踏/空中状态的统一清理。

**退出条件：** 只有两种指定来源增加舞踏；倍率封顶且按段快照；两种操虫斩方向正确；惯性动作末速度连续；动作转向不被 Character Tick 覆盖；任一取消路径都只剩一个 CMC 移动所有者且不存在残留 WarpTarget。

### M6：觉虫击、粉尘、UI 与反馈闭环

**修改范围：** 虫棍特殊 GA、Powder Actor、HUD/Widget、GameplayCue 临时资产。

**进入条件：** §1.4 的觉虫击每次有效贯通伤害立即 ApplyExtract 规则已写入动作/资源/验收文档。

**工作：**

1. 实现觉虫击三灯原子消费、±60° 修正、猎虫贯通，以及每次成功伤害后的即时 ApplyExtract+粉尘生成和猎人位移。
2. 实现己方通用粉尘、Reserved/Consumed 状态和粉尘集约取消回滚。
3. 删除当前空壳 UISubsystem；由 HUD 独占 WBP_HUD 与资源面板生命周期，在 Possess 时绑定 RuntimeHost，把 ResourceWidget 插入主 HUD 资源插槽，并通过 Resource/Aim/Feedback Delegate 显示三灯、猎虫、舞踏、准心颜色和伤害数字；不轮询 Actor、不写 UI Loose Tag、不让资源面板单独 AddToViewport。
4. 完成命中 Cue、三灯提示、临时音效和必要调试可视化。

**退出条件：** 觉虫击失败不消耗、成功只消费一次；每次成功贯通伤害恰好对应一次 ApplyExtract 和一团粉尘，三色可重新形成三灯且后续吞灯不刷新；伤害提交失败不点灯也不产粉尘；只有粉尘集约能引爆；取消集约恢复 Reserved 粉尘；WBP_HUD 与资源面板只有 HUD 一个所有者，资源面板只存在于指定插槽；换装/死亡后无旧绑定，旧 Pawn 的 Ready/Invalidated 回调不能污染新 Pawn UI。

### M7：集成、打包与回归

**修改范围：** 只修复验收发现的问题，不新增系统。

**工作：**

1. 执行 [编辑器搭建指南](../editor/demo-setup.md) 和 [验证清单](../editor/verification.md) 的全部当前 Demo 项。
2. 运行 Development Editor 编译、自动化测试、PIE 多轮重开、换装/死亡/落地/受击压力测试。
3. 打包 Win64 Development，验证不依赖编辑器临时对象或硬编码加载路径。
4. 更新文档的“已实现/规划”状态和剩余已知问题。

**退出条件：** 打包 Demo 可从进入训练场到完成全部动作验证；连续多次 PIE/换装无残留 Actor、Timer、Delegate、Loose Tag、GE 或 RootMotionSource；没有未说明的验收跳过项。

## 5. 禁止的实现捷径

- 不在通用 Input/GA/Coordinator 中按 WeaponType 写虫棍分支。
- 不把世界 Actor、Hitzone、猎虫、粉尘或 MovementTask 引用存进 PlayerState 持久层。
- 不用全局 `PendingGrantedTags`、无所有者 Loose Tag 或裸 Timer 表示动作状态。
- 不让 AnimNotify 扫描全部 Active Ability，也不让同一个 Notify 对象保存播放实例状态；必须由 MontageInstanceID 解析 ActionToken。
- 不让 ASC 或 PlayerController 建立第二套 Enhanced Input/IMC 绑定。
- 不用 DynamicAssetTag 代替 `ExecuteGameplayCue`，不在 ExecCalc 内直接播放表现。
- 不丢弃真实 HitResult 后再按 BoneName/Actor 反查命中点。
- 不让 MultiHit Timer 对已经离开接触范围的缓存目标继续伤害，除非动作显式选择 LockedTarget 策略并逐跳重验。
- 不直接覆盖 `CustomTimeDilation` 后无条件恢复 1.0。
- 不用 MotionWarping 给零 Root Motion 动画制造整段位移。
- 不让 Character locomotion 与动作任务在同一帧分别写角色旋转，不复用跨动作 WarpTargetName。
- 不用 Weapon Trace Channel 充当猎虫或 Hitzone 的 Object Type。
- 不按 Hitzone 名称硬编码精华颜色，不保留 Yellow 与 Orange 两套命名。
- 不在三灯状态外再维护一个可能失步的布尔真相源。
- 不批量先建完所有 GA 蓝图再回头修底层；每个里程碑必须先通过退出条件。

## 6. 需求到里程碑追踪

| 需求 | 负责里程碑 | 最终验证 |
|---|---|---|
| 世界地面基底、两种红灯模式 | M4 | 红灯模式切换与完整连招回归 |
| Y+B / 前+Y+B 与 StarterOnly | M1、M4 | 方向场景与派生负例 |
| 回旋斩反击与舞踏 | M2、M4、M5 | 固定 AttackInstance 时序 |
| 三色、三灯吞灯、觉虫击消费 | M3、M6 | GE 到期/失败/重复输入 |
| LT/RT 三种瞄准语义 | M1、M3 | 收刀/持刀/空中状态矩阵 |
| 虫印与猎虫滑翔 | M3、M4、M5 | 有印/无印/过期/卸装 |
| 操虫斩、穿刺、舞踏、惯性 | M5 | 命中/未命中/落地/受击 |
| 急袭突刺、强化跳跃斩、降龙 | M5 | FinalVelocity 与清理检查 |
| 觉虫击、逐击点灯/粉尘、粉尘集约 | M6 | 原子消费、伤害/点灯/粉尘一一对应与预留回滚 |
| 真实命中、伤害、硬直、反馈 | M2、M6 | 零伤害、部位、Cue、卡肉 |
| 输入/动作实例/Notify 精确归属 | M1 | 重入、Superseded、迟到回调与重复 Possess |
| 装备差分与运行时生命周期 | M2 | 护甲/饰品 no-op、换武器完整清理 |
| 基础闪避 | M1 | 方向快照、碰撞响应恢复、失败路径清理 |
| RB 双语义（纳刀/奔跑） | M3、M4 | 0.1s 阈值、攻击/硬直拒绝、奔跑中拔刀中断 |
| 位移旋转与 Warp 所有权 | M2、M5 | 单写入者、唯一目标、全路径回收 |
| 无遗留生命周期对象 | M2～M7 | 多次 PIE、死亡、换装、打包 |

## 7. 开始改代码前的最终检查

只有同时满足以下条件，才开始 M0 代码修改：

1. 本文不再出现“方案 A/方案 B 二选一”的公共架构描述。
2. [虫棍动作设计](insect-glaive-actions.md) 没有与本文冲突的输入、消费、舞踏或清理规则。
3. 每个 P0/P1 差距都映射到一个里程碑和可观察退出条件。
4. [重构范围与资产处置](demo-refactor-scope.md) 的保留、引用解除、删除与重建顺序可以一次完成，不保留永久双结构兼容层。
5. 用户确认 §1.4 的玩法语义；数值未定项可以保留为 CombatConfig 参数，不阻塞接口冻结。
6. 输入绑定、动作实例、Notify、Tag、Movement/Warp、Resource reservation、Equipment Snapshot 和 Widget 树均有唯一所有者及幂等清理路径。
