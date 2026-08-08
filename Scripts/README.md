# 项目维护脚本

`Scripts` 只保存仍可重复使用的维护工具，不参与游戏运行时、打包或启动流程。阶段迁移清单和一次性修复脚本在验证完成后删除；历史操作仍可通过 Git 记录和 `docs/asset-organization.md` 追溯。

## AssetOrganization

| 脚本 | 用途 | 写入行为 |
|---|---|---|
| `audit_animation_assets.py` | 读取动画类型、Skeleton、Root Motion 与反向引用 | 仅向 `Saved/AssetOrganization` 写报告 |
| `audit_asset_paths.py` | 审计显式传入的 `/Game/...` 包、资产类型与反向引用 | 仅向 `Saved/AssetOrganization` 写报告 |
| `organize_assets.py` | 按显式 JSON manifest 移动逐个 Unreal 资产 | 默认只读；只有 `--apply` 才移动资产 |
| `organize_asset_roots.py` | 按显式 JSON manifest 移动完整资产根目录 | 默认只读；只有 `--apply` 才移动资产 |
| `verify_demo_structure.py` | 用普通 Python 检查 Demo 目录骨架、占位文件与关键 DataAsset 位置 | 只读 |
| `verify_project_assets.py` | 在 UnrealEditor-Cmd 中检查最终资产数量、旧路径、Redirector、Skeleton 并编译蓝图 | 仅向 `Saved/AssetOrganization` 写报告 |

两个整理脚本不再绑定某个历史阶段的默认清单，调用时必须显式传入 `--manifest=<绝对或相对路径>`。所有实际移动仍须先备份，并在新 UE 进程中验证引用关系。

## Maps

`Maps/manage_demo_arena.py` 用于重建或核验 `/Game/Maps/L_DemoArena`。默认只读；`--create` 才会创建或重存 10×10 封闭训练场。旧地图和 External Actor 的一次性删除逻辑已经移除。
