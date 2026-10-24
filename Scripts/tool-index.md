# 维护工具发现索引

更新：2026-10-13。此索引用于先找已有工具，避免重复实现；不授权执行全部工具。环境来自脚本依赖，简述来自文件头。具体参数、读写范围和前置条件仍以脚本为准；常用且已核验的调用见[README](README.md)。历史迁移/退役工具不可自动重跑。

工具新增、改名或退役时更新本表。普通分析脚本可能写`Saved`报告；`UE Python`不代表只读资产。

中文检索提示：摄像头→camera；准心/界面→ui、hud；猎虫/虫印/虫粉→kinsect、powder；曲线/位移→curve、trajectory、motion；急袭突刺→descending_thrust、aerial_dive；撑杆跳→vault；蓝图接线→anim_graph、AnimBlueprint、Graph。

## Audio

Combat兼容清理回查：同一`migrate_powder_vortex_config.py`入口加`-MHGZCombatCleanup`；只重存Combat的首次Apply已完成并禁止重放，66字段删除、现用值/37保护包保持，证据`Saved/Maintenance/CombatCleanup20261024/`。清理后的旧迁移只读入口须按精确的before→after证明识别本次Combat变更，不能宽放其它包或旧聚爆档位漂移；无Saved旧事务时不重建旧Schema。

单招参数批量迁移/回查：[AssetOrganization/migrate_ig_action_config.py](AssetOrganization/migrate_ig_action_config.py)，由现有`migrate_powder_vortex_config.py -MHGZActionConfig`调度。10 GA/38字段赋值已保存回载；默认只读，首次显式`-MHGZActionConfigApply`已完成且禁止重放。排除虫印弹与共享规则；Combat及28份保护包不变，证据`Saved/Maintenance/IGActionConfig20261024/ActionDefaults/`。使用原生结构导出+精确反射子字段避免浮点舍入，不重跑历史全局动作值/曲线Apply。

当前聚爆GA配置迁移/回查：[AssetOrganization/migrate_powder_vortex_config.py](AssetOrganization/migrate_powder_vortex_config.py)，UE Python，默认只读来源/回载；唯一显式写入开关`-MHGZPowderVortexConfigApply`，仅首次保存`GA_IG_PowderVortex`。十项配置/音频从旧Combat冻结源迁入，第一段数字现为动作值；逐次快照由Resource持有。已有`applied.json`禁止重放；保留GA/Combat备份、非聚爆CDO文本与Combat二进制hash。普通粉尘MV/CD仍在Combat，旧全局动作值迁移不适用于此任务。

| 最新分支入口 | 环境 | 用途 | 状态 |
|---|---|---|---|
| [Audio/audio_notify_tracks.py](Audio/audio_notify_tracks.py)的cloth-only模式 | 现有`setup_current_weapon_audio --cloth-tracks`／`-MHGZAudioClothTracks` | 同帧衣服声分配独立ClothAudio副轨；从完整原生轨道数组读取索引，避免API去重/加载自动错轨 | 6包11点已保存回载，16项规则检查；不改时间/声音/Gameplay，不重放Apply；证据`ClothTracks20261023/StableLanes/` |
| [Audio/rise_audio_cloth_comparison.py](Audio/rise_audio_cloth_comparison.py)、[Audio/setup_powder_audio.py](Audio/setup_powder_audio.py) | 前者由`map_swing_positions --aerial-powder`/`prepare_current_weapon_audio --powder-reviewed`调用；后者UE | 1.37复用四类装备证据补空中空点、四项粉尘/聚爆试听与6变体；既有工厂生成常驻源/配置 | 已保存回载：11 Montage/77空点；6 Wave/4 Cue/独立并发/3 DA音频字段。只读续接`setup_current_weapon_audio --aerial-cloth-placeholders`或`--powder-audio`，不重放Apply；未PIE耳听 |
| [Audio/audio_section_maintenance.py](Audio/audio_section_maintenance.py)、[规则](Audio/section_audio_rules.py) | 由既有`setup_current_weapon_audio.py --section-maintenance --source-time-repair`调用；规则为普通Python | 1.36按Native Present与Lua动画关联后重算源帧→源片段位置；复用受GUID/旧时间保护的作者桥接 | 已独立回载：6 Montage改时12点/移除1点；旧1.35的5补点已撤回，禁止重放旧Apply；证据`SourceTime20261022/`，未PIE耳听 |

| 定向补充入口 | 环境 | 用途 | 状态 |
|---|---|---|---|
| [Audio/prepare_ground_audio.py](Audio/prepare_ground_audio.py)、[Audio/setup_ground_audio.py](Audio/setup_ground_audio.py) | 前者普通Python；后者由现有`setup_current_weapon_audio.py`调度 | 最新`--descending-thrust`按曲线/参考阶段补急袭突刺9点并启用GA声音；此前`--ground-impact`/`--landing-events`/`--ground-events`处理砸地/落地/扬尘与棍花 | 最新`DescendingThrustAudio20261021/`已保存/独立回载，仅3包，默认只读，不重放Inspect/prepare/Apply；未PIE耳听 |
| 同入口 `--dust-volume` / `-MHGZAudioDustVolume` | UE Python；远程或Commandlet | 仅将扬尘Cue音量设为1.0，备份并保护其它声音图/设置 | 已保存/独立回载；证据`DustVolume20261021/`；默认只读，不重放旧MixRecall或本次Apply |
| 同入口 `--landing-events` / `-MHGZAudioLandingEvents` | UE Python；远程或Commandlet | 10 Montage/10个落地声迁为事件，保留原通知及独立扬尘；支持可选同帧扬尘 | 已保存/独立回载；另43个同范围Montage不变；证据`LandingEvents20261021/`，默认审计，不重放Apply或旧冻结事务 |

