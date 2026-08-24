# UE5.6 编辑器接线指南——虫棍木桩 Demo

> **使用时机：** 仅在 [Demo 实施计划](../design/demo-implementation-plan.md) 的对应 C++ 里程碑已经编译通过后执行。本文不指导修改 C++，只说明代码完成后需要在 Unreal Editor 5.6 中进行的资产迁移、蓝图/DataAsset 接线、Montage/碰撞/UI 配置和人工验收。

> **编辑器真相源：** 本文决定“在编辑器里做什么和按什么顺序做”；玩法和代码接口分别以 [虫棍动作设计](../design/insect-glaive-actions.md)、[虫棍资源设计](../design/insect-glaive.md) 和 [冻结实施计划](../design/demo-implementation-plan.md) 为准。字段名称与重构后的 C++ 不一致时先停止，不要凭相似名称猜填。

> **阶段门禁：** 当前所在阶段、M/E/L 的进入/退出条件和已知阻塞项统一见 [阶段门禁](../design/milestone-gates.md)。本文件不再用某个旧日期的进度段落判断“是否可以进入下一阶段”。

## 1. 执行顺序

按下列依赖顺序操作。M 是代码、E 是编辑器工作包，并非机械的一一对应；除明确标为可并行的 E5-A 外，未满足前置不得批量制作后续资产。

| 阶段 | 编辑器工作 | 完成标志 |
|---|---|---|
| E0 | 首次启动、保留资产体检、旧原型删除前审计 | 保留资产无 Missing Class/Property；删除清单与引用链已确认，尚不执行删除 |
| E1 | Project Settings、碰撞、Tag、插件检查 | Hitzone Object Channel 和输入基础可用 |
| E2 | 核心蓝图接线 | GameMode、Controller、PlayerState、Character、HUD 关系唯一 |
| E3 | Runtime/Input/Combat/Combo DataAsset | 武器只从 RuntimeDefinition 接线；正式数据壳存在，Combat/Combo 的动作类引用待 E4 完成 |
| E4-A | 最小纵切 GE、GA、Montage、Notify | 收/拔刀、放/收虫和一个地面起手动作使用最终资产接线；不是完整 E4 |
| E5-A | 木桩三色部位与基础猎虫物理 | M3 的 Hitzone Sweep、三色萃取和猎虫碰撞有真实测试目标；可在 E4-A 收尾时并行完成 |
| E4-B | 其余 GE、GA、Montage、Notify | 仅在 L5 与 M4-B.1 后接入，全部计划地面动作可独立激活并正确结束 |
| E5-B | 虫印/粉尘表现与训练场 | 只接通已完成代码阶段公开的表现资产；不抢做 M6 UI |
| E6 | HUD、资源面板、准心与反馈 | Viewport 只有一份主 HUD，资源面板只在插槽内 |
| E7 | 全流程 PIE、生命周期压力、打包 | [验证清单](verification.md) 的当前 Demo 项全部通过 |

## 2. E0——第一次打开编辑器与删除前审计

### 2.1 打开前

1. 关闭 Unreal Editor，使用 Development Editor 配置完整编译项目；不要依靠 Live Coding/Hot Reload 验证反射类型。
2. 记录当前 `Content` 工作树和已有用户资产改动；确认 Git 中存在可恢复的阶段提交。
3. 本轮只体检必须保留的资产：`BP_PlayerState`、`BP_IG_Character`、`BP_MHGZ_PlayerController`、`BP_Demo_GameMode`、`BP_TrainingDummy`、`DA_TrainingDummy`、`ABP_MH_Character`、PSD/PSS、`L_DemoArena`、输入基础资产、虫棍美术与原始 `AS_Shth_*` 动画。
4. 不再把旧虫棍动作原型当作迁移数据源。以下资产只做删除前引用确认，不在 E0 打开后补字段或重存：

   - `/Game/Data/DT_WeaponComboConfig`。
   - `/Game/Weapons/InsectGlaive/Data/DA_IG_Combo`、`DA_IG_HuoLongGun`。
   - `/Game/Blueprints/Ability/InsectGlaive/GA_IG_BaDao`、`GA_IG_R_TuCI`。
   - `/Game/Weapons/InsectGlaive/Anims/Montage/AM_Shth_BaDao`、`AM_Shth_R_TuCi`。
   - `/Game/Input/Actions/MHGZ/IA_RTA`、`IA_RTB`、`IA_RTY`、`IA_YB`；M1 已移除代码读取，E2 Compile/Save PlayerState 清旧序列化引用，E3 清 IMC 后才删除。

`BP_MHGZHUD`、WBP 与 `DT_WeaponResourceConfig` 当前不存在，不要把它们写进“待删除”清单。

### 2.2 首次启动

1. 启动 UE5.6，等待 Asset Registry 的 `Discovering Assets` 完成；Shader 编译归零只用于减少干扰，不是 Redirect 或删除审计的前提。Blueprint skeleton 没有全项目完成按钮，目标蓝图在逐个打开时分别重建。
2. 打开 `Window → Developer Tools → Output Log` 和 `Window → Developer Tools → Message Log`。
3. 不要先点 `Save All`。依次打开上节“必须保留”的资产，记录：

   - Missing Class、Unknown Struct、失效 Pin、失效 Parent Class。
   - 组件、父类、Anim Class、GameMode 默认类和地图内木桩引用是否仍有效。

4. 若保留资产出现 Missing Property/Class，先退出编辑器修正 C++ 类型或必要 Redirect；禁止删除未知数据后继续保存。

### 2.3 E0 的停止点

E0 不执行旧动作资产迁移，也不执行删除。记录保留资产的错误后关闭编辑器；若无错误即可继续 M1/M2 代码阶段。

旧原型必须等 M2 已删除 `DefaultGame.ini → DT_WeaponComboConfig → EquipmentComponent` 运行时读取链后，在 E3/E4 按以下事务处理：

1. 用 `Reference Viewer` 复核引用链仍为 `DT_WeaponComboConfig → DA_IG_Combo → GA_IG_R_TuCI → AM_Shth_R_TuCi`，以及旁支 `GA_IG_BaDao → AM_Shth_BaDao`；出现额外引用就停止，不使用 Force Delete。
2. 在 Content Browser 中按引用者到依赖项的顺序删除：`DT_WeaponComboConfig` → 旧 `DA_IG_Combo` → 两个旧 GA → 两个旧 Montage；`DA_IG_HuoLongGun` 无引用，可在同批删除。
3. 保留两个 Montage 所引用的 `AS_Shth_BaDao`、`AS_Shth_R_TuCi`、`SK_Demo_Body`，以及全部其他原始动画和美术资产。
4. 关闭并重新打开编辑器，确认这些旧包均不存在且没有 Missing Class/Package，再创建最终资产；不要从旧包复制属性或节点。
5. M1 已删除代码侧旧输入读取与 `InputBindings` 属性；E2 Compile/Save `BP_PlayerState` 以清除旧序列化引用；E3 §5.3 从 `IMC_MHGZ_Demo` 移除旧组合映射。两处资产引用都归零后，才在 E3 删除 `IA_RTA/IA_RTB/IA_RTY/IA_YB`。保留 `IA_A`，直到 E3 明确它是否复用为闪避输入。
6. 删除与重建完成后对相关目录执行 `Fix Up Redirectors in Folder`，重启并复查引用闭合。
7. 同一资产批次完成后更新 `Scripts/AssetOrganization/verify_project_assets.py` 的资产总数、Blueprint 数与关键资产清单；在实际创建最终 GA 之前，不以旧的 `EXPECTED_BLUEPRINT_COUNT=8` 作为验收依据。

## 3. E1——Project Settings

### 3.1 插件

在 `Edit → Plugins` 检查项目已经启用代码实际依赖的插件：

- Gameplay Ability System 相关模块。
- Enhanced Input。
- Motion Warping。
- Pose Search / Motion Matching。

已有插件不要重复切换。若新增启用项，重启编辑器后重新编译所有相关蓝图。

### 3.2 碰撞通道

本项目的 M0 已把通道和四个 Preset 写入 `Config/DefaultEngine.ini`。正常情况下，本节执行的是逐项核对；只有目标行确实不存在时才点击 `New...` 创建。不要因为下拉框暂时没刷新而创建同名通道或第二套 Preset。

#### 3.2.1 先确认三个自定义通道

1. 打开 `Edit → Project Settings → Engine → Collision`。
2. 展开 `Trace Channels`，确认以下两行存在且类型是 Trace Channel：

   | Name | Default Response | 用途 |
   |---|---|---|
   | `Weapon` | Block | 武器 Socket Sweep 使用的 Trace Channel |
   | `MonsterAttack` | Block | 木桩/怪物攻击玩家时使用的 Trace Channel |

3. 展开 `Object Channels`，确认以下一行存在：

   | Name | Default Response | 用途 |
   |---|---|---|
   | `Hitzone` | Ignore | 怪物部位身份；武器、猎虫和 Aim 用各自的查询方式读取 |

4. 如果 `Hitzone` 不存在，点击 `Object Channels → New Object Channel...`，Name 填写区分大小写的 `Hitzone`，Default Response 选择 `Ignore`，确认后关闭并重启编辑器，再继续创建 Preset。
5. 如果 `Weapon` 或 `MonsterAttack` 缺失，分别使用 `Trace Channels → New Trace Channel...` 创建，Default Response 选 `Block`。如果同名项存在但类型错误，不要再创建近似名称；停止操作并核对 `DefaultEngine.ini`。

选择 `Object Channel` 就代表 `bTraceType=false`，选择 `Trace Channel` 就代表 `bTraceType=true`；编辑器界面不会另外提供 `bTraceType` 复选框。`Kinsect` 是 Collision Preset，不是 Object Channel，不要创建名为 `Kinsect` 的通道。

#### 3.2.2 Preset 创建入口与基础字段

在 `Preset` 区域查找下列四行。已有行点击右侧编辑按钮逐项核对；缺失时点击 `New...`，填写完全相同的名称。先设置基础字段，再按下一节设置全部响应：

| Preset Name | Collision Enabled | Object Type | 建议初始化方式 |
|---|---|---|---|
| `MonsterBody` | Collision Enabled (Query and Physics) | Pawn | 先 `Block All`，再修改例外项 |
| `MonsterHitzone` | Query Only (No Physics Collision) | Hitzone | 先 `Ignore All`，再把 Weapon/Visibility 改为 Block |
| `Kinsect` | Query Only (No Physics Collision) | WorldDynamic | 先 `Ignore All`，再把 WorldStatic 改为 Block |
| `PlayerCapsule` | Collision Enabled (Query and Physics) | Pawn | 先 `Block All`，再修改例外项 |

名称是运行时代码调用 `SetCollisionProfileName` 的契约，必须精确使用上表拼写。不要创建 `MonsterHitZone`、`Player Capsule` 等近似名称。

#### 3.2.3 完整响应矩阵

在每个 Preset 的 `Trace Responses` 和 `Object Responses` 中按下表设置。未列出 Overlap 的位置不要选择 Overlap：

| Channel | MonsterBody | MonsterHitzone | Kinsect | PlayerCapsule |
|---|---|---|---|---|
| WorldStatic | Block | Ignore | Block | Block |
| WorldDynamic | Block | Ignore | Ignore | Block |
| Pawn | Block | Ignore | Ignore | Block |
| PhysicsBody | Block | Ignore | Ignore | Block |
| Vehicle | Block | Ignore | Ignore | Block |
| Destructible | Block | Ignore | Ignore | Block |
| Visibility | Ignore | Block | Ignore | Ignore |
| Camera | Block | Ignore | Ignore | Block |
| Weapon | Ignore | Block | Ignore | Ignore |
| MonsterAttack | Ignore | Ignore | Ignore | Block |
| Hitzone | Ignore | Ignore | Ignore | Ignore |

