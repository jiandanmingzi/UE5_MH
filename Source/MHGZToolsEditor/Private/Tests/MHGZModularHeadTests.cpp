// Copyright MHGZ Project. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "MHGZCharacter.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"

namespace
{
TArray<FTransform> ComponentPose(const FReferenceSkeleton& Skeleton, TArray<FTransform> Local)
{
	for (int32 Index = 0; Index < Local.Num(); ++Index)
	{
		const int32 Parent = Skeleton.GetParentIndex(Index);
		if (Parent != INDEX_NONE)
		{
			Local[Index] *= Local[Parent];
		}
	}
	return Local;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZModularHeadTest,
	"MHGZ.Maintenance.Character.ModularHead",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZModularHeadTest::RunTest(const FString& Parameters)
{
	UClass* CharacterClass = LoadClass<AMHGZCharacter>(nullptr,
		TEXT("/Game/Blueprints/Characters/Demo/BP_IG_Character.BP_IG_Character_C"));
	if (!TestNotNull(TEXT("Existing character Blueprint"), CharacterClass)) { return false; }
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	AMHGZCharacter* Character = World->SpawnActor<AMHGZCharacter>(CharacterClass);
	if (!TestNotNull(TEXT("Character instance"), Character))
	{
		World->DestroyWorld(false);
		return false;
	}
	USkeletalMeshComponent* Body = Character->GetMesh();
	const FReferenceSkeleton& BodySkeleton = Body->GetSkeletalMeshAsset()->GetRefSkeleton();
	const int32 BodyHead = BodySkeleton.FindBoneIndex(TEXT("Head_00"));
	const int32 BodyNeck = BodySkeleton.FindBoneIndex(TEXT("Neck_00"));
	Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	Body->SetComponentSpaceTransformsDoubleBuffering(false);
	const auto BodyReference = ComponentPose(BodySkeleton, BodySkeleton.GetRefBonePose());
	Body->GetEditableComponentSpaceTransforms() = BodyReference;
	Body->GetEditableBoneVisibilityStates().Init(BVS_Visible, BodyReference.Num());
	TestTrue(TEXT("Synthetic pose uses the readable buffer"),
		Body->GetComponentSpaceTransforms()[BodyHead].Equals(BodyReference[BodyHead]));

	for (USkeletalMeshComponent* Module : { Character->GetHeadMesh(), Character->GetHairMesh() })
	{
		if (!TestNotNull(TEXT("Modular mesh"), Module)) { continue; }
		const FString Label = Module->GetName();
		TestTrue(Label + TEXT(" attaches to the body, not an animated head socket"),
			Module->GetAttachParent() == Body && Module->GetAttachSocketName().IsNone());
		TestTrue(Label + TEXT(" shares component space"), Module->GetRelativeTransform().Equals(FTransform::Identity));
		TestTrue(Label + TEXT(" follows the body after Blueprint initialization"), Module->LeaderPoseComponent.Get() == Body);
		TestTrue(Label + TEXT(" has no independent AnimBP"),
			!Module->GetAnimInstance() || Module->GetAnimInstance()->IsA<UAnimSingleNodeInstance>());
		USkeletalMesh* Mesh = Module->GetSkeletalMeshAsset();
		const FReferenceSkeleton& Skeleton = Mesh->GetRefSkeleton();
		const auto Reference = ComponentPose(Skeleton, Skeleton.GetRefBonePose());
		const int32 Head = Skeleton.FindBoneIndex(TEXT("Head_00"));
		const int32 Neck = Skeleton.FindBoneIndex(TEXT("Neck_00"));
		TestTrue(Label + TEXT(" shorter ancestor chain is actually exercised"),
			Skeleton.GetBoneName(Skeleton.GetParentIndex(Skeleton.FindBoneIndex(TEXT("Spine_01")))).IsEqual(TEXT("Root")));
		TestTrue(Label + TEXT(" common neck-to-head reference geometry agrees"),
			Reference[Head].GetRelativeTransform(Reference[Neck]).Equals(
				BodyReference[BodyHead].GetRelativeTransform(BodyReference[BodyNeck]), .001));

		int32 RootWeightedVertices = 0, TorsoWeightedVertices = 0;
		const FSkeletalMeshLODModel& LOD = Mesh->GetImportedModel()->LODModels[0];
		for (const FSkelMeshSection& Section : LOD.Sections)
		{
			for (const FSoftSkinVertex& Vertex : Section.SoftVertices)
			{
				bool bRoot = false, bTorso = false;
				for (int32 Influence = 0; Influence < MAX_TOTAL_INFLUENCES; ++Influence)
				{
					if (Vertex.InfluenceWeights[Influence] == 0) { continue; }
					const FName Bone = Skeleton.GetBoneName(Section.BoneMap[Vertex.InfluenceBones[Influence]]);
					bRoot |= Bone == TEXT("Root");
					bTorso |= Bone == TEXT("Spine_01") || Bone == TEXT("Neck_00");
				}
				RootWeightedVertices += bRoot;
				TorsoWeightedVertices += bTorso;
			}
		}
		AddInfo(FString::Printf(TEXT("%s: bones=%d, chest/neck weighted vertices=%d, root weighted vertices=%d"),
			*Label, Skeleton.GetNum(), TorsoWeightedVertices, RootWeightedVertices));
		TestEqual(Label + TEXT(" unaligned import roots do not directly skin vertices"), RootWeightedVertices, 0);
		if (Module == Character->GetHeadMesh())
		{
			TestTrue(TEXT("Actual head asset contains skin that needs neck/torso deformation"), TorsoWeightedVertices > 0);
		}

		TArray<FBoneIndexType> AllBones;
		for (int32 Index = 0; Index < Skeleton.GetNum(); ++Index) { AllBones.Add(Index); }
		auto VerifySkinning = [&]()
		{
			const auto& BodyPose = Body->GetComponentSpaceTransforms();
			TArray<FTransform> Expected;
			TArray<FMatrix44f> RenderMatrices;
			Module->GetCurrentRefToLocalMatrices(RenderMatrices, 0, &AllBones);
			for (int32 Index = 0; Index < Skeleton.GetNum(); ++Index)
			{
				const int32 Source = BodySkeleton.FindBoneIndex(Skeleton.GetBoneName(Index));
				const int32 Parent = Skeleton.GetParentIndex(Index);
				Expected.Add(Source != INDEX_NONE ? BodyPose[Source] :
					(Parent == INDEX_NONE ? Skeleton.GetRefBonePose()[Index] :
					 Skeleton.GetRefBonePose()[Index] * Expected[Parent]));
				const FString BoneLabel = Label + TEXT("/") + Skeleton.GetBoneName(Index).ToString();
				TestTrue(BoneLabel + TEXT(" socket shares the final animated ancestor"),
					Module->GetBoneTransform(Index).Equals(Expected[Index] * Module->GetComponentTransform(), .002));
				const FMatrix44f ExpectedSkin = Mesh->GetRefBasesInvMatrix()[Index] *
					FMatrix44f(Expected[Index].ToMatrixWithScale());
				TestTrue(BoneLabel + TEXT(" render skinning preserves extra face/hair bones"),
					RenderMatrices.IsValidIndex(Index) && RenderMatrices[Index].Equals(ExpectedSkin, .002f));
			}
		};

		for (const FRotator Rotation : { FRotator::ZeroRotator, FRotator(20, 60, 0), FRotator(-25, -55, 15) })
		{
			TArray<FTransform> Local = BodySkeleton.GetRefBonePose();
			Local[BodyHead].ConcatenateRotation(Rotation.Quaternion());
			Body->GetEditableComponentSpaceTransforms() = ComponentPose(BodySkeleton, MoveTemp(Local));
			VerifySkinning();
			TestTrue(Label + TEXT(" turning the head does not rigidly drag neck skin"),
				Body->GetComponentSpaceTransforms()[BodyNeck].Equals(BodyReference[BodyNeck], .001));
		}
		for (const TCHAR* Path : {
			TEXT("/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/AS_Shth_Run_Loop"),
			TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Locomotion/AS_UnSh_Walk_Loop") })
		{
			UAnimSequence* Animation = LoadObject<UAnimSequence>(nullptr, Path);
			if (!TestNotNull(TEXT("Real locomotion pose"), Animation)) { continue; }
			Body->PlayAnimation(Animation, true);
			for (float Fraction : { .1f, .45f, .8f })
			{
				Body->SetPosition(Animation->GetPlayLength() * Fraction, false);
				Body->TickAnimation(0.f, false);
				Body->RefreshBoneTransforms();
				VerifySkinning();
			}
		}
	}
	World->DestroyWorld(false);
	return true;
}
#endif
