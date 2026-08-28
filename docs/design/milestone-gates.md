# 虫棍木桩 Demo：阶段门禁、顺序与当前状态

> **唯一状态真相源（2026-08-28）。** 本文定义 M（C++/运行时）、E（编辑器资产）和 PMM（纯 Motion Matching 普通移动）之间的顺序、进入条件、退出条件及当前状态。阶段状态必须同时有本文的验收项、可复核的验证证据和阶段快照；Git 提交本身只能证明快照，不可替代 PIE 或编辑器验证。其他文档中的日期型“当前状态”只作为当时记录；与本文冲突时，以本文为准。玩法规则仍以动作/资源设计文档为准，接口仍以冻结实施计划为准。

## 1. 状态与验收规则

### 1.1 状态标签

| 标签 | 含义 |
|---|---|
| 已完成 | 本阶段代码、所属资产和规定验证均已完成；可以作为后续阶段前置。 |
| 代码完成 | 原生代码与自动化已完成，但仍缺本阶段资产接线、PIE 或数据验证。 |
| 接线中 | 所需原生接口已存在，正在创建/配置本阶段编辑器资产。 |
| 修复中 | 已完成基础接入，但有已复现的本阶段验收失败；只能修复该失败并重跑本阶段验收，不能作为后续阶段前置。 |
| 阻塞 | 有明确、可复现的门禁未满足；表中必须写明归属与解除动作。 |
| 未开始 | 不得提前创建依赖该阶段接口或运行时合同的资产。 |

### 1.2 两层验证

1. **阶段验收**：只验证本阶段拥有的资产和它直接依赖的已完成资产。阶段可据此提交；不能因为未来 M6 Widget、未进入 E5 的美术资产而被迫伪造占位内容。
2. **全项目验收**：扫描全部 Content 的 Data Validation、完整 PIE、自动化和打包，只在 M7/E7 前要求全绿。发现其他阶段资产无效时，记录为该资产所属阶段的未完成项；它不改变当前阶段的功能归属。

例外：如果无效资产是当前阶段的**直接运行时依赖**，它同时阻塞当前阶段验收。例如 `DA_WeaponRuntime_IG` 是 E4-A 的直接依赖；`DA_TrainingDummy` 的三色部位属于 E5-A，不是送虫/收虫动作的直接依赖。

### 1.3 跨阶段约束

- 不用未来功能填补当前阶段：`ResourceWidgetClass`、HUD、资源面板属于 M6/E6；M4-A 只要求 `ResourceComponentClass=URes_InsectGlaive`，允许 Widget 为 `None`。
- 不用临时蓝图/Event Graph 代替尚未完成的 C++ 合同；最终父类、ActionToken、Commit Notify 和 DataAsset 引用必须先存在。
- E 是编辑器工作包，M 是运行时里程碑，二者不是严格的一一映射。某个 M 可以先完成代码，之后由对应 E 接线；某个 E 也可为已完成 M 的端到端验证补齐资产。
- E5 拆成两个门禁：**E5-A** 为 M3 所需的木桩三色 Hitzone 与基础猎虫物理配置；**E5-B** 为后续动作/表现所需的猎虫、训练场和粉尘表现资产。E5-A 不必等待 E4-B/M5。

## 2. 固定执行顺序

```text
M0 -> M1 -> M2 -> M3
 |     |     |     |
E0/E1  E2    E3    E5-A（木桩三色部位与猎虫物理）
                         \
                          M4-A 代码 -> E4-A 接线 -> PMM-0~PMM-5
                                                    -> PMM-6（调试，尚未签收）
                                                    -> PMM-7 基础接入
                                                    -> PMM-7.1 Stop 生命周期修复
                                                    -> PMM-6 最终固定矩阵回归
                                                    -> M4-A 最终 PIE/阶段验证
                                                    -> M4-B.0 -> M4-B.1 -> E4-B
                                                    -> M5 -> E5-B
                                                    -> M6 -> E6
                                                    -> M7 -> E7（全项目验证/打包）
```

规则如下：

