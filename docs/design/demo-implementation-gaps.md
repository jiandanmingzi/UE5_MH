# 虫棍 Demo 当前实现差距

> **用途：** 本文跟踪实际源码到 [Demo 目标](../editor/demo-setup.md) 的必要改动。完成项必须从“当前问题”表移入已解决记录；未完成项继续保留原问题证据和目标里程碑。

> **范围：** 只列完成虫棍木桩 Demo 所必需的改动。装备成长、背包仓库、任务、存档、完整怪物和完整伤害模型即使仍有设计问题，也不在本轮实现范围内。

> **实施方向：** 下表说明“为什么必须改”；保留/重写/删除边界见 [重构范围与资产处置](demo-refactor-scope.md)，唯一公共接口、顺序与退出条件见 [Demo 冻结实施计划](demo-implementation-plan.md)。

## 1. 可以保留的基础

| 已实现基础 | 结论 |
|---|---|
| PlayerState 持有 ASC/AttributeSet | 身份与容器基础保留；Avatar、输入和姿态生命周期重写 |
| `UMHGZAttackAbility` 的多 Region 自适应 Sweep、时间采样 | 算法保留；配置、实例所有权、Notify 解析和命中上下文重写 |
| 原生 Damage GE/ExecCalc 与木桩 Health | 保留为 Demo 简化伤害基线；反馈载荷重写 |
| 木桩配置驱动 Hitzone | 概念保留；补 ExtractColor、实体分离和确定性攻击测试器 |
| Motion Matching 地面移动资产/骨架 | 保留；动作持有旋转/位移时普通 locomotion 必须让出 |
| EquipmentDefinition/Instance 与装备槽 | 保留为持久数据；装备统计变化与武器运行时变化拆分 |

“保留”只表示不必推倒局部算法或数据概念，不表示原文件可以原样使用。

## 2. Demo 前必须修改

### 已在 M1 解决

| 系统 | 当前实现证据 | 解决结果 |
|---|---|---|
| 成本/冷却与跨系统成本事务 | `MHGZGameplayAbility`、`MHGZAbilityCostGameplayEffects`、`AbilityTask_MHGZStaminaDrain`、Resource reservation 接口 | None/Instant/PerSecond 使用有效原生 GE/Task；reservation→Commit→Consume/Release 事务与幂等 End 已接通 |
| 输入组合与所有权 | `MHGZInputComponent`、`MHGZWeaponInputRouterComponent`、PlayerController | InputComponent 独占 IMC/Binding；组合键、不可变快照、方向与 Completed 身份已实现；重复 Setup/重绑测试通过 |
| 姿态生命周期 | `MHGZWeaponRuntimeHostComponent`、Character MovementMode/Landed 转发 | Grounded/Aerial、Sheathed/Unsheathed 改由当前 Pawn RuntimeHost 的 TagLedger 持有，不再由 ASC 一次性默认写入 |
| 连招与动作实例 | `MHGZComboCoordinatorAbility`、`MHGZGameplayAbility` | Coordinator=InstancedPerActor，Action=InstancedPerExecution；Pending/Confirm/Superseded、自动边、落地与迟到回调隔离已实现 |
| Notify 归属 | RuntimeHost Montage Registry、`MHGZAnimNotifyActionResolver`、Attack/Combo/Dodge Notify | `(Mesh, MontageInstanceID)` 精确解析 ActionToken；玩家动作 Notify 不再扫描 Active Specs |
| 闪避 | `MHGZDodgeAbility`、`AnimNotifyState_DodgeWindow` | 前向翻滚使用冻结姿态/方向选择与 AbilityTask；窗口由 Ledger 持有并逐通道恢复原碰撞响应；缺 Montage 零副作用测试通过。M4-A.3.1 仍需加入持刀左右后专用选择/强制 IdleExit，及收刀左右后拒绝且不耗耐 |

构建、15 项 M1 测试与 5 项 M0 回归证据见 [M1 实施审计](m1-implementation-audit.md)。

### 已在 M2 解决

| 系统 | 当前实现证据 | 解决结果 |
|---|---|---|
| 武器运行时与装备差分 | `MHGZEquipmentComponent`、`MHGZWeaponRuntimeHostComponent`、`MHGZWeaponResourceComponent` | Stats 与 Weapon Snapshot 事件分离；相同武器身份 no-op；真正换武器按旧 Token 失效、Ability/Coordinator、Resource、HitStop、Ledger/Registry 的固定顺序清理并以新 Generation 重建 |
| 命中、伤害与反馈 | `MHGZGameplayEffectContext`、`MHGZDamageExecCalc`、`MHGZAttributeSet`、`MHGZHitFeedbackRouterComponent` | 真实 HitResult/AttackInstanceID 进入自定义 Context；四个 Incoming Meta 原子结算；MotionValue=0 不扣血；Cue、伤害数字、卡肉和镜头只消费已结算结果 |
| 攻击轨迹与多跳 | `MHGZAttackAbility` | 运行时只读 TraceRegions；同帧多 Region 选最早命中；默认接触式去重，只有显式 LockedTargetTicks 才能离散复击且逐跳重验；每个动作使用独占 WarpTarget 名称 |
| 玩家来袭与反击窗口 | `MHGZIncomingHitResolverComponent` | Hit 必须属于 Resolver Owner；AttackInstanceID 权威去重；反击 Token 绑定当前 Runtime/Action 身份、优先级和 TTL；Apply 失败回滚去重记录 |
| 木桩与部位 | `MHGZDummyConfig`、`MHGZMonsterBase`、`MHGZTrainingDummy` | C++ 可生成恰好 Red/White/Orange 三个互不重叠球形 Hitzone，并提供固定 HitResult/AttackInstanceID 的确定性提交入口；E5 仍需配置实际 DataAsset/蓝图位置 |
| 旧运行时桥接 | `DefaultGame.ini`、`MHGZDataManager`、`MHGZEquipmentComponent`、`MHGZAttackAbility` | 旧 WeaponComboConfig/DataTable 查询和 Attack 旧字段兼容读取已归零；旧字段只剩不参与决策的序列化壳，待 E3 删除旧包后由 M4 移除 |

