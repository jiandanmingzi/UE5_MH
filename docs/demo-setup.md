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

### 3.3 BP_Demo_GameMode（GameMode 蓝图）

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

**创建步骤：**

```
Content/Weapons/InsectGlaive/Data/ 右键 → Miscellaneous → Data Asset
  → 搜索 "WeaponDefinition" → 选中 UMHGZWeaponDefinition → 命名 "DA_IG_Weapon"
```

双击打开，填写：

| 字段 | 值 | 说明 |
|------|------|------|
| ItemID | `IG_IronBlade` | 主资产 ID（`FPrimaryAssetId` 由此生成） |
| DisplayName | "铁虫棍" | 物品显示名称 |
| WeaponTypeTag | `Weapon.InsectGlaive` | ★ 关键——所有 DT 靠此 Tag 匹配 |
| AttackPower | 50 | 基础攻击力 |
| EquipmentSlotTag | `Equipment.Slot.Weapon` | 装备槽位 |
| RarityLevel | 1 | Demo 白板 |
| Entries | 空 | Demo 不配词条 |
| Sockets | 空 | |
| Mesh | `SKM_chong`（或留空） | 武器骨骼网格体，视觉显示用 |
| AttachSocket | `Weapon_R` | 挂载到角色骨骼的 Socket 名 |
| SwingSoundOverrides | 空 | Demo 不配武器音效覆盖 |

### 4.4 DA_IG_ComboData（虫棍连招表）

**创建步骤：**

```
Content/Weapons/InsectGlaive/Data/ 右键 → Miscellaneous → Data Asset
  → 搜索 "WeaponComboData" → 选中 UMHGZWeaponComboData → 命名 "DA_IG_ComboData"
```

双击打开，设置 `GlobalComboTimeout = 10.0`，然后点击 `ComboTable` 旁边的 `+` 添加 **6 个元素**，逐行填入：

| # | StateName | bMatchAnyState | AbilityClass | InputTag | RequiredTags | BlockedTags | Priority | NextState |
|---|-----------|:---:|-------------|----------|--------------|-------------|:--------:|-----------|
| 0 | *空* | ✅ | `GA_Unsheathe` | `Input.Weapon.Y` | `Combat.State.Sheathed` | — | 30 | *空* |
| 1 | *空* | ✅ | `GA_SendKinsect` | `Input.Weapon.Y` | `Combat.State.Aiming` | `Combat.State.Hitstun`, `Combat.State.Knockdown` | 20 | *空* |
| 2 | *空* | ✅ | `GA_RecallKinsect` | `Input.Weapon.B` | `WeaponResource.IG.Kinsect.Active` | `Combat.State.Hitstun`, `Combat.State.Knockdown` | 15 | *空* |
| 3 | *空* | ✅ | `GA_DrawAndSendKinsect` | `Input.Weapon.RT` | `Combat.State.Sheathed` | — | 10 | *空* |
| 4 | `Idle` | ❌ | `GA_IG_RedSlash_01` | `Input.Weapon.Y` | `Combat.Branch.Extract.Red` | — | 10 | `Idle` |
| 5 | `Idle` | ❌ | `GA_IG_Slash_01` | `Input.Weapon.Y` | — | — | 0 | `Idle` |

> **操作提示：**
> - 点击 `ComboTable` 的 `+` → 展开新元素 → 逐字段填写
> - `StateName` = `"Idle"` 是字符串，不是 Tag（直接输入 `Idle`）
> - `InputTag` / `RequiredTags` / `BlockedTags` 是 GameplayTag——点击下拉搜索选择
> - `AbilityClass` 先留空（选 `None`），等 GA 蓝图创建完毕后再回来选上
> - `bMatchAnyState=true` 的行 `NextState` **必须留空**（不转移协调器状态）
> - `bMatchAnyState=false` 的行 `NextState` 填 `"Idle"`（攻击结束后回归 Idle）
> - 其他省略字段用默认值：`StaminaRequired=0`、`DirectionalInput=None`、`bRequiresHitToGrantTags=false`、`bRequiresWindowOpen=false`、`bAutoTransition=false`

**完整配置（6 个节点——还原虫棍完整战斗流程：拔刀→瞄准→送虫→萃取→三灯→攻击→收虫→收刀直飞）：**

