# Demo 搭建与验收指南——虫棍打木桩

> 适用版本：UE 5.6，项目 `MHGZ`。
>
> 本文是 Demo 的唯一执行指南。第 1～13 节使用当前已有资产完成单次攻击闭环；第 14～22 节保留完整 Demo 的猎虫、萃取、三灯和 UI 扩展方案。
>
> **首次配置从“步骤 1”开始，按编号依次执行到“步骤 11”。** 本标题下方的目标、完成标准和文件表只是开始前说明，不属于操作步骤。

资产目录已于 2026-08-06 通过 UE 5.6 AssetTools 完成第一批整理。本文只使用当前正式资产名，不再使用旧的 `Phase1` 示例名。不要另外创建 `AM_IG_Phase1_Slash`、`GA_IG_Phase1_Slash`、`DA_IG_Phase1_Combo`、`DA_IG_Phase1_Weapon` 或 `DA_TrainingDummy_Phase1`。

**本阶段目标（开始前说明）**

建立以下最小战斗闭环：

```text
IA_Y
  -> Input.Weapon.Y
  -> 虫棍连招协调器
  -> GA_IG_R_TuCI
  -> AM_Shth_R_TuCi 中的 Attack Collision Notify State
  -> IG_Base / IG_Tip 武器轨迹 Sweep
  -> 木桩 Hitzone
  -> MHGZDamageGameplayEffect / MHGZDamageExecCalc
  -> 木桩 Health 下降并输出日志
```

**最终完成标准（开始前说明）**

- 按一次 Y（或映射的键盘键）只播放一次攻击 Montage；
- 虫棍接触木桩时只结算一次伤害；
- 木桩初始生命为 `1000`，命中后下降；
- Output Log 中能看到 `[Damage]` 和 `[TrainingDummy]`；
- Montage 完成后可以再次攻击；
- 空挥或 Montage 被打断后，不会永久残留 `Combat.State.Attacking` 或 `Combat.State.BlockMovement`。

**开始前的文件准备情况（开始前说明）**

当前工作区已经存在第一阶段需要的所有 UE 资产，**当前需创建资产：无**。接下来只需打开并配置这些现有资产：

| 当前状态 | 正式资产 | Content Browser 路径 | 在哪个步骤配置 |
|---|---|---|---|
| `【已存在，需配置】` | 攻击 Montage `AM_Shth_R_TuCi` | `/Game/Weapons/InsectGlaive/Anims/Montage/AM_Shth_R_TuCi` | 步骤 3 |
| `【已存在，需配置】` | 攻击 GA `GA_IG_R_TuCI` | `/Game/Blueprints/Ability/InsectGlaive/GA_IG_R_TuCI` | 步骤 4 |
| `【已存在，需配置】` | 连招数据 `DA_IG_Combo` | `/Game/Weapons/InsectGlaive/Data/DA_IG_Combo` | 步骤 5 |
| `【已存在，需配置】` | 连招映射表 `DT_WeaponComboConfig` | `/Game/Data/DT_WeaponComboConfig` | 步骤 6 |
| `【已存在，需配置】` | 武器定义 `DA_IG_HuoLongGun` | `/Game/Weapons/InsectGlaive/Data/DA_IG_HuoLongGun` | 步骤 7 |
| `【已存在，需配置】` | 木桩配置 `DA_TrainingDummy` | `/Game/Monster/TrainingDummy/Data/DA_TrainingDummy` | 步骤 9 |
| `【已存在，需配置】` | 虫棍角色 `BP_IG_Character` | `/Game/Blueprints/Characters/Demo/BP_IG_Character` | 步骤 2、7 |
| `【已存在，需配置】` | 玩家状态 `BP_PlayerState` | `/Game/Blueprints/Characters/Demo/BP_PlayerState` | 步骤 8 |
| `【已存在，需配置】` | 角色动画蓝图 `ABP_MH_Character` | `/Game/Blueprints/Characters/Demo/Animation/ABP_MH_Character` | 步骤 3 |
| `【已存在，需配置】` | 木桩蓝图 `BP_TrainingDummy` | `/Game/Blueprints/Monster/TrainingDummy/BP_TrainingDummy` | 步骤 9 |
| `【已存在，需配置】` | 输入映射 `IMC_MHGZ_Demo` | `/Game/Input/Contexts/IMC_MHGZ_Demo` | 步骤 8 |
| `【已存在，需配置】` | 输入动作 `IA_Y` | `/Game/Input/Actions/MHGZ/IA_Y` | 步骤 8 |
| `【已存在，需配置】` | 虫棍网格 `SKM_IG_Glaive` | `/Game/Weapons/InsectGlaive/Meshes/Glaive/SKM_IG_Glaive` | 步骤 2 |

如果你的 Content Browser 中确实缺少某个文件，才按下表创建。创建后仍使用上表中的正式名称和路径：

