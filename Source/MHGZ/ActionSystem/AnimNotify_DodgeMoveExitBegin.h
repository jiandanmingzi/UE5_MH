// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_DodgeMoveExitBegin.generated.h"

/**
 * Deprecated compatibility Notify. UMHGZDodgeAbility releases only this
 * Dodge's movement lock from its active-Montage SectionChanged callback.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Dodge Move Exit Begin"))
class MHGZ_API UAnimNotify_DodgeMoveExitBegin : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp,
		UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;
};
