// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemGlobals.h"
#include "GameplayEffectTypes.h"
#include "GameplayTagContainer.h"
#include "MHGZDamageTypes.h"
#include "MHGZGameplayEffectContext.generated.h"

class UCameraShakeBase;

UENUM(BlueprintType)
enum class EMHGZDamageSourceType : uint8
{
	Weapon,
	Kinsect,
	Powder,
	DummyAttack
};

/**
 * MHGZ 伤害链唯一上下文。
 *
 * 真实命中使用基类 HitResult 存储；新增字段只保存可复制的身份与标签，
 * 不保存组件裸指针。
 */
USTRUCT(BlueprintType)
struct MHGZ_API FMHGZGameplayEffectContext : public FGameplayEffectContext
{
	GENERATED_BODY()

	/** 本次攻击的唯一身份；目标侧按此身份限制重复结算。 */
	UPROPERTY(BlueprintReadWrite, Category = "MHGZ|Damage")
	FGuid AttackInstanceID;

	/** 攻击来源的语义标签，随伤害上下文传递给结算与反馈链。 */
	UPROPERTY(BlueprintReadWrite, Category = "MHGZ|Damage")
	FGameplayTag SourceActionTag;

	/** 命中部位的语义标签，用于查询肉质与反馈。 */
	UPROPERTY(BlueprintReadWrite, Category = "MHGZ|Damage")
	FGameplayTag HitzoneTag;

	/** false 时忽略物理肉质；属性肉质与硬直肉质仍独立参与。 */
	UPROPERTY(BlueprintReadWrite, Category = "MHGZ|Damage")
	bool bUseHitzoneDefense = true;

	/** 伤害来源种类，用于选择武器、猎虫或粉尘的结算规则。 */
	UPROPERTY(BlueprintReadWrite, Category = "MHGZ|Damage")
	EMHGZDamageSourceType DamageSourceType = EMHGZDamageSourceType::Weapon;

	/** 物理伤害种类，用于选择斩击、打击或弹类肉质。 */
	UPROPERTY(BlueprintReadWrite, Category = "MHGZ|Damage")
	EMHGZPhysicalDamageType PhysicalDamageType = EMHGZPhysicalDamageType::Sever;

	/** 仅 Weapon 来源消费；猎虫/粉尘不继承玩家武器的斩味与属性。 */
	UPROPERTY(BlueprintReadWrite, Category = "MHGZ|Damage")
	FMHGZWeaponDamageProfile WeaponDamageProfile;

	/** 成功结算物理命中后使用的反馈标签。 */
	UPROPERTY(BlueprintReadWrite, Category = "MHGZ|Damage")
	FGameplayTag HitCueTag;

	/** 成功结算属性伤害后使用的反馈标签。 */
	UPROPERTY(BlueprintReadWrite, Category = "MHGZ|Damage")
	FGameplayTag ElementCueTag;

	/** 硬直等级 Tag（Combat.Stagger.Light/Medium/Heavy）；AttributeSet 按它广播 HitStagger 事件。 */
	UPROPERTY(BlueprintReadWrite, Category = "MHGZ|Damage")
	FGameplayTag HitStaggerTag;

	/** 卡肉时长（秒）；Router 提交给 SourceActor 的 HitStopController。 */
	UPROPERTY(BlueprintReadWrite, Category = "MHGZ|Damage")
	float HitStopDuration = 0.f;

	/** 镜头震动类（可空）。 */
	UPROPERTY(BlueprintReadWrite, Category = "MHGZ|Damage")
	TSubclassOf<UCameraShakeBase> CameraShakeClass;

	/** 镜头震动强度倍率（0.0~1.0）。 */
	UPROPERTY(BlueprintReadWrite, Category = "MHGZ|Damage")
	float CameraShakeScale = 0.f;

	virtual UScriptStruct* GetScriptStruct() const override;
	virtual FMHGZGameplayEffectContext* Duplicate() const override;
	virtual bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess) override;

	static FMHGZGameplayEffectContext* ExtractEffectContext(FGameplayEffectContextHandle& Handle);
	static const FMHGZGameplayEffectContext* ExtractEffectContext(const FGameplayEffectContextHandle& Handle);
};

template<>
struct TStructOpsTypeTraits<FMHGZGameplayEffectContext> : public TStructOpsTypeTraitsBase2<FMHGZGameplayEffectContext>
{
	enum
	{
		WithNetSerializer = true,
		WithCopy = true
	};
};

/** 配置到 AbilitySystemGlobalsClassName，保证 MakeEffectContext 分配项目上下文。 */
UCLASS()
class MHGZ_API UMHGZAbilitySystemGlobals : public UAbilitySystemGlobals
{
	GENERATED_BODY()

public:
	virtual FGameplayEffectContext* AllocGameplayEffectContext() const override;
};
