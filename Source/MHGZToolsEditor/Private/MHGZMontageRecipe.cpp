// Copyright MHGZ Project. All Rights Reserved.
#include "MHGZAnimationSeamLibrary.h"

#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Animation/MHGZMontageSegmentBlend.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

UAnimSequence* UMHGZAnimationSeamLibrary::CreateRootMotionCopy(UAnimSequence* Source, const FString& OutputPackage)
{
	if (!Source || !Source->GetSkeleton() || !OutputPackage.StartsWith(TEXT("/Game/"))
		|| !FPackageName::IsValidLongPackageName(OutputPackage) || FPackageName::DoesPackageExist(OutputPackage)
		|| FindPackage(nullptr, *OutputPackage)) { return nullptr; }
	UPackage* Package = CreatePackage(*OutputPackage);
	UAnimSequence* Copy = DuplicateObject<UAnimSequence>(Source, Package,
		FName(*FPackageName::GetLongPackageAssetName(OutputPackage)));
	if (!Copy) { return nullptr; }
	Copy->ClearFlags(RF_Transient);
	Copy->SetFlags(RF_Public | RF_Standalone);
	Copy->bEnableRootMotion = true;
	Copy->bForceRootLock = false;
	Copy->RefreshCacheData();
	Copy->PostEditChange();
	Copy->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Copy);
	return Copy;
}

bool UMHGZAnimationSeamLibrary::SetMontageSegmentBlend(UAnimMontage* Montage, float Duration)
{
	if (!Montage || !FMath::IsFinite(Duration) || Duration < 0.0f) { return false; }
	Montage->Modify();
	UMHGZMontageSegmentBlendSettings* Settings = Montage->FindMetaDataByClass<UMHGZMontageSegmentBlendSettings>();
	if (!Settings)
	{
		Settings = NewObject<UMHGZMontageSegmentBlendSettings>(Montage);
		Montage->AddMetaData(Settings);
	}
	Settings->Modify();
	Settings->Duration = Duration;
	Settings->bEnabled = Duration > 0.0f;
	Montage->MarkPackageDirty();
	return true;
}

