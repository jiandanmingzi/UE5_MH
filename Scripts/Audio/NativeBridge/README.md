# 崛起原生音频采集桥（1.21.0）

## 1.21三项修复已实采通过

最新`mhrise_20261019_145235_02`有28组完整伤害/pre/post，新对象队列2624行无gap/丢弃，756 Post全部有精确对象键；字段66项齐全、读取/覆盖警告0，F9保存正常。三项修复接受，不重复启动/短录。命中声27/28有完整唯一归属，1次因发声地址曾被其它触发使用而保守未知；最终Media仍未采集。证据`Combat121/Capture145235-Audit/findings.md`；下方加载记录与部署时待办仅作历史，不再恢复为任务。

进程30288加载核对通过：新对象队列模式已运行，保存快照2591对象/1477 trigger与原Post/条件均无丢弃/gap/错误。Lua选择engine_context_stack，尚无实际hit/post回调，不能代替采集验收。证据`Combat121/Runtime30288/`；下方待加载描述是部署时检查点，现仅需少量真实命中短录。

对象键/生命周期/trigger独立改用`NativeObjectQueue`：8192格有界MPSC序号环，不与消费端争用互斥锁，生产回调无分配/对象读取/文件IO。每次最多256次CAS预留重试；队满与预留耗尽分别写status/live的`objects_full_dropped`、`objects_reservation_dropped`，任何新增丢失仍写原15列CSV的queue_gap，离线失败关闭。环只在启用挂钩前初始化，F9不重置正在使用的环；stop后已预留格仍完整发布，避免堵死消费。原Post/条件队列、ASM/入口profile/CSV列不改。

Lua增加旧宿主上下文栈后快照、明确POST_ERROR与96字段，见MHRise README。3项CTest通过，含4生产者/消费者交错4000条零丢失、12轮回绕、溢出原因、真实透传/ABI/IPC/CSV；89音频Lua、17动画/耐力、10关联审计通过。只修IPC测试夹具误用512字节命令读取器读取status，不放宽命令限制。

用户退出后仅替换Lua/DLL并核对哈希，1.20备份/清单`Saved/Maintenance/RecorderDeploy/20261019-Combat121/`。**尚未真机加载/采集签收，不宣称最终Media或绝对不丢失**。回滚需退出后恢复两份备份，不热卸载固定DLL、不更新其它模组。下方1.20/更早内容均为历史证据。

## 1.20已实采：原生命中边有效，对象队列损失需修

最新短录`mhrise_20261019_134722_03`有42条原生命中上下文，各自唯一对应42次伤害，但对象侧车在录制期间有21处queue_gap，完整Post关联不通过。Lua后快照0也已定位到当前宿主缺少所依赖的hook-storage能力。先修兼容与队列，不要求新录制；证据`Combat120/Capture134722-Audit/findings.md`，实施计划1.13。下方加载检查与测试通过不是实际录制整体通过。

在保留旧Post/条件入口与CSV的基础上，增加可选`+0x4DA0BA0`提交入口观察。只复制原前四实参；透明ASM保留全部原栈参数/标志/GPR/XMM及原返回，不猜完整ABI。新入口、空间调用prologue、三处直接call指令分别按固定profile核对；失败只令trigger不可用，不强行覆盖其它挂钩或禁用原Post。

`+0x4DA4900`空间调用返回`+0x4DA4A20`，且上层返回`+0x11971E8/+0x119700D`时，原空间函数保存的RDI是HitInfo，R14是线程上下文。入口只在这些地址均匹配时读取固定调用栈槽里的标量，不解引用游戏对象；其它调用保持hit上下文空。独立本进程MASM嵌套夹具验证此栈形状、未知调用者拒绝及原参数/返回保护，不以夹具代替真机验证。证据`Combat120/hit-trigger-exact.json`、`hit-spatial-trigger.json`、`container-trigger*.json`；最早错误RVA试读未用于任何挂钩。

对象CSV从10扩为15列，旧前10列名保留；追加container/owner/HitInfo/thread-context/caller-RVA。新增kind为`container_trigger_context`，`sub_id`在该kind下是TriggerID而非EventID，`game_object_id`保持空（之后只按精确emitter地址与键关联）。临时对象池/HitInfo复用有歧义时审计不按最近时间强行归属。状态新增trigger_ready/observed/hit_contexts/error；无F9即可查安装/活动。

