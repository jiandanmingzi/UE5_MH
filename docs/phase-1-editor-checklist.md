# 第一阶段：虫棍单次攻击打木桩——UE5 编辑器手工配置清单

> 适用版本：UE 5.6，当前项目 `MHGZ`。
>
> 资产目录已于 2026-08-06 通过 UE 5.6 AssetTools 完成第一批整理：虫棍 GA 位于 `/Game/Blueprints/Ability/InsectGlaive`，Montage 位于 `/Game/Weapons/InsectGlaive/Anims/Montage`，木桩蓝图位于 `/Game/Blueprints/Monster`。资产名没有改变，已有序列化引用已由引擎重写。

本文主体保留“从零重建第一阶段”的操作步骤。当前仓库已经使用下列正式资产名，不要再按正文中的旧 `Phase1` 示例名创建重复资产：

| 旧示例名 | 当前正式资产 |
|---|---|
| `AM_IG_Phase1_Slash` | `/Game/Weapons/InsectGlaive/Anims/Montage/AM_Shth_R_TuCi` |
| `GA_IG_Phase1_Slash` | `/Game/Blueprints/Ability/InsectGlaive/GA_IG_R_TuCI` |
| `DA_IG_Phase1_Combo` | `/Game/Data/DA_IG_Combo` |
| `DA_IG_Phase1_Weapon` | `/Game/Data/DA_IG_HuoLongGun` |
| `DA_TrainingDummy_Phase1` | `/Game/Data/DA_TrainingDummy` |

`DT_WeaponComboConfig` 名称和路径没有变化，仍为 `/Game/Data/DT_WeaponComboConfig`。

## 1. 第一阶段完成标准

第一阶段只做一个最小闭环：

```text
IA_Y
  -> Input.Weapon.Y
  -> 虫棍连招协调器
  -> GA_IG_Phase1_Slash
  -> 攻击 Montage 的 Attack Collision Notify State
  -> 武器 Socket Sweep
  -> 木桩 Hitzone
  -> MHGZDamageGameplayEffect / MHGZDamageExecCalc
  -> 木桩 Health 下降并输出日志
```

验收时必须同时满足：

- 按一次 Y（或映射的键盘键）只播放一次攻击 Montage；
- 碰到木桩时只结算一次伤害；
- 木桩初始生命为 `1000`，命中后下降；
- 输出日志能看到 `[Damage]` 和 `[TrainingDummy]`；
- Montage 完成后再次按键还能继续攻击；
- 空挥、打断 Montage 后不会永久保留 `Combat.State.Attacking` 或 `Combat.State.BlockMovement`。

## 2. 本阶段固定资产名（需创建项已明确标记）

本节是从零重建第一阶段时使用的资产总表。当前仓库已有上方映射表中的正式资产；只有在资产缺失时才按本节重建，正常开发不要重复创建。

### 2.1 需要先准备的目录

如果 Content Browser 中还没有以下目录，则需要创建目录；目录本身不是资产：

| 状态 | 目录 | 用途 |
|---|---|---|
| `【需创建目录（若不存在）】` | `/Game/Weapons/InsectGlaive/Anims/Montage` | 存放虫棍攻击 Montage |
| `【需创建目录（若不存在）】` | `/Game/Blueprints/Ability/InsectGlaive` | 存放虫棍 GA 蓝图 |
| `【需创建目录（若不存在）】` | `/Game/Data` | 存放武器、连招、木桩 Data Asset 和 Data Table |

在 UE5 Content Browser 中右键目标父目录，选择 `New Folder` 即可。磁盘上的实际位置分别对应 `Content/Weapons/InsectGlaive/Anims/Montage`、`Content/Blueprints/Ability/InsectGlaive` 和 `Content/Data`，不要在资源管理器中手工新建空 `.uasset` 文件。

### 2.2 从零重建时必须创建的六个 UE 资产

以下六个资产目前均需要你在 UE5 编辑器中创建：

