// Copyright MHGZ Project. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/MHGZMotionMatchingAnimInstance.h"
#include "Animation/MHGZPoseSearchFeatureChannel_LoopPhase.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSyncScope.h"
#include "BlendStack/AnimNode_BlendStack.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "MHGZCharacter.h"
#include "MHGZAnimationSeamLibrary.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "PoseSearch/PoseSearchContext.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchDerivedData.h"
#include "PoseSearch/PoseSearchIndex.h"
#include "PoseSearch/AnimNode_MotionMatching.h"
#include "UObject/UnrealType.h"

namespace
{
struct FSeamProxyAccess : UAnimInstance
{
	static FAnimInstanceProxy& Read(UAnimInstance& Anim)
	{ return *GetProxyOnGameThreadStatic<FAnimInstanceProxy>(&Anim); }
};
struct FSeamProxyMethods : FAnimInstanceProxy
{
	static void Prepare(FAnimInstanceProxy& Proxy, UAnimInstance* Anim, float Delta)
	{
		auto Pre = &FSeamProxyMethods::PreUpdate;
		(Proxy.*Pre)(Anim, Delta);
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZLiveBlendStackTest,
	"MHGZ.Maintenance.Locomotion.LiveRootMotionBlend",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZLiveBlendStackTest::RunTest(const FString& Parameters)
{
	UAnimSequence* Run = LoadObject<UAnimSequence>(nullptr, TEXT(
		"/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/AS_Shth_Run_Loop.AS_Shth_Run_Loop"));
	UAnimSequence* Sprint = LoadObject<UAnimSequence>(nullptr, TEXT(
		"/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/AS_Shth_Sprint_Loop_125x.AS_Shth_Sprint_Loop_125x"));
	UClass* CharacterClass = LoadClass<AMHGZCharacter>(nullptr, TEXT(
		"/Game/Blueprints/Characters/Demo/BP_IG_Character.BP_IG_Character_C"));
	if (!TestNotNull(TEXT("Run"), Run) || !TestNotNull(TEXT("Sprint"), Sprint)
		|| !TestNotNull(TEXT("Character"), CharacterClass)) { return false; }
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	AMHGZCharacter* Character = World->SpawnActor<AMHGZCharacter>(CharacterClass);
	USkeletalMeshComponent* Mesh = Character->GetMesh();
	Mesh->InitAnim(true);
	UAnimInstance* Anim = Mesh->GetAnimInstance();
	Anim->SetRootMotionMode(ERootMotionMode::RootMotionFromEverything);
	Mesh->TickAnimation(1.f / 60.f, false);
	Mesh->RefreshBoneTransforms();
	FAnimInstanceProxy& Proxy = FSeamProxyAccess::Read(*Anim);
	FMemMark Mark(FMemStack::Get());
	for (int32 MaxBlends : { 1, 2 })
	{
		FAnimNode_BlendStack_Standalone Stack;
		Stack.SetMaxActiveBlends(MaxBlends);
		Stack.Initialize_AnyThread(FAnimationInitializeContext(&Proxy));
		Stack.CacheBones_AnyThread(FAnimationCacheBonesContext(&Proxy));
		for (int32 Frame = 0; Frame < 7; ++Frame)
		{
			const float Delta = 1.f / 60.f;
			FSeamProxyMethods::Prepare(Proxy, Anim, Delta);
			Proxy.GetExtractedRootMotion().Clear();
			FAnimationUpdateSharedContext SharedContext;
			FAnimationUpdateContext Context(&Proxy, Delta, &SharedContext);
			UE::Anim::TScopedGraphMessage<UE::Anim::FAnimSyncGroupScope> SyncScope(Context, Context);
			if (Frame == 0) { Stack.BlendTo(Context, Run, 0.2f, true, false, nullptr, 0.f); }
			if (Frame == 1) { Stack.BlendTo(Context, Sprint, 0.17f, true, false, nullptr, 0.05f); }
			Stack.Update_AnyThread(Context);
			PRAGMA_DISABLE_DEPRECATION_WARNINGS
			Proxy.TickAssetPlayerInstances(); // Isolated low-level player test, not a game update path.
			PRAGMA_ENABLE_DEPRECATION_WARNINGS
			FPoseContext Pose(&Proxy);
			Stack.Evaluate_AnyThread(Pose);
			if (Frame == 1)
			{
				TestEqual(TEXT("Two samples exist during transition"), Stack.AnimPlayers.Num(), 2);
				const bool bOldStillPlaying = Stack.AnimPlayers.Last().GetAnimationAsset() == Run;
				TestEqual(TEXT("Only the two-active configuration preserves the old player"),
					bOldStillPlaying, MaxBlends == 2);
			}
			if (MaxBlends == 2 && Frame >= 1 && Frame <= 4)
			{
				const FVector DeltaRoot = Proxy.GetExtractedRootMotion().GetRootMotionTransform().GetTranslation();
				TestTrue(TEXT("Live blend extracts nonzero root movement every seam frame"),
					DeltaRoot.Size() > 1.f);
			}
		}
	}
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZLoopPhaseTest,
	"MHGZ.Maintenance.Locomotion.LoopPhase",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZLoopPhaseTest::RunTest(const FString& Parameters)
{
	using Channel = UMHGZPoseSearchFeatureChannel_LoopPhase;
	UPoseSearchSchema* Schema = LoadObject<UPoseSearchSchema>(nullptr, TEXT(
		"/Game/Blueprints/Characters/Demo/Animation/MotionMatching/PSS_MH_Shth_LoopPhase"));
	if (!TestNotNull(TEXT("Real skeleton-bound Loop schema"), Schema)) { return false; }
	const Channel* Phase = Cast<Channel>(Schema->FindChannel([](const UPoseSearchFeatureChannel* C)
		-> const UPoseSearchFeatureChannel* { return Cast<Channel>(C); }));
	if (!TestNotNull(TEXT("Phase channel"), Phase)) { return false; }
	TestEqual(TEXT("Phase plus cooked foot and root velocity entrance features"), Phase->GetChannelCardinality(), 17);
	const UE::PoseSearch::FPoseMetadata Metadata;
	for (int32 Step = 0; Step < 120; ++Step)
	{
		const float Cycle = Step / 120.f;
		const FVector2D Source = Channel::EncodePhase(Cycle * (50.f / 60.f), 50.f / 60.f);
		const FVector2D Target = Channel::EncodePhase(Cycle * (40.f / 60.f), 40.f / 60.f);
		TestTrue(TEXT("Run50 and Sprint40 share phase, not elapsed seconds"), Source.Equals(Target, 0.0001));
		TArray<float> Q, P;
		Q.SetNumZeroed(Schema->SchemaCardinality);
		P.SetNumZeroed(Schema->SchemaCardinality);
		const int32 Offset = Phase->GetChannelDataOffset();
		Q[Offset] = float(Source.X); Q[Offset + 1] = float(Source.Y);
		P[Offset] = float(Target.X); P[Offset + 1] = float(Target.Y);
		TestTrue(TEXT("Same phase is legal"), Phase->IsFilterValid(P, Q, 0, Metadata));
		P[Offset] = -P[Offset]; P[Offset + 1] = -P[Offset + 1];
		TestFalse(TEXT("Opposite-foot half-cycle cannot win on trajectory cost"), Phase->IsFilterValid(P, Q, 0, Metadata));
		const FVector2D Earlier = Channel::EncodePhase(Cycle - .08f, 1.f);
		P[Offset] = float(Earlier.X); P[Offset + 1] = float(Earlier.Y);
		TestFalse(TEXT("Previously legal backward phase jump is now rejected"), Phase->IsFilterValid(P, Q, 0, Metadata));
		Q[Offset] = Q[Offset + 1] = 0.f;
		TestTrue(TEXT("No source loop never invents a Start/Exit phase"), Phase->IsFilterValid(P, Q, 0, Metadata));
	}
	TestTrue(TEXT("Wrap has no phase discontinuity"),
		Channel::EncodePhase(0.999f, 1.f).Equals(Channel::EncodePhase(-0.001f, 1.f), 0.0001));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZLoopBankTest,
	"MHGZ.Maintenance.Locomotion.CandidateBanks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZLoopBankTest::RunTest(const FString& Parameters)
{
	using namespace UE::PoseSearch;
	for (const TCHAR* Name : { TEXT("PSD_MH_Shth_Run_LoopOnly"), TEXT("PSD_MH_Shth_Sprint_LoopOnly"),
		TEXT("PSD_MH_Shth_Walk_LoopOnly"), TEXT("PSD_MH_UnSh_Walk_LoopOnly") })
	{
		UPoseSearchDatabase* DB = LoadObject<UPoseSearchDatabase>(nullptr, *(FString(
			TEXT("/Game/Blueprints/Characters/Demo/Animation/MotionMatching/")) + Name));
		if (!TestNotNull(Name, DB)) { return false; }
		TestEqual(TEXT("Exactly one Loop, no Start/Idle/Stop candidate"), DB->GetNumAnimationAssets(), 1);
		const auto* Entry = DB->GetDatabaseAnimationAsset<FPoseSearchDatabaseSequence>(0);
		if (!TestNotNull(TEXT("Sequence entry"), Entry) || !TestNotNull(TEXT("Loop sequence"), Entry->Sequence.Get()))
		{ return false; }
		TestTrue(TEXT("Authored looping asset"), Entry->Sequence->bLoop);
		TestTrue(TEXT("Same asset cannot be freshly reselected"), Entry->bDisableReselection);
		TestEqual(TEXT("Index builds successfully"), FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(DB,
			ERequestAsyncBuildFlag::NewRequest | ERequestAsyncBuildFlag::WaitForCompletion), EAsyncBuildIndexResult::Success);
		TestTrue(TEXT("Candidates exist"), DB->GetSearchIndex().GetNumPoses() > 0);
		TestFalse(TEXT("Loop bank has no BlockTransition tail"), DB->GetSearchIndex().bAnyBlockTransition);
		if (FString(Name).Contains(TEXT("Run_")) || FString(Name).Contains(TEXT("Sprint_")))
		{
			const auto* Phase = DB->Schema->FindChannel([](const UPoseSearchFeatureChannel* C)
				-> const UPoseSearchFeatureChannel* { return Cast<UMHGZPoseSearchFeatureChannel_LoopPhase>(C); });
			if (!TestNotNull(TEXT("Dedicated phase channel installed"), Phase)) { return false; }
			TArray<float> Scratch;
			for (int32 I = 0; I < DB->GetSearchIndex().GetNumPoses(); ++I)
			{
				const auto Values = DB->GetSearchIndex().GetPoseValuesSafe(I, Scratch);
				const FVector2D Actual = FFeatureVectorHelper::DecodeVector2D(Values, Phase->GetChannelDataOffset());
				const FVector2D Expected = UMHGZPoseSearchFeatureChannel_LoopPhase::EncodePhase(
					DB->GetRealAssetTime(I), Entry->Sequence->GetPlayLength());
				TestTrue(TEXT("Indexed phase agrees with real source frame"), Actual.Equals(Expected, 0.001));
			}
		}
	}
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZMobileExitRouteTest,
	"MHGZ.Maintenance.Locomotion.MobileExitRoute",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZMobileExitRouteTest::RunTest(const FString& Parameters)
{
	using namespace MHGZMotionMatching;
	UClass* CharacterClass = LoadClass<AMHGZCharacter>(nullptr, TEXT(
		"/Game/Blueprints/Characters/Demo/BP_IG_Character.BP_IG_Character_C"));
	if (!TestNotNull(TEXT("Real character class"), CharacterClass)) { return false; }
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	auto* Character = World->SpawnActor<AMHGZCharacter>(CharacterClass);
	Character->GetMesh()->InitAnim(true);
	auto* Anim = Cast<UMHGZMotionMatchingAnimInstance>(Character->GetMesh()->GetAnimInstance());
	if (!TestNotNull(TEXT("Real AnimBP instance"), Anim)) { World->DestroyWorld(false); return false; }
	FAnimNode_MotionMatching* MM = nullptr;
	for (TFieldIterator<FStructProperty> It(Anim->GetClass()); It; ++It)
	{
		if (It->Struct == FAnimNode_MotionMatching::StaticStruct())
		{ MM = It->ContainerPtrToValuePtr<FAnimNode_MotionMatching>(Anim); break; }
	}
	if (!TestNotNull(TEXT("Real MM node"), MM)) { World->DestroyWorld(false); return false; }
	const FAnimNodeReference Ref(Anim, *MM);
	const FArrayProperty* BankProperty = FindFProperty<FArrayProperty>(FAnimNode_MotionMatching::StaticStruct(), TEXT("DatabasesToSearch"));
	if (!TestNotNull(TEXT("Auditable candidate bank array"), BankProperty)) { World->DestroyWorld(false); return false; }
	auto SelectedBank = [&]() -> UObject*
	{
		FScriptArrayHelper Banks(BankProperty, BankProperty->ContainerPtrToValuePtr<void>(MM));
		return Banks.Num() == 1 ? CastFieldChecked<FObjectPropertyBase>(BankProperty->Inner)
			->GetObjectPropertyValue(Banks.GetRawPtr(0)) : nullptr;
	};
	Anim->bMMHandoffActive = false;
	Anim->bMMHasLocomotionInput = Anim->bMMExitWantsMoveDatabase = true;
	Anim->bStartQueryActive = false;
	for (EMHGZNormalMoveGait Gait : { EMHGZNormalMoveGait::Walk, EMHGZNormalMoveGait::Run, EMHGZNormalMoveGait::Sprint })
	{
		Anim->MMSheathedTargetMoveGait = Gait;
		Anim->MMSheathedActionExitRoute = EMHGZMMActionExitRoute::ExitAndMove;
		Anim->MMSheathedActionExitHandoffType = EMHGZMotionMatchingHandoffType::DodgeMoveExit;
		Anim->MMSheathedActionExitSelectedTime = 0.2f;
		Anim->bMMStopRequestActive = false;
		UPoseSearchDatabase* Expected = Anim->GetMobileExitLoopDatabase(EMHGZMotionMatchingNode::Sheathed);
		Anim->OnSheathedMotionMatchingPreUpdate(FAnimUpdateContext(), Ref);
		TestEqual(TEXT("Exit does not clear the input's actual gait"), Anim->MMSheathedTargetMoveGait, Gait);
		TestEqual(TEXT("Safe tail searches exactly the requested Loop bank"), SelectedBank(), static_cast<UObject*>(Expected));
		Anim->bMMStopRequestActive = true;
		Anim->OnSheathedMotionMatchingPreUpdate(FAnimUpdateContext(), Ref);
		TestEqual(TEXT("Release immediately restores the ordinary Stop-capable bank"),
			SelectedBank(), static_cast<UObject*>(Anim->MMSheathedMoveDatabase.Get()));
		TestEqual(TEXT("Stop is not held until the Exit ends"), Anim->MMSheathedActionExitRoute, EMHGZMMActionExitRoute::Move);
	}
	World->DestroyWorld(false);
	return true;
}
#endif
