// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "Animation/ActiveMontageInstanceScope.h"
#include "Animation/AnimNotifyQueue.h"
#include "Animation/AnimTypes.h"
#include "Components/SkeletalMeshComponent.h"
#include "MHGZCharacter.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"

namespace MHGZ::AnimNotify
{
	inline bool ResolveAction(USkeletalMeshComponent* MeshComp,
		const FAnimNotifyEventReference& EventReference, FWeaponActionToken& OutAction)
	{
		if (!MeshComp) return false;
		const UE::Anim::FAnimNotifyMontageInstanceContext* MontageContext =
			EventReference.GetContextData<UE::Anim::FAnimNotifyMontageInstanceContext>();
		const AMHGZCharacter* Character = Cast<AMHGZCharacter>(MeshComp->GetOwner());
		UMHGZWeaponRuntimeHostComponent* Host = Character
			? Character->GetWeaponRuntimeHost() : nullptr;
		return MontageContext && Host
			&& Host->ResolveMontage(MeshComp, MontageContext->MontageInstanceID, OutAction);
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
