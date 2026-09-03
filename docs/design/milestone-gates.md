# 虫棍木桩 Demo：阶段门禁、顺序与当前状态

> **唯一状态真相源（2026-08-29）。** 本文定义 M（C++/运行时）与 E（编辑器资产）的顺序、进入条件、退出条件及当前状态。阶段状态必须同时有本文的验收项、可复核的验证证据和阶段快照；Git 提交本身只能证明快照，不可替代 PIE 或编辑器验证。其他文档中的日期型“当前状态”只作为当时记录；与本文冲突时，以本文为准。玩法规则仍以动作/资源设计文档为准，接口仍以冻结实施计划为准。

> **编号规则：** `M4.x` 是运行时/代码及其 PIE 验收，`E4.x` 是仅需编辑器可视化操作的资产工作包。字母后缀（如 `M4-B.1A`、`E4-A.6`）和 `PMM-x` 均为历史记录，不再作为可执行阶段名；旧 PMM 工作的已完成证据统一归入 M4.2 的前置基础。

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

例外：如果无效资产是当前阶段的**直接运行时依赖**，它同时阻塞当前阶段验收。例如 `DA_WeaponRuntime_IG` 是 E4.1 的直接依赖；`DA_TrainingDummy` 的三色部位属于 E5.1，不是送虫/收虫动作的直接依赖。

### 1.3 跨阶段约束

- 不用未来功能填补当前阶段：`ResourceWidgetClass`、HUD、资源面板属于 M6/E6；M4.1 只要求 `ResourceComponentClass=URes_InsectGlaive`，允许 Widget 为 `None`。
- 不用临时蓝图/Event Graph 代替尚未完成的 C++ 合同；最终父类、ActionToken、Commit Notify 和 DataAsset 引用必须先存在。
- E 是编辑器工作包，M 是运行时里程碑，二者不是严格的一一映射。某个 M 可以先完成代码，之后由对应 E 接线；某个 E 也可为已完成 M 的端到端验证补齐资产。
- E5 拆成两个门禁：**E5.1** 为 M3 所需的木桩三色 Hitzone 与基础猎虫物理配置；**E5.2** 为后续动作/表现所需的猎虫、训练场和粉尘表现资产。E5.1 不必等待 E4.3/M5。
- M4.2 只验收普通移动/Stop；M4.5 专门验收动作退出。这消除了“先要求动作退出全绿、才允许实现动作退出候选语境”的循环依赖，不降低动作退出验收要求。
- M4.4 是唯一的通用 `MMHandoff + ExitTransition` 基础设施纵切。它仅可在 M4.2 与 M4.3 通过后、M4.5 前开展；只允许修复已记录的 M4.1 动作退出，不得顺势开始批量地面招式或 E4.3。

## 2. 固定执行顺序

```text
M0 -> M1 -> M2 -> M3
 |     |     |     |
E0/E1  E2    E3    E5.1（木桩三色部位与猎虫物理）
                         \
                          M4.1 代码 -> E4.1 接线 -> M4.2（普通移动/Stop）
                                                    -> M4.3（输入释放基础设施）
                                                    -> M4.4（MMHandoff + Exit 基础设施）
                                                    -> E4.2（既存动作退出资产）
                                                    -> M4.5（动作退出固定矩阵 + M4.1 最终 PIE）
                                                    -> M4.2.1（收刀 Run/Sprint Loop 候选库重路由）
                                                    -> M4.6（攻击 Entry Section） -> E4.3
                                                    -> M4.7（地面招式/虫印） -> M5 -> E5.2
                                                    -> M6 -> E6
                                                    -> M7 -> E7（全项目验证/打包）
```

规则如下：

1. M0～M3 的运行时代码必须先于依赖其反射字段的 E3/E4/E5 资产接线。
2. E4.1 的非移动动作资产完成后，先完成 M4.2，才允许进入 M4.3 与 M4.4；此例外只为建立动作退出的通用基础设施和验证现有动作，不授权 E4.3 或 M4.7 批量动作资产接线。
3. M4.3 是 M4.4 的前置。M4.4 完成并由 E4.2 接线后，必须通过 M4.5。其后先完成 M4.2.1 的收刀 Run/Sprint Loop 候选库重路由验收，才能开始 M4.6。M4.6 的 Entry Section 合同是所有拆分攻击资产的前置。
4. M6/E6 才创建并赋值 `ResourceWidgetClass`。此前不得为满足校验创建空 Widget。
5. E7/M7 才做“全 Content 必须无 Data Validation 错误”的发布验收；各早期阶段只对自己的验收集负责。

