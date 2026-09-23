// Copyright MHGZ Project. All Rights Reserved.

#include "InsectGlaive/InsectGlaiveCombatConfig.h"

#include "GameplayEffect.h"
#include "Generated/MHGZVaultCurveTables.h"
#include "Misc/DataValidation.h"

#define LOCTEXT_NAMESPACE "MHGZInsectGlaiveCombatConfig"
UInsectGlaiveCombatConfig::UInsectGlaiveCombatConfig()
{
	// 撑杆跳的默认值。前 / 左 / 右六条**不是手抄** —— 直接读
	// `Generated/MHGZVaultCurveTables.h`（由 `Scripts/MHRise/build_vault_curves.py`
	// 从 MHR 实机录制生成），所以「重跑生成器」就足以让配置跟上新录制。
	//
	// 后撑杆跳两条仍是本文件的手写常量：它的实录不在采集目录里、生成器覆盖不到，
	// 而迁移计划明确要求这一步不动它。值是迁移前那两个字段的值，逐字未变。
	auto Add = [this](EDirectionalInput Direction, bool bWhite, float VaultDuration,
		float ArcDuration, float Distance, float ApexHeight)
	{
		FVaultTuning& Tuning = VaultTunings.AddDefaulted_GetRef();
		Tuning.Direction = Direction;
		Tuning.bWhite = bWhite;
		Tuning.VaultDuration = VaultDuration;
		Tuning.ArcDuration = ArcDuration;
		Tuning.Distance = Distance;
		Tuning.ApexHeight = ApexHeight;
	};

	using namespace MHGZ::VaultCurves;

	// ── 后撑杆跳 ────────────────────────────────────────────────────────
	// 无白灯：ArcDuration 1.968 减掉下坠残差 0.2347 = 1.733333。那个残差就是
	// 「动作 id 146+147+143，24 次采样」里 143 那一段的实测 **0.234 s**。
	Add(EDirectionalInput::Back, false, 1.733333f, 1.968f, 583.87f, 579.7f);
	// 白灯：VaultDuration 取**飞行时长**而不是 clip 长度。曾用 2.233333
	// (= 0.650 + 1.5833，两条 clip 自然长度之和)，那是被 `HandoffProgress <= 1.0`
	// 的守卫逼出来的值，不是 MHR 说的值：159 的自然长度约 1.596 s，但众数只有
	// 1.486 s —— **它是被触地砍断的**。拿 clip 长度当窗口等于把 110 ms 本该不存在的
	// 飞行补出来，实录路径被拉伸 4.7%（实测白灯时长 +3.4%、水平位移 +2.1%）。
	//
	// 白灯**没有独立的下坠段**（159 一直覆盖到落地），所以 ArcDuration 与
	// VaultDuration 相等、HandoffProgress 恰为 1.0 —— 那是合法的「无自由落体窗口」，
	// 不是配置错误，不要再为过守卫凑数。
	Add(EDirectionalInput::Back, true, 2.137f, 2.137f, 706.05f, 748.9f);

	// ── 前 / 左 / 右六条：四个标量全部来自生成表 ──────────────────────────
	auto AddGenerated = [&Add](EDirectionalInput Direction, bool bWhite, const FMeta& Meta)
	{
		Add(Direction, bWhite, Meta.VaultDuration, Meta.ArcDuration,
			Meta.Distance, Meta.Apex);
	};
	AddGenerated(EDirectionalInput::Forward, false, Forward_NoWhite_Meta);
	AddGenerated(EDirectionalInput::Left, false, Left_NoWhite_Meta);
	AddGenerated(EDirectionalInput::Right, false, Right_NoWhite_Meta);
	AddGenerated(EDirectionalInput::Forward, true, Forward_White_Meta);
	AddGenerated(EDirectionalInput::Left, true, Left_White_Meta);
	AddGenerated(EDirectionalInput::Right, true, Right_White_Meta);
}

const FVaultTuning* UInsectGlaiveCombatConfig::FindVaultTuning(
	const EDirectionalInput Direction, const bool bWhite) const
{
	for (const FVaultTuning& Tuning : VaultTunings)
	{
		if (Tuning.Direction == Direction && Tuning.bWhite == bWhite)
		{
			return &Tuning;
		}
	}
	return nullptr;
}

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
	// 撑杆跳：逐条校验，而且**跨字段**自洽。
	//
	// 平铺字段时代只能逐个 CheckPositive —— 那查不出「移动窗口比归一化域还长」
	// 这种错，而它的后果是 HandoffProgress > 1.0，运行时**直接拒绝移动**（不出招）。
	// 收成一条一条正是为了能在这里比较两个字段。
	if (VaultTunings.Num() == 0)
	{
		AddError(LOCTEXT("VaultTuningsEmpty",
			"VaultTunings is empty: every vault direction will fail to activate."));
	}
	for (const FVaultTuning& Tuning : VaultTunings)
	{
		const FString Label = FString::Printf(TEXT("VaultTunings[%d/%s]"),
			static_cast<int32>(Tuning.Direction), Tuning.bWhite ? TEXT("white") : TEXT("normal"));
		CheckPositive(*(Label + TEXT(".VaultDuration")), Tuning.VaultDuration);
		CheckPositive(*(Label + TEXT(".ArcDuration")), Tuning.ArcDuration);
		CheckPositive(*(Label + TEXT(".Distance")), Tuning.Distance);
		CheckPositive(*(Label + TEXT(".ApexHeight")), Tuning.ApexHeight);
		if (!FMath::IsFinite(Tuning.ArcDuration) || !FMath::IsFinite(Tuning.VaultDuration))
		{
			continue;
		}
		if (Tuning.ArcDuration < Tuning.VaultDuration)
		{
			AddError(FText::FromString(FString::Printf(
				TEXT("%s: ArcDuration %.4f < VaultDuration %.4f -- the movement window is "
					 "longer than the normalization domain, so HandoffProgress would exceed "
					 "1.0 and the movement would be rejected at runtime."),
				*Label, Tuning.ArcDuration, Tuning.VaultDuration)));
			continue;
		}
		// 残余自由落体 = ArcDuration - VaultDuration。非白灯实测 0.2347 s（下坠段
		// `143`/`157` 就是它）；白灯没有独立下坠段，实测为 0。容差给到 ±0.06 s：
		// 下坠段短一次就少 0.03 s，那是录制抖动，不是配置错误。
		const float Residual = Tuning.ArcDuration - Tuning.VaultDuration;
		const bool bResidualOk = Tuning.bWhite
			? FMath::IsNearlyZero(Residual, 0.02f)
			: (Residual >= 0.18f && Residual <= 0.30f);
		if (!bResidualOk)
		{
			AddError(FText::FromString(FString::Printf(
				TEXT("%s: residual free fall %.4f s is outside the measured range "
					 "(normal 0.18~0.30, white 0). A too-long window stretches the recorded "
					 "path; a too-short one cuts the arc off before the ground."),
				*Label, Residual)));
		}
	}
	CheckPositive(TEXT("AerialFallGravityScale"), AerialFallGravityScale);
	CheckPositive(TEXT("WhiteAerialFallGravityScale"), WhiteAerialFallGravityScale);
	CheckNonNegative(TEXT("AerialFallBrakingDeceleration"), AerialFallBrakingDeceleration);
	CheckPositive(TEXT("AirDodgeHorizontalSpeed"), AirDodgeHorizontalSpeed);
	CheckPositive(TEXT("AirDodgeVerticalSpeed"), AirDodgeVerticalSpeed);
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
