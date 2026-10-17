# 蒙太奇内部片段惯性混合

更新：2026-10-16。**通用基底已实现并接线；原22份多序列Montage的0.05s内部混合配置继续保留。已移除5个合成Blend与10个攻击EntryHold；其中4份Montage现为单序列，当前多序列数为18。本轮新增二连→飞圆310→117显式持械约束，代码/资产及定向检查完成，待局部PIE视觉确认。** 历史检查不自动重新成为门禁；当前签收以[状态入口](../using/current-status.md)为准。本功能自身不自动改入口路由；已批准的清理与入口变化见下文和[六组入口修正](ground-attack-entry-fixes.md)。

本次扫描`/Game`共54份Montage，匹配16份虫棍攻击、1份收刀和5份持刀/收刀翻滚；其余32份保持不变。同一Sequence的不同裁切/循环不计作多个序列。完整资产名单及验证见`Saved/Maintenance/MontageSegmentBlendBatch/applied.json`和`disk-verify.json`；上捞/拔刀原0.06s也已更新为0.05s。

## 配置方法

1. 打开需要处理的常驻 Montage，在资产 Details 中找到 **Meta Data / 元数据**（不是某个 Section 的元数据）。
2. 添加 `MHGZ 蒙太奇内部片段混合`，原生类型为 `MHGZMontageSegmentBlendSettings`。
3. 打开“启用内部片段混合”，设置“混合时间（秒）”并保存。当前已配置资产统一使用 **0.05s**，不是原作真值或已签收观感参数；资产实例值以这里的配置为准。
4. 这份 Montage 的不同 Sequence 片段切换时自动请求混合。没有此元数据、关闭开关或时间为0，都维持原行为。

混合发生在目标动画继续正常播放的过程中，不插入动画、不延长总时长，也不会先停住等待混合。时间按实际秒数计，不再乘 Sequence RateScale 或 Montage PlayRate。当前按用户要求统一配置0.05s，后续可按局部观感在单份资产中调整。

当前使用同一 Montage 统一时间，作用于其不同序列接缝。批量配置阶段曾保留人工桥接，造成“原片段→桥接→目标”两次真实序列变化；随后按用户授权移除了飞圆、横扫、突进中的5个合成Blend片段，再移除8份攻击中的10个EntryHold。翻滚保持段、蓄力和功能性IP/RM资产保留。若未来需要按接缝覆盖时间或禁用某个接缝，应另行扩展配置，不靠文件名猜测。

## 二连→飞圆310→117持械约束（2026-10-16）

最新两份录制（133700正常、133736慢放）定位到310短衔接结束→117大回旋，不是GA启动，
也不是304被错误播放；全程Slot权重1。真实ABP的最终混合姿势出现左右持械参考分离约45–78cm，
而右参考相对右手只漂移约0.09cm。单纯延长身体混合不能保证两个辅助骨维持持械关系。

当前已保存链路：DefaultSlot→原Inertialization→LocalToComponent→CopyBone→ComponentToLocal
→原全身俯仰链路。CopyBone复制`R_Weapon_00`的位置/旋转到`L_Weapon_00`，不复制缩放，
Alpha绑定原生`WeaponGripConstraintAlpha`。不移动手臂，不更改Weapon_L插槽或武器挂接，
UpperBody独立链路保持不变。新增功能的Montage元数据默认关闭，仅飞圆资产明确开启：

| 字段 | 当前值 |
|---|---|
| 启用接缝持械约束 | 开启（其他Montage默认关闭） |
| 来源序列 | AS_Unsh_R_ErLianFeiYuan（310） |
| 目标序列 | AS_Unsh_R_FeiYuan（117） |
| 保持时间 | 复用该Montage内部混合Duration=0.05s |
| 约束退出时间（秒） | 1/30s≈0.0333s，Smoothstep退出 |

这些字段在Montage Details的`MHGZ 蒙太奇内部片段混合`元数据内可调，不需要新增通知。
仅同实例、DefaultSlot实际发生指定来源→目标时启用一次；直接117、横扫304→117、同序列裁切、
其他Montage不参与。混出、暂停、停止、目标退出、元数据关闭、初始化/卸载清除约束。
保持/退出与身体混合使用同一动画更新时钟；没有输入死区、额外动画帧或时长补偿。

