// Copyright MHGZ Project. All Rights Reserved.

#include "Editor/MHGZM5KinsectPiercerAssetSetupCommandlet.h"

#if WITH_EDITOR

#include "ActionSystem/AnimNotifyState_AttackCollision.h"
#include "ActionSystem/AnimNotifyState_ComboWindow.h"
#include "ActionSystem/AnimNotifyState_DodgeAcceptWindow.h"
#include "ActionSystem/MHGZDamageGameplayEffect.h"
#include "ActionSystem/MHGZKinsectPiercerAbility.h"
#include "ActionSystem/MHGZKinsectSlashAbility.h"
#include "ActionSystem/MHGZWeaponComboData.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/Skeleton.h"
#include "AnimationBlueprintLibrary.h"
#include "Curves/CurveVector.h"
#include "Engine/Blueprint.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "GameplayEffect.h"
#include "HAL/FileManager.h"
#include "InsectGlaive/InsectGlaiveCombatConfig.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/DataValidation.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "InputAction.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "WeaponRuntime/MHGZWeaponInputProfile.h"
#include "WeaponRuntime/MHGZWeaponRuntimeDefinition.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"

namespace UE::MHGZ::KinsectPiercerAssetSetup
{
constexpr TCHAR MainMontagePackage[] =
	TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_KinsectPiercer");
constexpr TCHAR LandingMontagePackage[] =
	TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_KinsectPiercer_Over");
constexpr TCHAR AbilityPackage[] =
	TEXT("/Game/Blueprints/Ability/InsectGlaive/GA_IG_KinsectPiercer");
constexpr TCHAR SequenceDirectory[] =
	TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Generated");
constexpr TCHAR LegacyLandingSequencePath[] =
	TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Generated/AS_Unsh_CaoChongChuanCi_Over_IP.AS_Unsh_CaoChongChuanCi_Over_IP");
constexpr TCHAR CombatConfigPath[] =
	TEXT("/Game/Weapons/InsectGlaive/Data/DA_IG_Combat.DA_IG_Combat");
constexpr TCHAR ComboDataPath[] =
	TEXT("/Game/Weapons/InsectGlaive/Data/DA_IG_Combo.DA_IG_Combo");
constexpr TCHAR RuntimeDefinitionPath[] =
	TEXT("/Game/Weapons/InsectGlaive/Data/DA_WeaponRuntime_IG.DA_WeaponRuntime_IG");
constexpr TCHAR GlaiveMeshPath[] =
	TEXT("/Game/Weapons/InsectGlaive/Meshes/Glaive/SKM_IG_Glaive.SKM_IG_Glaive");
constexpr TCHAR ExistingAttackBlueprintPath[] =
	TEXT("/Game/Blueprints/Ability/InsectGlaive/GA_IG_KinsectSlash.GA_IG_KinsectSlash");
constexpr TCHAR DefaultSlotName[] = TEXT("DefaultSlot");
constexpr TCHAR TargetTransitionID[] = TEXT("Air_KinsectPiercer_RTY");
constexpr TCHAR TargetStateName[] = TEXT("IG.Aerial.KinsectPiercer");
constexpr TCHAR TuCiSourceStateName[] = TEXT("Ground.TuCi");
constexpr TCHAR MainSectionNames[][16] = { TEXT("Start200"), TEXT("Dive201"), TEXT("DiveLoop201") };
constexpr TCHAR MainTrackNames[][16] = { TEXT("Attack"), TEXT("Action"), TEXT("Window") };
constexpr float ProvisionalMotionValue = 0.20f;
constexpr float MainBlendInTime = 0.05f;
constexpr float OrdinaryBlendOutTime = 0.10f;
constexpr float ExpectedStartSeconds = 0.391327f;
constexpr float ExpectedDiveLastKeySeconds = 0.358333f;
constexpr float ExpectedDiveCanonicalCoverageSeconds = 0.366547f;
constexpr float ExpectedDiveTailModelAnchorSeconds = 0.341667f;
constexpr float ExpectedLandingSeconds = 1.566650f;
constexpr float LandingSequenceRateScale = 2.0f;
constexpr double LandingRootMotionMaxZCm = 1.0;
constexpr double LandingRootMotionMaxZRatio = 0.01;
constexpr float LayoutTolerance = 0.001f;

struct FSetupRun
{
	bool bApply = false;
	bool bPackagesLoaded = false;
	TArray<FString> Errors;
	TArray<FString> Warnings;
	TArray<FString> SavedPackages;
	TSharedPtr<FJsonObject> Report = MakeShared<FJsonObject>();

	void Error(const FString& Message)
	{
		Errors.Add(Message);
		UE_LOG(LogTemp, Error, TEXT("[KinsectPiercerAssets] %s"), *Message);
	}

	void Warning(const FString& Message)
	{
		Warnings.Add(Message);
		UE_LOG(LogTemp, Warning, TEXT("[KinsectPiercerAssets] %s"), *Message);
	}
};

struct FSetupTags
{
	FGameplayTag InputRTY;
	FGameplayTag PhysicalY;
	FGameplayTag ModifierRT;
	FGameplayTag Unsheathed;
	FGameplayTag Falling;
	FGameplayTag DanceSourceKinsectSlash;
	FGameplayTag Grounded;
	FGameplayTag Dead;
	FGameplayTag Hitstun;
	FGameplayTag Knockdown;
	FGameplayTag AerialCantAttack;
};

struct FSequencePlan
{
	FString SourcePath;
	FString GeneratedPackage;
	FString GeneratedObjectPath;
	FString GeneratedName;
	UAnimSequence* Source = nullptr;
	UAnimSequence* Generated = nullptr;
	UAnimSequence* LegacyGenerated = nullptr;
	FString LegacyGeneratedObjectPath;
	float DesiredRuntimeSeconds = 0.0f;
	float SegmentPlayRate = 0.0f;
	bool bWasExisting = false;
	bool bIsNew = false;
	bool bChanged = false;
	bool bSourcePoseValidated = false;
	bool bLandingRootMotion = false;
	bool bSourceRootValidated = false;
};

struct FMontagePlan
{
	FString PackageName;
	FString ObjectPath;
	FString AssetName;
	UAnimMontage* Montage = nullptr;
	USkeleton* Skeleton = nullptr;
	bool bWasExisting = false;
	bool bIsNew = false;
	bool bLayoutMatches = false;
	bool bChanged = false;
	TArray<FString> NotifyFingerprintBefore;
	TArray<FString> TrackFingerprintBefore;
};

TSharedPtr<FJsonObject> NewJsonObject()
{
	return MakeShared<FJsonObject>();
}

TSharedPtr<FJsonValue> JsonObjectValue(const TSharedPtr<FJsonObject>& Object)
{
	return MakeShared<FJsonValueObject>(Object);
}

TSharedPtr<FJsonValue> JsonStringValue(const FString& Value)
{
	return MakeShared<FJsonValueString>(Value);
}

TArray<TSharedPtr<FJsonValue>> StringValues(const TArray<FString>& Values)
{
	TArray<TSharedPtr<FJsonValue>> Result;
	Result.Reserve(Values.Num());
	for (const FString& Value : Values)
	{
		Result.Add(JsonStringValue(Value));
	}
	return Result;
}

TArray<TSharedPtr<FJsonValue>> TagValues(const FGameplayTagContainer& Tags)
{
	TArray<FString> Names;
	for (const FGameplayTag& Tag : Tags)
	{
		Names.Add(Tag.ToString());
	}
	Names.Sort();
	return StringValues(Names);
}

FString VectorString(const FVector& Value)
{
	return FString::Printf(TEXT("%.6f,%.6f,%.6f"), Value.X, Value.Y, Value.Z);
}

FString FloatString(const float Value)
{
	return FString::Printf(TEXT("%.9g"), Value);
}

FString EnumString(const int32 Value)
{
	return FString::FromInt(Value);
}

FGameplayTag ResolveTag(FSetupRun& Run, const TCHAR* TagName)
{
	const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TagName), false);
	if (!Tag.IsValid())
	{
		Run.Error(FString::Printf(TEXT("Required gameplay tag is not registered: %s"), TagName));
	}
	return Tag;
}

bool ResolveTags(FSetupRun& Run, FSetupTags& OutTags)
{
	OutTags.InputRTY = ResolveTag(Run, TEXT("Input.Weapon.RTY"));
	OutTags.PhysicalY = ResolveTag(Run, TEXT("Input.Weapon.Y"));
	OutTags.ModifierRT = ResolveTag(Run, TEXT("Input.Modifier.RT"));
	OutTags.Unsheathed = ResolveTag(Run, TEXT("Combat.State.Unsheathed"));
	OutTags.Falling = ResolveTag(Run, TEXT("Combat.State.Aerial.Falling"));
	OutTags.DanceSourceKinsectSlash = ResolveTag(Run,
		TEXT("Combat.State.Aerial.Dance.Source.KinsectSlash"));
	OutTags.Grounded = ResolveTag(Run, TEXT("Combat.State.Grounded"));
	OutTags.Dead = ResolveTag(Run, TEXT("Combat.State.Dead"));
	OutTags.Hitstun = ResolveTag(Run, TEXT("Combat.State.Hitstun"));
	OutTags.Knockdown = ResolveTag(Run, TEXT("Combat.State.Knockdown"));
	OutTags.AerialCantAttack = ResolveTag(Run, TEXT("Combat.State.Aerial.CantAttack"));
	return Run.Errors.IsEmpty();
}

bool HasTagOrDescendant(const FGameplayTagContainer& Container, const FGameplayTag& Parent)
{
	if (!Parent.IsValid())
	{
		return false;
	}
	for (const FGameplayTag& Tag : Container)
	{
		if (Tag.MatchesTag(Parent))
		{
			return true;
		}
	}
	return false;
}

FString MakeAssetObjectPath(const FString& PackageName, const FString& AssetName)
{
	return PackageName + TEXT(".") + AssetName;
}

UObject* LoadAnyAsset(const FString& ObjectPath)
{
	return StaticLoadObject(UObject::StaticClass(), nullptr, *ObjectPath);
}

template <typename T>
T* LoadRequiredAsset(FSetupRun& Run, const TCHAR* ObjectPath, const TCHAR* Label)
{
	UObject* Object = LoadAnyAsset(ObjectPath);
	if (!Object)
	{
		Run.Error(FString::Printf(TEXT("Missing required %s asset: %s"), Label, ObjectPath));
		return nullptr;
	}
	T* TypedObject = Cast<T>(Object);
	if (!TypedObject)
	{
		Run.Error(FString::Printf(TEXT("Wrong asset type for %s: %s has class %s"), Label,
			ObjectPath, *Object->GetClass()->GetPathName()));
	}
	return TypedObject;
}

template <typename T>
T* LoadTargetAsset(FSetupRun& Run, const FString& ObjectPath, const TCHAR* Label,
	bool& bOutExists)
{
	UObject* Object = LoadAnyAsset(ObjectPath);
	bOutExists = Object != nullptr;
	if (!Object)
	{
		if (!Run.bApply)
		{
			Run.Error(FString::Printf(TEXT("Expected generated %s is missing: %s"), Label,
				*ObjectPath));
		}
		return nullptr;
	}
	T* TypedObject = Cast<T>(Object);
	if (!TypedObject)
	{
		Run.Error(FString::Printf(TEXT("Wrong asset type for %s: %s has class %s"), Label,
			*ObjectPath, *Object->GetClass()->GetPathName()));
	}
	return TypedObject;
}

TSharedPtr<FJsonObject> ProfileJson(const TCHAR* Name,
	const FRecordedLocalTrajectoryProfile& Profile)
{
	TSharedPtr<FJsonObject> Json = NewJsonObject();
	Json->SetStringField(TEXT("name"), Name);
	Json->SetBoolField(TEXT("valid"), Profile.IsValid());
	Json->SetStringField(TEXT("curve"), Profile.Curve ? Profile.Curve->GetPathName() : TEXT(""));
	Json->SetNumberField(TEXT("coveredSeconds"), Profile.CoveredSeconds);
	Json->SetStringField(TEXT("terminalVelocityLocal"), VectorString(Profile.TerminalVelocityLocal));
	Json->SetStringField(TEXT("terminalAccelerationLocal"),
		VectorString(Profile.TerminalAccelerationLocal));
	return Json;
}

void PreloadCombatEffectDefaults(const UInsectGlaiveCombatConfig& CombatConfig)
{
	UClass* Classes[] = {
		CombatConfig.WhiteEffectClass.Get(), CombatConfig.RedEffectClass.Get(),
		CombatConfig.OrangeEffectClass.Get(), CombatConfig.TripleUpEffectClass.Get()
	};
	for (UClass* EffectClass : Classes)
	{
		if (EffectClass && EffectClass->IsChildOf(UGameplayEffect::StaticClass()))
		{
			EffectClass->GetDefaultObject();
		}
	}
}

void AddCombatConfigReport(FSetupRun& Run, const UInsectGlaiveCombatConfig* CombatConfig)
{
	if (!CombatConfig)
	{
		return;
	}
	TSharedPtr<FJsonObject> Json = NewJsonObject();
	Json->SetStringField(TEXT("path"), CombatConfig->GetPathName());
	Json->SetArrayField(TEXT("kinsectPiercerProfiles"), {
		JsonObjectValue(ProfileJson(TEXT("KinsectPiercerStartProfile"),
			CombatConfig->KinsectPiercerStartProfile)),
		JsonObjectValue(ProfileJson(TEXT("KinsectPiercerDiveProfile"),
			CombatConfig->KinsectPiercerDiveProfile)),
		JsonObjectValue(ProfileJson(TEXT("KinsectPiercerLandingProfile"),
			CombatConfig->KinsectPiercerLandingProfile))
	});
	Json->SetNumberField(TEXT("landingBlendInSeconds"),
		CombatConfig->KinsectPiercerLandingBlendInTime);
	Run.Report->SetObjectField(TEXT("nativeCombatConfig"), Json.ToSharedRef());
}

bool HasSameSourcePose(const UAnimSequence& Source, const UAnimSequence& Generated)
{
	const IAnimationDataModel* SourceModel = Source.GetDataModel();
	const IAnimationDataModel* GeneratedModel = Generated.GetDataModel();
	if (!SourceModel || !GeneratedModel || Source.GetSkeleton() != Generated.GetSkeleton()
		|| SourceModel->GetFrameRate() != GeneratedModel->GetFrameRate()
		|| SourceModel->GetNumberOfFrames() != GeneratedModel->GetNumberOfFrames()
		|| SourceModel->GetNumberOfKeys() != GeneratedModel->GetNumberOfKeys()
		|| !FMath::IsNearlyEqual(SourceModel->GetPlayLength(), GeneratedModel->GetPlayLength(), 1.e-6))
	{
		return false;
	}
	TArray<FName> SourceTracks, GeneratedTracks;
	SourceModel->GetBoneTrackNames(SourceTracks);
	GeneratedModel->GetBoneTrackNames(GeneratedTracks);
	SourceTracks.Sort(FNameLexicalLess());
	GeneratedTracks.Sort(FNameLexicalLess());
	if (SourceTracks.IsEmpty() || SourceTracks != GeneratedTracks) { return false; }
	for (const FName Track : SourceTracks)
	{
		TArray<FTransform> SourceKeys, GeneratedKeys;
		SourceModel->GetBoneTrackTransforms(Track, SourceKeys);
		GeneratedModel->GetBoneTrackTransforms(Track, GeneratedKeys);
		if (SourceKeys.Num() != GeneratedKeys.Num()) { return false; }
		for (int32 Index = 0; Index < SourceKeys.Num(); ++Index)
		{
			if (!SourceKeys[Index].Equals(GeneratedKeys[Index], 1.e-6)) { return false; }
		}
	}
	return true;
}

bool HasSameSourceKeys(const UAnimSequence& Source, const UAnimSequence& Generated)
{
	if (!HasSameSourcePose(Source, Generated)
		|| Source.GetPlayLength() != Generated.GetPlayLength()) { return false; }
	const IAnimationDataModel* SourceModel = Source.GetDataModel();
	const IAnimationDataModel* GeneratedModel = Generated.GetDataModel();
	if (SourceModel->GetPlayLength() != GeneratedModel->GetPlayLength()) { return false; }
	TArray<FName> Tracks;
	SourceModel->GetBoneTrackNames(Tracks);
	for (const FName Name : Tracks)
	{
		TArray<FTransform> SourceKeys, GeneratedKeys;
		SourceModel->GetBoneTrackTransforms(Name, SourceKeys);
		GeneratedModel->GetBoneTrackTransforms(Name, GeneratedKeys);
		if (SourceKeys.Num() != GeneratedKeys.Num()) { return false; }
		for (int32 Index = 0; Index < SourceKeys.Num(); ++Index)
		{
			if (!SourceKeys[Index].Equals(GeneratedKeys[Index], 0.0)) { return false; }
		}
	}
	return true;
}

bool HasValidSourceRoot(const UAnimSequence& Source)
{
	const USkeleton* Skeleton = Source.GetSkeleton();
	const IAnimationDataModel* Model = Source.GetDataModel();
	if (!Skeleton || !Model || Skeleton->GetReferenceSkeleton().GetNum() == 0) { return false; }
	const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();
	const FTransform& RefRoot = RefSkeleton.GetRefBonePose()[0];
	// Modern data models address tracks by bone name. The deprecated serialized
	// BoneTreeIndex may remain INDEX_NONE on a valid imported track; it is not the
	// authoritative skeleton binding. Verify the skeleton root name and samples.
	const FName RootName = RefSkeleton.GetBoneName(0);
	if (!Model->IsValidBoneTrackName(RootName) || RefSkeleton.GetParentIndex(0) != INDEX_NONE
		|| RefRoot.ContainsNaN() || !RefRoot.GetRotation().IsNormalized()) { return false; }
	TArray<FTransform> Keys;
	Model->GetBoneTrackTransforms(RootName, Keys);
	if (Keys.Num() < 2 || Keys.Num() != Model->GetNumberOfKeys()) { return false; }
	for (const FTransform& Key : Keys)
	{
		if (Key.ContainsNaN() || !Key.GetRotation().IsNormalized()
			|| FMath::IsNearlyZero(Key.GetScale3D().X)
			|| FMath::IsNearlyZero(Key.GetScale3D().Y)
			|| FMath::IsNearlyZero(Key.GetScale3D().Z)) { return false; }
	}
	return true;
}

TSharedPtr<FJsonObject> RootMotionTransformJson(const FTransform& Transform)
{
	TSharedPtr<FJsonObject> Json = NewJsonObject();
	const FVector Translation = Transform.GetTranslation();
	const FQuat Rotation = Transform.GetRotation();
	const FVector Scale = Transform.GetScale3D();
	Json->SetArrayField(TEXT("translationCm"), {
		MakeShared<FJsonValueNumber>(Translation.X), MakeShared<FJsonValueNumber>(Translation.Y),
		MakeShared<FJsonValueNumber>(Translation.Z) });
	Json->SetArrayField(TEXT("rotationQuaternionXYZW"), {
		MakeShared<FJsonValueNumber>(Rotation.X), MakeShared<FJsonValueNumber>(Rotation.Y),
		MakeShared<FJsonValueNumber>(Rotation.Z), MakeShared<FJsonValueNumber>(Rotation.W) });
	Json->SetArrayField(TEXT("scaleXYZ"), {
		MakeShared<FJsonValueNumber>(Scale.X), MakeShared<FJsonValueNumber>(Scale.Y),
		MakeShared<FJsonValueNumber>(Scale.Z) });
	return Json;
}