| 状态 | 需创建的资产名 | UE 资产类型/创建入口 | 父类或行结构 | 建议路径 | 被谁引用 |
|---|---|---|---|---|---|
| **`【需创建】`** | `AM_IG_Phase1_Slash` | Anim Montage；从已有攻击 Anim Sequence 右键创建 | Skeleton 必须与角色兼容 | `/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_Phase1_Slash` | `GA_IG_Phase1_Slash.AttackMontage` |
| **`【需创建】`** | `GA_IG_Phase1_Slash` | Blueprint Class | `MHGZInsectGlaiveAbility` | `/Game/Blueprints/Ability/InsectGlaive/GA_IG_Phase1_Slash` | `DA_IG_Phase1_Combo.ComboTable[0].AbilityClass` |
| **`【需创建】`** | `DA_IG_Phase1_Combo` | `Miscellaneous -> Data Asset` | `MHGZWeaponComboData` | `/Game/Data/DA_IG_Phase1_Combo` | `DT_WeaponComboConfig` 的 `IG` 行 |
| **`【需创建】`** | `DT_WeaponComboConfig` | `Miscellaneous -> Data Table` | `WeaponComboConfigRow` / `FWeaponComboConfigRow` | **`/Game/Data/DT_WeaponComboConfig`** | `DefaultGame.ini -> MHGZDataManager.WeaponComboConfig` |
| **`【需创建】`** | `DA_IG_Phase1_Weapon` | `Miscellaneous -> Data Asset` | `MHGZWeaponDefinition` | `/Game/Data/DA_IG_Phase1_Weapon` | `BP_IG_Character.DefaultWeaponDefinition` |
| **`【需创建】`** | `DA_TrainingDummy_Phase1` | `Miscellaneous -> Data Asset` | `MHGZDummyConfig` | `/Game/Data/DA_TrainingDummy_Phase1` | `BP_TrainingDummy.DummyConfig` |

这里的“创建”指在 Content Browser 中生成新的 `.uasset`。只创建空资产还不算完成，必须继续按照第 5～11 节填写字段和引用。

### 2.3 已经存在，只需要配置或复用的资产

下列内容不需要重复创建：

| 状态 | 已有内容 | 本阶段需要做什么 |
|---|---|---|
| `【已有，可复用】` | 你已经导入 UE5 的虫棍攻击 Anim Sequence | 任选一个 Skeleton 兼容的攻击动画，用它创建新的 `AM_IG_Phase1_Slash`；原动画本身不需要复制或重新导入 |
| `【已有，可作示例】` | `/Game/Characters/Mannequins/Anims/Armed/InsectGlaive/AS_Shth_TuCi` | 仅是推荐示例，可以替换成你自己的攻击动画 |
| `【已有，需配置】` | `/Game/Blueprints/Characters/BP_IG_Character` | 配置武器组件、`WeaponTrace` 标签和 `DefaultWeaponDefinition`，不要再新建同名角色蓝图 |
| `【已有，需配置】` | `/Game/Blueprints/Characters/BP_PlayerState` | 在继承的 ASC 上增加 `IA_Y -> Input.Weapon.Y`，不要再新建 PlayerState |
| `【已有，需配置】` | `/Game/Blueprints/Characters/ABP_MH_Character` | 确认 AnimGraph 中存在 `DefaultGroup.DefaultSlot` |
| `【已有，需配置】` | `/Game/Blueprints/Monster/BP_TrainingDummy` | 确认父类并指定新建的 `DA_TrainingDummy_Phase1`；该资产不是 Codex 创建的 |
| `【已有，需配置】` | `/Game/Input/Contexts/IMC_MHGZ_Demo` 和 `/Game/Input/Actions/MHGZ/IA_Y` | 确认按键映射，不需要重复创建 Input Action |
| `【已有，需配置】` | `/Game/Weapons/InsectGlaive/Meshes/Glaive/SKM_gun` 或你的实际虫棍网格 | 添加/校准 `IG_Base`、`IG_Tip` Socket |

### 2.4 已经由代码提供，不需要创建 UE 资产的内容

| 状态 | 内容 | 说明 |
|---|---|---|
| `【已有代码，无需创建】` | `MHGZDamageGameplayEffect` | 原生 Instant GameplayEffect，已挂载 `MHGZDamageExecCalc`；在 GA 的 `Damage Effect Class` 下拉框中直接选择 |
| `【已有代码，无需创建】` | `Attack Collision` Notify State | 原生 `UAnimNotifyState_AttackCollision`；直接添加到新建的攻击 Montage |
| `【已有配置，无需创建】` | `Weapon`、`MonsterAttack` Collision Channel | 已写入 `Config/DefaultEngine.ini`，重启编辑器后确认即可 |
| `【已有配置，无需创建】` | `Weapon.InsectGlaive` GameplayTag | 已写入 `Config/DefaultGameplayTags.ini` |
| `【已有文本文件，无需创建】` | `Config/DefaultGame.ini` | 已写入 `DT_WeaponComboConfig` 的预期加载路径 |

