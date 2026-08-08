# UI 系统

**设计原则：** UI 由 GameplayTag/Attribute/Delegate 驱动，GAS Ability 不直接操作 UI。数据层（GAS / ResourceComponent / AimComponent）提供数据，表现层（UMG Widget）订阅变化。武器资源 UI 通过 `UMHGZUISubsystem`（GameInstanceSubsystem）工厂模式按武器种类动态创建/销毁。准心系统由 `UMHGZAimComponent`（Character 端）提供瞄准目标数据。

> **当前阶段：** 主 HUD 容器 + 血条 + 耐力条 + 准心 + 武器资源插槽。小地图等后续扩展。所有 Widget 蓝图由策划创建，C++ 只提供数据绑定接口。

---

## 架构总览

```
数据层（不依赖 UI）
  ├── GAS ASC Tag 变化
  │     → UI 订阅 RegisterGameplayTagEvent
  ├── GAS Attribute 变化
  │     → UI 订阅 GetGameplayAttributeValueChangeDelegate
  ├── WeaponResourceComponent Delegate（各武器自行定义）
  │     → UI 直接绑定
  └── UMHGZAimComponent Delegate（瞄准检测——Character 端）
        → UI 直接绑定
        │
        ▼
管理层
  ├── AMHGZHUD（主 HUD Actor——Widget 创建/销毁/容器管理）
  └── UMHGZUISubsystem（GameInstanceSubsystem——武器资源 Widget 工厂）
        │
        ▼
表现层（UMG Widget——仅读数据，不写数据）
  ├── WBP_HUD（主面板——血条/耐力条/武器资源插槽/准心容器/小地图）
  ├── WBP_Crosshair（准心——订阅 OnAimTargetChanged）
  └── WBP_{Weapon}ResourcePanel（武器资源——按 WeaponTypeTag 动态创建）
```

---

## 一、AMHGZHUD — 主 HUD

```
UCLASS()
class AMHGZHUD : public AHUD
```

管理全局 Widget 的创建/销毁。持有主面板和准心 Widget 实例。武器切换时委托 `UMHGZUISubsystem` 重建资源 Widget。

| 成员 | 类型 | Category | 默认值 | 说明 |
|------|------|----------|--------|------|
| MainHUDClass | TSubclassOf\<UUserWidget\> | "HUD\|Config" | nullptr | 主 HUD Widget 类（WBP_HUD） |
| HealthBarClass | TSubclassOf\<UMHGZUserWidget\> | "HUD\|Config" | nullptr | 血条 Widget 类（WBP_HealthBar） |
| StaminaBarClass | TSubclassOf\<UMHGZUserWidget\> | "HUD\|Config" | nullptr | 耐力条 Widget 类（WBP_StaminaBar） |
| CrosshairClass | TSubclassOf\<UMHGZCrosshairWidget\> | "HUD\|Config" | nullptr | 准心 Widget 类（WBP_Crosshair） |
| MainHUDWidget | TObjectPtr\<UUserWidget\> | "HUD\|State" | nullptr | 主 HUD Widget 实例 |
| HealthBarWidget | TObjectPtr\<UMHGZUserWidget\> | "HUD\|State" | nullptr | 血条 Widget 实例 |
| StaminaBarWidget | TObjectPtr\<UMHGZUserWidget\> | "HUD\|State" | nullptr | 耐力条 Widget 实例 |
| CrosshairWidget | TObjectPtr\<UMHGZCrosshairWidget\> | "HUD\|State" | nullptr | 准心 Widget 实例 |

### 关键方法

- `void BeginPlay() override`
  - 作用：`CreateWidget(MainHUDClass)` → `AddToViewport` → 依次创建 `HealthBarWidget` / `StaminaBarWidget` / `CrosshairWidget` → 放入 MainHUDWidget 对应容器（`Canvas_HealthBar` / `Canvas_StaminaBar` / `Canvas_Crosshair`）→ 绑定血条/耐力条到 ASC Attribute 变化委托 → 绑定 `UMHGZAimComponent::OnAimTargetChanged` → 转发给 CrosshairWidget。

