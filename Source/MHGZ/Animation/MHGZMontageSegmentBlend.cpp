// Copyright MHGZ Project. All Rights Reserved.
#include "Animation/MHGZMontageSegmentBlend.h"

#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"

void FMHGZMontageSegmentBlend::Capture(const TArray<FAnimMontageInstance*>& Instances,
	TArray<FMHGZMontageSegmentBlendSample>& OutSamples)
{
	OutSamples.Reset();
	for (const FAnimMontageInstance* Instance : Instances)
	{
		if (!Instance || !Instance->IsActive() || !Instance->IsPlaying()) { continue; }
		UAnimMontage* Montage = Instance->Montage;
		const UMHGZMontageSegmentBlendSettings* Settings =
			Montage->FindMetaDataByClass<UMHGZMontageSegmentBlendSettings>();
		if (!Settings || !Settings->bEnabled || !FMath::IsFinite(Settings->Duration)
			|| Settings->Duration <= 0.0f) { continue; }
		for (const FSlotAnimationTrack& Slot : Montage->SlotAnimTracks)
		{
			// Use UE's exact segment selection, including inclusive boundary precedence.
			const int32 Index = Slot.AnimTrack.GetSegmentIndexAtTime(Instance->GetPosition());
			if (!Slot.AnimTrack.AnimSegments.IsValidIndex(Index)) { continue; }
			const FAnimSegment& Segment = Slot.AnimTrack.AnimSegments[Index];
			if (!Segment.IsValid() || !IsValid(Segment.GetAnimReference())) { continue; }
			FMHGZMontageSegmentBlendSample& Sample = OutSamples.AddDefaulted_GetRef();
			Sample.Montage = Montage;
			Sample.Sequence = Segment.GetAnimReference();
			Sample.InstanceID = Instance->GetInstanceID();
			Sample.SegmentIndex = Index;
			Sample.SlotName = Slot.SlotName;
			Sample.GroupName = Montage->GetGroupName();
			Sample.Duration = Settings->Duration;
			Sample.Weight = Instance->GetWeight();
		}
	}
}

void FMHGZMontageSegmentBlend::BuildRequests(
	const TConstArrayView<FMHGZMontageSegmentBlendSample> Before,
	const TConstArrayView<FMHGZMontageSegmentBlendSample> After, TMap<FName, float>& OutRequests)
{
	OutRequests.Reset();
	for (const FMHGZMontageSegmentBlendSample& Current : After)
	{
		if (!Current.Montage.IsValid() || !Current.Sequence.IsValid()
			|| Current.GroupName.IsNone() || Current.Weight <= ZERO_ANIMWEIGHT_THRESH
			|| !FMath::IsFinite(Current.Duration) || Current.Duration <= 0.0f) { continue; }
		const FMHGZMontageSegmentBlendSample* Previous = Before.FindByPredicate(
			[&Current](const FMHGZMontageSegmentBlendSample& Candidate)
			{
				return Candidate.InstanceID == Current.InstanceID && Candidate.Montage == Current.Montage
					&& Candidate.SlotName == Current.SlotName;
			});
		if (!Previous || !Previous->Sequence.IsValid()
			|| Previous->SegmentIndex == Current.SegmentIndex
			|| Previous->Sequence == Current.Sequence) { continue; }
		// One request per group/update, including a multi-slot montage. No frame countdown.
		float& Duration = OutRequests.FindOrAdd(Current.GroupName);
		Duration = FMath::Max(Duration, Current.Duration);
	}
}
