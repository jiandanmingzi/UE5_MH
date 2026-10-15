// Copyright MHGZ Project. All Rights Reserved.
#include "Commandlets/MHGZM5KinsectPiercerCurveSetupCommandlet.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Curves/CurveVector.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "InsectGlaive/InsectGlaiveCombatConfig.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

namespace UE::MHGZ::PiercerCurves
{
const TCHAR* CombatPath = TEXT("/Game/Weapons/InsectGlaive/Data/DA_IG_Combat.DA_IG_Combat");
constexpr float TimeTolerance = 0.00001f;
constexpr float ValueTolerance = 0.0001f;

// Byte fingerprints bind this importer to the signed M1 outputs (not a
// security boundary). New recordings must be regenerated and reviewed in M1.
const TCHAR* FrozenCsvSHA1[] = {
	TEXT("07F00FAC57F209297E5F1B4F01E88A235D6BA92B"),
	TEXT("287F8F4E4C802FC137E7C46D55ABBBD58C9EAEA2"),
	TEXT("499A3AF292F65C45A2EBD6785DFB1083383383E6")
};
bool MatchesSignedBytes(const TArray<uint8>& Bytes, const TCHAR* Expected)
{
	FSHAHash Hash;
	FSHA1::HashBuffer(Bytes.GetData(), Bytes.Num(), Hash.Hash);
	return Hash.ToString().Equals(Expected, ESearchCase::IgnoreCase);
}
bool MatchesSignedFile(const FString& Path, const TCHAR* Expected)
{
	TArray<uint8> Bytes;
	return FFileHelper::LoadFileToArray(Bytes, *Path) && MatchesSignedBytes(Bytes, Expected);
}

// M3 imports the already signed M1 recording, not an arbitrary re-timed variant.
bool FrozenMetadata(const int32 PhaseIndex, const double End, const int32 Keys, const double Last, const double Anchor = 0)
{
	const double Ends[] = { 0.391327, 0.366547, 1.566650 };
	const double LastKeys[] = { 0.391667, 0.358333, 1.566667 };
	const int32 Counts[] = { 48, 44, 189 };
	return PhaseIndex >= 0 && PhaseIndex < 3 && Keys == Counts[PhaseIndex]
		&& FMath::IsNearlyEqual(End, Ends[PhaseIndex], 0.000001)
		&& FMath::IsNearlyEqual(Last, LastKeys[PhaseIndex], 0.000001)
		&& (PhaseIndex != 1 || FMath::IsNearlyEqual(Anchor, 0.341667, 0.000001));
}

struct FSample { float Time = 0; FVector Offset = FVector::ZeroVector; };
struct FImport
{
	FString Phase, Suffix, ObjectPath, CsvPath;
	TArray<FSample> Samples;
	float CanonicalEnd = 0, Covered = 0, AnchorTime = 0;
	FVector Velocity = FVector::ZeroVector, Acceleration = FVector::ZeroVector;
	UCurveVector* Existing = nullptr;
	UCurveVector* Desired = nullptr;
	bool Matches = false;
};

bool Number(const FString& Raw, float& Value)
{
	const FString S = Raw.TrimStartAndEnd();
	int32 I = 0, Digits = 0;
	if (S.IsEmpty()) return false;
	if (S[I] == '+' || S[I] == '-') ++I;
	while (S.IsValidIndex(I) && S[I] >= '0' && S[I] <= '9') { ++I; ++Digits; }
	if (S.IsValidIndex(I) && S[I] == '.')
	{
		++I;
		while (S.IsValidIndex(I) && S[I] >= '0' && S[I] <= '9') { ++I; ++Digits; }
	}
	if (!Digits) return false;
	if (S.IsValidIndex(I) && (S[I] == 'e' || S[I] == 'E'))
	{
		++I;
		if (S.IsValidIndex(I) && (S[I] == '+' || S[I] == '-')) ++I;
		const int32 Begin = I;
		while (S.IsValidIndex(I) && S[I] >= '0' && S[I] <= '9') ++I;
		if (I == Begin) return false;
	}
	if (I != S.Len()) return false;
	Value = FCString::Atof(*S);
	return FMath::IsFinite(Value);
}

bool ReadSamples(const FString& Text, const FString& Phase, TArray<FSample>& Samples, FString& Error)
{
	TArray<FString> Lines, Header;
	Text.ParseIntoArrayLines(Lines, false);
	if (Lines.Num() < 3) { Error = TEXT("CSV needs a header and at least two keys"); return false; }
	Lines[0].ParseIntoArray(Header, TEXT(","), false);
	for (FString& S : Header) S.TrimStartAndEndInline();
	const TCHAR* Names[] = { TEXT("phase"), TEXT("sample_index"), TEXT("time_s"), TEXT("forward_cm"), TEXT("right_cm"), TEXT("up_cm") };
	int32 Col[6];
	for (int32 I = 0; I < 6; ++I)
	{
		Col[I] = Header.IndexOfByKey(FString(Names[I]));
		if (Col[I] == INDEX_NONE || Header.FilterByPredicate([&](const FString& S){ return S == Names[I]; }).Num() != 1)
		{ Error = FString::Printf(TEXT("Missing/duplicate CSV column %s"), Names[I]); return false; }
	}
	Samples.Reset();
	for (int32 L = 1; L < Lines.Num(); ++L)
	{
		if (Lines[L].TrimStartAndEnd().IsEmpty()) continue;
		TArray<FString> Fields;
		Lines[L].ParseIntoArray(Fields, TEXT(","), false);
		if (Fields.Num() != Header.Num()) { Error = FString::Printf(TEXT("Wrong column count at row %d"), L + 1); return false; }
		float Index;
		FSample S;
		float XYZ[3];
		if (Fields[Col[0]].TrimStartAndEnd() != Phase || !Number(Fields[Col[1]], Index) || Index != Samples.Num()
			|| !Number(Fields[Col[2]], S.Time) || !Number(Fields[Col[3]], XYZ[0])
			|| !Number(Fields[Col[4]], XYZ[1]) || !Number(Fields[Col[5]], XYZ[2]))
		{ Error = FString::Printf(TEXT("Invalid phase/index/numeric value at row %d"), L + 1); return false; }
		S.Offset = FVector(XYZ[0], XYZ[1], XYZ[2]);
		if (S.Time < 0 || (!Samples.IsEmpty() && S.Time <= Samples.Last().Time))
		{ Error = TEXT("Times must be nonnegative and strictly increasing"); return false; }
		Samples.Add(S);
	}
	if (Samples.Num() < 2 || !FMath::IsNearlyZero(Samples[0].Time, TimeTolerance) || !Samples[0].Offset.IsNearlyZero(ValueTolerance))
	{ Error = TEXT("Curve must start at (time=0, offset=0) and have at least two keys"); return false; }
	return true;
}

bool JsonVector(const FJsonObject& Object, const TCHAR* Name, FVector& Result)
{
	const TArray<TSharedPtr<FJsonValue>>* Values;
	if (!Object.TryGetArrayField(Name, Values) || Values->Num() != 3) return false;
	double XYZ[3];
	for (int32 I = 0; I < 3; ++I) if (!(*Values)[I]->TryGetNumber(XYZ[I]) || !FMath::IsFinite(XYZ[I])) return false;
	Result = FVector(XYZ[0], XYZ[1], XYZ[2]);
	return true;
}

void FillCurve(UCurveVector& Curve, const TArray<FSample>& Samples)
{
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		FRichCurve& C = Curve.FloatCurves[Axis];
		C.Reset();
		C.PreInfinityExtrap = C.PostInfinityExtrap = RCCE_Constant;
		for (const FSample& S : Samples) C.SetKeyInterpMode(C.AddKey(S.Time, S.Offset[Axis]), RCIM_Linear);
	}
}

