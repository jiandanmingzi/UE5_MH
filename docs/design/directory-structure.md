# 目录结构

本文记录当前项目采用的目录规范。设计中但尚未实现的类型不列为“已存在文件”。

## Docs

```text
docs/
├── README.md           文档导航
├── using/              当前维护规范、状态、进行中的计划与已知问题
├── design/             当前玩法、架构、接口与未来设计
├── editor/             UE5资产接线、编辑器工具与按需验收
├── reference/          录制真值、规范曲线、动作值与声音映射
└── archive/            按日期保留的阶段审计、计划和维护流水
```

`docs` 根目录只放导航。日常任务先看 `using/README.md` 与 `using/current-status.md`，再按需读设计或编辑器文档。2026-10-26用户签收当前维护阶段，音效/协同计划与完整旧状态流水移入[当日归档](../archive/maintenance-2026-10-26/README.md)；using仅保留规范、精简状态与已知问题。未实现的未来设计保留在原计划或design中，不因归档删除，也不冒称已经完成。

## Scripts、Tools与本机产物

`Scripts/` 按资产诊断、资产组织、音效、Rise、World、Setup分域；根目录保留README、索引、`ue_remote.py`及`ue_asset_utils.py`共享入口。World录制器在`Scripts/MHWorld/`，用户级可选安装在`Scripts/Setup/`；不因整理重新部署。`Tools/`保存独立开发工具。

`Saved/`保存录制衍生数据、日志、维护报告及回滚备份；其中已入库的`_mhr_frames/`与`_mhr_curves/`是真值资产，不能按缓存删除。`Intermediate/`、`DerivedDataCache/`、`Binaries/`与IDE目录由工具管理，普通文件整理不清空或归档它们。

## Source

```text
Source/MHGZ/
├── ActionSystem/        GAS Ability、连招协调、攻击碰撞、伤害执行
├── AttributeSystem/     角色属性与武器专属资源
├── Data/                全局 DataTable/CurveTable 管理器
├── Equipment/           装备定义、实例与装备组件
├── InsectGlaive/
│   └── Kinsect/         猎虫 Actor、碰撞与品种 DataAsset 类型
├── InputSystem/         Enhanced Input、快捷栏与移动辅助
├── Inventory/           背包、仓库和物品结构
├── Monster/             怪物基类、肉质组件与训练木桩
├── UI/                  HUD、瞄准与资源 Widget 基类
├── MHGZCharacter.*
├── MHGZPlayerController.*
├── MHGZPlayerState.*
└── MHGZGameMode.*
```

源码按运行时职责划分。移动 `.h/.cpp` 时需要同步 include 和 `MHGZ.Build.cs`；原生类名和 `/Script/MHGZ` 不随源码文件夹改变。

`Source/MHGZEditor/`和`Source/MHGZToolsEditor/`是现有独立编辑器模块；Commandlet与资产作者工具归ToolsEditor。测试继续按M阶段保留，本轮不移动源码或改变模块依赖。

Demo 重构后 `UI/` 由 `MHGZHUD`、`MHGZMainHUDWidget`、通用/资源/准心 Widget 基类和 AimComponent 组成；现有空壳 `MHGZUISubsystem` 删除。`ActionSystem/` 的玩家动作 Notify 通过 RuntimeHost 的 ActionToken/Montage Registry 定位实例，不建立按武器类型分叉的 Notify 目录。

## Content：项目资产

