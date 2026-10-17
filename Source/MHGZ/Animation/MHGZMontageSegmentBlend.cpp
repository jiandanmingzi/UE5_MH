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
			Sample.DesiredWeight = Instance->GetDesiredWeight();
		}
	}
}

void FMHGZWeaponGripConstraint::Clear()
{
	Alpha = 0.f;
	OwnerMontage.Reset(); TargetSequence.Reset(); InstanceID = INDEX_NONE;
	Elapsed = HoldDuration = ReleaseDuration = 0.f;
}

void FMHGZWeaponGripConstraint::Update(
	const TConstArrayView<FMHGZMontageSegmentBlendSample> Before,
	const TConstArrayView<FMHGZMontageSegmentBlendSample> After, const float DeltaSeconds)
{
	if (!FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.f) { Clear(); return; }
	if (InstanceID != INDEX_NONE)
	{
		const bool bStillOwned = After.ContainsByPredicate([this](const auto& Sample)
		{
			const auto* Montage = Sample.Montage.Get();
			const auto* Settings = Montage ? Montage->template FindMetaDataByClass<UMHGZMontageSegmentBlendSettings>() : nullptr;
			return Sample.Montage == OwnerMontage && Sample.InstanceID == InstanceID
				&& Sample.Sequence == TargetSequence && Sample.SlotName == TEXT("DefaultSlot")
				&& Settings && Settings->bEnabled && Settings->bEnableWeaponGripConstraint
				&& Sample.Weight > ZERO_ANIMWEIGHT_THRESH && Sample.DesiredWeight > ZERO_ANIMWEIGHT_THRESH;
		});
		if (!bStillOwned) { Clear(); }
		else
		{
			Elapsed += DeltaSeconds;
			const float T = FMath::Clamp((Elapsed - HoldDuration) / ReleaseDuration, 0.f, 1.f);
			Alpha = 1.f - T * T * (3.f - 2.f * T);
			if (T >= 1.f) { Clear(); }
		}
	}
	for (const auto& Current : After)
	{
		const UAnimMontage* Montage = Current.Montage.Get();
		const auto* Settings = Montage ? Montage->FindMetaDataByClass<UMHGZMontageSegmentBlendSettings>() : nullptr;
		if (!Settings || !Settings->bEnabled || !Settings->bEnableWeaponGripConstraint
			|| Current.SlotName != TEXT("DefaultSlot") || Current.Weight <= ZERO_ANIMWEIGHT_THRESH
			|| Current.DesiredWeight <= ZERO_ANIMWEIGHT_THRESH
			|| !Settings->WeaponGripFromSequence || !Settings->WeaponGripToSequence
			|| Settings->WeaponGripFromSequence == Settings->WeaponGripToSequence
			|| !FMath::IsFinite(Settings->Duration) || Settings->Duration <= 0.f
			|| !FMath::IsFinite(Settings->WeaponGripReleaseDuration) || Settings->WeaponGripReleaseDuration <= 0.f
			|| Current.Sequence.Get() != Settings->WeaponGripToSequence.Get()) { continue; }
		const auto* Previous = Before.FindByPredicate([&Current](const auto& Candidate)
		{
			return Candidate.Montage == Current.Montage && Candidate.InstanceID == Current.InstanceID
				&& Candidate.SlotName == Current.SlotName;
		});
		if (!Previous || Previous->Sequence.Get() != Settings->WeaponGripFromSequence.Get()
			|| Previous->SegmentIndex == Current.SegmentIndex || Previous->Weight <= ZERO_ANIMWEIGHT_THRESH) { continue; }
		OwnerMontage = Current.Montage; TargetSequence = Current.Sequence; InstanceID = Current.InstanceID;
		Elapsed = 0.f; HoldDuration = Settings->Duration; ReleaseDuration = Settings->WeaponGripReleaseDuration;
		Alpha = 1.f; ++Serial;
		break;
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
