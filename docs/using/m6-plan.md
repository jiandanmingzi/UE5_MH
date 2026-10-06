# M6：觉虫击、粉尘、HUD 与视觉反馈实施计划（不含音效）

> **2026-10-06，计划态；未实施、未验收。** 本文依据当前源码、Content 和现行玩法文档编写，不把设计中的类当作已经存在。M5 已签收，不重开 M5；本轮只生成文档。
>
> **范围：** 觉虫击、通用粉尘、粉尘集约、健康/耐力/虫棍资源 HUD、准心、伤害数字及必要的视觉 Cue。**排除全部音效工作**：不提取音频，不制作/配置新 Sound，不编写音效选择或播放方案，不把音效作为本轮门禁。已有声音引用和播放代码保持原样，不能为排除音效清空必填字段。
>
> 代码/脚本工作统一用 `M6.x`，编辑器/资产接线及 PIE 工作统一用 `E6.x`。它们是主 M6 的子工作包，不是重新执行主 M1～M5。总体状态唯一入口仍为[阶段门禁](../design/milestone-gates.md)；具体编辑器步骤见[M6 编辑器指导](../editor/m6-setup.md)。

## 1. 任务合同与阅读顺序

### 1.1 真相源

| 内容 | 真相源与用途 |
|---|---|
| 当前实现 | `Source/MHGZ`、`Content`，以及实际构建/审计/PIE 结果；文档写有类名不等于类已经实现 |
| 当前玩法 | [虫棍动作 §5.3、§5.6、§6.1](../design/insect-glaive-actions.md)：粉尘集约、觉虫击、舞踏来源 |
| 通用所有权 | [冻结实施计划](../design/demo-implementation-plan.md)、现有 `FWeaponActionToken`、RuntimeHost、MovementTask |
| UI 归属 | [UI 系统](../design/ui-system.md)：HUD 唯一所有者、ResourceWidget 插槽、委托驱动 |
| 原作资源与测量 | [资产序号对照](../reference/资产序号对照.md)、[操虫斩/觉虫击曲线 §7～8](../reference/操虫斩曲线.md)、[下落物理](../reference/下落物理.md) |
| 本轮详细执行顺序 | 本文；与旧 E6 概括步骤不一致的字段/点击细节，以本文及配套 E6 指导为准 |

实施者必须亲自读取本轮涉及的函数，不根据类名猜行为。进入一个子工作包前只读取它的依赖和指定源码，不重跑旧资产修复 commandlet，也不扫描无关对话。

### 1.2 不变量

1. GAS ASC 保持在 PlayerState；Resource、猎虫、粉尘属于当前 Pawn 的武器运行期。HUD 属于本地 PlayerController。
2. 输入经现有 InputProfile → Router → Combo → GA；不能在 Character 蓝图另绑 RTYB/LTYB。
3. 所有动作使用**常驻 Montage 资产**。缺失资产时预检失败，不在运行时临时拼 Montage。
4. 猎人移动只用现有 `UAbilityTask_MHGZWeaponMovement`/RuntimeHost ownership；表现 Root Motion 与程序位移不能同时驱动胶囊。
5. 三灯、精华以 Active GE Handle 为真值；粉尘状态以 Resource 事务和 Actor 状态为真值；UI 只显示，不写资源或 Loose Tag。
6. 觉虫击一次成功贯通伤害，恰好触发一次普通 `ApplyExtract` 调用和一次粉尘生成；失败伤害不触发这两项。
7. 粉尘不会被武器/猎虫接触引爆；只有粉尘集约能消费。一个爆炸对同一目标 Actor 只结算一次。
8. 重复 Notify、EndAbility、解绑和取消均幂等；旧 Runtime/Action/Flight/Reservation 身份不能修改新一轮状态。
9. 不新增 GA 后强制 Idle 时间段、输入死区，不修改普通 MM 的 Start/Stop/挡位算法掩盖退出错误。
10. 不增加已移出 Demo 的降龙/强化跳跃斩，不扩展空中受击、死亡、台阶或其他武器玩法。生命周期关闭/强制取消仍必须清理，使用自动化测试，不等于制作这些玩法。

### 1.3 本轮与后续验收的边界

- 本文关闭的是 **M6 非音效工作**；音效明确延期，不能把尚未制作的音效写成已完成，也不能因此阻止本轮非音效验收。
- 普通猎虫命中后回手交付精华仍是 M7 前独立欠项。觉虫击即时点灯通过，**不等于**普通回手链通过。
- 本轮不重新要求人工精确控制几百分之一秒的按键释放。组合键/到期/重复回调的边界以自动化覆盖，PIE 使用正常操作。
- M5 的历史测试计数不是 M6 成绩。每次记录实际运行的数量、日志和资产审计结果。

## 2. 当前基线：已有与缺失

| 模块 | 实际状态 | M6 的处理 |
|---|---|---|
| `URes_InsectGlaive` | 已有三灯/单灯 Handle、`ApplyExtract`、三灯预留和消费、猎虫伤害、舞踏与四类资源委托 | 扩展粉尘及只读 UI 快照；保留现有行为 |
| `AKinsect` | 已有 AlongDirection/ToPoint、SingleHit/Piercing、ApplyPerValidHit、逐 Hitzone 世界时间限频、真实 Sweep Hit | 在伤害成功分支增添**显式启用**的产粉尘策略，不复制飞行/命中系统 |
| 成本结算 | `ConsumeReservedCosts` 当前为 `void`；GA 无法知道非空预留是否真正结算 | M6.1 补结算结果，避免过期三灯让觉虫击免费启动 |
| 觉虫击/粉尘集约 GA | 未发现对应生产 C++ 类及最终 GA 资产 | M6.2/M6.3 新增 |
| 粉尘 Actor/注册表/事务 | 未实现；`PS_IG_MarkYellowPowder` 是**虫印视觉**，不是通用可消费粉尘 | 新增 Actor 和 Resource 所有权，不冒用虫印粒子逻辑 |
| CombatConfig | 已有觉虫击距离/动作值/间隔/角度、集约半径/时长/数量 | 默认值不是原作已验证数值；补位移数据、PowderClass、Lifetime/爆炸参数 |
| RuntimeHost | 已有 `OnWeaponRuntimeInvalidated`、当前 Token/Context/ResourceProvider；**没有 Ready 委托** | M6.4 新增成功装配后的 Ready 信号与可读就绪状态 |
| `AMHGZHUD` | `BeginPlay` 只有 Super；不创建 HUD | 实现唯一 Widget 树和绑定生命周期 |
| Widget 基类 | `BindToASC`/`BindToResourceComponent` 只保存引用；无完整解绑 | 保持入口兼容并完善；新增主 HUD/属性条/虫棍资源显示原生类 |
| AimComponent | 已有 Kinsect 瞄准、颜色委托、输入时快照及独立的晚锁定采样 | UI 订阅、初始同步；不把觉虫击改为晚锁定 |
| HitFeedbackRouter | 已显式执行命中/暴击/伤害数字 Cue，携带真实 Hit 和最终伤害 | 实现缺失的视觉资产与数字显示，不造第二条伤害反馈链 |
| `UMHGZUISubsystem` | 只有静态 Get 的空壳；当前源码无其他调用点 | 先审计蓝图引用，迁移后删除，不让它再当 Widget 工厂 |
| `Content/UI`、`Content/GameplayCues` | 未发现本轮所需正式资产 | E6.3/E6.4 制作，不提前伪填 ResourceWidgetClass |
| RuntimeTelemetry | 已有输入、角色、MM、动画分类；默认 60Hz，命令 `mhgz.Telemetry.*` | 追加战斗事务/UI 生命周期事件，不替换现有记录系统 |

重点源码入口：

- `Source/MHGZ/ActionSystem/MHGZGameplayAbility.cpp`：预检 → 预留 → GAS Commit → 消费预留 → Confirm → 注册 Action。
- `Source/MHGZ/ActionSystem/MHGZAttackAbility.cpp`：自动取得 Attacking/BlockMovement、入招修正、选择 Section、精确注册 Montage。
- `Source/MHGZ/AttributeSystem/Res_InsectGlaive.cpp`：`TryReserveCosts`、`ConsumeReservedCosts`、`ApplyKinsectDamage`、`ApplyExtract`、`CheckAndActivateTripleUp`、`ShutdownRuntime`。
- `Source/MHGZ/InsectGlaive/Kinsect/Kinsect.cpp`：`HandleHitzoneHit`、`BeginFlight`、`EndFlight`。
- `Source/MHGZ/WeaponRuntime/MHGZWeaponRuntimeHostComponent.cpp`：`BuildWeaponRuntime`、失败回滚、`TeardownRuntime`。
- `Source/MHGZ/UI`、`Source/MHGZ/MHGZPlayerController.cpp`、`Source/MHGZ/ActionSystem/MHGZHitFeedbackRouterComponent.cpp`。