| StateName | bMatchAnyState | BlockedStateNames | AbilityClass | InputTag | RequiredTags | BlockedTags | Priority |
|------|:--:|------|------|------|------|------|:--:|
| — | ✅ | `[]` | `GA_Unsheathe` | `Input.Weapon.Y` | `Combat.State.Sheathed` | — | 30 |
| — | ✅ | `[]` | `GA_SendKinsect` | `Input.Weapon.Y` | `Combat.State.Aiming` | `Combat.State.Hitstun`, `Combat.State.Knockdown` | 20 |
| — | ✅ | `[]` | `GA_RecallKinsect` | `Input.Weapon.B` | `WeaponResource.IG.Kinsect.Active` | `Combat.State.Hitstun`, `Combat.State.Knockdown` | 15 |
| — | ✅ | `[]` | `GA_DrawAndSendKinsect` | `Input.Weapon.RT` | `Combat.State.Sheathed` | — | 10 |
| Idle | ❌ | — | `GA_IG_RedSlash_01` | `Input.Weapon.Y` | `Combat.Branch.Extract.Red` | — | 10 |
| Idle | ❌ | — | `GA_IG_Slash_01` | `Input.Weapon.Y` | — | — | 0 |

> **节点 1（拔刀）：** `bMatchAnyState=true, BlockedStateNames=[]`——从任意招式状态（含 Idle）均可触发拔刀。`Priority=30` 最高，收刀态（`Sheathed`）下按 Y 优先拔刀。拔刀后 ASC 持有 `Unsheathed` Tag，后续 Y 键回退到攻击/送虫节点。
>
> **节点 2（送虫）：** `bMatchAnyState=true, BlockedStateNames=[]`——从任意状态可送虫。瞄准态（`Aiming`）下按 Y → RequiredTags 满足 → 优先匹配（Priority 20）。
>
> **节点 3（召回）：** `bMatchAnyState=true, BlockedStateNames=[]`——从任意状态可召回。有虫放出（`Kinsect.Active`）时按 B → 匹配成功。
>
> **节点 4（收刀直飞）：** `bMatchAnyState=true, BlockedStateNames=[]`——从任意状态可触发。收刀态（`Sheathed`）按 RT → 单发普通萃取。
>
> **节点 5/6（攻击）：** `bMatchAnyState=false`，仅 `Idle` 态可触发。红灯存在→`GA_IG_RedSlash_01` 匹配（Priority 10）。无红灯→`GA_IG_Slash_01`。
>
> **★ `BlockedStateNames`（新增字段）：** 仅 `bMatchAnyState=true` 时生效——排除列表中的源状态。Demo 中送虫/收虫/拔刀/直飞均 `BlockedStateNames=[]`（空=匹配任意状态）。太刀特殊纳刀应设 `BlockedStateNames=["Idle","Sheathed"]`（任意派生但不可起手）。
>
> **★ M-7 修复——省略字段的默认值：** `NextState`（必填）对 `bMatchAnyState=true` 的节点填 `""`（空=不转移状态）；对 `bMatchAnyState=false` 的节点填 `"Idle"`（攻击结束后回归 Idle）。其他省略字段使用 C++ 默认值：`StaminaRequired=0`、`DirectionalInput=EDirectionalInput::None`、`bRequiresHitToGrantTags=false`、`bRequiresWindowOpen=false`、`bAutoTransition=false`。

---

### 4.5 DT_WeaponComboConfig（武器→连招表映射）

**创建步骤：**

```
Content/Weapons/InsectGlaive/Data/ 右键 → Miscellaneous → Data Table
  → Row Structure 搜索 "FWeaponComboConfigRow" → 选中 → 命名 "DT_WeaponComboConfig"
```

打开后点击 `+ Add`，仅需 **1 行**：

| Row Name | WeaponTypeTag | ComboDataAsset |
|----------|---------------|----------------|
| `IG` | `Weapon.InsectGlaive` | `DA_IG_ComboData` |

> **操作提示：** Row Name 不支持 FGameplayTag——直接用纯字符串 `IG`。`ComboDataAsset` 字段点击下拉选取刚创建的 `DA_IG_ComboData`。

