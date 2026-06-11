# Demo 搭建指南——虫棍打木桩

**目标：** 用固定装备的虫棍角色对木桩完成"LT 瞄准→Y 送虫萃取→三灯→Y 攻击→血条扣减"的完整流程。

**资产来源：** 动画 Montage 和骨骼模型通过**解包**获得（虫棍攻击动画 + 猎虫模型 + 木桩模型）。GameplayCue 粒子和音效先用引擎自带临时资产替代，后续替换。

---

## 零、前提条件

| 条件 | 说明 |
|------|------|
| C++ 全部编译通过 | 所有 Source/MHGZ/ 下的类无编译错误 |
| GameplayTag 已创建 | `Combat.State.Aiming`、`WeaponResource.IG.*`、`Combat.Branch.*`、`UI.Aim.*` 等全部 Tag 在 Project Settings 中注册 |
| 碰撞通道已配置 | `DefaultEngine.ini` 中 Weapon (GameTraceChannel1) + MonsterAttack (GameTraceChannel2) 已定义 |
| GameplayCue 路径已配置 | `DefaultGame.ini` 中 `+GameplayCueNotifyPaths=/Game/GameplayCues` |

---

## 一、C++ 编译顺序

按依赖从底向上逐批编译：

### 第 1 批：基础设施（无项目内依赖）

```
Source/MHGZ/
├── MHGZPlayerState.h/cpp
├── MHGZCharacter.h/cpp
├── MHGZPlayerController.h/cpp
├── Inventory/MHGZItemTypes.h          ← FEquipmentSocket 等结构体
├── AttributeSystem/MHGZAttributeSet.h/cpp
└── AttributeSystem/MHGZWeaponResourceComponent.h/cpp  ← 武器资源基类
```

### 第 2 批：GAS 核心（依赖第 1 批）

```
ActionSystem/
├── MHGZAbilitySystemComponent.h/cpp
├── MHGZGameplayAbility.h/cpp
├── MHGZAttackAbility.h/cpp            ← 碰撞+伤害基类
├── MHGZWeaponComboData.h/cpp          ← FComboNode + 协调器
└── MHGZComboCoordinatorAbility.h/cpp
```

### 第 3 批：虫棍 + 怪物（依赖第 1-2 批）

```
InsectGlaive/Kinsect/
├── KinsectCollisionComponent.h/cpp
├── InsectGlaiveKinsectData.h/cpp
└── Kinsect.h/cpp                      ← AKinsect Actor

AttributeSystem/
└── Res_InsectGlaive.h/cpp             ← 依赖 Kinsect.h + MHGZWeaponResourceComponent

ActionSystem/
└── MHGZInsectGlaiveAbility.h/cpp      ← 依赖 Res_InsectGlaive

Monster/
├── MHGZMonsterHitzoneComponent.h/cpp
├── MHGZDummyConfig.h/cpp
├── MHGZMonsterBase.h/cpp
└── MHGZTrainingDummy.h/cpp
```

### 第 4 批：UI（依赖第 1-3 批）

```
UI/
├── MHGZUserWidget.h/cpp
├── MHGZWeaponResourceWidget.h/cpp
├── MHGZCrosshairWidget.h/cpp
├── MHGZAimComponent.h/cpp
├── MHGZUISubsystem.h/cpp
└── MHGZHUD.h/cpp
```

---

## 二、解包资产准备

### 从游戏解包获取的最小集合

| 用途 | 资产类型 | 解包目标 | 导入后命名 |
|------|------|------|------|
| 虫棍纵斩动画 | AnimSequence/Montage | 虫棍 △ 攻击动画（1 个即可） | `A_IG_Slash_01` → 制作 Montage `Montage_IG_Slash_01` |
| 猎虫模型 | SkeletalMesh | 任意昆虫/猎虫骨骼模型 | `SK_Kinsect_Speed` |
| 猎虫飞行动画 | AnimSequence | 猎虫翅膀扇动循环 | `A_Kinsect_Fly` → 制作 Montage `Montage_Kinsect_Fly` |
| 木桩模型 | SkeletalMesh | 人形靶/木桩骨骼模型 | `SK_Dummy` |
| 木桩闲置动画 | AnimSequence | 木桩呼吸/待机循环 | `A_Dummy_Idle` → 制作 Montage `Montage_Dummy_Idle` |
| 虫棍武器模型（可选） | StaticMesh/SkeletalMesh | 虫棍武器模型 | `SM_IG_Weapon` |

