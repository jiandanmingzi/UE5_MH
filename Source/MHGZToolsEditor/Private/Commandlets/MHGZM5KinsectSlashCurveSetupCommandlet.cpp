// Copyright MHGZ Project. All Rights Reserved.

#include "Commandlets/MHGZM5KinsectSlashCurveSetupCommandlet.h"

#include "ActionSystem/MHGZWeaponMovementCurves.h"
#include "Curves/CurveFloat.h"
#include "InsectGlaive/InsectGlaiveCombatConfig.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace UE::MHGZ::KinsectSlashCurveSetup
{
const TCHAR* SourceCsv = TEXT("_mhr_curves/操虫斩_187.csv");
const TCHAR* CurveObjectPath =
	TEXT("/Game/Weapons/InsectGlaive/Data/Curves/CF_IG_KinsectSlash_Distance.CF_IG_KinsectSlash_Distance");
const TCHAR* CombatConfigObjectPath =
	TEXT("/Game/Weapons/InsectGlaive/Data/DA_IG_Combat.DA_IG_Combat");
const TCHAR* ReportName = TEXT("_kinsect_slash_curve_setup.json");
constexpr float EndpointTolerance = 0.001f;

struct FCurveSample
{
	float Time = 0.0f;
	float Progress = 0.0f;
};

bool ParseCsvFloat(const FString& RawField, float& OutValue)
{
	FString Field = RawField;
	Field.TrimStartAndEndInline();
	if (Field.IsEmpty())
	{
		return false;
	}

	int32 Index = 0;
	if (Field[Index] == TEXT('+') || Field[Index] == TEXT('-'))
	{
		++Index;
	}

	int32 DigitCount = 0;
	while (Field.IsValidIndex(Index) && FChar::IsDigit(Field[Index]))
	{
		++Index;
		++DigitCount;
	}
	if (Field.IsValidIndex(Index) && Field[Index] == TEXT('.'))
	{
		++Index;
		while (Field.IsValidIndex(Index) && FChar::IsDigit(Field[Index]))
		{
			++Index;
			++DigitCount;
		}
	}
	if (DigitCount == 0)
	{
		return false;
	}

	if (Field.IsValidIndex(Index) && (Field[Index] == TEXT('e') || Field[Index] == TEXT('E')))
	{
		++Index;
		if (Field.IsValidIndex(Index) && (Field[Index] == TEXT('+') || Field[Index] == TEXT('-')))
		{
			++Index;
		}

		int32 ExponentDigits = 0;
		while (Field.IsValidIndex(Index) && FChar::IsDigit(Field[Index]))
		{
			++Index;
			++ExponentDigits;
		}
		if (ExponentDigits == 0)
		{
			return false;
		}
	}

	if (Index != Field.Len())
	{
		return false;
	}

	OutValue = FCString::Atof(*Field);
	return FMath::IsFinite(OutValue);
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
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	return UPackage::SavePackage(Package, &Asset, *Filename, SaveArgs);
}

bool CurveMatchesSamples(const UCurveFloat& Curve, const TArray<FCurveSample>& Samples)
{
	if (Curve.FloatCurve.GetNumKeys() != Samples.Num())
	{
		return false;
	}

	for (int32 Index = 0; Index < Samples.Num(); ++Index)
	{
		const FRichCurveKey& Key = Curve.FloatCurve.Keys[Index];
		if (Key.InterpMode != RCIM_Linear
			|| !FMath::IsNearlyEqual(Key.Time, Samples[Index].Time, EndpointTolerance)
			|| !FMath::IsNearlyEqual(Key.Value, Samples[Index].Progress, EndpointTolerance))
		{
			return false;
		}
	}
	return true;
}

bool ReadSamples(TArray<FCurveSample>& OutSamples, FString& OutError)
{
	const FString CsvPath = FPaths::ProjectSavedDir() / SourceCsv;
	FString CsvText;
	if (!FFileHelper::LoadFileToString(CsvText, *CsvPath))
	{
		OutError = FString::Printf(TEXT("Could not read source CSV: %s"), *CsvPath);
		return false;
	}

	TArray<FString> Lines;
	CsvText.ParseIntoArrayLines(Lines, false);
	if (Lines.Num() < 3)
	{
		OutError = TEXT("CSV must contain a header and at least two sample rows.");
		return false;
	}

	TArray<FString> Headers;
	Lines[0].ParseIntoArray(Headers, TEXT(","), false);
	for (FString& Header : Headers)
	{
		Header.TrimStartAndEndInline();
	}
	const int32 TimeColumn = Headers.IndexOfByKey(FString(TEXT("t_norm")));
	const int32 ProgressColumn = Headers.IndexOfByKey(FString(TEXT("s_norm")));
	if (TimeColumn == INDEX_NONE || ProgressColumn == INDEX_NONE)
	{
		OutError = TEXT("CSV header must contain t_norm and s_norm columns.");
		return false;
	}

	OutSamples.Reset();
	for (int32 LineIndex = 1; LineIndex < Lines.Num(); ++LineIndex)
	{
		if (Lines[LineIndex].TrimStartAndEnd().IsEmpty())
		{
			continue;
		}

		TArray<FString> Fields;
		Lines[LineIndex].ParseIntoArray(Fields, TEXT(","), false);
		if (!Fields.IsValidIndex(TimeColumn) || !Fields.IsValidIndex(ProgressColumn))
		{
			OutError = FString::Printf(TEXT("CSV row %d has too few columns."), LineIndex + 1);
			return false;
		}

		FCurveSample Sample;
		if (!ParseCsvFloat(Fields[TimeColumn], Sample.Time)
			|| !ParseCsvFloat(Fields[ProgressColumn], Sample.Progress))
		{
			OutError = FString::Printf(TEXT("CSV row %d contains an empty or invalid numeric value."), LineIndex + 1);
			return false;
		}
		OutSamples.Add(Sample);
	}

	if (OutSamples.Num() < 2
		|| FMath::Abs(OutSamples[0].Time) > EndpointTolerance
		|| FMath::Abs(OutSamples[0].Progress) > EndpointTolerance
		|| FMath::Abs(OutSamples.Last().Time - 1.0f) > EndpointTolerance
		|| FMath::Abs(OutSamples.Last().Progress - 1.0f) > EndpointTolerance)
	{
		OutError = TEXT("CSV progress must have at least two keys and exact normalized endpoints (0,0) and (1,1).");
		return false;
	}

	for (int32 Index = 1; Index < OutSamples.Num(); ++Index)
	{
		if (OutSamples[Index].Time <= OutSamples[Index - 1].Time
			|| OutSamples[Index].Progress < OutSamples[Index - 1].Progress - EndpointTolerance
			|| OutSamples[Index].Time < -EndpointTolerance
			|| OutSamples[Index].Time > 1.0f + EndpointTolerance
			|| OutSamples[Index].Progress < -EndpointTolerance
			|| OutSamples[Index].Progress > 1.0f + EndpointTolerance)
		{
			OutError = FString::Printf(TEXT("CSV normalized curve is not strictly time-ordered and monotonic at sample %d."), Index);
			return false;
		}
	}
	return true;
}

FString JsonEscape(const FString& Value)
{
	return Value.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("\""), TEXT("\\\""));
}
}

