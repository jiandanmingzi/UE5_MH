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
├── Blueprints/
│   ├── Ability/InsectGlaive/       GA_IG_*
│   ├── Characters/                 角色、PlayerState、AnimBP、PoseSearch
│   ├── GameModes/
│   ├── Monster/                    BP_TrainingDummy
│   └── PlayerController/
├── Characters/
│   └── Mannequins/                 角色模型与源动画；模板和项目资源仍有混合
├── Data/                           全局 DA/DT；DT_WeaponComboConfig 固定在此
├── GameplayCues/
│   ├── InsectGlaive/
│   ├── Hit/
│   ├── Buff/
│   ├── Character/
│   ├── Monster/
│   └── UI/
├── GameplayEffects/
│   ├── Core/
│   └── InsectGlaive/
├── Input/
│   ├── Actions/MHGZ/               项目 InputAction
│   ├── Contexts/                   IMC_MHGZ_Demo
│   └── Touch/
├── Kinsect/                        猎虫模型、动画和品种资产的规划目录
├── ThirdPerson/
│   └── Input/                      保留的模板输入资产
│       ├── Actions/
│       └── Contexts/
├── UI/
│   └── InsectGlaive/
└── Weapons/
    └── InsectGlaive/
        ├── Anims/
        │   ├── Montage/            虫棍 Montage
        │   └── Sequences/
        │       └── Locomotion/     已确认的待机、行走与 Dash 动画
        ├── Data/
        ├── Materials/
        │   ├── Glaive/
        │   └── Kinsect/
        ├── Meshes/
        │   ├── Glaive/
        │   └── Kinsect/
        ├── Textures/
        │   ├── Glaive/
        │   └── Kinsect/
        └── VFX/
```

统一原则是“系统域顶层、武器类型作为次级目录”：GA 归 Ability，GE 归 GameplayEffects，GC 归 GameplayCues，Montage 和美术资源归武器目录。全局映射表继续放在 `/Game/Data`。

## Content：暂不整理的边界

- `ThirdPerson/`、`LevelPrototyping/` 是模板/原型内容。
- `Map/` 和 `ThirdPerson/Lvl_ThirdPerson` 是地图批次；默认地图路径位于 `Config/DefaultEngine.ini`。
- `__ExternalActors__/`、`__ExternalObjects__/` 由地图和 World Partition 管理，禁止手工移动。
- `Characters/Mannequins/Anims/Armed/InsectGlaive` 仍有 137 个待分类源动画；名称或动作语义未确认前保持原位。
- ThirdPerson 模板 Input 已隔离至 `ThirdPerson/Input`，项目运行时输入保持在 `Input/Actions/MHGZ` 和 `Input/Contexts`；两者不得交叉引用。

## 资产移动规则

1. `.uasset`、`.umap` 只能通过 Content Browser 或 AssetTools 移动。
2. 移动前生成源路径、目标路径、反向引用和冲突报告。
3. C++、INI 与普通字符串中的 `/Game/...` 路径必须手工同步。
4. 移动后验证目标资产、反向引用、Redirector、蓝图编译和 C++ 构建。
5. 地图与外部 Actor 永远作为独立批次处理。

首批迁移记录和可重复执行工具见 `docs/asset-organization.md` 与 `Scripts/AssetOrganization/`。
