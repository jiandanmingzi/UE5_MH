// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemGlobals.h"
#include "GameplayEffectTypes.h"
#include "GameplayTagContainer.h"
#include "MHGZGameplayEffectContext.generated.h"

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

	UPROPERTY(BlueprintReadWrite, Category = "MHGZ|Damage")
	FGuid AttackInstanceID;

	UPROPERTY(BlueprintReadWrite, Category = "MHGZ|Damage")
	FGameplayTag SourceActionTag;

	UPROPERTY(BlueprintReadWrite, Category = "MHGZ|Damage")
	FGameplayTag HitzoneTag;

	UPROPERTY(BlueprintReadWrite, Category = "MHGZ|Damage")
	EMHGZDamageSourceType DamageSourceType = EMHGZDamageSourceType::Weapon;

	UPROPERTY(BlueprintReadWrite, Category = "MHGZ|Damage")
	FGameplayTag HitCueTag;

	UPROPERTY(BlueprintReadWrite, Category = "MHGZ|Damage")
	FGameplayTag ElementCueTag;

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