不要额外创建 `GE_Damage`，也不要创建 `BP_AttackCollisionNotify`。第一阶段直接使用上述两个原生 C++ 类型。

### 2.5 路径是否必须固定

六个新资产中，只有 `DT_WeaponComboConfig` 的路径目前被 `Config/DefaultGame.ini` 直接引用，因此请优先严格使用：

```text
/Game/Data/DT_WeaponComboConfig
```

另外五个路径属于项目统一命名建议，可以调整，但调整后必须在引用它的资产字段中重新选择正确对象。例如，修改 `GA_IG_Phase1_Slash` 的目录后，需要在 `DA_IG_Phase1_Combo.ComboTable[0].AbilityClass` 中重新选择它。

如果修改了 `DT_WeaponComboConfig` 的路径，则必须同步修改：

```ini
[/Script/MHGZ.MHGZDataManager]
WeaponComboConfig=/Game/你的目录/DT_WeaponComboConfig.DT_WeaponComboConfig
```

路径格式中的前半段是包路径，最后一个点号后的内容是资产对象名。资产改名时，这两处名称通常都要随之改变。

### 2.6 推荐创建顺序

按以下顺序操作可以减少资产下拉框中找不到引用的问题：

1. **`【需创建】AM_IG_Phase1_Slash`**：从已有攻击动画创建并添加碰撞通知；
2. **`【需创建】GA_IG_Phase1_Slash`**：引用上一步的 Montage；
3. **`【需创建】DA_IG_Phase1_Combo`**：引用上一步的 GA；
4. **`【需创建】DT_WeaponComboConfig`**：引用上一步的 Combo Data Asset；
5. **`【需创建】DA_IG_Phase1_Weapon`**：随后填入角色的默认武器字段；
6. **`【需创建】DA_TrainingDummy_Phase1`**：随后填入已有木桩蓝图。

完成每一步后立即 `Save`，最后执行一次 `Save All`。

## 3. 编译并让编辑器识别新类型

1. 关闭 UE5 编辑器，使用 IDE 编译 `MHGZEditor Win64 Development`。
2. 编译成功后重新打开项目。不要依赖 Live Coding 更新 `USTRUCT` 布局。
3. 打开 `Project Settings -> Collision`，确认存在：
   - `Weapon`：Trace Channel，默认 `Block`；
   - `MonsterAttack`：Trace Channel，默认 `Block`。
4. 打开 Gameplay Tags，确认能搜索到 `Weapon.InsectGlaive`。

本阶段代码新增了可直接选择的原生类 `MHGZDamageGameplayEffect`，它是 Instant GE，并已挂载 `MHGZDamageExecCalc`，不需要再创建 `GE_Damage` 蓝图。

## 4. 【已有资产，需配置】虫棍显示组件和轨迹 Socket

攻击代码优先查找带组件标签 `WeaponTrace` 的 `SkeletalMeshComponent`。这一步用于让 Sweep 从真实虫棍网格读取棍首和棍尾位置，而不是从角色身体网格读取。

### 4.1 武器 Skeleton 上创建 Socket

1. 打开 `/Game/Weapons/InsectGlaive/Meshes/Glaive/SKM_gun`。
2. 在其 Skeleton Tree 的合适骨骼上创建两个 Socket：
   - `IG_Base`：靠近握持端；
   - `IG_Tip`：靠近棍尖。
3. 在预览窗口调整两个 Socket，保证 `IG_Base -> IG_Tip` 覆盖主要棍身。
4. 保存 Skeleton/武器资产。

如果 `SKM_gun` 不是最终使用的虫棍网格，就在实际武器网格上创建同名 Socket。

### 4.2 角色蓝图中的武器组件

打开 `/Game/Blueprints/Characters/BP_IG_Character`：

1. 如果蓝图已经有虫棍 `SkeletalMeshComponent`，直接选择它；否则新增一个，命名 `IGWeapon`。
2. 将 Skeletal Mesh 设置为 `SKM_gun`。
3. 把该组件附加到角色 `Mesh` 的握持 Socket（项目当前约定为 `Weapon_R`；若实际骨架名称不同，以能正确握持为准）。
4. 调整相对位置和旋转，让虫棍与手对齐。
5. 在组件 Details 的 `Tags -> Component Tags` 中添加：`WeaponTrace`。
6. 将武器组件碰撞设为 `NoCollision`。伤害检测由代码 Sweep 完成，不依赖武器网格自身碰撞。

