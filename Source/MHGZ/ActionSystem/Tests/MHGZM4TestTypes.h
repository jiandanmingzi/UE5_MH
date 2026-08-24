// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ActionSystem/MHGZAttackAbility.h"
#include "ActionSystem/MHGZDodgeAbility.h"
#include "ActionSystem/MHGZDrawAttackAbility.h"
#include "ActionSystem/MHGZInsectGlaiveKinsectAbilities.h"
#include "ActionSystem/MHGZSheatheAbility.h"
#include "MHGZM4TestTypes.generated.h"

class ACharacter;
class UAnimMontage;

/** Test-only sheathe ability that replaces the asynchronous montage boundary. */
UCLASS()
class UMHGZM4TestSheatheAbility : public UMHGZSheatheAbility
{
	GENERATED_BODY()

public:
	bool CommitForTest();
	void FinishNormallyForTest();
	void InterruptForTest();

protected:
	virtual bool ValidateSheatheMontageDependencies() const override { return true; }
	virtual bool StartSheatheMontage(ACharacter& Character, UAnimMontage* Montage,
		FName StartSection) override;
};

/** Test attack with a synchronous action boundary and no montage dependency. */
UCLASS()
class UMHGZM4TestAttackAbility : public UMHGZAttackAbility
{
	GENERATED_BODY()

public:
	UMHGZM4TestAttackAbility();
	void ApplyDirectionCorrectionForTest() { ApplyDirectionCorrection(); }
	bool ApplyInActionDirectionCorrectionForTest(const FWeaponActionToken& ActionToken,
		float MaxCorrectionAngleOverride = -1.f)
	{
		return ApplyInActionDirectionCorrection(ActionToken, MaxCorrectionAngleOverride);
	}
	void SetInActionDirectionForTest(const FVector& Direction)
	{
		InActionDirectionForTest = Direction;
	}

	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

protected:
	virtual bool ValidateActionDependencies() const override { return true; }
	virtual FVector GetInActionCorrectionDirection() const override
	{
		return InActionDirectionForTest;
	}

private:
	FVector InActionDirectionForTest = FVector::ZeroVector;
};

/** Test-only draw ability that keeps the native DrawCommit contract without a montage asset. */
UCLASS()
class UMHGZM4TestDrawAbility : public UMHGZDrawAttackAbility
{
	GENERATED_BODY()

public:
	bool CommitForTest();
	void InterruptForTest();

	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

protected:
	virtual bool ValidateActionDependencies() const override { return true; }
};

/** Test-only RT draw-and-send ability; the kinsect remains deferred to DrawCommit. */
UCLASS()
class UMHGZM4TestDrawAndSendAbility : public UMHGZDrawAndSendKinsectAbility
{
	GENERATED_BODY()

public:
	bool CommitDrawForTest();
	bool CommitSendForTest();
	void FinishNormallyForTest();
	void InterruptForTest();

protected:
	virtual bool ValidateActionDependencies() const override;
	virtual bool ValidateActionMontageDependencies() const override { return true; }
	virtual bool StartActionMontage(ACharacter& Character, UAnimMontage* Montage) override;
};

/** Test-only LTY send ability that replaces asynchronous montage playback. */
UCLASS()
class UMHGZM4TestSendKinsectAbility : public UMHGZSendKinsectAbility
{
	GENERATED_BODY()

public:
	bool CommitForTest();
	void FinishNormallyForTest();
	void InterruptForTest();

protected:
	virtual bool ValidateActionMontageDependencies() const override { return true; }
	virtual bool StartActionMontage(ACharacter& Character, UAnimMontage* Montage) override;
};

/** Test-only LTB recall ability that replaces asynchronous montage playback. */
UCLASS()
class UMHGZM4TestRecallKinsectAbility : public UMHGZRecallKinsectAbility
{
	GENERATED_BODY()

public:
	bool CommitForTest();
	void FinishNormallyForTest();
	void InterruptForTest();

protected:
	virtual bool ValidateActionMontageDependencies() const override { return true; }
	virtual bool StartActionMontage(ACharacter& Character, UAnimMontage* Montage) override;
};

/** Test dodge that replaces montage playback while preserving all M4 semantics. */
UCLASS()
class UMHGZM4TestDodgeAbility : public UMHGZDodgeAbility
{
	GENERATED_BODY()

public:
	UMHGZM4TestDodgeAbility();

	void SetLiveMovementInputForTest(bool bInHasInput) { bHasInputForTest = bInHasInput; }
	FName GetLastJumpedSectionForTest() const { return LastJumpedSection; }
	int32 GetFallbackExitConfigurationCountForTest() const
	{
		return FallbackExitConfigurationCount;
	}
	bool IsDirectionAllowedForTest(bool bSheathed, EDirectionalInput Direction) const
	{
		return IsDodgeDirectionAllowedForPose(bSheathed, Direction);
	}
	UAnimMontage* ResolveDodgeMontageForTest() const
	{
		return SelectDodgeSelection().Montage;
	}
	void BlendOutForTest();
	void FinishNormallyForTest();
	void InterruptForTest();
	void ExpireMontageEndFailsafeForTest();
	void EnterDodgeSectionForTest(FName SectionName);

protected:
	virtual bool ValidateDodgeMontageDependencies() const override { return true; }
	virtual bool StartDodgeMontage(ACharacter& Character, UAnimMontage* Montage,
		FName StartSection) override;
	virtual bool ConfigureDodgeCoreFallbackExit() override;
	virtual bool HasLiveMovementInput() const override { return bHasInputForTest; }
	virtual bool JumpToDodgeSection(FName SectionName) override;

private:
	bool bHasInputForTest = false;
	FName LastJumpedSection = NAME_None;
	int32 FallbackExitConfigurationCount = 0;
};

/** Starts through Commit, then deterministically fails the montage boundary. */
UCLASS()
class UMHGZM4FailingDodgeAbility : public UMHGZM4TestDodgeAbility
{
	GENERATED_BODY()

protected:
	virtual bool StartDodgeMontage(ACharacter& Character, UAnimMontage* Montage,
		FName StartSection) override;
};
