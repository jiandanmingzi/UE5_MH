# 项目设计与代码生成文档

本目录是架构评审、代码生成和代码重构的真相源。内容描述目标玩法、公共接口、所有权、生命周期、数据结构和实施顺序；不把 Unreal Editor 点击步骤混入代码规格。

## 阅读顺序

为虫棍木桩 Demo 修改代码时，按以下顺序读取：

1. [重构范围与资产处置](demo-refactor-scope.md)：决定保留、重写、删除重建和延期边界。
2. [阶段门禁、顺序与当前状态](milestone-gates.md)：先确认当前所在阶段、可做/不可做的工作和跨阶段阻塞项。
3. [冻结实施计划](demo-implementation-plan.md)：决定公共结构、所有权、M0～M7 的完整接口与退出条件。
4. [虫棍动作与连招](insect-glaive-actions.md)：决定输入、派生、舞踏、位移和特殊动作玩法。
5. [动作系统](actions.md)、[GAS 基础设施](gas-infrastructure.md)、[属性与装备](attributes.md)、[虫棍资源](insect-glaive.md)：决定模块接口和运行时状态。
6. [当前实现差距](demo-implementation-gaps.md)：核对源码问题是否已被对应里程碑解决。
7. [编辑器接线指南](../editor/demo-setup.md) 与 [验证清单](../editor/verification.md)：代码完成后再执行资产接线和人工验收。

发生冲突时：玩法以虫棍动作文档为准；通用架构以冻结实施计划为准；**阶段顺序、进出条件和当前状态以阶段门禁文档为准**；实现事实最终以实际源码、Content 和最近验证结果为准。

## 核心战斗文档

| 文档 | 用途 |
|---|---|
| [demo-refactor-scope.md](demo-refactor-scope.md) | Keep/Rewrite/Delete/Defer 与一次性迁移合同 |
| [milestone-gates.md](milestone-gates.md) | M/E/L 阶段顺序、进出条件、当前状态和跨阶段门禁的唯一入口 |
| [demo-implementation-plan.md](demo-implementation-plan.md) | M0～M7 实施计划、接口和验收追踪 |
| [demo-implementation-gaps.md](demo-implementation-gaps.md) | 当前源码 P0/P1 问题和证据 |
| [m1-implementation-audit.md](m1-implementation-audit.md) | M1 实施范围、验证证据与 E2 交接 |
| [m2-implementation-audit.md](m2-implementation-audit.md) | M2 Runtime/命中/木桩实现、验证证据与 E3 交接 |
| [insect-glaive-actions.md](insect-glaive-actions.md) | 虫棍最终招式、输入和连招规则 |
| [insect-glaive.md](insect-glaive.md) | 猎虫、精华、三灯、虫印、粉尘和资源状态 |
| [actions.md](actions.md) | 输入快照、ActionToken、攻击、闪避、Notify 和移动任务 |
| [gas-infrastructure.md](gas-infrastructure.md) | ASC、PlayerState、RuntimeHost 和组件归属 |
| [attributes.md](attributes.md) | AttributeSet、装备 Snapshot 和资源接线 |
| [monster-system.md](monster-system.md) | 木桩、Hitzone、受击与反击测试器 |
| [exec-calc.md](exec-calc.md) | Demo 伤害、Meta Attribute 和反馈结算 |

## 支撑系统文档

| 文档 | 用途 |
|---|---|
| [gameplay-tags.md](gameplay-tags.md) | GameplayTag 层级和碰撞说明 |
| [gameplay-cue.md](gameplay-cue.md) | 命中、Buff、伤害数字和 Cue 路由 |
| [ui-system.md](ui-system.md) | HUD 唯一所有权、准心和资源面板接口 |
| [motion-matching.md](motion-matching.md) | Locomotion、Trajectory、旋转和动画图 |
| [locomotion-refactor.md](locomotion-refactor.md) | 已冻结、待 M4-A.5 后实施的 CMC + 状态机 + Distance Matching 普通移动重构 |
| [locomotion-refactor-code-guide.md](locomotion-refactor-code-guide.md) | L0～L5 的代码文件、冻结接口、AI 实施边界、测试与退出条件 |
| [directory-structure.md](directory-structure.md) | Source/Content 目录和资产归属规范 |
| [design-decisions.md](design-decisions.md) | 已冻结决策和被取代方案 |
| [pending.md](pending.md) | 明确延期内容 |

## 完整游戏后续文档

| 文档 | 用途 |
|---|---|
| [items.md](items.md) | 物品定义与实例 |
| [entries.md](entries.md) | 词条目录与数值曲线 |
| [storage.md](storage.md) | 背包、仓库和整理 |
| [use-system.md](use-system.md) | 快捷栏和 UseAction |

这些内容不属于当前虫棍木桩 Demo 时，不得为了“顺手完善”扩大代码修改范围。

## 对代码生成的约束

- 先检查实际源码和资产引用，再判断文档中的“已实现/规划”状态。
- 不为旧、新结构建立永久并行运行时路径。
- 不在通用模块按 WeaponType 编写虫棍分支。
- 不替用户执行本应在编辑器中完成的蓝图、Montage、DataAsset 或碰撞体调校；代码完成后把所需操作维护到 `../editor/`。
- 每个 P0/P1 改动必须有代码证据、自动化/编译验证和对应编辑器验收项。