- `void OnWeaponChanged(FGameplayTag WeaponTypeTag, UMHGZWeaponResourceComponent* ResourceComp)`
  - 输入：新武器种类 Tag、新武器的 ResourceComponent 指针（可为 nullptr——卸下武器时）。
  - 作用：调用 `UMHGZUISubsystem::SwapResourceWidget(WeaponTypeTag, ResourceComp)` → 工厂方法销毁旧 Widget、创建新 Widget、放入 MainHUDWidget 的武器资源插槽。

- `void OnHealthChanged(const FOnAttributeChangeData& Data)`
  - 作用：GAS Health Attribute 变化回调 → 读取当前 MaxHealth → 调用 `HealthBarWidget->OnValueUpdated(Data.NewValue, MaxHealth)`。

- `void OnMaxHealthChanged(const FOnAttributeChangeData& Data)`
  - 作用：GAS MaxHealth Attribute 变化回调 → 读取当前 Health → 调用 `HealthBarWidget->OnValueUpdated(CurrentHealth, Data.NewValue)`。**MaxHealth 变化时 Health 可能不变——需单独订阅 Max 确保 UI 正确显示比例。**

- `void OnStaminaChanged(const FOnAttributeChangeData& Data)`
  - 作用：GAS Stamina Attribute 变化回调 → 读取当前 MaxStamina → 调用 `StaminaBarWidget->OnValueUpdated(Data.NewValue, MaxStamina)`。

- `void OnMaxStaminaChanged(const FOnAttributeChangeData& Data)`
  - 作用：GAS MaxStamina Attribute 变化回调 → 读取当前 Stamina → 调用 `StaminaBarWidget->OnValueUpdated(CurrentStamina, Data.NewValue)`。

- `void OnAimTargetChanged(AActor* Target, FGameplayTag HitzoneTag, FGameplayTag ExtractColor)`
  - 作用：转发给 CrosshairWidget 的 `OnAimTargetUpdated`。

> **BeginPlay 关键代码：**
> ```cpp
> void AMHGZHUD::BeginPlay()
> {
>     Super::BeginPlay();
>     MainHUDWidget = CreateWidget(MainHUDClass)->AddToViewport();
>
>     UMHGZAbilitySystemComponent* ASC = GetASC();
>
>     // ── 血条 ──
>     HealthBarWidget = CreateWidget<UMHGZUserWidget>(HealthBarClass);
>     MainHUDWidget->HealthBarSlot->AddChild(HealthBarWidget);
>     // Health 当前值变化（扣血/回血）
>     ASC->GetGameplayAttributeValueChangeDelegate(UMHGZAttributeSet::GetHealthAttribute())
>         .AddUObject(this, &AMHGZHUD::OnHealthChanged);
>     // MaxHealth 变化（装备/Buff 改变上限）——必须单独订阅
>     ASC->GetGameplayAttributeValueChangeDelegate(UMHGZAttributeSet::GetMaxHealthAttribute())
>         .AddUObject(this, &AMHGZHUD::OnMaxHealthChanged);
>
>     // ── 耐力条 ──
>     StaminaBarWidget = CreateWidget<UMHGZUserWidget>(StaminaBarClass);
>     MainHUDWidget->StaminaBarSlot->AddChild(StaminaBarWidget);
>     ASC->GetGameplayAttributeValueChangeDelegate(UMHGZAttributeSet::GetStaminaAttribute())
>         .AddUObject(this, &AMHGZHUD::OnStaminaChanged);
>     ASC->GetGameplayAttributeValueChangeDelegate(UMHGZAttributeSet::GetMaxStaminaAttribute())
>         .AddUObject(this, &AMHGZHUD::OnMaxStaminaChanged);
>
>     // ── 准心 ──
>     CrosshairWidget = CreateWidget<UMHGZCrosshairWidget>(CrosshairClass);
>     MainHUDWidget->CrosshairSlot->AddChild(CrosshairWidget);
>     Character->GetAimComponent()->OnAimTargetChanged.AddDynamic(this, &AMHGZHUD::OnAimTargetChanged);
> }
>
> void AMHGZHUD::OnHealthChanged(const FOnAttributeChangeData& Data)
> {
>     float Max = ASC->GetNumericAttribute(UMHGZAttributeSet::GetMaxHealthAttribute());
>     HealthBarWidget->OnValueUpdated(Data.NewValue, Max);
> }
>
> void AMHGZHUD::OnMaxHealthChanged(const FOnAttributeChangeData& Data)
> {
>     float Current = ASC->GetNumericAttribute(UMHGZAttributeSet::GetHealthAttribute());
>     HealthBarWidget->OnValueUpdated(Current, Data.NewValue);
> }
> ```

