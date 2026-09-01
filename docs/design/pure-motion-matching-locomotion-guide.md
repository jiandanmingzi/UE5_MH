# 参考 Game Animation Sample 的 MHGZ Motion Matching 改进设计

> **状态（2026-09-02）：M4.5 与 E4.2 已完成并签收；当前阶段为 M4.2.1 普通移动档位重搜 / Blend。** M4.2 普通移动 / Stop 固定矩阵、历史 Stop 生命周期专项、M4.3 输入释放补丁、M4.4 根运动交接、E4.2 Exit / ActionIdle 路由均已签收。阶段状态、唯一允许的下一阶段与完成证据以 [阶段门禁](milestone-gates.md) 为准；M4.2.1 通过后才进入 M4.6，M4.6 前不得创建 E4.3 的批量地面动作资产。

> **目的：** 在不放弃 MHGZ 的“前向 Root Motion + GAS 动作所有权 + 纯 Motion Matching 普通移动”路线的前提下，借鉴 UE5.6 Game Animation Sample（GASP）的**候选集组织**和**动作退出交接**方法，解决：
>
> 1. Walk / Run / Sprint 直接切 Loop 的观感断层；
> 2. 移动收刀、前向翻滚 MoveExit 等功能动作后半段如何自然回到普通移动；
> 3. 解包资源中大量“看似动作之间的衔接序列”应如何审计、分类和接入。

> **历史清理：** 旧版 PMM 编号的实施流水、废弃调参尝试和临时修复理由均已移除。已落地行为以代码、资产审计和 [阶段门禁](milestone-gates.md) 为事实源；本文件只定义当前基线、目标架构和后续改造合同。所有后续工作只使用 M4.x / E4.x 编号。

## 1. 证据范围与决策边界

### 1.1 已核对的参考工程

只读对照工程位于 D:\study\MH\游戏动画示例\游戏动画示例.uproject，EngineAssociation=5.6，与本项目一致。

已实际确认的组织事实包括：

- ABP_SandboxCharacter 和 CHT_PoseSearchDatabases；
- PSS_Default、PSS_Stop、PSS_Idle、PSS_Jump、PSS_Traversal；
- Stand Walk / Run / Sprint 分别拥有 Starts、Loops、Stops、Pivots、Lands 候选集合；
- 存在 PSD_Dense_Stand_Walk_FromTraversal、PSD_Dense_Stand_Run_FromTraversal、PSD_Traversal 一类“功能动作回到移动”的集合；
- 使用 Chooser / Gameplay Context 缩小候选集合，并在 MM 更新或选帧后消费结果。

这证明其核心方法是“**先由语境选择允许搜索的候选集合，再由 Motion Matching 在集合内选帧**”。它不证明某个具体权重、曲线数值或蓝图连线可直接复制；这些没有在二进制资产中可靠读取，实施前如有必要必须在编辑器中再次人工核对。详细只读对照记录见 [Game Animation Sample 对照附录](game-animation-sample-reference-comparison.md)。

### 1.2 本项目不照搬的部分

| GASP 能力 | MHGZ 决策 | 原因 |
|---|---|---|
| Dense / Sparse 双层数据库、Normalization Set | 当前不采用 | MHGZ 候选量很小，Brute Force 与资产审计更易验证。 |
| 360° Pivot、Strafe、Turn-in-place | 当前不采用 | 项目可用资源以正向动画为主，角色朝向由现有角色逻辑处理。 |
| 独立 PSS_Stop | 不采用 | Stop 必须与 Start / Loop 在同一姿势、轨迹和语义空间内竞争，不能拆开规避生命周期问题。 |
| Capsule 驱动 + Offset Root Bone | 不采用 | 普通移动与部分动作依赖真实 Root Motion；叠加 Root Offset 会破坏根位移、胶囊与武器表现的一致性。 |
| MM 决定攻击 / GA 入口 | 明确禁止 | 攻击入口必须由 GAS 输入快照、窗口、资源和 ActionToken 在播放前决定。MM 只能接管已经失去玩法语义的移动尾段。 |

## 2. 当前 MHGZ 基线：不得被后续改造破坏

### 2.1 普通移动数据流

~~~text
原始摇杆 / RB
  -> AMHGZCharacter::DoMove
  -> RawMoveInput、InputMagnitude、TargetCruiseSpeed、DesiredSpeed、角色朝向
  -> UMHGZMotionMatchingAnimInstance
  -> 预测 Trajectory + 曲线查询
  -> PSS_MH_Move
  -> PSD_MH_Shth_Move 或 PSD_MH_UnSh_Move
  -> Motion Matching 节点 / Pose History
  -> Root Motion 驱动的普通移动