### 4.6 DT_WeaponResourceConfig（武器→资源组件映射）

**创建步骤：**

```
Content/Weapons/InsectGlaive/Data/ 右键 → Miscellaneous → Data Table
  → Row Structure 搜索 "FWeaponResourceConfigRow" → 选中 → 命名 "DT_WeaponResourceConfig"
```

打开后点击 `+ Add`，仅需 **1 行**：

| Row Name | WeaponTypeTag | ResourceComponentClass | ResourceWidgetClass |
|----------|---------------|----------------------|---------------------|
| `IG` | `Weapon.InsectGlaive` | `URes_InsectGlaive` | *留空*（Widget 蓝图创建后再补） |

> **操作提示：** `ResourceComponentClass` 点击下拉 → 搜索 `Res_InsectGlaive` → 选中 C++ 类。`ResourceWidgetClass` 暂时留空——等 `WBP_IG_ResourcePanel` 创建后再回来补上。

---

## 五、编辑器资产创建——GameplayEffect 蓝图

全部在 `Content/GameplayEffects/` 下创建。

### 5.0 GE_InitStats（★ H-4 修复——角色初始属性）

| 属性 | 值 |
|------|------|
| DurationPolicy | Infinite |
| Modifiers | Health=100(Override), MaxHealth=100(Override), Stamina=100(Override), MaxStamina=100(Override), StaminaRegenRate=1.0(Override), StaminaDeductionRate=1.0(Override), StaminaConsumptionRate=1.0(Override) |
| GameplayCueTags | —（无视觉反馈） |

> **用途：** 在 `CoreAttributeEffects` 中应用，确保所有属性有明确的初始值，不依赖 C++ 构造函数默认值。Infinite 持续——角色死亡前始终生效。

### 5.1 GE_Damage（伤害 GE——通用）

| 属性 | 值 |
|------|------|
| DurationPolicy | Instant |
| Modifiers | — **留空**——所有数值由 `UExecCalc_Damage` 通过 `OutExecutionOutput` 写入 |
| ExecCalc | `UExecCalc_Damage` |
| GameplayCueTags | 由 `MakeDamageSpec` 动态注入——蓝图留空 |

### 5.1b GE_KinsectDamage（★ I-7 修复——猎虫伤害 GE）

| 属性 | 值 |
|------|------|
| DurationPolicy | Instant |
| Modifiers | — 留空（复用同一 ExecCalc） |
| ExecCalc | `UExecCalc_Damage` |
| GameplayCueTags | 由 `ApplyKinsectDamage` 动态注入 `GameplayCue.Hit.Kinsect`——蓝图留空 |

> **说明：** 猎虫伤害复用武器的 `UExecCalc_Damage`——通过 `SetByCaller` 的 `Damage.AttackPower` 覆写（传入猎虫攻击力）区分来源。伤害公式统一：`AttackPower × MotionValue × HitzoneDefense`。

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

### 6.3 GA_SendKinsect（送虫——通过连招表匹配，非直接ASC激活）

**父类：** `UMHGZGameplayAbility`（不受击碰撞——不需要继承 `UMHGZInsectGlaiveAbility`）

| 成员 | 值 |
|------|------|
| StaminaCost | 0 |

> **激活方式：** 由协调器的 `HandleWeaponInput(Input.Weapon.Y)` → 匹配连招表中 `bMatchAnyState=true` 的送虫节点 → `RequiredTags={Combat.State.Aiming}` 满足时激活。**不在蓝图设 `InputTag` 或 `ActivationRequiredTags`**——这些由连招表的 `FComboNode` 统一管理。

**Event Graph：** `ActivateAbility` → 读取 `UMHGZAimComponent` 当前相机朝向 → 获取 `URes_InsectGlaive`→ `DeployKinsect()` → ★ `Kinsect->SetDamageParams(Piercing, 1.0, 0.12s, AlwaysOverwrite)` → `EndAbility`。

> **★ H-3 修复：** `GA_SendKinsect` 连招节点 `bMatchAnyState=true`——协调器激活此 GA 后**不改变 CurrentState**（`NextState` 字段对 `bMatchAnyState` 节点无效）。因此即使此 GA 继承 `UMHGZGameplayAbility`（不含 `OnAttackFinished` 回调），协调器状态不受影响——仍保持在 `Idle`，后续攻击可正常匹配。