## 3. 工作包顺序与进出条件

| 顺序 | 工作包 | 入口 | 本包产物/出口 |
|---|---|---|---|
| 1 | **M6.1 基线审计、资源事务与数据合同** | M5 已签收；本轮范围确认 | 资源/动画清单、成本结算成功信号、粉尘 Actor/事务、数据校验、基础测试可编译；未签收招式 |
| 2 | **M6.2 觉虫击代码与资产生成** | M6.1；觉虫击资源时序/位移审计完成 | GA、精确 Notify、猎虫成功命中策略、移动/退出链、幂等资产生成及专项测试 |
| 3 | **E6.1 觉虫击资产核对和 PIE** | M6.2 冷编译及只读审计通过 | 用户确认 Montage 姿势/速度/通知；三灯消费、贯通、点灯、粉尘、退出证据 |
| 4 | **M6.3 粉尘集约代码与资产生成** | M6.1；粉尘事务可测；集约动画已确认 | GA、聚拢任务/通知、一次 AoE 伤害、取消回滚、专项测试和生成审计 |
| 5 | **E6.2 粉尘集约接线和 PIE** | M6.3 编译/审计通过；E6.1 可产生粉尘 | 聚拢/爆炸、所有权、多 Hitzone 去重、取消和过期验证 |
| 6 | **M6.4 HUD、资源显示与视觉反馈代码** | M6.1 数据合同；不要求音效 | Ready、UI 快照、Widget 生命周期、准心、数字/Cue 接口；UI 测试通过 |
| 7 | **E6.3 HUD/准心编辑器接线** | M6.4 已编译 | HUDClass/WidgetClass/插槽连接，资源面板由 None 改为最终类，UI 功能 PIE |
| 8 | **E6.4 Cue、数字及视觉资产接线** | M6.4；伤害路由接口稳定 | Cue 发现、视觉与数字参数正确；不添加 Sound |
| 9 | **M6.5 静态回归和证据审计** | 本轮代码/接线已完成 | 实跑构建/测试、阶段资产验证、Telemetry 分析、剩余问题列表 |
| 10 | **E6.5 非音效总验收** | M6.5 无阻塞项 | 固定矩阵与用户目视通过，记录签收，允许准备 M7；音效/普通回手欠项单列 |

M6.4 在 M6.1 合同稳定后可提前实施；同一文件只能有一个写入者。默认按表顺序执行，不能因为主 UI 完成就宣布觉虫击/集约完成。能用脚本生成/审计的资产由脚本完成，仅把必须看画面或选择未确认素材的工作交给用户。

## 4. M6.1：数据、资源与粉尘基础设施

### 4.1 先输出两张实际审计清单

产物放 `Saved/M6Audit/`；可复用 `Scripts/AssetProbe` 与 C++ commandlet，不在运行时硬编码资产路径。

1. **数据清单**：导出 `DA_WeaponRuntime_IG`、`DA_IG_Combat`、`DA_IG_InputProfile`、`DA_IG_Combo` 的实际字段/边，包含 SourceState、Tags、AbilityClass、Chord 与优先级。存在 RTYB/LTYB Tag 不代表资产里已有可用 Chord。
2. **动画清单**：逐条读取下表真实包路径、Skeleton、ImportID、序列时长、采样帧数、RateScale、Root 位移、原轴向、已有曲线/Notify；通过 AssetImportData 对照，不按文件名臆测。

| ID | 当前已有候选序列（`/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/`） | 用途/限制 |
|---|---|---|
| 193 | `AS_Unsh_JueChongJi` | 觉虫击起手 |
| 194 | `AS_Unsh_JueChongJi_Hold` | 持续帧候选；录制链不证明必须循环或蓄力，不能自动接入 |
| 195 | `AS_Unsh_JueChongJi_Send` | 丢虫阶段 |
| 196 | `AS_Unsh_JueChongJi_Pose` | 突进前摇；`Pose` 不是待改名错误 |
| 199 | `AS_Unsh_JueChongJi_Flying` | 猎人飞行表现 |
| 206 | `AS_Unsh_LieChongHuaJueChongJiFall` | 未命中后下落表现；与胶囊物理分开 |
| 189 | `AS_Unsh_CaoChongZhanJueChongJiWuTa` | 原作命中舞踏证据，不自动授予 M6 新舞踏权限 |

**现行规则边界：** 当前动作文档 §6.1 只允许操虫斩与突进回旋斩增加舞踏；本轮觉虫击不新增第三种来源，不把其命中伪装成 `KinsectSlash`，也不放开强化操虫穿刺的来源限制。参考资料中的 `199→189`、晚瞄准/持续段是原作证据，不等于当前 Demo 已授权规则。若以后决定完整加入该分支，须先单独修订玩法、`EIGDanceSource`、来源 Tag/预算及曲线，不直接复制操虫斩舞踏参数。

**已有曲线不等于可直接消费：**

- `Saved/_mhr_frames/193_绝虫击起手.csv`、`195_绝虫击丢虫.csv`、`199_绝虫击突进飞行.csv`、`206_猎虫滑翔命中以及觉虫击未命中的下坠段.csv` 是逐样本证据。
- `Saved/_mhr_curves/觉虫击_199.csv` 是现有代表曲线，不是已验证的任意方向完整规范轨迹；不得把其中 `y_cm` 当 UE 的前向或直接复制为世界 Z。
- MHR 世界竖直为 Y，UE 世界竖直为 Z；资产 Root 的局部轴又是另一层转换。保持本项目已确认的骨架轴向，不强行旋转 Skeleton 或烘焙成向上爬/浮空。
- 139/194/44 是录制采样数量，相关秒数约 1.160/1.619/0.367；**不是 UE 序列帧号**。196 的时长和阶段接缝须资产/记录实际核对，不能把“195 后继缺失”解释成无 196。
- 导入序列可能为 0.5 倍速。需要时离线烘焙真正 2x 的序列；要检查端点、Root 位移、手指/武器姿势，不只改 RateScale。最终 Montage 各段 PlayRate=1。
- 不因本机原始 CSV 缺失，重跑全库生成器覆盖已入库的历史真值。可以做本招定向只读复算；缺少的阶段数据明确记录待补。

集约动画尚未由现有对照表确认。M6.1 应检查本地资源及录制动作 ID，给出最终候选；不得拿 `JueChongJi_Hold` 或虫印动作充当已确认的集约动画。**缺素材不阻止写粉尘/事务/UI 代码，但阻止该招最终 Montage 与 PIE 签收。**

### 4.2 成本结算必须有成功结果

限定修改域：`MHGZWeaponResourceComponent`、`Res_InsectGlaive`、RuntimeHost、`MHGZGameplayAbility`，以及覆写该接口的现有测试桩。不要另造只给觉虫击使用的 Commit 路径。

目标接口：把现有 `ConsumeReservedCosts(const FWeaponResourceCostReservation&)` 的返回类型改为 `bool`，名称与参数保持不变；同步所有覆写/调用。

- 空 `WeaponResourceCosts`：GA 跳过非空资源消费，保持 M1 的无成本激活语义。
- 非空成本：Host 检查当前 RuntimeToken、Provider、Reservation 身份；Resource 核对预留 Handle 仍为当前活跃 Triple Handle且未消费，成功移除后返回 true。
- 同一 Triple Handle 不能同时被两份有效预留占用；同一 Reservation 第二次消费返回 false，不重复扣除。
- Handle 结构有效但 GE 已过期，不能当成有效三灯。失败时释放预留、拒绝转移、结束动作，**不得 Confirm/注册 Action/播放 Montage/部署猎虫**。
- 只在成功结算后置 `bReservationConsumed`/激活提交状态。GAS Commit 与离散资源结算之间的失败要有专门回归。
- 觉虫击本轮不新增未确认的耐力 CostGE/CooldownGE；如果将来给本招加 GAS 成本，需要另处理跨系统回滚，不能声称上述 bool 已是所有 GAS 成本的全局事务。
- 正常三灯建立时已移除单灯 GE。原子消费仍由 Resource 处理并检查不留单灯；如果防御性清理残留 Handle，应在 transition guard 内一次完成，最后广播一致快照，禁止 GA 逐个 Remove GE。

