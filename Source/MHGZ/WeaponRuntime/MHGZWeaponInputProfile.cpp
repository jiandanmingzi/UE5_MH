// Copyright MHGZ Project. All Rights Reserved.

#include "WeaponRuntime/MHGZWeaponInputProfile.h"

#include "InputAction.h"
#include "Misc/DataValidation.h"

#define LOCTEXT_NAMESPACE "MHGZWeaponInputProfile"

namespace
{
bool SameTagSet(const TArray<FGameplayTag>& A, const TArray<FGameplayTag>& B)
{
	if (A.Num() != B.Num())
	{
		return false;
	}

	TSet<FGameplayTag> SetA;
	SetA.Append(A);
	for (const FGameplayTag& Tag : B)
	{
		if (!SetA.Contains(Tag))
		{
			return false;
		}
	}
	return true;
}
}

FPrimaryAssetId UWeaponInputProfile::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("WeaponInputProfile"), GetFName());
}

#if WITH_EDITOR
EDataValidationResult UWeaponInputProfile::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	bool bInvalid = false;

	auto AddError = [&Context, &bInvalid](const FText& Error)
	{
		Context.AddError(Error);
		bInvalid = true;
	};

	if (!FMath::IsFinite(ChordGracePeriod) || ChordGracePeriod <= 0.f)
	{
		AddError(FText::Format(
			LOCTEXT("InvalidChordGracePeriod", "ChordGracePeriod must be > 0 (current {0})."),
			FText::AsNumber(ChordGracePeriod)));
	}
	if (!FMath::IsFinite(DirectionInputThreshold)
		|| DirectionInputThreshold < 0.f || DirectionInputThreshold > 1.f)
	{
		AddError(FText::Format(
			LOCTEXT("InvalidDirectionThreshold", "DirectionInputThreshold must be within [0, 1] (current {0})."),
			FText::AsNumber(DirectionInputThreshold)));
	}
	if (!FMath::IsFinite(ForwardConeHalfAngle)
		|| ForwardConeHalfAngle < 0.f || ForwardConeHalfAngle > 180.f)
	{
		AddError(FText::Format(
			LOCTEXT("InvalidForwardCone", "ForwardConeHalfAngle must be within [0, 180] degrees (current {0})."),
			FText::AsNumber(ForwardConeHalfAngle)));
	}

	// RawAction -> PhysicalInputTag：拒绝空映射与重复物理标签
	TSet<FGameplayTag> MappedPhysicalTags;
	for (const TPair<TObjectPtr<UInputAction>, FGameplayTag>& Pair : RawActionToPhysicalInputTag)
	{
		if (!Pair.Key)
		{
			AddError(LOCTEXT("NullRawAction", "RawActionToPhysicalInputTag contains a null InputAction."));
			continue;
		}
		if (!Pair.Value.IsValid())
		{
			AddError(FText::Format(
				LOCTEXT("InvalidMappedTag", "RawAction '{0}' maps to an invalid/empty PhysicalInputTag."),
				FText::FromName(Pair.Key->GetFName())));
		}
		else if (MappedPhysicalTags.Contains(Pair.Value))
		{
			AddError(FText::Format(
				LOCTEXT("DuplicateMappedTag", "PhysicalInputTag '{0}' is produced by more than one RawAction."),
				FText::FromString(Pair.Value.ToString())));
		}
		else
		{
			MappedPhysicalTags.Add(Pair.Value);
		}
	}

	for (int32 Index = 0; Index < Chords.Num(); ++Index)
	{
		const FWeaponChordDefinition& Chord = Chords[Index];
		const FText IndexText = FText::AsNumber(Index);

		if (!Chord.OutputTag.IsValid())
		{
			AddError(FText::Format(
				LOCTEXT("InvalidOutputTag", "Chords[{0}] has an invalid/empty OutputTag."),
				IndexText));
		}

		if (Chord.TriggerControls.Num() == 0)
		{
			AddError(FText::Format(
				LOCTEXT("MissingTriggerControls", "Chords[{0}] must declare at least one TriggerControl."),
				IndexText));
		}

		TSet<FGameplayTag> TriggerSet;
		TSet<FGameplayTag> ModifierSet;

		for (const FGameplayTag& Trigger : Chord.TriggerControls)
		{
			if (!Trigger.IsValid())
			{
				AddError(FText::Format(
					LOCTEXT("InvalidTriggerTag", "Chords[{0}] contains an invalid/empty TriggerControl."),
					IndexText));
			}
			else if (TriggerSet.Contains(Trigger))
			{
				AddError(FText::Format(
					LOCTEXT("DuplicateTriggerTag", "Chords[{0}] repeats TriggerControl '{1}'."),
					IndexText, FText::FromString(Trigger.ToString())));
			}
			else
			{
				TriggerSet.Add(Trigger);
			}

			if (Trigger.IsValid() && !MappedPhysicalTags.Contains(Trigger))
			{
				AddError(FText::Format(
					LOCTEXT("UnmappedTrigger", "Chords[{0}] TriggerControl '{1}' is not produced by any RawAction."),
					IndexText, FText::FromString(Trigger.ToString())));
			}
		}

		for (const FGameplayTag& Modifier : Chord.RequiredHeldModifiers)
		{
			if (!Modifier.IsValid())
			{
				AddError(FText::Format(
					LOCTEXT("InvalidModifierTag", "Chords[{0}] contains an invalid/empty RequiredHeldModifier."),
					IndexText));
			}
			else if (ModifierSet.Contains(Modifier))
			{
				AddError(FText::Format(
					LOCTEXT("DuplicateModifierTag", "Chords[{0}] repeats RequiredHeldModifier '{1}'."),
					IndexText, FText::FromString(Modifier.ToString())));
			}
			else
			{
				ModifierSet.Add(Modifier);
			}

			if (Modifier.IsValid() && !MappedPhysicalTags.Contains(Modifier))
			{
				AddError(FText::Format(
					LOCTEXT("UnmappedModifier", "Chords[{0}] RequiredHeldModifier '{1}' is not produced by any RawAction."),
					IndexText, FText::FromString(Modifier.ToString())));
			}
		}

		// Trigger 与 Modifier 不得重叠
		for (const FGameplayTag& Overlap : TriggerSet.Intersect(ModifierSet))
		{
			AddError(FText::Format(
				LOCTEXT("TriggerModifierOverlap", "Chords[{0}] uses '{1}' as both TriggerControl and RequiredHeldModifier."),
				IndexText, FText::FromString(Overlap.ToString())));
		}

		// ReleaseControlTag 必须属于本 Chord 的 Trigger 或 Modifier
		if (Chord.ReleaseControlTag.IsValid()
			&& !TriggerSet.Contains(Chord.ReleaseControlTag)
			&& !ModifierSet.Contains(Chord.ReleaseControlTag))
		{
			AddError(FText::Format(
				LOCTEXT("InvalidReleaseControl", "Chords[{0}] ReleaseControlTag '{1}' is not one of its TriggerControls/RequiredHeldModifiers."),
				IndexText, FText::FromString(Chord.ReleaseControlTag.ToString())));
		}

		// 相同成员与同 Priority 必须产生唯一 OutputTag，否则解析结果不确定。
		for (int32 OtherIndex = 0; OtherIndex < Index; ++OtherIndex)
		{
			const FWeaponChordDefinition& Other = Chords[OtherIndex];
			if (Chord.Priority == Other.Priority
				&& Chord.bRequireExactModifiers == Other.bRequireExactModifiers
				&& SameTagSet(Chord.TriggerControls, Other.TriggerControls)
				&& SameTagSet(Chord.RequiredHeldModifiers, Other.RequiredHeldModifiers))
			{
				AddError(FText::Format(
					LOCTEXT("AmbiguousChord", "Chords[{0}] and Chords[{1}] have tied members and Priority; outputs cannot be resolved deterministically (current '{2}')."),
					IndexText, FText::AsNumber(OtherIndex),
					FText::FromString(Chord.OutputTag.ToString())));
				break;
			}
		}
	}

	return bInvalid ? EDataValidationResult::Invalid
		: (Result == EDataValidationResult::NotValidated ? EDataValidationResult::Valid : Result);
}
#endif

#undef LOCTEXT_NAMESPACE