各项含义：

- `MonsterBody` 只提供角色和场景的实体阻挡。它忽略 Weapon 与 Visibility，因此武器和准心不会把整只怪物当成某个部位；它也忽略 MonsterAttack，避免怪物自己的攻击命中自己的 Body。
- `MonsterHitzone` 只提供查询身份。Weapon=Block 让武器 Sweep 得到部位 HitResult，Visibility=Block 让准心选中部位；所有物理对象和 MonsterAttack 都 Ignore，防止部位球把角色或怪物顶开。
- 目标 `Kinsect` 根碰撞只被 WorldStatic 阻挡，因此会撞墙；Pawn/Hitzone 都 Ignore。M3 后猎虫命中部位由代码单独执行 Hitzone Object Capsule Sweep，不依赖根组件 Overlap。
- `PlayerCapsule` 对 MonsterAttack 常态 Block，闪避窗口临时改为 Ignore，窗口结束后恢复原响应；Weapon 与 Hitzone 始终 Ignore，避免玩家被自己的武器查询或部位查询命中。

#### 3.2.4 把 Preset 用到正确组件

Preset 建好后还必须在对应阶段应用到组件，不能只在 Project Settings 中创建名称。E1 不要求提前创建尚不存在的猎虫/部位资产：

| 执行阶段 | 组件 | Collision Preset | 额外要求 |
|---|---|---|---|
| E2 | `BP_IG_Character` 的 CapsuleComponent | `PlayerCapsule` | Collision Enabled 应显示 Query and Physics |
| E5-A | `BP_TrainingDummy` 的 CapsuleComponent | `MonsterBody` | 木桩唯一实体 Body；负责阻挡玩家 |
| E5-A | `BP_TrainingDummy` 的 SkeletalMeshComponent | `NoCollision` | 只负责显示和提供骨骼挂点，不再建立第二个 Body |
| E5-A | 运行时生成的 Head/Torso/Leg Hitzone Sphere | `MonsterHitzone` | Query Only；Generate Overlap Events=false |
| E5-A | `AKinsect` 的 Collision Root | `Kinsect` | Query Only；Generate Overlap Events=false；ProjectileMovement UpdatedComponent 指向它 |
| E2 | `BP_IG_Character` 的武器 SkeletalMeshComponent | `NoCollision` | 武器命中来自显式 Socket Sweep，不靠 Mesh 物理碰撞 |

`AMHGZMonsterBase` 和 `UMHGZMonsterHitzoneComponent` 会在 C++ 中再次设置 `MonsterBody/MonsterHitzone`，这是运行时兜底，不代表可以省略 Project Settings 中的 Preset 定义。若名称不存在，`SetCollisionProfileName` 无法得到本文要求的响应矩阵。

M3 已删除旧 Overlap 命中路径；`EnableKinsectCollision` 现在只打开 `Kinsect` Preset 的 QueryOnly，猎虫命中由 Hitzone Object Capsule Sweep 完成。不要把 Preset 改回 Weapon=Overlap。

M3 已把 `UMHGZAimComponent::AimChannel` 默认值改为 Visibility，并在命中后验证组件确为 Hitzone ObjectType。`MonsterHitzone` 仍对 Weapon/Visibility Block，`MonsterBody` 仍同时 Ignore；不要用资产覆盖回旧 Weapon 默认值。

M1 已将 `UAnimNotifyState_DodgeWindow` 改为缓存并恢复窗口开始前的逐通道响应；不要用旧版“结束时强制 Block”的行为反向修改 Preset。

#### 3.2.5 保存、重启与最小验证

1. Project Settings 会把修改写入项目配置；关闭编辑器并重启 UE5.6，使通道和 Preset 重新加载。
2. 再次进入 Collision 页面，确认只存在一组 `Weapon`、`MonsterAttack`、`Hitzone` 和一组四个 Preset。
3. E1 的完成门槛到此为止：重新打开每个 Preset，逐项对照 §3.2.2～3.2.3；Object Type、Collision Enabled 和 11 个响应都一致。

以下集成验证等表中对应组件完成后，在 E5-A/E7 执行，不阻塞当前 E1：

4. 打开 Character 与 TrainingDummy 蓝图，逐个选择 Capsule/Mesh，在 Details → Collision 中确认 `Collision Presets` 没有变成 `Custom` 或 `Default`。
5. PIE 中打开控制台执行 `show collision`：玩家和木桩各只有一个实体 Capsule；三个 Hitzone 不应推开玩家。
6. 用 Visibility 准心测试：瞄准 Head/Torso/Leg 能命中对应 Hitzone；瞄准 Body 空隙不应由 `MonsterBody` 伪造部位结果；墙后的 Hitzone 被墙遮挡。
7. 用武器测试：Weapon Sweep 命中 `MonsterHitzone`，不命中 `MonsterBody` 或玩家 Capsule。
8. M3 完成后用猎虫测试：猎虫根组件被墙阻挡，但不会被 Pawn/Hitzone 物理弹开；代码 Hitzone Sweep 仍能取得部位。
9. M1 最终 Dodge 完成后用木桩攻击测试：常态 MonsterAttack 命中 `PlayerCapsule`；DodgeWindow 中忽略，窗口结束或中断后 MonsterAttack 恢复 Block、Weapon 恢复 Ignore。

任一验证失败时先回到响应矩阵核对，不要在单个蓝图上改成 `Custom` 来掩盖错误；共享规则只能在这四个 Preset 中维护。

### 3.3 GameplayTags

打开 `Project Settings → Gameplay Tags`，运行 Tag 检查。Native Tag 已存在时不要在配置中重复建立。至少确认以下根可解析：

- `Input.Weapon.*`、`Input.Dodge`。
- `Combat.State.Grounded/Aerial/Sheathed/Unsheathed/Sheathing/Dodging`。M4-A.2 已注册后二者不得在编辑器重复建立；`Sheathing` 已由收刀 GA 从成功提交持有到 Ability End，`Dodging` 的 GA 生产端在 M4-A.3 接入。二者不是姿态 Tag，也不是 `BlockMovement`。
- `Combat.State.Aiming.Kinsect/Action/Slinger`。
- `Combat.State.ComboWindowOpen/DodgeAcceptOpen/Invincible`。
- `Combat.Config.IG.RedMode.Classic/Numeric`。
- `WeaponResource.IG.Extract.Red/Orange/White`、`WeaponResource.IG.TripleUp`。
- `Combat.Branch.Extract.Red` 与虫棍舞踏/动作分支 Tag。
- `GameplayCue.Hit.*`、`GameplayCue.IG.*`、`GameplayCue.Character.Dodge`。

完整层级见 [GameplayTags](../design/gameplay-tags.md)。

## 4. E2——核心蓝图接线

### 4.1 BP_PlayerState

打开 `/Game/Blueprints/Characters/Demo/BP_PlayerState`：

1. 确认 ASC、AttributeSet、EquipmentComponent 仍由 PlayerState 持有。
2. 选中 ASC 后 Compile。M1 已从 C++ 删除 `InputBindings` 属性，Details 中应不再出现该字段；不要新建替代数组。若蓝图编译仍报告旧输入字段或节点，使用 `Find in Blueprint` 搜索 `InputBindings`/`Bind Action`，只删除命中的旧输入节点后再次 Compile。
3. E2 暂不把原生 `UMHGZDodgeAbility` 作为临时项塞进 Core Abilities，也不要加入 `MHGZM1Placeholder*` 测试 Ability。M4-A 创建并配置最终 `GA_Dodge` 后，再回到这里把且只把该最终类加入一次；它和 `GA_Sheathe` 是唯一的通用 `Input.Dodge`/`Input.Sheathe` Spec，不得按武器类型在 CoreAbilities 添加多个同 InputTag 的 GA。死亡等未实现项保持为空。
4. 不在 PlayerState 创建虫棍 Resource、猎虫、粉尘或 Pawn 相关 Timer。
5. 若 PlayerState Tick 过去只为虫棍 Resource 开启，改为关闭。

### 4.2 Demo PlayerController

打开 `/Game/Blueprints/PlayerController/Demo/BP_MHGZ_PlayerController`：

1. 先确认 Parent Class 为 `AMHGZPlayerController`，再 Compile。`MHGZInputComponent` 与 `WeaponInputRouter` 是父类构造的 inherited components；不要使用 Add Component 再创建。若 Components 树另有蓝图手工添加的同类组件，只删除蓝图新增的重复项，保留各一份 inherited component。
2. 选择 inherited `MHGZInputComponent`，在 `Default IMCs` 中把数组设为 1 个元素：`/Game/Input/Contexts/IMC_MHGZ_Demo`。同一 Context 不得出现两次。
3. M1 已删除 PlayerController 旧 `DefaultMappingContexts` C++ 字段；Compile 后该字段应不存在。打开 Construction Script/Event Graph，搜索 `Add Mapping Context`、`Remove Mapping Context`、`SetupInput`，删除 Controller 蓝图中与默认 IMC/武器按键有关的旧调用；移动端或与本 Demo 输入无关的逻辑不要误删。
4. Router 的 `InputProfile` 在 E2 保持空；E3 创建最终 `DA_IG_InputProfile` 并由 RuntimeDefinition 接线。不要临时引用旧组合 InputAction。
5. 不在蓝图中实现 Chord、连招状态或按武器类型分支。保存后关闭再打开蓝图，确认仍只有一份 InputComponent/Router。

### 4.3 BP_IG_Character

打开 `/Game/Blueprints/Characters/Demo/BP_IG_Character`：

1. 确认组件只有一份：WeaponRuntimeHost、AimComponent、MotionWarping、CharacterMovement，以及角色 Mesh/武器 Mesh。
   - `AimComponent`、`MotionWarping`、`CharacterMovement` 来自 C++ 父类；不要重复添加。
   - `WeaponRuntimeHost` 当前不是 C++ 默认子对象：若 Components 树中没有它，使用 `Add → MHGZ Weapon Runtime Host Component` 新增一份并命名 `WeaponRuntimeHost`；若已有则保留原组件，不再新增。
2. CapsuleComponent 的 Collision Preset 设为 `PlayerCapsule`；不要在蓝图中改成 Custom 响应。
3. 武器 SkeletalMeshComponent 的 Collision Preset 设为 `NoCollision`，Component Tag 设置为代码约定的 `WeaponTrace`。NoCollision 不影响代码读取 Socket Transform 执行 Sweep；该 Tag 同时是 RuntimeHost 定位武器视觉组件的默认约定。
4. **不要**把武器永久附着到左手 `Weapon_L`。角色 Skeleton 必须有已校准的手部 `Weapon_L` 与背部 `Weapon_Back`；武器自身用于判定的 `Root`、`IG_FrontTip`、`IG_RearTip` Socket 必须可在 Skeleton Tree 中预览。运行时初始收刀会挂到 `Weapon_Back`，`DrawCommit` 才切到 `Weapon_L`，`SheatheCommit` 再切回背部；猎虫仍只使用独立的 `Kinsect_Arm_Socket`。
5. E2 不给默认武器字段接入仍待删除的旧 `DA_IG_HuoLongGun`；保持空值。E3 §5.1 重建同名正式资产后再指向它。Character 不直接引用 ComboData、Resource Class 或 Resource Widget。
6. Anim Class 指向最终 `ABP_MH_Character`。
7. 检查 Character 蓝图没有每帧 SetActorRotation、LaunchCharacter 或 Timeline 位移与新的 MovementTask 并行写入。

### 4.4 GameMode 与 HUD

在 Demo GameMode 中配置：

| 字段 | 值 |
|---|---|
| Default Pawn Class | `BP_IG_Character` |
| Player Controller Class | 最终 Demo PlayerController |
| Player State Class | `BP_PlayerState` |
| HUD Class | E2 暂用原生 `AMHGZHUD`；E6 新建 `BP_MHGZHUD` 后替换 |