“失败不消耗”指激活预检/成本门槛失败；**成功 Commit 后被取消不返还旧三灯**。否则会出现已造成贯通伤害后取消刷回旧三灯的漏洞。

### 4.3 目标类型与文件

以下均为**拟新增**，本轮文档并未创建这些代码：

| 文件/类型 | 责任 |
|---|---|
| `InsectGlaive/IGPowderTypes.h` | `EIGPowderState`、`FIGPowderReservationToken`、只读粉尘快照与聚合结果 |
| `InsectGlaive/IGPowderActor.h/.cpp` → `AIGPowderActor` | 世界表现、自然到期、受控状态；不查找玩家、不自行结算伤害 |
| `AttributeSystem/Res_InsectGlaive.h/.cpp` | OwnedPowders、筛选/预留/释放/消费、生成与运行期清理 |
| `InsectGlaive/InsectGlaiveCombatConfig.h/.cpp` | PowderClass、Lifetime、贡献/爆炸参数及本招运动数据 |

类型合同：

- `EIGPowderState : uint8`：Available、Reserved、Consumed；无直接 `Available→Consumed` 公共蓝图入口。
- `FIGPowderReservationToken`：`FWeaponRuntimeToken RuntimeToken`、`uint32 ActivationSequenceID`、`uint64 ReservationID`；等值判断必须包含全部身份，不只比较一个递增数字。
- Actor：`TWeakObjectPtr<URes_InsectGlaive> OwnerResource`、`TWeakObjectPtr<APlayerController> OwningPlayer`、RuntimeToken、`FGuid PowderID`、`FGuid SourceAttackInstanceID`、`FGuid SourceHitInstanceID`、`float SpawnWorldTime/Lifetime/DamageContribution`、State、ReservationToken。Actor.Owner设置为源Pawn；筛选以OwnerResource和RuntimeToken为准，OwningPlayer用于显示/审计，不用GetPlayerController(0)。
- Resource：`TArray<TWeakObjectPtr<AIGPowderActor>> OwnedPowders`，按 ReservationID 保存成员快照、中心、创建时刻；下一 ID 单调递增。
- `SourceAttackInstanceID` 对应觉虫击源攻击；每次贯通伤害另外有唯一 `SourceHitInstanceID`，用于“伤害→点灯→粉尘”核账，不用 FlightID 代替每跳伤害 ID。

新增配置及单位：

| 字段 | C++ 类型 | 单位/约束 |
|---|---|---|
| `PowderActorClass` | `TSubclassOf<AIGPowderActor>` | 指向最终 `BP_IG_PowderCloud` |
| `PowderLifetime` | float | 世界游戏秒，>0；暂停/slomo 随游戏时间，不用墙钟 |
| `PowderDamageContribution` | float | 每团直接伤害贡献，≥0，生成时快照 |
| `PowderVortexBaseDamage` | float | 基础直接伤害，≥0 |
| `PowderVortexExplosionRadius` | float | cm，>0；**不同于**收集半径 |
| `PowderVortexBaseStagger` | float | 基础破坏值，≥0 |
| `PowderVortexHitCueTag` | FGameplayTag | 目标 `GameplayCue.Hit.IG.Powder` |
| 现有 `PowderGatherRadius/Duration/MaxCount` | float/float/int32 | 当前默认 600cm/0.5s/5，不冒充原作真值 |

数据校验分层：未把特殊 GA 接入 Combo 前，新增功能的空引用不得使已签收 M5 全部失效；该功能启用/GA 被接入时，校验其必需 Class、Montage、Curve、Notify、参数。不能用硬编码默认类掩盖遗漏，也不能删除现有四个必填 Sound 的校验来绕过本轮。

### 4.4 Resource API（目标接口）

```cpp
// 以下是设计签名，实施时连同相关 USTRUCT 一起落地。
bool SpawnPowderForSuccessfulHit(
    const FWeaponRuntimeToken& Runtime,
    const FGuid& SourceAttackID, const FGuid& HitID,
    const FHitResult& Hit);

bool CanGatherPowders(const FVector& Center) const; // 无副作用
bool TryReservePowders(const FWeaponActionToken& Action,
    const FVector& Center, FIGPowderReservationToken& OutToken);
bool CommitPowderReservation(const FIGPowderReservationToken& Token,
    FIGPowderExplosionSnapshot& OutExplosion);
void ReleasePowderReservation(const FIGPowderReservationToken& Token);
void ClearOwnedPowders(EWeaponRuntimeEndReason Reason);
```

`FIGPowderExplosionSnapshot` 至少包含 `FGuid ExplosionID`、`FVector Center`、`int32 ConsumedCount`、`float TotalDamage`、`float Radius`、`float BaseStagger`。它只由成功提交产生，之后不可修改。

### 4.5 状态与事务规则

```text
有效贯通伤害成功 → 生成 Available
Available → Reserved(精确 ReservationToken)
Reserved → Available                    // 取消，仍有效
Reserved → Consumed → Destroy           // 集约成功，一次
Available/Reserved → Expired → Destroy  // 生命周期自然结束
任意存活状态 → RuntimeShutdown → Destroy
```

“Expired”是到期原因，不要求追加可消费状态。规则如下：

1. 粉尘 Collision Disabled，无物理模拟，不阻挡胶囊/武器/猎虫，不影响 Visibility 准心。
2. 用真实 Hit 的接触点生成，适当小法线偏移只用于表现；不重新 Trace 寻找另一个部位。记录原始 Hit 与表现位置。
3. 筛选仅当前 Resource/Runtime 所有、Available、未到期、在范围内的 Actor；不使用每帧 `GetAllActorsOfClass`。
4. 稳定排序：中心距离升序，距离相同时按生成顺序；截取 MaxCount。一次预留固定成员，期间新粉尘不能自动混入。
5. 预留完整成功才返回 Token。失败回滚本次所有状态，不留“半份 Reserved”。
6. Lifetime 不因 Reserved 重置；过期或 Actor 销毁从注册表/事务中摘除。聚拢完成消费仍存活的成员，按实际数量计伤；全失效则失败、无爆炸。
7. 取消释放仍存活的成员为 Available并恢复其聚拢前位置；已经自然到期的粉尘不复活。卸装则统一销毁旧 Runtime 粉尘，不把它们释放给新武器。
8. Commit 必须先将事务标为已提交、移除预留表并将成员变为 Consumed，产出快照，然后才发送事件/Destroy/执行伤害，防止重入重复爆炸。
9. 集约预留与 `WeaponResourceCosts` 的三灯预留是**两种不同事务**：三灯在入招 Commit 消耗，粉尘在聚拢完成消耗。不能把粉尘放进通用 GA 入招立即结算的成本数组。
10. Shutdown 顺序：拒绝新请求 → 取消本运行期动作/聚拢回调 → 清事务和粉尘 → 猎虫/GE 等现有清理。迟到到期/取消回调按旧身份退出。

## 5. M6.2：觉虫击

### 5.1 类、授权与输入

拟新增 `ActionSystem/MHGZAwakenedKinsectAttackAbility.h/.cpp`，类 `UMHGZAwakenedKinsectAttackAbility : UMHGZInsectGlaiveAbility`；蓝图为 `/Game/Blueprints/Ability/InsectGlaive/GA_IG_AwakenedKinsectAttack`。