## 3. M 阶段门禁

| 阶段 | 进入条件 | 退出条件（摘要） | 当前状态 |
|---|---|---|---|
| M0 配置/验证地基 | 无；先完成资产审计。 | 最终公共类型、Redirect、输入/碰撞基础与验证骨架可编译、可稳定报错。 | 已完成。 |
| M1 生命周期/输入/FSM | M0 类型与 Tag 可用。 | 输入快照、组合键、Token、窗口、Superseded 与基础 Dodge 的自动化通过。 | 已完成。 |
| M2 RuntimeHost/命中/木桩运行时 | M1 生命周期合同完成。 | RuntimeHost、命中上下文、Body/Hitzone 运行时和旧读取清理完成。 | 代码完成；实体木桩资产由 E5.1 完成。 |
| M3 猎虫/精华/瞄准 | M2 运行时与萃取规则已冻结。 | Collision Root、Hitzone Sweep、萃取/三灯、基础飞行与召回代码完成；三色端到端验收须有 E5.1。 | 代码完成；E5.1 未完成。 |
| M4.1 最小动作纵切 | M3 接口已编译；E4.1 使用最终 GA/DataAsset，不走临时蓝图路径。 | 收刀、翻滚、Y 拔刀分流、收刀 RT、LT+Y/LT+B 的 Commit/中断合同可验证；最终动作退出行为留给 M4.5。 | **已完成；E4.1 本体与 M4.5 最终 PIE 均已签收。** |
| M4.2 普通移动 / Stop 固定矩阵 | M4.1/E4.1 的非移动纵切、历史 Stop 生命周期专项证据已完成。 | 收刀/拔刀普通 Idle、Walk/Run/Sprint Start/Loop、真实松杆 Stop、左右脚 Stop、首次 PIE 进入普通移动均通过；**不验收功能 GA 的退出。** | **已完成；普通移动已签收。** |
| M4.3 输入释放补丁 | M4.2 通过。 | `OnReleaseIfUnconsumed` 不破坏已有组合键/释放身份；按 [M4.3 详细设计与实施](m4.3-input-release-implementation.md) 的自动化完成签收。 | **已完成（2026-08-29）：Development Editor 编译通过；`MHGZ.M4.3.Input` 7/7、`MHGZ.M1.Input` 9/9 通过。** |
| M4.4 动作退出 / 根运动交接基础设施 | M4.2、M4.3。 | 通用 `MMHandoff`、ActionToken 精确 Root Motion Phase、候选库路由和 Telemetry 合同可验证；只接 M4.1 已存在的动作。 | **已完成（2026-08-31）：Development Editor 编译通过；本次 `MHGZ.M4.4.Handoff` 3/3、`MHGZ.PMM.Query` 4/4 通过；E4.2 自动路由已保存并审计。允许进入 M4.5 PIE。** |
| M4.5 动作退出固定矩阵与 M4.1 最终验收 | M4.4、E4.2。 | 既存收刀/翻滚/拔刀/突刺在 Exit→Loop/Stop/Idle 下的固定矩阵通过，并完成 M4.1 最终 PIE。 | **已完成（2026-09-01）：PIE 与 RuntimeTelemetry 确认 Exit 起始进入、持续输入 Exit→Move、真实松杆即时一次 Stop、无输入 ActionIdle；M4.1 最终 PIE 已签收。** |
| M4.2.1 收刀 Run/Sprint Loop 候选库重路由 | M4.5；M4.2 的 Stop 生命周期保持已签收。 | 已进入 Run/Sprint Loop 的持续输入改挡，只通过候选数据库成员变化触发一次合法搜索；目标 Loop 库不得暴露 Start/Stop；真实松杆仍回既有 FullMove 并保持一次正确 Stop。节点 Blend 配置保持基线。 | **已完成并签收（2026-09-03）：** Development Editor 编译通过；`MHGZ.PMM` 11/11（资产 5/5、查询 6/6）通过；LoopOnly commandlet `-AuditOnly` 为 2/2。PIE Telemetry `Saved/RuntimeTelemetry/20260903-211806-BP_IG_Character_C_0-53906` 已确认：`RunLoopOnly` 与 `SprintLoopOnly` 会在至多两个动画更新内双向切换，目标候选库不含 Start/Stop；Start 期间保持 FullMove；真实松杆仍只产生一次正确 Stop。用户已完成目视回归且未发现问题。**允许进入 M4.6。** |
| M4.6 攻击 Entry Section | M4.5、M4.2.1。 | Section 在播放前选择；拆分攻击的 Root Motion Phase 与 MovementTask 所有权可验证。 | **可开始（未实施）。** M4.2.1 已签收，前置条件满足。 |
| M4.7 地面招式 / 虫印 | M4.6 完成、E4.3 的最终 Combo 壳有效。 | 地面连段、虫印、反击/舞踏入口符合动作设计。 | 未开始。 |
| M5 空中/舞踏/终结 | M4.7 与 M4.5 最终回归完成。 | 空中位移、舞踏、落地和取消只保留一个位移所有者。 | 未开始。 |
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
| E4.1 最小动作资产 | M4.1 原生父类/Notify 已编译，E3 数据壳有效。 | 最终 GA、Montage、Notify、两条 Y 边、CoreAbilities 与 AnimGraph 接线完成；对 E4.1 资产做阶段验证。 | **已完成；M4.1.5 阶段快照 `15bbb6b`。** |
| E4.2 动作退出资产 | M4.4 代码和数据接口已编译。 | 仅收刀 Walk、前向 Dodge MoveExit、以及 Telemetry 已证明直接交接失败的既存拔刀/突刺路径，完成无功能 ExitTransition、Handoff Notify、Root/Notify 审计、Exit PSD/Chooser 接线与索引。 | **已完成（2026-09-01）：单一完整 Exit PSD、起始 BlockTransition、原生 PreSearch 路由与即时 Exit→Stop 交接均已编译、审计并通过 M4.5 PIE。** |
| E4.3 其余地面动作 | M4.6 已完成且 M4.5 已最终验收。 | 批量地面招式均从正确 Section 启动并正确结束。 | 未开始。 |
| E5.1 木桩/基础猎虫 | M3 Collision Root 与 Sweep 已完成。 | `DA_TrainingDummy` 恰有 Head=Red、Torso=Orange、Leg=White；Body/Hitzone 碰撞与猎虫物理配置正确。 | 阻塞/待接线。 |
| E5.2 猎虫表现/训练场 | M5 或对应表现接口完成。 | 正式猎虫表现、训练场、可用的粉尘/虫印表现资产按所属阶段接线。 | 部分资产存在，未验收。 |
| E6 UI/Cue | M6 接口完成。 | HUD 唯一所有者，ResourceWidget 填入 RuntimeDefinition，Cue/反馈闭环。 | 未开始。 |
| E7 全流程验证 | M0～M6、E0～E6 完成。 | 全 Content Data Validation、完整 PIE、打包均通过。 | 未开始。 |