---

## 二、WBP_HealthBar / WBP_StaminaBar — 血条与耐力条

两个 Widget 设计相同（ProgressBar + 数值文本 + 颜色渐变），仅数据源不同。均继承 `UMHGZUserWidget`。

### WBP_HealthBar

| 子控件 | 类型 | 说明 |
|------|------|------|
| ProgressBar_Health | UProgressBar | 填充百分比 = Current / Max |
| Text_HealthValue | UTextBlock | 显示 "85 / 100" |

**蓝图接口（C++ 调用）：**
- `OnValueUpdated(float Current, float Max)`——BlueprintImplementableEvent
  - Current/Max > 0.6 → 绿色
  - 0.3~0.6 → 黄色
  - < 0.3 → 红色 + 闪烁
  - == 0 → 红色常亮 + "死亡" 文字

**数据源：** GAS `Health` Attribute → `GetGameplayAttributeValueChangeDelegate`

### WBP_StaminaBar

| 子控件 | 类型 | 说明 |
|------|------|------|
| ProgressBar_Stamina | UProgressBar | 填充百分比 = Current / Max |
| Text_StaminaValue | UTextBlock | 显示 "85 / 100" |

**蓝图接口：** 同 `OnValueUpdated`。

**数据源：** GAS `Stamina` Attribute → `GetGameplayAttributeValueChangeDelegate`

> **为何不用 GAS AttributeSet 直接驱动 Widget：** 遵循 UI 只读原则——HUD 是数据→Widget 的中介。Attribute 变化→HUD 回调→调用 Widget 的 `OnValueUpdated`。Widget 本身不持有 ASC 引用，不订阅 Attribute——零 GAS 耦合，纯蓝图换皮只需改 Widget 视觉，不碰数据管道。

---

## 三、UMHGZUISubsystem — 武器资源 Widget 工厂

```
UCLASS()
class UMHGZUISubsystem : public UGameInstanceSubsystem
```

全局单例。持有当前活跃的武器资源 Widget 引用，提供工厂方法按 WeaponTypeTag 创建/销毁/切换资源 Widget。

| 成员 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| ActiveResourceWidget | TObjectPtr\<UUserWidget\> | nullptr | 当前活跃的武器资源 Widget |
| ActiveWeaponTypeTag | FGameplayTag | 空 | 当前武器种类 Tag |

### 关键方法

- `void SwapResourceWidget(FGameplayTag WeaponTypeTag, UMHGZWeaponResourceComponent* ResourceComp)`
  - 输入：新武器种类 Tag、新武器的 ResourceComponent 指针（可为 nullptr）。
  - 作用：
    1. 若 `ActiveResourceWidget` 存在 → `RemoveFromParent` → 解绑旧事件 → 置空
    2. 若 `ResourceComp != nullptr` → 查 `DT_WeaponResourceConfig` 获取 `ResourceWidgetClass` → `CreateWidget` → 注入 `ResourceComp` 引用 → `AddToViewport`（放入主 HUD 的武器资源插槽）→ 绑定 Delegate/Tag 事件
    3. 更新 `ActiveWeaponTypeTag`

- `void BindResourceWidget(UUserWidget* Widget, UMHGZWeaponResourceComponent* ResourceComp)`
  - 作用：虚函数——各武器子类覆写，将 Widget 的数据源绑定到 ResourceComponent 的具体 Delegate 和 ASC Tag 事件。基类默认实现为空。

> **绑定时机：** 在 `SwapResourceWidget` 的 CreateWidget 之后、AddToViewport 之前调用。确保 Widget 添加到视口时数据已就绪。

---

## 四、UI Widget C++ 基类

**设计原则：** 与 GA/Ability 继承层级一致——C++ 基类封装通用逻辑（ASC 缓存、Tag 订阅管理、Delegate 绑定），蓝图子类只配置视觉表现。基类不画 UI（不改 UMG 布局）——所有 `BindWidget` 的子控件由蓝图创建。

