# 2026-10-15 拔刀、收刀修复与 MM 接缝诊断

状态：三个非MM修改及随后获批的MM/ABP修复均已保存；增量编译、四项定向测试和独立磁盘重载通过，待局部PIE观察。§3～6保留首轮诊断依据，不再代表待执行任务；当前实现和续接以§7为准。未重开全量门禁，未提交/推送Git。

## 1. 本轮事实源与边界

本轮录制为 `Saved/RuntimeTelemetry/20261014-230556-BP_IG_Character_C_0-101684`，包含10029条60Hz采样、116条解析输入、10200条 MM 选择事件和原生 Pose Search 成本。采样帧与游戏帧不是同一频率，事件以 Frame 对齐，必要时取不超过3个游戏帧的最近前向采样。不能把采样行数直接当游戏帧数。

证据目录为 `Saved/Maintenance/Locomotion20261014/`：

- `telemetry.json`：输入/GA/查询/选择的关联时间线。
- `assets.json`、`probe/`、`abp-graph.json`：磁盘 GA、Combo、Montage、PSS/PSD和真实蓝图引脚连线；不是仅字符串扫描。
- `cost-examples.json`：原生候选与逐通道成本。Top-24里可能全是被过滤的低成本候选；未收录胜者时必须结合 Selection，不能把第一名误认成胜者。
- `poses.json`、`seams.json`：录制涉及序列的原始组件空间双脚/腰/根采样。双脚误差是去除根平移后的源姿势 RMS，不是 PIE 最终混合输出误差；采样精度1/60s。
- `abp-open-conformance.json`：独立进程中模拟编辑器打开时的校正步骤，内存变更全部丢弃，磁盘哈希保持不变。
- `draw-blend-applied.json`、`draw-blend-disk-verify.json`、`Tests/index.json`：本轮已执行修改与检查。

没有删除、裁切或重新生成移动/收刀序列。前一轮女性收刀轨道替换保留。本轮不改 MM 权重、候选路由、混合参数、Pose History 连线或 ABP 节点标志。

## 2. 已执行的非 MM 修复

### 2.1 前+Y 拔刀的入口显示

录制中的前+Y确实启动 `GA_IG_BaDaoZhan / AM_IG_BaDaoZhan`，没有激活飞身跃入 GA。Combo 的 `ForwardY_DrawSlash`、GA 的 Montage 引用及第一段300动画都正确。第一段完整0.45s，之后接102的源67帧入口。

问题是普通 Montage 混入默认0.25s。例如帧41439后，Montage一直在推进但权重缓慢从零升至一；拔刀姿态提交时尚未满权重，底层持刀 Idle 会透出，起手被弱化。不能把这个观感误诊为 Combo 选错 GA。

仅将 `AM_IG_BaDaoZhan.BlendIn.BlendTime` 从0.25改为0.05s。独立磁盘回读确认原生导出除 BlendIn 外完全相同，片段、通知、入口、倍率、根运动和内部0.05s元数据不变。备份为证据目录中的 `AM_IG_BaDaoZhan.before.uasset`。参数是本项目视觉修复候选，不是原作混入真值；仍需局部目视确认。

### 2.2 收刀同时召回猎虫

`AM_IG_ShouDao` 的 Idle/Walk都有 `KinsectRecallCommit`，但通知代码仅处理 `UMHGZRecallKinsectAbility`，对 `UMHGZSheatheAbility` 没有分支。因此通知存在不等于召回调用存在。

已给收刀 GA 增加精确 ActionToken 校验的幂等 `CommitKinsectRecall`，通知分发到该方法，复用 `URes_InsectGlaive::RecallKinsect` 的既有指令/返航/精华回手流程，不另起一个收虫 GA。成功启动收刀 Montage 后也调用一次，避免 Idle 第0帧通知早于实例登记时漏执行。猎虫已经在手上或缺失不会阻止收刀。

定向技术测试覆盖在外猎虫开始返航、错误 Token 拒绝、同一通知幂等及收刀继续提交。没有改虫印、粉尘或携带精华机制。