| 脚本 | 环境 | 用途 | 执行边界 |
|---|---|---|---|
| [Audio/rise_audio_m1.py](Audio/rise_audio_m1.py) | 普通Python；parse/试听另需便携wwiser和vgmstream | 崛起虫棍有限银行索引、事件/媒体依赖、WAV试听及元数据清单 | 原始输出只写Saved；publish显式写reference JSON；不写Content/游戏安装。先看[说明](Audio/README.md) |
| [Audio/test_rise_audio_m1.py](Audio/test_rise_audio_m1.py) | 普通Python unittest | 银行边界、DIDX、TXTP路径与PCM16定向检查 | 无UE/游戏/网络；只用Saved临时夹具 |
| [Audio/rise_audio_capture_map.py](Audio/rise_audio_capture_map.py) | Python＋已固定wwiser/vgmstream | 原生录制→事件/条件→有限试听；可仅刷新元数据及补单事件分支对照 | 默认只写Saved；`--publish`发布reference，`--refresh-metadata`保留既有音频，`--compare-event`显式有限解码；不猜运行时分支 |
| [Audio/test_rise_audio_capture_map.py](Audio/test_rise_audio_capture_map.py) | 普通Python unittest | 19项映射/输出隔离/条件头/媒体切片/分支对照及有界用户签收检查 | 无UE/游戏/网络；只用Saved临时夹具 |
| [Audio/audit_swing_assets.py](Audio/audit_swing_assets.py) | UE Python；编辑器远程或关闭时Commandlet | 只读52个虫棍Montage、26个GA、引用序列及Combo入口，保留裁切/倍率/Section快照 | 不保存/编译Content；导出Saved/SwingMap并核对包哈希，详见Audio说明 |
| [Audio/map_swing_positions.py](Audio/map_swing_positions.py) | 普通Python；已有资产快照＋录制 | `--map-current-weapon`当前主映射；`--compare-cloth`显式无装备对照，验证静音插件/同源时机；原模式保留 | 衣服模式只写新Saved，不改旧主映射/UE/录制器；参数见Audio README |
| [Audio/rise_audio_cloth_comparison.py](Audio/rise_audio_cloth_comparison.py)、[测试](Audio/test_cloth_comparison.py) | 上述对照/空中补充模式调用；普通Python | 复用归属/共享Present时钟/wwiser；8项定向检查；空中模式只生成四项粉尘试听，reviewed模式复用解码器导出实际变体 | 补充只写Saved；装备层未再细分布料/金属，未覆盖/歧义源不写空点，不猜未知539 |
| [Audio/rise_audio_weapon_mapping.py](Audio/rise_audio_weapon_mapping.py) | 普通Python；由映射及推进声显式模式调用 | 条件图/源帧→候选；限定事件同帧FX链、末帧保持、137入场前唯一请求识别，14项专项检查 | 不放宽通用归属/不把适配当原作精确帧；137为空回、156为白灯前跳Over，不能按请求当帧一刀切 |
| [Audio/test_current_weapon_mapping.py](Audio/test_current_weapon_mapping.py) | 普通Python unittest | 17项分支排除、对象/条件生命周期、真实元组、歧义与Loop时机检查 | 无UE/游戏/全银行解码；与19项原时序检查合计36项定向检查 |
| [Audio/prepare_current_weapon_audio.py](Audio/prepare_current_weapon_audio.py) | 普通Python；冻结当前Mapping及作者基线 | 默认源池/武器/动作；`--propulsion`准备推进；`--kinsect-contact --capture-directory`委托prepare_kinsect_contact_audio复用源池补精确猎虫归属/伴随证据 | 仅Saved；计划不覆盖/重解码，不写Content/Config；同期关系不当精确事务或全部贯通真值 |
| [Audio/setup_current_weapon_audio.py](Audio/setup_current_weapon_audio.py) | UE Python远程或Commandlet，默认审计 | 最新`--inspect-aerial-audio`新鲜路由检查、`--repair-aerial-audio`／`-MHGZAudioRepairAerial`回查5包26点的窄修复，继续复用setup_propulsion_audio；旧模式保留 | 首次Apply已完成拒绝重放；原窗口/声音/混音保护，43 Montage/422点；旧冻结hash不作本轮全局审计 |
| [Audio/test_kinsect_contact_audio.py](Audio/test_kinsect_contact_audio.py) | 普通Python unittest | 6项精确猎虫对象/重复伴随、错误归属/指针复用/稀疏歧义/未知事件隔离 | 不启动UE/游戏，不把时间窗当因果命中配对；原生检查在Audio.KinsectHit/Hit |
| [Audio/audio_notify_tracks.py](Audio/audio_notify_tracks.py) | 上述维护入口加载，UE Python | 原位按五种类分轨，29包54点仅换轨；复用现有编辑器桥接/快照/备份/配方验证 | 不重建/删除/排序通知，不删旧轨；387点保持、100衣服点仍无音源；默认审计 |
| [Audio/audio_spatial_maintenance.py](Audio/audio_spatial_maintenance.py) | 由上述入口加载的UE事务模块 | 复用导出/保护/工厂；最新13包追加100个空音源衣服通知，原287有音源点保留；此前分类/混音留历史 | 13包备份、382其它包不变；原GUID/顺序/窗口保护；不删音源，不重放旧纠正入口 |
| [Audio/spatial_audio_rules.py](Audio/spatial_audio_rules.py)、[空间测试](Audio/test_spatial_audio_rules.py)、[撑杆跳配方测试](Audio/test_aerial_audio_repair.py) | 普通Python | 声源/五类轨道/保护12项；同动作阶段源帧转换3项，只允许155→141、156→142补普通前跳初始配方 | 不按整段时长拉伸，不强塞裁切外点；适配不当原作精确真值，不给衣服填声、不放宽玩法保护 |
| [Audio/test_current_weapon_audio.py](Audio/test_current_weapon_audio.py) | 普通Python unittest | 23项源图/同源/路由/边界/重复及声音GUID、动作分层和CombatConfig音频白名单保护 | 无游戏/UE；与17项当前映射、40项原保护合计80项定向检查 |
| [Audio/rise_audio_coverage.py](Audio/rise_audio_coverage.py) | 普通Python；由现有入口调用 | 跨录制覆盖辅助模块；复用主节点/路由/combat审计，区分共用源、边界和音层 | 不独立启动UE/游戏/解码器，不是新音频解析器；输入前后哈希保护 |
| [Audio/test_audio_coverage.py](Audio/test_audio_coverage.py) | 普通Python unittest | 12项独立命中/声链、共用源、缺前缀、世界动作、入口和Markdown列数检查 | 无UE/游戏/网络；不写Content |
| [Audio/test_swing_positions.py](Audio/test_swing_positions.py) | 普通Python unittest | 19项采样边界、逐序列长度校核、裁切/倍率/重复片段、入口跳转与循环检查 | 无UE/游戏/网络；不写Content |
| [Audio/setup_swing_pilot.py](Audio/setup_swing_pilot.py) | UE Python；远程或Commandlet | M2突刺两个挥击通知＋2个Wave/2个Cue/衰减/并发接线；原GA/Montage先备份，独立回载审计 | 默认只读；`--apply`或`-MHGZSwingPilotApply`写明确8包。音频Commandlet需`-AllowCommandletAudio`且不用`-nosound`；不重导入已有音源，不改伤害/原通知 |
| [Audio/test_swing_pilot_contract.py](Audio/test_swing_pilot_contract.py) | 普通Python unittest | 5项实际资产保护函数检查：GUID/顺序/时间/轨道、片段/Section和GA非音频字段 | 无UE；当前音频离线共49项，不等同PIE |
| [Audio/setup_weapon_hit_audio.py](Audio/setup_weapon_hit_audio.py) | UE Python；远程或Commandlet | M2真实武器命中Wave/Cue/衰减/并发及GCN，共5个常驻包；唯一注册/引用/独立回载审计 | 默认只读；`--apply`或`-MHGZHitAudioApply`才创建；复用小样工厂/所有权保护，85个既有包哈希不变；音频Commandlet不用`-nosound` |
| [Audio/plan_swing_coverage.py](Audio/plan_swing_coverage.py) | 普通Python；已有新快照/position报告 | M3冻结24个Montage/70点/22个GA、5份人工音源及来源哈希；输出所需Tag | 只写Saved/M3；已有计划不同拒绝覆盖，缺证据不填声；当前已有成功检查点，不重跑旧快照 |
| [Audio/setup_swing_coverage.py](Audio/setup_swing_coverage.py) | UE Python；远程或Commandlet | M3录制支持的逐动作声音接线、备份/断点/原字段保护及独立回载 | 默认只读；`--apply`/`-MHGZSwingCoverageApply`写冻结配方；`-MHGZSwingCoverageDiagnose`只收集差异；成功后只审计，不重复生成 |
| [Audio/test_swing_coverage.py](Audio/test_swing_coverage.py) | 普通Python unittest | 配方、重复事件/入口、缺项、轨道、真实窗口严格保护及两个已验证原生缓存例外 | 16项离线定向；同test_swing*.py共40项；不启动UE/游戏，不改Content |
| [Audio/NativeBridge](Audio/NativeBridge/README.md) | MSVC x64/MASM、固定MinHook | 1.21对象专用MPSC序号环＋分项丢失；保留对象键/条件/Post/命中边；3项CTest | 三项修复实采通过；保留池复用未知，不猜最终Media，不重复短录 |
| [Audio/rise_audio_combat.py](Audio/rise_audio_combat.py) | 普通Python、既有wwiser | 由capture_map的`--audit-combat`调用；精确身份/作用域与实际根容器默认审计 | 10项检查通过；输出仅Saved，不解码/发布/覆盖试听 |
| [Audio/NativeBridge/probe_native_audio.py](Audio/NativeBridge/probe_native_audio.py) | Windows Python、隔离Capstone | 有界只读反汇编/队列引用；`--audio-backend-callers`列固定448KiB调用，`--audio-object-writers`列RequestInfo+600写入 | 仅读指定Rise进程及代码/异常表；无对象搜索、写内存/注入/执行；输出仅Saved |