Lua补必要字段优先、DamageRS/有限嵌套及执行后声音快照，见[MHRise README](../../MHRise/README.md)。离线`--audit-combat`从实际银行读根Switch默认，单列实采请求/默认候选/未知；不设置游戏条件，也不宣称最终Media。旧记录重审在`Combat120/Capture123713-AuditV2/`，首次开发审计的请求/有效值标签错误已标superseded。

3项原生CTest、81项音频Lua、17项动画/耐力、10项新审计、19项既有映射检查通过。1.20已在用户退出后备份/替换两个原文件并核对哈希；清单`Saved/Maintenance/RecorderDeploy/20261019-Combat120/manifest.json`。进程33060已实证全部观察器ready、新增trigger1250次活动且无入口错误；实际hit上下文尚0。F9前对象队列累计丢弃3条，Post/条件丢弃0，不宣称零损失；录制开始重设丢失基线并写初始未知，录制中新增丢失仍标gap、审计不作完整关联。证据`Combat120/Runtime33060/`。**加载通过，新字段/关系及CSV健康仍待短录，不要求全招式重录**。回滚需正常退出后恢复该目录的1.19 Lua/DLL；不运行时卸载固定DLL。以下是历史1.19/1.18证据。

## 1.19独立命中与发声对象键：已构建/部署，待真机验证（2026-10-19）

**短录补充（当前结论）：** `mhrise_20261019_123713_02`验证44次独立命中＋44次声音上下文，1130 Post均有精确同ID的先前键观察；479个ID无地址冲突，原生地址与三个已知命中对象实证一致。条件17675行/对象2153行、无丢弃/gap或错列，正常停止。基础采集通过；通用字段截断、临时命中声发声对象的父归属、仅目标对象上出现的Switch3805839008仍需补，最终Media未验证。详见`Combat119/Capture123713/audit.json`与实施计划1.11；不用先全招式重录。下文“待真机验证/实际回调0”是此前启动检查点，已被本段取代。

启动补充：进程1512已验证1.19加载及对象键实际调用；快照661 Post/1987条件/1410对象键与生命周期，0丢弃/gap/错误，证据`Combat119/Runtime1512/`。命中两个监听器已安装、实际回调仍0；下面的待验证现收窄为真实命中字段/对象身份与短F9侧车，而不是需要重复安装或重启。

在1.18基础上追加，不更改原Post/条件CSV。Lua的独立命中侧见[MHRise README](../../MHRise/README.md)：精确描述符选择`EnemyCharacterBase.afterCalcDamage_DamageSide(DamageFlowInfoBase, HitInfo)`与单独的`WwisePlayerMonitoredParameters.hitTrigger`，`*_hits.csv`分开标记，不把音频回调当成独立扣血。

原生新增**可选**键观察器`+0xB5E100`：已从音频分配`+0x4DAD9A0/+0x4DADE20`及完整返回路径核实两参数`(GameObject*, UInt32 sub-ID)`与UInt64返回；引擎将该返回键交给数值音频后端。证据`Saved/Audio/RiseIG/NativeBridge/Combat119/{object-lookup,gameobject-id-full}.json`。全映像哈希/PE/原入口保护保留，新入口另核对25字节前缀；不匹配/安装失败只让对象键不可用，不把原Post/条件禁用。原函数调用一次、原参数/返回保留；回调不解引用对象、不进入Lua、不分配/写文件。

`*_audio_objects.csv`10列，保存完整64位引擎对象地址、子编号、返回键、QPC/线程/Present帧，以及实际注册/注销请求。独立8192条队列，首次明确`initial_coverage_unknown;no_pref9_baseline`，丢失写`queue_gap`；不把启动前/录制前键当已知当前归属。后续只能按精确键与Post关联、再用实际对象身份判断猎人/目标；**原生指针与Lua对象地址是否同一身份尚待真机对照，不能先认定相等**。返回键不是最终Media或播放成功证明。状态新增objects计数/错误，自动live带最近原始键，不填猜测owner。

