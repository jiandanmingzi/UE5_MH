# 物品系统

> **实施状态说明（以源码为准）：** 本文保留通用物品、消耗品、堆叠与存档的完整方案。当前项目只实现了装备相关定义/实例及共享结构体；不存在 `UMHGZItemDefinition`、`UMHGZItemInstance`、`UMHGZConsumableDefinition` 等通用物品类。

## 当前实现

| 项目 | 当前状态 |
|------|----------|
| 装备定义 | `UMHGZEquipmentDefinition` 直接继承 `UPrimaryDataAsset`；`UMHGZWeaponDefinition` 继承装备定义。 |
| 装备实例 | `UMHGZEquipmentInstance` 直接继承 `UObject`，包含 GUID、Definition、Status、Customization 和镶嵌 Map。 |
| 镶嵌结构 | `FEquipmentSocket` 当前只有 `SocketName`、`SocketLevel`，没有 `bIsLocked`。 |
| 稀有度 | `EItemRarity` 当前为 Common/Uncommon/Rare/Epic/Legendary；装备另有 `RarityLevel` 整数，但没有自动生成 `Item.Rarity.r1~r12` Tag 的代码。 |
| 通用物品 | 数量、堆叠、可使用物品、材料、消耗品及序列化流程均未实现；下文作为详细方案保留。 |

**设计原则：** 定义与实例分离——`UPrimaryDataAsset` 定义物品模板（策划可编辑），`UObject` 表示运行时实例（数量/镶嵌状态）。`bIsUsable` 用成员变量 bool（决定代码路径），物品类型用 GameplayTag（纯分类扩展）。装备槽位用类继承+Tag 双重标识（子类决定字段，Tag 决定运行时槽位匹配）。

## FEquipmentSocket — 镶嵌孔位

```
USTRUCT(BlueprintType)
```

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| SocketName | FName | 必填 | 孔位名（如 "Gem_01"） |
| SocketLevel | int32 | 1 | 孔位等级（1-4），饰品等级 ≤ 此值方可镶入 |
| bIsLocked | bool | false | 规划扩展；当前结构体没有该字段 |

## FEntryReference — 词条引用

```
USTRUCT(BlueprintType)
```

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| EntryID | FName | 必填 | 词条编号，匹配 DT_EntryCatalog 中的行 |
| EntryLevel | int32 | 1 | 当前词条等级，须 ≤ 词条定义的 MaxLevel |

> **纯数据结构——不持有查询方法。** `FindEntryDefinition`、DT_EntryCatalog 和 CT_EntryMagnitudes 是保留方案，当前尚未实现/创建。

## FEntryModifier — 词条属性修饰器

```
USTRUCT(BlueprintType)
```

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| AttributeTag | FGameplayTag | 必填 | 目标属性标签 |
| Operation | TEnumAsByte\<EGameplayModOp::Type\> | Multiplicitive | 操作类型（Add/Multiply/Override） |
| MagnitudeCurve | TSoftObjectPtr\<UCurveFloat\> | nullptr | 当前每个 Modifier 直接引用等级→数值曲线 |

> **规划扩展：** 如改为集中 `CT_EntryMagnitudes`，可将 `MagnitudeCurve` 重构为 `FCurveTableRowHandle`，并增加 `DataManager::EvaluateEntryMagnitude` 与 Editor-time 校验。该集中曲线方案尚未实现。

## FEntryDefinition — 词条目录行

```
USTRUCT(BlueprintType)
struct FEntryDefinition
```

