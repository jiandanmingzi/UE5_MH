// Copyright MHGZ Project. All Rights Reserved.

#include "Commandlets/MHGZM5DescendingThrustAssetSetupCommandlet.h"

#if WITH_EDITOR

#include "ActionSystem/AnimNotify_DescendingThrustEvent.h"
#include "ActionSystem/AnimNotifyState_AttackCollision.h"
#include "ActionSystem/AnimNotifyState_ComboWindow.h"
#include "ActionSystem/MHGZAttackAbility.h"
#include "ActionSystem/MHGZDamageGameplayEffect.h"
#include "ActionSystem/MHGZDescendingThrustAbility.h"
#include "ActionSystem/MHGZKinsectSlashAbility.h"
#include "ActionSystem/MHGZWeaponComboData.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "AnimationBlueprintLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Curves/CurveVector.h"
#include "Engine/Blueprint.h"
#include "Engine/SkeletalMesh.h"
#include "GameplayEffect.h"
#include "InsectGlaive/InsectGlaiveCombatConfig.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "WeaponRuntime/MHGZWeaponInputProfile.h"
#include "WeaponRuntime/MHGZWeaponRuntimeDefinition.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <bcrypt.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace UE::MHGZ::DescendingThrustAssetSetup
{
constexpr TCHAR Source380Path[] = TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_JiXiTuCi_StartAndFalling.AS_Unsh_JiXiTuCi_StartAndFalling");
constexpr TCHAR Source381Path[] = TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_JiXiTuCi_Over.AS_Unsh_JiXiTuCi_Over");
constexpr TCHAR GeneratedSequenceDir[] = TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Generated");
constexpr TCHAR MainMontagePackage[] = TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_JiXiTuCi");
constexpr TCHAR OverMontagePackage[] = TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_JiXiTuCi_Over");
constexpr TCHAR AbilityPackage[] = TEXT("/Game/Blueprints/Ability/InsectGlaive/GA_IG_JiXiTuCi");
constexpr TCHAR CurvePackage[] = TEXT("/Game/Weapons/InsectGlaive/Data/Curves/CV_IG_DescendingThrust_AirBasis");
constexpr TCHAR CombatConfigPath[] = TEXT("/Game/Weapons/InsectGlaive/Data/DA_IG_Combat.DA_IG_Combat");
constexpr TCHAR RuntimeDefinitionPath[] = TEXT("/Game/Weapons/InsectGlaive/Data/DA_WeaponRuntime_IG.DA_WeaponRuntime_IG");
constexpr TCHAR ComboDataPath[] = TEXT("/Game/Weapons/InsectGlaive/Data/DA_IG_Combo.DA_IG_Combo");
constexpr TCHAR FeiYuanAbilityPath[] = TEXT("/Game/Blueprints/Ability/InsectGlaive/GA_IG_FeiYuan.GA_IG_FeiYuan");
constexpr TCHAR SlashAbilityPath[] = TEXT("/Game/Blueprints/Ability/InsectGlaive/GA_IG_CaoChongZhan.GA_IG_CaoChongZhan");
constexpr TCHAR CurveCsvRelativePath[] = TEXT("MHWAnalysis/20261003_155024_02/LMT380_pooled_parameter_curve.csv");
constexpr TCHAR Source380RelativeFilename[] = TEXT("Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_JiXiTuCi_StartAndFalling.uasset");
constexpr TCHAR Source381RelativeFilename[] = TEXT("Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_JiXiTuCi_Over.uasset");
constexpr TCHAR Source380SHA256[] = TEXT("757F3F59E703C4DC37B8C05F3E851CC311F263EEBA536A4C3058F687F1F81BD6");
constexpr TCHAR Source381SHA256[] = TEXT("DC2685698143F07AA845825B4117932BD5CDB14F54E3EDAE8437DA2DD479565A");
constexpr TCHAR CurveCsvSHA256[] = TEXT("970F28E2EBA1C69396F69A9B662C0057BBD0CE64ED94D8CCD43782EEA9C46ABD");
constexpr TCHAR DefaultSlotName[] = TEXT("DefaultSlot");
constexpr TCHAR NotifyTracks[][16] = { TEXT("Attack"), TEXT("Action"), TEXT("Window") };
constexpr TCHAR InputRTName[] = TEXT("Input.Weapon.RT");
constexpr TCHAR PhysicalRTName[] = TEXT("Input.Modifier.RT");
constexpr TCHAR UnsheathedName[] = TEXT("Combat.State.Unsheathed");
constexpr TCHAR SheathedName[] = TEXT("Combat.State.Sheathed");
constexpr TCHAR GroundedName[] = TEXT("Combat.State.Grounded");
constexpr TCHAR FallingName[] = TEXT("Combat.State.Aerial.Falling");
constexpr TCHAR CantAttackName[] = TEXT("Combat.State.Aerial.CantAttack");
constexpr TCHAR DeadName[] = TEXT("Combat.State.Dead");
constexpr TCHAR HitstunName[] = TEXT("Combat.State.Hitstun");
constexpr TCHAR KnockdownName[] = TEXT("Combat.State.Knockdown");
constexpr TCHAR AerialStateName[] = TEXT("IG.Aerial.DescendingThrust");
constexpr float UnitToCentimeters = 1.0f;
constexpr float MainStartEndSourceFrame = 39.0f;
constexpr int32 LoopStartSourceFrame = 39;
constexpr float Correction3SourceFrame = 59.0f;
constexpr float Correction3MaxDegrees = 60.0f;
constexpr float ProvisionalMotionValue = 10.0f;
constexpr float BlendInTime = 0.05f;
constexpr float BlendOutTime = 0.10f;
constexpr float MinContributorCount = 15.0f;
constexpr float TimeGrid = 1.0f / 120.0f;

struct FCurveSample
{
	float Time = 0.0f;
	float HorizontalTimeFactor = 0.0f;
	float UpUnits = 0.0f;
	float VerticalVelocity = 0.0f;
	float HorizontalVelocityFactor = 0.0f;
	int32 Contributors = 0;
};

struct FSetupRun
{
	bool bApply = false;
	TArray<FString> Errors;
	TArray<FString> ChangedAssets;
	TArray<FString> PendingAssets;
	TArray<FCurveSample> CurveSamples;
	int32 CurveCoverageIndex = INDEX_NONE;
	FVector TerminalVelocity = FVector::ZeroVector;
	FVector TerminalAcceleration = FVector::ZeroVector;
	FRecordedLocalTrajectoryProfile ExpectedProfile;
	bool bAbilityConfigurationChanged = false;
	bool bComboConfigurationChanged = false;
	UAnimSequence* Source380 = nullptr;
	UAnimSequence* Source381 = nullptr;
	UAnimSequence* StartSequence = nullptr;
	UAnimSequence* LoopSequence = nullptr;
	UAnimSequence* OverSequence = nullptr;
	UCurveVector* Curve = nullptr;
	UAnimMontage* MainMontage = nullptr;
	UAnimMontage* OverMontage = nullptr;
	UBlueprint* AbilityBlueprint = nullptr;
	UMHGZDescendingThrustAbility* Ability = nullptr;
	UInsectGlaiveCombatConfig* Combat = nullptr;
	UWeaponRuntimeDefinition* Runtime = nullptr;
	UWeaponInputProfile* InputProfile = nullptr;
	UMHGZWeaponComboData* Combo = nullptr;
	UBlueprint* FeiYuanBlueprint = nullptr;
	UMHGZAttackAbility* FeiYuanAbility = nullptr;
};

FString ObjectPath(const TCHAR* Package, const TCHAR* Asset)
{
	return FString::Printf(TEXT("%s.%s"), Package, Asset);
}

bool SaveAsset(UObject& Asset)
{
	UPackage* Package = Asset.GetOutermost();
	FString Filename;
	if (!Package || !FPackageName::TryConvertLongPackageNameToFilename(
		Package->GetName(), Filename, FPackageName::GetAssetPackageExtension()))
	{
		return false;
	}
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
	FSavePackageArgs Args;
	Args.TopLevelFlags = RF_Public | RF_Standalone;
	return UPackage::SavePackage(Package, &Asset, *Filename, Args);
}

FGameplayTag Tag(const TCHAR* Name)
{
	return FGameplayTag::RequestGameplayTag(FName(Name), false);
}

bool ParseFloat(const TArray<FString>& Cells, const TMap<FString, int32>& Columns,
	const TCHAR* Name, float& OutValue)
{
	const int32* Index = Columns.Find(Name);
	if (!Index || !Cells.IsValidIndex(*Index) || Cells[*Index].IsEmpty())
	{
		return false;
	}
	OutValue = FCString::Atof(*Cells[*Index]);
	return FMath::IsFinite(OutValue);
}

bool MatchesSHA256(const FString& Filename, const TCHAR* Expected)
{
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *Filename) || Bytes.IsEmpty())
	{
		return false;
	}
#if PLATFORM_WINDOWS
	BCRYPT_ALG_HANDLE Algorithm = nullptr;
	BCRYPT_HASH_HANDLE HashHandle = nullptr;
	TArray<uint8> HashObject;
	uint8 Digest[32] = {};
	DWORD HashObjectLength = 0;
	DWORD ResultLength = 0;
	bool bHasDigest = false;
	if (BCryptOpenAlgorithmProvider(&Algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0
		&& BCryptGetProperty(Algorithm, BCRYPT_OBJECT_LENGTH,
			reinterpret_cast<PUCHAR>(&HashObjectLength), sizeof(HashObjectLength),
			&ResultLength, 0) >= 0)
	{
		HashObject.SetNumUninitialized(static_cast<int32>(HashObjectLength));
		if (BCryptCreateHash(Algorithm, &HashHandle, HashObject.GetData(), HashObjectLength,
			nullptr, 0, 0) >= 0
			&& BCryptHashData(HashHandle, Bytes.GetData(), static_cast<ULONG>(Bytes.Num()), 0) >= 0
			&& BCryptFinishHash(HashHandle, Digest, UE_ARRAY_COUNT(Digest), 0) >= 0)
		{
			bHasDigest = true;
		}
	}
	if (HashHandle)
	{
		BCryptDestroyHash(HashHandle);
	}
	if (Algorithm)
	{
		BCryptCloseAlgorithmProvider(Algorithm, 0);
	}
	if (!bHasDigest)
	{
		return false;
	}
	FString HexDigest;
	HexDigest.Reserve(UE_ARRAY_COUNT(Digest) * 2);
	for (const uint8 Byte : Digest)
	{
		HexDigest += FString::Printf(TEXT("%02X"), Byte);
	}
	return HexDigest.Equals(Expected, ESearchCase::IgnoreCase);
#else
	return false;
#endif
}

bool ValidateFrozenInputHashes(FSetupRun& Run)
{
	const FString Source380Filename = FPaths::ProjectContentDir() / Source380RelativeFilename;
	const FString Source381Filename = FPaths::ProjectContentDir() / Source381RelativeFilename;
	if (!MatchesSHA256(Source380Filename, Source380SHA256))
	{
		Run.Errors.Add(TEXT("Imported LMT380 sequence file hash differs from the M1-frozen source manifest."));
	}
	if (!MatchesSHA256(Source381Filename, Source381SHA256))
	{
		Run.Errors.Add(TEXT("Imported LMT381 sequence file hash differs from the M1-frozen source manifest."));
	}
	return Run.Errors.IsEmpty();
}

