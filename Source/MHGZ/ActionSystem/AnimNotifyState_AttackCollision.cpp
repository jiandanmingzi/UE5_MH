// Copyright MHGZ Project. All Rights Reserved.

#include "AnimNotifyState_AttackCollision.h"
#include "MHGZAttackAbility.h"
#include "MHGZAbilitySystemComponent.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerState.h"
#include "MHGZPlayerState.h"

void UAnimNotifyState_AttackCollision::NotifyBegin(USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation, float TotalDuration,
	const FAnimNotifyEventReference& EventReference)
{
	if (!MeshComp) return;

	AActor* Owner = MeshComp->GetOwner();
	if (!Owner) return;

	// 获取 MHGZ ASC
	ACharacter* Character = Cast<ACharacter>(Owner);
	if (!Character) return;

	AMHGZPlayerState* PS = Character->GetPlayerState<AMHGZPlayerState>();
	if (!PS) return;

	UMHGZAbilitySystemComponent* ASC = PS->GetMHGZAbilitySystemComponent();
	if (!ASC) return;

	// 遍历所有活跃 Ability，查找 UMHGZAttackAbility
	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (Spec.IsActive())
		{
			if (UMHGZAttackAbility* AttackGA = Cast<UMHGZAttackAbility>(Spec.GetPrimaryInstance()))
			{
				AttackGA->EnableCollision(ConfigIndex);
			}
		}
	}
}

void UAnimNotifyState_AttackCollision::NotifyEnd(USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	if (!MeshComp) return;

	AActor* Owner = MeshComp->GetOwner();
	if (!Owner) return;

	ACharacter* Character = Cast<ACharacter>(Owner);
	if (!Character) return;

	AMHGZPlayerState* PS = Character->GetPlayerState<AMHGZPlayerState>();
	if (!PS) return;

	UMHGZAbilitySystemComponent* ASC = PS->GetMHGZAbilitySystemComponent();
	if (!ASC) return;

	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (Spec.IsActive())
		{
			if (UMHGZAttackAbility* AttackGA = Cast<UMHGZAttackAbility>(Spec.GetPrimaryInstance()))
			{
				AttackGA->DisableCollision(ConfigIndex);
			}
		}
	}
}

void UAnimNotifyState_AttackCollision::NotifyTick(USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation, float FrameDeltaTime,
	const FAnimNotifyEventReference& EventReference)
{
	if (!MeshComp) return;

	ACharacter* Character = Cast<ACharacter>(MeshComp->GetOwner());
	if (!Character) return;

	AMHGZPlayerState* PS = Character->GetPlayerState<AMHGZPlayerState>();
	if (!PS) return;

	UMHGZAbilitySystemComponent* ASC = PS->GetMHGZAbilitySystemComponent();
	if (!ASC) return;

	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (Spec.IsActive())
		{
			if (UMHGZAttackAbility* AttackGA = Cast<UMHGZAttackAbility>(Spec.GetPrimaryInstance()))
			{
				AttackGA->TickCollision(ConfigIndex, FrameDeltaTime);
			}
		}
	}
}