### 6.4 GA_RecallKinsect（召回——通过连招表匹配）

**父类：** `UMHGZGameplayAbility`

| 成员 | 值 |
|------|------|
| StaminaCost | 0 |

> **激活方式：** 由协调器的 `HandleWeaponInput(Input.Weapon.B)` → 匹配连招表中 `bMatchAnyState=true` 的召回节点 → `RequiredTags={WeaponResource.IG.Kinsect.Active}` 满足时激活。同样不在蓝图设 `InputTag`。

**Event Graph：** `ActivateAbility` → 获取 `URes_InsectGlaive`→ `RecallKinsect()` → `EndAbility`。

> **★ H-3 修复（同上）：** `bMatchAnyState=true`——协调器不改变 CurrentState。召回完成后猎虫异步返回——`AttachToPlayer` 回调中移除 `Kinsect.Active` Tag，此后 B 键将匹配攻击节点（而非召回节点）。

### 6.5 GA_Unsheathe（拔刀——通过连招表匹配）

**父类：** `UMHGZGameplayAbility`

| 成员 | 值 |
|------|------|
| StaminaCost | 0 |

> **★ H-1 修复：** `FComboNode::GrantedTags` 仅在 `OnAttackHit()` 回调中生效——拔刀无攻击命中概念。因此**不在连招表中设 GrantedTags**，改为在 `GA_Unsheathe::ActivateAbility` 中直接操作 ASC Tag：
> ```cpp
> ASC->AddLooseGameplayTag(Combat.State.Unsheathed);
> ASC->RemoveLooseGameplayTag(Combat.State.Sheathed);
> ```
> 这两个 Tag 声明为 `Categories="Combat.State"` 的互斥标签，GAS 自动处理互斥移除。

> **激活方式：** 由协调器匹配连招表中 `bMatchAnyState=true` 的拔刀节点 → `RequiredTags={Combat.State.Sheathed}` 满足时激活，`Priority=30` 最高。不在蓝图设 `InputTag` 或 `ActivationRequiredTags`。

**Event Graph：** `ActivateAbility` → `AddLooseGameplayTag(Unsheathed)` + `RemoveLooseGameplayTag(Sheathed)` → 播放拔刀 Montage（若有）→ `EndAbility`。

### 6.6 GA_DrawAndSendKinsect（收刀直飞——通过连招表匹配）

**父类：** `UMHGZGameplayAbility`

| 成员 | 值 |
|------|------|
| StaminaCost | 0 |

> **激活方式：** 由协调器匹配连招表中 `bMatchAnyState=true` 的收刀直飞节点 → `RequiredTags={Combat.State.Sheathed}` 满足时激活。不在蓝图设 `InputTag`。

**Event Graph：** `ActivateAbility` → 先调用 `GA_Unsheathe` 逻辑拔刀 → 获取 `URes_InsectGlaive`→ `DeployKinsect()`（内部自动走 `StartFlightAlongRay(PlayerForward, StraightFlightDistance)` + `SetDamageParams(SingleHit, 0.8, 0, FirstHitOnly)`）→ `EndAbility`。

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

### 8.1 补填 ResourceWidgetClass

创建 `WBP_IG_ResourcePanel` 后，回到 `DT_WeaponResourceConfig`（§4.6）把 `ResourceWidgetClass` 补填为 `WBP_IG_ResourcePanel`。

### 8.2 GameMode 配置

打开 `BP_Demo_GameMode`：
- `Default Pawn Class = BP_IG_Character`
- `HUD Class = AMHGZHUD`

打开 `BP_IG_Character` → Class Defaults：
- `Auto Possess Player = Player 0`

### 8.3 Input 配置

创建 InputAction 资产：
- `IA_Y`（轻攻击/送虫）
- `IA_B`（重攻击/召回）
- `IA_LT`（瞄准——由 `UMHGZAimComponent` 直接绑定，不走 ASC）
- `IA_RT`（收刀直飞）

