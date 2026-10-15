# 项目维护脚本

> **2026-10-13维护规范：** 回归测试与全量资产验证已转为按需启用，不是日常小修前置流程。保留原工具，不自动调用套件或全量审计；用户指明、重大修改等必要情况按[维护规范](../docs/using/README.md)选择最小范围。录制器、遥测与具体故障的只读诊断继续可用。

`Scripts` 保存维护工具和明确标记的历史实现，不参与游戏运行时、打包或启动流程。历史脚本按索引状态识别，不自动删除或重跑。当前签收见[状态入口](../docs/using/current-status.md)。

## 先找工具

- [完整工具索引](tool-index.md)：Python、PowerShell、Lua及编辑器Commandlet，按环境和目录查找。用途来自现有脚本说明，历史工具有明确标记；调用前核对参数和写入范围。
- 关键词查找：`rg -n '关键词' Scripts/tool-index.md`；新工具加入时同步索引。命令参数以脚本的 `--help` 或文件头为准。
- 不确定接口时先看下面的已知限制，不重复探测已经证实不支持的API。

## 常用执行入口

普通Python使用本机真实解释器 `C:/apps/anaconda/python.exe`，不要使用WindowsApps占位程序。PowerShell中的路径按下例调用：

```powershell
# 编辑器已打开：只读连接探测，默认发现截止8秒
& 'C:/apps/anaconda/python.exe' Scripts/ue_remote.py --probe
# 在匹配项目的现有编辑器中执行一个明确的脚本
& 'C:/apps/anaconda/python.exe' Scripts/ue_remote.py Scripts/AssetProbe/inspect_camera_defaults.py
# 脚本参数不经过shell重新拼接
& 'C:/apps/anaconda/python.exe' Scripts/ue_remote.py Scripts/AssetOrganization/audit_asset_paths.py --script-arg=--paths=/Game/Weapons/InsectGlaive/Data/DA_IG_Combat
# UI专用只读检查，保留原入口兼容
& 'C:/apps/anaconda/python.exe' Scripts/AssetOrganization/run_m6_ui_remote.py --probe
```

`ue_remote.py`使用Epic原有协议。`--engine-root`或`UE_ENGINE_ROOT`可更换引擎位置；`--project-root`选择项目；`--discovery-timeout`控制发现等待；`--timeout`是整个调用的截止时间（默认120秒）；`--report`指定报告路径。默认报告位于 `Saved/Maintenance/ue_remote`，记录失败层、耗时、编辑器和脚本结果。不会启动新编辑器、自动重试或自动应用资产修复。执行阶段超时可能已经发生写入，必须核对后置条件。

兼容UI入口的`--timeout`保持旧含义（发现等待），`--deadline`控制总截止时间；支持`--engine-root/--project-root/--report`。新调用优先使用通用入口。

编辑器关闭时，按工具文件头使用现有UE Python Commandlet；例如：

```powershell
& 'C:/apps/UE5/UE_5.6/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'D:/study/MH/MHGZ/MHGZ.uproject' -run=pythonscript -script='D:/study/MH/MHGZ/Scripts/AssetOrganization/audit_damage_formula_units.py' -unattended -nosplash -NullRHI
```

纯资产值修改不需要C++编译。编辑器打开时不另起写资产的Commandlet覆盖其未保存内容；资产变更需按工具约定备份和验证保存。报告成功只证明该脚本做完，不能替代实例创建/显示或PIE签收。

## 已验证的限制与复用点

