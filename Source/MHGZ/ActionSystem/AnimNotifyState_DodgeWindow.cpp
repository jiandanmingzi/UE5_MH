// Copyright MHGZ Project. All Rights Reserved.

#include "AnimNotifyState_DodgeWindow.h"
#include "AbilitySystemComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerState.h"

void UAnimNotifyState_DodgeWindow::NotifyBegin(USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation, float TotalDuration,
	const FAnimNotifyEventReference& EventReference)
{
	if (!MeshComp) return;

	ACharacter* Character = Cast<ACharacter>(MeshComp->GetOwner());
	if (!Character) return;

	if (UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
	{
		Capsule->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Ignore);
		Capsule->SetCollisionResponseToChannel(ECC_GameTraceChannel2, ECR_Ignore);
	}

	APlayerState* PS = Character->GetPlayerState();
	if (PS)
	{
		if (UAbilitySystemComponent* ASC = PS->FindComponentByClass<UAbilitySystemComponent>())
		{
			ASC->AddLooseGameplayTag(
				FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Invincible")));
		}
	}
}

void UAnimNotifyState_DodgeWindow::NotifyEnd(USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	if (!MeshComp) return;

	ACharacter* Character = Cast<ACharacter>(MeshComp->GetOwner());
	if (!Character) return;

	if (UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
	{
		Capsule->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Block);
		Capsule->SetCollisionResponseToChannel(ECC_GameTraceChannel2, ECR_Block);
	}

	APlayerState* PS = Character->GetPlayerState();
	if (PS)
	{
		if (UAbilitySystemComponent* ASC = PS->FindComponentByClass<UAbilitySystemComponent>())
		{
			ASC->RemoveLooseGameplayTag(
				FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Invincible")));
		}
	}
}
