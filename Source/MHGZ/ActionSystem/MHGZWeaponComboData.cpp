// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZWeaponComboData.h"

#include "Abilities/GameplayAbility.h"

#define LOCTEXT_NAMESPACE "MHGZWeaponComboData"

namespace
{
bool HaveSameMatchCondition(const FComboTransition& A, const FComboTransition& B)
{
	return A.SourceState == B.SourceState
		&& A.bMatchAnyState == B.bMatchAnyState
		&& A.BlockedSourceStates == B.BlockedSourceStates
		&& A.InputTag == B.InputTag
		&& A.Direction == B.Direction
		&& A.RequiredTags == B.RequiredTags
		&& A.BlockedTags == B.BlockedTags
		&& A.StaminaRequired == B.StaminaRequired
		&& A.bRequiresComboWindow == B.bRequiresComboWindow
		&& A.bAutoTransition == B.bAutoTransition
		&& A.Priority == B.Priority;
}

bool IsClearlyGroundState(const FName State)
{
	const FString Value = State.ToString();
	return State == FName(TEXT("Idle")) || Value.StartsWith(TEXT("Ground")) || Value.StartsWith(TEXT("IG.Ground"));
}
}

FPrimaryAssetId UMHGZWeaponComboData::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("WeaponComboData"), GetFName());
}

void UMHGZWeaponComboData::PostLoad()
{
	Super::PostLoad();

	// bool -> enum 无法由 PropertyRedirect 安全转换，只在加载旧资产时迁移一次。
	for (FComboTransition& Transition : Transitions)
	{
		if (Transition.bRequiresHitToGrantTags)
		{
			Transition.GrantTiming = ETransitionGrantTiming::OnFirstHit;
			Transition.bRequiresHitToGrantTags = false;
		}
	}
}

#if WITH_EDITOR
EDataValidationResult UMHGZWeaponComboData::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	bool bInvalid = false;
	TSet<FName> IDs;

	auto AddError = [&Context, &bInvalid](const FText& Error)
	{
		Context.AddError(Error);
		bInvalid = true;
	};

	for (int32 Index = 0; Index < Transitions.Num(); ++Index)
	{
		const FComboTransition& Transition = Transitions[Index];
		const FText IndexText = FText::AsNumber(Index);

		if (Transition.TransitionID.IsNone())
		{
			AddError(FText::Format(LOCTEXT("EmptyTransitionID", "Transitions[{0}] has an empty TransitionID."), IndexText));
		}
		else if (IDs.Contains(Transition.TransitionID))
		{
			AddError(FText::Format(LOCTEXT("DuplicateTransitionID", "TransitionID '{0}' is duplicated."), FText::FromName(Transition.TransitionID)));
		}
		else
		{
			IDs.Add(Transition.TransitionID);
		}

		if (!Transition.bMatchAnyState && Transition.SourceState.IsNone())
		{
			AddError(FText::Format(LOCTEXT("MissingSourceState", "Transitions[{0}] must define SourceState unless bMatchAnyState is true."), IndexText));
		}
		if (!Transition.bMatchAnyState && !Transition.BlockedSourceStates.IsEmpty())
		{
			AddError(FText::Format(LOCTEXT("IllegalBlockedSourceStates", "Transitions[{0}] may use BlockedSourceStates only with bMatchAnyState."), IndexText));
		}

		if (Transition.bAutoTransition == Transition.InputTag.IsValid())
		{
			AddError(FText::Format(LOCTEXT("InvalidInputAutoPair", "Transitions[{0}] must be either an input edge with InputTag or an automatic edge without InputTag."), IndexText));
		}

		if (Transition.StatePolicy == EComboStatePolicy::Replace && Transition.TargetState.IsNone())
		{
			AddError(FText::Format(LOCTEXT("ReplaceMissingTarget", "Transitions[{0}] uses Replace but has no TargetState."), IndexText));
		}
		if (Transition.StatePolicy == EComboStatePolicy::Preserve && !Transition.GrantedTags.IsEmpty())
		{
			AddError(FText::Format(LOCTEXT("PreserveGrantsTags", "Transitions[{0}] uses Preserve and cannot own cross-action GrantedTags."), IndexText));
		}

		if (Transition.ExecutionPolicy == EComboExecutionPolicy::ActivateAbility && !Transition.AbilityClass)
		{
			AddError(FText::Format(LOCTEXT("MissingAbilityClass", "Transitions[{0}] uses ActivateAbility but has no AbilityClass."), IndexText));
		}
		if (Transition.ExecutionPolicy == EComboExecutionPolicy::StateOnly
			&& (!Transition.bAutoTransition || Transition.AbilityClass))
		{
			AddError(FText::Format(LOCTEXT("InvalidStateOnly", "Transitions[{0}] StateOnly edges must be automatic and must not specify AbilityClass."), IndexText));
		}
		if (!Transition.bAutoTransition && Transition.ExecutionPolicy == EComboExecutionPolicy::StateOnly)
		{
			AddError(FText::Format(LOCTEXT("InputStateOnly", "Transitions[{0}] input edges cannot use StateOnly."), IndexText));
		}

		if (Transition.LandingPolicy == EComboLandingPolicy::AbilityOwned
			&& !Transition.bMatchAnyState && IsClearlyGroundState(Transition.SourceState))
		{
			AddError(FText::Format(LOCTEXT("GroundAbilityOwnedLanding", "Transitions[{0}] is a ground transition and cannot own landing."), IndexText));
		}

		for (int32 OtherIndex = 0; OtherIndex < Index; ++OtherIndex)
		{
			if (HaveSameMatchCondition(Transition, Transitions[OtherIndex]))
			{
				AddError(FText::Format(
					LOCTEXT("AmbiguousTransitions", "Transitions[{0}] and Transitions[{1}] have a completely tied match condition."),
					IndexText, FText::AsNumber(OtherIndex)));
				break;
			}
		}
	}

	return bInvalid ? EDataValidationResult::Invalid
		: (Result == EDataValidationResult::NotValidated ? EDataValidationResult::Valid : Result);
}
#endif

#undef LOCTEXT_NAMESPACE