> 当前结构体不是 `FTableRowBase`，并保留 `EntryID` 字段；“DataTable RowName 即 EntryID、移除冗余字段”是创建词条目录时的目标方案。

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| EntryID | FName | 空 | 当前结构体中的词条 ID |
| DisplayName | FText | 空 | 词条名称 |
| Description | FText | 空 | 规划字段；当前不存在 |
| MaxLevel | int32 | 5 | 词条等级上限 |
| EntryTags | FGameplayTagContainer | 空 | 规划字段；当前不存在 |
| EffectType | EEntryEffectType | SimpleStat | SimpleStat=数值类（走 AttributeSet GE） / Complex=行为类（自定义 GE） / WeaponResource=武器资源类（不走 GE，直接修改资源组件参数） |
| Modifiers | TArray\<FEntryModifier\> | 空 | 属性修饰列表。SimpleStat→目标为 `Attribute.*`；WeaponResource→目标为资源参数标识（如 `WeaponResource.LongSword.RegenMultiplier`） |
| EffectClass | TSubclassOf\<UGameplayEffect\> | nullptr | 自定义 GE 蓝图（仅 Complex 时生效） |

> **前缀校验：** `UExecCalc_EntryStat` 检查 `Modifier.AttributeTag.MatchesTag("Attribute")`——仅处理 SimpleStat 修饰器。`WeaponResourceComponent::ApplyEntryModifier` 检查 `MatchesTag("WeaponResource")`——仅处理武器资源修饰器。不匹配则日志警告+跳过，防止属性修饰器被错误路由到资源组件（反之亦然）。

## UMHGZItemDefinition — 物品定义基类（规划，当前不存在）

```
UCLASS(BlueprintType, Abstract)
class UMHGZItemDefinition : public UPrimaryDataAsset
```

| 成员 | 类型 | Category | 默认值 | 说明 |
|------|------|----------|--------|------|
| ItemID | FName | "Item\|Base" | NAME_None | 全局唯一标识 |
| DisplayName | FText | "Item\|Base" | 空 | 显示名称 |
| Description | FText | "Item\|Base" | 空 | 描述文本 |
| Icon | TSoftObjectPtr\<UTexture2D\> | "Item\|Base" | nullptr | 图标 |
| RarityLevel | int32 | "Item\|Base" | 1 | 稀有度 r1-r12，自动生成 `Item.Rarity.r{N}` Tag |
| ItemTags | FGameplayTagContainer | "Item\|Base" | 空 | 多维度分类标签 |
| bIsUsable | bool | "Item\|Base" | false | 是否可使用（决定能否进快捷栏） |
| BackpackMaxStack | int32 | "Item\|Base" | 1 | 背包堆叠上限 |
| Price | int32 | "Item\|Base" | 0 | 售价 |
| bCanDiscard | bool | "Item\|Base" | true | 是否可丢弃 |

方法：

- `bool IsEquipment() const`
  - 作用：虚函数，装备子类覆写返回 true。用于运行时分清装备/非装备。
- `FPrimaryAssetId GetPrimaryAssetId() const override`
  - 输出：`FPrimaryAssetId("MHGZItemDefinition", ItemID)`，供 AssetManager 异步加载。

> **堆叠上限的职责归属：** `BackpackMaxStack` 是物品定义层的固有属性。仓库 99999 是存储层的策略——`UMHGWarehouseComponent` 内部使用常量 `WAREHOUSE_MAX_STACK = 99999`，不依赖 Definition 的方法。

## UMHGZEquipmentDefinition — 装备定义（已实现；下述通用 Item 基类重构为规划）

当前类非 Abstract，直接继承 `UPrimaryDataAsset`，已包含 ItemID、DisplayName、RarityLevel、ItemTypeTag、EquipmentSlotTag、AttackPower、Defense、CriticalRate、Entries、Sockets、Mesh、AttachSocket，PrimaryAssetType 为 `EquipmentDef`。以下继承 `UMHGZItemDefinition` 的层级是后续统一物品系统方案。

```
UCLASS(BlueprintType, Abstract)
class UMHGZEquipmentDefinition : public UMHGZItemDefinition
```

构造函数设置 `bIsUsable=false`，`BackpackMaxStack=1`。

| 成员（追加） | 类型 | Category | 默认值 | 说明 |
|------|------|----------|--------|------|
| EquipmentSlotTag | FGameplayTag | "Equipment" | 空 | 装备槽位标签（protected，子类构造函数设置，只读） |
| SocketSlots | TArray\<FEquipmentSocket\> | "Equipment\|Socket" | 空 | 镶嵌孔位（0-3 个） |
| Entries | TArray\<FEntryReference\> | "Equipment\|Entries" | 空 | 词条引用列表 |

