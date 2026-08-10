# GameplayCue 系统

> **实施状态说明（以源码、配置和 Content 为准）：** 当前只完成 GameplayCue Tag 注册以及扫描路径配置；Content 中没有 GC 资产。攻击代码把 Cue Tag 当 DynamicAssetTag 保存，但 DynamicAssetTag 不会自动触发 GameplayCue。本文给出木桩 Demo 所需的修订目标，本轮不修改代码。

## 当前实现

| 项目 | 当前状态 |
|------|----------|
| 配置 | `/Game/GameplayCues` 扫描路径已配置；没有 `GlobalGameplayCueManagerClass`。 |
| Tag | Slash/Blunt/Fire/Crit/DamageNumber/Kinsect、IG 三灯反馈、Monster.Roar、Weapon.Trail 等部分 Tag 已注册；本文其他 Tag 是规划。 |
| C++ | `UMHGZGameplayCueManager`、`UMHGZCue_HitBase`、`UMHGZCue_BuffBase`、`UMHGZDamageNumberPool` 均未创建。 |
| 资产 | `Content/GameplayCues` 只有目录占位文件，没有任何 GameplayCueNotify 或伤害数字 Widget。 |
| 模块 | Build.cs 已有 GameplayAbilities/GameplayTags/UMG，尚未添加 Niagara。 |

> **设计原则：** GameplayCue 只负责表现。伤害结算先生成明确的 HitFeedbackResult，再由目标侧 Router 显式执行瞬时 Cue；Duration GE 可使用自身配置的 GameplayCue 处理 Add/Remove。Demo 不需要自定义 GameplayCueManager。

> **适用范围：** 当前版本仅单机。GameplayCue 在单机模式下等价于本地函数调用，零网络开销。

> **与 AnimNotify 的职责边界：** 帧同步 VFX（武器拖尾、脚步声）由 AnimNotify 驱动；状态驱动 VFX（命中反馈、Buff 光环、死亡）由 GameplayCue 驱动；镜头效果由 Ability 内 CameraModifier 管理。三者互补，不重叠。

---

> **设计决策：** 见 [design-decisions.md](design-decisions.md) #99-#103

---

## GameplayCue Tag 目标层级（是否已注册以 DefaultGameplayTags.ini 为准）

### 命中反馈（Execute——瞬时触发）

```
GameplayCue.Hit.Slash           ← 斩击命中火花
GameplayCue.Hit.Blunt           ← 打击命中火花
GameplayCue.Hit.Fire            ← 火属性命中特效
GameplayCue.Hit.Ice             ← 冰属性命中特效
GameplayCue.Hit.Thunder         ← 雷属性命中特效
GameplayCue.Hit.Dragon          ← 龙属性命中特效
GameplayCue.Hit.Crit            ← 暴击命中特效
GameplayCue.Hit.Block           ← 格挡/防御命中特效
GameplayCue.Hit.DamageNumber    ← 伤害数字浮空文字
```

> **触发方式：** AttackAbility 把真实 HitResult 和命中类型写入自定义 GameplayEffectContext；ExecCalc/AttributeSet 得到最终伤害后生成 `FMHGZHitFeedbackResult`，由目标侧 `UMHGZHitFeedbackRouter` 依次调用 `ExecuteGameplayCue`。DynamicAssetTags 只作为元数据，不承担触发。

### Buff/Debuff 视觉（Add/Remove——持续触发）

```
GameplayCue.Buff.AttackUp       ← 攻击力提升光环
GameplayCue.Buff.DefenseUp      ← 防御力提升光环
GameplayCue.Buff.SpeedUp        ← 速度提升光环
GameplayCue.Buff.HealOverTime   ← 持续回复光环
GameplayCue.Buff.ElementResist  ← 属性耐性光环
```

> **触发方式：** Buff GE 的 `GameplayCueTags` 中添加对应 Tag。GE 生效时→`GC.OnActive`（Add）；GE 到期/被移除时→`GC.OnRemove`（Remove）。

### 角色/怪物特效（Execute）

```
GameplayCue.Character.Death     ← 角色死亡特效
GameplayCue.Character.Dodge     ← 翻滚/闪避尘土
GameplayCue.Character.Heal      ← 回复特效
GameplayCue.Monster.Roar        ← 怪物咆哮
GameplayCue.Monster.Death       ← 怪物死亡
GameplayCue.Monster.Stagger     ← 怪物硬直
```

---

## 架构总览（规划）