bool UMHGZAnimationSeamLibrary::RebuildSingleSlotMontage(UAnimMontage* Montage, const FString& RecipeJson)
{
	if (!Montage || Montage->SlotAnimTracks.Num() != 1) { return false; }
	TSharedPtr<FJsonObject> Recipe;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(RecipeJson), Recipe) || !Recipe) { return false; }
	const TArray<TSharedPtr<FJsonValue>> *ClipSpecs, *SectionSpecs, *NotifySpecs;
	if (!Recipe->TryGetArrayField(TEXT("segments"), ClipSpecs)
		|| !Recipe->TryGetArrayField(TEXT("sections"), SectionSpecs)
		|| !Recipe->TryGetArrayField(TEXT("notifies"), NotifySpecs)) { return false; }
	TArray<FAnimSegment> Clips;
	float Length = 0.0f;
	for (const auto& Value : *ClipSpecs)
	{
		const auto& Spec = Value->AsObject();
		FString Path;
		double Start, End, Rate, Loops;
		if (!Spec || !Spec->TryGetStringField(TEXT("asset"), Path)
			|| !Spec->TryGetNumberField(TEXT("source_start"), Start)
			|| !Spec->TryGetNumberField(TEXT("source_end"), End)
			|| !Spec->TryGetNumberField(TEXT("play_rate"), Rate)
			|| !Spec->TryGetNumberField(TEXT("loops"), Loops)) { return false; }
		UAnimSequence* Sequence = LoadObject<UAnimSequence>(nullptr, *Path);
		if (!Sequence || Sequence->GetSkeleton() != Montage->GetSkeleton()
			|| !FMath::IsFinite(Start) || !FMath::IsFinite(End) || !FMath::IsFinite(Rate)
			|| Rate <= 0 || Sequence->RateScale <= 0 || Start < 0 || End <= Start
			|| End > Sequence->GetPlayLength() + KINDA_SMALL_NUMBER || Loops != 1) { return false; }
		FAnimSegment& Clip = Clips.AddDefaulted_GetRef();
		Clip.SetAnimReference(Sequence, true);
		Clip.StartPos = Length;
		Clip.AnimStartTime = Start;
		Clip.AnimEndTime = End;
		Clip.AnimPlayRate = Rate;
		Clip.LoopingCount = 1;
		Length = Clip.GetEndPos();
	}
	if (Clips.IsEmpty() || !FMath::IsFinite(Length) || Length <= 0) { return false; }
	TArray<FName> Names, NextNames;
	TArray<float> SectionTimes;
	for (const auto& Value : *SectionSpecs)
	{
		const auto& Spec = Value->AsObject();
		FString Name, Next;
		double Time;
		if (!Spec || !Spec->TryGetStringField(TEXT("name"), Name)
			|| !Spec->TryGetStringField(TEXT("next"), Next)
			|| !Spec->TryGetNumberField(TEXT("time"), Time)
			|| Name.IsEmpty() || Names.Contains(FName(*Name)) || !FMath::IsFinite(Time)
			|| Time < 0 || Time >= Length || (!SectionTimes.IsEmpty() && Time <= SectionTimes.Last())) { return false; }
		Names.Add(FName(*Name)); NextNames.Add(Next.IsEmpty() ? NAME_None : FName(*Next)); SectionTimes.Add(Time);
	}
	for (FName Next : NextNames) { if (!Next.IsNone() && !Names.Contains(Next)) { return false; } }
	if (Names.IsEmpty() || !FMath::IsNearlyZero(SectionTimes[0])) { return false; }
	TArray<FAnimNotifyEvent> Events;
	TArray<bool> Cloned;
	TArray<float> NotifyStarts, NotifyEnds;
	TSet<int32> UsedOriginals;
	for (const auto& Value : *NotifySpecs)
	{
		const auto& Spec = Value->AsObject();
		double OldIndex, Start, End;
		bool bClone;
		if (!Spec || !Spec->TryGetNumberField(TEXT("old_index"), OldIndex) || !FMath::IsFinite(OldIndex)
			|| OldIndex != FMath::FloorToDouble(OldIndex) || !Montage->Notifies.IsValidIndex(static_cast<int32>(OldIndex))
			|| !Spec->TryGetNumberField(TEXT("start"), Start) || !Spec->TryGetNumberField(TEXT("end"), End)
			|| !Spec->TryGetBoolField(TEXT("clone"), bClone)
			|| !FMath::IsFinite(Start) || !FMath::IsFinite(End) || Start < 0 || End < Start
			|| End > Length + KINDA_SMALL_NUMBER || (!bClone && UsedOriginals.Contains(OldIndex))) { return false; }
		FAnimNotifyEvent Event = Montage->Notifies[static_cast<int32>(OldIndex)];
		if (Event.NotifyStateClass && End <= Start) { return false; }
		if (!bClone) { UsedOriginals.Add(static_cast<int32>(OldIndex)); }
		Events.Add(Event); Cloned.Add(bClone);
		NotifyStarts.Add(Start); NotifyEnds.Add(End);
	}
	Montage->Modify();
	Montage->SlotAnimTracks[0].AnimTrack.AnimSegments = MoveTemp(Clips);
	Montage->SetCompositeLength(Length);
	Montage->CompositeSections.Reset();
	for (int32 Index = 0; Index < Names.Num(); ++Index)
	{
		FCompositeSection& Section = Montage->CompositeSections.AddDefaulted_GetRef();
		Section.SectionName = Names[Index]; Section.NextSectionName = NextNames[Index];
		Section.Link(Montage, SectionTimes[Index], 0);
	}
	Montage->Notifies = MoveTemp(Events);
	for (int32 Index = 0; Index < Montage->Notifies.Num(); ++Index)
	{
		FAnimNotifyEvent& Event = Montage->Notifies[Index];
		const float Start = NotifyStarts[Index], End = NotifyEnds[Index];
		if (Cloned[Index])
		{
			Event.Guid = FGuid::NewGuid();
			if (Event.Notify)
			{
				Event.Notify = DuplicateObject<UAnimNotify>(Event.Notify, Montage,
					MakeUniqueObjectName(Montage, Event.Notify->GetClass(), Event.Notify->GetFName()));
			}
			if (Event.NotifyStateClass)
			{
				Event.NotifyStateClass = DuplicateObject<UAnimNotifyState>(Event.NotifyStateClass, Montage,
					MakeUniqueObjectName(Montage, Event.NotifyStateClass->GetClass(), Event.NotifyStateClass->GetFName()));
			}
		}
		Event.Link(Montage, Start, 0);
		if (Event.NotifyStateClass) { Event.EndLink.Link(Montage, End, 0); Event.SetDuration(End - Start); }
	}
	Montage->UpdateLinkableElements();
	Montage->RefreshCacheData();
	Montage->PostEditChange();
	Montage->MarkPackageDirty();
	return true;
}
