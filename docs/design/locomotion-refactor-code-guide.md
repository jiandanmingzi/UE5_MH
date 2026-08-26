# 普通移动重构：代码侧设计与 AI 实施指南

> **状态：历史备选，禁止按本文开工。** 2026-08-25 已改用 [纯 Motion Matching 普通移动实施指南](pure-motion-matching-locomotion-guide.md)；本文只保留 CMC + 状态机回退方案。除非用户明确撤销纯 MM 决策，否则 AI 不得执行下面的 L0～L5 代码任务。
>
> **编辑器配套文档：** [普通移动重构：UE5.6 编辑器操作指南](../editor/locomotion-refactor-setup.md)。任何阶段都按“本阶段代码完成并编译 → 执行本阶段编辑器接线 → 通过本阶段验收”的顺序推进，不允许先一次性写完 L2～L5 再统一接线。

## 1. 任务合同

### 1.1 目标

普通地面移动改为以下单一主链路：

```text
Enhanced Input
  -> AMHGZCharacter 保存原始输入
  -> 计算目标速度、步态和动作所有权
  -> UCharacterMovementComponent 提交普通移动
  -> AnimBP 读取 CMC 实际速度和确定性事件
  -> Idle / Start / Loop / Stop 状态机只负责表现
  -> UpperBody_IGAction / DefaultSlot 叠加动作
```

完成 L5 后，普通 Walk/Run/Sprint 的胶囊位移只允许由 CMC 写入；攻击、翻滚、收刀、拔刀和特殊招式继续由现有 Montage Root Motion、RootMotionSource 或 MovementTask 写入。

### 1.2 必须保持的合同

- `Combat.State.BlockMovement` 继续是普通移动提交的硬门禁。
- `RawMoveInput` 与 `LastMovementInputDir` 在移动被锁时仍要更新，供输入快照、动作方向、Dodge `MoveExit` 和移动收刀转向使用。
- `UMHGZWeaponRuntimeHostComponent::IsMontageRootMotionOwned()` 继续表示动作正在拥有 Montage 根位移；普通 CMC 位移不得与其叠加。
- ActionToken、Montage Instance 路由、Commit Notify、Dodge 分支、收刀分支和 GA 清理流程不因移动重构改变。
- 送虫/收虫不取得 `BlockMovement`，也不取得 Montage Root Motion 所有权，因此普通 CMC 移动必须继续运行。
- `MoveSpeedMultiplier` 来自 `UMHGZAttributeSet`，不得从虫棍资源参数或 Widget 状态反向推导。
- `Root Motion Mode = Root Motion From Montages Only`；普通 locomotion 序列不得贡献胶囊 Root Motion。
- 不建立永久的 `bUseLegacyMotionMatching`、旧/新数据库切换或两套位移同时运行的兼容架构。

### 1.3 明确不做

- 不重写攻击、猎虫、精华、伤害、Combo、输入组合键或 UI。
- 不把所有动作位移改成 CMC，也不把普通移动改成 GAS Ability。
- 不实现锁定视角横移、完整 360 度 locomotion、Pivot、脚部 IK、坡度 Warping 或网络专项优化。
- 不在 C++ 中硬编码虫棍资产路径。
- 不用 `StopMovementImmediately()` 每帧压制移动；若某动作确实需要清惯性，只能在取得对应所有权的边沿执行一次，并由该动作策略明确提出。

## 2. 当前代码事实与改动边界

当前普通移动仍集中在 `AMHGZCharacter`：

- `DoMove` 已保存 `RawMoveInput`、`LastMovementInputDir`、`InputMagnitude` 与 `TargetCruiseSpeed`，但尚未调用 `AddMovementInput`。
- `Tick` 仍维护 Motion Matching 所需的 `DesiredSpeed`、`bForceMMIdle` 和手动朝向。
- CMC 当前主要作为碰撞壳，`MaxWalkSpeed=1200` 是旧 Root Motion 上限，不是目标巡航速度。
- `UMHGZAttributeSet::MoveSpeedMultiplier` 已存在、复制并有合法范围，但普通移动尚未把它乘入目标速度。
- `MHGZ.Build.cs` 仍依赖 `PoseSearch` 与 `MotionTrajectory`；L5 前不得提前移除。

