// Copyright MHGZ Project. All Rights Reserved.
#include "MHGZAnimationSeamLibrary.h"
#include "ActionSystem/AnimNotify_CategorizedSound.h"
#include "Animation/AnimMontage.h"
#include "UObject/UnrealType.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

bool UMHGZAnimationSeamLibrary::EditSoundNotifyTimes(UAnimMontage* Montage, const FString& ChangesJson)
{
	TArray<TSharedPtr<FJsonValue>> Changes;
	if (!Montage || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ChangesJson), Changes)
		|| Changes.IsEmpty()) return false;
	TArray<FAnimNotifyEvent> Events = Montage->Notifies;
	TSet<FGuid> Seen, Removed;
	for (const auto& Value : Changes)
	{
		const auto Spec = Value->AsObject();
		FString GuidText;
		double ExpectedTime, Time;
		bool bRemove;
		FGuid Guid;
		if (!Spec || !Spec->TryGetStringField(TEXT("guid"), GuidText) || !FGuid::Parse(GuidText, Guid)
			|| !Guid.IsValid() || Seen.Contains(Guid) || !Spec->TryGetNumberField(TEXT("expected_time"), ExpectedTime)
			|| !FMath::IsFinite(ExpectedTime) || !Spec->TryGetBoolField(TEXT("remove"), bRemove)) return false;
		Seen.Add(Guid);
		FAnimNotifyEvent* Event = nullptr;
		for (auto& Candidate : Events)
		{
			if (Candidate.Guid != Guid) continue;
			if (Event) return false;
			Event = &Candidate;
		}
		if (!Event || Event->NotifyStateClass || !Cast<UAnimNotify_SwingSound>(Event->Notify)
			|| FMath::Abs(Event->GetTime() - ExpectedTime) > 1e-5
			|| Event->GetLinkMethod() != EAnimLinkMethod::Absolute) return false;
		if (bRemove) { Removed.Add(Guid); continue; }
		if (!Spec->TryGetNumberField(TEXT("time"), Time) || !FMath::IsFinite(Time)
			|| Time < 0 || Time >= Montage->GetPlayLength()) return false;
		Event->Link(Montage, Time, Event->GetSlotIndex());
		Event->TriggerTimeOffset = GetTriggerTimeOffsetForType(Montage->CalculateOffsetForNotify(Time));
	}
	Events.RemoveAll([&Removed](const FAnimNotifyEvent& Event) { return Removed.Contains(Event.Guid); });
	Montage->Modify();
	Montage->Notifies = MoveTemp(Events);
	// Do not sort/re-track other events as a side effect of a cosmetic timing edit.
	for (auto& Track : Montage->AnimNotifyTracks) Track.Notifies.Reset();
	for (auto& Event : Montage->Notifies)
	{
		if (Montage->AnimNotifyTracks.IsValidIndex(Event.TrackIndex))
			Montage->AnimNotifyTracks[Event.TrackIndex].Notifies.Add(&Event);
	}
	Montage->MarkPackageDirty();
	return true;
}

bool UMHGZAnimationSeamLibrary::ReplaceSoundNotifyClass(UAnimMontage* Montage, FGuid NotifyGuid,
	TSubclassOf<UAnimNotify> NewClass)
{
	if (!Montage || !NotifyGuid.IsValid() || !NewClass
		|| (NewClass != UAnimNotify_DustSound::StaticClass()
			&& NewClass != UAnimNotify_GroundDust::StaticClass()
			&& NewClass != UAnimNotify_LandingSound::StaticClass()
			&& NewClass != UAnimNotify_LandingEvent::StaticClass()
			&& NewClass != UAnimNotify_ClothSound::StaticClass())) return false;
	FAnimNotifyEvent* Event = nullptr;
	for (FAnimNotifyEvent& Candidate : Montage->Notifies)
	{
		if (Candidate.Guid != NotifyGuid) continue;
		if (Event) return false; // Ambiguous author IDs must not trigger a partial migration.
		Event = &Candidate;
	}
	if (!Event || Event->NotifyStateClass) return false;
	UAnimNotify_SwingSound* Previous = Cast<UAnimNotify_SwingSound>(Event->Notify);
	if (!Previous) return false;
	if (Previous->GetClass() == NewClass) return true;
	if (Previous->GetClass() != UAnimNotify_SwingSound::StaticClass()
		&& Previous->GetClass() != UAnimNotify_ActionSound::StaticClass()
		&& !(Previous->GetClass() == UAnimNotify_DustSound::StaticClass()
			&& NewClass == UAnimNotify_GroundDust::StaticClass())
		&& !(Previous->GetClass() == UAnimNotify_LandingSound::StaticClass()
			&& NewClass == UAnimNotify_LandingEvent::StaticClass())) return false;
	UAnimNotify_SwingSound* Replacement = NewObject<UAnimNotify_SwingSound>(Montage, NewClass, NAME_None, RF_Transactional);
	// Copy inherited author properties, not just today's hand-picked Sound/Socket fields.
	for (TFieldIterator<FProperty> It(UAnimNotify_SwingSound::StaticClass()); It; ++It)
	{
		It->CopyCompleteValue_InContainer(Replacement, Previous);
	}
	Montage->Modify();
	Event->Notify = Replacement;
	// Keep the entire event (including cached links and NotifyName) untouched.
	// The category's GetNotifyName is the editor-facing timeline label.
	// Do not RefreshCacheData here: it sorts events and may move overlapping
	// tracks. The FAnimNotifyEvent itself and all branching flags stay identical.
	Montage->MarkPackageDirty();
	return true;
}

bool UMHGZAnimationSeamLibrary::MoveSoundNotifyToTrack(UAnimMontage* Montage, FGuid NotifyGuid, FName TrackName)
{
	if (!Montage || !NotifyGuid.IsValid() || TrackName.IsNone()) return false;
	FAnimNotifyEvent* Event = nullptr;
	for (FAnimNotifyEvent& Candidate : Montage->Notifies)
	{
		if (Candidate.Guid != NotifyGuid) continue;
		if (Event) return false;
		Event = &Candidate;
	}
	if (!Event || Event->NotifyStateClass || !Cast<UAnimNotify_SwingSound>(Event->Notify)) return false;
	int32 Index = INDEX_NONE;
	for (int32 I = 0; I < Montage->AnimNotifyTracks.Num(); ++I)
	{
		if (Montage->AnimNotifyTracks[I].TrackName != TrackName) continue;
		if (Index != INDEX_NONE) return false;
		Index = I;
	}
	if (Index != INDEX_NONE && Event->TrackIndex == Index) return true;
	Montage->Modify();
	if (Index == INDEX_NONE)
	{
		FAnimNotifyTrack Track;
		Track.TrackName = TrackName;
		Track.TrackColor = FLinearColor(.3f, .65f, .85f, 1.f);
		Index = Montage->AnimNotifyTracks.Add(Track);
	}
	Event->TrackIndex = Index;
	// Refresh editor event pointers only; RefreshCacheData also sorts events and
	// can relocate overlapping points to other tracks, which is not this operation.
	for (FAnimNotifyTrack& Track : Montage->AnimNotifyTracks) Track.Notifies.Reset();
	for (FAnimNotifyEvent& Existing : Montage->Notifies)
	{
		if (Montage->AnimNotifyTracks.IsValidIndex(Existing.TrackIndex))
		{
			Montage->AnimNotifyTracks[Existing.TrackIndex].Notifies.Add(&Existing);
		}
	}
	Montage->MarkPackageDirty();
	return true;
}
