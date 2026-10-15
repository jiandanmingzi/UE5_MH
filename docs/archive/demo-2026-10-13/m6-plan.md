# M6：觉虫击、虫印/虫粉、粉尘聚爆、HUD与视觉反馈实施计划（不含音效）

> **阶段档案（2026-10-13）：** Demo 已阶段性完成。本文件保留开发期方案、决策和验收记录；其中“当前入口/下一步/待验收”均是当时快照，不自动成为维护期待办。后续验证遵循[维护规范](../../using/README.md)；未来未实现内容仍按原文保留。

> **2026-10-13用户确认与当前入口：** Demo五项收尾第1项（近期修复及E6.3真实PIE）与第2项（普通猎虫采集→召回→交付）已完成并签收。当前开启第3项 **E6.4，仅伤害数字**；其他命中/暴击特效、精华/三灯提示与粉尘爆炸视觉Cue延期，不作为本轮数字或Demo验收门禁；音效继续排除。开启不等于数字签收，第4项M6.5/E6.5总验收及第5项M7/E7验证/打包尚未完成。下方较早“E6.3待PIE、普通回手欠项、E6.4不开启”均为历史快照，由本条覆盖。

> **最新精华合同修复（2026-10-13，待PIE）：** 用户明确强化觉虫击不能贯通命中即给角色点灯，须先由真实猎虫携带不同精华，回手后一起交付。已将强化请求改为 `CollectPerValidHit`，成功伤害按红白橙去重收集，实时发布完整 `KinsectPendingExtractColors`；逐击伤害、间隔与产粉不变。普通猎虫/操虫斩仍单色；三色椭圆服务当前强化多色与未来双色猎虫，一/二/三色全填/二分/三分，回手清空。原“途中立即ApplyExtract/形成三灯、实际最多携带一种”的文字仅为旧合同，不再指导新实现。编译成功；最终相关回归 **177/177通过**（54无测试警告、123带测试警告，0失败/未运行），报告 `Saved/M6Audit/AwakenedCargo20261013Verified/index.json`；真实WBP的一/二/三色GPU绘制测试包含在本组。无需改资产或接线，短PIE见[E6指导§3.19](../../editor/m6-setup.md#319-强化觉虫击多色携带与回手交付2026-10-13)。保留E6.1/E6.2历史签收，不自动签收本轮或M6。

> **最新建印来源扩展（2026-10-13，待针对性PIE）：** 四连印斩已有 `UMHGZMarkSlashAbility` / `MarkSegmentIndex=1`，用户确认指GA第二个伤害段（约1.033333～1.283333s），不是时间线上第二个ConfigIndex=0窗口。觉虫击新增猎人199冲刺ConfigIndex=0成功武器伤害建印，普通/三灯均适用，调用位于原有下一帧189舞踏交接之前；猎虫自身命中不走此入口。两者共享现有90s唯一局部虫印，不改伤害、窗口、通知、曲线或资产。Development Editor编译成功；M2/M3/M5/M6＋M4翻滚/预输入最终回归 **176/176通过**（54无测试警告、122带测试警告，0失败/未运行），报告 `Saved/M6Audit/AdditionalMarkSources20261013Verified/index.json`；只读资产审计 `AdditionalMarkSources20261013-asset-audit.json` 成功。短PIE见[E6指导§3.18](../../editor/m6-setup.md#318-四连印斩与觉虫击冲刺建印2026-10-13)。E6.2历史签收保留，不自动签收新效果或E6.3/M6。

> **最新定向修复（2026-10-12，优先于较早快照）：** 用户确认最新录制的命中问题已解决；新增强化操虫穿刺落地A导致“攻击取消、翻滚不播”已定位为同步中断抢先清空交接，急袭突刺Over同类缺口一并修复。最终构建及M2/M3/M5/M6＋M4翻滚/预输入回归174/174通过，反例验证两招均复现原问题。资产、通知/窗口、预输入时长及曲线不变，短PIE见[E6指导§3.17](../../editor/m6-setup.md#317-强化操虫穿刺急袭突刺地面收尾翻滚交接2026-10-12)，仍待用户确认。M5历史签收保持，E6.3/M6不自动总签收。

> **最新修复入口（2026-10-12，待PIE）：** 木桩旧内嵌部位球导致觉虫击擦击未生成候选；现三段已改为完整覆盖柱面/Body的盒形查询，攻击/猎虫/准心/虫印/粉尘统一解析部位。全项目覆盖要求已写入总规划§1.6。Development Editor编译通过，M2/M3/M5/M6 **164/164**（含完整M6 49/49）通过，DA回读/验证1/1有效且`changed=false`；旧非UI失败已通过显式夹具初始化和测试半径修正关闭，不修改生产玩法。范围、证据和PIE步骤以[E6§3.16](../../editor/m6-setup.md#316-觉虫击擦击漏判受击表面覆盖修复2026-10-12)为准；下方35/37、39/39等为较早快照，不再代表当前静态成绩。M6/E6.3仍待用户真实PIE签收，未宣称所有视觉漏判已实机解决。

> **当前追加纠正（2026-10-12，待PIE）：** 所有普通空中落地统一已有`AM_IG_AerialLanding`，觉虫击低空199不使用专用落地，不重生成/裁剪通用资产，见[E6§3.15](../../editor/m6-setup.md#315-所有普通空中动作统一落地2026-10-12)。−50°角度补丁与0.30s裁段撤回；§3.14真实地面目标连续路程、小抛弧及地面约束保持。距离公式是项目还原策略，原作未记录目标点，尚不声称已确定内部机制；见[真值§7.2.3](../../reference/操虫斩曲线.md#723-当前低角度策略地面落点连续路程与小抛弧2026-10-12)。既有地面攻击段和签收不回滚，当前运动观感仍待PIE。

> **最新运动修复（2026-10-12，待PIE）：** 最新MHR录制20段199均保持约0.38s；旧PIE贴地冲刺约0.033s被Falling落地回调终止。199已改用现有保护飞行曲线租约，碰地/墙只裁位移，伤害成功仍可进189；曲线末端实查地面再回Walking或206。4826cm/s、1190cm/s²、0.38s保留，未改通知/攻击窗口/瞄准/相机/206/189。真值及分类见[曲线§7.2.1](../../reference/操虫斩曲线.md#721-2026-10-12追加199贴地不能立即结束)，复验见[E6§3.12](../../editor/m6-setup.md#312-觉虫击199贴地起飞立即结束修复2026-10-12)。下方早期Falling模型描述由此修正，M6/E6.3仍未总签收。

> **当前修复入口（2026-10-12第二次录制之后）：** E6.3仍待签收。普通伤害型放虫须真实接触，不再提前以大到达容差停止；物理猎虫胶囊固定直立、Mesh独立转向。操虫斩改为真实单精华携带→回手交付；觉虫击成功发动清空所有已有灯，仅完整三灯强化。椭圆放大，D/P调试文字移除。觉虫击补攻击窗口末帧/前冲退出采样，并记录真实候选门禁和区域几何；**视觉接触漏判剩余根因尚未完整裁定，不能宣布全部解决**。本轮代码/验证及短PIE步骤以[E6指导§3.11](../../editor/m6-setup.md#311-放虫接触贴地接管精华链路追加修复2026-10-12)为准；下方39/39为前一轮历史快照，相机/资产通知/速度曲线未改。

> **最新进度（2026-10-12，追加修复已实施，待PIE签收）：** 已修三灯后图标全灭、臂上放虫平行偏靶和觉虫击起点视差，携带图标改为竖立椭圆；保留上一轮初始化/准心修复。Development Editor编译成功，UI/瞄准/觉虫击/基础猎虫/自动寻印/聚爆定向回归39/39（UI 6/6含GPU实际WBP绘制）通过，证据与复验见[E6指导§3.10](../../editor/m6-setup.md#310-三灯携带图标与放虫瞄准追加修复2026-10-12待pie)。五张永久原图仍为同文§3.0视觉合同。不重生成或接线资产。

> **2026-10-12 当前状态：** E6.1/E6.2历史签收保留；E6.3仍待追加修复后的真实PIE签收，离屏截图不代替验收。相机位置/管理、快照采样时机、单精华玩法未改；本轮改变冻结目标到飞行方向/起点的解析，须回归觉虫击瞄准。完整`MHGZ.M6`此前35/37，本轮39/39是跨阶段定向集合，不代表重跑完整M6；两个非UI既有失败仍须总验收前处理或裁定。E6.4/E6.5未完成，M6总验收未关闭。

> **历史快照（2026-10-10；E6.2 后来已由用户签收）：** M5及觉虫击自身的运动、三灯、召回、命中舞踏签收保留。虫印视觉为约1m三维球状雾团，Gameplay表面覆盖100×100cm（`DA_IG_Combat.KinsectMarkPatchHalfExtent=50cm`），不随目标组件Scale额外膨胀。虫粉视觉包络半径75cm；普通到期爆炸伤害半径150cm，聚爆两段伤害半径300cm。虫印持续时间90s、法向容差5cm及虫粉寿命/收集范围等其余规则不变。新增聚爆规则：猎虫抵达聚爆中心、开始吸粉时，范围内虫印立即清除，不播放虫印被吸引的表现；此后取消聚爆也不恢复虫印。代码冷编译、DA/视觉资产只读审计、3D粒子离线渲染及DataValidation 628资产0错误/0警告通过；`MHGZ.M6.Mark`整组为3/4通过，另1项可见网格表面投影测试失败，和本次范围调整无关，仍需单独处理。当时 PIE 尚待用户目视；该状态已被上方当前入口取代。离线验证不能替代真实关卡验收；动画源仍是`AS_Unsh_CaoChong`，不需重录F9。
>
> **范围：** 觉虫击、局部虫印、猎虫自动攻击/玩家指令仲裁、虫粉生成与自然爆炸、粉尘聚爆、健康/耐力/虫棍资源HUD、准心、伤害数字及视觉Cue。**排除全部音效工作**：不提取/配置新Sound，不把音效作为门禁；已有声音引用/代码保持，不清空必填字段。
>
> 代码/脚本工作统一用 `M6.x`，编辑器/资产接线及 PIE 工作统一用 `E6.x`。它们是主 M6 的子工作包，不是重新执行主 M1～M5。总体状态唯一入口仍为[阶段门禁](../../design/milestone-gates.md)；具体编辑器步骤见[M6 编辑器指导](../../editor/m6-setup.md)。

## 1. 任务合同与阅读顺序

### 1.1 真相源

| 内容 | 真相源与用途 |
|---|---|
| 当前实现 | `Source/MHGZ`、`Content`，以及实际构建/审计/PIE 结果；文档写有类名不等于类已经实现 |
| 当前玩法 | [虫棍动作§5.3～5.4、§5.6、§6.1](../../design/insect-glaive-actions.md)：虫印、虫粉/聚爆、觉虫击、舞踏来源；新合同实施细节见本文§6 |
| 通用所有权 | [冻结实施计划](../../design/demo-implementation-plan.md)、现有 `FWeaponActionToken`、RuntimeHost、MovementTask |
| UI 归属 | [UI 系统](../../design/ui-system.md)：HUD 唯一所有者、ResourceWidget 插槽、委托驱动 |
| 原作资源与测量 | [资产序号对照](../../reference/资产序号对照.md)、[操虫斩/觉虫击曲线 §7～8](../../reference/操虫斩曲线.md)、[下落物理](../../reference/下落物理.md) |
| 本轮详细执行顺序 | 本文；与旧 E6 概括步骤不一致的字段/点击细节，以本文及配套 E6 指导为准 |

实施者必须亲自读取本轮涉及的函数，不根据类名猜行为。进入一个子工作包前只读取它的依赖和指定源码，不重跑旧资产修复 commandlet，也不扫描无关对话。

### 1.2 不变量

1. GAS ASC 保持在 PlayerState；Resource、猎虫、粉尘属于当前 Pawn 的武器运行期。HUD 属于本地 PlayerController。
2. 输入经现有 InputProfile → Router → Combo → GA；不能在 Character 蓝图另绑 RTYB/LTYB。
3. 所有动作使用**常驻 Montage 资产**。缺失资产时预检失败，不在运行时临时拼 Montage。
4. 猎人移动只用现有 `UAbilityTask_MHGZWeaponMovement`/RuntimeHost ownership；表现 Root Motion 与程序位移不能同时驱动胶囊。
5. 猎人已获得的三灯/精华以 Active GE Handle 为真值；尚未交付的精华以真实AKinsect携带容器为真值；粉尘以Resource事务和Actor状态为真值。UI只显示，不写资源或Loose Tag。
6. 觉虫击按是否原子消费三灯选择猎虫模式。强化贯通成功伤害先收集不同颜色并产一团虫粉，角色在猎虫回手后才点灯；产粉绕过虫印10s冷却。普通猎虫/操虫斩仍单色，局部虫印命中可受冷却产粉。同一HitID最多产一团；伤害失败不采集/不产粉，同色重复不加携带格数。
7. 虫粉不被武器/猎虫接触直接引爆；180s自然到期产生半径150cm的一段爆炸，粉尘聚爆产生半径300cm的两段爆炸。每个伤害阶段各自对一个目标Actor只结算一次，不能跨阶段去重吞掉第二段。
8. 重复 Notify、EndAbility、解绑和取消均幂等；旧 Runtime/Action/Flight/Reservation 身份不能修改新一轮状态。
9. 不新增 GA 后强制 Idle 时间段、输入死区，不修改普通 MM 的 Start/Stop/挡位算法掩盖退出错误。
10. 不增加已移出 Demo 的降龙/强化跳跃斩，不扩展空中受击、死亡、台阶或其他武器玩法。生命周期关闭/强制取消仍必须清理，使用自动化测试，不等于制作这些玩法。

### 1.3 本轮与后续验收的边界

- 本文关闭的是 **M6 非音效工作**；音效明确延期，不能把尚未制作的音效写成已完成，也不能因此阻止本轮非音效验收。
- 普通猎虫采集→召回→交付独立PIE已由用户于2026-10-13确认完成；后续只在相关路径改变时回归，不再作为M7前欠项。
- 本轮不重新要求人工精确控制几百分之一秒的按键释放。组合键/到期/重复回调的边界以自动化覆盖，PIE 使用正常操作。
- M5 的历史测试计数不是 M6 成绩。每次记录实际运行的数量、日志和资产审计结果。

## 2. 当前基线：已有与缺失

| 模块 | 实际状态 | M6 的处理 |
|---|---|---|
| `URes_InsectGlaive` | 已有三灯/单灯 Handle、`ApplyExtract`、三灯预留和消费、猎虫伤害、舞踏与四类资源委托 | 扩展粉尘及只读 UI 快照；保留现有行为 |
| `AKinsect` | AlongDirection/ToPoint、SingleHit/Piercing、FirstHitOnly/CollectPerValidHit、逐Hitzone限频与真实Sweep | 成功贯通收集不同色；普通单色；回手原子取出完整携带容器；保留显式产粉策略 |
| 成本结算 | 原接口无结算结果，GA 无法知道非空预留是否真正结算 | **M6.1 已改为 bool**；Host/Resource 验证运行期、激活和预留身份，失败时 GA 不确认/注册 Action |
| 虫印 | M6.3.1两种近战来源与M6.3.2离手自动寻印/攻击、玩家指令仲裁均已通过代码测试 | E6.2已连接正式虫印粒子；默认搜索范围3000cm、自动攻击间隔2s，PIE核对行为 |
| 觉虫击/粉尘聚爆 GA | 觉虫击已签收；正式`GA_IG_PowderVortex`/Montage/Combo资产已按新合同只读审计通过；聚爆Montage使用与送虫/收虫相同的`UpperBody_IGAction`槽 | 聚爆运行时代码、资产和DA接线均已确认；到达后默认1s、两段伤害及操虫/收虫取消仍需PIE核验 |
| 虫粉 Actor/注册表/事务 | M6.3.3统一成功命中入口、10s共享冷却、180s寿命、自然爆炸/伤害快照、远距移除与100团上限已实现；M6.3.4事务/两阶段伤害已通过测试。E6.2已连接正式`BP_IG_PowderCloud`/粒子及DA参数 | 完成PIE核对生成、自然爆炸、聚拢取消/提交及伤害反馈 |
| CombatConfig | `DA_IG_Combat`已连接正式粒子/Actor并写入当前调试默认值；可调字段和单位见§6.7 | PIE后按观察调值；不要重新运行Apply重置用户调整 |
| RuntimeHost | 已有 `OnWeaponRuntimeInvalidated`、当前 Token/Context/ResourceProvider；**没有 Ready 委托** | M6.4 新增成功装配后的 Ready 信号与可读就绪状态 |
| `AMHGZHUD` | `BeginPlay` 只有 Super；不创建 HUD | 实现唯一 Widget 树和绑定生命周期 |
| Widget 基类 | `BindToASC`/`BindToResourceComponent` 只保存引用；无完整解绑 | 保持入口兼容并完善；新增主 HUD/属性条/虫棍资源显示原生类 |
| AimComponent | 已有 Kinsect 瞄准、颜色委托、输入时快照及独立的晚锁定采样 | UI 订阅、初始同步；不把觉虫击改为晚锁定 |
| HitFeedbackRouter | 已显式执行命中/暴击/伤害数字 Cue，携带真实 Hit 和最终伤害 | 实现缺失的视觉资产与数字显示，不造第二条伤害反馈链 |
| `UMHGZUISubsystem` | AssetRegistry中类对象/静态Get引用均为0；源码空壳已删除 | 保留只读回查commandlet；不恢复为Widget工厂 |
| `Content/UI` | E6.3五个Widget已生成并链接；基础布局存在，引用回读确认 | 生命周期修复后仍需PIE确认准心可见/按住LT或RT切换及属性条风格；E6.4另行接线 |
| `Content/GameplayCues` | 当前伤害数字/Cue代码接口已实现，E6.4资产接线尚未完成 | E6.4制作/接线，不改变既有伤害反馈链 |
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
| 1 | **M6.1 基线审计、资源事务与数据合同** | M5 已签收；本轮范围确认 | **已完成（2026-10-06）**：资产/数据清单、成本结算成功信号、粉尘 Actor/事务、数据校验、专项测试和全量回归；不签收招式 |
| 2 | **M6.2 觉虫击代码与资产生成** | M6.1；F9世界运动数据已足够冻结可调候选 | GA/Notify类、猎虫成功命中策略、移动/退出链、幂等常驻资产生成及专项测试；Montage精确源段与点Notify位置由E6.1确认 |
| 3 | **E6.1 觉虫击资产核对和 PIE** | M6.2 冷编译及只读审计通过 | 用户确认 Montage 姿势/速度/通知；三灯消费、贯通、点灯、粉尘、退出证据 |
| 4 | **M6.3 虫印—自动攻击—虫粉—粉尘聚爆代码改造** | M6.1基础设施、M6.2/E6.1觉虫击动作已签收；§6新规则冻结 | M6.3.1～M6.3.6已完成且E6.2历史签收保留；现含四种近战建印来源，2026-10-13扩展仅做针对性PIE复验 |
| 5 | **E6.2 虫印/虫粉/聚爆资产和PIE** | 新M6.3代码/配置迁移/资产审计通过 | 两种粒子、90s/180s、局部产粉、指令抢占、到期爆炸与两段聚爆均有真实PIE证据 |
| 6 | **M6.4 HUD、资源显示与视觉反馈代码** | M6.1 数据合同；不要求音效 | Ready、UI 快照、Widget 生命周期、准心、数字/Cue 接口；UI 测试通过 |
| 7 | **E6.3 HUD/准心编辑器接线** | M6.4 已编译 | HUDClass/WidgetClass/插槽连接，资源面板由 None 改为最终类，UI 功能 PIE |
| 8 | **E6.4 伤害数字资产接线与PIE** | E6.3及普通回手闭环已由用户确认完成 | 数字Widget/Actor/唯一Cue已接线；其他视觉Cue/音效延期；待短PIE |
| 9 | **M6.5 静态回归和证据审计** | 本轮代码/接线已完成 | 实跑构建/测试、阶段资产验证、Telemetry 分析、剩余问题列表 |
| 10 | **E6.5 Demo总验收** | M6.5无阻塞项；E6.4数字已签收 | 综合矩阵与用户目视通过后进入M7；其他视觉Cue/音效延期，普通回手已签收 |

M6.4 在 M6.1 合同稳定后可提前实施；同一文件只能有一个写入者。默认按表顺序执行，不能因为主 UI 完成就宣布觉虫击/集约完成。能用脚本生成/审计的资产由脚本完成，仅把必须看画面或选择未确认素材的工作交给用户。

## 4. M6.1：数据、资源与粉尘基础设施（已实现基底）

本节记录现有接口和历史实现。2026-10-08新增的虫印、产粉、寿命/爆炸和聚爆合同以§6为准；基础设施已实现不等于新闭环已通过。

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
| 189 | `AS_Unsh_CaoChongZhanJueChongJiWuTa` | 觉虫击猎人冲刺武器伤害成功后复用操虫斩常驻 `HitDance189`；沿用已验证 Dash→Dance 入口运动参数 |

**现行规则边界：** 本轮已授权觉虫击新增独立 `EIGDanceSource::AwakenedKinsect`。只有199猎人冲刺期间、AttackSegments[0]（棍尾 `IG_RearTip`→`Root`）成功应用伤害 GE，才排队复用操虫斩 `HitDance189`；不把命中伪装成 `KinsectSlash`。此授权不放开强化操虫穿刺的来源限制；它仍只接受操虫斩舞踏来源。觉虫击不另造189蒙太奇/曲线，Dash→Dance 的准备时间、混合时间与位移参数从已验收的操虫斩 GA 复制并审计相等。

**已有曲线不等于可直接消费：**

- `Saved/_mhr_frames/193_绝虫击起手.csv`、`195_绝虫击丢虫.csv`、`199_绝虫击突进飞行.csv`、`206_猎虫滑翔命中以及觉虫击未命中的下坠段.csv` 是逐样本证据。
- `Saved/_mhr_curves/觉虫击_199.csv` 是现有代表曲线，不是已验证的任意方向完整规范轨迹；不得把其中 `y_cm` 当 UE 的前向或直接复制为世界 Z。
- MHR 世界竖直为 Y，UE 世界竖直为 Z；资产 Root 的局部轴又是另一层转换。保持本项目已确认的骨架轴向，不强行旋转 Skeleton 或烘焙成向上爬/浮空。
- `player_motion_old_id` 段时长是游戏动作状态的观测区间，**不是 UE 序列帧号，也不必等于动画序列全长**。最新 F9 测得 193 状态约 1.167–1.175s（蓄力保持时长随玩家输入）、195 约 1.625–1.634s、196 仅 1 个采样（0.008–0.009s，像状态边界标记而非持续播放段）、199 约 0.375–0.384s、206 约 0.850–1.108s。不得把 196 的 1 个样本解释成其 UE 动画应播放约 1 秒。
- 导入序列可能为 0.5 倍速。需要时离线烘焙真正 2x 的序列；要检查端点、Root 位移、手指/武器姿势，不只改 RateScale。最终 Montage 各段 PlayRate=1。
- 不因本机原始 CSV 缺失，重跑全库生成器覆盖已入库的历史真值。可以做本招定向只读复算；缺少的阶段数据明确记录待补。
- **2026-10-06 F9 录制与脚本审计：**两份新录制 `mhrise_20261006_202708_01_samples.csv`、`mhrise_20261006_205745_02_samples.csv` 共包含 11 条 193→195→196→199→206 完整链，另有 1 段额外 206。旧动作 ID、位置、速度、朝向、相机和摇杆数据均可读；`motion_l*_frame` 与 `motion_l*_id` 在这些目标段仍为空。因此不要求 F8，也不再把“缺 199/196 ID”列为阻塞；仍不得用 ID 时长推测动画内部源帧。机器可读报告：`Saved/M6Audit/m6_action_motion_capture_audit_latest.json`；运行命令见 `Scripts/MHRise/README.md`。
- **新录制的运动估计（原始 MHRise 坐标，Y 向上；距离已换算 cm）：**11 段 199 的下一状态间隔为 0.375–0.384s；水平位移 1515–1725cm、竖直位移 +324–830cm、段内平均速率 4564–4721cm/s。排除 ID 切换的首个零速样本后，11 段中 10 段竖直速度线性拟合加速度为 −1267 至 −1145cm/s²（中位数约 −1190cm/s²）；另 1 段为 −2996cm/s²且 R²≈0.50，必须保留为离群/不同条件待查，不能静默剔除。11 段拟合总初速中 10 段为 4785–4901cm/s，另 1 段为 4504cm/s；用 193 起手时冻结的相机方向与 199 总位移方向比较，10/11 段夹角不超过 14.2°，1 段为 27.6°。因此代码可用“激活时冻结并限幅的 Aim 方向 × 约 4.8k cm/s”作为**可调候选基线**；保留离群样本并把总速、方向、飞行时长、重力暴露在 CombatConfig，不把拟合结果宣称为最终真值。MHRise 的 Y-up 轴在导入/分析时转换为 UE Z-up；UE 运行时的 Aim 已是 UE 坐标，不再重复旋转轴。
- **新录制的 206 运动估计：**12 段状态时长 0.850–1.108s，竖直位移 −319 至 −821cm；排除入口首样本后的竖直速度线性拟合为 −2942 至 −2828cm/s²、R²约 0.94–0.99。入口拟合速度约 +821 至 +986cm/s；原始边界行常记录为零，故实现时必须明确“接管速度”语义，不能把回归截距直接当成无条件真值。206 持续时长随高度/触地变化，最终出口必须是真实 CMC 触地，不可固定时长切 Idle。

**M6.1审计的历史快照（2026-10-06）：** 当时导出了193/194/195/196/199/206/189的资产元数据及四个DataAsset接线，但没有确认粉尘集约动作源。该待确认状态已被2026-10-08用户确认和Montage只读审计取代：粉尘集约复用`AS_Unsh_CaoChong`，新Montage/GA/Combo边已生成。不要把这段历史缺口当成当前阻塞；审计原始报告仍保留在`Saved/M6Audit/m6_animation_audit.md`与`Saved/M6Audit/m6_baseline_audit.json`。

当前审计证明的是动作状态边界与胶囊运动，不等于完整复原动画源帧。193 的时长由按键保持决定，不再强行与整段序列对齐；193 后半段循环候选第220帧已有脚本指标，曾作为E6.1接缝核对依据；E6.1现已签收，不是待做项。195 F9 状态时长 `1.625–1.634s` 与 UE 序列 `3.2333s / RateScale 2` 的有效全长 `1.6167s` 接近。199 F9 状态时长 `0.375–0.384s`，但 UE 序列有效全长约 `1.4667s`，存在约 3.9 倍的时长差；因此不能把整个 199 序列按默认速率直接当成游戏状态段。196 在 F9 中仅一帧左右，不能据此把 UE 196 全段作为独立等待段。206 动作状态随真实下落/触地变化。目标 CSV 的动作层帧字段为空，故199内部裁剪范围、196姿势片段选择仍未确认；这些属于动画/Montage资产映射，不再阻止先完成基于 F9 世界运动的 C++ 接管实现。详细资产审计见 `Saved/M6Audit/m6_animation_audit.md`，F9逐段结果见最新 JSON。

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

以下是M6.1及旧M6.3已落地的基础类型/字段快照，不是新合同完整接口。旧聚爆GA/资产存在；局部产粉、自然爆炸和两段聚爆仍须按§6迁移，UI另按后续工作包实施。

| 文件/类型 | 责任 |
|---|---|
| `InsectGlaive/IGPowderTypes.h` | `EIGPowderState`、`FIGPowderReservationToken`、只读粉尘快照与聚合结果 |
| `InsectGlaive/IGPowderActor.h/.cpp` → `AIGPowderActor` | 世界表现、自然到期、受控状态；不查找玩家、不自行结算伤害 |
| `AttributeSystem/Res_InsectGlaive.h/.cpp` | OwnedPowders、筛选/预留/释放/消费、生成与运行期清理 |
| `InsectGlaive/InsectGlaiveCombatConfig.h/.cpp` | PowderClass、Lifetime、贡献/爆炸参数及本招运动数据 |

类型合同：

- `EIGPowderState : uint8`：Available、Reserved、Consumed；无直接 `Available→Consumed` 公共蓝图入口。
- `FIGPowderReservationToken`：`FWeaponRuntimeToken RuntimeToken`、`uint32 ActivationSequenceID`、`uint64 ReservationID`；等值判断必须包含全部身份，不只比较一个递增数字。该身份 Token 供 C++ 事务使用，不直接暴露为蓝图类型；只读快照可供后续 UI/审计使用。
- Actor：`TWeakObjectPtr<URes_InsectGlaive> OwnerResource`、`TWeakObjectPtr<APlayerController> OwningPlayer`、RuntimeToken、`FGuid PowderID`、`FGuid SourceAttackInstanceID`、`FGuid SourceHitInstanceID`、`float SpawnWorldTime/Lifetime/DamageContribution`、State、ReservationToken。Actor.Owner设置为源Pawn；筛选以OwnerResource和RuntimeToken为准，OwningPlayer用于显示/审计，不用GetPlayerController(0)。
- Resource：`TArray<TWeakObjectPtr<AIGPowderActor>> OwnedPowders`，按 ReservationID 保存成员快照、中心、创建时刻；下一 ID 单调递增。
- `SourceAttackInstanceID` 对应觉虫击源攻击；每次贯通伤害另外有唯一 `SourceHitInstanceID`，用于“伤害→点灯→粉尘”核账，不用 FlightID 代替每跳伤害 ID。

旧版已存在的配置及单位（当前源码基底，不再作为最终接线表；新值/迁移见§6.7）：

| 字段 | C++ 类型 | 单位/约束 |
|---|---|---|
| `PowderActorClass` | `TSubclassOf<AIGPowderActor>` | 指向最终 `BP_IG_PowderCloud` |
| `PowderLifetime` | float | 世界游戏秒，>0；暂停/slomo 随游戏时间，不用墙钟 |
| `PowderDamageContribution` | float | 每团直接伤害贡献，≥0，生成时快照 |
| `PowderVortexBaseDamage` | float | 基础直接伤害，≥0 |
| `PowderVortexExplosionRadius` | float | cm，>0；**不同于**收集半径 |
| `PowderVortexBaseStagger` | float | 基础破坏值，≥0 |
| `PowderVortexHitCueTag` | FGameplayTag | 目标 `GameplayCue.Hit.IG.Powder` |
| `PowderVortexExplosionCueTag` | FGameplayTag | 聚拢中心只播放一次的 `GameplayCue.IG.PowderVortex` |
| 现有 `PowderGatherRadius/Duration/MaxCount` | float/float/int32 | 当前默认 600cm/0.5s/5，不冒充原作真值 |

数据校验分层：未把特殊 GA 接入 Combo 前，新增功能的空引用不得使已签收 M5 全部失效；该功能启用/GA 被接入时，校验其必需 Class、Montage、Curve、Notify、参数。不能用硬编码默认类掩盖遗漏，也不能删除现有四个必填 Sound 的校验来绕过本轮。

### 4.4 Resource API（M6.1 已实现）

```cpp
// M6.1 已实现的基础接口；以头文件中的实际声明为准。
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

此外已实现 `CanGatherPowders` 与 `GetPowderSnapshots`。旧Commit要求Action仍登记，旧自然到期只销毁、可能从固定预留集合消失；这些是现状，不是新版目标。按§6扩展：成功命中产粉策略、ExpireAt/Revision、到期自然爆炸、可跨猎人Montage正常完成的Resource事务身份、两段独立伤害和零团无伤害；Runtime teardown仍清理粉尘。

旧`FIGPowderExplosionSnapshot`只有聚合伤害。新版自然爆炸与聚爆消费仍产出不可变快照，但必须区分爆炸种类、StageIndex/StageAttackID和两段伤害值，不继续以一个`TotalDamage`冒充两次伤害。

### 4.5 状态与事务规则（新合同的迁移要求）

```text
虫印局部有效猎虫伤害且10s冷却通过 / 三灯觉虫击有效贯通伤害 → Available
Available → Reserved(精确 ReservationToken)
Reserved → Available                    // 操虫/收虫取消引爆，保留已发生的聚拢和寿命刷新
Reserved → Consumed → Destroy           // 聚爆提交一次，产生两个阶段快照
Available → Consumed → Destroy          // 180s到期，产生一次自然爆炸快照
任意存活状态 → DistanceCulled → Destroy // 离玩家过远，仅消失、不伤害
任意存活状态 → RuntimeShutdown → Destroy
```

状态枚举的最终命名在实施时迁移；终止原因必须可区分自然爆炸、聚爆消费、远距清理和运行期关闭。规则如下：

1. 粉尘 Collision Disabled，无物理模拟，不阻挡胶囊/武器/猎虫，不影响 Visibility 准心。
2. 用真实 Hit 的接触点生成，适当小法线偏移只用于表现；不重新 Trace 寻找另一个部位。记录原始 Hit 与表现位置。
3. 筛选仅当前 Resource/Runtime 所有、Available、未到期、在范围内的 Actor；不使用每帧 `GetAllActorsOfClass`。
4. 聚爆选择目标中心球内全部本玩家Available虫粉，总存量上限100；不再截取最近5团。一次预留固定成员，期间新虫粉不自动混入。
5. 预留完整成功才返回 Token。失败回滚本次所有状态，不留“半份 Reserved”。
6. 猎虫到达聚爆目标并开始聚拢时，将成员ExpireAt原子刷新为当前世界游戏时间+180s；1s引爆时按实际仍存活成员数量和各团伤害快照结算。零团没有两段伤害。
7. 取消释放仍存活成员为Available，保留已聚拢到的位置及刷新的寿命；不强制瞬移回原位置。已经爆炸/销毁的成员不复活。卸装/关闭运行期作技术清理，不转交新武器。
8. Commit 必须先将事务标为已提交、移除预留表并将成员变为 Consumed，产出快照，然后才发送事件/Destroy/执行伤害，防止重入重复爆炸。
9. 粉尘事务与`WeaponResourceCosts`三灯事务分别维护：三灯在觉虫击入招Commit消费；虫粉在聚爆1s截止成功提交或自然到期时消费。
10. Shutdown 顺序：拒绝新请求 → 取消本运行期动作/聚拢回调 → 清事务和粉尘 → 猎虫/GE 等现有清理。迟到到期/取消回调按旧身份退出。

## 5. M6.2：觉虫击

### 5.0 当前执行状态

**M6.2/E6.1的觉虫击动作、三灯成本、飞行/召回和命中舞踏签收保留。** 旧M6.3专项10/10、完整M6 27/27、621资产校验只覆盖旧粉尘合同，不覆盖新的虫粉产出/自然爆炸或新版两段聚爆。当前Demo已实现虫印建立、离手自动寻印攻击、统一产粉/自然爆炸、定点聚爆和Combat事件遥测。M6.3.5资产AuditOnly、M6.3.6表现资产/DA接线、冷编译、M6/M5回归及DataValidation均已通过，**当前唯一入口为E6.2 PIE**。动画源`AS_Unsh_CaoChong`已核实，既有专用Montage/GA/Combo资产已符合新合同，不需重录动作源。

- `FKinsectFlightRequest` 的逐次成功命中取精华/产粉尘授权已作为旧基底实现；每次有效命中用同一HitID核账，失败零副作用。新版普通放虫及招式猎虫判定命中局部虫印时也受10s冷却产粉，不能继续用旧“普通请求一律不产粉”遮断公共入口。
- `UMHGZAwakenedKinsectAttackAbility`：无三灯时生成普通单击请求；有三灯时在 GA 激活事务中原子消费三灯，发生在 Montage/丢虫通知前，即按下并成功发动招式时扣灯；之后不重复扣灯。激活时按Action Aim相机方向立即修正角色Yaw，GA `MaxCorrectionAngle=90°`、Combo边 `-1` 继承该值；Y/B全松开时重新采样准心与当时角色Forward并锁定猎虫/猎人方向，但不立即转向；新增`ReleaseFacingCorrection`点通知，要求放在Montage第230帧，由其启动共享方向任务`0.1s EaseOut`。
- 正式 Montage 开始前，GA 通过 `KinsectSlashAttachSocket` 将真实猎虫临时附着到武器棍尾；`KinsectLaunch` 通知用同一 `ActionToken` 将“棍尾附着”原子转成猎虫飞行，不先回手。无三灯的普通虫命中怪物/世界、或到达自身距离终点时只停止并悬停；由猎人199冲刺的结束回调（正常完成、途中落地、动作/冲刺中断）统一召回，且只在 FlightInstanceID 仍属于本招时执行。三灯贯通分支维持请求自身的 `ReturnToOwner` 终点策略。
- `UAnimNotify_IG_AwakenedKinsectEvent`：精确 Montage 实例解析入口已添加；`ReleaseFacingCorrection`只启动已冻结释放准心对应的平滑Yaw修正；`KinsectLaunch`提交释放帧方向与固定速度/时长请求，飞行上限由固定速度乘`KinsectLaunch → HunterFlightStart`的实际通知间隔得出；`HunterFlightStart`启动199 CMC位移。三种事件通知均已放置并由只读回读确认唯一、顺序正确。飞行结束后由Host用独立206下坠Montage/Tag接管，CMC `Landed` 回调清理。
- 193 段的 C++ 路由：Startup→HoldLoop；HoldLoop最多播放3遍，第三遍结束后自动进入Send195；收到 RTYB `Completed` 后下一 Tick 检查物理 Y/B 是否都松开。若仍在Startup193，只把下一段改为Send195，让完整前摇播完；若已在Hold193Loop，才立即跳到Send195；Send195及之后不回跳。RT仍只作Chord修饰键。保持到三遍的自动出口、按键释放出口都受自动化覆盖；实际前摇完整度和动画接缝仍需PIE确认。

前一轮（2026-10-07）方向修正确认：激活事件使用Action Aim相机方向，GA默认上限90°；`DA_IG_Combo`对应RTYB边的覆盖值为-1，继承GA。仅修改GA角度不能改变旧代码的方向来源；此前失效根因是激活修正仍走通用左摇杆方向。当时冷编译、`MHGZ.M6.AwakenedKinsect` 13/13、项目级`MHGZ`自动化200/200及`-CorrectionOnly -AuditOnly`只读核账均通过；这些结果只覆盖方向修正等既有主体，不覆盖本轮199命中→HitDance189分支。日志：`Saved/Logs/M6AwakenedKinsectCameraAimFrame230Tests_20261007_final.log`、`Saved/Logs/M6AwakenedKinsectDirectionFullRegression_20261007.log`、`Saved/Logs/M6CorrectionOnlyAudit_20261007.log`。

随后按用户指定时刻，新增`UMHGZM6ReleaseFacingNotifyCommandlet`，在编辑器关闭后只改`AM_IG_JueChongJi`的`Action`通知轨道，添加唯一`ReleaseFacingCorrection`点通知。实际显示/触发时间均为`3.83333325s`（第230帧@60fps），位于既有`KinsectLaunch=3.95804286s`之前；既有`HunterFlightStart=4.84202242s`未变。Apply回读成功，第二次AuditOnly幂等成功；报告`Saved/M6Audit/m6_release_facing_notify.json`，日志`Saved/Logs/M6ReleaseFacingNotifyApply_20261007.log`与`Saved/Logs/M6ReleaseFacingNotifyAudit_20261007.log`。

- 2026-10-07 转向修复PIE复测：`Saved/RuntimeTelemetry/20261007-205646-BP_IG_Character_C_0-56055`，92.72秒、5,580个Spatial采样（约60.2Hz）；9次`Input.Weapon.RTYB Started`均对应`GA_IG_JueChongJi`与`AM_IG_JueChongJi`实例，9次释放均发生在Montage约1.43–2.95秒，且所有实例均继续越过第230帧位置。用户目视确认无异常，故签收“RTYB激活朝相机准心修正 + 第230帧启动释放后平滑转向”的本轮回归。数据限制：记录中无`WeaponResource.IG.Extract.White`，本次没有覆盖三灯分支；Playback也无CameraYaw字段，无法从遥测定量复核水平准心/角色偏航角关系。E6.1整招其他固定矩阵仍按下方当前门禁执行。
- 2026-10-07 三灯 PIE 记录：`Saved/RuntimeTelemetry/20261007-210436-BP_IG_Character_C_0-56055`，4,589个Spatial样本、75.79秒（约60.5Hz）。两次RTYB激活前均有`WeaponResource.IG.TripleUp`/`Combat.Branch.TripleUp`；GA分别为`GA_IG_JueChongJi_C#27`、`#45`。TripleUp在招式启动同一采样点消失，随后均进入`AM_IG_JueChongJi_Fall`和专属Aerial.Falling Tag。`Saved/Logs/MHGZ.log`确认两次`empowered=1`，猎虫固定速度`3111.9cm/s`、距离`2750.8cm`，猎人199速度`4826cm/s`、时长`0.380s`；Frame-230修正回调两次均触发，Yaw差约67.2°、0°，Actor/Mesh Scale均为1。回调日志包含锁定Aim向量和目标Yaw，两个样本的目标Yaw与Aim的水平分量吻合；未记录独立CameraYaw，也未测试±90°边界。一次空中RTYB发生于`QianChengGanTiao_Over`期间且没有新觉虫击GA，符合RequiredTags=`Grounded`，不作为故障。日志无逐次猎虫HitID/贯通伤害/ApplyExtract/粉尘事件；故仅确认三灯分支选择/消耗、发虫配置、199启动与Frame-230回调，不签收多次贯通命中副作用。本轮用户尚未报告目视结果。

验证记录：

- 2026-10-07 RTYB保持/释放PIE根因与修复：遥测 `Saved/RuntimeTelemetry/20261007-130333-BP_IG_Character_C_0-56051` 中，RTYB `Started` 位于52.600258s；原始输入显示约53.735592s时Y/B均已松开、RT仍按住，但 `ParsedInput.csv` 没有对应的RTYB `Completed`。`MontageInstances.csv` 显示 `Hold193Loop` 反复回跳，GA持续活动约10s。根因是Chord未配置 `ReleaseControlTag`/`AdditionalReleaseControlTags`；现已将Y配为主释放控制、B配为附加释放控制，RT仍为修饰键。最终隔离资产审计 `changed=0, errors=0`；Y先松/B先松自动化均通过。
- 2026-10-07 最新 PIE 遥测 `Saved/RuntimeTelemetry/20261007-131956-BP_IG_Character_C_0-56051`：RTYB `Completed` 已在按键释放后出现，故 InputProfile 修复生效；但释放至 Send195 仍被旧 `Montage_SetNextSection` 延迟到 Startup/HoldLoop 段尾（首个样本约多等1.6s）。同一动作 GA 与 Montage 确实运行，但整段角色 Z/VelocityZ 不变且一直 Grounded；代码中的 `StartHunterFlight` 又明确拒绝 `Host->IsGrounded()`，与该招从地面发动的 Combo 条件冲突。已将释放策略改为从193立即 `Montage_JumpToSection(Send195)`，移除地面起飞拒绝；并增加棍尾 ActionAttached→Flying 的同租约原子转换，以及HunterFlight结束时按FlightInstanceID召回。以上代码需冷编译/专项自动化验证后，再由 PIE 确认接缝和实际起飞。
- 2026-10-07 随后一次PIE纠正了上一版路由目标：用户观察到前摇被整个跳过，要求“只有Hold193Loop里松键才立即丢虫”。新实现区分`QueueSendAfterStartup`与`JumpToSendNow`：Startup193松键仅重设NextSection，完整播放前摇；Hold193Loop松键才JumpToSection(Send195)。录制 `Saved/RuntimeTelemetry/20261007-134541-BP_IG_Character_C_0-56051` 中，RTYB Completed在30.933748s，Montage上一样本位置0.2087s，下一样本却跳至3.2286s；该文件没有记录Section名，因此只证明发生了时间跳段，不能单凭它断言实际PIE使用了哪一版路由/Section。HunterFlight约32.571779s时已有非零XY速度，但 `ConstrainAnimRootMotionVelocity` 输出为(0,0,641.46)，因而CMC只保留垂直分量。现改为HunterFlightStart时验证Host登记的精确Montage实例与ActionToken，并只对该实例调用`PushDisableRootMotion`；姿势继续播放，CMC接管199运动。
- 2026-10-07 本轮代码验证：Development Editor冷编译成功，日志`Saved/Logs/M6AwakenedKinsectReleaseRootMotionFix_20261007.log`；`MHGZ.M6.AwakenedKinsect` **8/8通过、退出码0**，日志`Saved/Logs/M6AwakenedKinsectReleaseRootMotionFixTests_20261007.log`。自动化包含生产Montage的播放/Host实例注册、HunterFlight事件后该精确实例`IsRootMotionDisabled()`断言，以及CMC初始速度等于Aim方向×总速（包含非零水平分量）。测试验证了代码交接，不等同于实际Pie帧位移；仍待用户PIE确认前摇不跳、循环松键即时丢虫、199 XY移动和真实落地。
- 2026-10-07 针对不同瞄准俯仰角的专项修复：`FWeaponMovementRequest::HasValidAuthoredAerialPhysicsParameters` 原先要求`LaunchVelocity.Z > 0`，因此水平准心（Z=0）和下斜准心（Z<0）会被运动请求校验拒绝，199的CMC位移Task无法启动。现改为仅拒绝零向量，CMC仍在任务启动时切到`MOVE_Falling`并按当前重力处理后续轨迹；专项测试新增水平、下斜方向与水平速度保留断言。Development Editor冷编译成功；`MHGZ.M6.AwakenedKinsect` **9/9通过**，完整`MHGZ`自动化回归**196/196通过、退出码0**；日志分别为`Saved/Logs/M6AwakenedKinsectAimAndFlightAutomation_20261007_retry.log`、`Saved/Logs/FullMHGZAutomation_AwakenedKinsectMovement_20261007.log`。旧遥测`Saved/RuntimeTelemetry/20261007-142404-BP_IG_Character_C_0-55805`显示当时MovementMode仍为Walking且`RootMotionDisabled=0`，证明那次记录没有完成CMC交接；它未记录释放Aim方向，因此不能再进一步声称是哪一个具体角度导致，也不是本轮修改后的回归证据。
- 较早的2026-10-07修复版记录（`M6AwakenedKinsectFlightFix_Final_20261007.log`）属于前一版，包含“Startup/Hold都立即选择Send”的旧测试语义；其Startup断言已被本次真实PIE反馈纠正，不可当作当前路由规则。此前8/8只证明当时版本，不代替本次验证。
- 2026-10-07 Development Editor 冷编译成功；最终 M6.2 专项 `MHGZ.M6.AwakenedKinsect` **7/7 通过**，日志 `Saved/Logs/MHGZM62AwakenedKinsectFinal_20261007.log`。新增 `HunterFlightHandoffInterruptionAndLanding` 覆盖真实 GA/CMC MovementTask 的正常完成、能力取消、Host 位移租约释放及 `Landed` 清理回调；测试中落地是把 CMC 切到 Walking 后直接调用生产 `Character::Landed`，**不等于真实地面 Sweep/碰撞落地**，该部分由 E6.1 PIE 验证。
- `UMHGZM6AssetSetupCommandlet` 已生成常驻 `GA_IG_JueChongJi`、`AM_IG_JueChongJi`、`AM_IG_JueChongJi_Fall`，并接入 RTYB Chord、Combo 边及 `DA_IG_Combat.AwakenedHunterFallMontage`。首次 Apply `changed=6/errors=0`；生成后 `AuditOnly success=true changed=0 errors=0`；幂等复跑 `Apply success=true changed=0 errors=0`。日志：`Saved/Logs/MHGZM62AssetSetupApply_20261007_1130.log`、`Saved/Logs/MHGZM62AssetSetupAuditAfterApply_20261007.log`、`Saved/Logs/MHGZM62AssetSetupIdempotency_20261007.log`；机器可读报告 `Saved/M6Audit/m6_awakened_asset_setup_report.json`。
- DataValidation 冷启动验证 **619/619 有效，0错误/0警告**，日志 `Saved/Logs/MHGZM62DataValidation_20261007.log`。注意这只证明资产数据结构有效，不证明 Montage 源段剪裁、视觉接缝或 PIE 动作正确。
- 2026-10-07命名整理：通过 UE AssetTools 将攻击/下坠Montage改名为 `AM_IG_JueChongJi` 与 `AM_IG_JueChongJi_Fall`；新进程反向引用复核为 `GA_IG_JueChongJi` 与 `DA_IG_Combat`，报告 `Saved/AssetOrganization/m6-awakened-kinsect-montage-pinyin-apply.json` 的 `outcome=verified`。原 `.uasset` 备份位于 `Saved/AssetOrganization/Backups/M6AwakenedKinsectPinyin/`，SHA-256一致。更新生成器后冷编译成功；重跑 M6 `AuditOnly` 为 `success=true, changed=0, errors=0`；重命名后全量 DataValidation 619项、0错误/0警告，日志 `Saved/Logs/M6MontageRenameDataValidation.log`。
- 2026-10-07 `HunterFlightStart` 放置：MHR F9样本 `mhrise_20261006_205745_02_samples.csv` 的动作196样本时间为55.05007227s，首个199样本为55.05886133s，观测间隔0.00878906s。更新 `MHGZM6AssetSetupCommandlet` 使196段采用源时间`[0,0.01757812]`（序列RateScale=2，对应Montage 0.00878906s）；`MHGZM6RecordedHunterFlightSetupCommandlet -Apply` 将199段/Section移至该边界，并在 `AM_IG_JueChongJi` 的 `Action` 轨道放置唯一 `IG Awakened Kinsect Event`（Event=`HunterFlightStart`）。显示时间与Section入口均为Montage 4.84212255s；UE在Section边界将实际TriggerTime偏移至4.84202242s（约提前0.0001s）。原报告 `Saved/M6Audit/m6_hunter_flight_notify_placement.json` 只记录了`HunterFlightStart`，生成时早于用户后来放置`KinsectLaunch`；旧审计实现没有读取`KinsectLaunch`，因此报告不完整，而非证据显示Montage缺通知。已修正审计为统计两种事件各自的显示/触发时间、数量及间隔，并在只读审计中要求两种事件各唯一且顺序正确；重跑后才可形成当前资产审计证据。
- 2026-10-07 本轮觉虫击审计已实际重跑：`Saved/M6Audit/m6_hunter_flight_notify_placement.json` 为 `AuditOnly success=true`，读到 `KinsectLaunch` 唯一、触发时刻 `3.95804286s`；`HunterFlightStart` 唯一、触发时刻 `4.84202242s`；固定未命中时长 `0.88397956s`。审计代码现额外输出由同一生产函数计算的普通/三灯速度与终点距离；只读审计不保存 Montage。
- 2026-10-07 本轮固定速度计算：普通猎虫目标未命中距离按当前199猎人飞行候选 `4826 cm/s × 0.38s = 1833.88cm`；除以资产审计的 `0.88397956s`，得到普通固定速度 `2074.573cm/s`。三灯暂取其1.5倍 `3111.8595cm/s`；同一通知间隔下终点距离由速度派生，普通约`1833.88cm`、三灯约`2750.82cm`。这里的1833.88cm是当前候选总速乘199段时长的名义路径基准，不宣称包含不同俯仰/重力下的实际猎人世界位移；仍需PIE观察并按反馈校准代码常量。
- 2026-10-07 前一轮 M6.2 主体代码门禁：Development Editor 冷编译成功；`MHGZ.M6.AwakenedKinsect` 专项13/13通过，覆盖普通虫命中后等待猎人冲刺结束才回收、入招摇杆共享修正及持键循环最多三遍（第三遍后自动进入Send195）。完整`MHGZ`自动化197/197为该轮变更前基线；该轮尝试启动全量测试未生成有效日志，因此不记为该轮通过。该证据不覆盖本轮新增的199武器命中→HitDance189分支。最后一次只读通知审计 `success=true`，报告有两通知触发时间、0.88397956s间隔及普通/三灯最终速度/终点距离。PIE尚未在前一轮最终行为下重测。
- 2026-10-07攻击动作命名统一：通过 UE AssetTools 将仍为英文名的攻击 GA/Montage 包名改为项目中文拼音形式；GA 的原生 C++ 类名、Combo 状态/转移 ID、GameplayTag、非攻击动作和动画序列名均未改。对应关系：`GA_IG_DrawSlash`/`AM_IG_DrawSlash` → `GA_IG_BaDaoZhan`/`AM_IG_BaDaoZhan`；`GA_IG_KinsectSlash`/`AM_IG_KinsectSlash` → `GA_IG_CaoChongZhan`/`AM_IG_CaoChongZhan`（`_Dance` 后缀保留）；`GA_IG_KinsectPiercer`/`AM_IG_KinsectPiercer` → `GA_IG_QiangHuaCaoChongChuanCi`/`AM_IG_QiangHuaCaoChongChuanCi`（`_Over` 保留）；`GA_IG_DescendingThrust`/`AM_IG_DescendingThrust` → `GA_IG_JiXiTuCi`/`AM_IG_JiXiTuCi`（`_Over` 保留）；`GA_IG_AwakenedKinsectAttack`/`AM_IG_AwakenedKinsectAttack` → `GA_IG_JueChongJi`/`AM_IG_JueChongJi`（`_Fall` 保留）。其余攻击资产原本已是拼音名，不重复改名；`SendKinsect`、`RecallKinsect`、`DrawAndSendKinsect`、Dodge、Sheathe、AerialFall/Landing 不在本次攻击命名范围。迁移清单为 `Scripts/AssetOrganization/Manifests/weapon_move_asset_pinyin.json`，UE AssetTools 新进程反向引用复核报告为 `Saved/AssetOrganization/weapon-move-asset-pinyin-apply.json`（`outcome=verified`，10个包均已迁移，0缺失/冲突）；M5 KinsectSlash、KinsectPiercer、DescendingThrust 与 M6 资产只读审计均通过，开发编辑器冷编译及619项 DataValidation 通过。旧源名只保留在迁移清单的 `source` 字段作为可审计历史，不是有效资产引用。
- 新增只读 193 全身姿势循环候选审计：`MHGZM6BaselineAuditCommandlet` 已重编译并执行成功，输出 `Saved/M6Audit/m6_193_loop_candidates.csv`；编译/运行日志分别为 `Saved/M6Audit/M6_2_LoopMetricBuild.log` 和 `Saved/M6Audit/M6_2_LoopMetricAudit.log`。候选按首末身体姿势差排序；排除 382–385 这些仅 0.008–0.033s 的末端近重复帧后，第220帧是姿势接缝误差最低的有效长度候选，运行时循环长度 1.383s。其首末身体姿势差 0.158347cm RMS / 0.322714° RMS，边界逐帧变化差 0.076321cm / 0.170227°，循环段内部逐骨骼逐帧变化 RMS 0.073505cm / 0.126615°，Root 端点差 0cm、段内逐帧位移 RMS 0.082144cm。脚本现在导出接缝、时长和段内稳定性指标供复核；它不按未定义的人工阈值自动判定“通过”。第220帧可作为 `Hold193Loop` 的代码/Section 候选，真实 Montage/PIE 观感留到 E6.1 验证，不要求现在手动预览。

F9 已足以给出 199 的状态间隔、运动方向/速度候选与 206 下落物理范围；F8 是可选诊断，不是门禁。**E6.1已由用户签收完成**：193循环、196短过渡与199段配置，以及 `HunterFlightStart`/`KinsectLaunch` 通知放置和相关接线已作为该阶段交付关闭；下方源段长度/姿势映射说明留作复测参考，不是当前编辑器待办。Y/B释放映射和旧的段尾等待已修复。猎虫固定速度现按已审计通知间隔和名义距离写入代码；CombatConfig保留已配置的199运动候选。若未来改动这些内容，再按对应条目做定向回归，不重开E6.1。

**E6.1通知时序真值补充（2026-10-07）：** 点通知不是凭UE预览猜时机，而应映射MHR实录。`mhrise_20261006_205745_02_samples.csv` 中猎人动作ID 196样本约55.050072s，首个199样本约55.058861s；Montage已把这段0.008789s间隔映射到196短过渡，并在199 Section入口放置唯一 `HunterFlightStart`。该CSV没有猎虫位置/骨骼变换/离手事件字段，不能单独确定 `KinsectLaunch`；此点由用户按MHR离手真值放置。编辑器中的通知类名是 `IG Awakened Kinsect Event`，两种名称是其Event枚举值，不是独立Notify类。

### 5.1 类、授权与输入

已新增 `ActionSystem/MHGZAwakenedKinsectAttackAbility.h/.cpp`，类 `UMHGZAwakenedKinsectAttackAbility : UMHGZInsectGlaiveAbility`；常驻蓝图 `/Game/Blueprints/Ability/InsectGlaive/GA_IG_JueChongJi` 已由生成器创建并配置。精确编辑器步骤和当前未完成源段映射见[E6.1指导](../../editor/m6-setup.md#13-常驻montage核对)。

- 继承现有攻击 Montage/生命周期；觉虫击 GA 配置唯一 `AttackSegments[0]`，伤害模板取已验收操虫斩 `AttackSegments[1]`（`IG_RearTip → Root`），采用同一伤害 GE/命中部位防御/单次 ContactOnly 策略。碰撞通知 `ConfigIndex=0` 必须完全位于 `HunterFlight199`，帧数由用户已配置资产保留，代码/命令let只读审计、不重排。
- 只有该武器段成功应用伤害 GE 才能进入 Dance；Sweep接触、伤害 GE 应用失败、199外的命中均不能触发。命中回调先锁定 Hit 与精确 Montage 实例，再排至下一 Tick 切段，避免在 Sweep 调用栈中更换 Montage/MovementTask；未命中保持原206下坠流程。
- Dance 使用操虫斩常驻 `AM_IG_CaoChongZhan_Dance` 的 `HitDance189`，以及 `GA_IG_CaoChongZhan.DanceFromDashTuning`、Montage BlendIn 和 Preparation 配置；C++/资产工具不生成临时Montage、不复制出第二份189资产。命中源加入独立 `AwakenedKinsect` DanceSource，成功提交时重置空中动作预算；189蒙太奇与运动都完成后再交回常规下落，落地仍由 Host 清层。
- RuntimeHost 从 `DA_IG_Combo.Transitions.AbilityClass` 唯一收集授权；**不放进 `BP_PlayerState.CoreAbilities`**。
- 首批入口为持刀地面 `Idle` 的 `Input.Weapon.RTYB`；后续连段派生须由确认的 ComboWindow 边单独增加，不能默认任意状态可打断。
- 输入 Chord：TriggerControls 为现有物理 Y/B，HeldModifier 为 `Input.Modifier.RT`，AimSnapshotContext=Action；保留现有宽限期与更高成员数组合优先。物理 Y/B 的 Tag 从实际 Profile 映射读取，不能猜新的 Tag 名。
- 组合解析输出一次 RTYB，获胜后消费 Y/B。不能再同时派发 RTY、RTB 或 RT 松开 fallback；已在组合前合法发出的单 RT 不追溯撤销。
- Required：`Combat.State.Grounded`、`Combat.State.Unsheathed`；还须校验真实猎虫、配置和移动依赖。三灯不是 Required。RTYB 始终可发动：激活时若 Resource 中存在有效 TripleUp，则自动走强化分支；若无三灯，则普通猎虫分支。必须读取真实 Handle/事务状态，不能根据松散镜像 Tag 猜测。
- **2026-10-12现行消费规则：不论有无三灯，觉虫击成功发动都会消耗猎人已有的所有灯。** 有完整三灯仍通过`Cost.IG.TripleUp`、Amount=1原子预留/结算，决定强化分支，不二次扣Triple。无完整三灯的资源成本数组仍为空，但GAS/动作确认成功、Montage播放前通过Resource清除已有单灯；零灯也可发动。一灯/两灯消费不提供强化，携带且尚未交付的猎虫精华不属于猎人灯，不在此清空。预检/GAS Commit失败不清单灯。
- 首批Idle入口的Blocked为 `Combat.State.Attacking`、`Combat.State.Dodging`、`Combat.State.Sheathing`、`Combat.State.Hitstun`、`Combat.State.Knockdown`、`Combat.State.Dead`、`Combat.State.Aerial`，与GA激活标签保持一致。当前CantAttack是空中预算标签，不能把它杜撰成地面通用锁；不能只靠标签绕过 Resource/Token 校验。

### 5.2 方向合同

1. 激活时立即使用 `GetWeaponActivationContext().Input.Aim.Direction`（Action相机准心世界方向）进行入口Yaw修正；不读左摇杆。Combo边`MaxCorrectionAngle=-1`时继承GA的`MaxCorrectionAngle=90°`，使用通用攻击基类相同的水平投影、角度上限和90°超限容错规则。该入口修正即时完成。
2. Y/B全部松开后，冻结释放帧的Aim.TargetPoint与角色平面Forward。按猎人当前位置到该目标的方向计算Yaw/Pitch，而不是直接复用相机平行方向；不增加快照采样。
3. 水平Yaw Clamp 为±`AwakenedAimHorizontalCorrectionAngle`（默认90°），俯仰Clamp为±`AwakenedAimCorrectionAngle`（默认60°）；超出时饱和到边界，不拒绝发动。角色朝向修正和猎虫水平丢出方向都以松键帧角色Forward为基准；两轴独立限幅，不是球面圆锥。
4. `LockedHunterFlightDirection`与`LockedKinsectDirection`使用同一个冻结目标，但起点分别是猎人和棍尾，不能强制同向。猎虫离手及猎人起飞分别从实际起点重新解析到该目标的限幅方向，不读取实时相机。角色胶囊只修正Yaw；第230帧`ReleaseFacingCorrection`仍启动共享`0.1s EaseOut`任务，不在松键回调或`KinsectLaunch`瞬转，不Pitch旋转胶囊，也不写Scale。2026-10-12视差修复证据见E6指导§3.10。
5. `KinsectLaunch`与`HunterFlightStart`均为`AM_IG_JueChongJi`中的正式点通知。只读审计实际测得两者触发时间分别为`3.95804286s`、`4.84202242s`，未命中固定时长=`0.88397956s`。普通目标距离按当前199猎人飞行候选名义路径`4826 cm/s × 0.38s = 1833.88cm`，故普通分支固定C++速度=`1833.88 / 0.88397956 = 2074.573cm/s`；三灯暂取普通的1.5倍=`3111.8595cm/s`。通用`FKinsectFlightRequest.MaxDistance`只作为内部终点派生值=`固定速度 × 通知间隔`，不是CombatConfig或编辑器射程字段；不再根据剩余起飞时间动态算速度，也不追加大型怪物体长。普通分支命中、碰地/世界阻挡或到达距离端点后停止并悬停；猎人冲刺自然结束、途中落地或中断时才由GA显式召回。三灯分支按贯通/逐击点灯运行，到派生终点后召回。PIE核对速度时只调整代码常量，三灯保持1.5倍；199名义路径不代表不同俯仰/重力条件下的准确猎人世界位移。
6. HunterFlightStart Notify及`HunterFlight199` SectionChanged兜底共用幂等入口启动`RecordedLocalTrajectory + ProtectedFlight`。释放时冻结目标，起飞时由实际胶囊起点解析方向，不追踪实时准心。199时长约0.38s、总初速约4826cm/s、重力约1190cm/s²，仍可由CombatConfig调试。接触只裁当前子步位移，不立即结束199；末端实查地面，回Walking或206。`AwakenedHunterFlightMaxDistance`与`AwakenedHunterFlightStartTime`是旧字段，不得另设Timer。
7. Y/B全部松开后重新锁定释放帧准心；角色转向不在松键回调或KinsectLaunch时启动，而由`AM_IG_JueChongJi`第230帧`ReleaseFacingCorrection`点通知触发共享0.1s EaseOut方向任务。猎虫与猎人不追踪后续准心变化。

**2026-10-07 方向修正回归：** 最新遥测记录了RTYB摇杆方向，但没有相机水平Yaw字段，不能用该CSV确认准心角；代码追踪确认原实现只调用普通左摇杆入口。故问题不是仅把GA角度从30°改成90°就能修复：`MaxCorrectionAngle`只限制可应用的Yaw范围，不会改变方向来源。现改为Action Aim相机方向即时入口修正，GA上限90°；释放帧仍冻结准心/ActorForward，但把平滑角色转向延后到Montage第230帧专用`ReleaseFacingCorrection`点通知。冷编译、专项13/13和GA/Combo只读核账现已通过；PIE仍需复测激活转向和第230帧转向起点。

### 5.3 阶段与 Montage

拟新增 `EAwakenedKinsectAttackPhase : uint8`：Inactive、Startup、KinsectFlight、HunterFlight、Falling、Ended。阶段字段是执行状态，不授予新舞踏来源。

常驻 `AM_IG_JueChongJi`，至少有 `Startup193`、`Hold193Loop`、`Send195`、`PreFlight196`、`HunterFlight199`。先完整播放起手段，再进入193后半段`Hold193Loop`；该循环最多播放3遍，第三遍结束自动进入Send195，不要求按键松开才能退出。Y/B全部松开时仍可提前退出：若当前仍在`Startup193`，只把它的下一Section改为`Send195`，让前摇播完；若当前已在`Hold193Loop`，立即`Montage_JumpToSection(Send195)`；若已进入Send195或更后段则不回跳。此跳转不结束GA，也不让Combo Router再派发单独RT。RT仍只作Chord修饰键。即时跳段可能产生动画姿势接缝，须在PIE检查；若明显突变，应调整Montage中Send195的首帧/混出，不恢复秒级输入等待。

变长下落使用独立 `AM_IG_JueChongJi_Fall`（206），不能靠把它塞到固定蒙太奇末尾来决定实际触地。

已新增点通知类 `UAnimNotify_IG_AwakenedKinsectEvent`，枚举仅包含：

- `KinsectLaunch`：开始猎虫请求；代码实现、正式 Montage Notify 放置已完成。当前实现先将猎虫挂到武器棍尾Socket，再由同一动作租约原子转入飞行；仍待PIE确认实际视觉与飞行结果。
- `HunterFlightStart`：启用现有保护飞行曲线CMC任务，以锁定方向×可调总速及世界向下重力生成199解析路径；按实测时间结束后，地面清惯性回Walking，离地才重置206候选速度/重力，由Host播放独立206 Montage直到真实落地。
- `ReleaseFacingCorrection`：第230帧只启动释放准心对应的平滑角色Yaw修正；点通知已放置并审计。释放准心与角色Forward已在最终Y/B释放时锁定，通知不重新采样Aim。

通知对象只保存事件类型；运行时通过当前 Mesh + Montage 实例解析精确 ActionToken，再交给 GA。不能在 Notify 类里缓存 GA/Token，不能只用 GetCurrentActiveMontage 猜归属。

时序由审计后的最终 Montage 点通知驱动。旧 `AwakenedHunterFlightStartTime=0.4` 字段只为资产兼容保留，不调度 Timer；禁止 Notify 与 Timer 双触发。

- 193→蓄力循环→195 是由按键保持/释放决定的显式分支；不把 194 接进来。松开路由须使用本项目精确释放身份和“释放回调后下一 Tick 再查 HeldControls”的模式。现有 `UMHGZAdvancingChargeAbility` 只提供释放检查与一次性 Charge→Core 跳转，本身没有 Charge→Charge 自循环；不能不审计就直接认为它已满足本招。可扩展该通用类或在本招实现等价路由，并用输入/Section 专项测试覆盖快速松开、组合键逐个释放、GA取消和重复释放。
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

觉虫击的猎虫请求按激活时的资源分支选模式：

- **本次已消耗三灯：** `Piercing` + `CollectPerValidHit`，成功命中按真实部位收集红白橙不同颜色，同色去重，途中不授予猎人灯；真正回手后原子取出全部携带颜色，再逐色ApplyExtract。粉尘仍在每次合法成功贯通命中后产生。请求身份/方向/速度/距离/间隔和PostFlightPolicy=`ReturnToOwner`保持不变。
- **本次未消耗三灯：** 沿用普通操虫的 `SingleHit` + `FirstHitOnly` 行为，不启用逐击粉尘；命中、碰地/世界阻挡或到达距离上限后仅悬停，猎人199冲刺自然结束、落地或中断时才由GA显式召回。

若激活时查询到有效TripleUp，必须原子预留/消费后确认强化，失败拒绝整招，不降级。没有完整TripleUp仍使用普通飞行分支，但成功确认后、播放Montage前清除所有已有单灯，无额外效果；不再称为“无灯成本”分支。分支判定依Resource真值，不依赖UI或镜像Tag。实际清灯、三灯消费及Action携带精华事件进入现有Combat遥测。

在 `AKinsect::HandleHitzoneHit` 的**现有 Piercing 分支**中执行：

```text
真实 Sweep Hit + 活跃 Runtime + Hitzone有效 + 该部位已过间隔
    → 生成唯一 HitInstanceID
    → ApplyKinsectDamage(Hit, MotionValue, HitInstanceID)
      ├─ false：记录失败；不采集、不产粉尘、不写成功命中时间
      └─ true：写成功时间
             → CaptureExtractFromKinsectHit：收集不同颜色、立即发布真实携带UI
             → 若本请求开关=true：SpawnPowderForSuccessfulHit，一次
```

- 以已有 `WasSuccessfullyApplied()` 伤害提交成功为当前链路的判据，不另造更宽松的碰撞成功定义；专项测试同时验证最终 Health/HitSignal。
- Valid Hitzone 没有合法精华颜色是资产错误，预先校验；不静默假装“颜色随便一种”。GE Apply 或 Actor Spawn 异常须有结构化错误和计数，不能重发伤害来补粉尘。
- 普通/操虫斩只捕获首色；强化贯通捕获不同颜色，不缓存“最后一种”覆盖前色。召回/中断保留携带，回手清空后只交付一次；不在每次Hit直接给猎人点灯。
- 角色新三灯只能在回手交付时形成。交付时已存在三灯则按现有规则吞灯、不刷新；逐击产粉不因携带同色/角色三灯而停用。
- 同一 Hitzone 限频按世界时间，与 Tick/帧率无关；同一 Flight 的每次有效伤害 ID 必须不同，否则 IncomingHitResolver 去重会吞掉后续贯通。
- 猎虫正常飞行可独立于猎人动作尾段继续，RuntimeToken 是存续门槛；不得因为 GA 正常结束就禁掉所有晚到的合法贯通。
- 异常取消可请求该 Flight 召回；必须核对 GA 保存的 FlightInstanceID 与猎虫当前 Flight 身份，不能召回后来另一招启动的猎虫。取消后不再产粉尘；正常到达/超距/撞墙按 ReturnToOwner 收尾。

### 5.5 猎人位移与退出

必须使用**本招专用**运动参数，不能拷贝操虫斩6600/1800等速度或舞踏曲线。199由F9参数构造两端Hermite解析曲线，复用保护飞行CMC租约，不新增动态Montage或独立GA Tick物理；206仍采用原有下落物理。后续若193/195/196录制证实需要额外位移，再单独提出数据和接口改动。

| 拟新增成员 | 类型 | 合同 |
|---|---|---|
| `AwakenedHunterFlightDurationSeconds` | float | 当前候选 0.38s，来自199状态间隔 |
| `AwakenedHunterFlightSpeed` | float | 当前候选 4826cm/s；方向取冻结 Aim，数据为总速而非单轴速度 |
| `AwakenedHunterFlightGravityMagnitude` | float | 当前候选 1190cm/s²，199重力 |
| `AwakenedHunterFallHandoffVelocity` | FVector | 当前候选 `(0,0,900)` cm/s；206首行常为边界零，故可调且需PIE核验 |
| `AwakenedHunterFallGravityMagnitude` | float | 当前候选 2885cm/s²，206重力 |
| `AwakenedHunterFallMontage` | UAnimMontage* | 常驻206下坠Montage；动画仅表现，Root Motion关闭 |

本包**不把F9候选值伪装成原作精确真值**；必须保留离群段与PIE调节记录。2000/1500cm旧距离字段不再限制本199时间积分路径；若后续数据证实需要距离截断，再另行设计，不借旧字段暗中钳位。

执行规则：

1. 193/195/196 的小位移如果保留，使用本招 Startup RecordedLocalTrajectory，源数据要统一局部建基和相位时钟；不得忽略 195 已有的位移证据后声称完整复原。
2. **199 的 F9 参数是可调候选，不是已签收常量：**11 段中10段总初速拟合为4785–4901cm/s，另1段4504cm/s；冻结相机方向与位移方向10/11段相差≤14.2°，另1段27.6°。因此当前用Action Aim方向×4826cm/s中位数启动，199重力用1190cm/s²中位数候选。不得删掉离群样本，也不得把代码默认值称为最终真值。
3. **206交接语义：**12段206拟合重力约2828–2942cm/s²、拟合入口垂直速度约821–986cm/s；边界原始行常为零。实现把199水平惯性清零并用可调`AwakenedHunterFallHandoffVelocity`重置206入口速度，落地由CMC/Host真实触地处理；这不是固定时长回Idle。
4. **运动接口：**199采用统一`AbilityTask_MHGZWeaponMovement`的`RecordedLocalTrajectory + ProtectedFlight`；同Action可无缝退休曲线租约并接189准备段。原`AuthoredAerialPhysics`仍供189实际物理段使用。不新增GA Tick手动扫胶囊，不用限距平移假装运动模型。
5. HunterFlightStart 控制猎人199位移启动；猎虫启动点独立，两者可以重叠，不共用ProjectileMovement。
6. 实际胶囊Sweep/CMC判定地面与世界阻挡；落地由Host处理。墙面测试留到PIE，不能以动画到尾代替真实触地。
7. **未命中**时199到206交接保留候选的物理语义，结束GA后由Host托管下坠Tag/Montage；206蒙太奇不驱动胶囊位移。该未命中分支不新增DanceSource；199期间武器段真实伤害成功后的共享189分支见§5.6及§6.2.1。
8. 真实落地按既有规则清舞踏、恢复CMC物理、再以实际摇杆走现有Idle/Move/Stop交接；不在GA结束后额外冻结输入。

### 5.6 M6.2 出口与 E6.1 入口

M6.2代码/资产生成门禁要求：构建通过；普通/三灯分支、消费、输入修正、循环、贯通副作用、猎人飞行和生命周期测试通过；新增的199命中→HitDance189还须仅在成功GE结算后排队；常驻资产幂等；只读审计及DataValidation通过。

**2026-10-08 本轮新增命中舞踏分支门禁结果：**

- 编译：Development Editor 冷编译成功。`AnimNotify_IG_PowderGatherBegin.cpp` 调用 `UMHGZInsectGlaiveAbility::HandlePowderGatherBegin(ActionToken)` 虚函数，基类默认返回 false；M6.3 的 `UMHGZPowderVortexAbility` 已覆写并核验当前 ActionToken/ReservationToken，避免其他虫棍 GA 误启动聚拢。
- 自动化：`MHGZ.M6.AwakenedKinsect` **14/14**、全量`MHGZ` **201/201**通过，包含`DanceRequiresSuccessfulHunterFlightWindow`测试；这是代码/自动化证据，不替代真实Montage PIE。
- 资产：首轮只读审计发现GA CDO缺少`DanceMontage`、`DanceFromDashTuning`且`AttackSegments.Num`不符。使用既有生成器Apply后仅`changed=1`，只更新`GA_IG_JueChongJi`；共享189 Montage与Dash→Dance参数从已验收M5 GA读取，新增GA伤害段复制M5 `AttackSegments[1]`。没有重建Montage，也未改用户配置的AttackCollision通知时间。新进程`AuditOnly success=true, changed=0, errors=0`；幂等复跑`Apply success=true, changed=0, errors=0`。
- DataValidation：619项资产，0错误、0警告。
- 本轮日志：`Saved/Logs/MHGZM62AwakenedDanceFocusedFinal.log`、`Saved/Logs/MHGZM62AwakenedDanceFullFinal.log`、`Saved/Logs/MHGZM62AwakenedDanceAssetApply.log`、`Saved/Logs/MHGZM62AwakenedDanceAssetAuditFinal.log`、`Saved/Logs/MHGZM62AwakenedDanceAssetIdempotency.log`、`Saved/Logs/MHGZM62AwakenedDanceDataValidationFinal.log`；结构化审计报告`Saved/M6Audit/m6_awakened_asset_setup_report.json`（最终模式为AuditOnly）。

**E6.1已由用户签收完成。** 本节保留原有逐项操作/回归清单供将来改动后复测；不再要求用户现在重做PIE，也不因本轮M6.3代码工作重新打开觉虫击。M6.2命中舞踏分支、飞行/召回及落地均归入已签收阶段结果；若未来修改这些生产路径，才按原矩阵做针对性回归。F8不是门禁，F9不要求重录。

## 6. M6.3：虫印、自动攻击、虫粉与粉尘聚爆（进行中）

### 6.1 版本边界与目标流程

2026-10-08用户更新本节规则。本节覆盖旧版“猎虫当前位置聚拢、600cm/0.5s/最多5团、基础伤害加贡献只结算一次、到期直接销毁”的方案。代码和资产尚未按新合同修改；旧测试成绩只作历史证据。

```text
虫印斩 / 急袭突刺地面第一击成功伤害
  → 唯一局部虫印（90s）
猎虫已离手 + 有范围内虫印 + 无玩家指令占用
  → 飞向虫印 → 按频率对局部区域做真实攻击
所有猎虫成功伤害 → 统一产粉入口
  → 命中虫印局部区域且10s冷却通过，或三灯觉虫击贯通例外
  → 红色虫粉（180s，上限100团）
  → 自然到期：半径150cm的一段爆炸
  → 粉尘聚爆：飞向准心目标 → 吸入范围内虫粉/刷新寿命
     → 1s等待；操虫/收虫可取消 → 半径300cm的两段爆炸
```

沿用 `URes_InsectGlaive`、`AKinsect`、既有GE/真实Hit/HitFeedbackRouter和常驻Montage，不另造独立权威状态。术语“虫粉”与“粉尘”指同一种可消费Actor；“虫印”是怪物表面局部标记。玩家动作名称统一称“粉尘聚爆”；既有C++/资产名 `PowderVortex` 暂时保留。

### 6.2 虫印来源、空间范围和90秒生命周期

**本轮必做来源：**

| 来源 | 实际动作/输入 | 建立时刻 |
|---|---|---|
| 地面虫印斩 | 既有 `GA_IG_ChongYin`，持刀地面单RT短按释放 | 指定MarkSegment的第一笔成功武器伤害，透传该次真实Hit |
| 急袭突刺地面攻击第一下 | 既有 `GA_IG_JiXiTuCi` / `AM_IG_JiXiTuCi_Over`，空中单RT进入动作链 | 地面第一笔指定武器段成功伤害；不能把空中下坠段或所有四段都当来源 |
| 四连印斩第2个伤害段 | `GA_IG_SiLianYinZhan`，`AttackSegments[1]` | 用户于2026-10-13确认指GA索引1（约1.033～1.283s），不是第二个碰撞窗口；该段第一笔成功武器伤害建印 |
| 觉虫击猎人冲刺伤害 | `GA_IG_JueChongJi`，199阶段`AttackSegments[0]` | 有效猎人冲刺窗口内的成功武器伤害建印，再排队进入189；普通/三灯均适用，猎虫伤害不是该建印来源 |

四种来源均调用同一 `Resource::SetKinsectMarkFromMeleeHit`。虫印斩和四连印斩使用 `UMHGZMarkSlashAbility`，实际MarkSegmentIndex分别为0/1；急袭突刺仅在GroundOver指定索引2；觉虫击仅在有效HunterFlight199索引0成功伤害后、舞踏排队前建立。碰撞/伤害窗由原资产决定，不重放通知、不猜索引，也不把身体接触当作成功伤害。

四种近战来源每个Action最多成功建印一次；空挥、无效Hitzone、伤害GE失败不更改旧印。已有LT+RT虫印弹接口作为兼容存量保留；不把它新增为本轮必做来源，也不能以它的测试代替以上四种来源验收。新增两来源是2026-10-13定向扩展，不重开E6.2既有签收；新效果仍需针对性PIE确认。

- 每名玩家/当前虫棍Runtime至多一个虫印。新印成功建立时替换旧印，新到期时间为当前世界游戏时间+90s。
- 普通玩法中只在**怪物死亡、90s到期、新虫印成功建立**时消失。收刀、召回、操虫、出招、离开寻印范围均不清虫印、不刷新时长。
- 目标组件销毁、卸装、Pawn/Runtime关闭属于技术清理；清弱引用/粒子/Timer，不代表额外设计了玩家受击或死亡玩法。
- 权威信息至少包含MarkID/Serial、RuntimeToken、目标Actor、命中组件、可用的BoneName、命中局部点/法线、CreatedAt/ExpireAt。组件或骨骼的当前变换用于求世界位置，怪物移动时虫印跟随原命中点。
- 虫印是**局部100cm×100cm左右的斑块**，不是整个Hitzone/整只怪物带印。产粉判定先核对同一目标及表面命中，再将真实ImpactPoint变换到虫印局部表面坐标，检查约±50cm的切向覆盖和可配置法向厚度容差。不能仅凭HitzoneTag或“目标Actor有印”通过。
- 100cm为实际世界尺寸，不随放大后的木桩/怪物组件Scale变成更大的范围。局部锚点只负责跟随表面；判定用当前世界锚点及归一化表面切向基投影真实Hit差值、以cm检查，或作等价的局部尺度补偿。附着粒子同样保持约1m世界覆盖，不直接继承目标的缩放倍率；测试须覆盖放大木桩。
- 为骨骼Mesh命中保留BoneName；当前木桩Hitzone没有BoneName时用组件变换。标签 `WeaponResource.IG.Mark.Active` 只作镜像，不能替代位置/寿命信息。
- 虫印视觉：不规则雾状、淡黄色、发光，填充约1m×1m×1m的三维球状空间；Gameplay局部表面覆盖仍为约1m×1m，附着表面而非给整个部位染色。正式资产`PS_IG_KinsectMarkCloud`使用球体粒子体积，不能退回平面贴片，也不能作为可消费虫粉Actor。

### 6.3 猎虫指令仲裁与自动寻印攻击

`URes_InsectGlaive` 负责选择当前行为，`AKinsect` 负责既有移动/真实Sweep。增加显式指令来源和CommandSerial；不以“GA已结束”推断“猎虫玩家指令已完成”。

| 优先级 | 所有者/行为 | 自动寻印的处理 |
|---|---|---|
| 最高 | Runtime关闭、耐力归零强制返回、必要生命周期清理 | 立即终止自动攻击 |
| 玩家/动作 | 主动送虫/定点攻击、收虫、操虫斩棍尾附着、觉虫击、粉尘聚爆及其他明确占用猎虫的招式 | 按原指令与动作回收合同完整执行，自动寻印等待 |
| 最低 | 飞向虫印、虫印周期攻击 | 有新的有效玩家指令时立即让出 |

玩家指令之间仍按项目已有输入、ActionAttached lease和动作合法性判断；并不授权玩家在不允许的动作阶段任意打断。自动寻印的优先级不能覆盖已有玩家行为。

1. 只有猎虫**已离手**、非Returning/ActionAttached、耐力允许、没有玩家指令/动作租约、且有效虫印在 `KinsectAutoMarkSearchRadius` 内时启动自动飞行。猎虫Attached时不因有印自动离手。
2. 普通操虫先执行原AlongDirection/ToPoint、伤害、萃取和PostFlightPolicy；该**猎虫请求**真正完成后若留在场外，才重新评估虫印。不能在操虫Montage播完但虫子仍飞行时提前抢走。
3. 收虫在Returning全程及回手后拥有优先级；回手后保持Attached，自动系统不得立刻重新部署。操虫斩/觉虫击等已有强制回收结束后同理。
4. 到达虫印后以 `KinsectAutoMarkAttackInterval` 的世界游戏时间频率执行一次真实接触攻击；每次使用独立HitID/Flight或Attack身份、当前虫印目标位置和真实Hitzone Sweep。没有实际命中/成功GE时不隔空造成伤害、不产粉。
5. 怪物移动时修正自动飞行目标；虫印替换、目标死亡、过期或超出寻印范围时停止旧自动请求。范围外虫印仍保留；猎虫按原悬停/耐力规则等待，不把虫印删除。
6. 玩家有效新指令通过只读预检后，先使旧自动CommandSerial失效再部署新请求；验证失败的输入不打断正在执行的有效指令。旧Timer/Flight/Hit回调必须核对RuntimeToken+CommandSerial+MarkSerial。
7. 无输入期间“每次自动攻击都会产粉”的具体实现是**每次攻击都会进入统一产粉资格判定**；实际产粉仍受§6.4明确的10s冷却约束。攻击间隔没有给出具体数值，不能暗中把它写成10s来消除两种频率的区别。

### 6.4 统一产粉入口：局部虫印命中与觉虫击例外

所有**真实猎虫伤害**成功后进入Resource统一入口，包括普通玩家送虫、自动虫印攻击、操虫斩AttackCollision中的猎虫段、无三灯觉虫击普通猎虫段，以及未来已明确为猎虫伤害的段。武器自身伤害和粉尘爆炸不进入产粉入口。

| 产粉策略 | 条件 | 10秒冷却 |
|---|---|---|
| MarkedContact | 真实成功猎虫Hit位于该玩家有效虫印的局部覆盖内 | 每玩家虫棍Runtime共享一个产粉时钟，10s内最多1团 |
| AwakenedPiercing | 消耗三灯的觉虫击，每个通过既有RehitInterval并成功伤害的贯通Hit | 绕过虫印和10s产粉限制，每个成功Hit一团 |
| None | 无有效伤害/无局部虫印/不属于猎虫伤害 | 不生成 |

- 同一 `SourceHitInstanceID` 最多生成1团。觉虫击贯通命中虫印时使用AwakenedPiercing，不能再叠加MarkedContact生成第二团；这类生成不占用虫印产粉冷却。
- MarkedContact冷却以**成功创建虫粉**为起点：首个合格命中可立即生成，下一次须至少过去10s。新建/替换虫印、重新送虫、切换部位不重置冷却；创建失败/上限拒绝不算成功产粉。
- 通用成功命中数据至少包含RuntimeToken、CommandSerial、SourceAttackID、HitID、真实Hit、伤害来源/产粉策略、猎虫攻击力。操虫斩独立探针与AKinsect SingleHit/Piercing都提交这份数据，不能只在Piercing代码里实现。
- 每次有效贯通伤害仍按原规则点灯；产粉逻辑不修改三灯吞灯/不刷新的合同。无三灯觉虫击可走MarkedContact产粉，但不会获得贯通无冷却产粉。
- 在该次命中点生成虫粉，保存真实Hit及仅用于表现的小法线偏移。虫印跟随怪物；生成后的虫粉留在世界中，不跟随整只怪物移动。
- 粒子视觉：虫粉由橙褐色不规则烟团和稀疏金黄色发光火星组成，整体视觉包络半径75cm；烟团粒子中心分布至66cm，粒子外缘约75cm，火星同样限制在该包络内。粒子持续随机出生、消散、漂移，轮廓会变化。表现组件NoCollision、无物理；烟雾不发光，只有火星使用Additive Emissive；视觉Opacity/尺寸不作为伤害或收集判定。该外观覆盖早期“红色不发光15cm”的视觉尺寸/颜色要求，不改Gameplay数值。

### 6.5 虫粉生命周期、100团上限与自然爆炸

`AIGPowderActor` 持有身份和表现；Resource维护注册表、Timer版本、伤害快照和唯一消费事务。Actor到期只向Resource请求处理，不能在蓝图中直接ApplyDamage。

- 寿命180s世界游戏时间。保存不可变OriginSpawnTime及可刷新ExpireAt；首次为SpawnTime+180，聚爆聚拢时刷新为GatherStart+180。
- 每玩家当前Runtime最多 `PowderMaxCount` 团存活虫粉（默认100），Available和Reserved都计数。满额时拒绝新生成并记录CapRejected，不炸掉/替换旧团。此上限可在 `DA_IG_Combat` 调整。
- 虫粉距离当前所属玩家超过 `PowderDespawnDistance` 时仅消失，不造成伤害；当前默认100m（10000cm），可在DA调整。世界关闭/卸装也仅作清理。目标怪物死亡不会把世界虫粉和虫印混为一起清掉。
- 自然到期时，在虫粉当时位置，对 `PowderNaturalExplosionRadius`（默认150cm，即1.5m半径）球内怪物结算一次伤害，然后消费/销毁；无命中目标也照常销毁。对同一怪物多个Hitzone只选一笔真实Hit。
- 单团伤害按当前公式合同 `Di = KinsectAttackPowerSnapshot × PowderMotionValue/100`；猎虫攻击力由 `GetModifiedKinsectAttackPower()` 在生成时快照，`PowderMotionValue` 默认100（原始百分数）。本行取代本计划初版的倍率表达。
- `Di` 不受肉质、猎人攻击力、红灯/三灯猎人增伤、舞踏或暴击影响。使用既有GE/ExecCalc，显式关闭HitzoneDefense且设置CritOverride=0；仍保留真实怪物Hit上下文。
- 同一PowderID自然爆炸/聚爆消费/远距删除/关闭只能成功一种终止；先原子撤注册表和预留成员、记录消费原因及ExplosionID，再执行伤害/反馈/Destroy。
- 刷新寿命时取消旧Timer并递增LifetimeRevision；旧到期回调不得让刷新后的虫粉在原截止点爆炸。预留前处理已到期虫粉，不能把本已到期的团刷新复活。
- 粉尘爆炸不再产粉、不点灯、不新增舞踏，防止递归链。

### 6.6 粉尘聚爆：准心目标、聚拢刷新、1秒取消与两段伤害

#### 6.6.1 入口与中心

沿用持刀地面Idle的 `Input.Weapon.LTYB` / `GA_IG_PowderVortex` / `AM_IG_PowderVortex`，不消费三灯、不新增武器AttackSegments。InputProfile的LTYB必须在最终组合解析时捕获Kinsect AimSnapshot；旧Chord的AimSnapshotContext=None需迁移。

准心命中怪物组件/骨骼时，保存目标局部点与弱引用，飞行/聚拢跟随该点；命中场景或空气时保存AimSnapshot.TargetPoint世界坐标，空气使用既有Aim Trace距离规则。整个请求不持续重读相机。中心来自**准心目标**，不再用激活时猎虫所在地或玩家脚下。

GA通过猎虫有效性/ActionAttached/目标快照和新配置预检，提交玩家优先级的ToPoint聚爆请求。既有正常飞行移动/墙阻挡仍有效：到达不了目标时取消本次，不隔墙在目标点瞬间聚爆。允许零虫粉命令飞向目标，但N=0时无两段伤害；无猎虫/非法配置仍拒绝。

#### 6.6.2 时间和成员

本计划将“1s后引爆”约定为**猎虫抵达目标、开始吸粉的时刻+1s**，飞往目标的旅途不计入1s；这项计时解释在实施前按本节执行，若用户另行指定起点再调整。

1. 到达时冻结本次成员集合：中心球内本玩家全部Available虫粉。球**直径等于训练场可走地面的边长L，半径=L/2**；与爆炸半径独立。
2. 现有场地扩容脚本 `Scripts/AssetProbe/resize_demo_arena.py` 按10×10块、每块600cm布局，推得L=6000cm/60m，配置候选收集半径3000cm。脚本注释中的“6km”是单位描述错误，不能据此填写600000cm。E6.2须回读当前地图有效地面边界，再写最终L；不得把整个地图包中其他Actor的包围盒当场地边长。
3. “吸全部粉尘”指该球内本玩家的全部团；球直径等于场地边长并不等于球能覆盖任意中心下整个方形训练场。取消最近5团截断，最大可收集数由全局100团存量上限决定。
4. 预留集合及ExpireAt刷新在同一事务中完成。1s内逐步将全部成员表现聚向目标中心；不增加原先0.5s之后再等待1s的额外阶段。
5. 新生成虫粉不自动追加进本次预留集合。失效/远距删除成员从集合摘除；到截止点以仍存活实际成员数N和这些团的Di快照结算。
6. 虫印不属于虫粉成员、不播放被拉向中心的表现。猎虫抵达并成功开始吸粉时，若当前虫印表面点落在`PowderGatherRadius`内，则立即清除虫印；这是直接消耗，不等到1s后引爆。抵达前取消不消耗虫印，抵达后再取消也不恢复已消耗虫印。范围外虫印保持附着。
7. **成功操虫或收虫指令**在抵达前、吸粉/等待的1s内均取消聚爆，然后立即按原指令执行；自动虫印请求无权取消。取消保留已发生的粉尘聚拢位置及180s寿命刷新，将虫粉成员释放Available，不瞬移回出生点；虫印按上一条的即时消耗规则处理。
8. 处理同帧指令和引爆时，以有效玩家指令的接收/提交顺序为准；先受理的取消使旧事务Serial失效。重复取消/完成/旧Timer不再提交。
9. 猎虫目标失效、必要强制召回、Runtime关闭同样停止待引爆事务；已消费的粉尘/虫印不回滚，不需要实现Demo范围外的空中受击/死亡玩法。

#### 6.6.3 两段伤害

在1s截止时原子消费一次成员集合，建立一个ExplosionID及两个**独立StageAttackID**：

```text
N = 实际成功消费团数
D1(N) = 0          , N = 0
        DamageAt1  , 1 ≤ N < 5
        DamageAt5  , 5 ≤ N < 10
        DamageAt10 , N ≥ 10
D2 = Σ Di           // 本次所消费虫粉各自单团固定伤害之和
```

DamageAt1/5/10按已确认默认10/50/100写成显式配置阶梯表，禁止插值或写成N倍基础值。D1是数量表直接伤害，不添加猎人攻击力或猎虫倍率；D2严格复用§6.5单团Di。

- 两段在同一引爆事件中先后提交，**各自半径300cm（3m）**。N=0时D1不提交、D2=0也不提交；N>0时按配置提交两笔，不能合并成一个Spec/一个伤害数字。
- 第二段是以聚爆中心、半径300cm查询一次后提交ΣDi，不能循环调用N次半径150cm自然爆炸造成范围/跳数错误。
- 每阶段分别执行真实Hitzone Sphere Sweep，再按目标Actor去重选择最近有效Hit；共享ExplosionID作事务关联，但Resolver去重使用各自StageAttackID。一次聚爆命中多部位木桩应为两笔，不是每部位两笔。
- 两段走已有SourceType=Powder、GE/ExecCalc/HitFeedbackRouter；伤害不受肉质/暴击/舞踏/猎人Buff影响。每团已经消费后某目标/某段GE失败记录失败，不恢复虫粉或重炸。
- 空间爆炸视觉可统一播放一次；两段伤害数字/反馈按真实提交分别产生。粉尘不得递归产粉或ApplyExtract。

#### 6.6.4 Montage与事务寿命

专用Montage已使用共同序列 `AS_Unsh_CaoChong`，有效长度约0.766667s；不会因为数值改成1s就自动满足新流程。

计划把猎人动作与猎虫聚爆事务分别管理：Montage中的唯一点事件用于**提交定点猎虫命令**；猎虫到达后由Resource进入Gathering并启动1s时钟，不能在时间0的Notify直接在旧猎虫位置爆炸。Montage正常结束释放猎人动作锁，但已提交的猎虫事务继续由RuntimeToken+CommandSerial+ReservationToken管理；不得把猎人姿势拉长/冻结到旅途和1s都结束。

保留现有Notify类/资产名作为兼容入口，更新其事件含义和审计。Montage提交前中断时无请求；提交后普通动作完成不取消猎虫事务；合法新操虫/收虫立即取消。现有AbilityTask绑定GA寿命、EndAbility无条件Release、以及“Montage覆盖GatherBegin+Duration”校验需要重构，改为Resource持有的明确事务，不能仅改Duration=1就签收。常驻Montage原则继续保持。

### 6.7 新配置、数据身份与旧字段迁移

以下字段已在 `UInsectGlaiveCombatConfig` 中实现并由 `DA_IG_Combat` 使用。单位全部为cm/世界游戏秒；视觉尺寸与命中/收集半径分别存储。表中是本轮 Apply 后的默认值；PIE 调参可直接改 DA，改完不要重跑会覆盖这些字段的 `-Apply`。

| 字段/数据 | 类型 | 本次值/要求 |
|---|---|---|
| KinsectMarkDuration | float | 90s，迁移源码默认45及DA实际值 |
| Mark footprint / depth tolerance | float×2 | Gameplay 100×100cm覆盖（半宽50cm），法向容差5cm；视觉为约1m三维球状体积 |
| KinsectAutoMarkSearchRadius | float | 3000cm（训练场可走区域半边长） |
| KinsectAutoMarkAttackInterval | float | 2s；与10s产粉CD分离 |
| MarkedHitPowderCooldown | float | 默认10s，Runtime共享，可调 |
| PowderLifetime | float | 180s；当前玩法合同固定，初始与聚拢刷新均遵循180s |
| PowderMaxCount | int32 | 默认100，Available+Reserved一起计数，可调 |
| PowderDespawnDistance | float | 默认100m（10000cm），玩家远距仅消失 |
| PowderMotionValue | float | 当前字段；原始百分数，默认100，伤害为生成时猎虫攻击力×动作值/100 |
| Powder visual envelope | generated particle parameters | 整体半径75cm；烟团最大中心半径66cm，粒子外缘约75cm |
| PowderNaturalExplosionRadius | float | 默认150cm（1.5m），可调；仅伤害球半径 |
| PowderGatherRadius | float | 3000cm（ArenaPlayableSideLength/2） |
| PowderVortexDelay | float | 默认1s，到达并开始吸粉后计时，可调 |
| PowderVortexExplosionRadius | float | 默认300cm（3m），两阶段共同使用，可调 |
| PowderVortexDamageAt1/5/10 | float×3 | 默认10/50/100；数量区间1..4 / 5..9 / ≥10，可调 |
| MarkID / CommandSerial / LifetimeRevision | FGuid / uint64 / uint32 | 定位标记、指令、寿命Timer代次 |
| PowderHitOrigin / SpawnPolicy | enum | MarkedContact / AwakenedPiercing，去重同一Hit |
| ExplosionID / StageIndex / StageAttackID | FGuid / int32 / FGuid | 自然爆炸1阶段；聚爆2阶段，分别伤害去重 |

旧 `PowderDamageContribution` 从“所有团统一固定值”迁移为每团Di快照；不能同时相乘两次。旧 `PowderVortexBaseDamage` 迁移为三个阶梯配置；不再沿用单个基础值加Σ的公式。旧 `PowderGatherDuration=0.5` 和 `PowderGatherMaxCount=5` 要有明确弃用/迁移处理，资产生成器不得重新写回这些旧值。ActorClass与两种正式粒子引用分开，完善DataValidation并回读序列化DA。

本轮已填完配置默认值；E6.2是否通过仍由实际PIE判定。自动化夹具数值只用于测试，不应覆盖或取代DA默认值。

### 6.8 分步代码任务和出口

| 次序 | 工作包/写域 | 必做改动 | 可验证出口 |
|---|---|---|---|
| 1 | **M6.3.1 虫印（已完成）**；GroundAbilities、DescendingThrustAbility、Res、Config | 虫印斩/急袭突刺地面Over第一击成功伤害钩子；唯一局部印、90s、死亡/替换/过期、跟随表面 | 资产审计通过：虫印斩ConfigIndex 0；急袭Over第一地面窗ConfigIndex 2。Development Editor构建通过；`MHGZ.M6.Mark` 3/3通过；报告`Saved/M6Audit/m6_3_1_asset_audit.json` |
| 2 | **M6.3.2 指令仲裁（已完成）**；Res、Kinsect、请求类型及既有操虫入口 | CommandSource/Serial、自动寻印和周期真实攻击、玩家占用/ActionAttached/Recall优先 | Development Editor构建通过；`MHGZ.M6.Kinsect` 3/3通过，覆盖不自动离手、自动追随移动标记、真实Sweep/伤害、玩家抢占、失败预检不抢占、回手不重飞、旧Runtime/Mark身份拒绝 |
| 3 | **M6.3.3 虫粉（代码/自动化已完成）**；统一成功命中入口、KinsectSlash猎虫段、PowderActor/Types/Res/Config | 两产粉策略、10s共享CD、180s、远距删除/100团、自然爆炸和Di快照 | `MHGZ.M6` 37/37、`MHGZ.M5.KinsectSlash` 27/27通过；覆盖局部印/共享CD/贯通绕CD/HitID去重、Reserved计数、上限拒绝不消耗CD、自然爆炸单目标单次固定伤害与远距无伤移除。参数/视觉/PIE仍待E6.2 |
| 4 | **M6.3.4 聚爆代码/自动化已完成**；PowderVortexAbility/GatherTask/Res/输入快照 | LTYB Aim快照、目标ToPoint、到达后收集/刷新、1s、可取消、两阶段100cm伤害、事务脱离正常Montage完成 | 2026-10-09冷编译成功；`PowderVortex` 4/4、M6 31/31、M5 KinsectSlash 27/27通过。仍不等于资产审计或PIE签收 |
| 5 | **M6.3.5 集成与资产审计（已完成）**；Tests/M6、Telemetry、Editor commandlet | 新合同专项与M6/M5针对性回归；只读回读GA/Montage/Combo/Notify/InputAim；新增Combat事件流 | AuditOnly `changed=0, errors=0`；DataValidation 621资产0错0警；M6 32/32、M5 KinsectSlash 27/27；开放E6.2 |
| 6 | **M6.3.6 表现资产与参数接线（已完成）**；生成器/CombatConfig/Res | 生成并复用虫印与虫粉粒子及粉尘Actor；所有已定参数暴露为可配置属性，DA写入经确认的默认值 | Apply后AuditOnly成功；DataValidation 627资产0错0警；M6 32/32、Vortex 4/4、M5 KinsectSlash 27/27；仅E6.2 PIE未签收 |

一个写域一个实施者；按表先完成接口/依赖再做下一步，不并行修改共享Resource。M6.3.1～.4代码/自动化已通过；M6.3.5补齐Combat事件流、资产只读回读和门禁回归；M6.3.6完成表现资产与DA参数接线。报告确认常驻聚爆Montage、GA、Combo边、LTYB Kinsect Aim快照及DA配置匹配新合同，AuditOnly无写入错误。**M6.3代码与静态资产阶段均完成；E6.2只剩完整闭环PIE，不要再把“接粒子/填默认值”列为待办。**

M6.3.5验证证据：UE 5.6 Development Editor冷编译已通过（本次加入队列自动化后增量编译4/4成功）；`MHGZ.M6` 32/32（含`MHGZ.M6.Telemetry.CombatEventQueueIsOptInAndOrdered`）、`MHGZ.M6.PowderVortex` 4/4、`MHGZ.M5.KinsectSlash` 27/27通过。`MHGZM63PowderVortexAssetSetupCommandlet`默认AuditOnly报告`success=true, changed=0, errors=0`，回读Montage、GA、Combo边和LTYB AimSnapshotContext；全Content DataValidation 621资产0错误/0警告。Combat CSV writer及会话说明已补；UI事件仍留给M6.4。日志：`Saved/Logs/M6_3_5_M6_Regression_20261009.log`、`Saved/Logs/M6_3_5_PowderVortex_AuditOnly_20261009.log`、`Saved/Logs/M6_3_5_DataValidation_Final_20261009.log`。本阶段未运行PIE，且未自动配置粒子或填入待定最终数值。

M6.3.6 / E6.2静态接线证据（2026-10-09；当时爆炸半径默认值50/100cm已被2026-10-10调整取代）：`MHGZM636VisualAssetSetupCommandlet -Apply`仅接入`DA_IG_Combat`虫印粒子、粉尘Actor类、Cue与本节确认的参数；已有粒子外观由用户在编辑器确认。独立AuditOnly `success=true, changed=0, errors=0`，结构化回读报告`Saved/M6Audit/m6_3_6_visual_asset_setup_report.json`。当时默认值：自动寻印范围/粉尘聚拢半径3000cm，自动攻击间隔2s，标记命中产粉冷却10s，粉尘伤害倍率1，离玩家10000cm消失，寿命180s（当前设计固定），单Runtime上限100团，自然爆炸半径50cm，聚爆等待1s/伤害半径100cm，阶梯伤害10/50/100。最终Development Editor冷编译57/57动作成功；DataValidation 627资产0错误/0警告，M6 32/32、聚爆4/4、KinsectSlash 27/27通过。验证日志：`Saved/Logs/M6_3_6_VisualConfigAudit_Final_20261009.log`、`Saved/Logs/M6_3_6_DataValidation_Final_20261009.log`、`Saved/Logs/M6_3_6_M6_Automation_20261009.log`、`Saved/Logs/M6_3_6_PowderVortex_Automation_20261009.log`、`Saved/Logs/M6_3_6_KinsectSlashRegression_20261009.log`；静态接线结果见`Saved/M6Audit/m6_3_6_visual_asset_setup_report.json`。PIE尚未签收，因此E6.2仍未通过。

**2026-10-10虫印表现复验与修正（当时Gameplay斑块仍为20cm，已被后续1m合同取代）：** `20261010-004040`会话的`particles=0`采样发生在首个Cascade粒子Tick之前，不能作为未发射证据；`20261010-004603`及`20261010-011251`均在激活后记录`particles=6;active=1;visible=1;registered=1`且Template正确，说明Cascade已发射，但视觉点仍退回代理点。根因为基类表面查询把自身Actor作为`FCollisionQueryParams`忽略对象，故Trace过滤掉了目标网格；已移除自忽略，并把专用表面通道改为Overlap，避免其他怪物的可见网格作为阻挡。`AMHGZMonsterBase::ResolveHitEffectVisualSurface`继续使用真实网格查询，木桩无几何特化；当时点位、法线和20cm虫印区域以查询到的视觉表面为准，伤害的Gameplay `FHitResult`仍保持原样。猎虫后续命中也投影到同一可见网格表面后判定局部虫印范围，不要求命中必须来自建立虫印的同一个Hitzone组件。静态网格须有可查询简单/复杂碰撞，骨骼网格须有可查询Physics Asset；否则回退原命中点，需为该资源补碰撞/表面代理。最新`20261010-011251`中没有虫粉生成：猎虫命中发生在印记建立前，或印记于41.386786s替换、目标于44.848948s死亡后；印记有效期间没有`KinsectDamageSubmitted`。该旧版本将无有效印记、目标不匹配和真正在印记区域外的情况都记为`OutsideMarkPatch`，本次改为分别记录拒绝原因，避免混淆。**代码改后需重启编辑器并PIE复验；测试未通过前不能签收E6.2。**

**2026-10-10 12:12录制后的表现/自动寻印修复（当时20cm斑块；范围后来扩至100×100cm）：** 会话`20261010-121206-BP_IG_Character_C_0-56140`实际有2次`MarkCreated`，两次表现采样均为6粒子且激活/可见/注册有效，不能归为“虫印没有施加”；自动寻印指令也已接受，但多次`AutoMarkApproachEnded`为`WorldHit`，首个抵达后的接触又没有成功伤害。本次7次`PowderRejected`均为`NoActiveMark`且发生在首次建印之前，`PowderSpawn=0`，因此“没看见虫粉”首先是没有产粉，不能仅归因渲染。修复内容：虫印局部空间Mesh粒子不再继承父旋转两次；两类粒子显式初始化白色、alpha=1；自动接近点对世界静态几何做包络净空检查，近地虫印抬高安全接近点；自动飞行只转视觉Mesh、不把已贴地悬停的胶囊重新旋进地板；接触Trace径向穿过真实伤害部位代理，再将真实Hit投影回当时可见表面校验局部斑块，仍须成功伤害后才产粉。玩家指令优先级、召回留手及Runtime/Command/Mark代数校验保持原合同。

本次Development Editor冷编译通过；`MHGZM636VisualAssetSetupCommandlet -Diagnose -AllowCommandletRendering -d3d12`在临时世界中真实模拟/渲染，两类粒子均非空且可见，输出`Saved/M6Audit/PS_IG_KinsectMarkCloud_Runtime.png`和`PS_IG_PowderRedCloud_Runtime.png`。另加载真实`BP_TrainingDummy`蓝图并启动临时物理世界，对3个伤害部位、18个方向/高度表面样本验证接触投影，`contactSamples=18, contactFailures=0`；日志`Saved/Logs/M6_3_6_ParticleAndContactVerified.log`，0错0警。该诊断不保存木桩/地图、不运行Automation测试；只证明粒子渲染与静态表面接触几何，不替代飞行、产粉、召回的PIE闭环。修复资产使用`-Apply -VisualOnly`，不覆盖DA中用户调试参数。新增`AutoMarkWorldBlocked`（记录真实阻挡组件/是否初始穿透）和`AutoMarkContactMiss`，供下一次录制分辨接近失败与局部接触失败。**需重启编辑器后复测：离手→建印→停手观察至少10s→召回；建议分别打中段与近地位置。E6.2不因离线诊断通过而签收。**

**历史视觉迭代（2026-10-10；已被下方MHR截图版本取代）：** 曾将虫印和虫粉改为内部填充、持续重生的细粒子团；当时虫粉为约15cm红色球状雾团。它验证了粒子动态发射/真实渲染，但不是当前最终外观合同。

**2026-10-10 按MHR截图放大并改形虫印（早期52cm视觉快照，后续被1m版本取代）：** 用户反馈虫印有时难以看见，参考截图要求改为大范围暖黄色柔雾团。该阶段`PS_IG_KinsectMarkCloud`为半径17cm的局部圆盘内发射10–18cm柔边网格粒子；96颗初始喷发，之后64颗/秒，单颗寿命1.2s。材质使用暖黄色`(1.0,0.85,0.52)`、Opacity=`纹理Alpha×0.45`，不接强Emissive；圆盘随机分布让整体轮廓更圆润，漂移/涡旋继续保留。估算视觉直径52cm（17cm半径×2+18cm最大粒子边长）。该早期快照只有视觉放大，Gameplay半宽仍10cm；现行100cm视觉+50cm半宽合同见下方最新记录。该历史阶段`PS_IG_PowderRedCloud`仍为48颗/秒、1.2s寿命、初始36颗。其离线渲染与18个木桩表面样本只对应当时尺寸，不代表现行Gameplay边界已PIE验收。

**2026-10-10 按MHR截图重做虫粉外观，并让粒子轮廓持续变化（虫印描述为当时快照）：** `PS_IG_PowderRedCloud`现在含两层Emitter：烟雾层每秒48颗，初始36颗，单粒子寿命1.2s，粒子尺寸12–18cm，填充半径15–20cm的球状体积；材质为橙褐色`(0.85,0.30,0.08)`、Opacity 0.38、DefaultLit，不发光。火星层使用新增材质`M_IG_PowderEmber`，Linear Color采样器读取同一云雾Alpha纹理，以Additive/Unlit金黄色高亮发光；每秒18颗、初始12颗，尺寸0.8–1.8cm、寿命约0.525–0.875s，在20–42cm半径内随机散布并带轻微下坠。烟团理论最大直径约58cm，火星最远视觉直径约86cm；随机球内位置、漂移/加速度、不同寿命和连续补生使轮廓动态翻涌，不是静态贴图。该时点的虫印为约52cm暖黄雾团，后来被下方1m视觉+100×100cmGameplay范围合同取代。此阶段DA的旧20×20cm值已被后续范围调整取代。离线渲染与18个木桩表面样本不等于现行范围PIE验收；最新预览`Saved/M6Audit/PS_IG_PowderRedCloud_Runtime.png`及烟团参数仍有效，E6.2还需真实场景确认粉尘尺度、颜色、动态形变以及召回/聚爆回归。

**2026-10-10 虫印视觉与Gameplay范围同步放大（历史记录，随后增加了立体云团改造；PIE待验）：** 用户要求虫印视觉直径约1m，随后确认Gameplay覆盖范围也应匹配视觉。`PS_IG_KinsectMarkCloud`估算直径100cm；`DA_IG_Combat.KinsectMarkPatchHalfExtent=50cm`，即表面切向100×100cm方形覆盖。运行时仍以世界厘米投影判定，不继承目标组件Scale。持续时间90s、法向厚度容差5cm、表面定位方式不变。Development Editor编译通过；`m6_3_1_asset_audit.json`读回90s/50cm/5cm，`M6_3_6_MarkPatch100_FinalAudit.log`只读审计成功；专项测试`WorldSpacePatchFollowsTargetWithoutScaleInflation`通过，验证49cm内命中、50.1cm外拒绝及4倍组件Scale不扩大范围。整组`MHGZ.M6.Mark`为3/4通过，唯一失败是未修改的通用可见网格表面投影测试（`MonsterBaseProjectsProxyPointToVisibleMesh`），记录于`M6_3_6_MarkPatch100_Automation.log`，不可据整组结果称全绿。PIE目视与大/小Hitzone边界验证仍未完成，E6.2不签收。证据日志：`Saved/Logs/M6_3_6_MarkPatch100_BoundaryTest.log`、`Saved/Logs/M6_3_6_MarkPatch100_FinalAudit.log`。

**2026-10-10 虫粉/虫印最终外观和爆炸半径调整（静态验证完成，PIE待验）：** 粉尘视觉包络半径改为75cm；烟团中心分布至66cm，粒子半径补足到约75cm，火星也限制在75cm包络内。普通自然到期爆炸`PowderNaturalExplosionRadius=150cm`（1.5m），聚爆两段共用`PowderVortexExplosionRadius=300cm`（3m）。虫印改为填充的3D球状粒子团：使用球体Mesh、球形体积出生分布、X/Y/Z等比例粒子尺寸及三轴漂移，不再是平面圆盘；视觉包络仍约100cm，Gameplay方形仍100×100cm。新命令行模式`-ApplyPowderRadiiOnly`只写入两个爆炸半径，避免覆盖DA其他调参；`-Apply -VisualOnly`重建表现资产时不改DA。UE 5.6 Development Editor冷编译57动作成功；视觉资产生成后，`AuditOnly`回读`success=true`，参数分别为粉尘75cm、烟团中心66cm、普通爆炸150cm、聚爆300cm，虫印形状`filled_3d_spherical_volume`且深度100cm。UE DataValidation检查628项资产，0错误/0警告。真实渲染诊断图为`Saved/M6Audit/PS_IG_KinsectMarkCloud_Runtime.png`与`Saved/M6Audit/PS_IG_PowderRedCloud_Runtime.png`，已确认虫印不再是薄片。PIE仍需确认实际场景尺度/外观及伤害范围；在完成前E6.2仍未签收。

**2026-10-10 可见度调参（待PIE确认）：** 按用户反馈降低`L_DemoArena`的`Arena_DirectionalLight`强度8→6，保持SkyLight与曝光不变；虫印Tint从线性`(1.0,0.85,0.52)`改为更饱和的金黄`(1.0,0.68,0.28)`，Opacity 0.45→0.58，发射率64→96/s、初始粒子96→144；虫粉Tint从线性`(0.85,0.30,0.08)`改为更浓的橙红`(0.90,0.18,0.025)`，Opacity 0.38→0.58，烟团发射率48→72/s、初始粒子36→54。粒子尺寸、包络、寿命、Gameplay范围及伤害均不变。主光只作温和下调，真实可见度以PIE室外/阴影两处观察为准；若粉尘仍太暗，优先微调烟团材质和Opacity，不改伤害范围，也不把烟团改成发光材质。

### 6.9 E6.2资产与验收合同

代码出口后优先用脚本接线，目视验收见配套E6文档。最低矩阵：

- 四种近战来源在不同局部点建唯一虫印，90s/怪物死亡/替换清理；不因收刀或召回消失。
- 离手猎虫有印时自动寻印，连续真实攻击；玩家操作/招式优先，指令结束后在仍离手条件下恢复自动。
- 同部位覆盖内外分别攻击；操虫斩猎虫判定也参加；10s内不多产，替换印不绕CD；三灯觉虫击每Hit产粉。
- 橙褐动态烟团＋金黄色发光火星；表现无碰撞/物理、包络半径75cm；180s到期产生半径150cm一次伤害，远离玩家只消失；100团压力/上限及Runtime清理。
- 准心目标处吸粉，真实地图边长决定收集球直径；集合全部选入、寿命刷新、到达后1s；操虫/收虫取消无爆炸、无丢失/复活/原地瞬移。
- 0/1/4/5/9/10/100团分别核账；0团无伤害，1/5/10阈值跳变，多Hitzone怪物两阶段各一笔、D2为ΣDi。
- 不因Montage0.766667s自然结束提前取消已提交的聚爆，不留下猎人输入死区或永久Attacking锁。

### 6.10 旧版实现证据（不签收新合同）

旧版日志：`Saved/Logs/MHGZM63PowderVortexFinal_20261008.log`（10/10）、`MHGZM63M6Final_20261008.log`（27/27）、`MHGZM63DataValidation_20261008.log`（621资产0错0警）。独立 `MHGZM63PowderVortexAssetSetup` 曾生成Montage、GA及 `Idle_LTYB_PowderVortex`，AuditOnly为changed=0/errors=0；报告 `Saved/M6Audit/m6_powder_vortex_asset_setup_report.json`。

三份送虫/收虫/拔刀放虫Montage和专用聚爆Montage共同引用 `AS_Unsh_CaoChong`，Skeleton=SK_Demo_Body、RateScale=2、有效长度约0.766667s；专用Montage使用DefaultSlot、Section=PowderVortex、Action轨道唯一GatherBegin在0.0s。旧LTYB Chord没有Aim快照、旧GA取猎虫当前中心，均须按本节迁移。

来源 `AM_IG_DrawAndSendKinsect` 的DrawCommit和KinsectSendCommit都在0.0s，与设计中“至少提前一帧”的要求不同；保持为单独回归记录，不混入虫印/聚爆的新验收成绩。

## 7. M6.4：HUD、资源显示、准心

### 7.1 目标类与创建树

```text
Local PlayerController
└─ AMHGZHUD / /Game/Blueprints/HUD/BP_MHGZHUD  // 复用现有HUD Actor；唯一生命周期所有者
   └─ WBP_HUD : UMHGZMainHUDWidget       // 唯一 AddToViewport
      ├─ HealthBarSlot → WBP_HealthBar
      ├─ StaminaBarSlot → WBP_StaminaBar
      ├─ CrosshairSlot → WBP_Crosshair
      └─ WeaponResourceSlot → WBP_IG_ResourcePanel
```

M6.4代码实现清单：

| 文件/类型 | 目标职责 |
|---|---|
| `UI/MHGZHUD.h/.cpp` | MainHUDClass/HealthBarClass/StaminaBarClass/CrosshairClass；创建/插槽/绑定/销毁 |
| `UI/MHGZMainHUDWidget.h` | `UMHGZMainHUDWidget`；四个 `UOverlay*` BindWidget 插槽，命名同上 |
| `UI/MHGZAttributeBarWidget.h/.cpp` | `UMHGZAttributeBarWidget : UMHGZUserWidget`；供血/耐力两个数据型蓝图共用 |
| `UI/MHGZWeaponResourceWidget.h/.cpp` | 虚拟 Bind/Unbind、NativeDestruct 兜底；保留已有公开 Bind 名 |
| `UI/MHGZInsectGlaiveResourceWidget.h/.cpp` | `UMHGZInsectGlaiveResourceWidget : UMHGZWeaponResourceWidget`；虫棍快照委托订阅与显示事件 |
| `UI/MHGZCrosshairWidget.h/.cpp` | 目标变化、显隐及 `None/Kinsect/Action` 三种显示模式接口；LT/RT四个目标状态由Widget表现，不自己 Trace |
| `InsectGlaive/IGResourceUITypes.h` | `FIGResourceUISnapshot`，纯读模型，不是第二份战斗状态；同时包含猎虫携带、尚未交付的精华颜色 |
| `AttributeSystem/Res_InsectGlaive.h/.cpp` | GetUIState 与状态/时间更新广播 |
| RuntimeHost/PlayerController | Ready、Possess/UnPossess 通知及其幂等生命周期 |

以上原生类型均已实现并通过Development Editor冷编译；资源快照/RuntimeReady与UI事件队列专项自动化2/2通过。Widget蓝图外观、Slot绑定和PIE属于E6.3，不在M6.4代码出口内。

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
- 当前`UMHGZAttributeBarWidget`原生实现缓存显示比例并绘制条形，WBP不用覆盖OnValueUpdated；未来覆盖须Call Parent保留显示缓存。禁止 UMG 属性 Binding 每帧 GetActor/ASC，禁止 EventTick 轮询。

### 7.4 虫棍读模型

`FIGResourceUISnapshot` 字段及类型：

| 字段 | 类型 | 来源 |
|---|---|---|
| `RuntimeToken` | FWeaponRuntimeToken | 当前 Resource 运行期 |
| `KinsectStamina / MaxKinsectStamina` | float / float | Resource 真值 |
| `KinsectState` | EKinsectState | 当前猎虫；不存在时另用 bHasKinsect=false |
| `KinsectPendingExtractColors` | FGameplayTagContainer | 只读显示适配；当前Gameplay最多填一种，未扩展多精华携带 |
| `KinsectPendingExtractColor` | FGameplayTag | 猎虫当前携带但尚未交付的精华颜色；无效表示未携带。与猎人已获得的三个灯独立 |
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
- 每个角色灯显示`单灯有效 || bTripleUp`：Triple替换单灯GE后仍三色高亮，不伪造GE。携带精华为独立竖立椭圆，读取真实`KinsectPendingExtractColors`：1色全填、2色二分、3色三分。普通/操虫斩单色，强化觉虫击多色；未来双色猎虫仅预留显示能力，不在本轮扩普通容量。回手交付后椭圆清空；舞踏只显示真值层数/来源，落地清零。

原生资源面板提供 `BlueprintNativeEvent void OnResourceUIUpdated(const FIGResourceUISnapshot& State)`；收到汇总委托或Bind初始快照后调用，原生实现缓存快照并绘制猎虫/三灯图标，当前WBP不必覆盖。未来覆盖须Call Parent；Unbind清来源并向显示发送空快照，BP不用自己订阅Resource。`bRed/bWhite/bOrange/bTripleUp` 是猎人已获得的灯，`KinsectPendingExtractColors` 是猎虫尚未交付的精华；单数 `KinsectPendingExtractColor` 只兼容首色查询，不能用于多色绘制。准心提供 `OnAimVisibilityUpdated(bool bVisible)` 和 `OnAimDisplayStateUpdated(Mode, Target, HitzoneTag, ExtractColor)`，区分LT/Kinsect、RT/Action和隐藏状态。

### 7.5 准心

以[E6指导§3.0永久保存的用户《世界》原图](../../editor/m6-setup.md#30-目标视觉真相源用户提供的怪物猎人世界五张参考图)为视觉真相源，做四种输入/命中组合；不直接把整张截图当UI贴图：

| 状态 | 显示 |
|---|---|
| 无任何相关瞄准上下文 | 隐藏准心 |
| `Combat.State.Aiming.Action`，无有效Hitzone | 图四：浅灰白圆环、中心点及四个小三角标记；不据此点灯 |
| `Combat.State.Aiming.Action`，有有效Hitzone | 图五：橙色圆环、中心点及四个三角标记；不是菱形，不据此点灯或预览精华 |
| `Combat.State.Aiming.Kinsect`，无有效Hitzone | 图一：浅灰白小型断开菱形/斜向标记，无金色锁定 |
| `Combat.State.Aiming.Kinsect`，有有效Hitzone | 图二：金黄色细菱形、中心点及四向尖形标记；精华色用于独立资源图标，不替代该金色 |
| 两者都有 | Kinsect模式优先；由`OnAimDisplayStateUpdated`发出唯一当前显示模式 |
| Slinger 收刀上下文 | 本轮不新增投射器玩法/颜色语义 |

Widget视觉按原图实现细长绿/金属性条与紧凑猎虫资源区；三灯为横排1/2/3红/白/橙菱形，另设猎虫携带精华图标，只有 `KinsectPendingExtractColor` 有效时显示，交付后隐藏，不以调试文字/三根进度条代替图标。HUD订阅ASC瞄准子Tag和AimComponent目标变化；Bind时主动读完整当前目标/颜色，关闭瞄准立即清旧颜色。不要用CaptureAimLockSample作为UI Tick数据源。

屏幕中心锚点/Alignment=(0.5,0.5)，不通过Actor世界位置决定准心。**此UI工作只读现有相机视点；相机位置、SpringArm、PlayerCameraManager、镜头管理/限制、动作AimSnapshot和输入时机全部保持不变。**

### 7.6 删除UISubsystem空壳

已完成审计：源码/Config/脚本无调用点；只读`MHGZM64UISubsystemAuditCommandlet`查询AssetRegistry中的`/Script/MHGZ.MHGZUISubsystem`类对象和静态`Get`函数引用均为0。随后删除旧空壳`.h/.cpp`，冷编译成功。保留只读审计commandlet作为回查工具；HUD不使用GameInstanceSubsystem工厂。

## 8. M6.4视觉反馈代码 / E6.4伤害数字

**2026-10-13范围裁定：** 当前只做伤害数字Widget、Actor、唯一数字Cue及PIE。下方其他视觉Cue方案保留为延期项，不要求本轮制作，也不作为本Demo门禁；音效继续延期。E6.3与普通猎虫回手闭环已由用户确认完成。

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

M6.4已新增 `UI/MHGZDamageNumberActor.h/.cpp`（`AMHGZDamageNumberActor`）、`UI/MHGZDamageNumberWidget.h` 与 `GameplayCues/MHGZDamageNumberCue.h/.cpp`（`UMHGZDamageNumberCue : UGameplayCueNotify_Static`）；本节剩余工作是E6.4创建/接线蓝图视觉资产并PIE确认。

- `BP_DamageNumberActor` 继承 `AMHGZDamageNumberActor`，Screen空间无碰撞，接线WBP。当前总寿命1s，前0.75s原地不透明，末0.25s上飘60cm/s并渐隐；`DisplayLifetime`/`FadeOutDuration`/`RiseSpeed`在Class Defaults可调，不执行DamageGE。
- `WBP_DamageNumber : UMHGZDamageNumberWidget` 有绑定TextBlock `DamageText`；`SetDamageDisplay`已改为BlueprintNativeEvent，原生格式化已结算伤害整数及暴击配色/字号，无需BP图接线，未来覆盖需保留显示合同。
- Cue使用Params.Location和RawMagnitude。当前Context不存最终暴击布尔值，因此仅在伤害数字这次调用里，由Router设 `NormalizedMagnitude = Result.bCritical ? 1.f : 0.f`；数字Cue按该值读取已结算暴击，不根据玩家CriticalRate二次抽签、不改变其他Cue的参数语义。在本地表现世界Spawn一次数字Actor。
- Actor 显示时间到自动回收/销毁，WidgetComponent随 Actor 生命周期结束；不由 HUD/GA/木桩 AddToViewport 文本。
- 第一版先用有界 Lifetime 的 Spawn/Destroy，不强制实现对象池；M7 压力测试证明需要时再做池。不能在这里加第三个长期 UI 工厂。
- 单次真实伤害只出一个数字，0伤害不显示；不因多个命中Cue重复生成。Router已有`Result.ActualDamage > 0`门槛，Cue再防御有限正值；不改HitSignal/其他Cue。两段聚爆保持独立数字并小幅错位，避免同点覆盖。

### 8.3 资产与技术边界

- `/Game/GameplayCues` 已在 DefaultGame.ini 的 GameplayCueNotifyPaths 中；把本轮 Cue 放在那里并回读注册 Tag，不改目录发现规则。
- 临时视觉足够：粉尘 BP 可用无碰撞球/半透明材质，命中与爆炸可用简单粒子/材质表现。既有 `UParticleSystem` 路径可复用接口，但不能把虫印粒子误当成权威 PowderActor。
- 当前 Build.cs 没有 Niagara 模块。若最终选择 C++ `UNiagaraComponent/UNiagaraSystem` 字段，必须在该工作包显式添加 Niagara 依赖并构建；不写指导让用户填写一个尚未声明的属性。
- 音效暂不制作、不赋新引用；已有 CombatConfig Sound 不清空。Cue 的声音槽为空是本轮可接受状态。

## 9. 自动化资产与检查工具

`UMHGZM6AssetSetupCommandlet`（`-run=MHGZM6AssetSetup`）已实现并在本机运行通过。默认是 `AuditOnly`；显式 `-Apply` 才写资产。当前已执行生成和幂等验收；见§5.0中的日志与报告。

粉尘聚爆使用单独的`UMHGZM63PowderVortexAssetSetupCommandlet`（`-run=MHGZM63PowderVortexAssetSetup`），避免旧M6.2生成器接触新资产。常规`-Apply`创建聚爆Montage、GA及Combo边；需要单独修正聚爆Montage槽时，使用`-SetSlotOnly -Apply`，它只把`AM_IG_PowderVortex`唯一承载动画段的Slot track从旧`DefaultSlot`改为`UpperBody_IGAction`，并先核验送虫、收虫、拔刀送虫来源Montage的有效槽与Skeleton一致；空的默认轨道、动画段、Section、通知和其他资产保持不动。更新后的只读审计应确认有效动画轨道槽名为`UpperBody_IGAction`。

工具合同：

1. 默认只读 `-AuditOnly`；显式 `-Apply` 才写。本工具只写本次 M6.2 的两份Montage、觉虫击GA及 `DA_IG_InputProfile`、`DA_IG_Combo`、`DA_IG_Combat` 明确字段/新增边；禁止批量修旧 PSS/PSD/动画。
2. 更新前审计父类、Skeleton/Section/Root Motion/Notify/引用；不覆盖同名但不属本轮的资产，冲突立即报告。
3. 写入后重载并审计；第二次 Apply 无 diff。当前报告包含生成模式、资产路径、193循环候选、修改清单与错误；字段映射由只读审计实际校验。精确Notify帧属于E6.1，当前生成器不替用户判断帧位。
4. 编辑器已有未保存资产时停止外部写入，先保存/关闭或采用编辑器内接口；不能用 commandlet 覆盖内存中未保存的 ABP/DA。
5. Python 不能读取 Notifies/AnimNotifyTracks 的 protected 反射字段；不能把Python零发现当作无通知。当前M6.2生成器检查/创建语义轨道，但不审计E6.1最终Notify实例；最终事件数量、Section与时间须在E6.1核验，若需要机器回读则扩展C++只读审计。
6. 能脚本完成的 CreateAsset、字段写入、常驻 Montage、Chord/Combo、轨道命名和数据校验优先自动化；用户负责姿势、时机和 UI 布局目視确认。

M6目标资产清单（觉虫击GA/两份Montage属于已签收M6.2；聚爆GA/Montage已按新合同审计；虫印/红虫粉表现与Actor也已生成并接入DA）。本节列出资产作为回查清单；不要重复生成或重跑Apply。E6.2已签收；后续是E6.3 HUD/准心Widget、E6.4视觉Cue/伤害数字资产接线，以及M6总验收：

- `/Game/Blueprints/Ability/InsectGlaive/GA_IG_JueChongJi`、`GA_IG_PowderVortex`。
- `/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_JueChongJi`、`AM_IG_JueChongJi_Fall`、`AM_IG_PowderVortex`。
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

### 10.2 Combat与UI事件流

沿用现有记录启停/会话目录。M6.3.5已新增Combat事件队列及`Combat/Events.csv`；每条事件在发生点提交，不依赖60Hz快照抽样。当前覆盖虫印建立/清除、猎虫命令接受/抢占/自动优先级延迟/飞行结束、虫粉产出/拒绝/自然爆炸/远距移除、聚爆预留/寿命刷新/到达/取消/提交/消费及分段伤害。队列仅在`mhgz.Telemetry.Enable`开启时接收事件，并有界保留；AnimInstance按事件Serial读取，另写入独立CSV。测试验证关闭时不增长、开启时有序读取且不重复。

M6.4新增同会话目录下的 `UI/Events.csv`。本地 PlayerController 持有有界队列；只在`mhgz.Telemetry.Enable`开启时接收，AnimInstance按Serial取走并立即flush。没有把业务逻辑绑定到Telemetry或AnimInstance。队列容量4096，缺口写为`TelemetryEventsDropped`；单个AnimInstance捕获期间按递增Serial避免重复写入。

| 文件 | 记录 |
|---|---|
| `Combat/Events.csv`（已实现） | TripleCostReserved/Consumed/Rejected/Released；KinsectDamageSubmitted/Rejected；ExtractAttempt（`ResourceTag`记录成本/精华Tag，`Reason`区分Granted、IgnoredDuringTripleUp、拒绝原因）；MarkCreated/Cleared/TargetDeadOrInvalid；CommandAccepted/Preempted/AutoDeferred/CommandCompleted/AutoMarkApproachEnded/VortexFlightEnded；PowderSpawn/Rejected/NaturalDetonate/DistanceCulled/Reserved/LifetimeRefreshed/Released/Consumed；VortexArrived/Cancelled/Committed；StageDamageSubmitted；队列溢出时TelemetryEventsDropped。 |
| `UI/Events.csv`（M6.4已接线，需专项自动化/PIE验证） | HUDCreate/Destroy、PawnBind/Unbind、RuntimeReady/Invalidated、ResourcePanelCreate/Remove、StaleEventRejected、DamageNumberSpawn/Expire；含SessionID、WorldTimeSeconds、Frame、EventSerial、RuntimeGeneration、Reason、SubjectClass、Pawn、DroppedCount |

每条战斗事件至少有SessionID、WorldTimeSeconds、Frame、单调EventSerial、RuntimeGeneration及可用的ActivationSequenceID；依事件带MarkID/CommandSerial/FlightID/SourceAttackID/HitID/PowderID/ReservationID/ExplosionID/StageIndex/StageAttackID、TargetActor/Hitzone/ImpactPoint、产粉策略/ResourceTag/拒绝原因、ExpireAt/LifetimeRevision、成员数/伤害。空字段表示该类事件不适用，不伪造关联。

Combat事件在发生点进入RuntimeHost队列，停止时由现有捕获器flush；不能按60Hz抽样丢掉同帧多次贯通命中。捕获关闭时不积累事件，启用时队列有界并记录丢弃缺口。会话README说明Combat/Events.csv字段与启停方式。

不得把 GA/UI 业务绑定到 AnimInstance 的存在上：记录器可以收集事件，没开 Telemetry 或没有 AnimBP 时，粉尘与 HUD 仍正常运行。

### 10.3 必须可自动核账

```text
一次有三灯的成功觉虫击激活 → 旧Triple Consume成功次数 = 1
一次无三灯的成功觉虫击激活 → Triple reservation/consume次数 = 0，普通猎虫分支可运行
每个强化觉虫击的成功 Piercing HitID → ExtractAttempt = 1，PowderSpawn = 1
失败HitID → ExtractAttempt = 0，PowderSpawn = 0
同HitID → 两种产粉策略合计Spawn ≤ 1；上限/缺类等失败必须单独报告
MarkedContact成功Spawn时间差 ≥ 10s；AwakenedPiercing不推进此CD
Mark有效 → 唯一局部印；收虫/收刀不清印；Attached不因有印自动离手
每个PowderID → 最多一次Consume；不能同时属于两个Reservation
自然ExplosionID + TargetActor → 成功伤害提交 ≤ 1，半径150cm
聚爆ExplosionID + StageIndex + TargetActor → 成功伤害提交 ≤ 1，半径300cm
聚爆N > 0 → 两个StageAttackID；第一段按1/5/10档，第二段ΣDi；N=0两段均无
VortexArrived → LifetimeRefresh → 1s后一次Commit；操虫/收虫取消则不Commit
取消Reservation → 无聚爆；存活成员恢复Available，保留当前聚拢位置/新ExpireAt
同粉尘旧LifetimeRevision到期事件 → 不销毁/引爆已刷新的粉尘
每个LocalPC → HUD根实例 = 1；ResourceSlot子实例 ≤ 1
旧Generation UI事件 → 不改新Panel/新Pawn绑定
```

三灯形成后 ExtractAttempt 可以是“吞灯成功”，不要求每次都产生新 GE。Spawn失败/GE异常必须单列，不能让计数报告把错误当作满足一一对应。

## 11. 自动化测试、构建和人工验收

### 11.1 测试归档

新增测试放`Source/MHGZ/ActionSystem/Tests/M6/`；按Resources、MarkAndCommands、Powder、AwakenedKinsect、PowderVortex、UI、Feedback组织，不塞回M1。目录为目标分类；已有回归保留并按职责归档。

| 测试组（目标命名） | 最低覆盖 |
|---|---|
| `MHGZ.M6.Resources` | 三灯消费结果/过期/重复/争抢；Runtime身份、shutdown |
| `MHGZ.M6.Mark` / `MHGZ.M6.AwakenedKinsect` / `MHGZ.M6.MarkAndCommands` | 四种近战建印来源/指定段、90s和局部范围、替换/死亡；自动不离手、真实周期攻击、玩家优先、请求完成恢复、旧回调拒绝 |
| `MHGZ.M6.Powder` | 全部猎虫路径、区域内外、10sCD/贯通例外/同Hit去重、100上限、180s自然爆炸半径150cm、仅猎虫攻击力伤害快照、远距消失/寿命Revision |
| `MHGZ.M6.AwakenedKinsect` | 前置失败零副作用、冻结Aim/Clamp、逐击正确点灯/粉尘、间隔、正常/异常Flight、位移交接 |
| `MHGZ.M6.PowderVortex` | 准心目标/到达、全选球内己方虫粉、刷新180s、范围内虫印抵达即消耗且不回滚、1s与取消、Montage结束继续事务、重入；两个StageAttackID/每段按Actor去重、1/5/10阶梯/ΣDi、零团无伤害 |
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
| 无三灯RTYB；无猎虫/缺必需配置RTYB | 前者仍走普通单击；后者拒绝，无消费/半个动作，后续可操作 |
| 三灯 RTYB 空放 | 旧三灯只消费一次；猎虫和猎人按各自请求运行，最终回地面/空中正确，不残留位移者 |
| 贯穿三色木桩部位 | 每成功Hit一次点灯/粉尘；可重新形成三灯；后续吞灯不刷新 |
| 连续命中同部位 | 满足间隔才再次伤害，成功ID不同，不丢首击、不按帧率改变频率 |
| 觉虫击中断/卸装/运行期结束 | 本Action移动/回调清理；已付成本不返还；旧请求不污染新Runtime |
| 普通武器/放虫接触粉尘 | 不引爆、不消费、不新增精华/伤害 |
| 四种近战建印来源/局部范围/自动追印 | 90s唯一局部印；虫印斩0、急袭地面2、四连1、觉虫冲刺0；回手不自动离手；玩家指令优先，完成且仍在外才恢复 |
| 所有猎虫产粉与寿命 | 局部命中10s至多一团；贯通每次一团；不双产；75cm半径橙褐烟团持续变形、金色火星可见；180s到期单段150cm半径固定伤害，远距只消失 |
| LTYB零粉尘/无猎虫 | 零团可飞向目标但无两段伤害；无猎虫拒绝，无Reserved残留 |
| 超收集范围/100总上限/不同拥有者 | 全选目标球内己方Available，不限最近5；半径=实测场地边长/2，上限不删除旧团 |
| 正常聚拢爆炸/猎人Montage先结束 | 准心目标到达刷新180s，1s后一次消费；每TargetActor两笔独立伤害/数字、各自半径300cm，N档和ΣDi正确；正常Montage结束不取消 |
| 聚拢中操虫/收虫、旧到期/重复取消 | 当前聚拢位置和刷新寿命保留，成员Available；旧Timer不抢爆，新玩家请求立即接管，无聚爆伤害 |
| HUD 初始/最大值变化/精华到期/落地 | 主动同步，无需先变值；比例/倒计时/舞踏立即正确 |
| LT准心/RT准心/墙遮挡/结束瞄准 | 对照永久原图四态：LT目标金黄菱形、RT目标橙色圆环；合法Hitzone决定是否有目标，关闭隐藏清色，不影响镜头/快照 |
| 换Pawn/Runtime重建/再次PIE | 根HUD一个、资源面板最多一个；旧订阅/Widget/粉尘/数字无泄漏 |
| M5最小回归 | 普通放/收虫、收拔刀/翻滚、空回→操虫斩/急袭突刺、真实落地仍正常；无新增ForceIdle死区 |

### 4.6 M6.1 实际结果与验证（2026-10-06；历史快照）

**状态：M6.1 通过。** 已完成资源成本结算结果化、粉尘类型/Actor/Resource 事务、CombatConfig 新字段、资产只读审计、数据校验及测试。代码没有创建任何正式粉尘蓝图、觉虫击/集约 GA、Montage、HUD 或 Cue 资产；未执行 PIE。

- Development Editor 冷编译成功：`Saved/M6Audit/M6_1_DevelopmentBuild.log`。
- 全量 `Automation RunTests MHGZ`：187 项，退出码 0；M6 专项 `MHGZ.M6.Resources` 2/2 通过；日志 `Saved/M6Audit/M6_1_AutomationTests.log`。
- Content DataValidation：616 项，0 错误、0 警告；日志 `Saved/M6Audit/M6_1_DataValidation.log`。
- 四个数据资产审计：`Saved/M6Audit/m6_baseline_audit.json`；动画结构只读审计：`Saved/M6Audit/m6_animation_audit.md`。审计工具不修改/保存资产。
- `AIGPowderActor` 无碰撞；粉尘状态/身份由 Resource 与 Actor 事务维护。成本消费失败时 GA 在 Confirm/Action 注册/Montage 前拒绝；测试覆盖三灯预留互斥、失效句柄、粉尘所有权/激活身份/部分过期消费/取消恢复/Runtime 清理等边界。

**当时保留的后续入口条件（已由后续工作更新）：** 在该快照时，M6.2 的完整动作时序与运动参数拟合尚未签收，因此当时尚不允许进入 E6.1；这条状态已被 2026-10-07 的 F9 数据审计、M6.2实现及资产生成结果取代。该快照中“M6.3集约动作序号/源动画仍未确认”也已被2026-10-08的用户确认和Montage审计取代：送虫、收虫、拔刀放虫共用`AS_Unsh_CaoChong`，粉尘集约专用Montage已复用该Sequence。

## 12. 当前门禁与下一条可执行任务

**最新追加入口（2026-10-12）：** 普通落地资产选择以§3.15为准，全部走`AM_IG_AerialLanding`，不再使用专用78落地；§3.14连续地面目标小抛弧保持。§3.13最低角度/0.30s裁段撤回。相机、通知/伤害窗、猎虫机制、189和功能性地面攻击保持；原作内部距离规则仍未测明。E6.3还需最低/中间俯视/近点在身后/空中/统一落地的短PIE，并保留§3.11未完成的UI/接触复验；E6.4与M6总验收未关闭。下方较早数字为历史专项，不互相替代。

**2026-10-12第二次录制的当前覆盖入口：** 按E6指导§3.11验证接触采集、贴地再次放虫、操虫斩携带/交付和觉虫击所有灯消费。上一轮39/39不包含此轮行为修改。E6.3保持待PIE；觉虫击命中剩余问题需本轮命中审计数据复核，E6.4仍未开启。不得因旧门禁或专项全绿宣称真实视觉漏判已完全修复。

> **当前入口（2026-10-12）：E6.2已验收；E6.3追加修复定向回归39/39，待短PIE。** 按[E6指导§3.10](../../editor/m6-setup.md#310-三灯携带图标与放虫瞄准追加修复2026-10-12待pie)复验三灯保持高亮、单精华椭圆/交付、臂上中心放虫/场外不瞬移及觉虫击冻结目标准确性。无需重接线或生成资产，相机不改；用户签收E6.3后再进入E6.4。完整M6既有失败保留，音效排除，普通猎虫回手精华是M7前独立欠项。

| 工作包 | 当前状态 |
|---|---|
| M5 | 已签收；这次只增加急袭突刺地面第一击的虫印副作用，运动/输入/伤害窗签收保留 |
| M6.1 | 现有成本/粉尘事务基础设施已完成；新增规则要求扩展寿命、产粉与消费合同，按M6.3实施 |
| M6.2 / E6.1 | 觉虫击动作、三灯消费、猎虫飞行/召回、命中舞踏已签收；虫粉生成新规则另在M6.3/E6.2验证 |
| M6.3 | **静态阶段已完成**：M6.3.1～.6通过，含Combat事件遥测、表现资产和DA接线 |
| E6.2 | **已验收（2026-10-11）**：用户确认虫印/虫粉/聚爆PIE通过；若公共路径后续变更仅回归受影响项 |
| M6.4 | **HUD修复专项通过（2026-10-12）**：初始化/Ready顺序及原生显示已修，Development Editor编译成功；真实BP_MHGZHUD/WBP生命周期、绑定、四态Tag与D3D12绘制专项5/5通过。原2/2资源快照/队列测试包含在本组，删除UISubsystem等历史记录保留；未宣称完整M6通过 |
| E6.3 / 收尾第1项 | **用户确认已完成并签收（2026-10-13）**：近期修复、HUD/准心及携带图标真实PIE完成；新变更仅定向回归 |
| E6.4 / 收尾第3项 | **伤害数字实现/三资产接线完成，待PIE**；构建、179/179相关回归及只读资产审计通过，见E6指导§4；其他视觉Cue延期、不纳入本轮门禁 |
| M6.5 / E6.5 / 收尾第4项 | 未开始；相关构建、自动化、资产审计及一次综合PIE签收 |
| M7 / E7 / 收尾第5项 | 未开始；发布前验证、运行清理检查、Win64 Development打包及独立Demo流程 |
| 音效 | 本轮排除，已有配置保留 |
| 普通猎虫回手精华 / 收尾第2项 | **用户确认已完成并签收（2026-10-13）**：普通采集→召回→交付独立闭环关闭，不再列M7前欠项 |

M6.3.1实现与原始验收证据：`GA_IG_ChongYin` 父类为`UMHGZMarkSlashAbility`，MarkSegmentIndex=0，`AM_IG_ChongYin`唯一攻击窗ConfigIndex=0（0.550000～0.633333s）；`GA_IG_JiXiTuCi`父类为`UMHGZDescendingThrustAbility`，四段攻击中`AM_IG_JiXiTuCi_Over`地面首击ConfigIndex=2（0.583333～0.700000s），后续地面窗为3（1.200000～1.366667s）。原合同曾为90s/10cm半宽/5cm法向容差；截至2026-10-10最新命令行回读已是90s/50cm半宽/5cm。新增急袭突刺只在GroundOver、指定段且本次激活首次成功伤害时建印；虫印斩沿用同一Resource入口。死亡、替换、世界时间到期会清理唯一虫印；伤害失败/无效目标不替换；保存Hitzone局部命中点/表面基、跟随组件，几何判定按世界厘米且不随训练桩缩放。当前边界测试与审计证据见上方2026-10-10范围更新；旧`MHGZ.M6.Mark 3/3`记录仅代表早先快照，不代表最新整组结果。

历史实现快照（2026-10-08，默认值已被M6.3.6覆盖）：`URes_InsectGlaive`负责CommandSource/CommandSerial仲裁，自动请求绑定MarkSerial与RuntimeToken；只有猎虫已离手、仍在悬停/自动飞行、没有有效玩家动作或租约且两个自动参数均大于0时才寻印。其当时CombatConfig默认范围/攻击间隔为0，后续已在M6.3.6设为3000cm/2s。其余仲裁与测试记录仍有效；PIE仍属于E6.2。

历史实现快照（2026-10-08，随后由M6.3.6补齐可调属性及DA默认值）：M6.3.3实现统一成功命中产粉、HitID去重、共享MarkedContact冷却、注册表、寿命/距离终止及自然爆炸；当前运行合同和默认值以§6.7及M6.3.6为准。旧记录中的“未接DA/粒子、倍率或距离为0、下一项M6.3.4”等均为当时状态，不是当前待办。测试和旧阶段验证记录继续作为对应历史工作包的证据。

动画源已经确定：三份送虫/收虫/拔刀放虫Montage共用 `AS_Unsh_CaoChong`，正式 `AM_IG_PowderVortex` 已使用同源Sequence，约0.766667s。新聚爆事务需在猎人动作正常结束后继续由Resource持有；不得为凑1s冻结动作或误认为时间0 Notify直接开始原地聚爆。旧实现/资产报告见§6.10。

规则与当前可调默认值见§6.2～6.7；自动寻印/聚拢范围均为3000cm，自动攻击间隔2s，远距阈值100m，单团倍率1，阶梯伤害10/50/100。历史M6.3.1～.5快照中的“待定/候选”数值已由M6.3.6及本次用户确认取代，不能再作为当前阻塞项。

每包完成后记录实际改动、构建/测试/审计和PIE证据，再更新门禁；文档修订及旧测试通过不代表新规则已实现。

**2026-10-10 粉尘团密度与虫印/虫粉区分（虫印聚爆规则已由下方更正）：** `PS_IG_PowderRedCloud`烟团连续发射率从72提高到144粒/秒，初始爆发从54提高到108；两项均翻倍，粒子寿命、75cm视觉包络、火星层和透明度不变。虫印本体不是可移动粉尘：它是附着于目标Hitzone的`UParticleSystemComponent`，由Resource单独持有，不进入`OwnedPowders`，也不播放牵引表现。命中虫印覆盖区产生的`MarkedContact`虫粉是另一个独立粉尘Actor，位置可与虫印重叠，仍按普通虫粉牵引/消耗。此前版本的`ConsumesPowderButPreservesMark`规则已被下方更正取代。验证：Development Editor编译通过；此前的`MHGZ.M6.PowderVortex` 5/5通过只验证了旧规则（`Saved/Logs/M6_PowderVortex_MarkSeparation_Automation_Final.log`）；视觉资产`AuditOnly success=true`，读回虫粉144/s、burst108，虫印仍96/s、burst144（`Saved/Logs/M6_3_6_DoublePowderDensityAudit.log`）。尚未做PIE目视签收。

**2026-10-10 虫印聚爆规则更正：** 虫印不进入粉尘成员集，也不被视觉牵引；猎虫抵达准心中心、聚爆成功开始吸粉时，若虫印当前表面点位于`PowderGatherRadius`内，则调用`ClearKinsectMark(PowderVortex)`立即清除。该消耗发生在聚爆的1s等待之前；开始吸粉后即使玩家指令取消聚爆也不恢复。抵达前取消不消耗，范围外虫印保留。回归测试验证“范围内立即清除、取消不回滚、范围外仍附着”。Development Editor冷编译50/50动作成功；`MHGZ.M6.PowderVortex` 5/5通过，含新测试`ConsumesMarkAtGatherStart`，日志`Saved/Logs/M6_PowderVortex_ConsumeMark_Automation.log`（进程退出码0；测试开始前有两条无测试名的通用`Condition failed`日志，五项聚爆测试均报告成功）。PIE仍待确认。

**2026-10-10 聚爆移动与地面目标PIE问题修正：** 最新录制显示，聚爆 GA 继承普通攻击的`Combat.State.BlockMovement`，激活下一帧即把`TargetCruiseSpeed/DesiredSpeed/LocomotionInputMagnitude`清零（猎人速度从约394cm/s降至近0）；现增加攻击移动锁配置，普通攻击默认仍锁移动，`UMHGZPowderVortexAbility`默认不锁，Montage无Root Motion，因而可保留常规MM移动。地面目标失败源于`bAllowWorldHitAtTarget`只在“BlockingHit存在但HitResult缺失”的回退路径设为true；普通组件命中（包括常规地面Trace）未开启贴近目标点的世界碰撞到达判定，导致`VortexFlightEnded=WorldHit`并取消。现对输入快照中的任一有效BlockingHit启用此容差，仍要求碰撞点落在原有`ArrivalRadius + Kinsect半径 + 2cm`内，远处遮挡继续取消。Development Editor冷编译成功；新增移动策略和表面碰撞自动化覆盖，`MHGZ.M6.PowderVortex` 6/6通过，日志`Saved/Logs/M6_PowderVortex_MobileGroundTarget_20261010.log`（启动阶段两条既有通用`Condition failed`，自动化测试本身全部成功）。必须重做PIE：持续推摇杆施放聚爆、对准地板施放、分别测试近目标表面接触与被远处遮挡；E6.2未因此自动签收。