int32 UMHGZM5KinsectSlashCurveSetupCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	using namespace UE::MHGZ::KinsectSlashCurveSetup;
	const bool bAuditOnly = FParse::Param(*Params, TEXT("AuditOnly"));

	TArray<FCurveSample> Samples;
	FString Error;
	if (!ReadSamples(Samples, Error))
	{
		UE_LOG(LogTemp, Error, TEXT("[KinsectSlashCurveSetup] %s"), *Error);
		return 1;
	}

	const FString CurvePath(CurveObjectPath);
	const int32 ObjectSeparator = CurvePath.Find(TEXT("."), ESearchCase::CaseSensitive);
	if (ObjectSeparator == INDEX_NONE)
	{
		UE_LOG(LogTemp, Error, TEXT("[KinsectSlashCurveSetup] Invalid curve object path."));
		return 1;
	}
	const FString CurvePackageName = CurvePath.Left(ObjectSeparator);
	const FString CurveName = CurvePath.Mid(ObjectSeparator + 1);
	UCurveFloat* Curve = LoadObject<UCurveFloat>(nullptr, *CurvePath);
	if (!Curve && !bAuditOnly)
	{
		UPackage* Package = CreatePackage(*CurvePackageName);
		Curve = Package
			? NewObject<UCurveFloat>(Package, FName(*CurveName), RF_Public | RF_Standalone)
			: nullptr;
	}
	if (!Curve)
	{
		UE_LOG(LogTemp, Error, TEXT("[KinsectSlashCurveSetup] Could not create %s."), *CurvePath);
		return 1;
	}

	UInsectGlaiveCombatConfig* Config =
		LoadObject<UInsectGlaiveCombatConfig>(nullptr, CombatConfigObjectPath);
	if (!Config)
	{
		UE_LOG(LogTemp, Error, TEXT("[KinsectSlashCurveSetup] Could not load %s."), CombatConfigObjectPath);
		return 1;
	}

	if (bAuditOnly)
	{
		float TerminalSlope = 0.0f;
		const bool bCurveValid = MHGZWeaponMovementCurves::ValidateNormalizedProgressCurve(
			Curve, TerminalSlope);
		const bool bKeysMatch = CurveMatchesSamples(*Curve, Samples);
		const bool bReferenceReadBack = Config->KinsectSlashDistanceCurve == Curve;
		const bool bValid = bCurveValid && bKeysMatch && bReferenceReadBack;
		UE_LOG(LogTemp, Display,
			TEXT("[KinsectSlashCurveSetup] Audit keys=%d match=%d curve_valid=%d slope=%.6f config_reference=%d valid=%d"),
			Curve->FloatCurve.GetNumKeys(), bKeysMatch ? 1 : 0, bCurveValid ? 1 : 0,
			TerminalSlope, bReferenceReadBack ? 1 : 0, bValid ? 1 : 0);
		return bValid ? 0 : 1;
	}

	bool bCurveSaved = true;
	if (!CurveMatchesSamples(*Curve, Samples))
	{
		Curve->Modify();
		Curve->FloatCurve.Reset();
		for (const FCurveSample& Sample : Samples)
		{
			const FKeyHandle KeyHandle = Curve->FloatCurve.AddKey(Sample.Time, Sample.Progress);
			Curve->FloatCurve.GetKey(KeyHandle).InterpMode = RCIM_Linear;
		}
	}

	float TerminalSlope = 0.0f;
	const bool bCurveValid = MHGZWeaponMovementCurves::ValidateNormalizedProgressCurve(
		Curve, TerminalSlope);
	if (!bCurveValid || Curve->FloatCurve.GetNumKeys() != Samples.Num())
	{
		UE_LOG(LogTemp, Error, TEXT("[KinsectSlashCurveSetup] Generated curve failed runtime validation."));
		return 1;
	}

	if (Curve->GetOutermost()->IsDirty())
	{
		bCurveSaved = SaveAsset(*Curve);
	}
	if (!bCurveSaved)
	{
		const bool bConfigAlreadyReferencedCurve = Config->KinsectSlashDistanceCurve == Curve;
		const FString FailureReport = FString::Printf(
			TEXT("{\n  \"generated_utc\": \"%s\",\n  \"status\": \"failed\",\n")
			TEXT("  \"stage\": \"curve_save\",\n  \"curve\": \"%s\",\n  \"source_csv\": \"%s\",\n")
			TEXT("  \"keys\": %d,\n  \"linear_monotonic_valid\": true,\n")
			TEXT("  \"curve_saved\": false,\n  \"config_reference_before\": %s\n}\n"),
			*FDateTime::UtcNow().ToIso8601(), *JsonEscape(CurvePath), *JsonEscape(SourceCsv),
			Curve->FloatCurve.GetNumKeys(),
			bConfigAlreadyReferencedCurve ? TEXT("true") : TEXT("false"));
		const bool bFailureReportSaved = FFileHelper::SaveStringToFile(FailureReport,
			*(FPaths::ProjectSavedDir() / ReportName),
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		UE_LOG(LogTemp, Error,
			TEXT("[KinsectSlashCurveSetup] Failed to save curve asset %s; failure_report_saved=%d."),
			*CurvePath, bFailureReportSaved ? 1 : 0);
		return 1;
	}

	bool bConfigSaved = true;
	if (Config->KinsectSlashDistanceCurve != Curve)
	{
		Config->Modify();
		Config->KinsectSlashDistanceCurve = Curve;
		bConfigSaved = SaveAsset(*Config);
	}
	const bool bReferenceReadBack = Config->KinsectSlashDistanceCurve == Curve;
	const FString Report = FString::Printf(
		TEXT("{\n  \"generated_utc\": \"%s\",\n  \"curve\": \"%s\",\n  \"source_csv\": \"%s\",\n")
		TEXT("  \"keys\": %d,\n  \"first_time\": %.6f,\n  \"first_progress\": %.6f,\n")
		TEXT("  \"last_time\": %.6f,\n  \"last_progress\": %.6f,\n")
		TEXT("  \"terminal_slope\": %.6f,\n  \"linear_monotonic_valid\": %s,\n")
		TEXT("  \"config\": \"%s\",\n  \"reference_readback\": %s,\n  \"curve_saved\": %s,\n  \"config_saved\": %s\n}\n"),
		*FDateTime::UtcNow().ToIso8601(), *JsonEscape(CurvePath), *JsonEscape(SourceCsv),
		Curve->FloatCurve.GetNumKeys(),
		Curve->FloatCurve.Keys[0].Time, Curve->FloatCurve.Keys[0].Value,
		Curve->FloatCurve.Keys.Last().Time, Curve->FloatCurve.Keys.Last().Value,
		TerminalSlope, bCurveValid ? TEXT("true") : TEXT("false"),
		*JsonEscape(CombatConfigObjectPath), bReferenceReadBack ? TEXT("true") : TEXT("false"),
		bCurveSaved ? TEXT("true") : TEXT("false"), bConfigSaved ? TEXT("true") : TEXT("false"));
	const bool bReportSaved = FFileHelper::SaveStringToFile(Report,
		*(FPaths::ProjectSavedDir() / ReportName),
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	if (!bReportSaved)
	{
		UE_LOG(LogTemp, Error, TEXT("[KinsectSlashCurveSetup] Assets were saved but the report could not be written: %s"),
			*(FPaths::ProjectSavedDir() / ReportName));
		return 1;
	}

	UE_LOG(LogTemp, Display,
		TEXT("[KinsectSlashCurveSetup] keys=%d terminal_slope=%.4f valid=%d curve_saved=%d config_saved=%d report_saved=%d"),
		Curve->FloatCurve.GetNumKeys(), TerminalSlope, bCurveValid ? 1 : 0,
		bCurveSaved ? 1 : 0, bConfigSaved ? 1 : 0, bReportSaved ? 1 : 0);
	if (!bConfigSaved)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[KinsectSlashCurveSetup] Curve package is valid and saved, but DA_IG_Combat could not persist its new curve reference; rerun the commandlet after resolving the save failure."));
	}
	return bCurveValid && bReferenceReadBack && bCurveSaved && bConfigSaved && bReportSaved ? 0 : 1;
#else
	UE_LOG(LogTemp, Error, TEXT("[KinsectSlashCurveSetup] This commandlet requires an editor build."));
	return 1;
#endif
}

UMHGZM5KinsectSlashCurveSetupCommandlet::UMHGZM5KinsectSlashCurveSetupCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}
