// Copyright MHGZ Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "ActionSystem/MHGZHitReactionAbility.h"
#include "Animation/AnimSequenceBase.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM2HitReactionDirectionSelection,
	"MHGZ.M2.HitReaction.DirectionSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM2HitReactionDirectionSelection::RunTest(const FString& Parameters)
{
	const UMHGZHitReactionAbility* Reaction = GetDefault<UMHGZHitReactionAbility>();
	TestNotNull(TEXT("Forward source sequence is loadable"), Reaction
		? Reaction->LightForwardSequence.LoadSynchronous() : nullptr);
	TestNotNull(TEXT("Back source sequence is loadable"), Reaction
		? Reaction->LightBackSequence.LoadSynchronous() : nullptr);
	TestNotNull(TEXT("Left source sequence is loadable"), Reaction
		? Reaction->LightLeftSequence.LoadSynchronous() : nullptr);
	TestNotNull(TEXT("Right source sequence is loadable"), Reaction
		? Reaction->LightRightSequence.LoadSynchronous() : nullptr);

	const FVector Target = FVector::ZeroVector;
	const FRotator FacingNorth = FRotator::ZeroRotator;
	TestEqual(TEXT("front source selects Forward animation"),
		UMHGZHitReactionAbility::ResolveDirection(FVector(100.f, 0.f, 0.f), Target,
			FacingNorth), EMHGZLightHitDirection::Forward);
	TestEqual(TEXT("back source selects Back animation"),
		UMHGZHitReactionAbility::ResolveDirection(FVector(-100.f, 0.f, 0.f), Target,
			FacingNorth), EMHGZLightHitDirection::Back);
	TestEqual(TEXT("left source selects Left animation"),
		UMHGZHitReactionAbility::ResolveDirection(FVector(0.f, -100.f, 0.f), Target,
			FacingNorth), EMHGZLightHitDirection::Left);
	TestEqual(TEXT("right source selects Right animation"),
		UMHGZHitReactionAbility::ResolveDirection(FVector(0.f, 100.f, 0.f), Target,
			FacingNorth), EMHGZLightHitDirection::Right);
	TestEqual(TEXT("rotation changes the local source direction"),
		UMHGZHitReactionAbility::ResolveDirection(FVector(0.f, 100.f, 0.f), Target,
			FRotator(0.f, 90.f, 0.f)), EMHGZLightHitDirection::Forward);

	TestEqual(TEXT("Light stagger priority"), UMHGZHitReactionAbility::GetStaggerPriority(
		FGameplayTag::RequestGameplayTag(TEXT("Combat.Stagger.Light"))), 1);
	TestEqual(TEXT("Medium stagger priority"), UMHGZHitReactionAbility::GetStaggerPriority(
		FGameplayTag::RequestGameplayTag(TEXT("Combat.Stagger.Medium"))), 2);
	TestEqual(TEXT("Heavy stagger priority"), UMHGZHitReactionAbility::GetStaggerPriority(
		FGameplayTag::RequestGameplayTag(TEXT("Combat.Stagger.Heavy"))), 3);
	TestFalse(TEXT("Light cannot interrupt Light"),
		UMHGZHitReactionAbility::CanIncomingStaggerInterrupt(1, 1));
	TestTrue(TEXT("Medium interrupts Light"),
		UMHGZHitReactionAbility::CanIncomingStaggerInterrupt(2, 1));
	TestTrue(TEXT("Heavy interrupts Light"),
		UMHGZHitReactionAbility::CanIncomingStaggerInterrupt(3, 1));
	TestFalse(TEXT("Light cannot interrupt Medium"),
		UMHGZHitReactionAbility::CanIncomingStaggerInterrupt(1, 2));
	TestFalse(TEXT("Medium cannot interrupt Medium"),
		UMHGZHitReactionAbility::CanIncomingStaggerInterrupt(2, 2));
	TestTrue(TEXT("Heavy interrupts Medium"),
		UMHGZHitReactionAbility::CanIncomingStaggerInterrupt(3, 2));
	TestFalse(TEXT("Light cannot interrupt Heavy"),
		UMHGZHitReactionAbility::CanIncomingStaggerInterrupt(1, 3));
	TestFalse(TEXT("Medium cannot interrupt Heavy"),
		UMHGZHitReactionAbility::CanIncomingStaggerInterrupt(2, 3));
	TestTrue(TEXT("Heavy can interrupt Heavy"),
		UMHGZHitReactionAbility::CanIncomingStaggerInterrupt(3, 3));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