### 2.1 推荐文件所有权

| 文件 | L 阶段职责 |
|---|---|
| `Source/MHGZ/MHGZCharacter.h/.cpp` | 输入采样、CMC 提交、朝向、动作所有权边沿、AnimBP 快照 |
| `Source/MHGZ/Movement/MHGZLocomotionTypes.h` | 新增 Phase/Gait/快照等纯数据类型 |
| `Source/MHGZ/Movement/MHGZLocomotionStateMachine.h/.cpp` | 可单测的确定性事件与动作交接规则；不得访问 Content 资产 |
| `Source/MHGZ/Movement/Tests/MHGZLocomotionTests.cpp` | 速度、步态、门禁和交接自动化 |
| `Source/MHGZ/MHGZ.Build.cs` | 插件运行时依赖和 L5 旧依赖清理 |
| `MHGZ.uproject` | 启用 `AnimationLocomotionLibrary` 插件 |

不要新建一个会自行 Tick、复制状态并再次读取输入的 ActorComponent。移动输入、CMC 与 Character 已有生命周期一致；本次只把可测试的纯规则从 Character 中抽出，避免第二个运行时所有者。

## 3. 冻结的代码接口

### 3.1 类型

在 `Movement/MHGZLocomotionTypes.h` 定义：

```cpp
UENUM(BlueprintType)
enum class EMHGZLocomotionPhase : uint8
{
    Idle,
    Starting,
    Looping,
    Stopping
};

UENUM(BlueprintType)
enum class EMHGZLocomotionGait : uint8
{
    Walk,
    Run,
    Sprint
};
```

Stance 不并入 Gait。当前继续使用 `bUnsheathed` 选择收刀/持刀资产集；以后若改成枚举，应单独提交设计变更。

新增只读快照，供 AnimBP Property Access 读取。字段名可以按项目编码风格微调，但语义不得改变：

```cpp
USTRUCT(BlueprintType)
struct FMHGZLocomotionSnapshot
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly) FVector Velocity = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly) FVector Acceleration = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly) FVector LastUpdateVelocity = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly) FVector LastInputWorldDirection = FVector::ForwardVector;
    UPROPERTY(BlueprintReadOnly) FVector2D RawMoveInput = FVector2D::ZeroVector;

    UPROPERTY(BlueprintReadOnly) float Speed2D = 0.0f;
    UPROPERTY(BlueprintReadOnly) float InputMagnitude = 0.0f;
    UPROPERTY(BlueprintReadOnly) float TargetCruiseSpeed = 0.0f;
    UPROPERTY(BlueprintReadOnly) float MoveSpeedMultiplier = 1.0f;

    UPROPERTY(BlueprintReadOnly) EMHGZLocomotionGait TargetGait = EMHGZLocomotionGait::Walk;

    UPROPERTY(BlueprintReadOnly) bool bHasRawInput = false;
    UPROPERTY(BlueprintReadOnly) bool bHasLocomotionInput = false;
    UPROPERTY(BlueprintReadOnly) bool bUnsheathed = false;
    UPROPERTY(BlueprintReadOnly) bool bGrounded = true;
    UPROPERTY(BlueprintReadOnly) bool bMovementBlocked = false;
    UPROPERTY(BlueprintReadOnly) bool bActionOwnsTranslation = false;
    UPROPERTY(BlueprintReadOnly) bool bStandardLocomotionEnabled = true;

    // 仅在产生边沿的一个游戏帧为 true。
    UPROPERTY(BlueprintReadOnly) bool bMoveInputPressed = false;
    UPROPERTY(BlueprintReadOnly) bool bMoveInputReleased = false;
    UPROPERTY(BlueprintReadOnly) bool bRequestStart = false;
    UPROPERTY(BlueprintReadOnly) bool bRequestStop = false;
    UPROPERTY(BlueprintReadOnly) bool bForceLocomotionIdle = false;
};
```

