# GAS 基础设施

> **实施状态说明：** M1/M2 已完成 ASC/AttributeSet 挂载、`InitAbilityActorInfo`、输入/动作 Token、装备 Snapshot 和 Pawn RuntimeHost 生命周期。背包、仓库仍是桩组件；武器 Resource 现由 Character 上的 RuntimeHost 根据 `WeaponDefinition → RuntimeDefinition` 动态创建，不再读取装备 DataTable。Seamless Travel、SaveGame 与 QuestManager 均为后续方案。

> **Demo 冻结目标：** Weapon Resource 已迁移到 Pawn RuntimeHost 归属；换武器、死亡、UnPossess 与 EndPlay 统一走固定 Shutdown 顺序。虫棍 Resource 内部的精华/猎虫最终状态机仍由 M3 完成，接口和顺序见 [Demo 实施计划 §3.4](demo-implementation-plan.md#34-武器资源宿主与清理顺序)。

> 以下三节为 GAS 初始化必须明确的架构决策，是先决条件而非可选设计。

## ASC 挂载位置——PlayerState

`UMHGZAbilitySystemComponent` 挂载到 **AMHGZPlayerState**（非 Character）。

| 理由 | 说明 |
|------|------|
| 跨 Character 生命周期 | PlayerState 适合保存 ASC 身份；Pawn 重建后必须重新 `InitAbilityActorInfo`，不能假设全部运行时对象引用仍有效 |
| 官方推荐模式 | Epic 的 Lyra、ShooterGame 等官方项目均将 ASC 放在 PlayerState |
| 数据与运行时分层 | Equipment/Inventory 可在 PlayerState；当前武器 Resource、猎虫、舞踏和位移引用属于 Pawn/WeaponRuntimeHost；输入路由属于 PlayerController |
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

## 组件归属

> Demo 不追求把组件集中到同一 Actor，而按生命周期归属：持久角色数据在 PlayerState，世界/动画运行时在 Character，输入在 PlayerController。

| 组件 | 挂载位置 | 理由 |
|------|----------|------|
| `UMHGZAbilitySystemComponent` | PlayerState | GAS 核心 |
| `UMHGZAttributeSet` | PlayerState（ASC 子对象） | 属性生命周期同 ASC |
| `UMHGZEquipmentComponent` | PlayerState | 装备 GE 管理，依赖 ASC |
| `UMHGZBackpackComponent` | PlayerState | 跨关卡保留物品 |
| `UMHGZWarehouseComponent` | PlayerState | 仓库跨关卡 |
| `UMHGZWeaponRuntimeHostComponent` | Character | 只在 WeaponSnapshot 身份变化时创建/销毁当前 Resource；统一清理猎虫、粉尘、舞踏、Action/Notify Registry、位移/Warp 和 Pawn 引用 |
| `UMHGZWeaponResourceComponent` | Character 的 RuntimeHost（动态组件） | 武器运行时资源；通过接口访问 PlayerState ASC，不把世界 Actor 引用放入持久层 |
| `UMHGZQuickBarComponent` | PlayerController | 快捷栏——输入选择+音效反馈+使用触发。需访问 Backpack（通过 `GetPlayerState()` 一次跳转获取）、ASC Tag 查询（持刀态/受击/攻击中判断能否使用）。放在 PlayerController 使输入→反馈链路最短 |
| `UMHGZEdgeVaultComponent` | Character | 需要 CMC 访问（Velocity/边缘检测） |
| `UMHGZInputComponent` | PlayerController | IMC 与 Enhanced Input Binding 唯一所有者；保存 Handle 并支持重复 Possess 幂等重绑 |
| `UMHGZWeaponInputRouterComponent` | PlayerController | 原始 Action、Chord、方向、Aim 上下文和释放 SequenceID；输出不可变 InputSnapshot，不选择具体虫棍 GA |
| `UMHGZHitStopControllerComponent` | Character | 以 Token 合并可叠加卡肉请求，取消/死亡/换装时按所有权释放，不让 Ability 直接覆盖 CustomTimeDilation |
| `UMHGZHitFeedbackRouterComponent` | 可受击 Actor/Character | 接收已结算 HitFeedbackResult，显式执行 GameplayCue、伤害数字和表现请求，不重算伤害 |
| `UMotionWarpingComponent` | Character | UE5 内置，动画驱动。需 SkeletalMeshComponent+AnimBP 管线（PlayerState 不具备）——构造函数 `CreateDefaultSubobject` 随 Character 创建。仅有真实目标/平移或旋转对齐需求的特殊 GA 才通过 `FindComponentByClass` 建立自己拥有的 Warp Target，并由 Montage 的 `AnimNotifyState_MotionWarping` 消费/在结束时清理；普通攻击的入口方向修正直接设置 Actor Yaw，不建立 Warp Target。 |

> 当前源码为了 `URes_InsectGlaive` 打开 PlayerState Tick；目标 Demo 把 Resource 移到 Character RuntimeHost，由 Character/Pawn 生命周期驱动 Tick。EquipmentComponent 分别广播 StatsChanged 与 WeaponSnapshot；RuntimeHost 只消费后者并比较身份。PlayerState 不应只为武器运行时永久开启 Tick。

## 关卡切换——Seamless Travel + SaveGame 兜底（规划，当前未实现）

**怪猎游玩模式：** 据点（接任务/工坊/吃饭）→ 选择任务 → 加载指定地图（天气/怪物分布/采集点由任务参数决定）→ 完成/失败/放弃 → 返回据点。玩家背包、装备、仓库在据点↔任务地图之间**全部保留**。

**UE5 关卡切换方式对比：**

| 方式 | PlayerState | GameInstance | 加载耗时 | 适用场景 |
|------|:--:|:--:|:--:|------|
| **OpenLevel** | ❌ 销毁重建 | ✅ 保留 | 长（全量加载） | 主菜单→游戏、完全独立的关卡 |
| **Seamless Travel** | 通过 GameMode 创建/复制 | ✅ 保留 | 中 | 需要 `CopyProperties/SeamlessTravelTo` 明确复制持久 DTO；组件和 UObject/Actor 引用不会自动完整保留 |
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
- GameMode/PlayerState 显式复制角色标识和持久 DTO；新 Pawn/RuntimeHost 重建 ASC ActorInfo、当前武器资源和 UI 绑定
- 任务参数（地图名、天气、怪物配置、奖励条件）通过 `UGameInstanceSubsystem`（任务管理器）在 Travel 前存储，任务地图 BeginPlay 时读取

**Seamless Travel 失效时的兜底——SaveGame：**
- 若因引擎限制或特殊需求必须用 OpenLevel → PlayerState 会被销毁
- 在 Travel 前将 PlayerState 数据（背包/仓库/装备状态）序列化到 SaveGame 对象
- 新关卡 BeginPlay → 从 SaveGame 恢复 → 重建 PlayerState 状态
- `InstanceID`（FGuid）在 SaveGame 中保留，确保装备实例一致性

> **规划状态：** Seamless Travel、SaveGame 兜底和以下 QuestManager 流程尚未实现；本节保留为后续架构方案。

**任务参数传递——GameInstanceSubsystem：**
- 创建 `UMHGZQuestManager`（GameInstanceSubsystem），存储当前任务配置
- 任务配置包含：目标地图名、天气枚举、怪物 ID 列表与分布、采集点 ID 列表、时间限制、猫车次数限制、奖励表
- 据点接任务 → `QuestManager→SetPendingQuest(QuestConfig)` → Seamless Travel 到任务地图
- 任务地图 `GameMode::BeginPlay` → `QuestManager→GetPendingQuest()` → 按配置生成怪物/采集点/天气