bool ReadCurveSamples(FSetupRun& Run)
{
	const FString Filename = FPaths::ProjectSavedDir() / CurveCsvRelativePath;
	if (!MatchesSHA256(Filename, CurveCsvSHA256))
	{
		Run.Errors.Add(TEXT("The pooled LMT380 curve CSV does not match the M1-frozen SHA256."));
		return false;
	}
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Filename))
	{
		Run.Errors.Add(TEXT("Could not read the frozen M1 pooled LMT380 curve CSV: ") + Filename);
		return false;
	}
	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines, false);
	if (Lines.Num() < 100)
	{
		Run.Errors.Add(TEXT("The M1 curve CSV is unexpectedly short."));
		return false;
	}
	TArray<FString> Headers;
	Lines[0].ParseIntoArray(Headers, TEXT(","), false);
	TMap<FString, int32> Columns;
	for (int32 Index = 0; Index < Headers.Num(); ++Index)
	{
		Headers[Index].TrimStartAndEndInline();
		Columns.Add(Headers[Index], Index);
	}
	const TCHAR* RequiredColumns[] = {
		TEXT("time_s"), TEXT("contributors"), TEXT("horizontal_time_factor_s"),
		TEXT("up_units"), TEXT("vertical_velocity_units_s"),
		TEXT("horizontal_velocity_factor") };
	for (const TCHAR* Column : RequiredColumns)
	{
		if (!Columns.Contains(Column))
		{
			Run.Errors.Add(FString::Printf(TEXT("M1 curve CSV is missing column %s."), Column));
			return false;
		}
	}

	for (int32 LineIndex = 1; LineIndex < Lines.Num(); ++LineIndex)
	{
		if (Lines[LineIndex].TrimStartAndEnd().IsEmpty())
		{
			continue;
		}
		TArray<FString> Cells;
		Lines[LineIndex].ParseIntoArray(Cells, TEXT(","), false);
		FCurveSample Sample;
		float Contributors = 0.0f;
		if (!ParseFloat(Cells, Columns, TEXT("time_s"), Sample.Time)
			|| !ParseFloat(Cells, Columns, TEXT("contributors"), Contributors)
			|| !ParseFloat(Cells, Columns, TEXT("horizontal_time_factor_s"), Sample.HorizontalTimeFactor)
			|| !ParseFloat(Cells, Columns, TEXT("up_units"), Sample.UpUnits))
		{
			Run.Errors.Add(FString::Printf(TEXT("Malformed M1 curve CSV core values at row %d."), LineIndex + 1));
			return false;
		}
		// The pooled CSV intentionally leaves derivative columns empty at t=0.
		// Position is exact there; use an explicit zero derivative placeholder.
		if (FMath::IsNearlyZero(Sample.Time))
		{
			Sample.VerticalVelocity = 0.0f;
			Sample.HorizontalVelocityFactor = 0.0f;
		}
		else if (!ParseFloat(Cells, Columns, TEXT("vertical_velocity_units_s"), Sample.VerticalVelocity)
			|| !ParseFloat(Cells, Columns, TEXT("horizontal_velocity_factor"), Sample.HorizontalVelocityFactor))
		{
			Run.Errors.Add(FString::Printf(TEXT("Malformed M1 curve CSV derivative values at row %d."), LineIndex + 1));
			return false;
		}
		Sample.Contributors = FMath::RoundToInt(Contributors);
		if (Sample.Time < 0.0f || Sample.Contributors <= 0
			|| (!Run.CurveSamples.IsEmpty()
				&& Sample.Time <= Run.CurveSamples.Last().Time))
		{
			Run.Errors.Add(FString::Printf(TEXT("M1 curve CSV row %d has invalid ordering or contributor count."), LineIndex + 1));
			return false;
		}
		Run.CurveSamples.Add(Sample);
	}
	if (Run.CurveSamples.Num() < 100
		|| !FMath::IsNearlyZero(Run.CurveSamples[0].Time)
		|| !FMath::IsNearlyZero(Run.CurveSamples[0].HorizontalTimeFactor)
		|| !FMath::IsNearlyZero(Run.CurveSamples[0].UpUnits))
	{
		Run.Errors.Add(TEXT("M1 curve needs at least 100 ordered samples and an origin key at (0,0,0)."));
		return false;
	}
	for (int32 Index = 0; Index < Run.CurveSamples.Num(); ++Index)
	{
		if (Run.CurveSamples[Index].Contributors >= MinContributorCount)
		{
			Run.CurveCoverageIndex = Index;
		}
	}
	if (Run.CurveCoverageIndex < 12)
	{
		Run.Errors.Add(TEXT("No adequately supported M1 trajectory tail is available."));
		return false;
	}

	// Fit terminal acceleration from the final 12 adequately-supported velocity
	// samples. The last five-contributor row is intentionally not extrapolated.
	const int32 FitCount = FMath::Min(12, Run.CurveCoverageIndex + 1);
	const int32 FirstFit = Run.CurveCoverageIndex + 1 - FitCount;
	double MeanT = 0.0;
	double MeanHorizontal = 0.0;
	double MeanVertical = 0.0;
	for (int32 Index = FirstFit; Index <= Run.CurveCoverageIndex; ++Index)
	{
		MeanT += Run.CurveSamples[Index].Time;
		MeanHorizontal += Run.CurveSamples[Index].HorizontalVelocityFactor;
		MeanVertical += Run.CurveSamples[Index].VerticalVelocity;
	}
	MeanT /= FitCount;
	MeanHorizontal /= FitCount;
	MeanVertical /= FitCount;
	double Denominator = 0.0;
	double HorizontalNumerator = 0.0;
	double VerticalNumerator = 0.0;
	for (int32 Index = FirstFit; Index <= Run.CurveCoverageIndex; ++Index)
	{
		const double DT = Run.CurveSamples[Index].Time - MeanT;
		Denominator += DT * DT;
		HorizontalNumerator += DT * (Run.CurveSamples[Index].HorizontalVelocityFactor - MeanHorizontal);
		VerticalNumerator += DT * (Run.CurveSamples[Index].VerticalVelocity - MeanVertical);
	}
	if (Denominator <= SMALL_NUMBER)
	{
		Run.Errors.Add(TEXT("Cannot fit a finite M1 trajectory terminal acceleration."));
		return false;
	}
	const FCurveSample& End = Run.CurveSamples[Run.CurveCoverageIndex];
	Run.TerminalVelocity = FVector(End.HorizontalVelocityFactor,
		0.0f, End.VerticalVelocity) * UnitToCentimeters;
	Run.TerminalAcceleration = FVector(HorizontalNumerator / Denominator,
		0.0f, VerticalNumerator / Denominator) * UnitToCentimeters;
	Run.ExpectedProfile.CoveredSeconds = End.Time;
	Run.ExpectedProfile.TerminalVelocityLocal = Run.TerminalVelocity;
	Run.ExpectedProfile.TerminalAccelerationLocal = Run.TerminalAcceleration;
	return true;
}

float ReadCurveComponent(const UCurveVector& Curve, const int32 Axis, const float Time)
{
	return Curve.FloatCurves[Axis].Eval(Time);
}

bool CurveMatches(const UCurveVector& Curve, const FSetupRun& Run)
{
	if (Run.CurveCoverageIndex == INDEX_NONE)
	{
		return false;
	}
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const TArray<FRichCurveKey>& Keys = Curve.FloatCurves[Axis].Keys;
		if (Keys.Num() != Run.CurveCoverageIndex + 1)
		{
			return false;
		}
	}
	for (int32 Index = 0; Index <= Run.CurveCoverageIndex; ++Index)
	{
		const FCurveSample& Sample = Run.CurveSamples[Index];
		const float Values[] = { Sample.HorizontalTimeFactor,
			0.0f, Sample.UpUnits * UnitToCentimeters };
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const FRichCurveKey& Key = Curve.FloatCurves[Axis].Keys[Index];
			if (!FMath::IsNearlyEqual(Key.Time, Sample.Time, 0.00001f)
				|| !FMath::IsNearlyEqual(Key.Value, Values[Axis], 0.001f))
			{
				return false;
			}
		}
	}
	return true;
}

void FillCurve(UCurveVector& Curve, const FSetupRun& Run)
{
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		Curve.FloatCurves[Axis].Reset();
		Curve.FloatCurves[Axis].PreInfinityExtrap = RCCE_Constant;
		Curve.FloatCurves[Axis].PostInfinityExtrap = RCCE_Constant;
	}
	for (int32 Index = 0; Index <= Run.CurveCoverageIndex; ++Index)
	{
		const FCurveSample& Sample = Run.CurveSamples[Index];
		const float Values[] = { Sample.HorizontalTimeFactor,
			0.0f, Sample.UpUnits * UnitToCentimeters };
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const FKeyHandle Handle = Curve.FloatCurves[Axis].AddKey(Sample.Time, Values[Axis]);
			Curve.FloatCurves[Axis].SetKeyInterpMode(Handle, RCIM_Linear);
		}
	}
}

bool SavePackageAsset(UObject& Asset, FSetupRun& Run)
{
	UPackage* Package = Asset.GetOutermost();
	const FString AssetPath = Asset.GetPathName();
	const bool bCreatedThisRun = Run.ChangedAssets.Contains(AssetPath);
	if (!Package)
	{
		Run.Errors.Add(TEXT("Asset has no outer package: ") + AssetPath);
		return false;
	}
	// Do not force-save clean packages: an unchanged M3 rerun must leave both
	// package bytes and later editor-authored Montage notify windows untouched.
	if (!Package->IsDirty() && !bCreatedThisRun)
	{
		return true;
	}
	if (bCreatedThisRun && !Package->IsDirty())
	{
		Package->MarkPackageDirty();
	}
	if (!SaveAsset(Asset))
	{
		Run.Errors.Add(TEXT("Failed to save asset: ") + AssetPath);
		return false;
	}
	Run.ChangedAssets.AddUnique(AssetPath);
	return true;
}

UObject* LoadObjectByPath(const FString& Path)
{
	return StaticLoadObject(UObject::StaticClass(), nullptr, *Path, nullptr, LOAD_NoWarn);
}

bool GetOrCreateCurve(FSetupRun& Run)
{
	const FString Path = ObjectPath(CurvePackage, TEXT("CV_IG_DescendingThrust_AirBasis"));
	Run.Curve = LoadObject<UCurveVector>(nullptr, *Path, nullptr, LOAD_NoWarn);
	UObject* Existing = LoadObjectByPath(Path);
	if (Existing && !Run.Curve)
	{
		Run.Errors.Add(TEXT("The canonical curve path exists with an incompatible asset class."));
		return false;
	}
	if (!Run.Curve && Run.bApply)
	{
		UPackage* Package = CreatePackage(CurvePackage);
		Run.Curve = Package ? NewObject<UCurveVector>(Package,
			FName(TEXT("CV_IG_DescendingThrust_AirBasis")), RF_Public | RF_Standalone) : nullptr;
		if (!Run.Curve)
		{
			Run.Errors.Add(TEXT("Could not create the Descending Thrust curve asset."));
			return false;
		}
		FAssetRegistryModule::AssetCreated(Run.Curve);
		FillCurve(*Run.Curve, Run);
	}
	if (Run.Curve && !CurveMatches(*Run.Curve, Run))
	{
		Run.Errors.Add(TEXT("Existing Descending Thrust curve differs from the signed M1 curve; preserving it and refusing overwrite."));
		return false;
	}
	if (Run.Curve)
	{
		Run.ExpectedProfile.Curve = Run.Curve;
	}
	return Run.bApply ? Run.Curve != nullptr : true;
}

bool SampleBone(const UAnimSequence& Sequence, const int32 BoneIndex,
	const FFrameRate& Rate, const float SourceFrame, FTransform& Out)
{
	if (BoneIndex == INDEX_NONE || SourceFrame < 0.0f)
	{
		return false;
	}
	Sequence.GetBoneTransform(Out, FSkeletonPoseBoneIndex(BoneIndex),
		FAnimExtractContext(SourceFrame / Rate.AsDecimal()), true);
	return true;
}

bool SequenceHasNoAuthoredCurvesOrNotifies(const UAnimSequence& Sequence)
{
	if (!Sequence.Notifies.IsEmpty())
	{
		return false;
	}
	TArray<FName> CurveNames;
	UAnimationBlueprintLibrary::GetAnimationCurveNames(&Sequence,
		ERawCurveTrackTypes::RCT_Float, CurveNames);
	if (!CurveNames.IsEmpty())
	{
		return false;
	}
	UAnimationBlueprintLibrary::GetAnimationCurveNames(&Sequence,
		ERawCurveTrackTypes::RCT_Vector, CurveNames);
	if (!CurveNames.IsEmpty())
	{
		return false;
	}
	return true;
}

bool SequenceMatchesCrop(const UAnimSequence& Output, const UAnimSequence& Source,
	const int32 FirstSourceFrame, const int32 LastSourceFrame,
	const bool bEnableRootMotion)
{
	const IAnimationDataModel* SourceModel = Source.GetDataModel();
	const IAnimationDataModel* OutputModel = Output.GetDataModel();
	if (!SourceModel || !OutputModel || Output.GetSkeleton() != Source.GetSkeleton()
		|| OutputModel->GetNumberOfFrames() + 1 != LastSourceFrame - FirstSourceFrame + 1
		|| OutputModel->GetFrameRate() != SourceModel->GetFrameRate()
		|| !FMath::IsNearlyEqual(Output.RateScale, 1.0f)
		|| Output.bEnableRootMotion != bEnableRootMotion
		|| !SequenceHasNoAuthoredCurvesOrNotifies(Output))
	{
		return false;
	}
	TArray<FName> Tracks;
	SourceModel->GetBoneTrackNames(Tracks);
	const FReferenceSkeleton& RefSkeleton = Source.GetSkeleton()->GetReferenceSkeleton();
	for (const FName Track : Tracks)
	{
		const int32 BoneIndex = RefSkeleton.FindBoneIndex(Track);
		if (BoneIndex == INDEX_NONE)
		{
			return false;
		}
		for (int32 OutFrame = 0; OutFrame <= LastSourceFrame - FirstSourceFrame; ++OutFrame)
		{
			FTransform Expected;
			FTransform Actual;
			if (!SampleBone(Source, BoneIndex, SourceModel->GetFrameRate(),
				static_cast<float>(FirstSourceFrame + OutFrame), Expected))
			{
				return false;
			}
			Output.GetBoneTransform(Actual, FSkeletonPoseBoneIndex(BoneIndex),
				FAnimExtractContext(static_cast<float>(OutFrame)
					/ OutputModel->GetFrameRate().AsDecimal()), true);
			if (!Expected.Equals(Actual, 0.002f))
			{
				return false;
			}
		}
	}
	return true;
}

