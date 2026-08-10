# UE5.6 编辑器接线指南——虫棍木桩 Demo

> **使用时机：** 仅在 [Demo 实施计划](../design/demo-implementation-plan.md) 的对应 C++ 里程碑已经编译通过后执行。本文不指导修改 C++，只说明代码完成后需要在 Unreal Editor 5.6 中进行的资产迁移、蓝图/DataAsset 接线、Montage/碰撞/UI 配置和人工验收。

> **编辑器真相源：** 本文决定“在编辑器里做什么和按什么顺序做”；玩法和代码接口分别以 [虫棍动作设计](../design/insect-glaive-actions.md)、[虫棍资源设计](../design/insect-glaive.md) 和 [冻结实施计划](../design/demo-implementation-plan.md) 为准。字段名称与重构后的 C++ 不一致时先停止，不要凭相似名称猜填。

## 1. 执行顺序

严格按下列顺序操作，前一阶段未通过时不要批量制作后一阶段资产。

| 阶段 | 编辑器工作 | 完成标志 |
|---|---|---|
| E0 | 首次启动、保留资产体检、旧原型删除前审计 | 保留资产无 Missing Class/Property；删除清单与引用链已确认，尚不执行删除 |
| E1 | Project Settings、碰撞、Tag、插件检查 | Hitzone Object Channel 和输入基础可用 |
| E2 | 核心蓝图接线 | GameMode、Controller、PlayerState、Character、HUD 关系唯一 |
| E3 | Runtime/Input/Combat/Combo DataAsset | 武器只从 RuntimeDefinition 接线；正式数据壳存在，Combat/Combo 的动作类引用待 E4 完成 |
| E4 | GE、GA、Montage、Notify | 每个动作可独立激活并正确结束 |
| E5 | 猎虫、木桩和测试攻击器 | 三色部位、普通放虫和反击输入可复现 |
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
   - `/Game/Input/Actions/MHGZ/IA_RTA`、`IA_RTB`、`IA_RTY`、`IA_YB`；M1 移除代码读取、E2 清 PlayerState、E3 清 IMC 后，才在 E3 删除。

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
5. M1 只移除代码侧旧输入读取；E2 清空 `BP_PlayerState.ASC.InputBindings`；E3 §5.3 从 `IMC_MHGZ_Demo` 移除旧组合映射。两处资产引用都归零后，才在 E3 删除 `IA_RTA/IA_RTB/IA_RTY/IA_YB`。保留 `IA_A`，直到 E3 明确它是否复用为闪避输入。
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

打开 `Project Settings → Engine → Collision`，保留现有 `Weapon`、`MonsterAttack` Trace Channel，并新增或确认：

| 名称 | 类型 | 默认响应 | 用途 |
|---|---|---|---|
| `Hitzone` | Object Channel，`bTraceType=false` | Ignore | 怪物部位查询身份；武器、猎虫和 Aim 根据各自查询方式命中 |

创建或校准以下 Collision Preset：

| Preset | Object Type | Collision Enabled | 关键响应 |
|---|---|---|---|
| `MonsterBody` | Pawn | Query and Physics | Pawn=Block；Visibility=Ignore；不承担精华/肉质查询 |
| `MonsterHitzone` | Hitzone | Query Only | Weapon=Block；Visibility=Block；Pawn=Ignore；MonsterAttack=Ignore |
| `Kinsect` | WorldDynamic | Query Only | WorldStatic=Block；Pawn/Hitzone=Ignore；部位命中由代码 Capsule Sweep 负责 |
| `PlayerCapsule` | Pawn | Query and Physics | Pawn=Block；Weapon/MonsterAttack 的运行时变化由 Dodge/受击系统管理 |

保存后重启编辑器，确认 `Hitzone` 在组件 Object Type 下拉框中存在。不要建立第二个 Kinsect Object Channel。

### 3.3 GameplayTags

打开 `Project Settings → Gameplay Tags`，运行 Tag 检查。Native Tag 已存在时不要在配置中重复建立。至少确认以下根可解析：

