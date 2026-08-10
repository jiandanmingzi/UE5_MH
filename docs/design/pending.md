# 待补充设计

> 以下条目在本方案中被引用但尚未完成架构设计，将在后续版本或独立文档中补充。

## 怪物系统

> 已独立设计，详见 [monster-system.md](monster-system.md)。当前仅实现木桩（Training Dummy）。完整怪物（AI / 攻击 / 部位破坏 / 愤怒）在后续版本补充。

## GameplayCue 配置

> Demo 修订方案见 [gameplay-cue.md](gameplay-cue.md)：使用默认 GameplayCueManager，伤害结算后由 HitFeedbackRouter 显式执行一次性 Cue；持久 Buff 使用 Actor/Looping Notify；伤害数字池化拥有 WidgetComponent 的 Actor。上述类与资产均尚未实现。

## 碰撞通道 DefaultEngine.ini 配置——部分已实现

当前只有 Weapon/MonsterAttack 两条 Trace Channel。Demo M0 还需新增 Hitzone Object Channel；不新增 Kinsect Object Channel。

```ini
[/Script/Engine.CollisionProfile]
+DefaultChannelResponses=(Channel=ECC_GameTraceChannel1,DefaultResponse=ECR_Block,bTraceType=True,bStaticObject=False,Name="Weapon")
+DefaultChannelResponses=(Channel=ECC_GameTraceChannel2,DefaultResponse=ECR_Block,bTraceType=True,bStaticObject=False,Name="MonsterAttack")
+DefaultChannelResponses=(Channel=ECC_GameTraceChannel3,DefaultResponse=ECR_Ignore,bTraceType=False,bStaticObject=False,Name="Hitzone")
+Profiles=(Name="Pawn", ... Weapon=ECR_Block, MonsterAttack=ECR_Block, ...)
```

## 死亡/复活——已有方案，尚未实现

HP=0 → AttributeSet 结算层发送 `Combat.Event.Death` → **GA_Death** 显式取消其他 GA并调用 RuntimeHost Shutdown → 播放死亡 Montage。Demo 猫车可在同关卡复用 Character 并移动到 Camp，但复活时必须生成新 `FWeaponRuntimeToken{Host, Generation}` 并从 PlayerState 当前装备重新 Initialize RuntimeHost；不能继续使用死亡前的 Resource/Actor/Timer。完整游戏未来也可销毁并重建 Pawn，Host 变化同样使旧 Token 失效，PlayerState ASC 身份保持。

## 存档系统

- ItemInstance / EquipmentInstance 的完整序列化方案
- 仓库数据持久化（跨关卡 PlayerState 序列化）
- 装备状态（Equipped/Socketed）的存档恢复
- WeaponResource 词条的多来源句柄、目标参数过滤、叠加顺序与精确移除；Demo 禁止依赖当前 `ApplyEntryModifier/GetModifiedParam`

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
