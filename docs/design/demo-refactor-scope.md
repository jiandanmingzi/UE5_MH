# 木桩 Demo 重构范围与资产迁移

> **状态：冻结设计，尚未实施。** 本文规定为完成虫棍木桩 Demo，现有系统哪些保留、哪些重写、哪些删除，以及如何一次性迁移已有蓝图/数据资产。只有本文第七节的文档门槛通过后才能开始 [Demo 冻结实施计划](demo-implementation-plan.md) 的 M0；进入 M0 后再按里程碑修改代码、蓝图、资产和配置。

## 一、范围与真相源

本轮目标不是在现有类和蓝图上打补丁，而是在不考虑兼容旧运行时架构的前提下，得到可继续扩展其他武器的 GAS 战斗基底，并完成虫棍木桩 Demo。

- 玩法真相源：[虫棍动作规格](insect-glaive-actions.md) 与 [虫棍系统](insect-glaive.md)。
- 架构与实施真相源：[Demo 冻结实施计划](demo-implementation-plan.md)。
- 当前实现问题与证据：[Demo 实现差距](demo-implementation-gaps.md)。
- 本文只决定迁移边界，不为旧接口保留并行兼容路径。
- 完整伤害、网络、装备成长、背包仓库和完整怪物不属于本轮 Demo。

## 二、保留

“保留”表示概念和已验证的局部实现可以继续使用，不表示原文件无需修改。

| 范围 | 保留内容 | 必要调整 |
|---|---|---|
| GAS 身份 | PlayerState 持有 ASC 与 AttributeSet；角色更换后重新初始化 Avatar | 移除 ASC 对物理输入和 Pawn 姿态初始化的所有权 |
| 攻击轨迹 | `UMHGZAttackAbility` 的多 Region 自适应 Socket Sweep、时间采样和单窗口独立命中集合 | 配置迁入最终动作资产；命中必须保留精确 `FHitResult`；生命周期改由 ActionToken 管理 |
| 伤害入口 | 原生通用 Damage GE 与木桩 Demo 的简化伤害公式 | 重写 EffectContext、命中部位与反馈载荷；不在本轮扩展完整伤害模型 |
| 地面移动 | Motion Matching 地面移动资产、基础 Locomotion 骨架 | 动作持有旋转/位移时，普通移动必须让出控制权 |
| 木桩 | 配置驱动 Hitzone 的概念 | 补齐 Object Channel、精确命中和可复现验证场景 |
| 装备数据 | EquipmentDefinition/Instance 与 PlayerState 装备槽是持久数据来源 | 装备统计重算和武器运行时切换拆成两个事件 |

## 三、重写

| 范围 | 重写目标 | 完成标志 |
|---|---|---|
| 输入 | `UMHGZInputComponent` 独占 IMC 与 Enhanced Input 绑定；生成不可变 InputSnapshot；ASC 不读物理输入 | 重绑定/换 Pawn 无重复回调；组合键与方向可稳定复现 |
| 连招协调 | 删除全局 PreviousState 和旧 ComboTable 解释器；以冻结 Transition、ActionToken 和两阶段 Confirm 驱动 | 旧动作迟到回调不能结束新动作；同一 GA 类可连续重入 |
| 动作实例与 Notify | 武器动作 GA 使用 `InstancedPerExecution`；RuntimeHost 用 MontageInstanceID 精确注册 ActionToken | Notify 不扫描所有激活 Ability；只命中所属动作实例 |
| 成本 | GAS 成本/冷却与武器资源使用一次 reservation 事务 | Commit 任一步失败均不产生动画、状态或部分扣费 |
| 武器运行时 | Character 上 RuntimeHost 独占 Resource、猎虫、粉尘、虫印、移动请求和动作注册表 | 卸装、死亡、换 Pawn 和关卡结束走同一幂等 Shutdown |
| 装备联动 | 区分装备统计变化与武器身份变化；以 WeaponSnapshot/Revision 比较 | 镶嵌和护甲变化不重建武器运行时、不清空精华 |
| 虫棍资源/猎虫 | 显式状态机、原子点灯、PendingExtract 一次性交付、耐力阈值事件 | 可连续获取不同颜色；耐力归零只触发一次召回和音效 |
| 位移与转向 | Movement Token 同时拥有平移、旋转、转向策略与唯一 WarpTarget | 同帧只有一个写入者；所有结束路径移除 WarpTarget |
| 闪避 | 输入快照、AbilityTask Montage、TagLedger、精确 Notify Token、碰撞响应快照恢复 | 缺少依赖或被取消时也不会残留 Tag/碰撞状态 |
| 瞄准 | AimComponent 在 Runtime/ActorInfo Ready 后绑定并能随 Pawn/ASC 重绑 | 不依赖一次性 BeginPlay 顺序 |
| UI | `AMHGZHUD` 独占 Widget 树和资源面板插槽；RuntimeToken 校验数据源 | 资源面板只插入 WBP_HUD 插槽，不单独 AddToViewport |

## 四、删除或替换

以下内容在对应资产迁移完成后删除，不保留旧、新两条运行时路径。