```text
Content/
├── Audio/
│   ├── Combat/                       通用命中与碰撞音效（Demo 骨架）
│   └── UI/                           HUD、瞄准反馈音效（Demo 骨架）
├── Blueprints/
│   ├── Ability/InsectGlaive/       GA_IG_*
│   ├── Characters/Demo/
│   │   ├── Animation/
│   │   │   └── MotionMatching/     AnimBP、PoseSearch Database/Schema
│   │   └── BP_IG_Character、BP_PlayerState
│   ├── GameModes/Demo/
│   ├── Monster/TrainingDummy/      BP_TrainingDummy
│   └── PlayerController/Demo/
├── Characters/
│   └── Demo/
│       ├── Anims/                  Demo 自有收刀移动动画
│       ├── Materials/              M_Demo_*、MI_Demo_*
│       ├── Meshes/                 SKM_Demo_*、SK_Demo_*、PA_Demo_*
│       └── Textures/               T_Demo_*
├── GameplayCues/
│   ├── GCN_Hit_*                   当前伤害数字、猎虫和武器声音Cue
│   ├── InsectGlaive/               预留
│   └── Hit/                        预留；未为整理移动在用Cue路径
├── GameplayEffects/
│   ├── Core/
│   └── InsectGlaive/
├── Input/
│   ├── Actions/MHGZ/               项目 InputAction
│   ├── Contexts/                   IMC_MHGZ_Demo
│   └── Touch/
├── Environment/
│   └── DemoArena/                  训练平台网格、材质、纹理和原型交互资产
├── Maps/
│   └── L_DemoArena                 当前大型训练平台、空气墙与两种无限血量木桩；唯一默认地图
├── Monster/
│   └── TrainingDummy/
│       ├── Anims/
│       ├── Data/                   默认、Large和Hemisphere三份木桩配置
│       ├── Materials/
│       ├── Meshes/
│       └── Textures/
├── TemplateAssets/
│   ├── Characters/Mannequins/      UE 模板模型、材质、纹理、动作与 Control Rig
│   └── ThirdPerson/
│       └── Input/                  模板 InputAction 与 Mapping Context
├── UI/
│   ├── Common/                     预留通用控件
│   ├── Feedback/                   伤害数字等瞬时战斗反馈
│   ├── HUD/                        主HUD、准心、血量与耐力条
│   └── InsectGlaive/               猎虫耐力与三灯 UI
├── VFX/
│   └── Combat/Hit/                 通用命中特效
└── Weapons/
    └── InsectGlaive/
        ├── Anims/
        │   ├── Blueprints/         猎虫及武器专属 AnimBP
        │   ├── Montage/            虫棍 Montage
        │   └── Sequences/
        │       ├── Locomotion/     已确认的待机、行走与 Dash 动画
        │       └── Imported/       导入的虫棍动作序列
        │           ├── Review/
        │           │   ├── Unknown/
        │           │   └── UnusedCandidate/
        │           └── Transitions/
        ├── Audio/                  挥棍、猎虫、萃取与三灯音效
        ├── Data/                   WeaponRuntimeDefinition、虫棍物品/Combat/Input/Combo 与猎虫品种 DataAsset
        ├── Materials/
        │   ├── Glaive/
        │   └── Kinsect/
        ├── Meshes/
        │   ├── Glaive/
        │   └── Kinsect/
        ├── Textures/
        │   ├── Glaive/
        │   └── Kinsect/
        └── VFX/                    拖尾、猎虫和萃取专属特效
```

统一原则是“系统域顶层、武器类型作为次级目录”：GA 归 Ability，GE 归 GameplayEffects，GC 归 GameplayCues，Montage、猎虫和虫棍专属资源归武器目录。`/Game/Data` 只保存跨领域映射表，领域 DataAsset 放回所属系统；因此不再建立重复的顶层 `/Game/Kinsect`。

## Content：当前边界

- `/Game/Environment/DemoArena` 是训练平台使用的项目环境资源域；原始 FBX 与对应 `.uasset` 保持相同相对层级。
- `/Game/Maps/L_DemoArena` 是唯一地图，也是编辑器启动与游戏默认地图。
- 当前地图不使用 World Partition，项目中不再保留 `__ExternalActors__/` 或 `__ExternalObjects__/` 资产。
- `Weapons/InsectGlaive/Anims/Sequences/Imported/Review` 中的数字命名动画仍需人工预览；未经确认不得删除或按猜测重命名。
- ThirdPerson 模板 Input 已隔离至 `TemplateAssets/ThirdPerson/Input`，项目运行时输入保持在 `Input/Actions/MHGZ` 和 `Input/Contexts`；两者不得交叉引用。
- `TemplateAssets` 统一使用 ASCII 路径，避免 Git/UBT/CI 对 Unicode 目录的兼容性问题；项目自有资产不得迁入该目录。
- Demo 必需但尚无资产的目录使用 `.gitkeep` 保留；加入首个 `.uasset` 后应删除同目录的 `.gitkeep`。这些占位文件不属于 Unreal 资产，不参与引用或 Cook。

