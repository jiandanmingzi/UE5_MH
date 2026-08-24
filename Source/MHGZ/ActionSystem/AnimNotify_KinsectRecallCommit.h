// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_KinsectRecallCommit.generated.h"

/** Exact Montage-instance boundary where an already-deployed kinsect begins returning. */
UCLASS(BlueprintType, meta = (DisplayName = "Kinsect Recall Commit"))
class MHGZ_API UAnimNotify_KinsectRecallCommit : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;
};