在 `Project Settings → Maps & Modes`：

- Editor Startup Map 与 Game Default Map 均使用 `/Game/Maps/L_DemoArena`。
- Default GameMode 使用上述 Demo GameMode。

E2 完成时逐项 Compile/Save `BP_PlayerState`、`BP_MHGZ_PlayerController`、`BP_IG_Character`、`BP_Demo_GameMode`，然后关闭并重新打开一次确认无 Missing Property/重复组件。此时不要求武器输入可触发具体动作：InputProfile、ComboData、最终 GA/Montage 分别属于 E3/E4。

### 4.5 M2 代码完成后的 E3 前检查

M2 增加了原生组件并删除旧 DataTable/攻击兼容读取。开始删资产前，先关闭编辑器完成一次 Development Editor 编译，再重新打开项目并执行：

1. 打开并 Compile/Save `BP_PlayerState`。确认旧 `WeaponComboConfig`/DataManager 表路径属性不再出现；`AbilitySystemComponent` 仍只有一份。`AttributeSet` 是 PlayerState 的原生默认子对象，可能只显示在 Class Defaults 的 Components/属性区域，不要求它出现在左侧“添加组件”树中，也不要手工再加一份。
2. 打开并 Compile/Save `BP_IG_Character`。父类现在提供 `IncomingHitResolver`、`HitFeedbackRouter`、`HitStopController` 三个 inherited component；每种必须恰好一份。E2 手工添加的 `WeaponRuntimeHost` 仍保留恰好一份，不要因为前三个是 inherited component 而重建 Host。
3. 打开并 Compile/Save `BP_TrainingDummy`。父类提供 `AbilitySystemComponent`、`AttributeSet` 与 `HitFeedbackRouter`；Hitzone 在运行时由 `DA_TrainingDummy` 生成，蓝图 Components 树中不要再手工添加 Head/Torso/Leg 碰撞体。
4. 打开 `BP_MHGZ_PlayerController` 和 `BP_Demo_GameMode`，Compile/Save 后关闭再重开上述四个核心蓝图。Message Log 不得出现 Missing Property、Unknown Struct、失效 Pin 或重复 inherited component。
5. 在 Output Log 执行 `Automation RunTests MHGZ.M2`（或 Session Frontend 中运行 `MHGZ.M2`）。10 项均通过才继续。若命令行环境使用只读系统 DDC，可给 `UnrealEditor-Cmd` 增加 `-DDC-ForceMemoryCache`；这不需要改项目配置。
6. 用 Reference Viewer 复核 §2.3 的旧包引用链。M2 已解除代码/ini 读取，但 Content 引用仍必须按实际结果处理；出现文档未列出的引用者就停止，不使用 Force Delete。

## 5. E3——输入与武器运行时 DataAsset

所有虫棍运行时资产放在 `/Game/Weapons/InsectGlaive/Data`。同一用途只保留一个正式资产。

先完成一次建壳事务，再按 §5.1～§5.6 填字段：

1. 按 §2.3 删除并确认旧 DT/Combo/GA/Montage/武器定义包已不存在。
2. 新建空壳 `DA_IG_InputProfile`（`UWeaponInputProfile`）、`DA_IG_Combo`（`UMHGZWeaponComboData`）、`DA_IG_Combat`（`UInsectGlaiveCombatConfig`）、`DA_WeaponRuntime_IG`（`UWeaponRuntimeDefinition`）、`DA_IG_HuoLongGun`（`UMHGZWeaponDefinition`）。
3. 五个壳都存在后再互相赋引用，避免因章节顺序引用尚不存在的资产。E3 中 Combat/Combo 对 E4 资产的空引用按下文明确延期。

### 5.1 DA_IG_HuoLongGun

打开建壳事务中新建的 `DA_IG_HuoLongGun`；它必须是 `UMHGZWeaponDefinition` 类型且不是 Duplicate 旧资产。配置：

| 字段 | Demo 值 |
|---|---|
| ItemID | `IG_HuoLongGun` |
| WeaponTypeTag | `Weapon.InsectGlaive` |
| RuntimeDefinition | `DA_WeaponRuntime_IG` |
| Mesh | 当前正式虫棍 Mesh |
| AttachSocket | `Weapon_L` |
| AttackPower | Demo 初值，例如 100；后续可调 |

不得在该资产中再填写旧 Combo/Resource DataTable 行。保存并验证后回到 `BP_IG_Character`，将默认武器设为这个新建的正式 `DA_IG_HuoLongGun`；此后 Character 只通过它的 RuntimeDefinition 间接取得 Input/Combat/Resource/UI。

### 5.2 DA_WeaponRuntime_IG

打开建壳事务中新建的 `DA_WeaponRuntime_IG`：

| 字段 | 值 |
|---|---|
| WeaponTypeTag | `Weapon.InsectGlaive` |
| ResourceComponentClass | `URes_InsectGlaive` |
| InputProfile | `DA_IG_InputProfile` |
| CombatConfig | `DA_IG_Combat` |
| ResourceWidgetClass | E6 完成后填 `WBP_IG_ResourcePanel` |

该资产是 Resource/Input/Combat/UI 的唯一运行时接线入口。

### 5.3 Input Action 与 IMC

在 `/Game/Input/Actions/MHGZ` 复用或创建：`IA_Y`、`IA_B`、`IA_LT`、`IA_RT`、`IA_Dodge` 和移动输入。全部加入唯一 `IMC_MHGZ_Demo`。

`IA_Sprint` 的 Mapping 从 LS/L3 改到 RB/R1（RB 双语义：收刀态按住奔跑由 Character 的 `SprintPressed` 分流；仅持刀地面态按下由 Router 解析为纳刀，见 §5.4）。LS/L3 点击键释放，不再保留 Sprint Mapping。

不要在 Enhanced Input 资产中配置 Y+B 等 Chord Trigger；组合由 Router 的 InputProfile 解析。

先从 `IMC_MHGZ_Demo` 删除 `IA_RTA/IA_RTB/IA_RTY/IA_YB` 的所有 Mapping。用 Reference Viewer 同时确认 `BP_PlayerState` 与 IMC 都不再引用它们，再通过 Content Browser 删除这四个旧 InputAction；不要删除 `IA_Y/IA_B/IA_LT/IA_RT`。

### 5.4 DA_IG_InputProfile

配置 RawAction→PhysicalInputTag，并建立下列输出。字段使用重构后的实际名称：

`RawActionToPhysicalInputTag` 增加 `IA_RB → Input.Modifier.Sheathed`。RB 同时被 Character 的 `SprintAction` 绑定（收刀态奔跑），双绑定由 `UMHGZInputComponent` 统一管理，属于预期行为。

| 输出 | TriggerControls | RequiredHeldModifiers | Aim Context | 备注 |
|---|---|---|---|---|
| Y | Y | — | None | 唯一普通武器 Y 输入；不在 InputProfile 按摇杆方向拆 Chord，方向只由同一 InputSnapshot 交给 ComboData 匹配 |
| B | B | — | None | 地面普通输入；空中由 Combo 状态解释 |
| YB | Y+B | — | None | 四连印斩/前向回旋斩 |
| LTY | Y | LT | Kinsect | 地面送虫；操虫斩舞踏中可派生穿刺 |
| LTB | B | LT | Kinsect | 地面召回；空中操虫斩 |
| LTRT | RT | LT | Kinsect | 虫印弹；`RequiredContextTags={Combat.State.Unsheathed, Combat.State.Grounded}`；LT/RT 任意顺序均可成立 |
| RTY | Y | RT | Action | 猎虫滑翔 |
| LTYB | Y+B | LT | None | 地面粉尘集约；空中降龙 |
| RTYB | Y+B | RT | Action | 觉虫击 |
| RT（`Input.Weapon.RT`，收刀地面） | RT | — | None | 拔刀直飞；`RequiredContextTags={Combat.State.Sheathed, Combat.State.Grounded}`，`DispatchPolicy=OnPress`，`ReleaseControlTag=RT` |
| RT（`Input.Weapon.RT`，持刀地面） | RT | — | None | M4-B.0 后配置虫印斩；`RequiredContextTags={Combat.State.Unsheathed, Combat.State.Grounded}`，`DispatchPolicy=OnReleaseIfUnconsumed`，`ReleaseControlTag` 留空 |
| RT（`Input.Weapon.RT`，空中） | RT | — | None | M5 接入急袭突刺；`RequiredContextTags={Combat.State.Aerial}`，不要求 Sheathed/Unsheathed，`DispatchPolicy=OnPress` |
| Sheathe（`Input.Sheathe`） | RB（`Input.Modifier.Sheathed`） | — | None | 纳刀：`RequiredContextTags={Combat.State.Unsheathed, Combat.State.Grounded}`；通用路由（同 `Input.Dodge`）按 InputTag 激活 `GA_Sheathe`，不进入 ComboData；`ReleaseControlTag=RB`；持刀地面按下立即触发 |

设置：

- `bRequireExactModifiers=true`。
- TriggerControls 必须在 ChordGracePeriod 内；LT/RT 可预先持有或在等待期内最后补齐。
- DirectionInputThreshold、ForwardConeHalfAngle 和 ChordGracePeriod 使用一个明确 Demo 初值，并保留为 DataAsset 可调参数。
- `RequiredContextTags/BlockedContextTags` 在 Chord 派发瞬间读取当前 RuntimeHost/ASC 姿态。收刀 RT 必须限定 `Sheathed+Grounded` 且 `OnPress`；M4-B.0 的持刀 RT 必须限定 `Unsheathed+Grounded` 且 `OnReleaseIfUnconsumed`；空中 RT 仅限定 `Aerial` 且 `OnPress`。三条 Chord 的 ContextTags 互斥。`Input.Sheathe` 同样必须限定 `Unsheathed+Grounded`：空中没有收刀 Chord。LTRT 仍限定 `Unsheathed+Grounded`；这样收刀 RT 不等待 LT+RT 的 GracePeriod，而持刀态 RT 可作为 modifier 等待 LT 或 A/B/Y 组成获胜组合。
- 角色面向画面左时，摇杆左必须解析为 Forward。
- Sheathe 是唯一通用路由输出（同 `Input.Dodge` 按 InputTag 激活 `GA_Sheathe`），不是武器连招输入；Chord 本身要求 `Unsheathed+Grounded`，持刀地面按下立即触发。空中不得产生 Sheathe 快照，空中收刀属于 M5 空中动作范围而不是本 Demo 的隐式兜底；攻击/硬直等再由 `GA_Sheathe` 的原生激活检查拒绝。收刀态不产生 Sheathe 快照，只由 Character 的 `SprintPressed` 分流为奔跑（0.1s 阈值）。

运行 Data Validation，故意制造一个重复 Chord/Priority 并确认能报错，再恢复正确配置。

### 5.5 DA_IG_Combat

打开建壳事务中新建的 `DA_IG_Combat`。E3 先填写不依赖 E4 资产的模式、数值、猎虫、位移和范围参数，并把新建的空壳 `DA_IG_Combo` 接入 ComboData；四个精华 GE、动作类、Montage 和完整 Transitions 等待 E4 创建后回填。E3 允许这些明确列出的引用暂时为空，但不得运行 PIE 或把 Data Validation 失败当作正式完成；E4 回填后执行 **E4-A 阶段范围**的验证，全 Content 验证留到 E7：

