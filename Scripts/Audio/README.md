# 崛起音效维护工具

> **2026-10-19当前武器基准覆盖下方历史结论：** 用户撤销旧武器录制的音色/覆盖依据，当前仅`175309_05`为主、`180133_06`为只换武器对照，见[基准与排除](../../docs/reference/当前武器音效基准与刀尖收放排除.md)。旧固定3录制位置发布、混合所有历史录制的coverage发布、默认首分支试听都不能更新当前武器结论。复用按`--capture`指定的映射/命中审计；试听必须核对同对象实际武器条件。现有源文件、旧WAV和UE配置保留历史，不盲目Apply或清空。

`rise_audio_m1.py`只处理音效M1的银行索引、第三方解析器调用、候选解码和元数据导出，没有UE依赖，不写Content、游戏安装目录或原始解包目录。复用现有主机Python，不自行实现HIRC解析或音频编解码。

用户另行授权的[NativeBridge](NativeBridge/README.md)是运行时定位补充：新增小型REFramework插件并复用F9，不属于离线M1工具的默认写入范围。真机F9首录已验证1508行/73种事件，无错误丢弃；最新批74种事件、36项主试听＋3份对照。用户已确认14项用途，E1最小选型通过；其余候选/实际条件分支未全部确认，不代表UE接入已通过。

## 当前工具和证据

**最新轨道维护（2026-10-23）：** `setup_current_weapon_audio.py --cloth-tracks`／`-MHGZAudioClothTracks`只读回查`ClothTracks20261023/StableLanes/`，6包11点已保存/独立回载，原时间/音源/其它通知不变。UE加载时不允许同轨同帧；使用`ClothAudio_2`等专用副轨保留原时机，纯规则16项通过。Epic的`get_animation_notify_track_names`会AddUnique，不能把返回下标当真实TrackIndex；现从原生`AnimNotifyTracks`导出读取完整有序数组。初次单轨方案不可重放，日志/备份保留；当前默认空间/空中装备检查也续接稳定方案。纯资产整理，无需编译或PIE。

**最新1.37（2026-10-23）：** 最新两录制`003538_01/003831_02`已解析；空中11 Montage补77个`ClothAudio/ClothSound`空通知，沿用地面装备静音占位规则，原声音/玩法保持。用户已试听确认`615339279`普通爆炸、`647565313`聚爆附加、`18325887/1714085883`聚集两层，已创建6 Wave/4 Cue/独立并发并填入DA三个音频字段。按实际爆炸/到点聚集/提交/取消/Runtime结束播放或停止，非Montage计时；零粉尘不爆音，非目标配置原样。

本轮完成只读回查：`setup_current_weapon_audio.py --aerial-cloth-placeholders`和`--powder-audio`；Commandlet分别`-MHGZAudioAerialCloth/-MHGZAudioPowder`，需`-AllowCommandletAudio`。Generic spatial-maintenance也优先装备新检查点；旧历史事务不重放。装备保护仅接受后继粉尘事务已独立回载的CombatConfig精确hash，不忽略任意DA编辑。首次Apply已完成，勿加Apply/重跑旧prepare。证据/备份`Saved/Audio/RiseIG/AerialPowder20261023/`；44离线、8原生检查、编译及两类资产独立回载通过，未PIE耳听、不要求重录/全量验证。

离线准备复用`map_swing_positions.py --aerial-powder --capture <空中录制> --vortex-capture <聚爆录制> --directory <录制目录> --evidence-root Saved/Audio/RiseIG/Weapon20261019 --output Saved/Audio/RiseIG/AerialPowder20261023`，委托已有cloth模块，Saved内生成四项试听；需先有`Aerial/map.json`和`Vortex/map.json`（原capture_map入口）。已确认后`prepare_current_weapon_audio.py --powder-reviewed`仅用于本轮首次冻结6个音频变体，当前只校验保留。`setup_powder_audio.py`复用现有propulsion Wave/Cue工厂（增加可选并发路径，不改原默认）、空间设置和备份导出，不新增解析器/解码器。1714085883保留原双声道，其它变体单声道；不自动归一化。

**最新源帧对时修正（1.36）：** 四连砸地问题不是少加了一个统一Montage偏移，而是Native音频START握手迟约0.255s，旧`capture_time_estimate_s`错配Lua动画帧。现用`map_swing_positions.bracket_audio`按共享Present计数找源帧，再经现有`segment_positions`处理源裁切/RateScale×片段倍率/StartPos。四连2.292968→2.598957s；另修正约0.15s握手延迟的地面补声批，传播急袭Over既有共享棍花时差。6 Montage改时12点、删除1个实际属304却错归109的点，未新增通知/音源。新增编辑器桥接`EditSoundNotifyTimes`按GUID+预期旧时间检查，只接受瞬时声音子类，拒绝Gameplay窗口；不保存、不排序，调用者备份/保存/回载。68项离线＋3项原生检查和独立回载通过，未PIE听感。

当前`setup_current_weapon_audio.py --section-maintenance --source-time-repair`／Commandlet追加`-MHGZAudioSectionMaintenance -MHGZAudioSourceTimeRepair`只读回载；只有section-maintenance时也续接最新已保存事务。备份、冻结计划、原录制行、端点采样、编译/测试/回载证据在`SourceTime20261022/`。已有state禁止重放Apply/prepare；源帧估算仍有约1显示帧回调顺序误差。Present计数要同一已验证起点，缺失/重载不一致不能退回IPC估算时钟。旧映射报告不覆盖重写，也不能再拿旧包hash恢复本次合法改时。

**1.35补点方案已撤回：** 5个点并非用户所指问题的修复，三包在验证无后续修改后恢复原哈希，两版备份保留于`SectionAudio20261022/Withdrawal/`。下两段仅为历史记录，不得重新生成或使用其旧审计宣称当前已修复。

**最新分支音效边界维护（1.35，2026-10-22）：** `setup_current_weapon_audio.py --section-maintenance`／Commandlet `-MHGZAudioSectionMaintenance`只读回查新事务。复用现有源帧/原生导出/通知/保护函数，读取53份Montage、443个声音及表现点；当前仍匹配的241个旧源帧锚点无时间漂移。发现共用Core前置挥击声在跳段路径漏播，而非全音轨绝对时间算错：只为四连横扫入口、飞圆二连/横扫入口、上捞横扫/飞身入口补5点，保留原点及同一音源/音量/挂点/覆盖Tag，无新Cue/GA/计时器。已保存并独立回载，原通知/动画/Section完整保留，149个包基线中仅3个目标包变化。9项新规则＋19项原片段时序检查通过；真实配置21次逐入口互斥检查无双播。尚未PIE耳听，不要求全招重录。

证据和可恢复备份：`Saved/Audio/RiseIG/SectionAudio20261022/{findings.md,plan.json,state.json,Backup,apply.json,audit.json}`。修复已完成，不能重复`--apply`、`--inspect`或`section_audio_rules.py --prepare`；不要拿旧事务包hash回退这次合法修改。未来入口/源片段改变应先重新核对源帧与每条实际经过路径，不按时间轴所有片段顺序排音，也不把被跳过片段的所有声音补播。循环/输入释放/落地的动态跳段仍由GA决定，只在实际经过通知时发声。

**最新急袭突刺适配（1.34）：** `prepare_ground_audio.py --descending-thrust`复用原参考配方/曲线及骨骼采样，`setup_current_weapon_audio.py --descending-thrust`／Commandlet `-MHGZAudioDescendingThrust`当前只读回载。已保存Main推进1点、Over砸地/空挥/扬尘8点和GA声音启用开关；仅3包，常驻源全复用，不改运行时/曲线/原通知。证据`DescendingThrustAudio20261021/`，23项离线检查及独立回载通过，未PIE耳听。拒绝重复Apply，勿重跑Inspect/prepare或回放旧事务。这是将崛起声音适配到世界380/381，不是世界原声音效真值。

