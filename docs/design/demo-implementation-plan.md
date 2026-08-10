# 虫棍木桩 Demo 冻结实施计划

> **当前状态：** 已进入分阶段实施。M0 的代码、Project Settings 文本配置、验证与测试已完成；目标 `.uasset` 的首次创建、补录和重存仍待 [编辑器 E0/E1](../editor/demo-setup.md) 执行。后续阶段的实际完成状态以 Git 阶段提交和验证记录为准。

> **用途：** 当本文的公共接口、所有权和阶段退出条件确定后，再按里程碑逐步修改代码。实施时不得跨阶段顺手重构未列入范围的系统；每一阶段验收通过后才进入下一阶段。

> **迁移边界：** 现有实现允许按 [重构范围与资产迁移](demo-refactor-scope.md) 完整重写。该文档的 Keep/Rewrite/Delete/Defer 表和一次性资产迁移合同与本文同为开始改代码前的强制输入。

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
2. 本文与 [重构范围与资产迁移](demo-refactor-scope.md) 决定后续实现架构、公共接口、所有权、删除边界和实施顺序。
3. [编辑器搭建指南](../editor/demo-setup.md) 与 [验证清单](../editor/verification.md) 决定可观察验收。
4. 其他系统文档提供模块细节。
5. 当前源码只代表“现在有什么”，不能覆盖尚未实现的目标设计。

如果实施中发现必须改变本文的公共接口或玩法语义，应先停在当前里程碑并修改文档；不能一边写代码一边保留两套方案。

### 1.4 已冻结的补充玩法语义

- **觉虫击贯通萃取：** 每次产生有效贯通伤害时，立即读取该 Hitzone 的颜色并调用普通 `ApplyExtract`。觉虫击 Commit 已先消费旧三灯，因此贯通过程可以重新取得单灯，命中三种颜色时也可以重新形成三灯；一旦途中重新形成三灯，后续萃取按统一规则被吞且不刷新三灯时间。不使用颜色优先级，也不缓存“最后一种颜色”等待回手。
- **普通放虫命中后的猎虫状态：** SingleHit/FirstHitOnly 命中后立即停止伤害与 Hitzone Sweep，携带 `PendingExtractColor` 原地进入 Hovering；只有玩家主动召回或猎虫耐力归零才进入 Returning，到达玩家后交付精华。不会因取得颜色自动回手。

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

具体 `UMHGZWeaponDefinition`（物品攻击力、外观、词条等）只引用对应 RuntimeDefinition。当前 `DT_WeaponResourceConfig` 与 `DT_WeaponComboConfig` 是待迁移旧桥接；M0/M2 后不保留 DataTable 与 RuntimeDefinition 两套并行查找。虫棍 ComboData 由其 CombatConfig 继续引用。

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
- 离散武器动作只由 Started/组合解析产生一次；EnhancedInput 的逐帧 Triggered 只更新模拟量/held 状态，不重复派发攻击，除非未来某个 InputProfile 明确增加 RepeatPolicy。Completed 只用于释放身份和清 held 状态。
- `HeldModifierTags`、方向和姿态在事件产生时快照；GA 激活后不得重新读取摇杆决定是不是“前”。
- Coordinator 消费时仍以当前 Combo SourceState 处理预输入，但 Grounded/Aerial、Sheathed/Unsheathed 等姿态必须与 Input.ContextTags 兼容；GracePeriod 内发生落地/拔刀变化时不能把旧姿态输入解释成新姿态动作。
- 世界方向先由相机水平 Forward/Right 与摇杆合成，再相对角色 Forward/Right 分类。角色面朝画面左时，摇杆左可以得到 `Forward`。
- `Completed` 携带原 `SourceControlTag + SequenceID`。需要释放事件的 Ability 只订阅自己的激活输入身份；删除“任意 Completed 在 Charging 时发送统一 ChargeReleased”的全局逻辑。
- 收刀/持刀/空中语义由路由器结合 ASC 姿态 Tag 解析。路由器通过当前 RuntimeHost 的 TagLedger 持有 `Aiming.Kinsect/Action/Slinger` Token；失去控制器、换装、死亡时按 Token 释放。
- InputProfile 只描述键与通用上下文；虫棍的动作选择仍在 ComboData，不在路由器里写动作类。
- 所有解析后的离散输入共用 `HandleResolvedInputSnapshot` 入口。武器动作转交 Coordinator；`Input.Dodge` 等角色通用动作按明确 AbilityTag/SpecHandle 激活并携带同一快照，仍不得回退读取物理键或 `GetLastMovementInputVector()`。
- Grounded/Aerial、Sheathed/Unsheathed 等 Pawn 姿态由 RuntimeHost/CombatState 按当前 Avatar 初始化和维护，并通过 TagLedger/有身份事件修改。ASC 初始化不永久写入默认姿态 Tag；换 Pawn 后必须从新 Pawn 的真实状态重建。

### 3.3 连招 FSM

目标结构正式更名为 `FComboTransition`；`FComboNode`/`ComboTable` 是当前源码旧名，只允许在迁移阶段短暂存在。资产字段目标名为 `Transitions`。

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