## AssetOrganization

| 最新入口 | 环境 | 用途 | 状态/边界 |
|---|---|---|---|
| [AssetOrganization/setup_kinsect_coattack_pilot.py](AssetOrganization/setup_kinsect_coattack_pilot.py) | UE Python；远程或关闭编辑器时Commandlet | 默认突刺；`--ground-five`/`--ground-second-five`分别回查两批普通地面协同；`--descending-thrust`（`-MHGZCoAttackDescendingThrust`）只读核对急袭特殊协同原生默认值/实际地面末窗/共享间隔 | 地面三批已保存/独立回载，已有intent禁止重放Apply；急袭模式拒绝Apply、不改通知或保存Content，证据`E1DescendingThrust/`；保留各批保护hash链，新增范围未PIE签收 |

| 工具 | 环境 | 用途简述 | 状态 |
|---|---|---|---|
| [AssetOrganization/setup_player_initial_attributes.py](AssetOrganization/setup_player_initial_attributes.py) | UE Python | 创建可编辑的出生血耐初始化GE并接入BP_PlayerState.CoreAttributeEffects；保留其他默认值和已有GE。 | 默认独立回查；首次Apply备份PlayerState，不覆盖已有GE；日后调参直接编辑资产 |
| [AssetOrganization/apply_damage_formula_units.py](AssetOrganization/apply_damage_formula_units.py) | UE Python | Apply the insect-glaive damage-unit migration to project assets. | 历史迁移/排错；勿自动重跑 |
| [AssetOrganization/repair_locomotion_boundaries.py](AssetOrganization/repair_locomotion_boundaries.py) | UE Python | 四条Loop首尾闭合及两份专用PSS入口速度特征；复用C++桥接，保留时长/累计位移/曲线/通知。 | 默认只读验证；Apply仅一次，先备份，拒绝覆盖已有事务 |
| [AssetOrganization/audit_animation_assets.py](AssetOrganization/audit_animation_assets.py) | UE Python | Write a read-only Unreal animation inventory to Saved/AssetOrganization. | 按需；先核对参数/写入范围 |
| [AssetOrganization/audit_asset_paths.py](AssetOrganization/audit_asset_paths.py) | UE Python | Read-only audit for specific Unreal package paths. | 按需；先核对参数/写入范围 |
| [AssetOrganization/audit_damage_formula_units.py](AssetOrganization/audit_damage_formula_units.py) | UE Python | Read-only audit of insect-glaive damage and physical hitzone values. | 按需；先核对参数/写入范围 |
| [AssetOrganization/audit_m6_ui_widgets.py](AssetOrganization/audit_m6_ui_widgets.py) | UE Python | Deprecated: UE 5.6 Python does not expose WidgetBlueprint.widget_tree. | 已弃用；改用probe_m6_ui.py |
| [AssetOrganization/organize_asset_roots.py](AssetOrganization/organize_asset_roots.py) | UE Python | Move complete Unreal asset roots with count and backup safeguards. | 按需；先核对参数/写入范围 |
| [AssetOrganization/organize_assets.py](AssetOrganization/organize_assets.py) | UE Python | Dry-run or apply an Unreal asset migration manifest. | 按需；先核对参数/写入范围 |
| [AssetOrganization/probe_m6_ui.py](AssetOrganization/probe_m6_ui.py) | UE Python | Read-only M6 UI asset/reference probe; does not inspect unexposed WidgetTree. | 按需；先核对参数/写入范围 |
| [AssetOrganization/run_m6_montage_pinyin_migration.py](AssetOrganization/run_m6_montage_pinyin_migration.py) | UE Python | Run the reviewed M6 Montage rename manifest through the shared UE asset mover. | 历史迁移/排错；勿自动重跑 |
| [AssetOrganization/run_m6_ui_remote.py](AssetOrganization/run_m6_ui_remote.py) | 普通Python | Compatibility entry point for the M6 UI setup and read-only probe. | 按需；先核对参数/写入范围 |
| [AssetOrganization/run_weapon_move_asset_pinyin_migration.py](AssetOrganization/run_weapon_move_asset_pinyin_migration.py) | UE Python | Run the reviewed weapon-move asset rename manifest through AssetTools. | 历史迁移/排错；勿自动重跑 |
| [AssetOrganization/setup_m6_ui_widgets.py](AssetOrganization/setup_m6_ui_widgets.py) | UE Python | Create and wire the E6.3 HUD Widget Blueprints in UE 5.6. | 按需；先核对参数/写入范围 |
| [AssetOrganization/setup_montage_segment_blend.py](AssetOrganization/setup_montage_segment_blend.py) | UE Python | 默认审计Slot惯性混合；--configure-montages配置0.05s；--weapon-grip接入ABP并配置飞圆310→117持械约束；--remove-synthetic-bridges移除5个桥接；--remove-attack-holds移除10个攻击保持并更新4份GA入口；对应verify只读回查 | 默认只读；显式Apply才备份保存；检查点阻止重复；不删除序列、不动翻滚保持/蓄力 |
| [AssetOrganization/apply_ground_attack_entries.py](AssetOrganization/apply_ground_attack_entries.py) | UE Python | 六组地面入口显式配方，原相位通知重定位及308副本；保留突进蓄力/未点名来源 | 已应用；默认/已应用时仅审计，勿删除检查点盲目重跑 |
| [AssetOrganization/trigger_mhgz_livecoding_compile.py](AssetOrganization/trigger_mhgz_livecoding_compile.py) | UE Python | Ask the open MHGZ editor to compile and apply its Live Coding patch. | 按需；先核对参数/写入范围 |
| [AssetOrganization/verify_demo_structure.py](AssetOrganization/verify_demo_structure.py) | 普通Python | Verify the Git-tracked Content scaffold required by the combat demo. | 按需；先核对参数/写入范围 |
| [AssetOrganization/verify_project_assets.py](AssetOrganization/verify_project_assets.py) | UE Python | Verify the current finalized Demo asset layout and compile project Blueprints. | 按需；先核对参数/写入范围 |