~~~

DoMove 不调用 AddMovementInput。TargetCruiseSpeed 是“现有动画确实拥有的档位”的离散目标，不是胶囊移动速度：

| 姿态 | 现有目标档位 |
|---|---:|
| 收刀 Walk | 160 cm/s |
| 收刀 Run | 460 cm/s |
| 收刀 Sprint | 575 cm/s |
| 持刀移动 | 440 cm/s |

DesiredSpeed 与预测轨迹仅是 Pose Search 查询；实际位置变化由动画 Root Motion 产生。不能为了修饰观感重新启用 CMC 状态机或让 DesiredSpeed 伪造不存在的中间 Loop 速度。

### 2.2 当前六个 PSS 查询频道

PSS_MH_Move 是唯一正式普通移动 Schema。频道及职责如下：

| 频道 | 查询来源 | 语义 |
|---|---|---|
| Pose | Pose History | 当前身体姿势和脚相。 |
| Trajectory | 预测轨迹 | 预计的前向 Root Motion 距离。导入资源的前向轴为本地 +Y，因此必须使用完整 **Position** 三分量采样，不能退回 PositionXY。 |
| MM_Intent | MMIntentQuery | Start 为正、巡航/Idle 为零、Stop 为负。它不是“当前速度”。 |
| MM_DistanceToStop | MMDistanceToStopQuery | Stop 的剩余前向距离，负值收敛到零；Idle / Loop / 非 Stop 为零。 |
| MM_StopGait | MMStopGaitQuery | 松杆时锁存的 Stop Family，防止 Run / Sprint 被 Walk Stop 抢走。 |
| MM_MoveGait | MMMoveGaitQuery | 当前目标移动 Family：Walk=1/3、Run=2/3、Sprint=1，无输入为零。 |

现有 Start / Stop 的 Block Transition、Continuing、逐候选 SamplingRange 与 Generated Stop 的 60Hz 零值尾段，属于已经验收的 Stop 生命周期合同。新候选不得改写、简化或通过关闭 BlockTransition 绕开它。

### 2.3 位移所有权合同

~~~text
普通移动：Motion Matching Root Motion 是唯一位移来源
功能 Montage：MontageRootMotionOwner 是唯一位移来源
BlockMovement：限制普通 locomotion 输入，不等于 Root Motion 所有权
bForceMMIdle：每帧由 BlockMovement 或 MontageRootMotionOwner 重算
~~~

同一帧不能由普通 MM 和 Montage 同时贡献根位移；不能靠持续写入 bForceMMIdle、长时间输入死区或强制提前 Idle 掩盖交接错误。

### 2.4 已实现与未实现的边界

| 范围 | 当前状态 |
|---|---|
| 输入边沿 Start、一次性 Stop 请求、左右脚 Stop、Stop 曲线跟随 | 已实现并经历史 Stop 生命周期专项 PIE 验收。 |
| 收刀 / 持刀两套 MM 节点、同一 PSS、Root-Motion 普通移动 | 已实现。 |
| Walk / Run / Sprint 的真实挡位间桥接动画 | 未实现。当前可能直接从一个 Loop 选到目标 Loop。 |
| Montage 无功能尾段交给 MM 的通用接口 | M4.4 已实现并签收；E4.2 已自动完成三条既有路径的 Sequence/Notify/GA opt-in。 |
| ExitTransition 专用候选库、Exit PSD 路由 | 已完成；三个专用 PSD、原生 PreSearch 路由与即时 Exit→Stop 交接均已通过 M4.5 PIE。 |

## 3. 目标架构：三个候选语境，而不是一个万能数据库

GASP 的价值不在于“PSD 越多越好”，而在于每种问题都进入正确的候选语境。

~~~text
                    ┌──────────────────────────────┐
                    │ Base Locomotion                │
                    │ Idle / Start / Loop / Stop     │
                    │ 当前 PSD_MH_*_Move             │
                    └──────────────┬───────────────┘
                                   │
          ┌────────────────────────┼─────────────────────────┐
          ▼                        ▼                         ▼
┌─────────────────┐     ┌────────────────────┐   ┌────────────────────┐
│ 短 Blend         │     │ ExitTransition PSD │   │ Direct MM Handoff  │
│ Walk↔Run↔Sprint │     │ 动作尾段的正式路径  │   │ 已验证的简化路径   │
└─────────────────┘     └────────────────────┘   └────────────────────┘
~~~