实际ABP逐帧对照60/120FPS及0.2慢放：接缝峰值44.53/73.64/77.57cm→各1.27cm，
完全约束阶段位置/旋转重合，退出后无大幅二次分离；所有非目标/非其子骨姿势、缩放、
Montage时间、身体混合请求和根运动提取保持。7项定向检查通过，独立磁盘回载通过，
**尚待二连→飞圆局部PIE观感确认**。两份录制没有横扫样本，本轮不自动给横扫套用约束。

复用现有脚本`--weapon-grip --apply`，只保存ABP与飞圆元数据，已完成后不重复Apply；
`--verify-weapon-grip`独立只读核对。Commandlet开关`-MHGZWeaponGrip -MHGZSegmentBlendApply`，
只读`-MHGZWeaponGripVerify`。事务及精确回退备份在`Saved/Maintenance/FeiYuanWeaponGrip/applied.json`；
技术证据`Saved/Maintenance/FeiYuanGripFix20261016/AcceptedTests`。不要复建旧合成Blend或改变入口帧。

## 已清理的合成混合片段

2026-10-14，只修改下列3份Montage；移除引用，不删除Sequence文件。剩余源裁切、倍率、Section名称/NextSection、通知GUID/对象/轨道/配置保持，时间轴仅扣除已移除片段并重定位通知。全局RootMotion/接受窗口跨过桥接的部分随时间轴缩短，保留原动画时机；总根位移不宣称不变，因为桥接自身原本含推进位移。

| Montage | 已移除片段 | 物理总长（前→后） |
|---|---|---|
| `AM_IG_FeiYuan` | `ErLianCoreBlend`、`HengSaoCoreBlend`、`RecoveryBlend` | 3.941667→3.858333s |
| `AM_IG_HengSao` | `CoreBlend` | 2.325000→2.308333s |
| `AM_IG_TuJinHuiXuan` | `EntryToCoreBlend` | 4.900000→4.766667s |

这些是包含不同可选入口的资产物理总长度，不是每条运行路线都缩短同样时长。飞圆310/304→117及117→借用二连恢复段、横扫309→109直接使用导入来源；突进仍保留EntryHold、原作起手、普通/蓄力分支，两秒Charge不变。独立磁盘重载、0.05s批次回查及原入口检查点兼容性通过，尚未做本轮PIE视觉签收。证据`Saved/Maintenance/MontageBridgeRemoval`，备份路径见该目录`applied.json`。

## 攻击EntryHold清理（已保存）

2026-10-14，虫印斩2个、飞身1个、横扫1个、后撤1个、袈裟1个、四连1个、突刺2个、突进1个，共10个片段引用已移除。原Sequence文件暂存用于回退；四方向持刀翻滚保持段及另一目录中的收刀翻滚均未修改。

| 合并的旧Section | 当前入口 | 同步的GA |
|---|---|---|
| 飞身`Entry_WithBlend` | `Core` | `GA_IG_FeiShenYueRu`，3条来源映射 |
| 袈裟`Entry_From_TuJinHuiXuanZhan` | `Entry_From_TuCi` | `GA_IG_JiaSha`，1条来源映射 |
| 四连`Entry_From_ShangLaoFeiShenHouChe` | `Entry_From_ErLian` | `GA_IG_SiLianYinZhan`，3条来源映射 |
| 突刺`Entry_From_FeiYuan` | `Core` | `GA_IG_TuCi`，1条来源映射 |

只合并删除后等时的入口，不改变其实际原动画起始帧。其他入口、剩余源裁切/倍率、蓄力规则、混入混出和0.05s元数据保持。通知GUID、轨道、对象及配置不改，按源序列相位重定位。突刺DodgeAcceptWindow原越界0.10s、后撤Combo/Dodge窗口原越界0.05s，收回实际末帧；不裁短动画，也不提前关闭原本能播放到的窗口。

8份Montage和4份GA保存前已备份；独立磁盘重载检查包括GA图与非入口CDO属性保持、92个范围外相关资产哈希不变。52份目录复扫显示攻击Hold=0、仅余4个未修改的翻滚Hold。证据`Saved/Maintenance/AttackEntryHoldRemoval`，备份见`applied.json`；维护Python17项通过，未运行PIE、C++编译/原生自动化检查或全量回归。

## 已接通的链路