## AssetProbe

| 工具 | 环境 | 用途简述 | 状态 |
|---|---|---|---|
| [AssetProbe/adjust_demo_arena_sunlight.py](AssetProbe/adjust_demo_arena_sunlight.py) | UE Python | Lower only the Demo Arena's directional sun intensity, with guarded readback. | 按需；先核对参数/写入范围 |
| [AssetProbe/analyze_kinsect_over_pose.py](AssetProbe/analyze_kinsect_over_pose.py) | UE Python | Read-only pose-cycle analysis for AS_Unsh_CaoChongZhan_Over. | 按需；先核对参数/写入范围 |
| [AssetProbe/analyze_kinsect_piercer_telemetry.py](AssetProbe/analyze_kinsect_piercer_telemetry.py) | 普通Python | Read-only Kinsect Piercer PIE audit; output evidence, not an automatic sign-off. | 按需；先核对参数/写入范围 |
| [AssetProbe/audit_descending_thrust_m1_poses.py](AssetProbe/audit_descending_thrust_m1_poses.py) | UE Python | Read-only M1 audit of current dive assets and existing falling-loop options. | 按需；先核对参数/写入范围 |
| [AssetProbe/audit_descending_thrust_runtime_loop_seam.py](AssetProbe/audit_descending_thrust_runtime_loop_seam.py) | UE Python | Read-only pose audit for the runtime loop observed in MHW ActionNo 14. | 按需；先核对参数/写入范围 |
| [AssetProbe/audit_kinsect_anim_graph.py](AssetProbe/audit_kinsect_anim_graph.py) | UE Python | Read-only dump of ABP_MH_Character AnimGraph node/pin wiring. | 按需；先核对参数/写入范围 |
| [AssetProbe/audit_kinsect_piercer_combo_entries.py](AssetProbe/audit_kinsect_piercer_combo_entries.py) | UE Python | Read-only audit of copied TuCi combo routes and target montage entry selection. | 按需；先核对参数/写入范围 |
| [AssetProbe/audit_kinsect_slash_collision_windows.py](AssetProbe/audit_kinsect_slash_collision_windows.py) | UE Python | Read-only inspection of the saved Kinsect Slash montage windows and config. | 按需；先核对参数/写入范围 |
| [AssetProbe/audit_kinsect_slash_movement_profile.py](AssetProbe/audit_kinsect_slash_movement_profile.py) | UE Python | Read saved Kinsect Slash defaults after a cold build; never save assets. | 按需；先核对参数/写入范围 |
| [AssetProbe/audit_m6_baseline.py](AssetProbe/audit_m6_baseline.py) | UE Python | Read-only M6.1 baseline audit for weapon wiring and Insect Glaive assets. | 按需；先核对参数/写入范围 |
| [AssetProbe/audit_mhw_aerial_dive_finger_retarget.py](AssetProbe/audit_mhw_aerial_dive_finger_retarget.py) | UE Python | Audit source and retargeted finger poses for MHW aerial-dive review clips. | 按需；先核对参数/写入范围 |
| [AssetProbe/audit_mhw_aerial_dive_root_motion.py](AssetProbe/audit_mhw_aerial_dive_root_motion.py) | UE Python | Read source/target hierarchy motion without changing Unreal content. | 按需；先核对参数/写入范围 |
| [AssetProbe/audit_mhw_aerial_dive_weapon_pose.py](AssetProbe/audit_mhw_aerial_dive_weapon_pose.py) | UE Python | Compare MHW LMT381's final weapon/grip pose with native MHR idle poses. | 按需；先核对参数/写入范围 |
| [AssetProbe/configure_idle_vault_and_thrust_routes.py](AssetProbe/configure_idle_vault_and_thrust_routes.py) | UE Python | Update Idle Insect Glaive pole-vault and thrust Combo routes. | 按需；先核对参数/写入范围 |
| [AssetProbe/configure_training_dummy_hit_coverage.py](AssetProbe/configure_training_dummy_hit_coverage.py) | UE Python | Migrate only DA_TrainingDummy query geometry. Default is read-only. | 按需；先核对参数/写入范围 |
| [AssetProbe/create_mhw_aerial_dive_retarget_setup.py](AssetProbe/create_mhw_aerial_dive_retarget_setup.py) | UE Python | Create a reusable UE 5.6 IK Retarget setup for MHW dive 380/381. | 按需；先核对参数/写入范围 |
| [AssetProbe/create_mhw_aerial_dive_scale_variants.py](AssetProbe/create_mhw_aerial_dive_scale_variants.py) | UE Python | RETIRED: source-scale variants are no longer part of this review. | 已退役；入口拒绝执行 |
| [AssetProbe/ensure_kinsect_piercer_combo_entry_mappings.py](AssetProbe/ensure_kinsect_piercer_combo_entry_mappings.py) | UE Python | Mirror explicit Montage entry mappings for copied Kinsect Piercer Combo routes. | 按需；先核对参数/写入范围 |
| [AssetProbe/fix_a7_dance_vault.py](AssetProbe/fix_a7_dance_vault.py) | UE Python | A7: retune the dance vault to the MHRise ground truth. | 按需；先核对参数/写入范围 |
| [AssetProbe/fix_a9_root_lock.py](AssetProbe/fix_a9_root_lock.py) | UE Python | A9: lock the JumpOver root tracks so their pose lead cannot snap back. | 按需；先核对参数/写入范围 |
| [AssetProbe/inspect_camera_defaults.py](AssetProbe/inspect_camera_defaults.py) | UE Python | Read the live Blueprint CDO camera values without editing assets. | 按需；先核对参数/写入范围 |
| [AssetProbe/inspect_character_skeletons.py](AssetProbe/inspect_character_skeletons.py) | UE Python | Read-only Unreal Python probe for project and MHW review skeletons. | 按需；先核对参数/写入范围 |
| [AssetProbe/inspect_demo_arena.py](AssetProbe/inspect_demo_arena.py) | UE Python | Read actor placement, bounds, collision, materials, and lights in L_DemoArena. | 按需；先核对参数/写入范围 |
| [AssetProbe/inspect_ik_retarget_python_api.py](AssetProbe/inspect_ik_retarget_python_api.py) | UE Python | Print the exposed UE 5.6 Python API used to build a review retarget setup. | 按需；先核对参数/写入范围 |
| [AssetProbe/inspect_kinsect_piercer_root_motion.py](AssetProbe/inspect_kinsect_piercer_root_motion.py) | UE Python | Read-only source-root and AnimBP audit, run with Unreal Python commandlet. | 按需；先核对参数/写入范围 |
| [AssetProbe/inspect_mhw_aerial_dive_retarget_outputs.py](AssetProbe/inspect_mhw_aerial_dive_retarget_outputs.py) | UE Python | Inspect retargeted MHW dive assets and UE Python animation editing APIs. | 按需；先核对参数/写入范围 |
| [AssetProbe/inspect_mhw_root_helpers.py](AssetProbe/inspect_mhw_root_helpers.py) | UE Python | Read-only compare Root helper bones in native MHR and MHW retarget bakes. | 按需；先核对参数/写入范围 |
| [AssetProbe/probe_kinsect_over_pose_api.py](AssetProbe/probe_kinsect_over_pose_api.py) | UE Python | 见文件头和参数说明 | 按需；先核对参数/写入范围 |
| [AssetProbe/probe_kinsect_slash_dummy.py](AssetProbe/probe_kinsect_slash_dummy.py) | UE Python | Read-only audit of the live Kinsect Slash and training-dummy asset defaults. | 按需；先核对参数/写入范围 |
| [AssetProbe/probe_m5_assets.py](AssetProbe/probe_m5_assets.py) | UE Python | Read-only asset probe for the M5 aerial/back-vault audit. | 按需；先核对参数/写入范围 |
| [AssetProbe/probe_mhw_aerial_dive_retarget.py](AssetProbe/probe_mhw_aerial_dive_retarget.py) | UE Python | Read-only UE source/target hierarchy and animation-scale diagnostic. | 按需；先核对参数/写入范围 |
| [AssetProbe/probe_vault_seam_blend.py](AssetProbe/probe_vault_seam_blend.py) | UE Python | **采样器**：把撑杆跳接缝两侧的姿势倒成网格，交给离线脚本分析。 | 按需；先核对参数/写入范围 |
| [AssetProbe/probe_vault_seam_pose.py](AssetProbe/probe_vault_seam_pose.py) | UE Python | 量撑杆跳蒙太奇 Jump→JumpOver 接缝处的**姿势落差**。 | 按需；先核对参数/写入范围 |
| [AssetProbe/read_asset_import_ids.py](AssetProbe/read_asset_import_ids.py) | 普通Python | Copyright MHGZ Project. All Rights Reserved. | 按需；先核对参数/写入范围 |
| [AssetProbe/repair_mhw_aerial_dive_root_motion.py](AssetProbe/repair_mhw_aerial_dive_root_motion.py) | UE Python | Repair body and helper tracks of the MHW->MHR review-only FK bake. | 按需；先核对参数/写入范围 |
| [AssetProbe/resize_demo_arena.py](AssetProbe/resize_demo_arena.py) | UE Python | Expand L_DemoArena and turn its boundary walls into invisible blockers. | 按需；先核对参数/写入范围 |
| [AssetProbe/retarget_mhw_aerial_dive_sequences.py](AssetProbe/retarget_mhw_aerial_dive_sequences.py) | UE Python | Create finger-chain-fixed MHW LMT 380/381 review retargets in UE5.6. | 按需；先核对参数/写入范围 |
| [AssetProbe/set_kinsect_slash_range.py](AssetProbe/set_kinsect_slash_range.py) | UE Python | Set the shared B / RT+B dash range in DA_IG_Combat and verify the saved asset. | 按需；先核对参数/写入范围 |
| [AssetProbe/verify_kinsect_piercer_setup.ps1](AssetProbe/verify_kinsect_piercer_setup.ps1) | PowerShell | 见文件头和参数说明 | 按需；先核对参数/写入范围 |
| [AssetProbe/verify_mhw_aerial_dive_fingerfixed_disk.py](AssetProbe/verify_mhw_aerial_dive_fingerfixed_disk.py) | 普通Python | Reload and verify the saved finger-fixed MHW dive review animations. | 按需；先核对参数/写入范围 |
| [AssetProbe/verify_mhw_aerial_dive_root_motion.py](AssetProbe/verify_mhw_aerial_dive_root_motion.py) | UE Python | Read-only RAW/compressed verification of the saved review bake. | 按需；先核对参数/写入范围 |

