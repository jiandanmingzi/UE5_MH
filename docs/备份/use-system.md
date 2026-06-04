# 使用系统

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
| ItemInstance | TObjectPtr\<UMHGZItemInstance\> | nullptr | 物品引用 |
| SpecialActionDef | TObjectPtr\<UMHGZSpecialAction\> | nullptr | 动作引用 |
| CachedIcon | TSoftObjectPtr\<UTexture2D\> | nullptr | 缓存图标 |
| CachedName | FText | 空 | 缓存名称 |

## UMHGZUseAction

```
UCLASS(BlueprintType, Blueprintable, Abstract, EditInlineNew)
class UMHGZUseAction : public UPrimaryDataAsset
```

| 成员 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| ActionName | FText | 空 | 行为名称 |
| ActionIcon | TSoftObjectPtr\<UTexture2D\> | nullptr | 图标 |
| CooldownOverride | float | -1 | 冷却覆盖（-1=使用物品定义值） |
| AbilityClass | TSubclassOf\<UGameplayAbility\> | nullptr | GAS Ability 类 |

方法：`Execute(AActor* User, UMHGZItemInstance* SourceItem)`、`CanExecute(...)`

预置子类：`UUseAction_Heal`、`UUseAction_ThrowProjectile`、`UUseAction_ApplyBuff`、`UUseAction_PlaceTrap`

## UMHGZSpecialAction

```
UCLASS(BlueprintType, Blueprintable, Abstract, EditInlineNew)
class UMHGZSpecialAction : public UPrimaryDataAsset
```

| 成员 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| ActionName | FText | 空 | 动作名称 |
| ActionIcon | TSoftObjectPtr\<UTexture2D\> | nullptr | 图标 |
| AbilityClass | TSubclassOf\<UGameplayAbility\> | nullptr | GAS Ability 类 |

预置子类：`USpecialAction_Scan`、`USpecialAction_GrapplingHook`、`USpecialAction_PhotoMode`、`USpecialAction_PlayInstrument`

## UMHGZQuickBarComponent

挂载到 **PlayerController**。输入/反馈层组件——接收滚轮/Q/E/使用键输入，播音效，触发 GAS Ability。

### 音效规则（始终生效，不依赖执行结果）

| 操作 | 音效触发时机 | 说明 |
|------|------------|------|
| 切换选中项 | 立即触发 | 无论新选中项是否为空 |
| 尝试使用 | 立即触发 | **在能力检查之前**播放——玩家获得"按键已被接收"的听觉反馈 |

### 使用选中项——分级阻塞规则

```
UseSelected() 执行流程：
  ┌─ 步骤0：播音效（始终执行，无条件）
  ├─ 步骤1：检查槽位是否为空 → 空则 return
  ├─ 步骤2：检查不可打断状态（Hitstun/Knockdown/Dead/使用动画中）→ 阻塞
  ├─ 步骤3：检查持刀态
  │   └─ 是 → 存储"待执行使用"标记 + HandleGameplayEvent(RequestSheatheAndUse)
  │        （GA_Sheathe 监听此事件 → 收刀 → EndAbility 广播 SheatheComplete）
  │        （QuickBarComponent 订阅 SheatheComplete → 检查标记 → 执行步骤4）
  └─ 步骤4：执行使用
```

> **解耦方式——GameplayEvent 替代直接回调：** GA_Sheathe 不直接调用 QuickBarComponent::UseSelected。QuickBarComponent 通过 `HandleGameplayEvent(RequestSheatheAndUse)` 发出请求，GA_Sheathe 通过 `AbilityTrigger` 监听此事件执行收刀。收刀完成后 GA_Sheathe 广播 `HandleGameplayEvent(SheatheComplete)`——QuickBarComponent 订阅此事件，收到后检查标记→执行使用。GA_Sheathe 不持有 QuickBarComponent 引用。

### 成员与方法

| 成员 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| MaxSlotCount | int32 | 8 | 最大槽位数 |
| Slots | TArray\<FQuickBarSlot\> | 预分配 8 空槽 | 槽位数组 |
| SelectedIndex | int32 | 0 | 当前选中槽位索引 |

方法：`RefreshFromBackpack`、`SelectNext/Previous/Slot`、`UseSelected`、`AssignSpecialAction`、`RemoveSpecialAction`

输入绑定：`IA_QuickBar_Next`→SelectNext、`IA_QuickBar_Prev`→SelectPrevious、`IA_QuickBar_Use`→UseSelected