1. **Base Locomotion**：现有 Idle、Start、Loop、Stop，是当前系统的稳定基础；不能因一个新过渡问题而拆成状态机。
2. **短 Blend**：解决普通移动内部的 Walk / Run / Sprint 改挡。MM 仍先按 Pose、Trajectory、MM_MoveGait 选择目标 Loop；只对已发生的选帧切换做短时姿势混合，不新增动画状态机。
3. **Action Exit**：解决收刀、翻滚等功能动作结束后回到普通移动。为每类支持的动作退出准备无功能 ExitTransition 和小型 PSD；直接交给 Base Locomotion 只是一条经过专项验证后才允许采用的简化路径。

Chooser 是未来候选集路由层：它读取 Gameplay Context，返回一个或多个允许搜索的 PSD；Motion Matching 仍在返回集合内依据 Pose、Trajectory 和查询曲线选择具体动画帧。候选库扩展为多个明确语境后，Chooser 应路由下列集合：Base Locomotion、GaitTransition、ActionExit、Airborne / Landing。它只能缩小候选集，绝不直接指定某条动画或替代 Pose Search 的选帧。

## 4. 挡位切换：优先使用短 Blend，桥接动画按需补充

### 4.1 根因

MM_MoveGait 会在输入阈值或 RB 状态改变时立即变为目标 Family。这是正确的长期约束：它防止角色持续停在 Walk / Run / Sprint 的错误循环中。但档位变化还必须产生一次明确的“允许重新搜索”边沿；仅改变查询值不足以打破正在持续播放的源 Loop。候选库只有各自的 Start / Loop / Stop 时，MM 只能在“源 Loop”和“目标 Loop”之间选择，姿势、脚相或根速度可能不连续。

没有现成桥接资产时，首选方案是让 MM 依据 Pose、Trajectory、脚相和 MM_MoveGait 立即重搜目标 Loop，再以短 Blend 隐藏切帧。短 Blend 只改善源 Pose 到目标 Pose 的视觉接缝；它不自行触发重搜，也不改变 Root Motion 所有权。

### 4.2 首选方案：Phase / Pose 匹配后的短 Blend

初始调试合同如下，具体数值以固定矩阵和 Telemetry 为准：

| Motion Matching 节点设置 | 初始值 | 目的 |
|---|---:|---|
| Blend Time | 0.06 s（约 4 帧@60Hz） | 消除 Loop 切帧，而不把档位响应拖成长过渡。 |
| Use Inertial Blend | false | 先使用可直接观察的普通姿势混合，避免惯性化掩盖选择问题。 |
| Max Active Blends | 1 | 禁止频繁重选时叠加多个 Blend。 |
| 允许调试范围 | 0.04～0.10 s | 只在同一输入矩阵下比较；超过 0.10 s 视为正在掩盖缺少过渡资产。 |

三个节点参数是一组原子配置：启用 `Blend Time=0.06 s` 时，必须同时保持 `Use Inertial Blend=false` 与 `Max Active Blends=1`。不得只调整 Blend Time。

### 4.2.1 档位改变的一次性重搜合同

在收刀普通移动中，目标移动 Family 在有效输入期间发生变化（包括 Run→Sprint、Sprint→Run，以及后续经审计允许的 Walk↔Run）时，系统必须产生一次性 `GaitChange` 边沿：

~~~text
输入 / RB 改变目标档位
  -> 同帧更新 TargetCruiseSpeed 与 MMMoveGaitQuery
  -> 记录一次 GaitChangeSerial
  -> 仅对当前普通移动 MM 节点撤销源 Loop 的 Continuing 优先权
  -> 以新的 MMMoveGaitQuery 立即进行一次新搜
  -> MM 按当前 Pose、脚相和 Trajectory 选中目标 Family 的最佳帧
  -> 0.06 s / Max Active Blends=1 的短 Blend 负责视觉接缝
  -> 新结果恢复普通 Continuing 生命周期
~~~

该边沿只能在目标 Family 实际改变时消费一次，不能每帧强制重搜。它不重置 Stop 生命周期、不修改 Root Motion 所有权，也不以 `DesiredSpeed` 作为档位选择依据。

Start 的首个落脚承诺段保持不可打断；通过该承诺段后，仍在持续输入时的档位改变必须遵守同一 `GaitChange` 合同，不能等待整个 Start 序列自然结束。

执行顺序固定为：

