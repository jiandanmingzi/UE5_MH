// Copyright MHGZ Project. All Rights Reserved.
#pragma once

#include "GameplayTagContainer.h"

/** 共享标签的惰性查询入口；由既有配置注册，首次使用时查询，不提前初始化。 */
namespace MHGZGameplayTags
{
/** 角色在地面。 */
MHGZ_API const FGameplayTag& Grounded();
/** 角色在空中。 */
MHGZ_API const FGameplayTag& Aerial();
/** 空中动作已放行后续操作。 */
MHGZ_API const FGameplayTag& AerialFalling();
/** 本次滞空禁止再次发动空中攻击。 */
MHGZ_API const FGameplayTag& AerialCantAttack();
/** 当前武器已拔出。 */
MHGZ_API const FGameplayTag& Unsheathed();
/** 当前武器已收起。 */
MHGZ_API const FGameplayTag& Sheathed();
/** 角色死亡。 */
MHGZ_API const FGameplayTag& Dead();
/** 角色处于受击硬直。 */
MHGZ_API const FGameplayTag& Hitstun();
/** 角色处于倒地状态。 */
MHGZ_API const FGameplayTag& Knockdown();
/** 虫棍白色精华。 */
MHGZ_API const FGameplayTag& IGExtractWhite();
/** 虫棍红色精华。 */
MHGZ_API const FGameplayTag& IGExtractRed();
/** 虫棍橙色精华。 */
MHGZ_API const FGameplayTag& IGExtractOrange();
/** 虫棍三灯消耗请求。 */
MHGZ_API const FGameplayTag& IGTripleUpCost();
/** 武器B键的解析输入。 */
MHGZ_API const FGameplayTag& WeaponBInput();
/** 空回留下的空中位移来源。 */
MHGZ_API const FGameplayTag& IGAirDodgeSource();
/** 后撑杆跳留下的空中位移来源。 */
MHGZ_API const FGameplayTag& IGBackVaultSource();
/** 突进回旋斩舞踏留下的空中位移来源。 */
MHGZ_API const FGameplayTag& IGAdvancingCounterSource();
/** 强化操虫穿刺留下的空中位移来源。 */
MHGZ_API const FGameplayTag& IGKinsectPiercerSource();
/** 操虫斩的一般空中位移来源。 */
MHGZ_API const FGameplayTag& IGKinsectSlashSource();
/** 操虫斩命中后的空中位移来源。 */
MHGZ_API const FGameplayTag& IGKinsectSlashHitSource();
/** 操虫斩未命中后的空中位移来源。 */
MHGZ_API const FGameplayTag& IGKinsectSlashMissSource();
/** 伤害GE使用的动作值SetByCaller键。 */
MHGZ_API const FGameplayTag& DamageMotionValue();
}