### 2.3 长按 RB 收刀后 Sprint

原 `SprintPressed` 在持刀态直接返回，丢掉物理按住状态。RB虽然另一路成功发出 `Input.Sheathe`，收刀提交后仍按着却没有新 Started，冲刺标志保持0。录制中的移动收刀后因此持续选择 Run。

已保留持刀期间 RB 按下状态；权威收刀姿态成立后恢复 Sprint 请求，不要求再次按下。持刀态仍不能 Sprint，松开/取消仍清除状态，原站立按 RB 的计时规则及拔刀清理规则保留。MM如何选Sprint脚相不属于此修复。

两项 `MHGZ.Maintenance.Sheathe` 技术测试成功。测试报告含无模型/无正式WorldContext的测试夹具警告，不是零警告的 PIE 验收。Development Editor增量编译通过，未做全量回归。

## 3. MM 的共同根因：短姿势混合不等于连续 Root Motion

磁盘两 MM 节点均为 `BlendTime=0.05s / MaxActiveBlends=1 / bUseInertialBlend=false / bStoreBlendedPose=true`，搜索节流0.12s。这些是现有资产值，不是本轮修改。

本机 UE5.6 `BlendStack/Source/Runtime/Private/AnimNode_BlendStack.cpp` 的 `Evaluate_AnyThread` 在此配置下把旧播放器存成静态姿势；`FBlendStackAnimPlayer::StorePoseContext` 清除 Sequence/BlendSpace播放器。旧姿势可以继续参与视觉混合，旧动画却不再持续推进/提取根运动，新播放器从低权重开始，实际位移会出现凹陷。

录制不是仅“看起来停顿”：

| 接缝 | 记录到的水平速度变化 | 同时条件 |
|---|---|---|
| 收刀翻滚 Exit→Run_Start，帧40065附近 | 约513→76→0cm/s，再恢复 | 满推、ForceIdle=0、没有输入稳定等待 |
| 移动收刀 Exit→Run_Loop，帧42277附近 | 约517→148→0cm/s，再恢复 | 满推、ForceIdle=0 |
| 收刀翻滚 Exit→Run_Loop，帧49865附近 | 约501→0→96→199→454cm/s | 满推、ForceIdle=0 |
| 持刀翻滚 Exit→Loop，帧44025附近 | 约383→0→167→263→434cm/s | 满推、ForceIdle=0 |

MovementPhases记录 CMC 收到 `HasAnimRootMotion=1` 但位移为零，然后由根运动速度写入零；不是碰撞弹开、摇杆回中或额外 Idle 死区。Start→Loop和Run→Sprint也有不同程度的速度凹陷。

**建议先修这条共同链路，再调脚相权重。** 定向比较保留两个活动动画播放器的混合（例如MaxActiveBlends=2）与MM惯性混合路径，检查整个0.05s接缝的根运动，不只看画面。MaxActiveBlends=2是待测候选，不能先宣称必然解决。Exit/Start必须提前给目标段进入机会，保证混合期间源段仍有有效位移；不能等源段耗尽再渐增目标权重。禁止通过伪造速度、强制 Idle、输入延迟或锁满整个 Loop 掩盖。

旧文档的“0.06s/MaxActiveBlends=1原子配置”是当时视觉/可移动签收记录，不能覆盖本次实际 Root Motion 凹陷证据，也不能作为此轮继续强制使用1的理由。

## 4. 各类接缝的原因与下一步方案

### 4.1 收刀翻滚后重新起步

帧40065、48925、49345、51569等：持续满推、查询Intent=0，`AS_Shth_Dodge_MoveExit`尾部却选回 `AS_Shth_Run_Start@0.0833s`，然后约一秒后才进Loop。不是程序重新发了+1 Start，也不是必须等Start播完的锁；是Exit尾部开放的完整Move库仍允许Start入口参与竞争。Start中后段的低成本候选被正确的BlockTransition过滤，但入口仍合法，Intent只是成本，不是禁入门槛。