1. 保持当前 PSS、Start / Stop 曲线和候选集合不变；
2. 实现并验证一次性 `GaitChange` 重搜合同；
3. 将两个普通移动 Motion Matching 节点同时调整到上述 Blend 合同；
4. 在 Walk↔Run、Run↔Sprint、各自松杆、RB 运动中按下/松开、多个脚相下录制 Telemetry；
5. 确认目标 Loop 由 MM 正确选中，且 Blend 结束后没有脚滑、Root 速度突变或重复 Start；
6. 只有步骤 5 仍失败时，才把该特定挡位纳入桥接动画资产审计。

Blend 不是速度系统：Root Motion 在 Blend 期间只是在两个已存在动画结果间平滑过渡，不能产生作者制作的加速步态。它可作为无资源时的正式表现策略，但不能用长 Blend 代替正确的候选选择或 Stop 生命周期。

### 4.3 第二级方案：桥接动画的语义

仅当第 4.2 节的短 Blend 在固定输入矩阵中仍有可复现问题，并且审计确认存在合适资源后，才按下列逻辑类型接入：

| 逻辑类型 | 来源姿态 | 目标姿态 | 典型命名 |
|---|---|---|---|
| 升挡过渡 | Walk Loop | Run Loop | AS_Shth_WalkToRun |
| 降挡过渡 | Run Loop | Walk Loop | AS_Shth_RunToWalk |
| Sprint 升挡 | Run Loop | Sprint Loop | AS_Shth_RunToSprint |
| Sprint 降挡 | Sprint Loop | Run Loop | AS_Shth_SprintToRun |
| 持刀过渡 | 仅在资产确实存在且语义完整时加入 | 同上 | AS_UnSh_*To* |

每条过渡资产必须满足：

1. 开头姿势和脚相接近**源** Loop，结尾能自然接入**目标** Loop；
2. Root Motion 保持完整、EnableRootMotion=true、ForceRootLock=false，不能拿 in-place 片段伪装位移过渡；
3. 无攻击、收刀、拔刀、猎虫、窗口、碰撞等功能 Notify；
4. 播放倍率与正式 Loop 一致。需要 1.25x Sprint 表现时，应使用已烘焙的正确倍率资产，不能在 MM 节点上临时改变全局 Play Rate；
5. 只在正确的收刀 / 持刀 PSD 中注册，不能把收刀资产混入持刀库，反之亦然。

### 4.4 桥接动画的曲线与查询合同

挡位过渡不是一次新的“起步”或“停步”。在持续推杆的 Walk→Run / Run→Walk 中：

- MM_Intent=0；
- MM_DistanceToStop=0；
- MM_StopGait=0；
- MM_MoveGait 从**第一可搜索帧**起就标为**目标** Family。

例如 WalkToRun 的 MM_MoveGait 从第一可搜索帧起即为 2/3；当前实际姿势和轨迹自然仍像 Walk 的末段，故 Pose / Trajectory 成本会把它与其他 Run 候选区分开。RunToWalk 则从第一可搜索帧起标为 1/3。源 Family 不另做曲线：源状态由实际 Pose、当前实际速度和预测轨迹表达。

这保证输入一旦要求 Run，过渡资产不会被高权重 MM_MoveGait 直接排除；同时它也不会在已经巡航时反复当作 Start 选中。若完成首批资产和成本复盘后，Base Loop 仍系统性击败正确过渡资产，才允许提出明确命名的 MM_GaitTransition 查询频道。新增频道前必须先给出 Telemetry / PoseSearch Cost 证据，不能靠观感猜测添加。

### 4.5 允许中断的规则

- **持续推杆**：过渡可被目标 Loop 自然承接，不应强迫过渡播完。
- **在过渡中松杆**：应产生一次标准 Stop 请求，按当时实际姿势、预测轨迹和锁存 Family 选择 Stop；不得复制一个“过渡专用 Stop 状态机”。
- **再次改挡**：允许 MM 依成本选择更接近当前姿势的候选；不得硬锁原过渡资产直到播完。

如果桥接动画中立即松杆的可视质量不足，优先补一条真正的 TransitionToStop 资产；不要用加长输入死区、强制 Idle 或截断 Stop 来修饰。

## 5. 功能动作退出：优先使用 ExitTransition，直接交接仅作简化路径

### 5.1 不存在“运行时把 Montage 放进 PSD”

Pose Search 索引是在编辑器构建的。运行时只能选择已索引的数据库 / 资产，不能把一个正在播放的 Montage Section 临时插入 PSD 后立即参与同一搜索。

~~~text
编辑器：裁出或烘焙无功能尾段 -> 审计 Root / Notify -> 建立索引
运行时：GA 在安全帧释放 Montage 所有权 -> MM 搜索已索引候选
~~~