| 缺失时状态 | 资产 | 创建方式 |
|---|---|---|
| `【需创建（仅缺失时）】` | `AM_Shth_R_TuCi` | 右键兼容角色 Skeleton 的攻击 Anim Sequence，选择 `Create AnimMontage` |
| `【需创建（仅缺失时）】` | `GA_IG_R_TuCI` | 新建 Blueprint Class，父类选择 `MHGZInsectGlaiveAbility` |
| `【需创建（仅缺失时）】` | `DA_IG_Combo` | `Miscellaneous -> Data Asset`，类型选择 `MHGZWeaponComboData` |
| `【需创建（仅缺失时）】` | `DT_WeaponComboConfig` | `Miscellaneous -> Data Table`，Row Structure 选择 `WeaponComboConfigRow` |
| `【需创建（仅缺失时）】` | `DA_IG_HuoLongGun` | `Miscellaneous -> Data Asset`，类型选择 `MHGZWeaponDefinition` |
| `【需创建（仅缺失时）】` | `DA_TrainingDummy` | `Miscellaneous -> Data Asset`，类型选择 `MHGZDummyConfig` |

不要在 Windows 资源管理器中手工创建空 `.uasset`。资产必须由 UE5 Content Browser 创建。

**路径写法（开始前说明）**

Unreal 的 `/Game` 就是项目的 `Content` 根目录，不是与 `Content` 同级的文件夹：

```text
/Game/Data/DT_WeaponComboConfig
    对应
D:\study\MH\MHGZ\Content\Data\DT_WeaponComboConfig.uasset
```

`DefaultGame.ini` 使用完整对象路径：

```ini
[/Script/MHGZ.MHGZDataManager]
WeaponComboConfig=/Game/Data/DT_WeaponComboConfig.DT_WeaponComboConfig
```

只有 `DT_WeaponComboConfig` 的路径被配置文件直接引用。若移动或改名，必须同步修改 `Config/DefaultGame.ini`。

**已经由 C++ 提供的类型（开始前说明）**

以下内容不需要创建蓝图资产：

| 状态 | C++ 类型 | 使用方式 |
|---|---|---|
| `【已有代码，无需创建】` | `MHGZDamageGameplayEffect` | 在 GA 的 `Damage Effect Class` 中直接选择 |
| `【已有代码，无需创建】` | `Attack Collision` Notify State | 直接添加到攻击 Montage 时间轴 |
| `【已有配置，无需创建】` | `Weapon`、`MonsterAttack` Collision Channel | 重启编辑器后在 Project Settings 中确认 |
| `【已有配置，无需创建】` | `Weapon.InsectGlaive` GameplayTag | 在 Gameplay Tag 下拉框中直接选择 |

不要额外创建 `GE_Damage` 或 `BP_AttackCollisionNotify`。

## 1. 关闭 UE5、编译项目并确认基础类型已经加载

1. 如果 UE5 编辑器正在运行，先关闭编辑器。
2. 使用 IDE 编译：

```text
MHGZEditor / Win64 / Development
```

3. 编译成功后重新打开项目。不要依赖 Live Coding 刷新本阶段新增的 `UCLASS`、`USTRUCT` 和 `UPROPERTY`。
4. 打开 `Project Settings -> Collision`，确认存在：
   - `Weapon`：Trace Channel，默认 `Block`；
   - `MonsterAttack`：Trace Channel，默认 `Block`。
5. 打开 Gameplay Tags，确认能够搜索到：

```text
Weapon.InsectGlaive
Input.Weapon.Y
Equipment.Slot.Weapon
Hitzone.Torso
```

6. 在任意相关 Class 下拉框中确认可以找到 `MHGZDamageGameplayEffect`。

本步骤完成标准：编辑器能识别上述碰撞通道、Tag 和 C++ 类型。如果全部已经可见，不需要再次重复编译。

## 2. 在虫棍网格上配置轨迹 Socket，并标记角色的武器组件

攻击代码优先查找带组件标签 `WeaponTrace` 的 `SkeletalMeshComponent`，再从该组件读取棍身两端的 Socket。

### 2.1 在虫棍网格上配置 Socket

1. 打开：

```text
/Game/Weapons/InsectGlaive/Meshes/Glaive/SKM_IG_Glaive
```

2. 在 Skeleton Tree 的合适骨骼上确认或创建两个 Socket：
   - `IG_Base`：放在靠近握持端的位置；
   - `IG_Tip`：放在棍尖位置。
3. 在预览窗口调整 Socket Transform，保证 `IG_Base -> IG_Tip` 覆盖主要棍身。
4. 保存 Skeleton/武器资产。

### 2.2 在角色蓝图中标记武器组件

1. 打开：

```text
/Game/Blueprints/Characters/Demo/BP_IG_Character
```

2. 选择已有虫棍 `SkeletalMeshComponent`。如果确实没有，才新增一个并命名为 `IGWeapon`。
3. 设置 `Skeletal Mesh = SKM_IG_Glaive`。
4. 将组件附加到角色 `Mesh` 的握持 Socket；项目约定名为 `Weapon_R`，实际骨架不同时以正确握持位置为准。
5. 调整相对位置和旋转，让虫棍与手对齐。
6. 在 `Tags -> Component Tags` 中添加：

```text
WeaponTrace
```

7. 将武器组件碰撞设置为 `NoCollision`。实际攻击检测由 C++ Sweep 完成。

本步骤完成标准：角色预览中能正确持握虫棍，武器组件带有 `WeaponTrace`，网格上存在 `IG_Base` 和 `IG_Tip`。

## 3. 在现有攻击 Montage 中配置 Slot 和有效攻击窗口

1. 打开现有 Montage：

```text
/Game/Weapons/InsectGlaive/Anims/Montage/AM_Shth_R_TuCi
```

2. 确认 Montage 使用 `DefaultGroup.DefaultSlot`。
3. 第一阶段暂时只保留一次攻击，不增加多 Section、Motion Warping 或多段伤害逻辑。
4. 在棍身真正产生有效攻击的时间段添加 Notify State：