不要把 `PredictedStopLocation` 在 Character Tick 中复制一套。UE5.6 的 `Predict Ground Movement Stop Location` 是线程安全 AnimBP 节点，直接使用 CMC 的 `Velocity`、`bUseSeparateBrakingFriction`、`BrakingFriction`、`GroundFriction`、`BrakingFrictionFactor` 和 `BrakingDecelerationWalking`，可保证预测参数与实际 CMC 一致。

### 3.2 Character 对外接口

`AMHGZCharacter` 至少提供一个可被 Property Access 读取的只读属性和一个原生引用 Getter：

```cpp
UPROPERTY(BlueprintReadOnly, Category="Movement|Locomotion")
FMHGZLocomotionSnapshot LocomotionSnapshot;

const FMHGZLocomotionSnapshot& GetLocomotionSnapshot() const
{
    return LocomotionSnapshot;
}
```

如果确实需要从普通蓝图函数节点取完整快照，再额外提供一个按值返回的 `BlueprintPure` 函数；不要把 C++ `const&` 返回直接暴露成依赖临时引用生命周期的蓝图接口。

保留现有 `GetRawMoveInput()`、`HasRawMovementInput()`、`GetLastMovementInputDir()` 和 `ShouldBlockMovement()`，避免动作系统被迫迁移。L5 前还要保留 `DesiredSpeed`、`bForceMMIdle`，但只能供旧 AnimBP 临时读取，禁止新状态机引用。

### 3.3 纯状态规则

`FMHGZLocomotionStateMachine` 只处理事件和历史，不播放动画：

```cpp
struct FMHGZLocomotionFrameInput
{
    bool bHasRawInput = false;
    bool bMovementBlocked = false;
    bool bActionOwnsTranslation = false;
    bool bGrounded = true;
    bool bCMCIsMoving = false;
    EMHGZLocomotionGait DesiredGait = EMHGZLocomotionGait::Walk;
};

struct FMHGZLocomotionFrameEvents
{
    bool bMoveInputPressed = false;
    bool bMoveInputReleased = false;
    bool bRequestStart = false;
    bool bRequestStop = false;
    bool bForceIdle = false;
    EMHGZLocomotionGait FrozenEntryGait = EMHGZLocomotionGait::Walk;
};
```

规则按优先级执行：

1. `bMovementBlocked || bActionOwnsTranslation || !bGrounded` 时，普通 locomotion 关闭，取消待处理 Stop，不从动作速度生成 Stop。
2. 动作所有权从 `false -> true` 时，清除普通移动历史中的 Pending Stop。
3. 动作所有权从 `true -> false` 时：有原始输入则发出 `bRequestStart`，无原始输入则发出 `bForceIdle`；绝不发出 `bRequestStop`。
4. 只有“上一帧是普通 locomotion、上一帧有输入、本帧输入释放、当前无动作位移所有权”才发出 `bRequestStop`。
5. 无动作情况下输入从无到有，发出 `bRequestStart`，并冻结这一轮入口 Gait。
6. Walk/Run/Sprint 在 Starting 内的轻微阈值抖动不重启 Start；最新 Gait 在进入 Looping 时生效。

AnimBP 的 `EMHGZLocomotionPhase` 负责动画状态；C++ 状态规则负责证明“何时允许进入 Start/Stop”。不要让 Character 根据动画剩余时间猜测 Phase，也不要让 AnimBP 根据一次 `Speed2D > 0 -> 0` 自行生成 Stop。

## 4. 每帧执行顺序

### 4.1 输入采样