bool AuditLandingRootMotion(FSetupRun& Run, const FSequencePlan& Plan)
{
	const UAnimSequence* Sequence = Plan.Generated ? Plan.Generated : Plan.Source;
	TSharedPtr<FJsonObject> Json = NewJsonObject();
	Json->SetStringField(TEXT("extractionAPI"), TEXT("UAnimSequence::ExtractRootMotion(FAnimExtractContext)"));
	Json->SetBoolField(TEXT("sourceRootValid"), Plan.bSourceRootValidated);
	Json->SetNumberField(TEXT("maximumAllowedAbsZCm"), LandingRootMotionMaxZCm);
	Json->SetNumberField(TEXT("maximumAllowedZToHorizontalRatio"), LandingRootMotionMaxZRatio);
	Run.Report->SetObjectField(TEXT("landing203RootMotion"), Json.ToSharedRef());
	if (!Sequence || !Plan.bSourceRootValidated) { return false; }
	Json->SetStringField(TEXT("sequence"), Sequence->GetPathName());
	Json->SetStringField(TEXT("sourceRootBone"), Plan.Source->GetSkeleton()->GetReferenceSkeleton().GetBoneName(0).ToString());
	// Extract in the engine's native component axes. Never rotate or rewrite root keys.
	const float RawLength = Sequence->GetPlayLength();
	// Visit every source frame and its midpoint. A fixed 32-interval cap can
	// miss a one-key Z spike even when cloning and endpoint checks both pass.
	const int32 Intervals = FMath::Max(2, 2 * (Sequence->GetNumberOfSampledKeys() - 1));
	bool bFinite = true;
	double MaxHorizontal = 0.0, MaxAbsZ = 0.0, CompleteHorizontal = 0.0;
	TArray<TSharedPtr<FJsonValue>> Samples;
	for (int32 Index = 0; Index <= Intervals; ++Index)
	{
		const float Time = Index == Intervals ? RawLength : RawLength * (float(Index) / Intervals);
		FDeltaTimeRecord Delta;
		Delta.Set(0.0f, Time);
		const FTransform Motion = Sequence->ExtractRootMotion(FAnimExtractContext(0.0, true, Delta, false));
		const bool bSampleFinite = !Motion.ContainsNaN() && Motion.GetRotation().IsNormalized();
		bFinite &= bSampleFinite;
		TSharedPtr<FJsonObject> Sample = NewJsonObject();
		Sample->SetNumberField(TEXT("rawTimeSeconds"), Time);
		Sample->SetNumberField(TEXT("runtimeTimeSeconds"), Time / (Plan.SegmentPlayRate * LandingSequenceRateScale));
		Sample->SetBoolField(TEXT("finite"), bSampleFinite);
		if (bSampleFinite)
		{
			Sample->SetObjectField(TEXT("transform"), RootMotionTransformJson(Motion).ToSharedRef());
			const FVector Translation = Motion.GetTranslation();
			MaxHorizontal = FMath::Max(MaxHorizontal, Translation.Size2D());
			MaxAbsZ = FMath::Max(MaxAbsZ, FMath::Abs(Translation.Z));
			if (Index == Intervals)
			{
				CompleteHorizontal = Translation.Size2D();
				Json->SetObjectField(TEXT("completeTransform"), RootMotionTransformJson(Motion).ToSharedRef());
			}
		}
		Samples.Add(JsonObjectValue(Sample));
	}
	const bool bNonzeroHorizontal = MaxHorizontal > KINDA_SMALL_NUMBER && CompleteHorizontal > KINDA_SMALL_NUMBER;
	const bool bMinimalZ = MaxAbsZ <= LandingRootMotionMaxZCm
		&& MaxAbsZ <= MaxHorizontal * LandingRootMotionMaxZRatio;
	Json->SetArrayField(TEXT("sampleTrajectory"), MoveTemp(Samples));
	Json->SetNumberField(TEXT("rawRangeStartSeconds"), 0.0);
	Json->SetNumberField(TEXT("rawRangeEndSeconds"), RawLength);
	Json->SetNumberField(TEXT("sampleCount"), Intervals + 1);
	Json->SetNumberField(TEXT("maximumHorizontalCm"), MaxHorizontal);
	Json->SetNumberField(TEXT("completeHorizontalCm"), CompleteHorizontal);
	Json->SetNumberField(TEXT("maximumAbsZCm"), MaxAbsZ);
	Json->SetBoolField(TEXT("finite"), bFinite);
	Json->SetBoolField(TEXT("nonzeroHorizontal"), bNonzeroHorizontal);
	Json->SetBoolField(TEXT("minimalZ"), bMinimalZ);
	if (!bFinite || !bNonzeroHorizontal || !bMinimalZ)
	{
		Run.Error(FString::Printf(TEXT("Landing203 engine-extracted root motion is invalid or has anomalous Z: finite=%d horizontal=%.9g maxZ=%.9g (%s)"),
			bFinite, MaxHorizontal, MaxAbsZ, *Sequence->GetPathName()));
		return false;
	}
	return true;
}

bool MakeSequencePlan(FSetupRun& Run, FSequencePlan& Plan, const TCHAR* SourcePath,
	const TCHAR* GeneratedName, const float DesiredRuntimeSeconds, const bool bLandingRootMotion = false)
{
	Plan.bLandingRootMotion = bLandingRootMotion;
	Plan.SourcePath = SourcePath;
	Plan.GeneratedName = GeneratedName;
	Plan.GeneratedPackage = FString::Printf(TEXT("%s/%s"), SequenceDirectory, GeneratedName);
	Plan.GeneratedObjectPath = MakeAssetObjectPath(Plan.GeneratedPackage, GeneratedName);
	Plan.DesiredRuntimeSeconds = DesiredRuntimeSeconds;
	Plan.Source = LoadRequiredAsset<UAnimSequence>(Run, SourcePath, TEXT("source sequence"));
	if (!Plan.Source)
	{
		return false;
	}
	if (!Plan.Source->GetSkeleton() || !FMath::IsFinite(Plan.Source->GetPlayLength())
		|| Plan.Source->GetPlayLength() <= KINDA_SMALL_NUMBER
		|| !FMath::IsFinite(Plan.Source->RateScale)
		|| FMath::IsNearlyZero(Plan.Source->RateScale)
		|| Plan.Source->GetNumberOfSampledKeys() < 2)
	{
		Run.Error(FString::Printf(TEXT("Source sequence has invalid skeleton/length/rate/keys: %s"),
			SourcePath));
		return false;
	}

	if (Plan.bLandingRootMotion)
	{
		Plan.bSourceRootValidated = HasValidSourceRoot(*Plan.Source);
		if (!Plan.bSourceRootValidated || Plan.Source->RateScale != LandingSequenceRateScale)
		{
			Run.Error(TEXT("Landing203 source root track/reference pose or source RateScale=2 contract is invalid."));
			return false;
		}
		Plan.LegacyGeneratedObjectPath = LegacyLandingSequencePath;
		UObject* Legacy = LoadAnyAsset(Plan.LegacyGeneratedObjectPath);
		Plan.LegacyGenerated = Cast<UAnimSequence>(Legacy);
		if (Legacy && !Plan.LegacyGenerated)
		{
			Run.Error(TEXT("Legacy Over_IP is not an animation sequence; refusing migration."));
			return false;
		}
	}

	Plan.Generated = LoadTargetAsset<UAnimSequence>(Run, Plan.GeneratedObjectPath,
		TEXT("generated presentation sequence"), Plan.bWasExisting);
	Plan.bIsNew = !Plan.bWasExisting && Run.bApply;
	if (Plan.Generated)
	{
		if (Plan.Generated->GetSkeleton() != Plan.Source->GetSkeleton())
		{
			Run.Error(FString::Printf(TEXT("Generated sequence has a different skeleton; refusing to replace it: %s"),
				*Plan.GeneratedObjectPath));
			return false;
		}
		Plan.bSourcePoseValidated = Plan.bLandingRootMotion
			? HasSameSourceKeys(*Plan.Source, *Plan.Generated) : HasSameSourcePose(*Plan.Source, *Plan.Generated);
		if (!Plan.bSourcePoseValidated)
		{
			Run.Error(FString::Printf(TEXT("Generated sequence no longer preserves source timing/bone samples; refusing to overwrite: %s"),
				*Plan.GeneratedObjectPath));
			return false;
		}
		if (Plan.bLandingRootMotion && Plan.Generated->RateScale != LandingSequenceRateScale)
		{
			Run.Error(TEXT("Existing Landing203 _RM RateScale must be exactly 2; refusing to change timing."));
			return false;
		}
		const ERootMotionRootLock::Type RootLock = Plan.bLandingRootMotion
			? ERootMotionRootLock::AnimFirstFrame : ERootMotionRootLock::RefPose;
		if (!Run.bApply && (Plan.Generated->bEnableRootMotion != Plan.bLandingRootMotion
			|| !Plan.Generated->bForceRootLock || Plan.Generated->RootMotionRootLock != RootLock))
		{
			Run.Error(FString::Printf(TEXT("Generated presentation sequence root-lock settings are incorrect: %s"),
				*Plan.GeneratedObjectPath));
			return false;
		}
	}

	const UAnimSequence* RateSequence = Plan.Generated ? Plan.Generated : Plan.Source;
	const float SegmentLength = RateSequence->GetPlayLength();
	const float SequenceRateScale = Plan.bLandingRootMotion ? LandingSequenceRateScale : FMath::Abs(RateSequence->RateScale);
	if (!FMath::IsFinite(Plan.DesiredRuntimeSeconds)
		|| Plan.DesiredRuntimeSeconds <= KINDA_SMALL_NUMBER
		|| !FMath::IsFinite(SegmentLength) || SegmentLength <= KINDA_SMALL_NUMBER
		|| !FMath::IsFinite(SequenceRateScale) || SequenceRateScale <= KINDA_SMALL_NUMBER)
	{
		Run.Error(FString::Printf(TEXT("Cannot derive montage rate for sequence: %s"),
			*Plan.GeneratedObjectPath));
		return false;
	}
	Plan.SegmentPlayRate = SegmentLength
		/ (Plan.DesiredRuntimeSeconds * SequenceRateScale);
	return FMath::IsFinite(Plan.SegmentPlayRate) && Plan.SegmentPlayRate > 0.0f;
}

TSharedPtr<FJsonObject> SequenceJson(const FSequencePlan& Plan)
{
	TSharedPtr<FJsonObject> Json = NewJsonObject();
	Json->SetStringField(TEXT("source"), Plan.Source ? Plan.Source->GetPathName() : Plan.SourcePath);
	Json->SetStringField(TEXT("generated"), Plan.GeneratedObjectPath);
	Json->SetBoolField(TEXT("generatedExists"), Plan.Generated != nullptr);
	Json->SetBoolField(TEXT("generatedPlannedNew"), Plan.bIsNew);
	if (Plan.Source)
	{
		Json->SetNumberField(TEXT("sourcePlayLength"), Plan.Source->GetPlayLength());
		Json->SetNumberField(TEXT("sourceRateScale"), Plan.Source->RateScale);
		Json->SetStringField(TEXT("sourceSkeleton"), Plan.Source->GetSkeleton()
			? Plan.Source->GetSkeleton()->GetPathName() : TEXT(""));
	}
	const UAnimSequence* RateSequence = Plan.Generated ? Plan.Generated : Plan.Source;
	if (RateSequence)
	{
		Json->SetNumberField(TEXT("generatedPlayLength"), RateSequence->GetPlayLength());
		Json->SetNumberField(TEXT("sequenceRateScale"), RateSequence->RateScale);
		Json->SetNumberField(TEXT("segmentPlayRate"), Plan.SegmentPlayRate);
		Json->SetNumberField(TEXT("runtimeSecondsReadback"), RateSequence->GetPlayLength()
			/ (FMath::Max(Plan.SegmentPlayRate, KINDA_SMALL_NUMBER)
				* FMath::Abs(RateSequence->RateScale)));
		Json->SetBoolField(TEXT("rootMotionEnabled"), RateSequence->bEnableRootMotion);
		Json->SetBoolField(TEXT("forceRootLock"), RateSequence->bForceRootLock);
		Json->SetNumberField(TEXT("rootMotionRootLock"),
			static_cast<int32>(RateSequence->RootMotionRootLock.GetValue()));
	}
	Json->SetNumberField(TEXT("desiredRuntimeSeconds"), Plan.DesiredRuntimeSeconds);
	Json->SetBoolField(TEXT("duplicatePreservesSourceAnimationAndSkeleton"), Plan.bSourcePoseValidated);
	if (Plan.bLandingRootMotion)
	{
		Json->SetBoolField(TEXT("sourceRootValid"), Plan.bSourceRootValidated);
		Json->SetStringField(TEXT("preservedLegacySequence"), Plan.LegacyGeneratedObjectPath);
		Json->SetBoolField(TEXT("legacySequenceExists"), Plan.LegacyGenerated != nullptr);
	}
	return Json;
}

bool HasBoneOrSocket(const USkeletalMesh& Mesh, const FName Name)
{
	if (Name.IsNone())
	{
		return false;
	}
	if (Mesh.GetRefSkeleton().FindBoneIndex(Name) != INDEX_NONE)
	{
		return true;
	}
	int32 SocketIndex = INDEX_NONE;
	return Mesh.FindSocketAndIndex(Name, SocketIndex) != nullptr;
}

bool FindMeshSocket(const USkeletalMesh* Mesh, const FName Name, int32& OutIndex)
{
	OutIndex = INDEX_NONE;
	return Mesh && Mesh->FindSocketAndIndex(Name, OutIndex) != nullptr;
}

TSharedPtr<FJsonObject> MeshJson(const USkeletalMesh* Mesh, const bool bRootBoneFound,
	const bool bRootSocketFound, const int32 RootSocketIndex, const bool bFrontTipFound,
	const int32 FrontTipSocketIndex)
{
	TSharedPtr<FJsonObject> Json = NewJsonObject();
	Json->SetStringField(TEXT("path"), Mesh ? Mesh->GetPathName() : GlaiveMeshPath);
	Json->SetStringField(TEXT("assetClass"), Mesh ? Mesh->GetClass()->GetPathName() : TEXT(""));
	Json->SetStringField(TEXT("skeleton"), Mesh && Mesh->GetSkeleton()
		? Mesh->GetSkeleton()->GetPathName() : TEXT(""));
	Json->SetBoolField(TEXT("rootBonePresent"), bRootBoneFound);
	Json->SetBoolField(TEXT("rootSocketPresent"), bRootSocketFound);
	Json->SetBoolField(TEXT("rootPresent"), bRootBoneFound || bRootSocketFound);
	Json->SetNumberField(TEXT("rootSocketIndex"), RootSocketIndex);
	Json->SetBoolField(TEXT("IG_FrontTipSocketPresent"), bFrontTipFound);
	Json->SetNumberField(TEXT("IG_FrontTipSocketIndex"), FrontTipSocketIndex);
	return Json;
}

bool SegmentMatches(const FAnimSegment& Segment, const FSequencePlan& Plan,
	const float Start, const float Duration)
{
	const UAnimSequenceBase* ActualSequence = Segment.GetAnimReference();
	const UAnimSequence* RateSequence = Plan.Generated ? Plan.Generated : Plan.Source;
	return ActualSequence && RateSequence
		&& ActualSequence->GetPathName() == Plan.GeneratedObjectPath
		&& FMath::IsNearlyEqual(Segment.StartPos, Start, LayoutTolerance)
		&& FMath::IsNearlyEqual(Segment.AnimStartTime, 0.0f, LayoutTolerance)
		&& FMath::IsNearlyEqual(Segment.AnimEndTime, RateSequence->GetPlayLength(), LayoutTolerance)
		&& FMath::IsNearlyEqual(Segment.AnimPlayRate, Plan.SegmentPlayRate, LayoutTolerance)
		&& Segment.LoopingCount == 1
		&& FMath::IsNearlyEqual((Segment.AnimEndTime - Segment.AnimStartTime)
			* FMath::Max(Segment.LoopingCount, 1)
			/ (FMath::Max(Segment.AnimPlayRate, KINDA_SMALL_NUMBER)
				* FMath::Abs(RateSequence->RateScale)), Duration, LayoutTolerance);
}

bool LandingLayoutMatches(const UAnimMontage& Montage, const FSequencePlan& Plan,
	const UAnimSequence* ExpectedSequence, const FString& ExpectedPath)
{
	if (!ExpectedSequence || Montage.GetSkeleton() != ExpectedSequence->GetSkeleton()
		|| Montage.RateScale != 1.0f || ExpectedSequence->RateScale != LandingSequenceRateScale
		|| Plan.DesiredRuntimeSeconds != ExpectedLandingSeconds
		|| Montage.SlotAnimTracks.Num() != 1 || Montage.CompositeSections.Num() != 1) { return false; }
	const FSlotAnimationTrack& Slot = Montage.SlotAnimTracks[0];
	if (Slot.SlotName != FName(DefaultSlotName) || Slot.AnimTrack.AnimSegments.Num() != 1) { return false; }
	const FAnimSegment& Segment = Slot.AnimTrack.AnimSegments[0];
	const FCompositeSection& Section = Montage.CompositeSections[0];
	return Segment.GetAnimReference() == ExpectedSequence && ExpectedSequence->GetPathName() == ExpectedPath
		&& Segment.StartPos == 0.0f && Segment.AnimStartTime == 0.0f
		&& Segment.AnimEndTime == ExpectedSequence->GetPlayLength()
		&& Segment.AnimPlayRate == Plan.SegmentPlayRate && Segment.LoopingCount == 1
		&& Section.SectionName == FName(TEXT("Landing203")) && Section.NextSectionName.IsNone()
		&& Section.GetTime() == 0.0f
		// Accept only the canonical stored duration or the engine's float-rounded segment length.
		&& (Montage.GetPlayLength() == Plan.DesiredRuntimeSeconds || Montage.GetPlayLength() == Segment.GetLength());
}

bool CanMigrateLegacyLanding(const UAnimMontage& Montage, const FSequencePlan& Plan)
{
	return Plan.bLandingRootMotion && Plan.bSourceRootValidated && Plan.Source && Plan.LegacyGenerated
		&& HasSameSourceKeys(*Plan.Source, *Plan.LegacyGenerated)
		&& LandingLayoutMatches(Montage, Plan, Plan.LegacyGenerated, Plan.LegacyGeneratedObjectPath);
}

bool MontageLayoutMatches(const UAnimMontage& Montage, const bool bMain,
	const FSequencePlan& StartPlan, const FSequencePlan& DivePlan,
	const FSequencePlan& LoopPlan, const FSequencePlan& LandingPlan)
{
	if (!bMain && LandingPlan.bLandingRootMotion)
	{
		return LandingLayoutMatches(Montage, LandingPlan, LandingPlan.Generated, LandingPlan.GeneratedObjectPath);
	}
	if (Montage.SlotAnimTracks.Num() != 1 || Montage.CompositeSections.Num() != (bMain ? 3 : 1))
	{
		return false;
	}
	const FSlotAnimationTrack& Slot = Montage.SlotAnimTracks[0];
	const TArray<FAnimSegment>& Segments = Slot.AnimTrack.AnimSegments;
	if (Slot.SlotName != FName(DefaultSlotName) || Segments.Num() != (bMain ? 3 : 1))
	{
		return false;
	}
	if (bMain)
	{
		const float StartTime = StartPlan.DesiredRuntimeSeconds;
		const float DiveStart = StartTime;
		const float LoopStart = DiveStart + DivePlan.DesiredRuntimeSeconds;
		if (!SegmentMatches(Segments[0], StartPlan, 0.0f, StartTime)
			|| !SegmentMatches(Segments[1], DivePlan, DiveStart,
				DivePlan.DesiredRuntimeSeconds)
			|| !SegmentMatches(Segments[2], LoopPlan, LoopStart,
				LoopPlan.DesiredRuntimeSeconds))
		{
			return false;
		}
		const FCompositeSection& StartSection = Montage.CompositeSections[0];
		const FCompositeSection& DiveSection = Montage.CompositeSections[1];
		const FCompositeSection& LoopSection = Montage.CompositeSections[2];
		return StartSection.SectionName == FName(MainSectionNames[0])
			&& StartSection.NextSectionName == FName(MainSectionNames[1])
			&& FMath::IsNearlyEqual(StartSection.GetTime(), 0.0f, LayoutTolerance)
			&& DiveSection.SectionName == FName(MainSectionNames[1])
			&& DiveSection.NextSectionName == FName(MainSectionNames[2])
			&& FMath::IsNearlyEqual(DiveSection.GetTime(), DiveStart, LayoutTolerance)
			&& LoopSection.SectionName == FName(MainSectionNames[2])
			&& LoopSection.NextSectionName == FName(MainSectionNames[2])
			&& FMath::IsNearlyEqual(LoopSection.GetTime(), LoopStart, LayoutTolerance);
	}

	return SegmentMatches(Segments[0], LandingPlan, 0.0f,
		LandingPlan.DesiredRuntimeSeconds)
		&& Montage.CompositeSections[0].SectionName == FName(TEXT("Landing203"))
		&& Montage.CompositeSections[0].NextSectionName.IsNone()
		&& FMath::IsNearlyEqual(Montage.CompositeSections[0].GetTime(), 0.0f,
			LayoutTolerance);
}