### 5.2 正式优先路径：ExitTransition PSD

移动收刀和前向翻滚 MoveExit 的正式设计是：将 Montage 在所有功能 Commit 后、但尚未自然接入普通 Loop 前的无功能尾段裁为独立 ExitTransition，并在 Handoff 后先让 MM 搜索该退出候选集合。这样 Root 速度、脚相和姿势由该尾段自然消化，而不是要求 Base Loop 直接承担动作末帧。

计划路径为：

~~~text
Content/.../Locomotion/Transitions/Exit/
  AS_Shth_Sheathe_MoveExit_*
  AS_UnSh_Dodge_Forward_MoveExit_*
~~~

对应的未来数据库为 PSD_MH_Shth_Exit、PSD_MH_UnSh_Exit。它们与 PSS_MH_Move 使用同一六频道和采样率；不创建 PSS_Exit。M4.4 中的 Chooser 先按姿态和 Handoff 类型路由至相应 Exit PSD；ExitTransition 进入正常可搜索尾段后，Chooser 再返回该 Exit PSD 与当前姿态的 Move PSD，让 MM 依 Pose、Trajectory 和输入自然转入 Loop 或 Stop。

ExitTransition 必须完整保留源动画的 Root Track：包括首帧平移（其可能是贴地高度）、旋转/缩放坐标基和后续真实 Root Motion。不得把该项目源骨骼的任何 Root 分量强制设为 UE world identity 或归零；Root Motion 在运行时按帧间差量提取，不依赖这种重基。本项目导入资产的 `RateScale = 2` 必须改为两倍采样率的真实烘焙，供 Pose Search 以 `RateScale = 1` 播放。其中不得保留 AttackCollision、Combo / Dodge Window、Draw / Sheathe / Kinsect Commit、GameplayCue 或任何会再次改变玩法状态的 Notify。

**直接交给现有 Move PSD（仅作已验证的简化路径）**

只有在某个退出尾段与 Base Locomotion 的起始姿势、脚相、Root 速度及输入/松杆结果均已通过专项矩阵时，才允许省略对应 ExitTransition。它不是默认做法，也不能因“后半段看起来像 Loop”跳过资产审计。

### 5.3 M4.4 中的 MMHandoff 合同

通用交接接口在 M4.4 的 Root Motion Phase / Handoff 基础设施中统一实现，不为收刀、翻滚、拔刀或突刺建立平行路径。攻击的 Entry Section 选择留给 M4.5 与 M4.1 最终验收之后的 M4.6；它不是修复当前动作退出的前置。

~~~text
AnimNotify_MotionMatchingHandoff
  -> 以 (Mesh, MontageInstanceID) 解析当前 ActionToken
  -> GA 验证 Token、Section、Commit 和可交接 Root Motion 阶段
  -> 记录当前 RawMoveInput、姿态、是否曾移动后松杆
  -> 结束 / BlendOut 原 Montage 的无功能部分
  -> 精确释放该 ActionToken 的 MontageRootMotionOwner
  -> 下一次 MM 更新恢复搜索
~~~

接受 Handoff 的前提必须同时满足：

1. ActionToken 仍有效且属于当前 GA；
2. 当前动作显式声明此 Section 可交接；
3. 所有 gameplay Commit 已完成；
4. Handoff 帧及之后不再由 Montage 贡献根位移；
5. 没有其他 Action / RootMotionSource 已取得位移所有权。

Notify 不得直接修改 Character、Loose Tag 或 MM 节点。任一校验失败都保持原 Montage 路径，不能为了消除卡顿提前释放根位移所有权。

### 5.4 Handoff 时的摇杆语义

动作可移动阶段中的实际原始摇杆必须独立于 BlockMovement 记录。Handoff 时：

| 状态 | 正确行为 |
|---|---|
| 仍有有效摇杆 | 先搜索本次 Handoff 对应的 ExitTransition；它进入可交接尾段后再搜索目标 Family 的 Loop。不得重放 Start。 |
| 在动作可移动尾段中曾推杆、之后松杆 | 先通过 ExitTransition 消化动作尾段；其安全交接点消费一次 PendingStopAtMMHandoff，由 PSS 决定具体 Stop。 |
| 全程没有有效移动输入 | 先通过必要的 ExitTransition；其末段回 Idle，不得由 Montage 尾帧伪造 Stop。 |

PendingStopAtMMHandoff 只属于允许移动的收刀 / MoveExit 类动作；站立收刀、Dodge Core、持刀左右后翻滚、硬直和死亡不得创建它。

