// Copyright MHGZ Project. All Rights Reserved.
#pragma once
#include "MHGZCoAttackTestTypes.h"
#include "ActionSystem/Tests/Shared/MHGZM3TestHarness.h"
#include "ActionSystem/MHGZGameplayEffectContext.h"
#include "AttributeSystem/MHGZAttributeSet.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "InsectGlaive/Kinsect/IGCoAttackComponent.h"
#include "Monster/MHGZMonsterHitzoneComponent.h"
#include "Monster/MHGZTrainingDummy.h"

namespace MHGZCoAttackTest
{
/** Shared M2/M3 real GAS/resource/contact fixture; writes no production assets. */
struct FCoAttackFixture
{
	FMHGZM3Harness H;
	USkeletalMeshComponent* Weapon = nullptr;
	UMHGZCoAttackTestAbility* Ability = nullptr;
	UIGCoAttackComponent* Sampler = nullptr;
	AMHGZTrainingDummy* Target = nullptr;
	UMHGZMonsterHitzoneComponent* Part = nullptr;
	FGameplayAbilitySpecHandle Handle;
	int32 WeaponHits = 0, KinsectHits = 0;
	TArray<FGuid> HitIDs;
	TArray<float> Powers, Motions;
	FVector Origin = FVector(500, 0, 100);
	bool Build(const float ReturnSpeed = 2500.f, const bool bAlignEntry = true)
	{
		if (!H.Setup(false, AMHGZM3TestCharacter::StaticClass())) return false;
		// AddInstigator needs an avatar that actually resolves its ASC.
		H.Character->SetPlayerState(H.PlayerState);
		H.ASC->SetNumericAttributeBase(UMHGZAttributeSet::GetAttackPowerAttribute(), 200.f);
		USkeletalMesh* Glaive = LoadObject<USkeletalMesh>(nullptr,
			TEXT("/Game/Weapons/InsectGlaive/Meshes/Glaive/SKM_IG_Glaive.SKM_IG_Glaive"));
		if (!Glaive) return false;
		Weapon = NewObject<USkeletalMeshComponent>(H.Character);
		Weapon->ComponentTags.Add(TEXT("WeaponTrace"));
		Weapon->SetSkeletalMesh(Glaive); Weapon->RegisterComponent();
		SetSource(Origin);
		if (bAlignEntry) H.Kinsect->SetActorLocation(Origin);
		H.KinsectData->ReturnSpeed = ReturnSpeed;
		// Exercise exact integer damage boundaries through the real GE pipeline.
		H.KinsectData->KinsectAttackPower = 100.f;
		Target = H.World->SpawnActor<AMHGZTrainingDummy>();
		if (!Target) return false;
		Target->bInfiniteHealth = false;
		Target->GetAbilitySystemComponent()->InitAbilityActorInfo(Target, Target);
		Target->GetAttributeSet()->InitMaxHealth(10000); Target->GetAttributeSet()->InitHealth(10000);
		Target->GetAttributeSet()->InitDefense(0);
		Part = NewObject<UMHGZMonsterHitzoneComponent>(Target);
		Part->HitzoneTag = M3::Tag(TEXT("Hitzone.Head"));
		Part->DefenseMultiplier = 100.f;
		Part->SetSphereRadius(8.f);
		Part->RegisterComponent();
		SetTarget(Origin + FVector(20, 0, 0));
		// 地面协同的新准入要求；测试在动作激活前建立真实有效虫印。
		if (!H.Resource->SetKinsectMark(Part, Part->GetComponentLocation(), nullptr)) return false;
		Handle = H.GiveAbility(UMHGZCoAttackTestAbility::StaticClass());
		Ability = Activate(Handle);
		Sampler = H.Character->FindComponentByClass<UIGCoAttackComponent>();
		if (!Ability || !Ability->IsActive() || !Sampler || !Sampler->IsRunning()) return false;
		Target->GetAbilitySystemComponent()->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
			[this](UAbilitySystemComponent*, const FGameplayEffectSpec& S, FActiveGameplayEffectHandle)
			{
				const auto* C = FMHGZGameplayEffectContext::ExtractEffectContext(S.GetContext());
				if (!C) return;
				if (C->DamageSourceType == EMHGZDamageSourceType::Weapon) ++WeaponHits;
				if (C->DamageSourceType != EMHGZDamageSourceType::Kinsect) return;
				++KinsectHits; HitIDs.Add(C->AttackInstanceID);
				Powers.Add(S.GetSetByCallerMagnitude(M3::Tag(TEXT("Damage.AttackPower")), false));
				Motions.Add(S.GetSetByCallerMagnitude(M3::Tag(TEXT("Damage.MotionValue")), false));
			});
		At(0.0, Origin);
		return true;
	}
	UMHGZCoAttackTestAbility* Activate(FGameplayAbilitySpecHandle InHandle)
	{
		if (!H.TryActivateWithInput(InHandle, M3::MakePosedInput(true, false))) return nullptr;
		FGameplayAbilitySpec* Spec = H.ASC->FindAbilitySpecFromHandle(InHandle);
		if (Spec)
			for (UGameplayAbility* Instance : Spec->GetAbilityInstances())
				if (Instance->IsActive()) return Cast<UMHGZCoAttackTestAbility>(Instance);
		return nullptr;
	}
	void SetSource(const FVector& P)
	{
		Weapon->AddWorldOffset(P - Weapon->GetSocketLocation(TEXT("IG_FrontTip")));
	}
	void SetTarget(const FVector& P) { Part->SetWorldLocation(P); }
	void At(const double Time, const FVector& P)
	{
		const float Dt = float(Time - H.World->GetTimeSeconds());
		H.World->TimeSeconds = Time;
		SetSource(P);
		Sampler->TickComponent(Dt, LEVELTICK_All, nullptr);
	}
	void WarmUp()
	{
		for (int32 I = 1; I <= 5; ++I) At(I * .05, Origin);
	}
	~FCoAttackFixture() { if (H.ASC && Handle.IsValid()) H.ASC->CancelAbilityHandle(Handle); H.Teardown(); }
};
}
