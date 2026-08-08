# 存储系统

> **实施状态说明（以源码为准）：** `UMHGZBackpackComponent` 和 `UMHGZWarehouseComponent` 当前都是无数据、无接口的 Demo 桩组件。`FStorageSlot`、堆叠、转移、分类、搜索、整理和仓库 UI 均为下文保留的详细方案，尚未实现。

**设计原则：** 背包有限格（默认 30）、不可分类、堆叠上限跟随物品定义。仓库无限格、统一 99999 堆叠、可分类浏览（GameplayTag 标签页）。一键整理 = 堆叠合并 + 三级排序（品类→稀有度降序→名称升序）。

## FStorageSlot — 存储槽位

```
USTRUCT(BlueprintType)
```

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| ItemInstance | TObjectPtr\<UMHGZItemInstance\> | nullptr | 槽位中的物品实例 |
| SlotIndex | int32 | -1 | 槽位序号 |

方法：`bool IsEmpty() const`、`void Clear()`。

## UMHGZBackpackComponent — 背包组件

```
UCLASS(ClassGroup=(Inventory), BlueprintType)
class UMHGZBackpackComponent : public UActorComponent
```

挂载到 PlayerState，跨关卡保留。

| 成员 | 类型 | Category | 默认值 | 说明 |
|------|------|----------|--------|------|
| MaxSlotCount | int32 | "Backpack\|Config" | 30 | 最大槽位数 |
| Slots | TArray\<FStorageSlot\> | "Backpack\|State" | 预分配 30 空槽 | 槽位数组 |

方法：

- `int32 AddItem(UMHGZItemDefinition* Def, int32 Count)`
  - 输入：物品定义、数量。
  - 输出：**实际放入的数量**（支持部分成功：若背包只能容纳 3 个但请求 5 个，返回 3，剩余的 2 个由调用方处理——如留在地上或提示背包已满）。
  - 作用：优先堆叠已有同类物品，满堆叠后占用新空槽，直到 Count 全部放入或无空位。
- `int32 AddItemInstance(UMHGZItemInstance* Instance, int32 Count)`
  - 输入：已有物品实例、数量。
  - 输出：**实际放入的数量**（同上，支持部分成功）。
  - 作用：同上，使用已存在的 ItemInstance 而非新建。
- `int32 RemoveItem(UMHGZItemInstance* Instance, int32 Count)`
  - 输入：实例、数量。
  - 输出：实际移除量（可能小于 Count，若实例数量不足）。
- `int32 RemoveItemAtSlot(int32 SlotIndex, int32 Count)`
  - 输入：槽位索引、数量。
  - 输出：实际移除量。
- `UMHGZItemInstance* GetItemAtSlot(int32 SlotIndex)`
  - 输入：槽位索引。
  - 输出：物品实例指针，空槽返回 nullptr。
- `int32 FindFirstEmptySlot()`
  - 输出：首个空槽索引，-1 表示无空槽。
- `int32 FindSlotForItem(UMHGZItemInstance* Instance)`
  - 输入：物品实例。
  - 输出：所在槽位索引，-1 表示未找到。
- `int32 GetAvailableSpaceFor(UMHGZItemDefinition* Def)`
  - 输入：物品定义。
  - 输出：背包中还能放入该物品的总数量。
- `bool CanAddItem(UMHGZItemDefinition* Def, int32 Count)`
  - 输入：物品定义、数量。
  - 输出：是否有足够空间。
- `bool MoveItem(int32 From, int32 To)`
  - 输入：源槽位、目标槽位。
  - 输出：成功与否。
  - 作用：移动或交换两槽位的物品。
- `void SwapSlots(int32 A, int32 B)`
  - 输入：槽位 A、槽位 B。
  - 作用：直接交换两槽位内容。
- `void AutoSort()`
  - 作用：一键整理。先合并同类堆叠（上限 `BackpackMaxStack`），再按三级排序键排列（品类→RarityLevel 降序→名称升序），空格置尾。
- `int32 GetItemCount()`
  - 输出：物品种类数（非空槽数），不含 Quantity 聚合。
- `int32 GetUsedSlotCount()`
  - 输出：已占用的槽位数。
- `TArray<UMHGZItemInstance*> GetAllUsableItems()`
  - 输出：所有 `bIsUsable==true` 的物品实例列表。
  - 作用：供快捷栏 `RefreshFromBackpack` 自动登记。

