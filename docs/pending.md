# 待补充设计

> 以下条目在本方案中被引用但尚未完成架构设计，将在后续版本或独立文档中补充。

## 怪物系统

> 已独立设计，详见 [monster-system.md](monster-system.md)。当前仅实现木桩（Training Dummy）。完整怪物（AI / 攻击 / 部位破坏 / 愤怒）在后续版本补充。

## GameplayCue 配置

> 已独立设计，详见 [gameplay-cue.md](gameplay-cue.md)。自定义 `UMHGZGameplayCueManager` + `UMHGZCue_HitBase`（Burst）/ `UMHGZCue_BuffBase`（Latent）分类基类 + `UMHGZDamageNumberPool`（WorldSubsystem）对象池。

## 碰撞通道 DefaultEngine.ini 配置

```ini
[/Script/Engine.CollisionProfile]
+DefaultChannelResponses=(Channel=ECC_GameTraceChannel1, Name="Weapon", DefaultResponse=ECR_Ignore)
+DefaultChannelResponses=(Channel=ECC_GameTraceChannel2, Name="MonsterAttack", DefaultResponse=ECR_Ignore)
+Profiles=(Name="Pawn", ... Weapon=ECR_Block, MonsterAttack=ECR_Block, ...)
```

## 死亡/复活——已设计

HP=0 → ExecCalc 广播 `Combat.Event.Death` → **GA_Death** 激活（GAS 自动 Cancel 所有其他 GA）→ 播放死亡 Montage。**猫车=同关卡内 `SetActorLocation(CampLocation)`**（非 Seamless Travel）→ 移除 Dead Tag → 手动设为 Grounded + Sheathed → 回满 HP/Stamina。Character 不销毁，PlayerState 不重建。

## 存档系统

- ItemInstance / EquipmentInstance 的完整序列化方案
- 仓库数据持久化（跨关卡 PlayerState 序列化）
- 装备状态（Equipped/Socketed）的存档恢复

## UI 系统架构

- HUD 主框架（WBP_HUD）
- 战斗 UI（血条、耐力条、武器资源条、Buff/Debuff 图标、锁定准星）
- 背包/仓库 UI（WBP_BackpackPanel、WBP_WarehousePanel）
- 快捷栏 UI（WBP_QuickBar）
- **装备状态界面（独立于仓库）**：数据源为 `EquipmentComponent::EquippedItems` + `EquipmentInstance::SocketedAccessories`
- Tag 驱动更新机制（`RegisterGameplayTagEvent` 订阅）

## 音效管理

- MetaSound 还是传统 SoundCue？
- 音效分类（BGM / SFX / 环境音 / UI 音效）
- GameplayCue 与 AnimNotify 的音效职责划分

## 任务与关卡管理

- **UMHGZQuestManager**（GameInstanceSubsystem）：管理任务接取/完成/失败状态、猫车计数、任务奖励结算
- **任务配置 DataTable**：每行定义一个任务（目标地图、天气、怪物 ID 列表与分布点位、采集点、时间限制、猫车上限、报酬）
- **关卡切换流程**：据点 UI 选任务 → QuestManager 存储任务配置 → Seamless Travel 到任务地图 → GameMode::BeginPlay 读取配置 → 任务完成/失败/放弃 → Travel 回据点 → QuestManager 结算
- **Seamless Travel 配置**：`bUseSeamlessTravel=true`、Transition Map 设置
