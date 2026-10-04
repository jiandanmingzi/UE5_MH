// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "ActionSystem/MHGZDescendingThrustAbility.h"
#include "AnimNotify_DescendingThrustEvent.generated.h"

/** Resident Montage event used only by the Descending Thrust GA. */
UCLASS(meta = (DisplayName = "Descending Thrust Event"))
class MHGZ_API UAnimNotify_DescendingThrustEvent : public UAnimNotify
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Descending Thrust")
	EDescendingThrustMontageEvent Event = EDescendingThrustMontageEvent::EnterFallingLoop;

	virtual void Notify(USkeletalMeshComponent* MeshComp,
		UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;

#if WITH_EDITOR
	virtual FString GetNotifyName_Implementation() const override;
#endif
};