bool BuildSequence(UAnimSequence& Output, const UAnimSequence& Source,
	const int32 FirstSourceFrame, const int32 LastSourceFrame,
	const bool bEnableRootMotion)
{
	const IAnimationDataModel* SourceModel = Source.GetDataModel();
	if (!SourceModel || !Source.GetSkeleton()
		|| !SequenceHasNoAuthoredCurvesOrNotifies(Source)
		|| FirstSourceFrame < 0 || LastSourceFrame <= FirstSourceFrame
		|| LastSourceFrame >= SourceModel->GetNumberOfFrames() + 1)
	{
		return false;
	}
	TArray<FName> Tracks;
	SourceModel->GetBoneTrackNames(Tracks);
	if (Tracks.IsEmpty())
	{
		return false;
	}
	Output.Modify();
	Output.SetSkeleton(Source.GetSkeleton());
	Output.bLoop = false;
	Output.bEnableRootMotion = bEnableRootMotion;
	Output.bForceRootLock = false;
	Output.RootMotionRootLock = Source.RootMotionRootLock;
	Output.bUseNormalizedRootMotionScale = Source.bUseNormalizedRootMotionScale;
	Output.RateScale = 1.0f;
	Output.GetController().InitializeModel();
	Output.ResetAnimation();
	IAnimationDataController& Controller = Output.GetController();
	Controller.OpenBracket(FText::FromString(TEXT("Build M5 Descending Thrust Sequence")), false);
	Controller.SetFrameRate(SourceModel->GetFrameRate(), false);
	Controller.SetNumberOfFrames(FFrameNumber(LastSourceFrame - FirstSourceFrame), false);
	const FReferenceSkeleton& RefSkeleton = Source.GetSkeleton()->GetReferenceSkeleton();
	for (const FName Track : Tracks)
	{
		const int32 BoneIndex = RefSkeleton.FindBoneIndex(Track);
		if (BoneIndex == INDEX_NONE || !Controller.AddBoneCurve(Track, false))
		{
			Controller.CloseBracket(false);
			return false;
		}
		TArray<FVector3f> Positions;
		TArray<FQuat4f> Rotations;
		TArray<FVector3f> Scales;
		for (int32 Frame = FirstSourceFrame; Frame <= LastSourceFrame; ++Frame)
		{
			FTransform Transform;
			if (!SampleBone(Source, BoneIndex, SourceModel->GetFrameRate(),
				static_cast<float>(Frame), Transform))
			{
				Controller.CloseBracket(false);
				return false;
			}
			Positions.Add(FVector3f(Transform.GetTranslation()));
			Rotations.Add(FQuat4f(Transform.GetRotation()));
			Scales.Add(FVector3f(Transform.GetScale3D()));
		}
		if (!Controller.SetBoneTrackKeys(Track, Positions, Rotations, Scales, false))
		{
			Controller.CloseBracket(false);
			return false;
		}
	}
	Controller.NotifyPopulated();
	Controller.CloseBracket(false);
	Output.PostEditChange();
	Output.RefreshCacheData();
	return true;
}

UAnimSequence* GetOrCreateSequence(FSetupRun& Run, const TCHAR* Name,
	const UAnimSequence& Source, const int32 FirstFrame, const int32 LastFrame,
	const bool bRootMotion)
{
	const FString PackageName = FString::Printf(TEXT("%s/%s"), GeneratedSequenceDir, Name);
	const FString Path = ObjectPath(*PackageName, Name);
	UAnimSequence* Sequence = LoadObject<UAnimSequence>(nullptr, *Path, nullptr, LOAD_NoWarn);
	UObject* Existing = LoadObjectByPath(Path);
	if (Existing && !Sequence)
	{
		Run.Errors.Add(TEXT("Generated sequence path contains an incompatible class: ") + Path);
		return nullptr;
	}
	if (Sequence)
	{
		if (!SequenceMatchesCrop(*Sequence, Source, FirstFrame, LastFrame, bRootMotion))
		{
			Run.Errors.Add(TEXT("Existing generated sequence differs from its source crop; preserving it: ") + Path);
			return nullptr;
		}
		return Sequence;
	}
	if (!Run.bApply)
	{
		UE_LOG(LogTemp, Display, TEXT("[DescendingThrustAssets][Audit] Would create sequence %s."), *Path);
		return nullptr;
	}
	UPackage* Package = CreatePackage(*PackageName);
	Sequence = Package ? NewObject<UAnimSequence>(Package, FName(Name), RF_Public | RF_Standalone) : nullptr;
	if (!Sequence || !BuildSequence(*Sequence, Source, FirstFrame, LastFrame, bRootMotion))
	{
		Run.Errors.Add(TEXT("Could not bake source range into ") + Path);
		return nullptr;
	}
	FAssetRegistryModule::AssetCreated(Sequence);
	Run.ChangedAssets.Add(Path);
	return Sequence;
}

void AddSection(UAnimMontage& Montage, const FName Name, const float Time,
	const FName NextSection)
{
	FCompositeSection& Section = Montage.CompositeSections.AddDefaulted_GetRef();
	Section.SectionName = Name;
	Section.Link(&Montage, Time, 0);
	Section.SetTime(Time);
	Section.NextSectionName = NextSection;
}

bool MontageLayoutMatches(const UAnimMontage& Montage, const bool bMain,
	const UAnimSequence* Start, const UAnimSequence* Loop, const UAnimSequence* Over)
{
	if (Montage.SlotAnimTracks.Num() != 1 || Montage.SlotAnimTracks[0].SlotName != FName(DefaultSlotName))
	{
		return false;
	}
	const TArray<FAnimSegment>& Segments = Montage.SlotAnimTracks[0].AnimTrack.AnimSegments;
	if (bMain)
	{
		if (!Start || !Loop || Segments.Num() != 2 || Montage.CompositeSections.Num() != 2
			|| Segments[0].GetAnimReference() != Start || Segments[1].GetAnimReference() != Loop
			|| !FMath::IsNearlyEqual(Segments[0].StartPos, 0.0f)
			|| !FMath::IsNearlyEqual(Segments[1].StartPos, Start->GetPlayLength())
			|| !FMath::IsNearlyEqual(Segments[0].AnimPlayRate, 1.0f)
			|| !FMath::IsNearlyEqual(Segments[1].AnimPlayRate, 1.0f))
		{
			return false;
		}
		const int32 StartIndex = Montage.GetSectionIndex(TEXT("Start"));
		const int32 LoopIndex = Montage.GetSectionIndex(TEXT("FallingLoop"));
		if (StartIndex == INDEX_NONE || LoopIndex == INDEX_NONE)
		{
			return false;
		}
		const FCompositeSection& StartSection = Montage.GetAnimCompositeSection(StartIndex);
		const FCompositeSection& LoopSection = Montage.GetAnimCompositeSection(LoopIndex);
		return FMath::IsNearlyEqual(StartSection.GetTime(), 0.0f)
			&& FMath::IsNearlyEqual(LoopSection.GetTime(), Start->GetPlayLength())
			&& StartSection.NextSectionName == TEXT("FallingLoop")
			&& LoopSection.NextSectionName == TEXT("FallingLoop");
	}
	if (!Over || Segments.Num() != 1 || Montage.CompositeSections.Num() != 1
		|| Segments[0].GetAnimReference() != Over
		|| !FMath::IsNearlyEqual(Segments[0].StartPos, 0.0f)
		|| !FMath::IsNearlyEqual(Segments[0].AnimPlayRate, 1.0f))
	{
		return false;
	}
	const int32 SectionIndex = Montage.GetSectionIndex(TEXT("Over"));
	if (SectionIndex == INDEX_NONE)
	{
		return false;
	}
	const FCompositeSection& Section = Montage.GetAnimCompositeSection(SectionIndex);
	return FMath::IsNearlyZero(Section.GetTime()) && Section.NextSectionName.IsNone();
}

bool HasTrack(const UAnimMontage& Montage, const FName TrackName)
{
	return Montage.AnimNotifyTracks.ContainsByPredicate([TrackName](const FAnimNotifyTrack& Track)
	{
		return Track.TrackName == TrackName;
	});
}

bool EnsureNotifyTrack(UAnimMontage& Montage, const FName Name,
	const FLinearColor Color, bool& bOutChanged)
{
	if (!HasTrack(Montage, Name))
	{
		Montage.Modify();
		UAnimationBlueprintLibrary::AddAnimationNotifyTrack(&Montage, Name, Color);
		bOutChanged = true;
	}
	return HasTrack(Montage, Name);
}

bool ConfigureMontage(FSetupRun& Run, UAnimMontage& Montage,
	const bool bMain, UAnimSequence* Start, UAnimSequence* Loop, UAnimSequence* Over,
	USkeleton* Skeleton)
{
	const bool bLayoutMatches = MontageLayoutMatches(Montage, bMain, Start, Loop, Over);
	const bool bCreatedByThisRun = Run.ChangedAssets.Contains(Montage.GetPathName());
	bool bMontageChanged = false;
	if (!bLayoutMatches && !bCreatedByThisRun)
	{
		Run.Errors.Add(TEXT("Existing Montage layout is incompatible with the canonical M3 layout; preserving the asset: ") + Montage.GetPathName());
		return false;
	}
	if (!bLayoutMatches)
	{
		Montage.Modify();
		bMontageChanged = true;
		Montage.SetSkeleton(Skeleton);
		Montage.SlotAnimTracks.Reset();
		Montage.CompositeSections.Reset();
		FSlotAnimationTrack& Slot = Montage.SlotAnimTracks.AddDefaulted_GetRef();
		Slot.SlotName = FName(DefaultSlotName);
		const auto AddSegment = [&Slot](UAnimSequence& Sequence, const float StartTime)
		{
			FAnimSegment& Segment = Slot.AnimTrack.AnimSegments.AddDefaulted_GetRef();
			Segment.SetAnimReference(&Sequence, true);
			Segment.StartPos = StartTime;
			Segment.AnimStartTime = 0.0f;
			Segment.AnimEndTime = Sequence.GetPlayLength();
			Segment.AnimPlayRate = 1.0f;
			Segment.LoopingCount = 1;
		};
		if (bMain)
		{
			AddSegment(*Start, 0.0f);
			AddSegment(*Loop, Start->GetPlayLength());
			AddSection(Montage, TEXT("Start"), 0.0f, TEXT("FallingLoop"));
			AddSection(Montage, TEXT("FallingLoop"), Start->GetPlayLength(), TEXT("FallingLoop"));
		}
		else
		{
			AddSegment(*Over, 0.0f);
			AddSection(Montage, TEXT("Over"), 0.0f, NAME_None);
		}
		const float CalculatedLength = Montage.CalculateSequenceLength();
		if (!FMath::IsFinite(CalculatedLength) || CalculatedLength <= 0.0f)
		{
			Run.Errors.Add(TEXT("Montage composite length calculation failed: ") + Montage.GetPathName());
			return false;
		}
		Montage.SetCompositeLength(CalculatedLength);
		Montage.UpdateLinkableElements();
		Montage.bEnableAutoBlendOut = true;
		Montage.BlendIn.SetBlendTime(BlendInTime);
		Montage.BlendOut.SetBlendTime(BlendOutTime);
	}
	if (!EnsureNotifyTrack(Montage, FName(NotifyTracks[0]),
		FLinearColor(0.85f, 0.18f, 0.18f), bMontageChanged)
		|| !EnsureNotifyTrack(Montage, FName(NotifyTracks[1]),
			FLinearColor(0.18f, 0.65f, 0.95f), bMontageChanged)
		|| !EnsureNotifyTrack(Montage, FName(NotifyTracks[2]),
			FLinearColor(0.25f, 0.80f, 0.35f), bMontageChanged))
	{
		Run.Errors.Add(TEXT("Could not create the standard Attack/Action/Window notify tracks."));
		return false;
	}
	if (bMontageChanged)
	{
		Montage.RefreshCacheData();
	}
	return MontageLayoutMatches(Montage, bMain, Start, Loop, Over);
}

bool EnsureEvent(FSetupRun& Run, UAnimMontage& Montage,
	const float Time, const EDescendingThrustMontageEvent Kind, bool& bOutChanged)
{
	TArray<UAnimNotify_DescendingThrustEvent*> Matches;
	for (FAnimNotifyEvent& Event : Montage.Notifies)
	{
		if (UAnimNotify_DescendingThrustEvent* Notify =
			Cast<UAnimNotify_DescendingThrustEvent>(Event.Notify))
		{
			if (Notify->Event == Kind)
			{
				Matches.Add(Notify);
			}
		}
	}
	if (Matches.Num() > 1)
	{
		Run.Errors.Add(FString::Printf(TEXT("Montage contains duplicate owned event %s; preserving them for manual review."),
			*StaticEnum<EDescendingThrustMontageEvent>()->GetNameStringByValue(static_cast<int64>(Kind))));
		return false;
	}
	if (Matches.Num() == 1)
	{
		for (const FAnimNotifyEvent& Event : Montage.Notifies)
		{
			if (Event.Notify == Matches[0] && FMath::IsNearlyEqual(Event.GetTriggerTime(), Time, 0.001f))
			{
				return true;
			}
		}
		Run.Errors.Add(TEXT("Owned Descending Thrust event exists at a different authored time; preserving it: ") + Montage.GetPathName());
		return false;
	}
	if (!Run.bApply)
	{
		return true;
	}
	Montage.Modify();
	UAnimNotify_DescendingThrustEvent* Notify = Cast<UAnimNotify_DescendingThrustEvent>(
		UAnimationBlueprintLibrary::AddAnimationNotifyEvent(&Montage,
			FName(NotifyTracks[1]), Time, UAnimNotify_DescendingThrustEvent::StaticClass()));
	if (!Notify)
	{
		Run.Errors.Add(TEXT("Could not add Descending Thrust notify event to ") + Montage.GetPathName());
		return false;
	}
	Notify->Event = Kind;
	bOutChanged = true;
	return true;
}

