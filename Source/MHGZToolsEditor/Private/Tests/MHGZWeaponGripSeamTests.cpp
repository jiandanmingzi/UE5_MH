// Copyright MHGZ Project. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS
#include "Animation/AnimMontage.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/MHGZMotionMatchingAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "MHGZCharacter.h"
#include "MHGZAnimationSeamLibrary.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
constexpr const TCHAR* FeiYuanPath = TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_FeiYuan.AM_IG_FeiYuan");
constexpr const TCHAR* CharacterPath = TEXT("/Game/Blueprints/Characters/Demo/BP_IG_Character.BP_IG_Character_C");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponGripPolicyTest,
	"MHGZ.Maintenance.Animation.WeaponGrip.Policy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponGripPolicyTest::RunTest(const FString&)
{
	UAnimMontage* Montage = NewObject<UAnimMontage>();
	UAnimSequence* A = NewObject<UAnimSequence>();
	UAnimSequence* B = NewObject<UAnimSequence>();
	UAnimSequence* Other = NewObject<UAnimSequence>();
	auto* Settings = NewObject<UMHGZMontageSegmentBlendSettings>(Montage);
	Montage->AddMetaData(Settings);
	Settings->WeaponGripFromSequence = A; Settings->WeaponGripToSequence = B;
	FMHGZMontageSegmentBlendSample Old;
	Old.Montage = Montage; Old.Sequence = A; Old.InstanceID = 1; Old.SegmentIndex = 0;
	Old.SlotName = TEXT("DefaultSlot"); Old.Weight = 1.f;
	auto New = Old; New.Sequence = B; New.SegmentIndex = 3;
	TArray<FMHGZMontageSegmentBlendSample> Before {Old}, After {New};
	FMHGZWeaponGripConstraint State;
	State.Update(Before, After, .01f);
	TestEqual(TEXT("Default off"), State.Alpha, 0.f);
	Settings->bEnableWeaponGripConstraint = true;
	State.Update({}, After, .01f);
	TestEqual(TEXT("No startup constraint"), State.Serial, int64(0));
	Before[0].Sequence = Other; State.Update(Before, After, .01f);
	TestEqual(TEXT("Other entry (304) unaffected"), State.Serial, int64(0));
	Before[0] = Old; After[0].InstanceID = 2; State.Update(Before, After, .01f);
	TestEqual(TEXT("Different instance rejected"), State.Serial, int64(0));
	After[0] = New; After[0].SlotName = TEXT("UpperBody_IGAction"); State.Update(Before, After, .01f);
	TestEqual(TEXT("Other slot rejected"), State.Serial, int64(0));
	After[0] = New; State.Update(Before, After, .01f);
	TestEqual(TEXT("Explicit seam enabled"), State.Alpha, 1.f);
	TestEqual(TEXT("One trigger"), State.Serial, int64(1));
	for (int32 Index = 0; Index < 4; ++Index) { State.Update(After, After, .01f); }
	TestEqual(TEXT("Hold covers body inertialization"), State.Alpha, 1.f);
	State.Update(After, After, .025f);
	TestTrue(TEXT("Smooth release"), State.Alpha > 0.f && State.Alpha < 1.f);
	State.Update(After, After, .025f);
	TestEqual(TEXT("Fully returns to authored pose"), State.Alpha, 0.f);
	TestEqual(TEXT("No repeated trigger"), State.Serial, int64(1));
	State.Update(Before, After, .01f); After[0].DesiredWeight = 0.f;
	State.Update(After, After, .01f);
	TestEqual(TEXT("Blend-out cancels immediately"), State.Alpha, 0.f);
	After[0] = New; State.Update(Before, After, .01f); State.Update(After, {}, .01f);
	TestEqual(TEXT("Stop/pause clears ownership"), State.Alpha, 0.f);
	State.Update(Before, After, .01f); State.Update(After, After, 0.f);
	TestEqual(TEXT("Unavailable/zero delta clears"), State.Alpha, 0.f);
	State.Update(Before, After, .01f); After[0].InstanceID = 2; State.Update(After, After, .01f);
	TestEqual(TEXT("New owner clears"), State.Alpha, 0.f);
	After[0] = New; State.Update(Before, After, .01f); Settings->bEnableWeaponGripConstraint = false;
	State.Update(After, After, .01f);
	TestEqual(TEXT("Disabling metadata clears active constraint"), State.Alpha, 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponGripGraphTest,
	"MHGZ.Maintenance.Animation.WeaponGrip.Graph",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWeaponGripGraphTest::RunTest(const FString&)
{
	auto* BP = LoadObject<UAnimBlueprint>(nullptr, TEXT("/Game/Blueprints/Characters/Demo/Animation/ABP_MH_Character.ABP_MH_Character"));
	if (!TestNotNull(TEXT("Actual ABP"), BP)) { return false; }
	TestTrue(TEXT("Exact post-inertia wiring"), UMHGZAnimationSeamLibrary::InstallWeaponGripConstraint(BP, false));
	TestTrue(TEXT("Idempotent install"), UMHGZAnimationSeamLibrary::InstallWeaponGripConstraint(BP, true));
	TestFalse(TEXT("Read-only audit does not dirty asset"), BP->GetOutermost()->IsDirty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponGripRuntimeTest,
	"MHGZ.Maintenance.Animation.WeaponGrip.Runtime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponGripRuntimeTest::RunTest(const FString&)
{
	auto* CharacterClass = LoadClass<AMHGZCharacter>(nullptr, CharacterPath);
	auto* Montage = LoadObject<UAnimMontage>(nullptr, FeiYuanPath);
	if (!TestNotNull(TEXT("Character"), CharacterClass) || !TestNotNull(TEXT("FeiYuan"), Montage)) { return false; }
	auto* Settings = const_cast<UMHGZMontageSegmentBlendSettings*>(Montage->FindMetaDataByClass<UMHGZMontageSegmentBlendSettings>());
	if (!TestNotNull(TEXT("Saved metadata"), Settings)) { return false; }
	TestTrue(TEXT("Production seam enabled"), Settings->bEnableWeaponGripConstraint);
	const float Length = Montage->GetPlayLength();
	const FTransform RootMotion = Montage->ExtractRootMotionFromTrackRange(0.f, Length, FAnimExtractContext());
	const int32 NotifyCount = Montage->Notifies.Num();
	TArray<FGuid> NotifyGuids; for (const auto& N : Montage->Notifies) { NotifyGuids.Add(N.Guid); }
	struct FSample { FTransform Left, Right, Hand; float Position; int64 Request; TArray<FTransform> Pose; };
	const FString Out = FPaths::ProjectSavedDir() / TEXT("Maintenance/FeiYuanGripFix20261016");
	for (const float Delta : {1.f / 60.f, 1.f / 120.f, .2f / 60.f})
	{
		TArray<FSample> Baseline;
		for (const bool bEnabled : {false, true})
		{
			TGuardValue<bool> Guard(Settings->bEnableWeaponGripConstraint, bEnabled);
			UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
			auto* Character = World->SpawnActor<AMHGZCharacter>(CharacterClass);
			if (!TestNotNull(TEXT("Transient character"), Character)) { World->DestroyWorld(false); return false; }
			Character->bUnsheathed = true;
			auto* Mesh = Character->GetMesh(); Mesh->InitAnim(true);
			const FReferenceSkeleton& Ref = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
			const int32 WeaponIndex = Ref.FindBoneIndex(TEXT("L_Weapon_00"));
			auto* Anim = Cast<UMHGZMotionMatchingAnimInstance>(Mesh->GetAnimInstance());
			if (!TestNotNull(TEXT("Real ABP"), Anim)) { World->DestroyWorld(false); return false; }
			Mesh->TickAnimation(Delta, false); Mesh->RefreshBoneTransforms();
			Anim->Montage_Play(Montage);
			TArray<FString> Lines { TEXT("frame,position,alpha,gripSerial,gapCm,angleDeg,bodyDriftCm,requestSerial") };
			float PeakGap = 0.f, PeakHoldGap = 0.f, PeakBodyDrift = 0.f;
			bool bSawHold = false, bSawRelease = false, bReturned = false;
			for (int32 Frame = 0; Frame < FMath::CeilToInt(.65f / Delta); ++Frame)
			{
				Mesh->TickAnimation(Delta, false); Mesh->RefreshBoneTransforms();
				FSample S {Mesh->GetSocketTransform(TEXT("L_Weapon_00"), RTS_Component),
					Mesh->GetSocketTransform(TEXT("R_Weapon_00"), RTS_Component),
					Mesh->GetSocketTransform(TEXT("R_Arm_03"), RTS_Component),
					Anim->Montage_GetPosition(Montage), Anim->MontageSegmentBlendRequestSerial,
					Mesh->GetComponentSpaceTransforms()};
				float Drift = 0.f;
				if (!bEnabled) { Baseline.Add(S); }
				else if (TestTrue(TEXT("Paired frame exists"), Baseline.IsValidIndex(Frame)))
				{
					const auto& B = Baseline[Frame];
					Drift = FVector::Distance(S.Hand.GetLocation(), B.Hand.GetLocation());
					PeakBodyDrift = FMath::Max(PeakBodyDrift, Drift);
					TestTrue(TEXT("Body and stable reference unchanged"), S.Hand.Equals(B.Hand, .001f) && S.Right.Equals(B.Right, .001f));
					TestTrue(TEXT("Scale never copied/changed"), S.Left.GetScale3D().Equals(B.Left.GetScale3D(), .0001f));
					TestEqual(TEXT("Montage time unchanged"), S.Position, B.Position);
					TestEqual(TEXT("Body blend requests unchanged"), S.Request, B.Request);
					for (int32 Bone = 0; Bone < S.Pose.Num(); ++Bone)
					{
						if (Bone == WeaponIndex || Ref.BoneIsChildOf(Bone, WeaponIndex)) { continue; }
						TestTrue(TEXT("All non-weapon bones unchanged"), S.Pose[Bone].Equals(B.Pose[Bone], .001f));
					}
				}
				const float Gap = FVector::Distance(S.Left.GetLocation(), S.Right.GetLocation());
				const float Angle = FMath::RadiansToDegrees(S.Left.GetRotation().AngularDistance(S.Right.GetRotation()));
				if (S.Position >= .875f && S.Position <= .975f) { PeakGap = FMath::Max(PeakGap, Gap); }
				const float Alpha = Anim->WeaponGripConstraintAlpha;
				if (Alpha >= .9999f)
				{
					bSawHold = true; PeakHoldGap = FMath::Max(PeakHoldGap, Gap);
					TestTrue(TEXT("Final evaluated grip position/rotation constrained"), Gap < .01f && Angle < .02f);
				}
				else if (Alpha > 0.f) { bSawRelease = true; }
				else if (bSawHold) { bReturned = true; }
				Lines.Add(FString::Printf(TEXT("%d,%.7f,%.7f,%lld,%.5f,%.5f,%.5f,%lld"), Frame, S.Position, Alpha,
					Anim->WeaponGripConstraintSerial, Gap, Angle, Drift, S.Request));
				if (S.Position > 1.02f) { break; }
			}
			if (bEnabled)
			{
				TestTrue(TEXT("Full hold observed"), bSawHold); TestTrue(TEXT("Smooth release observed"), bSawRelease);
				TestTrue(TEXT("Returned to authored motion"), bReturned);
				TestEqual(TEXT("Only310->117triggers"), Anim->WeaponGripConstraintSerial, int64(1));
				TestTrue(TEXT("No large second drift on release"), PeakGap < 10.f);
			}
			else { TestEqual(TEXT("Disabled branch has zero influence"), Anim->WeaponGripConstraintSerial, int64(0)); }
			AddInfo(FString::Printf(TEXT("dt=%.7f enabled=%d peakGap=%.4f holdGap=%.5f bodyDrift=%.5f"),
				Delta, bEnabled, PeakGap, PeakHoldGap, PeakBodyDrift));
			FFileHelper::SaveStringArrayToFile(Lines, *(Out / FString::Printf(TEXT("paired-%s-%.0f.csv"),
				bEnabled ? TEXT("fixed") : TEXT("baseline"), 1.f / Delta)));
			Anim->Montage_Stop(0.f); Mesh->TickAnimation(Delta, false); Mesh->RefreshBoneTransforms();
			TestEqual(TEXT("Stop clears alpha"), Anim->WeaponGripConstraintAlpha, 0.f);
			if (bEnabled && FMath::IsNearlyEqual(Delta, 1.f / 60.f))
			{
				const int64 PreviousSerial = Anim->WeaponGripConstraintSerial;
				for (FName Entry : {FName(TEXT("Entry_From_HengSao")), FName(TEXT("Core"))})
				{
					Anim->Montage_Play(Montage); Anim->Montage_JumpToSection(Entry, Montage);
					for (int32 Frame = 0; Frame < 28; ++Frame)
					{ Mesh->TickAnimation(Delta, false); Mesh->RefreshBoneTransforms(); }
					TestEqual(TEXT("Other real routes do not opt in"), Anim->WeaponGripConstraintSerial, PreviousSerial);
					TestEqual(TEXT("Other real routes remain unconstrained"), Anim->WeaponGripConstraintAlpha, 0.f);
					Anim->Montage_Stop(0.f); Mesh->TickAnimation(Delta, false); Mesh->RefreshBoneTransforms();
				}
				Anim->Montage_Play(Montage);
				for (int32 Frame = 0; Frame < 30 && Anim->WeaponGripConstraintAlpha == 0.f; ++Frame)
				{ Mesh->TickAnimation(Delta, false); Mesh->RefreshBoneTransforms(); }
				TestEqual(TEXT("Restarted route can trigger anew"), Anim->WeaponGripConstraintAlpha, 1.f);
				Anim->Montage_Pause(Montage); Mesh->TickAnimation(Delta, false); Mesh->RefreshBoneTransforms();
				TestEqual(TEXT("Actual pause cancels"), Anim->WeaponGripConstraintAlpha, 0.f);
				Mesh->InitAnim(true);
				Anim = CastChecked<UMHGZMotionMatchingAnimInstance>(Mesh->GetAnimInstance());
				TestEqual(TEXT("Reinitialize resets alpha"), Anim->WeaponGripConstraintAlpha, 0.f);
				TestEqual(TEXT("Reinitialize resets owner/serial"), Anim->WeaponGripConstraintSerial, int64(0));
			}
			World->DestroyWorld(false);
		}
	}
	TestEqual(TEXT("Duration preserved"), Montage->GetPlayLength(), Length);
	TestEqual(TEXT("Notify count preserved"), Montage->Notifies.Num(), NotifyCount);
	for (int32 I = 0; I < NotifyGuids.Num(); ++I) { TestEqual(TEXT("Notify identity preserved"), Montage->Notifies[I].Guid, NotifyGuids[I]); }
	TestTrue(TEXT("Root motion preserved"), Montage->ExtractRootMotionFromTrackRange(0.f, Length, FAnimExtractContext()).Equals(RootMotion));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponGripObservationTest,
	"MHGZ.Maintenance.Animation.WeaponGrip.Observation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponGripObservationTest::RunTest(const FString&)
{
	UClass* CharacterClass = LoadClass<AMHGZCharacter>(nullptr,
		TEXT("/Game/Blueprints/Characters/Demo/BP_IG_Character.BP_IG_Character_C"));
	UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr,
		TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_FeiYuan.AM_IG_FeiYuan"));
	if (!TestNotNull(TEXT("production character"), CharacterClass)
		|| !TestNotNull(TEXT("production FeiYuan"), Montage)) { return false; }
	auto* Settings = const_cast<UMHGZMontageSegmentBlendSettings*>(Montage->FindMetaDataByClass<UMHGZMontageSegmentBlendSettings>());
	if (!TestNotNull(TEXT("blend metadata"), Settings)) { return false; }
	TGuardValue<bool> Guard(Settings->bEnableWeaponGripConstraint, false);
	const FString Out = FPaths::ProjectSavedDir() / TEXT("Maintenance/FeiYuanGripFix20261016");
	IFileManager::Get().MakeDirectory(*Out, true);
	for (const float Delta : {1.f / 60.f, 1.f / 120.f, .2f / 60.f})
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		AMHGZCharacter* Character = World->SpawnActor<AMHGZCharacter>(CharacterClass);
		if (!TestNotNull(TEXT("transient production character"), Character))
		{ World->DestroyWorld(false); return false; }
		Character->bUnsheathed = true;
		USkeletalMeshComponent* Mesh = Character->GetMesh();
		Mesh->InitAnim(true);
		UMHGZMotionMatchingAnimInstance* Anim = Cast<UMHGZMotionMatchingAnimInstance>(Mesh->GetAnimInstance());
		if (!TestNotNull(TEXT("real ABP instance"), Anim))
		{ World->DestroyWorld(false); return false; }
		Mesh->TickAnimation(Delta, false); Mesh->RefreshBoneTransforms();
		TestTrue(TEXT("real FeiYuan begins at its 310 entry"), Anim->Montage_Play(Montage) > 0.f);
		TArray<FString> Lines {TEXT("dt,frame,position,requestSerial,referenceDistanceCm,referenceAngleDeg,rightGripDriftCm,leftX,leftY,leftZ,rightX,rightY,rightZ")};
		float PeakGap = 0.f, PeakRightDrift = 0.f;
		FTransform PriorRightGrip;
		bool bHaveReference = false, bCrossed = false;
		for (int32 Frame = 0; Frame < FMath::CeilToInt(.65f / Delta); ++Frame)
		{
			Mesh->TickAnimation(Delta, false); Mesh->RefreshBoneTransforms();
			const float Position = Anim->Montage_GetPosition(Montage);
			const FTransform Left = Mesh->GetSocketTransform(TEXT("L_Weapon_00"), RTS_Component);
			const FTransform Right = Mesh->GetSocketTransform(TEXT("R_Weapon_00"), RTS_Component);
			const FTransform Hand = Mesh->GetSocketTransform(TEXT("R_Arm_03"), RTS_Component);
			const FTransform RightGrip = Right.GetRelativeTransform(Hand);
			if (Position < .383333f) { PriorRightGrip = RightGrip; bHaveReference = true; }
			const float Gap = FVector::Distance(Left.GetLocation(), Right.GetLocation());
			const float Angle = FMath::RadiansToDegrees(Left.GetRotation().AngularDistance(Right.GetRotation()));
			const float Drift = bHaveReference ? FVector::Distance(RightGrip.GetLocation(), PriorRightGrip.GetLocation()) : 0.f;
			if (Position >= .875f && Position <= .975f)
			{
				bCrossed = true; PeakGap = FMath::Max(PeakGap, Gap); PeakRightDrift = FMath::Max(PeakRightDrift, Drift);
			}
			const FVector L = Left.GetLocation(), R = Right.GetLocation();
			Lines.Add(FString::Printf(TEXT("%.7f,%d,%.7f,%lld,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f"),
				Delta, Frame, Position, Anim->MontageSegmentBlendRequestSerial, Gap, Angle, Drift, L.X,L.Y,L.Z,R.X,R.Y,R.Z));
			if (Position > 1.02f) { break; }
		}
		TestTrue(TEXT("natural playback crossed 310 to 117"), bCrossed);
		AddInfo(FString::Printf(TEXT("Final evaluated pose dt=%.7f: peak L/R reference gap %.4f cm; stable right grip drift %.4f cm"), Delta, PeakGap, PeakRightDrift));
		FFileHelper::SaveStringArrayToFile(Lines, *(Out / FString::Printf(TEXT("observation-%.0f.csv"), 1.f / Delta)));
		Anim->Montage_Stop(0.f);
		World->DestroyWorld(false);
	}
	return true;
}
#endif