FString LinkFingerprint(const FAnimLinkableElement& Element)
{
	// Include serialized cached link values and linked objects, not just the public link method.
	FString Result;
	FAnimLinkableElement::StaticStruct()->ExportText(Result, &Element, nullptr, nullptr, PPF_None, nullptr);
	return Result;
}

TArray<FString> MontageNotifyFingerprints(const UAnimMontage& Montage)
{
	TArray<FString> Result;
	Result.Reserve(Montage.Notifies.Num());
	for (const FAnimNotifyEvent& Event : Montage.Notifies)
	{
		const FString NotifyClass = Event.Notify ? Event.Notify->GetClass()->GetPathName() : TEXT("");
		const FString StateClass = Event.NotifyStateClass
			? Event.NotifyStateClass->GetClass()->GetPathName() : TEXT("");
#if WITH_EDITORONLY_DATA
		const FString Guid = Event.Guid.ToString();
#else
		const FString Guid;
#endif
		Result.Add(FString::Printf(TEXT("%s|%s|%s|%d|%.9g|%.9g|%.9g|%d|%d"),
			*Guid, *Event.NotifyName.ToString(), *NotifyClass, Event.TrackIndex,
			Event.GetTriggerTime(), Event.GetEndTriggerTime(), Event.GetDuration(),
			static_cast<int32>(Event.GetLinkMethod()),
			static_cast<int32>(Event.EndLink.GetLinkMethod())) + TEXT("|") + StateClass
			+ TEXT("|") + LinkFingerprint(Event) + TEXT("|") + LinkFingerprint(Event.EndLink)
			+ TEXT("|") + FloatString(Event.TriggerTimeOffset) + TEXT("|") + FloatString(Event.EndTriggerTimeOffset));
	}
	return Result;
}

TArray<FString> MontageTrackFingerprints(const UAnimMontage& Montage)
{
	TArray<FString> Result;
	Result.Reserve(Montage.AnimNotifyTracks.Num());
	for (int32 Index = 0; Index < Montage.AnimNotifyTracks.Num(); ++Index)
	{
		const FAnimNotifyTrack& Track = Montage.AnimNotifyTracks[Index];
		Result.Add(FString::Printf(TEXT("%d|%s|%s"), Index, *Track.TrackName.ToString(),
			*Track.TrackColor.ToString()));
	}
	return Result;
}

TArray<FString> MontageTimingFingerprints(const UAnimMontage& Montage)
{
	TArray<FString> Result = { FloatString(Montage.RateScale), FloatString(Montage.GetPlayLength()) };
	for (const FSlotAnimationTrack& Slot : Montage.SlotAnimTracks)
	{
		Result.Add(Slot.SlotName.ToString());
		for (const FAnimSegment& Segment : Slot.AnimTrack.AnimSegments)
		{
			Result.Add(FString::Printf(TEXT("%.9g|%.9g|%.9g|%.9g|%d"), Segment.StartPos,
				Segment.AnimStartTime, Segment.AnimEndTime, Segment.AnimPlayRate, Segment.LoopingCount));
#if WITH_EDITORONLY_DATA
			Result.Add(FloatString(Segment.CachedPlayLength));
#endif
		}
	}
	for (const FCompositeSection& Section : Montage.CompositeSections)
	{
		Result.Add(Section.SectionName.ToString() + TEXT("|") + Section.NextSectionName.ToString()
			+ TEXT("|") + LinkFingerprint(Section));
	}
	return Result;
}

bool MigrateLegacyLanding(FSetupRun& Run, FMontagePlan& Plan, const FSequencePlan& LandingPlan)
{
	if (!Run.bApply || !Plan.Montage || !LandingPlan.Generated
		|| !CanMigrateLegacyLanding(*Plan.Montage, LandingPlan)
		|| !HasSameSourceKeys(*LandingPlan.Source, *LandingPlan.Generated)
		|| LandingPlan.Generated->RateScale != LandingSequenceRateScale) { return false; }
	const TArray<FString> NotifiesBefore = MontageNotifyFingerprints(*Plan.Montage);
	const TArray<FString> TracksBefore = MontageTrackFingerprints(*Plan.Montage);
	const TArray<FString> TimingsBefore = MontageTimingFingerprints(*Plan.Montage);
	FAnimSegment& Segment = Plan.Montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0];
	const FAnimSegment SegmentBefore = Segment;
	Plan.Montage->Modify();
	Segment.SetAnimReference(LandingPlan.Generated, false);
#if WITH_EDITORONLY_DATA
	// SetAnimReference updates this cache even without initialization; preserve its original bytes.
	Segment.CachedPlayLength = SegmentBefore.CachedPlayLength;
#endif
	if (!LandingLayoutMatches(*Plan.Montage, LandingPlan, LandingPlan.Generated, LandingPlan.GeneratedObjectPath)
		|| NotifiesBefore != MontageNotifyFingerprints(*Plan.Montage)
		|| TracksBefore != MontageTrackFingerprints(*Plan.Montage)
		|| TimingsBefore != MontageTimingFingerprints(*Plan.Montage))
	{
		Segment = SegmentBefore;
		Run.Error(TEXT("Landing203 pointer-only migration changed timing/notify/link/track fingerprints; refusing save."));
		return false;
	}
	// Do not finalize length, update linkable elements, or refresh notify caches on this path.
	Plan.bChanged = true;
	Plan.bLayoutMatches = true;
	return true;
}

TSharedPtr<FJsonObject> NotifyJson(const FAnimNotifyEvent& Event, const int32 ArrayIndex)
{
	TSharedPtr<FJsonObject> Json = NewJsonObject();
	Json->SetNumberField(TEXT("arrayIndex"), ArrayIndex);
#if WITH_EDITORONLY_DATA
	Json->SetStringField(TEXT("guid"), Event.Guid.ToString());
#else
	Json->SetStringField(TEXT("guid"), TEXT("unavailable_without_editoronly_data"));
#endif
	Json->SetStringField(TEXT("notifyName"), Event.NotifyName.ToString());
	Json->SetStringField(TEXT("notifyEventName"), Event.GetNotifyEventName().ToString());
	Json->SetStringField(TEXT("notifyClass"), Event.Notify
		? Event.Notify->GetClass()->GetPathName() : TEXT(""));
	Json->SetStringField(TEXT("notifyStateClass"), Event.NotifyStateClass
		? Event.NotifyStateClass->GetClass()->GetPathName() : TEXT(""));
	Json->SetNumberField(TEXT("trackIndex"), Event.TrackIndex);
	Json->SetNumberField(TEXT("startSeconds"), Event.GetTriggerTime());
	Json->SetNumberField(TEXT("endSeconds"), Event.GetEndTriggerTime());
	Json->SetNumberField(TEXT("durationSeconds"), Event.GetDuration());
	Json->SetNumberField(TEXT("startLinkMethod"), static_cast<int32>(Event.GetLinkMethod()));
	Json->SetNumberField(TEXT("endLinkMethod"),
		static_cast<int32>(Event.EndLink.GetLinkMethod()));
	return Json;
}

TSharedPtr<FJsonObject> MontageJson(const FMontagePlan& Plan)
{
	TSharedPtr<FJsonObject> Json = NewJsonObject();
	Json->SetStringField(TEXT("path"), Plan.ObjectPath);
	Json->SetBoolField(TEXT("exists"), Plan.Montage != nullptr);
	Json->SetBoolField(TEXT("plannedNew"), Plan.bIsNew);
	if (!Plan.Montage)
	{
		return Json;
	}
	Json->SetStringField(TEXT("skeleton"), Plan.Montage->GetSkeleton()
		? Plan.Montage->GetSkeleton()->GetPathName() : TEXT(""));
	Json->SetBoolField(TEXT("layoutMatches"), Plan.bLayoutMatches);
	Json->SetBoolField(TEXT("enableAutoBlendOut"), Plan.Montage->bEnableAutoBlendOut);
	Json->SetNumberField(TEXT("blendInSeconds"), Plan.Montage->BlendIn.GetBlendTime());
	Json->SetNumberField(TEXT("blendOutSeconds"), Plan.Montage->BlendOut.GetBlendTime());
	Json->SetNumberField(TEXT("playLengthSeconds"), Plan.Montage->GetPlayLength());

	TArray<TSharedPtr<FJsonValue>> Sections;
	for (const FCompositeSection& Section : Plan.Montage->CompositeSections)
	{
		TSharedPtr<FJsonObject> SectionJson = NewJsonObject();
		SectionJson->SetStringField(TEXT("name"), Section.SectionName.ToString());
		SectionJson->SetNumberField(TEXT("startSeconds"), Section.GetTime());
		SectionJson->SetStringField(TEXT("nextSection"), Section.NextSectionName.ToString());
		Sections.Add(JsonObjectValue(SectionJson));
	}
	Json->SetArrayField(TEXT("sections"), MoveTemp(Sections));

	TArray<TSharedPtr<FJsonValue>> Slots;
	for (const FSlotAnimationTrack& Slot : Plan.Montage->SlotAnimTracks)
	{
		TSharedPtr<FJsonObject> SlotJson = NewJsonObject();
		SlotJson->SetStringField(TEXT("slotName"), Slot.SlotName.ToString());
		TArray<TSharedPtr<FJsonValue>> Segments;
		for (const FAnimSegment& Segment : Slot.AnimTrack.AnimSegments)
		{
			TSharedPtr<FJsonObject> SegmentJson = NewJsonObject();
			const UAnimSequenceBase* Sequence = Segment.GetAnimReference();
			SegmentJson->SetStringField(TEXT("sequence"), Sequence ? Sequence->GetPathName() : TEXT(""));
			SegmentJson->SetNumberField(TEXT("startSeconds"), Segment.StartPos);
			SegmentJson->SetNumberField(TEXT("animationStart"), Segment.AnimStartTime);
			SegmentJson->SetNumberField(TEXT("animationEnd"), Segment.AnimEndTime);
			SegmentJson->SetNumberField(TEXT("segmentPlayRate"), Segment.AnimPlayRate);
			SegmentJson->SetNumberField(TEXT("loopingCount"), Segment.LoopingCount);
			const float RateScale = Sequence ? FMath::Abs(Sequence->RateScale) : 0.0f;
			SegmentJson->SetNumberField(TEXT("runtimeSecondsReadback"),
				RateScale > KINDA_SMALL_NUMBER && Segment.AnimPlayRate > KINDA_SMALL_NUMBER
					? (Segment.AnimEndTime - Segment.AnimStartTime)
						* FMath::Max(Segment.LoopingCount, 1) / (Segment.AnimPlayRate * RateScale)
					: 0.0f);
			Segments.Add(JsonObjectValue(SegmentJson));
		}
		SlotJson->SetArrayField(TEXT("segments"), MoveTemp(Segments));
		Slots.Add(JsonObjectValue(SlotJson));
	}
	Json->SetArrayField(TEXT("slots"), MoveTemp(Slots));

	TArray<TSharedPtr<FJsonValue>> Tracks;
	for (int32 Index = 0; Index < Plan.Montage->AnimNotifyTracks.Num(); ++Index)
	{
		const FAnimNotifyTrack& Track = Plan.Montage->AnimNotifyTracks[Index];
		TSharedPtr<FJsonObject> TrackJson = NewJsonObject();
		TrackJson->SetNumberField(TEXT("index"), Index);
		TrackJson->SetStringField(TEXT("name"), Track.TrackName.ToString());
		TrackJson->SetStringField(TEXT("color"), Track.TrackColor.ToString());
		Tracks.Add(JsonObjectValue(TrackJson));
	}
	Json->SetArrayField(TEXT("notifyTracks"), MoveTemp(Tracks));

	TArray<TSharedPtr<FJsonValue>> Notifies;
	for (int32 Index = 0; Index < Plan.Montage->Notifies.Num(); ++Index)
	{
		Notifies.Add(JsonObjectValue(NotifyJson(Plan.Montage->Notifies[Index], Index)));
	}
	Json->SetArrayField(TEXT("notifies"), MoveTemp(Notifies));
	Json->SetArrayField(TEXT("notifyFingerprintFields"),
		StringValues(MontageNotifyFingerprints(*Plan.Montage)));
	Json->SetArrayField(TEXT("trackFingerprintFields"),
		StringValues(MontageTrackFingerprints(*Plan.Montage)));
	return Json;
}

bool HasRequiredBaseNotifyTracks(const UAnimMontage& Montage, const bool bMainMontage)
{
	// The generated airborne montage owns the fixed Attack/Action/Window layout.
	// After E1, the Over montage may have user-owned Combo/Dodge tracks (and added
	// attack tracks); only its stable Attack/Action base tracks are constrained.
	const int32 RequiredTrackCount = bMainMontage ? UE_ARRAY_COUNT(MainTrackNames) : 3;
	if (Montage.AnimNotifyTracks.Num() < RequiredTrackCount)
	{
		return false;
	}
	const int32 RequiredBaseCount = bMainMontage ? UE_ARRAY_COUNT(MainTrackNames) : 2;
	for (int32 Index = 0; Index < RequiredBaseCount; ++Index)
	{
		if (Montage.AnimNotifyTracks[Index].TrackName != FName(MainTrackNames[Index]))
		{
			return false;
		}
	}
	if (bMainMontage
		&& Montage.AnimNotifyTracks[2].TrackName != FName(MainTrackNames[2]))
	{
		return false;
	}
	return true;
}

void AddMontageSection(UAnimMontage& Montage, const FName Name, const float Time,
	const FName NextSection)
{
	FCompositeSection& Section = Montage.CompositeSections.AddDefaulted_GetRef();
	Section.SectionName = Name;
	Section.Link(&Montage, Time, 0);
	Section.SetTime(Time);
	Section.NextSectionName = NextSection;
}

FAnimSegment& AddMontageSegment(UAnimMontage& Montage, FSequencePlan& Plan,
	const float StartSeconds)
{
	FAnimSegment& Segment = Montage.SlotAnimTracks[0].AnimTrack.AnimSegments.AddDefaulted_GetRef();
	Segment.SetAnimReference(Plan.Generated, true);
	Segment.StartPos = StartSeconds;
	Segment.AnimStartTime = 0.0f;
	Segment.AnimEndTime = Plan.Generated->GetPlayLength();
	Segment.AnimPlayRate = Plan.SegmentPlayRate;
	Segment.LoopingCount = 1;
	return Segment;
}

bool FinalizeMontageLength(UAnimMontage& Montage)
{
	const float CalculatedLength = Montage.CalculateSequenceLength();
	if (!FMath::IsFinite(CalculatedLength) || CalculatedLength <= KINDA_SMALL_NUMBER)
	{
		return false;
	}
	Montage.SetCompositeLength(CalculatedLength);
	Montage.UpdateLinkableElements();
	Montage.RefreshCacheData();
	return FMath::IsNearlyEqual(Montage.GetPlayLength(), CalculatedLength, 0.001f);
}

void BuildMontageLayout(FMontagePlan& Plan, FSequencePlan& StartPlan,
	FSequencePlan& DivePlan, FSequencePlan& LoopPlan, FSequencePlan& LandingPlan,
	const bool bMain)
{
	UAnimMontage& Montage = *Plan.Montage;
	Montage.SlotAnimTracks.Reset();
	Montage.CompositeSections.Reset();
	FSlotAnimationTrack& Slot = Montage.SlotAnimTracks.AddDefaulted_GetRef();
	Slot.SlotName = FName(DefaultSlotName);
	if (bMain)
	{
		const float StartTime = StartPlan.DesiredRuntimeSeconds;
		const float DiveStart = StartTime;
		const float LoopStart = DiveStart + DivePlan.DesiredRuntimeSeconds;
		AddMontageSegment(Montage, StartPlan, 0.0f);
		AddMontageSegment(Montage, DivePlan, DiveStart);
		AddMontageSegment(Montage, LoopPlan, LoopStart);
		AddMontageSection(Montage, FName(MainSectionNames[0]), 0.0f,
			FName(MainSectionNames[1]));
		AddMontageSection(Montage, FName(MainSectionNames[1]), DiveStart,
			FName(MainSectionNames[2]));
		AddMontageSection(Montage, FName(MainSectionNames[2]), LoopStart,
			FName(MainSectionNames[2]));
	}
	else
	{
		AddMontageSegment(Montage, LandingPlan, 0.0f);
		AddMontageSection(Montage, FName(TEXT("Landing203")), 0.0f, NAME_None);
	}
	Montage.CompositeSections.Sort([](const FCompositeSection& A, const FCompositeSection& B)
	{
		return A.GetTime() < B.GetTime();
	});
	Plan.bChanged = true;
}

void AddNewMontageTracks(UAnimMontage& Montage)
{
	UAnimationBlueprintLibrary::AddAnimationNotifyTrack(&Montage, FName(MainTrackNames[0]),
		FLinearColor(0.85f, 0.18f, 0.18f));
	UAnimationBlueprintLibrary::AddAnimationNotifyTrack(&Montage, FName(MainTrackNames[1]),
		FLinearColor(0.18f, 0.65f, 0.95f));
	UAnimationBlueprintLibrary::AddAnimationNotifyTrack(&Montage, FName(MainTrackNames[2]),
		FLinearColor(0.25f, 0.80f, 0.35f));
}

bool SetupMontage(FSetupRun& Run, FMontagePlan& Plan, const bool bMain,
	FSequencePlan& StartPlan, FSequencePlan& DivePlan, FSequencePlan& LoopPlan,
	FSequencePlan& LandingPlan, const float LandingBlendIn)
{
	if (!Plan.Montage)
	{
		if (!Run.bApply)
		{
			return false;
		}
		UPackage* Package = CreatePackage(*Plan.PackageName);
		Plan.Montage = Package ? NewObject<UAnimMontage>(Package, FName(Plan.AssetName),
			RF_Public | RF_Standalone) : nullptr;
		if (!Plan.Montage)
		{
			Run.Error(FString::Printf(TEXT("Could not create montage package: %s"), *Plan.ObjectPath));
			return false;
		}
		Plan.bIsNew = true;
		Plan.bChanged = true;
		Plan.Montage->SetSkeleton(Plan.Skeleton);
	}

	Plan.bLayoutMatches = MontageLayoutMatches(*Plan.Montage, bMain,
		StartPlan, DivePlan, LoopPlan, LandingPlan);
	const bool bTrackLayoutMatches = HasRequiredBaseNotifyTracks(*Plan.Montage, bMain);
	if (!bTrackLayoutMatches && !Plan.bIsNew)
	{
		Run.Error(FString::Printf(TEXT("Existing montage is missing or reordered required notify tracks; preserving layout and failing closed: %s"),
			*Plan.ObjectPath));
		return false;
	}
	if (!bMain && !Plan.bIsNew
		&& (!FMath::IsNearlyEqual(Plan.Montage->BlendIn.GetBlendTime(), LandingBlendIn, 0.0001f)
			|| !FMath::IsNearlyEqual(Plan.Montage->BlendOut.GetBlendTime(), OrdinaryBlendOutTime, 0.0001f)
			|| !Plan.Montage->bEnableAutoBlendOut))
	{
		Run.Error(TEXT("Existing Over montage blend settings are incompatible; refusing changes beyond the animation reference."));
		return false;
	}
	if (!bMain && !Plan.bLayoutMatches && !Plan.bIsNew)
	{
		if (!MigrateLegacyLanding(Run, Plan, LandingPlan))
		{
			Run.Error(FString::Printf(TEXT("Existing Over montage is incompatible or unmigrated; only an exact legacy Landing203 pointer migration is allowed: %s"),
				*Plan.ObjectPath));
			return false;
		}
	}
	if (!Plan.bLayoutMatches && Plan.Montage->Notifies.Num() > 0 && !Plan.bIsNew)
	{
		Run.Error(FString::Printf(TEXT("Existing montage layout is incompatible while user notifies exist; refusing to relink: %s"),
			*Plan.ObjectPath));
		return false;
	}
	if (!Plan.bLayoutMatches && !Run.bApply)
	{
		Run.Error(FString::Printf(TEXT("Montage slot/segment/section layout does not match the Kinsect Piercer contract: %s"),
			*Plan.ObjectPath));
	}
	if (!Plan.bLayoutMatches && Run.bApply && Plan.Montage->Notifies.IsEmpty())
	{
		BuildMontageLayout(Plan, StartPlan, DivePlan, LoopPlan, LandingPlan, bMain);
		if (!FinalizeMontageLength(*Plan.Montage))
		{
			Run.Error(FString::Printf(TEXT("Montage length could not be finalized: %s"), *Plan.ObjectPath));
		}
	}
	if (Plan.bIsNew && Run.bApply)
	{
		if (!FinalizeMontageLength(*Plan.Montage))
		{
			Run.Error(FString::Printf(TEXT("New montage length could not be finalized: %s"), *Plan.ObjectPath));
		}
		if (Plan.Montage->Notifies.IsEmpty())
		{
			Plan.Montage->AnimNotifyTracks.Reset();
		}
		AddNewMontageTracks(*Plan.Montage);
		if (!HasRequiredBaseNotifyTracks(*Plan.Montage, bMain))
		{
			Run.Error(FString::Printf(TEXT("New montage notify tracks were not created as requested: %s"),
				*Plan.ObjectPath));
		}
	}

	if (Run.bApply)
	{
		const float WantedBlendIn = bMain ? MainBlendInTime : LandingBlendIn;
		if (!FMath::IsNearlyEqual(Plan.Montage->BlendIn.GetBlendTime(), WantedBlendIn, 0.0001f)
			|| !FMath::IsNearlyEqual(Plan.Montage->BlendOut.GetBlendTime(),
				OrdinaryBlendOutTime, 0.0001f)
			|| !Plan.Montage->bEnableAutoBlendOut)
		{
			if (!Plan.bIsNew)
			{
				Plan.Montage->Modify();
			}
			Plan.Montage->BlendIn.SetBlendTime(WantedBlendIn);
			Plan.Montage->BlendOut.SetBlendTime(OrdinaryBlendOutTime);
			Plan.Montage->bEnableAutoBlendOut = true;
			Plan.bChanged = true;
		}
	}
	else
	{
		const float WantedBlendIn = bMain ? MainBlendInTime : LandingBlendIn;
		if (!FMath::IsNearlyEqual(Plan.Montage->BlendIn.GetBlendTime(), WantedBlendIn, 0.0001f)
			|| !FMath::IsNearlyEqual(Plan.Montage->BlendOut.GetBlendTime(),
				OrdinaryBlendOutTime, 0.0001f)
			|| !Plan.Montage->bEnableAutoBlendOut)
		{
			Run.Error(FString::Printf(TEXT("Montage blend settings do not match expected values: %s"),
				*Plan.ObjectPath));
		}
	}
	return Plan.Montage != nullptr && Run.Errors.IsEmpty();
}

