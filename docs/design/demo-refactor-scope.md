# 木桩 Demo 重构范围与资产处置

> **状态：冻结设计。** 本段的 M0～M2/E0～E2 描述是 2026-08-11 的资产审计快照，不用来判断当前可进入的阶段。当前 M/E/L 顺序、门禁和阻塞项以 [阶段门禁](milestone-gates.md) 为准；本文继续只定义 Keep/Rewrite/Delete/Defer 的资产处置合同。

## 一、范围与真相源

本轮目标不是在现有类和蓝图上打补丁，而是在不考虑兼容旧运行时架构的前提下，得到可继续扩展其他武器的 GAS 战斗基底，并完成虫棍木桩 Demo。

- 玩法真相源：[虫棍动作规格](insect-glaive-actions.md) 与 [虫棍系统](insect-glaive.md)。
- 架构与实施真相源：[Demo 冻结实施计划](demo-implementation-plan.md)。
- 当前实现问题与证据：[Demo 实现差距](demo-implementation-gaps.md)。
- 本文只决定资产处置边界，不为旧接口保留并行兼容路径。
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

以下内容在对应代码替代完成后删除，不保留旧、新两条运行时路径。

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
| 旧原型 `DT_WeaponComboConfig`、`DA_IG_Combo`、`GA_IG_BaDao`、`GA_IG_R_TuCI`、`AM_Shth_BaDao`、`AM_Shth_R_TuCi` | M2 退役旧读取链；E3/E4 全新创建最终 Combo、GA 与 Montage |
| 零引用旧 `DA_IG_HuoLongGun` | E3 使用最终 `UMHGZWeaponDefinition` 全新创建同名正式资产 |
| 旧组合 InputAction `IA_RTA/IA_RTB/IA_RTY/IA_YB` | M1 已删除代码读取，E2 Compile/Save PlayerState 清旧序列化引用，E3 清 IMC 映射后删除；InputRouter 直接组合 Y/B/LT/RT |

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

## 六、一次性资产删除重建合同

目标是保留真正有价值的地图、核心蓝图、AnimBP、输入基础、美术与原始动画；旧虫棍动作原型不做字段迁移，等最终代码入口就绪后整链删除并从最终类型重建。不是永久维护兼容分支，也不是全量删除 Content。

具体 Content Browser、Reference Viewer、删除顺序和重建接线见 [UE5.6 编辑器接线指南 E0](../editor/demo-setup.md#2-e0第一次打开编辑器与删除前审计)。

### 6.1 修改前清点

M0 必须先记录工作树基线并只读清点下列引用：

- 旧链 `DefaultGame.ini → DT_WeaponComboConfig → DA_IG_Combo → GA_IG_R_TuCI → AM_Shth_R_TuCi`。
- 零引用旁支 `GA_IG_BaDao → AM_Shth_BaDao` 与零引用 `DA_IG_HuoLongGun`。
- 角色/Controller/PlayerState/木桩/GameMode/地图等保留资产的直接引用者。
- `BP_PlayerState`、`IMC_MHGZ_Demo` 对旧组合 InputAction 的引用。

用户已有的源码和资产改动属于基线。实际删除前必须有 Git 阶段提交；只允许通过 Content Browser 删除精确清单，不得在资源管理器中删包。

### 6.2 最终类型一次到位

1. 在同一个可编译阶段建立最终 RuntimeDefinition、InputSnapshot、Transition、ActionToken、AttackSegment、CostSpec 和 MovementRequest 类型。
2. M0 已加入的精确 `CoreRedirects/StructRedirects/PropertyRedirects` 只用于证明旧包可安全加载和审计；新建最终资产不得依赖 Redirect 生成数据。
3. 旧字段在原型删除前可临时标记 Deprecated，但不得继续被最终运行时读取；M2 先删除运行时读取，E3 删除旧包，M4 再移除序列化壳与临时兼容代码。

### 6.3 删除与重建

1. E0 只检查保留资产，不补录、不重存、不删除旧动作原型。
2. M1 已清除 ASC 的旧输入读取及 `InputBindings` 属性；E2 Compile/Save `BP_PlayerState` 清除旧序列化引用；E3 清除 IMC 旧组合映射并删除四个旧组合 InputAction。M2 删除 `DefaultGame.ini` 的旧表配置、DataManager Getter、Equipment 旧读取和 Attack 旧字段的运行时读取，但保留旧包加载所需的序列化壳。
3. E3 用 Reference Viewer 复核无额外引用后，按引用者到依赖项删除旧 DT、旧 Combo、两个旧 GA、两个旧 Montage及零引用旧武器定义；不使用 Force Delete。
4. E3 全新创建 `DA_IG_HuoLongGun`、`DA_WeaponRuntime_IG`、`DA_IG_InputProfile`、`DA_IG_Combat` 和空壳 `DA_IG_Combo`，然后才把 Character 默认武器指向新定义；M4 确认旧包已不存在后删除序列化壳；E4 全新创建最终 GA/Montage 后一次回填完整 Transitions。
5. E3 删除旧组合 InputAction 前，必须先让 E2 的 `BP_PlayerState` 和 E3 的 `IMC_MHGZ_Demo` 对 `IA_RTA/IA_RTB/IA_RTY/IA_YB` 的引用归零。
6. `AS_Shth_BaDao`、`AS_Shth_R_TuCi`、`SK_Demo_Body`、全部其他原始动画和美术资产不是删除对象。

### 6.4 删除重建验收

- 项目冷启动和编辑器资产扫描无缺失引用或重定向循环。
- 旧 Combo/Resource DataTable 不再被运行时代码或目标资产引用。
- 最终虫棍 RuntimeDefinition 能独立解析输入、连招、资源、UI 与动作配置。
- AttackAbility 蓝图不再显示或保存旧 Collision/成本字段。
- 旧拼音 GA/Montage、旧组合 InputAction 与旧 Combo 包均不存在；最终资产不是 Duplicate 旧包所得。
- 删除旧类/字段后再次编译、重开编辑器并保存最终资产仍通过。

## 七、开始改代码的门槛

只有以下条件同时满足，才能从文档阶段进入 M0/M1 实施：

- [ ] 本文、实施计划、问题清单和验证清单对同一架构无冲突。
- [ ] 所有 Demo 必需动作都有输入、派生、消耗、位移、命中和结束规则。
- [ ] 保留、删除、重建清单与引用解除顺序明确，旧资产不会被盲目覆盖或 Force Delete。
- [ ] 每个运行时对象、Tag、输入绑定、Notify、Timer、Movement/WarpTarget 都有唯一所有者和幂等清理路径。
- [ ] 所有 P0/P1 问题均映射到具体里程碑和可执行验收项。