双脚源姿势RMS约33.8cm，也明显比可匹配的后段姿势差。只改变Intent权重会改变出现概率，不能表达“已经在移动的功能退出不应重新起步”。

方案：持续输入的Exit尾部开放**相应挡位Loop候选语境**，不开放Start/Idle；真正松杆仍立即走既有一次性Stop流程，不强制播完Exit。保持动作功能承诺和所有权合同，不删除普通Start/Stop的BlockTransition。

当前只在最后0.08s开放Exit+完整Move，但SearchThrottle=0.12s，普通搜索时机可能错过整个窗口，只在继续播放失败/尾部结束时才重搜。这与本次多处几乎到末帧才切换相符。应让安全开放窗口覆盖搜索和混合所需时间，或在尾部开放这一次边沿安排搜索；不能每帧Force搜索。

### 4.2 Run↔Sprint 的脚相

帧53642：Run源0.3036s→Sprint125x源0.55s，周期分别0.8333/0.6667s，归一化相位约0.364→0.825，接近换到另一只脚的半周期。去根平移的双脚源姿势RMS约102.1cm。目标Sprint约0.25s的双脚RMS仅约7.9cm。另一方面，帧53872的Run0.5734→Sprint0.45s相位0.688→0.675较接近，说明不是两条动画一律不能衔接。

LoopOnly路由已正确限定目标动画，但它不限定脚相。原生帧53642成本是Pose0.176396+Trajectory0.072376，Intent/MoveGait等语义成本为0；“选对Sprint”不保证“选对Sprint帧”。这里没有必要再抬Intent或修改回中缓冲。

PSS已有双脚 Position/Velocity/Phase，不是缺少Phase勾选。但是当前Pose默认UseContinuingPose，切库会撤销Continuing；本机UE5.6的 `PoseSearchFeatureChannel_Phase::BuildQuery` 在不能复制当前结果时仍有未实现的phase查询路径，不能从角色Pose History生成可靠步态相位。不能把“Phase频道存在”当作严格同步保证，也不建议盲目切成UseCharacterPose就认为Phase已实现。

方案：沿用目标LoopOnly语境，补可跨数据库失效保存的明确循环相位查询（如共同落脚基准的cos/sin相位曲线），再匹配目标Loop。Run/Sprint同起点资源可以按周期换算；其他Start/Exit不能简单用整段归一化时间代替步态周期。脚位置/速度仍用于筛选，短混合只消除剩余姿势差，不修复半周期选错。不必改成状态机、等待整圈结束或制作新招式桥接动画。

### 4.3 持刀移动“一跳一跳”

录制中持刀Loop有21次正常Continuing跨周期，未发现同Loop非Continuing回跳重启；其他Loop也同样连续循环。`AS_UnSh_Walk_Loop`长度0.783333s、RateScale=1、loop=true，没有被每圈提前截断。

源序列自身腰部组件Z相对根位置上下变化约10.1cm，脚间首尾差5.7cm低于序列内正常单帧最大差6.9cm。因此不能仅凭首尾不完全重合判定循环坏了，也不能将所有“跳感”归因于未播完整。动作自带明显起伏，进出接缝的Root Motion凹陷会进一步加重观感。

方案：先修共同Root Motion混合与相位查询，保持自然Continuing循环。若稳定Loop中仍嫌起伏大，再由用户确认是否换源或局部平滑表现；不通过PSS权重承诺消除源序列自身腰部起伏，不默认修脚轨道或重做动画。

### 4.4 持刀翻滚、两种Start→Loop

这些接缝不全是严重反脚相。例：持刀翻滚Exit→Loop实际目标0.3833s，双脚源RMS约5.7cm，而约0.4s可到1.8cm；持刀Start→Loop例子约7.2～11.8cm；Run_Start→Loop例子约4.8cm。Source姿势指标本身不能证明PIE混合后的脚滑幅度，仍要结合速度/最终输出。

两个更明确的问题是共同Root Motion混合凹陷，以及位于动作Slot之前的Pose History：动作期间基础图被隔离到Idle，该History看不到最终Montage姿势。强制退出首次搜索因而不是从用户实际看见的姿势历史建立查询。Exit持续播放后可恢复Continuing，但尾部搜索又受节流及候选语境影响。