注意：`DA_IG_Phase1_Weapon.Mesh` 是装备数据字段；当前第一阶段不会自动生成武器显示组件，所以仍需保留本节的蓝图组件。

## 5. 【需创建】攻击 Montage：`AM_IG_Phase1_Slash`

建议先使用已有动画：

```text
/Game/Characters/Mannequins/Anims/Armed/InsectGlaive/AS_Shth_TuCi
```

1. 右键该 Anim Sequence，选择 `Create -> Create AnimMontage`。
2. 移动并重命名为 `/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_Phase1_Slash`。
3. Montage Slot 使用 `DefaultGroup.DefaultSlot`。
4. 第一阶段先不要增加 Section、Motion Warping 或多段攻击。
5. 在挥击真正接触目标的时间段添加 Notify State：`Attack Collision`。
6. 选中该 Notify State，把 `Config Index` 设置为 `0`。
7. Notify 开始时间放在棍尖开始快速运动之前，结束时间放在有效挥击结束之后。不要覆盖整段 Montage。

然后打开 `/Game/Blueprints/Characters/ABP_MH_Character`，确认最终输出 Pose 之前经过 `Slot (DefaultGroup.DefaultSlot)`。如果没有 Slot 节点，Montage 会被 GAS 激活但画面不播放。

第一阶段建议先关闭攻击 Anim Sequence 的 Root Motion，先验证输入、碰撞和伤害；位移攻击放到下一阶段校准。

## 6. 【需创建】攻击 GA：`GA_IG_Phase1_Slash`

1. 在 `/Game/Blueprints/Ability/InsectGlaive` 新建 Blueprint Class。
2. 搜索并选择父类 `MHGZInsectGlaiveAbility`。
3. 命名 `GA_IG_Phase1_Slash`。
4. 不写 Event Graph，所有逻辑使用 C++ 父类。

在 Class Defaults 中配置：

### Ability 基础字段

| 字段 | 值 |
|---|---|
| `Input Tag` | `Input.Weapon.Y` |
| `Stamina Cost` | `0` |
| `Max Correction Angle` | `30` |
| `Attack Montage` | `AM_IG_Phase1_Slash` |

### `Attack Segments[0]`

先给 `Attack Segments` 添加一个元素，然后配置：

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
| `Collision.Draw Debug` | 首次测试设为 `true`，通过后改回 `false` |
| `Damage.Damage Effect Class` | `MHGZDamageGameplayEffect` |
| `Damage.Motion Value` | `0.30` |
| `Damage.Base Stagger Value` | `10` |
| `Damage.Use Hitzone Defense` | `true` |
| `Damage.Requires Hit To Continue` | `false` |
| `Damage.Hit Stop Base` | 第一轮先设 `0` |
| `Multi Hit Count` | `1` |
| `Multi Hit Interval` | `0.10` |
| `Max Warp Angle` | `0` |

其余音效、GameplayCue、震屏和元素字段先留空。它们不影响第一阶段扣血闭环。

## 7. 【需创建】连招 Data Asset：`DA_IG_Phase1_Combo`

1. 在 `/Game/Data` 右键，选择 `Miscellaneous -> Data Asset`。
2. 选择 `MHGZWeaponComboData`，命名 `DA_IG_Phase1_Combo`。
3. 设置：
   - `Weapon Type Tag = Weapon.InsectGlaive`
   - `Global Combo Timeout = 10.0`
4. 给 `Combo Table` 添加一个元素：

| 字段 | 值 |
|---|---|
| `State Name` | `Idle` |
| `Match Any State` | `false` |
| `Input Tag` | `Input.Weapon.Y` |
| `Ability Class` | `GA_IG_Phase1_Slash` |
| `Next State` | `IG_Phase1_Slash` |
| `Priority` | `100` |
| `Stamina Required` | `0` |
| `Directional Input` | `None` |
| `Requires Hit To Grant Tags` | `false` |
| `Requires Window Open` | `false` |
| `Auto Transition` | `false` |

`Required Tags`、`Blocked Tags` 和 `Granted Tags` 全部留空。攻击结束后，协调器发现 `IG_Phase1_Slash` 没有后继节点，会自动回到 `Idle`。

## 8. 【需创建】武器到连招映射表：`DT_WeaponComboConfig`

1. 在 `/Game/Data` 右键，选择 `Miscellaneous -> Data Table`。
2. Row Structure 选择 `WeaponComboConfigRow`（C++ 名称为 `FWeaponComboConfigRow`）。
3. 命名 `DT_WeaponComboConfig`。
4. 添加一行，Row Name 填 `IG`：
   - `Weapon Type Tag = Weapon.InsectGlaive`
   - `Combo Data Asset = DA_IG_Phase1_Combo`