`DoMove(Right, Forward)` 每次被 Enhanced Input 调用时：

1. 先无条件写入 `RawMoveInput`。
2. 有 Controller 时按控制器 Yaw 计算世界空间方向；非零输入才更新 `LastMovementInputDir`。
3. 使用 `FMath::Clamp(RawMoveInput.Size(), 0.0f, 1.0f)` 得到原始幅度。
4. 记录本帧输入已更新。输入 `Completed` 必须走同一路径并传入 `(0, 0)`，不能只依靠 Tick 超时猜测松开。
5. 不在 `DoMove` 中平滑 `DesiredSpeed`，也不从动画速度反推输入。

### 4.2 目标速度与步态

沿用现有 `CalcCruiseSpeed` 的手感映射，并明确：

```cpp
const float BaseCruiseSpeed = CalcCruiseSpeed(InputMagnitude);
const float AttributeMultiplier = AttributeSet
    ? AttributeSet->GetMoveSpeedMultiplier()
    : 1.0f;

TargetCruiseSpeed = BaseCruiseSpeed * AttributeMultiplier;
CMC->MaxWalkSpeed = FMath::Max(0.0f, TargetCruiseSpeed);
```

`MoveSpeedMultiplier` 建议在 ASC/AttributeSet 就绪后缓存指针并读取当前值；不得每帧通过 GameplayEffect 列表查找来源。若后续需要立即响应属性变化，可注册属性变更委托更新缓存，但 L2 不强制引入第二套速度所有者。

目标 Gait 由现有阈值映射：

- 收刀，死区至 `0.5`：Walk。
- 收刀，`0.5` 至 `0.9`：Run。
- 收刀，输入大于 `0.9` 且按住 Sprint：Sprint；否则 Run。
- 持刀：固定映射为 Run，但使用持刀资产集。

若 `MoveSpeedMultiplier` 改变，只改变最终速度，不改变摇杆阈值对应的 Gait。

### 4.3 位移提交

冻结实现如下：

```cpp
const bool bActionOwnsTranslation =
    (RuntimeHost && RuntimeHost->IsMontageRootMotionOwned())
    || CMC->HasRootMotionSources();

const bool bCanSubmitStandardMovement =
    bGrounded
    && !ShouldBlockMovement()
    && !bActionOwnsTranslation;

if (bCanSubmitStandardMovement && bHasRawInput)
{
    CMC->MaxWalkSpeed = TargetCruiseSpeed;
    AddMovementInput(LastMovementInputDir, 1.0f);
}
```

`CalcCruiseSpeed` 已把摇杆幅度映射到 `TargetCruiseSpeed`，所以 `AddMovementInput` 的 Scale 固定为 `1.0`；禁止再乘 `InputMagnitude`，否则会发生双重模拟量缩放。

动作持有 Montage Root Motion 或 CMC 中存在活跃 RootMotionSource 时停止普通 CMC 位移提交，但继续记录原始输入。使用 MovementTask 且不创建 RootMotionSource 的当前攻击仍由其 `BlockMovement` 覆盖；以后若新增一个既不加锁、又直接写位移的 Task，必须先扩展统一的 Action Translation Owner 查询，不能让普通移动靠猜测速度识别。这样移动收刀、Dodge `MoveExit` 可继续读取和改变朝向，而位移仍只有一个所有者。

### 4.4 朝向

保留当前最短角差、`TurnRate` 限速逻辑，但把“是否允许普通转向”显式化：

- 普通 locomotion：有原始输入且没有动作旋转所有权时，朝 `LastMovementInputDir` 转向。
- `BlockMovement` 锁定动作：不得由普通 locomotion 抢写 Yaw。
- Steering Root Motion 阶段：由现有动作合同决定是否允许读取实时摇杆转向；普通系统只提供输入，不重写动作角度策略。
- AttackAbility 的起手一次性角度修正保持原样，不并入普通移动转向。