- 继承现有攻击 Montage/生命周期；本轮猎人没有另行声明的武器伤害，`AttackSegments` 为空，伤害来自现有猎虫贯通链。
- RuntimeHost 从 `DA_IG_Combo.Transitions.AbilityClass` 唯一收集授权；**不放进 `BP_PlayerState.CoreAbilities`**。
- 首批入口为持刀地面 `Idle` 的 `Input.Weapon.RTYB`；后续连段派生须由确认的 ComboWindow 边单独增加，不能默认任意状态可打断。
- 输入 Chord：TriggerControls 为现有物理 Y/B，HeldModifier 为 `Input.Modifier.RT`，AimSnapshotContext=Action；保留现有宽限期与更高成员数组合优先。物理 Y/B 的 Tag 从实际 Profile 映射读取，不能猜新的 Tag 名。
- 组合解析输出一次 RTYB，获胜后消费 Y/B。不能再同时派发 RTY、RTB 或 RT 松开 fallback；已在组合前合法发出的单 RT 不追溯撤销。
- Required：`Combat.State.Grounded`、`Combat.State.Unsheathed`、`WeaponResource.IG.TripleUp`；GA 还查询真正的 Triple Handle、猎虫、配置和移动依赖。首批Idle入口的Blocked为 `Combat.State.Attacking`、`Combat.State.Dodging`、`Combat.State.Sheathing`、`Combat.State.Hitstun`、`Combat.State.Knockdown`、`Combat.State.Dead`、`Combat.State.Aerial`，与GA激活标签保持一致。当前CantAttack是空中预算标签，不能把它杜撰成地面通用锁；不能只靠标签绕过 Resource/Token 校验。
- `WeaponResourceCosts` 只有一项：`Cost.IG.TripleUp`、Amount=1。不得再在 GA 调 `TryConsumeTripleUpAtomic` 扣第二次。

### 5.2 方向合同

1. 只读 `GetWeaponActivationContext().Input.Aim` 中已有的 Action AimSnapshot，不在 GA 激活/丢虫/飞行 Notify 重采样镜头。直连缺少有效快照时预检失败。冻结角色基准优先用同一 `Input.ActorForward`，不拿上一帧缓存的移动方向当角色Forward。
2. 冻结入招角色平面 Forward 与 Up，计算输入瞄准方向的相对 Yaw/Pitch。
3. 按现行“水平和垂直分别 ±60°”描述，各自 Clamp 到 `AwakenedAimCorrectionAngle`。**这是两分量上限，不是对角线也限制 60°的数学圆锥**；实现时同步修正旧配置中含混的“锥边缘”注释，不改变其他攻击的 +90°容错规则。
4. 得到 `LockedKinsectDirection`；胶囊只修正 Yaw，猎虫允许三维方向。不要 Pitch 旋转碰撞胶囊。
5. 猎人目标点仍为输入时 Aim.TargetPoint；从猎人启动位移时的位置指向该冻结点，截断到 `AwakenedHunterFlightMaxDistance`。目标移动不能追踪，超过距离不能瞬移吸附。
6. 基类默认摇杆入招修正对此招禁用（Combo/GA MaxCorrectionAngle=0），避免先按摇杆转一次、再按准心转一次；本招修正由精确动作 Token 的现有旋转所有权执行。

### 5.3 阶段与 Montage

拟新增 `EAwakenedKinsectAttackPhase : uint8`：Inactive、Startup、KinsectFlight、HunterFlight、Falling、Ended。阶段字段是执行状态，不授予新舞踏来源。

常驻 `AM_IG_AwakenedKinsectAttack`，至少有 `Startup193`、`Send195`、`PreFlight196`、`HunterFlight199`。变长下落使用独立 `AM_IG_AwakenedKinsectFall`（206），不能靠把它塞到固定蒙太奇末尾来决定实际触地。

拟新增点通知 `UAnimNotify_IG_AwakenedKinsectEvent`，枚举仅包含：

- `KinsectLaunch`：开始猎虫贯通请求。
- `HunterFlightStart`：开始猎人位移；同一激活只执行一次。

通知对象只保存事件类型；运行时通过当前 Mesh + Montage 实例解析精确 ActionToken，再交给 GA。不能在 Notify 类里缓存 GA/Token，不能只用 GetCurrentActiveMontage 猜归属。

时序由审计后的最终 Montage 点通知驱动。已有 `AwakenedHunterFlightStartTime=0.4` 不是原作真值；迁移为**审计/显示用派生时间**或明确 Legacy 后不再调度第二个 Timer。禁止 Notify 和 0.4s Timer 双触发。

- 默认自动段链，不接 194 自循环；没有本轮明确的蓄力需求。
- 下落姿势是否循环、保持末帧或只播放一次，由 206 的资源审计决定。循环仅影响姿势，不重置重力/动作时间，不能因 Montage 播完强制地面 Idle。
- 猎人真正可交给空中动作时使用现有 `AnimNotify_IG_AerialHandoff` 和 `Combat.State.Aerial.Falling`；不恢复旧 Actionable 设计。通知位置需时序证据，不猜固定第几帧。
- Root Motion 表现盾、动作退出及落地均复用 M5 路径；只有触地后才用地面退出/MM 路由。

### 5.4 猎虫请求及逐击副作用

扩展 `FKinsectFlightRequest`，拟新增：

```cpp
bool bSpawnPowderPerSuccessfulHit = false;
FGuid SourceAttackInstanceID;
```

默认 false 保证 LT+Y、RT 放虫、操虫斩临时附着等现有动作不因全局改动产粉尘。

觉虫击请求填写：RuntimeToken；AlongDirection；LockedKinsectDirection；MaxDistance=AwakenedKinsectMaxDistance；FlightSpeed 来自品种/审计后的显式配置；Piercing；ApplyPerValidHit；MotionValue=AwakenedKinsectMotionValue；RehitInterval=AwakenedPierceHitInterval；唯一 FlightInstanceID/SourceAttackInstanceID；产粉尘开关=true；PostFlightPolicy=ReturnToOwner。

在 `AKinsect::HandleHitzoneHit` 的**现有 Piercing 分支**中执行：

```text
真实 Sweep Hit + 活跃 Runtime + Hitzone有效 + 该部位已过间隔
    → 生成唯一 HitInstanceID
    → ApplyKinsectDamage(Hit, MotionValue, HitInstanceID)
      ├─ false：记录失败；不点灯、不产粉尘、不写成功命中时间
      └─ true：写成功时间
             → 普通 ApplyExtract(Hitzone.ExtractColorTag)，一次
             → 若本请求开关=true：SpawnPowderForSuccessfulHit，一次
```

- 以已有 `WasSuccessfullyApplied()` 伤害提交成功为当前链路的判据，不另造更宽松的碰撞成功定义；专项测试同时验证最终 Health/HitSignal。
- Valid Hitzone 没有合法精华颜色是资产错误，预先校验；不静默假装“颜色随便一种”。GE Apply 或 Actor Spawn 异常须有结构化错误和计数，不能重发伤害来补粉尘。
- 第一击不先缓存 PendingExtract 再回手交付。ApplyPerValidHit 的返回不能多送一次精华。
- 新三灯形成后，`ApplyExtract` 仍被调用但按现有规则吞灯、不刷新；粉尘仍正常生成。
- 同一 Hitzone 限频按世界时间，与 Tick/帧率无关；同一 Flight 的每次有效伤害 ID 必须不同，否则 IncomingHitResolver 去重会吞掉后续贯通。
- 猎虫正常飞行可独立于猎人动作尾段继续，RuntimeToken 是存续门槛；不得因为 GA 正常结束就禁掉所有晚到的合法贯通。
- 异常取消可请求该 Flight 召回；必须核对 GA 保存的 FlightInstanceID 与猎虫当前 Flight 身份，不能召回后来另一招启动的猎虫。取消后不再产粉尘；正常到达/超距/撞墙按 ReturnToOwner 收尾。

### 5.5 猎人位移与退出

必须生成**本招专用**运动配置，不能拷贝操虫斩 6600/1800 等默认速度或舞踏曲线。

目标配置 `FAwakenedHunterMovementProfile`，配置挂 `UInsectGlaiveCombatConfig`；Curve 资产命名 `CV_IG_AwakenedKinsect_Startup` 等，并附生成审计 JSON。

| 拟新增成员 | 类型 | 合同 |
|---|---|---|
| `StartupTrajectory` | FRecordedLocalTrajectoryProfile | 193/195/196相位秒数与局部位移，曲线不可变 |
| `FlightDurationSeconds` | float | 199有效飞行秒数；由本招数据审计确认 |
| `FlightInitialSpeed` | float | cm/s；不能使用操虫斩默认6600 |
| `FlightVerticalAcceleration` | float | 有符号世界Z加速度，cm/s²；与准心Pitch分离 |
| `FallingGravityMagnitude` | float | >0，cm/s²；206物理档，非195/199共用重力 |
| `FlightFinishPolicy` | 本项目现有末速度策略枚举 | 向下一物理相位交付真实末速度，具体枚举值在接口审计中冻结 |

本包的**数据出口门槛**是生成审计明确这些数值及动作/曲线时间映射，缺数值时不得启动GA写入猜测参数。2000/1500cm是现有Config默认上限；若它们截断录制基准路径，报告“Demo上限差异”并调整经确认的配置，不声称距离已完整复原。