## MHRise

| 工具 | 环境 | 用途简述 | 状态 |
|---|---|---|---|
| [MHRise/analyze_kinsect_slash_outcomes.py](MHRise/analyze_kinsect_slash_outcomes.py) | 普通Python | Inspect Kinsect Slash hit/miss captures without changing runtime assets. | 按需；先核对参数/写入范围 |
| [MHRise/audit_action_motion_captures.py](MHRise/audit_action_motion_captures.py) | 普通Python | Audit MHRise recorder CSVs for M6 action-ID episodes and frame telemetry. | 按需；先核对参数/写入范围 |
| [MHRise/build_fall_physics.py](MHRise/build_fall_physics.py) | 普通Python | Copyright MHGZ Project. All Rights Reserved. | 按需；先核对参数/写入范围 |
| [MHRise/build_kinsect_piercer_curves.py](MHRise/build_kinsect_piercer_curves.py) | 普通Python | Export an auditable intermediate trajectory dataset for MHRise motion 201. | 按需；先核对参数/写入范围 |
| [MHRise/build_kinsect_piercer_full_curves.py](MHRise/build_kinsect_piercer_full_curves.py) | 普通Python | Rebuild the Enhanced Kinsect Piercer (强化操虫穿刺) 200/201/203 chain curves. | 按需；先核对参数/写入范围 |
| [MHRise/build_kinsect_slash_curves.py](MHRise/build_kinsect_slash_curves.py) | 普通Python | Copyright MHGZ Project. All Rights Reserved. | 按需；先核对参数/写入范围 |
| [MHRise/build_truth_table.py](MHRise/build_truth_table.py) | 普通Python | Copyright MHGZ Project. All Rights Reserved. | 按需；先核对参数/写入范围 |
| [MHRise/build_vault_curves.py](MHRise/build_vault_curves.py) | 普通Python | Copyright MHGZ Project. All Rights Reserved. | 按需；先核对参数/写入范围 |
| [MHRise/discover_action_ids_by_input.py](MHRise/discover_action_ids_by_input.py) | 普通Python | Discover MHRise motion IDs associated with controller input transitions. | 按需；先核对参数/写入范围 |
| [MHRise/MHGZ_AerialTrajectoryRecorder.lua](MHRise/MHGZ_AerialTrajectoryRecorder.lua) | 游戏Lua宿主＋NativeBridge | 1.21旧宿主上下文栈后快照、96字段/容器诊断；原采样不变 | 三项修复实采通过，可继续逐招式映射；完整音效未签收，依赖见MHRise README |
| [MHRise/recorder_path.py](MHRise/recorder_path.py) | 普通Python | Locate the installed MHRise recorder data across development machines. | 公共模块；导入复用 |