> **若解包不到：** 猎虫模型可以用 UE 自带 Cube/Sphere 缩放代替（验证碰撞逻辑）；木桩用引擎自带 Mannequin 代替；攻击动画用 Mixamo 免费动画临时替代。**先跑通逻辑，再换正式资产。**

### 导入后处理

1. **制作 Montage：** 在编辑器中右键 AnimSequence → Create AnimMontage。攻击 Montage 中需要加 `AnimNotifyState_AttackCollision` 区间（~0.2-0.5s），配置碰撞形状和 Socket。
2. **猎虫动画蓝图：** 创建 `ABP_Kinsect`（继承 `AnimInstance`）→ 添加默认 Slot 节点播放 `Montage_Kinsect_Fly`。
3. **木桩动画蓝图：** 创建 `ABP_Dummy` → 默认播放 `Montage_Dummy_Idle`（Loop）。

---

## 三、编辑器资产创建——C++ 类蓝图

### 3.1 BP_IG_Character（虫棍角色蓝图）

**父类：** `AMHGZCharacter`

**Components 添加：**
- `UMHGZAimComponent`（瞄准检测——必须手动 Add）

**Class Defaults 设置：**
- `Auto Possess Player = Player 0`

### 3.2 BP_TrainingDummy（木桩蓝图）

**父类：** `AMHGZTrainingDummy`

**Components：** 按 `DA_DummyConfig` 的 `Hitzones` 数组自动生成——创建 DataAsset 后再回来配置。

**Mesh 设置：** `SK_Dummy` + `ABP_Dummy`

### 3.3 BP_IG_GameMode（GameMode 蓝图）

**父类：** `AMHGZGameMode`

**Class Defaults 设置：**
- `Default Pawn Class = BP_IG_Character`
- `HUD Class = AMHGZHUD`
- `bUseSeamlessTravel = false`（Demo 用 OpenLevel）

---

## 四、编辑器资产创建——DataAsset

### 4.1 DA_DummyConfig（木桩配置）

**父类：** `UMHGZDummyConfig`

| 字段 | 值 | 说明 |
|------|------|------|
| DisplayMesh | `SK_Dummy` | 木桩骨骼模型 |
| LoopingMontage | `Montage_Dummy_Idle` | 待机循环 |
| Hitzones[0] | BoneName=`Head`, HitzoneTag=`Hitzone.Head`, Shape=Sphere, HalfExtent=(30,30,30), DefenseMultiplier=0.8 | 头部——红灯 |
| Hitzones[1] | BoneName=`Spine`, HitzoneTag=`Hitzone.Torso`, Shape=Capsule, HalfExtent=(40,40,60), DefenseMultiplier=0.5 | 躯干——黄灯 |
| Hitzones[2] | BoneName=`Leg_L`, HitzoneTag=`Hitzone.LeftLeg`, Shape=Capsule, HalfExtent=(20,20,40), DefenseMultiplier=0.3 | 腿——白灯 |

### 4.2 DA_Kinsect_Speed（猎虫品种）

**父类：** `UInsectGlaiveKinsectData`

| 字段 | 值 |
|------|------|
| KinsectDisplayName | "速度型猎虫" |
| KinsectMesh | `SK_Kinsect_Speed` |
| FlyMontage | `Montage_Kinsect_Fly` |
| FlightSpeed | 2000 |
| StraightFlightDistance | 1500 |
| StaminaPool | 100 |
| StaminaRegenRate | 15 |
| HoverDrainRate | 3 |
| FlightDrainRate | 8 |

### 4.3 DA_IG_Weapon（虫棍武器定义）

**父类：** `UMHGZWeaponDefinition`

| 字段 | 值 | 说明 |
|------|------|------|
| ItemID | `IG_IronBlade` | |
| DisplayName | "铁虫棍" | |
| WeaponTypeTag | `Weapon.InsectGlaive` | ★ 关键——武器种类标识 |
| AttackPower | 50 | 基础攻击力 |
| EquipmentSlotTag | `Equipment.Slot.Weapon` | |
| Entries | 空（Demo 不配词条） | |

### 4.4 DA_IG_ComboData（虫棍连招表）

**父类：** `UMHGZWeaponComboData`

**最简配置（1 个招式——能按 Y 打出纵斩即可）：**

| StateName | AbilityClass | RequiredTags | InputTag | Priority |
|------|------|------|------|:--:|
| Idle | `GA_IG_Slash_01` | — | `Input.Weapon.Y` | 0 |
| Idle（红灯版） | `GA_IG_RedSlash_01` | `Combat.Branch.Extract.Red` | `Input.Weapon.Y` | 10 |

