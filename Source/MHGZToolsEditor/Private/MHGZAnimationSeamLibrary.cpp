// Copyright MHGZ Project. All Rights Reserved.
#include "MHGZAnimationSeamLibrary.h"

#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/Skeleton.h"
#include "AnimationBlueprintLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

namespace
{
constexpr float FrameDuration = 1.0f / 60.0f;
constexpr float Tolerance = 0.00001f;
struct FSeamNotifyTime { int32 Index; float Start; float End; int32 Slot; };
struct FSeamSectionTime { int32 Index; float Time; int32 Slot; };

bool Snapshot(UAnimMontage* Montage, TArray<FSeamSectionTime>& Sections, TArray<FSeamNotifyTime>& Notifies)
{
	if (!Montage || Montage->SlotAnimTracks.Num() != 1) return false;
	for (int32 i = 0; i < Montage->CompositeSections.Num(); ++i)
	{
		const FCompositeSection& S = Montage->CompositeSections[i];
		Sections.Add({i, S.GetTime(), S.GetSlotIndex()});
	}
	for (int32 i = 0; i < Montage->Notifies.Num(); ++i)
	{
		const FAnimNotifyEvent& N = Montage->Notifies[i];
		Notifies.Add({i, N.GetTime(), N.GetTime() + N.GetDuration(), N.GetSlotIndex()});
	}
	return true;
}

void Relink(UAnimMontage& Montage, const TArray<FSeamSectionTime>& Sections,
	const TArray<FSeamNotifyTime>& Notifies, TFunctionRef<float(float)> MapTime)
{
	// PostEditChange refreshes metadata but does not update the composite length.
	Montage.SetCompositeLength(Montage.CalculateSequenceLength());
	for (const FSeamSectionTime& S : Sections)
		Montage.CompositeSections[S.Index].Link(&Montage, MapTime(S.Time), S.Slot);
	for (const FSeamNotifyTime& N : Notifies)
	{
		FAnimNotifyEvent& Event = Montage.Notifies[N.Index];
		const float Start = MapTime(N.Start);
		Event.Link(&Montage, Start, N.Slot);
		if (Event.NotifyStateClass)
		{
			const float End = MapTime(N.End);
			Event.EndLink.Link(&Montage, End, N.Slot);
			Event.SetDuration(FMath::Max(0.0f, End - Start));
		}
	}
	Montage.UpdateLinkableElements();
	Montage.RefreshCacheData();
	Montage.PostEditChange();
	Montage.MarkPackageDirty();
}
}

