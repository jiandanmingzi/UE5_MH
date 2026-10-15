# 编辑器维护工具模块拆分评估

流程优化第6项。全部43个Commandlet迁移后，用户明确批准B，两份UI构建/遥测导出实现也已移入ToolsEditor，原稳定接口留在MHGZ转发。当前约定的A+B范围完成，不意味着运行时所有WITH_EDITOR分支或封存测试都应移走。详细边界见[整体方案](maintenance-module-full-plan.md)。

## 实际结构与成本

- 原项目只有`MHGZ` Runtime模块；现在全部43个Commandlet归属MHGZToolsEditor，原Editor目录已无Commandlet源码。
- MHGZ累计移除八项工具专用编辑器依赖和Bcrypt；仍保留真实运行时编辑分支/封存测试使用的四项依赖。下文历史批次的“依赖不变/仍保留”描述当时基线，当前以B段及实际Build.cs为准。
- 公共玩法头改变仍可能导致依赖它的工具与测试重编译；134个历史构建动作不能全部归因于未拆模块。
- `bUseUnityBuild=false`保持不变。文件局部名称冲突与模块拆分是不同问题，Unity设置不是本次迁移的障碍。
- 玩法、Widget父类与`UMHGZUIAssetSetupLibrary`的`/Script/MHGZ`路径不变。本次没有修改或重保存任何Content资产。

## 第一批试迁移合同与结构（2026-10-13，历史基线）

只迁移`MHGZM64UISubsystemAudit`，它读取AssetRegistry和现有HUD，不写资产、没有项目玩法头依赖。迁移前对Content的类名检索没有命中；本次不构成全资产引用审计。

- 声明：`Source/MHGZToolsEditor/Public/Commandlets/MHGZM64UISubsystemAuditCommandlet.h`；实现：对应`Private/Commandlets`。
- 试迁移时模块依赖Core、CoreUObject、Engine、AssetRegistry；后续动画资产作者桥接增加AnimationBlueprintLibrary（见下文）。当前工具不需要MHGZ依赖；未来工具实际使用玩法声明时再添加单向`ToolsEditor → MHGZ`依赖，禁止反向依赖。
- `MHGZ.uproject`使用`Type=Editor`、`LoadingPhase=Default`，编辑器Target加入该模块；不是`EditorNoCommandlet`，确保Commandlet宿主也加载。
- 原`-run=MHGZM64UISubsystemAudit`入口不变，实现仅改变include路径、声明仅改变模块导出宏。
- 唯一迁移类的原生路径改为`/Script/MHGZToolsEditor.MHGZM64UISubsystemAuditCommandlet`。DefaultEngine.ini的精确ClassRedirect兼容旧序列化/soft-class路径。
- **Python字面量限制：** `unreal.load_class(None, old_string)`使用StaticLoadClass，不自动应用该类重定向。直接按字符串加载时需使用新路径；本项目没有发现旧路径的现有调用。不宣称所有旧字面量入口兼容。
- 新增唯一原生定向测试`MHGZ.Maintenance.ToolsEditor.ClassPaths`；只检查迁移类、CoreRedirect以及5个既有UI/桥接类路径，不运行玩法回归。

## 第一批实测证据与收益边界

日志目录：`Saved/Maintenance/ModulePilot`。相同Development Editor、NoUBA/NoUbaLocal/NoHotReloadFromIDE设置，先确认现有目标已编译，再只更新同一工具cpp的时间戳触发真实重编译；内容不变，不做Clean。每侧2个样本，不能视作严格性能基准或保证每次耗时。

| 检查 | 结果 | 证据 |
|---|---|---|
| 迁移前单cpp增量编译 | 3.13s / 3.04s；编译工具并链接MHGZ.lib/dll | `before-1.log`、`before-2.log` |
| 迁移后单cpp增量编译 | 2.46s / 2.42s；只链接MHGZToolsEditor.lib/dll | `after-1.log`、`after-2.log` |
| 首次迁移构建/UHT | 成功，10.27s；一次性迁移成本，不计入上述比较 | `migration-build.log` |
| 原Commandlet入口 | 前后均退出0；25行审计输出排序后完全一致 | `audit-before.log`、`audit-after.log` |
| 原生类路径定向测试 | 1/1成功；CoreRedirect精确映射、5个运行时类原路径存在 | `native-class-test.log` |
| 非Editor目标排除 | Game目标JSON中无MHGZToolsEditor；没有编译/打包游戏 | `game-target.json` |
| 普通编辑器重启后远程读取 | 成功，1.219s；迁移类和5个既有运行时类路径均正确 | `live-editor-after.json` |