**最新四连印斩砸地（1.33）：** `prepare_ground_audio.py --four-hit-impact --directory <录制目录> --capture mhrise_20261021_123512_01`复用严格对象链、条件分支和逐源帧标定；该录制命中CSV为空，使用紧接录制的master报告并与原生owner图交叉验证，冻结报告文本。两次161接触请求匹配已入库1762027986，不重新解码。`setup_current_weapon_audio.py --ground-impact`／Commandlet `-MHGZAudioGroundImpact`当前默认只读，单Montage已补2.292968s砸地事件，`Host.OnGroundImpact`可独立接未来特效；不改伤害/原声音/独立扬尘。20离线＋4原生检查、独立回载通过，未PIE耳听。证据`SiLianImpact20261021/`；首次Apply已完成，禁止重放prepare/Apply/Inspect，不用旧事务包哈希回退后继合法修改。

**最新落地事件迁移（2026-10-21）：** `setup_current_weapon_audio.py --landing-events`／Commandlet `-MHGZAudioLandingEvents` 调用现有 `setup_ground_audio.py`，已将10 Montage/10个 `LandingSound` 改为 `LandingEvent`，原通知身份、时钟、轨道、声音、独立扬尘及动画结构保留；另43个同范围Montage不变。新通知支持 `bTriggerGroundDust`＋可选 `GroundDustSound`，现有点均关闭，避免重复。不生成VFX，不改物理落地。18离线＋4原生检查、保存/独立回载通过，未PIE；证据`LandingEvents20261021/`。默认审计，禁止重复Apply；后继迁移后旧GroundEvents冻结类/包哈希为历史，不能重放或据此回退本轮改动。

**最新单字段调音：** `setup_current_weapon_audio.py --dust-volume`（Commandlet `-MHGZAudioDustVolume`）复用既有导出/保存/备份，只调整扬尘Cue音量2.0→1.0，已保存及独立回载；声音图与其它配置不变。证据`Saved/Audio/RiseIG/DustVolume20261021/`，默认只读，不重复Apply；旧MixRecall音量4→2为历史，不重放。GroundEvents审计只认这一个明确后继的哈希链，不能据旧音量哈希要求恢复。

**2026-10-21定向补充：** `mhrise_20261021_000522_01` 仅补二连/飞远/横扫收招棍花和突进回旋接触；其余仍沿用已选主录制。`prepare_ground_audio.py`复用原条件/对象归属和逐片段帧标定，`setup_current_weapon_audio.py --ground-events`调度`setup_ground_audio.py`，不另造银行解析器/解码器。已保存并独立回载32 Montage：48扬尘点改为 `AnimNotify_GroundDust` 事件＋11缺声点，新建2 Wave/1 Cue，400其它包不变。扬尘的 `Host.OnGroundDust` 可供未来粒子订阅，音源为空不阻断事件；现不生成VFX，旧 `DustAudio` 轨位不改。详见[计划1.30](../../docs/using/崛起虫棍攻击与命中音效实施计划.md)。

事务证据 `Saved/Audio/RiseIG/GroundEvents20261021/` 已完成；日常仅用 `--ground-events` / `-MHGZAudioGroundEvents`默认审计。不重跑prepare、Apply或`-MHGZAudioGroundResume`：Resume只用于首次未保存任何Montage、全部原包/新源哈希一致时的特定校验故障续接，现状态禁止重放。资产操作和实际音频测试使用 `-AllowCommandletAudio`，不能加 `-nosound`。32离线＋5原生检查通过，未PIE耳听。旧分类/轨道历史事务不因本次新类而重新Apply。

**最新修复（1.29）：** 普通前/后撑杆跳的4个Montage原先完全无声音点，空回137原有4点但缺推进。旧解释把156误当空回，实际156为白灯前跳Over；11次推进请求均在137进入前2个原生渲染帧，被旧“请求当帧当前动画”筛选误归属。本轮按连续原生帧/唯一入场前请求补空回0.016667s推进点，普通前后跳补25点。前跳无直接样本，按同动作白灯阶段的源帧/速率转换作项目配方；后跳使用当前源146/147单次记录。共5包26点，独立回载/29项纯规则通过，427其它包保持，现43 Montage/422点；不改C++、旧声音、动作、音量或衣服空点，待局部耳听。

```powershell
# 打开编辑器时：只读新鲜路由检查（不回放事务）
& 'C:/apps/anaconda/python.exe' Scripts/ue_remote.py Scripts/Audio/setup_current_weapon_audio.py --script-arg=--inspect-aerial-audio
# 本轮事务默认只读；首次Apply已完成
& 'C:/apps/anaconda/python.exe' Scripts/ue_remote.py Scripts/Audio/setup_current_weapon_audio.py --script-arg=--repair-aerial-audio
# 编辑器关闭时独立回载；无Apply
& 'C:/apps/UE5/UE_5.6/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'D:/study/MH/MHGZ/MHGZ.uproject' -run=pythonscript -script='D:/study/MH/MHGZ/Scripts/Audio/setup_current_weapon_audio.py' -MHGZAudioRepairAerial -unattended -nop4 -nosplash -NullRHI -AllowCommandletAudio
```

继续复用`setup_propulsion_audio.py`，没有另造解析器或资产生成器；`transition_precursor_posts`只读取已独立证明归属的请求，在新源首帧、连续采样且窗口唯一时关联动作边界，不按任意墙钟邻近认领、不改变`scoped_posts`。`transfer_vault_stage_audio`只允许155→141、156→142，同源帧映射不强行压缩整段，不给无证据服装层填声音。证据/备份/state/audit在`Saved/Audio/RiseIG/AerialAudioFix20261020/`；旧冻结事务因本轮合法追加已部分过时，不恢复旧hash。

**最新配置（1.28）：** 猎虫真实命中基础层复用已有`GameplayCue.Hit.Kinsect`，新增`GCN_Hit_Kinsect`、引用已入库B023，不改蒙太奇/音源/混音/DA。共用声音Cue叶新增`ContactSource`，旧Weapon资产默认行为保持，新GCN只收Kinsect上下文。13条精确猎虫发声对象及重复命中同期观测是语义推断依据，不冒称精确事务/原作全部贯通分支；未知539、粉尘继续不配。431个原包保持，396点不变，独立回载通过；6项离线、1项新猎虫及4项原武器原生定向检查通过。正式测试前启动诊断与报告边界见计划1.28，未PIE耳听/全量验证。

```powershell
# 准备已完成：同一入口 --kinsect-contact --capture-directory <录制目录> 只校验/保留，不重新解码
# 本轮只读回查；首次Apply已完成，不追加--apply
& 'C:/apps/anaconda/python.exe' Scripts/ue_remote.py Scripts/Audio/setup_current_weapon_audio.py --script-arg=--kinsect-contact
& 'C:/apps/UE5/UE_5.6/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'D:/study/MH/MHGZ/MHGZ.uproject' -run=pythonscript -script='D:/study/MH/MHGZ/Scripts/Audio/setup_current_weapon_audio.py' -MHGZAudioKinsectContact -unattended -nop4 -nosplash -NullRHI -AllowCommandletAudio
& 'C:/apps/anaconda/python.exe' -m unittest discover -s Scripts/Audio -p test_kinsect_contact_audio.py -q
```

`prepare_kinsect_contact_audio.py`只补原工具没有的“精确猎虫发声对象＋已观测伴随关系”证据，复用严格归属和已解码源池，不加解析器/解码器。`setup_kinsect_contact_audio.py`复用工厂/作者归属/哈希保护，只创建一个原来缺失的GCN；已有同名/同Tag处理器拒绝覆盖，state存在拒绝重放Apply。证据/源计划、测试及`UE/{plan,state,apply,audit}`在`Saved/Audio/RiseIG/KinsectContact20261020/`；粉尘源、其它独立动作缺口及细调顺序见计划1.28。