> **Demo 阶段只需 Idle → Slash 一条。** 更多招式（Slash→Sweep→RoundSlash 连招链）后续添加。红灯版 GA 可以先用同一蓝图——先验证 Tag 分流逻辑，再替换不同 Montage。

---

## 五、编辑器资产创建——GameplayEffect 蓝图

全部在 `Content/GameplayEffects/InsectGlaive/` 下创建。

### 5.1 GE_Damage（伤害 GE——通用）

| 属性 | 值 |
|------|------|
| DurationPolicy | Instant |
| Modifiers | Attribute=`Health`, Op=Add, Magnitude=`SetByCaller`（Tag=`Damage.Value`） |
| GameplayCueTags | 由 `MakeDamageSpec` 动态注入——蓝图留空 |

### 5.2 GE_IG_WhiteExtract（白灯）

| 属性 | 值 |
|------|------|
| DurationPolicy | HasDuration |
| Duration | `DurationMagnitude=90, DurationMultiplier=1.0` |
| GrantedTags | `WeaponResource.IG.Extract.White` |
| Modifiers | Attribute=`MoveSpeedMultiplier`, Op=Multiply, Magnitude=1.15 |

### 5.3 GE_IG_YellowExtract（黄灯）

| DurationPolicy | HasDuration |
| Duration | `90`（Demo 统一时长，后续分 90/120/60） |
| GrantedTags | `WeaponResource.IG.Extract.Yellow` |
| Modifiers | Attribute=`Defense`, Op=Multiply, Magnitude=1.1 |
| 额外 GrantedTags | `Combat.Poise.Light`（在 GE 的 GrantedTags 数组中添加） |

### 5.4 GE_IG_RedExtract（红灯）

| DurationPolicy | HasDuration |
| Duration | `90` |
| GrantedTags | `WeaponResource.IG.Extract.Red`, `Combat.Branch.Extract.Red` |
| Modifiers | Attribute=`AttackPower`, Op=Multiply, Magnitude=1.2 |

### 5.5 GE_IG_TripleUp（三灯）

| DurationPolicy | HasDuration |
| Duration | `90` |
| GrantedTags | `WeaponResource.IG.TripleUp`, `Combat.Branch.TripleUp`, `Combat.Poise.Medium` |
| Modifiers | AttackPower×1.25, MoveSpeedMultiplier×1.15, Defense×1.15 |

---

## 六、编辑器资产创建——GameplayAbility 蓝图

全部在 `Content/Blueprints/Ability/InsectGlaive/` 下创建。

### 6.1 GA_IG_Slash_01（标准纵斩——无红灯）

**父类：** `UMHGZInsectGlaiveAbility`

| 成员 | 值 |
|------|------|
| InputTag | `Input.Weapon.Y` |
| StaminaCost | 0（Demo 不扣耐） |
| AttackSegments[0].Collision | AttachSocket=`weapon_tip`, Shape=Sphere, Extent=(30,30,30) |
| AttackSegments[0].Damage | DamageEffectClass=`GE_Damage`, MotionValue=1.0, HitCueTag=`GameplayCue.Hit.Slash` |
| Montage | `Montage_IG_Slash_01` |
| MaxCorrectionAngle | 30 |

> **Montage 中必须添加 `AnimNotifyState_AttackCollision`**——区间 ~0.2-0.5s，`ConfigIndex=0`。

### 6.2 GA_IG_RedSlash_01（红灯纵斩——Demo 先用同一蓝图）

**父类：** `UMHGZInsectGlaiveAbility`

暂时和 `GA_IG_Slash_01` 完全相同的配置（包括同一个 Montage）——先验证红灯 Tag 分流逻辑，再替换强化 Montage。

### 6.3 GA_SendKinsect（送虫）

**父类：** `UMHGZGameplayAbility`（不是 InsectGlaiveAbility——不需要攻击碰撞）

| 成员 | 值 |
|------|------|
| InputTag | `Input.Weapon.Y`（和攻击共用一个 Tag——由瞄准态区分：瞄准时 Y=送虫，非瞄准时 Y=攻击） |
| StaminaCost | 0 |

**Event Graph：** `ActivateAbility` → 获取 `URes_InsectGlaive`→ `DeployKinsect()` → `EndAbility`。

> **注意：** `GA_SendKinsect` 的 `CanActivateAbility` 需要检查 `ASC->HasMatchingGameplayTag(Combat.State.Aiming)`——只有瞄准态下 Y 才触发送虫。这个检查在 C++ `UMHGZGameplayAbility` 基类中通过 `ActivationRequiredTags` 实现——蓝图设 `ActivationRequiredTags = Combat.State.Aiming`。