| 内容 | 当前做法 |
|---|---|
| UE5.6 UMG WidgetTree | Python未暴露；复用原`unreal.MHGZUIAssetSetupLibrary`入口，经MHGZ薄接口转发到ToolsEditor构建，通过`setup_m6_ui_widgets.py`调用。`audit_m6_ui_widgets.py`已弃用，不再探索同一接口 |
| UE Remote ExecuteStatement | 外层按Python `single`编译；多行负载需用单个`exec(compile(..., 'exec'))`包装，已由通用入口处理 |
| 本机沙箱与UDP发现 | 本轮沙箱内未发现已启动编辑器，同一入口在桌面网络上下文成功连接；先核对执行上下文，不反复修改UE配置或重启编辑器 |
| UE Array/Map | 使用`ue_asset_utils.as_sequence/map_items/map_values`；错误字典和字符串不能当数组迭代 |
| ScalableFloat/CDO | 使用`ue_asset_utils.scalable_float_value/blueprint_cdo`；缺失字段作为错误报告，不能猜成0或None |
| AnimBP连线 | 复用`MHGZE4AnimBlueprintAudit`及已有修复Commandlet；只核对当前涉及的函数/节点 |
| Montage内部片段/通知 | Python受保护字段通过`ObjectExporterT3D`只导出到Saved读取；通知事件可用`AnimationLibrary.get_animation_notify_events`，不要反复读取受保护Notifies。写单Slot片段复用`MHGZAnimationSeamLibrary`，同步重定位通知并刷新CompositeLength |
| 原生文本导出编码 | 有中文时可能为UTF-16/BOM，否则UTF-8；使用`ue_asset_utils.decode_native_export`统一解码并归一化CRLF后再解析，不使用忽略解码错误 |
| 原位更新动画骨骼轨道 | 必须按DataModel实际类型匹配控制器：`AnimationSequencerDataModel`使用`AnimSequencerController`，不能绑定到通用`AnimDataController`（会触发原生强制转换崩溃）。相同骨架/骨骼集合、长度与帧率下可按骨名`set_bone_track_keys`覆盖，骨骼排序无需相同；女性收刀已验证示例在`Saved/Maintenance/FemaleSheathe/replace.py`，只用于该明确三序列范围，不盲目重跑Apply |
| HUD显示 | 核对WBP生成类→HUD引用→GameMode/WeaponRuntime引用→运行时实例/显示条件；`probe_m6_ui.py`只证明静态引用，不证明PIE |
| 录制器 | Rise与World宿主不同；复用各自现有脚本，按F9开始后查看健康提示、行数和事件，F8只在特定接口排错时使用 |

共享模块：`ue_remote.py`（主机执行入口）、`ue_asset_utils.py`（无UE依赖的类型适配）。定向维护工具测试：`python -m unittest discover -s Scripts/tests -p test_maintenance_tools.py`，不触发游戏回归。

## Editor工具模块

移动接缝局部修复入口：`AssetOrganization/repair_locomotion_boundaries.py`。默认只读回查已保存事务；明确的 `-MHGZEntryRepairApply` 只允许首次执行，先备份四条指定Loop及两份专用PSS，再调用 `MHGZAnimationSeamLibrary` 的 `CloseLocomotionLoop/ConfigureLoopContinuitySchema`。不重建/保存ABP，不修改普通Start/Stop库。快照、保护资产哈希及独立回载证据在 `Saved/Maintenance/LocomotionEntryRepair20261015/`。遇已有意图/成功记录时先核对，不重复Apply。闭合工具使用DataModel自身的Controller，保留帧数、时长、内部段与累计Root Motion，只处理短边界的姿势及速度。

`Source/MHGZToolsEditor`已收纳全部43个既有Commandlet、纯资产作者桥接`MHGZAnimationSeamLibrary`及UI构建/PoseSearch Trace导出实现；原`-run=短名称`入口保持。工具只读/写入合同不因迁移而改变，历史资产生成工具仍不得自动重跑。模块为Editor（包含Commandlet宿主），Game目标不包含它；工具单向依赖MHGZ及实际使用的引擎模块，运行时MHGZ不依赖它。没有迁移UI/GA/动画父类。

直接按Python字符串加载这些Commandlet时使用`/Script/MHGZToolsEditor.<原类名>`，例如`/Script/MHGZToolsEditor.MHGZE4AnimBlueprintAuditCommandlet`；旧序列化/soft-class引用由43条精确ClassRedirect兼容，不能把它当成Python `load_class`字面量兼容。定向原生测试筛选名为`MHGZ.Maintenance.ToolsEditor.ClassPaths`，覆盖43个迁移类、重定向、原构造默认标记和7个保留的运行时类路径，按需运行，不调用任何Main。测量结果、依赖边界与回滚见[模块迁移记录](../docs/editor/maintenance-module-assessment.md)，整体合同见[方案](../docs/editor/maintenance-module-full-plan.md)。

运行时累计删除工具独用的八项编辑器依赖和Bcrypt链接。UI类`/Script/MHGZ.MHGZUIAssetSetupLibrary`及`FMHGZPoseSearchTelemetryExport`原四参数签名保留在MHGZ，完整实现已迁至ToolsEditor；Core-only Modular Features服务在Startup注册/Shutdown注销，薄接口不缓存指针、不主动加载DLL。原Python脚本无需改名；非Editor或没有服务时返回false。MHGZ仍保留有实际使用者的四项编辑器依赖，不为“搬空”迁移玩法父类或封存测试。