执行规则：

1. 193/195/196 的小位移如果保留，使用本招 Startup RecordedLocalTrajectory，源数据要统一局部建基和相位时钟；不得忽略 195 已有的位移证据后声称完整复原。
2. 199 的已测“水平近恒速、世界竖直加速度约 −1188～−1201cm/s²”仅是拟合证据，当前 2000/1500cm 上限和输入冻结为 Demo 规则；先验证运动模型对基准样本的误差，再确定最终参数。
3. **接口检查门槛：** 现有 BoundedDirectional 是有限距方向移动，SegmentedDirectional 是操虫斩专用分段模型，不能为了省代码把觉虫击硬塞进它们的默认配置。如果现有 RecordedLocalTrajectory 对本招需要的方向/距离缩放已足够，复用；若需要“沿目标方向初速 + 世界 Z 独立加速度”，只在统一 MovementTask 内增加带明确参数的模式，先补纯数学/CMC/所有权测试，再接 GA。不能在 GA Tick 手动移动胶囊。
4. 猎人位移的启动点由 HunterFlightStart 控制；猎虫启动点独立。两者可重叠，不共用一个 ProjectileMovement。
5. 实际胶囊 Sweep/CMC 判定地面与世界阻挡；墙面不能把未实现的动作硬切 Idle 再持续上浮。阻挡后的动作相位/末速度策略有独立测试。
6. 每段交接退役旧运动来源、安装新来源，并保留明确的末速度；不能出现一帧假 Walking、残余 RMS 继续推人或重复重力。
7. 悬空进入 Falling 时复用 Host 的下落/落地表现与空中来源记账。若来源枚举需要补 `AwakenedKinsectAttack`，追加具体名称、Tag 与物理档，不用 Other，不改变 M5 已有来源；**它不是 DanceSource**。
8. 真实落地按既有规则清舞踏、关闭本招物理租约，再以实际摇杆走现有 Idle/Move/Stop 交接；不在 GA 结束后额外冻结输入。

### 5.6 M6.2 出口

构建成功；专项覆盖消费/过期、缺依赖、Clamp、冻结镜头、贯通限频、逐击副作用、三灯重建及吞灯、取消/正常结束/旧 Flight 回调、唯一移动者、真实落地；资产生成再跑一次为无修改，只读审计通过。缺失运动真值/循环区时报告具体待确认项，不能给 E6.1 一个假完成 GA。

## 6. M6.3：粉尘集约

### 6.1 GA 与启动条件

拟新增 `ActionSystem/MHGZPowderVortexAbility.h/.cpp`，类 `UMHGZPowderVortexAbility : UMHGZInsectGlaiveAbility`；蓝图 `/Game/Blueprints/Ability/InsectGlaive/GA_IG_PowderVortex`。

- 使用常驻 `AM_IG_PowderVortex` 和现有攻击 Montage Task；AttackSegments 为空、MaxCorrectionAngle=0。本轮是地面固定动作，不启用上半身操虫 Slot 或猎人位移。
- Input=LTYB，初始只从持刀地面 Idle 进入；Required=Grounded/Unsheathed，Blocked 遵守现有锁；不需要三灯，`WeaponResourceCosts` 为空。
- 预检：当前 Runtime/Resource/猎虫有效、猎虫非 ActionAttached、Montage/必需 Notify/参数有效、猎虫当前位置周围存在本玩家可用粉尘。失败不进入 Attacking，不预留，不爆炸。
- 成功入招后以当前猎虫位置冻结 GatherCenter，立即 `TryReservePowders`；正常情况下与预检同一调用链，仍要处理期间状态变化导致的预留失败，精确退出且零粉尘消费。
- 聚拢期间保持本次成员/中心不变；不追踪后来移动的猎虫，不因为新粉尘出现重新选一遍。

### 6.2 聚拢与消费时刻

拟新增 `UAnimNotify_IG_PowderGatherBegin` 点通知和 `UAbilityTask_MHGZGatherPowder`：

- Notify 精确核对 Action/Montage 实例；只把事务从 Reserved 推到 Gathering，一次。
- Task 持有 ReservationToken，按 PowderGatherDuration 推进**表现位置**，不拥有猎人移动、不做伤害、不直接销毁粉尘。
- 正常完成回 GA → Resource `CommitPowderReservation` → 不可变爆炸快照 → 一次范围伤害与视觉 Cue。
- Task 异常结束、Montage 中断、GA 取消、换武器或 Runtime 关闭 → Release；结束幂等，未 Commit 不产生爆炸。
- Montage 必须覆盖 GatherBegin 后完整聚拢时间；配置审计检测“GatherBegin + Duration > 有效播放末端”。若动画提前结束，按配置异常取消，不能绕开动画在结束后偷偷爆炸。
- 爆炸可以在蒙太奇后摇前发生；GA 正常结束仍由本招 Montage 完成决定。进入合法窗口的派生须先完成本次爆炸或取消事务，不继承未完成的预留。

### 6.3 一次 AoE 的伤害合同

本轮使用简单且可验证的公式：

```text
TotalDamage = PowderVortexBaseDamage
            + Σ(成功消费粉尘在生成时快照的 DamageContribution)
```

不声称这是原作粉尘品种/伤害表；本 Demo 只一种通用粉尘。参数在 CombatConfig，可调不硬编码。爆炸只发生在成功消费至少一团之后。

1. 球形范围查询 Hitzone Object Channel，中心为快照 GatherCenter，半径为 ExplosionRadius。使用真正的 Sphere Sweep HitResult；不能把 Actor Location 包装成伪 HitResult 规避现有 ExecCalc。
2. 按 Target Actor 去重；目标多部位相交只选一个真实 Hit 作为上下文，选距中心最近的有效 Hit，等距离以组件稳定路径排序。没有精华颜色优先级。
3. 整次爆炸一个唯一 ExplosionID/AttackInstanceID，各目标复用该身份；每个目标只提交一个 Spec。预留成员已消费后不得重试同一爆炸再次扣血。
4. 复用 `UMHGZDamageGameplayEffect` 和现有 Context/Resolver/ExecCalc/Router：SourceType=Powder、真实 Hit、HitzoneTag、HitCueTag。
5. 直接伤害映射：`Damage.AttackPower=TotalDamage`、`Damage.MotionValue=1`、`Damage.CritOverride=0`、`Damage.DanceMultiplier=1`、`bUseHitzoneDefense=false`；破坏值填 `Damage.BaseStagger`，肉质破坏倍率继续由现有 ExecCalc 处理。
6. 不乘猎人攻击力/舞踏/三灯增伤；不单独直接改 Health，不让 PowderActor 各炸一遍，不为每团创建新 GE 类。
7. 粉尘爆炸不点灯、不增舞踏、不产新粉尘。视觉爆炸与每目标命中数字分开：爆炸 Cue 一次，目标数字随 Router 的真实结算产生。
8. Consume 完后某目标伤害提交失败，不复活粉尘或整次重炸；记录该目标失败，不影响其他合法目标。

### 6.4 M6.3 出口

测试至少覆盖：零粉尘/无猎虫失败、不同玩家互不消费、超范围/上限/稳定排序、两事务争抢、GatherBegin 重复、重复 Commit、取消恢复位置、到期不复活、部分/全部成员失效、多个 Hitzone 单目标只一次、伤害失败、换装/旧回调、连续集约。

## 7. M6.4：HUD、资源显示、准心

### 7.1 目标类与创建树

```text
Local PlayerController
└─ AMHGZHUD / BP_MHGZHUD                 // 唯一生命周期所有者
   └─ WBP_HUD : UMHGZMainHUDWidget       // 唯一 AddToViewport
      ├─ HealthBarSlot → WBP_HealthBar
      ├─ StaminaBarSlot → WBP_StaminaBar
      ├─ CrosshairSlot → WBP_Crosshair
      └─ WeaponResourceSlot → WBP_IG_ResourcePanel
```

拟新增/扩展文件：