### 6.4 GA_RecallKinsect（召回）

**父类：** `UMHGZGameplayAbility`

| 成员 | 值 |
|------|------|
| InputTag | `Input.Weapon.B` |
| StaminaCost | 0 |

**Event Graph：** `ActivateAbility` → 获取 `URes_InsectGlaive`→ `RecallKinsect()` → `EndAbility`。

### 6.5 GA_Unsheathe（拔刀——虫棍默认持刀）

**父类：** `UMHGZGameplayAbility`

| 成员 | 值 |
|------|------|
| InputTag | `Input.Weapon.Y`（收刀态 Y=拔刀） |
| ActivationRequiredTags | `Combat.State.Sheathed` |
| GrantedTags | `Combat.State.Unsheathed`（激活后移除 Sheathed） |

---

## 七、编辑器资产创建——Widget 蓝图

### 7.1 WBP_HUD（主 HUD 面板）

**父类：** `UUserWidget`（纯蓝图）

**设计器布局：**
```
Canvas Panel（根）
├── Canvas_HealthBar（左上角，勾选 Is Variable = "HealthBarSlot"）
├── Canvas_StaminaBar（血条下方，勾选 Is Variable = "StaminaBarSlot"）
├── Canvas_WeaponResource（中下方，勾选 Is Variable = "WeaponResourceSlot"）
└── Canvas_Crosshair（屏幕中央，勾选 Is Variable = "CrosshairSlot"）
```

### 7.2 WBP_Crosshair（准心）

**父类：** `UMHGZCrosshairWidget`

**设计器：** `Image` 控件（勾选 Is Variable = "CrosshairImage"）。

**Event Graph：** 覆写 `OnAimTargetUpdated`：
- Target==nullptr → CrosshairImage 灰色小点
- ExtractColor==Red → 红色 + PlayAnimation(ZoomPulse)
- ExtractColor==Yellow → 黄色 + ZoomPulse
- ExtractColor==White → 白色 + ZoomPulse

### 7.3 WBP_HealthBar / WBP_StaminaBar

**父类：** `UMHGZUserWidget`

**设计器：** `ProgressBar` + `TextBlock`。

**Event Graph：** 覆写 `OnValueUpdated(Current, Max)`：
- `ProgressBar.SetPercent(Current/Max)`
- `TextBlock.SetText("{Current} / {Max}")`
- Current/Max > 0.6 → 绿色
- 0.3~0.6 → 黄色
- < 0.3 → 红色

### 7.4 WBP_IG_ResourcePanel（虫棍资源面板）

**父类：** `UMHGZWeaponResourceWidget`

**设计器：** 两个子 Widget 容器——`WBP_IG_KinsectStamina` + `WBP_IG_ExtractDisplay`（直接拖入或预留 Named Slot）。

**C++ 绑定（自动）：** 父类 `BindToResourceComponent` 已处理 Delegate 和 Tag 订阅——蓝图只需实现响应函数。

### 7.5 WBP_IG_KinsectStamina + WBP_IG_ExtractDisplay

同 `insect-glaive.md §六` 设计——`ProgressBar` + 三个 `Image`（灯图标）。Demo 阶段最简实现即可。

---

## 八、编辑器中集成

### 8.1 DT_WeaponResourceConfig

| WeaponTypeTag | ResourceWidgetClass |
|------|------|
| `Weapon.InsectGlaive` | `WBP_IG_ResourcePanel` |

### 8.2 GameMode 配置

打开 `BP_IG_GameMode`：
- `Default Pawn Class = BP_IG_Character`
- `HUD Class = AMHGZHUD`

打开 `BP_IG_Character` → Class Defaults：
- `Auto Possess Player = Player 0`

### 8.3 Input 配置

创建 InputAction 资产：
- `IA_Y`（轻攻击/送虫）
- `IA_B`（重攻击/召回）
- `IA_LT`（瞄准）
- `IA_RT`（收刀直飞——Demo 可暂不配）

创建 InputMappingContext `IMC_IG`：
- `IA_Y` → `Input.Weapon.Y`（Triggered + Completed）
- `IA_B` → `Input.Weapon.B`
- `IA_LT` → `Input.Modifier.Aiming`（Triggered + Completed）

