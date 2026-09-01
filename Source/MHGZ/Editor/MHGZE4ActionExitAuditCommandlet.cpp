// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZE4ActionExitAuditCommandlet.h"

#include "Animation/AnimCompositeBase.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "AnimationBlueprintLibrary.h"
#include "ActionSystem/AnimNotify_MotionMatchingHandoff.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace UE::MHGZ::E4ActionExitAudit
{
#if WITH_EDITOR
struct FAuditTarget
{
	const TCHAR* Label;
	const TCHAR* ObjectPath;
	const TCHAR* IntendedRoute;
};

static const FAuditTarget Targets[] =
{
	{TEXT("????"), TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_ShouDao.AM_IG_ShouDao"), TEXT("GA_Sheathe / Walk / SheatheMoveExit")},
	{TEXT("??????"), TEXT("/Game/Characters/Demo/Anims/Montage/AM_Shth_Dodge.AM_Shth_Dodge"), TEXT("GA_Dodge / MoveExit / DodgeMoveExit")},
	{TEXT("??????"), TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_Dodge_Forward.AM_IG_Dodge_Forward"), TEXT("GA_Dodge / MoveExit / DodgeMoveExit")},
	{TEXT("???????"), TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_TuCi.AM_IG_TuCi"), TEXT("GA_IG_TuCi / AttackExit; telemetry-gated")}
};

FString JoinNames(const TArray<FName>& Names)
{
	TArray<FString> Strings;
	for (const FName Name : Names)
	{
		Strings.Add(Name.ToString());
	}
	return Strings.IsEmpty() ? TEXT("(none)") : FString::Join(Strings, TEXT(", "));
}

FString DescribeNotify(const FAnimNotifyEvent& Event)
{
	if (Event.NotifyStateClass)
	{
		return Event.NotifyStateClass->GetName();
	}
	if (const UAnimNotify_MotionMatchingHandoff* Handoff =
		Cast<UAnimNotify_MotionMatchingHandoff>(Event.Notify))
	{
		const UEnum* HandoffEnum = StaticEnum<EMHGZMotionMatchingHandoffType>();
		return FString::Printf(TEXT("%s (HandoffType=%s)"), *Handoff->GetClass()->GetName(),
			HandoffEnum ? *HandoffEnum->GetNameStringByValue(static_cast<int64>(Handoff->HandoffType))
				: TEXT("<enum missing>"));
	}
	if (Event.Notify)
	{
		return Event.Notify->GetClass()->GetName();
	}
	return Event.NotifyName.IsNone() ? TEXT("UnnamedNotify") : Event.NotifyName.ToString();
}

void AddSequenceAudit(TArray<FString>& Lines, const UAnimSequenceBase& Sequence)
{
	TArray<FName> CurveNames;
	UAnimationBlueprintLibrary::GetAnimationCurveNames(&Sequence, ERawCurveTrackTypes::RCT_Float, CurveNames);

	const float Length = Sequence.GetPlayLength();
	const FFrameRate SamplingFrameRate = Sequence.GetSamplingFrameRate();
	const float SamplingFps = SamplingFrameRate.AsDecimal();
	const FTransform RootAtStart = UAnimationBlueprintLibrary::ExtractRootTrackTransform(&Sequence, 0.f);
	const FTransform RootAtEnd = UAnimationBlueprintLibrary::ExtractRootTrackTransform(&Sequence, Length);
	const FVector RootDelta = RootAtEnd.GetTranslation() - RootAtStart.GetTranslation();
	Lines.Add(FString::Printf(TEXT("  - Sequence %s | Length=%.4fs | Sampling=%d keys @ %.3ffps | RateScale=%.3f | RootMotion=%s | RootDelta=(%.3f, %.3f, %.3f) | Curves=%s"),
		*Sequence.GetPathName(), Length, Sequence.GetNumberOfSampledKeys(), SamplingFps, Sequence.RateScale,
		Sequence.HasRootMotion() ? TEXT("true") : TEXT("false"),
		RootDelta.X, RootDelta.Y, RootDelta.Z, *JoinNames(CurveNames)));

	for (const FAnimNotifyEvent& Event : Sequence.Notifies)
	{
		Lines.Add(FString::Printf(TEXT("    - Sequence Notify t=%.4f duration=%.4f %s"),
			Event.GetTime(), Event.GetDuration(), *DescribeNotify(Event)));
	}
}

void AddMontageAudit(TArray<FString>& Lines, const FAuditTarget& Target, UAnimMontage& Montage)
{
	Lines.Add(FString::Printf(TEXT("## %s"), Target.IntendedRoute));
	Lines.Add(FString::Printf(TEXT("- Intended route: %s"), Target.IntendedRoute));
	Lines.Add(FString::Printf(TEXT("- Montage: %s | Length=%.4fs | Slots=%d | Sections=%d"),
		*Montage.GetPathName(), Montage.GetPlayLength(), Montage.SlotAnimTracks.Num(), Montage.CompositeSections.Num()));

	Lines.Add(TEXT("- Sections:"));
	for (int32 Index = 0; Index < Montage.CompositeSections.Num(); ++Index)
	{
		const FCompositeSection& Section = Montage.CompositeSections[Index];
		float StartTime = 0.f;
		float EndTime = 0.f;
		Montage.GetSectionStartAndEndTime(Index, StartTime, EndTime);
		Lines.Add(FString::Printf(TEXT("  - %s: [%.4f, %.4f], Next=%s"),
			*Section.SectionName.ToString(), StartTime, EndTime,
			*Section.NextSectionName.ToString()));
	}

	Lines.Add(TEXT("- Slot Segments and source Sequences:"));
	for (const FSlotAnimationTrack& SlotTrack : Montage.SlotAnimTracks)
	{
		Lines.Add(FString::Printf(TEXT("  - Slot %s"), *SlotTrack.SlotName.ToString()));
		for (const FAnimSegment& Segment : SlotTrack.AnimTrack.AnimSegments)
		{
			UAnimSequenceBase* Sequence = Segment.GetAnimReference();
			const float EffectiveRate = Segment.GetValidPlayRate();
			const float SamplingFps = Sequence ? Sequence->GetSamplingFrameRate().AsDecimal() : 0.f;
			const int32 SourceStartFrame = FMath::RoundToInt(Segment.AnimStartTime * SamplingFps);
			const int32 SourceEndFrame = FMath::RoundToInt(Segment.AnimEndTime * SamplingFps);
			Lines.Add(FString::Printf(TEXT("    - MontageRange=[%.4f, %.4f] SourceRange=[%.4f, %.4f] SourceFrames~=[%d, %d] SegmentRate=%.3f SequenceRateScale=%.3f EffectiveRate=%.3f Loops=%d Sequence=%s"),
				Segment.StartPos, Segment.GetEndPos(), Segment.AnimStartTime, Segment.AnimEndTime,
				SourceStartFrame, SourceEndFrame, Segment.AnimPlayRate, Sequence ? Sequence->RateScale : 0.f,
				EffectiveRate, Segment.LoopingCount, *GetPathNameSafe(Sequence)));
			if (Sequence)
			{
				AddSequenceAudit(Lines, *Sequence);
			}
		}
	}

	TArray<FName> MontageCurves;
	UAnimationBlueprintLibrary::GetAnimationCurveNames(&Montage, ERawCurveTrackTypes::RCT_Float, MontageCurves);
	Lines.Add(FString::Printf(TEXT("- Montage float curves: %s"), *JoinNames(MontageCurves)));
	Lines.Add(TEXT("- Montage Notifies:"));
	for (const FAnimNotifyEvent& Event : Montage.Notifies)
	{
		Lines.Add(FString::Printf(TEXT("  - t=%.4f duration=%.4f %s"),
			Event.GetTime(), Event.GetDuration(), *DescribeNotify(Event)));
	}
	if (Montage.Notifies.IsEmpty())
	{
		Lines.Add(TEXT("  - (none)"));
	}
	Lines.Add(TEXT("- Human decision required: identify the first frame after every gameplay Commit/Window/Damage operation, then the later frame where Base Move candidates can be admitted. Asset names alone are not evidence."));
	Lines.Add(TEXT(""));
}
#endif
}

UMHGZE4ActionExitAuditCommandlet::UMHGZE4ActionExitAuditCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UMHGZE4ActionExitAuditCommandlet::Main(const FString& Params)
{
	(void)Params;
#if WITH_EDITOR
	using namespace UE::MHGZ::E4ActionExitAudit;

	TArray<FString> Lines;
	Lines.Add(TEXT("# E4.2 Action Exit Read-Only Audit"));
	Lines.Add(TEXT(""));
	Lines.Add(TEXT("Generated by MHGZE4ActionExitAudit. It loads assets only; it never calls Modify or SavePackage."));
	Lines.Add(TEXT(""));

	bool bAllTargetsLoaded = true;
	for (const FAuditTarget& Target : Targets)
	{
		UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, Target.ObjectPath);
		if (!Montage)
		{
			bAllTargetsLoaded = false;
			Lines.Add(FString::Printf(TEXT("## %s"), Target.Label));
			Lines.Add(FString::Printf(TEXT("- ERROR: could not load %s"), Target.ObjectPath));
			Lines.Add(TEXT(""));
			continue;
		}
		AddMontageAudit(Lines, Target, *Montage);
	}

	const FString OutputDirectory = FPaths::ProjectSavedDir() / TEXT("ActionExitAudit");
	const FString OutputPath = OutputDirectory / TEXT("E4_2_ActionExitAudit.md");
	IFileManager::Get().MakeDirectory(*OutputDirectory, true);
	if (!FFileHelper::SaveStringToFile(FString::Join(Lines, TEXT("\n")), *OutputPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogTemp, Error, TEXT("[E4ActionExitAudit] Failed to write %s"), *OutputPath);
		return 1;
	}

	UE_LOG(LogTemp, Display, TEXT("[E4ActionExitAudit] Wrote %s (AllTargetsLoaded=%d)"),
		*OutputPath, bAllTargetsLoaded ? 1 : 0);
	return bAllTargetsLoaded ? 0 : 1;
#else
	UE_LOG(LogTemp, Error, TEXT("[E4ActionExitAudit] This commandlet requires an editor build."));
	return 1;
#endif
}