1. M0～M3 的运行时代码必须先于依赖其反射字段的 E3/E4/E5 资产接线。
2. E4-A 的非移动动作资产完成后，必须完成 PMM-7.1 并重跑通过 PMM-6 最终固定矩阵，才允许进入 M4-B.0、M4-B.1 或 M5 的批量动作资产接线。
3. M4-B.0 是 M4-B.1 的前置；M4-B.1 的 Entry Section/Root Motion Phase 是所有拆分招式资产的前置。
4. M6/E6 才创建并赋值 `ResourceWidgetClass`。此前不得为满足校验创建空 Widget。
5. E7/M7 才做“全 Content 必须无 Data Validation 错误”的发布验收；各早期阶段只对自己的验收集负责。

## 3. M 阶段门禁

| 阶段 | 进入条件 | 退出条件（摘要） | 当前状态 |
|---|---|---|---|
| M0 配置/验证地基 | 无；先完成资产审计。 | 最终公共类型、Redirect、输入/碰撞基础与验证骨架可编译、可稳定报错。 | 已完成。 |
| M1 生命周期/输入/FSM | M0 类型与 Tag 可用。 | 输入快照、组合键、Token、窗口、Superseded 与基础 Dodge 的自动化通过。 | 已完成。 |
| M2 RuntimeHost/命中/木桩运行时 | M1 生命周期合同完成。 | RuntimeHost、命中上下文、Body/Hitzone 运行时和旧读取清理完成。 | 代码完成；实体木桩资产由 E5-A 完成。 |
| M3 猎虫/精华/瞄准 | M2 运行时与萃取规则已冻结。 | Collision Root、Hitzone Sweep、萃取/三灯、基础飞行与召回代码完成；三色端到端验收须有 E5-A。 | 代码完成；E5-A 未完成。 |
| M4-A 最小动作纵切 | M3 接口已编译；E4-A 使用最终 GA/DataAsset，不走临时蓝图路径。 | 收刀、翻滚、Y 拔刀分流、收刀 RT、LT+Y/LT+B 的 Commit/中断合同与阶段 PIE 均通过；普通移动须完成 PMM-7.1 与 PMM-6 最终回归。 | **非移动纵切已在 M4-A.5 快照完成；最终交接被 PMM-7.1 阻塞。** |
| M4-B.0 输入释放补丁 | M4-A 最终验收、PMM-7.1 与 PMM-6 最终回归完成。 | `OnReleaseIfUnconsumed` 不破坏已有组合键/释放身份。 | 未开始。 |
| M4-B.1 入口 Section/根运动阶段 | M4-B.0、PMM-7.1 与 PMM-6 最终回归完成。 | Section 在播放前选择；Root Motion Phase 与 MovementTask 所有权可验证。 | 未开始。 |
| M4-B 地面招式/虫印 | M4-B.1 完成、最终 Combo 壳有效。 | 地面连段、虫印、反击/舞踏入口符合动作设计。 | 未开始。 |
| M5 空中/舞踏/终结 | M4-B 与 PMM-7.1、PMM-6 最终回归完成。 | 空中位移、舞踏、落地和取消只保留一个位移所有者。 | 未开始。 |
| M6 觉虫击/粉尘/UI | M5、觉虫击规则和表现接口已冻结。 | HUD 唯一所有权、Resource Widget、Cue、粉尘和觉虫击闭环。 | 未开始。 |
| M7 集成/打包 | M0～M6 与 E0～E6 均完成。 | 全项目验证、反复 PIE、压力测试、Win64 Development 打包通过。 | 未开始。 |