UAnimMontage* GetOrCreateMontage(FSetupRun& Run, const TCHAR* Package,
	const TCHAR* Name, USkeleton* Skeleton)
{
	const FString Path = ObjectPath(Package, Name);
	UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *Path, nullptr, LOAD_NoWarn);
	UObject* Existing = LoadObjectByPath(Path);
	if (Existing && !Montage)
	{
		Run.Errors.Add(TEXT("Montage path contains an incompatible class: ") + Path);
		return nullptr;
	}
	if (!Montage && Run.bApply)
	{
		UPackage* AssetPackage = CreatePackage(Package);
		Montage = AssetPackage ? NewObject<UAnimMontage>(AssetPackage, FName(Name), RF_Public | RF_Standalone) : nullptr;
		if (Montage)
		{
			Montage->SetSkeleton(Skeleton);
			FAssetRegistryModule::AssetCreated(Montage);
			Run.ChangedAssets.Add(Path);
		}
	}
	if (Montage && Montage->GetSkeleton() != Skeleton)
	{
		Run.Errors.Add(TEXT("Montage Skeleton differs from the generated sequences: ") + Path);
		return nullptr;
	}
	return Montage;
}

UBlueprint* GetOrCreateAbilityBlueprint(FSetupRun& Run)
{
	const FString Path = ObjectPath(AbilityPackage, TEXT("GA_IG_JiXiTuCi"));
	Run.AbilityBlueprint = LoadObject<UBlueprint>(nullptr, *Path, nullptr, LOAD_NoWarn);
	UObject* Existing = LoadObjectByPath(Path);
	if (Existing && !Run.AbilityBlueprint)
	{
		Run.Errors.Add(TEXT("Ability path exists with an incompatible class: ") + Path);
		return nullptr;
	}
	if (Run.AbilityBlueprint)
	{
		if (!Run.AbilityBlueprint->GeneratedClass
			|| !Run.AbilityBlueprint->GeneratedClass->IsChildOf(UMHGZDescendingThrustAbility::StaticClass()))
		{
			Run.Errors.Add(TEXT("Existing GA_IG_JiXiTuCi is not a child of UMHGZDescendingThrustAbility."));
			return nullptr;
		}
		Run.Ability = Cast<UMHGZDescendingThrustAbility>(
			Run.AbilityBlueprint->GeneratedClass->GetDefaultObject());
		return Run.AbilityBlueprint;
	}
	if (!Run.bApply)
	{
		return nullptr;
	}
	UPackage* Package = CreatePackage(AbilityPackage);
	Run.AbilityBlueprint = Package ? FKismetEditorUtilities::CreateBlueprint(
		UMHGZDescendingThrustAbility::StaticClass(), Package,
		FName(TEXT("GA_IG_JiXiTuCi")), BPTYPE_Normal,
		UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(),
		FName(TEXT("MHGZM5DescendingThrustAssetSetup"))) : nullptr;
	Run.Ability = Run.AbilityBlueprint && Run.AbilityBlueprint->GeneratedClass
		? Cast<UMHGZDescendingThrustAbility>(Run.AbilityBlueprint->GeneratedClass->GetDefaultObject()) : nullptr;
	if (!Run.Ability || !Run.AbilityBlueprint)
	{
		Run.Errors.Add(TEXT("Could not create the resident Descending Thrust ability Blueprint."));
		return nullptr;
	}
	Run.ChangedAssets.Add(Path);
	return Run.AbilityBlueprint;
}

bool SetPrivateMontage(UMHGZDescendingThrustAbility& Ability, UAnimMontage* Montage,
	bool& bOutChanged)
{
	FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(
		UMHGZDescendingThrustAbility::StaticClass(), FName(TEXT("LandingMontage")));
	if (!Property)
	{
		return false;
	}
	if (Property->GetObjectPropertyValue_InContainer(&Ability) != Montage)
	{
		Property->SetObjectPropertyValue_InContainer(&Ability, Montage);
		bOutChanged = true;
	}
	return true;
}

const FAttackCollisionConfig* FindWeaponTraceTemplate()
{
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, SlashAbilityPath, nullptr, LOAD_NoWarn);
	const UMHGZKinsectSlashAbility* Ability = Blueprint && Blueprint->GeneratedClass
		? Cast<UMHGZKinsectSlashAbility>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr;
	if (!Ability)
	{
		return nullptr;
	}
	for (const FAttackSegmentConfig& Segment : Ability->AttackSegments)
	{
		// Match the established Kinsect Piercer asset setup contract: collision
		// routing is borrowed from an authored WeaponTrace region. HitzoneQueryTag
		// is intentionally optional in runtime (invalid means accept any hitzone),
		// and the kinsect-anchored segment may not itself carry a weapon Damage GE.
		if (!Segment.Collision.TraceRegions.IsEmpty()
			&& Segment.Collision.TraceMeshComponentTag == FName(TEXT("WeaponTrace")))
		{
			return &Segment.Collision;
		}
	}
	return nullptr;
}

bool ConfigureAbility(FSetupRun& Run)
{
	if (!Run.Ability || !Run.MainMontage || !Run.OverMontage)
	{
		return !Run.bApply;
	}
	const FAttackCollisionConfig* TraceTemplate = FindWeaponTraceTemplate();
	if (!TraceTemplate)
	{
		Run.Errors.Add(TEXT("Could not resolve an authored WeaponTrace collision template from GA_IG_CaoChongZhan."));
		return false;
	}
	const FGameplayTag RT = Tag(InputRTName);
	if (!RT.IsValid())
	{
		Run.Errors.Add(TEXT("Input.Weapon.RT is not registered."));
		return false;
	}
	const FName AirTransitionID(TEXT("Air_DescendingThrust_RT"));
	if (const FName* ExistingEntry = Run.Ability->EntrySectionByTransitionID.Find(AirTransitionID))
	{
		if (*ExistingEntry != FName(TEXT("Start")))
		{
			Run.Errors.Add(TEXT("GA_IG_JiXiTuCi already maps its owned aerial transition ID to a different Montage section."));
			return false;
		}
	}
	FObjectPropertyBase* LandingMontageProperty = FindFProperty<FObjectPropertyBase>(
		UMHGZDescendingThrustAbility::StaticClass(), FName(TEXT("LandingMontage")));
	if (!LandingMontageProperty)
	{
		Run.Errors.Add(TEXT("Could not resolve GA_IG_JiXiTuCi.LandingMontage."));
		return false;
	}
	const bool bNeedsConfiguration = Run.Ability->InputTag != RT
		|| Run.Ability->AttackMontage != Run.MainMontage
		|| Run.Ability->DefaultEntrySection != FName(TEXT("Start"))
		|| !Run.Ability->EntrySectionByTransitionID.Contains(AirTransitionID)
		|| LandingMontageProperty->GetObjectPropertyValue_InContainer(Run.Ability) != Run.OverMontage
		|| Run.Ability->AttackSegments.IsEmpty();
	if (bNeedsConfiguration)
	{
		Run.AbilityBlueprint->Modify();
		Run.Ability->Modify();
		Run.bAbilityConfigurationChanged = true;
		Run.Ability->InputTag = RT;
		Run.Ability->AttackMontage = Run.MainMontage;
		Run.Ability->DefaultEntrySection = TEXT("Start");
		if (!Run.Ability->EntrySectionByTransitionID.Contains(AirTransitionID))
		{
			Run.Ability->EntrySectionByTransitionID.Add(AirTransitionID, TEXT("Start"));
		}
		bool bLandingMontageChanged = false;
		if (!SetPrivateMontage(*Run.Ability, Run.OverMontage, bLandingMontageChanged))
		{
			Run.Errors.Add(TEXT("Could not assign GA_IG_JiXiTuCi.LandingMontage."));
			return false;
		}
	}
	if (Run.Ability->AttackSegments.IsEmpty())
	{
		for (int32 Index = 0; Index < 4; ++Index)
		{
			FAttackSegmentConfig& Segment = Run.Ability->AttackSegments.AddDefaulted_GetRef();
			Segment.Damage.DamageEffectClass = UMHGZDamageGameplayEffect::StaticClass();
			Segment.Damage.MotionValue = ProvisionalMotionValue;
			Segment.Damage.bUseHitzoneDefense = true;
			Segment.Damage.bRequiresHitToContinue = false;
			Segment.MultiHitCount = 1;
			Segment.MultiHitPolicy = EAttackMultiHitPolicy::ContactOnly;
			Segment.Collision = *TraceTemplate;
			Segment.Collision.TraceMeshComponentTag = FName(TEXT("WeaponTrace"));
			Segment.Collision.TraceRegions.Reset();
			Segment.Collision.bDrawDebug = false;
			FWeaponTraceRegion& Region = Segment.Collision.TraceRegions.AddDefaulted_GetRef();
			Region.StartSocketName = TEXT("Root");
			Region.EndSocketName = TEXT("IG_FrontTip");
			Region.Radius = 14.0f;
		}
	}
	if (Run.Ability->AttackSegments.Num() != 4)
	{
		Run.Errors.Add(TEXT("GA_IG_JiXiTuCi must contain exactly four AttackSegments; existing user values are preserved."));
		return false;
	}
	for (const FAttackSegmentConfig& Segment : Run.Ability->AttackSegments)
	{
		if (!Segment.Damage.DamageEffectClass
			|| Segment.Damage.MotionValue.GetValueAtLevel(1.0f) <= 0.0f
			|| Segment.Damage.bRequiresHitToContinue
			|| Segment.MultiHitCount != 1
			|| Segment.MultiHitPolicy != EAttackMultiHitPolicy::ContactOnly
			|| Segment.Collision.TraceRegions.IsEmpty())
		{
			Run.Errors.Add(TEXT("A Descending Thrust AttackSegment is incomplete; no damage windows were authored."));
			return false;
		}
	}
	if (bNeedsConfiguration)
	{
		// The GA's transition-section validation resolves TransitionIDs against
		// DA_IG_Combo. ConfigureCombo runs immediately after this function, so do a
		// structural compile now and defer full validation until all route rows exist.
		FKismetEditorUtilities::CompileBlueprint(Run.AbilityBlueprint,
			EBlueprintCompileOptions::SkipSave
			| EBlueprintCompileOptions::SkipDefaultObjectValidation
			| EBlueprintCompileOptions::SkipGarbageCollection);
	}
	if (Run.AbilityBlueprint->Status == BS_Error || !Run.AbilityBlueprint->GeneratedClass)
	{
		Run.Errors.Add(TEXT("GA_IG_JiXiTuCi failed Blueprint compilation."));
		return false;
	}
	Run.Ability = Cast<UMHGZDescendingThrustAbility>(
		Run.AbilityBlueprint->GeneratedClass->GetDefaultObject());
	return Run.Ability && Run.Ability->AttackMontage == Run.MainMontage
		&& Run.Ability->AttackSegments.Num() == 4;
}

