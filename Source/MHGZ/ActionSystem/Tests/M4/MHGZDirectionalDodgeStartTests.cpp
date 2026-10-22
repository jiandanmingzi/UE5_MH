// Copyright MHGZ Project. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "../Shared/MHGZM3TestHarness.h"
#include "../Shared/MHGZDodgeTestHarness.h"
#include "ActionSystem/MHGZDodgeAbility.h"
#include "ActionSystem/MHGZM1PlaceholderAbilities.h"
#include "Animation/MHGZMotionMatchingAnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "PoseSearch/AnimNode_MotionMatching.h"
#include "PoseSearch/MotionMatchingAnimNodeLibrary.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZDirectionalDodgeStartTest,
	"MHGZ.Maintenance.DodgeRecovery.SameFrameStartWithoutIdleOrSettle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZDirectionalDodgeStartTest::RunTest(const FString&)
{
	UClass* AnimClass = LoadClass<UMHGZMotionMatchingAnimInstance>(nullptr,
		TEXT("/Game/Blueprints/Characters/Demo/Animation/ABP_MH_Character.ABP_MH_Character_C"));
	UClass* DodgeClass = LoadClass<UMHGZDodgeAbility>(nullptr,
		TEXT("/Game/Blueprints/Ability/Core/GA_Dodge.GA_Dodge_C"));
	if (!TestNotNull(TEXT("resident AnimBP"), AnimClass)
		|| !TestNotNull(TEXT("resident dodge"), DodgeClass)) { return false; }

	enum class ECase { Held, Late, ReleaseBeforeUpdate, Successor, RuntimeInvalidated, Forward };
	auto Check = [&](EDirectionalInput Direction, float Delta, ECase Case)
	{
		FMHGZM3Harness H;
		if (!H.Setup(false, AMHGZM3TestCharacter::StaticClass())) { H.Teardown(); return false; }
		auto* Character = CastChecked<AMHGZCharacter>(H.Character);
		auto* Mesh = Character->GetMesh();
		Mesh->SetSkeletalMesh(LoadObject<USkeletalMesh>(nullptr,
			TEXT("/Game/Characters/Demo/Meshes/Body/SKM_Demo_Body.SKM_Demo_Body")));
		Mesh->SetAnimInstanceClass(AnimClass);
		H.ASC->RefreshAbilityActorInfo();
		auto* Anim = Cast<UMHGZMotionMatchingAnimInstance>(Mesh->GetAnimInstance());
		if (!TestNotNull(TEXT("real AnimBP"), Anim)
			|| !TestTrue(TEXT("same runtime host"), Character->GetWeaponRuntimeHost() == H.Host))
		{ H.Teardown(); return false; }
		H.Host->SetSheathed(false);
		Character->bUnsheathed = true;
		Character->bForceMMIdle = false;
		Character->bHasInput = false;
		Character->TargetCruiseSpeed = 0.f;
		// Leave a real recent Idle result/player behind, as in the recorded bug.
		// Without the one-shot interrupt its continuing pose/search throttle wins.
		Mesh->TickAnimation(Delta, false);
		Mesh->RefreshBoneTransforms();
		Anim->DrainMMSelectionEvents();
		const auto* IdleSelection = Anim->GetLatestSelection(EMHGZMotionMatchingNode::Unsheathed);
		TestTrue(TEXT("fixture begins with a real armed Idle player"), IdleSelection
			&& IdleSelection->Animation.IsValid() && IdleSelection->Animation->GetName() == TEXT("AS_UnSh_Idle"));
		Character->bForceMMIdle = true;
		Character->bHasInput = false;
		Anim->NativeUpdateAnimation(Delta); // Real action lock/reset, with no synthetic Start.
		const auto Handle = H.GiveAbility(DodgeClass);
		auto Input = M3::MakePosedInput(true, false);
		Input.ResolvedInputTag = M3::Tag(TEXT("Input.Dodge"));
		Input.Direction = Direction;
		if (Direction != EDirectionalInput::Forward)
		{ TestNotNull(TEXT("source attack owns an open cancel window"), M4Dodge::StartCancelableAttack(H)); }
		TestTrue(TEXT("real dodge starts"), H.TryActivateWithInput(Handle, Input));
		auto* Dodge = UMHGZDodgeAbility::FindOpenDirectionalRecovery(H.ASC);
		TestNull(TEXT("Core is not a recovery"), Dodge);
		FGameplayAbilitySpec* Spec = H.ASC->FindAbilitySpecFromHandle(Handle);
		if (!TestNotNull(TEXT("dodge spec"), Spec)) { H.Teardown(); return false; }
		for (UGameplayAbility* Ability : Spec->GetAbilityInstances())
		{ if (Ability->IsActive()) { Dodge = Cast<UMHGZDodgeAbility>(Ability); break; } }
		if (!TestNotNull(TEXT("live dodge"), Dodge)) { H.Teardown(); return false; }
		const FWeaponActionToken Source = Dodge->GetActionToken();
		TestFalse(TEXT("Core cannot queue a Start"), Anim->RequestDirectionalDodgeLocomotionStart(Source));
		UAnimMontage* Montage = Mesh->GetAnimInstance()->GetCurrentActiveMontage();
		auto* Playing = Montage ? Anim->GetActiveInstanceForMontage(Montage) : nullptr;
		if (!TestNotNull(TEXT("live montage"), Playing)) { H.Teardown(); return false; }
		if (Case == ECase::Held) { Character->DoMove(0, 1); } // Raw input persists under lock.
		Anim->Montage_JumpToSection(TEXT("IdleExit"), Montage);
		Playing->OnMontageSectionChanged.ExecuteIfBound(Montage, TEXT("IdleExit"), false);
		if (Case == ECase::Forward)
		{
			TestFalse(TEXT("forward cannot queue this path"), Anim->RequestDirectionalDodgeLocomotionStart(Source));
			TestEqual(TEXT("forward has no movement-start request"), Anim->PendingDirectionalDodgeStart.ActivationSequenceID, 0u);
			H.Teardown(); return true;
		}
		if (Case != ECase::Held)
		{
			TestTrue(TEXT("neutral IdleExit stays live"), Dodge->IsDirectionalRecoveryOpen());
			TestEqual(TEXT("neutral exit does not queue a Start"), Anim->PendingDirectionalDodgeStart.ActivationSequenceID, 0u);
			Character->DoMove(0, 1);
			Dodge->TryCompleteDirectionalRecoveryForMovement();
		}
		TestFalse(TEXT("movement retires the old action"), Dodge->IsActive());
		TestEqual(TEXT("movement queues the exact retired source"), Anim->PendingDirectionalDodgeStart.ActivationSequenceID, Source.ActivationSequenceID);
		TestFalse(TEXT("retired source cannot queue a new request"), Anim->RequestDirectionalDodgeLocomotionStart(Source));
		// Simulate the normal Character input update after its exact lock has retired.
		Character->bForceMMIdle = false;
		Character->bHasInput = Case != ECase::ReleaseBeforeUpdate;
		Character->TargetCruiseSpeed = Character->bHasInput ? 260.f : 0.f;
		Character->SetActorLocation(Character->GetActorLocation() + FVector(400, 0, 0));
		if (Case == ECase::Successor)
		{
			const auto Other = H.GiveAbility(UMHGZM1PlaceholderActionA::StaticClass());
			TestTrue(TEXT("a newer action can win"), H.TryActivateWithInput(Other, Input));
		}
		if (Case == ECase::RuntimeInvalidated) { H.Host->ShutdownRuntime(EWeaponRuntimeEndReason::RuntimeShutdown); }
		const bool bPositive = Case == ECase::Held || Case == ECase::Late;
		if (bPositive)
		{
			// Run the resident graph, its actual pre-search callback and Pose Search.
			Mesh->TickAnimation(Delta, false);
			Mesh->RefreshBoneTransforms();
			Anim->DrainMMSelectionEvents();
			TestEqual(TEXT("last action displacement is discarded"), Anim->MMActualSpeed2D, 0.f);
			TestEqual(TEXT("Start intent exists in first released update"), Anim->MMIntentQuery, 1.f);
			TestTrue(TEXT("ordinary Start lifecycle is armed"), Anim->bStartQueryActive);
			TestFalse(TEXT("no forced Idle on takeover"), Anim->bMMForceIdle);
			TestFalse(TEXT("no analogue settle on takeover"), Anim->bStartInputSettling);
			TestEqual(TEXT("no deferred settle timer"), Anim->MMStartInputSettleRemaining, 0.f);
			TestTrue(TEXT("held input is remembered, not a second rising edge"), Anim->bHadMoveInput);
			TestTrue(TEXT("predicted movement exists before searching"), FVector::Dist(
				Anim->MMPredictedTrajectory.Samples.Last().Position, Character->GetActorLocation()) > 1.f);
			const auto* Selection = Anim->GetLatestSelection(EMHGZMotionMatchingNode::Unsheathed);
			if (TestNotNull(TEXT("real first-frame search result"), Selection))
			{
				TestTrue(TEXT("winner is Walk_Start, not Idle/Loop"), Selection->Animation.IsValid()
					&& Selection->Animation->GetName() == TEXT("AS_UnSh_Walk_Start"));
			}
			TestFalse(TEXT("pre-search consumed its single interrupt"), Anim->bMMForceDirectionalStartSearch);
			FAnimNode_MotionMatching* ActiveMM = nullptr;
			for (TFieldIterator<FStructProperty> It(Anim->GetClass()); It; ++It)
			{
				if (It->Struct != FAnimNode_MotionMatching::StaticStruct()) { continue; }
				auto* Node = It->ContainerPtrToValuePtr<FAnimNode_MotionMatching>(Anim);
				if (Node->GetAnimAsset() && Node->GetAnimAsset()->GetName() == TEXT("AS_UnSh_Walk_Start")) { ActiveMM = Node; break; }
			}
			if (TestNotNull(TEXT("live armed Start player"), ActiveMM))
			{
				const auto* BlendTime = FindFProperty<FFloatProperty>(FAnimNode_MotionMatching::StaticStruct(), TEXT("BlendTime"));
				TestEqual(TEXT("no extra Idle->Start MM fade under the montage"), BlendTime->GetPropertyValue_InContainer(ActiveMM), 0.f);
			}
			Mesh->TickAnimation(Delta, false);
			Mesh->RefreshBoneTransforms();
			TestFalse(TEXT("next update does not re-enter forced Idle"), Anim->bMMForceIdle);
			TestEqual(TEXT("next update does not restart settle"), Anim->MMStartInputSettleRemaining, 0.f);
			TestFalse(TEXT("next update does not force another search"), Anim->bMMForceDirectionalStartSearch);
			if (ActiveMM)
			{
				const auto* BlendTime = FindFProperty<FFloatProperty>(FAnimNode_MotionMatching::StaticStruct(), TEXT("BlendTime"));
				const auto* Authored = reinterpret_cast<const FMotionMatchingBlueprintBlendSettings*>(Anim->MMBaseBlend[1]->GetStructMemory());
				TestEqual(TEXT("ordinary MM blend restored on next update"), BlendTime->GetPropertyValue_InContainer(ActiveMM), Authored->BlendTime);
			}
			// A genuinely new neutral->stick edge still uses the original settle rule.
			Anim->ResetMMTemporalState(Character->GetActorLocation());
			Anim->NativeUpdateAnimation(Delta);
			TestTrue(TEXT("ordinary standing start keeps original analogue settle"), Anim->bStartInputSettling);
		}
		else
		{
			Anim->NativeUpdateAnimation(Delta);
			TestFalse(TEXT("cancelled request cannot force a search"), Anim->bMMForceDirectionalStartSearch);
			TestEqual(TEXT("cancelled request is not retained"), Anim->PendingDirectionalDodgeStart.ActivationSequenceID, 0u);
		}
		H.Teardown();
		return true;
	};
	for (auto Direction : { EDirectionalInput::Left, EDirectionalInput::Right, EDirectionalInput::Back })
	{
		if (!Check(Direction, 1.f/60.f, ECase::Held) || !Check(Direction, .2f/120.f, ECase::Late)) { return false; }
	}
	for (auto Case : { ECase::ReleaseBeforeUpdate, ECase::Successor, ECase::RuntimeInvalidated, ECase::Forward })
	{ if (!Check(Case == ECase::Forward ? EDirectionalInput::Forward : EDirectionalInput::Back, 1.f/120.f, Case)) { return false; } }
	return true;
}
#endif
