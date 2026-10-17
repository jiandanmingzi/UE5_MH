// Copyright MHGZ Project. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/MHGZMontageSegmentBlend.h"
#include "Animation/MHGZMotionMatchingAnimInstance.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "MHGZAnimationSeamLibrary.h"
#include "MHGZCharacter.h"
#include "Misc/AutomationTest.h"

namespace
{
// Static protected-access bridge only; never constructed or used as a reflected object.
struct FProxyAccess : UAnimInstance
{
	static FAnimInstanceProxy& Read(UAnimInstance& Anim)
	{ return *GetProxyOnGameThreadStatic<FAnimInstanceProxy>(&Anim); }
};

UAnimMontage* MakeMontage(UAnimSequence* A, UAnimSequence* B, bool bConfigured = true)
{
	UAnimMontage* Montage = NewObject<UAnimMontage>(GetTransientPackage());
	Montage->SetSkeleton(A->GetSkeleton());
	Montage->BlendIn.SetBlendTime(0.0f);
	Montage->BlendOut.SetBlendTime(0.0f);
	Montage->bEnableAutoBlendOut = false;
	Montage->SlotAnimTracks.Reset();
	FSlotAnimationTrack& Slot = Montage->SlotAnimTracks.AddDefaulted_GetRef();
	Slot.SlotName = TEXT("DefaultSlot");
	for (UAnimSequence* Sequence : { A, B })
	{
		const float Start = Slot.AnimTrack.GetLength();
		FAnimSegment& Segment = Slot.AnimTrack.AnimSegments.AddDefaulted_GetRef();
		Segment.SetAnimReference(Sequence, true);
		Segment.StartPos = Start;
		Segment.AnimStartTime = 0.0f;
		Segment.AnimEndTime = 0.4f;
		Segment.AnimPlayRate = 1.0f;
		Segment.LoopingCount = 1;
	}
	Montage->SetCompositeLength(Montage->CalculateSequenceLength());
	FCompositeSection& Section = Montage->CompositeSections.AddDefaulted_GetRef();
	Section.SectionName = TEXT("Default");
	Section.Link(Montage, 0.0f, 0);
	Montage->RefreshCacheData();
	if (bConfigured)
	{
		auto* Settings = NewObject<UMHGZMontageSegmentBlendSettings>(Montage);
		Settings->Duration = .06f; // Fixture's explicit override, independent of the production default.
		Montage->AddMetaData(Settings);
	}
	return Montage;
}

void Tick(USkeletalMeshComponent& Mesh, float Delta)
{
	Mesh.TickAnimation(Delta, false);
	Mesh.RefreshBoneTransforms();
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSegmentBlendPolicyTest,
	"MHGZ.Maintenance.Animation.SegmentBlend.Policy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSegmentBlendPolicyTest::RunTest(const FString& Parameters)
{
	UAnimMontage* Montage = NewObject<UAnimMontage>();
	UAnimSequence* A = NewObject<UAnimSequence>();
	UAnimSequence* B = NewObject<UAnimSequence>();
	FMHGZMontageSegmentBlendSample Old;
	Old.Montage = Montage; Old.Sequence = A; Old.InstanceID = 1; Old.SegmentIndex = 0;
	Old.SlotName = TEXT("DefaultSlot"); Old.GroupName = TEXT("DefaultGroup");
	Old.Duration = 0.06f; Old.Weight = 1.0f;
	FMHGZMontageSegmentBlendSample New = Old;
	New.Sequence = B; New.SegmentIndex = 1;
	TArray<FMHGZMontageSegmentBlendSample> Before { Old }, After { New };
	TMap<FName, float> Requests;
	FMHGZMontageSegmentBlend::BuildRequests(Before, After, Requests);
	TestEqual(TEXT("Different sequence requests exactly one group"), Requests.Num(), 1);
	TestEqual(TEXT("Duration is actual seconds"), Requests.FindRef(Old.GroupName), 0.06f);
	FMHGZMontageSegmentBlend::BuildRequests(After, After, Requests);
	TestTrue(TEXT("Next update does not retrigger"), Requests.IsEmpty());
	After[0].Sequence = A;
	FMHGZMontageSegmentBlend::BuildRequests(Before, After, Requests);
	TestTrue(TEXT("Same-sequence trim or loop does not retrigger"), Requests.IsEmpty());
	After[0] = New; After[0].InstanceID = 2;
	FMHGZMontageSegmentBlend::BuildRequests(Before, After, Requests);
	TestTrue(TEXT("New/restarted instance is not an internal seam"), Requests.IsEmpty());
	After[0] = New; After[0].Weight = 0.0f;
	FMHGZMontageSegmentBlend::BuildRequests(Before, After, Requests);
	TestTrue(TEXT("Invisible zero-weight instance does not request"), Requests.IsEmpty());
	After[0] = New; After[0].Duration = 0.0f;
	FMHGZMontageSegmentBlend::BuildRequests(Before, After, Requests);
	TestTrue(TEXT("Zero duration disables"), Requests.IsEmpty());
	After[0] = New; After.Add(New);
	FMHGZMontageSegmentBlend::BuildRequests(Before, After, Requests);
	TestEqual(TEXT("Multi-slot/group events are coalesced"), Requests.Num(), 1);
	FMHGZMontageSegmentBlend::BuildRequests({}, After, Requests);
	TestTrue(TEXT("No previous sample means no startup request"), Requests.IsEmpty());
	FMHGZMontageSegmentBlend::BuildRequests(Before, {}, Requests);
	TestTrue(TEXT("Stopped/removed instance means no request"), Requests.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSegmentBlendGraphTest,
	"MHGZ.Maintenance.Animation.SegmentBlend.Graph",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSegmentBlendGraphTest::RunTest(const FString& Parameters)
{
	UAnimBlueprint* Blueprint = LoadObject<UAnimBlueprint>(nullptr,
		TEXT("/Game/Blueprints/Characters/Demo/Animation/ABP_MH_Character.ABP_MH_Character"));
	if (!TestNotNull(TEXT("Actual AnimBlueprint"), Blueprint)) { return false; }
	TestTrue(TEXT("Both real action Slots have downstream inertialization"),
		UMHGZAnimationSeamLibrary::InstallMontageSegmentInertialization(Blueprint, false));
	TestTrue(TEXT("Reapplying is an idempotent no-op"),
		UMHGZAnimationSeamLibrary::InstallMontageSegmentInertialization(Blueprint, true));
	TestFalse(TEXT("Audit/no-op does not dirty the asset"), Blueprint->GetOutermost()->IsDirty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSegmentBlendRuntimeTest,
	"MHGZ.Maintenance.Animation.SegmentBlend.Runtime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSegmentBlendRuntimeTest::RunTest(const FString& Parameters)
{
	UAnimSequence* A = LoadObject<UAnimSequence>(nullptr,
		TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_ShangLao.AS_Unsh_ShangLao"));
	UAnimSequence* B = LoadObject<UAnimSequence>(nullptr,
		TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_JiaSha.AS_Unsh_JiaSha"));
	UClass* CharacterClass = LoadClass<AMHGZCharacter>(nullptr,
		TEXT("/Game/Blueprints/Characters/Demo/BP_IG_Character.BP_IG_Character_C"));
	if (!TestNotNull(TEXT("Source A"), A) || !TestNotNull(TEXT("Source B"), B)
		|| !TestNotNull(TEXT("Character class"), CharacterClass)) { return false; }
	for (float Delta : { 1.0f / 60.0f, 1.0f / 120.0f })
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		AMHGZCharacter* Character = World->SpawnActor<AMHGZCharacter>(CharacterClass);
		if (!TestNotNull(TEXT("Transient character"), Character)) { World->DestroyWorld(false); return false; }
		USkeletalMeshComponent* Mesh = Character->GetMesh();
		Mesh->InitAnim(true);
		UMHGZMotionMatchingAnimInstance* Anim = Cast<UMHGZMotionMatchingAnimInstance>(Mesh->GetAnimInstance());
		if (!TestNotNull(TEXT("Actual ABP instance"), Anim)) { World->DestroyWorld(false); return false; }
		Tick(*Mesh, Delta);
		UAnimMontage* Montage = MakeMontage(A, B);
		const float SeamTime = Montage->SlotAnimTracks[0].AnimTrack.AnimSegments[1].StartPos;
		const float OriginalLength = Montage->GetPlayLength();
		const FTransform OriginalRootMotion = Montage->ExtractRootMotionFromTrackRange(0.0f, OriginalLength, FAnimExtractContext());
		TestTrue(TEXT("Transient configured montage starts"), Anim->Montage_Play(Montage) > 0.0f);
		int64 PreviousSerial = Anim->MontageSegmentBlendRequestSerial;
		bool bObserved = false;
		for (int32 Frame = 0; Frame < 40; ++Frame)
		{
			// Observe while proxy object references still exist. PostEvaluate clears
			// Skeleton, so slot-to-group lookup after RefreshBoneTransforms is invalid.
			Mesh->TickAnimation(Delta, false);
			const int64 Serial = Anim->MontageSegmentBlendRequestSerial;
			FInertializationRequest Request;
			const bool bRequested = FProxyAccess::Read(*Anim).GetSlotInertializationRequestData(TEXT("DefaultSlot"), Request);
			if (Serial != PreviousSerial)
			{
				TestEqual(TEXT("Exactly one request at the seam"), Serial - PreviousSerial, int64(1));
				TestTrue(TEXT("Request reaches current graph update"), bRequested);
				TestEqual(TEXT("Proxy preserves the requested duration"), Request.Duration, 0.06f);
				const float Position = Anim->Montage_GetPosition(Montage);
				TestTrue(TEXT("Request is at actual switch, not prediction/next frame"),
					Position >= SeamTime && Position <= SeamTime + Delta + KINDA_SMALL_NUMBER);
				bObserved = true;
			}
			else { TestFalse(TEXT("No duplicate request on subsequent updates"), bRequested); }
			Mesh->RefreshBoneTransforms();
			PreviousSerial = Serial;
			if (bObserved && Anim->Montage_GetPosition(Montage) > SeamTime + 0.08f) { break; }
		}
		TestTrue(TEXT("Real playback crossed the seam"), bObserved);
		TestEqual(TEXT("Montage duration unchanged"), Montage->GetPlayLength(), OriginalLength);
		TestTrue(TEXT("Root Motion extraction unchanged"), Montage->ExtractRootMotionFromTrackRange(
			0.0f, OriginalLength, FAnimExtractContext()).Equals(OriginalRootMotion));
		FAnimMontageInstance* Instance = Anim->GetActiveInstanceForMontage(Montage);
		if (TestNotNull(TEXT("Instance remains active"), Instance))
		{
			const int64 SeekSerial = Anim->MontageSegmentBlendRequestSerial;
			// Simulate JumpToSection/seek occurring outside Montage_Advance.
			Instance->SetPosition(0.0f);
			Mesh->TickAnimation(Delta, false);
			FInertializationRequest SeekRequest;
			TestEqual(TEXT("External seek to a different sequence requests once"),
				Anim->MontageSegmentBlendRequestSerial, SeekSerial + 1);
			TestTrue(TEXT("Seek request also arrives this update"),
				FProxyAccess::Read(*Anim).GetSlotInertializationRequestData(TEXT("DefaultSlot"), SeekRequest));
			Mesh->RefreshBoneTransforms();
			Mesh->TickAnimation(Delta, false);
			TestFalse(TEXT("Seek does not leave a next-frame request"),
				FProxyAccess::Read(*Anim).GetSlotInertializationRequestData(TEXT("DefaultSlot"), SeekRequest));
			Mesh->RefreshBoneTransforms();
		}
		const int64 FinalSerial = Anim->MontageSegmentBlendRequestSerial;
		Anim->Montage_Pause(Montage); Tick(*Mesh, Delta);
		TestEqual(TEXT("Paused montage does not request"), Anim->MontageSegmentBlendRequestSerial, FinalSerial);
		Anim->Montage_Stop(0.0f); Tick(*Mesh, Delta);
		UAnimMontage* Legacy = MakeMontage(A, B, false);
		Anim->Montage_Play(Legacy);
		for (int32 Frame = 0; Frame < 32; ++Frame) { Tick(*Mesh, Delta); }
		TestEqual(TEXT("Unconfigured Montage keeps legacy behavior"), Anim->MontageSegmentBlendRequestSerial, FinalSerial);
		Anim->Montage_Stop(0.0f);
		World->DestroyWorld(false);
	}
	return true;
}
#endif