| 旧内容 | 替代物 |
|---|---|
| ASC 内 `FAbilityInputBinding`、Enhanced Input 绑定与 `bInputBound` | InputComponent + WeaponInputRouter + InputSnapshot |
| `FComboNode`、ComboTable、全局 `PreviousState`、PendingGrantedTags | 冻结 `FComboTransition` + Coordinator + ActionToken/TagLedger |
| `bIsContinuous`、`bRequiresWeaponResource`、单 float `WeaponResourceCost` | 成本策略 + `TArray<FWeaponResourceCostSpec>` + reservation |
| DataTable 版武器连招/资源运行时桥接 | `UWeaponRuntimeDefinition` 引用的 Input/Combat/Resource 配置资产 |
| AttackAbility 旧 Collision/Socket 字段和未使用 DamageSetByCaller 字段 | 最终 `AttackSegments/CollisionConfig/DamageConfig` |
| Resource 中静态 Yellow 映射、`bTriple`、`bDeployed`、Overlap 式猎虫命中 | Hitzone 颜色配置、显式状态与 Sweep 查询 |
| 当前空壳 `UMHGZUISubsystem` | `AMHGZHUD` 的资源面板工厂与插槽所有权 |
| Character Tick 无条件 `SetActorRotation` | Movement/Action Token 仲裁后的 locomotion steering |

## 五、明确延期且 Demo 不得依赖

| 范围 | 本轮约束 |
|---|---|
| 完整伤害模型 | 只保留可扩展入口与木桩所需简化公式，不补完整肉质、锐利度、会心、部位破坏平衡 |
| WeaponResource 词条 | 当前多来源合并和按参数过滤有缺陷；本轮标记不可用，Demo 配置不得依赖 `ApplyEntryModifier` |
| 装备成长 | `SetNumericAttributeBase` 覆盖未来成长来源的问题记录但不在本轮统一 GE 化 |
| 背包/仓库/存档/任务 | 不作为 Demo 验收项，不为其保留旧武器运行时接口 |
| 完整怪物 AI | 只实现固定木桩和可控攻击测试器 |
| 网络同步 | Demo 按单机设计；接口不故意阻断未来网络化，但不宣称已支持预测/复制 |
| 全武器内容 | 只验证通用基底可挂载不同 RuntimeDefinition，不制作其他武器完整动作 |

## 六、一次性资产迁移合同

迁移目标是让旧资产被最终类型正确加载并保存一次，然后删除旧运行时字段；不是永久维护兼容分支。

本节规定迁移约束；代码编译完成后的具体 Content Browser、蓝图重编译、Reference Viewer 和重存顺序见 [UE5.6 编辑器接线指南 E0](../editor/demo-setup.md#2-e0第一次打开编辑器与资产迁移)。

### 6.1 修改前清点

M0 必须先记录工作树基线并只读清点下列引用：

- 当前虫棍 GA 蓝图、角色/Controller/PlayerState/HUD/木桩蓝图。
- `DA_IG_Combo`、旧 `DT_WeaponComboConfig`、旧资源配置及其所有引用者。
- 虫棍动作 Montage 中 AttackCollision、ComboWindow、DodgeWindow、位移与 MotionWarping Notify。
- AttackAbility 蓝图中旧 Socket、Collision、MotionValue、资源成本字段的实际覆写值。

用户已有的源码和资产改动属于基线，迁移不得覆盖或回滚。

### 6.2 最终类型一次到位

1. 在同一个可编译阶段建立最终 RuntimeDefinition、InputSnapshot、Transition、ActionToken、AttackSegment、CostSpec 和 MovementRequest 类型。
2. 若序列化名称发生变化，只为资产加载添加精确的 `CoreRedirects/StructRedirects/PropertyRedirects`，例如 `FComboNode → FComboTransition`、`ComboTable → Transitions`；最终名称以 M0 资产清点为准，禁止猜测重定向。
3. 旧字段在资产尚未成功迁移前可临时标记 Deprecated，但不得继续被运行时读取。

### 6.3 迁移与重存

1. 编译最终类型后，只打开或命令行重存本轮清单中的目标资产。
2. 将旧 DataTable/字段的有效值映射到最终 DataAsset；没有等价语义的值必须人工登记，不静默使用默认值。
3. 逐项验证资产加载无 Missing Class/Property、Montage Notify 指向最终类、RuntimeDefinition 引用闭合。
4. 只有目标资产全部可加载并通过内容审计后，才删除旧 DataManager Getter、DataTable 读取、旧字段和临时迁移代码。
5. 序列化 Redirect 可以保留用于历史资产兼容，但运行时只允许最终 DataAsset/接口这一条路径。

### 6.4 迁移验收

- 项目冷启动和编辑器资产扫描无缺失引用或重定向循环。
- 旧 Combo/Resource DataTable 不再被运行时代码或目标资产引用。
- 最终虫棍 RuntimeDefinition 能独立解析输入、连招、资源、UI 与动作配置。
- AttackAbility 蓝图不再显示或保存旧 Collision/成本字段。
- 删除旧类/字段后再次编译、重开编辑器和重存目标资产仍通过。

## 七、开始改代码的门槛

只有以下条件同时满足，才能从文档阶段进入 M0/M1 实施：

- [ ] 本文、实施计划、问题清单和验证清单对同一架构无冲突。
- [ ] 所有 Demo 必需动作都有输入、派生、消耗、位移、命中和结束规则。
- [ ] 资产清点范围与迁移顺序明确，旧资产不会被盲目覆盖。
- [ ] 每个运行时对象、Tag、输入绑定、Notify、Timer、Movement/WarpTarget 都有唯一所有者和幂等清理路径。
- [ ] 所有 P0/P1 问题均映射到具体里程碑和可执行验收项。