该样本均值约`3.085s → 2.440s`，缩短约21%。已证实工具单cpp改动不再重链接玩法DLL；不外推到公共头改动、整个项目构建或agent所有任务的耗时。

兼容检查中的失败也保留：`class-compatibility.log`记录Python字面量旧路径失败；`soft-class-compatibility.log`记录Python未暴露NativeMake接口。没有重复尝试同一路径；最终用原生测试验证真正的CoreRedirect行为。

## 后续新增动画作者桥接（2026-10-14）

用户要求在两份Montage中烘焙内部1帧桥接，Python未暴露片段数组写入；新增`Public/MHGZAnimationSeamLibrary.h`和`Private/MHGZAnimationSeamLibrary.cpp`，直接使用已经验证的骨骼姿态读取及原生动画数据控制器。增加私有AnimationBlueprintLibrary依赖，没有ToolsEditor→MHGZ或反向依赖，没有迁移运行时动画类，也不动态生成Montage。具体API/操作限制见[工具README](../../Scripts/README.md)。

首次构建7.10s；补充长度刷新后增量构建4.09s，5个动作均属于工具模块/UHT/元数据，没有重链接玩法DLL。两份Montage与三份桥接的定向磁盘检查退出0、0错误/0警告；证据`Saved/Maintenance/FeiYuanEntry/seams-length-build.log`及`seams-disk-verify.log`。这些是此次维护实现证据，不添加新的模块性能基准结论；资产视觉仍待用户观察。

先前“没有写Content”的结论只描述原模块迁移试验；此次新任务确实保存了2个Montage和3个生成序列，不能把两轮范围混为一谈。历史类路径测试和Game目标排除签收继续有效。

## 第二批：纯编辑器蓝图工具（2026-10-14，已完成）

只迁移以下两个工具，不改变其操作合同。源文件备份和检查脚本位于`Saved/Maintenance/ModuleBatch2`。

| 原短命令名 | 当前职责 | 当前文件位置 |
|---|---|---|
| `MHGZE4AnimBlueprintAudit` | 只读导出AnimBP相关函数、宏、折叠图与引脚链接 | `Public/Commandlets/MHGZE4AnimBlueprintAuditCommandlet.h`及对应`Private/Commandlets`实现 |
| `MHGZE4SheathedMacroNaming` | 历史一次性宏重命名，会编译并保存AnimBP | `Public/Commandlets/MHGZE4SheathedMacroNamingCommandlet.h`及对应`Private/Commandlets`实现 |

四个文件的完整内容只替换自身include路径、`MHGZ_API → MHGZTOOLSEDITOR_API`和换行格式；原目录文件已移走，UClass名称与原`-run=`短名称保持不变。没有项目玩法头依赖；私有依赖新增`BlueprintGraph`、`UnrealEd`，既有`AssetRegistry`、`AnimationBlueprintLibrary`保留。不新增`ToolsEditor → MHGZ`依赖，也不改运行时Build.cs、Game Target、Unity配置、uproject或Editor Target。

两条新精确ClassRedirect映射旧`/Script/MHGZ.<类名>`至`/Script/MHGZToolsEditor.<类名>`；第一批重定向保留。序列化/soft-class路径与Python字面量加载的区别继续适用，不添加包级/通配符重定向。