当前 RuntimeHost 只有 Montage 根位移所有权查询。实施 L2 时若代码中不存在通用“动作旋转所有权”查询，不得自行假设 `IsMontageRootMotionOwned()` 等价于旋转锁；应继续调用现有动作/Character 的明确门禁，或先以不改变当前 Tick 转向行为为兼容基线，并把补充接口作为独立小改动和测试提交。

### 4.5 快照更新时间

推荐在 Character `Tick` 中按以下顺序执行：

1. 从 ASC 刷新 `bUnsheathed` 与 `MoveSpeedMultiplier`。
2. 读取 `ShouldBlockMovement()`、RuntimeHost 根位移所有权和 CMC Grounded。
3. 处理本帧未收到输入回调的兜底，但输入 `Completed` 仍是正常释放路径。
4. 计算 TargetCruiseSpeed/TargetGait。
5. 更新纯状态规则，生成一帧事件。
6. 向 CMC 提交普通移动。
7. 更新普通朝向。
8. 从 CMC 写入 `FMHGZLocomotionSnapshot`。

事件字段在下一帧计算前先清零，保证只脉冲一帧；连续输入不得每帧重复发 `bRequestStart`。

## 5. 分阶段实施

### L0：冻结与基线

代码任务：

- 不改生产行为。
- 记录当前构建与 `Automation RunTests MHGZ` 结果。
- 使用日志或录屏固定以下复现：持续移动重选单脚、错误 Stop、第一次拔刀后 Stop、动作退出衔接。
- 不再调整 PSS Schema、Trajectory 采样点和 Pose Search 权重。

退出条件：现有故障可稳定复现，E4-A 非移动动作仍可运行，工作树有可回退的阶段快照。

### L1：资产预处理

代码任务：

- 在 `MHGZ.uproject` 启用 `AnimationLocomotionLibrary`。
- 若 Distance Matching、停止预测和 PlayRate 节点全部只在 AnimBP 使用，`MHGZ.Build.cs` 暂时不必加入插件模块。
- 只有 C++ 直接包含 `AnimDistanceMatchingLibrary.h` 或 `AnimCharacterMovementLibrary.h` 时，才向依赖中加入 `AnimationLocomotionLibraryRuntime`；禁止加入仅编辑器模块 `AnimationLocomotionLibraryEditor`。
- 不修改 Character 位移行为。

退出条件：插件启用后 Development Editor 构建成功；编辑器可找到 `DistanceCurveModifier` 和 Distance Matching 节点。

### L2：CMC 普通移动纵切

代码任务：

1. 新增 Locomotion 类型和纯状态规则骨架；这一阶段只使用 Idle/Loop 所需事件。
2. `DoMove` 接入 CMC，应用 `MoveSpeedMultiplier`，取消 `DesiredSpeed` 对胶囊速度的任何影响。
3. 将 `MaxWalkSpeed=1200` 的旧“RM 上限”语义改为当前目标巡航速度；CMC 的碰撞、台阶、斜坡仍用引擎默认路径。
4. 暂保留 `DesiredSpeed`、`bForceMMIdle` 与旧 Trajectory 字段，使尚未删除的蓝图引用能编译，但新状态机禁止读取。
5. 加入以下自动化：
   - 输入幅度只影响 TargetCruiseSpeed，不二次缩放 `AddMovementInput`。
   - `MoveSpeedMultiplier` 正确乘入目标速度。
   - BlockMovement、Montage Root Motion Owner 和活跃 RootMotionSource 均阻止普通位移提交，但不清除 RawMoveInput。
   - 动作结束无输入不发 Stop；有输入发 Start。

编辑器接线只接 Idle/Loop。L2 不实现 Start/Stop，避免同时调试位移所有权与 Distance Matching。

退出条件：