路径必须是 `/Game/Data/DT_WeaponComboConfig`，否则要同步修改 `Config/DefaultGame.ini`。

## 9. 【需创建】虫棍武器定义：`DA_IG_Phase1_Weapon`

1. 在 `/Game/Data` 创建 Data Asset，类型选择 `MHGZWeaponDefinition`。
2. 命名 `DA_IG_Phase1_Weapon`。
3. 配置：

| 字段 | 值 |
|---|---|
| `Item ID` | `IG_Phase1` |
| `Display Name` | `Phase1 Insect Glaive` |
| `Rarity Level` | `1` |
| `Item Type Tag` | `Item.Type.Weapon.Staff` |
| `Equipment Slot Tag` | `Equipment.Slot.Weapon` |
| `Attack Power` | `100` |
| `Defense` | `0` |
| `Critical Rate` | `0` |
| `Weapon Type Tag` | `Weapon.InsectGlaive` |
| `Mesh` | `SKM_gun` |
| `Attach Socket` | `Weapon_R` |

打开 `BP_IG_Character -> Class Defaults`，把 `Equipment -> Demo -> Default Weapon Definition` 设置为 `DA_IG_Phase1_Weapon`。角色被 PlayerController Possess 后，C++ 会自动创建武器实例并装备；装备过程会把攻击力写入玩家 GAS Attribute，并授予连招表内的攻击 GA。

## 10. 【已有资产，需配置】攻击输入

### 10.1 Input Mapping Context

打开 `/Game/Input/Contexts/IMC_MHGZ_Demo`：

1. 确认已有 `IA_Y`。
2. 给 `IA_Y` 映射一个测试键，例如鼠标左键或游戏手柄 Face Button Top。
3. 第一阶段不要给该映射增加 Chord Trigger。

### 10.2 ASC 的 InputBindings

ASC 在 `PlayerState` 上，不在角色蓝图上。打开 `/Game/Blueprints/Characters/BP_PlayerState`：

1. 选择继承的 `AbilitySystemComponent`。
2. 在 `Input -> Input Bindings` 添加一项：
   - `Input Action = IA_Y`
   - `Ability Tag = Input.Weapon.Y`
   - `Consume Input = true`

确认当前 PlayerController 已在 BeginPlay/Local Player Subsystem 中添加 `IMC_MHGZ_Demo`。如果移动输入已经正常，通常这一步已经完成。

## 11. 创建木桩配置并设置已有木桩蓝图

### 11.1 【需创建】`DA_TrainingDummy_Phase1`

1. 在 `/Game/Data` 创建 Data Asset，类型选择 `MHGZDummyConfig`。
2. 命名 `DA_TrainingDummy_Phase1`。
3. `Display Mesh` 选择一个已知骨骼名称的 Skeletal Mesh。
4. `Looping Montage` 第一阶段可以留空。
5. `Hitzones` 先只添加一个元素，避免多个球体重叠时难以判断肉质：

| 字段 | 示例值 |
|---|---|
| `Bone Name` | `spine_03`，必须替换为所选网格真实存在的躯干骨骼 |
| `Hitzone Tag` | `Hitzone.Torso` |
| `Defense Multiplier` | `1.0` |
| `Stagger Rate` | `1.0` |
| `Half Extent` | `(50, 50, 50)` |

当前 Hitzone 组件是球体，代码使用 `Half Extent.X` 作为半径，Y/Z 暂不参与计算。

### 11.2 【已有资产，需配置】`BP_TrainingDummy`

打开已有的 `/Game/Blueprints/Monster/BP_TrainingDummy`：

1. 确认父类是 `MHGZTrainingDummy`；不是则 Reparent。
2. Class Defaults 中设置：
   - `Dummy Config = DA_TrainingDummy_Phase1`
   - `Dummy Max Health = 1000`
3. 不要在蓝图中另外创建 Hitzone Component；BeginPlay 会根据 Data Asset 动态生成。
4. 将木桩放到测试关卡，距玩家约 150～250 cm。

如果所选 Display Mesh 没有 `spine_03`，必须填写其真实骨骼名，否则球体会附加失败或停在错误位置。可先只用一个半径较大的根骨骼 Hitzone 验证闭环。

## 12. GameMode 检查

打开当前 Demo GameMode，确认：