| 文件/类型 | 目标职责 |
|---|---|
| `UI/MHGZHUD.h/.cpp` | MainHUDClass/HealthBarClass/StaminaBarClass/CrosshairClass；创建/插槽/绑定/销毁 |
| `UI/MHGZMainHUDWidget.h/.cpp` | `UMHGZMainHUDWidget`；四个 `UOverlay*` BindWidget 插槽，命名同上 |
| `UI/MHGZAttributeBarWidget.h/.cpp` | `UMHGZAttributeBarWidget : UMHGZUserWidget`；供血/耐力两个数据型蓝图共用 |
| `UI/MHGZWeaponResourceWidget.h/.cpp` | 虚拟 Bind/Unbind、NativeDestruct 兜底；保留已有公开 Bind 名 |
| `UI/MHGZInsectGlaiveResourceWidget.h/.cpp` | `UMHGZInsectGlaiveResourceWidget : UMHGZWeaponResourceWidget`；虫棍快照委托订阅与显示事件 |
| `UI/MHGZCrosshairWidget.h/.cpp` | 目标变化与显隐接口；不自己 Trace |
| `InsectGlaive/IGResourceUITypes.h` | `FIGResourceUISnapshot`，纯读模型，不是第二份战斗状态 |
| `AttributeSystem/Res_InsectGlaive.h/.cpp` | GetUIState 与状态/时间更新广播 |
| RuntimeHost/PlayerController | Ready、Possess/UnPossess 通知及其幂等生命周期 |

HUD 属性类型全部为 `TSubclassOf<对应Widget基类>`；运行实例为 `UPROPERTY(Transient) TObjectPtr<...>`。绑定对象用弱引用、保存精确 `FDelegateHandle`/动态委托绑定目标，不在蓝图全局 GetPlayerCharacter(0)。

先补基类可覆写入口：现有 `UMHGZUserWidget::BindToASC`、`UMHGZWeaponResourceWidget::BindToResourceComponent` 改为virtual，保留UFUNCTION名/参数避免断蓝图引用；分别新增virtual `UnbindFromASC()`、`UnbindFromResourceComponent()`。派生原生类负责订阅/解绑，NativeDestruct再次调用解绑作为兜底；不能只在派生类声明一个同名但不发生虚调用的函数。

Dedicated Server 或非本地 Controller 不 CreateWidget。只有主 HUD AddToViewport；子面板只 AddChild 到插槽。资源面板通过 RuntimeDefinition.ResourceWidgetClass 创建，不用 HUD 硬编码虫棍 WBP 路径。

### 7.2 Ready 和初始同步

当前 RuntimeHost 没有 Ready 委托，因此必须实际实现，不能在指导里让用户搜索一个不存在的事件：

- 新增原生 `OnWeaponRuntimeReady(const FWeaponRuntimeToken&, UMHGZWeaponResourceComponent*)` 委托及 `IsWeaponRuntimeReady()`。
- Ready 只在 BuildWeaponRuntime 的 Resource 初始化、武器 Ability 授予、Coordinator 激活和 Combo 注入**全部成功后**广播；失败回滚不 Ready。无需资源的成功武器可带 nullptr。
- HUD 先订阅 Ready/Invalidated，再读取当前 Token/Ready/Provider 快照；即使 Ready 早于 HUD 创建也能同步，不靠 Tick 等待。
- 新 RuntimeDefinition.ResourceWidgetClass 为 None 时合法：清空资源插槽，其余 HUD 仍显示。
- Ready 身份相同且实例已绑定时无操作；真实 Generation 变化先解绑并移除旧面板，再创建/绑定新面板。旧 Invalidated 只清与它 Token 一致的绑定。

PlayerController 的 Possess/UnPossess/EndPlay 通知只触发 HUD 重新解析/解绑，不在 Controller 创建第二套 Widget。可使用现有 Pawn 变化信号并补 ASC 就绪通知；若现有信号不覆盖顺序，新增 `NotifyPawnContextReady` 明确由 Character 完成 ASC ActorInfo/Runtime 初始化后通知 HUD，禁止常驻定时轮询。

生命周期顺序：

```text
本地 HUD 创建树 → 订阅 Pawn/就绪变化 → RebindPawnContext
    → UnbindOldContext（所有 handles、旧资源面板、准心）
    → Bind 新 ASC / Aim / Host
    → 主动读 Attribute、Aiming、Resource 当前值
    → 之后只处理委托
UnPossess/EndPlay/Invalidated → 精确解绑；重复执行无害
```

ASC 在同一 PlayerState 跨 Pawn 留存也要按正确拥有者解绑重绑；晚到旧 Pawn 事件不能写新 Widget。

### 7.3 健康/耐力条

在 `UMHGZAttributeBarWidget` 暴露 `FGameplayAttribute CurrentAttribute` 与 `MaxAttribute`（EditDefaultsOnly/BlueprintReadOnly）；血条填 Health/MaxHealth，耐力填 Stamina/MaxStamina。

- Bind 先 Unbind 旧 ASC，分别订阅两个 `GetGameplayAttributeValueChangeDelegate`，然后主动读两个值并调用现有 `OnValueUpdated(float Current,float Max)`。
- 任一属性变化都用两者最新值重新显示；Max≤0 时 Percent=0，否则 Clamp(Current/Max,0,1)。不把 Max 写死成 100。
- Unbind/NativeDestruct 清两个 Handle、BoundASC 与显示状态。Repeated Bind 不加重复订阅。
- BP 的 OnValueUpdated 只 SetPercent/SetText；禁止 UMG 属性 Binding 每帧 GetActor/ASC，禁止 EventTick 轮询。

### 7.4 虫棍读模型

`FIGResourceUISnapshot` 字段及类型：

| 字段 | 类型 | 来源 |
|---|---|---|
| `RuntimeToken` | FWeaponRuntimeToken | 当前 Resource 运行期 |
| `KinsectStamina / MaxKinsectStamina` | float / float | Resource 真值 |
| `KinsectState` | EKinsectState | 当前猎虫；不存在时另用 bHasKinsect=false |
| `bHasKinsect` | bool | 只读指针/有效性 |
| `bRed / bWhite / bOrange` | bool | 各活跃单灯 GE，不从 Ratio>0 反推 |
| `RedRemainingSeconds/WhiteRemainingSeconds/OrangeRemainingSeconds` | float | ASC GE 剩余时间，秒 |
| `RedRatio/WhiteRatio/OrangeRatio` | float | GE Remaining/Duration，0～1 |
| `bTripleUp`、`TripleRemainingSeconds`、`TripleRatio` | bool、float、float | 当前 Triple Handle；不缓存第二份战斗真值 |
| `DanceStacks`、`DanceSource` | int32、EIGDanceSource | 现有舞踏状态 |
| `AvailablePowderCount / ReservedPowderCount` | int32 / int32 | 当前注册表，用于调试/最小 HUD 提示 |

目标 `URes_InsectGlaive::GetUIState() const` 与动态 `OnIGResourceUIChanged(const FIGResourceUISnapshot&)`。保留原有四类资源委托，旧消费者不用迁移行为；新面板使用汇总事件避免部分状态先后回调造成闪灯。

- Resource 已有 Tick；在那里读取活跃 GE 时间，按显示值变化广播即可。Widget 自己不 Tick 资源，不让每个子 Widget 重查 GE。
- ApplyExtract、三灯建立/消费/到期、耐力/舞踏/猎虫状态/粉尘事务变更即时发布一致快照；transition guard 中间态不对外广播。
- UI 最近发出的快照只作显示去重，**不能**被 CanActivate/ApplyExtract 当作战斗真值。
- 面板 Bind 必须先订阅，再立即 GetUIState；切换面板不能等下一次点灯才显示已存在的灯。
- Triple 显示三色高亮和单独 Triple 倒计时，不能伪造三个单灯 GE。Single 独立倒计时，失效归暗；舞踏只显示真值层数/来源，落地马上清零。

原生资源面板提供 `BlueprintImplementableEvent void OnResourceUIUpdated(const FIGResourceUISnapshot& State)`；收到汇总委托或Bind初始快照后调用。Unbind清来源并向显示发送空快照；BP不用自己订阅Resource。准心新增 `BlueprintImplementableEvent void OnAimVisibilityUpdated(bool bVisible)`，与已有OnAimTargetUpdated组成完整显示接口。

### 7.5 准心

本轮最小方案：

| 状态 | 显示 |
|---|---|
| 无任何相关瞄准上下文 | 隐藏准心 |
| `Combat.State.Aiming.Action` | 屏幕中心中性准心；不据此点灯 |
| `Combat.State.Aiming.Kinsect` | 中心准心；合法 Hitzone按Red/Orange/White变色，未命中/墙遮挡为中性 |
| 两者都有 | Kinsect 合法目标颜色优先，中性为兜底；不影响动作方向快照 |
| Slinger 收刀上下文 | 本轮不新增投射器玩法/颜色语义 |