1. `RedExtractMode` 默认 `ClassicMovesetGate`。
2. ComboData 指向唯一 `DA_IG_Combo`。
3. 配置并引用四个精华 GE：White、Orange、Red、TripleUp。
4. 配置精华持续时间、红/白/橙/三灯数值 Buff；这些数值只在此资产维护。
5. 配置 `MaxDanceStacks` 和逐层伤害倍率数组；数组长度必须覆盖 0～最大层。
6. 猎虫品种数值（飞行速度、最大距离、RT 直飞距离、耐力池/回复/悬停与飞行耗耐、基础攻击力，以及 M3 新增的召回速度）在 `DA_IG_Kinsect_Speed`（UInsectGlaiveKinsectData）维护；`DA_IG_Combat` 只保留猎虫到达半径这一附着判定常量。
7. 配置四连/回旋、滑翔、操虫斩、穿刺、跳跃斩、急袭突刺、降龙、觉虫击的位移/修正角度参数。
8. 觉虫击修正角固定支持正前方上下左右 ±60°，猎人飞行距离另设上限。
9. 引用唯一 Demo 猎虫品种（`DA_IG_Kinsect_Speed`；品种资产只含外观/速度/耐力等品种数值，不写武器机制），并配置虫印弹/粉尘 Class 和粉尘参数。
10. 指定萃取成功、三灯激活/到期、猎虫耐力归零等资源音效；没有正式资源时可用明确命名的临时音效，但不能留空后再用硬编码路径加载。M3 已在 `DA_IG_Combat` 暴露并接通 `ExtractCollectedSound`、`TripleUpActivatedSound`、`TripleUpExpiredSound`、`KinsectDepletedSound`；E4 创建 `Tmp_SFX_*` 临时音效后回填四个槽位。
11. 不配置 WeaponResource EntryModifier；本 Demo 明确禁用该路径。

### 5.6 DA_IG_Combo

打开建壳事务中新建的 `DA_IG_Combo`。它必须是 `UMHGZWeaponComboData` 类型；不要 Duplicate、导出或抄录旧资产的唯一 Y→突刺节点。

E3 只建立空壳并把它接到 `DA_IG_Combat`；E4 创建最终 GA/Montage 后，再按照 [虫棍动作与连招](../design/insect-glaive-actions.md) 一次配置完整 `Transitions`：

1. 所有输入边有唯一 TransitionID、SourceState、InputTag、Direction、TargetState 和 AbilityClass。
2. Classic 与 Numeric 使用同一张表，以互斥 RedMode Tag 和红灯 Branch Tag 分流。
3. 四连印斩可从 Idle 起手，也可在窗口内从飞圆斩以外地面动作派生；结束进入 `StarterOnly`。
4. Forward+YB 的突进回旋斩 Priority 高于普通 YB；未反击时同样进入 `StarterOnly`。
5. `StarterOnly` 只允许突刺、上捞斩、横扫、飞身跃入斩四个起手。
6. 粉尘集约、虫印、滑翔、觉虫击、操虫斩、穿刺、急袭突刺和降龙按地面/空中状态分别配置。
7. 急袭突刺、降龙使用 AbilityOwned Landing；其他空中动作默认 ResetToIdle。
8. 自动边 InputTag 为空并按确定 TransitionID 触发；StateOnly 不填写 AbilityClass。
9. 不建立跳跃突进斩（电风扇）转移。

M4-A 的最小 Y 拔刀只先配置下列两条边；这不是完整地面连招表：

| TransitionID（建议） | SourceState → TargetState | InputTag | Direction | RequiredTags | AbilityClass | 关键约束 |
|---|---|---|---|---|---|---|
| `IG_DrawOnly` | `Idle → DrawOnly` | `Input.Weapon.Y` | `None` | `Combat.State.Grounded`、`Combat.State.Sheathed` | `GA_IG_Draw` | `None` 是**不要求方向**的通配项，覆盖无、左、右、后输入；无 AttackSegment，只播放拔刀。 |
| `IG_DrawSlash_Forward` | `Idle → GroundStarter` | `Input.Weapon.Y` | `Forward` | `Combat.State.Grounded`、`Combat.State.Sheathed` | `GA_IG_DrawSlash` | 具体 `Forward` 天然优先于 `None`；配置一个或多个正式 AttackSegment。 |

两行的 `ExecutionPolicy=ActivateAbility`、`StatePolicy=Replace`、`GrantTiming=OnActivation`、`bRequiresComboWindow=false`；不需要为前推 Y 新增 Chord。`GA_IG_Draw` 或 `GA_IG_DrawSlash` 结束后由 Coordinator 按其精确 ActionToken 回到 Idle。不要在 `RequiredTags` 里加 `Unsheathed`，也不要在 Blueprint Event Graph 写 `SetSheathed(false)`。

**PIE 修复检查：** `DA_IG_Combo` 必须实际引用 `IG_DrawOnly` 的最终 Draw Ability，否则 RuntimeHost 不会授予该 GA，Y 即使已由 Router 解析为 `Input.Weapon.Y` 也不会拔刀。现有 `GA_IG_BaDao` 若已确认父类为 `UMHGZDrawAttackAbility`、`AttackMontage` 已填且 Montage 有原生 `Draw Commit`，可直接填入 `IG_DrawOnly.AbilityClass`；资产名不参与运行时匹配。是否改名为 `GA_IG_Draw` 仅是后续统一命名的整理，不是本轮 PIE 的前置条件。`GA_IG_DrawSlash` 未完成时，不创建或不保留指向空类的 Forward 边，不能用旧 `GA_IG_TuCi` 代替。

E4 回填完成后运行 Data Validation，消除重复 TransitionID、并列 Priority、非法 StateOnly 和缺失 AbilityClass。E3 的临时空壳不是阶段最终验收数据，不得拿它运行 PIE 或宣称 Combo 已完成。

## 6. E4——GameplayEffect、Ability 与 Montage

> **允许分段：** 本节先执行 E4-A；其余清单留在 E4-B。E4-A 的可玩闭环还需要紧随其后的 M4-A 代码接线（`GA_Sheathe` 的真实收刀与选定起手动作）。在 M4-A 前，不能在 Blueprint Event Graph 临时播放 Montage 或直接写姿态 Tag 来伪造结果。

### 6.0 E4-A——最小纵切资产接线