bool Matches(const UCurveVector& Curve, const TArray<FSample>& Samples)
{
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const FRichCurve& C = Curve.FloatCurves[Axis];
		if (C.GetNumKeys() != Samples.Num() || C.PreInfinityExtrap != RCCE_Constant || C.PostInfinityExtrap != RCCE_Constant) return false;
		for (int32 K = 0; K < Samples.Num(); ++K)
		{
			const FRichCurveKey& Key = C.Keys[K];
			if (Key.InterpMode != RCIM_Linear || !FMath::IsNearlyEqual(Key.Time, Samples[K].Time, TimeTolerance)
				|| !FMath::IsNearlyEqual(Key.Value, Samples[K].Offset[Axis], ValueTolerance)) return false;
		}
	}
	return true;
}

FRecordedLocalTrajectoryProfile Profile(const FImport& I, UCurveVector* Curve)
{
	FRecordedLocalTrajectoryProfile P;
	P.Curve = Curve; P.CoveredSeconds = I.Covered;
	P.TerminalVelocityLocal = I.Velocity; P.TerminalAccelerationLocal = I.Acceleration;
	return P;
}

bool MatchesProfile(const FRecordedLocalTrajectoryProfile& A, const FRecordedLocalTrajectoryProfile& B)
{
	return A.Curve == B.Curve && FMath::IsNearlyEqual(A.CoveredSeconds, B.CoveredSeconds, TimeTolerance)
		&& A.TerminalVelocityLocal.Equals(B.TerminalVelocityLocal, 0.01) && A.TerminalAccelerationLocal.Equals(B.TerminalAccelerationLocal, 0.01);
}