```text
Attack Collision
```

5. 选中 Notify State，设置 `Config Index = 0`。
6. Notify 开始点放在棍尖开始快速运动前，结束点放在有效挥击结束后；不要覆盖整段 Montage。
7. 第一阶段先关闭该攻击动画的 Root Motion，优先验证输入、碰撞和伤害闭环。
8. 打开：

```text
/Game/Blueprints/Characters/Demo/Animation/ABP_MH_Character
```

9. 确认 AnimGraph 最终输出 Pose 之前经过 `Slot (DefaultGroup.DefaultSlot)`。

本步骤完成标准：Montage 可以通过 `DefaultSlot` 输出，并且时间轴中有一个 `Config Index = 0` 的 `Attack Collision` 窗口。

## 4. 在现有攻击 GA 中填写 Montage、Sweep 和伤害参数

1. 打开：

```text
/Game/Blueprints/Ability/InsectGlaive/GA_IG_R_TuCI
```

2. 确认父类是 `MHGZInsectGlaiveAbility`。
3. 第一阶段不需要编写 Event Graph，使用 C++ 父类逻辑。
4. 在 Class Defaults 中填写基础字段：

| 字段 | 值 |
|---|---|
| `Input Tag` | `Input.Weapon.Y` |
| `Stamina Cost` | `0` |
| `Max Correction Angle` | `30` |
| `Attack Montage` | `AM_Shth_R_TuCi` |

5. 确认 `Attack Segments` 只有一个用于本次测试的元素，并填写 `Attack Segments[0]`：

| 字段 | 值 |
|---|---|
| `Collision.Trace Mesh Component Tag` | `WeaponTrace` |
| `Collision.Attach Socket Name` | `IG_Tip` |
| `Collision.Trace Start Socket Name` | `IG_Base` |
| `Collision.Shape` | `Sphere` |
| `Collision.Shape Extent` | `(14, 14, 14)` |
| `Collision.Collision Channel` | `Weapon` |
| `Collision.Hitzone Query Tag` | 留空 |
| `Collision.Trace Sample Count` | `5` |
| `Collision.Draw Debug` | 首次测试设为 `true`，验收后改为 `false` |
| `Damage.Damage Effect Class` | `MHGZDamageGameplayEffect` |
| `Damage.Motion Value` | `0.30` |
| `Damage.Base Stagger Value` | `10` |
| `Damage.Use Hitzone Defense` | `true` |
| `Damage.Requires Hit To Continue` | `false` |
| `Damage.Hit Stop Base` | `0` |
| `Multi Hit Count` | `1` |
| `Multi Hit Interval` | `0.10` |
| `Max Warp Angle` | `0` |

6. 音效、GameplayCue、震屏、元素和 OnHitSelfEffect 第一阶段先留空。
7. 编译并保存 GA 蓝图。

本步骤完成标准：GA 引用了正确 Montage，只有一个攻击段，伤害类为原生 `MHGZDamageGameplayEffect`。

## 5. 在现有虫棍连招数据中添加 Y 键攻击节点

1. 打开：

```text
/Game/Weapons/InsectGlaive/Data/DA_IG_Combo
```

2. 设置：

```text
Weapon Type Tag = Weapon.InsectGlaive
Global Combo Timeout = 10.0
```

3. 在 `Combo Table` 中确认存在以下测试节点；不存在时添加一个：

| 字段 | 值 |
|---|---|
| `State Name` | `Idle` |
| `Match Any State` | `false` |
| `Input Tag` | `Input.Weapon.Y` |
| `Ability Class` | `GA_IG_R_TuCI` |
| `Next State` | `IG_R_TuCi` |
| `Priority` | `100` |
| `Stamina Required` | `0` |
| `Directional Input` | `None` |
| `Requires Hit To Grant Tags` | `false` |
| `Requires Window Open` | `false` |
| `Auto Transition` | `false` |

4. `Required Tags`、`Blocked Tags` 和 `Granted Tags` 第一阶段全部留空。
5. 保存 Data Asset。

本步骤完成标准：`Idle + Input.Weapon.Y` 能唯一匹配到 `GA_IG_R_TuCI`。

## 6. 在现有武器连招映射表中绑定虫棍连招数据

1. 打开：

```text
/Game/Data/DT_WeaponComboConfig
```

2. 确认 Row Structure 是 `WeaponComboConfigRow`，C++ 名称为 `FWeaponComboConfigRow`。
3. 确认存在 Row Name 为 `IG` 的行；不存在时添加一行。
4. 填写：

```text
Weapon Type Tag = Weapon.InsectGlaive
Combo Data Asset = DA_IG_Combo
```

5. 保存 Data Table。
6. 确认 `Config/DefaultGame.ini` 仍然指向：

```ini
WeaponComboConfig=/Game/Data/DT_WeaponComboConfig.DT_WeaponComboConfig
```

本步骤完成标准：`Weapon.InsectGlaive` 能从 Data Table 映射到 `DA_IG_Combo`。

## 7. 在现有武器定义中填写属性，并设为角色默认武器

1. 打开：

```text
/Game/Weapons/InsectGlaive/Data/DA_IG_HuoLongGun
```

2. 配置：

