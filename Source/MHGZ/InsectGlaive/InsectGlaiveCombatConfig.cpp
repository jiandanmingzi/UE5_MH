// Copyright MHGZ Project. All Rights Reserved.

#include "InsectGlaive/InsectGlaiveCombatConfig.h"

#include "GameplayEffect.h"
#include "InsectGlaive/IGPowderActor.h"
#include "Misc/DataValidation.h"

#define LOCTEXT_NAMESPACE "MHGZInsectGlaiveCombatConfig"
#if WITH_EDITOR
EDataValidationResult UInsectGlaiveCombatConfig::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	bool bInvalid = false;

	auto AddError = [&Context, &bInvalid](const FText& Error)
	{
		Context.AddError(Error);
		bInvalid = true;
	};

	auto CheckPositive = [&AddError](const TCHAR* FieldName, float Value)
	{
		if (!FMath::IsFinite(Value) || Value <= 0.f)
		{
			AddError(FText::Format(
				LOCTEXT("NotPositive", "{0} must be > 0 (current {1})."),
				FText::FromString(FieldName), FText::AsNumber(Value)));
		}
	};

	auto CheckNonNegative = [&AddError](const TCHAR* FieldName, float Value)
	{
		if (!FMath::IsFinite(Value) || Value < 0.f)
		{
			AddError(FText::Format(
				LOCTEXT("Negative", "{0} must be >= 0 (current {1})."),
				FText::FromString(FieldName), FText::AsNumber(Value)));
		}
	};

	// 必需 GE 引用
	if (WhiteEffectClass == nullptr)
	{
		AddError(LOCTEXT("MissingWhiteEffect", "WhiteEffectClass must be assigned."));
	}
	if (RedEffectClass == nullptr)
	{
		AddError(LOCTEXT("MissingRedEffect", "RedEffectClass must be assigned."));
	}
	if (OrangeEffectClass == nullptr)
	{
		AddError(LOCTEXT("MissingOrangeEffect", "OrangeEffectClass must be assigned."));
	}
	if (TripleUpEffectClass == nullptr)
	{
		AddError(LOCTEXT("MissingTripleUpEffect", "TripleUpEffectClass must be assigned."));
	}
	if (ExtractCollectedSound == nullptr)
	{
		AddError(LOCTEXT("MissingExtractCollectedSound", "ExtractCollectedSound must be assigned."));
	}
	if (TripleUpActivatedSound == nullptr)
	{
		AddError(LOCTEXT("MissingTripleActivatedSound", "TripleUpActivatedSound must be assigned."));
	}
	if (TripleUpExpiredSound == nullptr)
	{
		AddError(LOCTEXT("MissingTripleExpiredSound", "TripleUpExpiredSound must be assigned."));
	}
	if (KinsectDepletedSound == nullptr)
	{
		AddError(LOCTEXT("MissingKinsectDepletedSound", "KinsectDepletedSound must be assigned."));
	}
	if (KinsectAttachSocket.IsNone())
	{
		AddError(LOCTEXT("MissingKinsectAttachSocket", "KinsectAttachSocket must be assigned."));
	}
	if (KinsectSlashAttachSocket.IsNone())
	{
		AddError(LOCTEXT("MissingKinsectSlashAttachSocket",
			"KinsectSlashAttachSocket must be assigned to the temporary weapon attachment socket."));
	}
	if (KinsectData == nullptr)
	{
		AddError(LOCTEXT("MissingKinsectData", "KinsectData must be assigned."));
	}
	if (KinsectMarkLaunchSocket.IsNone())
	{
		AddError(LOCTEXT("MissingKinsectMarkLaunchSocket", "KinsectMarkLaunchSocket must be assigned."));
	}

	// 时长/间隔必须为正
	CheckPositive(TEXT("WhiteExtractDuration"), WhiteExtractDuration);
	CheckPositive(TEXT("RedExtractDuration"), RedExtractDuration);
	CheckPositive(TEXT("OrangeExtractDuration"), OrangeExtractDuration);
	CheckPositive(TEXT("TripleUpDuration"), TripleUpDuration);
	CheckPositive(TEXT("AwakenedPierceHitInterval"), AwakenedPierceHitInterval);
	CheckPositive(TEXT("KinsectArrivalRadius"), KinsectArrivalRadius);
	CheckPositive(TEXT("KinsectMarkMaxDistance"), KinsectMarkMaxDistance);
	CheckPositive(TEXT("KinsectMarkDuration"), KinsectMarkDuration);
	CheckNonNegative(TEXT("KinsectAutoMarkSearchRadius"), KinsectAutoMarkSearchRadius);
	CheckNonNegative(TEXT("KinsectAutoMarkAttackInterval"), KinsectAutoMarkAttackInterval);
	if ((KinsectAutoMarkSearchRadius > 0.0f)
		!= (KinsectAutoMarkAttackInterval > 0.0f))
	{
		AddError(LOCTEXT("IncompleteKinsectAutoMarkTuning",
			"KinsectAutoMarkSearchRadius and KinsectAutoMarkAttackInterval must both be zero (disabled/unset) or both be positive."));
	}
	CheckPositive(TEXT("KinsectMarkProjectileSpeed"), KinsectMarkProjectileSpeed);
	CheckPositive(TEXT("KinsectMarkProjectileRadius"), KinsectMarkProjectileRadius);
	CheckPositive(TEXT("KinsectMarkProjectileLifetime"), KinsectMarkProjectileLifetime);
	CheckPositive(TEXT("AerialFallGravityScale"), AerialFallGravityScale);
	CheckPositive(TEXT("WhiteAerialFallGravityScale"), WhiteAerialFallGravityScale);
	CheckNonNegative(TEXT("AerialFallBrakingDeceleration"), AerialFallBrakingDeceleration);
	CheckPositive(TEXT("MarkedHitPowderCooldown"), MarkedHitPowderCooldown);
	CheckPositive(TEXT("PowderNaturalExplosionRadius"), PowderNaturalExplosionRadius);
	if (PowderMaxCount < 1)
	{
		AddError(LOCTEXT("InvalidPowderMaxCount", "PowderMaxCount must be at least 1."));
	}
	// Powder is still dormant until a final actor class is assigned. This keeps
	// existing M5 weapon assets valid while enforcing the complete tuning contract
	// as soon as a weapon opts into powder production.
	if (PowderActorClass)
	{
		CheckPositive(TEXT("KinsectAutoMarkSearchRadius"), KinsectAutoMarkSearchRadius);
		CheckPositive(TEXT("KinsectAutoMarkAttackInterval"), KinsectAutoMarkAttackInterval);
		CheckPositive(TEXT("MarkedHitPowderCooldown"), MarkedHitPowderCooldown);
		CheckPositive(TEXT("PowderLifetime"), PowderLifetime);
		CheckPositive(TEXT("PowderMotionValue"), PowderMotionValue);
		CheckPositive(TEXT("PowderDespawnDistance"), PowderDespawnDistance);
		if (!FMath::IsNearlyEqual(PowderLifetime, 180.0f, 0.001f))
		{
			AddError(LOCTEXT("InvalidPowderLifetimeContract",
				"PowderLifetime is fixed at 180 world seconds by the current design."));
		}
	}

	// 倍率/动作值必须非负
	CheckNonNegative(TEXT("WhiteMoveSpeedMultiplier"), WhiteMoveSpeedMultiplier);
	CheckNonNegative(TEXT("RedAttackMultiplier"), RedAttackMultiplier);
	for (const auto Pair : {TPair<const TCHAR*, float>(TEXT("RedKinsectStaminaDrainMultiplier"), RedKinsectStaminaDrainMultiplier),
		TPair<const TCHAR*, float>(TEXT("TripleKinsectStaminaDrainMultiplier"), TripleKinsectStaminaDrainMultiplier)})
	{
		if (!FMath::IsFinite(Pair.Value) || Pair.Value < 0.f || Pair.Value > 1.f)
		{
			AddError(FText::Format(LOCTEXT("InvalidDrainMultiplier", "{0} must be finite and between 0 and 1."),
				FText::FromString(Pair.Key)));
		}
	}
	CheckNonNegative(TEXT("OrangeDefenseMultiplier"), OrangeDefenseMultiplier);
	CheckNonNegative(TEXT("TripleAttackMultiplier"), TripleAttackMultiplier);
	CheckNonNegative(TEXT("TripleMoveSpeedMultiplier"), TripleMoveSpeedMultiplier);
	CheckNonNegative(TEXT("TripleDefenseMultiplier"), TripleDefenseMultiplier);
	CheckNonNegative(TEXT("SendKinsectMotionValue"), SendKinsectMotionValue);
	CheckNonNegative(TEXT("AerialLandingHorizontalSpeed"), AerialLandingHorizontalSpeed);

	if (AerialFallMontage == nullptr)
	{
		AddError(LOCTEXT("MissingAerialFallMontage", "AerialFallMontage must be assigned."));
	}
	if (WhiteAerialFallMontage == nullptr)
	{
		AddError(LOCTEXT("MissingWhiteAerialFallMontage", "WhiteAerialFallMontage must be assigned."));
	}
	if (AerialLandingMontage == nullptr)
	{
		AddError(LOCTEXT("MissingAerialLandingMontage", "AerialLandingMontage must be assigned."));
	}
	// 共享舞踏层数上限
	if (MaxDanceStacks < 0)
	{
		AddError(FText::Format(
			LOCTEXT("InvalidMaxDanceStacks", "MaxDanceStacks must be >= 0 (current {0})."),
			FText::AsNumber(MaxDanceStacks)));
	}

	FWeaponMovementRequest DanceVaultRequest;
	DanceVaultRequest.Mode = EWeaponMovementMode::BallisticVault;
	DanceVaultRequest.BallisticMode = DanceVaultBallisticMode;
	DanceVaultRequest.ApexHeight = DanceVaultApexHeight;
	DanceVaultRequest.Duration = DanceVaultDuration;
	DanceVaultRequest.MaxDistance = DanceVaultDistance;
	DanceVaultRequest.LaunchVelocity = DanceVaultLaunchVelocity;
	if (!DanceVaultRequest.HasValidBallisticParameters())
	{
		AddError(LOCTEXT(
			"InvalidDanceVaultBallisticParameters",
			"DanceVault ballistic tuning must enable exactly the parameter group selected by DanceVaultBallisticMode."));
	}


	return bInvalid ? EDataValidationResult::Invalid
		: (Result == EDataValidationResult::NotValidated ? EDataValidationResult::Valid : Result);
}
#endif

#undef LOCTEXT_NAMESPACE
