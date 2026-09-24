# M5 工作文档导航

> 2026-09-24 整理。先看本页，再按任务进入对应文档；较早 PIE 记录、测试计数和试验性修法不能覆盖后来的结论。项目阶段门禁以[里程碑表](../design/milestone-gates.md)为准，Rise 数值以[参考资料](../reference/README.md)及其生成脚本为准。

## 当前状态（2026-09-24 换机前快照）

| 项 | 值 |
|---|---|
| 构建 | `Build.bat MHGZEditor Win64 Development` → `Result: Succeeded` |
| 全量自动化 | `Automation RunTests MHGZ` = **117 项 117 成功 / 0 失败、`EXIT CODE 0`**（`MHGZ.M5` = **29/29**）—— 这是套件**首次全绿**（PMM 那条自 2026-09-15 起一直是红的） |
| 资产校验 | `-run=DataValidation` → **592 个资产，`0 error(s), 0 warning(s)`** |
| 本轮完成 | P0-1 空回接缝、P0-2 的 §2.1–2.3（生产链路测试 + 受控反例由第二人独立复现）、P1-1、P1-3；PMM `PoseSearchControlNotifies` 已修（17 条序列的 notify 轨名归位） |
| **未兑现**（逐条清单见[空回 P0/P1 方案](空回P0-P1完整修复方案.md)的「本轮收束」） | §2.2 的姿势两列量化断言；**§2.4 的覆盖面（提前触地 / 被新空中动作打断 / 白灯·无白灯 / 不同朝向）**；§3.4 的落地尾段位移数值判据；**修后 PIE 原始录制为空**；`slomo 0.1` 未说明是否执行 |
| 待点名才动的范围外项 | 「入口硬零」族（突刺 466/469、突进回旋斩 193/193、地面前翻滚 4/11）与 P1-4（落地重设被到期 RMS 的 `FinishVelocity` 冲掉），都在[项目已知的不重要问题](项目已知的不重要问题.md) |

**复现 / 验证命令**（headless，需先关编辑器；`<引擎>` = `Engine/`，`<abs>` = 工程绝对路径）：

    # 1) 编译
    "<引擎>/Build/BatchFiles/Build.bat" MHGZEditor Win64 Development -Project="<abs>/MHGZ.uproject" -WaitMutex -NoHotReloadFromIDE -architecture=x64
    # 2) 全量自动化（117 项）
    "<引擎>/Binaries/Win64/UnrealEditor-Cmd.exe" "<abs>/MHGZ.uproject" -unattended -nop4 -nosplash -NullRHI -DDC-ForceMemoryCache -stdout -ExecCmds="Automation RunTests MHGZ;Quit" -TestExit="Automation Test Queue Empty" -log
    # 3) 资产校验（592 个资产）
    "<引擎>/Binaries/Win64/UnrealEditor-Cmd.exe" "<abs>/MHGZ.uproject" -run=DataValidation -unattended -nop4 -nosplash -NullRHI -DDC-ForceMemoryCache -stdout -log
    # 4) PMM notify 轨名自查（只报告；加 -Apply 才写回资产）
    "<引擎>/Binaries/Win64/UnrealEditor-Cmd.exe" "<abs>/MHGZ.uproject" -run=MHGZPMMNotifyTrackRepair -unattended -nop4 -nosplash -NullRHI -stdout -log

⚠ 两条踩过的坑：①**不要**改成重跑 `MHGZPMMAssetFixupCommandlet` —— 它的 Stop 块起点是固定 `0.12`，与 PMM-7.1 从提交键推导的口径冲突，重跑会改坏生成停步的时序；②`-run=pythonscript` **读不了** `Notifies` / `AnimNotifyTracks`（Python 反射里是 protected），那种脚本会**静默「零发现」**，看着像「没问题」。

| 要查什么 | 入口 | 阅读边界 |
|---|---|---|
| M5 还要制作哪些空中招式、资产和验收 | [M5 总计划](m5-plan.md) | 阶段 D/E 是工作清单；前面的测试时间线和附录是历史记录。 |
| 当前空回接缝与落地速度的 P0/P1 验收 | [空回 P0/P1 方案](空回P0-P1完整修复方案.md) | “当前决策”至“交付门槛”为现行方案；后面的逐轮试验仅供判因追溯。 |
| 下落重力、落地 `337 cm/s` 与尚缺的 `188` 特例 | [空中下落现状与缺口](空中下落实现缺口.md) | 只列实现事实及尚需完成/确认的配置，不再重复空回交棒试验日志。 |
| 已决定暂缓或尚未复核的观感问题 | [项目已知的不重要问题](项目已知的不重要问题.md) | “仍存在”和“待复核”分开标记；不作为当前 P0/P1 修复门槛。 |

`空回坠落顿挫与项目现状审查.md` 是 2026-09-23 20:00 回退版的快照，后续结论已推翻其中部分“当前待办”，本次移除；需要逐帧旧证据时可从 Git 历史读取。`空中动作已知问题.md` 已在前一轮更名并合并到低优先级问题单，不再使用旧文件名。