| 字段 | 值 |
|---|---|
| `Item ID` | `IG_HuoLongGun` |
| `Display Name` | 可填写实际武器显示名 |
| `Rarity Level` | `1` |
| `Item Type Tag` | `Item.Type.Weapon.Staff` |
| `Equipment Slot Tag` | `Equipment.Slot.Weapon` |
| `Attack Power` | `100` |
| `Defense` | `0` |
| `Critical Rate` | `0` |
| `Weapon Type Tag` | `Weapon.InsectGlaive` |
| `Mesh` | `SKM_IG_Glaive` |
| `Attach Socket` | `Weapon_R` |

3. 保存武器 Data Asset。
4. 打开 `/Game/Blueprints/Characters/Demo/BP_IG_Character`。
5. 在 Class Defaults 中设置：

```text
Equipment -> Demo -> Default Weapon Definition = DA_IG_HuoLongGun
```

6. 编译并保存角色蓝图。

本步骤完成标准：角色被 Possess 后自动装备 `DA_IG_HuoLongGun`，装备系统能够把 `Attack Power = 100` 写入 GAS Attribute。

## 8. 在输入映射和 PlayerState ASC 中绑定 Y 键攻击

### 8.1 在 Input Mapping Context 中确认按键

1. 打开：

```text
/Game/Input/Contexts/IMC_MHGZ_Demo
```

2. 确认其中引用 `/Game/Input/Actions/MHGZ/IA_Y`。
3. 为 `IA_Y` 映射一个测试键，例如鼠标左键或手柄 Face Button Top。
4. 第一阶段不要给该映射增加 Chord Trigger。

### 8.2 在 PlayerState 的 ASC 中绑定 GameplayTag

1. 打开：

```text
/Game/Blueprints/Characters/Demo/BP_PlayerState
```

2. 选择继承的 `AbilitySystemComponent`。
3. 在 `Input -> Input Bindings` 中确认存在：

```text
Input Action = IA_Y
Ability Tag = Input.Weapon.Y
Consume Input = true
```

4. 如果不存在则添加；如果已有完全相同的项，不要重复添加。
5. 编译并保存 PlayerState 蓝图。
6. 确认当前 PlayerController 已通过 Local Player Subsystem 添加 `IMC_MHGZ_Demo`。移动输入已经正常时，这项通常已经完成。

本步骤完成标准：按下 `IA_Y` 时，ASC 能收到 `Input.Weapon.Y`，并把它转发给虫棍连招协调器。

## 9. 在木桩数据和木桩蓝图中配置生命值与受击部位

### 9.1 配置现有木桩 Data Asset

1. 打开：

```text
/Game/Monster/TrainingDummy/Data/DA_TrainingDummy
```

2. `Display Mesh` 选择一个已知骨骼名称的 Skeletal Mesh。
3. `Looping Montage` 第一阶段可以留空。
4. `Hitzones` 先只配置一个元素，避免多个球体重叠影响肉质判断：

| 字段 | 示例值 |
|---|---|
| `Bone Name` | `spine_03`，必须替换为所选网格真实存在的躯干骨骼 |
| `Hitzone Tag` | `Hitzone.Torso` |
| `Defense Multiplier` | `1.0` |
| `Stagger Rate` | `1.0` |
| `Half Extent` | `(50, 50, 50)` |

当前 Hitzone 是球体，代码使用 `Half Extent.X` 作为半径，Y/Z 暂不参与计算。

### 9.2 配置现有木桩蓝图

1. 打开：

```text
/Game/Blueprints/Monster/TrainingDummy/BP_TrainingDummy
```

2. 确认父类为 `MHGZTrainingDummy`；不是时执行 Reparent。
3. 在 Class Defaults 中设置：

```text
Dummy Config = DA_TrainingDummy
Dummy Max Health = 1000
```

4. 不要在蓝图中另外创建 Hitzone Component；BeginPlay 会根据 `DA_TrainingDummy` 动态生成。
5. 编译并保存木桩蓝图。
6. 把木桩放入测试关卡，距离玩家约 150～250 cm。

本步骤完成标准：木桩启动时拥有 `1000` 生命，并生成至少一个响应 `Weapon` Trace 的 Hitzone。

## 10. 在 Demo GameMode 和测试关卡中指定正确的玩家类

1. 打开当前 Demo GameMode。
2. 确认：

```text
Default Pawn Class = BP_IG_Character
Player State Class = BP_PlayerState
Player Controller Class = BP_MHGZ_PlayerController
```

3. 打开测试关卡的 World Settings。
4. 确认 `GameMode Override` 使用上述 Demo GameMode。
5. 确认关卡中已经放置 `BP_TrainingDummy`，并且玩家出生点面向木桩。
6. 保存 GameMode 和关卡。

本步骤完成标准：PIE 时生成正确角色、PlayerState 和 PlayerController，场景中存在可攻击木桩。

## 11. 运行 PIE 并按日志完成第一阶段验收

1. 打开 `Output Log`，过滤：

```text
Equipment
Damage
TrainingDummy
Attack
```

2. 启动 PIE，先确认出现类似日志：

```text
[Equipment] Base stats Attack=100.0 Defense=0.0 Crit=0.0
```

3. 按攻击键，确认 `AM_Shth_R_TuCi` 播放一次。
4. 观察调试球和线是否沿 `IG_Base -> IG_Tip` 移动。
5. 命中木桩。攻击力 `100`、Motion Value `0.30`、肉质 `1.0` 时，预期约为：