创建 InputMappingContext `IMC_IG`：
- `IA_Y` → `Input.Weapon.Y`（Triggered + Completed）
- `IA_B` → `Input.Weapon.B`
- `IA_LT` → `Input.Modifier.Aiming`（Triggered + Completed）
- `IA_RT` → `Input.Weapon.RT`（Triggered + Completed）

打开 `BP_IG_Character` → `UMHGZAbilitySystemComponent` → `InputBindings` 数组：
- [0] InputAction=`IA_Y`, AbilityTag=`Input.Weapon.Y`
- [1] InputAction=`IA_B`, AbilityTag=`Input.Weapon.B`
- [2] InputAction=`IA_RT`, AbilityTag=`Input.Weapon.RT`

> **★ I-3 修复：** RT 键使用 `Input.Weapon.RT`（而非 `Input.Modifier.Sheathed`）——确保匹配 `Input.Weapon.*` 命名空间，经 `OnInputActionTriggered` 正确路由到连招协调器。`Input.Modifier.*` 命名空间仅用于非武器输入（如 LT 瞄准的修饰键状态）。
>
> **注意：** `IA_LT` 不在 ASC 的 `InputBindings` 中——瞄准是输入状态而非招式。`UMHGZAimComponent::BeginPlay` 直接向 EnhancedInput Subsystem 绑定 `IA_LT` 的 Triggered/Completed，自行调用 `ASC->AddLooseGameplayTag(Combat.State.Aiming)` / `RemoveLooseGameplayTag`。不走 GAS 的 `OnInputActionTriggered` 分叉路由。

### 8.4 初始装备配置

**前置条件：** `EquipItem` 必须在 `PossessedBy`（`InitAbilityActorInfo` + `InitializeAbilitySystem`）之后调用，否则协调器激活时 ASC 未就绪。

在 `BP_IG_Character` 的 `Event BeginPlay` 中：

```
1. 创建 EquipmentInstance:
     Instance = NewObject<UMHGZEquipmentInstance>(self)
     Instance.Definition = DA_IG_Weapon

2. 获取 EquipmentComponent（在 PlayerState 上）:
     PS = GetPlayerState<AMHGZPlayerState>
     EqComp = PS->GetComponentByClass<UMHGZEquipmentComponent>

3. 调用 EquipItem:
     EqComp->EquipItem(Equipment.Slot.Weapon, Instance)
```

> **调用后自动触发（C++ 内部链条）：**
> ```
> EquipItem
>   → OnEquipmentChangedInternal
>     → 清空旧装备 GE + 销毁旧 ResourceComponent
>     → ApplyItemEffects
>       → ① 查 DT_WeaponResourceConfig → 创建 URes_InsectGlaive
>       → ② 查 DT_WeaponComboConfig → 加载 DA_IG_ComboData
>       → ③ 激活 GA_WeaponComboCoordinator → InjectComboData
>       → ④ GrantWeaponAbilities（从 ComboTable 收集）
>     → OnEquipmentChanged.Broadcast（UI 刷新）
> ```
>
> 此后按 Y = 协调器开始监听 `Input.Weapon.Y` → 匹配连招表 → 激活对应 GA。

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
| 8 | 木桩受击 | 伤害数字浮空（木桩无 AttributeSet，仅显示伤害数字） | `monster-system.md` |
| 9 | 三灯萃取完毕 | 三个灯逐一亮起 → 自动三灯齐聚 → UI 三灯合一光环 | `insect-glaive.md §十一·6` |
| 10 | 三灯后按 Y | 攻击激活 → `PlaySound2D(TripleUpSwingSound)` | `insect-glaive.md §十一·11` |

---

## 十、Demo 简化策略

| 复杂功能 | Demo 做法 |
|------|------|
| 连招表多招式 | 只需 1 招 Idle→Slash（+ 红灯版复用） |
| 红灯版招式 | 复用同一 GA 蓝图——先验证 Tag 分流 |
| 消耗灯特殊技 | 不做——只验证萃取+三灯+红灯连招 |
| 装备词条 | 不做——固定白板铁虫棍 |
| GameplayCue 粒子 | 引擎自带粒子临时替代 |
| 猎虫动画 | 不做——纯 Mesh 飞行，无翅膀扇动 |
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
