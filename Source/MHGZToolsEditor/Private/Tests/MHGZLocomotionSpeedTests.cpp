// Copyright MHGZ Project. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "ActionSystem/MHGZAbilitySystemComponent.h"
#include "Animation/MHGZMotionMatchingAnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSyncScope.h"
#include "AttributeSystem/MHGZAttributeSet.h"
#include "BlendStack/AnimNode_BlendStack.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayEffect.h"
#include "MHGZCharacter.h"
#include "MHGZPlayerState.h"
#include "PoseSearch/AnimNode_MotionMatching.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "UObject/UnrealType.h"

namespace
{
struct FSpeedProxyAccess : UAnimInstance
{
	static FAnimInstanceProxy& Read(UAnimInstance& Anim)
	{ return *GetProxyOnGameThreadStatic<FAnimInstanceProxy>(&Anim); }
};
struct FSpeedProxyMethods : FAnimInstanceProxy
{
	static void Prepare(FAnimInstanceProxy& Proxy, UAnimInstance* Anim, float Delta)
	{ auto Pre = &FSpeedProxyMethods::PreUpdate; (Proxy.*Pre)(Anim, Delta); }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZLocomotionSpeedTest,
	"MHGZ.Maintenance.Locomotion.WhiteExtractSpeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZLocomotionSpeedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FMemMark Mark(FMemStack::Get());
	auto* CharacterClass = LoadClass<AMHGZCharacter>(nullptr,
		TEXT("/Game/Blueprints/Characters/Demo/BP_IG_Character.BP_IG_Character_C"));
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	auto* Character = World->SpawnActor<AMHGZCharacter>(CharacterClass);
	auto* PS = World->SpawnActor<AMHGZPlayerState>();
	if (!TestTrue(TEXT("actual character and PlayerState spawn"), Character && PS)) { return false; }
	Character->SetPlayerState(PS);
	auto* ASC = PS->GetMHGZAbilitySystemComponent();
	ASC->InitAbilityActorInfo(PS, Character);
	Character->bUnsheathed = Character->bHasInput = true;
	Character->bForceMMIdle = false;
	Character->TargetCruiseSpeed = 440.f;
	Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	auto* Mesh = Character->GetMesh();
	Mesh->InitAnim(true);
	auto* Anim = Cast<UMHGZMotionMatchingAnimInstance>(Mesh->GetAnimInstance());
	if (!TestNotNull(TEXT("real animation Blueprint instance"), Anim)) { return false; }
	Anim->SetRootMotionMode(ERootMotionMode::RootMotionFromEverything);
	Mesh->TickAnimation(1.f/120.f, false);
	Mesh->RefreshBoneTransforms();
	FAnimNode_MotionMatching* MM = nullptr;
	FAnimNode_MotionMatching* SheathedMM = nullptr;
	for (TFieldIterator<FStructProperty> It(Anim->GetClass()); It; ++It)
	{
		if (It->Struct != FAnimNode_MotionMatching::StaticStruct()) { continue; }
		auto* Candidate = It->ContainerPtrToValuePtr<FAnimNode_MotionMatching>(Anim);
		const auto* DB = FindFProperty<FObjectPropertyBase>(FAnimNode_MotionMatching::StaticStruct(), TEXT("Database"));
		if (DB && DB->GetObjectPropertyValue_InContainer(Candidate) == Anim->MMUnsheathedMoveDatabase)
		{ MM = Candidate; }
		if (DB && DB->GetObjectPropertyValue_InContainer(Candidate) == Anim->MMSheathedMoveDatabase)
		{ SheathedMM = Candidate; }
	}
	if (!TestNotNull(TEXT("actual unsheathed MM node"), MM)) { return false; }
	if (!TestNotNull(TEXT("actual sheathed MM node"), SheathedMM)) { return false; }
	const FAnimNodeReference Ref(Anim, *MM);
	const auto* RateProperty = FindFProperty<FFloatProperty>(FAnimNode_MotionMatching::StaticStruct(), TEXT("PlayRateMultiplier"));
	auto ApplyRate = [&]
	{
		Anim->bMMUnsheathedLocomotion = true;
		Anim->bMMHasLocomotionInput = true;
		Anim->bMMForceIdle = false;
		Anim->MMUnsheathedActionExitRoute = EMHGZMMActionExitRoute::Move;
		Anim->OnUnsheathedMotionMatchingPreUpdate(FAnimUpdateContext(), Ref);
		return RateProperty->GetPropertyValue_InContainer(MM);
	};
	auto ApplyExtract = [&](const TCHAR* Path)
	{
		auto* EffectClass = LoadClass<UGameplayEffect>(nullptr, Path);
		FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(EffectClass, 1.f, ASC->MakeEffectContext());
		if (!Spec.IsValid()) { return FActiveGameplayEffectHandle(); }
		for (const TCHAR* Name : { TEXT("Data.IG.Buff.AttackMultiplier"), TEXT("Data.IG.Buff.DefenseMultiplier") })
		{ Spec.Data->SetSetByCallerMagnitude(FGameplayTag::RequestGameplayTag(Name), 1.f); }
		Spec.Data->SetSetByCallerMagnitude(FGameplayTag::RequestGameplayTag(TEXT("Data.IG.Buff.MoveSpeedMultiplier")), 1.15f);
		Spec.Data->SetDuration(90.f, true);
		return ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
	};
	Anim->UpdateLocomotionSpeed(*Character, 1.f);
	TestEqual(TEXT("no extract retains original 1x MM"), ApplyRate(), 1.f);
	const auto White = ApplyExtract(TEXT("/Game/GameplayEffects/InsectGlaive/GE_IG_WhiteExtract.GE_IG_WhiteExtract_C"));
	TestTrue(TEXT("actual White effect applies"), White.WasSuccessfullyApplied());
	Anim->UpdateLocomotionSpeed(*Character, 1.f);
	TestTrue(TEXT("actual White GE drives the existing MM node to 1.15x"), FMath::IsNearlyEqual(ApplyRate(), 1.15f));
	TestTrue(TEXT("expected world cruise is 506 while the query target remains 440"),
		FMath::IsNearlyEqual(Anim->MMExpectedWorldCruiseSpeed, 506.f) && Character->TargetCruiseSpeed == 440.f);
	Anim->ResetMMTemporalState(Character->GetActorLocation());
	Anim->bHadMoveInput = true;
	Anim->MMStartInputSettleDuration = 0.f;
	Character->SetActorLocation(Character->GetActorLocation() + FVector(50.6f, 0.f, 0.f));
	Anim->NativeUpdateAnimation(.1f);
	TestTrue(TEXT("world speed stays physical, trajectory query returns to authored units"),
		FMath::IsNearlyEqual(Anim->MMActualSpeed2D, 506.f, .02f)
		&& FMath::IsNearlyEqual(Anim->MMQuerySpeed2D, 440.f, .02f));
	const auto& Future = Anim->MMPredictedTrajectory.Samples;
	TestTrue(TEXT("0.2s database trajectory remains 88cm, not a second 1.15 multiplier"),
		Future.Num() > 1 && FMath::IsNearlyEqual(float(FVector::Dist(Future[1].Position, Future[0].Position)), 88.f, .02f));
	ASC->RemoveActiveGameplayEffect(White);
	const auto Triple = ApplyExtract(TEXT("/Game/GameplayEffects/InsectGlaive/GE_IG_TripleUp.GE_IG_TripleUp_C"));
	TestTrue(TEXT("actual TripleUp replacement applies"), Triple.WasSuccessfullyApplied());
	TestFalse(TEXT("replacement does not require a separate White tag"), ASC->HasMatchingGameplayTag(
		FGameplayTag::RequestGameplayTag(TEXT("WeaponResource.IG.Extract.White"))));
	Anim->UpdateLocomotionSpeed(*Character, 1.f);
	TestTrue(TEXT("TripleUp keeps the speed through the attribute, without double stacking"), FMath::IsNearlyEqual(ApplyRate(), 1.15f));
	Anim->MMUnsheathedActionExitRoute = EMHGZMMActionExitRoute::ExitOnly;
	Anim->MMLoopQueryDatabases[1] = nullptr;
	Anim->ApplyUnsheathedLocomotionSpeed(Ref);
	TestEqual(TEXT("functional Exit remains 1x"), RateProperty->GetPropertyValue_InContainer(MM), 1.f);
	Character->bForceMMIdle = true;
	Anim->UpdateLocomotionSpeed(*Character, 1.f);
	TestEqual(TEXT("action isolation never speeds up locomotion underneath its root motion"), ApplyRate(), 1.f);
	Character->bForceMMIdle = false;
	Character->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
	Anim->UpdateLocomotionSpeed(*Character, 1.f);
	TestEqual(TEXT("airborne locomotion is not sped up"), ApplyRate(), 1.f);
	Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	Anim->UpdateLocomotionSpeed(*Character, 1.f);
	const float BuffRate = ApplyRate();
	const float SheathedBefore = RateProperty->GetPropertyValue_InContainer(SheathedMM);
	Anim->OnSheathedMotionMatchingPreUpdate(FAnimUpdateContext(), FAnimNodeReference(Anim, *SheathedMM));
	TestEqual(TEXT("actual sheathed MM rate is unchanged by White/TripleUp"),
		RateProperty->GetPropertyValue_InContainer(SheathedMM), SheathedBefore);
	TestEqual(TEXT("sheathed callback does not install an additional buff rate"), Anim->GetLocomotionPlaybackRateScale(false), 1.f);