- 走、跑、冲刺的胶囊立即响应输入，并正确碰撞、上斜坡和转向。
- 不再由普通序列产生胶囊 Root Motion；无双位移和 Mesh 回弹。
- 翻滚、移动收刀、拔刀攻击等现有 Montage Root Motion 位移不回归。
- 第一次拔刀不会触发持刀 Stop，因为 L2 根本没有 Stop 入口。
- 送虫/收虫时可继续移动。

### L3：确定性 Start/Stop

代码任务：

1. 完成 `FMHGZLocomotionStateMachine` 的输入边沿、动作进入/退出和 Stop 授权规则。
2. 快照输出 `bRequestStart`、`bRequestStop`、`bForceLocomotionIdle` 与冻结入口 Gait。
3. 不根据 Montage 尾帧速度、`LastUpdateVelocity` 瞬时下降或 AnimBP 当前姿势生成 Stop。
4. 为以下情况增加自动化：
   - Idle 输入边沿只发一次 Start。
   - Loop/Start 中真实输入释放才发 Stop。
   - 动作开始取消 Pending Stop。
   - 动作结束无输入强制 Idle，有输入请求 Start。
   - Stopping 中重新输入请求 Start，且不等待 Stop 动画自然结束。
   - Starting 内 Gait 抖动不重复进入 Start。

AnimBP 负责计算 Start 每个动画更新步内的胶囊实际平面位移；`Advance Time By Distance Matching` 会在当前 Evaluator 时间上逐步累计该增量。AnimBP 同时使用 CMC 参数预测停止距离；C++ 不直接控制 Sequence Evaluator 时间。

退出条件：连续重复 20 次“起步—松开—停步—再起步”无误触、无等待动画才移动；连续重复 20 次动作进入/退出均不把动作根位移识别成普通 Stop。

### L4：步态、同步和表现

代码任务：

- 完成 Walk/Run/Sprint 与持刀固定步态映射。
- 将 CMC `MaxAcceleration`、`GroundFriction`、`BrakingDecelerationWalking`、`bUseSeparateBrakingFriction`、`BrakingFriction`、`BrakingFrictionFactor` 暴露为角色蓝图可调默认值；不要硬编码 PIE 调参结果到 Tick。
- 保证停止预测读取同一 CMC 实例的同一组参数。
- 真正移除根平移的 in-place Loop 已无法让 UE 的 `Set Playrate To Match Speed` 从资产 Root Motion 自动计算原始速度；这类资产使用 `PlayRate = Speed2D / AuthoredLoopSpeed`，其中 `AuthoredLoopSpeed` 在 L1 从保留根位移的源序列测量并记录。只有运行时序列仍保留可提取 Root Motion、且已证明不会让 Mesh 漂移时，才可直接使用该节点。
- 若加入 Stride Warping，参数属于 AnimBP 表现，不得反馈修改 CMC 速度。
- 增加阈值边界和 Sprint Hold 自动化。

退出条件：各步态目标速度稳定，跨步态不反复重启 Start，脚相同步和 PlayRate 在编辑器手测中通过。

### L5：删除旧路径

只能在 L2～L4 全部通过后执行：

1. 从 `ABP_MH_Character` 删除 Motion Matching、Pose History 和 Trajectory 主链引用后，再删除 Character 中的：
   - `DesiredSpeed`
   - `DesiredSpeedInterpSpeed`
   - `bForceMMIdle`
   - `LastTheoryUpdateFrame`
   - 仅为手工 `FTransformTrajectory` 服务的字段、函数和包含
2. 使用 `rg` 确认 Source、Config 和 docs 现行章节不再把普通移动描述成“AnimBP Root Motion 驱动”。
3. 若全项目不再使用 `PoseSearch`、`MotionTrajectory`，从 `MHGZ.Build.cs` 和 `MHGZ.uproject` 移除对应依赖/插件；若其他非普通移动功能仍引用，则保留并记录引用证据。
4. 保留 `MotionWarping`，因为动作系统仍可能使用；不得因清理普通 MM 顺手删除。
5. 将 `motion-matching.md` 标记为历史实现，并把动作交接术语统一为：

