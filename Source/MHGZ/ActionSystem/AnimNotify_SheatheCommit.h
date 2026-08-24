// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_SheatheCommit.generated.h"

/** Exact Montage-instance boundary where the weapon becomes authoritatively sheathed. */
UCLASS(BlueprintType, meta = (DisplayName = "Sheathe Commit"))
class MHGZ_API UAnimNotify_SheatheCommit : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;
};