- `Default Pawn Class = BP_IG_Character`
- `Player State Class = BP_PlayerState`
- `Player Controller Class = BP_MHGZ_PlayerController`

关卡 World Settings 必须使用这个 GameMode。

## 13. PIE 验收顺序

1. 打开 `Output Log`，过滤 `Equipment`、`Damage`、`TrainingDummy`、`Attack`。
2. PIE 后先确认出现类似日志：

```text
[Equipment] Base stats Attack=100.0 Defense=0.0 Crit=0.0
```

3. 按攻击键，确认 Montage 播放。
4. 观察红/黄/绿调试球和线是否沿 `IG_Base -> IG_Tip` 移动。
5. 命中木桩，`MotionValue=0.30`、攻击力 `100`、肉质 `1.0` 时，预期约为：

```text
[Damage] ... Attack=100.00 Motion=0.30 Hitzone=1.00 Final=30.00
[TrainingDummy] ... Health 970.0 / 1000.0
```

6. 等 Montage 自然结束，再次攻击；重复三次后木桩应为约 `910 / 1000`。
7. 验证通过后把 `Collision.Draw Debug` 改回 `false`。

## 14. 常见故障定位

### 按键没有反应

依次检查：

- `IMC_MHGZ_Demo` 是否真的被 Local Player 添加；
- `BP_PlayerState.AbilitySystemComponent.InputBindings` 是否有 `IA_Y -> Input.Weapon.Y`；
- `BP_IG_Character.DefaultWeaponDefinition` 是否已设置；
- `DT_WeaponComboConfig` 路径和 Row Structure 是否正确；
- `DA_IG_Phase1_Combo` 的 WeaponTypeTag 是否与武器定义完全一致。

### GA 激活但没有动画

- `Attack Montage` 是否为空；
- AnimBP 是否包含 `DefaultGroup.DefaultSlot`；
- Montage Skeleton 是否与角色 Mesh 兼容。

### 有动画但没有 Sweep 调试线

- Montage 上是否添加的是 Notify **State** `Attack Collision`；
- `Config Index` 是否为 `0`；
- `Attack Segments` 是否至少有一个元素；
- 武器 SkeletalMeshComponent 是否有 `WeaponTrace` Component Tag；
- 武器网格是否真的存在 `IG_Base` 和 `IG_Tip` Socket。

若日志出现：

```text
[Attack] Missing trace mesh/socket ...
```

说明组件标签、网格或 Socket 至少一项不匹配。

### Sweep 穿过木桩但不扣血

- Project Settings 中 `Weapon` 是否是 Trace Channel；
- 关闭并重开编辑器，让 `DefaultEngine.ini` 的通道变更生效；
- 木桩父类是否为 `MHGZTrainingDummy`；
- `DummyConfig.Hitzones` 的 Bone Name 是否存在；
- GA 的 `Damage Effect Class` 是否为 `MHGZDamageGameplayEffect`。

### 每次只造成 1 点伤害

通常代表攻击力仍为 `0`。检查是否出现 `[Equipment] Base stats Attack=100.0`，并检查默认武器、DT 映射和 `Equipment.Slot.Weapon`。

### 第一次攻击后不能再次攻击

检查 Montage 是否能够自然到达结尾，以及是否有别的 Montage 持续打断它。当前 C++ 会在 Completed、Interrupted、Cancelled 三条路径中结束 GA 并清理攻击状态。

## 15. 撤销说明

### 编辑器资产

第一批目录整理没有创建或重命名资产，但移动并重存了 5 个 `.uasset`，同时重存了引用它们的 `DA_IG_Combo`。正常撤销必须通过 UE AssetTools 按迁移清单反向移动；不要在资源管理器里直接复制或覆盖资产。迁移前备份只用于故障恢复，位置记录在 `docs/asset-organization.md`。

以后如果按本文手工创建资产，撤销时只删除第 2 节列出的六个新资产，并撤销 `BP_IG_Character`、`BP_PlayerState`、AnimBP、GameMode 和测试关卡中的手工改动。删除前建议先用源码管理提交或备份，因为 `.uasset` 是二进制文件。

### 代码和文本

当前代码改动都可以撤销。先查看：

```powershell
git diff
git status --short
```

如果期间没有混入自己的修改，可对明确文件使用 `git restore <文件>`；如果已经有自己的改动，使用 `git restore -p <文件>` 逐块选择，不要整仓库执行恢复。`BP_TrainingDummy.uasset` 是未跟踪的既有文件，`git restore` 不会处理它，也不应删除它。
