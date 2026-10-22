// Copyright MHGZ Project. All Rights Reserved.
#pragma once
#include "AnimNotify_SwingSound.h"
#include "AnimNotify_ActionSound.generated.h"

/** Cosmetic point recipe for committed actions or the exact Host landing instance. */
UCLASS(meta=(DisplayName="Action Sound（动作/地面声音）"))
class MHGZ_API UAnimNotify_ActionSound : public UAnimNotify_SwingSound
{
	GENERATED_BODY()
public:
	UAnimNotify_ActionSound();
	bool IsCurrentPresentation(USkeletalMeshComponent* Mesh, int32 InstanceID) const;
	virtual void Notify(USkeletalMeshComponent* Mesh, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override;
};
