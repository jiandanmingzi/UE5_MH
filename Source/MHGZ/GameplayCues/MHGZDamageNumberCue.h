// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayCueNotify_Static.h"
#include "MHGZDamageNumberCue.generated.h"

class AMHGZDamageNumberActor;

/** 将Router传入的已结算伤害值转为局部表现；Cue不执行伤害。 */
UCLASS(Blueprintable)
class MHGZ_API UMHGZDamageNumberCue : public UGameplayCueNotify_Static
{
	GENERATED_BODY()

public:
	virtual bool OnExecute_Implementation(AActor* MyTarget,
		const FGameplayCueParameters& Parameters) const override;

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MHGZ|Feedback")
	TSubclassOf<AMHGZDamageNumberActor> DamageNumberActorClass;
};