## MHRise/tests

音频接口探针定向检查：`python -m unittest discover -s Scripts/MHRise/tests -p test_recorder_audio_probe.py -v`；14项，含真实Lua userdata模拟容器，使用与动画读取器相同的既有隔离lupa依赖。只跑元数据桩，不启动游戏，不证明原生监听可用。

音频监听修复定向检查：`python -m unittest discover -s Scripts/MHRise/tests -p 'test_recorder_audio*.py' -v`；29项监听检查+14项探针检查。覆盖禁用原型查找、实际userdata、歧义/坏描述符、诊断与自动live报告、getter禁用及调用/返回透传；不把模拟通过当真机已安装。

| 工具 | 环境 | 用途简述 | 状态 |
|---|---|---|---|
| [MHRise/tests/test_recorder_animation_reader.py](MHRise/tests/test_recorder_animation_reader.py) | 普通Python + lupa | 执行真实Lua读取器的受限模拟、CSV行构造及Lua5.4/LuaJIT编译 | 定向工具测试；不启动游戏，不代替游戏内API验收 |
| [MHRise/tests/test_action_motion_capture_audit.py](MHRise/tests/test_action_motion_capture_audit.py) | 普通Python | Synthetic tests for MHRise action-ID episode and frame-field auditing. | 定向测试；按需 |
| [MHRise/tests/test_discover_action_ids_by_input.py](MHRise/tests/test_discover_action_ids_by_input.py) | 普通Python | Synthetic tests for controller-input to motion-ID discovery. | 定向测试；按需 |
| [MHRise/tests/test_kinsect_piercer_curves.py](MHRise/tests/test_kinsect_piercer_curves.py) | 普通Python | Pure-function tests for the Enhanced Kinsect Piercer curve extractor. | 定向测试；按需 |
| [MHRise/tests/test_kinsect_piercer_full_curves.py](MHRise/tests/test_kinsect_piercer_full_curves.py) | 普通Python | Tests for full 200 → 201 → 203 curve-chain extraction and canonical curves. | 定向测试；按需 |
| [MHRise/tests/test_kinsect_slash_outcomes.py](MHRise/tests/test_kinsect_slash_outcomes.py) | 普通Python | Offline extraction tests; no Unreal Editor or live game required. | 定向测试；按需 |

## MHWorld