方法（追加）：

- `bool IsEquipment() const override`
  - 输出：true。
- `bool HasSocketSlots() const`
  - 输出：SocketSlots 数组非空。
- `bool GetSocketByName(FName Name, FEquipmentSocket& Out) const`
  - 输入：孔位名。
  - 输出：通过 Out 返回孔位信息。返回值为是否找到。
- `bool CanSocketAccessory(int32 AccessoryLevel, FName SocketName) const`
  - 输入：饰品等级、孔位名。
  - 输出：是否可镶入。
  - 作用：比较 `AccessoryLevel ≤ SocketLevel`。

## UMHGZWeaponDefinition — 武器定义

```
UCLASS(BlueprintType)
class UMHGZWeaponDefinition : public UMHGZEquipmentDefinition
```

构造函数设置 `EquipmentSlotTag = Equipment.Slot.Weapon`。

| 成员（追加） | 类型 | Category | 默认值 | 说明 |
|------|------|----------|--------|------|
| WeaponTypeTag | FGameplayTag | "Weapon" | 空 | 武器种类标签 |
| AttackPower | float | "Weapon\|Combat" | 0 | 攻击力（装备时转为 GE 加成角色攻击力） |
| CriticalRate | float | "Weapon\|Combat" | 0 | 会心率（装备时转为 GE 加成角色会心率） |
| Mesh | TSoftObjectPtr\<USkeletalMesh\> | "Weapon\|Visual" | nullptr | 武器模型 |
| SwingSoundOverrides | TMap\<FGameplayTag, TSoftObjectPtr\<USoundBase\>\> | "Weapon\|Audio" | 空 | 字段已存在，但 AudioIdentityTag 消费与 Notify 注入尚未实现 |

> **Tag 覆盖模式：** 每个 GA 蓝图的 `AudioIdentityTag` 是键。骨刀纵斩 = Map 中加 `{Audio.Swing.LS_VerticalSlash → SW_Bone_Slash_01}`，其余招式未覆盖→自动回退 GA 默认。零新蓝图，Map 空时零配置。

## UMHGZArmorDefinition — 衣服定义（规划，当前不存在）

```
UCLASS(BlueprintType)
class UMHGZArmorDefinition : public UMHGZEquipmentDefinition
```

构造函数设置 `EquipmentSlotTag = Equipment.Slot.Armor`。

| 成员（追加） | 类型 | Category | 默认值 | 说明 |
|------|------|----------|--------|------|
| ArmorTypeTag | FGameplayTag | "Armor" | 空 | 护甲类型标签 |
| Defense | float | "Armor\|Combat" | 0 | 防御力（装备时转为 GE 加成角色防御力） |
| Mesh | TSoftObjectPtr\<USkeletalMesh\> | "Armor\|Visual" | nullptr | 衣服模型 |

## UMHGZAccessoryDefinition — 饰品定义（规划，当前不存在）

```
UCLASS(BlueprintType)
class UMHGZAccessoryDefinition : public UMHGZEquipmentDefinition
```

构造函数设置 `EquipmentSlotTag = Equipment.Slot.Accessory`，`SocketSlots` 强制为空（饰品不可再镶嵌）。饰品不提供属性，仅通过词条 GE 生效。

| 成员（追加） | 类型 | Category | 默认值 | 说明 |
|------|------|----------|--------|------|
| AccessoryLevel | int32 | "Accessory" | 1 | 饰品等级（1-4），须 ≤ 孔位等级方可镶入 |

## UMHGZConsumableDefinition — 可使用物品定义（规划，当前不存在）

```
UCLASS(BlueprintType)
class UMHGZConsumableDefinition : public UMHGZItemDefinition
```

构造函数设置 `bIsUsable = true`。