### 继承层级

```
UUserWidget（UE 原生）
  └── UMHGZUserWidget（项目基类——ASC 缓存 + Tag 订阅统一管理）
        ├── UMHGZWeaponResourceWidget（武器资源基类——Bind/Unbind 虚函数）
        │     ├── WBP_IG_ResourcePanel ← 虫棍资源面板（蓝图子类）
        │     └── WBP_LS_SpiritGauge  ← 太刀气刃槽（蓝图子类，后续）
        └── UMHGZCrosshairWidget（准心基类——OnAimTargetUpdated 处理）
              └── WBP_Crosshair ← 通用准心（蓝图子类）
```

> **WBP_HUD** 不需要 C++ 基类——主 HUD 的职责是容器布局（Canvas Panel 层级）+ 委托 `UMHGZUISubsystem` 做 Widget 生命周期，纯蓝图实现即可。

### UMHGZUserWidget — 项目基类

```
UCLASS(Abstract, Blueprintable)
class UMHGZUserWidget : public UUserWidget
```

所有 Widget 的基类。提供 ASC 缓存、Tag 订阅统一管理。不画 UI——不持有任何 `BindWidget` 子控件。

| 成员 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| CachedASC | TObjectPtr\<UMHGZAbilitySystemComponent\> | nullptr | ASC 缓存——`NativeConstruct` 时从 Owner PlayerState 获取 |

| 方法 | 说明 |
|------|------|
| `void ListenForGameplayTag(FGameplayTag Tag, TFunction<void(const FGameplayTag, int32)> Callback)` | 订阅 ASC Tag 变化——自动缓存 Handle。`NativeDestruct` 时自动批量取消，无需子类手动管理 |
| `virtual void NativeConstruct() override` | 获取 ASC → 调用 `BP_OnInitialized`（BlueprintImplementableEvent——蓝图子类在此绑定数据源） |
| `virtual void NativeDestruct() override` | 遍历 TagEventHandles → 逐个 `UnregisterGameplayTagEvent` → 调用 `BP_OnTeardown` |

> **设计理由：** 蓝图 `RegisterGameplayTagEvent` 返回的 DelegateHandle 需要手动缓存并在析构时取消——纯蓝图经常遗漏导致悬空回调。基类封装后子类只需 `ListenForGameplayTag(Tag, [this](auto Tag, int32 Count) { ... })`——一行调用，零泄漏风险。

### UMHGZWeaponResourceWidget — 武器资源基类

```
UCLASS(Abstract, Blueprintable)
class UMHGZWeaponResourceWidget : public UMHGZUserWidget
```

所有武器资源 Widget 的基类。提供 Bind/Unbind 虚函数——`UMHGZUISubsystem::SwapResourceWidget` 中一行多态调用完成数据源切换。

| 成员 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| ResourceComponent | TObjectPtr\<UMHGZWeaponResourceComponent\> | nullptr | 当前绑定到的武器 ResourceComponent |

| 方法 | 说明 |
|------|------|
| `virtual void BindToResourceComponent(UMHGZWeaponResourceComponent* InResourceComp)` | 子类覆写——绑定 WeaponTypeTag 特定的 Delegate（如 `OnKinsectStaminaChanged`）+ 订阅 ASC Tag。基类默认实现：设置 ResourceComponent 引用 |
| `virtual void UnbindFromResourceComponent()` | 子类覆写——解绑所有 Delegate。基类默认实现：置空 ResourceComponent |

> **子类覆写示例（虫棍）：**
> ```cpp
> void WBP_IG_ResourcePanel::BindToResourceComponent(UMHGZWeaponResourceComponent* InRC)
> {
>     Super::BindToResourceComponent(InRC);
>     URes_InsectGlaive* IG = Cast<URes_InsectGlaive>(InRC);
>     IG->OnKinsectStaminaChanged.AddDynamic(this, &OnStaminaChanged);
>     ListenForGameplayTag(WeaponResource.IG.Extract.Red, [this](auto, int32 C) { OnExtractChanged(Red, C); });
> }
> ```

### UMHGZCrosshairWidget — 准心基类

```
UCLASS(Abstract, Blueprintable)
class UMHGZCrosshairWidget : public UMHGZUserWidget
```