bool ConfigureProfile(FSetupRun& Run)
{
	if (!Run.InputProfile)
	{
		return false;
	}
	const FGameplayTag ResolvedRT = Tag(InputRTName);
	const FGameplayTag PhysicalRT = Tag(PhysicalRTName);
	const FGameplayTag Unsheathed = Tag(UnsheathedName);
	const FGameplayTag Sheathed = Tag(SheathedName);
	const FGameplayTag Grounded = Tag(GroundedName);
	const FGameplayTag Aerial = Tag(TEXT("Combat.State.Aerial"));
	if (!ResolvedRT.IsValid() || !PhysicalRT.IsValid() || !Unsheathed.IsValid()
		|| !Sheathed.IsValid() || !Grounded.IsValid() || !Aerial.IsValid())
	{
		Run.Errors.Add(TEXT("Input route requires unregistered RT or context tags."));
		return false;
	}
	TArray<int32> ReleaseOwners;
	TArray<int32> SheathedOwners;
	TArray<int32> AerialOwners;
	for (int32 Index = 0; Index < Run.InputProfile->Chords.Num(); ++Index)
	{
		const FWeaponChordDefinition& Chord = Run.InputProfile->Chords[Index];
		if (Chord.OutputTag != ResolvedRT || Chord.TriggerControls.Num() != 1
			|| Chord.TriggerControls[0] != PhysicalRT || !Chord.RequiredHeldModifiers.IsEmpty())
		{
			continue;
		}
		if (Chord.DispatchPolicy == EWeaponChordDispatchPolicy::OnReleaseWithinMaxDurationIfUnconsumed)
		{
			ReleaseOwners.Add(Index);
		}
		if (Chord.RequiredContextTags.HasTagExact(Sheathed)
			&& Chord.RequiredContextTags.HasTagExact(Grounded))
		{
			SheathedOwners.Add(Index);
		}
		if (Chord.RequiredContextTags.HasTagExact(Aerial))
		{
			AerialOwners.Add(Index);
		}
	}
	if (ReleaseOwners.Num() != 1 || SheathedOwners.Num() != 1 || AerialOwners.Num() > 1)
	{
		Run.Errors.Add(TEXT("Input profile preflight expected exactly one short-release RT owner and one sheathed-ground RT route; refusing ambiguous profile."));
		return false;
	}
	const int32 ReleaseOwnerIndex = ReleaseOwners[0];
	const FWeaponChordDefinition& Owner = Run.InputProfile->Chords[ReleaseOwnerIndex];
	if (!Owner.RequiredContextTags.HasTagExact(Unsheathed)
		|| Owner.DispatchPolicy != EWeaponChordDispatchPolicy::OnReleaseWithinMaxDurationIfUnconsumed
		|| Owner.MaxPressDurationSeconds <= 0.0f
		|| Owner.ChordGracePeriodOverrideSeconds <= 0.0f)
	{
		Run.Errors.Add(TEXT("Existing short-release RT owner does not match the audited M1 release contract."));
		return false;
	}
	const bool bReleaseOwnerStillGroundLimited = Owner.RequiredContextTags.HasTagExact(Grounded);
	if ((!bReleaseOwnerStillGroundLimited && !AerialOwners.IsEmpty())
		|| (bReleaseOwnerStillGroundLimited && AerialOwners.Num() > 1))
	{
		Run.Errors.Add(TEXT("RT profile is in a mixed/ambiguous partial-migration state; refusing to rewrite it."));
		return false;
	}
	const FWeaponChordDefinition& SheathedOwner = Run.InputProfile->Chords[SheathedOwners[0]];
	if (SheathedOwner.DispatchPolicy != EWeaponChordDispatchPolicy::OnPressAfterChordGrace)
	{
		Run.Errors.Add(TEXT("The sheathed-ground RT route no longer uses its expected grace-period press policy."));
		return false;
	}
	for (int32 Index : AerialOwners)
	{
		if (Run.InputProfile->Chords[Index].DispatchPolicy
			!= EWeaponChordDispatchPolicy::OnPressAfterChordGrace)
		{
			Run.Errors.Add(TEXT("The legacy aerial RT route is not the expected OnPressAfterChordGrace entry."));
			return false;
		}
	}
	const bool bProfileNeedsUpdate = Owner.RequiredContextTags.HasTagExact(Grounded)
		|| !AerialOwners.IsEmpty();
	if (Run.bApply && bProfileNeedsUpdate)
	{
		Run.InputProfile->Modify();
		Run.InputProfile->Chords[ReleaseOwnerIndex].RequiredContextTags.RemoveTag(Grounded);
		for (int32 Index = AerialOwners.Num() - 1; Index >= 0; --Index)
		{
			Run.InputProfile->Chords.RemoveAt(AerialOwners[Index]);
		}
	}
	int32 ReleaseCountAfter = 0;
	int32 AerialOwnerCountAfter = 0;
	const FWeaponChordDefinition* UpdatedReleaseOwner = nullptr;
	for (const FWeaponChordDefinition& Chord : Run.InputProfile->Chords)
	{
		AerialOwnerCountAfter += Chord.OutputTag == ResolvedRT
			&& Chord.TriggerControls.Num() == 1 && Chord.TriggerControls[0] == PhysicalRT
			&& Chord.RequiredHeldModifiers.IsEmpty()
			&& Chord.RequiredContextTags.HasTagExact(Aerial);
		if (Chord.OutputTag == ResolvedRT && Chord.DispatchPolicy
			== EWeaponChordDispatchPolicy::OnReleaseWithinMaxDurationIfUnconsumed
			&& Chord.TriggerControls.Num() == 1 && Chord.TriggerControls[0] == PhysicalRT
			&& Chord.RequiredHeldModifiers.IsEmpty())
		{
			++ReleaseCountAfter;
			UpdatedReleaseOwner = &Chord;
		}
	}
	if (ReleaseCountAfter != 1 || !UpdatedReleaseOwner
		|| ((Run.bApply || !bReleaseOwnerStillGroundLimited)
			&& UpdatedReleaseOwner->RequiredContextTags.HasTagExact(Grounded))
		|| ((Run.bApply || !bReleaseOwnerStillGroundLimited) && AerialOwnerCountAfter != 0)
		|| !UpdatedReleaseOwner->RequiredContextTags.HasTagExact(Unsheathed))
	{
		Run.Errors.Add(TEXT("The resulting InputProfile does not have exactly one release-fallback RT owner."));
		return false;
	}
	return true;
}

bool AddUniqueTag(FGameplayTagContainer& Container, const FGameplayTag& Value)
{
	if (!Value.IsValid())
	{
		return false;
	}
	Container.AddTag(Value);
	return true;
}

bool ValidateComboAsset(FSetupRun& Run, const TCHAR* Stage)
{
	FDataValidationContext Context;
	if (Run.Combo->IsDataValid(Context) != EDataValidationResult::Invalid)
	{
		return true;
	}
	TArray<FText> Warnings;
	TArray<FText> Errors;
	Context.SplitIssues(Warnings, Errors);
	for (const FText& Error : Errors)
	{
		Run.Errors.Add(FString::Printf(TEXT("DA_IG_Combo %s validation: %s"),
			Stage, *Error.ToString()));
	}
	if (Errors.IsEmpty())
	{
		Run.Errors.Add(FString::Printf(TEXT("DA_IG_Combo failed %s validation without a diagnostic."), Stage));
	}
	return false;
}

FComboTransition* FindTransition(UMHGZWeaponComboData& Combo, const FName ID)
{
	return Combo.Transitions.FindByPredicate([ID](const FComboTransition& Transition)
	{
		return Transition.TransitionID == ID;
	});
}

bool TransitionMatches(const FComboTransition& Existing, const FComboTransition& Expected)
{
	return FComboTransition::StaticStruct()->CompareScriptStruct(
		&Existing, &Expected, PPF_None);
}

bool CloneTransition(UMHGZWeaponComboData& Combo, const FName SourceID,
	const FName NewID, FComboTransition*& Out)
{
	if (FComboTransition* Existing = FindTransition(Combo, NewID))
	{
		Out = Existing;
		return true;
	}
	const FComboTransition* Source = Combo.Transitions.FindByPredicate([SourceID](const FComboTransition& Transition)
	{
		return Transition.TransitionID == SourceID;
	});
	if (!Source)
	{
		return false;
	}
	Out = &Combo.Transitions.Add_GetRef(*Source);
	Out->TransitionID = NewID;
	return true;
}

const FComboTransition* FindLiveAerialConflict(const UMHGZWeaponComboData& Combo,
	const FGameplayTag& Input, const FGameplayTagContainer& LiveTags,
	const FName OwnedID)
{
	for (const FComboTransition& Transition : Combo.Transitions)
	{
		if (Transition.TransitionID == OwnedID || Transition.bAutoTransition
			|| Transition.InputTag != Input || !Transition.bMatchAnyState)
		{
			continue;
		}
		if (LiveTags.HasAll(Transition.RequiredTags)
			&& !LiveTags.HasAny(Transition.BlockedTags))
		{
			return &Transition;
		}
	}
	return nullptr;
}

bool ConfigureCombo(FSetupRun& Run)
{
	if (!Run.Combo || (Run.bApply && (!Run.Ability || !Run.AbilityBlueprint)))
	{
		return false;
	}
	const FGameplayTag RT = Tag(InputRTName);
	const FGameplayTag B = Tag(TEXT("Input.Weapon.B"));
	const FGameplayTag Y = Tag(TEXT("Input.Weapon.Y"));
	const FGameplayTag Grounded = Tag(GroundedName);
	const FGameplayTag Unsheathed = Tag(UnsheathedName);
	const FGameplayTag Falling = Tag(FallingName);
	const FGameplayTag CantAttack = Tag(CantAttackName);
	const FGameplayTag Dead = Tag(DeadName);
	const FGameplayTag Hitstun = Tag(HitstunName);
	const FGameplayTag Knockdown = Tag(KnockdownName);
	if (!RT.IsValid() || !B.IsValid() || !Y.IsValid() || !Grounded.IsValid()
		|| !Unsheathed.IsValid() || !Falling.IsValid() || !CantAttack.IsValid()
		|| !Dead.IsValid() || !Hitstun.IsValid() || !Knockdown.IsValid())
	{
		Run.Errors.Add(TEXT("Combo setup requires unregistered action or state tags."));
		return false;
	}

	const FName AerialEntryID(TEXT("Air_KinsectSlash_B"));
	const FName AirID(TEXT("Air_DescendingThrust_RT"));
	const FName GroundState(AerialStateName);
	const FName GroundIDs[] = {
		TEXT("Air_DescendingThrust_Y_ErLian"),
		TEXT("Air_DescendingThrust_B_FeiYuan"),
		TEXT("Air_DescendingThrust_BackB_HouChe") };
	const FName GroundSources[] = {
		TEXT("JiaSha_Y_ErLian"), TEXT("ErLian_B_FeiYuan"), TEXT("ErLian_BackB_HouChe") };
	const FName GroundSourceState(AerialStateName);
	const FName TargetStates[] = { TEXT("Ground.ErLian"), TEXT("Ground.FeiYuan"), TEXT("Ground.HouChe") };

	if (!Run.bApply)
	{
		// Audit-only mode validates the source templates and potential RT conflict
		// without modifying the loaded Combo asset.
		for (const FName SourceID : GroundSources)
		{
			if (!FindTransition(*Run.Combo, SourceID))
			{
				Run.Errors.Add(FString::Printf(TEXT("Could not find source Combo edge %s."), *SourceID.ToString()));
				return false;
			}
		}
		if (!FindTransition(*Run.Combo, AerialEntryID))
		{
			Run.Errors.Add(TEXT("Could not find the audited Air_KinsectSlash_B template transition."));
			return false;
		}
		const FName ManagedIDs[] = { AirID, GroundIDs[0], GroundIDs[1], GroundIDs[2] };
		for (const FName ManagedID : ManagedIDs)
		{
			if (FindTransition(*Run.Combo, ManagedID))
			{
				Run.Errors.Add(FString::Printf(TEXT("Managed M3 Combo ID %s already exists; run Apply mode for strict canonical readback."), *ManagedID.ToString()));
				return false;
			}
		}
		FGameplayTagContainer AerialLive;
		AerialLive.AddTag(Falling);
		AerialLive.AddTag(Unsheathed);
		if (const FComboTransition* Conflict = FindLiveAerialConflict(
			*Run.Combo, RT, AerialLive, AirID))
		{
			Run.Errors.Add(FString::Printf(
				TEXT("Existing any-state RT route %s (Ability=%s, RequiredTags=%s, BlockedTags=%s, Priority=%d) matches the Descending Thrust aerial context."),
				*Conflict->TransitionID.ToString(), *GetPathNameSafe(Conflict->AbilityClass.Get()),
				*Conflict->RequiredTags.ToStringSimple(), *Conflict->BlockedTags.ToStringSimple(),
				Conflict->Priority));
			return false;
		}
		return ValidateComboAsset(Run, TEXT("audit"));
	}

	const FComboTransition* AirTemplate = Run.Combo->Transitions.FindByPredicate([AerialEntryID](const FComboTransition& Transition)
	{
		return Transition.TransitionID == AerialEntryID;
	});
	if (!AirTemplate)
	{
		Run.Errors.Add(TEXT("Could not find the audited Air_KinsectSlash_B template transition."));
		return false;
	}
	FComboTransition ExpectedAir = *AirTemplate;
	ExpectedAir.TransitionID = AirID;
	ExpectedAir.InputTag = RT;
	ExpectedAir.AbilityClass = Run.AbilityBlueprint->GeneratedClass;
	ExpectedAir.TargetState = GroundState;
	ExpectedAir.ExecutionPolicy = EComboExecutionPolicy::ActivateAbility;
	ExpectedAir.StatePolicy = EComboStatePolicy::Replace;
	ExpectedAir.LandingPolicy = EComboLandingPolicy::AbilityOwned;
	ExpectedAir.bRequiresComboWindow = false;
	ExpectedAir.bRequiresDodgeAcceptWindow = false;
	ExpectedAir.RequiredTags.Reset();
	AddUniqueTag(ExpectedAir.RequiredTags, Falling);
	AddUniqueTag(ExpectedAir.RequiredTags, Unsheathed);
	ExpectedAir.BlockedTags.Reset();
	AddUniqueTag(ExpectedAir.BlockedTags, CantAttack);
	AddUniqueTag(ExpectedAir.BlockedTags, Grounded);
	AddUniqueTag(ExpectedAir.BlockedTags, Dead);
	AddUniqueTag(ExpectedAir.BlockedTags, Hitstun);
	AddUniqueTag(ExpectedAir.BlockedTags, Knockdown);
	FComboTransition* Air = FindTransition(*Run.Combo, AirID);
	if (Air)
	{
		if (!TransitionMatches(*Air, ExpectedAir))
		{
			Run.Errors.Add(TEXT("Existing managed aerial Combo edge differs from the canonical M3 contract; preserving it."));
			return false;
		}
	}
	else
	{
		if (!Run.bComboConfigurationChanged)
		{
			Run.Combo->Modify();
			Run.bComboConfigurationChanged = true;
		}
		Air = &Run.Combo->Transitions.Add_GetRef(ExpectedAir);
	}
	FGameplayTagContainer AerialLive;
	AerialLive.AddTag(Falling);
	AerialLive.AddTag(Unsheathed);
	if (const FComboTransition* Conflict = FindLiveAerialConflict(
		*Run.Combo, RT, AerialLive, AirID))
	{
		Run.Errors.Add(FString::Printf(
			TEXT("Existing any-state RT route %s (Ability=%s, RequiredTags=%s, BlockedTags=%s, Priority=%d) matches the Descending Thrust aerial context."),
			*Conflict->TransitionID.ToString(), *GetPathNameSafe(Conflict->AbilityClass.Get()),
			*Conflict->RequiredTags.ToStringSimple(), *Conflict->BlockedTags.ToStringSimple(),
			Conflict->Priority));
		return false;
	}

	for (int32 Index = 0; Index < UE_ARRAY_COUNT(GroundIDs); ++Index)
	{
		const FComboTransition* Source = Run.Combo->Transitions.FindByPredicate([&GroundSources, Index](const FComboTransition& Transition)
		{
			return Transition.TransitionID == GroundSources[Index];
		});
		if (!Source)
		{
			Run.Errors.Add(FString::Printf(TEXT("Could not find source Combo edge %s."), *GroundSources[Index].ToString()));
			return false;
		}
		FComboTransition ExpectedGround = *Source;
		ExpectedGround.TransitionID = GroundIDs[Index];
		ExpectedGround.SourceState = GroundSourceState;
		ExpectedGround.bMatchAnyState = false;
		ExpectedGround.BlockedSourceStates.Reset();
		ExpectedGround.TargetState = TargetStates[Index];
		ExpectedGround.RequiredTags.AddTag(Grounded);
		ExpectedGround.RequiredTags.AddTag(Unsheathed);
		ExpectedGround.BlockedTags.AddTag(Hitstun);
		ExpectedGround.bRequiresComboWindow = true;
		ExpectedGround.bRequiresDodgeAcceptWindow = false;
		ExpectedGround.ExecutionPolicy = EComboExecutionPolicy::ActivateAbility;
		FComboTransition* NewEdge = FindTransition(*Run.Combo, GroundIDs[Index]);
		if (NewEdge)
		{
			if (!TransitionMatches(*NewEdge, ExpectedGround))
			{
				Run.Errors.Add(FString::Printf(TEXT("Existing managed Combo edge %s differs from its audited source-derived contract; preserving it."), *GroundIDs[Index].ToString()));
				return false;
			}
		}
		else
		{
			if (!Run.bComboConfigurationChanged)
			{
				Run.Combo->Modify();
				Run.bComboConfigurationChanged = true;
			}
			NewEdge = &Run.Combo->Transitions.Add_GetRef(ExpectedGround);
		}
	}

	return ValidateComboAsset(Run, TEXT("apply"));
}

