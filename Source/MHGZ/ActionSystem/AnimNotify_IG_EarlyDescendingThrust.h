// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_IG_EarlyDescendingThrust.generated.h"

/**
 * Opens only the AirDodge -> DescendingThrust early-cancel route.
 *
 * This is a point notify. The Coordinator owns the ActionToken-scoped
 * permission until the ordinary aerial handoff or source-action teardown.
 */
UCLASS(meta = (DisplayName = "IG Early Descending Thrust"))
class MHGZ_API UAnimNotify_IG_EarlyDescendingThrust : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp,
		UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;

#if WITH_EDITOR
	virtual FString GetNotifyName_Implementation() const override;
#endif
};
