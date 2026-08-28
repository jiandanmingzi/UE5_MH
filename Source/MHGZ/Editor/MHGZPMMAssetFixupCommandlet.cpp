// Copyright MHGZ Project. All Rights Reserved.

#include "Editor/MHGZPMMAssetFixupCommandlet.h"

#if WITH_EDITOR

#include "Animation/AnimData/CurveIdentifier.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimSequenceBase.h"
#include "AnimationBlueprintLibrary.h"
#include "Misc/Parse.h"
#include "Misc/PackageName.h"
#include "PoseSearch/PoseSearchAnimNotifies.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
constexpr float PMMEndPadding = 0.001f;
constexpr float PMMStartContinuingWindow = 0.10f;
// This only leaves the authored tail searchable as an exit from a continuing
// Start.  It is deliberately unrelated to the Start intent duration below.
constexpr float PMMStartBlockTailWindow = 1.0f / 30.0f;

enum class EPMMAssetRole : uint8
{
	Idle,
	Loop,
	Start,
	Stop
};

struct FPMMAssetSpec
{
	const TCHAR* ObjectPath;
	EPMMAssetRole Role;
	// Start is an input-edge semantic, not a clip-long state.  A non-zero value
	// is required only for Start specs and defines when its MM_Intent reaches 0.
	float StartIntentDuration = 0.0f;
};