HUD 订阅 ASC Aiming 子 Tag 变化和 AimComponent.OnAimTargetChanged；Bind 时主动读完整当前目标/颜色（为 AimComponent 增加只读显示快照 Getter），关闭瞄准立即清旧颜色。不要用 CaptureAimLockSample 作为 UI Tick 数据源，也不要因为做准心修改镜头限角/快照时机。

屏幕中心锚点/Alignment=(0.5,0.5)，不通过 Actor 的世界位置决定准心。其数据与右肩摄像机位置、动作 AimSnapshot 不混为一体。

### 7.6 删除UISubsystem空壳

源码当前只有定义/静态Get，不足以证明蓝图无引用。M6.4先审计源码、Config、脚本，以及AssetRegistry的 `/Script/MHGZ.MHGZUISubsystem` 依赖和Blueprint节点；存在引用则只迁移实际消费点到HUD的读接口，并Compile/Save相关BP。清单为零后删除其.h/.cpp、清include并冷编译；不得先删Class导致Missing Class，再把错误当编辑器重启问题。HUD代码/子Widget不引入新的GameInstanceSubsystem工厂。

## 8. M6.4/E6.4：视觉 Cue 与伤害数字

### 8.1 事件出口

| 事件 | 发起者 | 最小表现 |
|---|---|---|
| 斩击/猎虫/暴击 | 已有 HitFeedbackRouter | 命中点短暂火花/闪光，无 Sound |
| 最终伤害数字 | 已有 Router，`RawMagnitude=ActualDamage` | 命中点屏幕朝向文本；不是 MotionValue/理论伤害 |
| 精华成功吸收 | Resource.ApplyExtract 的合法成功路径 | 对应颜色轻量提示；吞灯可提示但不改灯/计时 |
| 三灯形成 | 成功建立 Triple GE 后 | 一次高亮/视觉脉冲，不在每个 UI Tick 播 |
| 自然到期 | Resource GE 移除回调且非主动消费/Shutdown | 灯到期视觉，区分消费和到期 |
| Dodge | 现有动作精确事件出口 | 已有表现可保留；若无出口，在合法动作启动点一次发 Cue |
| 粉尘爆炸 | 成功 Commit 的集约 GA | 一个聚合爆炸视觉，不为每团各炸一次 |
| 粉尘命中 | 每个真实目标的 Router | `GameplayCue.Hit.IG.Powder` 及伤害数字 |

所有 Cue 是表现。不能在 Cue/Widget 内造成伤害、点灯、Consume、增舞踏；DynamicAssetTags 不自动等于执行 Cue。

资源视觉Cue的参数合同：Instigator/EffectCauser为当前Pawn，Location为角色/猎虫适合的反馈点；精华颜色放在本次Params.AggregatedSourceTags中，三灯/自然到期由明确CueTag区分。只能修改本次参数容器，不修改ASC的真实标签。不会从HitResult的部位名猜颜色，也不为播放提示执行第二次ApplyExtract。

### 8.2 数字实现合同

拟新增 `UI/MHGZDamageNumberActor.h/.cpp`（`AMHGZDamageNumberActor`）、`UI/MHGZDamageNumberWidget.h/.cpp` 与 `GameplayCues/MHGZDamageNumberCue.h/.cpp`（`UMHGZDamageNumberCue : UGameplayCueNotify_Static`）。

- `BP_DamageNumberActor` 继承 Actor，拥有 `UWidgetComponent`；Class Defaults 保存 widgetClass、显示秒数、上飘速度和颜色，不存在任何 DamageGE。
- `WBP_DamageNumber : UMHGZDamageNumberWidget` 暴露 BlueprintImplementableEvent `SetDamageDisplay(float ActualDamage, bool bCritical)`；BP 将最终整数文本写入 TextBlock。
- Cue使用Params.Location和RawMagnitude。当前Context不存最终暴击布尔值，因此仅在伤害数字这次调用里，由Router设 `NormalizedMagnitude = Result.bCritical ? 1.f : 0.f`；数字Cue按该值读取已结算暴击，不根据玩家CriticalRate二次抽签、不改变其他Cue的参数语义。在本地表现世界Spawn一次数字Actor。
- Actor 显示时间到自动回收/销毁，WidgetComponent随 Actor 生命周期结束；不由 HUD/GA/木桩 AddToViewport 文本。
- 第一版先用有界 Lifetime 的 Spawn/Destroy，不强制实现对象池；M7 压力测试证明需要时再做池。不能在这里加第三个长期 UI 工厂。
- 单次真实伤害只出一个数字，0伤害显示策略固定为不显示数字；不因为多个命中Cue重复生成数字。当前Router只按Health属性存在性发数字，本包应为数字分支补 `Result.ActualDamage > 0` 门槛，并同步相关反馈测试，不改变HitSignal/其他合法视觉Cue出口。

### 8.3 资产与技术边界

- `/Game/GameplayCues` 已在 DefaultGame.ini 的 GameplayCueNotifyPaths 中；把本轮 Cue 放在那里并回读注册 Tag，不改目录发现规则。
- 临时视觉足够：粉尘 BP 可用无碰撞球/半透明材质，命中与爆炸可用简单粒子/材质表现。既有 `UParticleSystem` 路径可复用接口，但不能把虫印粒子误当成权威 PowderActor。
- 当前 Build.cs 没有 Niagara 模块。若最终选择 C++ `UNiagaraComponent/UNiagaraSystem` 字段，必须在该工作包显式添加 Niagara 依赖并构建；不写指导让用户填写一个尚未声明的属性。
- 音效暂不制作、不赋新引用；已有 CombatConfig Sound 不清空。Cue 的声音槽为空是本轮可接受状态。

## 9. 自动化资产与检查工具

目标 commandlet `UMHGZM6AssetSetupCommandlet`（`-run=MHGZM6AssetSetup`）和审计脚本属于**待实现**，不能现在让用户执行不存在的命令。

工具合同：

1. 默认只读 `-AuditOnly`；显式 `-Apply` 才写。本轮允许写集合为新 GA/AM/Curve/Powder/UI/Cue 与上述四个虫棍 DA 的明确字段/新增边；禁止批量修旧 PSS/PSD/动画。
2. 更新前审计父类、Skeleton/Section/Root Motion/Notify/引用；不覆盖同名但不属本轮的资产，冲突立即报告。
3. 写入后重载并审计；第二次 Apply 无 diff。Audit 报告实际 path、原/新字段、输入与 Combo 边、对应 Notify 时间，不只打印“成功保存”。
4. 编辑器已有未保存资产时停止外部写入，先保存/关闭或采用编辑器内接口；不能用 commandlet 覆盖内存中未保存的 ABP/DA。
5. Python 不能读取 Notifies/AnimNotifyTracks 的 protected 反射字段；涉及完整通知审计由 C++ commandlet 读取。不能把 Python 零发现当作无通知。
6. 能脚本完成的 CreateAsset、字段写入、常驻 Montage、Chord/Combo、轨道命名和数据校验优先自动化；用户负责姿势、时机和 UI 布局目視确认。

目标新增/改动资产清单（不存在的标为待生成）：

- `/Game/Blueprints/Ability/InsectGlaive/GA_IG_AwakenedKinsectAttack`、`GA_IG_PowderVortex`。
- `/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_AwakenedKinsectAttack`、`AM_IG_AwakenedKinsectFall`、`AM_IG_PowderVortex`。
- `/Game/Weapons/InsectGlaive/Data/Curves/` 下本招有证据的曲线；不直接覆写 `_mhr_frames`/历史生成表。
- `/Game/Weapons/InsectGlaive/BP/BP_IG_PowderCloud`（创建时先核对本项目同类 Actor 目录，如已有规范冲突沿用该目录并同步文档，不能出现两份最终类）。
- `/Game/UI/` 下 E6 指导所列 HUD/Widget/数字资产。
- `/Game/GameplayCues/` 下本轮视觉 Cue。
- 当前 `DA_IG_Combat`、`DA_IG_InputProfile`、`DA_IG_Combo`、`DA_WeaponRuntime_IG` 只追加本轮字段/边；旧 Combo 边保留，不重新生成整个表。

## 10. 记录系统与验收证据

