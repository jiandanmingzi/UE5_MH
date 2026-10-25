# 当前签收与维护任务

更新：2026-10-25。本页是当前状态入口；档案中的旧日期“下一步/待验收”不自动恢复成任务。状态基于用户确认与已保存证据，未执行的项目保持未执行。

## 项目字段中文说明（2026-10-25）

扫描全项目原生反射字段，补齐73份头文件中681项缺失/英文说明；全部1114个UPROPERTY现有中文文档注释，含937个编辑器/蓝图可见字段、结构体成员及私有反射引用。保留已有中文说明与显式ToolTip，按实际用途补充单位、条件、身份归属和兼容字段边界；字段名、类型、默认值、分类和属性标志均未改变。逐头文件去除注释后的C++词法序列与原稿一致。

63个蓝图成员/局部变量的中文说明已保存到5份资产：`ABP_MH_Character`、训练场`BP_DoorFrame/BP_JumpPad`、触屏`UI_Thumbstick/UI_TouchSimple`。同步变量描述、骨架类及生成类ToolTip；没有重编蓝图或改字节码。变量GUID/完整类型/默认值和节点/引脚身份、连接及默认输入均受结构签名保护，其余806个Content文件SHA-256保持。

新增只读原生工具`MHGZFieldDocumentation`，补足Python无法读取蓝图变量描述的接口；`-RequireChinese`对遗漏返回失败。最终增量构建`build-final.log`成功，独立新进程严格回载`reflection-verified.json`确认原生1114/1114、蓝图63/63均为中文，蓝图实际生成属性的63个ToolTip也全部为中文，0错误/0警告。源字段清单、原生报告、备份及保护结果见`Saved/Maintenance/ChineseFieldDocs20261025/verified.json`。此前工具接口/参数类型构建失败和Python接口不可用证据保留，最终结果为准。