## 6. 解包“衔接动作”资产审计

未知序列不是自动可用的候选。每一条必须先进入清单，完成自动测量和人工视觉确认后再决定归属。

### 6.1 分类结果

| 分类 | 定义 | 可以进入普通 MM 吗 |
|---|---|---|
| BaseStart / BaseLoop / BaseStop | 完整普通移动资产 | 可以，遵循现有曲线 / Notify 合同。 |
| GaitTransition | 两个普通移动档位之间的无功能桥接 | 可以，遵循第 4 节。 |
| ActionExitCandidate | 功能动作完成后的无功能尾段 | 第 5 节正式优先路径：裁出独立 ExitTransition 并进入对应 Exit PSD。 |
| FunctionalMontageSection | 有伤害、Commit、窗口、资源、Root Motion Gameplay Phase 的招式段 | 不可以；保留在 Montage / GA。 |
| DuplicateOrVariant | 同一动作的倍率、镜像、空片段或不匹配骨架变体 | 不可以，除非审计重新证明独立价值。 |
| Unknown | 语义和去向无法确认 | 不可以；不得因“看起来像移动”直接加进 PSD。 |

### 6.2 审计表的最小字段

每条候选必须记录在后续新增的资产审计表中；表未建立前不接入。

| 字段 | 自动获取 | 人工确认 |
|---|---:|---:|
| 资产路径、Skeleton、时长、播放倍率 | 是 | 否 |
| Root Motion 开关、总位移、逐帧速度曲线、首末帧 Root Transform | 是 | 否 |
| 动画曲线、PoseSearchControl Notify、普通 / Gameplay Notify | 是 | 是 |
| 首帧接近的源资产 / 帧、末帧接近的目标资产 / 帧 | 可计算候选 | 必须确认视觉与脚相 |
| 姿态、步态、脚相、是否有功能语义 | 否 | 是 |
| 最终分类、目标 PSD、SamplingRange、接入理由 | 否 | 是 |

自动化可以输出“姿势最相似帧、根位移和 Notify”来缩小人工工作量，但不能凭名称或姿势距离自动宣称动作可用。尤其是解包序列中，前后摇、衔接、攻击段和 Root Motion 片段常会被拆分且命名不完整。

### 6.3 接入前的强制检查

1. 打开序列，以实际播放倍率检查首末帧与源 / 目标动作；
2. 检查 Root Motion 是否连续，且导入轴仍符合本项目本地 +Y 前向约定；
3. 检查所有 Notify。若残留任何 gameplay Notify，则不得作为 MM 候选；
4. 写入六条语义曲线并设置逐候选 SamplingRange；ExitTransition 还必须标明所属 Handoff 类型和其向 Base Move 交接的安全区；
5. 重建索引，运行现有 PMM 资产审计，再以 Telemetry 和 Pose Search Debugger 验证真实选帧；
6. 未满足完整固定矩阵前，立即从 PSD 移除候选并保留审计记录，不能让未知资产长期混在正式库中。

### 6.4 空中、落地与功能性空中动作

未来扩展采用高层语境路由与 MM / GA 分层所有权：`Grounded`、`InAir`、`Traversal`、`Action`、`HitReact` 是高层语境；Walk / Run / Sprint 仍由 Grounded 普通移动 MM 管理，不建立档位状态机。

普通跌落、普通下落和无功能落地的位移与碰撞由 CharacterMovement / 物理权威处理。`MovementMode`、垂直与水平速度、有效输入、离地前档位和预测落点共同决定 Airborne / Landing 候选集；Chooser 返回相应 PSD 后，MM 在集合中选取具体 Fall、Land 或 Recovery 姿势。真实 `OnLanded` 事件是普通落地语义的唯一提交点。

带伤害或其他玩法语义的空中动作必须由 GA + Montage + ActionToken 持有：空中攻击、下落突刺、落地冲击、受击击飞与硬直落地的 Root Motion、碰撞、命中回调、资源消耗和 Commit 均保持在功能动作时间轴中。运行时落地通过 CharacterMovement 事件通知该 GA；需要落地攻击时，由 GA 决定并播放/进入其功能性落地段。

只有功能 Commit 已完成、后续没有伤害/资源/状态/Gameplay Notify 的尾段，才可裁为 `ActionExitCandidate` 或 `LandingExitCandidate` 并经 Handoff 交给 MM。此类候选按第 6.3 节审计后进入 ActionExit 或 Airborne / Landing PSD；MM 只负责无功能恢复、落地后接移动或 Stop 的选帧。