```text
[Damage] ... Attack=100.00 Motion=0.30 Hitzone=1.00 Final=30.00
[TrainingDummy] ... Health 970.0 / 1000.0
```

6. 等 Montage 自然结束，再次攻击；连续命中三次后木桩应约为 `910 / 1000`。
7. 对空气攻击一次，确认 Montage 结束后角色恢复移动且可以再次攻击。
8. 验收通过后回到 `GA_IG_R_TuCI`，将 `Collision.Draw Debug` 改为 `false`，编译并保存蓝图。

只有同时满足本文开头的全部完成标准，第一阶段才算完成。

## 12. 按现象逐项排查未通过的验收项

### 12.1 排查按键没有反应

- `IMC_MHGZ_Demo` 是否已被 Local Player 添加；
- `BP_PlayerState.AbilitySystemComponent.InputBindings` 是否有 `IA_Y -> Input.Weapon.Y`；
- `BP_IG_Character.DefaultWeaponDefinition` 是否为 `DA_IG_HuoLongGun`；
- `DT_WeaponComboConfig` 的路径和 Row Structure 是否正确；
- `DA_IG_Combo` 与 `DA_IG_HuoLongGun` 的 WeaponTypeTag 是否都为 `Weapon.InsectGlaive`。

### 12.2 排查 GA 激活但没有动画

- `GA_IG_R_TuCI.AttackMontage` 是否为 `AM_Shth_R_TuCi`；
- AnimBP 是否包含 `DefaultGroup.DefaultSlot`；
- Montage Skeleton 是否与角色 Mesh 兼容。

### 12.3 排查有动画但没有 Sweep 调试线

- Montage 上添加的是否为 Notify **State** `Attack Collision`；
- `Config Index` 是否为 `0`；
- `Attack Segments` 是否至少有一个元素；
- 武器 SkeletalMeshComponent 是否有 `WeaponTrace` Component Tag；
- 武器网格是否真的存在 `IG_Base` 和 `IG_Tip` Socket。

如果日志出现：

```text
[Attack] Missing trace mesh/socket ...
```

说明组件标签、网格或 Socket 至少有一项不匹配。

### 12.4 排查 Sweep 穿过木桩但不扣血

- Project Settings 中 `Weapon` 是否是 Trace Channel；
- 是否在修改 `DefaultEngine.ini` 后关闭并重新打开过编辑器；
- 木桩父类是否为 `MHGZTrainingDummy`；
- `DA_TrainingDummy.Hitzones` 的 Bone Name 是否真实存在；
- GA 的 `Damage Effect Class` 是否为 `MHGZDamageGameplayEffect`。

### 12.5 排查每次只造成 1 点伤害

这通常说明攻击力仍然是 `0`。检查是否出现 `[Equipment] Base stats Attack=100.0`，再检查默认武器、DT 映射和 `Equipment.Slot.Weapon`。

### 12.6 排查第一次攻击后不能再次攻击

检查 Montage 是否能自然到达结尾，以及是否有其他 Montage 持续打断它。C++ 会在 Completed、Interrupted 和 Cancelled 三条路径中结束 GA 并清理攻击状态。

## 13. 需要撤销时按资产类型执行对应恢复操作

### 13.1 撤销编辑器资产配置

当前正式资产都已经存在，不要为了撤销配置而直接删除它们。使用源码管理还原对应 `.uasset`，或者在编辑器中手工改回字段。

第一批目录整理曾移动并重存部分 `.uasset`，同时重存了引用它们的 `DA_IG_Combo`。如需撤销目录整理，必须通过 UE AssetTools 反向移动；不要在 Windows 资源管理器中直接复制或覆盖。迁移记录见 [asset-organization.md](asset-organization.md)。

### 13.2 撤销代码和文本配置

先查看：

```powershell
git diff
git status --short
```

没有混入个人修改时，可对明确文件执行 `git restore <文件>`；已经混入个人修改时，使用 `git restore -p <文件>` 逐块选择。不要对整个仓库执行恢复。

---

## 14. 完整 Demo 扩展范围与编译顺序

完成第 1～11 节后，项目已经具备“输入 → GA → Montage → Sweep → 木桩伤害”的最小闭环。完整演示再逐步加入：LT 瞄准、Y 送虫、萃取三色灯、红灯招式分流、三灯强化、资源 UI 和命中反馈。

扩展功能按依赖从底向上编译；这不是要求逐个文件单独构建，而是发生编译错误时的排查顺序。

### 14.1 基础设施

```text
Source/MHGZ/
├── MHGZPlayerState.*
├── MHGZCharacter.*
├── MHGZPlayerController.*
├── Inventory/MHGZItemTypes.h
├── AttributeSystem/MHGZAttributeSet.*
└── AttributeSystem/MHGZWeaponResourceComponent.*
```

### 14.2 GAS 核心

```text
Source/MHGZ/ActionSystem/
├── MHGZAbilitySystemComponent.*
├── MHGZGameplayAbility.*
├── MHGZAttackAbility.*
├── MHGZWeaponComboData.*
└── MHGZComboCoordinatorAbility.*
```

### 14.3 虫棍与训练木桩

```text
Source/MHGZ/InsectGlaive/Kinsect/
├── KinsectCollisionComponent.*
├── InsectGlaiveKinsectData.h
└── Kinsect.*

Source/MHGZ/AttributeSystem/
└── Res_InsectGlaive.*

Source/MHGZ/ActionSystem/
└── MHGZInsectGlaiveAbility.*

Source/MHGZ/Monster/
├── MHGZMonsterHitzoneComponent.*
├── MHGZDummyConfig.h
├── MHGZMonsterBase.*
└── MHGZTrainingDummy.*
```

