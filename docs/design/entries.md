# 词条系统

> **实施状态说明（以源码为准）：** 本文中的目录、曲线、通用 GE、ExecCalc 和异步竞态保护是保留的完整方案。当前只实现了词条相关结构体、DataManager 的 SoftObjectPtr 配置字段及部分同步 Getter；尚未形成可用的词条 Apply 链路。

## 当前实现

| 项目 | 当前状态 |
|------|----------|
| 数据结构 | `FEntryReference`、`FEntryDefinition`、`FEntryModifier`、`EEntryEffectType` 已存在。当前 `FEntryDefinition` 仍包含 `EntryID` 字段。 |
| DataManager | `Initialize` 不预加载资产；`GetEntryCatalog/GetWeaponComboConfig/GetWeaponResourceConfig/GetAbilityScalars` 使用 `LoadSynchronous`。 |
| 查询接口 | `FindEntryDefinition`、`GetEntryMagnitude`、ComboData 异步请求和 RequestID 机制尚未实现。 |
| 应用链 | `UMHGZEquipmentComponent::ApplyEntryGEs` 是空实现；`GE_EntryStat`、`UExecCalc_EntryStat` 及对应 DataTable/CurveTable 资产未创建。 |

**设计原则：** 目录集中定义 + 装备仅存 ID 引用。DT_EntryCatalog（DataTable）登记所有词条的完整信息（名称、描述、最大等级、效果类型、修饰器或 GE 类）。装备/饰品只存 `{EntryID, EntryLevel}`。80% 词条为 SimpleStat——通过 CurveTable 曲线 + 通用 GE_EntryStat + UExecCalc_EntryStat 参数化处理，无需各自创建 GE 蓝图。20% 为 Complex——各自创建自定义 GE 蓝图。所有 SimpleStat 词条的等级→数值曲线集中在 CT_EntryMagnitudes 一个 CurveTable。

## DT_EntryCatalog — 词条目录（规划，资产未创建）

规划中的 DataTable，RowStruct = `FEntryDefinition`（见 [items.md](items.md) 1.4 节）。目标约定是 **RowName 即 EntryID**；当前 C++ 结构体仍保留 `FEntryDefinition::EntryID` 字段，待实现目录资产时再决定是否去重。

示例行：

| EntryID | MaxLevel | EffectType | Modifiers | EffectClass |
|------|:--:|:--:|------|------|
| AttackUp | 5 | SimpleStat | [{ Attr=AttackPower, Op=Add, Curve=Curve_AttackUp }] | — |
| CritBoost | 3 | SimpleStat | [{ Attr=CriticalRate, Op=Add, Curve=Curve_CritBoost }] | — |
| AttackMaster | 5 | SimpleStat | [{ Attr=AttackPower, Op=Add, Curve=Curve_AtkMas_Atk }, { Attr=CriticalRate, Op=Add, Curve=Curve_AtkMas_Crit }] | — |
| LifeSteal | 3 | Complex | — | GE_LifeSteal |

> 规划中的运行时查询通过 `UMHGZDataManager::FindEntryDefinition(EntryID)`；当前没有该方法。

## CT_EntryMagnitudes — 词条数值曲线表（规划，资产未创建）

CurveTable，每行一条 FRichCurve，X=词条等级，Y=该等级数值。策划在编辑器中拖拽曲线节点，支持任意非线性形状——可配置平滑递增、阶梯突变、高等级解锁新属性等复杂数值模型。

示例行：

| 曲线名 | Lv1 | Lv2 | Lv3 | Lv4 | Lv5 |
|------|:--:|:--:|:--:|:--:|:--:|
| Curve_AttackUp | 3 | 6 | 9 | 12 | 15 |
| Curve_CritBoost | 20 | 50 | 100 | — | — |
| Curve_AtkMas_Atk | 3 | 6 | 9 | 12 | 15 |
| Curve_AtkMas_Crit | 0 | 0 | 0 | 5 | 10 |

> Curve_CritBoost：非均匀——Lv1=20%, Lv2=50%, Lv3=100%。Curve_AtkMas_Crit：Lv4 才出现会心加成——多属性+突变。

## 三种 EffectType 实现流程（规划；ApplyEntryGEs 当前为空）

### SimpleStat（80% 词条）——纯数值修改