方案：校正History采集为实际最终动作/移动姿势，保留现有Root Motion测量隔离和GAS动作所有权；对Start/Exit的合法尾部匹配双脚位置、速度和后续循环相位，允许及时进Loop。先保留所有已验证动作时长与Stop规则，不加“必须播完整Start/Loop”的锁，也不盲目去掉承诺段BlockTransition。

## 5. ABP 重启后每次需要编译：已定位，未修复

用户确认：没有错误/警告，点击即编译成功，但编译保存重启后仍出现。独立磁盘加载最初为BS_UpToDate(3)、package dirty=false，不能据此否定用户打开窗口的现象。

模拟UE编辑器打开的校正步骤时，前六步均保持已编译。最后 `FBlueprintEditorUtils::UpdateTransactionalFlags` 把Status变为BS_Dirty(1)，**包仍不脏**。缺失 `RF_Transactional` 的12节点是：新增上半身操虫层的缓存/UseCache/Slot/LayeredBoneBlend/BoolBlend/VariableGet共10个，以及两条新Inertialization节点。完整路径在诊断JSON中。

UE此方法只补节点标志并置编译状态，不标记资产包需要保存。加上现有作者脚本用 `NewObject(...Graph)` 未设RF_Transactional、保存常用only_if_is_dirty，所以“编译成功→通常保存→重启”未必持久化节点标志修复。不是MM查询算法编译失败，也不是功能回调没有接上：生成类CDO中两节点实际使用正确原生PreUpdate/StateUpdated回调，遗留旧函数图当前未绑定，不应拿它们当运行时链路。

方案：修复相关作者工具的新建节点标志，已有ABP只补这12节点的RF_Transactional，显式MarkPackageDirty→编译→保存。独立重载再模拟打开，要求状态保持已编译、缺失列表为零、连线/参数不变。不关闭bRecompileOnLoad，不删缓存/函数图或清空DerivedDataCache代替修复。本轮模拟过程从未保存ABP，磁盘SHA256前后相同。

## 6. 下一轮实施顺序与最小检查

1. 单独修复ABP/作者工具节点标志，磁盘重载模拟打开即可检查，无需玩法全量回归。
2. 修复MM短混合的持续根运动推进，定向核对Start→Loop、Exit→Loop、Loop改挡整个接缝；不能只把速度归零帧延后。
3. 限定持续移动Exit的Loop候选，安排一次安全尾部搜索，并校正最终Pose History来源；松杆Stop保留。
4. Run↔Sprint明确相位查询，再按Pose/Velocity成本调局部权重；不恢复历史GaitChange每帧强制搜索。
5. 用户局部PIE观察三个非MM修复和这些具体接缝，已录过的无关功能不重录。持刀稳定Loop起伏只有在其余接缝修复后仍不满意时才另行处理源表现。

以上是首轮诊断时的方案，随后授权的实施状态以§7为准，不照搬历史门禁的“下一步”。

## 7. 按顺序实施的当前结果

### 7.1 已保存的行为

