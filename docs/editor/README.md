# UE5 编辑器操作文档

本目录只说明代码完成后需要在 Unreal Editor 5.6 中进行的操作，包括资产迁移、蓝图/DataAsset 接线、Montage/碰撞/UI 配置和人工验收。这里不是 C++ 架构或玩法规则的真相源。

## 使用顺序

1. 完成并编译 [设计目录](../design/README.md) 中对应里程碑的代码。
2. 按 [UE5.6 编辑器接线指南](demo-setup.md) 从 E0 开始操作。
3. 按 [验证方案](verification.md) 执行当前 Demo 验收。
4. 只有需要移动或重存资产时，读取 [资产目录整理规范](asset-organization.md)。

## 文档

| 文档 | 用途 |
|---|---|
| [demo-setup.md](demo-setup.md) | 重构代码完成后的完整编辑器操作顺序 |
| [locomotion-refactor-setup.md](locomotion-refactor-setup.md) | L0～L5 普通移动重构的资产预处理、AnimBP/Distance Matching 接线与 PIE 验收 |
| [verification.md](verification.md) | PIE、生命周期、伤害、输入和打包验收 |
| [asset-organization.md](asset-organization.md) | Content Browser/AssetTools 移动、Redirector 和引用保护 |

## 动画混合：彻底关闭的调试清单

> **用途：** 本节用于定位“究竟是哪个动画层在做姿势插值”的短期调试，不是推荐的最终表现配置。关闭 Motion Matching 的混合会产生硬切；关闭上半身 Layer 会让送虫/收虫动画完全不可见，但 GA 的 Commit 仍会执行。

### 0. 先理解什么能关、什么不能关

| 项目 | 是否是姿势混合 | 关闭方式 | 关闭后的含义 |
|---|:---:|---|---|
| Montage Blend In / Blend Out | 是 | Blend Time 设为 `0` | 该 Montage 开始/结束时不再渐变权重 |
| `Blend Poses by Bool` | 是 | True/False Blend Time 设为 `0` | 收刀/持刀图分支瞬时切换 |
| Motion Matching 候选切换 | 是 | MM 的 Blend Time=0，关闭 Inertial Blend，Max Active Blends=0 | MM 换候选时硬切 |
| `Layered Blend per Bone` | 只有权重混合，不自带时间 | 令 Layer Alpha=0 或绕过节点 | 整个上半身 Slot 层不可见，不是“只关闭渐变” |
| Montage Section / Notify / Pose Search 轨迹采样 | 否 | 不适用 | 不能用它们关闭混合 |
| `Enable Auto Blend Out` | 否，只控制何时结束 | **不要**为了关混合而关闭 | 尤其 Dodge 需要正常结束并释放状态 |

在开始前记录每个改动前的数值。不要批量保存后再试图凭记忆恢复；逐个资产调试、逐个保存或使用 Source Control/备份。

### 1. 所有动作 Montage：关闭 Blend In / Blend Out

对每个正在测试的 `AM_*` Montage：双击资产，右侧 **Asset Details → Blend Settings**。

1. 在 **Blend In** 中设置：
   - `Blend Time = 0.0`
   - `Blend Profile = None`
   - `Custom Curve = None`
2. 在 **Blend Out** 中设置：
   - `Blend Time = 0.0`
   - `Blend Profile = None`
   - `Custom Curve = None`
3. `Blend Option` 在 Time 为 0 时不生效，可保留 Linear。
4. **不要**为了这一项关闭 `Enable Auto Blend Out`。前向 Dodge 的运行时也会强制开启它；关闭后 GA 可能不结束、输入可能被堵住。
5. `Blend Out Trigger Time` 决定何时开始退出，不是混合开关。排查姿势插值时先保留原值；只有需要确认“是否过早退出”时才单独测试它。

当前项目中应逐个检查的最终 Montage 包括：

- `AM_UnSh_Dodge`
- `AM_IG_Dodge_Forward`、`AM_IG_Dodge_Left`、`AM_IG_Dodge_Right`、`AM_IG_Dodge_Back`
- `AM_IG_BaDao`、`AM_IG_BaDao_CaoChong`、`AM_IG_DrawAndSendKinsect`
- `AM_IG_SendKinsect`、`AM_IG_RecallKinsect`
- `AM_IG_ShouDao`、`AM_IG_TuCi`

Montage Section 没有独立的 Crossfade 时间。若 `DodgeCore`、`IdleExit`、`MoveExit` 或其他 Section 之间看起来不连贯，应检查动作姿势、专用衔接序列或 MM 交接；不能在 Section 面板补一个 Blend Time。

### 2. `ABP_MH_Character`：关闭收刀/持刀 Bool 图分支的混合

1. 打开 `ABP_MH_Character` → **AnimGraph**。
2. 找到 `Blend Poses by Bool`（当前用于收刀/持刀图分支，布尔值为 `bUnsheathed`）。
3. 选中节点，在 Details 或展开的节点引脚中设置：
   - `True Blend Time = 0.0`
   - `False Blend Time = 0.0`
   - `Transition Type = Standard Blend`，不要使用 Inertialization。
   - `Blend Profile = None`；若节点显示 True/False 独立 Profile，两边都清空。
4. Compile、Save，并在 PIE 中执行一次拔刀、收刀，确认没有额外的模型抽搐。