所有放虫路径构造单个 `FKinsectFlightRequest`，包含轨迹、距离、Damage/Extract 模式、动作值、间隔和 AttackInstanceID。`DeployKinsect(Request)` 必须先更新全部参数与 FlightInstanceID/命中表，再启动 ProjectileMovement 和 Hitzone Sweep；禁止“先飞行、后 SetDamageParams”的两步接口。

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

**修改范围：** Project Settings、GameplayTags、最终 C++/DataAsset 结构、资产引用清点、Redirect 与 Data Validation/自动化测试骨架；不创建具体动作 GA。

**工作：**

1. 新增 Hitzone Object Channel 并统一木桩 Hitzone/Body 碰撞预设。
2. 按 [重构范围与资产迁移](demo-refactor-scope.md) 清点目标蓝图/DataAsset/Montage 及用户工作树基线；建立 Keep/Rewrite/Delete/Defer 执行表，禁止覆盖已有改动。
3. 定义 WeaponRuntimeDefinition、RuntimeToken/ActionToken/TagLedger、InputProfile、CombatConfig、Transition、ActivationContext、CostReservation、EffectContext、MovementRequest/Result 的最终 C++ 结构。
4. 给 TransitionID 唯一性、并列匹配、ExecutionPolicy/AbilityClass 合法组合、CombatConfig 必需 GE/Combo 引用、方向阈值、Dance 数组长度、位移正值、Ballistic 参数互斥、伤害段显式 MotionValue，以及 LockedTargetTicks 的正距离/间隔添加 Data Validation。
5. 准备唯一 `DA_IG_Combat`（默认 Classic、引用唯一 ComboData）、`DA_IG_Combo` 和唯一 `DA_IG_InputProfile`；为实际旧序列化名称添加精确 Redirect，重存本轮数据壳，不建立旧 DataTable 并行读取。

**退出条件：** 项目编译；资产清点和迁移映射完整；最终类型与精确 Redirect 可加载目标数据壳且无 Missing Property；数据验证可以故意构造重复 TransitionID/错误舞踏数组并稳定报错；Hitzone 与 Body 碰撞互不承担对方职责。

### M1：Ability 生命周期、输入路由与 FSM

**修改范围：** `ActionSystem`、`InputSystem`、Character 上的 RuntimeHost/TagLedger 最小骨架及对应测试；不迁移具体武器 Resource，不实现虫棍特殊状态。

**工作：**

1. 迁移成本/冷却与 reservation 事务合同，删除空 GE Spec、永久 Loose Cooldown 和旧 float 武器成本。
2. 让 InputComponent 独占 IMC/Binding；实现 WeaponInputRouter、InputSnapshot、任意必需成员补齐组合、输入释放身份和重复 Possess 安全重绑；从 ASC 删除 Enhanced Input。
3. 将 `FComboNode/ComboTable` 迁移为 `FComboTransition/Transitions`，实现统一 `ExecuteTransition`。
4. 建立 RuntimeHost/TagLedger/ActionToken/Notify Registry 最小生命周期；动作 GA 固定 InstancedPerExecution，协调器固定 InstancedPerActor，实现 Superseded 两阶段交接、精确 MontageInstance Notify、旧回调隔离、自动派生和落地重置。
5. 将 Pawn 姿态初始化从 ASC 一次性默认 Tag 移到 RuntimeHost/CombatState，并接通 `HandleResolvedInputSnapshot` 的武器与角色通用两条逻辑分发。
6. 重写基础 `GA_Dodge`：使用解析后的方向快照和 AbilityTask Montage；DodgeWindow 通过 ActionToken 获取实例，以 TagLedger 持有窗口，并缓存/恢复每个被修改碰撞通道的原响应。缺角色、Montage、AnimInstance 或 Commit 失败均走统一 End 清理；Demo 不读取旧 Dodge DataTable 并行配置。
7. 先用两到三个无虫棍资源的占位 GA 验证 Idle→A→B、同类 GA 连续重入、组合键、方向分流、TryActivate/Commit 失败和资源 reservation 回滚。

**退出条件：** Y+B 不泄漏 Y/B；LT/RT 先按或最后补齐都能在 GracePeriod 内解析同一组合；角色朝画面左+摇杆左得到 Forward；重复 Possess/Setup 不重复 IMC 或回调；TryActivate/Commit 任一失败都保持原状态、回滚 reservation 且不授予标签；两个重叠窗口关闭一个后 Tag 仍有效；同一 GA Class 连续重入时 Notify 只调用所属 Montage 实例；旧 Ability 以 Superseded 结束且迟到回调不重置新状态；Dodge 缺 Montage/中断后无残留 Tag 或碰撞响应；任意 Completed 不再误释放其他 Ability。

### M2：RuntimeHost、命中上下文与训练木桩

**修改范围：** Equipment、Character、AttributeSystem、Monster、AttackAbility、Feedback；不实现虫棍特殊动作。

**工作：**