- `Input.Weapon.*`、`Input.Dodge`。
- `Combat.State.Grounded/Aerial/Sheathed/Unsheathed`。
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
2. Core Abilities 中加入代码要求的通用 Ability（至少基础 Dodge；死亡等未实现项不要伪配空蓝图）。
3. 删除迁移完成后的 ASC `InputBindings` 配置；ASC 不绑定 `IA_Y/IA_B/IA_LT/IA_RT`。
4. 不在 PlayerState 创建虫棍 Resource、猎虫、粉尘或 Pawn 相关 Timer。
5. 若 PlayerState Tick 过去只为虫棍 Resource 开启，改为关闭。

### 4.2 Demo PlayerController

1. 挂载且只挂载一份 `UMHGZInputComponent` 和 `UMHGZWeaponInputRouterComponent`。
2. `UMHGZInputComponent` 的默认 Mapping Context 只加入 `IMC_MHGZ_Demo` 一次。
3. 清空 PlayerController 自身旧 `DefaultMappingContexts` 或 BeginPlay `AddMappingContext` 蓝图逻辑。
4. 不在蓝图中实现 Chord、连招状态或按武器类型分支。
5. 确认 Possess/UnPossess 后由 C++ 重新绑定，蓝图不重复调用 SetupInput。

### 4.3 BP_IG_Character

打开 `/Game/Blueprints/Characters/Demo/BP_IG_Character`：

1. 确认组件只有一份：WeaponRuntimeHost、AimComponent、MotionWarping、CharacterMovement，以及角色 Mesh/武器 Mesh。
2. 武器 SkeletalMeshComponent 的 Component Tag 设置为代码约定的 `WeaponTrace`。
3. 武器附着到角色 `Weapon_R` Socket；武器自身用于判定的 `Root`、`IG_FrontTip`、`IG_RearTip` Socket 必须可在 Skeleton Tree 中预览。
4. E2 不给默认武器字段接入仍待删除的旧 `DA_IG_HuoLongGun`；保持空值。E3 §5.1 重建同名正式资产后再指向它。Character 不直接引用 ComboData、Resource Class 或 Resource Widget。
5. Anim Class 指向最终 `ABP_MH_Character`。
6. 检查 Character 蓝图没有每帧 SetActorRotation、LaunchCharacter 或 Timeline 位移与新的 MovementTask 并行写入。

### 4.4 GameMode 与 HUD

在 Demo GameMode 中配置：

| 字段 | 值 |
|---|---|
| Default Pawn Class | `BP_IG_Character` |
| Player Controller Class | 最终 Demo PlayerController |
| Player State Class | `BP_PlayerState` |
| HUD Class | `BP_MHGZHUD`（父类 `AMHGZHUD`） |

在 `Project Settings → Maps & Modes`：

- Editor Startup Map 与 Game Default Map 均使用 `/Game/Maps/L_DemoArena`。
- Default GameMode 使用上述 Demo GameMode。

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
| AttachSocket | `Weapon_R` |
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

不要在 Enhanced Input 资产中配置 Y+B 等 Chord Trigger；组合由 Router 的 InputProfile 解析。

先从 `IMC_MHGZ_Demo` 删除 `IA_RTA/IA_RTB/IA_RTY/IA_YB` 的所有 Mapping。用 Reference Viewer 同时确认 `BP_PlayerState` 与 IMC 都不再引用它们，再通过 Content Browser 删除这四个旧 InputAction；不要删除 `IA_Y/IA_B/IA_LT/IA_RT`。

### 5.4 DA_IG_InputProfile

配置 RawAction→PhysicalInputTag，并建立下列输出。字段使用重构后的实际名称：

| 输出 | TriggerControls | RequiredHeldModifiers | Aim Context | 备注 |
|---|---|---|---|---|
| Y | Y | — | None | 普通武器输入 |
| B | B | — | None | 地面普通输入；空中由 Combo 状态解释 |
| YB | Y+B | — | None | 四连印斩/前向回旋斩 |
| LTY | Y | LT | Kinsect | 地面送虫；操虫斩舞踏中可派生穿刺 |
| LTB | B | LT | Kinsect | 地面召回；空中操虫斩 |
| LTRT | RT | LT | Kinsect | 虫印弹；LT/RT 任意顺序均可成立 |
| RTY | Y | RT | Action | 猎虫滑翔 |
| LTYB | Y+B | LT | None | 地面粉尘集约；空中降龙 |
| RTYB | Y+B | RT | Action | 觉虫击 |