bool Save(UObject& Asset)
{
	FString Filename;
	UPackage* Package = Asset.GetOutermost();
	if (!FPackageName::TryConvertLongPackageNameToFilename(Package->GetName(), Filename, FPackageName::GetAssetPackageExtension())) return false;
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
	Asset.MarkPackageDirty();
	FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone;
	return UPackage::SavePackage(Package, &Asset, *Filename, Args);
}

TArray<TSharedPtr<FJsonValue>> Vec(const FVector& V)
{
	return { MakeShared<FJsonValueNumber>(V.X), MakeShared<FJsonValueNumber>(V.Y), MakeShared<FJsonValueNumber>(V.Z) };
}
}
#endif

UMHGZM5KinsectPiercerCurveSetupCommandlet::UMHGZM5KinsectPiercerCurveSetupCommandlet()
{
	IsClient = false; IsEditor = true; IsServer = false; LogToConsole = true;
}

int32 UMHGZM5KinsectPiercerCurveSetupCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	using namespace UE::MHGZ::PiercerCurves;
	const bool Apply = FParse::Param(*Params, TEXT("Apply"));
	const bool AuditOnly = FParse::Param(*Params, TEXT("AuditOnly"));
	TArray<FString> Errors, Changed;
	if (Apply == AuditOnly) Errors.Add(TEXT("Specify exactly one of -Apply or -AuditOnly; bare execution is refused"));
	const FString SourceDir = FPaths::ProjectSavedDir() / TEXT("_mhr_curves");
	const FString AuditFile = SourceDir / TEXT("强化操虫穿刺_生成审计.json");
	if (!MatchesSignedFile(AuditFile, TEXT("332A0A2DA9DC0BA65BF8277F12687BA255B4F8D1")))
		Errors.Add(TEXT("Generation audit content differs from signed M1 output; review M1 before importing changed data"));
	FString AuditText, SourceHash;
	TSharedPtr<FJsonObject> Audit, Phases;
	if (!FFileHelper::LoadFileToString(AuditText, *AuditFile) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(AuditText), Audit))
		Errors.Add(TEXT("Missing/invalid canonical generation audit JSON"));
	else
	{
		const TSharedPtr<FJsonObject>* P;
		FString Generator;
		if (!Audit->TryGetStringField(TEXT("generator"), Generator) || Generator != TEXT("Scripts/MHRise/build_kinsect_piercer_full_curves.py")
			|| !Audit->TryGetObjectField(TEXT("phases"), P)) Errors.Add(TEXT("Unexpected generator or missing phase metadata"));
		else Phases = *P;
		if (Audit->TryGetObjectField(TEXT("generated_from"), P)) (*P)->TryGetStringField(TEXT("sha256"), SourceHash);
		if (SourceHash != TEXT("c7e41f008c48e0e2b66e59dd21efa983f514667018dc7c7664576779027eec82"))
			Errors.Add(TEXT("Source-capture SHA256 differs from signed M1 recording"));
		FString Basis, Pitch;
		if (!Audit->TryGetObjectField(TEXT("coordinate_contract"), P)
			|| !(*P)->TryGetStringField(TEXT("runtime_transform"), Basis)
			|| Basis != TEXT("WorldOffset(t) = D*Curve.X(t) + (WorldUp x D)*Curve.Y(t) + WorldUp*Curve.Z(t), D = frozen horizontal aim")
			|| !(*P)->TryGetStringField(TEXT("pitch_policy"), Pitch)
			|| Pitch != TEXT("camera pitch never enters the basis, grouping, scaling or curve rotation"))
			Errors.Add(TEXT("Canonical audit does not declare the frozen-yaw/world-up coordinate contract"));
	}
	TArray<FImport> Imports;
	const TCHAR* PhaseNames[] = { TEXT("200"), TEXT("201"), TEXT("203") };
	const TCHAR* Suffixes[] = { TEXT("Start"), TEXT("Dive"), TEXT("Landing") };
	for (int32 N = 0; N < 3 && Phases.IsValid(); ++N)
	{
		FImport I; I.Phase = PhaseNames[N]; I.Suffix = Suffixes[N];
		const FString AssetName = TEXT("CV_IG_KinsectPiercer_") + I.Suffix;
		I.ObjectPath = TEXT("/Game/Weapons/InsectGlaive/Data/Curves/") + AssetName + TEXT(".") + AssetName;
		I.CsvPath = SourceDir / (TEXT("强化操虫穿刺_") + I.Phase + TEXT(".csv"));
		if (!MatchesSignedFile(I.CsvPath, FrozenCsvSHA1[N]))
		{ Errors.Add(I.Phase + TEXT(": CSV content differs from signed M1 output; middle-key edits are not accepted")); continue; }
		FString CsvText, Error;
		if (!FFileHelper::LoadFileToString(CsvText, *I.CsvPath) || !ReadSamples(CsvText, I.Phase, I.Samples, Error))
		{ Errors.Add(I.CsvPath + TEXT(": ") + Error); continue; }
		const TSharedPtr<FJsonObject>* Phase;
		const TArray<TSharedPtr<FJsonValue>>* Domain;
		double KeyCount = 0, Last = 0, Begin = 0, End = 0;
		if (!Phases->TryGetObjectField(I.Phase, Phase) || !(*Phase)->TryGetArrayField(TEXT("canonical_domain_s"), Domain)
			|| Domain->Num() != 2 || !(*Domain)[0]->TryGetNumber(Begin) || !(*Domain)[1]->TryGetNumber(End)
			|| !FMath::IsFinite(Begin) || !FMath::IsFinite(End) || Begin != 0 || End <= 0
			|| !(*Phase)->TryGetNumberField(TEXT("key_count"), KeyCount) || KeyCount != I.Samples.Num()
			|| !(*Phase)->TryGetNumberField(TEXT("key_last_s"), Last) || !FMath::IsNearlyEqual(Last, double(I.Samples.Last().Time), double(TimeTolerance)))
		{ Errors.Add(I.Phase + TEXT(": CSV and audit domain/key metadata disagree")); continue; }
		I.CanonicalEnd = End;
		I.Covered = N == 1 ? I.Samples.Last().Time : I.CanonicalEnd;
		double TailAnchor = 0;
		if (N == 1)
		{
			const TSharedPtr<FJsonObject>* Tail;
			double Anchor;
			if (!(*Phase)->TryGetObjectField(TEXT("tail"), Tail) || !(*Tail)->TryGetNumberField(TEXT("time_s"), Anchor)
				|| !FMath::IsFinite(Anchor) || Anchor < 0 || Anchor > I.Covered
				|| !JsonVector(**Tail, TEXT("velocity_cm_s"), I.Velocity) || !JsonVector(**Tail, TEXT("acceleration_cm_s2"), I.Acceleration))
			{ Errors.Add(TEXT("201: missing/invalid fully recorded terminal model")); continue; }
			bool FullyCovered = false;
			if (!(*Tail)->TryGetBoolField(TEXT("fit_window_fully_covered"), FullyCovered) || !FullyCovered)
			{ Errors.Add(TEXT("201: terminal model fit was not fully covered by recording")); continue; }
			I.AnchorTime = Anchor;
			TailAnchor = Anchor;
			// Same constant-acceleration continuation model, evaluated at the selected splice time.
			I.Velocity += I.Acceleration * (double(I.Covered) - Anchor);
		}
		if (!FrozenMetadata(N, End, I.Samples.Num(), Last, TailAnchor))
		{ Errors.Add(I.Phase + TEXT(": metadata differs from signed M1 recording; rebuild/review M1 explicitly before importing a new variant")); continue; }
		I.Desired = NewObject<UCurveVector>();
		FillCurve(*I.Desired, I.Samples);
		if (!Profile(I, I.Desired).IsValid()) { Errors.Add(I.Phase + TEXT(": runtime profile validation failed")); continue; }
		UObject* Existing = LoadObject<UObject>(nullptr, *I.ObjectPath, nullptr, LOAD_NoWarn);
		I.Existing = Cast<UCurveVector>(Existing);
		if (Existing && !I.Existing) Errors.Add(I.ObjectPath + TEXT(": existing object has wrong type; refusing overwrite"));
		if (!Existing && FPackageName::DoesPackageExist(FPackageName::ObjectPathToPackageName(I.ObjectPath)))
			Errors.Add(I.ObjectPath + TEXT(": package exists but object cannot be loaded; refusing overwrite"));
		I.Matches = I.Existing && Matches(*I.Existing, I.Samples);
		Imports.Add(MoveTemp(I));
	}
	UInsectGlaiveCombatConfig* Combat = LoadObject<UInsectGlaiveCombatConfig>(nullptr, CombatPath);
	if (!Combat) Errors.Add(TEXT("Missing DA_IG_Combat"));
	if (Imports.Num() != 3) Errors.Add(TEXT("All three phases must validate before any asset is changed"));
	if (Errors.IsEmpty())
	{
		for (FImport& I : Imports)
		{
			if (Apply && !I.Matches)
			{
				if (!I.Existing)
				{
					const FString PackageName = FPackageName::ObjectPathToPackageName(I.ObjectPath);
					const FString Name = FPackageName::ObjectPathToObjectName(I.ObjectPath);
					I.Existing = NewObject<UCurveVector>(CreatePackage(*PackageName), *Name, RF_Public | RF_Standalone);
					FAssetRegistryModule::AssetCreated(I.Existing);
				}
				I.Existing->Modify(); FillCurve(*I.Existing, I.Samples);
				if (!Save(*I.Existing)) Errors.Add(TEXT("Save failed: ") + I.ObjectPath);
				else Changed.Add(I.ObjectPath);
				I.Matches = Matches(*I.Existing, I.Samples);
			}
			if (!I.Matches) Errors.Add(TEXT("Curve missing or differs from canonical CSV: ") + I.ObjectPath);
		}
		if (Errors.IsEmpty())
		{
			FRecordedLocalTrajectoryProfile* Dest[] = { &Combat->KinsectPiercerStartProfile, &Combat->KinsectPiercerDiveProfile, &Combat->KinsectPiercerLandingProfile };
			bool Dirty = false;
			for (int32 N = 0; N < 3; ++N)
			{
				const FRecordedLocalTrajectoryProfile Expected = Profile(Imports[N], Imports[N].Existing);
				if (!MatchesProfile(*Dest[N], Expected))
				{
					if (Apply) { if (!Dirty) Combat->Modify(); *Dest[N] = Expected; Dirty = true; }
					else Errors.Add(Imports[N].Phase + TEXT(": Combat profile not wired to canonical curve/duration/tail"));
				}
			}
			if (Dirty) { if (!Save(*Combat)) Errors.Add(TEXT("Save failed: DA_IG_Combat")); else Changed.Add(CombatPath); }
		}
	}
	TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
	Report->SetStringField(TEXT("mode"), Apply ? TEXT("apply") : TEXT("audit"));
	Report->SetBoolField(TEXT("success"), Errors.IsEmpty());
	Report->SetStringField(TEXT("source_audit"), AuditFile); Report->SetStringField(TEXT("source_capture_sha256"), SourceHash);
	Report->SetStringField(TEXT("signed_input_integrity"), TEXT("Exact SHA1 byte fingerprints of M1 CSVs and generation audit; capture SHA256 pinned to signed recording. Changed content requires explicit M1 review."));
	Report->SetStringField(TEXT("basis"), TEXT("X=frozen horizontal forward; Y=right; Z=world up; time=seconds; offsets=cm"));
	TArray<TSharedPtr<FJsonValue>> PhaseReports, ErrorReports, ChangedReports;
	for (const FImport& I : Imports)
	{
		TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
		R->SetStringField(TEXT("phase"), I.Phase); R->SetStringField(TEXT("curve"), I.ObjectPath); R->SetNumberField(TEXT("keys_per_axis"), I.Samples.Num());
		R->SetNumberField(TEXT("canonical_domain_end_s"), I.CanonicalEnd); R->SetNumberField(TEXT("covered_seconds"), I.Covered);
		R->SetNumberField(TEXT("last_key_s"), I.Samples.Last().Time); R->SetBoolField(TEXT("matches_csv"), I.Matches);
		R->SetNumberField(TEXT("tail_source_anchor_s"), I.AnchorTime); R->SetArrayField(TEXT("terminal_velocity_cm_s"), Vec(I.Velocity));
		R->SetArrayField(TEXT("terminal_acceleration_cm_s2"), Vec(I.Acceleration));
		R->SetArrayField(TEXT("end_offset_cm"), Vec(I.Samples.Last().Offset));
		R->SetStringField(TEXT("tail_policy"), I.Phase == TEXT("201") ? TEXT("IntegrateTerminalModel") : TEXT("HoldLastKey"));
		PhaseReports.Add(MakeShared<FJsonValueObject>(R));
	}
	for (const FString& E : Errors) { ErrorReports.Add(MakeShared<FJsonValueString>(E)); UE_LOG(LogTemp, Error, TEXT("[PiercerCurves] %s"), *E); }
	for (const FString& C : Changed) ChangedReports.Add(MakeShared<FJsonValueString>(C));
	Report->SetArrayField(TEXT("phases"), PhaseReports); Report->SetArrayField(TEXT("errors"), ErrorReports); Report->SetArrayField(TEXT("changed_assets"), ChangedReports);
	FString Json; FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json));
	const FString ReportPath = FPaths::ProjectSavedDir() / FString::Printf(TEXT("_kinsect_piercer_curve_setup_%s.json"), Apply ? TEXT("apply") : TEXT("audit"));
	if (!FFileHelper::SaveStringToFile(Json, *ReportPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) return 1;
	UE_LOG(LogTemp, Display, TEXT("[PiercerCurves] %s: %d changed assets; report %s"), Errors.IsEmpty() ? TEXT("PASS") : TEXT("FAIL"), Changed.Num(), *ReportPath);
	return Errors.IsEmpty() ? 0 : 1;
#else
	return 1;
#endif
}

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZPiercerCsvTest, "MHGZ.M5.KinsectPiercer.AssetImport.StrictCSV", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZPiercerCsvTest::RunTest(const FString& Parameters)
{
	using namespace UE::MHGZ::PiercerCurves;
	float V;
	TestTrue(TEXT("Signed exponent"), Number(TEXT("-1.2e+2"), V) && V == -120);
	TestTrue(TEXT("Signed M1 start metadata"), FrozenMetadata(0, 0.391327, 48, 0.391667));
	TestTrue(TEXT("Signed M1 dive metadata"), FrozenMetadata(1, 0.366547, 44, 0.358333, 0.341667));
	TestTrue(TEXT("Signed M1 landing metadata"), FrozenMetadata(2, 1.566650, 189, 1.566667));
	TestFalse(TEXT("Silent retiming is refused"), FrozenMetadata(0, 0.3, 48, 0.391667));
	TestFalse(TEXT("Silent tail-anchor change is refused"), FrozenMetadata(1, 0.366547, 44, 0.358333, 0.3));
	TestFalse(TEXT("Silent coverage endpoint change is refused"), FrozenMetadata(1, 0.366547, 44, 0.358000, 0.341667));
	for (const TCHAR* S : { TEXT(""), TEXT("nan"), TEXT("inf"), TEXT("1junk"), TEXT("1e"), TEXT("1e999") }) TestFalse(S, Number(S, V));
	const FString Header = TEXT("phase,sample_index,time_s,forward_cm,right_cm,up_cm\n");
	TArray<FSample> Samples; FString Error;
	TestTrue(TEXT("Nonmonotonic offsets are allowed"), ReadSamples(Header + TEXT("201,0,0,0,0,0\n201,1,0.1,-1,2,-3\n"), TEXT("201"), Samples, Error));
	TestFalse(TEXT("Time order rejected"), ReadSamples(Header + TEXT("201,0,0,0,0,0\n201,1,0,-1,2,-3\n"), TEXT("201"), Samples, Error));
	TestFalse(TEXT("Origin rejected"), ReadSamples(Header + TEXT("201,0,0,1,0,0\n201,1,0.1,2,0,0\n"), TEXT("201"), Samples, Error));
	TestFalse(TEXT("Other phase rejected"), ReadSamples(Header + TEXT("201,0,0,0,0,0\n203,1,0.1,2,0,0\n"), TEXT("201"), Samples, Error));
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const TCHAR* Phases[] = { TEXT("200"), TEXT("201"), TEXT("203") };
		const FString Path = FPaths::ProjectSavedDir() / TEXT("_mhr_curves")
			/ (FString(TEXT("强化操虫穿刺_")) + Phases[Index] + TEXT(".csv"));
		TArray<uint8> Bytes;
		if (!TestTrue(TEXT("Signed M1 fixture loads"), FFileHelper::LoadFileToArray(Bytes, *Path)) || Bytes.IsEmpty()) { return false; }
		TestTrue(TEXT("Signed M1 full sample content passes fingerprint"), MatchesSignedBytes(Bytes, FrozenCsvSHA1[Index]));
		Bytes[Bytes.Num() / 2] ^= 1;
		TestFalse(TEXT("Interior content change is rejected independently of endpoint metadata"), MatchesSignedBytes(Bytes, FrozenCsvSHA1[Index]));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZPiercerVectorImportTest, "MHGZ.M5.KinsectPiercer.AssetImport.LinearVectorAndTail", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZPiercerVectorImportTest::RunTest(const FString& Parameters)
{
	using namespace UE::MHGZ::PiercerCurves;
	FImport I;
	I.Samples = { {0.0f, FVector::ZeroVector}, {0.1f, FVector(10, -2, -20)}, {0.2f, FVector(30, -1, -50)} };
	I.Covered = 0.2f;
	I.Acceleration = FVector(20, 0, -100);
	I.Velocity = FVector(200, 10, -300) + I.Acceleration * 0.02;
	UCurveVector* Curve = NewObject<UCurveVector>();
	FillCurve(*Curve, I.Samples);
	const FRecordedLocalTrajectoryProfile P = Profile(I, Curve);
	TestTrue(TEXT("Imported axes match canonical keys"), Matches(*Curve, I.Samples));
	TestTrue(TEXT("Linear vector profile is valid"), P.IsValid());
	TestTrue(TEXT("No axis swap or normalization"), Curve->GetVectorValue(0.05f).Equals(FVector(5, -1, -10), 0.0001));
	const FVector Expected = I.Samples.Last().Offset + I.Velocity * 0.1 + I.Acceleration * 0.005;
	TestTrue(TEXT("Tail position anchored to selected endpoint"), P.GetOffset(0.3f, ERecordedTrajectoryTailPolicy::IntegrateTerminalModel).Equals(Expected, 0.001));
	TestTrue(TEXT("Finite phases hold last position"), P.GetOffset(0.3f, ERecordedTrajectoryTailPolicy::HoldLastKey).Equals(I.Samples.Last().Offset, 0.001));
	return true;
}
#endif