> **M4-A 当前入口：** 原生 M4-A.2～M4-A.5 接口已经可供本节使用；本节只完成最终 GA/Montage/Combo/AnimGraph 接线，不在 Event Graph 播放 Montage、直接操作猎虫或写姿态 Tag。当前还缺哪些资产、何时可以进入 L0、何时禁止进入 M4-B.1/M5，统一查 [阶段门禁](../design/milestone-gates.md#7-当前项目位置与下一步)。

先只创建下列最终资产，并直接使用本节列出的最终父类/路径；不要为“先跑起来”创建第二套临时 InputAction、ComboData、Resource 或姿态 Bool：

| 类别 | E4-A 必需资产 | 用途 |
|---|---|---|
| 猎虫数据 | `DA_IG_Kinsect_Speed` | Resource 运行时必需；先填数值和引用，外观可留到 E5-A |
| 精华 GE | White、Orange、Red、TripleUp 四个 GE | `DA_IG_Combat` Data Validation 必需；E4-A 可不做三灯 UI，但不能留空 |
| 猎虫 GA | `GA_IG_DrawAndSendKinsect`、`GA_IG_SendKinsect`、`GA_IG_RecallKinsect` | 分别对应收刀 RT、持刀 LT+Y、持刀 LT+B |
| 通用 GA | `GA_Sheathe` | 持刀地面 RB 纳刀；M4-A 先创建并编译原生父类 `UMHGZSheatheAbility`，再创建此最终蓝图子类；只加入 CoreAbilities |
| 地面动作 | `GA_IG_Draw` 与 `GA_IG_DrawSlash` 各一个 GA/Montage | 收刀 Y 无/左/右/后只拔刀；收刀前+Y 拔刀攻击；二者通过同一 `Input.Weapon.Y` 的两条 Combo 边分流 |

按以下顺序操作：

1. 完成 `DA_IG_Kinsect_Speed`、四个精华 GE 与 `DA_IG_Combat` 回填；创建角色 Skeleton 的 `Kinsect_Arm_Socket`，填入 `KinsectAttachSocket`。这一步通过 **E4-A 阶段范围**的 Data Validation 后，才允许启动 E4-A PIE；全 Content 验证留给 E7。M6/E6 前 `DA_WeaponRuntime_IG.ResourceWidgetClass` 保持 `None`，不得创建占位 Widget。
2. 在 InputProfile 至少确认 Y、B、LT、RT、RB 的 RawAction 映射，并完成 `Input.Weapon.RT`、`Input.Weapon.LTY`、`Input.Weapon.LTB`、`Input.Sheathe` 和唯一 `Input.Weapon.Y` Chord。Y 不添加方向 ContextTag；收刀 RT 必须保持 `Sheathed+Grounded`，Sheathe 必须保持 `Unsheathed+Grounded`；不要为了先测试删除 ContextTags。
3. 创建三个猎虫 GA 蓝图子类。`GA_IG_Draw`、`GA_IG_DrawSlash` 继承 `UMHGZDrawAttackAbility`，各自有完整拔刀 Montage，并仅由上一节的两条 `DA_IG_Combo` Transition 引用。`GA_IG_DrawAndSendKinsect` 则继承 `UMHGZDrawAndSendKinsectAbility`：它是无伤害、可移动的上半身动作，不继承攻击链，必须填写 `ActionMontage`，通常直接复用 `AM_IG_SendKinsect`。三个猎虫 GA 与两个 Y 拔刀 GA 都不在 Event Graph 实现姿态、Montage、Resource 或 Coordinator 逻辑。
4. 收刀所需的 ActionToken 阶段、Montage Root Motion 所有权、原生 `AnimNotify_SheatheCommit` 与 `UMHGZSheatheAbility` 已完成；现在创建/保留下方规定的最终收刀 Montage、数据型 `GA_Sheathe` 子类，并且只把它加入 `BP_PlayerState` 的 CoreAbilities。前向 `GA_Dodge` 及其 Montage 已完成；M4-A.3.1 的新字段现已编译，可为持刀 Left/Right/Back 创建并配置方向 Montage。暂不创建其他地面/空中/特殊 GA，不配置完整 Transitions，不创建虫印、粉尘、HUD 或正式猎虫表现资产。保存全部 E4-A/M4-A 资产后再 PIE 验证。

`AM_Shth_ShouDao` 的精确接线：

1. 使用与角色 Mesh 相同 Skeleton 的 `AS_Shth_ShouDao_Idle` 创建 Anim Montage，保存为 `/Game/Weapons/InsectGlaive/Anims/Montage/AM_Shth_ShouDao`。
2. 在同一 Slot Track 中放入两个 Segment：`AS_Shth_ShouDao_Idle` 与 `AS_Shth_ShouDao_Walk`；第二段从第一段结束时间开始，不重叠。
3. Section 名称必须精确为 `Idle`、`Walk`：`Idle` 位于第一段起点，`Walk` 位于第二段起点。两个 Section 的 Next Section 都设为 `None`，避免从 Idle 自动串播 Walk。
4. 在武器实际挂回背部、但 Walk 段的收刀移动尾帧之前，添加原生 `AnimNotify_SheatheCommit`。它必须是当前 Montage 实例的 Notify，不能用 Blueprint Event Graph 写 Tag；它将收刀状态切到 `Sheathed`，而 Slot 继续覆盖后续尾帧。Commit 前中断保持拔刀，Commit 后中断保持收刀。Notify 不负责“禁止按键”：原生 `UMHGZSheatheAbility` 应从激活开始持有 `Combat.State.Sheathing` 到 Ability End，禁止在 Notify、AnimBP 或 Event Graph 提前清它。
5. `Idle` 是锁移动选段，`Walk` 是可转向选段：GA 按激活快照只选择一次 Section，之后绝不在 Montage 中途从 Idle 改 Walk 或反向切换。Walk 段若含 Root Motion，应只含前向位移且不含与角色 `TurnRate` 竞争的 Root Yaw；原生阶段运行时会使实时摇杆从 Walk 首帧起影响 Actor 朝向。
6. 创建 `/Game/Blueprints/Ability/Common/GA_Sheathe`，父类选择完成阶段运行时后的 `UMHGZSheatheAbility`；设置 `SheatheMontage=AM_Shth_ShouDao`、`IdleSectionName=Idle`、`WalkSectionName=Walk`。`InputTag=Input.Sheathe`、`StaminaCostPolicy=None` 使用原生默认值，Event Graph 保持空白。
7. 只把 `GA_Sheathe` 加入 `BP_PlayerState.MHGZAbilitySystemComponent.CoreAbilities`，不得加入 `DA_IG_Combo` 或虫棍 RuntimeDefinition 的武器 Ability 列表。Compile、Save 后重启编辑器一次，确认父类没有 Missing Class。不要在 Ability 的 Tags 栏或 Event Graph 手工添加/移除 `Sheathing`；该 Token 必须由原生父类按 ActionToken 所有和回收。

`GA_Dodge` 与前向翻滚 Montage 的精确接线：

1. 用角色 Skeleton 的前向翻滚序列创建最终 Montage（建议 `/Game/Weapons/Common/Anims/Montage/AM_Dodge_Forward`；若持刀/收刀骨架姿势必须不同，可各建一个 Montage）。开启动画序列的 Root Motion，确认翻滚本体只有前向位移，不含会与代码实时转向竞争的 Root Yaw。
2. Montage 必须精确建立 `DodgeCore`、`IdleExit`、`MoveExit` 三个 Section。`DodgeCore` 从翻滚起点开始；两个 Exit 指向各自独立尾段。编辑器中三个 Section 的 Next Section 都设为 `None`，尤其不能让 IdleExit 播完后顺序串入 MoveExit；运行时由 GA 固定 `DodgeCore -> IdleExit`，并且只会按本次 Section 进入事件把前向翻滚改入 `MoveExit`。
3. **不要**在 `DodgeCore` 末尾添加 `AnimNotify_DodgeExitDecision`。`UMHGZDodgeAbility` 会直接绑定它本次播放的 `FAnimMontageInstance::OnMontageSectionChanged`：运行时先固定 `DodgeCore -> IdleExit`，刚进入 `IdleExit` 时读取这一帧原始摇杆。无输入则保留 `IdleExit`；仅前向翻滚且有输入时，立即跳到 `MoveExit`。不得读取被 `BlockMovement` 清零的 locomotion `bHasInput`，也不在此边界释放移动锁。
4. **不要**在 `MoveExit` 首帧添加 `AnimNotify_DodgeMoveExitBegin`。同一个 GA 在确认已进入 `MoveExit` 后才释放本次 Dodge 的 `BlockMovement` Token，使实时摇杆开始平滑转向；`Combat.State.Dodging` 与 Montage Root Motion 所有权仍保留。两个旧 Notify 类仅为已有资产兼容而保留，已经放在 Montage 上的实例可以删除，但即使暂时保留也不会执行逻辑或输出警告。
5. 在翻滚实际无敌帧范围放 `AnimNotifyState_DodgeWindow`。它只持有 `Invincible` 并临时把 Weapon/MonsterAttack 通道改为 Ignore，结束或中断时恢复窗口前的逐通道响应；不要用它代替攻击侧取消窗口。
6. 已完成的 `/Game/Blueprints/Ability/Common/GA_Dodge` 继续使用 `UMHGZDodgeAbility`。现有 `SheathedDodgeMontage`、`UnsheathedDodgeMontage` 保持为收刀/持刀**前向**资源（允许相同），Section 保持 `DodgeCore`/`IdleExit`/`MoveExit`；旧方向 Map 继续留空。M4-A.3.1 现已编译，应填写新增的 `UnsheathedLeftDodgeMontage`、`UnsheathedRightDodgeMontage`、`UnsheathedBackDodgeMontage`；每个左右后 Montage 只需要 `DodgeCore` 与 `IdleExit`，不要添加或填写 `MoveExit`。收刀左右后没有资产槽，也不创建“收刀侧滚/后滚”占位 GA。保留原生 `InputTag=Input.Dodge`、同一 Instant 耐力成本；Event Graph 保持空白。
7. 只把 `GA_Dodge` 加入一次 `BP_PlayerState.MHGZAbilitySystemComponent.CoreAbilities`。若已有旧 Dodge 类或同样输出 `Input.Dodge` 的 Spec，先移除旧项，确保全局只有这一个最终入口；不得把 `GA_Dodge` 加进 `DA_IG_Combo`。
8. 对允许用翻滚取消的每个攻击 Montage，仅在允许取消的后摇帧放 `AnimNotifyState_DodgeAcceptWindow`。窗口外按 Dodge 必须失败；窗口内由旧攻击的精确 ActionToken 授权两阶段交接。不要在 ASC/GA 的 Tags 栏手填 `DodgeAcceptOpen`，也不要在 Montage 蓝图事件中调用 Cancel Ability。
9. Compile、Save 后重启编辑器，依次验证：无输入走 IdleExit；翻滚本体期间推摇杆不改方向；`DodgeCore` 结束、实际进入 `IdleExit` 的同一帧持续推摇杆时立即改走 MoveExit，且只在 MoveExit 进入后开始转向；BlendOut 后 MM 接管且无论 Montage 是否补发 Completed 都会释放 Dodging；攻击窗口外 Dodge 不生效；窗口内成功 Dodge 令旧攻击以 Superseded 结束；耐力不足或缺 Montage 时旧攻击继续。

E4-A 的验收范围仅为：收刀 RT 拔刀放虫、持刀 LT+Y 放虫、LT+B 收虫、RB 纳刀，以及收刀 Y 的“仅拔刀 / 前+Y 拔刀攻击”分流；前向翻滚资产是本轮已完成的通用动作基底，持刀左右后翻滚在 M4-A.3.1 后补齐。M4-A 先提供收刀、翻滚选择、DrawCommit 和攻击瞬转的原生运行时，再创建对应数据型蓝图与 Combo 边。三色萃取、三灯、其他连招、虫印/粉尘/空战和 UI 都不在 E4-A 验收中。

E4 开始时先创建数据型空壳 `DA_IG_Kinsect_Speed`（类型 `UInsectGlaiveKinsectData`），填写飞行/召回/耐力/攻击力的 Demo 初值并回填 `DA_IG_Combat.KinsectData`。E5-A 再为同一资产接正式 Mesh、Anim Class、Material 和 Fly Montage，不创建第二个 Kinsect Data。这样 M3 Resource 的必需引用与 E4-A 阶段 Data Validation 一致。

#### 6.0.1 当前收尾：E4-A 通过后才进入 L0

本节是当前唯一的**进入 L0 前编辑器门禁**；前置 C++ 校验合同已经完成。按下面顺序只检查/补齐尚未完成的项，不创建 M4-B、M5 或 M6 占位资产：

1. 打开 `DA_WeaponRuntime_IG`，确认 `WeaponTypeTag=Weapon.InsectGlaive`、`ResourceComponentClass=URes_InsectGlaive`、`InputProfile=DA_IG_InputProfile`、`CombatConfig=DA_IG_Combat`；`ResourceWidgetClass` 必须是 `None`。保存。这里 `Resource` 有值而 Widget 为 `None` 是当前正确状态，不得创建空 Widget。
2. 逐个打开并 Compile/Save：`GA_Sheathe`、`GA_Dodge`、`GA_IG_Draw`、`GA_IG_DrawSlash`、`GA_IG_DrawAndSendKinsect`、`GA_IG_SendKinsect`、`GA_IG_RecallKinsect`、`BP_PlayerState` 和 `ABP_MH_Character`。Message Log 不得有 Missing Class、Unknown Property、失效 Pin 或编译 Error。`GA_Sheathe`、`GA_Dodge` 只在 `BP_PlayerState.MHGZAbilitySystemComponent.CoreAbilities` 各出现一次；其余虫棍 GA 只由 `DA_IG_Combo` 的 Transition 授予。
3. 逐个保存 Montage：`AM_IG_SendKinsect` 使用 `IGActionGroup.UpperBody_IGAction`、in-place、无 Root Motion，且 `Draw Commit` 早于 `Kinsect Send Commit` 至少一帧；`AM_IG_RecallKinsect` 同 Slot、in-place、无 Root Motion，仅有 `Kinsect Recall Commit`。不要在这两个 Montage 上放 AttackCollision、ComboWindow、DodgeWindow、SheatheCommit 或 Action Root Motion Phase。
4. 在 `ABP_MH_Character` 使用固定的上半身方案：先缓存当前 Motion Matching 输出为 `LocomotionBase`；`UpperBody_IGAction` Slot 的 Source 也是该缓存，再缓存为 `KinsectActionPose`。只保留一个 `Layered Blend per Bone`，Base=`LocomotionBase`、Blend=`KinsectActionPose`、Branch Filter=`spine_01` 及所有子骨骼、启用当前已验证的 Mesh Space Rotation Blend；其输出再进入 `DefaultSlot`。删除/绕过 `IsMoving` 和“静止全身、移动上半身”两套遮罩切换。不要把 `UpperBody_IGAction` 改为 `DefaultSlot`。静止时下半身自然来自 Idle，移动时下半身来自 locomotion。
5. 在 Content Browser 只选 E4-A 资产及其直接依赖：`DA_WeaponRuntime_IG`、`DA_IG_InputProfile`、`DA_IG_Combat`、`DA_IG_Combo`、四个 Extract GE、`DA_IG_Kinsect_Speed`、上述 GA 和 Montage。使用 `Tools -> Validate Data`（或资产右键的 Validate Data）验证；这一步不选 `DA_TrainingDummy`，其错误属于 E5-A。修正所选资产的错误后再次保存。
6. PIE 依 [验证清单](verification.md#e4-a--m4-a-最小纵切) 的 E4-A.1～E4-A.5 逐项验证：收刀 RT 拔刀放虫、持刀 LT+Y 送虫、LT+B 收虫、RB 纳刀，以及收刀 Y 的 DrawOnly/Forward DrawSlash 分流。每个动作至少测试一次 Commit 前中断与 Commit 后中断。旧 Motion Matching 的启停品质不是本门禁项；只确认送虫/收虫动作期间仍能移动、转向且下半身没有被全身 Montage 锁死。
7. 全部通过后，保存所有 E4-A 资产并提交 M4-A.5 阶段快照；随后才开始 L0。`DA_TrainingDummy` 的三色配置可在这之后或同一轮并行完成，但不是进入 L0 的前置。

### 6.1 精华 GameplayEffect

在 `/Game/GameplayEffects/InsectGlaive` 创建或迁移：

| 资产 | Duration Policy | Granted Tags | 说明 |
|---|---|---|---|
| `GE_IG_WhiteExtract` | Has Duration | `WeaponResource.IG.Extract.White` | 移速等数值由 CombatConfig/运行时 Spec 注入 |
| `GE_IG_OrangeExtract` | Has Duration | `WeaponResource.IG.Extract.Orange` | 防御/霸体按最终 C++ 合同注入 |
| `GE_IG_RedExtract` | Has Duration | `WeaponResource.IG.Extract.Red`、`Combat.Branch.Extract.Red` | Classic 正常动作权限 |
| `GE_IG_TripleUp` | Has Duration | `WeaponResource.IG.TripleUp`、`Combat.Branch.Extract.Red` | 不授予三个单灯 Tag，不允许刷新 |

将四个 Class 填回 `DA_IG_Combat`。不要在 GE 蓝图中再硬编码另一套持续时间真相源。

### 6.2 Ability 蓝图（E4-A 后续为 E4-B）

旧 `GA_IG_BaDao`、`GA_IG_R_TuCI` 只属于已删除原型，不 re-parent、不 Duplicate、不复制 Event Graph。E4-A 先创建上一节表中不依赖新收刀父类的 GA；M4-A 创建并编译 `UMHGZSheatheAbility`、`UMHGZDrawAttackAbility` 与对应原生 Commit Notify 后，再建立最终 `GA_Sheathe`、`GA_IG_Draw`、`GA_IG_DrawSlash` 数据型子类。E4-B 再基于最终原生父类，在 `/Game/Blueprints/Ability/InsectGlaive` 新建其余 Demo 必需 Ability：

- 现有《世界》地面基底和四个 Starter 动作。
- `GA_IG_SendKinsect`（父类 `UMHGZSendKinsectAbility`）、`GA_IG_DrawAndSendKinsect`（父类 `UMHGZDrawAndSendKinsectAbility`）、`GA_IG_RecallKinsect`（父类 `UMHGZRecallKinsectAbility`）。
- `GA_IG_Draw`、`GA_IG_DrawSlash`（均继承 `UMHGZDrawAttackAbility`；前者不配置 AttackSegments，后者配置最终攻击段；两个 Montage 都放原生 `AnimNotify_DrawCommit`）。
- `GA_IG_TetrasealSlash`、`GA_IG_AdvancingRoundslash`。
- `GA_IG_MarkSlash`（父类 `UMHGZMarkSlashAbility`）、`GA_IG_MarkTarget`（父类 `UMHGZMarkKinsectTargetAbility`，仅 LT+RT 虫印弹）、`GA_IG_PowderVortex`、`GA_IG_KinsectGlide`。
- `GA_IG_AwakenedKinsectAttack`。
- `GA_IG_KinsectSlash`、`GA_IG_EnhancedKinsectSpiker`。
- `GA_IG_StrongJumpingSlash`、`GA_IG_DescendingThrust`、`GA_IG_DivingWyvern`。
- `GA_Sheathe`（父类 `UMHGZSheatheAbility`；通用路由 `InputTag=Input.Sheathe`；仅 `Grounded+Unsheathed` 可激活，拒绝 Aerial/Dead/攻击/硬直/击倒/收刀中/翻滚中；播放 `AM_Shth_ShouDao`，静止/移动选段；`AnimNotify_SheatheCommit` 时由 RuntimeHost 切 Sheathed）。

每个蓝图只配置数据：Montage、AttackSegments、成本、位移请求和动作特有参数；不要在 Event Graph 复制 Coordinator、资源状态机或直接操作 UI。武器动作必须继承重构后的虫棍动作基类，并保持 native `InstancedPerExecution`。旧拼音 GA 名称不作为最终资产名占位；最终名称按本节动作清单建立。

`GA_Sheathe` 只在 `UMHGZSheatheAbility` 编译通过后创建；它不进入虫棍 ComboData，把它加入 `BP_PlayerState` 继承的 `MHGZAbilitySystemComponent.CoreAbilities`（与 `GA_Dodge` 同一通用授予入口）。其余 `Input.Weapon.*` 猎虫 GA 必须由 `DA_IG_Combo` 的 Transition 引用，RuntimeHost 才会按武器授予。

拔刀路径（`GA_IG_Draw`、`GA_IG_DrawSlash`、收刀 RT 的 `GA_IG_DrawAndSendKinsect`，以及奔跑中进入这些路径）都在武器实际取出的原生 `AnimNotify_DrawCommit` 解析到当前 ActionToken 后，由对应 Ability 调用 `RuntimeHost->SetSheathed(false)` 并清 `bSprintHeld`。收刀 RT 的 `GA_IG_DrawAndSendKinsect` 以 `ActionMontage=AM_IG_SendKinsect` 播放无 Root Motion 的 `UpperBody_IGAction`；它没有 AttackSegments、`Attacking`、`BlockMovement` 或 MontageRootMotionOwner。该 Montage 后续的原生 `Kinsect Send Commit` 才按冻结 ActorForward 部署猎虫。不得在 Ability Event Graph、激活函数或 Montage 蓝图直接写姿态。纳刀由 `AnimNotify_SheatheCommit` 经 `GA_Sheathe` 调用 `SetSheathed(true)`。

#### M4-A.5：持刀送虫/收虫的上半身 Montage 接线

代码已提供两个**普通 Notify**：`Kinsect Send Commit` 与 `Kinsect Recall Commit`。它们不在 Notify 中保存 Ability，也不直接操作猎虫；运行时优先由 UE Notify Context 中的 `Mesh + MontageInstanceID` 精确找到当前 ActionToken。若某个 Layered Slot 的回调没有携带该 Context，则仅以本次回调传入的源 Montage 在同一 Mesh 的 AnimInstance 中取得其**当前** InstanceID，再走同一份 `Mesh + MontageInstanceID` 注册表。两条路径都不扫描 ASC/Active Specs，也不会按 AbilityClass 猜实例；仍无法解析时会输出 `[KinsectSendCommit]` 或 `[KinsectRecallCommit]` Warning，并且不改变猎虫状态。

1. 确认 `GA_IG_SendKinsect` 的父类为 `UMHGZSendKinsectAbility`、`GA_IG_DrawAndSendKinsect` 的父类为 `UMHGZDrawAndSendKinsectAbility`、`GA_IG_RecallKinsect` 的父类为 `UMHGZRecallKinsectAbility`。三者都保持数据型蓝图：Event Graph 必须为空，不添加 `Activate Ability`、`Play Montage`、`DeployKinsect`、`RecallKinsect`、Tag 或 CharacterMovement 节点。
2. `AM_IG_SendKinsect` 同时供持刀 `GA_IG_SendKinsect` 和收刀 `GA_IG_DrawAndSendKinsect` 使用；`AM_IG_RecallKinsect` 只供 Recall 使用。两个 Montage 的 Slot 都设为 `IGActionGroup.UpperBody_IGAction`，源序列和 Montage 均保持 in-place、无 Root Motion。它们不是 `DefaultSlot` 全身攻击 Montage。
3. 在 `GA_IG_SendKinsect` 与 `GA_IG_DrawAndSendKinsect` 的 Defaults 都设置 `Action Montage = AM_IG_SendKinsect`、`Action Montage Play Rate = 1.0`；在 `GA_IG_RecallKinsect` 设置 `Action Montage = AM_IG_RecallKinsect` 和播放速率。三者都不填写 `AttackMontage`、`AttackSegments`、`AttackCollision`、`ComboWindow` 或 `DodgeWindow`。
4. 在共享的 `AM_IG_SendKinsect` 添加两个**单帧普通 Notify**：武器实际从背部转到手部的帧放 `Draw Commit`；猎虫实际离开角色的帧放 `Kinsect Send Commit`。`Draw Commit` 必须在时间轴上严格早于 `Kinsect Send Commit`，至少间隔一帧，不能放在同一帧赌 Notify 执行顺序。前者只由收刀 RT 的 DrawAndSend GA 响应，后者由两种 Send GA 响应；因此持刀送虫会忽略 `Draw Commit`，收刀拔刀放虫则先 Commit Draw、后 Commit Send。`AM_IG_RecallKinsect` 只在实际召回手势帧放 `Kinsect Recall Commit`。不要添加 `Sheathe Commit`、`Attack Collision`、`ComboWindow`、`DodgeWindow` 或 `Action Root Motion Phase`。
5. 在 `ABP_MH_Character` 保持 Motion Matching 为 Base Pose：插入/保留 `IGActionGroup.UpperBody_IGAction` Slot，并作为 `Layered Blend per Bone` 的 Blend Pose，从骨盆上方第一根脊椎骨向上混合。收刀 RT 在 `Draw Commit` 前使用收刀 MM，Commit 后切换到持刀 MM；全程下半身不能接 DefaultSlot，不能因这三个 GA 切 Idle，也不能新增 `BlockMovement`/Root Motion 开关。普通 locomotion 重构前这里只验证“仍可移动和转向”，不把旧 MM 的启停品质作为 M4-A.5 的最终通过条件。
6. Compile、Save 后重启编辑器，使两个新原生 Notify 出现在 Montage 通知轨道的右键菜单。若列表中没有它们，先关闭编辑器，确认 Development Editor 编译成功，再重新打开；不要用 Blueprint Notify 临时代替。

运行结果固定为：能力 Confirm 后先播放上半身 Montage，Notify 前被中断则猎虫状态不变；Notify 后才开始 Flight/Return，之后即使 Montage 中断也不回滚。正常播放到结尾却未经过 Commit Notify 视为取消。送虫的 Aim 只在 Confirm 时冻结，Commit 时不重读相机、摇杆或准心。

《世界》地面基底若需要 Classic 无红灯弱动作与红灯正常动作，分别配置明确 GA/Montage 或同 Montage 的明确 Section，并由 ComboData Tag 条件选择；Numeric 模式只走正常动作。不要在 AnimBP 根据红灯临时替换动作。

基础 `GA_Dodge` 的 `SheathedDodgeMontage`、`UnsheathedDodgeMontage` 分别配置角色通用的收刀/持刀前向翻滚，供 `Forward/None` 使用；M4-A.3.1 编译后再把持刀左/右/后资源填到三个新增直接字段。旧 `SheathedDodgeMontages`/`UnsheathedDodgeMontages` 方向 Map 只用于迁移并应留空；不要恢复旧 Dodge DataTable。收刀左/右/后在运行时被拒绝，不需要为它们创建 Montage。

每个有挥棍声音的 GA 填写稳定 `AudioIdentityTag`；具体武器若需要覆盖音色，在 `DA_IG_HuoLongGun` 的 SwingSoundOverrides 中按同一 Tag 配置。挥空音效放 AnimNotify，命中音效由 GameplayCue/FeedbackRouter 处理，避免一次命中播放两套声音。

### 6.3 Montage 通用检查

旧 `AM_Shth_BaDao`、`AM_Shth_R_TuCi` 没有可迁移的自定义 Notify，已随旧 GA 删除。以保留的 `AS_Shth_*` AnimSequence 为源全新创建每个最终动作 Montage，并按以下顺序检查：

1. Skeleton 与 `BP_IG_Character` Mesh 一致，Retarget 后骨骼无报错。
2. Slot 与 `ABP_MH_Character` 中实际 Slot 节点一致。
3. 有入口差异的**目标招式**创建明确 Section，例如 `Entry_From_Idle`、`Entry_From_Slash1`、`Core`、`Recovery_Common`；所有 `Entry_From_*` 在 Montage 的 Next Section 中连接到同一个 `Core`，再由 `Core` 接 `Recovery_Common`。`Link_Slash1_To_Slash2` 属于 `AM_IG_Slash2` 的 `Entry_From_Slash1`，不能塞进 Slash1 的尾部。没有后续输入时才播放当前招式自己的 Recovery。
4. `FComboTransition`/`DA_IG_Combo` 不填写 Montage 或 Section。M4-B.1 编译前可以先建 Section 和 Next Section 链，但不能假定现有 `UMHGZAttackAbility` 会选择它；它目前仍从 Montage 开头播放。M4-B.1 编译后，在**目标 GA**填写 `EntrySectionByTransitionID`（优先）、`EntrySectionBySourceState`（回退）和 `DefaultEntrySection`，选择顺序是 `TransitionID → SourceState → Default → Montage 开头`。映射的每个名字都必须是该 Montage 的真实 Section；不要在 Blueprint Event Graph 调 `Montage Jump to Section`，也不要在源 GA 填目标 GA 的 Section。
5. 每个 AttackCollision Notify 的 ConfigIndex 必须对应 Ability 的一个 AttackSegment。
6. ComboWindow、`AnimNotifyState_DodgeAcceptWindow`、CounterWindow、PoiseWindow 只标记精确帧区间。`DodgeAcceptWindow` 放在**攻击 Montage**，只允许当前攻击被 Dodge 取消；它由原生代码以该攻击的 ActionToken 持有 `DodgeAcceptOpen`，不得在蓝图手写 Tag。
7. Dodge Montage 使用 `AnimNotifyState_DodgeWindow`，它只负责无敌帧和恢复开始前的碰撞响应；它不是 `DodgeAcceptWindow`，不能决定攻击是否可取消。
8. Section 没有 Root Motion 开关。逐个检查其中引用的 AnimSequence 根轨迹：Entry/Recovery 若应 in-place，其根骨骼必须实际静止；真实位移片段保留 Root Motion。不能把有位移的根轨迹仅靠“该 Section 不提取”伪装成 in-place。M4-B.1 编译后，在每段真实 Root Motion 的首帧至末帧放原生 `Action Root Motion Phase` NotifyState；它只按 MontageInstanceID 路由到 GA，由 GA 获取/释放所有权。in-place 片段不放它；没有真实根位移但玩法需要移动的片段由对应 GA 的 MovementTask 驱动。
9. MotionWarping Notify 只属于经原生代码明确实现了**目标对齐**的特殊动作；普通攻击的入口方向修正是 `MaxCorrectionAngle` 控制的一次 Actor Yaw 瞬转，不添加 MotionWarping Notify。零 Root Motion 位移动作由 MovementTask 驱动。
10. 特殊动作若确实需要 WarpTarget，不在蓝图写固定共享名，而由该特殊动作的代码按 ActionToken 生成、拥有并清理；普通攻击不生成 `AttackDirection` WarpTarget。
10a. **单帧招内方向修正（M4-A.5）：** 仅对需要在某一精确帧按实时摇杆补正一次朝向的攻击，添加原生普通 `Action Direction Correction` Notify；它不是 NotifyState。其 `Max Correction Angle Override=-1` 表示使用 GA 的 `MaxCorrectionAngle`，填 `0` 禁用该帧，填正值则覆盖该帧阈值。Notify 前后不添加 MotionWarping Notify、WarpTarget、AttackCollision、ComboWindow 或 Tag；它通过当前 MontageInstanceID 精确找到所属 AttackAbility，读取 `LastMovementInputDir` 后最多 `SetActorRotation` 一次。只把它放在后续根 Yaw 不会抢回朝向的帧；若该段需要扭转根位移轨迹或贴合目标位置，改由专用特殊 GA 实现 MotionWarping/MovementTask。
11. 不在 Notify 蓝图保存 Ability 指针、Handle 或 Token。玩家动作 Notify 必须使用重构后的原生类。
12. 对“固定动作本体、后半段可接移动”的 Montage，按 `LockedRootMotion → SteeringRootMotion → MotionMatching` 配置：分支决定用 Branching Point Notify，实际进入可转向尾段用首帧 Notify；Notify 只经 ActionToken 调 GA，GA 精确释放自己的 BlockMovement Token。Montage 仍含 Root Motion 时不得让 MM 同时拥有根位移。
13. M4-A.5 的 `AM_IG_SendKinsect`、`AM_IG_RecallKinsect` 是例外的**上半身表现动作**：二者使用 `UpperBody_IGAction` Slot，源序列与 Montage 都不得启用 Root Motion；不含 AttackCollision/ComboWindow/DodgeWindow。共享的 Send Montage 在武器取出帧额外放 `Draw Commit`，并在实际出虫帧放 `Kinsect Send Commit`；Recall Montage 只在实际召回手势帧放 `Kinsect Recall Commit`。不要把它们放进全身攻击 Slot，也不要用 `BlockMovement` 或 Root Motion 所有权“锁住下半身”。

### 6.4 特殊动作时间轴

- 四连印斩：配置四个独立 AttackSegment/窗口；相邻段可重叠，但 ConfigIndex 各自独立。
- 突进回旋斩：移动阶段内放 CounterWindow；成功反击后由 GA/Coordinator 分支，不由 Montage 猜结果。
- 操虫斩：命中窗口和 MovementTask 的结束点分开；只有成功命中增加舞踏。
- 强化操虫穿刺：只在操虫斩舞踏来源的派生窗口开放。
- 强化跳跃斩、急袭突刺：校准末速度与惯性交接。
- 急袭突刺、降龙：落地段 Notify 不得被通用落地 Reset 提前截断。
- 觉虫击：猎虫贯穿与猎人位移是两个独立运行时请求；Montage 只负责表现时序。
- 前向翻滚：最终 `GA_Dodge` 只在 CoreAbilities 出现一次，整个 Montage 由原生 GA 持有 `Combat.State.Dodging`，因此不能被攻击、猎虫、收刀或第二次 Dodge 打断；硬直、死亡、换装例外。Montage 至少有 `DodgeCore`、`IdleExit`、`MoveExit` 三个 Section。GA 将 `DodgeCore` 运行时固定接到 `IdleExit`，并在实际进入 `IdleExit` 的 Section 边界读取实时原始输入；只有此刻持续推摇杆的前向翻滚会立即改进 `MoveExit`，同时才释放移动锁。冻结的 Forward/None 都选择同一前向资源；翻滚本体播放中不再按实时摇杆改轨。攻击是否能取消进翻滚只由攻击 Montage 上的 `DodgeAcceptWindow` 决定，不能放在此 Montage 的无敌 `DodgeWindow` 上。
- 持刀左/右/后翻滚（M4-A.3.1 原生字段编译后再接线）：三个方向各使用自己的 Montage，激活时由冻结 `InputSnapshot.Direction` 选择；它们只有 `DodgeCore` 与 `IdleExit`，原生选择结果强制 IdleExit，不能配置 MoveExit 或移动衔接。收刀左/右/后直接被 GA 拒绝，因此没有对应资产。所有允许变体继续使用 `GA_Dodge` 的同一 Instant 耐力成本。
- 纳刀：`AM_Shth_ShouDao` 由 `AS_Shth_ShouDao_Idle`（静止）与 `AS_Shth_ShouDao_Walk`（移动中）两个 Section 组成；GA 按激活时快照的移动输入幅度选段，之后不换段。Idle 锁移动，Walk 允许实时摇杆转向；武器归位帧的 `AnimNotify_SheatheCommit` 切 Sheathed，Montage 根位移结束/Blend Out 后才交由 MM 接续。

## 7. E5-A——木桩三色部位与基础猎虫物理

**进入条件：** M3 的猎虫 Collision Root、ProjectileMovement UpdatedComponent 和 Hitzone Object Capsule Sweep 已编译通过，旧 Overlap/GetOverlappingActors 命中路径已删除。若源码仍只有 `OnComponentBeginOverlap`，停止 E5-A，不要关闭 Generate Overlap Events 后继续测试。E5-A 是 M3 的端到端资产前置，可在 E4-A 收尾时执行，不等待 E4-B/M5。

### 7.1 猎虫资产

在 `/Game/Weapons/InsectGlaive` 下配置：

1. `DA_IG_Kinsect_Speed`：引用正式猎虫 Mesh/Material、`ABP_IG_Kinsect`、`AM_IG_Kinsect_Fly`，并填写 Demo 品种数值（飞行速度、最大距离、RT 直飞距离、耐力池/回复/悬停与飞行耗耐、基础攻击力；召回速度等 M3 新增字段到齐后一并填写）。
2. 在角色 Skeleton 上创建并校准独立 `Kinsect_Arm_Socket`，用于猎虫附着/召回终点；它不能与虫棍本体使用的左手 `Weapon_L` 重合。把 `DA_IG_Combat.KinsectAttachSocket` 设为 `Kinsect_Arm_Socket`。虫印弹发射点使用武器 Mesh（Component Tag=`WeaponTrace`）上的 `IG_FrontTip`，并把 `KinsectMarkLaunchSocket` 设为该名称；弹道方向由冻结的 Aim TargetPoint 与该 Socket 共同计算，不从相机位置直接生成。
3. 猎虫蓝图若仅用于指定 Mesh/AnimClass，父类使用最终 `AKinsect`，不要在 Tick 蓝图重复飞行、Overlap 或萃取逻辑。
4. 在 M3 最终实现上，猎虫 Collision 根组件使用 `Kinsect` Preset，关闭 Generate Overlap Events，ProjectileMovement 的 UpdatedComponent 指向该根组件。
5. Fly Montage/AnimBP 只负责飞行动画；悬停和附着不需要建立另一套伤害窗口。
6. 把 Kinsect Data 填入 `DA_IG_Combat` 的唯一 Demo 猎虫引用；M3 起该引用是运行时必需项，留空时 Resource 明确保持 inert，不再生成带默认数值的临时猎虫。

### 7.2 E5-B：虫印弹与粉尘表现资产

只有对应 C++ 阶段公开了 Actor Class 后，才在 `/Game/Weapons/InsectGlaive` 下创建配置型蓝图：虫印弹等待 M4-B，粉尘等待 M6；不得为提前完成 E5-B 创建占位 Actor。

| 建议名称 | 父类 | 蓝图只负责 |
|---|---|---|
| `BP_IG_MarkProjectile` | 最终虫印 Projectile C++ 类 | Mesh/Niagara、飞行外观、命中/消散音效 |
| `BP_IG_PowderCloud` | 最终 Powder C++ 类 | 粉尘 Niagara、颜色、范围提示和生命周期表现 |

将 Class 填入 `DA_IG_Combat`。蓝图不得实现以下权威逻辑：唯一虫印替换、Hitzone 局部附着、Owned/Reserved/Consumed、范围筛选、聚合爆炸和伤害提交；这些都由 C++ Resource/Actor 状态机负责。粉尘普通接触不会引爆，只有粉尘集约消费。

### 7.3 DA_TrainingDummy

打开 `/Game/Monster/TrainingDummy/Data/DA_TrainingDummy`：

- MaxHealth 使用便于测试的值，例如 1000。
- Body 只负责 Pawn 物理阻挡。
- 建立三个互不重叠 Hitzone 配置：Head=Red、Torso=Orange、Leg=White。
- 每个部位显式配置 DefenseMultiplier、StaggerRate 和 Hitzone Tag。

### 7.4 BP_TrainingDummy

1. Body Capsule 使用 `MonsterBody`；SkeletalMesh 使用 `NoCollision`，避免与 Capsule 形成两个实体 Body。
2. **不要**在 Blueprint Components 树中手工添加 Head/Torso/Leg Sphere。`AMHGZTrainingDummy::ApplyConfig` 会在 BeginPlay 从 `DA_TrainingDummy.Hitzones` 创建 `UMHGZMonsterHitzoneComponent`；手工组件会被该重建流程销毁或形成重复配置。
3. PIE 中用 `show collision` 检查运行时生成的三个 Sphere：它们必须为 Query Only、`MonsterHitzone`、Object Type=Hitzone，且不产生 Overlap Events。位置、半径、Bone 和颜色只回到 `DA_TrainingDummy` 调整。
4. `UMHGZDummyConfig` 的静态校验只能比较各项填写的 `RelativeLocation + Radius`，无法预测不同骨骼在全部动画帧中的最终世界变换；因此必须用 Debug Draw 和实际 LoopingMontage 人工确认 Head/Torso/Leg 全程不重叠。静态校验通过不能替代此项。
5. 开启 Debug Draw 时，武器 Sweep、猎虫 Sweep 和 Aim 射线应能明确区分 Body 与 Hitzone。Hitzone 不承担实体阻挡；玩家只被 Body Capsule 阻挡，不会被 Sphere 推开。
6. 启用确定性 CounterTestAttack：固定 InitialDelay、Telegraph、ActiveDuration、Interval 和唯一 AttackInstanceID；不加入随机、寻路或自动转向。

### 7.5 L_DemoArena

1. 只使用 `/Game/Maps/L_DemoArena`，不要再建第二张 Demo 地图。
2. 放置一个 PlayerStart 和一个 `BP_TrainingDummy`；木桩正面留出足够距离验证滑翔、觉虫击和空中动作。
3. 地面和墙使用 WorldStatic，确保猎虫/玩家位移能产生可复现阻挡。
4. 在 World Settings 覆写为最终 Demo GameMode。

## 8. E6——UI 与 GameplayCue

### 8.1 Widget

先在 `/Game/UI` 新建 `BP_MHGZHUD`，父类选择 `AMHGZHUD`；不要 Duplicate 其他 HUD。创建完成后回到 `BP_Demo_GameMode`，把 HUD Class 从 E2 的原生 `AMHGZHUD` 替换为该蓝图类。

在 `/Game/UI` 创建：

| Widget | 父类 | 必需内容 |
|---|---|---|
| `WBP_HUD` | `UMHGZMainHUDWidget` | 唯一 `WeaponResourceSlot`，以及 Health/Stamina/Crosshair 容器 |
| `WBP_HealthBar` | `UMHGZUserWidget` | 当前值/最大值显示 |
| `WBP_StaminaBar` | `UMHGZUserWidget` | 当前值/最大值显示 |
| `WBP_Crosshair` | `UMHGZCrosshairWidget` | 默认、Red、Orange、White 样式 |
| `WBP_IG_ResourcePanel` | `UMHGZWeaponResourceWidget` | 猎虫耐力、三灯、舞踏层显示容器 |
| `WBP_IG_KinsectStamina` | 项目通用 Widget 基类 | 猎虫耐力条 |
| `WBP_IG_ExtractDisplay` | 项目通用 Widget 基类 | Red/Orange/White/Triple 状态和倒计时 |

配置 `BP_MHGZHUD` Class Defaults：MainHUDClass、HealthBarClass、StaminaBarClass、CrosshairClass 分别指向上述资产。

完成后把 `DA_WeaponRuntime_IG.ResourceWidgetClass` 指向 `WBP_IG_ResourcePanel`。

必须满足：

- 只有 WBP_HUD 调用 AddToViewport。
- 资源面板只由 HUD AddChild 到 `WeaponResourceSlot`。
- 不创建或配置 UISubsystem Widget 工厂。
- Widget Event Graph 只处理显示和 C++ 暴露的 Delegate，不直接调用 Ability/Resource 写状态。

### 8.2 最小 GameplayCue

在 `/Game/GameplayCues` 创建 Demo 所需表现资产，并让名称/Tag 符合 GameplayCue 发现规则：

- `GameplayCue.Hit.Slash`。
- `GameplayCue.Hit.Kinsect`。
- `GameplayCue.Hit.DamageNumber`。
- `GameplayCue.Character.Dodge`。
- `GameplayCue.IG.ExtractGained`、`TripleUpActivated`、`ExtractExpired`。
- `GameplayCue.Hit.IG.DivingWyvern`。

先使用临时 Niagara/Sound 也可以，但 Cue 必须由 HitFeedbackRouter/资源事件显式执行；不要只把 Tag 放入 DynamicAssetTags。

若代码使用 `AMHGZDamageNumberActor` 对象池，再创建 `BP_DamageNumberActor` 和 `WBP_DamageNumber`，把 WidgetComponent 指向该 Widget，并在最终 Feedback 配置中设置 ActorClass。伤害数字 Actor 自己拥有 WidgetComponent；木桩、Ability 和 HUD 不直接 Spawn 文本 Widget。

## 9. E4-A-support——Motion Matching 与动画图（旧称 E6-A）

本节的 `UpperBody_IGAction` AnimGraph 支持是 E4-A 的直接验收条件；普通移动本身的 CMC/State Machine 重构仍属于 L0～L5，不在这里提前实现。

打开 `ABP_MH_Character`：

1. Motion Matching 节点使用项目正式 Pose Search Schema/Database。
2. 全身攻击/翻滚 Montage Slot 位于 locomotion 输出之后；BlockMovement 时能够切到无 Root Motion Idle Pose，避免 MM 与 Montage 叠加位移。
3. 为送虫/收虫/拔刀放虫建立独立的 `UpperBody_IGAction` Slot：把当前 Armed Motion Matching 输出缓存为 `LocomotionBase`；用此缓存作为该 Slot 的 Source 并缓存为 `KinsectActionPose`。随后按 `IsMoving` 选择两条路径：**静止**路径让 `KinsectActionPose` 全身输出；**移动**路径使用 `Layered Blend per Bone`，Base Pose=`LocomotionBase`、Blend Pose=`KinsectActionPose`，Branch Filter 从骨盆上方第一根脊椎骨（项目骨架为 `spine_01` 时即它）开始并覆盖所有子骨骼。移动路径绝不从 `root` 或 `pelvis` 开始；该层启用 Mesh Space Rotation Blend。两条路径最后都进入全身 `DefaultSlot`，供攻击/翻滚/收刀 Montage 使用；不要把 `UpperBody_IGAction` 改接到 `DefaultSlot`。
4. AnimBP 不写战斗状态、不直接 Reset Combo。
5. FacingRotation/Trajectory 使用 Character C++ 输出；动作拥有 Rotation Token 时普通 locomotion 不再改朝向。送虫/收虫没有 Rotation Token，因此其动作途中继续使用正常持刀转向。
6. 逐个预览外部导入动画的 Skeleton、帧率、Root Motion Root Lock 和脚底高度。
7. 只有确认归属的动作才移出 `Imported/Review`；未知动画不要凭文件名猜测删除或重命名。

## 10. E7——编辑器验证顺序

### 10.1 保存前静态检查

1. `Tools → Validate Data`，对 `DA_WeaponRuntime_IG`、`DA_IG_InputProfile`、`DA_IG_Combat`、`DA_IG_Combo`、全部虫棍 GA 和 `DA_TrainingDummy` 执行验证。
2. 全部目标蓝图 `Compile`，Message Log 中不得有 Error、Unknown Struct 或失效 Pin。
3. 使用 Reference Viewer 确认：

   - `DA_IG_HuoLongGun → DA_WeaponRuntime_IG → Input/Combat/Widget`。
   - `DA_IG_Combat → DA_IG_Combo + 四个精华 GE + Kinsect Data`。
   - 旧 Combo/Resource DataTable 不再被 Runtime 资产引用。

4. 保存全部目标资产，重启编辑器，再运行一次 Data Validation。

### 10.2 分层 PIE

按下列顺序验证，失败时只返回对应层修改：

1. 启动/重生：每次只创建一个 RuntimeHost、一个 Resource、一个猎虫和一份 HUD。
2. 输入：Y/B/YB、LT/RT 先按与最后补齐、角色 Forward 判定、RB 持刀/收刀分流（纳刀 vs 0.1s 后奔跑）；一个物理输入只产生一个 Snapshot。
3. 基础攻击：AttackCollision 只调用当前 Montage 对应 AbilityInstance；旧 BlendOut 不关闭新窗口。
4. 收刀输入互斥：确认仅 `Grounded+Unsheathed` 的 RB 产生收刀快照，空中 RB 不产生；分别以 Idle、Walk 收刀，在 Commit 前、Commit 后尾帧连续按 Y/B/RT/LT 组合和 Dodge；不得激活第二个 GA、不得建立 PendingTransition、不得播放第二个 Montage。收刀 GA 正常结束或被强制硬直/死亡取消后，才按当前 Sheathed/Unsheathed 姿态恢复合法输入。
5. 闪避：Idle 可翻滚；攻击本体/未开 `DodgeAcceptWindow` 的后摇按 Dodge 必须无效；窗口内按 Dodge 只有在新 Dodge 成功 Commit 后才精确结束当前攻击，Dodge 缺 Montage/耐力不足时原攻击必须继续。收刀/持刀 Forward 或无方向走已完成的前向翻滚；前向无输入回 Idle、有实时输入才进入移动衔接。持刀 Left/Right/Back 走各自翻滚并强制回 Idle；收刀 Left/Right/Back 必须拒绝且不耗耐。翻滚全程连续按 Y/B/RT/LT、收刀和 Dodge 都不得启动第二个 GA；`DodgeWindow` 结束/中断后只恢复无敌 Tag和开始前碰撞响应，`Dodging` 则到 Ability End 才释放。
6. 普通猎虫：命中后 Hover，召回到达才交付；交付 Red 后下一次可以取得 White。
7. 三灯：单灯→三灯、三灯吞灯不刷新、自然到期、Classic/Numeric 切换。
8. 地面特殊动作：四连、前向回旋、StarterOnly、反击舞踏。
9. 空中动作：操虫斩、穿刺、惯性、急袭突刺、降龙与落地清理。
10. 虫印/滑翔/粉尘/觉虫击：距离、所有权、原子三灯消费和逐击点灯/粉尘一一对应。
11. UI/反馈：准心颜色、灯/耐力/舞踏、伤害数字与 Cue；Viewport 只有一份 WBP_HUD。

详细预期见 [验证清单](verification.md)。

### 10.3 生命周期压力测试

每项至少连续执行三次：

- 同一 GA Class 快速连续派生，保留旧 Montage BlendOut。
- 攻击/位移中受击、闪避、落地、死亡。
- 真正换武器和重复广播同一武器 Snapshot。
- 只更换护甲/饰品/镶嵌，确认虫棍 Runtime 不重建。
- UnPossess/Possess、死亡重生、多次停止/启动 PIE。
- 猎虫耐力降为 0 并保持多帧，确认音效/ForceRecall 只触发一次。

结束后使用调试输出确认不存在旧 Actor、Timer、Delegate、Loose Tag、Active GE、RootMotionSource、WarpTarget、Reserved Powder 或重复 Widget。

### 10.4 打包

1. 保存并关闭编辑器，执行 Development Editor/游戏目标完整编译。
2. `Platforms → Windows → Package Project`，输出 Win64 Development 包。
3. 在打包版本从进入训练场到完成全部动作验证；不能依赖编辑器临时对象、未保存资产或硬编码 `/Game/...` 回退路径。
4. 若 Cook 报 Missing Asset/Class，先修复引用或 Redirect；不要把失败资产排除出 Cook 来掩盖问题。

## 11. 禁止操作

- 不恢复 ASC `InputBindings`、PlayerController 第二份 MappingContext 或第二个 `IMC_IG`。
- 不让多个单键 GA 与 ChordResolver 同时竞争 Y/B/LT/RT。
- 不恢复旧 Combo/Resource DataTable 运行时查询。
- 不在蓝图 Tick 实现猎虫伤害、精华状态机、连招 FSM 或动作位移。
- 不使用 Yellow 精华，不按 Hitzone 名称硬编码颜色。
- 不让资源 Widget 单独 AddToViewport，不恢复 UISubsystem Widget 工厂。
- 不用 MotionWarping 给零 Root Motion 动画制造整段距离。
- 不把共享 `AttackDirection` 当所有动作的 WarpTargetName。
- 不让 Notify 扫描所有 Active Ability 或保存运行时实例状态。
- 不接通当前已知有缺陷的 WeaponResource EntryModifier。
- 不在资源管理器移动/复制 `.uasset`，不盲目删除 Redirector。

## 12. 最终完成清单

- [ ] 所有目标蓝图无编译错误、失效 Pin 或 Unknown Struct。
- [ ] Data Validation 对 Runtime/Input/Combat/Combo/GA/木桩全部通过。
- [ ] 旧 DataTable 和 Attack 旧字段不再被运行时资产引用。
- [ ] Character、Controller、PlayerState、HUD、RuntimeHost、Resource 的所有权没有重复。
- [ ] 所有必需 GA/Montage/GE/Kinsect/UI/Cue 资产存在并已保存。
- [ ] 木桩三色 Hitzone、实体 Body 和 CounterTestAttack 可复现。
- [ ] [验证清单](verification.md) 的当前 Demo 项在 PIE 通过。
- [ ] 多轮换装、重生和 PIE 后无遗留运行时对象。
- [ ] Win64 Development 包可完整打木桩。