设置：

- `bRequireExactModifiers=true`。
- TriggerControls 必须在 ChordGracePeriod 内；LT/RT 可预先持有或在等待期内最后补齐。
- DirectionInputThreshold、ForwardConeHalfAngle 和 ChordGracePeriod 使用一个明确 Demo 初值，并保留为 DataAsset 可调参数。
- 角色面向画面左时，摇杆左必须解析为 Forward。

运行 Data Validation，故意制造一个重复 Chord/Priority 并确认能报错，再恢复正确配置。

### 5.5 DA_IG_Combat

打开建壳事务中新建的 `DA_IG_Combat`。E3 先填写不依赖 E4 资产的模式、数值、猎虫、位移和范围参数，并把新建的空壳 `DA_IG_Combo` 接入 ComboData；四个精华 GE、动作类、Montage 和完整 Transitions 等待 E4 创建后回填。E3 允许这些明确列出的引用暂时为空，但不得运行 PIE 或把 Data Validation 失败当作正式完成；全量验证在 E4 回填后执行：

1. `RedExtractMode` 默认 `ClassicMovesetGate`。
2. ComboData 指向唯一 `DA_IG_Combo`。
3. 配置并引用四个精华 GE：White、Orange、Red、TripleUp。
4. 配置精华持续时间、红/白/橙/三灯数值 Buff；这些数值只在此资产维护。
5. 配置 `MaxDanceStacks` 和逐层伤害倍率数组；数组长度必须覆盖 0～最大层。
6. 配置猎虫速度、普通/悬停耐力消耗、附着回复、召回速度、到达半径和最大距离。
7. 配置四连/回旋、滑翔、操虫斩、穿刺、跳跃斩、急袭突刺、降龙、觉虫击的位移/修正角度参数。
8. 觉虫击修正角固定支持正前方上下左右 ±60°，猎人飞行距离另设上限。
9. 配置唯一 Demo 猎虫品种、虫印弹/粉尘 Class 和粉尘参数。
10. 指定萃取成功、三灯激活/到期、猎虫耐力归零等资源音效；没有正式资源时可用明确命名的临时音效，但不能留空后再用硬编码路径加载。
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

E4 回填完成后运行 Data Validation，消除重复 TransitionID、并列 Priority、非法 StateOnly 和缺失 AbilityClass。E3 的临时空壳不是阶段最终验收数据，不得拿它运行 PIE 或宣称 Combo 已完成。

## 6. E4——GameplayEffect、Ability 与 Montage

### 6.1 精华 GameplayEffect

在 `/Game/GameplayEffects/InsectGlaive` 创建或迁移：

| 资产 | Duration Policy | Granted Tags | 说明 |
|---|---|---|---|
| `GE_IG_WhiteExtract` | Has Duration | `WeaponResource.IG.Extract.White` | 移速等数值由 CombatConfig/运行时 Spec 注入 |
| `GE_IG_OrangeExtract` | Has Duration | `WeaponResource.IG.Extract.Orange` | 防御/霸体按最终 C++ 合同注入 |
| `GE_IG_RedExtract` | Has Duration | `WeaponResource.IG.Extract.Red`、`Combat.Branch.Extract.Red` | Classic 正常动作权限 |
| `GE_IG_TripleUp` | Has Duration | `WeaponResource.IG.TripleUp`、`Combat.Branch.Extract.Red` | 不授予三个单灯 Tag，不允许刷新 |

将四个 Class 填回 `DA_IG_Combat`。不要在 GE 蓝图中再硬编码另一套持续时间真相源。

### 6.2 Ability 蓝图

旧 `GA_IG_BaDao`、`GA_IG_R_TuCI` 只属于已删除原型，不 re-parent、不 Duplicate、不复制 Event Graph。基于最终原生父类，在 `/Game/Blueprints/Ability/InsectGlaive` 全新创建以下 Demo 必需 Ability：

