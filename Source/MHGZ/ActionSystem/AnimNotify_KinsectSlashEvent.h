// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "ActionSystem/MHGZKinsectSlashAbility.h"
#include "AnimNotify_KinsectSlashEvent.generated.h"

/** Exact point event used by AM_IG_KinsectSlash's 186/187/188/189 sections. */
UCLASS(meta = (DisplayName = "Kinsect Slash Event"))
class MHGZ_API UAnimNotify_KinsectSlashEvent : public UAnimNotify
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect Slash")
	EKinsectSlashMontageEvent Event = EKinsectSlashMontageEvent::AimLock;

	virtual void Notify(USkeletalMeshComponent* MeshComp,
		UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;

#if WITH_EDITOR
	virtual FString GetNotifyName_Implementation() const override;
#endif
};