```text
AttackAbility
  → GE EffectContext 携带真实 HitResult/HitType/SourceAction
  → ExecCalc 输出最终伤害
  → AttributeSet/结算层生成 FMHGZHitFeedbackResult
  → UMHGZHitFeedbackRouter 显式 ExecuteGameplayCue
      ├── Hit.Slash / Hit.Blunt / Hit.Kinsect / IG 特殊命中
      ├── Hit.Crit（若暴击）
      └── Hit.DamageNumber（RawMagnitude=最终伤害）

Duration Buff GE
  → GE 自身 GameplayCue 配置
  → OnActive / WhileActive / OnRemove
```

---

## 基础设施（Demo 规划）

### UMHGZHitFeedbackRouter

由目标 Actor/ASC 侧持有或通过组件取得。输入结构至少包含：最终伤害、bCritical、真实 HitResult、物理命中 CueTag、可选元素 CueTag、Source/Target 和 AttackInstanceID。

| 方法 | 说明 |
|------|------|
| `RouteHitFeedback(Result)` | 校验 Damage/HitResult，构造 FGameplayCueParameters，并按确定顺序执行物理、元素、暴击、伤害数字 Cue |
| `BuildCueParameters(Result)` | `Location/Normal` 来自真实 HitResult，`RawMagnitude` 为最终伤害；Crit 等离散结果放入自定义 EffectContext/结果 Tag |
| `ShouldSuppressCue(CueTag, Location)` | 可选距离裁剪；不得影响伤害结算 |

Demo 继续使用引擎默认 GameplayCueManager 和现有扫描路径，不配置 `GlobalGameplayCueManagerClass`。

### UMHGZCue_HitBase — 一次性命中特效

继承 `UGameplayCueNotify_Burst`。该类型是一次性、非实例化 Notify，不把它描述或实现成可池化 Actor；Niagara/音频组件按各自并发和生命周期管理。

| 成员 | 类型 | 默认值 | 说明 |
|------|------|:--:|------|
| MaxCullDistance | float | 3000 | 距所有玩家相机超此距离不生成粒子 |
| SoundAttenuation | TObjectPtr\<USoundAttenuation\> | — | 音效衰减设置（按相机距离自动调音量） |
| NiagaraSystem | TObjectPtr\<UNiagaraSystem\> | — | 主粒子系统（蓝图子类配置） |
| HitSound | TObjectPtr\<USoundBase\> | — | 命中音效（蓝图子类配置） |

| 方法 | 说明 |
|------|------|
| `OnBurst(Target, Parameters)` | 读取 Parameters.Location/Normal/RawMagnitude，距离裁剪后生成一次 Niagara 和音效 |
| `CheckDistanceCull(Location)` | 遍历 PlayerController 取最近相机距离，> MaxCullDistance 返回 true |

### UMHGZCue_BuffBase — Buff 光环基类

持久 Buff 使用 `AGameplayCueNotify_Actor`（或满足相同 Add/Remove 合同的 Looped Notify），由 GAS 按 GE 生命周期创建和移除。`BurstLatent` 不作为通用持久 Buff 基类。

| 成员 | 类型 | 说明 |
|------|------|------|
| SocketName | FName | 挂载骨骼名（默认 `"Root"`） |
| LoopingVFX | TObjectPtr\<UNiagaraSystem\> | 循环粒子系统 |
| ApplySound | TObjectPtr\<USoundBase\> | Buff 应用瞬间音效 |
| RemoveSound | TObjectPtr\<USoundBase\> | Buff 移除瞬间音效 |
| RemoveBurstVFX | TObjectPtr\<UNiagaraSystem\> | 移除时爆发粒子（一次性） |

| 方法 | 说明 |
|------|------|
| `OnActive(Target, Parameters)` | 覆写——1. Spawn LoopingVFX 附加到 Target→GetMesh(), SocketName；2. Play ApplySound |
| `WhileActive(Target, Parameters)` | 覆写——若 Parameters 含 StackCount，更新 Niagara FloatParameter("Intensity", StackCount/MaxStack) |
| `OnRemove(Target, Parameters)` | 覆写——1. Play RemoveSound；2. Spawn RemoveBurstVFX at Target.Location；3. Destroy LoopingVFX 组件 |
| `GetAttachSocketName()` | BlueprintPure——返回 SocketName，供蓝图覆写动态选择骨骼 |

### UMHGZDamageNumberPool — 伤害数字对象池

`Source/MHGZ/GameplayCue/MHGZDamageNumberPool.h/.cpp`，继承 `UWorldSubsystem`。

