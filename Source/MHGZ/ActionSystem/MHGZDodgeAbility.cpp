// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZDodgeAbility.h"
#include "MHGZAbilitySystemComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

UMHGZDodgeAbility::UMHGZDodgeAbility()
{
	InputTag = FGameplayTag::RequestGameplayTag(TEXT("Input.Dodge"));
	StaminaCost = FScalableFloat(25.f);
}

bool UMHGZDodgeAbility::CanActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	const UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();
	if (!ASC) return false;

	// 受击/击倒中不可翻滚
	if (ASC->HasMatchingGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Hitstun"))) ||
		ASC->HasMatchingGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Knockdown"))))
	{
		return false;
	}

	// 攻击中但翻滚窗口未开 → 阻塞
	if (ASC->HasMatchingGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Attacking"))) &&
		!ASC->HasMatchingGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("Combat.State.DodgeAcceptOpen"))))
	{
		return false;
	}

	return true;
}

void UMHGZDodgeAbility::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	ACharacter* Character = Cast<ACharacter>(ActorInfo->AvatarActor.Get());
	if (!Character) return;

	UAnimMontage* DodgeMontage = SelectDodgeMontage();
	if (!DodgeMontage) return;

	// 播放 Dodge Montage（含 AnimNotifyState_DodgeWindow 控制无敌帧）
	UAnimInstance* AnimInstance = Character->GetMesh()->GetAnimInstance();
	if (AnimInstance)
	{
		AnimInstance->Montage_Play(DodgeMontage);

		// 绑定 Montage 结束回调
		FOnMontageEnded EndDelegate;
		EndDelegate.BindWeakLambda(this, [this](UAnimMontage* Montage, bool bInterrupted)
		{
			EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, false, bInterrupted);
		});
		AnimInstance->Montage_SetEndDelegate(EndDelegate, DodgeMontage);
	}
}

void UMHGZDodgeAbility::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

UAnimMontage* UMHGZDodgeAbility::SelectDodgeMontage() const
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC) return nullptr;

	const EComboDirection Direction = GetDodgeDirection();

	// 收刀态 → 使用 GA_Dodge 自身配置
	if (ASC->HasMatchingGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Sheathed"))))
	{
		if (const TSoftObjectPtr<UAnimMontage>* Found = SheathedDodgeMontages.Find(Direction))
		{
			return Found->LoadSynchronous();
		}
		// 回退到无方向
		if (const TSoftObjectPtr<UAnimMontage>* Fallback = SheathedDodgeMontages.Find(EComboDirection::None))
		{
			return Fallback->LoadSynchronous();
		}
	}

	// 拔刀态 → 通过 DataManager 查找武器配置
	// 简化：回退到收刀态配置
	if (const TSoftObjectPtr<UAnimMontage>* Found = SheathedDodgeMontages.Find(Direction))
	{
		return Found->LoadSynchronous();
	}

	return nullptr;
}

EComboDirection UMHGZDodgeAbility::GetDodgeDirection() const
{
	const ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (!Character) return EComboDirection::None;

	const FVector Input = Character->GetLastMovementInputVector();
	if (Input.IsNearlyZero())
		return EComboDirection::None;

	const FVector Forward = Character->GetActorForwardVector();
	const FVector Right = Character->GetActorRightVector();
	const FVector InputWorld = Forward * Input.Y + Right * Input.X;

	const float Angle = FMath::RadiansToDegrees(
		FMath::Atan2(
			FVector::DotProduct(InputWorld.GetSafeNormal(), Right.GetSafeNormal()),
			FVector::DotProduct(InputWorld.GetSafeNormal(), Forward.GetSafeNormal())));

	if (Angle >= -45.f && Angle < 45.f)       return EComboDirection::Forward;
	if (Angle >= 45.f && Angle < 135.f)       return EComboDirection::Right;
	if (Angle >= -135.f && Angle < -45.f)     return EComboDirection::Left;
	return EComboDirection::Back;
}