## 资产移动规则

1. `.uasset`、`.umap` 只能通过 Content Browser 或 AssetTools 移动。
2. 移动前生成源路径、目标路径、反向引用和冲突报告。
3. C++、INI 与普通字符串中的 `/Game/...` 路径必须手工同步。
4. 移动后定向核对目标资产、反向引用和Redirector；编译、Cook与PIE按实际影响及维护规范选择。
5. 地图与外部 Actor 永远作为独立批次处理。

迁移记录见 [资产整理记录](../editor/asset-organization.md)，可重复执行工具及用途见 [Scripts/README.md](../../Scripts/README.md)。

## 生成动画与候选资源（2026-10-25）

- `Anims/Sequences/Generated/`保留在用功能序列（四向翻滚保持段、急袭/强化操虫穿刺IP与RM等），不能因名称含Hold/IP误删。
- `Generated/Legacy/`是18份已退役的保持/视觉混合资产，保留追溯，不自动接回Montage。
- `Imported/Review/Unknown/`保留身份未确认序列；`Imported/Review/UnusedCandidate/`统一保存当前未使用候选，原`useless`已合并。
- 资产经UE AssetTools移动，按批核对字符串路径与引用；已有资产目录不再保留`.gitkeep`，空目录仍保留占位。

## 项目字段说明规范

- 项目自有`UPROPERTY`都写中文文档注释，包括编辑器可配置字段、只读快照和私有反射引用；编辑器使用UHT生成的ToolTip。已有显式`meta=(ToolTip=...)`同样使用中文，避免覆盖中文注释后仍显示英文。
- 蓝图自定义成员和函数局部变量通过变量说明元数据记录中文；字段名、GUID和类型保持稳定，不能为翻译改名而破坏资产引用或脚本入口。
- 说明先写用途，再按需要注明单位、范围、特殊值、何时生效，以及配置值或运行时快照的区别。未接入或兼容保留的字段明确标注，不暗示其已参与运行。
- 保留代码标识、引擎类型与常用技术名词；不为注释整理更改数值、属性标志、分类、图连线或玩法。
- 按需使用`MHGZFieldDocumentation -RequireChinese`检查实际原生/蓝图ToolTip，不以源码含注释代替编辑器元数据验证，也不因此恢复全量玩法回归。

## 虫棍资源组件的实现职责

同一个`URes_InsectGlaive`按职责分文件实现，类名、反射字段与外部接口不随文件拆分变化。定位维护问题时先读对应文件，跨域行为仍通过原成员调用，保持生命周期和回调顺序。

| 实现文件（`Source/MHGZ/AttributeSystem/`） | 职责 |
| --- | --- |
| `Res_InsectGlaive.cpp` | 运行期初始化/关停、猎虫装备与指令仲裁、通用猎虫伤害 |
| `Res_InsectGlaiveExtract.cpp` | 单灯/三灯、精华交付与成本预约、舞踏资源及UI快照 |
| `Res_InsectGlaiveMark.cpp` | 部位虫印Debuff、外表面特效定位、自动寻印与攻击 |
| `Res_InsectGlaivePowder.cpp` | 粉尘生成/寿命/自然爆炸、聚集预约与快照 |
| `Res_InsectGlaivePowderVortex.cpp` | 聚爆飞行、聚集表现、事务提交/取消与两段伤害 |
| 原有`CoAttack`、`DescendingThrust`、`GuaranteedHits`后缀实现 | 普通协同、急袭专属协同、已提交贯通保底伤害，保持原归属 |

`Res_InsectGlaivePowderPrivate.h`仅共享粉尘默认寿命常量，不新增反射字段或配置入口。招式调参仍以对应GA为准，资源共享规则以Combat配置为准。

## 动画实例的实现职责

`UMHGZMotionMatchingAnimInstance`保持原类和反射接口，在`Source/MHGZ/Animation/`按职责分文件实现：

