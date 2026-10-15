// Copyright MHGZ Project. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/MHGZMotionMatchingAnimInstance.h"
#include "Animation/MHGZPoseSearchFeatureChannel_LoopPhase.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNode_Inertialization.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSyncScope.h"
#include "Animation/Skeleton.h"
#include "AnimPose.h"
#include "BlendStack/AnimNode_BlendStack.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "MHGZCharacter.h"
#include "PoseSearch/AnimNode_MotionMatching.h"
#include "PoseSearch/MotionMatchingAnimNodeLibrary.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchDerivedData.h"
#include "PoseSearch/PoseSearchContext.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "UObject/UnrealType.h"

namespace
{
// Real source poses exposed through the engine's history contract. No fake query
// vectors or manufactured search winners; trajectory intentionally stays flat.
struct FSourcePoseHistory : UE::PoseSearch::IPoseHistory
{
	UAnimSequence* Sequence;
	float SourceTime;
	mutable TMap<float, FAnimPose> Poses;
	UE::PoseSearch::FBoneToTransformMap Bones;
	TArray<FName> Curves;
	UE::PoseSearch::FPoseHistoryEntry Entry;
	FTransformTrajectory Trajectory;
	FSourcePoseHistory(UAnimSequence* InSequence, float Time) : Sequence(InSequence), SourceTime(Time)
	{
		for (int32 I = 0; I < Sequence->GetSkeleton()->GetReferenceSkeleton().GetNum(); ++I)
		{ Bones.Add(FBoneIndexType(I), UE::PoseSearch::FComponentSpaceTransformIndex(I)); }
		for (float T : { -1.f, 0.f, 1.f }) { FTransformTrajectorySample S; S.TimeInSeconds = T; Trajectory.Samples.Add(S); }
	}
	virtual bool GetTransformAtTime(float Time, FTransform& Out, const USkeleton* Skeleton,
		FBoneIndexType Bone, FBoneIndexType Reference, bool bExtrapolate) const override
	{
		const auto& Ref = Sequence->GetSkeleton()->GetReferenceSkeleton();
		if (int32(Bone) >= Ref.GetNum()) { return false; }
		FAnimPose* Pose = Poses.Find(Time);
		if (!Pose)
		{
			Pose = &Poses.Add(Time);
			UAnimPoseExtensions::GetAnimPoseAtTime(Sequence,
				FMath::Clamp(SourceTime + Time, 0.f, Sequence->GetPlayLength()), FAnimPoseEvaluationOptions(), *Pose);
		}
		Out = UAnimPoseExtensions::GetBonePose(*Pose, Ref.GetBoneName(Bone), EAnimPoseSpaces::World);
		if (Reference == UE::PoseSearch::RootBoneIndexType)
		{ Out = Out.GetRelativeTransform(UAnimPoseExtensions::GetBonePose(*Pose, Ref.GetBoneName(0), EAnimPoseSpaces::World)); }
		return true;
	}
	virtual bool GetCurveValueAtTime(float, const FName&, float& Value, bool) const override { Value = 0.f; return true; }
	virtual const FTransformTrajectory& GetTrajectory() const override { return Trajectory; }
	virtual void SetTrajectory(const FTransformTrajectory& Value, float) override { Trajectory = Value; }
	virtual void GenerateTrajectory(const UObject*, float) override {}
	virtual float GetTrajectorySpeedMultiplier() const override { return 1.f; }
	virtual bool IsEmpty() const override { return false; }
	virtual const UE::PoseSearch::FBoneToTransformMap& GetBoneToTransformMap() const override { return Bones; }
	virtual const TConstArrayView<FName> GetCollectedCurves() const override { return Curves; }
	virtual int32 GetNumEntries() const override { return 2; }
	virtual const UE::PoseSearch::FPoseHistoryEntry& GetEntry(int32) const override { return Entry; }
#if ENABLE_DRAW_DEBUG && ENABLE_ANIM_DEBUG
	virtual void DebugDraw(const UWorld*, FColor) const override {}
	virtual void DebugDraw(FAnimInstanceProxy&, FColor) const override {}
#endif
};

struct FContinuityProxyAccess : UAnimInstance
{
	static FAnimInstanceProxy& Read(UAnimInstance& Anim) { return *GetProxyOnGameThreadStatic<FAnimInstanceProxy>(&Anim); }
};
struct FContinuityProxyMethods : FAnimInstanceProxy
{
	static void Prepare(FAnimInstanceProxy& Proxy, UAnimInstance* Anim, float Delta)
	{ auto Pre = &FContinuityProxyMethods::PreUpdate; (Proxy.*Pre)(Anim, Delta); }
};
struct FRapidLoopStack : FAnimNode_BlendStack_Standalone
{
	UAnimSequence* Requested = nullptr;
	float Time = 0.f;
	virtual void UpdateAssetPlayer(const FAnimationUpdateContext& Context) override
	{
		if (Requested)
		{
			BlendTo(Context, Requested, Time, true, false, nullptr, .05f, nullptr, EAlphaBlendOption::Linear, true);
			Requested = nullptr;
		}
		FAnimNode_BlendStack_Standalone::UpdateAssetPlayer(Context);
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZLoopContinuityTest,
	"MHGZ.Maintenance.Locomotion.LoopContinuity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZLoopContinuityTest::RunTest(const FString& Parameters)
{
	FMemMark Mark(FMemStack::Get());
	using namespace UE::PoseSearch;
	using Channel = UMHGZPoseSearchFeatureChannel_LoopPhase;
	for (float Delta : { 1.f/60.f, 1.f/120.f, .2f/120.f })
	{
		FMHGZLocomotionPhaseClock Clock;
		Clock.Seed(.4f, 50.f/60.f);
		double Expected = Clock.Cycle;
		for (int32 Frame = 0; Frame < 180; ++Frame)
		{
			Clock.Advance();
			TestTrue(TEXT("Rapid reversal and slomo retain unquantized phase"), FMath::Abs(Clock.Cycle - Expected) < .00001);
			const float Length = Frame % 2 ? 40.f/60.f : 50.f/60.f;
			Clock.Schedule(Delta, Length);
			Expected = FMath::Frac(Expected + double(Delta)/Length);
		}
	}
	UClass* CharacterClass = LoadClass<AMHGZCharacter>(nullptr, TEXT(
		"/Game/Blueprints/Characters/Demo/BP_IG_Character.BP_IG_Character_C"));
	if (!TestNotNull(TEXT("Real character"), CharacterClass)) { return false; }
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	auto* Character = World->SpawnActor<AMHGZCharacter>(CharacterClass);
	auto* Mesh = Character->GetMesh();
	Mesh->InitAnim(true);
	auto* Anim = Cast<UMHGZMotionMatchingAnimInstance>(Mesh->GetAnimInstance());
	if (!TestNotNull(TEXT("Real ABP instance"), Anim)) { World->DestroyWorld(false); return false; }
	FAnimNode_MotionMatching* MM = nullptr;
	for (TFieldIterator<FStructProperty> It(Anim->GetClass()); It; ++It)
	{ if (It->Struct == FAnimNode_MotionMatching::StaticStruct()) { MM = It->ContainerPtrToValuePtr<FAnimNode_MotionMatching>(Anim); break; } }
	if (!TestNotNull(TEXT("Real MM node"), MM)) { World->DestroyWorld(false); return false; }
	const FAnimNodeReference Reference(Anim, *MM);
	EAnimNodeReferenceConversionResult Conversion;
	const auto MMRef = UMotionMatchingAnimNodeLibrary::ConvertToMotionMatchingNode(Reference, Conversion);
	const auto* StateProp = FindFProperty<FStructProperty>(FAnimNode_MotionMatching::StaticStruct(), TEXT("MotionMatchingState"));
	auto* State = StateProp->ContainerPtrToValuePtr<FMotionMatchingState>(MM);
	const FHistoricalPoseIndex Ordinary { 0, FObjectKey(Anim->MMSheathedMoveDatabase.Get()) };
	const FHistoricalPoseIndex Loop { 0, FObjectKey(Anim->MMSheathedSprintLoopOnlyDatabase.Get()) };
	State->PoseIndicesHistory.IndexToTime.Add(Ordinary, .1f);
	State->PoseIndicesHistory.IndexToTime.Add(Loop, .1f);
	Anim->bMMHasLocomotionInput = true;
	Anim->bMMForceIdle = Anim->bStartQueryActive = Anim->bMMStopRequestActive = Anim->bMMUnsheathedLocomotion = false;
	Anim->MMLoopClock.Seed(.45f, 50.f/60.f);
	Anim->PrepareLoopContinuity(FAnimUpdateContext(), Reference, EMHGZMotionMatchingNode::Sheathed);
	TestTrue(TEXT("Invalid search result cannot erase a valid cycle clock"), Anim->MMLoopPhaseQuery.SizeSquared() > .9);
	Anim->InstallLoopDatabase(MMRef, EMHGZMotionMatchingNode::Sheathed, Anim->MMSheathedSprintLoopOnlyDatabase, false);
	TestFalse(TEXT("Only target Loop history is removed"), State->PoseIndicesHistory.IndexToTime.Contains(Loop));
	TestTrue(TEXT("Ordinary Stop history survives"), State->PoseIndicesHistory.IndexToTime.Contains(Ordinary));
	const auto* InertiaProperty = FindFProperty<FBoolProperty>(FAnimNode_MotionMatching::StaticStruct(), TEXT("bUseInertialBlend"));
	TestFalse(TEXT("An entrance without an active gait loop does not request inertial root switching"), InertiaProperty->GetPropertyValue_InContainer(MM));
	Anim->bMMStopRequestActive = true;
	Anim->PrepareLoopContinuity(FAnimUpdateContext(), Reference, EMHGZMotionMatchingNode::Sheathed);
	TestFalse(TEXT("Stop restores authored non-inertial blending"), InertiaProperty->GetPropertyValue_InContainer(MM));
	TestFalse(TEXT("Release retires the Loop clock immediately"), Anim->MMLoopClock.bValid);
	Anim->bMMStopRequestActive = false;

	UAnimSequence* Run = CastChecked<UAnimSequence>(Anim->MMSheathedRunLoopSequence);
	UAnimSequence* Sprint = CastChecked<UAnimSequence>(Anim->MMSheathedSprintLoopSequence);
	FSourcePoseHistory RunHistory(Run, .4265f);
	FChooserEvaluationContext Role(Anim);
	UPoseSearchDatabase* SprintDB = Anim->MMSheathedSprintLoopOnlyDatabase;
	TestEqual(TEXT("Sprint index ready"), FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(SprintDB,
		ERequestAsyncBuildFlag::NewRequest | ERequestAsyncBuildFlag::WaitForCompletion), EAsyncBuildIndexResult::Success);
	for (int32 Step = 0; Step < 80; ++Step)
	{
		Anim->InstallLoopDatabase(MMRef, EMHGZMotionMatchingNode::Sheathed, SprintDB, false);
		const float Cycle = Step/80.f;
		Anim->MMLoopPhaseQuery = Channel::EncodePhase(Cycle, 1.f);
		FSearchContext Context(0.f, nullptr, FSearchResult(), FFloatInterval(0.f,0.f), FPoseSearchEvent());
		Context.AddRole(DefaultRole, &Role, &RunHistory);
		const FSearchResult Result = SprintDB->Search(Context);
		if (!TestTrue(TEXT("Every cycle has an actual legal search winner"), Result.IsValid())) { break; }
		const float SelectedCycle = SprintDB->GetRealAssetTime(Result.PoseIdx)/Sprint->GetPlayLength();
		const float Error = FMath::Abs(FMath::UnwindRadians((SelectedCycle-Cycle)*2.f*PI))/(2.f*PI);
		TestTrue(TEXT("Trajectory cannot steal a different phase"), Error <= .5f/(60.f*Sprint->GetPlayLength())+.0001f);
	}

	Anim->SetRootMotionMode(ERootMotionMode::RootMotionFromEverything);
	Mesh->TickAnimation(1.f/60.f,false); Mesh->RefreshBoneTransforms();
	auto& Proxy = FContinuityProxyAccess::Read(*Anim);
	struct FExitCase { const TCHAR* Source; float Time; UPoseSearchDatabase* DB; bool bArmed; };
	const FExitCase Exits[] = {
		{ TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Locomotion/Transitions/Exit/AS_Shth_Sheathe_MoveExit"), .2701f, SprintDB, false },
		{ TEXT("/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/Transitions/Exit/AS_Shth_Dodge_MoveExit"), .1919f, Anim->MMSheathedRunLoopOnlyDatabase, false },
		{ TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Locomotion/Transitions/Exit/AS_UnSh_Dodge_Forward_MoveExit"), .4879f, Anim->MMUnsheathedLoopOnlyDatabase, true },
		{ TEXT("/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/AS_Shth_Run_Start"), 1.1081f, Anim->MMSheathedRunLoopOnlyDatabase, false },
		{ TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Locomotion/AS_UnSh_Walk_Start"), .8905f, Anim->MMUnsheathedLoopOnlyDatabase, true }
	};
	for (const auto& Exit : Exits)
	{
		auto* Source = LoadObject<UAnimSequence>(nullptr, Exit.Source);
		if (!TestNotNull(TEXT("Recorded Exit source"), Source)) { continue; }
		FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(Exit.DB,
			ERequestAsyncBuildFlag::NewRequest | ERequestAsyncBuildFlag::WaitForCompletion);
		FSourcePoseHistory History(Source, Exit.Time);
		Anim->InstallLoopDatabase(MMRef, Exit.bArmed ? EMHGZMotionMatchingNode::Unsheathed : EMHGZMotionMatchingNode::Sheathed, Exit.DB, true);
		FSearchContext Context(0.f,nullptr,FSearchResult(),FFloatInterval(0.f,0.f),FPoseSearchEvent());
		Context.AddRole(DefaultRole,&Role,&History);
		const FSearchResult Result = Exit.DB->Search(Context);
		if (!TestTrue(TEXT("Exit has a valid Loop handoff"), Result.IsValid())) { continue; }
		const auto* Phase = Exit.DB->Schema->FindChannel([](const UPoseSearchFeatureChannel* C)
			-> const Channel* { return Cast<Channel>(C); });
		const auto Query = Context.GetCachedQuery(Exit.DB->Schema).Slice(Phase->GetChannelDataOffset(),17);
		TArray<float> Scratch;
		const float WinnerCost = Channel::EntryMatchCost(
			Exit.DB->GetSearchIndex().GetPoseValuesSafe(Result.PoseIdx,Scratch).Slice(Phase->GetChannelDataOffset(),17),Query,.02f,.05f);
		for (int32 I = 0; I < Exit.DB->GetSearchIndex().GetNumPoses(); ++I)
		{
			const float Cost = Channel::EntryMatchCost(Exit.DB->GetSearchIndex().GetPoseValuesSafe(I,Scratch)
				.Slice(Phase->GetChannelDataOffset(),17),Query,.02f,.05f);
			TestTrue(TEXT("Native winner retains minimum feet and locomotion velocity entrance error"), WinnerCost <= Cost+.001f);
		}
		AddInfo(FString::Printf(TEXT("Exit %s@%.4f -> Loop@%.4f foot cost %.3f"),
			*Source->GetName(),Exit.Time,Exit.DB->GetRealAssetTime(Result.PoseIdx),WinnerCost));
		const auto* TargetEntry = Exit.DB->GetDatabaseAnimationAsset<FPoseSearchDatabaseSequence>(0);
		UAnimSequence* Target = TargetEntry->Sequence;
		const float TargetTime = Exit.DB->GetRealAssetTime(Result.PoseIdx);
		for (float Delta : { 1.f/60.f, 1.f/120.f, .2f/120.f })
		{
			auto MinimumRootSpeed = [&](float Duration)
			{
				FAnimNode_BlendStack_Standalone Stack;
				Stack.SetMaxActiveBlends(2);
				Stack.Initialize_AnyThread(FAnimationInitializeContext(&Proxy));
				Stack.CacheBones_AnyThread(FAnimationCacheBonesContext(&Proxy));
				float Minimum = MAX_flt;
				for (int32 Frame = 0; Frame < FMath::CeilToInt(.12f/Delta)+2; ++Frame)
				{
					FContinuityProxyMethods::Prepare(Proxy,Anim,Delta);
					Proxy.GetExtractedRootMotion().Clear();
					FAnimationUpdateSharedContext Shared;
					FAnimationUpdateContext Update(&Proxy,Delta,&Shared);
					UE::Anim::TScopedGraphMessage<UE::Anim::FAnimSyncGroupScope> Sync(Update,Update);
					if (Frame == 0) { Stack.BlendTo(Update,Source,Exit.Time-Delta,false,false,nullptr,0.f); }
					if (Frame == 1) { Stack.BlendTo(Update,Target,TargetTime,true,false,nullptr,Duration); }
					Stack.Update_AnyThread(Update);
					PRAGMA_DISABLE_DEPRECATION_WARNINGS
					Proxy.TickAssetPlayerInstances();
					PRAGMA_ENABLE_DEPRECATION_WARNINGS
					FPoseContext Pose(&Proxy);
					Stack.Evaluate_AnyThread(Pose);
					if (Frame >= 1) { Minimum = FMath::Min(Minimum,
						float(Proxy.GetExtractedRootMotion().GetRootMotionTransform().GetTranslation().Size()/Delta)); }
				}
				return Minimum;
			};
			const float Duration = FMHGZLocomotionBlendPolicy::EntryDuration(.1f,Source->GetPlayLength(),Exit.Time,Delta);
			const float GoodMinimum = MinimumRootSpeed(Duration);
			TestTrue(TEXT("Actual Start/Exit blend avoids a root-velocity hole at 60/120/slomo updates"),GoodMinimum > 250.f);
			if (Source->GetName().Contains(TEXT("_Start")))
			{
				const float OldMinimum = MinimumRootSpeed(.1f);
				TestTrue(TEXT("Negative control reproduces the long-fade source-end root dip"),OldMinimum < GoodMinimum * .8f);
			}
		}
	}

	// Real inertial node and real Root Motion, with a reversal every animation update.
	FAnimNode_Inertialization* LiveInertia = nullptr;
	for (TFieldIterator<FStructProperty> It(Anim->GetClass()); It; ++It)
	{
		if (It->Struct == FAnimNode_Inertialization::StaticStruct())
		{ LiveInertia = It->ContainerPtrToValuePtr<FAnimNode_Inertialization>(Anim); break; }
	}
	if (!TestNotNull(TEXT("Compiled inertial node with valid folded NodeData"), LiveInertia))
	{ World->DestroyWorld(false); return false; }
	const FPoseLink OriginalSource = LiveInertia->Source;
	for (float Delta : { 1.f/60.f, .2f/120.f })
	{
		FRapidLoopStack Stack;
		Stack.SetMaxActiveBlends(2);
		// Inertialization has folded parameters and is not a standalone native node.
		// Use this transient ABP instance's compiled node, never edit its asset/CDO.
		FAnimNode_Inertialization& Inertia = *LiveInertia;
		Inertia.Source.SetLinkNode(&Stack);
		Inertia.Initialize_AnyThread(FAnimationInitializeContext(&Proxy));
		Inertia.CacheBones_AnyThread(FAnimationCacheBonesContext(&Proxy));
		for (int32 Frame = 0; Frame < 16; ++Frame)
		{
			FContinuityProxyMethods::Prepare(Proxy,Anim,Delta);
			Proxy.GetExtractedRootMotion().Clear();
			FAnimationUpdateSharedContext Shared;
			FAnimationUpdateContext Context(&Proxy,Delta,&Shared);
			UE::Anim::TScopedGraphMessage<UE::Anim::FAnimSyncGroupScope> Sync(Context,Context);
			Stack.Requested = Frame%2 ? Sprint : Run;
			Stack.Time = .2f * Stack.Requested->GetPlayLength();
			Inertia.Update_AnyThread(Context);
			PRAGMA_DISABLE_DEPRECATION_WARNINGS
			Proxy.TickAssetPlayerInstances();
			PRAGMA_ENABLE_DEPRECATION_WARNINGS
			FPoseContext Pose(&Proxy);
			Inertia.Evaluate_AnyThread(Pose);
			TestTrue(TEXT("Rapid reversals do not accumulate frozen blend layers"), Stack.AnimPlayers.Num()<=2);
			TestTrue(TEXT("Rapid reversal keeps nonzero extracted Root Motion"),
				Proxy.GetExtractedRootMotion().GetRootMotionTransform().GetTranslation().Size()>.1f);
		}
		Inertia.Source = OriginalSource;
	}
	World->DestroyWorld(false);
	return true;
}
#endif