- 现有《世界》地面基底和四个 Starter 动作。
- `GA_IG_SendKinsect`、`GA_IG_DrawAndSendKinsect`、`GA_IG_RecallKinsect`。
- `GA_IG_TetrasealSlash`、`GA_IG_AdvancingRoundslash`。
- `GA_IG_MarkTarget`、`GA_IG_PowderVortex`、`GA_IG_KinsectGlide`。
- `GA_IG_AwakenedKinsectAttack`。
- `GA_IG_KinsectSlash`、`GA_IG_EnhancedKinsectSpiker`。
- `GA_IG_StrongJumpingSlash`、`GA_IG_DescendingThrust`、`GA_IG_DivingWyvern`。

每个蓝图只配置数据：Montage、AttackSegments、成本、位移请求和动作特有参数；不要在 Event Graph 复制 Coordinator、资源状态机或直接操作 UI。武器动作必须继承重构后的虫棍动作基类，并保持 native `InstancedPerExecution`。旧拼音 GA 名称不作为最终资产名占位；最终名称按本节动作清单建立。

《世界》地面基底若需要 Classic 无红灯弱动作与红灯正常动作，分别配置明确 GA/Montage 或同 Montage 的明确 Section，并由 ComboData Tag 条件选择；Numeric 模式只走正常动作。不要在 AnimBP 根据红灯临时替换动作。

基础 `GA_Dodge` 单独配置角色通用收刀 Montage；虫棍 RuntimeDefinition/CombatConfig 如提供持刀覆写，只填同一最终入口，不恢复旧 Dodge DataTable。

每个有挥棍声音的 GA 填写稳定 `AudioIdentityTag`；具体武器若需要覆盖音色，在 `DA_IG_HuoLongGun` 的 SwingSoundOverrides 中按同一 Tag 配置。挥空音效放 AnimNotify，命中音效由 GameplayCue/FeedbackRouter 处理，避免一次命中播放两套声音。

### 6.3 Montage 通用检查

旧 `AM_Shth_BaDao`、`AM_Shth_R_TuCi` 没有可迁移的自定义 Notify，已随旧 GA 删除。以保留的 `AS_Shth_*` AnimSequence 为源全新创建每个最终动作 Montage，并按以下顺序检查：

1. Skeleton 与 `BP_IG_Character` Mesh 一致，Retarget 后骨骼无报错。
2. Slot 与 `ABP_MH_Character` 中实际 Slot 节点一致。
3. 有入口差异的动作创建明确 Section，例如 `Entry_From_Idle`、`Entry_From_PreviousAction`；GA 只根据 ActivationContext.SourceState 选择。
4. 每个 AttackCollision Notify 的 ConfigIndex 必须对应 Ability 的一个 AttackSegment。
5. ComboWindow、DodgeAcceptWindow、CounterWindow、PoiseWindow 只标记精确帧区间。
6. Dodge Montage 使用 DodgeWindow，并确认结束后恢复的是开始前碰撞响应。
7. 只有实际包含 Root Motion 的动画段添加 MotionWarping Notify；零 Root Motion 位移动作由 MovementTask 驱动。
8. WarpTarget 不在蓝图写固定共享名；由代码按 ActionToken 生成。
9. 不在 Notify 蓝图保存 Ability 指针、Handle 或 Token。玩家动作 Notify 必须使用重构后的原生类。

### 6.4 特殊动作时间轴

- 四连印斩：配置四个独立 AttackSegment/窗口；相邻段可重叠，但 ConfigIndex 各自独立。
- 突进回旋斩：移动阶段内放 CounterWindow；成功反击后由 GA/Coordinator 分支，不由 Montage 猜结果。
- 操虫斩：命中窗口和 MovementTask 的结束点分开；只有成功命中增加舞踏。
- 强化操虫穿刺：只在操虫斩舞踏来源的派生窗口开放。
- 强化跳跃斩、急袭突刺：校准末速度与惯性交接。
- 急袭突刺、降龙：落地段 Notify 不得被通用落地 Reset 提前截断。
- 觉虫击：猎虫贯穿与猎人位移是两个独立运行时请求；Montage 只负责表现时序。

## 7. E5——猎虫、木桩和训练场

### 7.1 猎虫资产