1. **ABP编译状态。** 补齐12节点的`RF_Transactional`，显式标记包需要保存、编译并保存。上半身操虫层和Slot惯性节点的作者工具今后创建节点时也带齐标志。独立进程重载后模拟打开编辑器，各校正步骤保持已编译/包不脏，缺失列表为零。不关闭正常编译检查。
2. **根运动混合。** 两MM节点`MaxActiveBlends=2`，普通混合仍0.05s，搜索节流仍0.12s。真实Run/Sprint序列测试证实旧配置冻结旧播放器，新配置在被测接缝各帧保持非零根运动。没有伪造速度、加输入锁或锁满Start/Loop。
3. **移动Exit候选。** 新建`PSD_MH_Shth_Walk_LoopOnly`和`PSD_MH_UnSh_Walk_LoopOnly`，Run/Sprint复用原Loop库。功能退出前段仍由既有Exit播放，最后0.08s安全尾段按当前挡位切至唯一Loop库；一次数据库变化立即搜索，不等0.12s节流，也不开放Start/Idle。持续输入保留Loop语境；松杆立即回普通Move库走原一次性Stop，新Start/动作解除该语境。只重置旧候选路由，不得将当前目标挡位一并清成None/Walk。
4. **最终姿势历史。** 把现有Pose History从动作Slot前移至最终Root输出前，保留轨迹输入、所有节点及功能图，只重接五个节点的必要连线。上半身层、两条Slot惯性混合、俯仰、GAS根运动所有权和测量隔离不变，不恢复GA后强制Idle。
5. **循环脚相。** 新建`PSS_MH_Shth_LoopPhase`，只供原Run/Sprint Loop库使用；复制原通道，追加原生`MHGZ Loop Phase`。预搜索在切库失效前取得当前真实Run/Sprint源时间与周期，生成cos/sin；候选按自身周期索引同一落脚基准。允许±0.1周期（约36°）候选，拒绝半周期错脚，再由原Pose/Trajectory成本选帧。没有兼容源Loop（Start/Exit）时相位查询为零、不过滤，不用整段Start/Exit归一化时间伪造步态。

可调位置：ABP两MM节点的BlendTime/MaxActiveBlends，以及`PSS_MH_Shth_LoopPhase`中新频道的`MaxCycleError`（当前0.1）。后者是相位范围，不是等待时间。`PSS_MH_Move`、普通Start/Stop库、权重、源动画帧率/时长/轨道/通知不变；未制作Sprint Start125x，未修改稳定持刀Loop自身腰部起伏。

### 7.2 范围、检查点与验证

- 修改现有资产：`ABP_MH_Character`、`PSD_MH_Shth_Run_LoopOnly`、`PSD_MH_Shth_Sprint_LoopOnly`；前轮非MM修改保留。
- 新资产：两份Walk Loop-only库与独立LoopPhase Schema，均位于`/Game/Blueprints/Characters/Demo/Animation/MotionMatching/`。
- 证据/备份在`Saved/Maintenance/Locomotion20261015/`。`ABP.before-flags.uasset`是本轮之前版本，`ABP.seam-graph.before.uasset`是已修标志、未改接线版本；两Loop库各有before备份。恢复前须核对后续用户改动，不盲目覆盖。
- `flags-applied.json / flags-reload-verified.json`记录事务标志；`loop-banks-applied.json`记录新增资产及两库前后哈希；`graph-applied.json / disk-verified.json`记录接线与独立重载。
- Development Editor增量编译通过。`MHGZ.Maintenance.Locomotion`最终四项通过、零失败、测试项零警告：`LiveRootMotionBlend`、`LoopPhase`、`CandidateBanks`、`MobileExitRoute`。后者直接调用真实MM预搜索回调，覆盖Walk/Run/Sprint挡位保持与松杆立即恢复Stop候选。结果在`Tests/index.json`，不等于PIE视觉签收。
- 磁盘独立重载确认：编译状态稳定/标志零缺失，节点与函数集合不变，只有五节点的指定语义连线变化；0.05s/MaxActiveBlends=2及Schema引用正确。没有全量回归、全资产验证、Git提交或打包检查。

失败均限定在工具层并记录：早期低层测试缺UE共享更新/同步消息，独立测试进程断言退出，未写正式资产；补齐夹具后通过。首次接线因BlendStack子图Result也继承Root而被前置数量检查拒绝，磁盘哈希未变；限定主AnimGraph后从同一备份续接。文本核对剔除了编译重建引脚GUID、None名称省略和嵌套子图引脚的伪差异，按真实节点/引脚名与方向核对，未放宽五节点范围。

### 7.3 首轮之后的局部PIE（未签收，后续以§8为准）

只观察：重开ABP是否仍提示编译；持刀/收刀Start→Loop、两种翻滚→移动、移动收刀→移动及Run↔Sprint互切是否仍顿住/反脚；Exit途中松杆是否及时Stop。顺带确认前轮拔刀起手、收刀收虫和持续RB接Sprint。录制有问题的接缝即可，无关招式/伤害/精华闭环不重录。稳定持刀Loop的源动画起伏未改，不宣称已消除；本轮尚未PIE或打包签收。