构建、10 项 M2 测试及 M0～M2 共 30 项联合回归证据见 [M2 实施审计](m2-implementation-audit.md)。

### P0：不先改就无法可靠建立战斗基底

| 系统 | 当前源码证据 | 问题 | Demo 目标 / 里程碑 |
|---|---|---|---|
| 猎虫碰撞 | [KinsectCollisionComponent.cpp](../../Source/MHGZ/InsectGlaive/Kinsect/KinsectCollisionComponent.cpp)；[Kinsect.cpp](../../Source/MHGZ/InsectGlaive/Kinsect/Kinsect.cpp) | 用 Weapon Trace Channel Overlap 充当身份；Root/UpdatedComponent 合同不完整 | Hitzone Object Channel + Collision Root + 前后帧 Capsule Sweep，M0/M3 |
| 精华状态机 | [Res_InsectGlaive.cpp](../../Source/MHGZ/AttributeSystem/Res_InsectGlaive.cpp) 255～378 | 部位名/Yellow/硬编码路径；三灯先创建单灯；bool 与 GE Handle 可失步 | Hitzone 直接给颜色；唯一 CombatConfig；Active GE Handle 为真相源；原子三灯，M3 |
| 旧资产包 | 旧 Combo/GA/Montage/WeaponDefinition 与旧组合 InputAction 仍在 Content | C++ 已不再读取，但旧包会继续保存过时结构并干扰最终数据制作 | E3 按引用顺序删除并从最终类型建壳；E4 创建最终动作资产 |

### P1：链路局部能跑，但 Demo 会出现错误或遗留状态

| 系统 | 当前源码证据 | 问题 | Demo 目标 / 里程碑 |
|---|---|---|---|
| 位移/旋转所有权 | [MHGZCharacter.cpp](../../Source/MHGZ/MHGZCharacter.cpp)、[MHGZAttackAbility.cpp](../../Source/MHGZ/ActionSystem/MHGZAttackAbility.cpp) 与未来 MovementTask | 当前普通攻击仍按激活生成 `AttackDirection_<...>` WarpTarget，但没有通用资产侧消费者；完整平移、旋转、steering 与取消后速度交接仍未统一 | M4-A.5 先把普通攻击改为 Confirm 后基于冻结输入的一次 Actor Yaw 瞬转，并移除其普通 WarpTarget；特殊动作和完整 Movement Token 独占交接在 M5 分别实现/验证 |
| 瞄准重绑 | [MHGZAimComponent.cpp](../../Source/MHGZ/UI/MHGZAimComponent.cpp) 45～100 | BeginPlay 时一次性找 ASC，初始化顺序不对时不会重试；颜色按部位名硬编码 | Runtime/ActorInfo Ready 绑定并可随 Avatar 重绑；直接读 Hitzone 颜色，M3 |
| 普通猎虫交付 | [Kinsect.cpp](../../Source/MHGZ/InsectGlaive/Kinsect/Kinsect.cpp) 90、239～返回逻辑 | PendingExtract 交付后未清空，后续飞行不能取得新颜色 | 到达时原子取出并清 Pending，再 Apply 一次；下一 Flight 可取新色，M3 |
| 猎虫耐力归零 | [Res_InsectGlaive.cpp](../../Source/MHGZ/AttributeSystem/Res_InsectGlaive.cpp) 57～61 | 耐力为 0 的每帧重复音效与 ForceRecall | 只在阈值穿越时触发一次，Returning/Attached 不重复，M3 |
| 虫印/粉尘/舞踏 | 当前源码不存在 | 无状态所有者、生命周期和清理入口 | 归属 URes/虫棍派生层；数值来自 CombatConfig，M4～M6 |
| UI 所有权 | [MHGZUISubsystem](../../Source/MHGZ/UI) 当前为空壳；原文档同时让 WBP_HUD 有资源插槽又让 Subsystem AddToViewport | 两个潜在 Widget 所有者会产生重复显示、解绑和本地玩家生命周期冲突 | HUD 独占 Widget 树；资源面板只作为主 HUD 子控件；删除空壳 Subsystem，M6 |

## 3. 明确延期但必须记录

以下问题不是“没有问题”，只是 Demo 不依赖其实现：

- [MHGZEquipmentComponent.cpp](../../Source/MHGZ/Equipment/MHGZEquipmentComponent.cpp) 128～150 的 `SetNumericAttributeBase` 会覆盖未来角色成长来源。
- WeaponResource 词条当前按 Tag 合并多来源不足，且参数读取可能把所有 Modifier 应用于所有参数。本轮将该路径标为不可用，Demo 配置不得依赖 `ApplyEntryModifier`。
- Entry GE、背包、仓库、快捷栏、存档、任务、关卡旅行和正式怪物 AI 不属于本轮闭环。
- 完整伤害仍缺斩味、属性、异常、会心细则和部位破坏；本轮只保留扩展入口。
- 网络预测和复制未设计完成，Demo 只验收单机。

## 4. 覆盖结论

上述每个 P0/P1 项都已映射到 [Demo 冻结实施计划](demo-implementation-plan.md) 的 M0～M7，并应在 [验证清单](../editor/verification.md) 中有可执行场景。开始改代码前还必须满足 [重构范围与资产处置](demo-refactor-scope.md) 的清点和删除门槛；不能先创建全部 GA 蓝图，再回头修底层生命周期。
