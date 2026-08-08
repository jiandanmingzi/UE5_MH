# 目录结构

本文记录当前项目采用的目录规范。设计中但尚未实现的类型不列为“已存在文件”。

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
├── Data/                           仅跨领域映射表；DT_WeaponComboConfig 固定在此
├── GameplayCues/
│   ├── InsectGlaive/
│   ├── Hit/
│   └── ...                         以后按 GameplayCue Tag 根节点扩展
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
│   └── L_DemoArena                 10×10 格封闭训练平台；项目唯一地图
├── Monster/
│   └── TrainingDummy/
│       ├── Anims/
│       ├── Data/                   DA_TrainingDummy
│       ├── Materials/
│       ├── Meshes/
│       └── Textures/
├── TemplateAssets/
│   ├── Characters/Mannequins/      UE 模板模型、材质、纹理、动作与 Control Rig
│   └── ThirdPerson/
│       └── Input/                  模板 InputAction 与 Mapping Context
├── UI/
│   ├── Common/                     血量、耐力等通用控件
│   ├── Feedback/                   伤害数字等瞬时战斗反馈
│   ├── HUD/                        主 HUD 与准心
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
        ├── Data/                   虫棍定义、连招和猎虫品种 DataAsset
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
4. 移动后验证目标资产、反向引用、Redirector、蓝图编译和 C++ 构建。
5. 地图与外部 Actor 永远作为独立批次处理。

迁移记录见 [asset-organization.md](asset-organization.md)，可重复执行工具及用途见 [Scripts/README.md](../Scripts/README.md)。