UAnimSequence* UMHGZAnimationSeamLibrary::CreateOneFrameBridge(UAnimSequence* Before, float BeforeTime,
	UAnimSequence* After, float AfterTime, float BeforeRate, float AfterRate, const FString& OutputPackage)
{
	if (!Before || !After || !Before->GetSkeleton() || Before->GetSkeleton() != After->GetSkeleton()
		|| BeforeRate <= 0 || AfterRate <= 0 || !OutputPackage.StartsWith(TEXT("/Game/"))
		|| !FPackageName::IsValidLongPackageName(OutputPackage)
		|| FPackageName::DoesPackageExist(OutputPackage)
		|| BeforeTime < FrameDuration * BeforeRate || BeforeTime > Before->GetPlayLength() + Tolerance
		|| AfterTime < 0 || AfterTime + FrameDuration * AfterRate > After->GetPlayLength() + Tolerance)
	{
		UE_LOG(LogTemp, Error, TEXT("[AnimationSeam] Invalid sources/times or existing output: %s"), *OutputPackage);
		return nullptr;
	}
	const IAnimationDataModel* Model = Before->GetDataModel();
	if (!Model || !After->GetDataModel()) return nullptr;
	TArray<FName> Names;
	Model->GetBoneTrackNames(Names);
	const FName RootName = Before->GetSkeleton()->GetReferenceSkeleton().GetBoneName(0);
	const int32 RootIndex = Names.Find(RootName);
	if (Names.IsEmpty() || RootIndex == INDEX_NONE) return nullptr;
	for (const FName Name : Names)
		if (After->GetSkeleton()->GetReferenceSkeleton().FindBoneIndex(Name) == INDEX_NONE) return nullptr;
	TArray<FTransform> Left, Right;
	// This UE 5.6 API has a real AnimPose implementation and was verified against
	// the project's imported root tracks; the deprecated raw-track stubs are not used.
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	UAnimationBlueprintLibrary::GetBonePosesForTime(Before, Names, BeforeTime, false, Left);
	UAnimationBlueprintLibrary::GetBonePosesForTime(After, Names, AfterTime, false, Right);
	if (Left.Num() != Names.Num() || Right.Num() != Names.Num()) return nullptr;
	FTransform PrevRoot, NextRoot;
	UAnimationBlueprintLibrary::GetBonePoseForTime(Before, RootName,
		BeforeTime - FrameDuration * BeforeRate, false, PrevRoot);
	UAnimationBlueprintLibrary::GetBonePoseForTime(After, RootName,
		AfterTime + FrameDuration * AfterRate, false, NextRoot);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	// Different authored root bases need an explicit transform recipe, not a silent lerp.
	if (Left[RootIndex].GetRotation().AngularDistance(Right[RootIndex].GetRotation()) > 0.001f) return nullptr;
	const FVector RootDelta = 0.5 * (Left[RootIndex].GetTranslation() - PrevRoot.GetTranslation()
		+ NextRoot.GetTranslation() - Right[RootIndex].GetTranslation());
	UPackage* Package = CreatePackage(*OutputPackage);
	UAnimSequence* Output = NewObject<UAnimSequence>(Package,
		FName(*FPackageName::GetLongPackageAssetName(OutputPackage)), RF_Public | RF_Standalone);
	Output->SetSkeleton(Before->GetSkeleton());
	Output->RateScale = 1.0f;
	Output->bLoop = false;
	Output->bEnableRootMotion = true;
	Output->bForceRootLock = false;
	Output->RootMotionRootLock = Before->RootMotionRootLock;
	Output->bUseNormalizedRootMotionScale = Before->bUseNormalizedRootMotionScale;
	IAnimationDataController& Controller = Output->GetController();
	Controller.InitializeModel();
	Controller.OpenBracket(FText::FromString(TEXT("Bake one-frame midpoint pose bridge")), false);
	Controller.SetFrameRate(FFrameRate(60, 1), false);
	Controller.SetNumberOfFrames(FFrameNumber(1), false);
	for (int32 i = 0; i < Names.Num(); ++i)
	{
		FTransform Mid;
		Mid.Blend(Left[i], Right[i], 0.5f);
		FVector Start = Mid.GetTranslation();
		FVector End = Start;
		if (i == RootIndex) End += RootDelta;
		const TArray<FVector3f> Positions = {FVector3f(Start), FVector3f(End)};
		const TArray<FQuat4f> Rotations = {FQuat4f(Mid.GetRotation()), FQuat4f(Mid.GetRotation())};
		const TArray<FVector3f> Scales = {FVector3f(Mid.GetScale3D()), FVector3f(Mid.GetScale3D())};
		if (!Controller.AddBoneCurve(Names[i], false)
			|| !Controller.SetBoneTrackKeys(Names[i], Positions, Rotations, Scales, false))
		{
			Controller.CloseBracket(false);
			UE_LOG(LogTemp, Error, TEXT("[AnimationSeam] Failed to author %s"), *Names[i].ToString());
			return nullptr;
		}
	}
	Controller.NotifyPopulated();
	Controller.CloseBracket(false);
	Output->Notifies.Reset();
	Output->RefreshCacheData();
	Output->PostEditChange();
	Output->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Output);
	UE_LOG(LogTemp, Display, TEXT("[AnimationSeam] Created %s Duration=%.8f RootDelta=%s"),
		*Output->GetPathName(), Output->GetPlayLength(), *RootDelta.ToString());
	return Output;
}