bool ConfigureFeiYuanEntry(FSetupRun& Run)
{
	Run.FeiYuanBlueprint = LoadObject<UBlueprint>(nullptr, FeiYuanAbilityPath, nullptr, LOAD_NoWarn);
	Run.FeiYuanAbility = Run.FeiYuanBlueprint && Run.FeiYuanBlueprint->GeneratedClass
		? Cast<UMHGZAttackAbility>(Run.FeiYuanBlueprint->GeneratedClass->GetDefaultObject()) : nullptr;
	if (!Run.FeiYuanAbility)
	{
		Run.Errors.Add(TEXT("Could not load GA_IG_FeiYuan as an attack ability."));
		return false;
	}
	const FName ExistingID(TEXT("ErLian_B_FeiYuan"));
	const FName NewID(TEXT("Air_DescendingThrust_B_FeiYuan"));
	const FName* ExistingSection = Run.FeiYuanAbility->EntrySectionByTransitionID.Find(ExistingID);
	if (!ExistingSection || *ExistingSection != FName(TEXT("Entry_From_ErLian")))
	{
		Run.Errors.Add(TEXT("GA_IG_FeiYuan no longer maps ErLian_B_FeiYuan to Entry_From_ErLian; refusing to invent a target entry."));
		return false;
	}
	if (Run.FeiYuanAbility->AttackMontage
		&& !Run.FeiYuanAbility->AttackMontage->IsValidSectionName(*ExistingSection))
	{
		Run.Errors.Add(TEXT("GA_IG_FeiYuan's audited Entry_From_ErLian section is invalid in its Montage."));
		return false;
	}
	if (const FName* ExistingNewEntry = Run.FeiYuanAbility->EntrySectionByTransitionID.Find(NewID))
	{
		if (*ExistingNewEntry != *ExistingSection)
		{
			Run.Errors.Add(TEXT("GA_IG_FeiYuan already maps the Descending Thrust Combo ID to a different entry; preserving it."));
			return false;
		}
	}
	if (Run.bApply)
	{
		if (!Run.FeiYuanAbility->EntrySectionByTransitionID.Contains(NewID))
		{
			Run.FeiYuanBlueprint->Modify();
			Run.FeiYuanAbility->Modify();
			Run.FeiYuanAbility->EntrySectionByTransitionID.Add(NewID, *ExistingSection);
			FKismetEditorUtilities::CompileBlueprint(Run.FeiYuanBlueprint);
			if (Run.FeiYuanBlueprint->Status == BS_Error)
			{
				Run.Errors.Add(TEXT("GA_IG_FeiYuan failed Blueprint compilation after adding the explicit Descending Thrust entry."));
				return false;
			}
		}
	}
	return true;
}

bool LoadDependencies(FSetupRun& Run)
{
	Run.Source380 = LoadObject<UAnimSequence>(nullptr, Source380Path, nullptr, LOAD_NoWarn);
	Run.Source381 = LoadObject<UAnimSequence>(nullptr, Source381Path, nullptr, LOAD_NoWarn);
	Run.Combat = LoadObject<UInsectGlaiveCombatConfig>(nullptr, CombatConfigPath, nullptr, LOAD_NoWarn);
	Run.Runtime = LoadObject<UWeaponRuntimeDefinition>(nullptr, RuntimeDefinitionPath, nullptr, LOAD_NoWarn);
	Run.Combo = LoadObject<UMHGZWeaponComboData>(nullptr, ComboDataPath, nullptr, LOAD_NoWarn);
	if (!Run.Source380 || !Run.Source381 || !Run.Combat || !Run.Runtime || !Run.Combo
		|| Run.Runtime->CombatConfig != Run.Combat || Run.Combat->ComboData != Run.Combo)
	{
		Run.Errors.Add(TEXT("Could not load the source sequences or the exact IG Combat/Runtime/Combo dependency chain."));
		return false;
	}
	Run.InputProfile = Run.Runtime->InputProfile;
	if (!Run.InputProfile)
	{
		Run.Errors.Add(TEXT("DA_WeaponRuntime_IG has no InputProfile."));
		return false;
	}
	const IAnimationDataModel* Model380 = Run.Source380->GetDataModel();
	const IAnimationDataModel* Model381 = Run.Source381->GetDataModel();
	if (!Model380 || !Model381 || Run.Source380->GetSkeleton() != Run.Source381->GetSkeleton()
		|| Model380->GetNumberOfFrames() + 1 != 70
		|| Model381->GetNumberOfFrames() + 1 != 162
		|| !FMath::IsNearlyEqual(Model380->GetFrameRate().AsDecimal(), 60.0f, 0.001f)
		|| !FMath::IsNearlyEqual(Model381->GetFrameRate().AsDecimal(), 60.0f, 0.001f)
		|| !FMath::IsNearlyEqual(Run.Source380->RateScale, 1.0f)
		|| !FMath::IsNearlyEqual(Run.Source381->RateScale, 1.0f)
		|| !SequenceHasNoAuthoredCurvesOrNotifies(*Run.Source380)
		|| !SequenceHasNoAuthoredCurvesOrNotifies(*Run.Source381))
	{
		Run.Errors.Add(TEXT("Imported 380/381 source metadata changed from the audited 60Hz, 70/162-key, no-notify/curve M1 inputs."));
		return false;
	}
	return true;
}

bool AddExpectedMissingAsset(FSetupRun& Run, const FString& Path, UClass* ExpectedClass)
{
	UObject* Existing = LoadObjectByPath(Path);
	if (!Existing)
	{
		Run.PendingAssets.AddUnique(Path);
		return true;
	}
	if (!ExpectedClass || !Existing->IsA(ExpectedClass))
	{
		Run.Errors.Add(TEXT("Canonical M3 asset path is occupied by an incompatible class: ") + Path);
		return false;
	}
	return true;
}

bool AuditBeforeApply(FSetupRun& Run)
{
	const FString StartPath = ObjectPath(GeneratedSequenceDir, TEXT("AS_Unsh_JiXiTuCi_Start_IP"));
	const FString LoopPath = ObjectPath(GeneratedSequenceDir, TEXT("AS_Unsh_JiXiTuCi_Falling_Loop_IP"));
	const FString OverPath = ObjectPath(GeneratedSequenceDir, TEXT("AS_Unsh_JiXiTuCi_Over_RM"));
	const FString CurvePath = ObjectPath(CurvePackage, TEXT("CV_IG_DescendingThrust_AirBasis"));
	const FString MainMontagePath = ObjectPath(MainMontagePackage, TEXT("AM_IG_JiXiTuCi"));
	const FString OverMontagePath = ObjectPath(OverMontagePackage, TEXT("AM_IG_JiXiTuCi_Over"));
	const FString AbilityPath = ObjectPath(AbilityPackage, TEXT("GA_IG_JiXiTuCi"));
	const TPair<FString, UClass*> ExpectedAssets[] = {
		{ StartPath, UAnimSequence::StaticClass() },
		{ LoopPath, UAnimSequence::StaticClass() },
		{ OverPath, UAnimSequence::StaticClass() },
		{ CurvePath, UCurveVector::StaticClass() },
		{ MainMontagePath, UAnimMontage::StaticClass() },
		{ OverMontagePath, UAnimMontage::StaticClass() },
		{ AbilityPath, UBlueprint::StaticClass() }
	};
	for (const TPair<FString, UClass*>& Expected : ExpectedAssets)
	{
		if (!AddExpectedMissingAsset(Run, Expected.Key, Expected.Value))
		{
			return false;
		}
	}

	if (UAnimSequence* Existing = LoadObject<UAnimSequence>(nullptr, *StartPath, nullptr, LOAD_NoWarn))
	{
		if (!SequenceMatchesCrop(*Existing, *Run.Source380, 0, 39, false))
		{
			Run.Errors.Add(TEXT("Existing Start_IP sequence differs from the frozen 380 crop; preserving it."));
			return false;
		}
	}
	if (UAnimSequence* Existing = LoadObject<UAnimSequence>(nullptr, *LoopPath, nullptr, LOAD_NoWarn))
	{
		if (!SequenceMatchesCrop(*Existing, *Run.Source380, LoopStartSourceFrame, 69, false))
		{
			Run.Errors.Add(TEXT("Existing Falling_Loop_IP sequence differs from the frozen 380 crop; preserving it."));
			return false;
		}
	}
	if (UAnimSequence* Existing = LoadObject<UAnimSequence>(nullptr, *OverPath, nullptr, LOAD_NoWarn))
	{
		if (!SequenceMatchesCrop(*Existing, *Run.Source381, 0, 161, true))
		{
			Run.Errors.Add(TEXT("Existing Over_RM sequence differs from the frozen 381 root-motion crop; preserving it."));
			return false;
		}
	}
	if (UCurveVector* Existing = LoadObject<UCurveVector>(nullptr, *CurvePath, nullptr, LOAD_NoWarn))
	{
		if (!CurveMatches(*Existing, Run))
		{
			Run.Errors.Add(TEXT("Existing canonical curve differs from the frozen M1 curve; preserving it."));
			return false;
		}
	}
	if (Run.Combat->DescendingThrustAirProfile.Curve)
	{
		if (Run.Combat->DescendingThrustAirProfile.Curve->GetPathName() != CurvePath)
		{
			Run.Errors.Add(TEXT("DA_IG_Combat already references a different Descending Thrust curve; preserving it."));
			return false;
		}
	}

	if (!ConfigureProfile(Run) || !ConfigureCombo(Run) || !ConfigureFeiYuanEntry(Run))
	{
		return false;
	}
	if (UBlueprint* ExistingBlueprint = LoadObject<UBlueprint>(nullptr, *AbilityPath, nullptr, LOAD_NoWarn))
	{
		if (!ExistingBlueprint->GeneratedClass
			|| !ExistingBlueprint->GeneratedClass->IsChildOf(UMHGZDescendingThrustAbility::StaticClass()))
		{
			Run.Errors.Add(TEXT("Existing GA_IG_JiXiTuCi is not a child of the production ability class."));
			return false;
		}
	}
	return Run.Errors.IsEmpty();
}