```
装备 Apply 流程:
  EquipmentComponent::ApplyEntryGEs
    → ASC->MakeOutgoingSpec(GE_EntryStat)     // 通用 GE 蓝图，无预设 Modifiers
    → Spec->SetByCaller("EntryID", EntryID)
    → Spec->SetByCaller("EntryLevel", Level)
    → ASC->ApplyGameplayEffectSpecToSelf(Spec)
        → UExecCalc_EntryStat::Execute:
            → DataManager->FindEntryDefinition(EntryID)
            → 遍历 Modifiers:
                → 检查 MatchesTag("Attribute") ✓
                → Value = DataManager->EvaluateEntryMagnitude(CurveName, Level)
                → Out.AddOutputModifier(Attribute, Op, Value)
                   // 直接修改 ASC 的 Attribute，如 AttackPower +15
```

### Complex（少数）——自定义 GE 蓝图

```
装备 Apply 流程:
  EquipmentComponent::ApplyEntryGEs
    → 实例化 FEntryDefinition::EffectClass（策划创建的 GE 蓝图）
    → ASC->ApplyGameplayEffectSpecToSelf(Spec)
        → GE 蓝图自身配置了 Modifiers、Duration、ExecCalc 等
        → GAS 按 GE 蓝图定义执行（如 GE_LifeSteal: HasDuration + 自定义 ExecCalc）
```

### WeaponResource——直接修改资源组件参数（不走 GE）

```
装备 Apply 流程:
  EquipmentComponent::ApplyEntryGEs
    → 找到当前武器的 ResourceComponent（如 URes_LongSword）
    → 遍历 FEntryDefinition::Modifiers:
        → 检查 MatchesTag("WeaponResource") ✓
        → ResourceComponent->ApplyEntryModifier(Tag, Value, Op)
            → 存入 ActiveModifiers Map（Key=Tag, Value={Op, Magnitude}）
    → ResourceComponent::GetModifiedParam("RegenMultiplier")
        → 返回 BaseValue × ActiveModifiers 累计倍率
```

> **复合词条策略：** 若一个词条需同时修改 Attribute 和 WeaponResource，拆分为两个独立 `FEntryReference`（一个 SimpleStat + 一个 WeaponResource），或使用 Complex 创建自定义 GE 蓝图。详见 [attributes.md](attributes.md) ApplyEntryGEs 段。

## UMHGZDataManager — 全局数据管理器

```
UCLASS(Config=Game, DefaultConfig)
class UMHGZDataManager : public UGameInstanceSubsystem
```

GameInstanceSubsystem，全局单例。统一持有所有全局 DataTable/CurveTable 的引用，解决 `FEntryReference`（纯数据结构）和 `UExecCalc_EntryStat`（无实例状态）无法自行访问资产的问题。

### 重要成员

| 成员 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| EntryCatalog | TSoftObjectPtr\<UDataTable\> | nullptr | 词条目录配置字段；当前未配置资产 |
| EntryMagnitudes | TSoftObjectPtr\<UCurveTable\> | nullptr | 词条数值曲线配置字段；当前无 Getter |
| AbilityScalars | TSoftObjectPtr\<UCurveTable\> | nullptr | Ability FScalableFloat 全局曲线表 |
| WeaponResourceConfig | TSoftObjectPtr\<UDataTable\> | nullptr | 武器种类→资源组件类和 UI 类映射；当前未配置 |
| WeaponComboConfig | TSoftObjectPtr\<UDataTable\> | nullptr | 武器种类→连招表映射；当前已在 Ini 配置 |
| DodgeConfig | TSoftObjectPtr\<UDataTable\> | nullptr | 武器种类→翻滚参数；当前无 Getter |

### 当前方法与规划方法

- `virtual void Initialize(FSubsystemCollectionBase& Collection) override`
  - 当前为空实现，不预加载资产。

- 当前 Getter：`GetEntryCatalog()`、`GetWeaponComboConfig()`、`GetWeaponResourceConfig()`、`GetAbilityScalars()`，调用时各自 `LoadSynchronous()`。

以下查询、异步加载与竞态保护 API 均为保留方案，尚未实现：