const TArray<FPMMAssetSpec>& GetAssetSpecs()
{
	static const TArray<FPMMAssetSpec> Specs =
	{
		{ TEXT("/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/AS_Shth_Idle.AS_Shth_Idle"), EPMMAssetRole::Idle },
		{ TEXT("/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/AS_Shth_Walk_Start.AS_Shth_Walk_Start"), EPMMAssetRole::Start, 1.25f },
		{ TEXT("/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/AS_Shth_Walk_Loop.AS_Shth_Walk_Loop"), EPMMAssetRole::Loop },
		{ TEXT("/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/AS_Shth_Walk_Stop_Left.AS_Shth_Walk_Stop_Left"), EPMMAssetRole::Stop },
		{ TEXT("/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/AS_Shth_Walk_Stop_Right.AS_Shth_Walk_Stop_Right"), EPMMAssetRole::Stop },
		{ TEXT("/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/AS_Shth_Run_Start.AS_Shth_Run_Start"), EPMMAssetRole::Start, 0.80f },
		{ TEXT("/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/AS_Shth_Run_Loop.AS_Shth_Run_Loop"), EPMMAssetRole::Loop },
		{ TEXT("/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/AS_Shth_Run_Stop_Left.AS_Shth_Run_Stop_Left"), EPMMAssetRole::Stop },
		{ TEXT("/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/AS_Shth_Run_Stop_Right.AS_Shth_Run_Stop_Right"), EPMMAssetRole::Stop },
		{ TEXT("/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/AS_Shth_Sprint_Start.AS_Shth_Sprint_Start"), EPMMAssetRole::Start, 0.80f },
		{ TEXT("/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/AS_Shth_Sprint_Loop_125x.AS_Shth_Sprint_Loop_125x"), EPMMAssetRole::Loop },
		{ TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Locomotion/AS_UnSh_Idle.AS_UnSh_Idle"), EPMMAssetRole::Idle },
		{ TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Locomotion/AS_UnSh_Walk_Start.AS_UnSh_Walk_Start"), EPMMAssetRole::Start, 0.60f },
		{ TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Locomotion/AS_UnSh_Walk_Loop.AS_UnSh_Walk_Loop"), EPMMAssetRole::Loop },
		{ TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Locomotion/AS_UnSh_Walk_Stop.AS_UnSh_Walk_Stop"), EPMMAssetRole::Stop }
	};

	return Specs;
}

bool ReplaceFloatCurve(UAnimSequenceBase& Sequence, const FName CurveName, const TArray<float>& Times, const TArray<float>& Values)
{
	if (Sequence.HasCurveData(CurveName))
	{
		UAnimationBlueprintLibrary::RemoveCurve(&Sequence, CurveName);
	}
	UAnimationBlueprintLibrary::AddCurve(&Sequence, CurveName, ERawCurveTrackTypes::RCT_Float);
	UAnimationBlueprintLibrary::AddFloatCurveKeys(&Sequence, CurveName, Times, Values);
	return Sequence.HasCurveData(CurveName);
}

bool RenameStopDistanceCurve(UAnimSequenceBase& Sequence)
{
	const FName DistanceName(TEXT("Distance"));
	const FName StopDistanceName(TEXT("MM_DistanceToStop"));
	if (!Sequence.HasCurveData(DistanceName))
	{
		return Sequence.HasCurveData(StopDistanceName);
	}

	IAnimationDataController& Controller = Sequence.GetController();
	const FAnimationCurveIdentifier DistanceId(DistanceName, ERawCurveTrackTypes::RCT_Float);
	const FAnimationCurveIdentifier StopDistanceId(StopDistanceName, ERawCurveTrackTypes::RCT_Float);
	if (Sequence.HasCurveData(StopDistanceName)
		&& !Controller.RemoveCurve(StopDistanceId, false))
	{
		return false;
	}

	return Controller.RenameCurve(DistanceId, StopDistanceId, false);
}

bool ConfigurePoseSearchControlNotifies(UAnimSequenceBase& Sequence, const EPMMAssetRole Role,
	const float ContinuingModifier)
{
	const FName ControlTrackName(TEXT("PoseSearchControl"));
	Sequence.Notifies.RemoveAll([](const FAnimNotifyEvent& Notify)
	{
		return Cast<UAnimNotifyState_PoseSearchBlockTransition>(Notify.NotifyStateClass) != nullptr
			|| Cast<UAnimNotifyState_PoseSearchOverrideContinuingPoseCostBias>(Notify.NotifyStateClass) != nullptr;
	});
	Sequence.RefreshCacheData();

	if (!UAnimationBlueprintLibrary::IsValidAnimNotifyTrackName(&Sequence, ControlTrackName))
	{
		UAnimationBlueprintLibrary::AddAnimationNotifyTrack(&Sequence, ControlTrackName, FLinearColor::White);
	}

	const float Length = Sequence.GetPlayLength();
	const float BlockStart = Role == EPMMAssetRole::Start ? 0.10f : 0.12f;
	const float BlockEnd = Role == EPMMAssetRole::Start
		? FMath::Max(BlockStart + PMMEndPadding, Length - PMMStartBlockTailWindow)
		: FMath::Max(BlockStart + PMMEndPadding, Length - 0.05f);
	UAnimNotifyState* BlockState = UAnimationBlueprintLibrary::AddAnimationNotifyStateEvent(
		&Sequence,
		ControlTrackName,
		BlockStart,
		BlockEnd - BlockStart,
		UAnimNotifyState_PoseSearchBlockTransition::StaticClass());

	// Start may first be chosen during the controller's deadzone-crossing sample
	// and need to yield once the stick reaches Walk/Run/Sprint magnitude a few
	// frames later. Do not retain its continuing-cost override for the whole
	// semantic Start curve; the curve itself already makes a real Start beat a
	// Loop while the input intent remains positive.
	const float ContinuingEnd = Role == EPMMAssetRole::Start
		? FMath::Clamp(PMMStartContinuingWindow, PMMEndPadding, Length - PMMEndPadding)
		: FMath::Max(PMMEndPadding, Length - 0.08f);
	UAnimNotifyState_PoseSearchOverrideContinuingPoseCostBias* ContinuingState = Cast<UAnimNotifyState_PoseSearchOverrideContinuingPoseCostBias>(
		UAnimationBlueprintLibrary::AddAnimationNotifyStateEvent(
			&Sequence,
			ControlTrackName,
			0.0f,
			ContinuingEnd,
			UAnimNotifyState_PoseSearchOverrideContinuingPoseCostBias::StaticClass()));
	if (ContinuingState)
	{
		ContinuingState->CostAddend = ContinuingModifier;
	}

	Sequence.SortNotifies();
	Sequence.RefreshCacheData();
	return BlockState != nullptr && ContinuingState != nullptr;
}

bool SaveSequence(UAnimSequenceBase& Sequence)
{
	UPackage* Package = Sequence.GetOutermost();
	FString Filename;
	if (!FPackageName::TryConvertLongPackageNameToFilename(Package->GetName(), Filename, FPackageName::GetAssetPackageExtension()))
	{
		return false;
	}

	Package->MarkPackageDirty();
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	return UPackage::SavePackage(Package, &Sequence, *Filename, SaveArgs);
}

bool ConfigureSequence(UAnimSequenceBase& Sequence, const FPMMAssetSpec& Spec,
	const float StartContinuingModifier, const float StopContinuingModifier)
{
	const float Length = Sequence.GetPlayLength();
	if (Length <= PMMEndPadding)
	{
		return false;
	}

	const bool bLooping = Spec.Role == EPMMAssetRole::Idle || Spec.Role == EPMMAssetRole::Loop;
	Sequence.bLoop = bLooping;

	bool bSuccess = true;
	const float ContinuingModifier = Spec.Role == EPMMAssetRole::Start
		? StartContinuingModifier
		: StopContinuingModifier;
	if (Spec.Role == EPMMAssetRole::Start)
	{
		const float IntentEnd = FMath::Clamp(Spec.StartIntentDuration,
			PMMEndPadding, Length - PMMEndPadding);
		bSuccess &= ReplaceFloatCurve(Sequence, TEXT("MM_Intent"),
			// Start is a finite edge request.  Once this reaches zero, the currently
			// selected Start may continue if Pose/Trajectory support it, but a new
			// Start from its early frames no longer wins merely for matching +1.
			{ 0.0f, IntentEnd }, { 1.0f, 0.0f });
	}
	else if (Spec.Role == EPMMAssetRole::Stop)
	{
		bSuccess &= ReplaceFloatCurve(Sequence, TEXT("MM_Intent"), { 0.0f, Length }, { -1.0f, 0.0f });
		bSuccess &= RenameStopDistanceCurve(Sequence);
	}
	else
	{
		bSuccess &= ReplaceFloatCurve(Sequence, TEXT("MM_Intent"), { 0.0f, Length }, { 0.0f, 0.0f });
	}

	if (Spec.Role != EPMMAssetRole::Stop)
	{
		bSuccess &= ReplaceFloatCurve(Sequence, TEXT("MM_DistanceToStop"), { 0.0f, Length }, { 0.0f, 0.0f });
	}
	else
	{
		bSuccess &= ConfigurePoseSearchControlNotifies(Sequence, Spec.Role, ContinuingModifier);
	}

	if (Spec.Role == EPMMAssetRole::Start)
	{
		bSuccess &= ConfigurePoseSearchControlNotifies(Sequence, Spec.Role, ContinuingModifier);
	}

	return bSuccess;
}

bool ConfigureContinuingModifierOnly(UAnimSequenceBase& Sequence, const float ContinuingModifier)
{
	bool bFoundContinuingState = false;
	for (FAnimNotifyEvent& Notify : Sequence.Notifies)
	{
		if (UAnimNotifyState_PoseSearchOverrideContinuingPoseCostBias* ContinuingState =
			Cast<UAnimNotifyState_PoseSearchOverrideContinuingPoseCostBias>(Notify.NotifyStateClass))
		{
			ContinuingState->CostAddend = ContinuingModifier;
			bFoundContinuingState = true;
		}
	}

	if (bFoundContinuingState)
	{
		Sequence.RefreshCacheData();
	}
	return bFoundContinuingState;
}
}

#endif

UMHGZPMMAssetFixupCommandlet::UMHGZPMMAssetFixupCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UMHGZPMMAssetFixupCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	// Keep the commandlet input integer-only: UE's float parser follows the
	// current process locale, whereas an integer is unambiguous. Start gets no
	// special continuing bonus after its short 0.10 s handoff window: a stick
	// that reaches its final magnitude after the deadzone must be able to choose
	// the matching Run/Sprint Start. Stop retains the maximum approved bonus to
	// stay on its authored deceleration tail instead of chaining to another Stop.
	int32 StartContinuingBiasMagnitudeHundredths = 0;
	int32 StopContinuingBiasMagnitudeHundredths = 50;
	FParse::Value(*Params, TEXT("StartContinuingBiasMagnitudeHundredths="), StartContinuingBiasMagnitudeHundredths);
	FParse::Value(*Params, TEXT("StopContinuingBiasMagnitudeHundredths="), StopContinuingBiasMagnitudeHundredths);
	const float StartContinuingModifier = -0.01f * static_cast<float>(StartContinuingBiasMagnitudeHundredths);
	const float StopContinuingModifier = -0.01f * static_cast<float>(StopContinuingBiasMagnitudeHundredths);
	if (StartContinuingModifier < -0.25f || StartContinuingModifier > 0.0f
		|| StopContinuingModifier < -0.5f || StopContinuingModifier > -0.25f)
	{
		UE_LOG(LogTemp, Error, TEXT("[PMMAssetFixup] Start/Stop continuing modifiers %.3f / %.3f are outside the supported ranges [-0.25, 0.00] / [-0.50, -0.25]."),
			StartContinuingModifier, StopContinuingModifier);
		return 1;
	}

	const bool bOnlyContinuingModifier = FParse::Param(*Params, TEXT("OnlyContinuingModifier"));
	UE_LOG(LogTemp, Display, TEXT("[PMMAssetFixup] Applying Start/Stop ContinuingModifier %.3f / %.3f (OnlyContinuingModifier=%d)."),
		StartContinuingModifier, StopContinuingModifier, bOnlyContinuingModifier ? 1 : 0);
	int32 FailureCount = 0;
	for (const FPMMAssetSpec& Spec : GetAssetSpecs())
	{
		UAnimSequenceBase* Sequence = LoadObject<UAnimSequenceBase>(nullptr, Spec.ObjectPath);
		if (!Sequence)
		{
			UE_LOG(LogTemp, Error, TEXT("[PMMAssetFixup] Could not load %s"), Spec.ObjectPath);
			++FailureCount;
			continue;
		}

		const bool bRequiresPoseSearchControl = Spec.Role == EPMMAssetRole::Start
			|| Spec.Role == EPMMAssetRole::Stop;
		if (bOnlyContinuingModifier && !bRequiresPoseSearchControl)
		{
			continue;
		}

		const float ContinuingModifier = Spec.Role == EPMMAssetRole::Start
			? StartContinuingModifier
			: StopContinuingModifier;
		const bool bConfigured = bOnlyContinuingModifier
			? ConfigureContinuingModifierOnly(*Sequence, ContinuingModifier)
			: ConfigureSequence(*Sequence, Spec, StartContinuingModifier, StopContinuingModifier);
		if (!bConfigured || !SaveSequence(*Sequence))
		{
			UE_LOG(LogTemp, Error, TEXT("[PMMAssetFixup] Failed to configure or save %s"), *Sequence->GetPathName());
			++FailureCount;
			continue;
		}

		UE_LOG(LogTemp, Display, TEXT("[PMMAssetFixup] Updated %s"), *Sequence->GetPathName());
	}

	return FailureCount == 0 ? 0 : 1;
#else
	UE_LOG(LogTemp, Error, TEXT("[PMMAssetFixup] This commandlet requires an editor build."));
	return 1;
#endif
}