服务定向筛选为`MHGZ.Maintenance.ToolsEditor.Services`，按需运行，不保存Content。2026-10-14已定向修复Trace停止后的文件锁时序：导出器只重试打开目标文件，每5ms一次、最多2s，打开成功才解析，不使用AllowWrite读取尚未完成的Trace，也不停止/等待其他Trace会话。正常文件立即导出；超时明确报错、写Failed状态、保留原始Trace和已有CSV。这是同步导出入口，2s是异常等待上限，不是固定延迟；只发生在主动停止/导出录制时，不改角色输入。

TelemetryStop现要求真实录制结束后的首次自动导出成功，已移除旧错误豁免与测试补导出；TraceOpenFailure验证缺失文件、持续锁超时、已有CSV不变及另一条Trace身份不变。四项Services检查通过，证据在`Saved/Maintenance/TraceCloseFix`；CSV格式/成本算法与录制逻辑不变，不据此声明全量玩法或非零候选数值验收。

`MHGZE4SheathedMacroNaming`仍会按原合同重命名、编译并保存蓝图，**没有AuditOnly参数**；不能为验证迁移而执行其Main，或误以为添加`-AuditOnly`就不会写入。2026-10-14第二批仅验证源码等价、类注册和构造默认标记；实际运行的蓝图工具只有只读`MHGZE4AnimBlueprintAudit`。

动画桥接Python入口为`unreal.MHGZAnimationSeamLibrary`，声明见[头文件](../Source/MHGZToolsEditor/Public/MHGZAnimationSeamLibrary.h)：`create_one_frame_bridge`创建同Skeleton的1/60s中间姿态资产，根位移取相邻实际播放速度均值；`insert_bridge`按明确片段索引插入/替换下一段；`trim_segment_start`裁切明确片段开头；`refresh_montage_length`只刷新已存在片段的总长度。只支持单Slot修改，生成接口拒绝已存在磁盘包；调用者还必须先检查AssetRegistry/未保存同名资产，备份目标、排除PIE/用户未保存编辑、明确源帧和有效倍率，再显式保存。操作**不幂等、不自动保存**；失败先核对后置条件。替换/裁切区间中含通知边界时拒绝操作，不压缩伤害窗口。桥接是在资产中烘焙，不创建运行时Montage。

同一桥接另有`install_montage_segment_inertialization`：仅审计/幂等接入ABP动作Slot后的惯性混合，不改Montage片段、不自动保存。包装脚本`AssetOrganization/setup_montage_segment_blend.py`默认只读；默认模式处理ABP接线，`--configure-montages --apply --duration 0.05`备份后只保存`/Game`多序列Montage的内部混合元数据，`--verify-montages`只读回查已保存批次。关闭编辑器时用`-MHGZSegmentBlendMontages -MHGZSegmentBlendApply`写入0.05s，独立用`-MHGZSegmentBlendVerify`验证。批次检查点存在时拒绝重复应用；续接先验证，不能盲目重跑。当前基底已接通，不必重接线；招式通过Montage的`MHGZMontageSegmentBlendSettings`元数据单独开启，见[配置说明](../docs/editor/montage-segment-blend.md)。

该脚本另有`--remove-synthetic-bridges`，仅移除已授权的飞圆/横扫/突进5个合成Blend片段，使用现有配方桥接重定位Section/通知；不删除序列、不处理EntryHold/蓄力。Commandlet审计开关`-MHGZSegmentBridgeRemoval`，叠加`-MHGZSegmentBlendApply`才备份/保存；`-MHGZSegmentBridgeVerify`独立只读回查。检查点已存在时拒绝重复Apply；旧0.05s批次和入口验证均识别本次已批准的移除，不盲目恢复历史片段。

后续`--remove-attack-holds`审计8份攻击中的10个Hold配方及4份GA入口合并，显式Apply才备份/保存；对应Commandlet`-MHGZAttackHoldRemoval -MHGZSegmentBlendApply`、只读验证`-MHGZAttackHoldVerify`。只合并明确等时Section，旧越界接受窗口收回原动画实际末帧；不删除Sequence、不动翻滚Hold/蓄力。已有完成/未完成检查点均拒绝盲目Apply；旧Bridge/混合/入口检查优先验证最新清理状态。