| 工具 | 环境 | 用途简述 | 状态 |
|---|---|---|---|
| [MHWorld/analyze_aerial_dive_trajectory.py](MHWorld/analyze_aerial_dive_trajectory.py) | 普通Python | Read-only numerical audit of MHW aerial dive's runtime 49532 / LMT380 phase. | 按需；先核对参数/写入范围 |
| [MHWorld/audit_aerial_dive_inertia_source.py](MHWorld/audit_aerial_dive_inertia_source.py) | 普通Python | Compare pre-action identity and measured RT-edge speed against dive inertia. | 按需；先核对参数/写入范围 |
| [MHWorld/audit_aerial_dive_model.py](MHWorld/audit_aerial_dive_model.py) | 普通Python | Numerical inspection of the first-phase exports; no game/asset writes. | 按需；先核对参数/写入范围 |
| [MHWorld/audit_descending_thrust_m1_timing.py](MHWorld/audit_descending_thrust_m1_timing.py) | 普通Python | Build a derived timing audit for MHW runtime actions 49532/49533. | 按需；先核对参数/写入范围 |
| [MHWorld/audit_mhw_common_fall_lmt.py](MHWorld/audit_mhw_common_fall_lmt.py) | 普通Python | Inventory MHW LMT actions for possible aerial-fall loop sources. | 按需；先核对参数/写入范围 |
| [MHWorld/audit_mhw_common_fall_pose_cycles.py](MHWorld/audit_mhw_common_fall_pose_cycles.py) | Blender Python | Rank repeated-pose windows in an MHW LMT. | 按需；先核对参数/写入范围 |
| [MHWorld/audit_mhw_fullbody_ue_assets.py](MHWorld/audit_mhw_fullbody_ue_assets.py) | UE Python | Read-only validation of the normalized full-body MHW review assets. | 按需；先核对参数/写入范围 |
| [MHWorld/audit_mhw_rig_coverage.py](MHWorld/audit_mhw_rig_coverage.py) | Blender Python | Read-only audit of MHW armor-part MOD3 bones against WP10 LMT tracks. | 按需；先核对参数/写入范围 |
| [MHWorld/build_aerial_dive_curves.py](MHWorld/build_aerial_dive_curves.py) | 普通Python | Export observed LMT380 curves without averaging different planar inertias together. | 按需；先核对参数/写入范围 |
| [MHWorld/capture_mhw_xinput.py](MHWorld/capture_mhw_xinput.py) | 普通Python | Record raw XInput controller state and publish an atomic live sample. | 按需；先核对参数/写入范围 |
| [MHWorld/enable_blender_mhw.py](MHWorld/enable_blender_mhw.py) | Blender Python | Enable the installed MOD3/LMT add-on and persist Blender user preferences. | 按需；先核对参数/写入范围 |
| [MHWorld/export_aerial_dive_candidates.py](MHWorld/export_aerial_dive_candidates.py) | Blender Python | Export review FBXs from selected MHW WP10 LMT entries. | 按需；先核对参数/写入范围 |
| [MHWorld/export_mhw_fullbody_candidates.py](MHWorld/export_mhw_fullbody_candidates.py) | Blender Python | Build a review-only full-body MHW WP10 candidate rig from armor MOD3 parts. | 按需；先核对参数/写入范围 |
| [MHWorld/fit_aerial_dive_trajectory.py](MHWorld/fit_aerial_dive_trajectory.py) | 普通Python | Fit observations, excluding phase-entry carryover and contact-distorted samples. | 按需；先核对参数/写入范围 |
| [MHWorld/import_aerial_dive_candidates.py](MHWorld/import_aerial_dive_candidates.py) | UE Python | Import the exported MHW aerial-dive review FBXs as isolated UE assets. | 按需；先核对参数/写入范围 |
| [MHWorld/import_mhw_fullbody_candidates.py](MHWorld/import_mhw_fullbody_candidates.py) | UE Python | Import the review-only MHW full-body rig and candidate actions into UE. | 历史迁移/排错；勿自动重跑 |
| [MHWorld/import_mhw_fullbody_on_existing_skeleton.py](MHWorld/import_mhw_fullbody_on_existing_skeleton.py) | UE Python | Try a review-only full-body mesh import against the known MHW body Skeleton. | 历史迁移/排错；勿自动重跑 |
| [MHWorld/inspect_exported_fbx_scene.py](MHWorld/inspect_exported_fbx_scene.py) | Blender Python | Inspect an FBX in a fresh Blender process without saving or modifying it. | 按需；先核对参数/写入范围 |
| [MHWorld/inspect_mhw_asset_candidates.py](MHWorld/inspect_mhw_asset_candidates.py) | 普通Python | Read-only inventory check for candidate MHW hunter MOD3/LMT files. | 按需；先核对参数/写入范围 |
| [MHWorld/inspect_mhw_lmt_action_motion.py](MHWorld/inspect_mhw_lmt_action_motion.py) | 普通Python | Report which MOD3 bone IDs actually change in selected MHW LMT entries. | 按需；先核对参数/写入范围 |
| [MHWorld/inspect_mhw_skeleton.py](MHWorld/inspect_mhw_skeleton.py) | 普通Python | Read-only dump of the MHW reference MOD3 skeleton hierarchy. | 按需；先核对参数/写入范围 |
| [MHWorld/mhw_animation_current_action.py](MHWorld/mhw_animation_current_action.py) | Blender Python | Blender FBX operator preset: selected rig/mesh, current action, full sampling. | 按需；先核对参数/写入范围 |
| [MHWorld/prepare_mhw_oodle.py](MHWorld/prepare_mhw_oodle.py) | 普通Python | Unpack the Oodle dependency from the official URL used by MHWNoChunk. | 按需；先核对参数/写入范围 |
| [MHWorld/probe_aerial_dive_weapon_scale.py](MHWorld/probe_aerial_dive_weapon_scale.py) | Blender Python | Read-only comparison of raw LMT and exported review rig scale tracks. | 按需；先核对参数/写入范围 |
| [MHWorld/probe_blender_mhw.py](MHWorld/probe_blender_mhw.py) | Blender Python | Environment probe, executed by Blender in background mode without saving. | 按需；先核对参数/写入范围 |
| [MHWorld/probe_mhw_ue_import_api.py](MHWorld/probe_mhw_ue_import_api.py) | UE Python | Read-only UE Python API probe for isolated MHW review asset import. | 按需；先核对参数/写入范围 |
| [MHWorld/reimport_mhw_aerial_dive_review.py](MHWorld/reimport_mhw_aerial_dive_review.py) | UE Python | Rebuild the isolated MHW Aerial Dive review import with canonical names. | 按需；先核对参数/写入范围 |
| [MHWorld/render_mhw_common_lmt_pose_sheet.py](MHWorld/render_mhw_common_lmt_pose_sheet.py) | 普通Python | Render read-only skeletal contact sheets from MHW LMT pose samples. | 按需；先核对参数/写入范围 |
| [MHWorld/repair_mhw_fullbody_review_assets.py](MHWorld/repair_mhw_fullbody_review_assets.py) | UE Python | Repair only the isolated MHW full-body review mesh and preview assets. | 历史迁移/排错；勿自动重跑 |
| [MHWorld/test_capture_mhw_xinput.py](MHWorld/test_capture_mhw_xinput.py) | 普通Python | 见文件头和参数说明 | 按需；先核对参数/写入范围 |
| [MHWorld/verify_blender_mhw.py](MHWorld/verify_blender_mhw.py) | Blender Python | Validate the staged MHW add-on and a synthetic skeleton/animation FBX round trip. | 按需；先核对参数/写入范围 |

## tests

| 工具 | 环境 | 用途简述 | 状态 |
|---|---|---|---|
| [tests/test_maintenance_tools.py](tests/test_maintenance_tools.py) | 普通Python | Focused launcher/type-adapter checks; no UE process or gameplay suite. | 定向测试；按需 |

## 公共入口与录制器

| 工具 | 环境 | 用途简述 | 状态 |
|---|---|---|---|
| [MHGZ_MHW_AerialTrajectoryRecorder.lua](MHGZ_MHW_AerialTrajectoryRecorder.lua) | 游戏Lua宿主 | MHGZ Monster Hunter: World runtime recorder | 按需；先核对参数/写入范围 |
| [ue_asset_utils.py](ue_asset_utils.py) | 普通Python | Small, UE-independent adapters for reflected values; errors stay explicit. | 公共模块；导入复用 |
| [ue_remote.py](ue_remote.py) | 普通Python | Execute a Python file in the matching open UE editor using Epic's client. | 按需；先核对参数/写入范围 |

