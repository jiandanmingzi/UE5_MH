// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MHGZGameplayAbility.h"
#include "MHGZInsectGlaiveKinsectAbilities.generated.h"

class URes_InsectGlaive;
struct FKinsectFlightRequest;

/** 持刀地面 LT+Y：臂上沿瞄准方向送虫；已部署时飞向冻结目标点。 */
UCLASS(BlueprintType, Blueprintable)
class MHGZ_API UMHGZSendKinsectAbility : public UMHGZGameplayAbility
{
	GENERATED_BODY()

public:
	UMHGZSendKinsectAbility();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

protected:
	virtual bool ValidateActionDependencies() const override;
	bool BuildRequest(FKinsectFlightRequest& OutRequest) const;
	URes_InsectGlaive* GetIGResource() const;
};

/** 持刀地面 LT+B：主动召回并在到达时原子交付 Pending 精华。 */
UCLASS(BlueprintType, Blueprintable)
class MHGZ_API UMHGZRecallKinsectAbility : public UMHGZGameplayAbility
{
	GENERATED_BODY()

public:
	UMHGZRecallKinsectAbility();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

protected:
	virtual bool ValidateActionDependencies() const override;
	URes_InsectGlaive* GetIGResource() const;
};

/** 收刀地面 RT：Commit 后沿冻结的角色 Forward 送虫并切换到持刀态。 */
UCLASS(BlueprintType, Blueprintable)
class MHGZ_API UMHGZDrawAndSendKinsectAbility : public UMHGZGameplayAbility
{
	GENERATED_BODY()

public:
	UMHGZDrawAndSendKinsectAbility();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

protected:
	virtual bool ValidateActionDependencies() const override;
	bool BuildRequest(FKinsectFlightRequest& OutRequest) const;
	URes_InsectGlaive* GetIGResource() const;
};

/** 持刀地面 LT+RT：发射虫印弹；Resource 维护唯一虫印所有权。 */
UCLASS(BlueprintType, Blueprintable)
class MHGZ_API UMHGZMarkKinsectTargetAbility : public UMHGZGameplayAbility
{
	GENERATED_BODY()

public:
	UMHGZMarkKinsectTargetAbility();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

protected:
	virtual bool ValidateActionDependencies() const override;
	URes_InsectGlaive* GetIGResource() const;
};
