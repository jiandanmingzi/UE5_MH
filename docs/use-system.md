# 使用系统

> **实施状态说明（以源码为准）：** 当前只有挂载到 PlayerController 的空 `UMHGZQuickBarComponent` 桩组件。`EQuickBarSlotType`、`FQuickBarSlot`、`UMHGZUseAction`、`UMHGZSpecialAction`、自动登记、分级阻塞和收刀后使用流程均为下文保留方案，尚未实现。

**设计原则：** 与物品系统完全解耦。快捷栏自动登记背包中所有 `bIsUsable` 物品 + 少量手动分配的特殊动作。交互模式：切换键（滚轮/Q/E）循环选中 → 触发键（鼠标左键/F）执行当前选中项。UseAction 和 SpecialAction 均通过 GAS Ability 触发。常规动作（攻击/闪避）由 GAS 直接绑定输入，不经过快捷栏。

## EQuickBarSlotType

| 值 | 说明 |
|----|------|
| Empty | 空槽 |
| Item | 可使用物品 |
| SpecialAction | 非常规动作 |

## FQuickBarSlot

```
USTRUCT(BlueprintType)
```

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| SlotType | EQuickBarSlotType | Empty | 槽位类型 |
| ItemInstance | TObjectPtr\<UMHGZItemInstance\> | nullptr | 物品引用（SlotType==Item 时有效） |
| SpecialActionDef | TObjectPtr\<UMHGZSpecialAction\> | nullptr | 动作引用（SlotType==SpecialAction 时有效） |
| CachedIcon | TSoftObjectPtr\<UTexture2D\> | nullptr | 缓存图标 |
| CachedName | FText | 空 | 缓存名称 |

方法：`bool IsEmpty() const`、`void Clear()`、`UTexture2D* GetDisplayIcon()`、`FText GetDisplayName()`、`int32 GetQuantity()`（仅 Item 类型有效，其他返回 -1）。

## UMHGZUseAction

```
UCLASS(BlueprintType, Blueprintable, Abstract, EditInlineNew)
class UMHGZUseAction : public UPrimaryDataAsset
```

| 成员 | 类型 | Category | 默认值 | 说明 |
|------|------|----------|--------|------|
| ActionName | FText | "Action" | 空 | 行为名称 |
| ActionIcon | TSoftObjectPtr\<UTexture2D\> | "Action" | nullptr | 图标 |
| CooldownOverride | float | "Action" | -1 | 冷却覆盖（-1=使用物品定义值） |
| AbilityClass | TSubclassOf\<UGameplayAbility\> | "Action\|GAS" | nullptr | GAS Ability 类 |

方法：

- `bool Execute(AActor* User, UMHGZItemInstance* SourceItem)`
  - 输入：使用者 Actor、来源物品实例。
  - 输出：成功与否。
  - 作用：通过 `User` 的 `ASC→TryActivateAbility(AbilityClass)` 触发 GAS Ability。
- `bool CanExecute(AActor* User, UMHGZItemInstance* SourceItem) const`
  - 输入：使用者、来源物品实例。
  - 输出：是否满足执行条件。
  - 作用：检查冷却时间等前置条件。

预置子类：`UUseAction_Heal`（HealAmount, bPercentHeal）、`UUseAction_ThrowProjectile`（ProjectileClass, ThrowSpeed）、`UUseAction_ApplyBuff`（BuffGE, Duration）、`UUseAction_PlaceTrap`（TrapClass）。

## UMHGZSpecialAction

```
UCLASS(BlueprintType, Blueprintable, Abstract, EditInlineNew)
class UMHGZSpecialAction : public UPrimaryDataAsset
```

不属于常规战斗/移动系统、需玩家主动触发的动作（探测、钩爪、拍照、演奏等）。

| 成员 | 类型 | Category | 默认值 | 说明 |
|------|------|----------|--------|------|
| ActionName | FText | "Action" | 空 | 动作名称 |
| ActionIcon | TSoftObjectPtr\<UTexture2D\> | "Action" | nullptr | 图标 |
| AbilityClass | TSubclassOf\<UGameplayAbility\> | "Action\|GAS" | nullptr | GAS Ability 类 |

方法：

- `bool Execute(AActor* User)`
  - 输入：使用者 Actor。
  - 输出：成功与否。
  - 作用：通过 `ASC→TryActivateAbility(AbilityClass)` 触发 GAS Ability。
- `bool CanExecute(AActor* User) const`
  - 输入：使用者 Actor。
  - 输出：是否满足执行条件。

预置子类：`USpecialAction_Scan`、`USpecialAction_GrapplingHook`、`USpecialAction_PhotoMode`、`USpecialAction_PlayInstrument`。

## UMHGZQuickBarComponent

```
UCLASS(ClassGroup=(UseSystem), BlueprintType)
class UMHGZQuickBarComponent : public UActorComponent
```

挂载到 **PlayerController**。输入/反馈层组件——接收滚轮/Q/E/使用键输入，播音效，触发 GAS Ability。

### 音效规则（始终生效，不依赖执行结果）

| 操作 | 音效触发时机 | 说明 |
|------|------------|------|
| 切换选中项 | 立即触发 | 无论新选中项是否为空 |
| 尝试使用 | 立即触发 | **在能力检查之前**播放——玩家获得"按键已被接收"的听觉反馈 |