bool PrepareAssets(FSetupRun& Run)
{
	Run.StartSequence = GetOrCreateSequence(Run, TEXT("AS_Unsh_JiXiTuCi_Start_IP"),
		*Run.Source380, 0, 39, false);
	Run.LoopSequence = GetOrCreateSequence(Run, TEXT("AS_Unsh_JiXiTuCi_Falling_Loop_IP"),
		*Run.Source380, LoopStartSourceFrame, 69, false);
	Run.OverSequence = GetOrCreateSequence(Run, TEXT("AS_Unsh_JiXiTuCi_Over_RM"),
		*Run.Source381, 0, 161, true);
	if (!Run.Errors.IsEmpty())
	{
		return false;
	}
	const FString CurvePath = ObjectPath(CurvePackage, TEXT("CV_IG_DescendingThrust_AirBasis"));
	Run.Curve = LoadObject<UCurveVector>(nullptr, *CurvePath, nullptr, LOAD_NoWarn);
	if (!GetOrCreateCurve(Run))
	{
		return false;
	}
	if (!Run.bApply && (!Run.StartSequence || !Run.LoopSequence || !Run.OverSequence))
	{
		UE_LOG(LogTemp, Display, TEXT("[DescendingThrustAssets][Audit] Missing generated sequences are expected in audit mode; use -Apply after reviewing the source manifest."));
	}
	if (Run.bApply && (!Run.StartSequence || !Run.LoopSequence || !Run.OverSequence || !Run.Curve))
	{
		Run.Errors.Add(TEXT("The complete animation/curve set could not be created."));
		return false;
	}
	if (Run.StartSequence && Run.LoopSequence && Run.OverSequence)
	{
		Run.MainMontage = GetOrCreateMontage(Run, MainMontagePackage,
			TEXT("AM_IG_JiXiTuCi"), Run.Source380->GetSkeleton());
		Run.OverMontage = GetOrCreateMontage(Run, OverMontagePackage,
			TEXT("AM_IG_JiXiTuCi_Over"), Run.Source381->GetSkeleton());
		if (!Run.MainMontage || !Run.OverMontage
			|| !ConfigureMontage(Run, *Run.MainMontage, true, Run.StartSequence,
				Run.LoopSequence, nullptr, Run.Source380->GetSkeleton())
			|| !ConfigureMontage(Run, *Run.OverMontage, false, nullptr, nullptr,
				Run.OverSequence, Run.Source381->GetSkeleton()))
		{
			Run.Errors.Add(TEXT("Montage layout setup failed."));
			return false;
		}
		bool bMainEventChanged = false;
		bool bOverEventChanged = false;
		if (!EnsureEvent(Run, *Run.MainMontage,
			Run.StartSequence->GetPlayLength(), EDescendingThrustMontageEvent::EnterFallingLoop,
			bMainEventChanged)
			|| !EnsureEvent(Run, *Run.OverMontage,
			Correction3SourceFrame / 60.0f, EDescendingThrustMontageEvent::Correction3,
			bOverEventChanged))
		{
			return false;
		}
		if (bMainEventChanged)
		{
			Run.MainMontage->RefreshCacheData();
		}
		if (bOverEventChanged)
		{
			Run.OverMontage->RefreshCacheData();
		}
	}
	if (Run.bApply)
	{
		if (!GetOrCreateAbilityBlueprint(Run) || !Run.AbilityBlueprint)
		{
			return false;
		}
	}
	else
	{
		const FString Path = ObjectPath(AbilityPackage, TEXT("GA_IG_JiXiTuCi"));
		Run.AbilityBlueprint = LoadObject<UBlueprint>(nullptr, *Path, nullptr, LOAD_NoWarn);
		Run.Ability = Run.AbilityBlueprint && Run.AbilityBlueprint->GeneratedClass
			? Cast<UMHGZDescendingThrustAbility>(Run.AbilityBlueprint->GeneratedClass->GetDefaultObject()) : nullptr;
	}
	if (Run.Curve && Run.bApply)
	{
		Run.ExpectedProfile.Curve = Run.Curve;
		if (Run.Combat->DescendingThrustAirProfile.Curve
			&& Run.Combat->DescendingThrustAirProfile.Curve != Run.Curve)
		{
			Run.Errors.Add(TEXT("DA_IG_Combat already references another DescendingThrustAirProfile curve; preserving it."));
			return false;
		}
		const FRecordedLocalTrajectoryProfile& ExistingProfile = Run.Combat->DescendingThrustAirProfile;
		const bool bProfileDiffers = ExistingProfile.Curve != Run.ExpectedProfile.Curve
			|| !FMath::IsNearlyEqual(ExistingProfile.CoveredSeconds,
				Run.ExpectedProfile.CoveredSeconds, 0.00001f)
			|| !ExistingProfile.TerminalVelocityLocal.Equals(
				Run.ExpectedProfile.TerminalVelocityLocal, 0.001f)
			|| !ExistingProfile.TerminalAccelerationLocal.Equals(
				Run.ExpectedProfile.TerminalAccelerationLocal, 0.001f);
		if (bProfileDiffers)
		{
			Run.Combat->Modify();
			Run.Combat->DescendingThrustAirProfile = Run.ExpectedProfile;
		}
	}
	if (!ConfigureProfile(Run) || !ConfigureAbility(Run) || !ConfigureCombo(Run)
		|| !ConfigureFeiYuanEntry(Run))
	{
		return false;
	}
	if (Run.bApply)
	{
		// Now every owned TransitionID is present in DA_IG_Combo. Run the normal
		// Blueprint validation pass only after both sides of that contract exist.
		if (Run.bAbilityConfigurationChanged || Run.bComboConfigurationChanged)
		{
			FKismetEditorUtilities::CompileBlueprint(Run.AbilityBlueprint);
		}
		Run.Ability = Run.AbilityBlueprint && Run.AbilityBlueprint->GeneratedClass
			? Cast<UMHGZDescendingThrustAbility>(
				Run.AbilityBlueprint->GeneratedClass->GetDefaultObject())
			: nullptr;
		if (!Run.AbilityBlueprint || Run.AbilityBlueprint->Status == BS_Error
			|| !Run.Ability || Run.Ability->AttackMontage != Run.MainMontage
			|| Run.Ability->AttackSegments.Num() != 4
			|| Run.Ability->EntrySectionByTransitionID.FindRef(
				FName(TEXT("Air_DescendingThrust_RT"))) != FName(TEXT("Start")))
		{
			Run.Errors.Add(TEXT("GA_IG_JiXiTuCi final compile or CDO readback failed after Combo routes were configured."));
			return false;
		}
	}
	return Run.Errors.IsEmpty();
}