| 检查 | 本批结果 | 证据（`Saved/Maintenance/ModuleBatch2/`） |
|---|---|---|
| 四文件源码等价 | 仅冻结的include/export变化及换行格式；旧文件不存在 | 原文件备份、`verify-batch.ps1` |
| 首次迁移构建/UHT | 成功，18.76s、11动作；移除类的反射代码导致玩法DLL首次重链，属一次性迁移成本 | `migration-build.log` |
| 原只读短入口/输出 | 迁移前后均退出0、0错误/0警告；10个图表、83,002字节报告逐字节一致 | `audit-before.log`、`audit-after.log`、`graph-before.md`、`graph-after.md` |
| 原生类路径测试 | 唯一定向测试1/1成功；三工具的新路径、旧路径精确重定向、短类名、构造默认标记及五个原UI类路径 | `native-class-test.log` |
| 非Editor目标排除 | 新导出的Game目标JSON无MHGZToolsEditor；未编译/打包游戏 | `game-target.json`、`game-target-export.log` |
| 单工具cpp增量隔离 | 成功，3.32s、4动作；仅编译审计cpp、链接工具lib/dll及写目标元数据，无玩法DLL重链 | `tool-only-build.log` |
| 目标蓝图和既有构建边界 | ABP、MHGZ.Build.cs、Game Target的SHA256与迁移前一致 | `verify-batch.ps1`的固定基线 |

单工具增量前后玩法DLL的SHA256及修改时间均相同（`97B67A56C83586B47F93EED29006F38DC9290F349BA1322E0216712D7D269524`，UTC `2026-10-14T03:32:02.1269307Z`）。该批仅证明构建隔离；没有第二批迁移前耗时对照，不另报提速百分比，也不把第一批约21%推广到本批或整个项目。

**写资产工具没有运行：** `MHGZE4SheathedMacroNaming::Main`忽略参数、没有AuditOnly或dry-run，因此本批只验证类注册/默认标记和完整源码等价，未测试实际宏重命名结果，也未直接启动它的短命令入口。短入口的保留由未变类名和注册检查支持，实际执行证据仅属于只读审计。没有生成、修改、重保存Content；没有启动玩法回归、全资产验证、PIE或Git提交。

## 全量Commandlet迁移（2026-10-14，已完成）

用户要求整体优化后，一次迁移剩余40对文件。源码完整对照确认80文件只改自身include、模块导出声明及换行格式；两份原无API的工具头补工具导出。原有用户未提交的曲线/伤害/资产修改包含在迁移前备份中，没有被恢复成Git旧版本。

所有工具保持原UClass名和短命令名，43条精确重定向登记一次；索引补齐原来漏掉的AerialHandoffSetup和StripPresentationRootMotion，因此总数由旧记录41更正为43，不是新增工具。构造标记保持原样，包括ActionVisualPitchGraph沿用基类Server标记、StripPresentationRootMotion沿用全部基类默认值；没有顺便“统一”历史行为。

ToolsEditor新增单向MHGZ依赖，明确直接的GAS/PoseSearch/图编辑/UMG/系统库依赖。只补四个函数的MHGZ_API导出，不更改函数体、字段布局、GA/Widget/AnimInstance路径。运行时移除AnimGraph、BlueprintGraph、Kismet、Json、PoseSearchEditor和Bcrypt；仍保留AssetRegistry、AnimationBlueprintLibrary、TraceAnalysis、TraceServices、RenderCore、UnrealEd、UMGEditor供真实使用者，不依赖ToolsEditor。

| 验证 | 实际结果 | 证据（`Saved/Maintenance/ModuleFull/`） |
|---|---|---|
| 源码/资产边界 | 80文件等价；四个头仅加函数导出；其余运行时实现不变；667 Content文件数量/长度/时间不变，ABP哈希不变 | `baseline.json`、`original-tools/`、`verify-moves.ps1` |
| 完整迁移构建 | 成功，107.20s、176动作；公共头导出变化引发一次性重编译，不是日常单工具耗时 | `migration-build-4.log` |
| 无用运行时依赖清理构建 | 成功，98.24s、185动作；规则改变触发一次性重编译 | `cleanup-build.log` |
| 类路径/默认值 | 唯一定向测试1/1成功，实际循环覆盖43工具、43精确重定向及7个原运行时类型；不执行工具Main | `native-class-test.log` |
| 只读蓝图短入口 | 前后退出0、0错误/0警告；10图表、83,002字节输出逐字节一致 | `audit-before.log`、`audit-after.log`、`graph-before.md`、`graph-after.md` |
| Game目标边界 | 新Game目标JSON无ToolsEditor；未执行Game编译或打包 | `game-target.json`、`game-target-export.log` |
| 依赖玩法接口的单cpp增量 | 仅4动作、2.33s，编译TelemetryExport工具cpp、链接工具lib/dll及写元数据；玩法DLL哈希与时间未变 | `tool-only-build.log` |
| 工具索引 | 43/43覆盖；原目录链接已更新 | `Scripts/tool-index.md`及定向链接检查 |