入口重排复用此桥接的`rebuild_single_slot_montage`显式JSON配方：校验所有片段/Section/通知后才修改，不保存；通知复制使用新GUID与独立对象。`create_root_motion_copy`只创建原轨道不变的新常驻副本并开启Root Motion，拒绝已存在目标；`set_montage_segment_blend`设置元数据但不保存。六组地面入口调用者为`AssetOrganization/apply_ground_attack_entries.py`，默认只读，首次显式`--apply`或`-MHGZGroundEntriesApply`先备份；已有Saved检查点时仅验证。原作入口帧/倍率、蓄力例外及范围见[说明](../docs/editor/ground-attack-entry-fixes.md)，不为其他动作自动套用该配方。

## AssetOrganization

ABP连线/编译状态诊断复用`unreal.MHGZAnimationSeamLibrary.audit_anim_blueprint_graph(bp)`，返回真实节点与引脚文本，不编译或保存。第二参数`True`会模拟编辑器打开时的内存校正并报告缺失事务标志/首次置脏步骤；仅在独立Commandlet中使用，退出丢弃内存变更，禁止将诊断中的校正直接当作资产修复保存。2026-10-15使用实例与证据见[接缝维护诊断](../docs/editor/locomotion-maintenance-20261015.md)。

该桥接现另有明确的写操作：`repair_anim_blueprint_transactional_flags`只补节点事务标志并显式置包脏，由调用者编译/保存；`configure_locomotion_seams`仅适配现有两MM/单History主AnimGraph，设置两个活动播放器，可将原History移至最终输出前，不自动保存。`create_loop_only_database`和`create_loop_phase_schema`从显式模板创建常驻候选库/独立相位Schema，拒绝覆盖现有包、不自动保存。它们不是审计开关；必须先备份、确认编辑器状态和写入范围。首轮维护实例在`Saved/Maintenance/Locomotion20261015/`，已有检查点，禁止盲目重复Apply；后续连续相位修正见维护报告§8及`Saved/Maintenance/LocomotionFollowup20261015/`。

连续相位工具补充：`configure_loop_continuity_schema`只配置独立LoopPhase/LoopEntry Schema的窄相位门槛和双脚入口数据，不允许普通Move Schema；`install_locomotion_inertialization(bp, False)`只核对两MM之后的专用惯性节点，`True`才幂等创建并编译，不自动保存。不要用旧`configure_seams.py`重放首轮接线来替代当前配置。作者事务在`apply_continuity.py`，默认独立回读，已有成功检查点拒绝再次Apply。当前新增节点/Schema均已保存，日常不重跑资产生成。

| 脚本 | 用途 | 写入行为 |
|---|---|---|
| `audit_animation_assets.py` | 读取动画类型、Skeleton、Root Motion 与反向引用 | 仅向 `Saved/AssetOrganization` 写报告 |
| `audit_asset_paths.py` | 审计显式传入的 `/Game/...` 包、资产类型与反向引用 | 仅向 `Saved/AssetOrganization` 写报告 |
| `organize_assets.py` | 按显式 JSON manifest 移动逐个 Unreal 资产 | 默认只读；只有 `--apply` 才移动资产 |
| `organize_asset_roots.py` | 按显式 JSON manifest 移动完整资产根目录 | 默认只读；只有 `--apply` 才移动资产 |
| `verify_demo_structure.py` | 用普通 Python 检查 Demo 目录骨架、占位文件与关键 DataAsset 位置 | 只读 |
| `verify_project_assets.py` | 在 UnrealEditor-Cmd 中检查最终资产数量、旧路径、Redirector、Skeleton 并编译蓝图 | 仅向 `Saved/AssetOrganization` 写报告 |
| `audit_damage_formula_units.py` | 检查虫棍动作值、木桩肉质及猎虫/虫粉配置 | 只读资产；向`Saved/DamageUnitsAudit`写报告 |
| `apply_damage_formula_units.py` | 已完成的旧单位迁移，保留追溯 | 写资产并备份；勿自动重跑，现有0–100肉质可能小于1.5 |
| `probe_m6_ui.py` | 检查UI资产、能力和引用 | 只读资产 |
| `setup_m6_ui_widgets.py` | 首次创建并接线UI，拒绝覆盖已保存资产 | 写资产；日常调样式不重跑 |

两个整理脚本不再绑定某个历史阶段的默认清单，调用时必须显式传入 `--manifest=<绝对或相对路径>`。所有实际移动仍须先备份，并在新 UE 进程中验证引用关系。