**历史配置（1.27，先配置后细调）：** 推进声`3520842405`补到8个蒙太奇，新增1 Wave＋1 Cue，复用现有动作空间设置、不动旧混音；独立回载通过，当时396点，100衣服点仍空。仅当前主录制36条同帧猎人容器临时对象链得到支持，2条隔离；10项新归属/末帧规则＋17项原映射检查通过。末帧保持区只作项目交接前一显示帧的适配。当时将156误称空回并隔离是错误解释，已由1.29按137的入场前2帧请求纠正；不可继续当成“空回无稳定证据”。

准备入口`prepare_current_weapon_audio.py --propulsion --capture-directory <录制目录>`只写`Saved/Audio/RiseIG/Propulsion20261020/`，已完成准备不重复解码；模块`prepare_propulsion_audio.py`复用既有同对象条件、动画时钟、wwiser/媒体切片/有限解码。`hunter_fx_posts`只为确认事件显式启用，不放宽通用归属；`end_frame_hold_positions`只接纳完整保留尾段、身份帧长匹配且重复落入最后保持帧的来源。

```powershell
# 本轮显式只读回查；首次Apply已完成，不追加--apply
& 'C:/apps/anaconda/python.exe' Scripts/ue_remote.py Scripts/Audio/setup_current_weapon_audio.py --script-arg=--propulsion
# 编辑器关闭：独立新进程回载
& 'C:/apps/UE5/UE_5.6/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'D:/study/MH/MHGZ/MHGZ.uproject' -run=pythonscript -script='D:/study/MH/MHGZ/Scripts/Audio/setup_current_weapon_audio.py' -MHGZAudioPropulsion -unattended -nop4 -nosplash -NullRHI -AllowCommandletAudio
& 'C:/apps/anaconda/python.exe' -m unittest discover -s Scripts/Audio -p test_propulsion_ownership.py -q
```

`setup_propulsion_audio.py`是同入口的定向事务模块，复用现有工厂/快照/声音配方/保护逻辑；备份、plan/state/apply/audit及日志在`Propulsion20261020/UE/`，保护385个其它包。默认只读，已有事务拒绝再次Apply；没有新C++或运行时加载/动态蒙太奇。旧`--spatial-maintenance`仍选择历史分轨事务，不是本轮追加后的全局审计，不能拿旧hash失败回退本轮或1.26合法修改。

**最新小调（1.26）：** 扬尘Cue音量4→2、收虫补1个操虫同款声音点，只写两包并独立回载；证据及默认只读窄审计为`Saved/Audio/RiseIG/MixRecall20261020/adjust.py`，首次显式Apply已完成。现388通知，100衣服点仍空。下方分轨及更早的冻结hash早于这次合法调整，不能直接拿旧全范围hash失败要求恢复旧包，也不为两项小调重开整套资产生成/回归；实际状态见计划1.26及current-status。

**最新分轨（覆盖下方旧审计优先级）：** 默认`setup_current_weapon_audio.py --spatial-maintenance`／Commandlet `-MHGZAudioSpatialMaintenance`优先回查`Saved/Audio/RiseIG/NotifyTracks20261020/`。按类复用/新增`SwingAudio`、`ActionAudio`、`DustAudio`、`LandingAudio`、`ClothAudio`，29包54点仅换轨，387点及声音配方/玩法通知保持，100衣服点仍空。12项配方/保护规则、2项原生定向及独立回载通过，未PIE。

显式模式为同入口追加`--organize-tracks`或`-MHGZAudioOrganizeTracks`，首次Apply已完成。模块`audio_notify_tracks.py`复用快照/配方/备份和保护；已有编辑器桥接`move_sound_notify_to_track`只改唯一声音事件TrackIndex及轨道指针缓存，不重建/排序/删除通知或旧轨。新轨道只在有相应类时创建，同名复用。29包备份及plan/state/apply/audit/Tests/日志保留；勿重放旧事务或恢复旧轨道hash。`test_spatial_audio_rules.py`现12项（原9项＋3项轨道保护）。

- **2026-10-20当前续接入口：** `setup_current_weapon_audio.py --spatial-maintenance`，Commandlet选择`-MHGZAudioSpatialMaintenance`；有衣服检查点时回查`ClothComparison20261020/UE/`，否则依次回查`NotifyCategories20261020/`、`SpatialMaintenance20261020/`。最新13 Montage/100个`Sound=None`衣服点由装备/无装备静音分支及双录制时机定位，原287个有音源点不变，39 Montage共387个声音通知。5项对照＋9项配方及独立回载通过，未PIE耳听。旧事务是历史，不重跑Apply或强行恢复旧类/旧点数。

衣服对照复用`map_swing_positions.py --compare-cloth --capture mhrise_20261020_182603_03 --directory <录制目录> --evidence-root Saved/Audio/RiseIG/Weapon20261019 --output Saved/Audio/RiseIG/ClothComparison20261020`。辅助模块`rise_audio_cloth_comparison.py`复用同对象条件、动画时钟和wwiser；识别插件静音而非仅看Media ID。默认保留已完成报告并检查输入hash，不覆盖旧主映射、不解码新音频。5项测试为`test_cloth_comparison.py`。

UE同入口追加`--cloth-placeholders`或Commandlet `-MHGZAudioClothPlaceholders`选择空点事务，首次Apply已完成。13份备份及每点事件/源位置/片段/循环/GUID在`ClothComparison20261020/UE/`；只追加`ClothAudio`轨的`AnimNotify_ClothSound`，不删/移动旧通知，Sound为空、躯干挂点。原先并未接这4类音源，因此没有撤下既有音频；不把100个空点计为新增可听覆盖。旧分类显式审计会因本轮追加而过时，日常用默认最新审计，不恢复旧包。

```powershell
# 当前默认只读回查（打开编辑器）
& 'C:/apps/anaconda/python.exe' Scripts/ue_remote.py Scripts/Audio/setup_current_weapon_audio.py --script-arg=--spatial-maintenance
# 关闭编辑器：不叠加-MHGZCurrentWeaponAudioApply
& 'C:/apps/UE5/UE_5.6/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'D:/study/MH/MHGZ/MHGZ.uproject' -run=pythonscript -script='D:/study/MH/MHGZ/Scripts/Audio/setup_current_weapon_audio.py' -MHGZAudioSpatialMaintenance -unattended -nop4 -nosplash -NullRHI -AllowCommandletAudio
```

`audio_spatial_maintenance.py`是已有入口的定向事务模块，复用导出/保护/工厂；`spatial_audio_rules.py`是纯配方规则，9项测试见`test_spatial_audio_rules.py`，不另造录制器或解码器。首次Apply已完成，存在state时拒绝重放。原声音通知GUID/时机保留；手动改变点位或音量后先记录新快照，旧hash检查拒绝覆盖不等于要求用户还原。`-MHGZAudioRestrictSheathe`是误纳5个装备层点的**一次性纠正历史入口**，不可再次执行；仅清了本轮新增轨，原作者数据保留。`-MHGZAudioInspectExternal`只读导出收刀翻滚源身份（018/022），不补猜测时间。

分类模式由同入口追加`--classify-notifies`（Commandlet追加`-MHGZAudioNotifyCategories`）选择，默认计划/审计，明确Apply才写；当前首次写入已完成，日常只读。复用编辑器桥接`MHGZAnimationSeamLibrary.replace_sound_notify_class`按唯一GUID替换通知对象，不清轨、不排序、不另造资产工具。29份Backup、原事件/完整声音配方及366个未动包hash保存在最新检查点。原1.22状态不改写；默认续接识别后继分类事务，不能拿旧hash判新分类失败而恢复旧包。

