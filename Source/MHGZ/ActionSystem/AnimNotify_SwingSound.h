// Copyright MHGZ Project. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "GameplayTagContainer.h"
#include "AnimNotify_SwingSound.generated.h"

class USoundBase;
class USceneComponent;

UENUM(BlueprintType)
enum class EMHGZAnimationSoundSource : uint8
{
	WeaponSocket UMETA(DisplayName="武器挂点（棍尖/棍尾）"),
	CharacterSocket UMETA(DisplayName="角色挂点"),
	GroundContact UMETA(DisplayName="地面接触位置（不附着）")
};

/** Shared, immutable one-shot audio recipe. Runtime state belongs to the exact Action. */
UCLASS(meta=(DisplayName="Weapon Swing Sound"))
class MHGZ_API UAnimNotify_SwingSound : public UAnimNotify
{
	GENERATED_BODY()
public:
	/** 本次挥击的事件键；武器覆盖先查此键，再查GA的AudioIdentityTag。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Audio")
	FGameplayTag EventTag;

	/** 此声音事件的常驻默认音源；不按伤害段数量推断声音次数。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Audio")
	TObjectPtr<USoundBase> Sound;

	/** 可选兼容旧AttackSegments[Index].Damage.SwingSound；-1表示不用伤害段兜底。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Audio", meta=(ClampMin="-1"))
	int32 LegacyAttackSegmentIndex = INDEX_NONE;

	/** 声源职责；音源覆盖不会改变这个位置配方。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Audio")
	EMHGZAnimationSoundSource Source = EMHGZAnimationSoundSource::WeaponSocket;

	/** 武器声必须明确指定 IG_FrontTip / IG_RearTip；角色/地面声填写角色骨骼或挂点。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Audio")
	FName AttachSocket;

	/** 从指定角色挂点向下查询地表，仅用于声音表现，不改变碰撞或伤害。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Audio", meta=(ClampMin="1", EditCondition="Source == EMHGZAnimationSoundSource::GroundContact"))
	float GroundTraceDistance = 150.f;

	/** 在音源与其他混音设置基础上叠加的音量倍率。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Audio", meta=(ClampMin="0"))
	float VolumeMultiplier = 1.f;

	/** 声音播放音高倍率；同时影响该次音源的播放速度。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Audio", meta=(ClampMin="0.01"))
	float PitchMultiplier = 1.f;

	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override;

	bool ResolveEmission(USkeletalMeshComponent* Mesh, USceneComponent* Weapon,
		USceneComponent*& OutComponent, FVector& OutLocation, FName& OutReason,
		FVector* OutSurfaceNormal = nullptr) const;
	bool PlayOneShot(USkeletalMeshComponent* Mesh, USceneComponent* Weapon, USoundBase* ResolvedSound) const;
};