~~~text
普通跌落：Grounded -> InAir PSD/MM -> OnLanded -> Landing PSD/MM -> Grounded Move
空中攻击：GA Montage（功能阶段） -> OnLanded 通知 GA -> 功能落地段（如需要）
          -> 无功能 LandingExit Handoff -> Landing / Base MM
受击击飞：HitReact GA / 受控位移 -> OnLanded -> 受击落地 / 起身 GA
          -> 无功能恢复尾段 Handoff -> MM
~~~
## 7. 已裁定的实施顺序与门禁

旧门禁曾要求“普通移动先验证动作退出”同时又禁止实现动作退出候选语境，形成循环依赖。最新 Telemetry 已证明该前提不成立：功能动作释放后，查询语义为零时全量 Move PSD 仍可能让 Loop 的 Pose 成本击败 Idle。现将流程固定为以下运行时/编辑器工作包，任何后续文档不得再把动作退出并入 M4.2。

### M4.2：普通移动 / Stop 固定矩阵

已完成；其验收范围仅为不依赖功能 GA 退出的普通移动：

1. 收刀与拔刀 Idle；
2. 有效输入下的 Start / Loop，以及 Walk→Run、Run→Sprint 的正常换挡；
3. 真实松杆后的 Stop、左右脚 Stop、Stop 曲线连续与一次性生命周期；
4. 首次 PIE 进入普通移动；
5. 持续推杆时各挡位是否重放 Start、脚滑或直接换 Loop 的观感记录。

它**不**验证收刀、翻滚、拔刀、突刺等 GA 结束后怎么回到普通移动；这些项移入 M4.5。若 M4.2 失败，只能修复基础查询、基础资产或 Stop 合同，不能借动作退出分支掩盖。

### M4.3：输入释放基础设施

M4.2 通过后，先完成并独立验收 `OnReleaseIfUnconsumed`。它仍只改通用 Router/Chord 身份，不能绑定具体虫棍 GA、不能创建地面攻击资产、也不能改变 M4.1 的现有输入语义。字段、状态机、测试和禁止项以 [M4.3 详细设计与实施](m4.3-input-release-implementation.md) 为准。其存在的原因是后续虫印斩输入，而不是当前 Exit 问题；把它排在 M4.4 前只是维持输入基础设施从通用到具体的顺序。

### M4.4：通用 MMHandoff / Exit 基础设施

具体的源文件、原子 Root Motion 所有权顺序、AnimBP 公开变量、Telemetry 字段和已通过自动化见 [M4.4 动作退出与根运动交接：详细设计及实施记录](m4.4-action-handoff-implementation.md)。M4.4 的 C++ 合同完成后，才由 E4.2 创建任何 Exit PSD、Chooser 或 Handoff Notify 资产接线。

M4.2 与 M4.3 通过后，实施一个**唯一的、可复用的**动作退出纵切：

1. 原生 `AnimNotify_MotionMatchingHandoff` 以 `(Mesh, MontageInstanceID)` 解析当前 ActionToken；不扫全局 Active Ability。
2. 通用 Root Motion Phase / Handoff 逻辑只在功能 Commit 已完成、之后已无 Root Motion 或玩法 Notify 的安全帧，精确释放本 Token 的 Root Motion owner。
3. 记录 `HandoffType`、真实 RawMoveInput、是否在可移动阶段发生过输入下降沿，以及 Telemetry 所需的 Token、Montage、候选库和选帧信息。
4. 引入 `PSD_MH_Shth_Exit`、`PSD_MH_UnSh_Exit` 和最小 Chooser 路由；它们沿用 `PSS_MH_Move`，不建 PSS_Exit / PSS_Stop，不让 Chooser 直接指定动画。
5. 失败时保持原 Montage 路径和所有权，不提前释放，不以 `bMMForceIdle` 时间保持兜底。

本阶段只供 E4.2 的既存动作使用，不实现新的地面连段，不做攻击 Entry Section 映射，不进行 E4.3 批量接线。

### E4.2：最小动作退出资产

只有 M4.4 编译且自动化通过后，才能在编辑器处理四类已存在且有证据的路径：

1. 移动收刀 Walk；
2. 前向翻滚 `MoveExit`；
3. 当前 Telemetry 已证明直接全库交接失败的既存拔刀路径；
4. 当前 Telemetry 已证明直接全库交接失败的既存突刺路径。

每条路径先审计所有 Root Motion、曲线和 Notify，再裁出或确认对应的无功能 ExitTransition，接入 Handoff Notify、Exit PSD/Chooser、索引和资产审计。不存在合格无功能尾段时，必须停在审计结论，不能把完整 GA Montage 加入 PSD。

