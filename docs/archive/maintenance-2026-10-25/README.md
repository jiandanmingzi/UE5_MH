# 维护历史归档（2026-10-25）

[状态历史快照](status-history.md)完整保留本次整理前约197KB的维护记录，包括音效、移动、编辑器优化、有效签收和各次验证证据。日期对应的“当前任务”“下一步”保持原文，追溯时使用；当前续接统一看[维护状态](../../using/current-status.md)。

本次文件整理：

- 四份 M0–M3 阶段审计归入[Demo审计档案](../demo-2026-10-13/audits/README.md)，正文和未来设计保留。
- World 录制器与说明归入 `Scripts/MHWorld/`；一次性 MCP 安装脚本归入 `Scripts/Setup/`，没有执行部署或修改游戏安装。
- 18份零引用合成保持/混合动画归入 `Generated/Legacy`，7份候选动画从 `Review/useless` 合并到 `Review/UnusedCandidate`；保留资源，不删除动画。
- 删除10个已有资产目录中的多余 `.gitkeep`，保留空目录所需占位。

资产清单见[迁移Manifest](../../../Scripts/AssetOrganization/Manifests/project-files-20261025.json)。本机备份、移动前引用审计、文件移动清单和验证报告位于 `Saved/AssetOrganization/ProjectFiles20261025/`；源包备份按原始相对路径保存。运行时包的未提交修改保持，本轮不提交或推送Git。

最终检查：25份迁移资产独立回载成功，旧源包与重定向器0份，其余786个Content文件哈希保持；91份Markdown的本地文件链接检查通过，历史快照保留原92个章节。录制Lua和安装PowerShell移动前后字节一致，PowerShell语法解析通过。只更新两个历史检查脚本的说明，标明旧固定数量/占位基线不作为当前验收；没有运行回归、全量资产验证或重部署。