> **为何用同步加载而非异步：** `DT_EntryCatalog`（~100-500 行）、`CT_EntryMagnitudes`（~100-200 条曲线）等均为轻量资产，合计 < 1MB，`LoadSynchronous` 阻塞时间 < 50ms（编辑器开发模式下资产已驻留内存，近乎零开销）。这些数据被所有系统依赖（装备组件、ExecCalc、UI），异步加载反而要求每个调用方处理"尚未就绪"的情况——判空成本 > 50ms 启动延迟。仅在启动时加载一次，后续 `FindEntryDefinition` 等查询均 O(1)。

- `bool FindEntryDefinition(FName EntryID, FEntryDefinition& OutDef) const`
  - 输入：词条 ID（对应 DT_EntryCatalog RowName）。
  - 输出：通过 OutDef 返回词条完整定义。返回值为是否查到。
  - 作用：`DT_EntryCatalog→FindRow<FEntryDefinition>(EntryID)`。

- `float EvaluateEntryMagnitude(FName CurveName, int32 Level) const`
  - 输入：曲线名、词条等级。
  - 输出：该等级对应的数值。
  - 作用：`CT_EntryMagnitudes→Eval(CurveName, Level)`。

- `FWeaponResourceConfig* FindWeaponResourceConfig(FGameplayTag WeaponTypeTag) const`
  - 输入：武器种类 Tag。
  - 输出：对应的资源 UI Widget 配置指针，未找到返回 nullptr。
  - 作用：按武器种类查找资源 UI Widget 类引用。

- `UMHGZWeaponComboData* FindWeaponComboData(FGameplayTag WeaponTypeTag) const`
  - 输入：武器种类 Tag。
  - 输出：已加载的连招表 DataAsset 指针，未加载或未找到返回 nullptr。
  - 作用：同步查找已驻留在内存中的 ComboData。仅在资产已通过 `RequestWeaponComboData` 异步加载完成后可用。

- `FGuid RequestWeaponComboData(FGameplayTag WeaponTypeTag, TFunction<void(UMHGZWeaponComboData*, FGuid)> Callback)`
  - 输入：武器种类 Tag、加载完成回调。
  - 输出：`FGuid` RequestID（DataManager 内部 `NewGuid()` 唯一生成），用于竞态保护。
  - 作用：异步加载 ComboData 资产。加载完成后调用 Callback，传入加载结果和 RequestID。调用方可在回调中比较 RequestID 以丢弃过期数据。

- `FWeaponDodgeConfig* FindWeaponDodgeConfig(FGameplayTag WeaponTypeTag) const`
  - 输入：武器种类 Tag。
  - 输出：对应的翻滚配置指针，未找到返回 nullptr。
  - 作用：按武器种类查找翻滚参数配置。

> **访问方式：** 任意需要这些数据的代码（ExecCalc、EquipmentComponent、GA_Dodge 等）通过 `GetWorld()->GetGameInstance()->GetSubsystem<UMHGZDataManager>()` 获取。策划只需在 DataManager 蓝图或 Ini 中配置一次资产引用，无需在多处重复设置。

### 竞态保护——RequestID 机制（规划）

玩家快速连续换装时可能存在多个并发异步加载请求。`RequestWeaponComboData` 返回 `FGuid` RequestID（由 DataManager 内部 `FGuid::NewGuid()` 唯一生成）。

**调用方使用模式：**

```
// 协调器中
ActiveLoadRequestID = DataManager->RequestWeaponComboData(WeaponTag,
    [this](UMHGZWeaponComboData* Data, FGuid RequestID)
    {
        // 竞态保护：仅当 RequestID 与当前活跃请求一致时才应用
        if (RequestID == ActiveLoadRequestID && Data)
        {
            SetComboData(Data);
        }
        // 否则丢弃——这是旧装备的过期加载结果
    });
```

**竞态场景示例：**

```
时间线：
t0: 装备太刀 → RequestWeaponComboData(太刀) → RequestID=A
t1: 卸下太刀，装备大剑 → RequestWeaponComboData(大剑) → RequestID=B
t2: 太刀 ComboData 异步加载完成 → Callback(A, 太刀Data)
    → A != ActiveLoadRequestID(B) → 丢弃 ✓
t3: 大剑 ComboData 异步加载完成 → Callback(B, 大剑Data)
    → B == ActiveLoadRequestID(B) → SetComboData ✓
```

> 此机制确保即使异步加载乱序完成，也只会应用最新请求的结果，避免旧装备的连招表覆盖新装备。