```text
LockedRootMotion -> SteeringRootMotion -> CharacterLocomotion
```

6. 运行完整 MHGZ 自动化、Development Editor 构建、目标资产 Data Validation 和最终 PIE 回归。

退出条件：不存在永久旧/新开关；普通移动只有 CMC 位移主链；旧字段无蓝图残留引用；所有动作合同和移动验收通过。

## 6. 自动化测试矩阵

| 测试 | 级别 | 核心断言 |
|---|---|---|
| `MHGZ.Locomotion.SpeedMapping` | C++ | 死区、Walk/Run/Sprint/持刀边界正确 |
| `MHGZ.Locomotion.MoveSpeedMultiplier` | C++ | 属性倍率只乘一次且应用到 MaxWalkSpeed |
| `MHGZ.Locomotion.BlockPreservesRawInput` | C++ | Block 时无提交，但 Raw 与方向仍更新 |
| `MHGZ.Locomotion.RootMotionOwnership` | C++ | Montage Owner 存在时无普通 CMC 位移提交 |
| `MHGZ.Locomotion.InputEdges` | C++ | Press/Release 每次只脉冲一帧 |
| `MHGZ.Locomotion.ActionHandoff` | C++ | 动作退出不生成 Stop；按输入进入 Start/Idle |
| `MHGZ.Locomotion.GaitFreeze` | C++ | Starting 内阈值抖动不重启 Start |
| 普通碰撞/斜坡/台阶 | PIE | 胶囊位置与 Mesh 无分离 |
| Montage Root Motion 回归 | PIE | Dodge、Sheathe、Draw、攻击位移不叠加/不丢失 |
| 上半身操虫移动 | PIE | Send/Recall 动作可见且下半身继续 CMC locomotion |

纯状态测试不得创建真实 AnimBP 或依赖 Content；资产时间、Marker、曲线和脚相由编辑器验证。

## 7. 每阶段验证命令

关闭 Unreal Editor 后执行 Development Editor 构建：

```powershell
& 'C:\apps\UE5\UE_5.6\Engine\Build\BatchFiles\Build.bat' MHGZEditor Win64 Development 'D:\study\MH\MHGZ\MHGZ.uproject' -WaitMutex
```

启动编辑器后运行：

```text
Automation RunTests MHGZ
```

提交前至少检查：

```powershell
rg -n "DesiredSpeed|bForceMMIdle|FTransformTrajectory|Motion Matching|Pose History" Source Config docs
```

L2～L4 阶段允许旧字段仍有旧蓝图兼容引用；L5 必须逐条解释或清零。不要因为文本搜索仍命中历史文档就删除历史证据，应把历史文档明确标记为“已取代”。

## 8. AI 执行规则

AI 每次只执行一个 L 阶段，并在动手前输出以下契约：

1. 本阶段进入条件是否满足。
2. 将修改的代码文件与不会修改的系统。
3. 本阶段唯一位移所有者。
4. 自动化、构建和 PIE 需要分别证明什么。

完成后必须报告实际 diff、实际测试结果和未验证的编辑器项。出现以下任一情况时停止继续写下一阶段：

- 蓝图所需字段尚未编译出现。
- 旧 Motion Matching 与新状态机同时向最终 Output Pose 提供普通移动。
- 普通 CMC 与 Montage Root Motion 同时被当成动作阶段的位移所有者。
- Distance Curve、Sync Marker 或 in-place 资产尚未准备好。
- 既有 MHGZ 自动化回归失败。

这正是 L2、L3、L4 必须分开接线验证的原因：L2 先证明位移所有权，L3 再证明确定性启停，L4 最后处理脚相与表现。把全部代码一次性写完会让“胶囊位移错误、状态授权错误、曲线错误、动画混合错误”无法被独立定位。