在 `/Game/Weapons/InsectGlaive` 下配置：

1. `DA_IG_Kinsect_Speed`：引用正式猎虫 Mesh/Material、`ABP_IG_Kinsect`、`AM_IG_Kinsect_Fly`，并填写 Demo 速度/耐力基础值。
2. 猎虫蓝图若仅用于指定 Mesh/AnimClass，父类使用最终 `AKinsect`，不要在 Tick 蓝图重复飞行、Overlap 或萃取逻辑。
3. 猎虫 Collision 根组件使用 `Kinsect` Preset，ProjectileMovement 的 UpdatedComponent 指向该根组件。
4. Fly Montage/AnimBP 只负责飞行动画；悬停和附着不需要建立另一套伤害窗口。
5. 把 Kinsect Data 填入 `DA_IG_Combat` 的唯一 Demo 猎虫引用。

### 7.2 虫印弹与粉尘表现资产

如果最终 C++ 暴露了对应 Actor Class，在 `/Game/Weapons/InsectGlaive` 下创建配置型蓝图：

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

1. Body Capsule/Mesh 使用 `MonsterBody`。
2. Head、Torso、Leg 分别使用 QueryOnly 的 `MonsterHitzone`，Object Type 必须为 Hitzone。
3. 调整三个碰撞体，使同一条正面射线不会在同一位置重叠多个部位。
4. 开启 Debug Draw 时，武器 Sweep、猎虫 Sweep 和 Aim 射线应能明确区分 Body 与 Hitzone。
5. 启用确定性 CounterTestAttack：固定 InitialDelay、Telegraph、ActiveDuration、Interval 和唯一 AttackInstanceID；不加入随机、寻路或自动转向。

### 7.5 L_DemoArena

1. 只使用 `/Game/Maps/L_DemoArena`，不要再建第二张 Demo 地图。
2. 放置一个 PlayerStart 和一个 `BP_TrainingDummy`；木桩正面留出足够距离验证滑翔、觉虫击和空中动作。
3. 地面和墙使用 WorldStatic，确保猎虫/玩家位移能产生可复现阻挡。
4. 在 World Settings 覆写为最终 Demo GameMode。

## 8. E6——UI 与 GameplayCue

### 8.1 Widget

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

## 9. E6-A——Motion Matching 与动画图

打开 `ABP_MH_Character`：

1. Motion Matching 节点使用项目正式 Pose Search Schema/Database。
2. 攻击 Montage Slot 位于 locomotion 输出之后；BlockMovement 时能够切到无 Root Motion Idle Pose，避免 MM 与 Montage 叠加位移。
3. AnimBP 不写战斗状态、不直接 Reset Combo。
4. FacingRotation/Trajectory 使用 Character C++ 输出；动作拥有 Rotation Token 时普通 locomotion 不再改朝向。
5. 逐个预览外部导入动画的 Skeleton、帧率、Root Motion Root Lock 和脚底高度。
6. 只有确认归属的动作才移出 `Imported/Review`；未知动画不要凭文件名猜测删除或重命名。

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
2. 输入：Y/B/YB、LT/RT 先按与最后补齐、角色 Forward 判定；一个物理输入只产生一个 Snapshot。
3. 基础攻击：AttackCollision 只调用当前 Montage 对应 AbilityInstance；旧 BlendOut 不关闭新窗口。
4. 闪避：方向来自快照；DodgeWindow 结束/中断后 Tag 和碰撞响应恢复。
5. 普通猎虫：命中后 Hover，召回到达才交付；交付 Red 后下一次可以取得 White。
6. 三灯：单灯→三灯、三灯吞灯不刷新、自然到期、Classic/Numeric 切换。
7. 地面特殊动作：四连、前向回旋、StarterOnly、反击舞踏。
8. 空中动作：操虫斩、穿刺、惯性、急袭突刺、降龙与落地清理。
9. 虫印/滑翔/粉尘/觉虫击：距离、所有权、原子三灯消费和逐击点灯/粉尘一一对应。
10. UI/反馈：准心颜色、灯/耐力/舞踏、伤害数字与 Cue；Viewport 只有一份 WBP_HUD。

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