完整字段与逐项退出条件见 [冻结实施计划](demo-implementation-plan.md#4-实施里程碑)。

## 4. E 阶段门禁

| 工作包 | 进入条件 | 退出条件（摘要） | 当前状态 |
|---|---|---|---|
| E0 审计 | M0 前。 | 保留资产无 Missing Class/Property；删除链仅完成审计。 | 已完成。 |
| E1 项目设置 | M0 配置已编译。 | Collision Channel、Preset、Tag 基础正确。 | 已完成。 |
| E2 核心蓝图 | M1 父类/组件稳定。 | GameMode、Controller、PlayerState、Character 的所有权唯一。 | 已完成。 |
| E3 Runtime/Input/Combat 数据 | M2/M3 数据类型可用。 | RuntimeDefinition、InputProfile、Combat/Combo 最终数据壳存在；旧输入/旧 Combo 引用清理。 | 已完成，后续字段由 E4 回填。 |
| E4-A 最小动作资产 | M4-A 原生父类/Notify 已编译，E3 数据壳有效。 | 最终 GA、Montage、Notify、两条 Y 边、CoreAbilities 与 AnimGraph 接线完成；对 E4-A 资产做阶段验证。 | **已完成；M4-A.5 阶段快照 `15bbb6b`。** |
| E4-B 其余地面动作 | M4-B.1 已完成。 | 批量地面招式均从正确 Section 启动并正确结束。 | 未开始。 |
| E5-A 木桩/基础猎虫 | M3 Collision Root 与 Sweep 已完成。 | `DA_TrainingDummy` 恰有 Head=Red、Torso=Orange、Leg=White；Body/Hitzone 碰撞与猎虫物理配置正确。 | 阻塞/待接线。 |
| E5-B 猎虫表现/训练场 | M5 或对应表现接口完成。 | 正式猎虫表现、训练场、可用的粉尘/虫印表现资产按所属阶段接线。 | 部分资产存在，未验收。 |
| E6 UI/Cue | M6 接口完成。 | HUD 唯一所有者，ResourceWidget 填入 RuntimeDefinition，Cue/反馈闭环。 | 未开始。 |
| E7 全流程验证 | M0～M6、E0～E6 完成。 | 全 Content Data Validation、完整 PIE、打包均通过。 | 未开始。 |

完整点击步骤见 [编辑器接线指南](../editor/demo-setup.md)。

## 5. PMM：纯 Motion Matching 普通移动门禁

PMM 不是新的玩法阶段，而是 M4-A 与 M4-B/M5 之间的基础设施门禁。详细操作和逐步验收只以 [纯 Motion Matching 普通移动实施指南](pure-motion-matching-locomotion-guide.md) 为准；旧 L0～L5 CMC 文档已经降为历史备选。

| 阶段 | 进入条件 | 退出条件 | 当前状态 |
|---|---|---|---|
| PMM-0 基线 | E4-A 的非移动动作接线达到可验证状态。 | Git 基线、故障录屏与当前 PSS/PSD 参数记录完成。 | **已完成；阶段快照 `97cf7ae`。** |
| PMM-1 查询代码 | PMM-0。 | C++ AnimInstance 能生成实际速度、语义查询、停步距离和加减速轨迹；编译/数学测试通过。 | **已完成；实现与测试随 `97cf7ae` 进入基线。** |
| PMM-2 查询接线 | PMM-1。 | ABP Reparent；两个 BPSC 只读 C++ AnimInstance 缓存值，PIE 数值正确。 | **已完成；已确认 Reparent、两个 BPSC 接线与 PIE 查询值。** |
| PMM-3 动画语义 | PMM-2。 | 所有正式动画曲线一致；Start/Stop 的 Transition/Continuing Notify 正确。 | **基础审计已完成；PMM-7.1 后必须重跑。** |
| PMM-4 PSS/PSD | PMM-3。 | 纯前向 Schema、双脚姿势、完整 Stop 候选和 Brute Force 调试配置完成。 | **基础配置已完成；PMM-7.1 后必须重建索引并复验。** |
| PMM-5 AnimBP 切换 | PMM-4。 | Pose History 使用新轨迹；旧直线查询删除；先验证选帧再恢复短 Blend Stack。 | **已接通；其最终表现验收并入 PMM-6。** |
| PMM-6 调试验收 | PMM-5。 | 启停、首次拔刀、动作退出和左右脚 Stop 固定矩阵通过。 | **调试已进行，未签收；须在 PMM-7.1 后完整重跑。** |
| PMM-7 相位化 Extended Stop | PMM-5；PMM-6 可先进行候选/成本调试。 | 九条生成候选、StopGait/MoveGait、运行时曲线跟随、PSD 接入和基础审计完成；随后通过 PIE Stop 生命周期验收。 | **代码和资产基础接入完成；最终 PIE 验收失败，转入 PMM-7.1。** |
| PMM-7.1 Stop 生命周期修复 | PMM-7 基础接入和可复核的 Telemetry/PoseSearch 证据。 | Generated Stop 的全局可进入区、Continuing 区、语义归零区和 PSD 索引尾部按 60Hz 合同一致；无二次 Stop，随后 PMM-6 固定矩阵全过。 | **当前阶段；设计已冻结，尚未实施。** |

技术设计、代码字段、UE5.6 编辑器点击步骤和验收顺序统一见 [纯 Motion Matching 普通移动实施指南](pure-motion-matching-locomotion-guide.md)。

## 6. 当前门禁清单

**最近验证证据：** 2026-08-24 的全 Content Data Validation 扫描 464 个资产。修正 G-001 前有 G-001、G-002 两项错误；修正后复跑仅余 G-002（`DA_TrainingDummy` 三色 Hitzone），说明 `DA_WeaponRuntime_IG` 已使用新的可选 Widget 合同。完整 `MHGZ` 自动化为 54/54。全项目验收当前仍未满足；这不把 E5-A 的木桩资产错误改写成 E4-A 的 UI/动作需求。

| ID | 所属阶段 | 当前事实 | 解除条件 | 阻塞范围 |
|---|---|---|---|---|
| G-001 | M0/M2 验证合同，影响 E4-A | **已解除（2026-08-24）。** `IsDataValid` 已改为只拒绝“Widget 非空、Resource 为空”；Resource 非空、Widget 为 `None` 合法。新增 `MHGZ.M2.Validation.ResourceWidgetRequiresResourceComponent`，完整自动化 54/54 通过；全 Content Data Validation 已确认 `DA_WeaponRuntime_IG` 不再报错。 | 无。 | 不阻塞。 |
| G-002 | E5-A | `DA_TrainingDummy` 当前不满足恰好 Red/White/Orange 三个 Hitzone。 | 在 E5-A 按 Head=Red、Torso=Orange、Leg=White 配置并验证不重叠。 | M3 三色端到端、E5-A、全项目验证；**不阻塞 E4-A 的送虫/收虫功能验收。** |
| G-003 | PMM-7.1 | **当前阻塞。** 收刀 `Run_Stop_Right_Extended` 在 PSD 尾部失去可索引 Pose，下一帧不能 Continuing；残留的 `MM_StopGait=Run` 被迫进行全局搜索，随后进入另一条边界未被 Block 的 Stop。根因是数据库级 `ExcludeFromDatabaseParameters=[0,-0.05]` 与 Generated Stop 的 `0.001s` Notify 边界/曲线尾部合同冲突，不是普通攻击 GA、`BlockMovement` 或单纯 PSS 权重问题。 | 按纯 MM 指南 §14.10 实现帧对齐的 Stop 生命周期合同，重建九条候选和 PSD，并重跑 PMM-3/4/6 审计与 PIE。 | M4-A 最终移动验收、M4-B.0、M4-B.1、M5。 |
| G-004 | E4-A | **已解除；** 收刀、翻滚、Draw、送虫/收虫与 AnimGraph 已在 `15bbb6b` 的 M4-A.5 阶段快照中保存。 | 无；以后若修改这些资产，按 E4-A 阶段验证重新确认。 | 不阻塞 PMM。 |

## 7. 当前项目位置与唯一允许的下一步

**当前阶段：PMM-7.1 Stop 生命周期修复（修复中，尚未实施）。**

已完成的是 M4-A.5/E4-A 非移动纵切、PMM-1～PMM-5 的基础链路，以及 PMM-7 的生成资产、频道、运行时跟随和数据库接入。它们不等于普通移动最终验收通过。最新 Telemetry 已证明收刀 Extended Stop 在 PSD 尾部失去 Continuing 后触发一次不合法的全局 Stop 重选；因此 PMM-6 不能签收，M4-A 也不能标记最终完成。

当前只允许进行以下工作：

1. 按 [纯 Motion Matching 普通移动实施指南](pure-motion-matching-locomotion-guide.md) §14.10 修改 Generated Stop、PMM Fixup、索引审计和必要的运行时 Stop 收束逻辑；不得以降低 `MM_Intent` 权重、长 Blend、强制提前 Idle 或删除 `BlockTransition` 掩盖问题。
2. 重建生成资产和两个 PSD，重跑 PMM-3/PMM-4 自动审计，再录制并分析 Stop 生命周期 Telemetry。
3. 只有 PMM-7.1 的专属 PIE 验收通过，才重新执行 PMM-6 固定矩阵；PMM-6 全绿后才解除 G-003。

仍不得开始 M4-B.0、M4-B.1、M4-B、M5、M6 或它们的编辑器批量接线。G-002 / E5-A 的木桩三色配置可独立并行，但不改变本项目当前主线阶段。