3项CTest通过，新增本进程真实两参数MinHook透传、64位键/指针/子编号、10列CSV、生命周期、溢出标记/文件冲突隔离。76项音频Lua与17项动画/耐力检查通过，完整Lua5.4/LuaJIT编译通过。用户退出游戏后已只替换原Lua/DLL并核对SHA；备份/清单`Saved/Maintenance/RecorderDeploy/20261019-Combat119/`。**尚未验证1.19真实加载/回调及F9新侧车，先短测，不要求全招式重录**。没有修改UE或其它模组。

回滚：退出Rise后，将该目录的`MHGZ_AerialTrajectoryRecorder.v1.18.1.lua`与`MHGZ_RiseAudioBridge.v1.18.dll`恢复原位置。不得运行时卸载固定DLL。以下1.18/1.17是历史证据，不是当前仍未写代码的待办。

## 1.18条件请求采集：已构建/部署并确认真机实际调用/值

2026-10-18已从1.17.1自动报告及只读代码核实真实入口，并将Lua/DLL1.18部署到原路径。进程2960无F9自动证据：5965次请求、53个键（13 State/19 Switch/21 RTPC）、0丢弃/0gap；见`Conditions118/Runtime2960/audit.json`。3项原生CTest、63项Lua音频检查及17项动画/耐力检查通过，整脚本Lua5.4/LuaJIT编译通过。备份、安装哈希和阶段见`Saved/Maintenance/RecorderDeploy/20261018-AudioContext118/manifest.json`。**条件实际数值已验证，但新版F9条件CSV尚未真机录制，归属/独立命中未完成**。未改UE、dinput8、其它模组或旧录制。

- Post仍使用原入口与原`*_audio_native.csv`，列结构/请求语义保持。新增六个固定数值入口：State `+0x7335EE0`、Switch `+0x73360E0`、RTPC `+0x7335970`、RTPC重置 `+0x7330990`、音频对象注册/注销 `+0x7332A40/+0x73375F0`。全映像哈希、旧两个入口和新六段25字节前缀全部匹配才创建挂钩；一次启用、失败撤回自己的挂钩。
- 仍只观察**数值API提交请求**，不改参数/原返回值。共用透明汇编入口保存标志/易失GPR/XMM及原栈；RTPC另复制已核实的第5/6个栈参数。回调不读对象、不调用Lua、无分配/文件IO，只向独立8192条队列复制标量。
- 条件队列从桥加载后持续采集，F9前也保留已观察到的请求。Present侧最多缓存4096个条件键，记录State/Switch数值ID、RTPC目标浮点位模式/时长/曲线/是否绕过插值；全局与对象参数分开，GameObjectID始终是完整64位十进制。
- 注册/注销、参数重置会使相关旧条件失效。队列丢弃清空历史并建立QPC屏障，迟到的丢弃前记录不能把旧历史恢复成“已知”；缓存容量超限也有显式gap。不静默驱逐后宣称完整。
- 不用F9即可生成`audio_native_context_live.txt`：带当前PID、计数/丢弃/gap及最多64个缓存项，约每秒原子更新。先核对实际字段，不要求全招式重录。
- 同一F9 basename新增`*_audio_context.csv`（16列）。首行`initial_coverage_unknown`，未知ID/值留空；随后`baseline_last_observed_request`仅是F9前确实看见的最后请求，`qpc`保留原观察时刻。再记录`update_requested`、重置/生命周期/gap；时间估计仍沿用原IPC锚点，不能声称亚帧同步。
- **RTPC记录的是请求目标，不是插值过程中的瞬时值。** 未经过这些入口的内部更新/加载前初值仍未知；API请求不等于最终选中音色或Media。状态/CSV均不宣称发声归属、独立伤害或最终WEM已采到。
- 本次发现反射`getGameObjectByGameObjectId(UInt64/UInt32)`共同转到`+0x54AC0`，其实现是`xor eax,eax; ret`，并不能解析对象；不据此填猎人/目标标签。伤害回调签名已定位，但参数布局/真正结算语义尚未验证，继续由独立步骤补齐。

证据在`Saved/Audio/RiseIG/NativeBridge/Conditions118/`：`wrappers/engine-setters/condition-dispatch/numeric-routing/consumer-cases/numeric-entries/lifecycle-entries/backend-callers.json`。`probe_native_audio.py --audio-backend-callers`仅读固定448KiB音频代码区和异常表，列出到固定数值后端区的直接调用；无对象搜索、写内存、注入或主动执行。

