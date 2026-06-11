# MHGZ 第三人称游戏系统架构设计

> UE5.6 GAS 驱动。物品/属性/存储/使用/词条五大系统解耦。

> **⚠️ 适用范围：** 当前版本仅针对单机/本地游戏设计，不涉及网络复制（Multiplayer/Replication）。

> **📦 Build.cs 模块依赖：** 需在 `MHGZ.Build.cs` 中增加以下 GAS 核心模块：
> ```cpp
> "GameplayAbilities",   // UAbilitySystemComponent, UGameplayAbility, UAttributeSet, UGameplayEffect
> "GameplayTags",        // FGameplayTag, FGameplayTagContainer
> "GameplayTasks",       // UAbilityTask（GAS 异步任务基类）
> ```

---

## 文档索引

| 文档 | 内容 |
|------|------|
| [docs/gas-infrastructure.md](docs/gas-infrastructure.md) | GAS 基础设施——ASC挂载、组件归属、关卡切换 |
| [docs/items.md](docs/items.md) | 物品系统——定义、实例、客制化 |
| [docs/attributes.md](docs/attributes.md) | 角色属性与装备系统——AttributeSet、EquipmentComponent、DataManager、受击/霸体 |
| [docs/actions.md](docs/actions.md) | 动作系统——GAS+EnhancedInput、连招协调器、攻击Ability、翻滚、边缘跳越、AnimNotifyState |
| [docs/storage.md](docs/storage.md) | 存储系统——背包、仓库、一键整理 |
| [docs/use-system.md](docs/use-system.md) | 使用系统——快捷栏、UseAction、SpecialAction |
| [docs/entries.md](docs/entries.md) | 词条系统——DT_EntryCatalog、CT_EntryMagnitudes、DataManager |
| [docs/gameplay-tags.md](docs/gameplay-tags.md) | GameplayTags 完整层级 |
| [docs/monster-system.md](docs/monster-system.md) | 怪物与靶子系统——木桩、Hitzone、伤害数字、渐进式架构 |
| [docs/gameplay-cue.md](docs/gameplay-cue.md) | GameplayCue 系统——命中反馈/Buff光环/仇恨数字、自定义GC管理器、对象池 |
| [docs/design-decisions.md](docs/design-decisions.md) | 一键整理排序表 + 设计决策汇总 |
| [docs/directory-structure.md](docs/directory-structure.md) | 目录结构 |
| [docs/verification.md](docs/verification.md) | 验证方案 |
| [docs/pending.md](docs/pending.md) | 待补充设计 |
