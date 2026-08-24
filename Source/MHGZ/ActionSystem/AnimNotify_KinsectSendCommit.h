// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_KinsectSendCommit.generated.h"

/** Exact Montage-instance boundary where a held kinsect begins its flight. */
UCLASS(BlueprintType, meta = (DisplayName = "Kinsect Send Commit"))
class MHGZ_API UAnimNotify_KinsectSendCommit : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;
};