bool IsProfileReady(FSetupRun& Run, const UInsectGlaiveCombatConfig& CombatConfig)
{
	const FRecordedLocalTrajectoryProfile* Profiles[] = {
		&CombatConfig.KinsectPiercerStartProfile,
		&CombatConfig.KinsectPiercerDiveProfile,
		&CombatConfig.KinsectPiercerLandingProfile
	};
	const TCHAR* Names[] = {
		TEXT("KinsectPiercerStartProfile"), TEXT("KinsectPiercerDiveProfile"),
		TEXT("KinsectPiercerLandingProfile")
	};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Profiles); ++Index)
	{
		if (!Profiles[Index]->IsValid())
		{
			Run.Error(FString::Printf(TEXT("Combat profile is missing or invalid: %s"), Names[Index]));
		}
	}
	if (CombatConfig.KinsectPiercerStartProfile.IsValid()
		&& !FMath::IsNearlyEqual(CombatConfig.KinsectPiercerStartProfile.CoveredSeconds,
			ExpectedStartSeconds, 0.001f))
	{
		Run.Error(TEXT("KinsectPiercerStartProfile.CoveredSeconds differs from the calibrated 0.391327 s phase."));
	}
	if (CombatConfig.KinsectPiercerLandingProfile.IsValid()
		&& !FMath::IsNearlyEqual(CombatConfig.KinsectPiercerLandingProfile.CoveredSeconds,
			ExpectedLandingSeconds, 0.001f))
	{
		Run.Error(TEXT("KinsectPiercerLandingProfile.CoveredSeconds differs from the calibrated 1.566650 s phase."));
	}
	if (!FMath::IsNearlyEqual(CombatConfig.KinsectPiercerLandingBlendInTime,
		0.10f, 0.001f))
	{
		Run.Error(TEXT("CombatConfig landing montage blend-in must remain 0.1 s."));
	}
	float DiveLastKeySeconds = 0.0f;
	if (CombatConfig.KinsectPiercerDiveProfile.Curve
		&& MHGZRecordedLocalTrajectory::ValidateCurve(
			CombatConfig.KinsectPiercerDiveProfile.Curve, DiveLastKeySeconds))
	{
		const float CoveredSeconds = CombatConfig.KinsectPiercerDiveProfile.CoveredSeconds;
		const bool bUsesLastKeyDomain = FMath::IsNearlyEqual(CoveredSeconds,
			ExpectedDiveLastKeySeconds, 0.00001f)
			&& FMath::IsNearlyEqual(DiveLastKeySeconds, ExpectedDiveLastKeySeconds, 0.00001f);
		if (!bUsesLastKeyDomain)
		{
			Run.Error(TEXT("KinsectPiercerDiveProfile.CoveredSeconds must equal the M3 0.358333 s last-key endpoint; heldTailSeconds is zero."));
			return false;
		}
		TSharedPtr<FJsonObject> Decision = NewJsonObject();
		Decision->SetStringField(TEXT("coveredSecondsPolicy"), TEXT("m3_last_key_zero_held_tail"));
		Decision->SetNumberField(TEXT("coveredSeconds"), CoveredSeconds);
		Decision->SetNumberField(TEXT("curveLastKeySeconds"), DiveLastKeySeconds);
		Decision->SetNumberField(TEXT("heldTailSeconds"), 0.0);
		Decision->SetNumberField(TEXT("canonicalDomainEndSeconds"),
			ExpectedDiveCanonicalCoverageSeconds);
		Decision->SetNumberField(TEXT("tailModelAnchorSeconds"), ExpectedDiveTailModelAnchorSeconds);
		Decision->SetNumberField(TEXT("tailModelAnchorToCoverageSeconds"),
			CoveredSeconds - ExpectedDiveTailModelAnchorSeconds);
		Decision->SetStringField(TEXT("tailNote"),
			TEXT("M3 selects the 0.358333 s last-key endpoint; the held tail after the last key is zero, with the recorded 0.341667 s model anchor reported separately."));
		Run.Report->SetObjectField(TEXT("diveCoverageDecision"), Decision.ToSharedRef());
	}
	else if (CombatConfig.KinsectPiercerDiveProfile.IsValid())
	{
		Run.Error(TEXT("Dive curve failed the native recorded-trajectory key audit."));
	}
	return Run.Errors.IsEmpty();
}

bool IsExpectedSocketOnMesh(const USkeletalMesh& Mesh, const FName Name)
{
	return HasBoneOrSocket(Mesh, Name);
}

bool ValidateTraceShape(FSetupRun& Run, const UMHGZKinsectPiercerAbility& Ability,
	const USkeletalMesh& Mesh, const FAttackCollisionConfig* ReferenceCollision,
	const bool bNewAbility)
{
	if (Ability.AttackSegments.Num() != 3)
	{
		Run.Error(FString::Printf(TEXT("GA AttackSegments must contain exactly three entries; found %d."),
			Ability.AttackSegments.Num()));
		return false;
	}
	bool bValid = true;
	for (int32 Index = 0; Index < Ability.AttackSegments.Num(); ++Index)
	{
		const FAttackSegmentConfig& Segment = Ability.AttackSegments[Index];
		const float MotionValue = Segment.Damage.MotionValue.GetValueAtLevel(1.0f);
		const UClass* DamageClass = Segment.Damage.DamageEffectClass.Get();
		if (!DamageClass || !DamageClass->IsChildOf(UMHGZDamageGameplayEffect::StaticClass()))
		{
			Run.Error(FString::Printf(TEXT("AttackSegments[%d] must use UMHGZDamageGameplayEffect or a subclass."),
				Index));
			bValid = false;
		}
		if (!FMath::IsFinite(MotionValue) || MotionValue <= 0.0f)
		{
			Run.Error(FString::Printf(TEXT("AttackSegments[%d] must keep a positive explicit MotionValue."),
				Index));
			bValid = false;
		}
		if (Segment.MultiHitCount != 1
			|| Segment.MultiHitPolicy != EAttackMultiHitPolicy::ContactOnly
			|| Segment.Damage.bRequiresHitToContinue)
		{
			Run.Error(FString::Printf(TEXT("AttackSegments[%d] must be one ContactOnly hit and not truncate on a miss."),
				Index));
			bValid = false;
		}
		if (Segment.Collision.TraceMeshComponentTag != FName(TEXT("WeaponTrace")))
		{
			Run.Error(FString::Printf(TEXT("AttackSegments[%d] TraceMeshComponentTag must be WeaponTrace."),
				Index));
			bValid = false;
		}
		if (ReferenceCollision
			&& (Segment.Collision.CollisionChannel != ReferenceCollision->CollisionChannel
				|| Segment.Collision.HitzoneQueryTag != ReferenceCollision->HitzoneQueryTag))
		{
			Run.Error(FString::Printf(TEXT("AttackSegments[%d] collision channel/HitzoneQueryTag diverges from the existing working attack configuration."),
				Index));
			bValid = false;
		}
		if (Segment.Collision.TraceRegions.IsEmpty())
		{
			Run.Error(FString::Printf(TEXT("AttackSegments[%d] requires at least one TraceRegion for E1 Attack Collision windows."),
				Index));
			bValid = false;
		}
		for (int32 RegionIndex = 0; RegionIndex < Segment.Collision.TraceRegions.Num(); ++RegionIndex)
		{
			const FWeaponTraceRegion& Region = Segment.Collision.TraceRegions[RegionIndex];
			if (Region.EndSocketName.IsNone() || !FMath::IsFinite(Region.Radius)
				|| Region.Radius <= 0.0f
				|| !IsExpectedSocketOnMesh(Mesh, Region.EndSocketName)
				|| (!Region.StartSocketName.IsNone()
					&& !IsExpectedSocketOnMesh(Mesh, Region.StartSocketName)))
			{
				Run.Error(FString::Printf(TEXT("AttackSegments[%d].TraceRegions[%d] references a missing mesh bone/socket or invalid radius."),
					Index, RegionIndex));
				bValid = false;
			}
		}
		if (bNewAbility && !FMath::IsNearlyEqual(MotionValue, ProvisionalMotionValue, 0.0001f))
		{
			Run.Error(FString::Printf(TEXT("New AttackSegments[%d] did not retain the provisional E1 scaffold."), Index));
			bValid = false;
		}
		if (Segment.Collision.TraceRegions.Num() != 1
			|| Segment.Collision.TraceRegions[0].StartSocketName != FName(TEXT("Root"))
			|| Segment.Collision.TraceRegions[0].EndSocketName != FName(TEXT("IG_FrontTip"))
			|| !FMath::IsNearlyEqual(Segment.Collision.TraceRegions[0].Radius, 14.0f, 0.0001f))
		{
			Run.Error(FString::Printf(TEXT("AttackSegments[%d] must retain exactly one WeaponTrace region Root -> IG_FrontTip at radius 14."), Index));
			bValid = false;
		}
	}
	return bValid;
}

FAttackCollisionConfig* FindReferenceCollision(UMHGZKinsectSlashAbility* SlashAbility)
{
	if (!SlashAbility)
	{
		return nullptr;
	}
	for (FAttackSegmentConfig& Segment : SlashAbility->AttackSegments)
	{
		if (!Segment.Collision.TraceRegions.IsEmpty()
			&& Segment.Collision.TraceMeshComponentTag == FName(TEXT("WeaponTrace")))
		{
			return &Segment.Collision;
		}
	}
	return nullptr;
}

void SeedNewAbilitySegments(UMHGZKinsectPiercerAbility& Ability,
	const FAttackCollisionConfig& ReferenceCollision)
{
	Ability.AttackSegments.Reset();
	for (int32 Index = 0; Index < 3; ++Index)
	{
		FAttackSegmentConfig& Segment = Ability.AttackSegments.AddDefaulted_GetRef();
		Segment.Damage.DamageEffectClass = UMHGZDamageGameplayEffect::StaticClass();
		Segment.Damage.MotionValue = ProvisionalMotionValue;
		Segment.Damage.bUseHitzoneDefense = true;
		Segment.Damage.bRequiresHitToContinue = false;
		Segment.MultiHitCount = 1;
		Segment.MultiHitPolicy = EAttackMultiHitPolicy::ContactOnly;
		Segment.Collision = ReferenceCollision;
		Segment.Collision.TraceMeshComponentTag = FName(TEXT("WeaponTrace"));
		Segment.Collision.TraceRegions.Reset();
		Segment.Collision.bDrawDebug = false;
		FWeaponTraceRegion& Region = Segment.Collision.TraceRegions.AddDefaulted_GetRef();
		Region.StartSocketName = TEXT("Root");
		Region.EndSocketName = TEXT("IG_FrontTip");
		Region.Radius = 14.0f;
	}
}

bool ConfigureAbilityDefaults(UMHGZKinsectPiercerAbility& Ability,
	UAnimMontage& MainMontage, UAnimMontage& LandingMontage,
	const FSetupTags& Tags, const bool bSeedSegments)
{
	bool bChanged = false;
	const auto SetTag = [&bChanged](FGameplayTag& Field, const FGameplayTag& Value)
	{
		if (Field != Value) { Field = Value; bChanged = true; }
	};
	const auto SetName = [&bChanged](FName& Field, const FName Value)
	{
		if (Field != Value) { Field = Value; bChanged = true; }
	};
	const auto SetFloat = [&bChanged](float& Field, const float Value)
	{
		if (!FMath::IsNearlyEqual(Field, Value, 0.0001f)) { Field = Value; bChanged = true; }
	};
	SetTag(Ability.InputTag, Tags.InputRTY);
	if (Ability.AttackMontage != &MainMontage)
	{
		Ability.AttackMontage = &MainMontage;
		bChanged = true;
	}
	if (Ability.LandingMontage != &LandingMontage)
	{
		Ability.LandingMontage = &LandingMontage;
		bChanged = true;
	}
	SetName(Ability.DefaultEntrySection, FName(MainSectionNames[0]));
	SetName(Ability.StartSectionName, FName(MainSectionNames[0]));
	SetName(Ability.DiveSectionName, FName(MainSectionNames[1]));
	SetName(Ability.DiveLoopSectionName, FName(MainSectionNames[2]));
	SetName(Ability.LandingSectionName, FName(TEXT("Landing203")));
	SetFloat(Ability.MaxCorrectionAngle, 180.0f);
	if (bSeedSegments)
	{
		bChanged = true;
	}
	return bChanged;
}

bool AbilityDefaultsMatch(const UMHGZKinsectPiercerAbility& Ability,
	const UAnimMontage* MainMontage, const UAnimMontage* LandingMontage,
	const FSetupTags& Tags)
{
	return Ability.InputTag == Tags.InputRTY
		&& Ability.AttackMontage == MainMontage
		&& Ability.LandingMontage == LandingMontage
		&& Ability.DefaultEntrySection == FName(MainSectionNames[0])
		&& Ability.StartSectionName == FName(MainSectionNames[0])
		&& Ability.DiveSectionName == FName(MainSectionNames[1])
		&& Ability.DiveLoopSectionName == FName(MainSectionNames[2])
		&& Ability.LandingSectionName == FName(TEXT("Landing203"))
		&& FMath::IsNearlyEqual(Ability.MaxCorrectionAngle, 180.0f, 0.0001f);
}

TSharedPtr<FJsonObject> AbilityJson(const UBlueprint* Blueprint,
	const UMHGZKinsectPiercerAbility* Ability, const bool bCompiled)
{
	TSharedPtr<FJsonObject> Json = NewJsonObject();
	Json->SetStringField(TEXT("path"), Blueprint ? Blueprint->GetPathName() : FString(AbilityPackage));
	Json->SetStringField(TEXT("parentClass"), UMHGZKinsectPiercerAbility::StaticClass()->GetPathName());
	Json->SetBoolField(TEXT("exists"), Blueprint != nullptr);
	Json->SetBoolField(TEXT("compiled"), bCompiled);
	if (!Ability)
	{
		return Json;
	}
	Json->SetStringField(TEXT("inputTag"), Ability->InputTag.ToString());
	Json->SetStringField(TEXT("attackMontage"), Ability->AttackMontage
		? Ability->AttackMontage->GetPathName() : TEXT(""));
	Json->SetStringField(TEXT("landingMontage"), Ability->LandingMontage
		? Ability->LandingMontage->GetPathName() : TEXT(""));
	Json->SetStringField(TEXT("defaultEntrySection"), Ability->DefaultEntrySection.ToString());
	Json->SetStringField(TEXT("startSectionName"), Ability->StartSectionName.ToString());
	Json->SetStringField(TEXT("diveSectionName"), Ability->DiveSectionName.ToString());
	Json->SetStringField(TEXT("diveLoopSectionName"), Ability->DiveLoopSectionName.ToString());
	Json->SetStringField(TEXT("landingSectionName"), Ability->LandingSectionName.ToString());
	Json->SetNumberField(TEXT("maxCorrectionAngle"), Ability->MaxCorrectionAngle);
	Json->SetNumberField(TEXT("attackSegmentCount"), Ability->AttackSegments.Num());
	TArray<TSharedPtr<FJsonValue>> Segments;
	for (int32 Index = 0; Index < Ability->AttackSegments.Num(); ++Index)
	{
		const FAttackSegmentConfig& Segment = Ability->AttackSegments[Index];
		TSharedPtr<FJsonObject> SegmentJson = NewJsonObject();
		SegmentJson->SetNumberField(TEXT("configIndex"), Index);
		SegmentJson->SetStringField(TEXT("damageEffectClass"), Segment.Damage.DamageEffectClass
			? Segment.Damage.DamageEffectClass->GetPathName() : TEXT(""));
		SegmentJson->SetNumberField(TEXT("motionValue"),
			Segment.Damage.MotionValue.GetValueAtLevel(1.0f));
		SegmentJson->SetNumberField(TEXT("multiHitCount"), Segment.MultiHitCount);
		SegmentJson->SetNumberField(TEXT("multiHitPolicy"),
			static_cast<int32>(Segment.MultiHitPolicy));
		SegmentJson->SetStringField(TEXT("traceMeshComponentTag"),
			Segment.Collision.TraceMeshComponentTag.ToString());
		SegmentJson->SetNumberField(TEXT("collisionChannel"),
			static_cast<int32>(Segment.Collision.CollisionChannel.GetValue()));
		SegmentJson->SetStringField(TEXT("hitzoneQueryTag"),
			Segment.Collision.HitzoneQueryTag.ToString());
		TArray<TSharedPtr<FJsonValue>> Regions;
		for (const FWeaponTraceRegion& Region : Segment.Collision.TraceRegions)
		{
			TSharedPtr<FJsonObject> RegionJson = NewJsonObject();
			RegionJson->SetStringField(TEXT("startSocket"), Region.StartSocketName.ToString());
			RegionJson->SetStringField(TEXT("endSocket"), Region.EndSocketName.ToString());
			RegionJson->SetNumberField(TEXT("radius"), Region.Radius);
			RegionJson->SetNumberField(TEXT("maxSampleSpacing"), Region.MaxSampleSpacing);
			Regions.Add(JsonObjectValue(RegionJson));
		}
		SegmentJson->SetArrayField(TEXT("traceRegions"), MoveTemp(Regions));
		Segments.Add(JsonObjectValue(SegmentJson));
	}
	Json->SetArrayField(TEXT("attackSegments"), MoveTemp(Segments));
	return Json;
}