编辑器里挥击仍为`Weapon Swing Sound`，其它通用动作为`Action Sound`；新增可独立添加的`扬尘声（Dust Sound）`、`落地声（Landing Sound）`、`衣服晃动声（Cloth Sound）`，分别是`AnimNotify_DustSound/LandingSound/ClothSound`。前两类默认地表/`Root`，衣服默认角色/`Spine_00`且Sound为空；未来确认素材后逐点填写，不把武器晃动当衣服声。`Source`/`AttachSocket`等配方仍可编辑，武器明确棍尖`IG_FrontTip`或棍尾`IG_RearTip`。原`SwingAudio`/`ActionAudio`轨和点位保留，可直接拖动调时机；新分类只改变通知类型/中文显示。当前挂点是项目初始配方，不宣称原作Socket真值。局部诊断`mhgz.Audio.SwingDebug 1`输出实例拒绝/音源/挂点/位置/组件创建，0关闭；不用F9。猎虫循环/真实命中/实际精华交付继续按各自生命周期触发。

- **最新动作/猎虫事务：** `--expand-actions`复用前批源池/路由筛选，`UEActionRuntime/`已完成29 Montage/75个新增动作附加点，现全局33 Montage/265点；本批报告241点，另24点所在4包未动。只处理GA所属的地面/动作附加，不扩到普通MM脚步、装备摩擦或无精确实例的独立翻滚/收刀。新增75个`Audio.Action.IG.*`Tag，保持全部旧声音GUID/配方和其它通知。

猎虫新增自有`FlightAudio`组件及CombatConfig的`KinsectLaunchSound/KinsectFlightLoopSound`，交付复用`ExtractCollectedSound`；只设3音频字段。新循环Cue引用已入库B020的Wave并设置WavePlayer.looping=true，不能只设Wave.looping。它回载为`Duration=10000 / bLooping=True`，自由飞行/悬停/返航播放，回手/武器附着/失效/销毁停止；离手B021仅成功请求后播，共用交付B026按同一非零命令去重。源图/字段证据、语义限制及代码哈希写入plan，不把不明603272525/539819764猜成返航/命中。

```powershell
# 已完成，不重跑prepare或Apply；编辑器打开时默认审计最新事务
& 'C:/apps/anaconda/python.exe' Scripts/ue_remote.py Scripts/Audio/setup_current_weapon_audio.py --script-arg=--expand-actions
# 编辑器关闭：新进程只读回载
& 'C:/apps/UE5/UE_5.6/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'D:/study/MH/MHGZ/MHGZ.uproject' -run=pythonscript -script='D:/study/MH/MHGZ/Scripts/Audio/setup_current_weapon_audio.py' -MHGZCurrentWeaponActions -unattended -nop4 -nosplash -NullRHI -AllowCommandletAudio
```

首次准备`prepare_current_weapon_audio.py --expand-actions`，首次写入才额外明确Apply；本轮已冻结且独立回载，不覆盖计划或重置hash。源池96包保持，新增1 Loop Cue；29 Montage/CombatConfig改变，230个其它既有包不变。增量编译、80项离线、4项原生直接测试通过（24条既有harness警告），保存/回载均0错误/0警告；未PIE耳听。`UEActionRuntime/`含Backup、tags-config-before、build.log、Tests及manifest/audit。三灯/耗尽未替换新录制音色，推进声、粉尘、未知命中、普通移动/装备等未完成。

- **最新追加：** `prepare_current_weapon_audio.py --expand-weapon`复用已完成源池，冻结`UEWeaponCoverage/`的追加事务；`setup_current_weapon_audio.py --expand-weapon`或Commandlet `-MHGZCurrentWeaponExpand`选择该事务。33 Montage/190点（新增131，原59的GUID/配方不变），15种事件，包含Primary938、觉虫击起手/丢虫主体9点。25个GA启用，其中本次只保存3个GA的音频字段。134个新音频Tag通过冻结的append-only文本补丁加入配置，原配置保留。没有新增音源包/重解码/C++。

新增筛选只取至少2次观察、实际GA默认/Combo/次级Montage路由可达且非边界者；同事件一帧内近邻歧义不双写。觉虫击Send195通过已核对的运行时Hold释放路线可达，不误按静态自循环前缀裁掉；不加从GA激活开始的音频计时器。82项隔离不当作原作无声或强制补录。保持152条原通知（其中59条声音）及所有动画/Section；259包中36包变更、223包不变。37＋40项定向检查通过，两个Commandlet均0错误/0警告；`UEWeaponCoverage/audit.linked=true`、`manifest.phase=independently_reloaded`，尚未PIE耳听。

```powershell
# 已冻结并完成，不重跑prepare；默认审计最新追加事务（编辑器打开）
& 'C:/apps/anaconda/python.exe' Scripts/ue_remote.py Scripts/Audio/setup_current_weapon_audio.py --script-arg=--expand-weapon
# 编辑器关闭：新进程默认只读回载，不带Apply
& 'C:/apps/UE5/UE_5.6/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'D:/study/MH/MHGZ/MHGZ.uproject' -run=pythonscript -script='D:/study/MH/MHGZ/Scripts/Audio/setup_current_weapon_audio.py' -MHGZCurrentWeaponExpand -unattended -nop4 -nosplash -NullRHI -AllowCommandletAudio
```

首次准备使用`prepare_current_weapon_audio.py --expand-weapon`，首次明确写才同时加`-MHGZCurrentWeaponAudioApply`/`--apply`；当前已完成，日常不重跑Apply。`Backup/`和`tags-config-before.ini`保留修改前基线。上轮`UEAudio`及旧M3检查点已部分过时，保留历史，不再Apply或重置hash。下一项仍为独立生命周期的动作附加/猎虫等及推进声归属，不把已准备Cue当运行时完成。

- **最新落地（先于下方映射阶段/旧M3记录）：** `Weapon20261019/UEAudio/`已冻结36组源图，解码64份变体、去重导入60个Wave＋36个Cue。旧70个工具声音点按同源/事件/次数对应，59点换源重定位，11点缺观测或歧义备份后撤下。24个Montage已保存、新进程独立回载，85条非音效通知/动画结构保护，139个其它既有包不变。实际接线仍只用5种挥击，其余31组（含Primary938）只有音源准备，不宣称全覆盖。未PIE听感/编译C++/改GA/全量验证。

### 当前武器资产事务

`prepare_current_weapon_audio.py`只写Saved：复用`render_preview`和`export_media_slice`，对单源/平面随机组2～4叶子解码，选择器以外的TXTP增益/时序不变；补索引只涉及映射银行及装备`pl_eq_*`家族，不覆盖旧M1索引。每份WAV成功后立即记哈希，续接验证而非重新解码。`setup_current_weapon_audio.py`复用小样工厂与M3原字段保护；确认为旧M3作者点的整个`SwingAudio`轨才可重建，有未知/用户音效拒绝删除。原Tag复用，原非声音通知GUID/顺序/时间/载荷保护。备份保留，不删除旧音源。

```powershell
# 已完成准备，不需日常重跑；冻结输入不符时拒绝覆盖，不重置hash绕过保护
& 'C:/apps/anaconda/python.exe' Scripts/Audio/prepare_current_weapon_audio.py
# 编辑器已打开，默认审计（写入只能首次明确--script-arg=--apply）
& 'C:/apps/anaconda/python.exe' Scripts/ue_remote.py Scripts/Audio/setup_current_weapon_audio.py
# 编辑器关闭：默认独立回载；首次写入才加-MHGZCurrentWeaponAudioApply
& 'C:/apps/UE5/UE_5.6/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'D:/study/MH/MHGZ/MHGZ.uproject' -run=pythonscript -script='D:/study/MH/MHGZ/Scripts/Audio/setup_current_weapon_audio.py' -unattended -nop4 -nosplash -NullRHI -AllowCommandletAudio
& 'C:/apps/anaconda/python.exe' -m unittest discover -s Scripts/Audio -p 'test_current_weapon*.py' -q
```

