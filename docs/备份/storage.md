# 存储系统

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

挂载到 PlayerState，跨关卡保留。

| 成员 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| Slots | TArray\<FStorageSlot\> | 预分配 30 空槽 | 槽位数组 |

方法：`AddItem`（返回 int32 支持部分成功）、`AddItemInstance`、`RemoveItem`、`RemoveItemAtSlot`、`GetItemAtSlot`、`FindFirstEmptySlot`、`FindSlotForItem`、`GetAvailableSpaceFor`、`CanAddItem`、`MoveItem`、`SwapSlots`、`AutoSort`、`GetItemCount`、`GetUsedSlotCount`、`GetAllUsableItems`

Delegate：`FOnBackpackChanged` — 增/删/移/整理后广播。

## UMHGWarehouseComponent — 仓库组件

挂载到 PlayerState，跨关卡保留。常量 `WAREHOUSE_MAX_STACK = 99999`。

### 实例生命周期

- 实例是 UObject，由 Slots 数组持有引用（TObjectPtr）
- 整个游戏会话期间持续存在，不是"打开仓库才加载"
- 装备/镶嵌时：对象本体仍在仓库 Slots 中，EquippedItems 中仅存储指向同一对象的指针
- 存档/读档时：实例随 PlayerState 序列化/反序列化（待存档系统实现，见 [pending.md](pending.md)）

### 仓库 UI——仅显示 InStorage 物品

仓库界面**只显示 Status==InStorage 的物品**。已装备和已镶嵌物品不出现在仓库列表中——由独立的装备状态界面管理。

| 成员 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| Slots | TArray\<FStorageSlot\> | 空 | 槽位数组（动态增长） |

方法：`DepositItem`、`WithdrawItem`、`DepositAll`、`GetItemAtSlot`、`GetItemsByTag`、`GetItemsByTagQuery`、`SearchByName`、`AutoSort`

Delegate：`FOnWarehouseChanged`。

## 装备状态界面（独立于仓库）

**数据源：**
| 信息 | 数据来源 |
|------|---------|
| 当前已装备的武器/衣服 | `EquipmentComponent::EquippedItems` |
| 装备上已镶嵌的饰品 | `EquipmentInstance::SocketedAccessories` |
| 仓库中可用的替换装备 | `WarehouseComponent::Slots`（过滤 Status==InStorage + IsEquipment()） |
| 装备的词条列表 | `EquipmentDefinition::Entries` + `EquipmentInstance::Customization` |

> **指针语义：** EquippedItems 和 SocketedAccessories 中的 TObjectPtr 是指向同一 UObject 的指针。对象本体始终在 WarehouseComponent::Slots 中。装备/镶嵌操作只是复制指针，不移动、不复制对象。