1. 在 M1 RuntimeHost 骨架上迁移 Equipment/Resource 完整生命周期；拆分 StatsChanged 与 WeaponChanged Snapshot，只有武器实例/Runtime 身份变化才重建；换装/死亡/PIE End 执行固定清理顺序。
2. 接入玩家 IncomingHitResolver 与反击 Token；武器攻击保留真实 HitResult，并接入自定义 EffectContext、四个 Incoming Meta 与 FeedbackRouter。
3. 多跳默认改为真实接触策略；只有显式 LockedTarget 策略允许离散复击并逐跳重验。
4. 木桩建立独立 Body、三个不重叠的 Red/White/Orange Hitzone 与确定性反击攻击器。
5. 接入可叠加卡肉 Token；Cue/数字可先使用临时表现资产。
6. 按 M0 映射重存 AttackAbility/角色/木桩目标资产，确认最终 AttackSegment 数据后删除旧 Collision/Socket/成本字段、旧 DataManager Getter 和运行时兼容读取；所有失败早退必须 End 并清 Action/Movement/Warp Token。

**退出条件：** 同帧多 Region 只结算最早 Hitzone；MotionValue=0 不扣血；护甲/饰品/镶嵌变化不重建武器 Runtime 或清空临时资源，同一武器重复 Snapshot no-op，真正换武器后无旧 Resource/Timer/Tag；不同 Character 上相同 Generation 数值的 Token 不相等；旧 Host 回调不能操作新运行时；旧 Attack 字段和 DataTable 运行时引用归零；反击测试器的同一 AttackInstanceID 最多结算一次；两个卡肉请求不会互相提前恢复。

### M3：虫棍资源、基础猎虫、精华与瞄准

**修改范围：** `InsectGlaive`、`URes_InsectGlaive`、Aim/UI 基础绑定；不实现新增特殊攻击。

**进入条件：** §1.4 的普通放虫命中后 Hover、主动召回交付规则已写入动作/资源/验收文档。

**工作：**

1. 猎虫改为 Collision Root + Projectile UpdatedComponent + Hitzone 显式 Sweep。
2. Hitzone 直接提供 Red/White/Orange；删除 Yellow 与部位名映射。
3. 实现统一 ApplyExtract、Triple GE 到期、三灯吞灯和原子消费接口。
4. 接入持刀 LT 猎虫瞄准、收刀 RT 拔刀直飞、普通送虫/召回。
5. 实现 LT+RT 唯一虫印及其清理规则。
6. 修正 PendingExtract 到达时原子取出并清空、连续飞行取不同颜色、返回中保留规则，以及耐力归零阈值只触发一次召回/音效；冻结起飞点距离和 ToPoint 到达半径。

**退出条件：** 三部位都能正确点灯；三灯期间所有吸收路径只吞灯且不刷新；Triple GE Apply 失败不会丢单灯；猎虫高速穿过 Hitzone 仍可 Sweep 命中；回手后 Pending 为空且下一次能取得另一颜色；耐力持续为 0 时只播放一次警告并只请求一次召回；收刀保留虫印而卸装清除。

### M4：地面基底与新增地面动作

**修改范围：** 虫棍 ComboData、地面 GA/Montage/Notify；不实现完整空中动作。

**工作：**

1. 接通《世界》地面基底与两个红灯模式。
2. 实现四连印斩及 `StarterOnly` 派生限制。
3. 实现突进回旋斩位移、反击窗口、AttackInstance 消费和反击舞踏弹跳入口。
4. 实现虫印弹与猎虫滑翔的地面激活部分。
5. 建立每条特殊转移的唯一 TransitionID 和自动收尾边。

**退出条件：** 两种红灯模式行为符合设计；Y+B/前+Y+B 稳定分流；四连/未反击回旋斩后只能接四种起手；窗口内反击吞掉该次木桩攻击，窗口外正常受击。

### M5：舞踏、空中位移与终结动作

**修改范围：** MovementTask、虫棍空中 GA、Montage/Notify、ComboData。

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
| 位移旋转与 Warp 所有权 | M2、M5 | 单写入者、唯一目标、全路径回收 |
| 无遗留生命周期对象 | M2～M7 | 多次 PIE、死亡、换装、打包 |

## 7. 开始改代码前的最终检查

只有同时满足以下条件，才开始 M0 代码修改：

1. 本文不再出现“方案 A/方案 B 二选一”的公共架构描述。
2. [虫棍动作设计](insect-glaive-actions.md) 没有与本文冲突的输入、消费、舞踏或清理规则。
3. 每个 P0/P1 差距都映射到一个里程碑和可观察退出条件。
4. [重构范围与资产迁移](demo-refactor-scope.md) 的清点、重命名、Redirect、重存与删除顺序可以一次完成，不保留永久双结构兼容层。
5. 用户确认 §1.4 的玩法语义；数值未定项可以保留为 CombatConfig 参数，不阻塞接口冻结。
6. 输入绑定、动作实例、Notify、Tag、Movement/Warp、Resource reservation、Equipment Snapshot 和 Widget 树均有唯一所有者及幂等清理路径。
