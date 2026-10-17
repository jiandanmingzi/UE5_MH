# 出生血量与耐力配置

更新：2026-10-17。已接入现有GAS核心初始化，不新增运行时上限变量。

## 在编辑器哪里改

打开资产：`/Game/GameplayEffects/Core/GE_PlayerInitialAttributes`。

在类默认值的`Modifiers`中修改`Modifier Magnitude → Scalable Float Magnitude → Value`：

| 数组索引 | Attribute | 默认值 | 用途 |
|---|---|---:|---|
| 0 | MaxHealth | 200 | 出生体力上限 |
| 1 | MaxStamina | 200 | 出生耐力上限 |
| 2 | Health | 200 | 出生当前体力 |
| 3 | Stamina | 200 | 出生当前耐力 |

例如要出生满血300、满耐250，将四项依次改成`300、250、300、250`。仅改Max不代表会自动补满当前值；可以有意配置受伤/未满耐出生。

保留`Duration Policy = Instant`、四项`Modifier Op = Override`和先Max后Current的顺序。编译并保存资产，重新开始PIE即可生效；不需要重新编译C++，不重跑生成脚本或全量验证。

## 接入与生命周期

接入位置：`/Game/Blueprints/Characters/Demo/BP_PlayerState`，选择`AbilitySystemComponent`组件，在`Ability | Core → Core Attribute Effects`中已引用该GE一次。

既有`InitializeAbilitySystem()`在首次初始化时按列表应用GE，随后设置幂等标记。重复初始化不会再次补血补耐，换武器不重置本列表或应用出生值。新PlayerState/ASC会重新应用自己的出生配置；沿用原PlayerState的复活补满不由本GE自动处理。

原生AttributeSet的200只是无配置时的安全默认值；已配置的玩家以初始化GE为准。无需再为玩家调参修改C++构造函数，也不要把GE改为永久Override效果，否则会干扰后续上限增益。

## 运行时上限变化

装备、增益、减益使用其他GE修改`MaxHealth/MaxStamina`，不要重新施加出生GE。现有HUD会监听Max变化并按`条身长度 = 582 × Max / 200`更新；填充仍按`Current / Max`计算。

本次未替未来效果选择“加上限时补差值/等比例补值/不补值”或“降上限时如何收敛当前值”的玩法政策；这些由对应效果合同另行明确。当前定向检查覆盖增益50、移除增益及当前值低于撤销后上限的情况，不冒称已覆盖高当前值下的所有上限下降策略。

## 维护与证据

- 一次性创建/接入脚本：`Scripts/AssetOrganization/setup_player_initial_attributes.py`。默认只读回查；首次Apply会备份PlayerState，不覆盖已有GE。日常调数只改资产，不重跑Apply。
- 本轮仅保存新GE和PlayerState两包；PlayerState除了核心GE列表外的原生导出一致，其他90包哈希保持。
- 冷回载：`Saved/Maintenance/InitialAttributes20261017/verified.json`。
- 定向运行时检查：`MHGZ.Maintenance.PlayerInitialAttributes.OnceAndBuffs`，报告`Saved/Maintenance/InitialAttributes20261017/Tests/index.json`。验证真实BP初始化、重复初始化不补满、其他GE可增减上限且不被出生配置覆盖。
- 未执行PIE视觉验收或全量回归；之前血条/急袭判定的签收状态不因这次资产接入而改变。
