# 资产目录整理规范

## 当前结论

项目资产采用“系统域顶层、武器类型作为次级目录”的结构，不额外增加 `/Game/MHGZ` 或 `/Game/Combat` 顶层目录。

首批整理只处理低风险、项目自有资产：

| 类型 | 目标目录 |
|---|---|
| 虫棍 Gameplay Ability | `/Game/Blueprints/Ability/InsectGlaive` |
| 虫棍 Anim Montage | `/Game/Weapons/InsectGlaive/Anims/Montage` |
| 木桩蓝图 | `/Game/Blueprints/Monster` |
| 全局 Data Asset / Data Table | `/Game/Data` |

## 第一批执行结果

2026-08-06 已通过 UE 5.6 AssetTools 完成以下移动：

- `GA_IG_BaDao`、`GA_IG_R_TuCI` → `/Game/Blueprints/Ability/InsectGlaive`
- `AM_Shth_BaDao`、`AM_Shth_R_TuCi` → `/Game/Weapons/InsectGlaive/Anims/Montage`
- `BP_TrainingDummy` → `/Game/Blueprints/Monster`

`DA_IG_Combo` 已由引擎重存并指向新的 GA 路径。迁移后反向引用检查通过，3 个受影响蓝图编译为 0 error / 0 warning，`MHGZEditor Win64 Development` 构建成功。

迁移前 9 个未跟踪资产的校验备份位于 `Saved/AssetOrganizationBackup/phase1-pre-move`；该目录只用于故障恢复，不可在正常编辑流程中直接覆盖回 `Content`。

第二批输入目录整理也已完成：

- `IMC_MHGZ_Demo` → `/Game/Input/Contexts`
- 原 `/Game/Input/Triggers` 下 14 个 MHGZ InputAction → `/Game/Input/Actions/MHGZ`

两套同名 `IA_Move`、`IA_Look` 都被保留，未做合并。迁移后 15 个资产及其来自 `IMC_MHGZ_Demo`、角色、PlayerState 和 PlayerController 的反向引用全部通过新进程校验；3 个受影响蓝图编译为 0 error / 0 warning。迁移前输入资产备份位于 `Saved/AssetOrganizationBackup/phase2-input-pre-move`。

默认地图 Windows 迭代 Cook 覆盖了 524 个包。首次 Cook 发现 `Config/DefaultEngine.ini` 的 `GlobalDefaultGameMode` 仍指向 `BP_IG_GameMode` Redirector；资产记录确认其最终目标为 `BP_Demo_GameMode`，配置已改为直接引用 `BP_Demo_GameMode_C`。修正后的 Cook 成功完成。Redirector 资产本身暂时保留，等待以后执行全项目 Fix Up Redirectors 时再删除。

## 不可直接移动的内容

- 不得在资源管理器中移动或重命名 `.uasset`、`.umap`。
- `__ExternalActors__`、`__ExternalObjects__` 和地图必须作为独立批次处理。
- `/Game/Data/DT_WeaponComboConfig` 被 `Config/DefaultGame.ini` 直接引用，当前保持原位。
- `/Game/ThirdPerson/Lvl_ThirdPerson` 是默认地图，当前保持原位。
- `/Game/Characters/Mannequins` 同时包含模板、角色和虫棍源动画，第一批不移动。
- 资产移动不自动修复 C++ 或 INI 中的字符串路径；每一批必须单独扫描并同步修改。

## 执行方式

迁移清单位于 `Scripts/AssetOrganization/phase1.json`。`organize_assets.py` 默认只执行检查；只有显式传入 `--apply` 才会调用 Unreal AssetTools 批量移动资产。存在待移动资产时，脚本还会检查清单中的 `backup_directory`，并要求每个源包与备份的 SHA-256 一致。移动完成后还必须执行 Redirector 修复、蓝图编译、C++ 编译和地图加载验证。

## 后续批次

1. 虫棍源动画分类：先区分正式动画、`useless`、`unknown`、`XianJie`，再决定是否移动。
2. 输入去重：目录已归位，但 `IA_Move`、`IA_Look` 的模板版和 MHGZ 版仍分别有效，后续如需合并必须先重新绑定两个 IMC。
3. 武器美术：统一 `GUN`、`CHONG` 目录命名，但不在第一批同时改路径和资产名。
4. 模板隔离：`LevelPrototyping`、ThirdPerson 模板和无对应地图的 Variant 外部数据单独审查。
