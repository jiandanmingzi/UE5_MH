# Monster Hunter: World 运行时轨迹录制器

`MHGZ_MHW_AerialTrajectoryRecorder.lua` 是基于 LuaEngine 的只读录制脚本。按 F9 开始／停止，也可使用 LuaEngine 的 ImGui 面板。它在 `on_time` 回调中逐次采样，不承诺固定 60 Hz；停止时会汇报平均采样频率、最大采样间隔和超过 25 ms 的间隔数。

## 部署与当前安装状态

2026-10-01 已在本机游戏目录自动部署：

- LuaEngine **1.3.2**：来自[作者的 GitHub 正式发布](https://github.com/HalcyonAlcedo/LuaEngine/releases/tag/v1.3.2)。下载包 SHA-256：`809540bce40cc13a2c6b8d4d44a27895bbce668949b9ce0c4d463e6d97006937`。
- LuaEngine 核心、`Lua/modules` 解析模块和 `nativePC/plugins/LuaEngine.dll` 已安装。
- 2026-10-06 已将修复版 `0.2.6` 部署到游戏目录，SHA-256 为 `75AED9E221F3E6BFC9072C786C607AB7F30FC7C177177DDA2E7208290B98473E`；原 `0.2.4` 已备份为 `MHGZ_MHW_AerialTrajectoryRecorder.lua.codex-backup-20261006_before_026`。随后实机日志确认 0.2.6 已加载，但 `SearchPattern` 偶尔会把 `Engine_aob_Player` 缓存成错误候选地址。现已部署 `0.2.7`，SHA-256 为 `5125B96E0CC935CDC9BF1646A8492A030EBA6931B1FD2EE1513017A99DDF42A3`；0.2.6 部署件备份为 `MHGZ_MHW_AerialTrajectoryRecorder.lua.codex-backup-20261006_before_027`。游戏进程仍需 reload 后短测。
- 2026-10-02 已部署 Stracker Loader **v4.0**：`dinput8.dll`、`loader.dll`、配套 `QuestLoader.dll` 和 `MonsterLoader.dll`。安装包 SHA-256：`F0792D6015DF96EEB9239DE79A352B2CF9132075A4F3347EE97DEC879807FCBB`；部署文件已逐项复核 SHA-256。
- 旧 `dtdata.dll` 已改名保存在游戏根目录的 `dtdata.dll.codex-backup-20261002_165557`，便于回滚。原 `loader.dll`、`loader-config.json`、QuestLoader 和 MonsterLoader 也各有同时间戳备份。`LuaEngine.dll` 未覆盖，安装前后 SHA-256 一致。
- `loader-config.json` 保留诊断设置：`logfile=true`、`logcmd=true`、`logLevel=INFO`，并启用插件加载。
- 可选 `LuaEngineUI` 没有安装，因此 ImGui 面板不可用；录制控制使用 **F9**。

安装文件曾做 SHA-256 复核；诊断日志已验证 F9 边沿、脚本回调及 CSV 创建。2026-10-03 14:50–14:52 的日志进一步确认：多次 F9 按下和停止均被捕获；0.2.2 每次都进入 `capture_started`，但 `engine.Player` / `engine.World` 暴露的角色状态字段全为 0（包括生命、动作编号/帧、地图编号和任务时钟），旧逻辑因此把每帧拒绝，最终得到 0 行。这不是 F9 未触发。

2026-10-03 曾发现按 F9 开始录制时画面冻结、声音继续。0.2.2 起普通错误处理不再调用 `Message()` / `Console_Info()`，避免在异常路径上反复触发 UI/日志桥接。由于无提示让录制状态难以确认，0.2.4 对成功开始和停止各调用一次短 `Message()`，并在调用前后写入 `native_prompt_begin` / `native_prompt_end` 诊断标记；开始提示发生在 CSV 头与开始事件已 flush 之后，停止提示发生在文件关闭之后。LuaEngine 的 `on_time` 是游戏时钟钩子回调，因此这仍需本机短测：如果 F9 后卡住，诊断日志最后只有 `native_prompt_begin`，说明卡在提示 API；CSV 头/先前数据已经落盘。

0.2.3 修正“猎人数据全零导致整帧丢弃”：每次开始录制时重新创建 Player/World 包装对象；猎人字段仍不可用时将角色/世界字段留空、写入 `hunter_state_available=0`，但继续记录可用的按键、输入边沿、相机/准心等字段。2026-10-03 在明确可操作场景的 0.2.3 录制中，F9 和输入行均正常，但所有样本的猎人字段仍不可用。检查本机 LuaEngine 模块后发现 `Engine_aob_Player` / `Engine_aob_World` 是跨脚本共享的进程地址缓存，`Player:new()` / `World:new()` 仅在缓存为 0 时才重新 `SearchPattern`。0.2.4 因此在开始录制时记录缓存根地址、Player/Map/World 指针链及四元数/生命上限探针；重扫成功才保留新缓存，失败时恢复原共享缓存。不会每帧扫描，也不写入游戏内存。

**0.2.6 修复（已实机验证但仍有缺陷）：** 2026-10-06 修复了 `resolve_sources` 前置声明和重扫异常后的缓存恢复，也增加第二次有限重试及“猎人/动作数据为 0 行”的明确提示。实机日志确认 0.2.6 能成功加载，且有一次重扫找到有效猎人数据；但后续重载时 `SearchPattern` 又返回无效玩家根地址，连续录制仍为 0 行。详见 0.2.7 对误匹配的处理。

**0.2.7 修复（已部署；待 reload/实机验收）：** 0.2.6 的新日志证明脚本重扫流程已运行，但当前 `Engine_aob_Player=5452672016`，其 `{0x50}` 玩家指针链为空；重扫仍得到同一无效候选，于是被安全回滚。对照游戏随 LuaEngine 安装的 `Lua/modules/Engine_player.lua`，其玩家签名是短通配模式 `{0x20, 0x67, ??, ??, 0, 0, 0, 0}`，模块还内置备用根地址 `0x1450139A0`。历史有效样本中的玩家根地址 `5452675488` 正好等于该备用地址，且四元数范数为 1、生命上限为 200、地图 ID 有效。0.2.7 保留签名扫描优先；仅当扫描结果未通过玩家指针链及四元数/生命有效性探针时，才尝试 LuaEngine 自带备用地址，并且仍须通过同样的实时探针验证才接受。重扫只重建 Player 根，不再清空/改写正常的 World 根；若签名扫描和备用地址均未通过，则恢复旧 Player 缓存并报告失败，不盲目写入未经验证的根地址。

本次诊断应使用 `Scripts/MHWorld/loader-config.diagnostic.json` 中的配置（`logfile=true`、`logLevel=INFO`），以便在 LuaEngine 日志/控制台查看脚本加载、reload 和脚本错误。诊断后可将原配置恢复为用户此前的 `logfile=false`、`logLevel=ERROR`。脚本名是文件名去掉 `.lua`：`MHGZ_MHW_AerialTrajectoryRecorder`；LuaEngine 游戏聊天指令为 `reload MHGZ_MHW_AerialTrajectoryRecorder`，重载成功会由 LuaEngine显示确认消息。

在实际可操作的游戏场景做一次 3–5 秒短录制：按 F9，应看到“录制中：按 F9 停止并保存”；移动角色并按几次按键，再按 F9，应看到停止行数/频率提示。检查诊断日志有 `native_prompt_begin` 和 `native_prompt_end`，以及 `f9_state`、`capture_started`、`capture_stopped`；确认 `Lua` 目录出现对应的 `samples`、`events` CSV。检查 `hunter_state_available` 是否为 1，并查看 `player_root_probe` / `aob_refresh_result` 判断数据链路。如果 F9 后游戏卡住，停止继续测试，查看日志最后是否停在 `native_prompt_begin` 或 `aob_refresh_begin`，据此区分 UI 提示阻塞与 AOB 扫描阻塞。

## 记录内容

- 角色 XYZ 坐标、相邻采样位移、任务时钟差分速度、LuaEngine 提供的内部增量坐标。
- 角色朝向四元数、原始欧拉角，以及从四元数推导的朝向向量、水平 yaw、pitch、逐样本角度变化和角速度。推导值按 RE Engine 的 Y-up 世界、角色本地 +Z 前向计算；原始四元数始终保留，便于离线复算。
- LMT 动作编号、FSM 编号与目标、动作帧、结束帧、速率和倍率。
- 血量、耐力、重力、下落速度、是否离地。
- 准心瞄准状态、直线／抛物线准心目标；武器位置、类型、ID 和命中对象值。
- LuaEngine `GetCameraData` 暴露的 XYZ 与锁定标记（不等于相机旋转）。瞄准时额外从角色位置指向准星世界点，记录归一化方向、yaw/pitch，以及相对角色朝向的水平角；这是准星方向代理，不冒充相机姿态。
- LuaEngine `engine.keypad(..., true)` 暴露的 Xbox 数字按键和左右摇杆八方向布尔状态，以及派生的按下和释放事件；同时提供由方向布尔值拼出的 `input_*_stick_digital_x/y/magnitude`。
- `events` 文件记录录制启停、按键变化、动作/FSM 变化、场景切换和时钟回退。

### 能读到什么、读不到什么

本机 LuaEngine 1.3.2 的 `engine.keypad` 通过 `XCheckKeyIsPressed` 读取手柄按钮。`L↑/→/↓/←` 与 `R↑/→/↓/←` 是方向布尔值，不是模拟轴：digital x/y 只表示粗略方向（-1/0/+1），magnitude 可能为 0、1 或 √2，绝不能用于分析摇杆推了几成。0.2.6 已包含可选 XInput 伴随记录读取；若伴随进程未运行、没有连接 XInput 手柄或样本失效，`input_axis_*` 仍为空且 `input_axes_supported=0`。伴随进程的实机联测状态见下文。

`GetCameraData` 只暴露 XYZ 和锁定标志，没有可用的相机旋转接口。脚本不猜测相机角度；只有角色处于瞄准状态且准星世界点有效时，才输出 `collimator_dir_*`、`collimator_yaw_deg`、`collimator_pitch_deg` 和 `collimator_relative_yaw_deg`。这些量能回答“准星朝哪里”，但不能回答相机对象本身的旋转。

`facing_yaw_yup_local_z_deg` 从角色四元数计算，采用 Y-up、角色本地 +Z 前向。`facing_yaw_delta_deg` 使用带 ±180° 环绕处理的相邻样本差；`facing_yaw_rate_deg_s` 用同一对样本的任务时钟间隔计算。因此可以直接定位角色朝向何时开始变化、变化方向及变化速度，而不用把旧的 `euler_y_rad` 误当成水平 yaw。若后续验证发现游戏角色本地前向轴不是 +Z，应保留原始四元数并离线调整派生公式。

速度、角速度根据 `engine.World.Time`（任务时钟）和相邻样本计算。若时钟未前进或回退，速度留空，回退记在 events 中。停止时的频率统计也用有效任务时钟间隔计算。时钟刻度、坐标单位和采样完整性仍需本机录制后核验，不能预设为固定 60 Hz 或厘米/秒。LuaEngine 的 `on_time` 在框架内部 Lua 锁争用时可能跳过回调。

## 本次增强与使用方法

新版本在原 CSV 末尾追加派生字段，保留既有字段名和顺序，旧的按列名读取方式不受影响。用 `facing_yaw_delta_deg` / `facing_yaw_rate_deg_s` 查看三段角色水平转向；用 `collimator_relative_yaw_deg` 对照每段转向前后的瞄准方向；用 `input_left_stick_*_digital_*` 检查摇杆是否有方向输入。输入事件仍记录各方向按钮的边沿，采样行则给出每帧状态。

开始分析转向机制时，按同一录制里的 `action_lmt_id`、`action_frame` 和 `quest_time_s` 分段。若 `facing_yaw_delta_deg` 在连续样本中平滑分布，说明可观测朝向是逐样本变化；若单个样本出现接近目标角度的大跳变，则是离散跳变或采样间隔不足。单靠这份只读遥测不能区分角色代码插值、动画根骨旋转或其他引擎层驱动；要确认具体实现仍需对照动作/骨骼运动和更密的帧采样。

部署后若游戏进程仍运行旧脚本，先按一次 F9 完成当前可能仍在进行的录制，再执行 `reload MHGZ_MHW_AerialTrajectoryRecorder`，确认诊断日志 `script_loaded` 版本为当前部署版本。随后在可操作场景做 2–3 秒短录制，验收：F9 开始/停止各有屏幕提示；CSV 有样本和按键边沿；`hunter_state_available=1` 表示猎人对象可读；诊断日志显示 `aob_refresh_result` 且注明 `source=pattern_scan` 或 `source=module_fallback`。若重扫显示 `aob_refresh_candidate_rejected`，最终必须由有效探针验证后才能接受。停止提示若显示“猎人/动作数据为 0 行”，不要把该录制用于动作分析。若卡顿，依据最后的 `native_prompt_begin` 或 `aob_refresh_begin` 定位；不要连续反复按 F9 或反复 reload。

Stracker Loader v4.0 已完成静态部署与哈希验证。0.2.4/0.2.6 的部署哈希是历史记录；0.2.7 已部署，仍需在游戏进程中确认 reload 成功、屏幕提示可见、AOB 重扫来源及猎人字段恢复情况。只有短测通过后，再开始正式取样。

### 可选：原始模拟摇杆轴伴随采集（Lua 读取已部署，伴随程序尚未完成实机联测）

LuaEngine 的按键接口只返回方向按钮状态，不能还原摇杆幅度。项目现在提供 `Scripts/MHWorld/capture_mhw_xinput.py`：通过 Windows XInput 默认每秒采样 120 次原始摇杆、扳机、按钮掩码和高精度时钟；不访问或修改游戏内存。XInput 查询前后分别取 QPC，记录中点时间和查询耗时；Lua 每行保存它当时读到的最新样本时间戳。`quest_time_s` 与 `input_axis_qpc_ns` 可用于拟合游戏时间到 XInput 时间的对应关系，但不是同一瞬间的硬件输入快照；应从原始 CSV 与重复/跳跃时间戳估计采样滞后，不能声称零延迟。归一化值不施加死区；原始整数轴值保留在独立 CSV。

先检查控制器是否由 XInput 暴露：

```powershell
& 'C:\apps\anaconda\python.exe' 'D:\study\MH\MHGZ\Scripts\MHWorld\capture_mhw_xinput.py' --probe
```

开始正式录制时，在独立 PowerShell 窗口运行：

```powershell
& 'C:\apps\anaconda\python.exe' 'D:\study\MH\MHGZ\Scripts\MHWorld\capture_mhw_xinput.py' --rate-hz 120
```

保持该进程运行，再启动游戏/重载已部署的 Lua 记录器并按 F9。原始控制器文件会以唯一文件名写入 `Saved/AssetProbe/DescendingThrust/XInput/`；`live_state.pipe` 是供 Lua 读取的临时状态文件，按 Ctrl+C 正常结束时会删除。`capture.lock` 是固定路径的一字节互斥锁文件，会留在磁盘上；实际锁由 Windows 文件锁维护，进程正常退出或异常终止时由系统释放。不要在采集器运行时删除它。采集器拒绝第二个并发实例。强制结束时，带 PID/session 后缀的 `.tmp` 临时文件可能残留，不会被下一次会话复用。若异常退出遗留 `live_state.pipe`，先确认没有伴随进程，再只清除此专用临时状态文件。若伴随程序未运行或无 XInput 控制器，模拟轴列会为空；Lua 记录器仍可记录动作、按键和相机数据。本机当前已部署 0.2.7，但尚未完成伴随采集程序的真实手柄联测。

新 Lua 版本保留旧 CSV 列顺序，只在末尾追加 `input_axis_qpc_ns`、UTC 时间、控制器索引/连接状态、XInput Packet/按钮掩码/扳机、session id 和 `input_axis_poll_duration_ns`。时间戳要在当前 Lua 会话中至少前进一次才会标记为有效；这样重载后遗留的旧 `live_state.pipe` 不会被当作有效轴值。重复时间戳超过容许次数时同样标记无效。`input_axis_l_x/y` 与 `input_axis_r_x/y` 是归一化到 `[-1,1]` 的当前轴值；没有运行伴随程序、没有连接 XInput 控制器或样本已陈旧时，`input_axes_supported=0` 且轴值留空。原始 CSV 与 Lua 行引用同一 QPC 单调时钟；Lua 行引用的是“读取到的最近一次控制器样本”，并未测量从游戏回调到读取时刻的精确延迟。当前本机一次 `--probe` 未发现已连接的 XInput 控制器，故尚未完成真实手柄联测；正式录制前要让 Windows/XInput 能看到手柄。

纯逻辑测试：

```powershell
& 'C:\apps\anaconda\python.exe' -B 'D:\study\MH\MHGZ\Scripts\MHWorld\test_capture_mhw_xinput.py'
```