bool UMHGZAnimationSeamLibrary::InsertBridge(UAnimMontage* Montage, int32 BeforeSegmentIndex,
	UAnimSequence* Bridge, bool bReplaceNextSegment)
{
	TArray<FSeamSectionTime> Sections;
	TArray<FSeamNotifyTime> Notifies;
	if (!Bridge || !Snapshot(Montage, Sections, Notifies) || Bridge->GetSkeleton() != Montage->GetSkeleton()
		|| !FMath::IsNearlyEqual(Bridge->GetPlayLength(), FrameDuration, Tolerance)
		|| !FMath::IsNearlyEqual(Bridge->RateScale, 1.0f)) return false;
	TArray<FAnimSegment>& Segments = Montage->SlotAnimTracks[0].AnimTrack.AnimSegments;
	if (!Segments.IsValidIndex(BeforeSegmentIndex) || !Segments.IsValidIndex(BeforeSegmentIndex + 1)) return false;
	const float Cut = Segments[BeforeSegmentIndex].GetEndPos();
	const float RemovedLength = bReplaceNextSegment ? Segments[BeforeSegmentIndex + 1].GetLength() : 0.0f;
	if (!FMath::IsNearlyEqual(Cut, Segments[BeforeSegmentIndex + 1].StartPos, Tolerance)) return false;
	// No authored collision/accept boundary may be silently compressed into a bridge.
	if (bReplaceNextSegment)
		for (const FSeamNotifyTime& N : Notifies)
			if ((N.Start > Cut + Tolerance && N.Start < Cut + RemovedLength - Tolerance)
				|| (N.End > Cut + Tolerance && N.End < Cut + RemovedLength - Tolerance)) return false;
	Montage->Modify();
	FAnimSegment New;
	New.SetAnimReference(Bridge, true);
	New.StartPos = Cut;
	New.AnimStartTime = 0;
	New.AnimEndTime = FrameDuration;
	New.AnimPlayRate = 1;
	New.LoopingCount = 1;
	const float Shift = FrameDuration - RemovedLength;
	const int32 Following = BeforeSegmentIndex + (bReplaceNextSegment ? 2 : 1);
	for (int32 i = Following; i < Segments.Num(); ++i) Segments[i].StartPos += Shift;
	if (bReplaceNextSegment) Segments[BeforeSegmentIndex + 1] = New;
	else Segments.Insert(New, BeforeSegmentIndex + 1);
	Relink(*Montage, Sections, Notifies, [=](float T)
	{
		if (T < Cut - Tolerance) return T;
		if (bReplaceNextSegment && T < Cut + RemovedLength - Tolerance)
			return Cut + FMath::Clamp((T - Cut) / RemovedLength, 0.0f, 1.0f) * FrameDuration;
		return T + Shift;
	});
	return true;
}

bool UMHGZAnimationSeamLibrary::RefreshMontageLength(UAnimMontage* Montage)
{
	if (!Montage || Montage->SlotAnimTracks.Num() != 1) return false;
	const float Length = Montage->CalculateSequenceLength();
	if (!FMath::IsFinite(Length) || Length <= 0) return false;
	Montage->Modify();
	Montage->SetCompositeLength(Length);
	Montage->UpdateLinkableElements();
	Montage->RefreshCacheData();
	Montage->PostEditChange();
	Montage->MarkPackageDirty();
	return FMath::IsNearlyEqual(Montage->GetPlayLength(), Length, Tolerance);
}

bool UMHGZAnimationSeamLibrary::TrimSegmentStart(UAnimMontage* Montage, int32 SegmentIndex, float NewSourceStartTime)
{
	TArray<FSeamSectionTime> Sections;
	TArray<FSeamNotifyTime> Notifies;
	if (!Snapshot(Montage, Sections, Notifies)) return false;
	TArray<FAnimSegment>& Segments = Montage->SlotAnimTracks[0].AnimTrack.AnimSegments;
	if (!Segments.IsValidIndex(SegmentIndex)) return false;
	FAnimSegment& Segment = Segments[SegmentIndex];
	if (Segment.GetValidPlayRate() <= 0 || Segment.LoopingCount != 1
		|| NewSourceStartTime <= Segment.AnimStartTime || NewSourceStartTime >= Segment.AnimEndTime) return false;
	const float Cut = Segment.StartPos;
	const float Trim = (NewSourceStartTime - Segment.AnimStartTime) / Segment.GetValidPlayRate();
	for (const FSeamNotifyTime& N : Notifies)
		if ((N.Start > Cut + Tolerance && N.Start < Cut + Trim - Tolerance)
			|| (N.End > Cut + Tolerance && N.End < Cut + Trim - Tolerance)) return false;
	Montage->Modify();
	Segment.AnimStartTime = NewSourceStartTime;
	for (int32 i = SegmentIndex + 1; i < Segments.Num(); ++i) Segments[i].StartPos -= Trim;
	Relink(*Montage, Sections, Notifies, [=](float T)
	{
		if (T < Cut - Tolerance) return T;
		return FMath::Max(Cut, T - Trim);
	});
	return true;
}