### 14.4 UI

```text
Source/MHGZ/UI/
├── MHGZUserWidget.*
├── MHGZWeaponResourceWidget.*
├── MHGZCrosshairWidget.*
├── MHGZAimComponent.*
├── MHGZUISubsystem.*
└── MHGZHUD.*
```

## 15. 外部资产准备与导入

当前项目已经有虫棍角色、武器、猎虫、攻击动画和封闭训练场；只有资产缺失或需要替换时才重新导入。不要重复创建与当前正式资产同义的旧示例文件。

| 用途 | 最小来源 | 当前目标或建议命名 | 缺失时回退 |
|---|---|---|---|
| 虫棍纵斩 | 1 个可用 AnimSequence | 复用 `AS_Shth_R_TuCi` 与 `AM_Shth_R_TuCi` | 临时使用兼容 `SK_Demo_Body` 的攻击动画 |
| 猎虫模型 | 任意猎虫骨骼模型 | 复用 `SKM_IG_Kinsect` | Sphere/Cube 缩放后只验证飞行和碰撞 |
| 猎虫飞行 | 翅膀循环动画 | 计划创建专用 Fly Sequence/Montage | 先只移动 Mesh，不播放动画 |
| 木桩模型 | 人形靶或木桩 | 放入 `Monster/TrainingDummy/Meshes` | 临时使用 `TemplateAssets` Mannequin |
| 木桩待机 | 待机循环动画 | 放入 `Monster/TrainingDummy/Anims` | 不播放动画也可验证 Hitzone |
| 虫棍模型 | SkeletalMesh | 复用 `SKM_IG_Glaive` | 保留现有模型 |

导入后按以下顺序处理：

1. 动画必须使用 `SK_Demo_Body` 或明确完成重定向，不要混用模板 Skeleton。
2. 攻击 AnimSequence 制作 Montage，并在有效挥击区间放置 `Attack Collision` Notify State。
3. 猎虫需要动画时，在 `Weapons/InsectGlaive/Anims/Blueprints` 创建专用 AnimBP，通过 Slot 播放 Fly Montage。
4. 木桩需要动画时，在 `Monster/TrainingDummy/Anims` 创建专用 AnimBP；纯逻辑木桩可以没有动画。
5. 正式资源就绪前可以使用引擎基础形状或模板 Mannequin，先跑通逻辑，再替换视觉资产。

## 16. 猎虫品种与资源映射

### 16.1 猎虫 DataAsset

在 `/Game/Weapons/InsectGlaive/Data` 创建 `UInsectGlaiveKinsectData` 类型的 DataAsset，建议命名 `DA_IG_Kinsect_Speed`。旧文档使用过 `DA_Kinsect_Speed`，不要同时保留两个同义资产。

| 字段 | Demo 值 |
|---|---|
| KinsectDisplayName | `速度型猎虫` |
| KinsectMesh | `SKM_IG_Kinsect` |
| FlyMontage | 专用猎虫 Fly Montage；尚未制作时留空 |
| FlightSpeed | `2000` |
| StraightFlightDistance | `1500` |
| StaminaPool | `100` |
| StaminaRegenRate | `15` |
| HoverDrainRate | `3` |
| FlightDrainRate | `8` |

### 16.2 武器资源映射表

如果当前装备链需要按武器类型创建资源组件，在 `/Game/Data` 创建跨领域映射表 `DT_WeaponResourceConfig`，Row Structure 使用 `FWeaponResourceConfigRow`：

| Row Name | WeaponTypeTag | ResourceComponentClass | ResourceWidgetClass |
|---|---|---|---|
| `IG` | `Weapon.InsectGlaive` | `URes_InsectGlaive` | `WBP_IG_ResourcePanel`；UI 尚未创建时留空 |

`ResourceWidgetClass` 在第 20 节完成 Widget 后补填。映射表属于跨领域数据，保持在 `/Game/Data`，不要放回虫棍领域目录。

## 17. 从单次攻击扩展为完整虫棍流程

继续使用现有 `/Game/Weapons/InsectGlaive/Data/DA_IG_Combo`，不要创建旧名 `DA_IG_ComboData`。在第 5 节单节点基线通过后，再增加以下节点：

| StateName | Match Any State | AbilityClass | InputTag | RequiredTags | BlockedTags | Priority | NextState |
|---|:---:|---|---|---|---|:---:|---|
| 留空 | 是 | `GA_Unsheathe` | `Input.Weapon.Y` | `Combat.State.Sheathed` | 留空 | 30 | 留空 |
| 留空 | 是 | `GA_SendKinsect` | `Input.Weapon.Y` | `Combat.State.Aiming` | `Combat.State.Hitstun`、`Combat.State.Knockdown` | 20 | 留空 |
| 留空 | 是 | `GA_RecallKinsect` | `Input.Weapon.B` | `WeaponResource.IG.Kinsect.Active` | `Combat.State.Hitstun`、`Combat.State.Knockdown` | 15 | 留空 |
| 留空 | 是 | `GA_DrawAndSendKinsect` | `Input.Weapon.RT` | `Combat.State.Sheathed` | 留空 | 10 | 留空 |
| `Idle` | 否 | 红灯攻击 GA | `Input.Weapon.Y` | `Combat.Branch.Extract.Red` | 留空 | 10 | `Idle` |
| `Idle` | 否 | `GA_IG_R_TuCI` | `Input.Weapon.Y` | 留空 | 留空 | 0 | `Idle` |