### 10.1 延用现有捕获命令

```text
mhgz.Telemetry.SampleRateHz 60
mhgz.Telemetry.FlushRows 60
mhgz.Telemetry.Enable 1
// 执行本轮操作
mhgz.Telemetry.Enable 0
```

这套命令与 `Saved/RuntimeTelemetry/<session>/` 当前分类保留。只有调查 MM 竞争时才额外启用 `mhgz.Telemetry.PoseSearchDetail 1`，普通粉尘/UI 核账不强制开第二档。

### 10.2 补事件流，不靠 60Hz 快照推算次数

沿用现有记录启停/会话目录，拟新增：

| 文件 | 记录 |
|---|---|
| `Combat/Events.csv` | TripleReserved/Consumed/Rejected、KinsectDamageSubmitted/Rejected、ExtractAttempt及吞灯结果、PowderSpawn/Reserve/Release/Expire/Consume、VortexDamageSubmitted |
| `UI/Events.csv` | HUDCreate/Destroy、PawnBind/Unbind、RuntimeReady/Invalidated、ResourcePanelCreate/Remove、旧事件拒绝、DamageNumberSpawn/Expire |

每个事件至少有 SessionID、WorldTimeSeconds、Frame、单调 EventSerial、RuntimeGeneration、ActivationSequenceID；战斗事件再带 FlightID/SourceAttackID/HitID/PowderID/ReservationID/ExplosionID、TargetActor/Hitzone、成功原因、成员数/伤害；UI事件带 PC/Pawn/Widget 路径及绑定 Token。

事件在发生点写入现有捕获器的队列，停止时 flush；不能按60Hz抽样丢掉同帧三次贯通命中。捕获关闭时不分配持续增长的日志缓存。更新会话 README 解释新目录、单位、ID关系和拒绝原因。

不得把 GA/UI 业务绑定到 AnimInstance 的存在上：记录器可以收集事件，没开 Telemetry 或没有 AnimBP 时，粉尘与 HUD 仍正常运行。

### 10.3 必须可自动核账

```text
一个成功觉虫击激活 → 旧Triple Consume成功次数 = 1
每个成功Piercing HitID → ExtractAttempt = 1，PowderSpawn = 1
失败HitID → ExtractAttempt = 0，PowderSpawn = 0
每个PowderID → 最多一次Consume；不能同时属于两个Reservation
每个ExplosionID + TargetActor → 成功伤害提交 ≤ 1
取消Reservation → 无Explosion；未过期成员恢复Available
每个LocalPC → HUD根实例 = 1；ResourceSlot子实例 ≤ 1
旧Generation UI事件 → 不改新Panel/新Pawn绑定
```

三灯形成后 ExtractAttempt 可以是“吞灯成功”，不要求每次都产生新 GE。Spawn失败/GE异常必须单列，不能让计数报告把错误当作满足一一对应。

## 11. 自动化测试、构建和人工验收

### 11.1 测试归档

新增测试放 `Source/MHGZ/ActionSystem/Tests/M6/`；按 Resources、AwakenedKinsect、PowderVortex、UI、Feedback 子目录组织，不把 M6 测试塞回 M1 目录。

| 测试组（目标命名） | 最低覆盖 |
|---|---|
| `MHGZ.M6.Resources` | 三灯消费结果/过期/重复/争抢；粉尘状态机、寿命、Token、shutdown |
| `MHGZ.M6.AwakenedKinsect` | 前置失败零副作用、冻结Aim/Clamp、逐击正确点灯/粉尘、间隔、正常/异常Flight、位移交接 |
| `MHGZ.M6.PowderVortex` | 选择/上限/取消/过期/重入、一次爆炸、按Actor去重、公式、失败目标 |
| `MHGZ.M6.UI` | Ready早到/晚到/重复/失败回滚、UnPossess/EndPlay/Generation、初始同步、Delegate泄漏、插槽实例数 |
| `MHGZ.M6.Feedback` | 最终伤害/真实Hit/只发一次数字、无声音依赖、Cue无战斗副作用 |

测试桩优先放 Tests/M6 的独立类型，不新增无用途的生产 PlaceholderAbility；迁移成本 bool 所需现有 M1 桩同步，但不删除已签收回归。

### 11.2 验证流程

1. 关闭编辑器后 Development Editor 冷编译。引擎路径先从本机安装查实；UBT日志指定工作区 `Saved/M6Audit`，不依赖不可写的用户默认日志目录。
2. 跑本包专项 → `MHGZ.M6` →完整 `MHGZ` 回归，记录实际计数/退出码。DDC使用本项目已有工作区路径；按最近成功方式使用 `-RenderOffscreen`，不反复重试已知崩溃的 NullRHI 虚拟窗口组合。
3. 只读本轮资产审计、蓝图 Compile、阶段资产及其依赖 DataValidation。完整 Content/打包最终仍由 M7负责；不能把无关旧资产警告当成 M6 的新需求。
4. E6各包PIE一次覆盖对应矩阵，录制并分析。正常速度必测；复杂飞行/聚拢取消可补 `slomo 0.1`，不要求用户帧级按键控制。
5. 非音效验收后更新本文 §12 与总体门禁；提交Git由用户另行授权，本轮文档编写不自动提交或push。

### 11.3 E6.5 固定矩阵

| 场景 | 合格条件 |
|---|---|
| 非三灯/无猎虫/缺必需配置 RTYB | 不消费、不进入攻击态、不播放半个动作；可继续正常操作 |
| 三灯 RTYB 空放 | 旧三灯只消费一次；猎虫和猎人按各自请求运行，最终回地面/空中正确，不残留位移者 |
| 贯穿三色木桩部位 | 每成功Hit一次点灯/粉尘；可重新形成三灯；后续吞灯不刷新 |
| 连续命中同部位 | 满足间隔才再次伤害，成功ID不同，不丢首击、不按帧率改变频率 |
| 觉虫击中断/卸装/运行期结束 | 本Action移动/回调清理；已付成本不返还；旧请求不污染新Runtime |
| 普通武器/放虫接触粉尘 | 不引爆、不消费、不新增精华/伤害 |
| LTYB 零粉尘/无猎虫 | 激活失败，无Reserved残留，后续输入正常 |
| 超收集范围/数量，多团与不同拥有者 | 仅选本玩家范围内MaxCount团，选择稳定 |
| 正常聚拢爆炸 | 事务只提交一次；多Hitzone木桩只一跳聚合伤害/一个数字 |
| 聚拢中取消、自然到期、重复取消 | 仍存活成员回原位/Available；到期不复活；不爆炸 |
| HUD 初始/最大值变化/精华到期/落地 | 主动同步，无需先变值；比例/倒计时/舞踏立即正确 |
| LT准心/RT准心/墙遮挡/结束瞄准 | 中心位置正确，颜色来自合法Hitzone，关闭清色，不影响镜头/快照 |
| 换Pawn/Runtime重建/再次PIE | 根HUD一个、资源面板最多一个；旧订阅/Widget/粉尘/数字无泄漏 |
| M5最小回归 | 普通放/收虫、收拔刀/翻滚、空回→操虫斩/急袭突刺、真实落地仍正常；无新增ForceIdle死区 |

## 12. 当前门禁与下一条可执行任务

| 工作包 | 本文生成时的状态 |
|---|---|
| M5 | 已签收；不重开 |
| M6.1～M6.5 | **均未实施**；本轮只是制定计划 |
| E6.1～E6.5 | **均未实施/未签收** |
| 音效 | 明确排除本轮，延期；不作为本轮入口/出口条件 |
| 普通猎虫回手精华 | M7 前独立欠项，不能由本轮即时点灯替代 |

**下一条代码/自动化任务：启动 M6.1。** 先只读导出四个DA的实际接线和觉虫击动画/曲线审计；确认本轮接口与数据缺项后，实现成本结算 bool、粉尘基础类型/Actor/Resource事务及测试。无需先新建 Widget；也无需为进入 M6重新验收已通过的急袭突刺。

M6.1交接报告必须单列“已冻结”和“仍待确认”：尤其是196时长、206表现尾段、集约源动画、猎人Flight运动模式/数值。基础事务可先通过；有这些缺项时不能把M6.2的GA/资产出口标绿，也不能让后续AI用默认值补完后宣布通过。

每包完成后在此记录日期、实际文件、验证命令、日志路径、PIE会话和剩余项；禁止只把状态改成“完成”却没有对应证据。
