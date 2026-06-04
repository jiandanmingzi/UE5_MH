# GAS 基础设施

> 以下三节为 GAS 初始化必须明确的架构决策，是先决条件而非可选设计。

## ASC 挂载位置——PlayerState

`UMHGZAbilitySystemComponent` 挂载到 **AMHGZPlayerState**（非 Character）。

| 理由 | 说明 |
|------|------|
| 跨 Character 生命周期 | PlayerState 不随 Character 销毁而丢失——猫车/关卡切换时 ASC 和属性持续存在 |
| 官方推荐模式 | Epic 的 Lyra、ShooterGame 等官方项目均将 ASC 放在 PlayerState |
| 组件集中管理 | `EquipmentComponent`、`BackpackComponent`、`WeaponResourceComponent`、`WarehouseComponent` 全部挂载到 PlayerState——全部在同一 Actor 上，无跨 Actor 引用。`QuickBarComponent`、`InputComponent` 挂载到 PlayerController（输入/反馈层职责） |
| 未来网络扩展 | 无需迁移 ASC——PlayerState 本身就是网络同步的载体 |

> **注意：** ASC 放 PlayerState 后，AnimNotifyState 的 `MeshComp→GetOwner()` 链路变为 `MeshComp→GetOwner()→GetPlayerState()→GetAbilitySystemComponent()`，多一次跳转。对性能影响可忽略（指针跳转 < 1ns），且 AnimNotifyState 仅在 Montage 播放期间触发，频率可控。

## InitAbilityActorInfo

在 `AMHGZCharacter::PossessedBy` 中调用（此时 PlayerState 已就绪）：

```cpp
void AMHGZCharacter::PossessedBy(AController* NewController)
{
    Super::PossessedBy(NewController);

    AMHGZPlayerState* PS = GetPlayerState<AMHGZPlayerState>();
    UMHGZAbilitySystemComponent* ASC = PS->GetAbilitySystemComponent();
    if (ensure(ASC))
    {
        ASC->InitAbilityActorInfo(PS, this);
        // Owner = PlayerState（拥有这些 Ability 的逻辑实体，跨 Character 存在）
        // Avatar = Character（物理表现实体）
        ASC->InitializeAbilitySystem();
    }
}
```

## 组件归属——全部挂载到 PlayerState

> 以下组件统一挂载到 `AMHGZPlayerState`，消除跨 Actor 引用。`EdgeVaultComponent` 和 `InputComponent` 除外——前者需要 CMC 访问，后者管理 EnhancedInput IMC。

| 组件 | 挂载位置 | 理由 |
|------|----------|------|
| `UMHGZAbilitySystemComponent` | PlayerState | GAS 核心 |
| `UMHGZAttributeSet` | PlayerState（ASC 子对象） | 属性生命周期同 ASC |
| `UMHGZEquipmentComponent` | PlayerState | 装备 GE 管理，依赖 ASC |
| `UMHGZBackpackComponent` | PlayerState | 跨关卡保留物品 |
| `UMHGZWarehouseComponent` | PlayerState | 仓库跨关卡 |
| `UMHGZWeaponResourceComponent` | PlayerState | 武器资源，依赖 ASC Tag 查询 |
| `UMHGZQuickBarComponent` | PlayerController | 快捷栏——输入选择+音效反馈+使用触发。需访问 Backpack（通过 `GetPlayerState()` 一次跳转获取）、ASC Tag 查询（持刀态/受击/攻击中判断能否使用）。放在 PlayerController 使输入→反馈链路最短 |
| `UMHGZEdgeVaultComponent` | Character | 需要 CMC 访问（Velocity/边缘检测） |
| `UMHGZInputComponent` | PlayerController | 管理 IMC 生命周期 |
| `UMotionWarpingComponent` | Character | UE5 内置，动画驱动。需 SkeletalMeshComponent+AnimBP 管线（PlayerState 不具备）——构造函数 `CreateDefaultSubobject` 随 Character 创建，GA 在 ActivateAbility 中通过 `FindComponentByClass` 设 Warp Target，Montage 中 `AnimNotifyState_MotionWarping` 自动消费。单向交互，零耦合 |

## 关卡切换——Seamless Travel + SaveGame 兜底

**怪猎游玩模式：** 据点（接任务/工坊/吃饭）→ 选择任务 → 加载指定地图（天气/怪物分布/采集点由任务参数决定）→ 完成/失败/放弃 → 返回据点。玩家背包、装备、仓库在据点↔任务地图之间**全部保留**。

**UE5 关卡切换方式对比：**

| 方式 | PlayerState | GameInstance | 加载耗时 | 适用场景 |
|------|:--:|:--:|:--:|------|
| **OpenLevel** | ❌ 销毁重建 | ✅ 保留 | 长（全量加载） | 主菜单→游戏、完全独立的关卡 |
| **Seamless Travel** | ✅ 保留（需配置） | ✅ 保留 | 中（非持久层切换） | 据点↔任务地图——PlayerState 不销毁，ASC/背包/仓库持续存在 |
| **Level Streaming** | ✅ 始终存在 | ✅ 始终存在 | 短（增量加载） | 同一持久世界内加载子关卡（如据点内进入训练场） |

**推荐方案——Seamless Travel 为主，OpenLevel + SaveGame 兜底：**

```
据点（Hub Map, 持久关卡）
  │
  ├─ 接任务 → Seamless Travel → 任务地图（动态设置天气/怪物/采集点）
  │     │
  │     ├─ 任务完成 → Seamless Travel → 返回据点
  │     └─ 猫车/放弃 → Seamless Travel → 返回据点
  │
  └─ 主菜单 → OpenLevel → 据点（首次进入，无存档则新建 PlayerState）
```

**Seamless Travel 配置要点：**
- `AMHGZGameMode::bUseSeamlessTravel = true`
- 任务地图设为非持久关卡（在 World Settings 中配置 Transition Map）
- PlayerState 自动跨关卡保留——ASC、Backpack、Warehouse、Equipment 全部无缝衔接
- 任务参数（地图名、天气、怪物配置、奖励条件）通过 `UGameInstanceSubsystem`（任务管理器）在 Travel 前存储，任务地图 BeginPlay 时读取

**Seamless Travel 失效时的兜底——SaveGame：**
- 若因引擎限制或特殊需求必须用 OpenLevel → PlayerState 会被销毁
- 在 Travel 前将 PlayerState 数据（背包/仓库/装备状态）序列化到 SaveGame 对象
- 新关卡 BeginPlay → 从 SaveGame 恢复 → 重建 PlayerState 状态
- `InstanceID`（FGuid）在 SaveGame 中保留，确保装备实例一致性

> **当前阶段：** Seamless Travel 是优先方案（符合"ASC 放 PlayerState"的架构优势）。若调试/开发期间用 OpenLevel 更方便，则搭配 SaveGame 兜底。两种方式在组件接口层无差异——Backpack/Warehouse/Equipment 的读写接口不变。

**任务参数传递——GameInstanceSubsystem：**
- 创建 `UMHGZQuestManager`（GameInstanceSubsystem），存储当前任务配置
- 任务配置包含：目标地图名、天气枚举、怪物 ID 列表与分布、采集点 ID 列表、时间限制、猫车次数限制、奖励表
- 据点接任务 → `QuestManager→SetPendingQuest(QuestConfig)` → Seamless Travel 到任务地图
- 任务地图 `GameMode::BeginPlay` → `QuestManager→GetPendingQuest()` → 按配置生成怪物/采集点/天气