## 8. 两份PIE之后的连续相位修正

### 8.1 已确认的问题与本次边界

用户提交`20261015-103426-BP_IG_Character_C_0-103553`（含0.2倍速）和`20261015-103751-BP_IG_Character_C_0-103558`（正常速度）。首轮§7不是视觉签收：±0.1周期仍允许明显前后跳；少量帧的相位查询退化为无方向，跨半周期候选仍合法。快速回切还会受Loop重选历史与未结束的普通BlendStack混合影响。Exit已进入Loop而非Start，但更近的脚姿会被轨迹成本压过。完整只读证据见`Saved/Maintenance/LocomotionFollowup20261015/report.md`。

本次不改输入缓冲、挡位阈值、GA、Stop生命周期、承诺段/退出时机、普通Start/Stop的Schema及权重、动画源轨道/时长/倍率；不通过锁满Loop、延长输入等待或增加GA后Idle保持解决。

### 8.2 当前实现合同

1. **持续Run/Sprint相位。** `FMHGZLocomotionPhaseClock`在进入循环时从实际播放器或首次Loop结果初始化；随后按实际动画更新Delta和所播放序列的周期推进，切库不重置，不逐帧从搜索胜者回填。使用未量化相位累计，避免每次切换的采样误差叠加；临时无效搜索不清时钟，真实松杆/Start/动作所有权等既有边界才退出循环语境。
2. **精确候选门槛。** `MHGZ Loop Phase`先从目标库真实索引中找最近相位，再以`MaxCycleError=0.001`限定该采样，而非继续在±0.1周期内让轨迹任选。循环末/首的等价相位允许正常换圈，不是倒播。
3. **Exit→Loop一次入口。** 同一通道新增可打包的双脚根空间位置/速度数据。没有循环时钟的入口，从最终Pose History取双脚位置和速度，先确定联合误差最小的Loop入口；速度折算时间`EntryVelocityHorizon=0.02s`，只用于入口比较，不是输入延迟。普通PSS成本不能再把该入口换成反脚相；后续正常Continuing及松杆Stop不受这次入口约束。整段Exit时间没有被伪装成循环相位。
4. **重选历史局部处理。** 仅当进入/切换指定Loop-only库时移除该目标库自己的历史键；FullMove中的Stop历史、节点原`PoseReselectHistory`时长与其他数据库历史保留。UE5.6没有对应公开写入API，当前通过反射访问该节点的`MotionMatchingState`，只改`PoseIndicesHistory.IndexToTime`中的精确目标数据库键；引擎升级需定向复核此适配。
5. **快速回切惯性混合。** 两MM输出各新增一个专用Inertialization节点，与动作Slot的惯性节点分开。仅Loop-only语境运行时启用原生惯性混合，沿用原0.05s时长；其他语境恢复原节点配置。不会不断堆积旧BlendStack冻结层，也不等待上次混合播完才接受RB。普通混合仍保留`MaxActiveBlends=2`。

资产：更新`PSS_MH_Shth_LoopPhase`并供收刀Walk/Run/Sprint三Loop-only库使用；新增`PSS_MH_UnSh_LoopEntry`供持刀Walk Loop-only库使用；ABP只新增两个MM惯性节点。普通Move候选库及其原Schema磁盘哈希保持不变，所有源Sequence保持。

`Selection.csv`追加同一次动画搜索的`LoopPhaseQueryX/Y`、`LoopQuerySource`、`LoopHistoryEntriesRemoved`、`PreBlendActivePlayers`。最后一项明确是新混合创建前的播放器数，不冒充最终输出层权重；`FinalPoseFeetEntry`与`CycleClock`可区分入口匹配和时钟，缺最终姿势历史时显式标为`MissingFinalPoseHistory`。