完整点击步骤见 [编辑器接线指南](../editor/demo-setup.md)。

## 5. M4.2 的已完成基础与验收边界

纯 Motion Matching 不是独立玩法阶段；它是 M4.2 的运行时基础。历史记录中曾使用 `PMM-0～PMM-7.1` 编号，现仅作为可复核的完成证据，**不得**据此推断当前可执行事项。旧 L0～L5 CMC 文档同样只是历史备选。

| 已完成基础 | 可复核证据 | 对 M4.2 的意义 |
|---|---|---|
| 查询与 AnimBP 接线 | C++ AnimInstance、ABP Reparent、两个 BPSC 和 PIE 查询值已确认；基线快照 `97cf7ae`。 | 允许直接验证普通移动，不再返工查询架构。 |
| 语义曲线与 PSS/PSD | 纯前向 Schema、双脚姿势、Stop 候选、索引报告 `MHGZPMM4AssetReport` 已建立。 | 可按当前六频道和 Telemetry 检查选帧。 |
| Stop 生命周期专项 | 生成 Stop 的 60Hz 零值尾段、SamplingRange、Continuing 与八项自动化已通过；2026-08-28 PIE 由用户确认。 | M4.2 只需签收普通移动固定矩阵；不得重新开启已修复的二次 Stop 问题。 |

