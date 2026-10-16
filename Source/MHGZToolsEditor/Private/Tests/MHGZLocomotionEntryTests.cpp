// Copyright MHGZ Project. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/MHGZMotionMatchingAnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimSyncScope.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "EditorFramework/AssetImportData.h"
#include "MHGZCharacter.h"
#include "MHGZAnimationSeamLibrary.h"
#include "PoseSearch/AnimNode_MotionMatching.h"
#include "PoseSearch/MotionMatchingAnimNodeLibrary.h"
#include "UObject/UnrealType.h"

namespace
{
struct FEntryProxyAccess : UAnimInstance
{
	static FAnimInstanceProxy& Read(UAnimInstance& Anim) { return *GetProxyOnGameThreadStatic<FAnimInstanceProxy>(&Anim); }
};
struct FEntryProxyMethods : FAnimInstanceProxy
{
	static void Prepare(FAnimInstanceProxy& Proxy,UAnimInstance* Anim,float Delta)
	{ auto Pre=&FEntryProxyMethods::PreUpdate; (Proxy.*Pre)(Anim,Delta); }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZLocomotionEntryTest,
	"MHGZ.Maintenance.Locomotion.EntryAndClosure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZLocomotionEntryTest::RunTest(const FString& Parameters)
{
	FMemMark Mark(FMemStack::Get());
	for (float Delta : { 1.f/60.f, 1.f/120.f, .2f/120.f })
	{
		const float Duration = FMHGZLocomotionBlendPolicy::EntryDuration(.1f,1.15f,1.1081f,Delta);
		TestTrue(TEXT("Source does not reach its end with residual crossfade weight"), Duration + Delta < 1.15f-1.1081f);
		TestTrue(TEXT("Entry crossfade remains nonzero at normal/slomo rates"), Duration > 0);
	}
	TestEqual(TEXT("Exhausted source cannot carry a long ordinary fade"), FMHGZLocomotionBlendPolicy::EntryDuration(.1f,1.f,1.f,.01f),0.f);
	for (const TCHAR* Path : {
		TEXT("/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/AS_Shth_Run_Loop"),
		TEXT("/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/AS_Shth_Sprint_Loop_125x"),
		TEXT("/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/AS_Shth_Walk_Loop"),
		TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Locomotion/AS_UnSh_Walk_Loop") })
	{
		auto* Source = LoadObject<UAnimSequence>(nullptr,Path);
		if (!TestNotNull(TEXT("Actual Loop source"),Source)) { return false; }
		// Only a transient duplicate is authored by the test. No formal asset saves.
		auto* Copy = DuplicateObject<UAnimSequence>(Source,GetTransientPackage());
		if (Copy->AssetImportData) { Copy->AssetImportData->ConditionalPostLoad(); }
		const float Length = Copy->GetPlayLength();
		const auto* Model = Source->GetDataModel();
		const int32 N = Model->GetNumberOfFrames();
		const FName Root = Source->GetSkeleton()->GetReferenceSkeleton().GetBoneName(0);
		TArray<FName> Names;
		Model->GetBoneTrackNames(Names);
		TestTrue(TEXT("Local boundary closure succeeds with the actual data-model controller"), UMHGZAnimationSeamLibrary::CloseLocomotionLoop(Copy,.1f));
		TestEqual(TEXT("Duration is not stretched or padded"),Copy->GetPlayLength(),Length);
		for (FName Name : Names)
		{
			TArray<FTransform> Old,New;
			Model->GetBoneTrackTransforms(Name,Old);
			Copy->GetDataModel()->GetBoneTrackTransforms(Name,New);
			if (!TestEqual(TEXT("No key count change"),New.Num(),Old.Num())) { continue; }
			TestTrue(TEXT("Interior original motion is untouched"),New[N/2].Equals(Old[N/2],.0001));
			TestTrue(TEXT("Loop translation velocity closes"),
				(New[1].GetTranslation()-New[0].GetTranslation()).Equals(New[N].GetTranslation()-New[N-1].GetTranslation(),.0002));
			if (Name == Root)
			{
				TestTrue(TEXT("Accumulated Root Motion and root start/end positions survive"),
					New[0].Equals(Old[0],.0001) && New[N].Equals(Old[N],.0001));
			}
			else
			{
				TestTrue(TEXT("Non-root local pose closes without an inserted hold frame"), New[0].Equals(New[N],.0002));
				TestTrue(TEXT("Angular velocity closes as well as endpoint pose"),
					(New[1].GetRotation()*New[0].GetRotation().Inverse()).Equals(New[N].GetRotation()*New[N-1].GetRotation().Inverse(),.0002));
			}
		}
	}
	UClass* CharacterClass=LoadClass<AMHGZCharacter>(nullptr,TEXT("/Game/Blueprints/Characters/Demo/BP_IG_Character.BP_IG_Character_C"));
	UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
	auto* Character=World->SpawnActor<AMHGZCharacter>(CharacterClass);
	auto* Mesh=Character->GetMesh();
	Mesh->InitAnim(true);
	Mesh->TickAnimation(1.f/120.f,false);
	Mesh->RefreshBoneTransforms();
	auto* Anim=CastChecked<UMHGZMotionMatchingAnimInstance>(Mesh->GetAnimInstance());
	auto& Proxy=FEntryProxyAccess::Read(*Anim);
	FEntryProxyMethods::Prepare(Proxy,Anim,1.f/120.f);
	FAnimNode_MotionMatching* MM=nullptr;
	for (TFieldIterator<FStructProperty> It(Anim->GetClass());It;++It)
	{ if (It->Struct==FAnimNode_MotionMatching::StaticStruct()) { MM=It->ContainerPtrToValuePtr<FAnimNode_MotionMatching>(Anim); break; } }
	if (!TestNotNull(TEXT("Live MM node"),MM)) { World->DestroyWorld(false); return false; }
	const FAnimNodeReference Ref(Anim,*MM);
	EAnimNodeReferenceConversionResult Conversion;
	const auto MMRef=UMotionMatchingAnimNodeLibrary::ConvertToMotionMatchingNode(Ref,Conversion);
	auto* Start=LoadObject<UAnimSequence>(nullptr,TEXT("/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/AS_Shth_Run_Start"));
	FAnimationUpdateSharedContext Shared;
	FAnimationUpdateContext Context(&Proxy,1.f/120.f,&Shared);
	// Captured GA resumes: a paused Start survived at these three source times,
	// but the action's measurement reset had retired its semantic input cycle.
	auto* ArmedStart=LoadObject<UAnimSequence>(nullptr,TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Locomotion/AS_UnSh_Walk_Start"));
	if (!TestNotNull(TEXT("Actual unsheathed Start"),ArmedStart)) { World->DestroyWorld(false); return false; }
	for (EMHGZMotionMatchingNode Node : { EMHGZMotionMatchingNode::Sheathed, EMHGZMotionMatchingNode::Unsheathed })
	{
		const int32 I=static_cast<int32>(Node);
		UAnimSequence* PausedStart=I==1 ? ArmedStart : Start;
		for (float PausedTime : { .3728f, .5005f, .4034f })
		{
			MM->BlendTo(Context,PausedStart,PausedTime,false,false,nullptr,0.f);
			Anim->bStartQueryObservedStart=true; // The interrupted pre-action cycle.
			Anim->ResetMMTemporalState(Character->GetActorLocation());
			Anim->bMMHasLocomotionInput=true;
			Anim->bMMForceIdle=false;
			Anim->bMMUnsheathedLocomotion=I==1;
			Anim->bMMHandoffActive=false;
			Anim->GetActionExitRoute(Node)=EMHGZMMActionExitRoute::Move;
			Anim->bMMMobileExitCruise[I]=false;
			Anim->PrepareLoopContinuity(FAnimUpdateContext(),Ref,Node);
			TestTrue(TEXT("GA reset leaves the engine's old Start player intact"),MM->GetAnimAsset()==PausedStart);
			TestFalse(TEXT("GA reset retires observation of the previous Start cycle"),Anim->bStartQueryObservedStart);
			TestFalse(TEXT("Paused pre-action Start cannot authorize Loop-only"),Anim->TryRouteStartToLoop(MMRef,Node));
			Anim->RouteMotionMatchingPreSearch(Ref,Node);
			TestNull(TEXT("First action-release callback does not install a Loop query"),Anim->GetLoopQueryDatabase(I==1));
			TestFalse(TEXT("No accidental cruise latch after action release"),Anim->bMMMobileExitCruise[I]);
			// The next ordinary input edge still requests a fresh Start, without a
			// new timer, forced-Idle hold, or waiting for the paused source to end.
			Anim->bStartQueryActive=true; // NativeUpdateAnimation arms the edge before either stance helper.
			if (I==1) { Anim->UpdateLegacyIntentQueries(true,true,false,1.f/120.f); }
			else { Anim->UpdateSheathedIntentQueries(true,true,false,1.f/120.f,460.f); }
			TestTrue(TEXT("Fresh input still arms Start"),Anim->bStartQueryActive);
			TestEqual(TEXT("Fresh Start uses positive semantic"),Anim->MMIntentQuery,1.f);
			TestFalse(TEXT("Fresh pending Start is not mistaken for completed Start"),Anim->TryRouteStartToLoop(MMRef,Node));
		}
	}
	Anim->ResetMMTemporalState(Character->GetActorLocation());
	MM->BlendTo(Context,Start,1.1081f,false,false,nullptr,0.f);
	Anim->bMMHasLocomotionInput=true;
	Anim->bMMForceIdle=Anim->bMMStopRequestActive=Anim->bMMUnsheathedLocomotion=false;
	Anim->MMSheathedTargetMoveGait=MHGZMotionMatching::EMHGZNormalMoveGait::Run;
	Anim->bStartQueryActive=true;
	Anim->PrepareLoopContinuity(FAnimUpdateContext(),Ref,EMHGZMotionMatchingNode::Sheathed);
	Anim->MMNodeUpdateDelta[0]=1.f/120.f;
	TestFalse(TEXT("Positive Start Intent never gets bypassed"),Anim->TryRouteStartToLoop(MMRef,EMHGZMotionMatchingNode::Sheathed));
	Anim->bStartQueryActive=false;
	Anim->bStartQueryObservedStart=true;
	TestTrue(TEXT("Neutral Start tail uses the actual Loop entrance route"),Anim->TryRouteStartToLoop(MMRef,EMHGZMotionMatchingNode::Sheathed));
	TestTrue(TEXT("Start receives final-pose entrance matching"),Anim->WantsLoopEntryMatch(false));
	const auto* DurationProperty=FindFProperty<FFloatProperty>(FAnimNode_MotionMatching::StaticStruct(),TEXT("BlendTime"));
	const auto* InertiaProperty=FindFProperty<FBoolProperty>(FAnimNode_MotionMatching::StaticStruct(),TEXT("bUseInertialBlend"));
	TestTrue(TEXT("Start root blend is bounded to its remaining playback"),DurationProperty->GetPropertyValue_InContainer(MM)<=.0336f);
	TestFalse(TEXT("Entry uses a live root crossfade, not inertial-only root switching"),InertiaProperty->GetPropertyValue_InContainer(MM));
	Anim->bMMStopRequestActive=true;
	TestFalse(TEXT("Release always wins over a Start entrance"),Anim->TryRouteStartToLoop(MMRef,EMHGZMotionMatchingNode::Sheathed));
	World->DestroyWorld(false);
	return true;
}
#endif
