// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_IG_AdvancingChargeStarted.generated.h"

/** Announces the exact first frame of the authored Charge Section. */
UCLASS(meta = (DisplayName = "IG Advancing Charge Started"))
class MHGZ_API UAnimNotify_IG_AdvancingChargeStarted : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;
};
