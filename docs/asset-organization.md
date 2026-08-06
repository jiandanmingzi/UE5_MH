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

两套同名 `IA_Move`、`IA_Look` 当时都被保留，未做合并。迁移后 15 个资产及其来自 `IMC_MHGZ_Demo`、角色、PlayerState 和 PlayerController 的反向引用全部通过新进程校验；3 个受影响蓝图编译为 0 error / 0 warning。迁移前输入资产备份位于 `Saved/AssetOrganizationBackup/phase2-input-pre-move`。

第三批虫棍美术目录整理已完成：

- `Materials/GUN`、`Meshes/GUN`、`Textures/GUN` → 对应的 `Glaive` 子目录
- `Materials/CHONG`、`Meshes/CHONG`、`Textures/CHONG` → 对应的 `Kinsect` 子目录

本批仅规范 26 个资产的目录名，没有修改资产名。迁移后新进程验证了材质、纹理、网格、骨架、PhysicsAsset、`DA_IG_HuoLongGun` 和骨架预览网格之间的反向引用；全库扫描未发现旧路径残留，资产数量保持为 954 个 `.uasset` 和 2 个 `.umap`，默认地图 Windows 增量 Cook 为 0 error / 0 warning。迁移前备份位于 `Saved/AssetOrganizationBackup/phase3-insect-glaive-art-pre-move`。

第四批开始整理虫棍 AnimSequence。UE 只读审计确认旧目录共有 148 个 AnimSequence，其中仅 6 个当前存在反向引用。为避免按拼音或数字编号误判动作语义，本批仅采用精确白名单，将 11 个名称明确的待机、行走和 Dash 动画迁移至：

`/Game/Weapons/InsectGlaive/Anims/Sequences/Locomotion`

`AS_Shth_Idle` 与 3 个 `AS_Shth_Walk_*` 的 `PSD_MH_Shth_Move` 引用已由 AssetTools 重写并通过新进程验证；另外 7 个动画当前无引用。迁移没有修改资产名，全库未发现本批旧路径残留，资产总数仍为 954 个 `.uasset` 和 2 个 `.umap`。迁移前备份位于 `Saved/AssetOrganizationBackup/phase4-ig-anims-locomotion-pre-move`。

默认地图 Windows 迭代 Cook 覆盖了 524 个包。首次 Cook 发现 `Config/DefaultEngine.ini` 的 `GlobalDefaultGameMode` 仍指向 `BP_IG_GameMode` Redirector；资产记录确认其最终目标为 `BP_Demo_GameMode`，配置已改为直接引用 `BP_Demo_GameMode_C`。修正后的 Cook 成功完成。

第五批完成 Redirector 收口。清理前确认 `BP_IG_GameMode` 是全库唯一的 `ObjectRedirector`，序列化目标为 `BP_Demo_GameMode`，目标资产存在且反向引用为 0。原包已备份至 `Saved/AssetOrganizationBackup/phase5-redirectors-pre-cleanup`，删除后由全新 UE 进程确认旧资产不存在、目标仍可加载，项目内 ObjectRedirector 数量为 0。清理后的基线为 953 个 `.uasset` 和 2 个 `.umap`。

第六批隔离 ThirdPerson 模板输入资产。引用审计确认 `IMC_Default`、`IMC_MouseLook` 没有项目运行时引用，4 个模板 InputAction 只被这两个 IMC 内部引用，因此保留资产但迁入：

- `/Game/ThirdPerson/Input/Contexts`
- `/Game/ThirdPerson/Input/Actions`

迁移后新进程验证 6/6 资产和 4 条内部引用均指向新路径，全库无旧路径残留；MHGZ 输入继续独立位于 `/Game/Input/Actions/MHGZ` 和 `/Game/Input/Contexts`。迁移前备份位于 `Saved/AssetOrganizationBackup/phase6-third-person-input-pre-move`。

## 不可直接移动的内容

- 不得在资源管理器中移动或重命名 `.uasset`、`.umap`。
- `__ExternalActors__`、`__ExternalObjects__` 和地图必须作为独立批次处理。
- `/Game/Data/DT_WeaponComboConfig` 被 `Config/DefaultGame.ini` 直接引用，当前保持原位。
- `/Game/ThirdPerson/Lvl_ThirdPerson` 是默认地图，当前保持原位。
- `/Game/Characters/Mannequins` 同时包含模板、角色和虫棍源动画，第一批不移动。
- 资产移动不自动修复 C++ 或 INI 中的字符串路径；每一批必须单独扫描并同步修改。

## 执行方式

迁移清单位于 `Scripts/AssetOrganization/phase*.json`。`organize_assets.py` 默认只执行检查；只有显式传入 `--apply` 才会调用 Unreal AssetTools 批量移动资产。存在待移动资产时，脚本还会检查清单中的 `backup_directory`，并要求每个源包与备份的 SHA-256 一致。`audit_animation_assets.py` 和 `audit_redirectors.py` 都是只读审计工具，报告写入 `Saved/AssetOrganization`。移动或清理完成后还必须按受影响资产类型执行 Redirector、反向引用、蓝图编译、C++ 编译和地图 Cook 验证。

## 后续批次

1. 虫棍源动画分类：旧目录还剩 137 个动画；攻击、空中复合动作以及 `useless`、`unknown`、`XianJie` 下的 32 个数字命名动画继续保持原位，等待人工确认语义。
2. 模板隔离：`LevelPrototyping`、其地图外部 Actor 和无对应地图的 Variant 外部数据必须作为整体单独审查。
