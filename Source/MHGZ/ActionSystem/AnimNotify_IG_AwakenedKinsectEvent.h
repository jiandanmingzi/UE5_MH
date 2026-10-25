// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "ActionSystem/MHGZAwakenedKinsectAttackAbility.h"
#include "AnimNotify_IG_AwakenedKinsectEvent.generated.h"

/** Exact Montage-instance event boundary for the Awakened Kinsect action. */
UCLASS(BlueprintType, meta = (DisplayName = "IG Awakened Kinsect Event"))
class MHGZ_API UAnimNotify_IG_AwakenedKinsectEvent : public UAnimNotify
{
	GENERATED_BODY()

public:
	/** 本通知向觉虫击能力发送的事件种类。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Awakened Kinsect")
	EAwakenedKinsectAttackEvent Event = EAwakenedKinsectAttackEvent::KinsectLaunch;

	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;

#if WITH_EDITOR
	virtual FString GetNotifyName_Implementation() const override;
#endif
};