回滚：退出游戏后将部署备份中的`MHGZ_AerialTrajectoryRecorder.v1.17.1.lua`与`MHGZ_RiseAudioBridge.v1.17.dll`恢复到原autorun/plugins路径；不可运行时卸载已固定DLL。下文是1.17历史实施记录，不表示当前仍缺EventID。

当前：**DLL加载、IPC、底层调用及首份F9 CSV均已验证**。录制`mhrise_20261018_131457_01`含1508行/73种EventID，无错误或丢弃；26种/352次与M1资源精确匹配，尚未完成逐招式/条件及命中声定位。详见[资源映射第6节](../../../docs/reference/虫棍音效资源映射.md#6-首份原生事件录制验证2026-10-18)。用于修复Lua包装层挂钩安装成功却没有音频回调的问题，不是UE音效播放接入。

## 范围与安全合同

- 新增`reframework/plugins/MHGZ_RiseAudioBridge.dll`，配合原录制器1.17，仍使用F9。无独立热键、无运行时Lua ABI/FFI依赖。
- 游戏SHA256、PE时间戳、SizeOfImage及两个函数开头精确匹配才安装一个原生挂钩；有其他绕转或版本变化则拒绝。没有通配扫描后强行安装、远程注入、主动播放或修改游戏参数。
- `passthrough.asm`保存标志、易失整数寄存器和XMM0～5，保持原栈参数，尾跳原蹦床。入口只复制EventID/64位GameObjectID/flags、QPC与线程号；不读游戏对象、不调用Lua、无文件IO/分配。
- 固定8192条队列，生产者不等待消费者锁，满队列/锁争用显式计数。主Present回调有界排空并写文件，不增加Wwise回调标志、不改变返回值。
- DLL在安装前固定在进程中，不支持游戏运行期间卸载。回滚必须先退出游戏。
- 不替换`dinput8.dll`、现有插件，不修改UE、动作、输入、存档或伤害。先前3个Lua包装层监听保留作诊断对照，其`*_audio.csv`**仍不能当EventID真值**。

## 原生入口依据

2026-10-18只读检查本机运行映像：`MonsterHunterRise.exe` SHA256 `29fa23a9ce24613a9b05ce84f7ebfa3262aaf5053b377b43fd8dfc09071aa7fc`，PE时间戳1689076825、映像大小317313024。

三个SDK包装器转发到`+0x4DBBBB0`并写入`+DA0`请求队列；队列消费者调用`+0x4DBD320`。引擎原生播放调用点`+0x4DCAF22`把数值事件、64位发声对象及flags送入`+0x732FF10`，后者调用公共核心`+0x732FFE0`；核心以首参数查事件哈希表。桥监听最后一个入口；首三参数为RCX/RDX/R8。**不猜完整参数个数或把返回值当EventID**。这些反汇编证据不等于证明实际游戏中已经有回调，仍须启动后检查。

原始诊断只写`Saved/Audio/RiseIG/NativeBridge/probe-*.json`；`probe_native_audio.py`只申请QUERY_INFORMATION|VM_READ，拒绝其他EXE，默认每段最多4096字节/16段。`--queue-consumers`仅检查已定位WwiseManager的320KiB代码区及只读异常表，不搜索玩家对象。

## 构建与定向验证

依赖不进Git：

- [MinHook官方v1.3.4](https://github.com/TsudaKageyu/minhook/tree/v1.3.4)，commit `c3fcafdc10146beb5919319d0683e44e3c30d537`，放在`Saved/Audio/RiseIG/NativeBridge/ThirdParty/minhook`；按固定标签获取，现有副本不重复下载。BSD/HDE许可见[THIRD_PARTY_LICENSES](THIRD_PARTY_LICENSES.txt)。
- 只读反汇编依赖Capstone5.0.6，已隔离到`Saved/Audio/RiseIG/NativeBridge/PythonDeps`；插件运行不依赖Python或Capstone。
- 接口适配只使用[REFramework官方API.h](https://raw.githubusercontent.com/praydog/REFramework/master/include/reframework/API.h)的major1稳定前缀：三整数版本、Initialize前三字段及`on_present`槽；不用新版SDK偏移。`required_version`只写原三整数，旧版大小也安全。
- 本机MSVC14.44.35207、WindowsSDK10.0.26100.0、CMake3.31、MASM，静态CRT。DLL只依赖Windows的KERNEL32/bcrypt。

从仓库根目录在x64 VS开发终端执行：

```text
cmake -S Scripts/Audio/NativeBridge -B Saved/Audio/RiseIG/NativeBridge/Build1444 -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS_DEBUG="/Z7 /Ob0 /Od /RTC1" -DCMAKE_CXX_FLAGS_DEBUG="/Z7 /Ob0 /Od /RTC1"
cmake --build Saved/Audio/RiseIG/NativeBridge/Build1444
ctest --test-dir Saved/Audio/RiseIG/NativeBridge/Build1444 --output-on-failure
```

开发环境使用`C:/apps/VisualStudio/VC/Auxiliary/Build/vcvars64.bat -vcvars_ver=14.44`。初次默认14.38的CMake编译器检查报C1902；已换独立Build1444目录，不改机器全局PATH、不杀PDB服务。SDK头网络下载失败后只定义上述公开稳定前缀，不反复下载/更改网络配置。

3项CTest均通过：固定队列/MPSC/溢出及真实本进程MinHook透传；生产IPC/CSV/重载收尾/禁止覆盖；实际DLL加载/导出/旧版版本结构/主版本拒绝。测试只在自己的进程和独立夹具执行，**不会在游戏安装挂钩**。Lua另有50项音频、17项动画/耐力检查及Lua5.4/LuaJIT整脚本编译通过，包含Windows真实文件的LF二进制IPC检查；原轨迹采样/动画读取代码与1.16.1逐字一致。

## F9、输出与时钟

仍在`reframework/data/MHGZ_AerialTrajectoryRecorder/`输出；新文件为同一F9 basename的`*_audio_native.csv`。

- `audio_native_bridge_command.txt`：单写者Lua、PING→START→STOP顺序确认；只接受完整END标记、有限白名单文件名及递增seq。Lua以`rb/wb`确保Windows不会把LF转换成CRLF。新nonce先PING；不执行任意代码或外部路径。启动时忽略磁盘残留命令。
- `audio_native_bridge_status.txt`：原子替换，报告ready、recording、observed、rows、dropped、error；Lua只承认自己nonce/待确认seq，不信旧进程状态。4秒没确认则提示错误，轨迹仍继续。
- `event_id`为原生入口首参数，`game_object_id`保持完整64位；`kind=post_requested`，`source_scope=all_world_unattributed`。**提交请求不等于声音已成功发出**，不把它伪称PlayingID或最终WEM。
- `qpc/native_elapsed_s`是真实原生高精度时钟；`native_present_frame`是桥的Present计数，不是动作源帧。`capture_time_estimate_s`仅用命令写入前最近Lua样本对齐，存在IPC/Render延迟；`events.csv`另记`native_audio_anchor`及确认时刻，不能宣称亚帧精确同步。动作实际帧仍看原`animation.csv`。

游戏启动即可有`[AUDIO NATIVE] READY ... observed=...`，不必先F9。如果ready但observed一直0，先查此层；不要重复要求全招式录制。实际调用成立后再短录空挥/真实命中，与M1清单的EventID对照；不要在本阶段接UE/M2。

## 本机部署/回滚

已部署到`F:/games/Steam/steamapps/common/MonsterHunterRise/`，Lua SHA256 `53a16b1c885070d3c84f61ae8726824c83f80b77c015d448966da8c63b74338d`；DLL SHA256 `54769751c44004df51f2844c0bfb0e239610844ce6d48adb470dc74dd22c971c`。

部署清单与1.16.1备份：`Saved/Maintenance/RecorderDeploy/20261018-AudioNative117/`。部署前后dinput8、D2D和RiseCharmEditorREF哈希一致。用户已重启，加载/实际调用证据见该目录`RuntimeRestart32184/`；首份F9审计见`Saved/Audio/RiseIG/NativeBridge/Capture131457/`，CSV与STOP状态相符。**暂时不需重启或重录，下一步按已有记录定位招式/命中音源并试听**。

回滚：正常退出游戏后，将该新DLL移出plugins（保存到同一备份目录），恢复`MHGZ_AerialTrajectoryRecorder.v1.16.1.lua`到原autorun路径；不动其他插件或已有CSV。仅移除本桥，不替换REFramework。本轮没有Git提交/推送。