UE5.6适配注意：`FMotionMatchingBlueprintBlendSettings`构造函数未导出，使用其UScriptStruct初始化；`GetMotionMatchingBlendSettings`实现不会填写有效性出参，不能用该出参判断成功，已改用经验证的类型化节点引用。没有修改引擎源码。

### 8.3 保存与最小验证

备份/事务意图/保存哈希及独立回读在`Saved/Maintenance/LocomotionFollowup20261015/continuity-*.json`与`*.continuity-before.uasset`。首次作者接口遗漏的失败没有保存资产，补齐接口后先核对全部前置哈希才续接，禁止重跑旧Apply覆盖后续编辑。`apply_continuity.py`默认只读回查；已存在成功检查点时拒绝再次Apply。

独立进程回读已确认Schema引用、0.001门槛、持刀通道标识、ABP事务标志与稳定编译状态；离线语义连线检查保留331个旧节点，折叠两处新增惯性节点后全部原语义连线一致。最终Development Editor增量编译成功，5项`MHGZ.Maintenance.Locomotion`测试全部通过，测试项0错误/0警告，权威报告为`TestsAccepted/index.json`；不能据技术检查签收视觉。原生搜索复现的三个Exit分别选到Sprint 0.2500s、Run 0.6500s、持刀Walk 0.3167s（含速度项，不等同于只比较脚位置的最近帧）。

新增定向测试覆盖60/120FPS及0.2倍速时钟、80个相位的真实数据库选帧、三个实际Exit源姿势的原生搜索、只移除目标Loop历史、恢复Stop混合设置、每次动画更新都反向请求时的实际惯性节点与非零根运动。早期夹具因缺MemStack作用域、直接构造惯性节点缺折叠NodeData而断言退出，未写资产；已改成正确作用域及临时实际ABP实例。最后检查也发现并修正上述引擎有效性出参陷阱，未放宽失败断言。

当前仍需用户局部PIE确认Run↔Sprint普通/快速互切、两种翻滚及移动收刀接移动、Exit途中松杆；不重录无关招式，不启动全量回归、资产验证、打包或Git操作。

## 9. 新两份录制后的 Start/Exit 与自然循环修复（当前实现）

本节覆盖§8中“所有Loop-only都惯性混合、只匹配双脚、源Loop未修改”的旧描述，§7/8保留为过程证据，不重复执行其Apply脚本。

### 9.1 依据与修复合同

正常速度`20261015-133109-BP_IG_Character_C_0-124269`与低倍速`20261015-133549-BP_IG_Character_C_0-124269`确认：普通Start→Loop未走专用入口，Run入口源双脚差约24–28cm；Start剩余约0.04s却参与0.1s普通混合，实测根速度出现约439→183cm/s凹陷；Exit虽已按最终脚姿入Loop，却被统一惯性混合立即切根运动，正常收刀翻滚出现约572→359cm/s台阶。稳定Loop均自然Continuing，没有同Loop反复重选；源首尾仍有脚姿与根速度差，自然换圈又不会触发MM的BlendTo。完整只读诊断在`Saved/Maintenance/LocomotionCaptureReview20261015/report.md`。

此次保留Stop生命周期、摇杆阈值/缓冲、GA功能窗口、Root Motion所有权、Run↔Sprint相位时钟和0.001周期门槛。不增加等待整段Start/Loop的锁，不恢复GA后Idle保持。

### 9.2 已保存的实现