### 使用选中项——分级阻塞规则

**两种使用路径：**

| 路径 | 触发方式 | 持刀态处理 |
|------|----------|------------|
| **快捷栏直接使用** | 收刀态按使用键 | 立即执行（不缓存、不等收刀） |
| **轮盘选中使用** | 持刀态打开轮盘 → 选中道具 → 关闭轮盘 | 轮盘关闭时存储选中项为 `PendingUseItem` → 触发 `RequestSheatheAndUse` → 收刀完成自动执行 |

```
快捷栏直接使用（收刀态）：
  ┌─ 步骤0：播音效（始终执行，无条件）★ 按键已被接收的反馈
  ├─ 步骤1：检查槽位是否为空 → 空则 return
  ├─ 步骤2：检查不可打断状态（Hitstun/Knockdown/Dead/使用动画中）→ 阻塞
  ├─ 步骤3：检查持刀态 → 是则 return（持刀态不用快捷栏直接使用——走轮盘路径）
  └─ 步骤4：执行使用

轮盘选中使用（持刀态）：
  ┌─ 步骤0：玩家打开轮盘（Radial Menu）→ 浏览可选道具
  ├─ 步骤1：选中道具 → 关闭轮盘 = 确认意图
  │   └─ QuickBarComponent 将选中项存入 PendingUseItem（快照关闭轮盘时的选中项）
  ├─ 步骤2：HandleGameplayEvent(RequestSheatheAndUse)
  │   （GA_Sheathe 监听此事件 → 收刀 → EndAbility 广播 SheatheComplete）
  ├─ 步骤3：QuickBarComponent 订阅 SheatheComplete → 读 PendingUseItem → 执行使用
  └─ 步骤4：清除 PendingUseItem
```

> **设计理由：** 快捷栏不缓存"待执行使用"——持刀态下按使用键仅播音效反馈，不排队。使用意图统一通过轮盘表达：轮盘选中即意图确认，关闭轮盘自动收刀+执行。`PendingUseItem` 快照关闭轮盘时的选中项，后续切换快捷栏不影响。解耦方式不变：GA_Sheathe 仍通过 GameplayEvent 与 QuickBarComponent 通信，不持有直接引用。

### 成员与方法

| 成员 | 类型 | Category | 默认值 | 说明 |
|------|------|----------|--------|------|
| MaxSlotCount | int32 | "QuickBar\|Config" | 8 | 最大槽位数 |
| Slots | TArray\<FQuickBarSlot\> | "QuickBar\|State" | 预分配 8 空槽 | 槽位数组 |
| SelectedIndex | int32 | "QuickBar\|State" | 0 | 当前选中槽位索引 |
| PendingUseItem | FQuickBarSlot | "QuickBar\|State" | Empty | 轮盘选中的待执行项（持刀态→轮盘选中→收刀完成后执行）。仅在轮盘路径使用，快捷栏直接使用不涉及 |

方法：

- `void RefreshFromBackpack(UMHGZBackpackComponent* Backpack)`
  - 输入：背包组件。
  - 作用：清空快捷栏中 `SlotType==Item` 的槽位，遍历背包 `GetAllUsableItems()` 重新填入。`SlotType==SpecialAction` 的槽位（固定槽，如末位）不受影响——特殊动作由玩家手动分配，不随背包变化。
- `void SelectNext()`
  - 作用：选中下一个非空槽位，到末尾循环回开头。
- `void SelectPrevious()`
  - 作用：选中上一个非空槽位，到开头循环回末尾。
- `void SelectSlot(int32 Index)`
  - 输入：槽位索引。
  - 作用：直接选中指定槽位。
- `bool UseSelected()`
  - 输出：成功与否。
  - 作用：触发当前选中槽位。
  - 设计思路：
    - SlotType==Item → 检查 `Quantity>0` → 实例化 `UseAction` → `Execute` → 若 `bConsumeOnUse` 则 `ItemInstance→RemoveQuantity(1)` → 若 `ItemInstance→IsEmpty()` 则清空该槽位并从背包移除该实例。**不调用 `RefreshFromBackpack`**——单槽增量更新，避免因一次使用重建整个快捷栏。仅当背包物品增删（拾取/丢弃）时才调用 `RefreshFromBackpack` 全量刷新。
    - SlotType==SpecialAction → 实例化 `SpecialAction` → `Execute`。
- `bool AssignSpecialAction(int32 SlotIndex, UMHGZSpecialAction* Action)`
  - 输入：槽位索引、特殊动作定义。
  - 输出：成功与否。
  - 作用：手动将特殊动作分配到指定槽位。
- `void RemoveSpecialAction(int32 SlotIndex)`
  - 输入：槽位索引。
  - 作用：移除指定槽位的特殊动作。
- `FQuickBarSlot GetSelectedSlot()`
  - 输出：当前选中槽位的完整信息。

输入绑定：`IA_QuickBar_Next`→SelectNext、`IA_QuickBar_Prev`→SelectPrevious、`IA_QuickBar_Use`→UseSelected。

Delegate：`FOnQuickBarChanged(int32 SelectedIndex)` — 选中项变更后广播；`FOnSlotUsed(int32 SlotIndex, bool bSuccess)` — 使用完成后广播。