### M4.5：动作退出固定矩阵与 M4.1 最终验收

每条 E4.2 路径分别测试：

1. 持续推杆：ExitTransition 后进入正确 Loop，不重放 Start；
2. Handoff 前后松杆：必要尾段播完后恰好一次正常 Stop；
3. 全程无输入：ExitTransition 后自然 Idle；绝不由姿势成本把无意图角色送进 Loop；
4. Montage 中断、Token 过期、Notify 错放：不释放其他动作的 Root Motion owner；
5. 每帧只有 Montage 或 MM 之一贡献根位移。

通过后才签收 M4.1 最终 PIE。此时“功能动作退出”不再是未验证的直接全库交接，而是有资产、候选语境和 Telemetry 的正式合同。

### M4.2.1：普通移动档位重搜 / Blend

M4.5 已通过。M4.2.1 是独立的普通移动修正切片：先实现第 4.2.1 节的一次性 `GaitChange` 重搜合同，再以完整的短 Blend 原子配置验证 Walk↔Run、Run↔Sprint、RB 按下/松开与多个脚相。它不修改攻击 Entry Section、Combo、Stop 生命周期或动作退出所有权。

### M4.6 / E4.3 / M4.7：攻击入口与后续扩展

M4.2.1 通过后，再实现 M4.6 的攻击 Entry Section 播放前选择、E4.3 的批量动作接线和 M4.7 的地面招式/虫印。

## 8. 验收与诊断标准

### 8.1 挡位过渡

每一条 GaitTransition 至少验证：

1. 源 Loop 的多个脚相均能自然短 Blend 到目标 Loop；
2. 摇杆持续保持时，能进入目标 Loop 且不重放 Start；
3. Blend 中松杆只触发一次正确 Family 的 Stop；
4. 改回源挡位或切 Sprint 时不叠加多个 Blend、不锁死到原序列末尾；
5. Root Motion 连续、角色朝向仍由现有转向逻辑控制；
6. Telemetry 中查询的 MM_MoveGait、实际选中候选和胜出成本一致，不能仅靠肉眼判断；
7. 只有短 Blend 验收失败的具体挡位，才追加桥接动画验收。

### 8.2 动作退出交接

移动收刀与前向翻滚 MoveExit 分别验证：

1. 持续推杆：Handoff 后先进入正确 ExitTransition，再自然进入正确 Loop，绝不重放 Start；
2. Handoff 前、后松杆：ExitTransition 完成必要的尾段后，都至多发生一次标准 Stop；
3. 无输入：ExitTransition 的末段自然回 Idle，不伪造 Stop；
4. Token 过期、Montage 中断、Notify 错放：不得释放其他动作的 Root Motion 所有权；
5. 记录中必须能看到 Montage Root Motion 所有权在 MM 恢复搜索前精确释放，且不存在双重根位移。

### 8.3 不可接受的“修复”

- 用超过 0.10 s 的长 Blend、输入死区或强制 Idle 时间遮住错误选择或缺少资产；
- 为了让某候选胜出而盲改 MM_Intent、MM_MoveGait、MM_StopGait 权重；
- 把完整 GA Montage 或带功能 Notify 的序列加进普通 Move PSD；
- 删除 BlockTransition、全库尾裁剪策略或 Generated Stop 生命周期保护；
- 以状态机直接播放某条 Walk / Run / Sprint 过渡动画，绕开 Pose Search；
- 在没有审计和回归证据时引入 GASP 的双数据库、全方向或 Root Offset 系统。

## 9. 现阶段结论

当前项目不需要、也不应当为了“参考官方案例”立即重构移动系统。现有的纯 MM 查询、Stop 生命周期和 Root Motion 所有权仍是后续设计的基础。

官方案例提供的正确升级路径是：先把真正不同语境的资产分清，再让 MM 在受控候选集内选择；对 MHGZ 而言，第一优先是以 Phase / Pose 匹配后的短 Blend 平滑挡位切换，必要时才补 GaitTransition；第二优先是在 MMHandoff 后由 ExitTransition 消化动作尾段，再交回普通移动。Exit PSD 与 Chooser 是该正式动作退出路径的组成部分，直接交接仅是专项验证后的简化选择。

M4.2～M4.5 与 E4.2 均已完成。当前唯一正确的工作是 M4.2.1；其通过后严格按第 7 节进入 M4.6。不得把动作退出塞回已签收的 M4.2，也不得跳过 M4.2.1 直接开始 M4.6。
