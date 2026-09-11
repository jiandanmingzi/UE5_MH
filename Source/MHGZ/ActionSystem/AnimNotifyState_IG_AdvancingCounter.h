// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "AnimNotifyState_IG_AdvancingCounter.generated.h"

/** Exact Montage-instance counter window for GA_IG_TuJinHuiXuanZhan. */
UCLASS(BlueprintType, meta = (DisplayName = "IG Advancing Counter Window"))
class MHGZ_API UAnimNotifyState_IG_AdvancingCounter : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp,
		UAnimSequenceBase* Animation, float TotalDuration,
		const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp,
		UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;
};