单工具增量前后玩法DLL哈希为`D1E226B0BEDCE1EAFD99D645C077B9B9549BBA3D0957679A56DFFAE8781AC7CD`，UTC修改时间`2026-10-14T04:35:54.8988105Z`。只证明工具cpp改动不重链玩法DLL；没有新的迁移前性能对照，不声称全量优化提速百分比。

失败记录保留：第一次构建规则把系统库写为UE5.6不存在的PrivateSystemLibraries，已改PublicSystemLibraries；随后链接暴露缺少直接GAS依赖和四个未导出函数。一次补丁校验失败未落盘，build-3仅补到一项导出，余18符号仍失败；完整补齐后build-4成功。不是游戏业务故障，不通过重复运行资产生成器修复。

**A完成时的历史检查点：** 自动审批曾要求对新增运行时服务转发明确授权，因此当时仅完成A。随后用户批准B，当前结果如下，不要再把旧拦截视作待批准。A阶段未重新打开普通编辑器或执行PIE/全量回归/Git提交。

## B：稳定接口实现拆分（2026-10-14，已批准并完成）

MHGZ持有Core-only `IMHGZEditorToolServices`合同；ToolsEditor在Startup注册、Shutdown注销。原UI反射路径/Python类名及Trace四参数签名保留，调用时查询服务，不缓存指针、不硬加载工具DLL；缺失服务或非Editor目标返回false。两份完整实现只改变include/函数归属；`LogMHGZMM`补MHGZ_API导出。运行时移除UMGEditor、TraceAnalysis、TraceServices，无反向模块依赖。

| 检查 | 实际结果 | 证据（`Saved/Maintenance/EditorServices/`） |
|---|---|---|
| 源码及资产边界 | 两份作者/解析算法完整等价；307份既有运行时源码除批准变更外保持；667 Content文件数量/长度/时间不变 | `original/`、`baseline.json`、`verify.ps1` |
| Editor构建 | 成功，77.76s、191动作；规则/公共头引发一次性重编译 | `build.log` |
| 普通编辑器Python旧入口 | 原反射路径不变，远程调用0.659s成功，空资产进入真实作者实现并安全失败；正常退出确认 | `host-probe.json`、`host-remote.json`、`host.log`、`close-remote.json` |
| 定向原生测试 | 4/4成功：ClassPaths（43工具/7运行时类型）、五布局UIForwarding、ProviderWithdrawal、TelemetryStop；Trace项带下述警告 | `native-tests-10.log` |
| 实际录制结束链路 | AnimInstance启停调用编辑器导出；首次立即导出碰文件锁，关闭后同一薄接口成功解析真实Trace、生成原CSV头 | `native-tests-10.log`、`CaptureTests/20261014-140753-BP_IG_Character_C_0-56608/` |
| 非Editor薄接口 | Game目标仅编译两cpp成功，5.93s/2动作；没有完整Game链接或打包 | `game-facades-build.log` |
| Game模块排除 | 新JSON不包含MHGZToolsEditor | `game-target.json`、`game-target-export.log` |
| 已迁移UI单cpp增量 | 3.26s/4动作，只链接ToolsEditor；玩法DLL哈希/修改时间不变 | `tool-only-build.log` |

单cpp检查前后MHGZ.dll SHA256为`D52E8862DB9296912FFDCB79964FA604BC0AEE80FA730117F8F5722EC1D2A7A4`、UTC时间`2026-10-14T05:07:16.4143228Z`，只证明构建隔离，不报新提速比例。没有保存Content、启动全量玩法回归、PIE或Git提交；未做DLL热卸载测试，已覆盖注册表撤销/恢复和普通编辑器正常退出。

