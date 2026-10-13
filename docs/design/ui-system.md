# UI 系统

> **当前门禁与数字实现（2026-10-13）：** 用户确认E6.3 HUD/准心/携带图标与普通猎虫回手链已完成。E6.4本轮仅伤害数字，`WBP_DamageNumber`、`BP_DamageNumberActor`、`GCN_Hit_DamageNumber`已生成/接线，原生数字文本、有限寿命、唯一Cue与普通/暴击实绘验证通过；相关回归179/179，待E6指导§4.3短PIE。其他视觉Cue和音效延期，不作为当前Demo門禁。下方较早未接线/待E6.3描述为历史快照。

> **当前实施状态（以源码、Content和用户PIE反馈为准，2026-10-12）：** HUD已修复Widget创建前提前缓存上下文、早到Ready丢失与初始条值归零；复用现有五个WBP的原生父类，绘制细长血/耐力条、猎虫/三灯/携带精华图标及LT/RT四态准心。Development Editor编译成功；加载真实BP_MHGZHUD/WBP的UI专项5/5通过，含D3D12离屏绘制。证据见[E6指导§3.9](../editor/m6-setup.md#39-hud修复实施与验证2026-10-12e63仍待pie)，永久目标原图见同文§3.0，§3.8保留修复前证据。E6.3待用户真实关卡PIE签收；E6.4伤害数字/Cue资产尚未接线。相机、输入与Gameplay AimSnapshot未改；下方原始设计快照不替代当前源码。

> **历史设计快照（2026-10-06，已被下方当前实现表取代）：** 本段记录当时计划的字段、Widget父类和RuntimeHost Ready信号，不代表现状；接口是否已实现请看上方状态表和源码。音效不在本轮范围。

## 当前实现

| 模块 | 当前状态 |
|------|----------|
| `AMHGZHUD` | 唯一Widget所有者；Widget树就绪后统一绑定并同步当前值。Ready成功建/绑面板后才消费Token，晚到上下文/类引用可重试，重复/过期事件无副作用；实际资产生命周期测试通过。 |
| `UMHGZUISubsystem` | 空壳已删除；没有恢复为Widget工厂。 |
| `UMHGZUserWidget` | ASC绑定与解绑/属性更新接口已实现。 |
| `UMHGZWeaponResourceWidget` | 资源组件绑定/解绑与资源快照更新接口已实现。 |
| `UMHGZAttributeBarWidget` | 当前/上限属性委托绑定与初始值同步；原生绿/金细条，旧Designer占位隐藏，NativeConstruct不归零；Current/Max更新测试通过。 |
| `UMHGZInsectGlaiveResourceWidget` | 原生紧凑猎虫、1/2/3红白橙菱形及独立携带精华提示；消费Resource快照，不写战斗状态。初始/更新与解绑重建测试通过。 |
| `UMHGZCrosshairWidget` | 原生四态：LT断菱形/金黄目标准心，RT灰/橙圆环；仅Tag瞄准时显示，LT优先、释放隐藏。实际ASC绑定及离屏绘制测试通过，待PIE目视。 |
| `UMHGZAimComponent` | 维护显示模式/目标与精华数据，HUD有效绑定链已修复。显示检测沿用当前相机视点；本次未修改该组件、相机或Gameplay AimSnapshot。 |

**设计原则：** UI 由 GameplayTag/Attribute/Delegate 驱动，GAS Ability 不直接操作 UI。每个本地 PlayerController 的 `AMHGZHUD` 独占自己的 Widget 树，并按 RuntimeDefinition 在 WBP_HUD 的资源插槽动态创建/销毁面板；Dedicated Server 不建立 UI 依赖。准心数据由 Character 的 `UMHGZAimComponent` 提供。

> **E6.3范围：** 主HUD容器、血条、耐力条、准心和动态武器资源面板；资产、原生视觉及生命周期专项已完成，E6.3仍待修复后真实PIE签收，不以离屏截图代替。小地图等后续扩展。

---

## 架构总览（目标方案）

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
  └── AMHGZHUD（本 LocalPlayer 唯一 Widget 创建/销毁/容器管理者）
        │
        ▼
表现层（UMG Widget——仅读数据，不写数据）
  ├── WBP_HUD（主面板——血条/耐力条/武器资源插槽/准心容器/小地图）
  ├── WBP_Crosshair（准心——订阅 OnAimTargetChanged）
  └── WBP_{Weapon}ResourcePanel（武器资源——按 WeaponTypeTag 动态创建）
```

---

## 一、AMHGZHUD — 主 HUD（历史设计接口示意，当前实现以上表及源码为准）

```
UCLASS()
class AMHGZHUD : public AHUD
```

管理本地玩家全部 Widget 的创建/销毁，持有主面板、准心与当前武器资源 Widget。武器切换时由 HUD 自己在主面板资源插槽执行替换；其他系统不得 AddToViewport 创建第二份资源面板。

| 成员 | 类型 | Category | 默认值 | 说明 |
|------|------|----------|--------|------|
| MainHUDClass | TSubclassOf\<UMHGZMainHUDWidget\> | "HUD\|Config" | nullptr | 主 HUD Widget 类（WBP_HUD） |
| HealthBarClass | TSubclassOf\<UMHGZUserWidget\> | "HUD\|Config" | nullptr | 血条 Widget 类（WBP_HealthBar） |
| StaminaBarClass | TSubclassOf\<UMHGZUserWidget\> | "HUD\|Config" | nullptr | 耐力条 Widget 类（WBP_StaminaBar） |
| CrosshairClass | TSubclassOf\<UMHGZCrosshairWidget\> | "HUD\|Config" | nullptr | 准心 Widget 类（WBP_Crosshair） |
| MainHUDWidget | TObjectPtr\<UMHGZMainHUDWidget\> | "HUD\|State" | nullptr | 主 HUD Widget 实例 |
| HealthBarWidget | TObjectPtr\<UMHGZUserWidget\> | "HUD\|State" | nullptr | 血条 Widget 实例 |
| StaminaBarWidget | TObjectPtr\<UMHGZUserWidget\> | "HUD\|State" | nullptr | 耐力条 Widget 实例 |
| CrosshairWidget | TObjectPtr\<UMHGZCrosshairWidget\> | "HUD\|State" | nullptr | 准心 Widget 实例 |
| ActiveResourceWidget | TObjectPtr\<UMHGZWeaponResourceWidget\> | "HUD\|State" | nullptr | 当前资源面板，必须是 MainHUDWidget 资源插槽的子控件 |
| ActiveRuntimeToken | FWeaponRuntimeToken | "HUD\|State" | 空 | 当前资源面板所属 Host+Generation |
| BoundRuntimeHost | TWeakObjectPtr\<UMHGZWeaponRuntimeHostComponent\> | "HUD\|State" | nullptr | 当前受控 Pawn 的 RuntimeHost |
| AttributeDelegateHandles | TArray\<FDelegateHandle\> | "HUD\|State" | 空 | Health/MaxHealth/Stamina/MaxStamina 委托句柄，EndPlay 成对移除 |
| RuntimeHostDelegateHandles | TArray\<FDelegateHandle\> | "HUD\|State" | 空 | 当前 Possessed Pawn RuntimeHost 的 Ready/Invalidated 订阅 |
| BoundAimComponent | TWeakObjectPtr\<UMHGZAimComponent\> | "HUD\|State" | nullptr | 当前 Pawn 的 Aim 数据源；换 Pawn 前解绑 |

### 关键方法

- `void BeginPlay() override`
  - 作用：`CreateWidget(MainHUDClass)` → `AddToViewport` → 依次创建 `HealthBarWidget` / `StaminaBarWidget` / `CrosshairWidget` → 放入 MainHUDWidget 对应容器（`Canvas_HealthBar` / `Canvas_StaminaBar` / `Canvas_Crosshair`）→ 绑定血条/耐力条到 ASC Attribute 变化委托 → 绑定 `UMHGZAimComponent::OnAimTargetChanged` → 转发给 CrosshairWidget。

- `void OnPossessedPawnChanged(APawn* OldPawn, APawn* NewPawn)`
  - 作用：先 `ClearResourceWidget` 并解绑旧 RuntimeHost/Aim/ASC，再取得新 Pawn 的 RuntimeHost；订阅 Ready/Invalidated 并主动读取当前快照，避免漏接已发生事件。重复绑定同一 Host 必须幂等。

- `void EndPlay(const EEndPlayReason::Type Reason) override`
  - 作用：按保存句柄解绑 ASC、AimComponent、RuntimeHost 与 Possess Delegate，先清资源面板，再 Remove 主 HUD。方法必须幂等，PIE End 与正常关卡切换走同一路径。

- `void OnWeaponRuntimeChanged(const UWeaponRuntimeDefinition* RuntimeDef, UMHGZWeaponResourceComponent* ResourceComp, FWeaponRuntimeToken RuntimeToken)`
  - 输入：新武器运行时定义、当前 Character 上的 ResourceComponent（卸下/销毁时可为 nullptr）及完整 Host Token。
  - 作用：验证 Token 来自 BoundRuntimeHost 且为当前世代，再调用 HUD 自己的 `ReplaceResourceWidget`。UI 不自行监听 Equipment，也不按 WeaponTypeTag 查第二张映射表。

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

## 二、WBP_HealthBar / WBP_StaminaBar — 血条与耐力条（规划）

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

## 三、HUD 资源面板工厂与主面板插槽（规划）

### UMHGZMainHUDWidget

```cpp
UCLASS(Abstract, Blueprintable)
class UMHGZMainHUDWidget : public UMHGZUserWidget
{
    UPROPERTY(meta=(BindWidget))
    TObjectPtr<UPanelWidget> WeaponResourceSlot;
};
```

WBP_HUD 继承该类并提供唯一 `WeaponResourceSlot`。主 HUD 自身由 `AMHGZHUD` AddToViewport；所有 Health/Stamina/Crosshair/Resource 子 Widget 只 AddChild 到对应插槽。

### HUD 资源面板方法

- `void ReplaceResourceWidget(const UWeaponRuntimeDefinition* RuntimeDef, UMHGZWeaponResourceComponent* ResourceComp, FWeaponRuntimeToken RuntimeToken)`
  1. 验证 `RuntimeToken.Host == BoundRuntimeHost` 且 Host 认定该 Token 是当前世代；旧 Token 直接忽略。
  2. 同 Token、同 Resource、同 WidgetClass 的重复 Ready 为 no-op。
  3. 先对旧面板调用 `UnbindFromResourceComponent`，从 `WeaponResourceSlot` RemoveChild，再清空引用。
  4. RuntimeDef/Resource 有效时读取唯一 `ResourceWidgetClass`，`CreateWidget` 后先 Bind，再 `WeaponResourceSlot->AddChild`。
  5. 更新 `ActiveRuntimeToken`；资源面板永远不调用 AddToViewport。

- `void ClearResourceWidget(FWeaponRuntimeToken RuntimeToken, bool bForce = false)`
  - 普通 Invalidated 只有 Token 精确等于 ActiveRuntimeToken 才清理；HUD EndPlay/UnPossess 可用 bForce 幂等清理。旧 Pawn 的迟到 Invalidated 不能删除新面板。

当前 `UMHGZUISubsystem` 不承载数据或 Widget，在 M6 删除。未来若需要跨 Pawn 的 LocalPlayer ViewModel，可以新增只保存 ViewModel/设置的 Subsystem，但 Widget 树所有权仍属于 HUD。

---

## 四、UI Widget C++ 基类

**设计原则：** 与 GA/Ability 继承层级一致——C++ 基类封装通用逻辑（ASC 缓存、Tag 订阅管理、Delegate 绑定），蓝图子类只配置视觉表现。基类不画 UI（不改 UMG 布局）——所有 `BindWidget` 的子控件由蓝图创建。

### 继承层级

```
UUserWidget（UE 原生）
  └── UMHGZUserWidget（项目基类——ASC 缓存 + Tag 订阅统一管理）
        ├── UMHGZMainHUDWidget（主面板基类——唯一资源插槽）
        │     └── WBP_HUD
        ├── UMHGZWeaponResourceWidget（武器资源基类——Bind/Unbind 虚函数）
        │     ├── WBP_IG_ResourcePanel ← 虫棍资源面板（蓝图子类）
        │     └── WBP_LS_SpiritGauge  ← 太刀气刃槽（蓝图子类，后续）
        └── UMHGZCrosshairWidget（准心基类——OnAimTargetUpdated 处理）
              └── WBP_Crosshair ← 通用准心（蓝图子类）
```

> **WBP_HUD** 必须继承 `UMHGZMainHUDWidget` 并提供命名一致的资源插槽。该极薄基类只暴露容器，不处理战斗数据；资源生命周期由 `AMHGZHUD` 统一管理。

### UMHGZUserWidget — 项目基类（当前仅 BindToASC/BoundASC/OnValueUpdated；其余为规划）

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
| `void ListenForGameplayTag(FGameplayTag Tag, TFunction<void(const FGameplayTag, int32)> Callback)` | 订阅 ASC Tag 变化——自动缓存 Tag+Handle。`NativeDestruct` 时按原 Tag 精确取消，无需子类手动管理；回调只弱捕获 Widget |
| `virtual void NativeConstruct() override` | 获取 ASC → 调用 `BP_OnInitialized`（BlueprintImplementableEvent——蓝图子类在此绑定数据源） |
| `virtual void NativeDestruct() override` | 先调用资源 Widget 的幂等 Unbind（若适用），再遍历 Tag+Handle 精确取消 ASC 订阅，清空 BoundASC，最后调用 `BP_OnTeardown` |

> **设计理由：** 蓝图 `RegisterGameplayTagEvent` 返回的 DelegateHandle 需要手动缓存并在析构时取消——纯蓝图经常遗漏导致悬空回调。基类封装后子类只需 `ListenForGameplayTag(Tag, [this](auto Tag, int32 Count) { ... })`——一行调用，零泄漏风险。

### UMHGZWeaponResourceWidget — 武器资源基类（当前只保存 ResourceComponent；Unbind 为规划）

```
UCLASS(Abstract, Blueprintable)
class UMHGZWeaponResourceWidget : public UMHGZUserWidget
```

所有武器资源 Widget 的基类。提供 Bind/Unbind 虚函数，由 `AMHGZHUD::ReplaceResourceWidget` 完成数据源切换。

| 成员 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| ResourceComponent | TWeakObjectPtr\<UMHGZWeaponResourceComponent\> | nullptr | 当前绑定到的 Character 武器 ResourceComponent；Widget/GameInstance 不延长其寿命 |
| BoundRuntimeToken | FWeaponRuntimeToken | 空 | 当前数据源 Host+世代；异步/延迟刷新前必须由 Host 验证仍是当前 Token |

| 方法 | 说明 |
|------|------|
| `virtual void BindToResourceComponent(UMHGZWeaponResourceComponent* InResourceComp, FWeaponRuntimeToken RuntimeToken)` | 子类覆写——先调用 Unbind，再保存弱引用与 Token，绑定武器特定 Delegate（如 `OnKinsectStaminaChanged`）并推送一次完整初值 |
| `virtual void UnbindFromResourceComponent()` | 子类覆写——按保存的 DelegateHandle 或 `RemoveAll(this)` 解绑全部 Resource Delegate，再清空弱引用与世代；`NativeDestruct` 必须再次幂等调用 |

> **子类覆写示例（虫棍）：**
> ```cpp
> void UMHGZIGResourcePanel::BindToResourceComponent(UMHGZWeaponResourceComponent* InRC, FWeaponRuntimeToken Token)
> {
>     Super::BindToResourceComponent(InRC, Token);
>     URes_InsectGlaive* IG = Cast<URes_InsectGlaive>(InRC);
>     IG->OnKinsectStaminaChanged.AddUObject(this, &ThisClass::OnStaminaChanged);
>     ListenForGameplayTag(WeaponResource.IG.Extract.Red, [this](auto, int32 C) { OnExtractChanged(Red, C); });
>     RefreshFromCurrentState(*IG);
> }
>
> void UMHGZIGResourcePanel::UnbindFromResourceComponent()
> {
>     if (URes_InsectGlaive* IG = Cast<URes_InsectGlaive>(ResourceComponent.Get()))
>     {
>         IG->OnKinsectStaminaChanged.RemoveAll(this);
>     }
>     Super::UnbindFromResourceComponent();
> }
> ```

### UMHGZCrosshairWidget — 准心基类

```
UCLASS(Abstract, Blueprintable)
class UMHGZCrosshairWidget : public UMHGZUserWidget
```

准心 Widget 的基类。由原生HUD绑定 ASC瞄准Tag与 Character 的 AimComponent，蓝图只消费显示事件，不自己读输入、不自己 LineTrace。

| 方法 | 说明 |
|------|------|
| `void OnAimVisibilityUpdated(bool bVisible)` | 瞄准上下文是否存在；离开时隐藏并清除旧目标效果 |
| `void OnAimDisplayStateUpdated(EMHGZAimDisplayMode Mode, AActor* Target, FGameplayTag HitzoneTag, FGameplayTag ExtractColor)` | 当前模式及目标载荷；`None/Kinsect/Action`。WBP据此画LT/RT的无目标/有效目标四态 |
| `void OnAimTargetUpdated(AActor* Target, FGameplayTag HitzoneTag, FGameplayTag ExtractColor)` | 兼容既有蓝图逻辑；新Widget仅用上面的DisplayState事件绘图，避免重复处理 |

> **调用链：** `AMHGZHUD::RefreshPawnContext()`绑定ASC与AimComponent → `UMHGZCrosshairWidget`发布显示模式/目标 → WBP只绘制。LT与RT目标检测共用当前相机屏幕中心射线；该射线只供显示，不替代输入时刻采集的Gameplay AimSnapshot。

视觉规范必须以[E6指导§3.0的五张永久《世界》原图](../editor/m6-setup.md#30-目标视觉真相源用户提供的怪物猎人世界五张参考图)为准：细长生命绿/耐力金色条；紧凑猎虫图标及编号菱形三灯；LT无目标浅灰白断开菱形、有目标金黄色菱形；RT无目标浅灰白圆环、有目标橙色圆环，两种RT状态均带四个三角标记。此前“LT目标精华色预览、RT锁定菱形”的文字/实现不是用户原图要求，已更正。红/白/橙三灯图标与猎虫当前携带精华图标分开，后者在交付后隐藏。不得修改现有相机位置、SpringArm、PlayerCameraManager、镜头管理、俯仰限制或Gameplay AimSnapshot。

**携带椭圆的多色合同（2026-10-13）：** 普通猎虫/操虫斩在本Demo仍最多携带一种精华；强化觉虫击贯通成功伤害可携带红、白、橙不同颜色，同色去重，回手后才给角色点灯。竖立椭圆必须读取 `KinsectPendingExtractColors` 的真实携带集合：一色全填、两色二分、三色三分，回手交付后清空；编号菱形只显示角色已获得的灯。设计双色是为未来可携带两色的猎虫预留，不代表当前普通容量已扩展。不得用角色已有灯或准心部位颜色伪造携带颜色，也不能只读取兼容首色字段。

---

## 五、UMHGZAimComponent — 瞄准检测组件

> 虫棍扩展（萃取颜色预览）详见 [insect-glaive.md §十二](insect-glaive.md#十二瞄准与-ui-集成)。本节仅列出跨武器通用部分。

挂载到 **Character**。负责瞄准射线检测和准心目标识别。虫棍使用 Kinsect 瞄准上下文做精华颜色预览；其他武器可以提供自己的 Aim Profile，但通用组件不包含钩爪或虫棍部位映射。

| 成员 | 类型 | Category | 默认值 | 说明 |
|------|------|----------|--------|------|
| bIsAiming | bool | "Aim\|State" | false | 当前是否处于猎虫瞄准（订阅 `Combat.State.Aiming.Kinsect`）；保留既有Kinsect语义 |
| CurrentAimTarget | TWeakObjectPtr\<AActor\> | "Aim\|State" | nullptr | 准心当前指向的 Actor |
| CurrentAimHitzoneTag | FGameplayTag | "Aim\|State" | 空 | 准心指向的怪物部位 Tag |
| CurrentAimExtractColor | FGameplayTag | "Aim\|State" | 空 | 当前 Hitzone 的 `ExtractColorTag`（Red/Orange/White） |
| AimMaxDistance | float | "Aim\|Config" | 3000 | 瞄准射线最大距离（cm） |
| AimChannel | TEnumAsByte\<ECollisionChannel\> | "Aim\|Config" | Visibility | WorldStatic/Hitzone Block；命中后验证组件 ObjectType=Hitzone，墙壁会正确遮挡 |
| AimInputAction | — | — | — | AimComponent 不绑定输入；`IA_LT` 由 InputComponent 绑定并交给 WeaponInputRouter 处理 |

| Delegate | 签名 | 说明 |
|------|------|------|
| OnAimTargetChanged | `(AActor* Target, FGameplayTag HitzoneTag, FGameplayTag ExtractColor)` | 准心指向变化时广播。Target 为 nullptr 表示瞄空/场景 |

### 关键方法

- `void BeginPlay() override`
  - 作用：只登记等待 RuntimeHost/ASC ActorInfo Ready，不假设此时 PlayerState/ASC 已完成初始化。AimComponent 不直接绑定 EnhancedInput。

- `void BindToAbilitySystem(UAbilitySystemComponent*)` / `void UnbindFromAbilitySystem()`
  - 作用：分别订阅 `Combat.State.Aiming.Action` 与 `Combat.State.Aiming.Kinsect`；`bIsAiming/IsAiming()`仍仅代表Kinsect瞄准，Action瞄准另用内部显示状态，不改变玩法查询语义。Pawn替换/UnPossess/EndPlay时精确解绑并清目标。

- `void TickComponent(float DeltaTime) override`
  - 作用：Kinsect或Action瞄准期间才执行 `LineTraceSingleByChannel(Visibility)`；只有命中组件ObjectType=Hitzone且可Cast为`UMHGZMonsterHitzoneComponent`才读取目标/精华色。命中WorldStatic/其他对象广播空目标。**仅在目标变化时广播**，且不会透墙读取部位。使用PlayerController当前视点；不移动相机或改变镜头约束。

- `void OnAimingTagChanged(const FGameplayTag Tag, int32 NewCount)`
  - 作用：由ASC Tag Delegate调用；分别更新Kinsect/Action显示态，任一瞄准激活即刷新显示射线，两者都关闭时清目标。输入Gameplay快照仍由InputRouter在输入解析时独立捕获。

### 虫棍扩展

Demo 保留 Target/HitzoneTag/ExtractColor 三参数，但颜色直接来自命中的 Hitzone 配置，不在 AimComponent 按部位名称映射。以后其他武器需要不同准心载荷时，通过 Aim Profile/ViewModel 扩展，不把虫棍逻辑塞入通用射线组件。

---

## 六、Widget 数据绑定模式（目标方案）

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

## 七、资产目录结构（E6.3当前资产与后续扩展）

```
Source/MHGZ/
├── UI/
│   ├── MHGZHUD.h/cpp                    ← 主 HUD（AHUD 子类）
│   ├── MHGZUserWidget.h/cpp             ← Widget 基类（ASC 缓存 + Tag 订阅管理）
│   ├── MHGZMainHUDWidget.h/cpp           ← 主面板基类（资源插槽）
│   ├── MHGZWeaponResourceWidget.h/cpp   ← 武器资源 Widget 基类（Bind/Unbind 虚函数）
│   ├── MHGZCrosshairWidget.h/cpp        ← 准心 Widget 基类（OnAimTargetUpdated）
│   ├── MHGZAimComponent.h/cpp           ← 瞄准检测组件（Character 端）
│   └── （删除现有 MHGZUISubsystem 空壳）

Content/UI/
├── HUD/
│   ├── WBP_HUD.uasset                   ← 主HUD四个Overlay插槽
│   ├── WBP_HealthBar.uasset             ← Health属性条
│   ├── WBP_StaminaBar.uasset            ← Stamina属性条
│   └── WBP_Crosshair.uasset             ← 原生绘制准心
├── Feedback/                            ← E6.4数字已生成/接线，待PIE
│   └── WBP_DamageNumber.uasset          ← 原生最终伤害文本/暴击样式
└── InsectGlaive/
    └── WBP_IG_ResourcePanel.uasset      ← 虫棍资源面板
```

血条/耐力条、主HUD和资源面板由Designer树承载基础布局；猎虫耐力、三灯、携带精华、舞踏层数和粉尘计数都在资源面板内，不要求额外创建独立子Widget。是否需要美术微调以PIE目视结果为准。

---

## 八、设计决策

| # | 决策 | 理由 |
|---|------|------|
| UI-1 | UI 由 GameplayTag/Attribute/Delegate 驱动，Ability 不直接操作 UI | 遵循决策 #36——读数据而非写数据。UI 崩溃不影响游戏逻辑 |
| UI-2 | HUD 是本地 Widget 树唯一所有者；资源面板只插入 WBP_HUD 资源插槽，现有 UISubsystem 删除 | 消除 AddToViewport/插槽双所有者；每个 PlayerController/HUD 天然隔离本地玩家，RuntimeToken 阻止旧 Pawn 回调 |
| UI-3 | AIMComponent 挂载到 Character——每帧射线检测仅在 bIsAiming==true 时执行 | Character 有相机访问、随 Character 销毁自然清理。非瞄准时不 Tick——零开销 |
| UI-4 | AimComponent 仅在目标变化时广播——不每帧触发 UI 动画 | 准心样式是离散状态（红/橙/白/灰），不是连续渐变。变化时触发一次动画即可——避免每帧重复播放 |
| UI-5 | 当前 AimComponent 直接包含虫棍萃取颜色；后续再拆通用扩展接口 | `GetAimTargetData` 尚不存在，保留为多武器 UI 解耦方案 |
| UI-6 | WBP_HUD 预留武器资源插槽——武器切换时动态替换子 Widget | 血条/耐力条跨武器通用（始终可见），武器资源随武器切换。用 Canvas 插槽 + 动态 Add/Remove Child 实现 |
| UI-7 | 血条/耐力条由 HUD 订阅 ASC Attribute——Widget 不直接绑定 GAS | Widget 纯蓝图只处理视觉——HUD 是数据中介，收到 Attribute 变化后调用 `OnValueUpdated(Current, Max)`。Widget 零 GAS 耦合，换皮只需改蓝图 |

---

## 九、规划验收清单（Widget 创建并接线后执行）

| # | 测试项 | 预期结果 |
|:--:|------|------|
| 1 | BeginPlay → WBP_HUD + 血条 + 耐力条 + 准心创建 | MainHUDWidget / HealthBarWidget / StaminaBarWidget / CrosshairWidget 均非空；添加到 Viewport 正确容器 |
| 1a | 血条初始值 = 100 | ProgressBar 满；Text 显示 "100 / 100"；绿色 |
| 1b | 受击 → 血条实时扣减 | ProgressBar 填充下降；数值文本更新；< 30% → 变红+闪烁 |
| 1c | 奔跑 → 耐力条实时扣减 | ProgressBar 填充下降；数值文本更新；< 30% → 变红+闪烁 |
| 1d | 耐力归零 → 耐力条红色常亮 | ProgressBar = 0；Text "0 / 100"；红色常亮 |
| 1e | 装备加血护甲（MaxHealth 150→Health 仍 100）→ 血条 UI 更新比例 | OnMaxHealthChanged 触发 → ProgressBar 从满变为 100/150=66% |
| 2 | 装备虫棍 → 虫棍资源 Widget 显示 | WBP_IG_ResourcePanel 出现在武器资源插槽；耐力条+三灯圆盘可见 |
| 3 | 卸下虫棍 → 资源 Widget 移除 | HUD 的 ActiveResourceWidget 置空；武器资源插槽为空 |
| 4 | 装备太刀 → 虫棍 Widget 销毁 → 太刀 Widget 创建 | HUD 先 Unbind/RemoveChild 旧面板，再 Create/Bind/AddChild 新面板 |
| 5 | LT 瞄准 → AIMComponent 启动射线检测 | bIsAiming=true; Tick 中每帧 LineTrace |
| 6 | 准心扫过怪物头部 → 准心变色 | OnAimTargetChanged 广播; CrosshairWidget 响应 |
| 7 | 准心从怪物移到墙壁 → 准心恢复默认 | OnAimTargetChanged(nullptr) 广播 |
| 8 | 松开 LT → AIMComponent 停止检测 → 准心隐藏 | bIsAiming=false; Tick 跳过射线检测; 广播 nullptr |
| 9 | 虫棍萃取成功 → 三灯图标亮起 | ASC Tag 添加 → RegisterGameplayTagEvent → UI 动画 |
| 10 | 猎虫耐力扣减 → 进度条实时更新 | OnKinsectStaminaChanged Delegate → ProgressBar SetPercent |
| 11 | 死亡重建/切换受控 Pawn 后，旧 RuntimeHost 延迟广播 Ready/Invalidated | Token.Host 与 RuntimeSource 不一致，回调被忽略；新 Pawn Widget 和绑定不受影响 |
| 12 | 两个 LocalPlayer 同时装备不同武器 | 各自 PlayerController 的 HUD 只管理自己的 Widget 树，互不覆盖 |
| 13 | Runtime Ready 重复广播同一 Token | HUD no-op；资源插槽始终只有一个子控件，Viewport 只有一份 WBP_HUD |
