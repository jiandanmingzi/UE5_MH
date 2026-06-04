# 词条系统

**设计原则：** 目录集中定义 + 装备仅存 ID 引用。DT_EntryCatalog（DataTable）登记所有词条的完整信息。80% 词条为 SimpleStat——通过 CurveTable 曲线 + 通用 GE_EntryStat + UExecCalc_EntryStat 参数化处理。20% 为 Complex——各自创建自定义 GE 蓝图。所有 SimpleStat 词条的等级→数值曲线集中在 CT_EntryMagnitudes 一个 CurveTable。

## DT_EntryCatalog — 词条目录

DataTable，RowStruct = `FEntryDefinition`（结构定义见 [items.md](items.md)）。

示例行：

| EntryID | MaxLevel | EffectType | Modifiers | EffectClass |
|------|:--:|:--:|------|------|
| AttackUp | 5 | SimpleStat | [{ Attr=AttackPower, Op=Add, Curve=Curve_AttackUp }] | — |
| CritBoost | 3 | SimpleStat | [{ Attr=CriticalRate, Op=Add, Curve=Curve_CritBoost }] | — |
| AttackMaster | 5 | SimpleStat | [{ Attr=AttackPower, Op=Add, Curve=Curve_AtkMas_Atk }, { Attr=CriticalRate, Op=Add, Curve=Curve_AtkMas_Crit }] | — |
| LifeSteal | 3 | Complex | — | GE_LifeSteal |

## CT_EntryMagnitudes — 词条数值曲线表

CurveTable，每行一条 FRichCurve，X=词条等级，Y=该等级数值。

示例行：

| 曲线名 | Lv1 | Lv2 | Lv3 | Lv4 | Lv5 |
|------|:--:|:--:|:--:|:--:|:--:|
| Curve_AttackUp | 3 | 6 | 9 | 12 | 15 |
| Curve_CritBoost | 20 | 50 | 100 | — | — |
| Curve_AtkMas_Atk | 3 | 6 | 9 | 12 | 15 |
| Curve_AtkMas_Crit | 0 | 0 | 0 | 5 | 10 |

> Curve_CritBoost：非均匀——Lv1=20%, Lv2=50%, Lv3=100%。Curve_AtkMas_Crit：Lv4 才出现会心加成——多属性+突变。

## UMHGZDataManager — 全局数据管理器

```
UCLASS()
class UMHGZDataManager : public UGameInstanceSubsystem
```

GameInstanceSubsystem，全局单例。统一持有所有全局 DataTable/CurveTable 的引用。

| 成员 | 类型 | 说明 |
|------|------|------|
| DT_EntryCatalog | TSoftObjectPtr\<UDataTable\> | 词条目录 |
| CT_EntryMagnitudes | TSoftObjectPtr\<UCurveTable\> | 词条数值曲线 |
| DT_AbilityScalars | TSoftObjectPtr\<UCurveTable\> | Ability FScalableFloat 全局曲线表 |
| DT_WeaponResourceConfig | TSoftObjectPtr\<UDataTable\> | 武器种类→资源 UI Widget 桥接 |
| DT_WeaponComboConfig | TSoftObjectPtr\<UDataTable\> | 武器种类→连招表桥接 |
| DT_WeaponDodgeConfig | TSoftObjectPtr\<UDataTable\> | 武器种类→翻滚参数 |

方法：

- `Initialize` — 同步加载所有 DataTable/CurveTable 引用
- `FindEntryDefinition(FName EntryID, FEntryDefinition& OutDef)` — 查词条目录
- `EvaluateEntryMagnitude(FName CurveName, int32 Level)` — 查曲线数值
- `FindWeaponResourceConfig(FGameplayTag)` — 查武器资源 UI Widget
- `FindWeaponComboData(FGameplayTag)` — 查已加载连招表
- `FGuid RequestWeaponComboData(FGameplayTag, TFunction<void(UMHGZWeaponComboData*, FGuid)>)` — **异步加载** ComboData，返回 RequestID（竞态保护用）
- `FindWeaponDodgeConfig(FGameplayTag)` — 查武器翻滚配置

> **访问方式：** 任意代码通过 `GetWorld()->GetGameInstance()->GetSubsystem<UMHGZDataManager>()` 获取。

### 竞态保护——RequestID 机制

玩家快速连续换装时可能存在多个并发异步加载。`RequestWeaponComboData` 返回 `FGuid` RequestID（由 DataManager 内部 `NewGuid()` 唯一生成）。调用方存储到 `Coordinator→ActiveLoadRequestID`，回调中比较——不一致则丢弃过期数据。