Delegate：`FOnBackpackChanged` — 增/删/移/整理后广播。

## UMHGWarehouseComponent — 仓库组件

```
UCLASS(ClassGroup=(Inventory), BlueprintType)
class UMHGWarehouseComponent : public UActorComponent
```

挂载到 PlayerState，跨关卡保留。常量 `WAREHOUSE_MAX_STACK = 99999`。可混合存储 `ItemInstance`（消耗品/材料）和 `EquipmentInstance`（武器/衣服/饰品），通过多态统一管理。

### 实例生命周期

- 实例是 UObject，由 Slots 数组持有引用（TObjectPtr）
- 整个游戏会话期间持续存在，不是"打开仓库才加载"
- 装备/镶嵌时：对象本体仍在仓库 Slots 中，EquippedItems 中仅存储指向同一对象的指针
- 存档/读档时：实例随 PlayerState 序列化/反序列化（待存档系统实现，见 [pending.md](pending.md)）

### 仓库 UI——仅显示 InStorage 物品

仓库界面**只显示 Status==InStorage 的物品**。已装备和已镶嵌物品不出现在仓库列表中——由独立的装备状态界面管理。

| 成员 | 类型 | Category | 默认值 | 说明 |
|------|------|----------|--------|------|
| Slots | TArray\<FStorageSlot\> | "Warehouse\|State" | 空 | 槽位数组（动态增长） |

方法：

- `bool DepositItem(UMHGZItemInstance* Instance, int32 Count)`
  - 输入：物品实例、数量。
  - 输出：成功与否。
  - 作用：从背包存入仓库，按 `WAREHOUSE_MAX_STACK = 99999` 合并堆叠。若 `Instance→IsEmpty()` 则从背包移除该实例。
- `bool WithdrawItem(UMHGZItemInstance* Instance, int32 Count)`
  - 输入：物品实例、数量。
  - 输出：成功与否。
  - 作用：从仓库取出到背包，检查背包容量和堆叠限制。不满足则拒绝。
- `int32 DepositAll(TArray<UMHGZItemInstance*> Instances)`
  - 输入：物品实例列表。
  - 输出：成功存入的实例数量。
- `UMHGZItemInstance* GetItemAtSlot(int32 SlotIndex)`
  - 输入：槽位索引。
  - 输出：物品实例指针。
- `TArray<UMHGZItemInstance*> GetItemsByTag(FGameplayTag Tag)`
  - 输入：GameplayTag。
  - 输出：匹配标签的物品实例列表。
  - 作用：仓库分类页的核心查询方法。利用 UE 的 Tag 层级匹配（查 `Item.Type.Weapon` 自动包含 `.Sword/.Bow` 等子标签）。
- `TArray<UMHGZItemInstance*> GetItemsByTagQuery(FGameplayTagQuery Query)`
  - 输入：标签查询表达式。
  - 输出：匹配的物品实例列表。
  - 作用：支持 AND/OR 组合条件的高级筛选。
- `TArray<UMHGZItemInstance*> SearchByName(FString Keyword)`
  - 输入：关键词。
  - 输出：名称模糊匹配的物品实例列表。
- `void AutoSort(bool bGlobal = true)`
  - 输入：bGlobal=true 全仓库排序，false 仅当前分类页内排序。
  - 作用：堆叠合并（上限 99999）→ 三级排序（品类→RarityLevel 降序→名称升序）→ 空格置尾。**排序范围包括所有 Slots 中的物品（含 Status≠InStorage 的已装备/已镶嵌物品）**——保持仓库内部存储布局一致，UI 层按 Status 过滤显示。

Delegate：`FOnWarehouseChanged` — 增/删/移/整理后广播。

## 装备状态界面（独立于仓库）

**数据源：**
| 信息 | 数据来源 |
|------|---------|
| 当前已装备的武器/衣服 | `EquipmentComponent::EquippedItems` |
| 装备上已镶嵌的饰品 | `EquipmentInstance::SocketedAccessories` |
| 仓库中可用的替换装备 | `WarehouseComponent::Slots`（过滤 Status==InStorage + IsEquipment()） |
| 装备的词条列表 | `EquipmentDefinition::Entries` + `EquipmentInstance::Customization` |

> **指针语义：** EquippedItems 和 SocketedAccessories 中的 TObjectPtr 是指向同一 UObject 的指针。对象本体始终在 WarehouseComponent::Slots 中。装备/镶嵌操作只是复制指针，不移动、不复制对象。