准心 Widget 的基类。提供 `OnAimTargetUpdated` 蓝图事件——`AMHGZHUD` 直接调用，不需要在 HUD 蓝图中拖 Cast 节点。

| 方法 | 说明 |
|------|------|
| `void OnAimTargetUpdated(AActor* Target, FGameplayTag HitzoneTag, FGameplayTag ExtractColor)` | BlueprintImplementableEvent——蓝图子类覆写。根据 Target/HitzoneTag/ExtractColor 切换准心样式（颜色/形状/缩放动画） |

> **调用链：** `AMHGZHUD::OnAimTargetChanged(...)` → `CrosshairWidget->OnAimTargetUpdated(...)`——类型安全的直接调用，HUD 不需要知道准心内部如何实现。

---

## 五、UMHGZAimComponent — 瞄准检测组件

> 虫棍扩展（萃取颜色预览）详见 [insect-glaive.md §十二](insect-glaive.md#十二瞄准与-ui-集成)。本节仅列出跨武器通用部分。

挂载到 **Character**。负责瞄准射线检测、准心目标识别。虫棍复用此组件做萃取颜色预览，其他武器（弓箭/弩/钩爪）可复用做弱点高亮。

| 成员 | 类型 | Category | 默认值 | 说明 |
|------|------|----------|--------|------|
| bIsAiming | bool | "Aim\|State" | false | 当前是否瞄准中（订阅 `Combat.State.Aiming` Tag） |
| CurrentAimTarget | TWeakObjectPtr\<AActor\> | "Aim\|State" | nullptr | 准心当前指向的 Actor |
| CurrentAimHitzoneTag | FGameplayTag | "Aim\|State" | 空 | 准心指向的怪物部位 Tag |
| AimMaxDistance | float | "Aim\|Config" | 3000 | 瞄准射线最大距离（cm） |
| AimChannel | TEnumAsByte\<ECollisionChannel\> | "Aim\|Config" | GameTraceChannel1 | 碰撞通道（Weapon——检测怪物 HitzoneComponent） |
| AimInputAction | TObjectPtr\<UInputAction\> | "Aim\|Config" | nullptr | `IA_LT` 资产——`BeginPlay` 时直接绑定 EnhancedInput 的 Triggered/Completed |

| Delegate | 签名 | 说明 |
|------|------|------|
| OnAimTargetChanged | `(AActor* Target, FGameplayTag HitzoneTag)` | 准心指向变化时广播。Target 为 nullptr 表示瞄空/场景 |

### 关键方法

- `void BeginPlay() override`
  - 作用：获取 ASC → 订阅 `RegisterGameplayTagEvent(Combat.State.Aiming)` → `OnAimingTagChanged`。直接向 EnhancedInput Subsystem 绑定 `AimInputAction` 的 Triggered/Completed——按下 LT → `ASC->AddLooseGameplayTag(Combat.State.Aiming)`；松开 → `RemoveLooseGameplayTag`。不走 GAS 分叉路由（瞄准是输入状态，非 Ability）。

- `void TickComponent(float DeltaTime) override`
  - 作用：若 `bIsAiming` → `LineTraceSingleByChannel(AimChannel)` → 命中 `UMonsterHitzoneComponent` → 读 HitzoneTag → 若与上一帧不同 → 广播 `OnAimTargetChanged(Monster, HitzoneTag)`。命中其他 → 广播 `OnAimTargetChanged(nullptr, 空)`。**仅在目标变化时广播**——避免每帧重复触发 UI。

- `void OnAimingTagChanged(const FGameplayTag Tag, int32 NewCount)`
  - 作用：BeginPlay 时订阅 ASC 的 `RegisterGameplayTagEvent(Combat.State.Aiming)`。NewCount>0 → `bIsAiming=true`；NewCount==0 → `bIsAiming=false` → 广播 `OnAimTargetChanged(nullptr)`。

### 虫棍扩展

虫棍在基类上扩展了 `CurrentAimExtractColor` 字段和 `OnAimTargetChanged` 的 Delegate 签名增加 `ExtractColor` 参数。若其他武器不需要萃取预览，使用基类版本的 Delegate（不含 ExtractColor）即可。**建议用虚函数 `GetAimTargetData` 让子类覆写返回扩展数据**——避免不同武器用不同 Delegate 签名导致 HUD 需要分支处理。

---

## 六、Widget 数据绑定模式

三种驱动方式，按数据特征选择：

### 模式 1：GameplayTag 事件（一次性变化）

**适用场景：** 布尔状态切换（灯亮/灭、虫放出/归、三灯齐聚/解除）

```
Widget::NativeConstruct()
{
    ASC->RegisterGameplayTagEvent(Tag).AddUObject(this, &OnTagChanged);
}

void OnTagChanged(const FGameplayTag Tag, int32 NewCount)
{
    if (NewCount > 0)
        PlayAnimation(ActivateAnim);   // Tag 添加
    else
        PlayAnimation(DeactivateAnim); // Tag 移除
}
```

### 模式 2：Attribute 变化委托（连续数值变化）

**适用场景：** 血条、耐力条——由 `AMHGZHUD` 订阅 ASC Attribute，收到变化后调用 Widget 的 `OnValueUpdated`

```cpp
// AMHGZHUD::BeginPlay 中订阅
ASC->GetGameplayAttributeValueChangeDelegate(
    UMHGZAttributeSet::GetHealthAttribute())
    .AddUObject(this, &AMHGZHUD::OnHealthChanged);

// AMHGZHUD::OnHealthChanged —— HUD 是中介，Widget 不碰 ASC
void AMHGZHUD::OnHealthChanged(const FOnAttributeChangeData& Data)
{
    float MaxHealth = ASC->GetNumericAttribute(UMHGZAttributeSet::GetMaxHealthAttribute());
    HealthBarWidget->OnValueUpdated(Data.NewValue, MaxHealth);
}
```

> **Widget 不直接绑定 ASC：** `OnValueUpdated` 是 `BlueprintImplementableEvent`——蓝图子类只管 `ProgressBar.SetPercent` + 颜色切换。Widget 完全不感知 GAS，换皮零风险。

### 模式 3：ResourceComponent Delegate（武器专属非 GAS 数据）

**适用场景：** 猎虫耐力、灯剩余时间、气刃槽色阶

```
// URes_InsectGlaive 中声明
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnKinsectStaminaChanged, float, Current, float, Max);
UPROPERTY(BlueprintAssignable) FOnKinsectStaminaChanged OnKinsectStaminaChanged;

// Widget 中绑定
Widget::NativeConstruct()
{
    ResourceComp->OnKinsectStaminaChanged.AddDynamic(this, &OnStaminaChanged);
}
```

---

## 七、目录结构

```
Source/MHGZ/
├── UI/
│   ├── MHGZHUD.h/cpp                    ← 主 HUD（AHUD 子类）
│   ├── MHGZUserWidget.h/cpp             ← Widget 基类（ASC 缓存 + Tag 订阅管理）
│   ├── MHGZWeaponResourceWidget.h/cpp   ← 武器资源 Widget 基类（Bind/Unbind 虚函数）
│   ├── MHGZCrosshairWidget.h/cpp        ← 准心 Widget 基类（OnAimTargetUpdated）
│   ├── MHGZAimComponent.h/cpp           ← 瞄准检测组件（Character 端）
│   └── MHGZUISubsystem.h/cpp            ← 武器资源 Widget 工厂（GameInstanceSubsystem）

Content/UI/
├── HUD/
│   ├── WBP_HUD.uasset                   ← 主 HUD 面板
│   └── WBP_Crosshair.uasset             ← 通用准心 Widget
├── Common/
│   ├── WBP_HealthBar.uasset             ← 血条 Widget（ProgressBar + 颜色渐变）
│   └── WBP_StaminaBar.uasset            ← 耐力条 Widget（ProgressBar + 颜色渐变）
├── Feedback/
│   └── WBP_DamageNumber.uasset          ← 伤害数字浮空反馈
├── InsectGlaive/
│   ├── WBP_IG_ResourcePanel.uasset      ← 虫棍资源面板（容器）
│   ├── WBP_IG_KinsectStamina.uasset     ← 猎虫耐力条
│   └── WBP_IG_ExtractDisplay.uasset     ← 三灯圆盘
```

---

## 八、设计决策

| # | 决策 | 理由 |
|---|------|------|
| UI-1 | UI 由 GameplayTag/Attribute/Delegate 驱动，Ability 不直接操作 UI | 遵循决策 #36——读数据而非写数据。UI 崩溃不影响游戏逻辑 |
| UI-2 | 武器资源 Widget 工厂模式——UMHGZUISubsystem 按 WeaponTypeTag 创建/销毁 | 装备切换时一行调用完成 UI 重建。各武器 UI 独立开发，不互相耦合 |
| UI-3 | AIMComponent 挂载到 Character——每帧射线检测仅在 bIsAiming==true 时执行 | Character 有相机访问、随 Character 销毁自然清理。非瞄准时不 Tick——零开销 |
| UI-4 | AIMComponent 仅在目标变化时广播——不每帧触发 UI 动画 | 准心样式是离散状态（红/黄/白/灰），不是连续渐变。变化时触发一次动画即可——避免每帧重复播放 |
| UI-5 | 虫棍萃取颜色预览由 AIMComponent 扩展——基类不耦合武器特有逻辑 | 其他武器复用 AIMComponent 做弱点高亮，虫棍额外扩展萃取颜色映射。通过虚函数 `GetAimTargetData` 返回扩展数据 |
| UI-6 | WBP_HUD 预留武器资源插槽——武器切换时动态替换子 Widget | 血条/耐力条跨武器通用（始终可见），武器资源随武器切换。用 Canvas 插槽 + 动态 Add/Remove Child 实现 |
| UI-7 | 血条/耐力条由 HUD 订阅 ASC Attribute——Widget 不直接绑定 GAS | Widget 纯蓝图只处理视觉——HUD 是数据中介，收到 Attribute 变化后调用 `OnValueUpdated(Current, Max)`。Widget 零 GAS 耦合，换皮只需改蓝图 |

---

## 九、验证清单

| # | 测试项 | 预期结果 |
|:--:|------|------|
| 1 | BeginPlay → WBP_HUD + 血条 + 耐力条 + 准心创建 | MainHUDWidget / HealthBarWidget / StaminaBarWidget / CrosshairWidget 均非空；添加到 Viewport 正确容器 |
| 1a | 血条初始值 = 100 | ProgressBar 满；Text 显示 "100 / 100"；绿色 |
| 1b | 受击 → 血条实时扣减 | ProgressBar 填充下降；数值文本更新；< 30% → 变红+闪烁 |
| 1c | 奔跑 → 耐力条实时扣减 | ProgressBar 填充下降；数值文本更新；< 30% → 变红+闪烁 |
| 1d | 耐力归零 → 耐力条红色常亮 | ProgressBar = 0；Text "0 / 100"；红色常亮 |
| 1e | 装备加血护甲（MaxHealth 150→Health 仍 100）→ 血条 UI 更新比例 | OnMaxHealthChanged 触发 → ProgressBar 从满变为 100/150=66% |
| 2 | 装备虫棍 → 虫棍资源 Widget 显示 | WBP_IG_ResourcePanel 出现在武器资源插槽；耐力条+三灯圆盘可见 |
| 3 | 卸下虫棍 → 资源 Widget 移除 | ActiveResourceWidget 置空；武器资源插槽为空 |
| 4 | 装备太刀 → 虫棍 Widget 销毁 → 太刀 Widget 创建 | 工厂正确切换——旧 Widget RemoveFromParent + 解绑，新 Widget 创建+绑定 |
| 5 | LT 瞄准 → AIMComponent 启动射线检测 | bIsAiming=true; Tick 中每帧 LineTrace |
| 6 | 准心扫过怪物头部 → 准心变色 | OnAimTargetChanged 广播; CrosshairWidget 响应 |
| 7 | 准心从怪物移到墙壁 → 准心恢复默认 | OnAimTargetChanged(nullptr) 广播 |
| 8 | 松开 LT → AIMComponent 停止检测 → 准心隐藏 | bIsAiming=false; Tick 跳过射线检测; 广播 nullptr |
| 9 | 虫棍萃取成功 → 三灯图标亮起 | ASC Tag 添加 → RegisterGameplayTagEvent → UI 动画 |
| 10 | 猎虫耐力扣减 → 进度条实时更新 | OnKinsectStaminaChanged Delegate → ProgressBar SetPercent |