TArray<TSharedPtr<FJsonValue>> TransitionArrayJson(const TArray<FComboTransition>& Transitions,
	const FName ExcludedID = NAME_None)
{
	TArray<TSharedPtr<FJsonValue>> Result;
	for (int32 Index = 0; Index < Transitions.Num(); ++Index)
	{
		if (Transitions[Index].TransitionID == ExcludedID)
		{
			continue;
		}
		TSharedPtr<FJsonObject> Json = NewJsonObject();
		const FComboTransition& Transition = Transitions[Index];
		Json->SetNumberField(TEXT("index"), Index);
		Json->SetStringField(TEXT("transitionID"), Transition.TransitionID.ToString());
		Json->SetStringField(TEXT("sourceState"), Transition.SourceState.ToString());
		Json->SetBoolField(TEXT("matchAnyState"), Transition.bMatchAnyState);
		TArray<FString> BlockedSources;
		for (const FName Source : Transition.BlockedSourceStates)
		{
			BlockedSources.Add(Source.ToString());
		}
		Json->SetArrayField(TEXT("blockedSourceStates"), StringValues(BlockedSources));
		Json->SetStringField(TEXT("inputTag"), Transition.InputTag.ToString());
		Json->SetNumberField(TEXT("direction"), static_cast<int32>(Transition.Direction));
		Json->SetNumberField(TEXT("executionPolicy"), static_cast<int32>(Transition.ExecutionPolicy));
		Json->SetStringField(TEXT("abilityClass"), Transition.AbilityClass
			? Transition.AbilityClass->GetPathName() : TEXT(""));
		Json->SetNumberField(TEXT("montageBlendInTime"), Transition.MontageBlendInTime);
		Json->SetNumberField(TEXT("maxCorrectionAngle"), Transition.MaxCorrectionAngle);
		Json->SetStringField(TEXT("targetState"), Transition.TargetState.ToString());
		Json->SetNumberField(TEXT("statePolicy"), static_cast<int32>(Transition.StatePolicy));
		Json->SetNumberField(TEXT("landingPolicy"), static_cast<int32>(Transition.LandingPolicy));
		Json->SetArrayField(TEXT("requiredTags"), TagValues(Transition.RequiredTags));
		Json->SetArrayField(TEXT("blockedTags"), TagValues(Transition.BlockedTags));
		Json->SetNumberField(TEXT("staminaRequired"), Transition.StaminaRequired);
		Json->SetBoolField(TEXT("requiresComboWindow"), Transition.bRequiresComboWindow);
		Json->SetBoolField(TEXT("requiresDodgeAcceptWindow"), Transition.bRequiresDodgeAcceptWindow);
		Json->SetNumberField(TEXT("grantTiming"), static_cast<int32>(Transition.GrantTiming));
		Json->SetArrayField(TEXT("grantedTags"), TagValues(Transition.GrantedTags));
		Json->SetBoolField(TEXT("autoTransition"), Transition.bAutoTransition);
		Json->SetNumberField(TEXT("priority"), Transition.Priority);
		Result.Add(JsonObjectValue(Json));
	}
	return Result;
}

bool TransitionMatchesAerialPiercer(const FComboTransition& Transition,
	const FGameplayTagContainer& LiveTags, const FGameplayTagContainer& PiercerBlocked,
	const FGameplayTag InputTag)
{
	if (Transition.bAutoTransition || Transition.InputTag != InputTag
		|| LiveTags.HasAny(Transition.BlockedTags)
		|| Transition.RequiredTags.HasAny(PiercerBlocked)
		|| Transition.RequiredTags.HasAny(Transition.BlockedTags))
	{
		return false;
	}
	if (Transition.bMatchAnyState)
	{
		return true;
	}
	// Exact-state candidates precede AnyState candidates regardless of numeric priority.
	return Transition.SourceState.ToString().StartsWith(TEXT("IG.Aerial"))
		|| Transition.SourceState == FName(TEXT("Idle"));
}

bool TransitionCanOutrankPiercer(const FComboTransition& Transition)
{
	return !Transition.bMatchAnyState || Transition.Direction != EDirectionalInput::None
		|| Transition.Priority >= 1000; // Equal-priority earlier rows also win.
}

TSharedPtr<FJsonObject> ConfigureComboRoute(FSetupRun& Run, UMHGZWeaponComboData& Combo,
	const FSetupTags& Tags, UClass* AbilityClass, bool& bOutChanged)
{
	TSharedPtr<FJsonObject> Json = NewJsonObject();
	const FName OwnedID(TargetTransitionID);
	int32 OwnedCount = 0;
	for (const FComboTransition& Row : Combo.Transitions)
	{
		OwnedCount += Row.TransitionID == OwnedID ? 1 : 0;
	}
	Json->SetStringField(TEXT("path"), Combo.GetPathName());
	Json->SetNumberField(TEXT("targetTransitionCountBefore"), OwnedCount);
	Json->SetArrayField(TEXT("otherTransitionsBefore"), TransitionArrayJson(Combo.Transitions, OwnedID));
	if (OwnedCount > 1)
	{
		Run.Error(TEXT("DA_IG_Combo contains duplicate Air_KinsectPiercer_RTY transition IDs."));
		Json->SetArrayField(TEXT("otherTransitionsAfter"), TransitionArrayJson(Combo.Transitions, OwnedID));
		return Json;
	}

	FGameplayTagContainer PiercerLiveTags;
	PiercerLiveTags.AddTag(Tags.Unsheathed);
	PiercerLiveTags.AddTag(Tags.Falling);
	PiercerLiveTags.AddTag(Tags.DanceSourceKinsectSlash);
	FGameplayTagContainer PiercerBlocked;
	for (const FGameplayTag Tag : { Tags.Grounded, Tags.Dead, Tags.Hitstun,
		Tags.Knockdown, Tags.AerialCantAttack }) { PiercerBlocked.AddTag(Tag); }
	TArray<TSharedPtr<FJsonValue>> HigherPriorityConflicts;
	for (const FComboTransition& Row : Combo.Transitions)
	{
		if (Row.TransitionID == OwnedID || !TransitionCanOutrankPiercer(Row)
			|| !TransitionMatchesAerialPiercer(Row, PiercerLiveTags, PiercerBlocked, Tags.InputRTY))
		{
			continue;
		}
		TSharedPtr<FJsonObject> Conflict = NewJsonObject();
		Conflict->SetStringField(TEXT("transitionID"), Row.TransitionID.ToString());
		Conflict->SetStringField(TEXT("sourceState"), Row.SourceState.ToString());
		Conflict->SetStringField(TEXT("targetState"), Row.TargetState.ToString());
		Conflict->SetNumberField(TEXT("priority"), Row.Priority);
		Conflict->SetBoolField(TEXT("exactStateHasPrecedence"), !Row.bMatchAnyState);
		Conflict->SetBoolField(TEXT("directionHasPrecedence"), Row.Direction != EDirectionalInput::None);
		Conflict->SetArrayField(TEXT("conditionalRequiredTags"), TagValues(Row.RequiredTags));
		HigherPriorityConflicts.Add(JsonObjectValue(Conflict));
	}
	Json->SetArrayField(TEXT("conflictingHigherPriorityAerialRTY"), HigherPriorityConflicts);
	if (!HigherPriorityConflicts.IsEmpty())
	{
		Run.Error(TEXT("An aerial RTY transition can outrank Piercer by exact state, direction specificity or equal/higher priority; no other route will be overwritten."));
	}

	FGameplayTagContainer Required;
	Required.AddTag(Tags.Unsheathed);
	Required.AddTag(Tags.Falling);
	Required.AddTag(Tags.DanceSourceKinsectSlash);
	FGameplayTagContainer Blocked;
	Blocked.AddTag(Tags.Grounded);
	Blocked.AddTag(Tags.Dead);
	Blocked.AddTag(Tags.Hitstun);
	Blocked.AddTag(Tags.Knockdown);
	Blocked.AddTag(Tags.AerialCantAttack);

	FComboTransition* Existing = Combo.Transitions.FindByPredicate([OwnedID](const FComboTransition& Row)
	{
		return Row.TransitionID == OwnedID;
	});
	if (!Existing && !Run.bApply)
	{
		Run.Error(TEXT("DA_IG_Combo has no Air_KinsectPiercer_RTY transition."));
	}
	if (Existing)
	{
		Json->SetBoolField(TEXT("targetTransitionExists"), true);
		Json->SetNumberField(TEXT("staminaRequiredPreserved"), Existing->StaminaRequired);
		if (Run.bApply)
		{
			FComboTransition Expected = *Existing;
			Expected.SourceState = NAME_None;
			Expected.bMatchAnyState = true;
			Expected.BlockedSourceStates.Reset();
			Expected.InputTag = Tags.InputRTY;
			Expected.Direction = EDirectionalInput::None;
			Expected.ExecutionPolicy = EComboExecutionPolicy::ActivateAbility;
			Expected.AbilityClass = AbilityClass;
			Expected.MontageBlendInTime = -1.0f;
			Expected.MaxCorrectionAngle = 180.0f;
			Expected.TargetState = FName(TargetStateName);
			Expected.StatePolicy = EComboStatePolicy::Replace;
			Expected.LandingPolicy = EComboLandingPolicy::AbilityOwned;
			Expected.RequiredTags = Required;
			Expected.BlockedTags = Blocked;
			Expected.bRequiresComboWindow = false;
			Expected.bRequiresDodgeAcceptWindow = false;
			Expected.GrantTiming = ETransitionGrantTiming::OnActivation;
			Expected.GrantedTags.Reset();
			Expected.bAutoTransition = false;
			Expected.Priority = 1000;
			if (!FComboTransition::StaticStruct()->CompareScriptStruct(&Expected, Existing, PPF_None))
			{
				Combo.Modify();
				const float PreservedStamina = Existing->StaminaRequired;
				*Existing = Expected;
				Existing->StaminaRequired = PreservedStamina;
				Combo.MarkPackageDirty();
				bOutChanged = true;
			}
		}
		else
		{
			const bool bMatches = Existing->SourceState.IsNone() && Existing->bMatchAnyState
				&& Existing->InputTag == Tags.InputRTY
				&& Existing->Direction == EDirectionalInput::None
				&& Existing->ExecutionPolicy == EComboExecutionPolicy::ActivateAbility
				&& Existing->AbilityClass == AbilityClass
				&& Existing->MontageBlendInTime == -1.0f
				&& FMath::IsNearlyEqual(Existing->MaxCorrectionAngle, 180.0f, 0.0001f)
				&& Existing->TargetState == FName(TargetStateName)
				&& Existing->StatePolicy == EComboStatePolicy::Replace
				&& Existing->LandingPolicy == EComboLandingPolicy::AbilityOwned
				&& Existing->RequiredTags == Required && Existing->BlockedTags == Blocked
				&& !Existing->bRequiresComboWindow && !Existing->bRequiresDodgeAcceptWindow
				&& Existing->GrantTiming == ETransitionGrantTiming::OnActivation
				&& Existing->GrantedTags.IsEmpty() && !Existing->bAutoTransition
				&& Existing->Priority == 1000;
			if (!bMatches)
			{
				Run.Error(TEXT("Existing Air_KinsectPiercer_RTY transition does not match its owned routing fields."));
			}
		}
	}
	else if (Run.bApply)
	{
		Combo.Modify();
		FComboTransition& NewTransition = Combo.Transitions.AddDefaulted_GetRef();
		NewTransition.TransitionID = OwnedID;
		NewTransition.SourceState = NAME_None;
		NewTransition.bMatchAnyState = true;
		NewTransition.InputTag = Tags.InputRTY;
		NewTransition.Direction = EDirectionalInput::None;
		NewTransition.ExecutionPolicy = EComboExecutionPolicy::ActivateAbility;
		NewTransition.AbilityClass = AbilityClass;
		NewTransition.MontageBlendInTime = -1.0f;
		NewTransition.MaxCorrectionAngle = 180.0f;
		NewTransition.TargetState = FName(TargetStateName);
		NewTransition.StatePolicy = EComboStatePolicy::Replace;
		NewTransition.LandingPolicy = EComboLandingPolicy::AbilityOwned;
		NewTransition.RequiredTags = Required;
		NewTransition.BlockedTags = Blocked;
		NewTransition.StaminaRequired = 0.0f;
		NewTransition.bRequiresComboWindow = false;
		NewTransition.bRequiresDodgeAcceptWindow = false;
		NewTransition.GrantTiming = ETransitionGrantTiming::OnActivation;
		NewTransition.GrantedTags.Reset();
		NewTransition.bAutoTransition = false;
		NewTransition.Priority = 1000;
		Combo.MarkPackageDirty();
		bOutChanged = true;
	}
	Json->SetBoolField(TEXT("staminaIsProvisionalWhenNew"), true);
	Json->SetNumberField(TEXT("transitionCountAfter"), Combo.Transitions.Num());
	Json->SetArrayField(TEXT("otherTransitionsAfter"), TransitionArrayJson(Combo.Transitions, OwnedID));
	return Json;
}

bool OtherComboTransitionsUnchanged(const TArray<FComboTransition>& Before,
	const TArray<FComboTransition>& After, const TSet<FName>& OwnedTransitionIDs)
{
	TArray<const FComboTransition*> BeforeUnowned;
	TArray<const FComboTransition*> AfterUnowned;
	for (const FComboTransition& Row : Before)
	{
		if (!OwnedTransitionIDs.Contains(Row.TransitionID)) { BeforeUnowned.Add(&Row); }
	}
	for (const FComboTransition& Row : After)
	{
		if (!OwnedTransitionIDs.Contains(Row.TransitionID)) { AfterUnowned.Add(&Row); }
	}
	if (BeforeUnowned.Num() != AfterUnowned.Num()) { return false; }
	for (int32 Index = 0; Index < BeforeUnowned.Num(); ++Index)
	{
		if (!FComboTransition::StaticStruct()->CompareScriptStruct(
			BeforeUnowned[Index], AfterUnowned[Index], PPF_None))
		{
			return false;
		}
	}
	return true;
}

bool ConfigureKinsectPiercerDerivedRoutes(FSetupRun& Run, UMHGZWeaponComboData& Combo,
	bool& bOutChanged, TSet<FName>& OutOwnedTransitionIDs)
{
	const FName PiercerSourceState(TargetStateName);
	const FName TuCiSourceState(TuCiSourceStateName);
	OutOwnedTransitionIDs.Add(FName(TargetTransitionID));

	TArray<FComboTransition> TuCiRows;
	for (const FComboTransition& Row : Combo.Transitions)
	{
		if (Row.SourceState == TuCiSourceState)
		{
			TuCiRows.Add(Row);
		}
	}
	if (TuCiRows.IsEmpty())
	{
		Run.Error(TEXT("DA_IG_Combo has no rows whose SourceState is Ground.TuCi; cannot mirror its outgoing routes."));
		return false;
	}

	TArray<TSharedPtr<FJsonValue>> RouteReports;
	TSet<FName> SourceIDs;
	for (const FComboTransition& Source : TuCiRows)
	{
		if (Source.TransitionID.IsNone() || SourceIDs.Contains(Source.TransitionID))
		{
			Run.Error(TEXT("Ground.TuCi source rows must have unique, non-empty TransitionIDs."));
			continue;
		}
		SourceIDs.Add(Source.TransitionID);

		const FName CloneID(*FString::Printf(TEXT("KinsectPiercer_%s"),
			*Source.TransitionID.ToString()));
		OutOwnedTransitionIDs.Add(CloneID);
		FComboTransition Expected = Source;
		Expected.TransitionID = CloneID; // TransitionID is globally unique within the Combo asset.
		Expected.SourceState = PiercerSourceState;

		FComboTransition* Existing = nullptr;
		int32 ExistingCount = 0;
		for (FComboTransition& Candidate : Combo.Transitions)
		{
			if (Candidate.TransitionID == CloneID)
			{
				Existing = &Candidate;
				++ExistingCount;
			}
		}
		if (ExistingCount > 1)
		{
			Run.Error(FString::Printf(TEXT("Duplicate cloned transition ID %s."), *CloneID.ToString()));
			continue;
		}
		if (Existing && Existing->SourceState != PiercerSourceState)
		{
			Run.Error(FString::Printf(TEXT("Cloned transition ID %s is already owned by a different source state."),
				*CloneID.ToString()));
			continue;
		}

		bool bFieldsMatch = Existing
			&& FComboTransition::StaticStruct()->CompareScriptStruct(&Expected, Existing, PPF_None);
		if (!Existing)
		{
			if (!Run.bApply)
			{
				Run.Error(FString::Printf(TEXT("Missing cloned route %s for Ground.TuCi transition %s."),
					*CloneID.ToString(), *Source.TransitionID.ToString()));
			}
			else
			{
				Combo.Modify();
				Combo.Transitions.Add(Expected);
				Combo.MarkPackageDirty();
				bOutChanged = true;
				bFieldsMatch = true;
			}
		}
		else if (!bFieldsMatch)
		{
			if (!Run.bApply)
			{
				Run.Error(FString::Printf(TEXT("Cloned route %s differs from source route %s outside its owned SourceState/TransitionID."),
					*CloneID.ToString(), *Source.TransitionID.ToString()));
			}
			else
			{
				Combo.Modify();
				*Existing = Expected;
				Combo.MarkPackageDirty();
				bOutChanged = true;
				bFieldsMatch = true;
			}
		}

		TSharedPtr<FJsonObject> Route = NewJsonObject();
		Route->SetStringField(TEXT("sourceTransitionID"), Source.TransitionID.ToString());
		Route->SetStringField(TEXT("clonedTransitionID"), CloneID.ToString());
		Route->SetStringField(TEXT("sourceState"), Source.SourceState.ToString());
		Route->SetStringField(TEXT("clonedSourceState"), PiercerSourceState.ToString());
		Route->SetStringField(TEXT("inputTag"), Source.InputTag.ToString());
		Route->SetNumberField(TEXT("direction"), static_cast<int32>(Source.Direction));
		Route->SetStringField(TEXT("abilityClass"), Source.AbilityClass
			? Source.AbilityClass->GetPathName() : TEXT(""));
		Route->SetStringField(TEXT("targetState"), Source.TargetState.ToString());
		Route->SetBoolField(TEXT("gameplayFieldsMatchSource"), bFieldsMatch);
		RouteReports.Add(JsonObjectValue(Route));
	}

	Run.Report->SetArrayField(TEXT("kinsectPiercerDerivedTransitions"), MoveTemp(RouteReports));
	Run.Report->SetNumberField(TEXT("tuCiSourceTransitionCount"), TuCiRows.Num());
	return Run.Errors.IsEmpty();
}

TSharedPtr<FJsonObject> ChordJson(const FWeaponChordDefinition& Chord, const int32 Index)
{
	TSharedPtr<FJsonObject> Json = NewJsonObject();
	Json->SetNumberField(TEXT("index"), Index);
	Json->SetStringField(TEXT("outputTag"), Chord.OutputTag.ToString());
	Json->SetArrayField(TEXT("triggerControls"), TagValues(FGameplayTagContainer::CreateFromArray(Chord.TriggerControls)));
	Json->SetArrayField(TEXT("requiredHeldModifiers"),
		TagValues(FGameplayTagContainer::CreateFromArray(Chord.RequiredHeldModifiers)));
	Json->SetArrayField(TEXT("requiredContextTags"), TagValues(Chord.RequiredContextTags));
	Json->SetArrayField(TEXT("blockedContextTags"), TagValues(Chord.BlockedContextTags));
	Json->SetBoolField(TEXT("requireExactModifiers"), Chord.bRequireExactModifiers);
	Json->SetBoolField(TEXT("modifiersMustPrecedeTriggers"), Chord.bModifiersMustPrecedeTriggers);
	Json->SetNumberField(TEXT("priority"), Chord.Priority);
	Json->SetBoolField(TEXT("consumeTriggerControls"), Chord.bConsumeTriggerControls);
	Json->SetNumberField(TEXT("dispatchPolicy"), static_cast<int32>(Chord.DispatchPolicy));
	Json->SetStringField(TEXT("aimSnapshotContext"),
		EnumString(static_cast<int32>(Chord.AimSnapshotContext)));
	return Json;
}