## 原生编辑器反射桥接

2026-10-15：下表`MHGZAnimationSeamLibrary`新增定向ABP事务标志修复、两MM根运动/最终Pose History配置，以及拒绝覆盖的Loop候选库/相位Schema创建入口。均不自动保存；合同与已完成实例见[Scripts README](README.md#assetorganization)及[维护记录§7](../docs/editor/locomotion-maintenance-20261015.md#7-按顺序实施的当前结果)，不重复执行既有检查点。

| 入口 | 环境/源码 | 用途与约束 |
|---|---|---|
| `unreal.MHGZAnimationSeamLibrary` | UE Python调用；[声明](../Source/MHGZToolsEditor/Public/MHGZAnimationSeamLibrary.h)、[实现](../Source/MHGZToolsEditor/Private/MHGZAnimationSeamLibrary.cpp) | 常驻1帧中间姿态/推进根位移；单Slot插段、替换、裁切、刷新总长；同步通知/Section。显式索引、备份、输出不存在及无PIE/脏资产检查由调用者执行；不自动保存，不可盲目重跑。不是Commandlet，不搬玩法父类 |

## 既有编辑器Commandlet

通过对应引擎的`UnrealEditor-Cmd.exe <project> -run=<入口>`调用。下列多数属于历史阶段修复，运行前读取对应`.h/.cpp`与任务条件；工具存在不表示当前需要再次Apply。已有AnimBP审计、修复和表现资产桥接优先复用。

| 入口 | 源码 |
|---|---|
| `MHGZAerialHandoffSetup` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZAerialHandoffSetupCommandlet.h)；历史写资产工具，不作为迁移验证命令 |
| `MHGZActionVisualPitchGraph` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZActionVisualPitchGraphCommandlet.h) |
| `MHGZAirDodgeMontageSetup` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZAirDodgeMontageSetupCommandlet.h) |
| `MHGZDodgeBlendInSetup` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZDodgeBlendInSetupCommandlet.h) |
| `MHGZE4ActionExitAudit` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZE4ActionExitAuditCommandlet.h) |
| `MHGZE4ActionIdleAssetSetup` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZE4ActionIdleAssetSetupCommandlet.h) |
| `MHGZE4ActionIdleRouting` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZE4ActionIdleRoutingCommandlet.h) |
| `MHGZE4AnimBlueprintAudit` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZE4AnimBlueprintAuditCommandlet.h)；Editor模块，原短入口不变；只读蓝图连线审计 |
| `MHGZE4ExitAssetSetup` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZE4ExitAssetSetupCommandlet.h) |
| `MHGZE4ExitCallbackPolish` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZE4ExitCallbackPolishCommandlet.h) |
| `MHGZE4ExitTransition` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZE4ExitTransitionCommandlet.h) |
| `MHGZE4HandoffNotifySetup` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZE4HandoffNotifySetupCommandlet.h) |
| `MHGZE4PreSearchRouting` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZE4PreSearchRoutingCommandlet.h) |
| `MHGZE4SheathedExitGraphFixup` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZE4SheathedExitGraphFixupCommandlet.h) |
| `MHGZE4SheathedMacroNaming` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZE4SheathedMacroNamingCommandlet.h)；Editor模块，原短入口不变；历史写资产工具，无AuditOnly，不作为日常迁移验证命令 |
| `MHGZE4TailStartTimeRepair` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZE4TailStartTimeRepairCommandlet.h) |
| `MHGZE64DamageNumberSetup` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZE64DamageNumberSetupCommandlet.h) |
| `MHGZKinsectActionLayer` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZKinsectActionLayerCommandlet.h) |
| `MHGZM421LoopCandidateAssetSetup` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZM421LoopCandidateAssetSetupCommandlet.h) |
| `MHGZM5DescendingThrustAssetSetup` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZM5DescendingThrustAssetSetupCommandlet.h) |
| `MHGZM5KinsectPiercerAssetSetup` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZM5KinsectPiercerAssetSetupCommandlet.h) |
| `MHGZM5KinsectPiercerCurveSetup` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZM5KinsectPiercerCurveSetupCommandlet.h) |
| `MHGZM5KinsectSlashAssetSetup` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZM5KinsectSlashAssetSetupCommandlet.h) |
| `MHGZM5KinsectSlashCurveSetup` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZM5KinsectSlashCurveSetupCommandlet.h) |
| `MHGZM631MarkConfig` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZM631MarkConfigCommandlet.h) |
| `MHGZM636VisualAssetSetup` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZM636VisualAssetSetupCommandlet.h) |
| `MHGZM63PowderVortexAssetSetup` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZM63PowderVortexAssetSetupCommandlet.h) |
| `MHGZM64UISubsystemAudit` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZM64UISubsystemAuditCommandlet.h)；Editor模块试迁移，原入口保持不变 |
| `MHGZM6AssetSetup` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZM6AssetSetupCommandlet.h) |
| `MHGZM6BaselineAudit` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZM6BaselineAuditCommandlet.h) |
| `MHGZM6RecordedHunterFlightSetup` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZM6RecordedHunterFlightSetupCommandlet.h) |
| `MHGZM6ReleaseFacingNotify` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZM6ReleaseFacingNotifyCommandlet.h) |
| `MHGZPMM4AssetFixup` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZPMM4AssetFixupCommandlet.h) |
| `MHGZPMM4AssetReport` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZPMM4AssetReportCommandlet.h) |
| `MHGZPMM5AnimGraphFixup` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZPMM5AnimGraphFixupCommandlet.h) |
| `MHGZPMM7AssetFixup` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZPMM7AssetFixupCommandlet.h) |
| `MHGZPMMAssetFixup` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZPMMAssetFixupCommandlet.h) |
| `MHGZPMMExtendedStop` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZPMMExtendedStopCommandlet.h) |
| `MHGZPMMNotifyTrackRepair` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZPMMNotifyTrackRepairCommandlet.h) |
| `MHGZPoleVaultMontageSetup` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZPoleVaultMontageSetupCommandlet.h) |
| `MHGZPoseSearchTelemetryExport` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZPoseSearchTelemetryExportCommandlet.h) |
| `MHGZStripPresentationRootMotion` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZStripPresentationRootMotionCommandlet.h)；历史写资产工具，不作为迁移验证命令 |
| `MHGZWuTaMontageSetup` | [声明与前置说明](../Source/MHGZToolsEditor/Public/Commandlets/MHGZWuTaMontageSetupCommandlet.h) |
