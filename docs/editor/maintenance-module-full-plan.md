# 第6项整体编辑结构优化方案

更新：2026-10-14。范围是编辑器专用维护代码，不迁移GA、Widget、AnimInstance、怪物部位等运行时类型，不重跑历史资产生成。

## A：全部Commandlet迁移与无用依赖清理

本次用户要求继续整体优化，对剩余40个Commandlet集中迁移。前三个已经迁移，因此最终应为43个，不是旧索引的41个；旧索引漏列`MHGZAerialHandoffSetup`和`MHGZStripPresentationRootMotion`，并非新建工具。

- 所有头文件移入`Source/MHGZToolsEditor/Public/Commandlets`，实现移入`Private/Commandlets`。
- 只改变自身include和模块导出声明；两份原来没有导出宏的工具头补上工具模块导出，不改变函数实现。
- 原UClass名及`-run=短名称`保持，43条精确ClassRedirect兼容旧序列化/soft-class路径。Python字面量类加载继续用新路径，不承诺旧字符串自动重定向。
- 单向依赖为`ToolsEditor → MHGZ`。直接使用的GAS、PoseSearch、UMG、图编辑等引擎模块明确列在ToolsEditor规则中，禁止反向依赖。
- 四个原来模块内部调用的函数只补`MHGZ_API`导出：轨迹曲线校验、归一化进度曲线校验、部位包围半径查询、操虫斩舞踏参数校验。字段布局、函数实现和序列化数据不变。
- 运行时模块删除仅由工具使用的`AnimGraph`、`BlueprintGraph`、`Kismet`、`Json`、`PoseSearchEditor`和Bcrypt链接。仍被稳定接口/封存测试使用的依赖继续保留，不能为了“清空列表”删除。

验收只做源码等价检查、集中编译、43个类路径/重定向及构造标记检查、既有运行时类型路径、只读蓝图审计输出对照、Game目标模块清单、工具cpp单文件增量隔离。禁止执行任意历史写资产Main；不运行全项目玩法回归或全资产DataValidation。

源备份、387个运行时源码文件的哈希基线、667个Content文件的数量/长度/时间基线及ABP哈希在`Saved/Maintenance/ModuleFull`。源码对照容许明确的移动/include/export/规则变化，不容许业务函数变化。Content元数据检查不是逐资产内容验证。

## B：两个稳定接口背后的编辑器实现拆分（已批准、已实施）

A阶段曾因缺少明确架构授权暂停B；用户随后明确“批准”，现已按以下合同完成。此前拦截是历史检查点，不再作为当前待办。

| 保留入口 | 兼容约束 | 当前实现 |
|---|---|---|
| `/Script/MHGZ.MHGZUIAssetSetupLibrary`、原Python类名 | 需保持现有反射入口和类路径；运行时模块不能硬依赖工具模块 | 原类/函数留作薄接口，WidgetTree构建实现在ToolsEditor |
| `FMHGZPoseSearchTelemetryExport::ExportTraceToCSV` | AnimInstance在录制结束时也会调用，直接搬走会形成反向DLL依赖 | 原签名留作薄接口，TraceAnalysis解析/CSV生成实现在ToolsEditor |

共同合同：使用UE已有Modular Features；Core-only接口位于`Source/MHGZ/EditorServices/MHGZEditorToolServices.h`，ToolsEditor在Startup注册、Shutdown注销实现。两个原入口只保留转发，不缓存编辑器指针、不自动加载工具模块。非编辑器目标或没有服务实现时安全返回false；模块为Editor/Default，包含Commandlet宿主。真实编辑器Python入口、原生UI构建、服务撤销和录制结束调用均已定向检查。

WidgetTree作者实现位于`MHGZToolsEditor/Private/MHGZUIAssetSetup.cpp`，Trace解析与CSV生成位于同目录`MHGZPoseSearchTelemetryExport.cpp`。B迁移时两份完整算法只改变include和函数归属；后续Trace修复仅增加打开文件的有界等待，解析/成本/CSV算法仍不变。`LogMHGZMM`只补模块导出。MHGZ已移除`UMGEditor`、`TraceAnalysis`、`TraceServices`，保留真实使用者所需的AssetRegistry、AnimationBlueprintLibrary、RenderCore、UnrealEd。原资产/玩法父类不改。

原遥测非Editor分支的三参数定义/四参数声明不一致已随薄接口按原四参数声明修正；原默认参数保留。Game目标仅定向编译两个薄接口成功、模块JSON排除ToolsEditor，不代表完整Game链接或打包通过。

证据与验收边界见[迁移记录B段](maintenance-module-assessment.md)：服务测试4/4、真实Python旧入口、源码等价和667个Content文件元数据边界通过。单独重编译迁移UI实现只链接ToolsEditor。注册表撤销/恢复与普通编辑器正常退出已验证，未验证运行中DLL热卸载。没有跑全量玩法回归或保存Content。

**迁移时发现的遥测时序风险已由独立小修关闭（2026-10-14）：** UE Trace Stop异步关闭，原立即OpenRead可能碰Windows写句柄；导出器现对目标文件的Open做每5ms/最多2s的等待，成功获得不允许写入的读句柄后才解析，不停止或等待全局其他Trace。失败保留原始文件及已有CSV并写Failed状态，正常文件无固定延迟。首次自动导出测试已取消错误豁免和事后补导出；服务四项检查通过。修复范围只在Editor实现与定向测试，原AnimInstance启停、接口、解析/CSV算法不变；证据在`Saved/Maintenance/TraceCloseFix`，详见迁移记录后续修复段。不处理Stop请求被拒绝等其他故障，也不把异常2s上限当作输入死区。

## 边界与回滚

不迁移封存的玩法测试类型、DataValidation实现或运行时父类；它们仍有真实运行时归属。C++编译可能编译到封存测试文件，但这不等于执行回归测试。

A回滚应以本次源备份恢复40对文件及对应include/export，只撤销新增40条重定向、四个函数导出和本批规则变化；保留前三个工具、动画作者桥接及既有模块接入。保留用户原有未提交代码/资产，禁止删除整个ToolsEditor模块或Git硬重置。

只回滚B时，按`Saved/Maintenance/EditorServices/original`恢复两个原实现、运行时规则和日志声明，移除本轮服务合同/注册/测试、ToolsEditor的两份实现/私有声明和新增Trace依赖；保留A的43工具、重定向、作者桥接和模块接入。注意旧非Editor签名错误，不据恢复旧文件声称Game可编译；回滚前核对后续改动，不覆盖用户新修改。

只回滚后续Trace小修时，参考`Saved/Maintenance/TraceCloseFix/original`恢复导出器打开文件的旧行为及对应测试；这会恢复已确认的文件锁风险。旧`EditorServices/verify.ps1`保留为B迁移时点的精确源码基线，不能在后续有意修复后仍断言整个导出器逐字不变；应分别核对打开阶段改动及其后解析/CSV正文等价。

执行结果与当前门禁见[模块迁移记录](maintenance-module-assessment.md)和[维护状态](../using/current-status.md)。