**B时点的历史边界（随后已修，见下节）：** UE Trace Stop排队关闭，原代码立即OpenRead可能碰Windows写句柄；迁移时生产实现逐字等价，非迁移引入。旧TelemetryStop容许明确文件打开错误并在关闭后补导出；当时4/4不等于首次自动导出已修好。候选/通道为0，验证解析格式及调用链，非成本数值回归。旧日志保留，不改写历史结果。

夹具失败记录保留：重复World初始化、抽象角色生成、错误角色资产路径及启动待处理句柄已按现有框架修正；Trace::Update直接链接失败后撤回不必要调用。最终使用现有BP_IG_Character瞬态实例，不新增玩法父类导出、不保存资产；只归档测试新记录，不移动用户录制。`ModuleFull/verify-moves.ps1`是A时点基线，B后改用`EditorServices/verify.ps1`核对当前源码，不能用旧基线误报B变更。

## 后续Trace关闭时序修复（2026-10-14，已完成）

用户明确要求修复上述风险。只修改ToolsEditor导出器的文件打开阶段及服务测试：保持同步入口，目标文件打开失败时每5ms重试、最多2s，仍使用不允许写入的读句柄，避免读取正在写入的半截Trace；不调用Stop、不等待全局Trace状态。超时明确失败，写`PoseSearchDetail-Status.txt`，不删除raw或截断已有CSV。正常可读文件不等待；这是诊断导出的异常上限，不影响角色输入算法。

定向增量构建3.92s/5动作，仅工具模块，MHGZ.dll哈希/时间与B时点相同。`MHGZ.Maintenance.ToolsEditor.Services`四项成功：原UI转发、服务撤销、**首次自动导出**、新TraceOpenFailure。真实AnimInstance Stop直接生成CSV，已去掉文件锁错误豁免、测试侧重试与额外Stop等待；持续锁约2s失败，保留raw/既有两个CSV，另一条测试Trace身份不变且只由测试自己停止。日志启动阶段有旧的两条`Condition failed`（B日志也存在），不是本轮测试事件；不据四项成功声称全日志零错误。

证据：`Saved/Maintenance/TraceCloseFix/build.log`、`native-tests.log`、`original/`及同目录OpenFailure唯一夹具；成功捕获归档到`EditorServices/CaptureTests/20261014-143816-BP_IG_Character_C_0-56608`。解析/成本/CSV正文与修复前逐字相同，原AnimInstance源码哈希不变。无Content写入、玩法回归、PIE或Git提交；候选为0，未扩展为MM成本数值测试，未处理Stop请求被拒绝等其他故障。

`EditorServices/verify.ps1`已是历史精确基线，后续有意修改打开阶段不能再用它断言完整导出器未变；本轮以备份差异、解析正文等价和首次自动导出结果验收，不重新生成旧迁移结论。

## 后续与回滚

现阶段保留全部43个工具，A+B迁移及后续Trace文件锁定向修复均完成。玩法父类、DataValidation和封存测试的运行时归属不变，不把继续拆分作为日常小修前置；历史迁移证据与后续修复证据分别记录。

**回滚本次全量批次：** 按ModuleFull备份和整体方案恢复40对源文件、include/export、40条新重定向、四个新增函数导出及本批规则变更，恢复对应索引/测试；保留早先三个工具、动画作者桥接和模块接入，不覆盖既有未提交改动。下面第二批回滚步骤仅适用于该历史批次，不能直接用于当前全量状态。

**只回滚第二批：** 将这两个Commandlet四文件恢复到原目录、原include/导出宏；仅移除本批两条ClassRedirect和测试表中这两项，恢复两条索引链接；新增私有依赖先核对模块其余工具无使用再撤销，然后增量编译。保留第一批UI审计、动画作者桥接、整个MHGZToolsEditor模块及其uproject/Editor Target接入，不删除模块。

如需撤销整个模块拆分，必须另行明确范围，逐项处理第一批和动画桥接现有调用，不能使用最初试点的“删除新模块”步骤直接覆盖当前结构。任何回滚不得覆盖用户其余未提交修改、删除Content资产或执行Git硬重置。