配置规则：

- `Match Any State=true` 的送虫、召回、拔刀和收刀直飞不改变协调器状态，`NextState` 必须留空。
- 普通攻击和红灯攻击只从 `Idle` 匹配，并在结束后回到 `Idle`。
- `BlockedStateNames` 只对任意状态节点生效；本 Demo 默认留空。
- 其余字段保持：`StaminaRequired=0`、`DirectionalInput=None`、`RequiresHitToGrantTags=false`、`RequiresWindowOpen=false`、`AutoTransition=false`。
- 红灯攻击资产尚未创建时可以临时复用 `GA_IG_R_TuCI`，但最终应使用独立 GA 或独立 Montage Section 表达强化动作。

`DT_WeaponComboConfig` 仍只需要 `IG` 一行：`WeaponTypeTag=Weapon.InsectGlaive`，`ComboDataAsset=DA_IG_Combo`。

## 18. GameplayEffect 扩展

当前单次攻击使用原生 `MHGZDamageGameplayEffect` 与 `MHGZDamageExecCalc`。旧方案中的蓝图 `GE_Damage` 已废弃，不要再创建。以下是完整 Demo 仍需补齐的效果资产：

### 18.1 初始属性

`GE_InitStats` 放在 `/Game/GameplayEffects/Core`，Duration Policy 为 Infinite。Demo 可用 Override 初始化：Health/MaxHealth=`100`、Stamina/MaxStamina=`100`，三个耐力倍率=`1.0`。如果 C++ 或角色初始化流程已经提供同一组初值，只保留一个权威来源，避免重复叠加。

### 18.2 猎虫伤害

猎虫可以直接复用 `MHGZDamageGameplayEffect` 和同一 ExecCalc，通过 SetByCaller 传入猎虫攻击力、动作值和肉质倍率，并动态注入 `GameplayCue.Hit.Kinsect`。只有需要不同 Duration/Modifier 策略时才创建 `GE_KinsectDamage` 蓝图。

### 18.3 三色萃取与三灯

萃取效果放入 `/Game/GameplayEffects/InsectGlaive`：

| 资产 | Duration | Granted Tags | Demo Modifier |
|---|---:|---|---|
| `GE_IG_WhiteExtract` | 90s | `WeaponResource.IG.Extract.White` | MoveSpeedMultiplier × 1.15 |
| `GE_IG_YellowExtract` | 90s | `WeaponResource.IG.Extract.Yellow`、`Combat.Poise.Light` | Defense × 1.10 |
| `GE_IG_RedExtract` | 90s | `WeaponResource.IG.Extract.Red`、`Combat.Branch.Extract.Red` | AttackPower × 1.20 |
| `GE_IG_TripleUp` | 90s | `WeaponResource.IG.TripleUp`、`Combat.Branch.TripleUp`、`Combat.Poise.Medium` | AttackPower × 1.25、MoveSpeedMultiplier × 1.15、Defense × 1.15 |

先统一 90 秒便于验收；需要还原不同灯时长时再拆成正式数值。GameplayCue 粒子与音效可以先用引擎临时资产，但 Tag 和目录从一开始使用正式命名。

## 19. 完整流程所需 Ability

新增 GA 统一放在 `/Game/Blueprints/Ability/InsectGlaive`。

### 19.1 红灯攻击

红灯攻击父类仍使用 `MHGZInsectGlaiveAbility`。第一版可复制普通攻击参数并复用同一个 Montage，只验证 `Combat.Branch.Extract.Red` 的优先级分流；之后再换强化 Montage 或 Section。

### 19.2 送虫

`GA_SendKinsect` 使用 `MHGZGameplayAbility`。由连招表匹配 `Input.Weapon.Y + Combat.State.Aiming` 后激活：读取 `MHGZAimComponent` 的相机射线，获取 `URes_InsectGlaive`，调用 `DeployKinsect()`，设置穿透/动作值/重复命中间隔，再结束 Ability。输入 Tag 和激活条件由连招节点管理，不在 GA 中重复配置。

### 19.3 召回

`GA_RecallKinsect` 使用 `MHGZGameplayAbility`。由 `Input.Weapon.B + WeaponResource.IG.Kinsect.Active` 匹配，调用 `RecallKinsect()` 后结束；猎虫真正回到角色时再移除 Active Tag。

### 19.4 拔刀

`GA_Unsheathe` 使用 `MHGZGameplayAbility`。激活时直接添加 `Combat.State.Unsheathed`、移除 `Combat.State.Sheathed`，可选播放拔刀 Montage，然后结束。不要依赖攻击命中回调授予拔刀 Tag。

### 19.5 收刀直飞

`GA_DrawAndSendKinsect` 先执行拔刀状态切换，再让猎虫沿角色前方或瞄准射线直飞，使用 SingleHit/FirstHitOnly 策略，最后结束 Ability。

## 20. Widget 与 HUD

### 20.1 WBP_HUD

主面板建议使用以下插槽：

```text
Canvas Panel
├── HealthBarSlot          左上角
├── StaminaBarSlot         血条下方
├── WeaponResourceSlot     中下方
└── CrosshairSlot          屏幕中央
```