| 成员 | 类型 | 默认值 | 说明 |
|------|------|:--:|------|
| PoolSize | int32 | 30 | 预分配 Widget 数量 |
| ActorClass | TSubclassOf\<AMHGZDamageNumberActor\> | BP_DamageNumberActor | 每个 Actor 自己拥有 WidgetComponent |
| FloatOffset | FVector | (0,0,80) | Widget 生成位置相对命中点的偏移 |
| FloatDuration | float | 1.5 | 上浮+淡出动画总时长 |

| 方法 | 说明 |
|------|------|
| `Initialize(Collection)` | 世界就绪后 Spawn PoolSize 个 DamageNumberActor，隐藏并加入空闲池 |
| `Deinitialize()` | 销毁由该 WorldSubsystem Spawn 的全部 Actor |
| `AcquireWidget()` | 从空闲池 Pop→SetHidden(false)→加入使用池→返回。池空返回 nullptr |
| `ReleaseWidget(Widget)` | 从使用池 Remove→SetHidden(true)→加入空闲池。由动画完成回调调用 |
| `GetActiveCount()` | BlueprintPure——返回使用池大小，调试用 |

**由 GC_Hit_DamageNumber 驱动：**

```
OnBurst(Parameters)
  1. DamageValue = Parameters.RawMagnitude
  2. bCrit = 从自定义 EffectContext/HitFeedbackResult Tag 读取
  3. Widget = DamageNumberPool→AcquireWidget()
  4. 若 nullptr → return（池耗尽，静默丢弃）
  5. 设数值/暴击颜色/字号 → Actor 放到 HitLocation + FloatOffset；默认不附着移动目标
  6. PlayAnimation(上浮+淡出, FloatDuration) → 回调 ReleaseWidget
```

> **池耗尽策略：** 不阻塞伤害结算。Demo 记录 Peak/Drop 计数并复用最早已完成淡出的 Actor；PoolSize 是性能参数，不以某个招式的理论数字宣称永远足够。

---

> **攻击链路集成（FAttackDamageConfig 扩展 + MakeDamageSpec 修改）：** 见 [actions.md](actions.md) 中"核心配置结构"与"GameplayCue 集成 — MakeDamageSpec"段。ApplyDamage 现有链路零改动。

---

## Buff/Debuff 视觉集成（规划）

### Buff GE 配置

每个 Buff GE 蓝图需在 `GameplayCueTags` 中添加对应 Tag：

| Buff | GE 蓝图 | GameplayCueTag | Duration Policy |
|------|---------|----------------|:--:|
| 攻击力↑ | GE_Buff_AttackUp | `GameplayCue.Buff.AttackUp` | HasDuration |
| 防御力↑ | GE_Buff_DefenseUp | `GameplayCue.Buff.DefenseUp` | HasDuration |
| 速度↑ | GE_Buff_SpeedUp | `GameplayCue.Buff.SpeedUp` | HasDuration |
| 持续回复 | GE_Buff_HealOverTime | `GameplayCue.Buff.HealOverTime` | HasDuration |
| 属性耐性 | GE_Buff_ElementResist | `GameplayCue.Buff.ElementResist` | HasDuration |

> **Trigger 自动路由：** Duration GE 激活→ASC 自动调用 GC Notify 的 `OnActive`；GE 到期或被 `RemoveActiveEffects` 移除→`OnRemove`。无需手动调用 Execute/Add/Remove。

---

> **目录结构：** 见 [directory-structure.md](directory-structure.md) 中 `GameplayCue/` 与 `Content/GameplayCues/` 段

---

> **验证方案：** 见 [验证清单](../editor/verification.md) #60-#70

---

## Build.cs 目标模块依赖（Niagara 当前尚未添加）

在 `Source/MHGZ/MHGZ.Build.cs` 中确认/新增：

```cpp
PublicDependencyModuleNames.AddRange(new string[] {
    "GameplayAbilities",   // GameplayCue Router、Burst 与 Actor/Looping Notify
    "GameplayTags",        // FGameplayTag, FGameplayTagContainer
    "Niagara",             // UNiagaraSystem（GameplayCue 粒子系统）
    "UMG"                  // UUserWidget, UWidgetComponent（伤害数字）
});
```

---

## 排除范围

| 排除项 | 原因 | 归属 |
|--------|------|------|
| 武器拖尾 | 帧精确动画同步 | AnimNotify 直接驱动 TrailComponent |
| 镜头震动 | 独立系统 | Ability 内 CameraModifier（设计决策 #39） |
| 脚步声 | 帧精确动画同步 | AnimNotify |
| 环境交互特效 | 属交互系统范畴 | 后续独立设计 |
| MetaSound vs SoundCue | 音效资产选型 | 音效管理范畴（pending.md 标记） |
| 网络复制 | 当前仅单机 | 后续版本补充 |
