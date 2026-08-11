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
| 闪避 | `MHGZDodgeAbility`、`AnimNotifyState_DodgeWindow` | 使用方向快照和 AbilityTask；窗口由 Ledger 持有并逐通道恢复原碰撞响应；缺 Montage 零副作用测试通过 |

构建、15 项 M1 测试与 5 项 M0 回归证据见 [M1 实施审计](m1-implementation-audit.md)。

### P0：不先改就无法可靠建立战斗基底

| 系统 | 当前源码证据 | 问题 | Demo 目标 / 里程碑 |
|---|---|---|---|
| 武器资源宿主 | [MHGZEquipmentComponent.cpp](../../Source/MHGZ/Equipment/MHGZEquipmentComponent.cpp) 91～115；[Res_InsectGlaive.cpp](../../Source/MHGZ/AttributeSystem/Res_InsectGlaive.cpp) 136～182 | Resource 在 PlayerState 创建却把 Owner 当 Pawn；直接 DestroyComponent，无统一虫棍清理 | Character RuntimeHost 独占世界/Pawn 运行时并统一 Shutdown，M2 |
| 装备差分重建 | [MHGZEquipmentComponent.cpp](../../Source/MHGZ/Equipment/MHGZEquipmentComponent.cpp) 77、85、92～115 | 饰品/护甲变化走同一 OnEquipmentChanged，可能销毁 Resource、移除能力并清战斗态 | StatsChanged 与 WeaponChanged Snapshot 分离；相同武器身份 no-op，M2 |
| 猎虫碰撞 | [KinsectCollisionComponent.cpp](../../Source/MHGZ/InsectGlaive/Kinsect/KinsectCollisionComponent.cpp)；[Kinsect.cpp](../../Source/MHGZ/InsectGlaive/Kinsect/Kinsect.cpp) | 用 Weapon Trace Channel Overlap 充当身份；Root/UpdatedComponent 合同不完整 | Hitzone Object Channel + Collision Root + 前后帧 Capsule Sweep，M0/M3 |
| 精华状态机 | [Res_InsectGlaive.cpp](../../Source/MHGZ/AttributeSystem/Res_InsectGlaive.cpp) 255～378 | 部位名/Yellow/硬编码路径；三灯先创建单灯；bool 与 GE Handle 可失步 | Hitzone 直接给颜色；唯一 CombatConfig；Active GE Handle 为真相源；原子三灯，M3 |
| 资产双结构 | [MHGZAttackAbility.h](../../Source/MHGZ/ActionSystem/MHGZAttackAbility.h) 中旧 Socket/Collision/成本字段；旧 Combo DataTable/最小 Combo/GA/Montage | 旧、新字段和 DataTable 并存会让蓝图继续保存错误语义，重构后无法证明使用哪条路径 | M0 已审计引用；M2 删除旧运行时读取，E3/E4 删除旧原型并从最终类型重建；禁止永久兼容分支 |

### P1：链路局部能跑，但 Demo 会出现错误或遗留状态

| 系统 | 当前源码证据 | 问题 | Demo 目标 / 里程碑 |
|---|---|---|---|
| 攻击多跳 | [MHGZAttackAbility.cpp](../../Source/MHGZ/ActionSystem/MHGZAttackAbility.cpp) 564～619 | 首次接触后 Timer 对缓存目标继续跳伤，离开接触区仍受伤 | 默认 PerContactTrace；LockedTarget 每跳重验，M2 |
| 命中与反馈 | [MHGZAttackAbility.cpp](../../Source/MHGZ/ActionSystem/MHGZAttackAbility.cpp) 628～754；[MHGZAttributeSet.cpp](../../Source/MHGZ/AttributeSystem/MHGZAttributeSet.cpp) 92～119 | 丢失真实命中点；Cue 不执行；HitStop 覆盖全局倍率；PlayerState 被当物理目标 | 保留真实 HitResult/AttackInstanceID；从 Avatar 解析表现目标；统一 FeedbackRouter，M2 |
| 伤害零值 | [MHGZDamageExecCalc.cpp](../../Source/MHGZ/ActionSystem/MHGZDamageExecCalc.cpp) 154～181 | `max(1)` 让反击判定段和零动作值段扣血 | MotionValue≤0 为 0，正值最低 1，M2 |
| 位移/旋转/Warp | [MHGZCharacter.cpp](../../Source/MHGZ/MHGZCharacter.cpp) 176；[MHGZAttackAbility.cpp](../../Source/MHGZ/ActionSystem/MHGZAttackAbility.cpp) 193 | Character 每帧按输入 SetActorRotation；动作复用 `AttackDirection` WarpTarget 且无完整回收 | Action/Movement Token 同时拥有平移、旋转、steering、唯一 WarpTarget；全路径释放，M2/M5 |
| 瞄准重绑 | [MHGZAimComponent.cpp](../../Source/MHGZ/UI/MHGZAimComponent.cpp) 45～100 | BeginPlay 时一次性找 ASC，初始化顺序不对时不会重试；颜色按部位名硬编码 | Runtime/ActorInfo Ready 绑定并可随 Avatar 重绑；直接读 Hitzone 颜色，M3 |
| 普通猎虫交付 | [Kinsect.cpp](../../Source/MHGZ/InsectGlaive/Kinsect/Kinsect.cpp) 90、239～返回逻辑 | PendingExtract 交付后未清空，后续飞行不能取得新颜色 | 到达时原子取出并清 Pending，再 Apply 一次；下一 Flight 可取新色，M3 |
| 猎虫耐力归零 | [Res_InsectGlaive.cpp](../../Source/MHGZ/AttributeSystem/Res_InsectGlaive.cpp) 57～61 | 耐力为 0 的每帧重复音效与 ForceRecall | 只在阈值穿越时触发一次，Returning/Attached 不重复，M3 |
| 木桩 | [MHGZMonsterHitzoneComponent.cpp](../../Source/MHGZ/Monster/MHGZMonsterHitzoneComponent.cpp)；[MHGZTrainingDummy.cpp](../../Source/MHGZ/Monster/MHGZTrainingDummy.cpp) | Hitzone 同时阻挡 Pawn、没有 ExtractColor；不能产生可复现 IncomingHit | 三个分离颜色部位 + 固定 AttackInstance 测试器，M2 |
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