### 20.2 WBP_Crosshair

父类使用 `MHGZCrosshairWidget`，包含可变量 `CrosshairImage`。`OnAimTargetUpdated` 中：无目标显示灰色；红/黄/白部位分别切换对应颜色；需要时播放轻量 ZoomPulse。

### 20.3 血量与耐力条

`WBP_HealthBar`、`WBP_StaminaBar` 使用 `MHGZUserWidget`，包含 ProgressBar 和 TextBlock。`OnValueUpdated(Current, Max)` 更新百分比与文本；百分比大于 0.6 为绿、0.3～0.6 为黄、小于 0.3 为红。

### 20.4 虫棍资源面板

`WBP_IG_ResourcePanel` 使用 `MHGZWeaponResourceWidget`，容纳猎虫耐力条和三色萃取显示。父类负责绑定资源组件的 Delegate 和 GameplayTag；蓝图只实现视觉响应。可拆分为：

- `WBP_IG_KinsectStamina`：ProgressBar；
- `WBP_IG_ExtractDisplay`：红、白、黄三个 Image 与三灯状态；
- 三灯齐聚时可显示统一光环，Demo 初版只切换颜色和可见性。

资源面板创建后，回到 `DT_WeaponResourceConfig` 将 `ResourceWidgetClass` 指向 `WBP_IG_ResourcePanel`。所有 Widget 按 [directory-structure.md](directory-structure.md) 放入 `/Game/UI` 对应领域目录。

## 21. 扩展集成与完整 PIE 验收

### 21.1 角色与 GameMode

- `BP_IG_Character` 添加 `MHGZAimComponent`，并保持默认武器为 `DA_IG_HuoLongGun`。
- `BP_Demo_GameMode` 使用 `BP_IG_Character`、`BP_PlayerState`、`BP_MHGZ_PlayerController` 和 `MHGZHUD`。
- 默认地图继续使用 `/Game/Maps/L_DemoArena`，不要创建第二张演示地图。

### 21.2 输入

在现有 `IMC_MHGZ_Demo` 中复用或补齐：

| InputAction | GameplayTag/用途 |
|---|---|
| `IA_Y` | `Input.Weapon.Y`，攻击/送虫/拔刀 |
| `IA_B` | `Input.Weapon.B`，召回 |
| `IA_LT` | `Input.Modifier.Aiming`，由 AimComponent 管理状态 |
| `IA_RT` | `Input.Weapon.RT`，收刀直飞 |

ASC 的武器输入绑定放在 `BP_PlayerState`；`IA_LT` 是瞄准状态输入，不作为攻击 Ability 直接激活。不要再创建旧名 `IMC_IG` 形成第二套映射。

### 21.3 完整功能验收

| 步骤 | 操作 | 预期 |
|:---:|---|---|
| 1 | 启动 PIE | HUD、血量、耐力和准心正常显示 |
| 2 | 收刀态按 Y | 拔刀并持有 `Combat.State.Unsheathed` |
| 3 | 按住 LT | 准心启用，AimComponent 开始检测 |
| 4 | 瞄准不同 Hitzone | 准心按红/黄/白萃取类型变色 |
| 5 | 瞄准时按 Y | 猎虫离手、命中目标并返回 |
| 6 | 猎虫回手 | 对应萃取 Tag 与 UI 点亮，持续时间开始计算 |
| 7 | 非瞄准态按 Y | 播放攻击 Montage，对木桩结算一次伤害 |
| 8 | 木桩受击 | 血量下降并显示临时或正式命中反馈 |
| 9 | 获取三种萃取 | 三灯 Tag、强化效果和资源 UI 同步生效 |
| 10 | 红灯/三灯后按 Y | 连招表优先匹配强化攻击分支 |

任何一步失败时，先回到第 12 节验证最小攻击闭环，再分别检查猎虫状态、萃取 GE、资源映射和 UI；不要同时改动所有层。

## 22. Demo 简化边界与相关文档

| 完整功能 | Demo 初版允许的简化 |
|---|---|
| 多招式连招 | 先保留 1 个普通攻击和 1 个红灯分支 |
| 红灯动作 | 临时复用普通攻击 GA/Montage |
| 消耗灯特殊技 | 暂不实现 |
| 装备词条 | 使用固定白板虫棍 |
| GameplayCue 粒子 | 使用引擎临时资源 |
| 猎虫动画 | 只移动 Mesh，不播放翅膀动画 |
| 三灯音效 | 使用临时提示音 |
| UI 动画 | 只做颜色、百分比与可见性变化 |

| 文档 | 用途 |
|---|---|
| [gas-infrastructure.md](gas-infrastructure.md) | ASC 初始化与 PlayerState 组件架构 |
| [attributes.md](attributes.md) | Health、Stamina、装备与属性约束 |
| [actions.md](actions.md) | AttackAbility、输入绑定与连招协调器 |
| [insect-glaive.md](insect-glaive.md) | 猎虫、萃取、消耗和虫棍完整设计 |
| [monster-system.md](monster-system.md) | 木桩和 Hitzone |
| [gameplay-cue.md](gameplay-cue.md) | 命中反馈与 GameplayCue |
| [ui-system.md](ui-system.md) | HUD、Widget 与数据绑定 |
| [gameplay-tags.md](gameplay-tags.md) | GameplayTag 层级 |
| [design-decisions.md](design-decisions.md) | 关键架构决策 |