TSharedPtr<FJsonObject> ConfigureInputProfile(FSetupRun& Run, UWeaponRuntimeDefinition& Runtime,
	const FSetupTags& Tags, bool& bOutChanged)
{
	TSharedPtr<FJsonObject> Json = NewJsonObject();
	Json->SetStringField(TEXT("runtimeDefinition"), Runtime.GetPathName());
	UWeaponInputProfile* Profile = Runtime.InputProfile;
	if (!Profile)
	{
		Run.Error(TEXT("DA_WeaponRuntime_IG has no InputProfile reference."));
		return Json;
	}
	Json->SetStringField(TEXT("resolvedInputProfile"), Profile->GetPathName());
	Json->SetNumberField(TEXT("chordGracePeriodSeconds"), Profile->ChordGracePeriod);
	if (!FMath::IsFinite(Profile->ChordGracePeriod) || Profile->ChordGracePeriod <= 0.0f)
	{
		Run.Error(TEXT("Resolved weapon InputProfile has an invalid ChordGracePeriod."));
	}

	TArray<TSharedPtr<FJsonValue>> RawYMappings;
	for (const TPair<TObjectPtr<UInputAction>, FGameplayTag>& Pair
		: Profile->RawActionToPhysicalInputTag)
	{
		if (Pair.Value == Tags.PhysicalY)
		{
			TSharedPtr<FJsonObject> Mapping = NewJsonObject();
			Mapping->SetStringField(TEXT("rawAction"), Pair.Key ? Pair.Key->GetPathName() : TEXT(""));
			Mapping->SetStringField(TEXT("physicalTag"), Pair.Value.ToString());
			RawYMappings.Add(JsonObjectValue(Mapping));
		}
	}
	Json->SetArrayField(TEXT("rawYPhysicalMappings"), MoveTemp(RawYMappings));
	if (Json->GetArrayField(TEXT("rawYPhysicalMappings")).IsEmpty())
	{
		Run.Error(TEXT("Resolved InputProfile has no raw action mapped to Input.Weapon.Y."));
	}

	int32 ChordCount = 0;
	FWeaponChordDefinition* Existing = nullptr;
	TArray<TSharedPtr<FJsonValue>> ChordsBefore;
	for (int32 Index = 0; Index < Profile->Chords.Num(); ++Index)
	{
		const FWeaponChordDefinition& Chord = Profile->Chords[Index];
		ChordsBefore.Add(JsonObjectValue(ChordJson(Chord, Index)));
		if (Chord.OutputTag == Tags.InputRTY)
		{
			++ChordCount;
			Existing = const_cast<FWeaponChordDefinition*>(&Chord);
		}
	}
	Json->SetArrayField(TEXT("chordsBefore"), MoveTemp(ChordsBefore));
	Json->SetNumberField(TEXT("matchingRTYChordCount"), ChordCount);
	if (ChordCount > 1)
	{
		Run.Error(TEXT("Resolved InputProfile has duplicate Input.Weapon.RTY chord outputs; refusing to choose one."));
		Json->SetArrayField(TEXT("chordsAfter"), {});
		return Json;
	}

	if (Existing)
	{
		const bool bCorrectControls = Existing->TriggerControls.Num() == 1
			&& Existing->TriggerControls[0] == Tags.PhysicalY
			&& Existing->RequiredHeldModifiers.Num() == 1
			&& Existing->RequiredHeldModifiers[0] == Tags.ModifierRT;
		if (!bCorrectControls)
		{
			Run.Error(TEXT("Existing RTY chord has an incompatible trigger/modifier shape; it was not replaced."));
		}
		if (HasTagOrDescendant(Existing->RequiredContextTags,
			ResolveTag(Run, TEXT("Combat.State.Aerial.Dance.Source")))
			|| HasTagOrDescendant(Existing->BlockedContextTags,
				ResolveTag(Run, TEXT("Combat.State.Aerial.Dance.Source"))))
		{
			Run.Error(TEXT("Existing RTY chord has a DanceSource context gate; it must remain in Combo/GA routing."));
		}
		const bool bGroundedRequired = HasTagOrDescendant(Existing->RequiredContextTags, Tags.Grounded);
		const bool bGroundedBlocked = HasTagOrDescendant(Existing->BlockedContextTags, Tags.Grounded);
		const bool bFallingRequired = HasTagOrDescendant(Existing->RequiredContextTags, Tags.Falling);
		const bool bFallingBlocked = HasTagOrDescendant(Existing->BlockedContextTags, Tags.Falling);
		if (bGroundedRequired || bGroundedBlocked || bFallingRequired || bFallingBlocked
			|| HasTagOrDescendant(Existing->BlockedContextTags, Tags.Unsheathed))
		{
			Run.Error(TEXT("Existing RTY chord context constraints prevent it from serving both ground RTY and aerial Piercer."));
		}
		if (bCorrectControls && Run.bApply)
		{
			if (Existing->DispatchPolicy != EWeaponChordDispatchPolicy::OnPress
				|| !Existing->bConsumeTriggerControls
				|| Existing->AimSnapshotContext != EWeaponAimSnapshotContext::Action)
			{
				Profile->Modify();
				Existing->DispatchPolicy = EWeaponChordDispatchPolicy::OnPress;
				Existing->bConsumeTriggerControls = true;
				Existing->AimSnapshotContext = EWeaponAimSnapshotContext::Action;
				Profile->MarkPackageDirty();
				bOutChanged = true;
			}
		}
		else if (!Run.bApply && (Existing->DispatchPolicy != EWeaponChordDispatchPolicy::OnPress
			|| !Existing->bConsumeTriggerControls
			|| Existing->AimSnapshotContext != EWeaponAimSnapshotContext::Action))
		{
			Run.Error(TEXT("Existing RTY chord's DispatchPolicy/consume/AimSnapshotContext do not match the action routing contract."));
		}
	}
	else if (!Run.bApply)
	{
		Run.Error(TEXT("Resolved InputProfile has no Input.Weapon.RTY chord."));
	}
	else
	{
		Profile->Modify();
		FWeaponChordDefinition& Chord = Profile->Chords.AddDefaulted_GetRef();
		Chord.OutputTag = Tags.InputRTY;
		Chord.TriggerControls.Add(Tags.PhysicalY);
		Chord.RequiredHeldModifiers.Add(Tags.ModifierRT);
		Chord.bRequireExactModifiers = true;
		Chord.bModifiersMustPrecedeTriggers = true;
		Chord.Priority = 100;
		Chord.bConsumeTriggerControls = true;
		Chord.DispatchPolicy = EWeaponChordDispatchPolicy::OnPress;
		Chord.AimSnapshotContext = EWeaponAimSnapshotContext::Action;
		Profile->MarkPackageDirty();
		bOutChanged = true;
	}

	TArray<TSharedPtr<FJsonValue>> ChordsAfter;
	for (int32 Index = 0; Index < Profile->Chords.Num(); ++Index)
	{
		ChordsAfter.Add(JsonObjectValue(ChordJson(Profile->Chords[Index], Index)));
	}
	Json->SetArrayField(TEXT("chordsAfter"), MoveTemp(ChordsAfter));
	Json->SetNumberField(TEXT("chordGracePeriodSecondsAfter"), Profile->ChordGracePeriod);
	Json->SetBoolField(TEXT("profileGraceAndOtherChordsPreserved"), true);
	return Json;
}

TSharedPtr<FJsonObject> ComboRouteSummary(const FComboTransition* Transition)
{
	TSharedPtr<FJsonObject> Json = NewJsonObject();
	Json->SetBoolField(TEXT("exists"), Transition != nullptr);
	if (!Transition)
	{
		return Json;
	}
	Json->SetStringField(TEXT("transitionID"), Transition->TransitionID.ToString());
	Json->SetStringField(TEXT("inputTag"), Transition->InputTag.ToString());
	Json->SetStringField(TEXT("targetState"), Transition->TargetState.ToString());
	Json->SetNumberField(TEXT("priority"), Transition->Priority);
	Json->SetNumberField(TEXT("staminaRequired"), Transition->StaminaRequired);
	Json->SetBoolField(TEXT("staminaValueIsProvisionalWhenNew"), true);
	Json->SetArrayField(TEXT("requiredTags"), TagValues(Transition->RequiredTags));
	Json->SetArrayField(TEXT("blockedTags"), TagValues(Transition->BlockedTags));
	return Json;
}

void AddPendingE1Report(FSetupRun& Run, const FMontagePlan& Main,
	const FMontagePlan& Over)
{
	TSharedPtr<FJsonObject> Pending = NewJsonObject();
	TArray<TSharedPtr<FJsonValue>> CollisionConfigs;
	for (int32 ConfigIndex = 0; ConfigIndex < 3; ++ConfigIndex)
	{
		int32 MatchCount = 0;
		TArray<FString> AssetsWithWindows;
		const FMontagePlan* Montages[] = { &Main, &Over };
		for (const FMontagePlan* Plan : Montages)
		{
			if (!Plan->Montage)
			{
				continue;
			}
			for (const FAnimNotifyEvent& Event : Plan->Montage->Notifies)
			{
				const UAnimNotifyState_AttackCollision* Collision =
					Cast<UAnimNotifyState_AttackCollision>(Event.NotifyStateClass.Get());
				if (Collision && Collision->ConfigIndex == ConfigIndex)
				{
					++MatchCount;
					AssetsWithWindows.Add(Plan->ObjectPath);
				}
			}
		}
		TSharedPtr<FJsonObject> Entry = NewJsonObject();
		Entry->SetNumberField(TEXT("configIndex"), ConfigIndex);
		Entry->SetNumberField(TEXT("authoredWindowCount"), MatchCount);
		Entry->SetBoolField(TEXT("pendingE1"), MatchCount == 0);
		Entry->SetArrayField(TEXT("montagesWithWindows"), StringValues(AssetsWithWindows));
		Entry->SetStringField(TEXT("owner"), TEXT("E1 user-authored Attack Collision notify state"));
		CollisionConfigs.Add(JsonObjectValue(Entry));
	}
	Pending->SetArrayField(TEXT("collisionConfigIndices"), MoveTemp(CollisionConfigs));

	int32 ComboWindowCount = 0;
	int32 DodgeWindowCount = 0;
	if (Over.Montage)
	{
		for (const FAnimNotifyEvent& Event : Over.Montage->Notifies)
		{
			ComboWindowCount += Event.NotifyStateClass
				&& Event.NotifyStateClass->IsA(UAnimNotifyState_ComboWindow::StaticClass()) ? 1 : 0;
			DodgeWindowCount += Event.NotifyStateClass
				&& Event.NotifyStateClass->IsA(UAnimNotifyState_DodgeAcceptWindow::StaticClass()) ? 1 : 0;
		}
	}
	TSharedPtr<FJsonObject> LandingWindows = NewJsonObject();
	LandingWindows->SetStringField(TEXT("montage"), Over.ObjectPath);
	LandingWindows->SetNumberField(TEXT("comboWindowCount"), ComboWindowCount);
	LandingWindows->SetNumberField(TEXT("dodgeAcceptWindowCount"), DodgeWindowCount);
	TArray<FString> LandingPending;
	if (ComboWindowCount == 0) { LandingPending.Add(TEXT("Combo Window")); }
	if (DodgeWindowCount == 0) { LandingPending.Add(TEXT("Dodge Accept Window")); }
	LandingWindows->SetArrayField(TEXT("pendingE1Windows"), StringValues(LandingPending));
	LandingWindows->SetBoolField(TEXT("pendingDoesNotFailCommandlet"), true);
	Pending->SetObjectField(TEXT("landingWindows"), LandingWindows.ToSharedRef());
	Run.Report->SetObjectField(TEXT("pendingE1"), Pending.ToSharedRef());
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
	Package->MarkPackageDirty();
	FSavePackageArgs Args;
	Args.TopLevelFlags = RF_Public | RF_Standalone;
	return UPackage::SavePackage(Package, &Asset, *Filename, Args);
}

bool WriteReport(FSetupRun& Run, const TCHAR* Mode, const bool bPackagesWereLoaded)
{
	Run.Report->SetStringField(TEXT("mode"), Mode);
	Run.Report->SetBoolField(TEXT("apply"), Run.bApply);
	Run.Report->SetBoolField(TEXT("packagesLoaded"), bPackagesWereLoaded);
	Run.Report->SetBoolField(TEXT("success"), Run.Errors.IsEmpty());
	Run.Report->SetArrayField(TEXT("errors"), StringValues(Run.Errors));
	Run.Report->SetArrayField(TEXT("warnings"), StringValues(Run.Warnings));
	Run.Report->SetArrayField(TEXT("savedPackages"), StringValues(Run.SavedPackages));
	FString Output;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	const bool bSerialized = FJsonSerializer::Serialize(Run.Report.ToSharedRef(), Writer);
	const TCHAR* ModeSuffix = FCString::Strcmp(Mode, TEXT("apply")) == 0
		? TEXT("apply") : TEXT("audit");
	const FString ReportPath = FPaths::ProjectSavedDir()
		/ FString::Printf(TEXT("_kinsect_piercer_asset_setup_%s.json"), ModeSuffix);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(ReportPath), true);
	const bool bWritten = bSerialized && FFileHelper::SaveStringToFile(Output, *ReportPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	if (!bWritten)
	{
		UE_LOG(LogTemp, Error, TEXT("[KinsectPiercerAssets] Could not write JSON report: %s"), *ReportPath);
	}
	else
	{
		UE_LOG(LogTemp, Display, TEXT("[KinsectPiercerAssets] mode=%s success=%s report=%s"),
			Mode, Run.Errors.IsEmpty() ? TEXT("true") : TEXT("false"), *ReportPath);
	}
	return bWritten && Run.Errors.IsEmpty();
}