M4.2 的唯一退出条件仍是普通 Idle、Start、Loop、真实松杆 Stop、左右脚 Stop 和首次 PIE。功能 GA 退出完全属于 M4.4～M4.5。技术设计、代码字段、UE5.6 编辑器操作与验收步骤见 [纯 Motion Matching 普通移动实施指南](pure-motion-matching-locomotion-guide.md)。

## 6. 当前门禁清单

**最近验证证据：** 2026-08-24 的全 Content Data Validation 扫描 464 个资产。修正 G-001 前有 G-001、G-002 两项错误；修正后复跑仅余 G-002（`DA_TrainingDummy` 三色 Hitzone），说明 `DA_WeaponRuntime_IG` 已使用新的可选 Widget 合同。完整 `MHGZ` 自动化为 54/54。全项目验收当前仍未满足；这不把 E5.1 的木桩资产错误改写成 E4.1 的 UI/动作需求。

| ID | 所属阶段 | 当前事实 | 解除条件 | 阻塞范围 |
|---|---|---|---|---|
| G-001 | M0/M2 验证合同，影响 E4.1 | **已解除（2026-08-24）。** `IsDataValid` 已改为只拒绝“Widget 非空、Resource 为空”；Resource 非空、Widget 为 `None` 合法。新增 `MHGZ.M2.Validation.ResourceWidgetRequiresResourceComponent`，完整自动化 54/54 通过；全 Content Data Validation 已确认 `DA_WeaponRuntime_IG` 不再报错。 | 无。 | 不阻塞。 |
| G-002 | E5.1 | `DA_TrainingDummy` 当前不满足恰好 Red/White/Orange 三个 Hitzone。 | 在 E5.1 按 Head=Red、Torso=Orange、Leg=White 配置并验证不重叠。 | M3 三色端到端、E5.1、全项目验证；**不阻塞 E4.1 的送虫/收虫功能验收。** |
| G-003 | M4.2 普通移动 / Stop | **已解除。** 普通 Idle/Start/Loop/Stop 固定矩阵已按当前代码与 Telemetry 签收；功能动作退出不属于本门禁。 | 无。 | 不阻塞。 |
| G-004 | M4.5 动作退出 | **已解除（2026-09-01）。** 单一完整 Exit PSD 的起始 BlockTransition 防止跳尾；持续输入保留 Exit→Move，真实松杆立即让出 continuing pose 并进入一次正常 Stop；无输入进入 ActionIdle。PIE 与 RuntimeTelemetry 已通过。 | 无。 | 不阻塞。 |
| G-005 | E4.1 | **已解除；** 收刀、翻滚、Draw、送虫/收虫与 AnimGraph 已在 `15bbb6b` 的 M4.1.5 阶段快照中保存。 | 无；以后若修改这些资产，按 E4.1 阶段验证重新确认。 | 不阻塞 M4.2。 |

## 7. 当前项目位置与唯一允许的下一步

**当前阶段：M4.6 攻击 Entry Section（前置已签收，未实施）。** M4.1/E4.1、M4.2～M4.5、M4.2.1 与 E4.2 均已签收。M4.2.1 的两个 LoopOnly PSD 已创建并只含目标 Loop；运行时仅在已实际获选 Run/Sprint Loop 的目标挡位改变时切换候选库。旧 `GaitChangeSerial` / 无条件 Force 重搜方案仍禁止恢复。

M4.2.1 已于 2026-09-03 完成 PIE Telemetry 验收。当前唯一允许的下一步是 **M4.6 攻击 Entry Section**；E4.3、M4.7 与 M5 仍按各自前置门禁保持阻塞。

M4.2.1 完成后，M4.6 在 `UMHGZAttackAbility` 中实现播放前的 Entry Section 选择与校验，优先级固定为 `TransitionID → SourceState → DefaultEntrySection → Montage 开头`；必须在创建 Montage Task 前传入 StartSection，不得先播放再 Jump。E4.3 仅在 M4.6 完成后开放；M4.7 仍同时要求 M4.6 完成和 E4.3 的最终 Combo 壳有效。若今后出现动作退出回归，回归归属仍为 E4.2/M4.5。
