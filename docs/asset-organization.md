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

第七批将剩余 137 个已确认来源的导入虫棍 AnimSequence 从角色模板域迁入 `/Game/Weapons/InsectGlaive/Anims/Sequences/Imported`。本批不按招式含义重命名，也不删除候选资产；原有处置分组映射为：

- `unknown` → `Imported/Review/Unknown`
- `useless` → `Imported/Review/UnusedCandidate`
- `XianJie` → `Imported/Transitions`

迁移后新进程验证 137/137 目标资产及 `AM_Shth_BaDao`、`AM_Shth_R_TuCi` 两条 Montage 引用。Asset Registry 不再包含旧目录依赖；`AS_Shth_BaDao` 的 Interchange 导入设置仍保留历史 `contentImportPath`，它属于重导入元数据，不是资产引用。迁移前 94.43 MiB 备份位于 `Saved/AssetOrganizationBackup/phase7-insect-glaive-imported-animations-pre-move`。

第八阶段的数字动画语义复审按项目决定跳过。第九阶段删除了 Body 下 3 个历史备份资产，并将 Demo Body、Hair、Head 的 SkeletalMesh、Skeleton、PhysicsAsset 统一为 `Demo` 命名。新进程确认虫棍目录中的 150 个动画资产全部引用 `SK_Demo_Body`；该阶段独立提交为 `aa8d33f`。

第十阶段完成 Demo 美术命名、模板隔离和地图替换：

- 99 个 Demo/虫棍资产统一使用 `SKM/SK/PA/M/MI/T` 类型前缀、`Demo` 或 `IG_Glaive/IG_Kinsect` 主体名及 PascalCase 部位名；贴图保留 `ALBD/NRMR/NRRT` 等原始通道后缀。
- Demo 收刀移动动画及角色模型、材质、纹理迁入 `/Game/Characters/Demo`；虫棍和猎虫美术保持在 `/Game/Weapons/InsectGlaive`。
- 128 个 UE Mannequin 模板资产和 7 个 ThirdPerson 支持资产归档到 `/Game/系统自带`，新进程幂等验证为 135/135。
- 新建 `/Game/Maps/L_DemoArena`：10×10 个 2 米地砖组成 20m×20m 平台，四周为 3 米围墙，包含 PlayerStart、`BP_TrainingDummy` 和基础天空光照，共 110 个 Actor。
- `GameDefaultMap`、`EditorStartupMap` 和 `SimpleMapName` 均已切换到 `L_DemoArena`；旧 `NewMap`、`Lvl_ThirdPerson` 以及 507 个旧 External Actor/Object 包已删除。

三个 Variant 的 440 个 External 包因宿主地图早已不存在，UE 无法加载其外部容器；这些包在逐文件 SHA-256 对照备份后，按 6 个精确目录从文件系统清除。地图删除批次共 511 个文件，备份位于 `Saved/AssetOrganizationBackup/phase10-map-cleanup-pre-delete`。当前基线为 443 个 `.uasset`、1 个 `.umap`，地图审计结果为旧地图 0、外部包 0、源端残留 0。

## 不可直接移动的内容

- 不得在资源管理器中移动或重命名 `.uasset`、`.umap`。
- `__ExternalActors__`、`__ExternalObjects__` 和地图必须作为独立批次处理。
- `/Game/Data/DT_WeaponComboConfig` 被 `Config/DefaultGame.ini` 直接引用，当前保持原位。
- `/Game/Maps/L_DemoArena` 是当前唯一默认地图，删除或改名时必须同步三个配置路径。
- `/Game/系统自带` 只用于模板资产；项目自有运行时资产不得回流该目录。
- 资产移动不自动修复 C++ 或 INI 中的字符串路径；每一批必须单独扫描并同步修改。

## 执行方式

迁移清单位于 `Scripts/AssetOrganization/phase*.json`。`organize_assets.py` 默认只执行检查；只有显式传入 `--apply` 才会调用 Unreal AssetTools 批量移动资产。存在待移动资产时，脚本还会检查清单中的 `backup_directory`，并要求每个源包与备份的 SHA-256 一致。`audit_animation_assets.py` 和 `audit_redirectors.py` 都是只读审计工具，报告写入 `Saved/AssetOrganization`。移动或清理完成后还必须按受影响资产类型执行 Redirector、反向引用、蓝图编译、C++ 编译和地图 Cook 验证。

## 后续批次

1. 导入动画复审目前跳过；`Imported/Review` 与 `Imported/Transitions` 的数字命名动作继续保留，不自动删除。
2. 后续工作回到虫棍战斗系统实现与 Demo 演示验证；`LevelPrototyping` 仅作为训练平台环境依赖保留。
