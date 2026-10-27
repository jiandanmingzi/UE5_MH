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
	/** 启用后召回任意状态的猎虫；关闭时，普通攻击仅结束自身的协同。已在手臂上或回归途中时不重复离手。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect", meta = (DisplayName = "召回任意状态猎虫"))
	bool bRecallFromAnyState = false;

	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;
};
