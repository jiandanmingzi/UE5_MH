// Copyright MHGZ Project. All Rights Reserved.
#include "Animation/MHGZMotionMatchingAnimInstance.h"
#include "AbilitySystemComponent.h"
#include "AttributeSystem/MHGZAttributeSet.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MHGZCharacter.h"
#include "PoseSearch/AnimNode_MotionMatching.h"
#include "UObject/UnrealType.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"

void UMHGZMotionMatchingAnimInstance::UpdateLocomotionSpeed(
	const AMHGZCharacter& Character, const float DeltaSeconds)
{
	// All ASC/Host reads are game-thread only. Worker callbacks consume this snapshot.
	const auto* ASC = Character.GetAbilitySystemComponent();
	const float Attribute = ASC ? ASC->GetNumericAttribute(
		UMHGZAttributeSet::GetMoveSpeedMultiplierAttribute()) : 1.f;
	const float Target = Character.bUnsheathed && FMath::IsFinite(Attribute)
		&& Attribute > SMALL_NUMBER ? FMath::Clamp(Attribute, .2f, 3.f) : 1.f;
	MMUnsheathedMoveSpeedMultiplier = FMath::FInterpTo(MMUnsheathedMoveSpeedMultiplier,
		Target, FMath::Max(0.f, DeltaSeconds), MMUnsheathedSpeedInterpSpeed);
	const auto* Movement = Character.GetCharacterMovement();
	const auto* Host = Character.FindComponentByClass<UMHGZWeaponRuntimeHostComponent>();
	bMMCanApplyUnsheathedSpeed = Character.bUnsheathed && !Character.bForceMMIdle
		&& Movement && Movement->IsMovingOnGround()
		&& (!Host || (!Host->IsActionMovementOwned() && !Host->IsMontageRootMotionOwned()));
	MMBaseCruiseSpeed = Character.TargetCruiseSpeed;
	MMExpectedWorldCruiseSpeed = MMBaseCruiseSpeed * GetLocomotionPlaybackRateScale(Character.bUnsheathed);
}

void UMHGZMotionMatchingAnimInstance::ApplyUnsheathedLocomotionSpeed(const FAnimNodeReference& Reference)
{
	auto* MM = Reference.GetAnimNodePtr<FAnimNode_MotionMatching>();
	if (!MM) { return; }
	// A normal Stop interrupt or the first Loop-only search can retire an Exit on
	// this very callback. The outgoing Exit player keeps its existing 1x rate;
	// UE's BlendStack updates only the newest player's rate, not the old blend tail.
	const bool bNormalMove = MMUnsheathedActionExitRoute == EMHGZMMActionExitRoute::Move
		|| (MMUnsheathedLoopOnlyDatabase && GetLoopQueryDatabase(true) == MMUnsheathedLoopOnlyDatabase);
	const bool bActiveLocomotion = bMMHasLocomotionInput || bStartQueryActive || bMMStopRequestActive;
	MMUnsheathedAppliedSpeedMultiplier = bMMCanApplyUnsheathedSpeed && bMMUnsheathedLocomotion
		&& !bMMForceIdle && bNormalMove && bActiveLocomotion ? MMUnsheathedMoveSpeedMultiplier : 1.f;
	// Own only this node's additional multiplier. Sequence RateScale and MM's
	// WantedPlayRate remain unchanged; assigning (not multiplying) is reinit-safe.
	// UE5.6 exposes this authored UPROPERTY to AnimGraph but has no C++ setter.
	// Use its reflected field, as the existing routing does for MM state; no
	// engine changes, Blueprint reauthoring or per-frame property lookup.
	static const FFloatProperty* RateProperty = FindFProperty<FFloatProperty>(
		FAnimNode_MotionMatching::StaticStruct(), TEXT("PlayRateMultiplier"));
	check(RateProperty);
	RateProperty->SetPropertyValue_InContainer(MM, MMUnsheathedAppliedSpeedMultiplier);
	MMExpectedWorldCruiseSpeed = MMBaseCruiseSpeed * MMUnsheathedAppliedSpeedMultiplier;
}
