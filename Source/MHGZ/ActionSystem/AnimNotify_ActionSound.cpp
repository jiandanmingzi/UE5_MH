// Copyright MHGZ Project. All Rights Reserved.
#include "AnimNotify_ActionSound.h"
#include "MHGZAnimNotifyActionResolver.h"
#include "MHGZGameplayAbility.h"
#include "MHGZ.h"
#include "HAL/IConsoleManager.h"
#include "Sound/SoundBase.h"

UAnimNotify_ActionSound::UAnimNotify_ActionSound()
{
	Source = EMHGZAnimationSoundSource::GroundContact;
	AttachSocket = TEXT("Root");
}

bool UAnimNotify_ActionSound::IsCurrentPresentation(USkeletalMeshComponent* Mesh, int32 InstanceID) const
{
	AMHGZCharacter* Character = Mesh ? Cast<AMHGZCharacter>(Mesh->GetOwner()) : nullptr;
	UMHGZWeaponRuntimeHostComponent* Host = Character ? Character->GetWeaponRuntimeHost() : nullptr;
	if (!Host || Mesh != Character->GetMesh()) return false;
	if (Host->IsCurrentAerialLandingMontageInstance(Mesh, InstanceID)) return true;
	FWeaponActionToken Action;
	if (!Host->ResolveMontage(Mesh, InstanceID, Action) || !Host->IsTokenCurrent(Action.RuntimeToken)) return false;
	const UMHGZGameplayAbility* Ability = Cast<UMHGZGameplayAbility>(Action.AbilityInstance.Get());
	return Ability && Ability->IsActive() && Ability->IsActionActivationCommitted()
		&& Ability->GetActionToken() == Action;
}

void UAnimNotify_ActionSound::Notify(USkeletalMeshComponent* Mesh, UAnimSequenceBase*,
	const FAnimNotifyEventReference& EventReference)
{
	const auto* Context = EventReference.GetContextData<UE::Anim::FAnimNotifyMontageInstanceContext>();
	if (Context && IsCurrentPresentation(Mesh, Context->MontageInstanceID))
	{
		const auto* Character = Cast<AMHGZCharacter>(Mesh->GetOwner());
		PlayOneShot(Mesh, Character->GetWeaponRuntimeHost()->GetWeaponVisualComponent(), Sound);
	}
	else if (const IConsoleVariable* Debug = IConsoleManager::Get().FindConsoleVariable(TEXT("mhgz.Audio.SwingDebug"));
		Debug && Debug->GetInt() != 0)
	{
		UE_LOG(LogMHGZ, Display, TEXT("[AnimationAudio] key=%s rejected: no current exact action/Host presentation"), *EventTag.ToString());
	}
}

FString UAnimNotify_ActionSound::GetNotifyName_Implementation() const
{
	return FString::Printf(TEXT("Action Sound %s"), Sound ? *Sound->GetName() : TEXT("Missing Sound"));
}