bool ExecuteSetup(FSetupRun& Run)
{
	Run.bPackagesLoaded = true;
	FSetupTags Tags;
	ResolveTags(Run, Tags);

	UInsectGlaiveCombatConfig* CombatConfig = LoadRequiredAsset<UInsectGlaiveCombatConfig>(
		Run, CombatConfigPath, TEXT("Insect Glaive CombatConfig"));
	UMHGZWeaponComboData* Combo = LoadRequiredAsset<UMHGZWeaponComboData>(
		Run, ComboDataPath, TEXT("Insect Glaive Combo data"));
	UWeaponRuntimeDefinition* Runtime = LoadRequiredAsset<UWeaponRuntimeDefinition>(
		Run, RuntimeDefinitionPath, TEXT("Insect Glaive RuntimeDefinition"));
	TSharedPtr<FJsonObject> Wiring = NewJsonObject();
	Wiring->SetStringField(TEXT("runtimeCombatConfig"), Runtime && Runtime->CombatConfig
		? Runtime->CombatConfig->GetPathName() : TEXT(""));
	Wiring->SetStringField(TEXT("combatComboData"), CombatConfig && CombatConfig->ComboData
		? CombatConfig->ComboData->GetPathName() : TEXT(""));
	Wiring->SetBoolField(TEXT("matchesExpectedReferences"), Runtime && CombatConfig && Combo
		&& Runtime->CombatConfig == CombatConfig && CombatConfig->ComboData == Combo);
	Run.Report->SetObjectField(TEXT("runtimeWiring"), Wiring.ToSharedRef());
	if (Runtime && CombatConfig && Combo
		&& (Runtime->CombatConfig != CombatConfig || CombatConfig->ComboData != Combo))
	{
		Run.Error(TEXT("RuntimeDefinition -> CombatConfig -> ComboData does not reference the requested assets; refusing to edit disconnected configuration."));
	}
	USkeletalMesh* GlaiveMesh = LoadRequiredAsset<USkeletalMesh>(
		Run, GlaiveMeshPath, TEXT("Insect Glaive skeletal mesh"));
	UBlueprint* ExistingSlashBlueprint = LoadRequiredAsset<UBlueprint>(
		Run, ExistingAttackBlueprintPath, TEXT("existing working Insect Glaive attack Blueprint"));
	UMHGZKinsectSlashAbility* ExistingSlashAbility = ExistingSlashBlueprint
		&& ExistingSlashBlueprint->GeneratedClass
		? Cast<UMHGZKinsectSlashAbility>(ExistingSlashBlueprint->GeneratedClass->GetDefaultObject())
		: nullptr;
	FAttackCollisionConfig* ReferenceCollision = FindReferenceCollision(ExistingSlashAbility);
	if (!ReferenceCollision)
	{
		Run.Error(TEXT("Could not resolve a working WeaponTrace collision channel/HitzoneQueryTag from GA_IG_KinsectSlash."));
	}

	AddCombatConfigReport(Run, CombatConfig);
	if (CombatConfig)
	{
		IsProfileReady(Run, *CombatConfig);
		PreloadCombatEffectDefaults(*CombatConfig);
	}

	const bool bRootBonePresent = GlaiveMesh
		&& GlaiveMesh->GetRefSkeleton().FindBoneIndex(FName(TEXT("Root"))) != INDEX_NONE;
	int32 RootSocketIndex = INDEX_NONE;
	const bool bRootSocketPresent = FindMeshSocket(GlaiveMesh, FName(TEXT("Root")), RootSocketIndex);
	const bool bRootPresent = bRootBonePresent || bRootSocketPresent;
	int32 FrontTipSocketIndex = INDEX_NONE;
	const bool bFrontTipPresent = FindMeshSocket(GlaiveMesh,
		FName(TEXT("IG_FrontTip")), FrontTipSocketIndex);
	Run.Report->SetObjectField(TEXT("glaiveMesh"), MeshJson(GlaiveMesh,
	bRootBonePresent, bRootSocketPresent, RootSocketIndex,
	bFrontTipPresent, FrontTipSocketIndex).ToSharedRef());
	if (!GlaiveMesh || !GlaiveMesh->GetSkeleton() || !bRootPresent || !bFrontTipPresent)
	{
		Run.Error(TEXT("SKM_IG_Glaive must be a USkeletalMesh using the Insect Glaive animation skeleton (SK_IG_Glaive) and expose Root bone/socket plus the IG_FrontTip socket."));
	}

	TArray<FSequencePlan> Sequences;
	Sequences.SetNum(4);
	const FString SourceRoot = TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/");
	const FString SourceNames[] = {
		TEXT("AS_Unsh_CaoChongChuanCi"),
		TEXT("AS_Unsh_CaoChongChuanci_Flying"),
		TEXT("AS_Unsh_CaoChongChuanCI_Flying_Loop"),
		TEXT("AS_Unsh_CaoChongChuanCi_Over")
	};
	const FString GeneratedNames[] = {
		TEXT("AS_Unsh_CaoChongChuanCi_IP"),
		TEXT("AS_Unsh_CaoChongChuanci_Flying_IP"),
		TEXT("AS_Unsh_CaoChongChuanCI_Flying_Loop_IP"),
		TEXT("AS_Unsh_CaoChongChuanCi_Over_RM")
	};
	for (int32 Index = 0; Index < Sequences.Num(); ++Index)
	{
		const FString SourcePath = MakeAssetObjectPath(SourceRoot + SourceNames[Index], SourceNames[Index]);
		float DesiredRuntime = 0.0f;
		if (Index == 0 && CombatConfig)
		{
			DesiredRuntime = CombatConfig->KinsectPiercerStartProfile.CoveredSeconds;
		}
		else if ((Index == 1 || Index == 2))
		{
			UAnimSequence* SourceSequence = LoadRequiredAsset<UAnimSequence>(Run, *SourcePath,
				TEXT("source sequence"));
			DesiredRuntime = SourceSequence ? SourceSequence->GetPlayLength() / 2.0f : 0.0f;
		}
		else if (Index == 3 && CombatConfig)
		{
			DesiredRuntime = ExpectedLandingSeconds;
		}
		MakeSequencePlan(Run, Sequences[Index], *SourcePath, *GeneratedNames[Index], DesiredRuntime, Index == 3);
	}
	for (int32 Index = 1; Index < Sequences.Num(); ++Index)
	{
		if (Sequences[0].Source && Sequences[Index].Source
			&& Sequences[Index].Source->GetSkeleton() != Sequences[0].Source->GetSkeleton())
		{
			Run.Error(TEXT("All four source sequences must use the same skeleton."));
		}
	}
	// Character presentation sequences share the character skeleton, not the
	// separate glaive mesh skeleton. Weapon sockets are audited independently.

	FMontagePlan MainPlan;
	MainPlan.PackageName = MainMontagePackage;
	MainPlan.AssetName = TEXT("AM_IG_KinsectPiercer");
	MainPlan.ObjectPath = MakeAssetObjectPath(MainPlan.PackageName, MainPlan.AssetName);
	MainPlan.Skeleton = Sequences[0].Source ? Sequences[0].Source->GetSkeleton() : nullptr;
	MainPlan.Montage = LoadTargetAsset<UAnimMontage>(Run, MainPlan.ObjectPath,
		TEXT("main montage"), MainPlan.bWasExisting);
	MainPlan.bIsNew = !MainPlan.bWasExisting && Run.bApply;
	FMontagePlan LandingPlan;
	LandingPlan.PackageName = LandingMontagePackage;
	LandingPlan.AssetName = TEXT("AM_IG_KinsectPiercer_Over");
	LandingPlan.ObjectPath = MakeAssetObjectPath(LandingPlan.PackageName, LandingPlan.AssetName);
	LandingPlan.Skeleton = Sequences[0].Source ? Sequences[0].Source->GetSkeleton() : nullptr;
	LandingPlan.Montage = LoadTargetAsset<UAnimMontage>(Run, LandingPlan.ObjectPath,
		TEXT("landing montage"), LandingPlan.bWasExisting);
	LandingPlan.bIsNew = !LandingPlan.bWasExisting && Run.bApply;
	for (FMontagePlan* Plan : { &MainPlan, &LandingPlan })
	{
		if (Plan->Montage)
		{
			if (Plan->Montage->GetSkeleton() != Plan->Skeleton)
			{
				Run.Error(FString::Printf(TEXT("Existing montage skeleton is incompatible: %s"), *Plan->ObjectPath));
			}
			Plan->NotifyFingerprintBefore = MontageNotifyFingerprints(*Plan->Montage);
			Plan->TrackFingerprintBefore = MontageTrackFingerprints(*Plan->Montage);
		}
	}

	UObject* AbilityObject = LoadAnyAsset(MakeAssetObjectPath(AbilityPackage, TEXT("GA_IG_KinsectPiercer")));
	UBlueprint* AbilityBlueprint = Cast<UBlueprint>(AbilityObject);
	bool bAbilityExists = AbilityObject != nullptr;
	if (AbilityObject && !AbilityBlueprint)
	{
		Run.Error(FString::Printf(TEXT("Wrong asset type at Kinsect Piercer GA path: %s"),
			*AbilityObject->GetClass()->GetPathName()));
	}
	if (!AbilityObject && !Run.bApply)
	{
		Run.Error(TEXT("Expected generated Kinsect Piercer GA Blueprint is missing."));
	}
	if (AbilityBlueprint && (!AbilityBlueprint->GeneratedClass
		|| !AbilityBlueprint->GeneratedClass->IsChildOf(UMHGZKinsectPiercerAbility::StaticClass())))
	{
		Run.Error(TEXT("Existing GA_IG_KinsectPiercer Blueprint is not derived from UMHGZKinsectPiercerAbility."));
	}
	const bool bAbilityNew = !bAbilityExists && Run.bApply;
	if (bAbilityNew && Run.Errors.IsEmpty())
	{
		UPackage* Package = CreatePackage(AbilityPackage);
		AbilityBlueprint = Package ? FKismetEditorUtilities::CreateBlueprint(
			UMHGZKinsectPiercerAbility::StaticClass(), Package,
			FName(TEXT("GA_IG_KinsectPiercer")), BPTYPE_Normal,
			UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(),
			FName(TEXT("MHGZM5KinsectPiercerAssetSetup"))) : nullptr;
		if (!AbilityBlueprint || !AbilityBlueprint->GeneratedClass)
		{
			Run.Error(TEXT("Could not create the Kinsect Piercer GA Blueprint."));
		}
	}
	UMHGZKinsectPiercerAbility* Ability = AbilityBlueprint && AbilityBlueprint->GeneratedClass
		? Cast<UMHGZKinsectPiercerAbility>(AbilityBlueprint->GeneratedClass->GetDefaultObject()) : nullptr;

	if (MainPlan.Montage && !MainPlan.bIsNew)
	{
		MainPlan.bLayoutMatches = MontageLayoutMatches(*MainPlan.Montage, true,
			Sequences[0], Sequences[1], Sequences[2], Sequences[3]);
		if (!HasRequiredBaseNotifyTracks(*MainPlan.Montage, true))
		{
			Run.Error(TEXT("Existing main montage must retain Attack/Action/Window notify tracks in indices 0/1/2."));
		}
		if (!MainPlan.bLayoutMatches && MainPlan.Montage->Notifies.Num() > 0)
		{
			Run.Error(TEXT("Existing main montage layout is incompatible with user notifications; it will not be relinked."));
		}
		if (!Run.bApply && !MainPlan.bLayoutMatches)
		{
			Run.Error(TEXT("Existing main montage layout does not match expected three-section layout."));
		}
	}
	if (LandingPlan.Montage && !LandingPlan.bIsNew)
	{
		LandingPlan.bLayoutMatches = MontageLayoutMatches(*LandingPlan.Montage, false,
			Sequences[0], Sequences[1], Sequences[2], Sequences[3]);
		if (!HasRequiredBaseNotifyTracks(*LandingPlan.Montage, false))
		{
			Run.Error(TEXT("Existing Over montage must retain Attack/Action/Window notify tracks in indices 0/1/2."));
		}
		if (!LandingPlan.bLayoutMatches
			&& (!Run.bApply || !CanMigrateLegacyLanding(*LandingPlan.Montage, Sequences[3])))
		{
			Run.Error(TEXT("Existing Over montage is incompatible or unmigrated; only an exact legacy Landing203 pointer migration is allowed."));
		}
	}
	if (MainPlan.Montage && !Run.bApply && (!FMath::IsNearlyEqual(
		MainPlan.Montage->BlendIn.GetBlendTime(), MainBlendInTime, 0.0001f)
		|| !FMath::IsNearlyEqual(MainPlan.Montage->BlendOut.GetBlendTime(), OrdinaryBlendOutTime, 0.0001f)
		|| !MainPlan.Montage->bEnableAutoBlendOut))
	{
		Run.Error(TEXT("Main montage must use auto blend-out, 0.05 s blend-in convention, and 0.1 s blend-out."));
	}
	if (LandingPlan.Montage && CombatConfig
		&& (!FMath::IsNearlyEqual(LandingPlan.Montage->BlendIn.GetBlendTime(),
			CombatConfig->KinsectPiercerLandingBlendInTime, 0.0001f)
		|| !FMath::IsNearlyEqual(LandingPlan.Montage->BlendOut.GetBlendTime(),
			OrdinaryBlendOutTime, 0.0001f)
		|| !LandingPlan.Montage->bEnableAutoBlendOut))
	{
		Run.Error(TEXT("Over montage blend settings differ from CombatConfig landing blend-in / ordinary 0.1 s blend-out."));
	}

	bool bBlueprintNeedsCompile = false;
	if (Ability && !bAbilityNew && MainPlan.Montage && LandingPlan.Montage)
	{
		if (!Run.bApply && !AbilityDefaultsMatch(*Ability, MainPlan.Montage,
			LandingPlan.Montage, Tags))
		{
			Run.Error(TEXT("Existing Kinsect Piercer GA defaults do not match the montage/input/section/correction contract."));
		}
		if (!GlaiveMesh)
		{
			Run.Error(TEXT("Cannot audit the GA trace regions without the SKM_IG_Glaive skeletal mesh."));
		}
		else
		{
			ValidateTraceShape(Run, *Ability, *GlaiveMesh, ReferenceCollision, false);
		}
	}
	else if (Ability && !bAbilityNew && !MainPlan.Montage)
	{
		Run.Error(TEXT("Existing GA cannot resolve its required main montage."));
	}

	TArray<FComboTransition> ComboBefore = Combo ? Combo->Transitions : TArray<FComboTransition>();
	bool bComboChanged = false;
	TSharedPtr<FJsonObject> ComboJson;
	TSet<FName> OwnedComboTransitionIDs;
	OwnedComboTransitionIDs.Add(FName(TargetTransitionID));
	if (Combo && AbilityBlueprint && AbilityBlueprint->GeneratedClass
		&& (Ability || bAbilityNew || !Run.bApply))
	{
		ComboJson = ConfigureComboRoute(Run, *Combo, Tags,
			AbilityBlueprint ? AbilityBlueprint->GeneratedClass : nullptr, bComboChanged);
		ConfigureKinsectPiercerDerivedRoutes(Run, *Combo, bComboChanged,
			OwnedComboTransitionIDs);
		Run.Report->SetObjectField(TEXT("combo"), ComboJson.ToSharedRef());
	}

	bool bInputProfileChanged = false;
	TSharedPtr<FJsonObject> InputJson;
	if (Runtime)
	{
		InputJson = ConfigureInputProfile(Run, *Runtime, Tags, bInputProfileChanged);
		Run.Report->SetObjectField(TEXT("input"), InputJson.ToSharedRef());
	}

	TArray<TSharedPtr<FJsonValue>> SequenceReports;
	for (const FSequencePlan& Plan : Sequences)
	{
		SequenceReports.Add(JsonObjectValue(SequenceJson(Plan)));
	}
	Run.Report->SetArrayField(TEXT("presentationSequences"), MoveTemp(SequenceReports));
	if (Sequences[3].Source && Sequences[3].bSourceRootValidated)
	{
		AuditLandingRootMotion(Run, Sequences[3]);
	}

	if (Run.Errors.IsEmpty() && Run.bApply)
	{
		for (FSequencePlan& Plan : Sequences)
		{
			if (!Plan.Generated)
			{
				UPackage* Package = CreatePackage(*Plan.GeneratedPackage);
				Plan.Generated = Package ? DuplicateObject<UAnimSequence>(Plan.Source, Package,
					FName(Plan.GeneratedName)) : nullptr;
				if (!Plan.Generated)
				{
					Run.Error(FString::Printf(TEXT("Could not duplicate source sequence into persistent package: %s"),
						*Plan.GeneratedObjectPath));
					break;
				}
				Plan.Generated->SetFlags(RF_Public | RF_Standalone);
				if (Plan.bLandingRootMotion) { Plan.Generated->RateScale = LandingSequenceRateScale; }
				Plan.bChanged = true;
			}
			Plan.bSourcePoseValidated = Plan.bLandingRootMotion
				? HasSameSourceKeys(*Plan.Source, *Plan.Generated) : HasSameSourcePose(*Plan.Source, *Plan.Generated);
			if (!Plan.bSourcePoseValidated)
			{
				Run.Error(FString::Printf(TEXT("Duplicated sequence timing/bone samples changed unexpectedly: %s"),
					*Plan.GeneratedObjectPath));
				break;
			}
			const ERootMotionRootLock::Type RootLock = Plan.bLandingRootMotion
				? ERootMotionRootLock::AnimFirstFrame : ERootMotionRootLock::RefPose;
			const bool bRootSettingsDiffer = Plan.Generated->bEnableRootMotion != Plan.bLandingRootMotion
				|| !Plan.Generated->bForceRootLock
				|| Plan.Generated->RootMotionRootLock != RootLock;
			if (bRootSettingsDiffer)
			{
				if (!Plan.bIsNew)
				{
					Plan.Generated->Modify();
				}
				Plan.Generated->bEnableRootMotion = Plan.bLandingRootMotion;
				Plan.Generated->bForceRootLock = true;
				Plan.Generated->RootMotionRootLock = RootLock;
				Plan.bChanged = true;
			}
		}

		if (Run.Errors.IsEmpty()) { AuditLandingRootMotion(Run, Sequences[3]); }

		if (Run.Errors.IsEmpty())
		{
			for (FMontagePlan* Plan : { &MainPlan, &LandingPlan })
			{
				SetupMontage(Run, *Plan, Plan == &MainPlan,
					Sequences[0], Sequences[1], Sequences[2], Sequences[3],
					CombatConfig->KinsectPiercerLandingBlendInTime);
			}
		}

		if (Run.Errors.IsEmpty())
		{
			Ability = AbilityBlueprint && AbilityBlueprint->GeneratedClass
				? Cast<UMHGZKinsectPiercerAbility>(AbilityBlueprint->GeneratedClass->GetDefaultObject())
				: nullptr;
			if (!Ability)
			{
				Run.Error(TEXT("Kinsect Piercer GA Blueprint CDO is not the expected native class."));
			}
			else
			{
				const bool bSeedSegments = bAbilityNew;
				if (bSeedSegments && ReferenceCollision)
				{
					Ability->Modify();
					SeedNewAbilitySegments(*Ability, *ReferenceCollision);
				}
				const bool bDefaultsChanged = ConfigureAbilityDefaults(*Ability,
					*MainPlan.Montage, *LandingPlan.Montage, Tags, bSeedSegments);
				if (bDefaultsChanged || bAbilityNew)
				{
					AbilityBlueprint->Modify();
					Ability->Modify();
					bBlueprintNeedsCompile = true;
				}
				if (!ValidateTraceShape(Run, *Ability, *GlaiveMesh, ReferenceCollision, bSeedSegments))
				{
					// All three segments are audited; errors are kept in the report before any package save.
				}
			}
		}

		if (Run.Errors.IsEmpty() && AbilityBlueprint && bBlueprintNeedsCompile)
		{
			FKismetEditorUtilities::CompileBlueprint(AbilityBlueprint,
				EBlueprintCompileOptions::SkipSave
				| EBlueprintCompileOptions::SkipDefaultObjectValidation
				| EBlueprintCompileOptions::SkipGarbageCollection);
			Ability = AbilityBlueprint->GeneratedClass
				? Cast<UMHGZKinsectPiercerAbility>(AbilityBlueprint->GeneratedClass->GetDefaultObject())
				: nullptr;
			if (AbilityBlueprint->Status == BS_Error || !Ability
				|| !AbilityDefaultsMatch(*Ability, MainPlan.Montage, LandingPlan.Montage, Tags)
				|| !ValidateTraceShape(Run, *Ability, *GlaiveMesh, ReferenceCollision, bAbilityNew))
			{
				Run.Error(TEXT("Kinsect Piercer Blueprint compile or CDO readback failed."));
			}
		}
		if (Run.Errors.IsEmpty() && Combo && AbilityBlueprint && AbilityBlueprint->GeneratedClass)
		{
			ComboJson = ConfigureComboRoute(Run, *Combo, Tags,
				AbilityBlueprint->GeneratedClass, bComboChanged);
			ConfigureKinsectPiercerDerivedRoutes(Run, *Combo, bComboChanged,
				OwnedComboTransitionIDs);
			Run.Report->SetObjectField(TEXT("combo"), ComboJson.ToSharedRef());
		}
	}
	else if (Run.Errors.IsEmpty() && !Run.bApply)
	{
		if (AbilityBlueprint && AbilityBlueprint->Status == BS_Error)
		{
			Run.Error(TEXT("Kinsect Piercer Blueprint has compile errors."));
		}
	}

	if (Combo && Run.Errors.IsEmpty())
	{
		if (!OtherComboTransitionsUnchanged(ComboBefore, Combo->Transitions,
			OwnedComboTransitionIDs))
		{
			Run.Error(TEXT("A Combo transition outside the Kinsect Piercer route set changed during setup."));
		}
	}
	if (Combo)
	{
		Run.Report->SetArrayField(TEXT("otherTransitionsBefore"),
			TransitionArrayJson(ComboBefore, FName(TargetTransitionID)));
		Run.Report->SetArrayField(TEXT("otherTransitionsAfter"),
			TransitionArrayJson(Combo->Transitions, FName(TargetTransitionID)));
		const FComboTransition* TargetTransition = Combo->Transitions.FindByPredicate(
			[](const FComboTransition& Row) { return Row.TransitionID == FName(TargetTransitionID); });
		Run.Report->SetObjectField(TEXT("piercerTransition"),
			ComboRouteSummary(TargetTransition).ToSharedRef());
	}

	if (Run.bApply && Run.Errors.IsEmpty())
	{
		for (FMontagePlan* Plan : { &MainPlan, &LandingPlan })
		{
			if (Plan->bWasExisting
				&& (Plan->NotifyFingerprintBefore != MontageNotifyFingerprints(*Plan->Montage)
					|| Plan->TrackFingerprintBefore != MontageTrackFingerprints(*Plan->Montage)))
			{
				Run.Error(FString::Printf(TEXT("Montage notify GUID/time/link/track fingerprint changed during setup: %s"),
					*Plan->ObjectPath));
			}
		}
	}

	// Report post-Apply objects, not the preflight source placeholders.
	SequenceReports.Reset();
	for (const FSequencePlan& Plan : Sequences)
	{
		SequenceReports.Add(JsonObjectValue(SequenceJson(Plan)));
	}
	Run.Report->SetArrayField(TEXT("presentationSequences"), MoveTemp(SequenceReports));
	if (MainPlan.Montage)
	{
		MainPlan.bLayoutMatches = MontageLayoutMatches(*MainPlan.Montage, true,
			Sequences[0], Sequences[1], Sequences[2], Sequences[3]);
		Run.Report->SetObjectField(TEXT("mainMontage"), MontageJson(MainPlan).ToSharedRef());
	}
	if (LandingPlan.Montage)
	{
		LandingPlan.bLayoutMatches = MontageLayoutMatches(*LandingPlan.Montage, false,
			Sequences[0], Sequences[1], Sequences[2], Sequences[3]);
		Run.Report->SetObjectField(TEXT("landingMontage"), MontageJson(LandingPlan).ToSharedRef());
	}
	Run.Report->SetObjectField(TEXT("ability"),
		AbilityJson(AbilityBlueprint, Ability, AbilityBlueprint && AbilityBlueprint->Status != BS_Error).ToSharedRef());
	if (Runtime)
	{
		Run.Report->SetStringField(TEXT("runtimeDefinitionPath"), Runtime->GetPathName());
		Run.Report->SetStringField(TEXT("resolvedInputProfilePath"), Runtime->InputProfile
			? Runtime->InputProfile->GetPathName() : TEXT(""));
	}
	AddPendingE1Report(Run, MainPlan, LandingPlan);

	if (Run.bApply && Run.Errors.IsEmpty())
	{
		TArray<TPair<UObject*, bool>> SaveCandidates;
		for (FSequencePlan& Plan : Sequences) { SaveCandidates.Emplace(Plan.Generated, Plan.bChanged); }
		SaveCandidates.Emplace(MainPlan.Montage, MainPlan.bChanged);
		SaveCandidates.Emplace(LandingPlan.Montage, LandingPlan.bChanged);
		SaveCandidates.Emplace(AbilityBlueprint, bBlueprintNeedsCompile || bAbilityNew);
		SaveCandidates.Emplace(Combo, bComboChanged);
		SaveCandidates.Emplace(Runtime ? Runtime->InputProfile.Get() : nullptr, bInputProfileChanged);
		TSet<UObject*> SavedObjects;
		for (const TPair<UObject*, bool>& Candidate : SaveCandidates)
		{
			if (!Candidate.Key || !Candidate.Value || SavedObjects.Contains(Candidate.Key))
			{
				continue;
			}
			if (!SaveAsset(*Candidate.Key))
			{
				Run.Error(FString::Printf(TEXT("Package save failed: %s"),
					*Candidate.Key->GetOutermost()->GetName()));
				break;
			}
			SavedObjects.Add(Candidate.Key);
			Run.SavedPackages.Add(Candidate.Key->GetOutermost()->GetName());
		}
	}

	return Run.Errors.IsEmpty();
}

} // namespace UE::MHGZ::KinsectPiercerAssetSetup

#endif // WITH_EDITOR

UMHGZM5KinsectPiercerAssetSetupCommandlet::UMHGZM5KinsectPiercerAssetSetupCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UMHGZM5KinsectPiercerAssetSetupCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	using namespace UE::MHGZ::KinsectPiercerAssetSetup;
	FSetupRun Run;
	const bool bAuditOnly = FParse::Param(*Params, TEXT("AuditOnly"));
	const bool bApply = FParse::Param(*Params, TEXT("Apply"));
	if (bAuditOnly == bApply)
	{
		Run.Error(bAuditOnly
			? TEXT("-AuditOnly and -Apply are contradictory; specify exactly one. No asset package was loaded.")
			: TEXT("An explicit mode is required: use -AuditOnly or -Apply. Bare invocation is rejected before asset packages load."));
		WriteReport(Run, TEXT("invalid"), false);
		return 1;
	}
	Run.bApply = bApply;
	const bool bSucceeded = ExecuteSetup(Run);
	const bool bReportWritten = WriteReport(Run, bApply ? TEXT("apply") : TEXT("audit"),
		Run.bPackagesLoaded);
	return bSucceeded && bReportWritten ? 0 : 1;