1. **Start入口覆盖。** 普通移动路由在现有Start Intent归零后，若真实播放器仍为Start，则立即一次切入当前挡位的Loop-only库，按最终Pose History匹配入口。收刀/持刀共用，持续输入保留巡航语境，松杆仍立即回原Stop链路。`BlockTransition`只限制候选重新进入，不能当来源退出锁；未改其通知或Start曲线。
2. **入口根运动交叉混合。** Start/Exit入口使用两个活跃播放器，普通交叉混合最多为`min(节点BlendTime, 源剩余秒数−一次更新Delta−0.0001s)`，避免尚未混完源动画已到末尾。ABP两节点仍为用户保存的0.1s/MaxActiveBlends=2；入口必要时实际时长会短于0.1s。只有真实Run↔Sprint改挡仍启用惯性混合，不再把Exit也统一改成根运动瞬切。
3. **入口根速度。** 两份专用Loop Schema频道由14维扩到17维，新增同一Root空间的真实移动速度XYZ；双脚速度预测距离0.02s、根速度预测距离0.05s，与位置共同确定入口。它们的索引维权重仍为0，只参与一次入口选帧；持续Run/Sprint仍按原独立相位时钟，不用根速度作为输入延迟。新增持久化布局版本2，避免UE DDC因省略默认值/Transient cardinality而把旧14维缓存当成新索引。普通Move/Start/Stop Schema及权重未变。
4. **自然循环闭合。** 原位处理`AS_Shth_Run_Loop`、`AS_Shth_Sprint_Loop_125x`、`AS_Shth_Walk_Loop`、`AS_UnSh_Walk_Loop`各首尾约0.1s短窗口。局部三次修正同时闭合非Root姿势、平移/旋转速度；根起点/终点坐标不变，仅闭合边界速度。帧数、时长、RateScale、曲线、通知、内部段和整圈累计Root Motion保持；短窗口内的逐时刻位移会相应调整，不能称逐点曲线不变。没有插入冻结帧，也没有额外蒙太奇或动态动画资源。

原生搜索技术样本：Run_Start@1.1081s→Run_Loop@0.3667s，持刀Start@0.8905s→Walk_Loop@0.7333s；三个Exit分别入Sprint@0.25s、Run@0.65s、持刀Walk@0.3167s。这是测试中的真实数据库选帧，不冒充新版PIE录制。

### 9.3 资产事务、验证与当前状态

- 仅保存四条上述Loop和两份专用PSS，共6资产。ABP、两普通Move库、原PSS与三条普通Start源磁盘哈希保持不变。调用已有C++编辑器桥接；`CloseLocomotionLoop`从DataModel自身取得正确Controller，不将通用Controller绑到Sequencer模型。
- 工具为`Scripts/AssetOrganization/repair_locomotion_boundaries.py`。默认只读验证；`-MHGZEntryRepairApply`只允许首次执行。`asset-intent.json`、`AssetBackups/`、`memory-checked.json`、`asset-applied.json`与`disk-verified.json`在`Saved/Maintenance/LocomotionEntryRepair20261015/`。有意图/成功记录时禁止自动重跑或覆盖后续用户改动。
- 独立进程回载通过；当前实际求值的四Loop去根双脚首尾RMS均小于0.00002cm，根边界0.001s差分速度差均小于0.04cm/s。此为源序列技术指标，不是PIE最终画面脚滑的测量。
- 最终Development Editor增量编译成功；6项`MHGZ.Maintenance.Locomotion`定向测试全部Success、0测试错误。权威报告`TestsFinal/index.json`；其中临时复制序列的测试有4条AssetImportData条件加载压缩提示，其余5项0警告；正式资产Apply与独立回载均0错误/0警告。不是“所有日志无任何提示”。
- 除既有候选/相位/松杆Stop保护外，新增实际Start入口与源尾时长检查、四Loop局部姿势/速度/根距离不变检查，以及60/120FPS和低倍速的真实BlendStack根运动正负对照：固定0.1s复现源耗尽速度凹陷，限长入口混合通过，无根运动空洞。Run↔Sprint快速回切检查保留。
- 失败仅用于定位并已修正：沙箱拒绝构建工具进入引擎目录，改为桌面权限下直接UBT；早期新测试夹具缺代理PreUpdate触发断言，未写资产；随后检查发现误把来源BlockTransition当退出锁，已移除。没有关闭运行时检查、放宽失败断言、删除全局缓存或更改机器配置。

**仍待用户局部PIE观感确认：** 收刀Start→Loop、两种翻滚→移动、移动收刀→移动及稳定Loop换圈；顺带快速Run↔Sprint和途中松杆一次即可。技术检查不升级为视觉签收，不重开全量回归、其他GA/空中/伤害门禁，不提交/推送Git。