当前`UEAudio/manifest.json phase=independently_reloaded`、`audit.json linked=true`，两个Commandlet均0错误/0警告；28项当前武器＋40项原保护测试通过。`plan.json`冻结源WAV/图及原包，`decode-checkpoint.json`记录叶子选择与实际WAV哈希；`Backup/`备份原24包。资产目录`Audio/Waves/MuGu`、`Cues/MuGu`，工厂创建Random只是项目随机适配，不冒称原作权重/历史相同。

旧M3/M2manifest已部分过时，不再Apply这些历史事务。下一步继续新增武器事件和实际入口筛选；通用命中缺同对象条件、普通移动未校准、运行时猎虫/精华层未接，均不能把已准备Cue当完成。首次沙箱启动没有日志/资产，换桌面上下文成功；缺装备媒体定位在`pl_eq_*`家族后已解决，不重复启动/全游戏扫描。

### 映射生成阶段（历史检查点）

- **当前武器映射已生成（先于下方历史清单）：** [逐动作映射](../../docs/reference/当前武器逐动作音效映射.md)及配套JSON；主录制5263条归属明确的相关请求、37条确认排除、4942条源动画内条件匹配，36个音源图/43个Montage/837个时间候选。这里只匹配候选图和时序，没有解码新WAV或生成Notify，不是837个正式播放点。240条缺条件（其中190条通用命中请求）及95条切换边界保留未决；28条非播放控制请求单列。17项新定向检查＋19项原时序检查通过，不要求逐音色用途确认。

```powershell
# 一份明确Primary录制；读取已澄清的分支级排除，绝不借Control/历史音色补项。
& 'C:/apps/anaconda/python.exe' Scripts/Audio/map_swing_positions.py --directory 'F:/games/Steam/steamapps/common/MonsterHunterRise/reframework/data/MHGZ_AerialTrajectoryRecorder' --map-current-weapon --capture mhrise_20261019_175309_05 --evidence-root Saved/Audio/RiseIG/Weapon20261019 --output Saved/Audio/RiseIG/Weapon20261019/MappingNext --publish
```

此模式复用原解析/时序工具，内部`rise_audio_weapon_mapping.py`只补同对象条件、归属和分支选择；按真实观察到的条件元组调用固定wwiser，不做材质/武器条件的笛卡尔积。循环/Field节点保留源时机，female来源额外校核帧长，不按尾号硬配到Field。未知用途不是自动排除或人工验收门禁。只有1942118895和Control对应938分支按用户裁定排除，Primary938必须保留。已完成输出拒绝覆盖，旧`Mapping/MappingV2`是诊断中间结果，当前权威文件在`MappingFinal/`；不要重跑旧默认三录制模式。

- **逐招式覆盖盘点已完成：** 11份录制、26个GA/16攻击、157个已回载包哈希未变；参考[覆盖与补录清单](../../docs/reference/虫棍逐招式音效覆盖与补录清单.md)，完整JSON在`Saved/Audio/RiseIG/Coverage20261019/`。旧工具的固定3录制/5风声模式不能盘点新增命中归属与全录制入口，因此只新增`map_swing_positions.py --audit-coverage`模式，内部复用`rise_audio_coverage.py`，不另造解析器或覆盖原positions/试听。12项新检查＋19项原位置检查通过。

```powershell
# 只读已有录制/银行/资产文件，不启动UE或游戏；输出只写新的Saved目录。
& 'C:/apps/anaconda/python.exe' Scripts/Audio/map_swing_positions.py --directory 'F:/games/Steam/steamapps/common/MonsterHunterRise/reframework/data/MHGZ_AerialTrajectoryRecorder' --audit-coverage --output Saved/Audio/RiseIG/CoverageNextRun --publish
```

覆盖模式最多32份原生音频录制、每份侧车64MiB；复核M3回载哈希，采集文件前后哈希不符则停止。已存在report拒绝覆盖，续跑另选输出目录；`--publish`仅更新专用覆盖清单，不发布旧音源/运行时映射。不把缺风声候选当原作无声，不把共用源或相邻动画链当GA/Combo证明；有gap/复用仍保留未知。

- **1.21三项修复实采通过：** 最新`145235_02`有28组伤害/pre/post，字段补齐、队列丢失/读取错误/覆盖警告0，F9保存正常；不用重复加载/短录。命中声27/28唯一完整归属，1个复用地址仍留未知，最终Media未采集；不把此结论扩大为全音效签收。证据`Combat121/Capture145235-Audit/findings.md`，可继续逐招式采集/映射。下方待短录为历史。
- **1.21加载已核对：** 进程30288正确选择旧宿主上下文栈，新对象队列与原生观察器均正常、快照丢弃/gap/错误0。证据`Combat121/Runtime30288/`；下方待加载已完成，现仅需少量实际命中短录，不重装/重启或录全招式。
- **1.21已部署待加载：** 旧宿主引擎上下文栈后快照/能力报错、对象专用MPSC序号环/损失原因计数、96字段及容器诊断已实现。89音频Lua、17动画/耐力、10关联审计与3原生CTest通过；仅替换Lua/DLL，1.20已备份。先核对加载，不要求全招重录；详见NativeBridge与MHRise README。下方1.20未通过是保留的修复依据，不是本轮已签收。
- **最新1.20短录未整体通过：** `mhrise_20261019_134722_03`有42条有效原生命中边，但post快照0、对象侧车21处queue_gap。当前宿主缺失所依赖的hook-storage能力，且对象队列已有录制内损失；先修采集器，不要求再次录制。证据`Saved/Audio/RiseIG/NativeBridge/Combat120/Capture134722-Audit/findings.md`；下方“待短录”是此前加载状态。
- 1.20录制修复已部署并在进程33060加载：必要字段优先/DamageRS/声音post快照、精确临时发声实参/HitInfo调用链。仍待短录真实命中，不要求全招重录。F9前对象队列丢弃3条单独记录，不宣称零损失。原Post/条件格式不变；详细边界/回滚见NativeBridge与MHRise README。最终Media及有歧义的池身份仍不猜填。
- `rise_audio_capture_map.py --audit-combat`：复用已有wwiser解析与路径保护，读取命中/对象/条件侧车，输出独立`combat-audit.json`，不解码/修改试听/发布reference。默认值来自实际Play根容器，不借其他对象Switch。例：`--capture mhrise_20261019_123713_02 --directory Saved/Audio/RiseIG/NativeBridge/Combat119/Capture123713 --output Saved/Audio/RiseIG/NativeBridge/Combat120/<new-audit-directory>`。已存在审计拒绝覆盖；新逻辑在`rise_audio_combat.py`，10项定向检查。

- `rise_audio_m1.py`：固定的20份工作银行；只对虫棍/猎虫相关媒体族与所选通用银行做额外索引，当前共84份来源银行。按哈希复制，不覆盖内容不同的现有工作副本。
- `test_rise_audio_m1.py`：六项纯离线检查，覆盖块边界、DIDX范围、空银行、TXTP缩进路径和PCM16波形指标；不启动UE/游戏。
- `rise_audio_capture_map.py`：在有限元数据银行中定位原生录制EventID，用固定wwiser确认事件及Play/Stop；关联实际动画上下文，按需生成有限试听。复用M1的TXTP读取、DIDX导出和波形检查，不另写编解码器。
- `test_rise_audio_capture_map.py`：十九项离线检查，覆盖动作类型/缺引用、同序列重播、输入输出隔离、来源切片、完整条件头、仅元数据刷新、随机分支对照及有界用户签收持久化；加上M1共25项通过。
- 原始输入：`D:/study/MHR/natives/STM/`，只读。
- 原始输出及便携工具：项目`Saved/Audio/RiseIG/`，不入Git。
- 持久映射：[虫棍音效资源映射](../../docs/reference/虫棍音效资源映射.md)、M1 JSON清单及[运行时映射JSON](../../docs/reference/虫棍音效运行时映射.json)，只有元数据、没有游戏音频。

