# MHGZ

使用 Unreal Engine 5.6 与 Gameplay Ability System 制作原创虫棍战斗系统的单机 Demo。动作资源和机制参考“新时代三部曲”——《怪物猎人：世界》《怪物猎人：崛起》《怪物猎人：荒野》：以《世界》的地面虫棍和猎虫操作为基底，选择性移植、调整《崛起》的动作，再组合成项目自己的连招系统；动作动画可由合法持有的游戏资源解包、重定向后用于个人研究 Demo。

本项目不是逐项复刻任意一作。当前规则明确不包含翔虫资源/翔虫技前置消耗、不包含《荒野》的集中模式，也不包含《冰原》的钩爪。被移植动作只继承经设计确认的动作语义，不自动继承原作所属的资源系统。

## 快速入口

- [设计与代码生成文档](docs/design/README.md)：玩法、架构、公共接口、重构范围和代码实施顺序。
- [UE5 编辑器操作文档](docs/editor/README.md)：代码完成后的资产迁移、蓝图/DataAsset 接线和验收入口。
- [Demo 冻结实施计划](docs/design/demo-implementation-plan.md)：动代码前冻结的公共接口、所有权、M0～M7 阶段与退出条件。
- [UE5.6 编辑器接线指南](docs/editor/demo-setup.md)：重构代码编译完成后，在编辑器中需要执行的完整操作。
- [虫棍 Demo 动作与连招](docs/design/insect-glaive-actions.md)：Demo 招式表、输入、位移、舞踏、粉尘和特殊动作规则。
- [验证方案](docs/editor/verification.md)：PIE、生命周期和打包验收清单。
- [维护脚本](Scripts/README.md)：仓库中保留的 Python 工具及其写入边界。
- [待办与后续范围](docs/design/pending.md)：完整怪物、存档、音效和任务系统等未完成内容。

## 设计文档

| 文档 | 内容 |
|---|---|
| [GAS 基础设施](docs/design/gas-infrastructure.md) | ASC 挂载、组件归属与关卡切换 |
| [物品系统](docs/design/items.md) | 物品定义、实例与客制化 |
| [属性与装备](docs/design/attributes.md) | AttributeSet、EquipmentComponent 与受击/霸体 |
| [动作系统](docs/design/actions.md) | Enhanced Input、攻击 Ability、连招、翻滚与 AnimNotifyState |
| [虫棍系统](docs/design/insect-glaive.md) | 猎虫、萃取、三灯与资源设计 |
| [虫棍 Demo 动作](docs/design/insect-glaive-actions.md) | 地面/空中连招、位移和特殊动作 |
| [Demo 实现差距](docs/design/demo-implementation-gaps.md) | 当前源码到目标 Demo 的必要改动矩阵 |
| [Demo 实施计划](docs/design/demo-implementation-plan.md) | 冻结架构、实施里程碑、禁止捷径与验收追踪 |
| [Motion Matching](docs/design/motion-matching.md) | 移动动画与 Motion Matching 配置 |
| [伤害计算](docs/design/exec-calc.md) | ExecCalc、SetByCaller、伤害与硬直公式 |
| [怪物与木桩](docs/design/monster-system.md) | Hitzone、训练木桩与渐进式怪物架构 |
| [GameplayCue](docs/design/gameplay-cue.md) | 命中反馈、Buff 表现与伤害数字 |
| [UI 系统](docs/design/ui-system.md) | HUD、准心、血条、耐力与虫棍资源 UI |
| [存储系统](docs/design/storage.md) | 背包、仓库与整理 |
| [使用系统](docs/design/use-system.md) | 快捷栏、UseAction 与 SpecialAction |
| [词条系统](docs/design/entries.md) | EntryCatalog、数值曲线与 DataManager |
| [GameplayTags](docs/design/gameplay-tags.md) | 项目 Tag 层级 |
| [设计决策](docs/design/design-decisions.md) | 架构选择与排序规则 |

## 编辑器文档

| 文档 | 内容 |
|---|---|
| [编辑器接线指南](docs/editor/demo-setup.md) | 代码完成后的 UE5.6 操作顺序 |
| [验证方案](docs/editor/verification.md) | 系统级验证清单 |
| [资产整理记录](docs/editor/asset-organization.md) | AssetTools 迁移历史、引用保护和 Redirector 规则 |

## 项目边界

- 当前面向单机/本地 Demo，不包含网络复制。
- 当前迭代只以训练木桩 Demo 为验收目标；完整伤害、怪物、任务、仓库和长期成长不阻塞 Demo。
- `/Game/Maps/L_DemoArena` 是唯一默认地图。
- `/Game/TemplateAssets` 只保存 UE 模板资产，项目运行时资产不得放入其中。
- 不要在文件系统中直接移动或重命名 `.uasset`、`.umap`；必须使用 Unreal Content Browser 或 AssetTools。
