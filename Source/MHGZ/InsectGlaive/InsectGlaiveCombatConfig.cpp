// Copyright MHGZ Project. All Rights Reserved.

#include "InsectGlaive/InsectGlaiveCombatConfig.h"

#include "GameplayEffect.h"
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
	CheckPositive(TEXT("KinsectSlashMaxDistance"), KinsectSlashMaxDistance);
	CheckPositive(TEXT("KinsectGlideMarkMaxDistance"), KinsectGlideMarkMaxDistance);
	CheckPositive(TEXT("KinsectMarkMaxDistance"), KinsectMarkMaxDistance);
	CheckPositive(TEXT("KinsectMarkDuration"), KinsectMarkDuration);
	CheckPositive(TEXT("KinsectMarkProjectileSpeed"), KinsectMarkProjectileSpeed);
	CheckPositive(TEXT("KinsectMarkProjectileRadius"), KinsectMarkProjectileRadius);
	CheckPositive(TEXT("KinsectMarkProjectileLifetime"), KinsectMarkProjectileLifetime);
	CheckPositive(TEXT("KinsectGlideFallbackDistance"), KinsectGlideFallbackDistance);
	CheckPositive(TEXT("KinsectGlideFallbackLiftHeight"), KinsectGlideFallbackLiftHeight);
	CheckPositive(TEXT("AwakenedKinsectMaxDistance"), AwakenedKinsectMaxDistance);
	CheckPositive(TEXT("AwakenedHunterFlightMaxDistance"), AwakenedHunterFlightMaxDistance);
	CheckPositive(TEXT("AwakenedHunterFlightStartTime"), AwakenedHunterFlightStartTime);
	CheckPositive(TEXT("DivingWyvernDistance"), DivingWyvernDistance);
	CheckPositive(TEXT("DivingWyvernHeight"), DivingWyvernHeight);
	CheckPositive(TEXT("DivingWyvernDuration"), DivingWyvernDuration);
	CheckPositive(TEXT("PowderGatherRadius"), PowderGatherRadius);
	CheckPositive(TEXT("PowderGatherDuration"), PowderGatherDuration);

	// 倍率/动作值必须非负
	CheckNonNegative(TEXT("WhiteMoveSpeedMultiplier"), WhiteMoveSpeedMultiplier);
	CheckNonNegative(TEXT("RedAttackMultiplier"), RedAttackMultiplier);
	CheckNonNegative(TEXT("OrangeDefenseMultiplier"), OrangeDefenseMultiplier);
	CheckNonNegative(TEXT("TripleAttackMultiplier"), TripleAttackMultiplier);
	CheckNonNegative(TEXT("TripleMoveSpeedMultiplier"), TripleMoveSpeedMultiplier);
	CheckNonNegative(TEXT("TripleDefenseMultiplier"), TripleDefenseMultiplier);
	CheckNonNegative(TEXT("SendKinsectMotionValue"), SendKinsectMotionValue);
	CheckNonNegative(TEXT("DrawSendKinsectMotionValue"), DrawSendKinsectMotionValue);
	CheckNonNegative(TEXT("AwakenedKinsectMotionValue"), AwakenedKinsectMotionValue);
	CheckNonNegative(TEXT("DescendingThrustAirControl"), DescendingThrustAirControl);

	// 修正角范围
	if (!FMath::IsFinite(AwakenedAimCorrectionAngle)
		|| AwakenedAimCorrectionAngle < 0.f || AwakenedAimCorrectionAngle > 180.f)
	{
		AddError(FText::Format(
			LOCTEXT("InvalidAimCorrectionAngle", "AwakenedAimCorrectionAngle must be within [0, 180] degrees (current {0})."),
			FText::AsNumber(AwakenedAimCorrectionAngle)));
	}

	// 舞踏数组契约
	if (MaxDanceStacks < 0)
	{
		AddError(FText::Format(
			LOCTEXT("InvalidMaxDanceStacks", "MaxDanceStacks must be >= 0 (current {0})."),
			FText::AsNumber(MaxDanceStacks)));
	}
	else
	{
		const int32 ExpectedCount = MaxDanceStacks + 1;
		if (DanceDamageMultipliers.Num() != ExpectedCount)
		{
			AddError(FText::Format(
				LOCTEXT("InvalidDanceArrayLength", "DanceDamageMultipliers.Num() must be MaxDanceStacks + 1 (expected {0}, current {1})."),
				FText::AsNumber(ExpectedCount), FText::AsNumber(DanceDamageMultipliers.Num())));
		}
		else if (DanceDamageMultipliers.Num() > 0)
		{
			if (!FMath::IsNearlyEqual(DanceDamageMultipliers[0], 1.0f, KINDA_SMALL_NUMBER))
			{
				AddError(FText::Format(
					LOCTEXT("InvalidDanceBaseMultiplier", "DanceDamageMultipliers[0] must be 1.0 (current {0})."),
					FText::AsNumber(DanceDamageMultipliers[0])));
			}
			for (int32 Index = 0; Index < DanceDamageMultipliers.Num(); ++Index)
			{
				if (!FMath::IsFinite(DanceDamageMultipliers[Index]) || DanceDamageMultipliers[Index] < 0.f)
				{
					AddError(FText::Format(
						LOCTEXT("InvalidDanceMultiplier", "DanceDamageMultipliers[{0}] must be >= 0 (current {1})."),
						FText::AsNumber(Index), FText::AsNumber(DanceDamageMultipliers[Index])));
				}
			}
		}
	}

	if (PowderGatherMaxCount <= 0)
	{
		AddError(FText::Format(
			LOCTEXT("InvalidPowderMaxCount", "PowderGatherMaxCount must be > 0 (current {0})."),
			FText::AsNumber(PowderGatherMaxCount)));
	}

	FWeaponMovementRequest DanceVaultRequest;
	DanceVaultRequest.Mode = EWeaponMovementMode::BallisticVault;
	DanceVaultRequest.BallisticMode = DanceVaultBallisticMode;
	DanceVaultRequest.ApexHeight = DanceVaultApexHeight;
	DanceVaultRequest.Duration = DanceVaultDuration;
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