bool WriteReport(const FSetupRun& Run)
{
	TArray<FString> Lines;
	Lines.Add(TEXT("{"));
	Lines.Add(FString::Printf(TEXT("  \"mode\": \"%s\","), Run.bApply ? TEXT("apply") : TEXT("audit")));
	Lines.Add(FString::Printf(TEXT("  \"success\": %s,"), Run.Errors.IsEmpty() ? TEXT("true") : TEXT("false")));
	Lines.Add(FString::Printf(TEXT("  \"source_380\": \"%s\","), Source380Path));
	Lines.Add(FString::Printf(TEXT("  \"source_381\": \"%s\","), Source381Path));
	Lines.Add(FString::Printf(TEXT("  \"curve_source_csv\": \"%s\","), CurveCsvRelativePath));
	Lines.Add(FString::Printf(TEXT("  \"curve_csv_rows\": %d,"), Run.CurveSamples.Num()));
	Lines.Add(FString::Printf(TEXT("  \"curve_key_count\": %d,"), Run.CurveCoverageIndex + 1));
	Lines.Add(FString::Printf(TEXT("  \"curve_covered_seconds\": %.7f,"),
		Run.CurveCoverageIndex != INDEX_NONE ? Run.CurveSamples[Run.CurveCoverageIndex].Time : 0.0f));
	Lines.Add(FString::Printf(TEXT("  \"unit_to_cm\": %.1f,"), UnitToCentimeters));
	Lines.Add(TEXT("  \"generated_assets\": ["));
	for (int32 Index = 0; Index < Run.ChangedAssets.Num(); ++Index)
	{
		Lines.Add(FString::Printf(TEXT("    \"%s\"%s"), *Run.ChangedAssets[Index],
			Index + 1 == Run.ChangedAssets.Num() ? TEXT("") : TEXT(",")));
	}
	Lines.Add(TEXT("  ],"));
	Lines.Add(TEXT("  \"pending_assets\": ["));
	for (int32 Index = 0; Index < Run.PendingAssets.Num(); ++Index)
	{
		Lines.Add(FString::Printf(TEXT("    \"%s\"%s"), *Run.PendingAssets[Index],
			Index + 1 == Run.PendingAssets.Num() ? TEXT("") : TEXT(",")));
	}
	Lines.Add(TEXT("  ],"));
	Lines.Add(TEXT("  \"damage_windows_created\": 0,"));
	Lines.Add(TEXT("  \"errors\": ["));
	for (int32 Index = 0; Index < Run.Errors.Num(); ++Index)
	{
		Lines.Add(FString::Printf(TEXT("    \"%s\"%s"), *Run.Errors[Index].Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("\""), TEXT("\\\"")),
			Index + 1 == Run.Errors.Num() ? TEXT("") : TEXT(",")));
	}
	Lines.Add(TEXT("  ]"));
	Lines.Add(TEXT("}"));
	const FString Output = FPaths::ProjectSavedDir() / TEXT("AssetProbe/DescendingThrust/M3/asset_setup_report.json");
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Output), true);
	return FFileHelper::SaveStringToFile(FString::Join(Lines, TEXT("\n")), *Output,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

struct FE1NotifyAuditRow
{
	FString Montage;
	FString Kind;
	FString Track;
	int32 ConfigIndex = INDEX_NONE;
	float Start = 0.0f;
	float End = 0.0f;
};

bool WriteE1AuditReport(const bool bSuccess, const TArray<FString>& Errors,
	const TArray<FE1NotifyAuditRow>& NotifyRows,
	const UAnimMontage* MainMontage, const UAnimMontage* OverMontage,
	const UMHGZDescendingThrustAbility* Ability)
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("mode"), TEXT("e1_read_only_audit"));
	Root->SetBoolField(TEXT("success"), bSuccess);
	Root->SetStringField(TEXT("ga_path"),
		TEXT("/Game/Blueprints/Ability/InsectGlaive/GA_IG_JiXiTuCi.GA_IG_JiXiTuCi"));
	Root->SetNumberField(TEXT("attack_segment_count"),
		Ability ? Ability->AttackSegments.Num() : 0);
	Root->SetBoolField(TEXT("ga_main_montage_matches"),
		Ability && MainMontage && Ability->AttackMontage == MainMontage);
	Root->SetNumberField(TEXT("main_montage_length_seconds"),
		MainMontage ? MainMontage->GetPlayLength() : 0.0);
	Root->SetNumberField(TEXT("over_montage_length_seconds"),
		OverMontage ? OverMontage->GetPlayLength() : 0.0);
	auto AddSections = [&Root](const TCHAR* FieldName, const UAnimMontage* Montage)
	{
		TArray<TSharedPtr<FJsonValue>> SectionValues;
		if (Montage)
		{
			for (const FCompositeSection& Section : Montage->CompositeSections)
			{
				TSharedRef<FJsonObject> SectionObject = MakeShared<FJsonObject>();
				SectionObject->SetStringField(TEXT("name"), Section.SectionName.ToString());
				SectionObject->SetNumberField(TEXT("start_seconds"), Section.GetTime());
				SectionObject->SetStringField(TEXT("next"), Section.NextSectionName.ToString());
				SectionValues.Add(MakeShared<FJsonValueObject>(SectionObject));
			}
		}
		Root->SetArrayField(FieldName, SectionValues);
	};
	AddSections(TEXT("main_sections"), MainMontage);
	AddSections(TEXT("over_sections"), OverMontage);

	TArray<TSharedPtr<FJsonValue>> NotifyValues;
	for (const FE1NotifyAuditRow& Row : NotifyRows)
	{
		TSharedRef<FJsonObject> NotifyObject = MakeShared<FJsonObject>();
		NotifyObject->SetStringField(TEXT("montage"), Row.Montage);
		NotifyObject->SetStringField(TEXT("kind"), Row.Kind);
		NotifyObject->SetStringField(TEXT("track"), Row.Track);
		NotifyObject->SetNumberField(TEXT("config_index"), Row.ConfigIndex);
		NotifyObject->SetNumberField(TEXT("start_seconds"), Row.Start);
		NotifyObject->SetNumberField(TEXT("end_seconds"), Row.End);
		NotifyObject->SetNumberField(TEXT("duration_seconds"), Row.End - Row.Start);
		NotifyValues.Add(MakeShared<FJsonValueObject>(NotifyObject));
	}
	Root->SetArrayField(TEXT("notifies"), NotifyValues);

	TArray<TSharedPtr<FJsonValue>> ErrorValues;
	for (const FString& Error : Errors)
	{
		ErrorValues.Add(MakeShared<FJsonValueString>(Error));
	}
	Root->SetArrayField(TEXT("errors"), ErrorValues);

	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	if (!FJsonSerializer::Serialize(Root, Writer))
	{
		return false;
	}
	const FString Output = FPaths::ProjectSavedDir()
		/ TEXT("AssetProbe/DescendingThrust/E1/asset_readback.json");
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Output), true);
	return FFileHelper::SaveStringToFile(Json, *Output,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

int32 AuditE1AssetsReadOnly()
{
	const TCHAR* MainPath = TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_JiXiTuCi.AM_IG_JiXiTuCi");
	const TCHAR* OverPath = TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_JiXiTuCi_Over.AM_IG_JiXiTuCi_Over");
	const TCHAR* AbilityPath = TEXT("/Game/Blueprints/Ability/InsectGlaive/GA_IG_JiXiTuCi.GA_IG_JiXiTuCi");
	const TCHAR* StartPath = TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Generated/AS_Unsh_JiXiTuCi_Start_IP.AS_Unsh_JiXiTuCi_Start_IP");
	const TCHAR* LoopPath = TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Generated/AS_Unsh_JiXiTuCi_Falling_Loop_IP.AS_Unsh_JiXiTuCi_Falling_Loop_IP");
	const TCHAR* OverSequencePath = TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Generated/AS_Unsh_JiXiTuCi_Over_RM.AS_Unsh_JiXiTuCi_Over_RM");

	UAnimMontage* MainMontage = LoadObject<UAnimMontage>(nullptr, MainPath, nullptr, LOAD_NoWarn);
	UAnimMontage* OverMontage = LoadObject<UAnimMontage>(nullptr, OverPath, nullptr, LOAD_NoWarn);
	UAnimSequence* Start = LoadObject<UAnimSequence>(nullptr, StartPath, nullptr, LOAD_NoWarn);
	UAnimSequence* Loop = LoadObject<UAnimSequence>(nullptr, LoopPath, nullptr, LOAD_NoWarn);
	UAnimSequence* Over = LoadObject<UAnimSequence>(nullptr, OverSequencePath, nullptr, LOAD_NoWarn);
	UBlueprint* AbilityBlueprint = LoadObject<UBlueprint>(nullptr, AbilityPath, nullptr, LOAD_NoWarn);
	UMHGZDescendingThrustAbility* Ability = AbilityBlueprint && AbilityBlueprint->GeneratedClass
		? Cast<UMHGZDescendingThrustAbility>(AbilityBlueprint->GeneratedClass->GetDefaultObject())
		: nullptr;

	TArray<FString> Errors;
	TArray<FE1NotifyAuditRow> NotifyRows;
	if (!MainMontage || !OverMontage || !Start || !Loop || !Over || !AbilityBlueprint || !Ability)
	{
		Errors.Add(TEXT("One or more saved M3 Montage, sequence, or GA assets could not be loaded."));
	}
	if (AbilityBlueprint && AbilityBlueprint->Status == BS_Error)
	{
		Errors.Add(TEXT("GA_IG_JiXiTuCi Blueprint reports BS_Error."));
	}
	if (MainMontage && (!Start || !Loop
		|| !MontageLayoutMatches(*MainMontage, true, Start, Loop, nullptr)))
	{
		Errors.Add(TEXT("Main Montage no longer matches the persistent Start -> FallingLoop layout/section links."));
	}
	if (OverMontage && (!Over
		|| !MontageLayoutMatches(*OverMontage, false, nullptr, nullptr, Over)))
	{
		Errors.Add(TEXT("Over Montage no longer matches the persistent single Over root-motion section."));
	}
	if (Ability && (!MainMontage || !OverMontage || Ability->AttackMontage != MainMontage
		|| Ability->AttackSegments.Num() != 4))
	{
		Errors.Add(TEXT("GA must reference the main Montage and contain exactly four AttackSegments."));
	}
	if (Ability)
	{
		FObjectPropertyBase* LandingMontageProperty = FindFProperty<FObjectPropertyBase>(
			UMHGZDescendingThrustAbility::StaticClass(), FName(TEXT("LandingMontage")));
		if (!LandingMontageProperty
			|| LandingMontageProperty->GetObjectPropertyValue_InContainer(Ability) != OverMontage)
		{
			Errors.Add(TEXT("GA LandingMontage does not reference AM_IG_JiXiTuCi_Over."));
		}
	}

	TMap<int32, FString> ConfigIndexOwners;
	int32 CollisionCount = 0;
	int32 MainComboWindowCount = 0;
	int32 OverComboWindowCount = 0;
	auto InspectMontage = [&Errors, &NotifyRows, &ConfigIndexOwners,
		&CollisionCount, &MainComboWindowCount, &OverComboWindowCount](
		const TCHAR* Label, const UAnimMontage* Montage)
	{
		if (!Montage)
		{
			return;
		}
		for (const FAnimNotifyEvent& Event : Montage->Notifies)
		{
			const float StartTime = Event.GetTriggerTime();
			const float EndTime = Event.GetEndTriggerTime();
			const bool bValidSpan = FMath::IsFinite(StartTime) && FMath::IsFinite(EndTime)
				&& StartTime >= -KINDA_SMALL_NUMBER
				&& EndTime > StartTime + KINDA_SMALL_NUMBER
				&& EndTime <= Montage->GetPlayLength() + 0.01f;
			const FString TrackName = Montage->AnimNotifyTracks.IsValidIndex(Event.TrackIndex)
				? Montage->AnimNotifyTracks[Event.TrackIndex].TrackName.ToString()
				: FString();

			if (const UAnimNotifyState_AttackCollision* Collision =
				Cast<UAnimNotifyState_AttackCollision>(Event.NotifyStateClass))
			{
				++CollisionCount;
				FE1NotifyAuditRow& Row = NotifyRows.AddDefaulted_GetRef();
				Row.Montage = Label;
				Row.Kind = TEXT("AttackCollision");
				Row.Track = TrackName;
				Row.ConfigIndex = Collision->ConfigIndex;
				Row.Start = StartTime;
				Row.End = EndTime;
				UE_LOG(LogTemp, Display,
					TEXT("[DescendingThrustE1][Readback] Montage=%s AttackCollision ConfigIndex=%d start=%.6f end=%.6f track=%s."),
					Label, Collision->ConfigIndex, StartTime, EndTime, *TrackName);
				if (Collision->ConfigIndex < 0 || Collision->ConfigIndex >= 4)
				{
					Errors.Add(FString::Printf(TEXT("%s has AttackCollision ConfigIndex=%d outside [0,3]."),
						Label, Collision->ConfigIndex));
				}
				else if (const FString* ExistingMontage = ConfigIndexOwners.Find(Collision->ConfigIndex))
				{
					Errors.Add(FString::Printf(TEXT("AttackCollision ConfigIndex=%d is duplicated in %s and %s."),
						Collision->ConfigIndex, **ExistingMontage, Label));
				}
				else
				{
					ConfigIndexOwners.Add(Collision->ConfigIndex, Label);
				}
				if (!bValidSpan)
				{
					Errors.Add(FString::Printf(TEXT("%s ConfigIndex=%d has an invalid/out-of-range notify-state span."),
						Label, Collision->ConfigIndex));
				}
				if (TrackName != TEXT("Attack"))
				{
					Errors.Add(FString::Printf(TEXT("%s ConfigIndex=%d is not on the Attack notify track."),
						Label, Collision->ConfigIndex));
				}
			}
			else if (Cast<UAnimNotifyState_ComboWindow>(Event.NotifyStateClass))
			{
				const bool bIsOver = FCString::Stricmp(Label, TEXT("Over")) == 0;
				bIsOver ? ++OverComboWindowCount : ++MainComboWindowCount;
				FE1NotifyAuditRow& Row = NotifyRows.AddDefaulted_GetRef();
				Row.Montage = Label;
				Row.Kind = TEXT("ComboWindow");
				Row.Track = TrackName;
				Row.Start = StartTime;
				Row.End = EndTime;
				UE_LOG(LogTemp, Display,
					TEXT("[DescendingThrustE1][Readback] Montage=%s ComboWindow start=%.6f end=%.6f track=%s."),
					Label, StartTime, EndTime, *TrackName);
				const bool bDedicatedComboTrack = TrackName == TEXT("Window")
					|| TrackName == TEXT("Combo");
				if (!bValidSpan || !bDedicatedComboTrack)
				{
					Errors.Add(FString::Printf(TEXT("%s ComboWindow has an invalid span or is not on a dedicated Window/Combo notify track."), Label));
				}
			}
		}
	};
	InspectMontage(TEXT("Main"), MainMontage);
	InspectMontage(TEXT("Over"), OverMontage);

	if (CollisionCount != 4 || ConfigIndexOwners.Num() != 4)
	{
		Errors.Add(FString::Printf(TEXT("Expected exactly four unique AttackCollision windows with ConfigIndex 0..3; read %d windows/%d unique indices."),
			CollisionCount, ConfigIndexOwners.Num()));
	}
	if (MainComboWindowCount != 0 || OverComboWindowCount != 1)
	{
		Errors.Add(FString::Printf(TEXT("Expected no ComboWindow on the aerial Montage and exactly one on Over; read Main=%d Over=%d."),
			MainComboWindowCount, OverComboWindowCount));
	}

	const bool bSuccess = Errors.IsEmpty();
	if (bSuccess)
	{
		UE_LOG(LogTemp, Display,
			TEXT("[DescendingThrustE1][Readback] success=true collisions=%d combo_windows_main=%d combo_windows_over=%d errors=%d."),
			CollisionCount, MainComboWindowCount, OverComboWindowCount, Errors.Num());
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("[DescendingThrustE1][Readback] success=false collisions=%d combo_windows_main=%d combo_windows_over=%d errors=%d."),
			CollisionCount, MainComboWindowCount, OverComboWindowCount, Errors.Num());
	}
	for (const FString& Error : Errors)
	{
		UE_LOG(LogTemp, Error, TEXT("[DescendingThrustE1][Readback] %s"), *Error);
	}
	if (!WriteE1AuditReport(bSuccess, Errors, NotifyRows, MainMontage, OverMontage, Ability))
	{
		UE_LOG(LogTemp, Error, TEXT("[DescendingThrustE1][Readback] Failed to write the read-only audit report."));
		return 1;
	}
	return bSuccess ? 0 : 1;
}
}
#endif // WITH_EDITOR

UMHGZM5DescendingThrustAssetSetupCommandlet::UMHGZM5DescendingThrustAssetSetupCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UMHGZM5DescendingThrustAssetSetupCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	using namespace UE::MHGZ::DescendingThrustAssetSetup;
	if (FParse::Param(*Params, TEXT("AuditE1")))
	{
		return AuditE1AssetsReadOnly();
	}
	FSetupRun Run;
	Run.bApply = FParse::Param(*Params, TEXT("Apply"));
	if (!ReadCurveSamples(Run) || !ValidateFrozenInputHashes(Run) || !LoadDependencies(Run))
	{
		WriteReport(Run);
		return 1;
	}
	const bool bPrepared = Run.bApply ? PrepareAssets(Run) : AuditBeforeApply(Run);
	if (!bPrepared)
	{
		for (const FString& Error : Run.Errors)
		{
			UE_LOG(LogTemp, Error, TEXT("[DescendingThrustAssets] %s"), *Error);
		}
		WriteReport(Run);
		return 1;
	}
	if (Run.bApply)
	{
		if (Run.Combat->DescendingThrustAirProfile.Curve != Run.Curve
			|| !Run.Combat->DescendingThrustAirProfile.IsValid())
		{
			Run.Errors.Add(TEXT("DA_IG_Combat does not retain the generated, self-validating air trajectory profile."));
		}
		if (!Run.Ability || Run.AbilityBlueprint->Status == BS_Error
			|| Run.Ability->AttackSegments.Num() != 4
			|| !Run.Ability->AttackMontage || Run.Ability->AttackMontage != Run.MainMontage)
		{
			Run.Errors.Add(TEXT("Generated GA CDO failed its static configuration check."));
		}
		if (Run.Errors.IsEmpty())
		{
			SavePackageAsset(*Run.StartSequence, Run);
			SavePackageAsset(*Run.LoopSequence, Run);
			SavePackageAsset(*Run.OverSequence, Run);
			SavePackageAsset(*Run.Curve, Run);
			SavePackageAsset(*Run.MainMontage, Run);
			SavePackageAsset(*Run.OverMontage, Run);
			SavePackageAsset(*Run.Combat, Run);
			SavePackageAsset(*Run.InputProfile, Run);
			SavePackageAsset(*Run.Combo, Run);
			SavePackageAsset(*Run.AbilityBlueprint, Run);
			SavePackageAsset(*Run.FeiYuanBlueprint, Run);
		}
	}
	for (const FString& Error : Run.Errors)
	{
		UE_LOG(LogTemp, Error, TEXT("[DescendingThrustAssets] %s"), *Error);
	}
	UE_LOG(LogTemp, Display, TEXT("[DescendingThrustAssets] mode=%s curveRows=%d curveKeys=%d coverage=%.4fs damageWindowsCreated=0 changed=%d errors=%d"),
		Run.bApply ? TEXT("Apply") : TEXT("Audit"), Run.CurveSamples.Num(),
		Run.CurveCoverageIndex + 1,
		Run.CurveCoverageIndex != INDEX_NONE ? Run.CurveSamples[Run.CurveCoverageIndex].Time : 0.0f,
		Run.ChangedAssets.Num(), Run.Errors.Num());
	if (!WriteReport(Run))
	{
		UE_LOG(LogTemp, Error, TEXT("[DescendingThrustAssets] Failed to write M3 report."));
		return 1;
	}
	return Run.Errors.IsEmpty() ? 0 : 1;
#else
	UE_LOG(LogTemp, Error, TEXT("[DescendingThrustAssets] This commandlet requires an editor build."));
	return 1;
#endif
}