本项目此前已经验证该节点的 True/False Blend Time 设为 0 可消除收刀/拔刀末尾的模型抽搐。因此它应作为当前基线，不应用它来修复走路、起步或停步问题。

### 3. `ABP_MH_Character`：关闭 Motion Matching 候选之间的混合

1. 在同一 AnimGraph 选中每个 `Motion Matching` 节点（收刀/持刀数据库都检查）。
2. 在 **Settings** 中设置：
   - `Blend Time = 0.0`
   - `Blend Profile = None`
   - `Blend Option` 无影响，可保留 Linear。
   - `Use Inertial Blend = false`
   - 在其 Blend Stack/高级设置中将 `Max Active Blends = 0`。
3. 若该节点后面有人为添加 `Inertialization` 节点，临时绕过或删除该节点；当前“完全关闭”测试不应保留它。
4. Compile、Save、PIE。此时 MM 在重新选帧时会硬切，这是预期诊断现象。

下列字段**不是**混合开关，禁止为了“关混合”而改成 0：

- `PoseJumpThresholdTime`
- `PoseReselectHistory`
- `SearchThrottleTime`
- Pose Search Schema 的 Trajectory/Pose 通道权重、采样时间点
- `PlayRate` / `PlayRateMultiplier`

它们决定 MM 何时、从哪个候选中选择，不决定两个姿势如何混合。此前走路抽搐、误选停步的根因属于这一组和轨迹输入，留给 `../design/locomotion-refactor.md` 的单独重构处理。

### 4. Slot 与 `Layered Blend per Bone`：区分“关闭渐变”和“隐藏整层”

- `DefaultSlot`、`UpperBody_IGAction` Slot 节点**没有独立 Blend Time**。播放它们的 Montage 的 Blend In/Out 才控制 Slot 权重变化。
- `Layered Blend per Bone` 也没有独立的时间参数；它只根据 Alpha/Blend Weight 和 Branch Filter 混合骨骼。
- 若只想使送虫/收虫立即显示或消失：保留 Layer 节点，按第 1 节把对应 `AM_IG_SendKinsect` / `AM_IG_RecallKinsect` 的 Montage Blend Time 设为 0。
- 若要排查该层是否导致问题：把 `Layered Blend per Bone` 的上半身 Alpha 设为 `0`，或临时把 Base Pose 直连输出。此操作会隐藏送虫/收虫表现，但不会阻止其 GA 的 Commit、送虫或收虫逻辑；仅用于隔离问题。
- 不要把 `UpperBody_IGAction` 接到 `DefaultSlot`，也不要把 Branch Filter 下移到 `pelvis` 或 `root` 来“关混合”；那会重新让上半身动作抢占下半身移动。

### 5. 将来若重新加入状态机

当前保存的 `ABP_MH_Character` 主移动链是 Motion Matching，不依赖传统 Idle/Start/Stop State Machine。若后续另建状态机，每条 Transition 还要单独检查：

1. 双击 Transition Arrow。
2. `Crossfade Duration = 0.0`。
3. `Blend Mode = Standard`；不要选 Inertialization 或 Custom Blend Graph。
4. 清空 Transition 的 Blend Profile/Custom Curve（若存在）。

这类 Transition 不会影响当前 MM 主链，除非该状态机被接入最终 Output Pose。

### 6. 运行时 C++ / GAS 的边界

当前 `UMHGZAttackAbility`、`UMHGZDodgeAbility`、`UMHGZSheatheAbility`、送虫和收虫 GA 都通过 `UAbilityTask_PlayMontageAndWait` 播放 Montage，只传入播放速率和起始 Section；没有可在各 GA 蓝图 Defaults 中填写的 Blend Override。因此编辑器中主要应调 Montage 资产本身。

底层 GAS 确实支持 `MontageStop(OverrideBlendOutTime)`，但项目没有把它开放为通用蓝图开关。不要为了视觉调试在 GA Event Graph 手写 `Montage Stop` 或手工 Cancel Ability；那会绕过 ActionToken、Commit 和清理流程。

### 7. 推荐的排查顺序与恢复

1. 先只将发生问题的一个 Montage 的 Blend In/Out 都设为 0，PIE 复现。
2. 若问题仍在，确认 Bool 图分支的两个 Blend Time 都为 0。
3. 仍在时，临时按第 3 节关闭 MM 候选混合，判断问题是“插值”还是“MM 选错帧”。
4. 送虫/收虫特有问题再按第 4 节隐藏上半身层，判断是否由 Layer 覆盖造成。
5. 找到根因后恢复非问题层的设置；不要把 MM 全部硬切作为最终方案。

对于目前“动作结束后误接停步/走路抽搐”的已知问题，第 3 节只能帮助确认现象，不能替代移动系统重构。最终修复应调整轨迹生成、Pose Search 数据库覆盖和动作→MM 的交接合同，而不是永久关闭所有混合。

## 边界

- 字段或父类与指南不一致时停止操作，回到设计文档和 C++ 检查；不要在蓝图中猜测替代字段。
- 不在 Windows 资源管理器中移动、复制或覆盖 `.uasset/.umap`。
- 不用蓝图 Tick 复制 C++ 已负责的连招、资源、位移、猎虫和生命周期逻辑。
- 编辑器操作产生的新设计决策必须先写回 `../design/`，再继续制作资产。
