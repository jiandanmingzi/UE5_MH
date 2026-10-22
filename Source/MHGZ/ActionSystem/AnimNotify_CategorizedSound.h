// Copyright MHGZ Project. All Rights Reserved.
#pragma once

#include "AnimNotify_ActionSound.h"
#include "AnimNotify_CategorizedSound.generated.h"

/** Unified ground-dust presentation event. Audio is optional; VFX listeners are independent. */
UCLASS(meta=(DisplayName="扬尘事件（Ground Dust）"))
class MHGZ_API UAnimNotify_GroundDust : public UAnimNotify_ActionSound
{
	GENERATED_BODY()
public:
	virtual void Notify(USkeletalMeshComponent* Mesh, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override { return TEXT("扬尘事件"); }
	bool DispatchGroundDust(USkeletalMeshComponent* Mesh, int32 InstanceID) const;
};

/** Weapon/attack ground strike, not a hunter landing or damage-confirmed enemy hit. */
UCLASS(meta=(DisplayName="砸地事件（Ground Impact）"))
class MHGZ_API UAnimNotify_GroundImpact : public UAnimNotify_ActionSound
{
	GENERATED_BODY()
public:
	virtual void Notify(USkeletalMeshComponent* Mesh, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override { return TEXT("砸地事件"); }
	bool DispatchGroundImpact(USkeletalMeshComponent* Mesh, int32 InstanceID) const;
};

/** Legacy sound-only type retained for old packages; migrate author points to GroundDust. */
UCLASS(meta=(DisplayName="扬尘声（Dust Sound）"))
class MHGZ_API UAnimNotify_DustSound : public UAnimNotify_ActionSound
{
	GENERATED_BODY()
public:
	virtual FString GetNotifyName_Implementation() const override { return TEXT("扬尘声"); }
};

/** Landing presentation event; separate dust points keep their own authored timing. */
UCLASS(meta=(DisplayName="落地事件（Landing Event）"))
class MHGZ_API UAnimNotify_LandingEvent : public UAnimNotify_ActionSound
{
	GENERATED_BODY()
public:
	/** 同帧落地需要扬尘时开启；已有独立扬尘通知的蒙太奇默认关闭，避免重复。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Landing")
	bool bTriggerGroundDust = false;

	/** 可选扬尘声；为空也广播扬尘事件，共用本通知的地面挂点和声音倍率。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Landing", meta=(EditCondition="bTriggerGroundDust"))
	TObjectPtr<USoundBase> GroundDustSound;

	virtual void Notify(USkeletalMeshComponent* Mesh, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override { return TEXT("落地事件"); }
	bool DispatchLanding(USkeletalMeshComponent* Mesh, int32 InstanceID) const;
};

/** Legacy landing sound retained for old packages; use LandingEvent for new authoring. */
UCLASS(meta=(DisplayName="落地声（Landing Sound）"))
class MHGZ_API UAnimNotify_LandingSound : public UAnimNotify_ActionSound
{
	GENERATED_BODY()
public:
	virtual FString GetNotifyName_Implementation() const override { return TEXT("落地声"); }
};

/** Clothing movement, not weapon rattles. Leave Sound unset until identified. */
UCLASS(meta=(DisplayName="衣服晃动声（Cloth Sound）"))
class MHGZ_API UAnimNotify_ClothSound : public UAnimNotify_ActionSound
{
	GENERATED_BODY()
public:
	UAnimNotify_ClothSound();
	virtual FString GetNotifyName_Implementation() const override { return TEXT("衣服晃动声"); }
};