| 成员（追加） | 类型 | Category | 默认值 | 说明 |
|------|------|----------|--------|------|
| UseActionClass | TSubclassOf\<UMHGZUseAction\> | "Consumable\|Use" | nullptr | 使用行为类 |
| UseCooldown | float | "Consumable\|Use" | 0 | 使用冷却（秒） |
| UseAnimation | TSoftObjectPtr\<UAnimMontage\> | "Consumable\|Animation" | nullptr | 使用动画 |
| bConsumeOnUse | bool | "Consumable\|Use" | true | 使用后是否消耗 |
| UseStartSound | TObjectPtr\<USoundBase\> | "Consumable\|Audio" | nullptr | 使用开始音效（喝药声/投掷风声） |
| UseLoopSound | TObjectPtr\<USoundBase\> | "Consumable\|Audio" | nullptr | 使用循环音效（持续喝药——可选） |
| UseCompleteSound | TObjectPtr\<USoundBase\> | "Consumable\|Audio" | nullptr | 使用完成音效（回复叮咚/爆炸声） |

> **触发点：** `GA_UseItem` 子类（如 `UseAction_Heal`）在 `ActivateAbility` 和效果生效时播放。

## FItemCustomization — 物品客制化

```
USTRUCT(BlueprintType)
struct FItemCustomization
```

每件物品实例独立持有的客制化数据。对 Definition 的数值和词条做增量修改。

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| StatOverrides | TMap\<FGameplayTag, float\> | 空 | 属性覆写（Key=`Attribute.AttackPower`/`Attribute.CriticalRate`/`Attribute.Defense`，有则覆盖 Definition 值，无则用 Definition 原值）。Key 用 GameplayTag 匹配 Attribute 体系——与词条修饰器 `FEntryModifier::AttributeTag` 一致，避免魔法字符串拼写错误 |
| AddedEntries | TArray\<FName\> | 空 | 当前只保存新增词条 ID；升级为 FEntryReference 是后续方案 |
| RemovedEntryIDs | TArray\<FName\> | 空 | 从 Definition.Entries 中移除的词条 ID |
| ModifiedEntries | TMap\<FName, int32\> | 空 | 覆盖 Definition.Entries 中同名 EntryID 的等级（Key=EntryID → Value=新 EntryLevel）。O(1) 查找，语义明确——"修改"而非"新增"。用于"词条升级"场景（如 AttackUp Lv2→Lv3），无需 Remove+Add 两步操作 |

> StatOverrides 支持增减：`Attribute.AttackPower`=20 覆盖原 15（+5），`Attribute.AttackPower`=10 覆盖原 15（-5）。AddedEntries 和 RemovedEntryIDs 让客制化可以增删词条。ModifiedEntries 用于覆盖已有词条的等级——`ApplyItemEffects` 收集有效词条时优先级：`ModifiedEntries` > `RemovedEntryIDs`（若同一 EntryID 同时出现在 ModifiedEntries 和 RemovedEntryIDs 中，以 ModifiedEntries 为准，即"升级后不删除"）。

## UMHGZItemInstance — 物品实例（消耗品/材料，规划，当前不存在）

```
UCLASS(BlueprintType)
class UMHGZItemInstance : public UObject
```

运行时对象，用于消耗品、材料、任务物品等在背包/仓库中管理的物品。

| 成员 | 类型 | Category | 默认值 | 说明 |
|------|------|----------|--------|------|
| Definition | TObjectPtr\<UMHGZItemDefinition\> | "Instance" | nullptr | 指向物品定义（只读） |
| Quantity | int32 | "Instance" | 1 | 当前数量 |
| InstanceID | FGuid | "Instance" | NewGuid() | 存档唯一标识。构造函数中 `NewGuid()`；`PostLoad()` 中若 Guid 有效（从存档恢复）则保留，若 Guid 无效（旧版存档或新创建）则生成新 Guid。存/读档系统待后续实现 |

> **Guid 生命周期：** 构造函数始终 `NewGuid()`（运行时创建新物品）。`PostLoad()` 检查 `UPROPERTY()` 标记的 Guid 是否已从存档反序列化恢复——若已有效（非默认值）则保留，否则生成新 Guid。避免构造函数无条件覆盖已恢复的存档数据。

