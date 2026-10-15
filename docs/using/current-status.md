# 当前签收与维护任务

更新：2026-10-15。本页是当前状态入口；档案中的旧日期“下一步/待验收”不自动恢复成任务。状态基于用户确认与已保存证据，未执行的项目保持未执行。

## 当前维护检查点：拔刀/收刀与 MM 接缝（2026-10-15）

**最新检查点：** 新两份正常/低倍速录制仍暴露Start→Loop、Exit→Loop与自然换圈接缝；用户授权后已修：Start Intent归零后按最终脚姿/脚速度/根速度一次进入Loop库；Start/Exit采用限于源剩余可播放时间的活跃根运动交叉混合，只有Run↔Sprint仍用惯性混合；四条Walk/Run/Sprint Loop首尾局部闭合，保留时长、帧数、倍率、通知、曲线及整圈累计Root Motion。两专用PSS增至17维并保存布局版本，普通Move/Start/Stop Schema、ABP用户保存的0.1s及输入/Stop规则不变。只保存6资产并备份，独立回载/保护哈希核对通过；最终增量编译及6项移动定向测试全部Success、0测试错误（临时副本压缩加载4提示，正式资产作者/回载0警告）。详见[最新维护实施§9](../editor/locomotion-maintenance-20261015.md#9-新两份录制后的-startexit-与自然循环修复当前实现)，证据`Saved/Maintenance/LocomotionEntryRepair20261015/`。**未做新版PIE视觉签收**；仅需局部观察上述接缝，不重开其他签收、全量验证或Git。下方段落为先前检查点过程，不按旧方案重新Apply。

用户要求先修非MM问题，再诊断MM，不提前修改MM行为。基于最新录制`20261014-230556-BP_IG_Character_C_0-101684`，已实现：拔刀斩Montage混入0.25→0.05s（实际GA入口正确，起手被长混入弱化）；收刀成功启动时及既有通知中幂等召回猎虫；保留持刀按RB的物理状态，收刀提交后无需重按即可请求Sprint。只改一份Montage，女性收刀序列与其他脏改动保留。增量编译、两个本轮技术测试及独立磁盘回读通过；尚未用户局部PIE签收，未做全量回归/Git。

随后用户授权按建议顺序实施，现已保存MM/ABP修复：12节点RF_Transactional持久化及两个作者工具的创建标志；两MM节点MaxActiveBlends=2（0.05s保持）；现有Pose History移至最终姿势输出前；移动Exit安全尾段改为对应Loop-only库的一次切库搜索，持续移动不开放Start/Idle，松杆仍走原一次性Stop；Run/Sprint专用LoopPhase Schema使用切库前的显式cos/sin循环相位，排除半周期反脚候选。新建两份Walk Loop-only库、一份独立Schema，未改源动画、普通Start/Stop Schema及权重。持刀稳定Loop的源动画起伏未修改。

随后两份正常/0.2倍速PIE仍暴露Run↔Sprint前后跳及Exit接Loop脚姿不连续，首轮方案未获视觉签收。用户再次授权后已实施：不随切库重置的Run/Sprint相位时钟，最近真实索引帧与0.001周期门槛；Exit从最终Pose History按双脚位置/速度确定一次Loop入口；只清目标Loop库自身重选历史；两MM各接专用惯性节点，Loop语境沿用0.05s原生惯性混合，其余语境恢复原配置。普通Move/Start/Stop库、Schema、输入响应、松杆Stop与源Sequence保持。

最新增量编译、5项`MHGZ.Maintenance.Locomotion`定向测试、资产独立磁盘重载及语义连线检查通过：保留331个旧节点，仅增加两处MM惯性节点；普通Move库/Schema哈希不变。状态与UE5.6适配注意见[实施记录§8](../editor/locomotion-maintenance-20261015.md#8-两份pie之后的连续相位修正)，证据`Saved/Maintenance/LocomotionFollowup20261015/TestsAccepted/index.json`及`continuity-*.json`。当前仍待新版局部PIE视觉确认，不把旧录制或技术测试当成新版观感签收；不重开全量回归/旧门禁，不提交或推送Git。

### 本地版本检查点（2026-10-15）

根据用户“进行版本管理”的要求，将上一检查点以来的维护成果整体收录到`main`的本地提交：编辑器工具模块迁移、录制/维护工具、伤害单位、虫棍动画配置及最新MM接缝修复。录制、缓存、备份仍留在忽略的`Saved`中，不推送远端；版本检查点不改变上方“待局部PIE视觉确认”状态，不重开全量回归。

## 有效签收

| 范围 | 当前状态与证据 | 重新验证条件 |
|---|---|---|
| Demo 日常开发 | 用户确认阶段性完成，进入小修维护；全量回归转为按需 | 用户要求、公共依赖重大变更或实际回归 |
| M5 空中动作 | 用户确认完整验收；见[阶段档案](../archive/demo-2026-10-13/m5-plan.md)与各动作计划 | 本轮修改直接影响对应输入、动作接管或运动 |
| M6.2 / E6.1 觉虫击 | 动作、三灯消费、召回和命中舞踏已签收 | 对应逻辑变化或新故障 |
| E6.2 虫印/虫粉/聚爆 | 用户确认验收；见[M6档案](../archive/demo-2026-10-13/m6-plan.md) | 对应生命周期、指令仲裁、生成/消费变化 |
| E6.3 / 收尾1、2 | 用户确认 HUD/准心与普通精华回手闭环完成；M6档案当前状态表保留 | UI创建/引用/显示条件、采集与交付变化 |
| E6.4 伤害数字 | 实现和资产接线完成；本对话未单独确认其正式 PIE 门禁关闭 | 修改数字表现时只检查该表现；不自动重开 M6 全量验收 |
| M7 打包/发布 | 尚无完成证据 | 用户要求发布时启动发布检查 |

音效与额外击中特效不纳入当前维护期补验收。低优先级边界见[已知问题](项目已知的不重要问题.md)，不因阅读档案而自动扩展范围。

## 最近已完成变更

- 伤害单位：虫棍 GA 的37个伤害段动作值均为10；物理肉质按0–100保存，执行公式保留动作值/100与肉质/100。训练桩三个默认肉质为100。Development Editor编译和磁盘重载审计通过；没有运行本轮PIE或全量回归。证据：`Saved/DamageUnitsAudit/after.json`及重载审计报告；设计见[伤害执行](../design/exec-calc.md)。

### 飞圆入口试验（2026-10-13，用户已目视签收）

根据用户授权，上捞/后撤→飞圆均从117源序列第0帧进入（复用`Entry_From_FeiShen`），对应两条Combo边混入0.10s，上捞/后撤Montage默认混出0.10s。来源默认混出也会作用于它们的其他退出；其他67条Combo边、二连/横扫/飞身/急袭入口、飞圆片段/通知及RateScale=2不变。只保存4个资产，保存前已备份；独立UE进程磁盘回读通过。没有修改或编译C++、提交Git或执行全量回归。

用户随后确认UE自身混合下动画十分流畅，保留此次配置，不再重新要求验收这两条来源。0.10s仍是视觉试验参数，不是原作测量真值。诊断和修改证据见[飞圆报告](../../Saved/Maintenance/FeiYuanEntry/report.md)，备份目录`Saved/AssetBackups/FeiYuanEntryTrial-20261013-1791904240630624900`。横扫入口多插片段已在以下新任务处理。

### 飞圆/横扫内部一帧桥接（2026-10-14，已保存，用户目视认为无效）

本次只调整两份常驻Montage，新增三个常驻Sequence；无玩法C++、GA/Combo、原导入序列修改。桥接时长按实际播放60FPS一帧，即1/60s，RateScale=1。身体姿势为相邻两端50%中间姿势，根轨道按相邻速度均值推进，不是零位移停帧；因此局部总位移会有小幅变化，不能宣称曲线逐点不变。

| 接缝 | 当前保存的结构 | 新资产（`/Game/Weapons/InsectGlaive/Anims/Sequences/Generated/`） |
|---|---|---|
| 二连→飞圆 | 310完整段→一帧桥接→117源第33帧Core | `AS_IG_FeiYuan_ErLianCoreBlend` |
| 横扫→飞圆 | 304完整段→一帧桥接→117源第33帧Core；替换旧117第26–33帧额外片段 | `AS_IG_FeiYuan_HengSaoCoreBlend` |
| 横扫内部 | 原入口保持段→309完整段→一帧桥接→109源第35帧 | `AS_IG_HengSao_CoreBlend` |

Rise最新录制确认横扫实际使用309→109、有效2倍速；干净样本109的首个采样拟合到源36/37帧，扣除采样相位后试取第35帧，不是直接读取到的动作帧真值。项目此前从第32帧开始，前0.5s根位移拟合误差约12–16cm；正确结构不需要换序列。既有横扫4帧入口保持不在本次移除范围。

`AM_IG_FeiYuan`总长3.941667s、8片段；`AM_IG_HengSao`总长2.325s、4片段。两者各5个通知的GUID、轨道、伤害索引/配置保留，时间随原来源片段重定位；全局RootMotionPhase仍覆盖对应动作结束。上捞/后撤直接117第0帧及0.10s混入/混出保留。尾帧通知的UE管理边界epsilon可能在0与-0.0001s间归一化，作者时间不变。

定向增量编译成功（仅`MHGZToolsEditor`，最后4.09s），独立UE进程磁盘重载全部断言通过：三个桥接各75骨骼中间姿态、推进根位移、长度/连贯片段、通知身份与源时序，以及69条Combo行与已签收入口。证据`Saved/Maintenance/FeiYuanEntry/seams_disk_verify.json`、`seams-disk-verify.log`；备份`Saved/AssetBackups/GroundAttackSeams-20261013-1791907219990598800`。未运行全量回归/全资产验证、未提交Git，也不据脚本成功声称PIE视觉通过。

用户随后反馈一帧段没有达到持续混合的视觉效果；这不是视觉签收，不能把磁盘检查升级为混合通过。当时运行时惯性混合仅讨论过；2026-10-14按后续授权完成下述通用基底，不代表一帧桥接已有效或现有招式已推广并签收该混合。

原作录制纠正（2026-10-14）：用户确认原游戏突进回旋斩没有蓄力部分，只需录完整普通动作；不得再要求“不蓄力/按住后松开”两种原作样本。项目现有`Charge` Section及按住保持与原作区别开来，本轮不改代码或资产。

当前录制范围已只读核对：Weapons目录52份Montage，10份攻击＋4份持刀翻滚引用22个专用桥接/保持序列；3份攻击含合成桥接、另7份攻击仅含保持。见[录制清单](../../Saved/Maintenance/WeaponMixInventory/录制清单.md)及同目录`inventory.json`。特别区分当前可达入口：上捞Idle保持未被默认Core路由使用，飞圆→突刺却会顺序经过两段保持共0.10s；不把原作304/310等衔接误算成项目合成混合。模型FPS、平台采样、倍率和实际秒数已分开保存。清单检查0错误/0警告、未保存玩法资产；不是全量回归或正式验收。

**录制研究范围随后扩大（2026-10-14）：** 用户要求所有由不同Sequence组成的虫棍Montage，不限人工混合。已对同52份重新只读核对，21份引用至少2个不同序列：16份攻击、1份收刀、4份持刀翻滚；另外31份是单序列。新增拔刀斩、二连、操虫斩、急袭突刺、觉虫击、强化操虫穿刺及收刀。[全部拼接蒙太奇](../../Saved/Maintenance/WeaponMixInventory/全部拼接蒙太奇.md)和`composition_inventory.json`记录该次快照，旧专用混合清单仅作子集；后续`/Game`实际资产配置以本页最新批次为准。Section可选入口不按物理顺序串播（收刀Idle/Walk互为选项）；同一原作序列裁出的不同资产也单独标明，急袭突刺应对照World而非Rise。只保存报告，未改Content/玩法C++。

**录制执行清单已展开（2026-10-14）：** [录制清单](../../Saved/Maintenance/WeaponMixInventory/录制清单.md)现在是总执行入口，包含G42地面来源、A10操虫斩/强化穿刺、W7急袭、X8空中招地面段派生、E7觉虫击、F9收刀/翻滚，共83个可勾选观察条目。已对照快照核对涉及16份攻击Montage的55条Combo边全部列入，无重复/漏项；这是文档覆盖检查，不是83项玩法回归。条件性项可N/A，已有World/其他录制可复用，不要求83份新录，也不要求毫秒级操作；原作突进无蓄力、觉虫击项目保持不可擅自当作原作规则。构成表负责资产/Section信息，专用混合子集移到执行清单附录。未改代码、动画或签收门禁。

此前受保护Notifies读取导致中断时尚未保存；后续接入已写入但总时长断言失败，已修正工具刷新长度并只保存长度修复，**不得重新执行`apply_seams.py`再次插入**。资产写入状态仍以磁盘验证报告为准；显示效果以用户反馈为准。

### 蒙太奇内部惯性混合基底（2026-10-14）

按用户授权仅实现通用基底：新增常驻Montage可编辑/可打包的`MHGZMontageSegmentBlendSettings`元数据，缺省无配置不改变动作。实际不同Sequence切换时，每Slot Group单次请求`RequestSlotGroupInertialization`；请求位于Montage推进后、UE复制/清空请求前，避免延后一帧和次帧重发。按实例ID隔离重播，兼容Section跳转，排除同序列循环、暂停、停止和不可见仅Montage更新。

ABP两个动作Slot已分别接上Inertialization，原俯仰/缓存链路保留；基底阶段只保存这一份ABP，保存前备份并核对哈希。当时没有给现有Montage添加混合元数据或改入口；后续授权的六组入口接入见下节。如何使用见[内部混合说明](../editor/montage-segment-blend.md)。

Editor增量编译及`MHGZ.Maintenance.Animation.SegmentBlend`三项技术检查通过，覆盖实际ABP、60/120FPS切换当帧请求及次帧清除、外部跳转、暂停和无配置兼容；原时长/Root Motion提取保持。独立磁盘回读、Game两源文件检查结果见本轮报告。证据`Saved/Maintenance/AttackInertializationStatus`。未做PIE视觉或全量回归；后续按明确授权完成下面的局部入口及批量元数据配置，不重开基底验证。

### 六组地面入口修正（2026-10-14，已保存，待局部视觉确认）

按用户指定修正站立/后撤→上捞0帧、飞身/普通突进→308→上捞7帧、反向突进→305→上捞0帧、上捞→袈裟119第0帧、拔刀300→102第67候选帧、站立突进160第0帧。保留项目蓄力：站立入口内松键走正常速度55–67帧后进Core，保持按住仍走原两秒Charge，Charge内松键仍进Core。其他突进来源路线未重做；横扫→上捞仍为7帧入口。具体结构、通知、候选精度与后续短测见[入口修正说明](../editor/ground-attack-entry-fixes.md)。

308原Root Motion关闭，新增原骨骼/原轨道不变的常驻Root Motion副本，原导入资产保持。仅上捞和拔刀斩启用内部0.06s混合；不是原作Blend真值。没有改四连、虫印、其他招式保持段、已签收飞圆入口、ABP或Combo。人工保持仅按指定路线录制处理，不恢复全项目冻结清理任务。

四Montage、两GA、一新Sequence已备份后保存；Editor编译、蓄力类Game定向编译、独立磁盘重载、GroundEntries三项技术检查通过（入口/倍率与混合元数据、原蓄力路由与未蓄力连续源帧、308全部骨骼复制一致）。维护脚本14项检查通过。证据`Saved/Maintenance/GroundAttackEntryFix`；没有PIE视觉/预输入实机签收、全量回归或Git提交。保留所有原未提交修改。

### 所有多序列Montage内部混合（2026-10-14，已保存）

按用户要求刷新`/Game`实际清单：54份Montage中22份引用至少两个不同Sequence，含16份虫棍攻击、1份收刀、5份持刀/收刀翻滚。全部启用内部混合0.05s，上捞/拔刀的原0.06s同步更新；另外32份Montage不变。同一序列的循环或多段裁切不纳入本次选择。

仅写Montage内嵌元数据；片段、Section、通知及其配置、原混入混出、倍率/总时长的原生文本导出均保持，ABP/GA/Combo/Sequence不写入。已有人工桥接仍保留，其两侧不同序列切换各自请求0.05s，不合并成一个接缝。独立UE进程磁盘重载检查通过（0错误/0警告），全部目标元数据值一致、其余32份磁盘哈希不变；没有C++编译、PIE视觉签收、全量回归或Git提交。

工具复用并扩展`Scripts/AssetOrganization/setup_montage_segment_blend.py`；清单/检查点/验证证据在`Saved/Maintenance/MontageSegmentBlendBatch`，备份`Saved/AssetBackups/MontageSegmentBlendBatch-1791980100570137000`。使用与续接见[内部混合说明](../editor/montage-segment-blend.md)。

### 合成视觉桥接清理（2026-10-14，已保存）

按后续授权，从飞圆移除`ErLianCoreBlend`、`HengSaoCoreBlend`、`RecoveryBlend`，横扫移除`CoreBlend`，突进移除`EntryToCoreBlend`。只移除3份Montage中的5个片段引用，合成Sequence文件暂存以便回退；原作衔接、EntryHold、两秒Charge、GA/Combo/ABP和源Sequence不改。剩余源裁切/倍率、Section名称/路由、通知GUID/对象/轨道/配置保持；时间轴扣除桥接后同步重定位Section/通知，内部混合仍0.05s。

飞圆物理长度3.858333s（原3.941667）、横扫2.308333s（原2.325）、突进4.766667s（原4.900）；可选入口的实际播放不能按物理总长解读。移除桥接自身根位移，不能声明整段Montage总位移不变。备份`Saved/AssetBackups/MontageBridgeRemoval-1791982364603214500`；证据`Saved/Maintenance/MontageBridgeRemoval`。

独立UE磁盘重载、原0.05s配置批次、六组入口检查点兼容性均通过（各0错误/0警告）；维护Python15项检查通过。原生GroundEntries检查源码同步到0.05s及突进7片段的新索引；本轮未编译/执行这些原生检查，未改玩法C++、未做PIE视觉/全量回归或Git提交。历史一帧桥接报告仍保留，但不是当前结构；后续续接先回查本次检查点，不重复旧Apply。

**后续残留扫描（2026-10-14，只读）：** 用户指定虫棍Montage目录全部52份已刷新核对，五个Blend引用无残留；仍有8份攻击引用10个视觉EntryHold，另4份持刀翻滚引用4个静止EntryHold。攻击保持属于前次漏清；翻滚代码仍将3帧作为Root Motion/supersede交接缓冲，清理须同步核对交接时序，不可仅删段。功能性IP/RM/下坠循环和蓄力不列为混合垃圾。清单、真实入口、逐源键姿态及合并Section/通知边界风险见[残留扫描报告](../../Saved/Maintenance/MontageRemainderAudit/report.md)和同目录JSON。Commandlet0错误/0警告、脏包前后均空；本轮未清理Hold、未保存Content或运行PIE。

### 攻击EntryHold清理（2026-10-14，已保存）

按用户授权移除上述8份攻击Montage的10个EntryHold，原保持Sequence文件暂存，四方向持刀翻滚及收刀翻滚不改。合并飞身、袈裟、四连、突刺删除后等时的入口，并仅更新这4份GA共8条来源入口映射。原导入动画裁切/倍率、通知身份/配置、其他GA/CDO属性、蓄力和0.05s混合保持；通知按原动画相位前移。仅将突刺Dodge窗口和后撤Combo/Dodge窗口原本无法播放到的越界末端收回真实末帧，不裁短动画。

只保存8份Montage＋4份GA；备份`Saved/AssetBackups/AttackEntryHoldRemoval-1791986591592657400`，检查点/原生导出/配方/磁盘验证在`Saved/Maintenance/AttackEntryHoldRemoval`。独立磁盘回读通过（0错误/0警告），GA图与非入口CDO属性一致，92个范围外相关资产哈希不变；目录52份复扫，攻击Hold=0，只剩4个翻滚Hold，脏包前后均空。0.05s配置回查通过，原22份配置仍保留，其中4份已变单序列，当前`/Game`多序列Montage为18份。

维护Python17项通过；修正了原生导出UTF-16/CRLF读取，旧Bridge/混合/入口验证识别最新批准的清理，不要求恢复历史保持片段。原生GroundEntries检查源码同步到袈裟2片段、突进6片段；本轮未编译/执行这些原生检查，未改玩法C++、未做PIE视觉/全量回归或Git提交。使用与入口合并表见[内部混合说明](../editor/montage-segment-blend.md)。

## 当前维护任务

### 女性收刀动作替换（2026-10-14，已保存）

用户新导入的`female003`、`female009`分别原位覆盖现有`AS_UnSh_ShouDao_Idle`和`AS_UnSh_ShouDao_Walk`的75条骨骼轨道；资产名称/骨架/时长/RateScale=2保持，原男性动作数据不再作为另一个运行版本保留。移动收刀的`AS_Shth_Sheathe_MoveExit`同步取female009源第108帧至末帧，以原120FPS/RateScale=1烘焙，时长仍0.35s，避免MM接管后回到男性姿势。

只保存3份既有Sequence；`AM_IG_ShouDao`磁盘哈希未变，通知、GA、PSD引用和Handoff时序不改。一次独立UE磁盘回读检查三份序列首/中/末帧全部75骨骼与女性源一致（0错误/0警告），未做全量验证、C++编译或PIE签收。证据`Saved/Maintenance/FemaleSheathe/applied.json`及`disk-verify.json`，精确备份见applied.json。执行中通用AnimDataController与AnimationSequencerDataModel类型不匹配曾在保存前终止，已改用匹配的AnimSequencerController完成；前述失败未写入资产。

目标：按用户授权顺序优化 agent 工作流程，减少重复探索和重复验证。

| 顺序 | 内容 | 状态 |
|---|---|---|
| 1 | 简短根 AGENTS 入口和按任务读取规则 | 已完成 |
| 2 | 工具索引、调用环境与已知限制 | 已完成：107个脚本、43个Commandlet入口（补齐两个原先漏列的历史工具） |
| 3 | 通用远程执行入口、反射读取复用与分层错误 | 已完成真实验证：项目编辑器连接、带参数只读脚本、原UI入口/CDO读取均成功；本轮修复多行语句包装 |
| 4 | 有效签收记录和续接检查点 | 已完成；沿用用户既有签收，不新增PIE通过结论 |
| 5 | 小修验证范围与录制器即时健康检查 | 已部署并核对哈希：Rise1.12.0、World0.2.8；World宿主确认加载0.2.8。用户本轮要求不再继续游戏录制测试，健康提示实机未签收 |
| 6 | 编辑器工具模块拆分收益/风险评估 | [A+B约定范围及后续Trace文件锁修复完成](../editor/maintenance-module-assessment.md)：43个Commandlet及UI构建/Trace导出实现归ToolsEditor，原稳定接口保留；累计清理八项运行时编辑依赖及Bcrypt。原有迁移验证继续有效；后续Services四项通过，首次自动导出不再需测试补偿，锁超时/数据保留/其他Trace不受影响已定向检查。见[整体方案](../editor/maintenance-module-full-plan.md) |

本轮保留伤害修改的未提交文件；不运行游戏全量回归、不重建玩法资产、不提交Git。用户随后明确授权启动项目编辑器、部署并测试录制器，已按范围执行；本轮最后收缩为完成第3项即可，停止继续第5项游戏录制。

第3项实际证据：`Saved/Maintenance/ue_remote/live-probe.json`（1.015秒）、`live-script.json`（1.263秒）和`live-ui-probe.json`（1.021秒），均为success=true。编辑器使用本项目`MHGZ.uproject`启动；带参数的只读脚本成功生成`Saved/AssetOrganization/maintenance-live-assets.json`，UI静态引用检查errors=[]。不以这些结果代替游戏PIE签收。

部署证据：`Saved/Maintenance/RecorderDeploy/20261013-195135/manifest.json`，旧版文件保存在同目录，部署与备份SHA256均已核对。World诊断日志确认0.2.8宿主加载；标题界面没有运行录制回调，未产生可用于健康签收的新录制。用户明确本轮完成验证3即可，停止继续详细游戏测试。

本轮定向检查：维护工具13/13单元检查通过（约0.11秒），新增UE `single`编译模式检查；原Python语法与文档链接检查保留。真实执行发现ExecuteStatement不接受直接多行代码，已改成单个`exec(compile(..., 'exec'))`外层语句并实测通过。本机沙箱内UDP未发现已启动编辑器，同一入口在桌面网络上下文成功连接，未来先核对执行上下文。无需开启游戏全量回归。编辑器端命令或游戏回调本线程若硬阻塞，线程内健康检查无法继续显示，仍需查看已存在的诊断心跳/最后事件。

第6项后续授权：用户要求先尝试小范围迁移，已建立Editor-only工具模块，只迁移一个只读Commandlet并保留原调用名称。工具行为输出前后25行一致；玩法/Widget/反射桥接路径和Unity设置不变，没有写入Content资产、提交Git或启动全量回归。详细构建、兼容边界与回滚步骤见模块评估文档，证据在`Saved/Maintenance/ModulePilot`。这次兼容检查发现Python字面量类加载不会自动应用ClassRedirect，已转为原生定向测试验证序列化/soft-class路径；不把字面量失败描述为兼容通过。编辑器经确认无未保存内容后正常退出，编译完成已使用本项目uproject重新打开；`live-editor-after.json`远程读取成功（1.219s）。

**第6项第二批续接（2026-10-14）：** 用户要求先继续编辑结构优化，暂停攻击蒙太奇混合研究。本批只迁移`MHGZE4AnimBlueprintAudit`和`MHGZE4SheathedMacroNaming`到既有ToolsEditor的Public/Private Commandlets；未改变工具函数实现，只改include和导出宏。新增精确类重定向及纯引擎`BlueprintGraph`/`UnrealEd`依赖，没有新增玩法模块依赖，也未改MHGZ.Build.cs/Game Target/Unity设置。

构建成功（18.76s）；原只读短入口前后均退出0，10图表审计输出83,002字节逐字节一致，ABP哈希不变；扩展的类路径测试1/1通过。Game目标JSON仍排除ToolsEditor，单审计cpp增量仅4动作/3.32s且玩法DLL哈希、时间均不变。证据、源备份和等价性检查在`Saved/Maintenance/ModuleBatch2`。宏命名工具没有AuditOnly且会写资产，未执行其Main；不声称重命名业务重新验收。此次没有修改Content、跑全量回归、PIE、提交或推送Git，也未重新打开普通编辑器。下一批仅在用户继续要求时按实际依赖筛选；运行时类型依赖工具及Widget/GA/动画父类暂不迁移。详见[模块迁移记录](../editor/maintenance-module-assessment.md)。

**第6项整体续接（2026-10-14，A阶段记录）：** 剩余40个Commandlet已集中迁移，加上前三个共43个，原Runtime/Editor目录已无Commandlet。80文件实现等价；仅新增四个既有函数的模块导出、单向ToolsEditor→MHGZ依赖及必要引擎链接。A时点运行时删除五项依赖和Bcrypt、UI/遥测稳定接口未修改；当前B状态如下。工具索引43/43，原漏列两项已补齐。

迁移和依赖清理构建成功（107.20s、98.24s，均为一次性迁移/规则成本）；唯一类路径定向测试1/1覆盖43工具及7运行时类型；只读10图表输出逐字节一致，ABP哈希及全部667 Content文件的数量/长度/时间未变。Game目标JSON排除ToolsEditor；依赖玩法接口的TelemetryExport工具单cpp增量4动作/2.33s、玩法DLL哈希/时间不变。没有执行历史写资产Main、全量玩法回归、全资产DataValidation、Game编译/打包、PIE或Git提交；证据和基线在`Saved/Maintenance/ModuleFull`。

**第6项B续接（2026-10-14，已批准并实施）：** 用户明确批准后，UI构建及Trace导出完整实现已移入ToolsEditor，MHGZ只保留原反射入口/四参数签名和Core-only Modular Features合同。Startup注册/Shutdown注销，无反向依赖、缓存或自动DLL加载；缺少服务或非Editor返回false。删除UMGEditor/TraceAnalysis/TraceServices，原非Editor定义的参数数目错误已修正。第6项约定的A+B范围完成，不要求迁移玩法父类或封存测试。

Editor构建77.76s成功；真实普通编辑器Python旧入口0.659s成功、正常退出；定向服务/路径测试4/4，五布局瞬态构建及服务撤销安全返回通过。Game只编译两薄接口（5.93s），新模块JSON无ToolsEditor；单UI实现增量3.26s/4动作且玩法DLL哈希/时间不变。作者/Trace完整算法等价、其余运行时源码边界及667个Content文件元数据不变。证据和备份在`Saved/Maintenance/EditorServices`，详见模块迁移记录；无Content写入、全量回归、PIE或Git提交。

**B检查点发现的风险已修复（2026-10-14，独立小修）：** 原Trace Stop异步关闭后立即导出会碰Windows文件锁。导出器现仅对目标文件打开做每5ms/最多2s的有界等待，读取句柄仍禁止写入，成功后才解析；不干预其他Trace，正常文件无固定延迟。超时保留raw和已有CSV，记录明确Failed状态。原接口/录制/解析成本/CSV算法不变，未处理Stop请求被拒绝等其他故障。

增量编译3.92s/5动作，仅ToolsEditor，玩法DLL哈希/时间不变；Services四项通过，真实启停后的**首次自动导出**成功（移除原错误豁免、测试补导出和额外Stop等待）。永久锁超时、缺失文件、两个已有CSV不被截断及另一Trace身份不变均通过。证据与修复前备份在`Saved/Maintenance/TraceCloseFix`；成功录制归档到`EditorServices/CaptureTests/20261014-143816-BP_IG_Character_C_0-56608`。旧B的警告/补导出仅是历史结果，新检查未跑玩法回归、全资产验证、PIE或Git提交；候选为0，非MM成本数值验收。旧精确迁移脚本不再作为修复后完整导出器的等价断言，按实际变更范围检查。
