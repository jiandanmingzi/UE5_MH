// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_DodgeExitDecision.generated.h"

/**
 * Deprecated compatibility Notify. Dodge exit selection is owned by
 * UMHGZDodgeAbility's active-Montage SectionChanged callback.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Dodge Exit Decision"))
class MHGZ_API UAnimNotify_DodgeExitDecision : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp,
		UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;
};
