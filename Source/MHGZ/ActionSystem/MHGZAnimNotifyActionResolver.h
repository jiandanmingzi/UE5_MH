// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "Animation/ActiveMontageInstanceScope.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimNotifyQueue.h"
#include "Animation/AnimTypes.h"
#include "Components/SkeletalMeshComponent.h"
#include "MHGZCharacter.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"

namespace MHGZ::AnimNotify
{
	inline bool ResolveAction(USkeletalMeshComponent* MeshComp,
		const FAnimNotifyEventReference& EventReference, FWeaponActionToken& OutAction,
		UAnimSequenceBase* Animation = nullptr)
	{
		OutAction = FWeaponActionToken();
		if (!MeshComp)
		{
			return false;
		}

		const UE::Anim::FAnimNotifyMontageInstanceContext* MontageContext =
			EventReference.GetContextData<UE::Anim::FAnimNotifyMontageInstanceContext>();
		const AMHGZCharacter* Character = Cast<AMHGZCharacter>(MeshComp->GetOwner());
		UMHGZWeaponRuntimeHostComponent* Host = Character
			? Character->GetWeaponRuntimeHost() : nullptr;
		if (!Host)
		{
			return false;
		}

		// The normal path is exact Mesh + MontageInstanceID supplied by UE's
		// notify context. Some montage notify delivery paths omit that context
		// (notably during layered-slot playback), despite still passing the
		// source montage as Animation. Fall back to the active instance of that
		// exact montage, then keep the same Host-side Mesh + InstanceID lookup.
		if (MontageContext
			&& Host->ResolveMontage(MeshComp, MontageContext->MontageInstanceID, OutAction))
		{
			return true;
		}

		UAnimInstance* AnimInstance = MeshComp->GetAnimInstance();
		UAnimMontage* Montage = Cast<UAnimMontage>(Animation);
		if (!AnimInstance || !Montage)
		{
			return false;
		}
		FAnimMontageInstance* ActiveInstance =
			AnimInstance->GetActiveInstanceForMontage(Montage);
		return ActiveInstance
			&& Host->ResolveMontage(MeshComp, ActiveInstance->GetInstanceID(), OutAction);
	}

	inline FName MakeNotifyEventID(const FAnimNotifyEventReference& EventReference)
	{
		const FAnimNotifyEvent* Notify = EventReference.GetNotify();
		return Notify
			? FName(*FString::Printf(TEXT("%s:%d:%.6f"), *Notify->NotifyName.ToString(),
				Notify->TrackIndex, Notify->GetTriggerTime()))
			: NAME_None;
	}
}