打开 `BP_IG_Character` → `UMHGZAbilitySystemComponent` → `InputBindings` 数组：
- [0] InputAction=`IA_Y`, AbilityTag=`Input.Weapon.Y`
- [1] InputAction=`IA_B`, AbilityTag=`Input.Weapon.B`
- [2] InputAction=`IA_LT`, AbilityTag=`Input.Modifier.Aiming`

### 8.4 初始装备配置

在 `BP_IG_Character` 的 `BeginPlay` 或 GameMode 的 `StartPlay` 中：
1. 从 `UMHGZDataManager` 获取 `DA_IG_Weapon`
2. `EquipmentComponent->EquipItem(Equipment.Slot.Weapon, DA_IG_Weapon)`
3. 这会触发 `OnEquipmentChanged` → 创建 `URes_InsectGlaive` → Spawn `AKinsect` → 授予连招协调器

### 8.5 木桩放置

在关卡 `Lvl_ThirdPerson`（或新建 `Lvl_IG_Demo`）中：
- 拖入 `BP_TrainingDummy` → 位置 (X=1000, Y=0, Z=0)
- `DA_DummyConfig` 设为其 Config 资产
- 运行 `ApplyConfig` 生成 Hitzone 碰撞体

---

## 九、PIE 测试流程

按顺序逐项验证：

| 步骤 | 操作 | 预期 | 验证文档章节 |
|:--:|------|------|:--:|
| 1 | 启动 PIE | WBP_HUD 显示；血条满 100/100 绿色；耐力条满 100/100 绿色；准心可见 | `ui-system.md §九·1` |
| 2 | 按 Y | 拔刀（ASC 持有 Unsheathed Tag） | `actions.md` |
| 3 | 按住 LT | 准心出现；AimComponent Tick 启动 | `ui-system.md §九·5` |
| 4 | LT 瞄准木桩头部 | 准心变红 + 缩放动画 | `insect-glaive.md §十二·UI-1` |
| 5 | 瞄头时按 Y | 猎虫从手臂 Detach → 沿准心飞出 → 碰到木桩 Head → 自动返回 | `insect-glaive.md §十一·0g/0c` |
| 6 | 猎虫回手 | 红灯图标亮起 + 倒计时；ASC 持有 `Extract.Red` + `Branch.Extract.Red` | `insect-glaive.md §十一·3` |
| 7 | 不瞄时按 Y | 打出纵斩 Montage；碰撞窗口检测到木桩 → ApplyDamage → 木桩受击 | `insect-glaive.md §十一·9` |
| 8 | 木桩受击 | 伤害数字浮空；血条扣减（若木桩挂了 AttributeSet） | `monster-system.md` |
| 9 | 三灯萃取完毕 | 三个灯逐一亮起 → 自动三灯齐聚 → UI 三灯合一光环 | `insect-glaive.md §十一·6` |
| 10 | 三灯后按 Y | 攻击激活 → `PlaySound2D(TripleUpSwingSound)` | `insect-glaive.md §十一·11` |

---

## 十、Demo 简化策略

| 复杂功能 | Demo 做法 |
|------|------|
| 连招表多招式 | 只需 1 招 Idle→Slash |
| 红灯版招式 | 复用同一 GA 蓝图——先验证 Tag 分流 |
| 猎虫悬停 | 先不做——萃取成功后直接 Returning |
| 收刀直飞（RT） | 不做——只做 LT+Y 瞄准送虫 |
| 消耗灯特殊技 | 不做——只验证萃取+三灯 |
| 装备词条 | 不做——固定白板铁虫棍 |
| GameplayCue 粒子 | 引擎自带粒子临时替代 |
| 猎虫 FlyMontage | 无动画时用纯 Mesh 飞行（没翅膀扇动也能验证碰撞） |
| 三灯攻击音效 | 用引擎自带提示音临时替代 |
| UI 动画（缩放/消散等） | 不做——只做颜色切换 |

---

## 十一、依赖文档索引

| 需要查阅的文档 | 场景 |
|------|------|
| `gas-infrastructure.md` | ASC 初始化、PlayerState 组件架构 |
| `attributes.md` | Health/Stamina Attribute + Clamp 约束 |
| `actions.md` | AttackAbility 配置、InputBinding、连招表结构 |
| `insect-glaive.md` | 虫棍全部系统设计（猎虫/萃取/消耗/UI） |
| `monster-system.md` | 木桩配置、HitzoneComponent |
| `gameplay-cue.md` | GameplayCue 注册路径、GC_HitBase 配置 |
| `ui-system.md` | HUD/Widget/数据绑定模式 |
| `gameplay-tags.md` | Tag 完整层级 |
| `design-decisions.md` | 所有决策编号引用 |