#else
	UE_LOG(LogTemp, Error, TEXT("[KinsectPiercerAssets] This commandlet requires an editor build."));
	return 1;
#endif
}

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5KinsectPiercerRouteRankTest,
	"MHGZ.M5.KinsectPiercer.AssetImport.RoutePrecedence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM5KinsectPiercerRouteRankTest::RunTest(const FString& Parameters)
{
	using namespace UE::MHGZ::KinsectPiercerAssetSetup;
	FComboTransition Row;
	Row.bMatchAnyState = true;
	Row.Direction = EDirectionalInput::None;
	Row.Priority = 999;
	TestFalse(TEXT("Lower-priority generic AnyState row cannot outrank Piercer"), TransitionCanOutrankPiercer(Row));
	Row.Priority = 1000;
	TestTrue(TEXT("Equal-priority earlier AnyState row may win"), TransitionCanOutrankPiercer(Row));
	Row.Priority = -100;
	Row.bMatchAnyState = false;
	TestTrue(TEXT("Exact source state beats numeric priority"), TransitionCanOutrankPiercer(Row));
	Row.bMatchAnyState = true;
	Row.Direction = EDirectionalInput::Forward;
	TestTrue(TEXT("Directional specificity beats numeric priority"), TransitionCanOutrankPiercer(Row));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5KinsectPiercerNotifyIdentityPreservedTest,
	"MHGZ.M5.KinsectPiercer.AssetImport.NotifyIdentityPreserved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM5KinsectPiercerNotifyIdentityPreservedTest::RunTest(const FString& Parameters)
{
	using namespace UE::MHGZ::KinsectPiercerAssetSetup;
	UAnimSequence* Source = LoadObject<UAnimSequence>(nullptr,
		TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_CaoChongChuanCi.AS_Unsh_CaoChongChuanCi"));
	if (!TestNotNull(TEXT("Imported source sequence is available for the transient montage fixture"), Source))
	{
		return false;
	}
	UAnimSequence* PoseCopy = DuplicateObject<UAnimSequence>(Source, GetTransientPackage(),
		FName(TEXT("AS_Piercer_SourcePoseAudit")));
	if (!TestNotNull(TEXT("Transient source-pose copy is created"), PoseCopy)) { return false; }
	TestTrue(TEXT("Raw source timing and all bone samples match its clone"), HasSameSourcePose(*Source, *PoseCopy));
	PoseCopy->SetSkeleton(nullptr);
	TestFalse(TEXT("Source-pose audit rejects a changed skeleton"), HasSameSourcePose(*Source, *PoseCopy));

	auto MakeSequencePlan = [Source](const float RuntimeSeconds)
	{
		FSequencePlan Plan;
		Plan.Source = Source;
		Plan.Generated = Source;
		Plan.GeneratedObjectPath = Source->GetPathName();
		Plan.DesiredRuntimeSeconds = RuntimeSeconds;
		Plan.SegmentPlayRate = Source->GetPlayLength()
			/ (RuntimeSeconds * FMath::Abs(Source->RateScale));
		return Plan;
	};
	FSequencePlan StartPlan = MakeSequencePlan(0.25f);
	FSequencePlan DivePlan = MakeSequencePlan(0.30f);
	FSequencePlan LoopPlan = MakeSequencePlan(0.25f);
	FSequencePlan LandingPlan = MakeSequencePlan(0.20f);

	FMontagePlan MatchingPlan;
	MatchingPlan.PackageName = TEXT("/Transient/MHGZ_KinsectPiercer_NotifyIdentity");
	MatchingPlan.AssetName = TEXT("AM_NotifyIdentity");
	MatchingPlan.ObjectPath = TEXT("/Transient/MHGZ_KinsectPiercer_NotifyIdentity.AM_NotifyIdentity");
	MatchingPlan.Skeleton = Source->GetSkeleton();
	MatchingPlan.bWasExisting = true;
	MatchingPlan.Montage = NewObject<UAnimMontage>(GetTransientPackage(),
		FName(TEXT("AM_NotifyIdentity")), RF_Transient);
	if (!TestNotNull(TEXT("Transient montage is created"), MatchingPlan.Montage))
	{
		return false;
	}
	MatchingPlan.Montage->SetSkeleton(MatchingPlan.Skeleton);
	BuildMontageLayout(MatchingPlan, StartPlan, DivePlan, LoopPlan, LandingPlan, true);
	TestTrue(TEXT("Transient montage layout finalizes"), FinalizeMontageLength(*MatchingPlan.Montage));
	MatchingPlan.Montage->AnimNotifyTracks.Reset();
	AddNewMontageTracks(*MatchingPlan.Montage);
	UAnimNotifyState_AttackCollision* CollisionNotify = Cast<UAnimNotifyState_AttackCollision>(
		UAnimationBlueprintLibrary::AddAnimationNotifyStateEvent(MatchingPlan.Montage,
			FName(MainTrackNames[2]), 0.10f, 0.07f,
			UAnimNotifyState_AttackCollision::StaticClass()));
	if (!TestNotNull(TEXT("Real AttackCollision notify state is attached to the transient montage"),
		CollisionNotify))
	{
		return false;
	}
	CollisionNotify->ConfigIndex = 1;
	FAnimNotifyEvent& Event = MatchingPlan.Montage->Notifies.Last();
	Event.ChangeLinkMethod(EAnimLinkMethod::Proportional);
	Event.EndLink.ChangeLinkMethod(EAnimLinkMethod::Relative);
	TestEqual(TEXT("Fixture has one real notify"), MatchingPlan.Montage->Notifies.Num(), 1);
#if WITH_EDITORONLY_DATA
	TestTrue(TEXT("Fixture notify has a nonempty editor GUID"), Event.Guid.IsValid());
#endif
	TestEqual(TEXT("Fixture notify uses the Window track index"), Event.TrackIndex, 2);
	TestEqual(TEXT("Fixture track name is Window"),
		MatchingPlan.Montage->AnimNotifyTracks[2].TrackName.ToString(), FString(TEXT("Window")));
	TestEqual(TEXT("Fixture track color is captured"),
		MatchingPlan.Montage->AnimNotifyTracks[2].TrackColor.ToString(),
		FLinearColor(0.25f, 0.80f, 0.35f).ToString());
	TestEqual(TEXT("Fixture start link method is proportional"),
		static_cast<int32>(Event.GetLinkMethod()), static_cast<int32>(EAnimLinkMethod::Proportional));
	TestEqual(TEXT("Fixture end link method is relative"),
		static_cast<int32>(Event.EndLink.GetLinkMethod()), static_cast<int32>(EAnimLinkMethod::Relative));

	const TArray<FString> NotifyFieldsBefore = MontageNotifyFingerprints(*MatchingPlan.Montage);
	const TArray<FString> TrackFieldsBefore = MontageTrackFingerprints(*MatchingPlan.Montage);
	FSetupRun MatchingRun;
	MatchingRun.bApply = true;
	TestTrue(TEXT("Rerunning setup accepts the already-matching layout"),
		SetupMontage(MatchingRun, MatchingPlan, true,
			StartPlan, DivePlan, LoopPlan, LandingPlan, 0.10f));
	TestTrue(TEXT("Matching-layout rerun reports no errors"), MatchingRun.Errors.IsEmpty());
	TestTrue(TEXT("Matching rerun leaves notify GUID, track index, timing and link methods intact"),
		NotifyFieldsBefore == MontageNotifyFingerprints(*MatchingPlan.Montage));
	TestTrue(TEXT("Matching rerun leaves track names and colors intact"),
		TrackFieldsBefore == MontageTrackFingerprints(*MatchingPlan.Montage));

	UAnimMontage* IncompatibleMontage = DuplicateObject<UAnimMontage>(
		MatchingPlan.Montage, GetTransientPackage(), FName(TEXT("AM_NotifyIdentity_Incompatible")));
	if (!TestNotNull(TEXT("Incompatible-layout transient copy is created"), IncompatibleMontage))
	{
		return false;
	}
	IncompatibleMontage->CompositeSections[0].NextSectionName = FName(TEXT("WrongNextSection"));
	const TArray<FString> IncompatibleNotifyBefore = MontageNotifyFingerprints(*IncompatibleMontage);
	const TArray<FString> IncompatibleTracksBefore = MontageTrackFingerprints(*IncompatibleMontage);
	FMontagePlan FailClosedPlan = MatchingPlan;
	FailClosedPlan.Montage = IncompatibleMontage;
	FailClosedPlan.AssetName = TEXT("AM_NotifyIdentity_Incompatible");
	FailClosedPlan.ObjectPath = TEXT("/Transient/MHGZ_KinsectPiercer_NotifyIdentity.AM_NotifyIdentity_Incompatible");
	FailClosedPlan.bChanged = false;
	FSetupRun FailClosedRun;
	FailClosedRun.bApply = true;
	AddExpectedError(TEXT("Existing montage layout is incompatible while user notifies exist"),
		EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Mismatched layout with a user notify fails closed"),
		SetupMontage(FailClosedRun, FailClosedPlan, true,
			StartPlan, DivePlan, LoopPlan, LandingPlan, 0.10f));
	TestFalse(TEXT("Fail-closed fixture remains layout-incompatible"), FailClosedPlan.bLayoutMatches);
	TestTrue(TEXT("Fail-closed fixture reports an error"), !FailClosedRun.Errors.IsEmpty());
	TestTrue(TEXT("Fail-closed rerun preserves nonempty notify identity and links"),
		IncompatibleNotifyBefore == MontageNotifyFingerprints(*IncompatibleMontage));
	TestTrue(TEXT("Fail-closed rerun preserves track names and colors"),
		IncompatibleTracksBefore == MontageTrackFingerprints(*IncompatibleMontage));
	return true;
}
namespace UE::MHGZ::KinsectPiercerAssetSetup
{
bool MakeLandingMigrationFixture(FSequencePlan& Sequence, FMontagePlan& Plan, const FString& Name,
	const bool bWithNotify)
{
	Sequence.Source = LoadObject<UAnimSequence>(nullptr,
		TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_CaoChongChuanCi_Over.AS_Unsh_CaoChongChuanCi_Over"));
	if (!Sequence.Source) { return false; }
	Sequence.Generated = DuplicateObject<UAnimSequence>(Sequence.Source, GetTransientPackage(), FName(Name + TEXT("_RM")));
	Sequence.LegacyGenerated = DuplicateObject<UAnimSequence>(Sequence.Source, GetTransientPackage(), FName(Name + TEXT("_IP")));
	if (!Sequence.Generated || !Sequence.LegacyGenerated) { return false; }
	Sequence.bLandingRootMotion = true;
	Sequence.bSourceRootValidated = HasValidSourceRoot(*Sequence.Source);
	Sequence.GeneratedObjectPath = Sequence.Generated->GetPathName();
	Sequence.LegacyGeneratedObjectPath = Sequence.LegacyGenerated->GetPathName();
	Sequence.DesiredRuntimeSeconds = ExpectedLandingSeconds;
	Sequence.SegmentPlayRate = Sequence.Source->GetPlayLength() / (ExpectedLandingSeconds * LandingSequenceRateScale);
	Sequence.Generated->RateScale = LandingSequenceRateScale;
	Sequence.Generated->bEnableRootMotion = true;
	Sequence.Generated->bForceRootLock = true;
	Sequence.Generated->RootMotionRootLock = ERootMotionRootLock::AnimFirstFrame;
	Sequence.LegacyGenerated->RateScale = LandingSequenceRateScale;
	Sequence.LegacyGenerated->bEnableRootMotion = false;
	Sequence.LegacyGenerated->bForceRootLock = true;
	Sequence.LegacyGenerated->RootMotionRootLock = ERootMotionRootLock::RefPose;
	Plan.Montage = NewObject<UAnimMontage>(GetTransientPackage(), FName(Name), RF_Transient);
	if (!Plan.Montage) { return false; }
	Plan.ObjectPath = Plan.Montage->GetPathName();
	Plan.Skeleton = Sequence.Source->GetSkeleton();
	Plan.Montage->SetSkeleton(Plan.Skeleton);
	Plan.bWasExisting = true;
	FSequencePlan Legacy = Sequence;
	Legacy.Generated = Sequence.LegacyGenerated;
	Legacy.GeneratedObjectPath = Sequence.LegacyGeneratedObjectPath;
	BuildMontageLayout(Plan, Legacy, Legacy, Legacy, Legacy, false);
	if (!FinalizeMontageLength(*Plan.Montage)) { return false; }
	// UAnimMontage constructs a default notify track. Model a generated asset,
	// which replaces that track before adding Attack/Action/Window at 0/1/2.
	Plan.Montage->AnimNotifyTracks.Reset();
	AddNewMontageTracks(*Plan.Montage);
	if (!HasRequiredBaseNotifyTracks(*Plan.Montage, false)) { return false; }
	Plan.Montage->BlendIn.SetBlendTime(0.10f);
	Plan.Montage->BlendOut.SetBlendTime(OrdinaryBlendOutTime);
	Plan.Montage->bEnableAutoBlendOut = true;
	if (bWithNotify)
	{
		if (!UAnimationBlueprintLibrary::AddAnimationNotifyStateEvent(Plan.Montage,
			FName(MainTrackNames[2]), 0.20f, 0.30f, UAnimNotifyState_AttackCollision::StaticClass())) { return false; }
		FAnimNotifyEvent& Event = Plan.Montage->Notifies.Last();
		Event.ChangeLinkMethod(EAnimLinkMethod::Proportional);
		Event.EndLink.ChangeLinkMethod(EAnimLinkMethod::Relative);
	}
	Plan.bChanged = false;
	return Sequence.bSourceRootValidated;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5KinsectPiercerLandingPointerMigrationTest,
	"MHGZ.M5.KinsectPiercer.AssetImport.Landing203PointerMigration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM5KinsectPiercerLandingPointerMigrationTest::RunTest(const FString& Parameters)
{
	using namespace UE::MHGZ::KinsectPiercerAssetSetup;
	FSequencePlan Sequence;
	FMontagePlan Plan;
	if (!TestTrue(TEXT("Landing203 legacy fixture has a valid native root and authored notify"),
		MakeLandingMigrationFixture(Sequence, Plan, TEXT("AM_Piercer_LandingMigration"), true))) { return false; }
	const TArray<FString> Notifies = MontageNotifyFingerprints(*Plan.Montage);
	const TArray<FString> Tracks = MontageTrackFingerprints(*Plan.Montage);
	const TArray<FString> Timings = MontageTimingFingerprints(*Plan.Montage);
#if WITH_EDITORONLY_DATA
	TestTrue(TEXT("Authored notify GUID is valid"), Plan.Montage->Notifies[0].Guid.IsValid());
#endif
	TestTrue(TEXT("_RM preserves every source bone key and timing"), HasSameSourceKeys(*Sequence.Source, *Sequence.Generated));
	FSetupRun Audit;
	AddExpectedError(TEXT("Existing Over montage is incompatible or unmigrated"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("AuditOnly rejects an unmigrated legacy reference"),
		SetupMontage(Audit, Plan, false, Sequence, Sequence, Sequence, Sequence, 0.10f));
	TestTrue(TEXT("AuditOnly leaves the legacy animation reference intact"),
		Plan.Montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0].GetAnimReference() == Sequence.LegacyGenerated);
	TestFalse(TEXT("AuditOnly does not mark the montage changed"), Plan.bChanged);
	TestTrue(TEXT("AuditOnly preserves all serialized links"), Notifies == MontageNotifyFingerprints(*Plan.Montage));
	TestTrue(TEXT("AuditOnly preserves all timing fields"), Timings == MontageTimingFingerprints(*Plan.Montage));
	FSetupRun Apply;
	Apply.bApply = true;
	TestTrue(TEXT("Apply accepts a pointer-only migration with authored notifies"),
		SetupMontage(Apply, Plan, false, Sequence, Sequence, Sequence, Sequence, 0.10f));
	TestTrue(TEXT("Only the animation reference changes to _RM"),
		Plan.Montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0].GetAnimReference() == Sequence.Generated);
	TestTrue(TEXT("Migration preserves notify GUID/time/track/link fingerprints"), Notifies == MontageNotifyFingerprints(*Plan.Montage));
	TestTrue(TEXT("Migration preserves track names/colors"), Tracks == MontageTrackFingerprints(*Plan.Montage));
	TestTrue(TEXT("Migration preserves slot/segment/rate/section timing and cached links"), Timings == MontageTimingFingerprints(*Plan.Montage));
	TestTrue(TEXT("Migration leaves legacy source keys intact"), HasSameSourceKeys(*Sequence.Source, *Sequence.LegacyGenerated));
	Plan.bChanged = false;
	FSetupRun Repeat;
	Repeat.bApply = true;
	TestTrue(TEXT("Repeat Apply accepts the migrated layout"),
		SetupMontage(Repeat, Plan, false, Sequence, Sequence, Sequence, Sequence, 0.10f));
	TestFalse(TEXT("Repeat Apply does not request a package save"), Plan.bChanged);
	TestTrue(TEXT("Repeat Apply preserves every notify fingerprint"), Notifies == MontageNotifyFingerprints(*Plan.Montage));
	TestTrue(TEXT("Repeat Apply preserves every timing fingerprint"), Timings == MontageTimingFingerprints(*Plan.Montage));
	FSetupRun MigratedAudit;
	TestTrue(TEXT("AuditOnly accepts the migrated layout"),
		SetupMontage(MigratedAudit, Plan, false, Sequence, Sequence, Sequence, Sequence, 0.10f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5KinsectPiercerLandingMigrationRejectsTimingTest,
	"MHGZ.M5.KinsectPiercer.AssetImport.Landing203MigrationRejectsTiming",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM5KinsectPiercerLandingMigrationRejectsTimingTest::RunTest(const FString& Parameters)
{
	using namespace UE::MHGZ::KinsectPiercerAssetSetup;
	for (const bool bWithNotify : { false, true })
	{
		for (int32 Case = 0; Case < 5; ++Case)
		{
			FSequencePlan Sequence;
			FMontagePlan Plan;
			if (!TestTrue(TEXT("Invalid-migration fixture is available"), MakeLandingMigrationFixture(Sequence, Plan,
				FString::Printf(TEXT("AM_Piercer_InvalidMigration_%d_%d"), bWithNotify, Case), bWithNotify))) { return false; }
			FAnimSegment& Segment = Plan.Montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0];
			switch (Case)
			{
			case 0: Segment.AnimEndTime -= 0.00001f; break;
			case 1: Segment.AnimPlayRate += 0.00001f; break;
			case 2: Plan.Montage->CompositeSections[0].SetTime(0.00001f); break;
			case 3: Plan.Montage->RateScale = 0.99999f; break;
			case 4: Sequence.LegacyGenerated->RateScale = 1.0f; break;
			}
			const TArray<FString> Notifies = MontageNotifyFingerprints(*Plan.Montage);
			const TArray<FString> Tracks = MontageTrackFingerprints(*Plan.Montage);
			const TArray<FString> Timings = MontageTimingFingerprints(*Plan.Montage);
			FSetupRun Apply;
			Apply.bApply = true;
			AddExpectedError(TEXT("Existing Over montage is incompatible or unmigrated"), EAutomationExpectedErrorFlags::Contains, 1);
			TestFalse(TEXT("Changed time/rate is rejected even inside the former layout tolerance"),
				SetupMontage(Apply, Plan, false, Sequence, Sequence, Sequence, Sequence, 0.10f));
			TestTrue(TEXT("Failure retains the original animation reference"), Segment.GetAnimReference() == Sequence.LegacyGenerated);
			TestFalse(TEXT("Failure does not request a package save"), Plan.bChanged);
			TestTrue(TEXT("Failure leaves authored notify links intact"), Notifies == MontageNotifyFingerprints(*Plan.Montage));
			TestTrue(TEXT("Failure leaves notify tracks intact"), Tracks == MontageTrackFingerprints(*Plan.Montage));
			TestTrue(TEXT("Failure does not rebuild or normalize timing"), Timings == MontageTimingFingerprints(*Plan.Montage));
		}
	}
	return true;
}
#endif // WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
