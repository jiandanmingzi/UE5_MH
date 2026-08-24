// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_DrawCommit.generated.h"

/** Exact Montage-instance boundary where a draw action becomes Unsheathed. */
UCLASS(BlueprintType, meta = (DisplayName = "Draw Commit"))
class MHGZ_API UAnimNotify_DrawCommit : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;
};