| 实现文件 | 职责 |
| --- | --- |
| `MHGZMotionMatchingAnimInstance.cpp` | 动画更新、MM查询与移动路由、查询结果队列消费及最新选择缓存 |
| `MHGZMotionMatchingAnimInstanceTelemetry.cpp` | 更新后姿态采集、录制启停、事件与CSV编码、会话写入及Trace停止导出 |

队列的出队、缓存更新与录制开关判断仍在主文件；私有`AppendMMSelectionTelemetry`仅为已消费结果组装CSV行，不参与动画决策。采集调用点、字段、采样和刷新策略不随文件拆分变化。沿用现有运行时导出接口及编辑器服务，保留Trace文件就绪与打开失败保护；不新增模块、依赖或独立录制对象。

## 共享标签与纯函数

- `Source/MHGZ/MHGZGameplayTags.h/.cpp`统一提供已归并的22个共享标签查询入口。调用使用完整`MHGZGameplayTags::`限定名，函数内静态变量在首次使用时查询既有注册表；不改为全局静态提前查询，不重复注册标签。新增调用优先复用匹配的现有入口；标签值、层级或缺失报错策略不同的查询不能只因名称相似就合并。
- `Source/MHGZ/MHGZMath.h`保存无对象状态、无副作用的数学辅助函数。目前仅包含`MHGZMath::IsFiniteVector`，逐项检查X/Y/Z是否有限，不归一化、不排除零向量；调用方保留各自方向、长度和容错判断。领域曲线算法仍归原`ActionSystem`文件，伤害舍入仍归`MHGZDamageMath.h`。
- 共用入口的调用保持显式限定，不使用全局`using namespace`重新引入文件作用域冲突。涉及资源查找、Token/租约、CMC或蒙太奇收尾的代码另按生命周期核对，不能当成纯函数迁移。两份Target仍关闭Unity Build，本轮归并不代表所有文件局部名称冲突均已消除。

## 空中动作的公共清理步骤

`Source/MHGZ/ActionSystem/MHGZAerialActionCleanup.h/.cpp`提供明确传参的单步操作，不拥有动作生命周期，也不保存额外状态：

| 函数 | 职责与顺序 |
| --- | --- |
| `EndTaskAndReset` | 对非空任务先执行EndTask，再清空原TObjectPtr字段；不提前清空或改变委托 |
| `UnbindRuntimeInvalidation` | 从既有Host移除本动作的有效失效委托，再重置句柄；Host为空仍清自身句柄 |
| `UnbindStateTagDelegates` | 从既有ASC依次移除本动作保存的NewOrRemoved委托，再清空映射；不改变标签 |
| `RestoreMovementModeFromFloor` | 执行原FindFloor及MAX_FLOOR_DIST判据，只选择Walking或Falling |

各GA仍决定何时调用，以及与碰撞、计时器、蒙太奇、租约和Super的先后关系。地面模式恢复必须留在原Token/无后继动作/CMC为Flying等守卫内；公共函数不代替所有权判断。自然下坠、取消恢复、落地表现、Root Motion恢复及回调重入标志继续由各招式原实现负责。不能为了复用把“只清引用”“结束任务”“先解绑再结束任务”混为同一操作。

## 伤害数字查询与生命周期

`GameplayCues/MHGZDamageNumberCue.cpp`按目标的引擎`Owner/Children`关系查询近期数字，为同目标多段命中计算位置错位；无需遍历全世界数字或额外登记缓存。`Children`表示归属，不能替换为仅查ChildActorComponent或场景附着的接口。引擎负责归属转移与销毁移除，查询仍检查Actor有效性、世界/活动层级及原时间距离条件，计数到显示上限后停止。

`UI/MHGZDamageNumberActor.*`继续拥有WidgetComponent、寿命计时器和末段上飘/淡出；`MHGZDamageNumberWidget.*`负责文字及暴击样式。Cue消费已结算伤害，沿用原逐次生成与自动销毁流程，不把查询优化扩展为新的伤害计算、UI所有者或对象池。相关边界验证归原M6/UI测试目录。