方法：

- `bool IsEquipment() const`
  - 输出：`Definition→IsEquipment()`。
- `bool IsUsable() const`
  - 输出：`Definition→bIsUsable`。
- `bool CanStackWith(const UMHGZItemInstance* Other) const`
  - 输入：另一个物品实例。
  - 输出：是否可堆叠。
  - 作用：同 Definition 且 Quantity 未达 `GetMaxStack()` 时返回 true。
- `int32 GetMaxStack() const`
  - 输出：最大堆叠数。
  - 作用：返回 `Definition→BackpackMaxStack`。仓库场景不使用此方法——仓库组件内部用常量 `WAREHOUSE_MAX_STACK = 99999` 自行管理堆叠逻辑。
- `bool IsStackFull() const`
  - 输出：Quantity 是否已达到 `GetMaxStack()`。
- `int32 AddQuantity(int32 Count)`
  - 输入：添加数量。
  - 输出：实际增加量（受 `GetMaxStack()` 约束，超出部分被截断）。
  - 注意：此方法仅用于背包场景（上限 = BackpackMaxStack）。仓库的存入操作由 `UMHGWarehouseComponent::DepositItem` 自行处理，不走此方法。
- `int32 RemoveQuantity(int32 Count)`
  - 输入：移除数量。
  - 输出：实际减少量（不低于 0，不会让 Quantity 变为负数）。
- `bool IsEmpty() const`
  - 输出：Quantity <= 0。

## UMHGZEquipmentInstance — 装备实例（武器/衣服/饰品）

```
UCLASS(BlueprintType)
class UMHGZEquipmentInstance : public UObject
```

当前直接继承 UObject，用于装备；没有 Quantity/堆叠接口。未来通用物品系统可再引入 ItemInstance 基类。

**EEquipmentStatus 枚举：**

| 值 | 说明 |
|----|------|
| InStorage | 在仓库中，空闲 |
| Equipped | 装备在角色身上 |
| Socketed | 镶嵌在某件装备中 |

| 成员（追加） | 类型 | Category | 默认值 | 说明 |
|------|------|----------|--------|------|
| InstanceID | FGuid | — | CreateEquipmentInstance 时 NewGuid | 运行时装备实例标识 |
| Definition | TObjectPtr\<UMHGZEquipmentDefinition\> | — | nullptr | 装备定义引用 |
| **Status** | **EEquipmentStatus** | **"Equipment\|State"** | **InStorage** | 当前状态，由 EquipmentComponent 维护 |
| Customization | FItemCustomization | "Equipment\|State" | 空 | 客制化覆写（饰品不可客制化，始终为空） |
| SocketedAccessories | TMap\<FName, TObjectPtr\<UMHGZEquipmentInstance\>\> | "Equipment\|State" | 空 | 已镶嵌饰品（Key=孔位名，作用域限本实例） |

方法（覆写/追加）：

- `CanStackWith/GetMaxStack`：规划中的通用 ItemInstance 接口，当前类没有这些方法。
- `bool CanSocketAccessory(const UMHGZEquipmentInstance* Accessory, FName SocketName) const`
  - 输入：待镶嵌饰品、目标孔位名。
  - 输出：是否可镶入。
  - 作用：当前遍历 `Definition->Sockets`，比较饰品 `Definition->RarityLevel ≤ SocketLevel`。
- `void SocketAccessory(UMHGZEquipmentInstance* Accessory, FName SocketName)`
  - 输入：饰品实例、孔位名。
  - 作用：将饰品写入 `SocketedAccessories` Map。
- `UMHGZEquipmentInstance* RemoveAccessory(FName SocketName)`
  - 输入：孔位名。
  - 输出：拆除的饰品实例指针。
  - 作用：从 `SocketedAccessories` 移除并返回。
- `void SetStatus(EEquipmentStatus NewStatus)`
  - 输入：新状态。
  - 作用：当前为内联赋值，没有状态变化 Delegate。

> `FOnEquipmentInstanceStatusChanged` 是后续 UI 刷新方案，当前未声明。