- `ABP_MH_Character`：`DefaultSlot → Inertialization → 显式接缝持械约束 → 原全身俯仰链路`（默认Alpha=0）。
- 同一 ABP：`UpperBody_IGAction → 独立 Inertialization → 原缓存/上下身混合链路`。
- 节点已保存；再次执行接线会先审计，不重复插入。MM 路由、Pose History、原 Slot、俯仰节点及后续连接保留。
- `UMHGZMotionMatchingAnimInstance::Montage_Advance` 在 `Super` 推进后比较上一次动画更新与本次的真实实例ID、Slot、片段及序列；调用 `RequestSlotGroupInertialization` 后交给 UE 更新图。

请求必须早于 `UpdateMontageEvaluationData` 复制并清空请求。不能改到 `NativeUpdateAnimation` 再请求，否则 UE 的实例请求表会残留到下一次更新，出现重复触发。每组每次更新最多一个请求，没有“接下来几帧持续请求”的倒计时。

自然推进、引擎内部 Section 路由，以及两次更新之间的 Section 跳转/定位，都按实际序列变化检测。新实例启动/重播、同序列循环或裁切接缝、暂停、停止/混出和仅更新不可见 Montage 的阶段不额外请求；弱引用快照在初始化/卸载及不可用阶段清理。请求在同组多Slot时合并，不保存裸 MontageInstance 指针。

这是姿势惯性混合，不是同时推进两条旧/新 Sequence 的交叉淡化。通知、伤害窗口、Root Motion 提取、Section 时间及原序列倍率不被这套代码修改；0.05s也不是输入死区。

## 工具与最小验证

已有编辑器桥接扩展为 `unreal.MHGZAnimationSeamLibrary.install_montage_segment_inertialization(blueprint, apply)`：检查或接线/编译，但不自行保存。包装脚本 [setup_montage_segment_blend.py](../../Scripts/AssetOrganization/setup_montage_segment_blend.py) 默认只读；默认模式处理ABP接线，`--configure-montages`模式只处理多序列Montage元数据（默认0.05s，可用`--duration`覆盖），`--verify-montages`只读验证已保存批次。显式应用前备份，不在批量模式保存ABP或Sequence。编辑器已开时使用`Scripts/ue_remote.py`；关闭时使用`-MHGZSegmentBlendMontages`选择批量模式，叠加`-MHGZSegmentBlendApply`才写入，`-MHGZSegmentBlendVerify`独立只读回查；不是普通的UE `--apply`。

同一脚本的`--remove-synthetic-bridges`（Commandlet：`-MHGZSegmentBridgeRemoval`）仅按明确的5个资产列表生成配方，显式`--apply`/`-MHGZSegmentBlendApply`备份后保存上述3份Montage；`--verify-bridge-removal`/`-MHGZSegmentBridgeVerify`只读回查。复用`rebuild_single_slot_montage`，不增加原生工具接口。已完成检查点拒绝重复Apply；原0.05s批次验证识别此次已批准的结构变更并逐项验证，不要求恢复旧片段。

`--remove-attack-holds`/`-MHGZAttackHoldRemoval`审计本次10个攻击Hold配方，叠加显式Apply才备份/保存；`--verify-attack-holds`/`-MHGZAttackHoldVerify`独立只读回查。旧Bridge/0.05s/入口检查点在该阶段完成后转向最新已批准的清理状态，不复建旧Hold。不要以旧DLL中7/8片段或旧GA映射断言判断当前资产错误；原生检查源码已同步，下次常规编译生效。

技术检查筛选：`MHGZ.Maintenance.Animation.SegmentBlend`，仅三项：策略、磁盘 ABP 接线/幂等、真实 ABP 的60/120FPS Montage推进。后者检查切换当帧请求、下一帧不重发、外部跳转、暂停、未配置兼容性和原时长/根运动提取保持。证据见 `Saved/Maintenance/AttackInertializationStatus`；这是定向技术验证，不代表任何招式的 PIE 视觉签收或发布验收。

调试时可在 AnimInstance 观察 `MontageSegmentBlendRequestSerial`；它只在新请求时递增。启用 `Log LogAnimation Verbose` 可查看带 `[MontageSegmentBlend]` 的组名/时间/请求序号。

基底资产回退使用`install.json`中的精确备份路径；批量元数据修改使用`Saved/Maintenance/MontageSegmentBlendBatch/applied.json`中的独立备份路径。关闭编辑器后只恢复对应目标，勿用Git reset覆盖用户其他未提交修改。基底接线阶段未改Montage元数据；局部入口修正先启用上捞/拔刀0.06s，随后本次授权将全部22份多序列Montage统一为0.05s。批次检查点已存在时拒绝重复写入，续接应先使用只读验证。