本次仅完成说明与直接元数据验证，未执行PIE目视、回归套件或全量资产DataValidation。按用户要求，将本轮中文字段说明、5份蓝图元数据、检查工具及规范文档保存到`main`本地提交，不推送远端；提交标识以Git日志为准。本次版本管理仅复核差异与暂存范围，沿用上述构建和独立回载结果。规范见[目录与字段说明约定](../design/directory-structure.md#项目字段说明规范)，工具入口见Scripts索引。

## 本地版本管理检查点（2026-10-25）

按用户要求，将`de16b30`以来的维护成果及当前资产调参保存到`main`本地提交，不推送远端。范围包括：独立发动耐力门槛、共享落地输入锁、觉虫击准心汇入与逐怪物六次保底、木桩放大/半球及贴合外观的受击判定、25份零引用动画归档、脚本/文档整理和录制清理记录；保留当前GA/Combat等既有调参。提交标识以Git日志为准。

复用已记录的构建及四组定向结果（4/23/5/24项）、资产整理回载与录制保留文件哈希核对；本轮仅做Git差异/冲突标记与暂存范围检查，不重跑回归或新增PIE/听感/发布签收。原始录制、清理清单、缓存及备份仍保留在忽略的本机目录，不强制加入Git。下文各次“未提交Git”是当次执行快照，本检查点现统一收录。

## 录制清理（2026-10-25）

按用户选择保留“被引用来源＋项目/崛起/世界各最近3份有效录制”，以整组录制及事件/音频旁路为单位筛选。崛起12组73文件、世界29组58文件已移入Windows回收站，共131文件/约189.68MiB；项目13组均被引用或最近录制规则保护，未删除。保留文件469份SHA-256一致，规范曲线、逐帧真值、录制脚本与控制文件保持；未清空回收站，不宣称已释放磁盘空间。完整路径、引用、哈希与恢复说明见`Saved/Maintenance/RecordingCleanup20261025/{plan.json,result.json,report.md}`。未修改游戏功能、运行回归或提交Git。

## 目录整理（2026-10-25）

旧维护流水已完整移入[历史快照](../archive/maintenance-2026-10-25/status-history.md)，本页保留近期修改和有效签收；当前音效/协同计划保留。25份零引用动画经AssetTools迁移并独立回载，运行时资产保持；World录制器与可选安装脚本已归位，细节见[整理记录](../archive/maintenance-2026-10-25/README.md)。本轮不提交Git、不启动全量验证。

最终引用/目录检查通过：25份迁移包旧路径及Redirector均为0，其余786个Content文件hash保持；91份Markdown本地文件链接无缺失，原92个历史章节完整保留。可执行脚本内容未因移动改变，备份/证据见`Saved/AssetOrganization/ProjectFiles20261025/verified.json`。

## 最新维护：觉虫击贯通逐怪物至少6次伤害（2026-10-25）

按用户选择：消耗白灯的觉虫击首次真实成功命中后，同一招式对每只怪物至少结算6次，穿出判定范围后仍补足。GA新增`AwakenedKinsectMinimumHitsPerTarget=6`；普通/无白灯分支请求为0，其他猎虫攻击默认0。Resource按SourceAttackID＋目标Actor持有已提交命中结算，真实命中与补击共用计数，跨多个部位不重复保底；正常第7次等额外接触继续按原间隔有效，不重开六次结算。

补击沿用原贯通间隔、首击AP/本次动作值及最后成功命中的部位，使用独立HitID和原GE/产粉规则；目标移动时部位接触位置同步。正常飞完、召回及回手不丢失已提交余数，也不拖延虫子运动；真实接触照常采集/交付，补击不在回手后重填已交付精华。目标死亡/部位销毁、结算失败、源死亡、Runtime结束或换武器会清理；首次失败命中不建立保底。未修改共享间隔、飞行速度/时长、通知、位移或Content资产。

增量编译通过；最终23项定向检查通过（9项无警告、14项有原夹具挂点/临时世界等警告，0失败/未运行）。新增三项覆盖穿出且回手后补足、六次成功伤害对应六团黄灯粉尘、携带UI不重新填充、多目标/多部位计数、自然第7击、无效/死亡目标与Runtime清理；原觉虫击/真实请求与准心轨迹检查保持。证据`Saved/Maintenance/AwakenedSixHits20261025/{build5.log,Tests4/index.json,tests4-engine.log}`。此前检查夹具的世界上下文/注销组件Tick及遥测计数、长帧限幅问题已修正，失败日志保留，最终报告为准。未PIE/全量回归，未提交Git。

## 木桩受击形状贴合外观（2026-10-25）

修复圆柱/半球沿用外接方盒造成的空白区域误命中：新增Mesh查询形状，使用实体简单凸形碰撞；圆柱为外切32边形柱体，半球每个三色部位为8块曲面包络凸体。同一部位仍只有一个查询组件和一个部位身份，保留约2cm起的外扩；半球实际烘焙顶点最大外扩分别3.968/3.748/3.870cm。部位解析、最近点查询、通道恢复/形状切换和销毁均支持新形状，隐藏查询网格不参与可见外表面定位。

三份配置`DA_TrainingDummy`、`DA_TrainingDummy_Large`、`DA_TrainingDummy_Hemisphere`已保存并引用`/Game/Monster/TrainingDummy/Meshes/SM_TD_Query_Cylinder`及`SM_TD_Query_Hemisphere_{White,Orange,Red}`。地图实际两目标的查询位置/三色身份已在内存重建回查；地图、BP、可见网格、材质及用户已有Combat包共10份保护资产哈希保持。仅改3份配置、新增4份查询网格，不保存地图或改变实体阻挡。

增量构建成功（`build3.log`）；最终`MHGZ.M6.HitzoneCoverage`四项全部成功，均含已有临时世界/缺预览挂点夹具警告，0失败/未运行。真实物理查询验证9408个表面/内部点覆盖、4704个外部点排除，另含三色瞄准、擦边扫掠、真实猎虫伤害/萃取、最近点、组件重建及形状切换。独立冷加载Commandlet为0错误/0警告，证据`Saved/Maintenance/TrainingTargetQueries20261025/{verified.json,TestsFinal/index.json,tests-final-engine.log,Backup/}`。未PIE、全量回归或Git提交。

此前失败保留：初次误用UE5.6不统计凸体的`GetSimpleCollisionCount`，已改为直接读`AggGeom.ConvexElems`；首轮顶部凸体过宽，修正几何后Interchange又保留旧UCX，已显式清除本轮新查询资产的旧碰撞并以传统FBX重导入。原生顶点/物理查询已确认最终资产正确，旧AABB包含检查不能证明贴合度。旧10-24配置哈希及方盒覆盖结论由本次授权迁移替代；历史Apply/Resume/Refine/ReplacePreservedCollision均禁止重放，工具默认只读回查。

## 共享落地表现禁止攻击提前抢占（2026-10-25）

原因是Host已有`Combat.State.Aerial.Landing`持有期，但`HasPlayerActionInputLock`仅检查Sheathing/Dodging，导致Grounded恢复后攻击可立即抢占落地Montage。现将Landing接入同一通用锁，直接GA/Combo输入和预输入消费均遵循该锁；保持原蒙太奇结束时解锁，不改动画、通知、移动/混合时长或资产。基础设施/强制反应的已有显式豁免保留；AbilityOwned的急袭/强化操虫穿刺地面攻击段正常派生窗口保持。

增量编译及5项定向检查通过：扩展原ActionLocks检查，使用真实Host与实际`AM_IG_AerialLanding`验证起始/中段输入不能启动新Action或替换Montage实例、自然结束释放锁且新攻击立即可用；另核对两种AbilityOwned落地翻滚接管、Host落地声音实例身份及觉虫击落地链。5项均带既有临时世界/预览挂点夹具警告，0失败/未运行。证据`Saved/Maintenance/LandingAttackLock20261025/{build2.log,Tests/index.json,tests-engine.log}`。本轮仅改3份源码及相关说明，未保存Content；用户现有Combat/地图/木桩和上轮耐力/觉虫击轨迹改动保持。未PIE或全量回归。

## 独立发动耐力门槛与觉虫击准心汇入（2026-10-24）

GA新增`Ability → Cost → 发动所需最低耐力`（`StaminaActivationRequirement`）：`-1`保留现有成本门槛，`0`无门槛，正值独立检查且不乘耗耐倍率。实际扣耐不变，不足时扣到0；Combo门槛仍独立生效。急袭突刺原生默认设0、成本仍20，其他动作保持原值/原行为。

觉虫击的猎虫请求改为`MergeToAimRay`：从真实棍尾连续出发，前段平滑汇入松键相机射线，后段及贯通后保持同线；近目标缩短汇入段。GA新增`AwakenedKinsectAimRayMergeDistance=250cm`，保留yaw/pitch限制、固定速度和通知间飞行时长。独立弧长推进且关闭该阶段ProjectileMovement Tick，逐20cm以内扫掠；原伤害、萃取、多色携带、产粉、命中停留及召回仍由原链路处理。普通操虫/猎人位移/相机/通知及Content资产未修改；此前木桩未提交改动保持。

增量编译通过，24项定向检查通过（15项带既有临时世界/缺预览挂点夹具警告，0失败/未运行），含新增独立成本、汇入几何与真实Resource飞行生命周期检查。证据`Saved/Maintenance/StaminaAimRay20261024/Tests/index.json`与`tests-engine.log`，构建日志`Saved/Logs/StaminaAimRay20261024-build2.log`。此次公共头变化暴露的10个旧测试/工具首头文件顺序仅作机械调整，未关闭UE检查。未PIE/全量回归，视觉待局部PIE；调参说明见[资源设计](../design/insect-glaive.md#当前配置归属2026-10-24覆盖下文历史combat招式参数说明)。

## 训练木桩放大与半球目标（2026-10-24）

`L_DemoArena`中的`Arena_TrainingDummy`模型、实体碰撞和三色受击区域同步放大至原先2倍（直径3m、高6.75m），配置独立为`/Game/Monster/TrainingDummy/Data/DA_TrainingDummy_Large`。新增`Arena_TrainingDummy_Hemisphere_R400`，底面半径4m、高4m，位于原木桩旁14m；沿高度保留白/橙/红三个精华部位，沿用训练木桩无限血量及现有伤害/虫印逻辑。

半球配置为`/Game/Monster/TrainingDummy/Data/DA_TrainingDummy_Hemisphere`，模型在`/Game/Monster/TrainingDummy/Meshes/SM_TD_Hemisphere_{White,Orange,Red}`。实体阻挡使用三段曲面网格的复杂碰撞，已显式开启面片碰撞并保留不可站立设置；新目标不自动移动，独立关闭火圈。原BP、原配置、圆柱网格及三份材质保持字节不变。

独立冷加载已核对实际尺寸、落地位置、两目标共6个受击区域覆盖及精华身份、半球3个高度的实体射线交点；Commandlet 0错误/0警告。证据`Saved/Maintenance/TrainingTargets20261024/verified.json`；仅保存地图和5份新资产，未编译C++、未做PIE。维护工具见`Scripts/AssetProbe/configure_demo_training_targets.py`，历史Apply/Resume/Repair均不重放；默认只读回查。原地图及新网格碰撞修正前备份在同目录`Backup/`。

## 普通攻击不阻断自由猎虫自动寻印（2026-10-24）

原因是自动寻印资格在实际猎虫控制权之外，额外要求`Host->HasRegisteredAction()==false`，导致普通攻击既能阻止自由态寻印启动，也会取消已经开始的自动飞行。现去掉该全局拦截；保留运行期/虫印/距离/耐力、自由态资格、玩家租约、招式附着和命令所有者检查。`AutoDeferred`诊断同样依据实际猎虫占用，普通猎人Action不再被误报为高优先级猎虫命令；普通协同的离手准入不放宽。

新增`MHGZ.Maintenance.Kinsect.AutoMarkDuringHunterAttack`：使用真实突刺类注册Action并持续保持Attacking标签，分别覆盖攻击期间启动寻印、已有寻印被普通攻击接入；验证原自动飞行身份不变、实际接触伤害/萃取、按原间隔重复攻击、真正玩家租约仍抢占、租约结束后在猎人仍攻击时恢复，以及主动召回/回臂不自动离手。此检查验证实际控制权与伤害链，不声称完整播放了猎人蒙太奇。

`Saved/Maintenance/AutoMarkDuringAttack20261024/build.log`增量编译通过；`Tests/index.json`7/7 Success（均带既有临时世界/挂点夹具警告，0失败/未运行），另含原自动寻印、近地朝向、玩家租约/旧回调、回手与萃取、协同准入/取消检查。仅修改2个源码文件及当前规则/状态文档，未保存Content、执行PIE/全量回归，未提交或推送Git。无需专门补录制；需要目视时可直接短PIE检查“放虫→上印→连续普通攻击”中的寻印持续性。

## 上轮维护：Combat兼容字段清理（2026-10-24）

按用户要求删除66个字段：35个已迁移GA的Combat重复字段，加31个旧/未启用参数，包含猎虫滑翔、旧降龙、旧聚爆、旧虫印斑块、旧操虫斩探测、旧觉虫击时刻/距离与旧舞踏通用倍率等。删除旧Combat构造/查询函数及对应无效校验/说明；有效的操虫斩GA曲线回退、虫印弹、共享精华/猎虫/粉尘/空中规则保留。觉虫击激活预检中两处先前仍通过`GetCombatConfig()`读旧角度的遗漏已改读GA自身值。

低层粉尘生成改为显式AP/MV，低层预留显式传范围/数量，低层提交显式传半径且只累计单团贡献，移除旧固定基础伤害/硬直；正式聚爆事务与伤害路径保持。测试输入改由每个Harness持有真实GA原型对象并用强引用控制生命周期，不再向生产Combat添加夹具字段；原生历史编辑器读取方同步指向常驻GA，旧斑块读写移除，不执行历史生成Apply。

只保存`DA_IG_Combat`一个Content包，保存前已备份，现用共享字段T3D逐项文本一致；37份保护资产（含现有GA、虫印弹及关联曲线/蒙太奇等）hash不变。独立回载`verified.json`确认66项删除和共享值保持；10 GA/38字段的历史迁移回查也成功，唯一允许的旧保护hash变化是本次已验证的Combat重存，未宽放其它变更。较早聚爆迁移回查因档位已由旧快照10/50/100改成当前20/120/300而拒绝；本次起始保护hash及最终37包比对证明这些现有GA值未被本轮改变，保留并不回退，也不将该旧快照回查报为成功。

`Saved/Maintenance/CombatCleanup20261024/`保留源码修改前副本、Combat包备份、`source.json`、`applied.json`、`verified.json`、构建及检查日志。`build-final.log`编译通过，`Tests/index.json`20/20 Success（5项无警告、15项带既有临时世界/挂点夹具警告，0失败/未运行）；覆盖粉尘预留/部分失效/取消/重复提交、自然爆炸/容量/超距、聚爆全流程、GA归属/中文说明和相关招式生命周期。首次编译发现的旧角度读取、历史工具命名空间及撑杆跳查询已修正；失败日志保留。未执行PIE、全量回归/资产验证、提交或推送Git。历史迁移工具依赖已有Saved事务，只作回查，不在新设备上重建已删除Schema。

## 上轮维护：虫印弹以外的单招参数迁入GA（2026-10-24）

用户在聚爆整理后进一步授权迁移其余单招专属配置，明确排除虫印弹。已保存10份GA：空回、四方向撑杆跳、操虫斩、强化操虫穿刺、急袭突刺、觉虫击及拔刀操虫；38项顶层字段赋值（含结构/数组）按迁移前反射精确值写入，不以文本舍入值或原生默认值覆盖。专属参数校验随GA迁入，并合并已有GA校验；撑杆跳默认表仍来自原生成数据。舞踏执行快照读取本GA，原Blueprint查询改读已授予GA默认配置；旧Combat字段隐藏，仅保留加载兼容。共享精华/资源/下落/落地/虫印/粉尘、共用猎虫挂点、共用贯通间隔及普通操虫动作值保持。未迁移虫印弹、未启用滑翔或旧降龙。

保存前冻结/备份，迁移后独立进程回载成功，GA其它CDO字段及28份保护包（Combat、虫印弹、聚爆和关联蒙太奇/引用）保持。精确数值、资产哈希及备份在`Saved/Maintenance/IGActionConfig20261024/ActionDefaults/{source.json,Backup,applied.json,verified.json}`；只读续接沿用`migrate_powder_vortex_config.py -MHGZActionConfig`，禁止重放Apply。相关伤害单位、操虫斩曲线及急袭音频只读读取方已切换到GA；历史Apply工具仍按历史工具对待，不重放旧Combat写入。

`build-final.log`最终增量编译通过；本次9项定向检查最终均Success。`Tests/index.json`初次8成功/1失败，唯一失败为急袭夹具在授予GA前查询资源倍率作为预期（现该查询正确返回1）；改成明确的一层1.1期望后，`TestsFinal/index.json`复查急袭完整生命周期及舞踏快照两项全Success。新增检查覆盖10份实际GA/38值的原生Identical、旧字段隐藏/中文说明/校验、共享规则保留和GA值隔离；另覆盖空回请求、穿刺入口、觉虫击资源分支、拔刀操虫请求。保留既有临时世界/挂点夹具警告；首次编译的重复校验入口已合并，快照阶段Python模块路径/布尔反射名已修正，失败证据保留。5个涉及的Python文件AST检查通过，未运行PIE/全量回归/全资产验证，未提交或推送Git。调参路径见[当前配置归属](../design/insect-glaive.md#当前配置归属2026-10-24覆盖下文历史combat招式参数说明)。

## 上轮维护：聚爆动作值/精度、GA归属与Combat中文整理（2026-10-24）

本轮按用户确认只迁移聚爆，其余招式参数仅分类/中文说明。粉尘生成的旧float贡献值及后续float求和会提前丢失精度，末端double无法恢复；现抽取共用`MHGZDamageMath`，单团用double的AP×MV/100及原取整/最小伤害规则计算，聚爆第二段由每团生成时AP/MV重算其单独爆炸伤害再以double求和。真正小数仍向下，不用字符串算术。第一段的实际GE保留猎虫AP快照与GA数量档MV，按AP×MV/100结算，不再把数量档当固定伤害；零团无伤害。

已仅保存`GA_IG_PowderVortex`：`类默认值 → Powder Vortex`包含`MotionValueAt1/At5/At10`（1–4/5–9/≥10团，当前10/50/100）、吸引半径3000cm、延迟1s、两段爆炸半径300cm、两个Cue、聚集声数组及附加爆炸声。十项值/引用从保存前快照迁入，GA其它CDO内容保持；Combat包字节不变，保留用户单团MV30和普通产粉CD5s。共享粉尘生命周期/动作值/基础爆炸声仍在Combat。Combat及所用项目嵌套结构补中文悬停说明并分类，聚爆旧字段与失效兼容项隐藏；未迁移其它GA。

**CD只核对未修改：** 时间戳在Resource，不在虫印；换印不重置。普通操虫/招式/地面协同受共享CD，自动寻印和觉虫击黄灯保留独立例外；仅凭建印时看到粉尘不能认定来自同一条协同判定。新增检查证明换印不会清除普通CD。

最终构建`build-tooltip-complete.log`通过；`TestsFinal/index.json`为15/15 Success（6项无警告、9项带既有夹具警告，0失败/未运行，启动Condition failed诊断保留）。包括数量档、AP100/200、MV30的1/10/100团、逐团真正小数取整、自然爆炸/取消/清印/音频生命周期、原武器公式和编译后中文元数据。单GA迁移及新进程回载`asset-apply-frozen-tags.log`、`asset-verify.log`通过；`verified.json`核对十项配置、GA其它CDO及Combat hash。来源/两包备份/日志在`Saved/Maintenance/PowderVortexConfig20261024/`。首次测试编译的辅助函数私有访问/类型推导错误已修正；首次NullRHI队列遭遇旧布局断言，改用引擎原生Automation Now调度；随后一项直接到达夹具遗漏第二次配置提交，已修正夹具且保留断言。首次Apply在保存前因旧字段已隐藏而停止，核实GA hash未变后改为从冻结的标签原文导入；失败日志均保留。

`Scripts/AssetOrganization/migrate_powder_vortex_config.py`默认只读回查，已有事务禁止重放Apply；音频回查及伤害单位审计已调整读取新归属。未运行PIE、全量回归/全资产验证或提交/推送Git。当前入口见[资源设计](../design/insect-glaive.md#虫印指令仲裁与虫粉的资源所有权m63新合同)。

## 同轮维护：自动寻印攻击采集精华（2026-10-24）

原因是自动寻印接近请求刻意关闭萃取，但独立的`PerformAutomaticMarkContactAttack`此前只结算真实伤害和产粉，未补采集。现仅在真实接触伤害成功、原Runtime/命令/虫印/猎虫飞行实例仍有效时，复用普通单色携带入口；不因接近或未命中采集，不直接给角色点灯，不覆盖已有携带精华。采集后立即发布资源UI，回臂仍原子清空携带并交付。GE/UI回调中召回、换命令或换印后旧接触不再采集/产粉，也不覆盖新会话的自动攻击计时。

增量编译通过，7项直接相关检查全Success，证据`Saved/Maintenance/AutoMarkExtract20261024/{build.log,tests.log,Tests/index.json}`。新增检查覆盖空载自动采集、已有白色不被红色覆盖、重复命中不重复携带、UI先显示携带/回手再点灯、GE拒绝和GE回调召回；兼容原自动寻印、近地正面接触、玩家抢占、回手不自动离臂、携带交付与独立产粉。保留既有夹具警告及启动Condition failed；未修改Content、运行PIE/全量回归或提交/推送Git。

## 同轮维护：精华降低猎虫耗耐、部位虫印Debuff（2026-10-24）

红灯（含红灯的非三灯组合）使猎虫飞行/协同/操虫斩离臂及悬停耗耐变为无灯的0.75；有效三灯为0.5，覆盖而非叠乘红灯，按当前有效GE实时计算，消耗/到期即切换。臂上回耐、出招耐力门槛与耗尽召回保持。可在`DA_IG_Combat → Extract|Multipliers`调整`RedKinsectStaminaDrainMultiplier / TripleKinsectStaminaDrainMultiplier`。

虫印现登记在实际Hitzone部位为来源/实例/到期时间隔离的Debuff，每人唯一90s与原生命周期不变。成功猎虫伤害命中该部位任意位置可评估产粉，不再依赖特效附近100×100cm斑块/法向厚度；同怪不同部位不共享，Box查询子组件解析回其部位。特效锚点和自动寻印位置保留，替换/死亡/到期/聚爆消费/Runtime关闭同步清理状态；普通CD与自动寻印、觉虫击黄灯的独立产粉例外不变。旧斑块配置仅保留加载兼容，不再有效。

生产增量编译`build.log`、测试增量编译`build-tests.log`通过；12项耗耐/部位Debuff/真实产粉/自动寻印/协同检查和2项聚爆消耗虫印及双段伤害检查全部Success，报告`Saved/Maintenance/KinsectMarkDebuff20261024/{Tests,TestsVortex}/index.json`。保留既有临时世界/挂点夹具警告及启动Condition failed诊断；未保存Content、运行PIE/全量回归或提交/推送Git。当前设计与聚爆伤害配置入口见[insect-glaive.md](../design/insect-glaive.md#虫印指令仲裁与虫粉的资源所有权m63新合同)。

## 同轮维护：猎虫/粉尘伤害整数边界精度（2026-10-24）

最新PIE日志已证实攻击力100、动作值10/20、肉质100时，原float乘法结果略低于整数，随后Floor导致实际9/19，并非隐藏伤害倍率。现仅将猎虫/粉尘公式到取整前改为double，并校正双精度舍入误差级别的整数边界；100与10/20正确得到10/20，9.99和单精度下一档仍向下取整。保留动作值/肉质的0–100与`/100`、最小伤害及原武器/木桩入站算法；移除协同测试101攻击力绕行，恢复100及原真实扣血断言。

增量编译通过；`Saved/Maintenance/KinsectDamageRounding20261024/TestsFinal/index.json`中20项通过，粉尘自然爆炸一项因渲染夹具HeadMesh与Leader骨架不匹配的ensure被标失败，伤害断言未失败；该项随后在`TestsPowderHeadless/index.json`单独无渲染复核通过。新增检查覆盖350组整数参数、真实小数/零值及无武器倍率泄漏；兼容检查覆盖原协同真实GE/快照/收尾、普通操虫/交付和武器公式。首次NullRHI长队列还遇到UE保存窗口布局的`GetRestoredDimensions`断言，失败日志保留，未修改引擎/机器配置或放宽断言。未保存Content、运行PIE/全量回归或提交/推送Git，详见[计划10.17](猎虫协同攻击实现计划.md#1017-猎虫粉尘伤害整数边界精度2026-10-24)。

## 同轮维护：急袭突刺特殊猎虫协同（2026-10-23，代码/定向检查完成，待局部PIE）

**最新规则：** 所有普通协同及急袭特殊协同都要求起手猎虫耐力≥上限10%，不是额外扣耐、不阻止猎人GA；每招锁定资格，低耐起手后回耐不补入，已获准者低于10%仍执行到正常结束/耗尽。急袭仍仅臂上可参与，留在激活位置正常耗耐，地面末窗（段3）结束后飞往收招最终胶囊中心水平前方1m固定点，贯通/萃取/交付不变，不追踪。

已修复“走开再出招仍拉回协同”：Actor原已Hovering，残留的是Resource特殊会话和注册监听。现在源Combo/正常结束且无合法后继时，完成俯冲后释放特殊命令/会话/委托，保持普通悬停与携带精华；只保留原窗口内提前续招及同帧Superseded事务交接，不让后来无关攻击加入。后继已过恢复/关闭/结束也会清理；手动/特殊命令、耗尽等优先级不变。

最终编译`build-admission-hover-final.log`与30项定向检查`TestsAdmissionHoverFinal/index.json`通过（2项无警告、28项带263条既有夹具警告，0失败/测试内错误；启动另有2条Condition failed）。包含10%边界/真实最大值/锁存、前方1m、悬停清理、早续招/晚出招、贯通携带及原普通协同兼容；下面34项/7项为旧版证据，不是本次重跑。未保存资产或Git提交/推送，最新仍待局部PIE，见[计划10.16](猎虫协同攻击实现计划.md#1016-前方1m统一10入场门槛与普通悬停清理2026-10-23)。

**此前终点修复（30cm已被上方1m取代）：** 原XYZ轴未错，但381在最后窗结束后的0.2s仍前移约31.56cm，到完整收招还有约43.83cm，原固定“当前前方30cm”因此落到后来的胶囊附近。GA按实际地面Montage位置提取剩余Root Motion，复用Mesh→World转换及平移倍率计算最终胶囊位置/朝向，地面不额外加Z；Resource只保留一次世界目标。预测以完整无遮挡Over为准，不预测后续撞墙/取消/新动作。

本次构建`build-descending-final-endpoint.log`及7项直接相关检查`TestsFinalEndpoint/index.json`通过（44条既有夹具警告，0失败/测试内错误；启动另有2条Condition failed）。包括实际381四朝向×两缩放、最终前方30cm、固定目标、原贯通与接管、真实GA触发/结束；下方34项是上一版证据，不是本次重跑。源轨道/模型只读数据见`E1DescendingThrust/verified.json`及`descending-endpoint-{audit,root-audit}.log`，未保存资产；最新效果仍待局部PIE。细节见[计划10.15](猎虫协同攻击实现计划.md#1015-固定俯冲终点改为收招最终位置2026-10-23)。

最终增量构建`build-descending-special-final.log`通过，`TestsDescendingFinal/index.json`为34/34 Success（2项无警告，32项带272条既有夹具警告，0失败/测试内错误；启动另有2条Condition failed）。包含生产GA实际末窗触发、自然结束继续俯冲/悬停、取消清理，真实贯通GE/多色携带/回手交付、GE回调抢占、延后续招与原普通协同兼容。36个已有保护Content包hash保持。证据根目录`Saved/Maintenance/KinsectCoAttack20261023/`；完整合同/结果见[计划10.14](猎虫协同攻击实现计划.md#1014-急袭突刺特殊协同2026-10-23代码及定向检查完成待局部pie)。首次编译的局部变量作用域错误已修正，失败日志保留，不冒充一次全过。

实际`GA_IG_JiXiTuCi`只读回载确认继承新动作值10，原四武器段和地面Over的段2/3窗口有效；`E1DescendingThrust/verified.json`与`descending-asset-audit.log`为0错误0警告。工具`setup_kinsect_coattack_pilot.py --descending-thrust`（Commandlet `-MHGZCoAttackDescendingThrust`）只读，无Apply；不需改GA资产或增加通知。未保存Content、全量回归/全资产验证、Git提交/推送或PIE；下一步仅局部看留位/俯冲、外出不参与、提前续招与无续招悬停，不要求新全套录制。

## 同轮背景：普通地面猎虫协同（共11招已配置；第二批五招已保存/独立回载）

已按[独立实现计划](猎虫协同攻击实现计划.md)完成M2：复用现有Attack Collision，实际成功打开时分配发生编号，Notify Tick/End匹配精确Mesh/Montage/通知及保留的引擎上下文，不在共享Notify对象保存状态。新打开/退役钩子仍是通用攻击扩展；虫棍层按`CoAttack.SegmentMotionValues`显式选择ContactOnly源段，窗口、位置共用0.2游戏秒延迟。Resource持有历史折线、独立目标次数/间隔和提交时攻击力/动作值快照，查询命中当时肉质；独立HitID、GE拒绝回滚次数，不复制武器命中/自身GE/卡肉/虫印/舞踏或萃取。命中已有局部虫印沿用普通MarkedContact与共享CD，普通猎虫仍从原入口读取实时攻击力。

仅地面与离臂正常耗耐保持M1规则；未入位则整次延迟机会跳过，不晚到补击。长帧保留尚未消费的历史折线及精确开闭边界，判定包住真实猎虫模型包围球及其根节点偏移，保持正面接触过滤。手动指令或GE回调中抢占后，旧查询不能伤害或召回新飞行。没有改原武器范围/动作值、Montage片段/通知载荷或任何Content、MM、输入。

最终增量编译通过；13项直接相关检查全部Success（M2新增7项、M1的3项、既有普通猎虫/操虫斩3项；2项无警告、11项带169条既有夹具警告，0失败/测试内错误，启动仍有2条既有Condition failed诊断）。证据：`Saved/Maintenance/KinsectCoAttack20261023/build-m2-coverage.log`、`TestsM2Coverage/index.json`。首次伤害夹具裸ACharacter无ASC导致实际扣血为0；改用绑定PlayerState的项目角色后，再通过现有Damage日志定位到原公式整数附近Floor少1的浮点边界。最终测试改用101点攻击力避开边界，严格保留实际扣血、命中肉质和快照断言，未改生产公式；失败报告均保留，详见计划10.4。

M3现已完成：正常结束及Superseded封口已提交回执，由Resource持有组件回放剩余0.2游戏秒，不延长旧GA或保留Attacking/输入锁；来源弱引用失效后仍可结算合法尾段。成功注册的新动作才裁定接管：同Mesh/Socket/偏移的协同GA共用命令号/单条轨迹，不中途回臂；每条回执按源Action与发生编号识别，不混用两招的“窗口1”。非协同动作或不兼容挂点撤销队列，失败Commit不动原协同。手动指令、特殊附着、耗尽、死亡、离地、组件失效、换武器/Runtime关闭均终止；旧End/Notify/GE回调不能覆盖新主人，自动寻印不能抢合法尾段。封口只延用最后已完成的PostPose样本到结束时刻，不在EndAbility读取未完成的新姿势；尾段期间不采后续Idle/新动作。

M3最终增量编译通过，24项直接相关检查全部Success（新增M3七项，原M1/M2十项、猎虫/操虫斩三项，以及动作注册/连招事务/换武器/输入身份四项）；3项无警告、21项带249条临时世界/挂点等夹具警告，0失败/测试内错误，启动仍有2条既有Condition failed。真实Combo确认顺序、实际扣血、弱引用失效、GE回调内续招和完整清理均有定向证据。证据：`Saved/Maintenance/KinsectCoAttack20261023/build-m3-reviewed.log`、`TestsM3Reviewed/index.json`；首轮19项通过的`TestsM3/`保留，详见计划10.5–10.6。

E1已按用户确认仅启用`/Game/Blueprints/Ability/InsectGlaive/GA_IG_TuCi`：实际有两个ContactOnly武器段，映射`0→10、1→10`猎虫动作值；跟随WeaponTrace的IG_FrontTip，零偏移、延迟0.2游戏秒，所有灯态/品种暂不额外限制，不萃取，按原2500cm/s回虫速度无判定入位，赶不上则跳过整次机会。原武器动作值13/12、现有两个攻击窗/声音/Section/入口保持；没有编译C++或扩开其他GA。

只保存突刺GA一包，先备份后应用；新UE进程独立回载通过（最终0错误/0警告），31个其他GA/Combat/Combo/Runtime/Montage/武器模型/猎虫Data保护包哈希不变，原伤害段逐项不变。证据`Saved/Maintenance/KinsectCoAttack20261023/E1TuCi/{intent,applied,verified}.json`，日志`e1-apply.log`及`e1-verify-final.log`在上级目录。首次只读审计误将突刺假设为单段而中止，未写Content；实际核清双段后才应用。维护入口`Scripts/AssetOrganization/setup_kinsect_coattack_pilot.py`默认只读，已有intent禁止重放Apply。备份可恢复，不自动回滚当前资产。

E1后用户已进行PIE并反馈，本轮按新要求更新运行时：最后一个实际可达攻击窗结束后，在PostPose取得该帧并保持，不再跟随后摇棍尖；按当前Montage实际Section链接排除跳过的段/入口，循环不误判为最终退出。有连招窗时，最后有效连招窗关闭立即清理协同并回手；无后续连招窗时，回放完0.2秒延迟的最后攻击帧即回手，不再等待GA结束。取消/特殊指令立即撤销，正常GA结束保留原兜底；不延长GA或输入锁。

产粉调查读取最新`20261023-173519-BP_IG_Character_C_0-57304/Combat/Events.csv`：14次协同产粉请求均为`NoActiveMark`，无有效虫印时不应强制产粉，不能据此移除虫印/CD门禁。新真实伤害定向检查已验证：有效虫印内命中产粉、共享CD内不产粉、冷却后再次产粉；保留普通MarkedContact逻辑及配置CD，不偷换成自动寻印的独立CD策略。该录制没有证明“有效虫印内命中却不产粉”，实际带有效虫印的PIE效果仍待用户确认。

猎虫地形规则已通用修改：Kinsect预设与飞行根组件忽略物理响应，ProjectileMovement关闭地形Sweep；普通/协同/自动寻印/特殊猎虫均不因地形停机，旧世界接触回调不结束飞行。对怪物的独立Hitzone Sweep、正面过滤和伤害保留。操虫准心仍以Visibility射线取得首个地形点；地形不成为怪物/精华目标，不变准心样式、不点灯、不算命中，ToPoint到该点后正常悬停。聚爆也改用真实目标点抵达，不再把墙/地板接触冒充抵达或取消。

最终构建`build-recovery-reviewed.log`通过；`TestsRecoveryReviewed/index.json`为34/34 Success（4项无警告，30项带292条既有临时世界/挂点等夹具警告，0失败/测试内错误；启动仍有2条既有Condition failed）。三项新增检查覆盖末帧保持/两种回手、有效虫印产粉/CD、穿地形后重新操虫；真实Combo关闭回手和聚爆检查同步核对新规则，旧34项相关入口均按筛选执行，未做全量回归。首轮三项通过报告`TestsRecovery/`保留；证据均在`Saved/Maintenance/KinsectCoAttack20261023/`。本轮未再保存Content或改突刺蒙太奇/通知/伤害值。

用户现已明确“已验收，开启下一步”，据此签收突刺试点E2，包含本轮收尾/回手、有效虫印产粉/CD及穿地形规则。签收依据为用户确认，不宣称代理另行运行PIE或读取了新录制；既有构建/34项检查及E1保存回载证据继续有效。本次只读复核既有报告0失败/0未运行，突刺包仍与E1保存哈希一致，完成M4交付状态对齐，不重复跑测试/PIE。

用户随后确认五招扩展，现已仅保存`GA_IG_ShangLao/JiaSha/ErLian/HengSao/FeiYuan`的`CoAttack`默认值：上捞段0/1/2/3、袈裟0/1、二连0/1/2、横扫0/1、飞圆0/1，各猎虫动作值10。上捞段3为两个替代入口分别使用的衔接段，不把全资产五个窗口当作一次出招五击；飞圆各入口均按原Section跳到Core，只有实际窗口触发协同。其余沿用突刺已签收规则；未更改原武器伤害、蒙太奇/通知、输入或公共配置。

五包均先备份后保存，独立UE进程回载成功，保存/回载日志各0错误0警告；31个保护包（含突刺、其他GA、五份Montage、共享配置/Combo/Runtime/武器/猎虫Data）哈希保持，原CDO其他属性及显式AttackSegments逐项不变。证据`Saved/Maintenance/KinsectCoAttack20261023/E1GroundFive/{audit,intent,applied,verified}.json`与`Backup/`，日志`ground-five-apply-stable.log`、`ground-five-verify.log`。首次Apply在备份/写入前因Vector包装字符串含进程内存地址误报而中止；已改用稳定原生数值导出，原伤害/通知保护不放宽，失败日志保留。旧突刺审计通过后继备份→原hash→新hash链识别合法五包变更，不改旧事务证据。

最新用户要求将停止采样边界统一改为Dodge Accept窗口（替代刚完成的Combo开启方案）。`BeginDodgeAcceptWindow`成功登记后、消费预输入之前，广播精确Action事件并保留最后已完成PostPose（通常为前一帧），不在通知内读棍尖；无Combo派生也生效。继续回放已有0.2游戏秒历史后保持姿势，重复/重叠开启及同招晚攻击窗不恢复采样，成功续招重置冻结。Combo开启已不触发冻结，Dodge Accept结束不触发回手；仍按原Combo结束/动作正常结束兜底及取消规则处理。六个已启用GA均有现有Dodge Accept通知；缺失该通知的旧资产保留原末次攻击窗兜底，不新增通知。本段覆盖上方所有旧冻结点说明。

最终增量编译`build-dodge-accept-boundary.log`通过；`TestsDodgeAcceptBoundary/index.json`为12/12 Success（103条既有临时世界/挂点等夹具警告，0失败/未运行/测试错误，启动另有2条既有Condition failed）。仅检查新Dodge Accept冻结、原收尾、协同M3生命周期/真实预输入、Combo事务及两项直接相关预输入兼容；日志`tests-dodge-accept-boundary.log`。前一版Combo方案的10项报告保留为历史，不当作最新证据。本次不改Content、蒙太奇/通知、段动作值或回手窗口。

随后用户明确新增突进回旋斩、四连印斩、拔刀飞身跃入斩、飞身跃入斩、后撤斩。现已仅保存对应`GA_IG_TuJinHuiXuanZhan`段0/1/2、`GA_IG_SiLianYinZhan`段0/1/2/3、`GA_IG_BaDaoZhan`段0、`GA_IG_FeiShenYueRu`段0、`GA_IG_HouChe`段0的CoAttack启用与每段猎虫动作值10。四连段0的两个窗口分别位于横扫/二连替代入口，仍按实际播放的发生实例独立判定；拔刀GA核对使用`AS_Unsh_BaDaoFeiShenYueRu`衔接`AS_Unsh_FeiShenYueRu`，不是普通拔刀或操虫拔刀。现累计11个GA启用，其余未点名GA保持。

第二批五包均先备份后保存、独立UE进程回载通过，写入和新批回载各0错误0警告；31个保护包及原六招不变，原武器伤害、虫印副作用、Section/通知/入口、突进蓄力和共享配置保持。证据`Saved/Maintenance/KinsectCoAttack20261023/E1GroundSecondFive/{audit,intent,applied,verified}.json`及`Backup/`；日志`ground-second-five-{audit,apply,verify}.log`。旧五招/突刺只读入口均通过显式后继备份hash链核对，未改旧intent/applied；旧五招兼容0错误0警告，突刺兼容仅有2条Zen缓存服务锁文件启动警告、0错误，资产核对成功，不据此修改机器缓存/配置。此次纯资产配置未编译C++或重跑12项检查；它们是此前Dodge Accept代码证据，不冒充本轮新运行。

下一步只观察新增范围，尤其突进蓄力→释放、四连两种替代入口与拔刀飞身跃入→后续招式，确认延迟跟随、实际判定、Dodge Accept冻结和正常回手/续招。代理未运行PIE，不把资产回载算作最新观感签收；无需全招式重录。空中动作、独占猎虫特殊招式、未点名地面动作均不自动加入。未跑全量回归/资产验证或提交/推送Git，既有本地检查点仍为`9182186`。

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

## 当前维护入口

- [音效实施计划](崛起虫棍攻击与命中音效实施计划.md)：保留当前1.37局部耳听及尚未覆盖项；不因本次整理重复部署/录制。
- [猎虫协同计划](猎虫协同攻击实现计划.md)：保留10.16后局部PIE的实际未确认边界及后续设计。
- [已知低优先级问题](项目已知的不重要问题.md)：按需处理。
- [维护历史快照](../archive/maintenance-2026-10-25/status-history.md)：完整保留旧音效、移动、编辑器工具及阶段优化证据；原有有效签收持续有效。

本次只整理文件与零引用资产，不改变玩法、重新要求历史验收或提交Git。旧记录中的完成成绩不等于本轮重跑，最近修改仍按各自条目的真实检查结果判断。