	// Exercise actual UE player ticking and extracted Root Motion, not a formula
	// that merely assumes that the animation's movement responds to play rate.
	auto* Loop = LoadObject<UAnimSequence>(nullptr,
		TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Locomotion/AS_UnSh_Walk_Loop.AS_UnSh_Walk_Loop"));
	if (!TestNotNull(TEXT("actual unsheathed loop"), Loop)) { return false; }
	auto& Proxy = FSpeedProxyAccess::Read(*Anim);
	for (int32 FPS : { 60, 120 })
	{
		auto Travel = [&](float Rate, int32 Frames)
		{
			FAnimNode_BlendStack_Standalone Stack;
			Stack.Initialize_AnyThread(FAnimationInitializeContext(&Proxy));
			Stack.CacheBones_AnyThread(FAnimationCacheBonesContext(&Proxy));
			FVector Total = FVector::ZeroVector;
			for (int32 I = 0; I < Frames; ++I)
			{
				FSpeedProxyMethods::Prepare(Proxy, Anim, 1.f/FPS);
				Proxy.GetExtractedRootMotion().Clear();
				FAnimationUpdateSharedContext Shared;
				FAnimationUpdateContext Update(&Proxy, 1.f/FPS, &Shared);
				UE::Anim::TScopedGraphMessage<UE::Anim::FAnimSyncGroupScope> Sync(Update, Update);
				if (I == 0) { Stack.BlendTo(Update, Loop, .1f, true, false, nullptr, 0.f); }
				Stack.UpdatePlayRate(Rate);
				Stack.Update_AnyThread(Update);
				PRAGMA_DISABLE_DEPRECATION_WARNINGS
				Proxy.TickAssetPlayerInstances();
				PRAGMA_ENABLE_DEPRECATION_WARNINGS
				FPoseContext Pose(&Proxy);
				Stack.Evaluate_AnyThread(Pose);
				Total += Proxy.GetExtractedRootMotion().GetRootMotionTransform().GetTranslation();
			}
			return float(Total.Size());
		};
		const float Baseline = Travel(1.f, FPS * 23 / 20);
		const float Faster = Travel(BuffRate, FPS);
		TestTrue(TEXT("1.15x covers the same root path in 1s as original in 1.15s, including loop seam"),
			Baseline > 1.f && FMath::IsNearlyEqual(Faster, Baseline, .05f));
		AddInfo(FString::Printf(TEXT("FPS=%d base1.15s=%.4fcm buff1s=%.4fcm"), FPS, Baseline, Faster));
	}
	auto* Start = LoadObject<UAnimSequence>(nullptr,
		TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Locomotion/AS_UnSh_Walk_Start.AS_UnSh_Walk_Start"));
	auto* Stop = LoadObject<UAnimSequence>(nullptr,
		TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Locomotion/AS_UnSh_Walk_Stop.AS_UnSh_Walk_Stop"));
	if (!TestTrue(TEXT("actual Start and Stop sequences load"), Start && Stop)) { return false; }
	Anim->bHasLatestSelection[1] = true;
	auto& Selection = Anim->MMLatestSelection[1];
	Selection.Animation = Start;
	Selection.AnimationTime = .345f;
	Anim->bStartQueryActive = true;
	Anim->bStartQueryObservedStart = false;
	Anim->UpdateStartIntentQuery(EMHGZMotionMatchingNode::Unsheathed, .01f);
	TestEqual(TEXT("Start clock follows actual source time, not wall time multiplied again"), Anim->MMStartQueryElapsed, .345f);
	TestEqual(TEXT("Start Intent follows the actual accelerated pose's curve"), Anim->MMIntentQuery,
		FMath::Clamp(Anim->EvaluateSelectedCurve(Selection, TEXT("MM_Intent")), 0.f, 1.f));
	Selection.Animation = Stop;
	Selection.AnimationTime = .1f;
	Selection.Frame = 1;
	Selection.bIsContinuing = true;
	Anim->UpdateLegacyIntentQueries(false, false, true, .01f);
	Selection.AnimationTime = .115f;
	Selection.Frame = 2;
	Anim->UpdateLegacyIntentQueries(false, false, false, .01f);
	TestEqual(TEXT("Stop Intent also follows source time, without slowing the query back down"), Anim->MMIntentQuery,
		FMath::Clamp(Anim->EvaluateSelectedCurve(Selection, TEXT("MM_Intent")), -1.f, 0.f));
	ASC->RemoveActiveGameplayEffect(Triple);
	Anim->UpdateLocomotionSpeed(*Character, .01f);
	TestTrue(TEXT("buff expiration smoothly reduces speed without restarting the node"),
		Anim->MMUnsheathedMoveSpeedMultiplier > 1.f && Anim->MMUnsheathedMoveSpeedMultiplier < BuffRate);
	Anim->UpdateLocomotionSpeed(*Character, 1.f);
	TestEqual(TEXT("expiration restores 1x"), ApplyRate(), 1.f);
	return true;
}
#endif
