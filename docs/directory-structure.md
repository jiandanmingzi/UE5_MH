# 目录结构

```
Source/MHGZ/
├── MHGZPlayerState.h/cpp              (PlayerState——ASC + 核心组件宿主)
├── MHGZCharacter.h/cpp
├── MHGZPlayerController.h/cpp
│
├── Inventory/
│   ├── MHGZItemTypes.h                   (FEquipmentSocket, FEntryReference, FEntryModifier, FEntryDefinition)
│   ├── MHGZItemDefinition.h/cpp
│   ├── MHGZEquipmentDefinition.h/cpp
│   ├── MHGZWeaponDefinition.h/cpp
│   ├── MHGZArmorDefinition.h/cpp
│   ├── MHGZAccessoryDefinition.h/cpp
│   ├── MHGZConsumableDefinition.h/cpp
│   ├── MHGZItemInstance.h/cpp
│   └── MHGZEquipmentInstance.h/cpp
│
├── AttributeSystem/
│   ├── MHGZAttributeSet.h/cpp
│   ├── MHGZEquipmentComponent.h/cpp
│   ├── MHGZDataManager.h/cpp           (GameInstanceSubsystem, 集中持有全局DataTable/CurveTable)
│   ├── MHGZWeaponResourceComponent.h/cpp (武器资源基类: GetCurrentValue/Consume/Restore)
│   ├── Res_LongSword.h/cpp             (太刀: 气刃槽色阶+衰减)
│   ├── Res_InsectGlaive.h/cpp          (虫棍: 三灯Timer+猎虫耐力)
│   ├── Res_ChargeBlade.h/cpp           (盾斧: 瓶计数+红盾Timer)
│   ├── Res_SwitchAxe.h/cpp             (斩斧: 充能槽)
│   ├── DT_WeaponResourceConfig.uasset
│   ├── DT_WeaponComboConfig.uasset       (武器种类→连招表桥接，一行映射)
│   ├── DT_WeaponDodgeConfig.uasset       (武器种类→翻滚参数)
│   ├── CT_EntryMagnitudes.uasset
│   ├── GE_EntryStat.uasset
│   └── UExecCalc_EntryStat.h/cpp
│
├── ActionSystem/
│   ├── MHGZAbilitySystemComponent.h/cpp
│   ├── MHGZGameplayAbility.h/cpp
│   ├── MHGZAttackAbility.h/cpp           (碰撞检测+伤害GE基类)
│   ├── MHGZLongSwordAbility.h/cpp       (太刀基类: 气刃槽判定+招内派生)
│   ├── MHGZInsectGlaiveAbility.h/cpp    (虫棍基类: 三灯+猎虫耐力)
│   ├── MHGZChargeBladeAbility.h/cpp     (盾斧基类: 瓶计数+盾充能)
│   ├── MHGZSwitchAxeAbility.h/cpp       (斩斧基类: 充能槽)
│   ├── MHGZDodgeAbility.h/cpp            (翻滚/闪避 Ability)
│   ├── MHGZWeaponComboData.h/cpp         (连招表 DataAsset + FComboNode)
│   ├── MHGZComboCoordinatorAbility.h/cpp (连招协调器)
│   ├── AnimNotify_SwingSound.h/cpp       (挥刀风声：读Character缓存→PlaySoundAtLocation)
│   ├── AnimNotifyState_AttackCollision.h/cpp
│   ├── AnimNotifyState_MonsterAttackCollision.h/cpp
│   ├── AnimNotifyState_ComboWindow.h/cpp
│   ├── AnimNotifyState_DodgeWindow.h/cpp
│   ├── AnimNotifyState_DodgeAcceptWindow.h/cpp
│   ├── AnimNotifyState_ForesightJudge.h/cpp
│   ├── AnimNotifyState_PoiseWindow.h/cpp
│   ├── MHGZInputComponent.h/cpp
│   ├── MHGZEdgeVaultComponent.h/cpp   (边缘跳越检测组件)
│   └── MHGZEdgeVaultAbility.h/cpp     (边缘跳越 Ability)
│
├── Storage/
│   ├── MHGZStorageSlot.h
│   ├── MHGZBackpackComponent.h/cpp
│   └── MHGZWarehouseComponent.h/cpp
│
├── UseSystem/
│   ├── MHGZQuickBarSlot.h
│   ├── MHGZUseAction.h/cpp
│   ├── MHGZSpecialAction.h/cpp
│   ├── UseAction_Heal.h/cpp
│   ├── UseAction_ThrowProjectile.h/cpp
│   ├── UseAction_ApplyBuff.h/cpp
│   ├── UseAction_PlaceTrap.h/cpp
│   ├── SpecialAction_Scan.h/cpp
│   ├── SpecialAction_GrapplingHook.h/cpp
│   ├── SpecialAction_PhotoMode.h/cpp
│   ├── SpecialAction_PlayInstrument.h/cpp
│   └── MHGZQuickBarComponent.h/cpp
│
├── GameplayCue/                              ← 新增：GC 基础设施
│   ├── MHGZGameplayCueManager.h/cpp           (自定义 GC 管理器：对象池、物理表面路由)
│   ├── MHGZCue_HitBase.h/cpp                  (命中特效基类：Burst——裁剪/限流/衰减/震屏)
│   ├── MHGZCue_BuffBase.h/cpp                 (Buff 光环基类：Latent——循环粒子/层数绑定)
│   └── MHGZDamageNumberPool.h/cpp             (伤害数字 Widget 对象池：WorldSubsystem)
│
├── Monster/                                   ← 新增：怪物系统
│   ├── MHGZMonsterBase.h/cpp                  (怪物基类：ASC + Hitzone集合 + Config)
│   ├── MHGZTrainingDummy.h/cpp                (木桩子类)
│   ├── MHGZDummyConfig.h/cpp                  (DataAsset 配置)
│   └── MHGZMonsterHitzoneComponent.h/cpp      (部位碰撞体)

Content/
├── Inventory/Definitions/
│   ├── Weapons/          (WeaponDefinition .uasset)
│   ├── Armors/           (ArmorDefinition .uasset)
│   ├── Accessories/      (AccessoryDefinition .uasset)
│   └── Consumables/      (ConsumableDefinition .uasset)
├── Inventory/
│   └── DT_EntryCatalog.uasset
├── Storage/UI/
│   ├── WBP_BackpackPanel.uasset
│   ├── WBP_WarehousePanel.uasset
│   └── WBP_StorageSlot.uasset
├── Equipment/UI/
│   └── WBP_EquipmentStatus.uasset
└── UseSystem/
    ├── Actions/
    └── UI/
        └── WBP_QuickBar.uasset

Content/GameplayCues/
├── Hit/
│   ├── GC_Hit_Slash.uasset
│   ├── GC_Hit_Blunt.uasset
│   ├── GC_Hit_Fire.uasset
│   ├── GC_Hit_Ice.uasset
│   ├── GC_Hit_Thunder.uasset
│   ├── GC_Hit_Dragon.uasset
│   ├── GC_Hit_Crit.uasset
│   ├── GC_Hit_Block.uasset
│   └── GC_Hit_DamageNumber.uasset
├── Buff/
│   ├── GC_Buff_AttackUp.uasset
│   ├── GC_Buff_DefenseUp.uasset
│   ├── GC_Buff_SpeedUp.uasset
│   ├── GC_Buff_HealOverTime.uasset
│   └── GC_Buff_ElementResist.uasset
├── Character/
│   ├── GC_Character_Death.uasset
│   ├── GC_Character_Dodge.uasset
│   └── GC_Character_Heal.uasset
├── Monster/
│   ├── GC_Monster_Roar.uasset
│   └── GC_Monster_Death.uasset
└── UI/
    └── WBP_DamageNumber.uasset
```
