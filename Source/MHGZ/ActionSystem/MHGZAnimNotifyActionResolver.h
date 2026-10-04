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
	inline bool HasMontageInstanceContext(
		const FAnimNotifyEventReference& EventReference)
	{
		return EventReference.GetContextData<
			UE::Anim::FAnimNotifyMontageInstanceContext>() != nullptr;
	}

	inline bool ResolveAction(USkeletalMeshComponent* MeshComp,
		const FAnimNotifyEventReference& EventReference, FWeaponActionToken& OutAction,
		UAnimSequenceBase* Animation = nullptr,
		int32* OutMontageInstanceID = nullptr,
		const bool bRequireMontageInstanceContext = false)
	{
		OutAction = FWeaponActionToken();
		if (OutMontageInstanceID)
		{
			*OutMontageInstanceID = INDEX_NONE;
		}
		if (!MeshComp)
		{
			return false;
		}

		const UE::Anim::FAnimNotifyMontageInstanceContext* MontageContext =
			EventReference.GetContextData<UE::Anim::FAnimNotifyMontageInstanceContext>();
		if (!MontageContext && bRequireMontageInstanceContext)
		{
			return false;
		}
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
		if (MontageContext)
		{
			// A supplied context is authoritative. Never recover a stale or
			// unregistered instance by switching to another active instance of the
			// same Montage asset; that would let an old Notify drive a new Action.
			if (!Host->ResolveMontage(MeshComp, MontageContext->MontageInstanceID,
				OutAction))
			{
				return false;
			}
			if (OutMontageInstanceID)
			{
				*OutMontageInstanceID = MontageContext->MontageInstanceID;
			}
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
		if (!ActiveInstance
			|| !Host->ResolveMontage(MeshComp, ActiveInstance->GetInstanceID(), OutAction))
		{
			return false;
		}
		if (OutMontageInstanceID)
		{
			*OutMontageInstanceID = ActiveInstance->GetInstanceID();
		}
		return true;
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