## 已部署的便携依赖

| 工具 | 本机位置（项目相对） | 实际版本/来源 |
|---|---|---|
| wwiser | `Saved/Audio/RiseIG/Tools/wwiser-v20260808/wwiser.py` | 官方`v20260808`，commit `94580dd3de3280933d69c435e96d4e2c3b443207` |
| vgmstream | `Saved/Audio/RiseIG/Tools/vgmstream-maintainer/vgmstream-cli.exe` | 实际输出`r1980 (Dec 31 2024)`；官方README所列维护者备用构建，不是r2117 |

vgmstream下载包SHA256为`110f9087e60057c4af6cff84e26c214159c224792421affdddd3aaa2091f2641`，EXE为`6e871a7dfd8b3138e3383f93520da10e4de1af8b8d0c02b40bc84f48d0f3661e`。哈希用于固定本机已测试的版本，不冒称是发布方签名。当前v140银行、事件组合和19份PCM16输出已实际通过本轮验证。

官方来源：[wwiser固定标签](https://github.com/bnnm/wwiser/tree/v20260808)、[vgmstream README](https://github.com/vgmstream/vgmstream)、[维护者备用构建](https://raw.githubusercontent.com/bnnm/vgmstream-builds/master/bin/vgmstream-latest-test-u.zip)。不随脚本自动下载或更新工具。

## 执行

```powershell
# 拷贝有限银行、索引媒体及来源哈希；不解码全库
& 'C:/apps/anaconda/python.exe' Scripts/Audio/rise_audio_m1.py prepare
# 运行wwiser，生成本次独立目录的XML/TXTP；续接已有产物不必带--parse
& 'C:/apps/anaconda/python.exe' Scripts/Audio/rise_audio_m1.py analyze --parse
# 每组最多4个不同事件，跳过缺依赖组合；实际默认随机分支，有限时长
& 'C:/apps/anaconda/python.exe' Scripts/Audio/rise_audio_m1.py audition
# 重新验证源/副本哈希、WEM切片、WAV非静音及哈希，不生成音频
& 'C:/apps/anaconda/python.exe' Scripts/Audio/rise_audio_m1.py verify
# 显式将元数据输出到docs/reference/虫棍音效M1资源清单.json
& 'C:/apps/anaconda/python.exe' Scripts/Audio/rise_audio_m1.py publish
# 仅本工具测试
& 'C:/apps/anaconda/python.exe' -m unittest discover -s Scripts/Audio -p 'test_*.py' -v
```

`--work-root`必须位于项目Saved内，`--source-root`只读；普通续接优先`verify`，不盲目重新生成。`analyze --parse`每次生成独立TXTP目录，`parse_run.json`指向当前版本，旧目录不参与清单。`Audition/index.html`只列当前`previews.json`里的19份候选，之前尝试遗留的文件不算当前交付。

### 对应原生录制与试听

```powershell
# 只读原生音频/动画CSV和解包资源，生成map.json；不需要游戏运行
& 'C:/apps/anaconda/python.exe' Scripts/Audio/rise_audio_capture_map.py --capture mhrise_20261018_131457_01
# 显式生成最多48种范围内Play事件的试听；会重新渲染，不用于普通续接
& 'C:/apps/anaconda/python.exe' Scripts/Audio/rise_audio_capture_map.py --capture mhrise_20261018_131457_01 --audition
# 复用已保存map/audition，发布reference元数据并刷新试听页；不扫描/解码
& 'C:/apps/anaconda/python.exe' Scripts/Audio/rise_audio_capture_map.py --capture mhrise_20261018_131457_01 --publish
# 修复旧清单中的条件遗漏；只刷新capture/M1元数据，不重解码既有WAV
& 'C:/apps/anaconda/python.exe' Scripts/Audio/rise_audio_capture_map.py --capture mhrise_20261018_131457_01 --refresh-metadata --publish
# 对单个事件的既有TXTP作有限分支对照；本事件新增3份，原第三项复用
& 'C:/apps/anaconda/python.exe' Scripts/Audio/rise_audio_capture_map.py --capture mhrise_20261018_131457_01 --compare-event 3451389755 --publish
# 需要同步初始M1 reference清单时，刷新后显式发布；不会生成音频
& 'C:/apps/anaconda/python.exe' Scripts/Audio/rise_audio_m1.py publish
```

`--directory`默认复用MHRise现有录制路径发现器，换设备时显式指向原始CSV目录；`--source-root`默认`D:/study/MHR/natives/STM`。默认结果在`Saved/Audio/RiseIG/CaptureMap/<capture>/`：`map.json`保存全部事件/动作/116段动画上下文及估计时间线，`audition.json`保存38份WAV的来源/条件/哈希与1项缺口，`index.html`可按动作筛选，接触候选排在最前。`--publish`只将元数据写到固定reference JSON，不包含原始CSV或音频；其中绝对路径是本机证据，换设备需重新生成。

每次最多扫描1000份、单份4MiB、合计32MiB元数据银行；二进制ID命中只是预筛，必须由wwiser确认`CAkEvent`及其动作。纯Stop/空事件不生成声音；未生成TXTP的Play事件不能误标成Stop。已存在M1的19份试听/清单不被覆盖，新工作副本和WEM切片必须与原文件哈希一致。

这些是**发生于录制时间段的候选**，不是已归属猎人或已确定随机/Switch分支的原声。单个动作可能伴随装备、语音或环境事件；不能按时间相邻就全部导入该GA。没有自动填写Notify帧，也没有接入UE。

最新页改用`mhrise_20261018_151536_02`，位于对应`CaptureMap/<capture>/index.html`：36项主试听＋3份对照、74种事件来源全部定位；首录旧目录不变。页面显示capture编号；第一项保留上轮初判的具体WAV并标记`preserved_candidate_from/review_note`，不把它说成本次实际分支。普通续接只`--publish`，不再次`--audition`覆盖人工选型。

最新人工反馈已将第一项从“初判”升级为确认，并确认另外13个事件用途。`previews[].semantic_review`保存用户确认、所听WAV哈希和项目约束，`review_note`显示于页面；`user_review.scope=E1_minimum_selection_only`且`listening_accepted=true`只表示最小音色选型通过，不表示全部候选/条件已辨认或UE/PIE已验收。精华交付`3971364902/806767037`只需选一种，具体默认尚未在UE配置。`--publish`保留这些字段，不解码音频；权威人工表见映射第9节。

本机当前执行环境的Steam自动发现落到了没有录制的默认C盘路径，已按部署文档确认F盘真实目录；本轮显式传`--directory F:/games/Steam/steamapps/common/MonsterHunterRise/reframework/data/MHGZ_AerialTrajectoryRecorder`，未修改注册表/全局环境或录制路径发现器。换环境同样先核对路径，不把找错目录当成零行录制。

条件读取优先采用TXTP的`# * gamesyncs:`和`# * statechunks:`完整头，分开保存条件与来源；只在相应头不存在时回退文件名，不能从`PATH/STATECHUNKS`里的所有可能值猜本次选择。此修复恢复了首个命中候选被截断文件名漏掉的3层Switch和1层State。

第三项分支对照位于同一试听页的`#branch-comparison`，保留原前3项顺序：9个条件组合按完全相同的渲染指令分为2/6/1组，分别是原第三项、3个随机素材、Wwise Silence。只修改wwiser原有`-R<count>><selection>`选择号来单独播放随机素材，不改音量/时长/其他指令。仅支持一个平坦2～4选随机组、最多16个组合/8份新增试听；复杂图拒绝自动展开。静音插件不生成WAV，也不能按关联的Source ID误称为可听媒体。`comparison-3451389755.json`保存9组来源哈希和候选；再次调用同一对照时复用已存在结果。

## 挥击源位置与当前Montage配置候选

`audit_swing_assets.py`补足现有音频映射未读取当前Montage裁切/Section的能力，复用`ue_asset_utils`原生导出解析与`read_asset_import_ids.RELATIVE`源文件身份读取，不另造二进制解析器。只读虫棍目录的52个Montage、26个GA、66条引用序列及Combo；输出`Saved/Audio/RiseIG/SwingMap/asset-snapshot.json`和原生文本导出，前后核对145个相关包哈希。无setter、保存或蓝图编译。

编辑器打开时按项目惯例用`ue_remote.py Scripts/Audio/audit_swing_assets.py`；关闭时：

```powershell
& 'C:/apps/UE5/UE_5.6/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'D:/study/MH/MHGZ/MHGZ.uproject' -run=pythonscript -script='D:/study/MH/MHGZ/Scripts/Audio/audit_swing_assets.py' -unattended -nop4 -nosplash -NullRHI -nosound
# 离线复用三份既有CSV和上述快照；默认仅输出Saved，加--publish才发布reference
& 'C:/apps/anaconda/python.exe' Scripts/Audio/map_swing_positions.py --directory 'F:/games/Steam/steamapps/common/MonsterHunterRise/reframework/data/MHGZ_AerialTrajectoryRecorder' --publish
```

读取录制固定为`20261018_151536_02`、`151312_01`、`131457_01`，不自动换成未知新录制；每种源动画优先用最新纯净样本，只有缺失才补旧样本。原始输入只读，发布前检查资产快照哈希仍匹配当前磁盘。输出[候选表](../../docs/reference/虫棍挥击音效位置配置.md)及JSON；Saved完整报告保留所有522次请求的时间关联、边界隔离和原始采样证据。

`frame_units=engine_native_unscaled`不能当UE帧。逐序列核对数字源ID与`UE帧数=2×(node_end_frame−1)`才准许候选换算；当前导入文件帧率与UE采样率均为60Hz，不误写30Hz。已有160 Root校准支持2倍关系，但本工具的其它序列校核只是长度一致，不冒充独立位姿/精确Notify真值。范围没有包含未知IPC/Render延迟，`runtime_ready=false`。FrameRate编辑器字段未暴露给Python，现从原生导出读`ImportFileFramerate/ImportResampleFramerate`，不再重试失败的属性名。

目前60组当前引用源候选映射为70个Montage位置；没有把缺样本当作原作无声，也不自动给所有52个Montage加声音。Section图按实际主AttackMontage和Combo配置入口筛选，其它入口跳过的声音不补播；Hold循环只报告有界前缀，运行时提前结束/跳转不由静态表推断。世界急袭和觉虫击仍有明确缺项。19项新增纯离线测试，加已有25项共44项通过；未执行UE播放/PIE。

## M2突刺挥击小样（已保存及独立回载，E2待听感）

公用`UAnimNotify_SwingSound`按精确Mesh＋Montage实例解析本次Action，不查“当前任意GA”，不修改共享Notify。`UMHGZGameplayAbility`在提交/注册后、子类播放Montage前异步准备武器软覆盖，只保留已加载强引用；通知不读盘，未加载用常驻默认。解析顺序：事件键→GA的`AudioIdentityTag`→通知默认Sound→显式`LegacyAttackSegmentIndex`旧段兜底。音效次数不绑定伤害段。

`bUseAuthoredSwingAudio`默认false，仅`GA_IG_TuCi`开启，阻止该GA旧三灯激活声重复；其它GA不被迁移。当前公用链**只接一次性挥击，循环音源拒绝**，未接命中/粉尘Cue。短尾音自然结束，附着组件销毁则停止；没有整招去重，真正循环再经过点通知仍可重播。

`setup_swing_pilot.py`复用只读原生导出和Epic的`SoundCueFactoryNew.InitialSoundWaves`生成WavePlayer图，不需要computer use、新UI桥或手工编辑SoundCue。只改`GA_IG_TuCi`和`AM_IG_TuCi`，新增6个音频包。每条音源保留已确认WAV哈希/增益、单声道和完整时长；没有剪辑或重新解码原素材。

```powershell
# 编辑器已打开：通过ue_remote运行；默认审计，明确--apply才写
& 'C:/apps/anaconda/python.exe' Scripts/ue_remote.py Scripts/Audio/setup_swing_pilot.py
# 编辑器关闭：音频导入/回查需要初始化解码器，不带-nosound
& 'C:/apps/UE5/UE_5.6/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'D:/study/MH/MHGZ/MHGZ.uproject' -run=pythonscript -script='D:/study/MH/MHGZ/Scripts/Audio/setup_swing_pilot.py' -unattended -nop4 -nosplash -NullRHI -AllowCommandletAudio
# 首次明确写入加-MHGZSwingPilotApply；已接线则优先默认审计，不反复Apply
```

已保存两点：`0.132898s / Audio.Swing.IG.TuCi.First / 2702726931`，`0.317741s / Second / 2231624382`，均为候选位置，不宣称精确原作帧。轨道`SwingAudio`追加在既有轨道后，原4个通知的GUID/顺序/时间/轨道/载荷、片段/Section、GA所有非音频默认值均核对不变；另143个相关包哈希不变。备份、意图、应用与独立回载报告位于`Saved/Audio/RiseIG/M2/TuCi/`。

常驻音频在`/Game/Weapons/InsectGlaive/Audio`：`Waves/SW_IG_HuiJi_<EventID>`、`Cues/SC_IG_HuiJi_<EventID>`；设置在`Settings/SA_IG_HuiJi`和`SCN_IG_HuiJi`。初始衰减内半径500cm、衰减距离2500cm，开启3D；并发上限8、按Owner限制、StopOldest；Cue音量1、通知音高/音量1。均可在编辑器调参，**不是原作混音真值**。

定向原生测试：`MHGZ.Maintenance.Audio.Swing.InstanceResolutionAndLifecycle`，覆盖实际Commit、实例/提交态校验、软覆盖/硬默认、两动作隔离、重复事件、旧段兼容、取消与迟到回调。最终报告`Saved/Audio/RiseIG/M2/TestsFinal/index.json`为1项成功、0失败，含既有transient harness警告；不是扬声器听感证明。离线49项通过。增量构建成功，独立音频回载Commandlet 0错误/0警告。

诊断可开`mhgz.Audio.SwingDebug 1`，日志区分无精确实例上下文、解析跳过、已派发及音源/组件；关回0即可。`dispatched=1`仅表明UE创建音频组件，仍不代表玩家听到了。E2只需正常速度突刺空挥/命中、快速派生和取消，确认可听同步/无额外重复；不要求全量录制。命中声仍未接入。

本轮失败已定位并保留证据：首次编译为访问私有武器视觉查询，改为复用只读访问器后构建通过；首次资产回读把本地导出对象名误当完整路径，已修正。音频`-nosound`会跳过BINKA解码器注册，曾产生handled ensure但未损坏音源；换`-AllowCommandletAudio`后独立回载成功，没有重导入6个已有包。初次失败时GA/Montage尚未保存，已核对原哈希再续接。`failure.json`是历史失败，当前状态看`manifest.phase=independently_reloaded`及`audit.linked=true`。

M2后原候选表中的TuCi/GA包哈希已过时，这是预期的声音接线变更。M3需要新只读快照或按结构核对，不能绕过旧快照的stale检查；候选表的`runtime_ready=false`仍表示全覆盖尚未完成。

## M2通用武器命中声（已保存/回载，待耳听）

`setup_weapon_hit_audio.py`补齐小样未负责的真实接触音；复用小样工厂/所有权保护、反射和原生导出，不用UI或新编辑器桥接。只创建`SW_IG_MingZhong_1932771116`、`SC_IG_MingZhong`、`SA_IG_MingZhong`、`SCN_IG_MingZhong`和`GCN_Hit_WeaponSound`五包。已有音源核对哈希不重导，所有权不符拒绝覆盖；85个既有GA/Montage/声音/Cue包受哈希保护。

```powershell
# 编辑器打开：默认只读；首次明确创建才额外传--apply
& 'C:/apps/anaconda/python.exe' Scripts/ue_remote.py Scripts/Audio/setup_weapon_hit_audio.py
# 编辑器关闭：默认只读；首次明确创建才加-MHGZHitAudioApply
& 'C:/apps/UE5/UE_5.6/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'D:/study/MH/MHGZ/MHGZ.uproject' -run=pythonscript -script='D:/study/MH/MHGZ/Scripts/Audio/setup_weapon_hit_audio.py' -unattended -nop4 -nosplash -NullRHI -AllowCommandletAudio
```

来源SHA/配置见[映射第13节](../../docs/reference/虫棍音效资源映射.md)。处理器从真实Weapon Context确认目标/组件/ImpactPoint，不依赖扣血、物理火花Tag或动画窗口；未命中不响，猎虫/粉尘排除，不按整招去重。全局并发16/StopOldest；3D内半径500cm/衰减2500cm，音量/音高1；只是一种项目通用主音。

首次在Python未暴露`GameplayCueName`处停下，先核对4个声音包已保存、GCN未保存，再仅补缺项。名称由UE原生`Serialize/PostEditChangeProperty`按GCN名称派生；资产注册表及实际CueSet均验证唯一处理器。历史`WeaponHit/failure.json`保留，当前看`manifest.phase=independently_reloaded`及`audit.linked=true`。没有重复导入音源或覆盖原攻击资产。

增量构建成功，`MHGZ.Maintenance.Audio.Hit.*`4项及直接结算2项共6/6通过，报告`Saved/Audio/RiseIG/M2/HitTestsFinal/`；较宽初检2项数值断言失败保留，见[current-status](../../docs/using/current-status.md)，不宣称全部伤害测试通过。Python编译及复用小样5项保护测试通过。没有全量回归。

接下来只需局部PIE空挥/命中/多段听感，不用全量录制。`mhgz.Audio.HitDebug 1`日志输出目标/音源/ImpactPoint/组件派发，0关闭；`dispatched=1`不替代耳听签收。

## M3录制支持的逐动作挥击（已保存/独立回载）

本次相关快照/已有3份录制形成24个Montage/70点、22个GA的冻结计划；新增68点和3个Wave/3个Cue，原突刺2点/两包不改。实际只改23个Montage/21个GA，GA仅音频开关/身份；原87条通知作者字段、轨道及动画结构保护，113个其它追踪包不变。全部5种人工音源均参与，不按伤害段数配置。完整范围、互斥入口计数和未录片段见[映射第14节](../../docs/reference/虫棍音效资源映射.md)。

工具复用小样的工厂/保存/所有权、原生导出/解析及字段保护，不新增C++或UI桥接：

- `plan_swing_coverage.py`：普通Python，只读新`SwingMap/{asset-snapshot,positions}.json`及已有试听；验证源/录制/包哈希后冻结`Saved/Audio/RiseIG/M3/plan.json`，输出所需Tag。已有不同计划拒绝覆盖。当前已成功，不再用接线前快照重跑。
- `setup_swing_coverage.py`：默认审计。首次`--apply`/`-MHGZSwingCoverageApply`才按冻结范围写；逐包备份、检查点和原字段校验，已有源只读不重导，部分完成不复制通知。当前manifest已成功，日常只审计。
- `-MHGZSwingCoverageDiagnose`：仅收集全24包/22个CDO的当前原生文本，不接受差异、不写Content，解决逐个失败导致重复诊断。
- `test_swing_coverage.py`：16项纯离线检查；加采样位置19项/小样保护5项共40项。

```powershell
# 编辑器打开：默认只读
& 'C:/apps/anaconda/python.exe' Scripts/ue_remote.py Scripts/Audio/setup_swing_coverage.py
# 编辑器关闭：只读独立回载，音频使用-AllowCommandletAudio，不带-nosound
& 'C:/apps/UE5/UE_5.6/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'D:/study/MH/MHGZ/MHGZ.uproject' -run=pythonscript -script='D:/study/MH/MHGZ/Scripts/Audio/setup_swing_coverage.py' -unattended -nop4 -nosplash -NullRHI -AllowCommandletAudio
& 'C:/apps/anaconda/python.exe' -m unittest discover -s Scripts/Audio -p 'test_swing*.py' -q
```

原候选/原M2/命中试点manifest中的保护哈希现在部分过时，不为“修审计”重新Apply它们；它们保留历史基线。本轮续接看`M3/manifest.phase=independently_reloaded`、`audit.linked=true`与每包检查点；新的用户改动若改变受保护包，先按新事务审计，不把旧hash重置成当前值绕过校验。

独立回载曾发现两类原生缓存变化，未重写资产：飞身/后撤绝对链接`SegmentLength 1.783334→1.783333`，后撤两个同一绝对末帧EndLink被引擎清空片段缓存。依据本机`FAnimLinkableElement::GetTime/RefreshSegmentOnLoad`，只允许<=1.1us缓存刷新及单片段同一绝对末帧/精确无效标记，LinkValue/Duration/GUID/轨道/顺序仍严格比较，Relative/Proportional（含CachedLinkMethod）不豁免。最终audit保留14项缓存变化；历史failure仅代表之前的严格缓存误报，不是当前失败状态。相关回归断言包含实际时间变动即失败，不能笼统忽略EndLink。

本轮40项离线、5项原生音频测试通过，后者挥击harness有4条既有警告；最终回载Commandlet为0错误/0警告。无C++编译或全量验证。当前仍需局部PIE耳听；缺少观测的动作/分支不猜填，命中声仍用通用候选。只读命中条件接口核查及下一项采集合同见[计划1.7](../../docs/using/崛起虫棍攻击与命中音效实施计划.md)，未部署新Lua/DLL，不要求现在重录。

## 历史资源工具限制

- GitHub API经当前网络返回403，发布附件下载遇TLS/证书吊销查询及重定向连接问题；已使用官方固定标签Git源码与README列出的维护者原始文件地址，不再反复下载同一失败附件，不修改机器代理或证书设置。
- wwiser的`-go`使用相对于银行工作目录的路径；绝对路径会使生成的媒体引用越层，文档示例的`/*`在本机也被当成字面目录。当前入口固定cwd为Banks、用`../Events/TXTP-<run>`解决，不修改第三方源码。
- 生成器报告multitracks限制，且部分名称/状态仅有数值ID；不能将TXTP渲染视为完全等价游戏运行时。WAV保留wwiser自动事件增益，没有额外响度归一化。
- 已选20份银行的139个组合依赖完整，其余350个组合未纳入解码，不把它们当作损坏。对应媒体大多已在其他型号银行定位；另有两个MediaID在本轮有限扫描中未定位，详见映射。
- 调用外部工具时设置超时并保存错误；本轮最终19份候选渲染错误清单为空。素材含义/音色仍需E1试听，不以“非静音”替代人工确认。

上述离线工具不扩展到全量Wwise解包、游戏录制或UE接入。1.15的仅元数据探针及1.16的包装入口监听是历史尝试，当前采集以已验证的1.17原生桥为准，见[NativeBridge说明](NativeBridge/README.md)。不把旧Lua零行报告当作新原生录制失败，不重复要求重启/重录。
