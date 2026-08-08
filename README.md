# MHGZ

使用 Unreal Engine 5.6 与 Gameplay Ability System 重现《怪物猎人：崛起》虫棍战斗系统的单机 Demo。当前项目包含虫棍角色、GAS 攻击闭环、训练木桩、封闭演示平台，以及猎虫、萃取、三灯和 UI 的扩展设计。

## 快速入口

- [Demo 搭建与验收](docs/demo-setup.md)：从当前已有资产完成单次攻击闭环，并继续扩展完整虫棍流程。
- [当前项目目录](docs/directory-structure.md)：Source、Content 和资产归属规范。
- [资产整理记录](docs/asset-organization.md)：AssetTools 迁移历史、引用保护规则与验证结果。
- [维护脚本](Scripts/README.md)：仓库中保留的 Python 工具及其写入边界。
- [待办与后续范围](docs/pending.md)：完整怪物、存档、音效和任务系统等未完成内容。

## 设计文档

| 文档 | 内容 |
|---|---|
| [GAS 基础设施](docs/gas-infrastructure.md) | ASC 挂载、组件归属与关卡切换 |
| [物品系统](docs/items.md) | 物品定义、实例与客制化 |
| [属性与装备](docs/attributes.md) | AttributeSet、EquipmentComponent 与受击/霸体 |
| [动作系统](docs/actions.md) | Enhanced Input、攻击 Ability、连招、翻滚与 AnimNotifyState |
| [虫棍系统](docs/insect-glaive.md) | 猎虫、萃取、三灯、动作和资源设计 |
| [Motion Matching](docs/motion-matching.md) | 移动动画与 Motion Matching 配置 |
| [伤害计算](docs/exec-calc.md) | ExecCalc、SetByCaller、伤害与硬直公式 |
| [怪物与木桩](docs/monster-system.md) | Hitzone、训练木桩与渐进式怪物架构 |
| [GameplayCue](docs/gameplay-cue.md) | 命中反馈、Buff 表现与伤害数字 |
| [UI 系统](docs/ui-system.md) | HUD、准心、血条、耐力与虫棍资源 UI |
| [存储系统](docs/storage.md) | 背包、仓库与整理 |
| [使用系统](docs/use-system.md) | 快捷栏、UseAction 与 SpecialAction |
| [词条系统](docs/entries.md) | EntryCatalog、数值曲线与 DataManager |
| [GameplayTags](docs/gameplay-tags.md) | 项目 Tag 层级 |
| [设计决策](docs/design-decisions.md) | 架构选择与排序规则 |
| [验证方案](docs/verification.md) | 系统级验证清单 |

## 项目边界

- 当前面向单机/本地 Demo，不包含网络复制。
- `/Game/Maps/L_DemoArena` 是唯一默认地图。
- `/Game/TemplateAssets` 只保存 UE 模板资产，项目运行时资产不得放入其中。
- 不要在文件系统中直接移动或重命名 `.uasset`、`.umap`；必须使用 Unreal Content Browser 或 AssetTools。
