#include "ActionSystem/Tests/Shared/MHGZIGActionFixtures.h"
// Copyright MHGZ Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Monster/MHGZMonsterHitzoneComponent.h"
#include "Monster/MHGZTrainingDummy.h"
#include "ActionSystem/MHGZDamageGameplayEffect.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "ActionSystem/AbilityTask_MHGZActionDirectionCorrection.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"

#include "ActionSystem/Tests/M6/MHGZM6AwakenedKinsectTestTypes.h"
#include "AttributeSystem/Res_InsectGlaive.h"
#include "InputSystem/MHGZWeaponInputRouterComponent.h"
#include "MHGZCharacter.h"
#include "Movement/MHGZInstrumentedCharacterMovementComponent.h"
#include "../../Shared/MHGZM3TestHarness.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"
#include "WeaponRuntime/MHGZWeaponInputProfile.h"

namespace
{
bool AddTripleUp(URes_InsectGlaive& Resource)
{
	return Resource.ApplyExtract(M3::Tag(TEXT("WeaponResource.IG.Extract.White")))
		&& Resource.ApplyExtract(M3::Tag(TEXT("WeaponResource.IG.Extract.Orange")))
		&& Resource.ApplyExtract(M3::Tag(TEXT("WeaponResource.IG.Extract.Red")))
		&& Resource.IsTripleUpActive();
}

FWeaponInputSnapshot MakeAwakenedInput(uint32 SequenceID)
{
	FWeaponInputSnapshot Input = M3::MakePosedInput(true, false);
	Input.ResolvedInputTag = M3::Tag(TEXT("Input.Weapon.RTYB"));
	Input.SourceControlTag = M3::Tag(TEXT("Input.Weapon.Y"));
	Input.ActorForward = FVector::ForwardVector;
	Input.SequenceID = SequenceID;
	Input.Phase = EWeaponInputPhase::Started;
	Input.Aim.Context = EWeaponAimSnapshotContext::Action;
	Input.Aim.Direction = FVector::ForwardVector;
	Input.Aim.TargetPoint = FVector(1000.f, 0.f, 0.f);
	return Input;
}

UMHGZM6AwakenedKinsectAbilityTestDouble* FindAwakenedInstance(
	UMHGZAbilitySystemComponent& ASC, const FGameplayAbilitySpecHandle Handle)
{
	FGameplayAbilitySpec* Spec = ASC.FindAbilitySpecFromHandle(Handle);
	if (!Spec)
	{
		return nullptr;
	}
	for (UGameplayAbility* Instance : Spec->GetAbilityInstances())
	{
		if (UMHGZM6AwakenedKinsectAbilityTestDouble* Ability =
			Cast<UMHGZM6AwakenedKinsectAbilityTestDouble>(Instance))
		{
			return Ability;
		}
	}
	return nullptr;
}

USkeletalMeshComponent* AddKinsectWeaponSocketForTest(ACharacter& Character)
{
	USkeletalMesh* SkeletalMesh = LoadObject<USkeletalMesh>(nullptr,
		TEXT("/Game/Weapons/InsectGlaive/Meshes/Glaive/SKM_IG_Glaive.SKM_IG_Glaive"));
	if (!SkeletalMesh || !SkeletalMesh->FindSocket(FName(TEXT("IG_Kinsect"))))
	{
		return nullptr;
	}

	USkeletalMeshComponent* WeaponMesh = NewObject<USkeletalMeshComponent>(&Character);
	WeaponMesh->SetSkeletalMeshAsset(SkeletalMesh);
	Character.AddInstanceComponent(WeaponMesh);
	WeaponMesh->RegisterComponent();
	return WeaponMesh->DoesSocketExist(FName(TEXT("IG_Kinsect")))
		? WeaponMesh : nullptr;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZAwakenedRefillStaminaTest,
	"MHGZ.M6.AwakenedKinsect.ActivationRefillsKinsectStamina",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZAwakenedRefillStaminaTest::RunTest(const FString&)
{
	// 0~7覆盖所有灯组合；8验证预检有红灯、实际准备前已失去红灯。
	for (int32 Mask = 0; Mask <= 8; ++Mask)
	{
		FMHGZM3Harness H;
		if (!TestTrue(TEXT("runtime"), H.Setup(false, AMHGZM6AwakenedKinsectTestCharacter::StaticClass()))) return false;
		if (!TestNotNull(TEXT("weapon socket"), AddKinsectWeaponSocketForTest(*H.Character))) { H.Teardown(); return false; }
		const int32 InitialMask = Mask == 8 ? 1 : Mask;
		const FGameplayTag Colors[] = {M3::Tag(TEXT("WeaponResource.IG.Extract.Red")),
			M3::Tag(TEXT("WeaponResource.IG.Extract.White")), M3::Tag(TEXT("WeaponResource.IG.Extract.Orange"))};
		for (int32 I = 0; I < 3; ++I)
			if (InitialMask & (1 << I)) TestTrue(TEXT("initial extract"), H.Resource->ApplyExtract(Colors[I]));
		FKinsectFlightRequest Drain;
		Drain.RuntimeToken = H.Host->GetCurrentToken(); Drain.FlightInstanceID = FGuid::NewGuid();
		Drain.FlightSpeed = 1000; Drain.MaxDistance = 2000; Drain.ArrivalRadius = 10;
		TestTrue(TEXT("drain flight"), H.Resource->DeployKinsect(Drain));
		H.Resource->TickComponent(100.f, LEVELTICK_All, nullptr);
		H.Kinsect->SetActorLocation(H.Character->GetMesh()->GetComponentLocation()); H.Kinsect->Tick(.016f);
		TestEqual(TEXT("kinsect starts empty"), H.Resource->GetKinsectStamina(), 0.f);
		TestFalse(TEXT("normal deploy still requires stamina"), H.Resource->CanDeployKinsect(Drain));
		const auto Handle = H.GiveAbility(UMHGZM6AwakenedKinsectAbilityTestDouble::StaticClass());
		auto Input = MakeAwakenedInput(1);
		auto Invalid = Input; Invalid.Aim.Context = EWeaponAimSnapshotContext::Kinsect;
		// GAS的TryActivate返回值只表示已进入Activate；最终预检结果看Commit/注册。
		H.TryActivateWithInput(Handle, Invalid);
		auto* Rejected = FindAwakenedInstance(*H.ASC, Handle);
		TestFalse(TEXT("invalid input never commits an action"), Rejected && Rejected->IsActionActivationCommitted());
		TestFalse(TEXT("failed preflight has no registered action"), H.Host->HasRegisteredAction());
		TestEqual(TEXT("failed preflight cannot refill"), H.Resource->GetKinsectStamina(), 0.f);
		if (Mask == 0)
		{
			H.TryActivateWithInput(Handle, Input);
			auto* Empty = FindAwakenedInstance(*H.ASC, Handle);
			TestFalse(TEXT("no extracts cannot preview a nonexistent zero-stamina refill"), Empty && Empty->IsActionActivationCommitted());
			TestEqual(TEXT("empty no-extract preflight leaves stamina unchanged"), H.Resource->GetKinsectStamina(), 0.f);
			// 无灯但有少量耐力仍可正常发动，并保持该耐力而非补满。
			H.Resource->TickComponent(1.f, LEVELTICK_All, nullptr);
		}
		const float Before = H.Resource->GetKinsectStamina();
		if (!TestTrue(TEXT("valid awakened activation accepted"), H.TryActivateWithInput(Handle, Input)))
		{ H.Teardown(); return false; }
		auto* Ability = FindAwakenedInstance(*H.ASC, Handle);
		if (!TestNotNull(TEXT("active awakened action"), Ability)) { H.Teardown(); return false; }
		// 替身仅执行真实Commit；显式调用生产起手准备，证明预检未提前回耐。
		TestEqual(TEXT("read-only preflight does not refill"), H.Resource->GetKinsectStamina(), Before);
		if (Mask == 8) TestTrue(TEXT("preview extract is lost before actual consumption"), H.Resource->ConsumeExtract(Colors[0]));
		auto Wrong = Ability->GetActionToken(); ++Wrong.ActivationSequenceID;
		TestFalse(TEXT("unregistered action cannot refill"), H.Resource->RestoreKinsectStaminaForAction(Wrong));
		TestTrue(TEXT("successful startup preparation"), Ability->PrepareKinsectAttachmentForTest());
		const float Expected = Mask >= 1 && Mask <= 7 ? H.Resource->GetMaxKinsectStamina() : Before;
		TestEqual(FString::Printf(TEXT("mask %d refills only after actual extract consumption"), Mask), H.Resource->GetKinsectStamina(), Expected);
		TestEqual(TEXT("UI sees the same conditional stamina result"), H.Resource->GetUIState().KinsectStamina, Expected);
		TestEqual(TEXT("refill does not prematurely launch"), H.Kinsect->State, EKinsectState::ActionAttached);
		Ability->RequestEndAction(EWeaponActionEndReason::Normal);
		H.Teardown();
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM6AwakenedKinsectConditionalTripleBranch,
	"MHGZ.M6.AwakenedKinsect.ConditionalTripleBranchTransaction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM6AwakenedKinsectConditionalTripleBranch::RunTest(
	const FString& Parameters)
{
	FMHGZM3Harness H;
	if (!H.Setup(false, AMHGZM6AwakenedKinsectTestCharacter::StaticClass()))
	{
		AddError(TEXT("M3 runtime harness setup failed"));
		return false;
	}
	if (!TestNotNull(TEXT("temporary IG_Kinsect test socket is available"),
		AddKinsectWeaponSocketForTest(*H.Character)))
	{
		H.Teardown();
		return false;
	}

	const FGameplayAbilitySpecHandle AbilityHandle =
		H.GiveAbility(UMHGZM6AwakenedKinsectAbilityTestDouble::StaticClass());
	TestTrue(TEXT("Awakened ability is granted"), AbilityHandle.IsValid());
	TestTrue(TEXT("ordinary activation begins with a red lamp"),
		H.Resource->ApplyExtract(M3::Tag(TEXT("WeaponResource.IG.Extract.Red"))));
	TestTrue(TEXT("ordinary activation begins with a white lamp"),
		H.Resource->ApplyExtract(M3::Tag(TEXT("WeaponResource.IG.Extract.White"))));

	const FWeaponInputSnapshot OrdinaryInput = MakeAwakenedInput(1);
	TestTrue(TEXT("RTYB activates without TripleUp"),
		H.TryActivateWithInput(AbilityHandle, OrdinaryInput));
	UMHGZM6AwakenedKinsectAbilityTestDouble* OrdinaryAbility =
		FindAwakenedInstance(*H.ASC, AbilityHandle);
	TestNotNull(TEXT("ordinary branch has an active instance"), OrdinaryAbility);
	if (OrdinaryAbility)
	{
		TestFalse(TEXT("no TripleUp selects ordinary kinsect mode"),
			OrdinaryAbility->IsEmpoweredKinsectBranch());
		OrdinaryAbility->SetReleaseAimSnapshotForTest(OrdinaryInput.ActorForward,
			OrdinaryInput.Aim.Direction, OrdinaryInput.Aim.TargetPoint);
		OrdinaryAbility->SetKinsectFlightDurationForTest(1.0f);
		TestTrue(TEXT("ordinary branch attaches the insect to the weapon tail before montage"),
			OrdinaryAbility->PrepareKinsectAttachmentForTest());
		TestFalse(TEXT("ordinary activation consumes the red lamp without empowerment"),
			H.Resource->HasExtract(M3::Tag(TEXT("WeaponResource.IG.Extract.Red"))));
		TestFalse(TEXT("ordinary activation consumes the white lamp without empowerment"),
			H.Resource->HasExtract(M3::Tag(TEXT("WeaponResource.IG.Extract.White"))));
		UMHGZMonsterHitzoneComponent* CargoZone = NewObject<UMHGZMonsterHitzoneComponent>(H.Character);
		CargoZone->ExtractColorTag = M3::Tag(TEXT("WeaponResource.IG.Extract.Orange"));
		TestTrue(TEXT("action-attached success can capture the shared ordinary cargo"),
			H.Resource->CaptureActionKinsectExtract(OrdinaryAbility->GetActionToken(), CargoZone));
		TestEqual(TEXT("action-attached cargo is visible before return"),
			H.Resource->GetUIState().KinsectPendingExtractColor, CargoZone->ExtractColorTag);
		TestFalse(TEXT("action capture does not prematurely grant a hunter lamp"),
			H.Resource->HasExtract(CargoZone->ExtractColorTag));
		TestFalse(TEXT("same single cargo cannot accumulate a second extract"),
			H.Resource->CaptureActionKinsectExtract(OrdinaryAbility->GetActionToken(), CargoZone));
		H.Kinsect->TakePendingExtractColor(); // Keep the remaining flight-profile fixture independent.
		// The release-frame yaw is authoritative, but turning starts only at the
		// dedicated frame-230 event—not at the input release or KinsectLaunch.
		H.Character->SetActorRotation(FRotator(0.0f, 180.0f, 0.0f));
		const FVector ActorScaleBeforeCorrection = H.Character->GetActorScale3D();
		const FVector MeshScaleBeforeCorrection = H.Character->GetMesh()
			? H.Character->GetMesh()->GetRelativeScale3D() : FVector::OneVector;
		TestTrue(TEXT("release aim can be frozen before the facing notify"),
			OrdinaryAbility->LockReleaseAimForTest());
		TestNull(TEXT("freezing release aim alone does not start facing correction"),
			OrdinaryAbility->GetFacingCorrectionTaskForTest());
		TestTrue(TEXT("frame-230 Montage event starts release-facing correction"),
			OrdinaryAbility->HandleAwakenedKinsectEvent(
				OrdinaryAbility->GetActionToken(),
				EAwakenedKinsectAttackEvent::ReleaseFacingCorrection));
		UAbilityTask_MHGZActionDirectionCorrection* FacingTask =
			OrdinaryAbility->GetFacingCorrectionTaskForTest();
		TestNotNull(TEXT("frame-230 correction uses the shared action-owned task"),
			FacingTask);
		if (FacingTask)
		{
			FacingTask->TickTask(0.05f);
			const float MidCorrectionYaw = H.Character->GetActorRotation().Yaw;
			TestFalse(TEXT("EaseOut correction advances over time rather than snapping"),
				FMath::IsNearlyEqual(FRotator::NormalizeAxis(MidCorrectionYaw), 0.0f, 0.01f)
				|| FMath::IsNearlyEqual(FRotator::NormalizeAxis(MidCorrectionYaw), 180.0f, 0.01f));
			FacingTask->TickTask(0.05f);
		}
		TestTrue(TEXT("ordinary request commits once at its notify boundary"),
			OrdinaryAbility->CommitKinsectLaunch(OrdinaryAbility->GetActionToken()));
	TestTrue(TEXT("eased frame-230 release-facing correction reaches the release-locked yaw"),
		FMath::IsNearlyZero(FRotator::NormalizeAxis(
			H.Character->GetActorRotation().Yaw), 0.01f));
	TestTrue(TEXT("facing correction preserves actor scale"),
		H.Character->GetActorScale3D().Equals(ActorScaleBeforeCorrection, 0.001f));
	if (H.Character->GetMesh())
	{
		TestTrue(TEXT("facing correction preserves mesh relative scale"),
			H.Character->GetMesh()->GetRelativeScale3D().Equals(
				MeshScaleBeforeCorrection, 0.001f));
	}
		TestTrue(TEXT("ordinary request is idempotent for the same action"),
			OrdinaryAbility->CommitKinsectLaunch(OrdinaryAbility->GetActionToken()));
		TestEqual(TEXT("consumed white grants piercing without requiring TripleUp"),
			H.Kinsect->ActiveRequest.DamageMode, EKinsectDamageMode::Piercing);
		TestEqual(TEXT("white piercing collects each successful hit"),
			H.Kinsect->ActiveRequest.ExtractMode, EKinsectExtractMode::CollectPerValidHit);
		TestEqual(TEXT("consumed red selects the 30 MV tier"), H.Kinsect->ActiveRequest.MotionValue, 30.0f);
		TestEqual(TEXT("ordinary branch holds the insect at a hit/range endpoint until HunterFlight ends"),
			H.Kinsect->ActiveRequest.PostFlightPolicy,
			EKinsectPostFlightPolicy::Hover);
		TestTrue(TEXT("ordinary flight uses the fixed range-matched 2074.573 cm/s code speed"),
			FMath::IsNearlyEqual(H.Kinsect->ActiveRequest.FlightSpeed, 2074.573f, 0.01f));
		TestTrue(TEXT("ordinary maximum range is speed times the authored/test interval"),
			FMath::IsNearlyEqual(H.Kinsect->ActiveRequest.MaxDistance, 2074.573f, 0.01f));
		TestEqual(TEXT("ordinary branch uses the shared marked-contact powder policy"),
			H.Kinsect->ActiveRequest.PowderSpawnPolicy,
			EIGPowderSpawnPolicy::MarkedContact);
		OrdinaryAbility->RequestEndAction(EWeaponActionEndReason::Normal);
	}
	TestFalse(TEXT("ordinary activation leaves TripleUp absent"),
		H.Resource->IsTripleUpActive());

	TestTrue(TEXT("test establishes TripleUp"), AddTripleUp(*H.Resource));
	const FWeaponInputSnapshot EmpoweredInput = MakeAwakenedInput(2);
	TestTrue(TEXT("RTYB activates with TripleUp"),
		H.TryActivateWithInput(AbilityHandle, EmpoweredInput));
	UMHGZM6AwakenedKinsectAbilityTestDouble* EmpoweredAbility =
		FindAwakenedInstance(*H.ASC, AbilityHandle);
	TestNotNull(TEXT("empowered branch has an active instance"), EmpoweredAbility);
	if (EmpoweredAbility)
	{
		TestTrue(TEXT("active TripleUp selects empowered piercing mode"),
			EmpoweredAbility->IsEmpoweredKinsectBranch());
		EmpoweredAbility->SetReleaseAimSnapshotForTest(EmpoweredInput.ActorForward,
			EmpoweredInput.Aim.Direction, EmpoweredInput.Aim.TargetPoint);
		EmpoweredAbility->SetKinsectFlightDurationForTest(1.0f);
		TestFalse(TEXT("TripleUp is consumed at successful GA activation, before montage/launch notify"),
			H.Resource->IsTripleUpActive());
		TestTrue(TEXT("empowered branch attaches the insect to the weapon tail before montage"),
			EmpoweredAbility->PrepareKinsectAttachmentForTest());
		TestTrue(TEXT("empowered request commits at its notify boundary"),
			EmpoweredAbility->CommitKinsectLaunch(EmpoweredAbility->GetActionToken()));
		TestEqual(TEXT("empowered branch pierces"),
			H.Kinsect->ActiveRequest.DamageMode, EKinsectDamageMode::Piercing);
		TestEqual(TEXT("empowered branch extracts each valid hit"),
			H.Kinsect->ActiveRequest.ExtractMode, EKinsectExtractMode::CollectPerValidHit);
		TestEqual(TEXT("empowered branch uses the cooldown-bypassing piercing powder policy"),
			H.Kinsect->ActiveRequest.PowderSpawnPolicy,
			EIGPowderSpawnPolicy::AwakenedEachHit);
		TestEqual(TEXT("TripleUp selects the 50 MV tier once"), H.Kinsect->ActiveRequest.MotionValue, 50.0f);
		TestTrue(TEXT("empowered request carries a source attack identity"),
			H.Kinsect->ActiveRequest.SourceAttackInstanceID.IsValid());
		TestTrue(TEXT("TripleUp changes speed to 1.5x ordinary"),
			FMath::IsNearlyEqual(H.Kinsect->ActiveRequest.FlightSpeed, 3111.8595f, 0.01f));
		TestTrue(TEXT("TripleUp's one-second miss range follows its speed, not an extension field"),
			FMath::IsNearlyEqual(H.Kinsect->ActiveRequest.MaxDistance, 3111.8595f, 0.01f));
		EmpoweredAbility->RequestEndAction(EWeaponActionEndReason::Normal);
	}
	TestFalse(TEXT("empowered activation atomically consumes TripleUp"),
		H.Resource->IsTripleUpActive());
	TestFalse(TEXT("empowered activation does not leave red extract behind"),
		H.Resource->HasExtract(M3::Tag(TEXT("WeaponResource.IG.Extract.Red"))));
	TestFalse(TEXT("empowered activation does not leave white extract behind"),
		H.Resource->HasExtract(M3::Tag(TEXT("WeaponResource.IG.Extract.White"))));
	TestFalse(TEXT("empowered activation does not leave orange extract behind"),
		H.Resource->HasExtract(M3::Tag(TEXT("WeaponResource.IG.Extract.Orange"))));

	H.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZAwakenedConsumedExtractMatrix,
	"MHGZ.Maintenance.AwakenedExtracts.EightCombinationsFreezeActualConsumption",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZAwakenedConsumedExtractMatrix::RunTest(const FString& Parameters)
{
	const FGameplayTag Colors[] = { M3::Tag(TEXT("WeaponResource.IG.Extract.Red")),
		M3::Tag(TEXT("WeaponResource.IG.Extract.White")), M3::Tag(TEXT("WeaponResource.IG.Extract.Orange")) };
	for (int32 Mask = 0; Mask < 8; ++Mask)
	{
		FMHGZM3Harness H;
		if (!TestTrue(TEXT("real resource/GA harness builds"),
			H.Setup(false, AMHGZM6AwakenedKinsectTestCharacter::StaticClass()))
			|| !TestNotNull(TEXT("weapon socket is available"), AddKinsectWeaponSocketForTest(*H.Character)))
		{
			H.Teardown(); return false;
		}
		const FGameplayAbilitySpecHandle Handle = H.GiveAbility(UMHGZM6AwakenedKinsectAbilityTestDouble::StaticClass());
		for (int32 Index = 0; Index < 3; ++Index)
		{
			if (Mask & (1 << Index))
			{
				TestTrue(TEXT("selected hunter lamp is granted"), H.Resource->ApplyExtract(Colors[Index]));
			}
		}
		const FWeaponInputSnapshot Input = MakeAwakenedInput(Mask + 1);
		if (!TestTrue(TEXT("all eight lamp combinations can activate"), H.TryActivateWithInput(Handle, Input)))
		{
			H.Teardown(); return false;
		}
		UMHGZM6AwakenedKinsectAbilityTestDouble* Ability = FindAwakenedInstance(*H.ASC, Handle);
		if (!TestNotNull(TEXT("active exact GA instance"), Ability)) { H.Teardown(); return false; }
		Ability->SetReleaseAimSnapshotForTest(Input.ActorForward, Input.Aim.Direction, Input.Aim.TargetPoint);
		Ability->SetKinsectFlightDurationForTest(1.0f);
		if (!TestTrue(TEXT("production attachment path consumes the actual single lamps"), Ability->PrepareKinsectAttachmentForTest()))
		{
			H.Teardown(); return false;
		}
		TestFalse(TEXT("all original hunter lamps are consumed before launch"), H.Resource->IsTripleUpActive());
		for (const FGameplayTag& Color : Colors)
		{
			TestFalse(TEXT("no original single color is left behind"), H.Resource->HasExtract(Color));
		}
		// New buffs obtained after the confirmed consumption must not rewrite this shot.
		TestTrue(TEXT("new lamps can arrive while the action winds up"), AddTripleUp(*H.Resource));
		if (!TestTrue(TEXT("launch uses the frozen consumed colors"), Ability->CommitKinsectLaunch(Ability->GetActionToken())))
		{
			H.Teardown(); return false;
		}
		const FKinsectFlightRequest& Request = H.Kinsect->ActiveRequest;
		const bool bTriple = Mask == 7, bRed = (Mask & 1) != 0, bWhite = (Mask & 2) != 0, bOrange = (Mask & 4) != 0;
		TestEqual(*FString::Printf(TEXT("mask %d: correct per-hit MV"), Mask), Request.MotionValue, bTriple ? 50.0f : bRed ? 30.0f : 15.0f);
		TestEqual(TEXT("white alone decides piercing"), Request.DamageMode, bWhite ? EKinsectDamageMode::Piercing : EKinsectDamageMode::SingleHit);
		TestEqual(TEXT("white collects cargo per successful hit"), Request.ExtractMode, bWhite ? EKinsectExtractMode::CollectPerValidHit : EKinsectExtractMode::FirstHitOnly);
		TestEqual(TEXT("yellow alone enables unmarked per-hit powder"), Request.PowderSpawnPolicy, bOrange ? EIGPowderSpawnPolicy::AwakenedEachHit : EIGPowderSpawnPolicy::MarkedContact);
		TestEqual(TEXT("only piercing owns a repeat interval"), Request.RehitInterval, bWhite ? H.CombatConfig->AwakenedPierceHitInterval : 0.0f);
		TestEqual(TEXT("speed is unchanged and still depends only on full TripleUp"), Request.FlightSpeed, bTriple ? 3111.8595f : 2074.573f);
		TestEqual(TEXT("endpoint recall policy is unchanged"), Request.PostFlightPolicy, bTriple ? EKinsectPostFlightPolicy::ReturnToOwner : EKinsectPostFlightPolicy::Hover);
		TestTrue(TEXT("newly acquired TripleUp is not consumed by the already frozen launch"), H.Resource->IsTripleUpActive());
		Ability->RequestEndAction(EWeaponActionEndReason::Normal);
		H.Teardown();
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM6AwakenedKinsectReleaseSectionPolicy,
	"MHGZ.M6.AwakenedKinsect.ReleasePreservesStartupAndJumpsFromHoldLoop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM6AwakenedKinsectReleaseSectionPolicy::RunTest(const FString& Parameters)
{
	const FName Startup(TEXT("Startup193"));
	const FName HoldLoop(TEXT("Hold193Loop"));
	const FName Send(TEXT("Send195"));
	TestEqual(TEXT("release during startup waits for the complete wind-up"),
		UMHGZAwakenedKinsectAttackAbility::ResolveReleaseRoute(
			Startup, Startup, HoldLoop),
		EAwakenedKinsectReleaseRoute::QueueSendAfterStartup);
	TestEqual(TEXT("release during hold loop jumps to Send immediately"),
		UMHGZAwakenedKinsectAttackAbility::ResolveReleaseRoute(
			HoldLoop, Startup, HoldLoop),
		EAwakenedKinsectReleaseRoute::JumpToSendNow);
	TestEqual(TEXT("release after Send195 begins does not rewind Montage"),
		UMHGZAwakenedKinsectAttackAbility::ResolveReleaseRoute(
			Send, Startup, HoldLoop),
		EAwakenedKinsectReleaseRoute::None);
	TestEqual(TEXT("unknown section is not rerouted"),
		UMHGZAwakenedKinsectAttackAbility::ResolveReleaseRoute(
			FName(TEXT("Unknown")), Startup, HoldLoop),
		EAwakenedKinsectReleaseRoute::None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM6AwakenedKinsectHoldLoopLimit,
	"MHGZ.M6.AwakenedKinsect.HoldLoopPlaysAtMostThreeTimes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM6AwakenedKinsectHoldLoopLimit::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FMHGZM3Harness H;
	if (!H.Setup(false, AMHGZM6AwakenedKinsectTestCharacter::StaticClass()))
	{
		AddError(TEXT("M3 runtime harness setup failed"));
		return false;
	}

	USkeletalMesh* BodyAsset = LoadObject<USkeletalMesh>(nullptr,
		TEXT("/Game/Characters/Demo/Meshes/Body/SKM_Demo_Body.SKM_Demo_Body"));
	UAnimMontage* AttackMontage = LoadObject<UAnimMontage>(nullptr,
		TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_JueChongJi.AM_IG_JueChongJi"));
	if (!TestNotNull(TEXT("compatible body mesh loads"), BodyAsset)
		|| !TestNotNull(TEXT("persistent awakened Montage loads"), AttackMontage)
		|| !TestNotNull(TEXT("character mesh component exists"), H.Character->GetMesh()))
	{
		H.Teardown();
		return false;
	}
	H.Character->GetMesh()->SetSkeletalMesh(BodyAsset);
	H.Character->GetMesh()->SetAnimInstanceClass(UAnimInstance::StaticClass());
	H.ASC->RefreshAbilityActorInfo();
	const FGameplayAbilitySpecHandle AbilityHandle =
		H.GiveAbility(UMHGZM6AwakenedKinsectAbilityTestDouble::StaticClass());
	const FWeaponInputSnapshot Input = MakeAwakenedInput(90);
	if (!TestTrue(TEXT("RTYB activates the action used for the real Montage callback"),
		H.TryActivateWithInput(AbilityHandle, Input)))
	{
		H.Teardown();
		return false;
	}
	UMHGZM6AwakenedKinsectAbilityTestDouble* Ability =
		FindAwakenedInstance(*H.ASC, AbilityHandle);
	if (!TestNotNull(TEXT("active awakened ability resolves"), Ability)
		|| !Ability->ConfigureAttackMontageForTest(AttackMontage)
		|| !Ability->StartProductionAttackMontageForTest(*H.Character,
			AttackMontage, FName(TEXT("Startup193"))))
	{
		AddError(TEXT("Could not start the production Montage and bind its exact section callback."));
		H.Teardown();
		return false;
	}
	UAnimInstance* AnimInstance = H.Character->GetMesh()->GetAnimInstance();
	FAnimMontageInstance* MontageInstance = AnimInstance
		? AnimInstance->GetActiveInstanceForMontage(AttackMontage) : nullptr;
	if (!TestNotNull(TEXT("active exact Montage instance resolves"), MontageInstance))
	{
		H.Teardown();
		return false;
	}

	const FName HoldLoop(TEXT("Hold193Loop"));
	const FName Send(TEXT("Send195"));
	const int32 HoldLoopIndex = AttackMontage->GetSectionIndex(HoldLoop);
	for (int32 EnteredPlay = 1; EnteredPlay <= 4; ++EnteredPlay)
	{
		MontageInstance->OnMontageSectionChanged.ExecuteIfBound(
			AttackMontage, HoldLoop, EnteredPlay > 1);
		const int32 NextSectionID = AnimInstance->Montage_GetNextSectionID(
			AttackMontage, HoldLoopIndex);
		const FName ExpectedNext = EnteredPlay < 3 ? HoldLoop : Send;
		TestEqual(FString::Printf(TEXT("after loop entry %d, the production Montage routes to the expected next section"),
			EnteredPlay), AttackMontage->GetSectionName(NextSectionID), ExpectedNext);
	}
	H.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM6AwakenedKinsectDanceHitWindowGate,
	"MHGZ.M6.AwakenedKinsect.DanceRequiresSuccessfulHunterFlightWindow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM6AwakenedKinsectDanceHitWindowGate::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	const FName FlightSection(TEXT("HunterFlight199"));
	TestTrue(TEXT("segment 0 can hit only in the exact active 199 flight"),
		UMHGZAwakenedKinsectAttackAbility::IsHunterFlightDamageWindowOpen(
			true, true, true, false, false, 0, FlightSection, FlightSection));
	TestFalse(TEXT("a hit before hunter flight cannot enter Dance"),
		UMHGZAwakenedKinsectAttackAbility::IsHunterFlightDamageWindowOpen(
			true, true, false, false, false, 0, FlightSection, FlightSection));
	TestFalse(TEXT("a stale or different Montage instance cannot enter Dance"),
		UMHGZAwakenedKinsectAttackAbility::IsHunterFlightDamageWindowOpen(
			true, false, true, false, false, 0, FlightSection, FlightSection));
	TestFalse(TEXT("only AttackSegments[0] is accepted"),
		UMHGZAwakenedKinsectAttackAbility::IsHunterFlightDamageWindowOpen(
			true, true, true, false, false, 1, FlightSection, FlightSection));
	TestFalse(TEXT("the sweep outside HunterFlight199 is rejected"),
		UMHGZAwakenedKinsectAttackAbility::IsHunterFlightDamageWindowOpen(
			true, true, true, false, false, 0, FName(TEXT("Send195")), FlightSection));
	TestFalse(TEXT("once a successful hit is queued, later sweeps cannot retrigger"),
		UMHGZAwakenedKinsectAttackAbility::IsHunterFlightDamageWindowOpen(
			true, true, true, true, false, 0, FlightSection, FlightSection));
	TestFalse(TEXT("once Dance is committed, later sweeps cannot retrigger"),
		UMHGZAwakenedKinsectAttackAbility::IsHunterFlightDamageWindowOpen(
			true, true, true, false, true, 0, FlightSection, FlightSection));
	TestFalse(TEXT("an inactive action cannot enter Dance"),
		UMHGZAwakenedKinsectAttackAbility::IsHunterFlightDamageWindowOpen(
			false, true, true, false, false, 0, FlightSection, FlightSection));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM6AwakenedKinsectUsesActivationCameraAimCorrection,
	"MHGZ.M6.AwakenedKinsect.ActivationUsesCameraAimCorrection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM6AwakenedKinsectUsesActivationCameraAimCorrection::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	FMHGZM3Harness H;
	if (!H.Setup(false, AMHGZM6AwakenedKinsectTestCharacter::StaticClass()))
	{
		AddError(TEXT("M3 runtime harness setup failed"));
		return false;
	}

	const FGameplayAbilitySpecHandle AbilityHandle =
		H.GiveAbility(UMHGZM6AwakenedKinsectAbilityTestDouble::StaticClass());
	FWeaponInputSnapshot Input = MakeAwakenedInput(91);
	Input.Direction = EDirectionalInput::None;
	Input.WorldDirection = FVector::ZeroVector;
	Input.ActorForward = FVector::ForwardVector;
	Input.Aim.Direction = FVector::RightVector;
	H.Character->SetActorRotation(FRotator::ZeroRotator);
	TestTrue(TEXT("RTYB activates with a frozen camera aim and neutral left stick"),
		H.TryActivateWithInput(AbilityHandle, Input));
	UMHGZM6AwakenedKinsectAbilityTestDouble* Ability =
		FindAwakenedInstance(*H.ASC, AbilityHandle);
	if (TestNotNull(TEXT("activation resolves the awakened ability instance"), Ability))
	{
		TestTrue(TEXT("AwakenedKinsect uses a 90-degree camera-aim correction cap"),
			FMath::IsNearlyEqual(Ability->MaxCorrectionAngle, 90.0f, 0.001f));
		Ability->ApplyActivationDirectionCorrectionForTest();
		TestTrue(TEXT("activation immediately corrects toward camera aim despite neutral left stick"),
			FMath::IsNearlyEqual(
				FRotator::NormalizeAxis(H.Character->GetActorRotation().Yaw),
				90.0f, 0.01f));
	}

	H.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM6AwakenedKinsectRTYBReleaseControls,
	"MHGZ.M6.AwakenedKinsect.RTYBChordCompletesOnEitherYBRelease",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM6AwakenedKinsectRTYBReleaseControls::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FGameplayTag Y = M3::Tag(TEXT("Input.Weapon.Y"));
	const FGameplayTag B = M3::Tag(TEXT("Input.Weapon.B"));
	const FGameplayTag RT = M3::Tag(TEXT("Input.Modifier.RT"));
	const FGameplayTag RTYB = M3::Tag(TEXT("Input.Weapon.RTYB"));

	for (const FGameplayTag& FirstReleased : { Y, B })
	{
		UWeaponInputProfile* Profile = NewObject<UWeaponInputProfile>();
		Profile->ChordGracePeriod = 0.25f;
		FWeaponChordDefinition& Chord = Profile->Chords.AddDefaulted_GetRef();
		Chord.OutputTag = RTYB;
		Chord.TriggerControls = { Y, B };
		Chord.RequiredHeldModifiers = { RT };
		Chord.bRequireExactModifiers = true;
		Chord.bConsumeTriggerControls = true;
		Chord.AimSnapshotContext = EWeaponAimSnapshotContext::Action;
		Chord.ReleaseControlTag = Y;
		Chord.AdditionalReleaseControlTags = { B };

		UMHGZWeaponInputRouterComponent* Router =
			NewObject<UMHGZWeaponInputRouterComponent>();
		Router->SetInputProfile(Profile);
		Router->HandlePhysicalStarted(RT, 0.0);
		Router->HandlePhysicalStarted(B, 0.1);
		Router->HandlePhysicalStarted(Y, 0.2);

		const TArray<FWeaponInputSnapshot>& StartedSnapshots =
			Router->GetCapturedSnapshots();
		TestEqual(FString::Printf(TEXT("%s-first: one RTYB Started event"),
			*FirstReleased.ToString()), StartedSnapshots.Num(), 1);
		if (StartedSnapshots.Num() != 1)
		{
			continue;
		}
		TestEqual(TEXT("resolved input is RTYB"),
			StartedSnapshots[0].ResolvedInputTag, RTYB);
		const uint32 ActivationSequence = StartedSnapshots[0].SequenceID;
		const FGameplayTag ActivationSource = StartedSnapshots[0].SourceControlTag;

		Router->HandlePhysicalCompleted(FirstReleased, 0.3);
		const TArray<FWeaponInputSnapshot>& FirstReleaseSnapshots =
			Router->GetCapturedSnapshots();
		TestEqual(FString::Printf(TEXT("%s-first: chord emits Completed"),
			*FirstReleased.ToString()), FirstReleaseSnapshots.Num(), 2);
		if (FirstReleaseSnapshots.Num() >= 2)
		{
			const FWeaponInputSnapshot& Release = FirstReleaseSnapshots.Last();
			TestEqual(TEXT("release retains RTYB identity"), Release.ResolvedInputTag, RTYB);
			TestEqual(TEXT("release retains activation sequence"), Release.SequenceID,
				ActivationSequence);
			TestEqual(TEXT("release retains activation source"), Release.SourceControlTag,
				ActivationSource);
			TestEqual(TEXT("release phase is Completed"), Release.Phase,
				EWeaponInputPhase::Completed);
		}
		TestTrue(TEXT("RT stays held; it is not the hold-release control"),
			Router->IsPhysicalInputHeld(RT));

		const FGameplayTag SecondReleased = FirstReleased == Y ? B : Y;
		Router->HandlePhysicalCompleted(SecondReleased, 0.4);
		const TArray<FWeaponInputSnapshot>& BothReleasedSnapshots =
			Router->GetCapturedSnapshots();
		TestEqual(TEXT("second trigger release also closes the same activation"),
			BothReleasedSnapshots.Num(), 3);
		if (BothReleasedSnapshots.Num() >= 3)
		{
			TestEqual(TEXT("second release has the same RTYB identity"),
				BothReleasedSnapshots.Last().ResolvedInputTag, RTYB);
			TestEqual(TEXT("second release is still the same activation"),
				BothReleasedSnapshots.Last().SequenceID, ActivationSequence);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM6AwakenedKinsectAimAxisClamp,
	"MHGZ.M6.AwakenedKinsect.AimYawPitchClampIndependently",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM6AwakenedKinsectAimAxisClamp::RunTest(const FString& Parameters)
{
	auto MakeDirection = [](const float YawDegrees, const float PitchDegrees)
	{
		const float Yaw = FMath::DegreesToRadians(YawDegrees);
		const float Pitch = FMath::DegreesToRadians(PitchDegrees);
		const float CosPitch = FMath::Cos(Pitch);
		return FVector(CosPitch * FMath::Cos(Yaw),
			CosPitch * FMath::Sin(Yaw), FMath::Sin(Pitch));
	};

	FVector LockedDirection;
	TestTrue(TEXT("valid frozen aim is accepted"),
		UMHGZAwakenedKinsectAttackAbility::ResolveLockedAimDirection(
			FVector::ForwardVector, MakeDirection(80.f, 10.f), 90.f, 60.f,
			LockedDirection));
	FRotator LockedRotation = LockedDirection.Rotation();
	TestTrue(TEXT("yaw below the 90-degree cap remains unchanged"),
		FMath::IsNearlyEqual(LockedRotation.Yaw, 80.f, 0.01f));
	TestTrue(TEXT("pitch below its cap remains unchanged"),
		FMath::IsNearlyEqual(LockedRotation.Pitch, 10.f, 0.01f));

	TestTrue(TEXT("both axes independently clamp"),
		UMHGZAwakenedKinsectAttackAbility::ResolveLockedAimDirection(
			FVector::ForwardVector, MakeDirection(-120.f, -70.f), 90.f, 60.f,
			LockedDirection));
	LockedRotation = LockedDirection.Rotation();
	TestTrue(TEXT("negative yaw clamps independently to 90 degrees"),
		FMath::IsNearlyEqual(LockedRotation.Yaw, -90.f, 0.01f));
	TestTrue(TEXT("negative pitch clamps independently"),
		FMath::IsNearlyEqual(LockedRotation.Pitch, -60.f, 0.01f));

	TestTrue(TEXT("positive horizontal correction clamps at 90 degrees"),
		UMHGZAwakenedKinsectAttackAbility::ResolveLockedAimDirection(
			FVector::ForwardVector, MakeDirection(135.f, 0.f), 90.f, 60.f,
			LockedDirection));
	TestTrue(TEXT("positive yaw is capped at 90 degrees"),
		FMath::IsNearlyEqual(LockedDirection.Rotation().Yaw, 90.f, 0.01f));
	TestTrue(TEXT("pitch independently clamps at positive 60 degrees"),
		UMHGZAwakenedKinsectAttackAbility::ResolveLockedAimDirection(
			FVector::ForwardVector, MakeDirection(25.f, 80.f), 90.f, 60.f,
			LockedDirection));
	TestTrue(TEXT("positive pitch is capped at 60 degrees"),
		FMath::IsNearlyEqual(LockedDirection.Rotation().Pitch, 60.f, 0.01f));
	FWeaponMovementRequest MovementRequest;
	TestTrue(TEXT("max-up aim keeps a strong horizontal launch component"),
		UMHGZAwakenedKinsectAttackAbility::BuildHunterFlightMovementRequest(
			LockedDirection, 4826.0f, 0.38f, 1190.0f, MovementRequest));
	TestTrue(TEXT("max-up aim does not degenerate into a vertical-only launch"),
		MovementRequest.LaunchVelocity.Size2D() > 2400.0f);
	TestTrue(TEXT("horizontal release aim produces a valid hunter-flight request"),
		UMHGZAwakenedKinsectAttackAbility::ResolveLockedAimDirection(
			FVector::ForwardVector, MakeDirection(35.f, 0.f), 90.f, 60.f,
			LockedDirection)
		&& UMHGZAwakenedKinsectAttackAbility::BuildHunterFlightMovementRequest(
			LockedDirection, 4826.0f, 0.38f, 1190.0f, MovementRequest));
	TestTrue(TEXT("level aim retains full horizontal launch speed"),
		FMath::IsNearlyEqual(MovementRequest.LaunchVelocity.Size2D(),
			4826.0f, 0.1f));
	TestTrue(TEXT("downward release aim produces a valid falling launch request"),
		UMHGZAwakenedKinsectAttackAbility::ResolveLockedAimDirection(
			FVector::ForwardVector, MakeDirection(-30.f, -45.f), 90.f, 60.f,
			LockedDirection)
	&& UMHGZAwakenedKinsectAttackAbility::BuildHunterFlightMovementRequest(
			LockedDirection, 4826.0f, 0.38f, 1190.0f, MovementRequest));
	TestTrue(TEXT("downward aim preserves its negative vertical component"),
		MovementRequest.LaunchVelocity.Z < 0.f
		&& MovementRequest.LaunchVelocity.Size2D() > 3000.f);

	TestFalse(TEXT("zero aim is rejected"),
		UMHGZAwakenedKinsectAttackAbility::ResolveLockedAimDirection(
			FVector::ForwardVector, FVector::ZeroVector, 90.f, 60.f,
			LockedDirection));
	TestFalse(TEXT("invalid angle is rejected"),
		UMHGZAwakenedKinsectAttackAbility::ResolveLockedAimDirection(
			FVector::ForwardVector, FVector::ForwardVector, 181.f, 60.f,
			LockedDirection));
	TestFalse(TEXT("pitch cap above 90 degrees is rejected"),
		UMHGZAwakenedKinsectAttackAbility::ResolveLockedAimDirection(
			FVector::ForwardVector, FVector::ForwardVector, 90.f, 91.f,
			LockedDirection));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM6AwakenedKinsectFlightTuning,
	"MHGZ.M6.AwakenedKinsect.FixedDurationAndSpeedProfile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM6AwakenedKinsectFlightTuning::RunTest(const FString& Parameters)
{
	(void)Parameters;
	constexpr float NotifyIntervalSeconds = 0.88397956f;

	float NormalDistance = 0.0f;
	float NormalSpeed = 0.0f;
	TestTrue(TEXT("ordinary branch uses a fixed speed and derives range from notify interval"),
		UMHGZAwakenedKinsectAttackAbility::BuildAwakenedKinsectFlightProfile(
			NotifyIntervalSeconds, false, NormalDistance, NormalSpeed));
	TestTrue(TEXT("ordinary speed is fixed from the 1833.88 cm hunter-flight reference and audited event interval"),
		FMath::IsNearlyEqual(NormalSpeed, 2074.573f, 0.01f));
	TestTrue(TEXT("ordinary miss range matches the nominal 199 hunter-flight reference"),
		FMath::IsNearlyEqual(NormalDistance, 1833.88f, 0.05f));

	float EmpoweredDistance = 0.0f;
	float EmpoweredSpeed = 0.0f;
	TestTrue(TEXT("TripleUp branch uses the same interval and a 1.5x speed"),
		UMHGZAwakenedKinsectAttackAbility::BuildAwakenedKinsectFlightProfile(
			NotifyIntervalSeconds, true, EmpoweredDistance, EmpoweredSpeed));
	TestTrue(TEXT("TripleUp speed is exactly 1.5x ordinary speed"),
		FMath::IsNearlyEqual(EmpoweredSpeed, 3111.8595f, 0.01f));
	TestTrue(TEXT("TripleUp range differs only as a consequence of its speed"),
		FMath::IsNearlyEqual(EmpoweredDistance, NormalDistance * 1.5f, 0.01f));

	TestFalse(TEXT("zero notify interval is rejected"),
		UMHGZAwakenedKinsectAttackAbility::BuildAwakenedKinsectFlightProfile(
			0.0f, false, NormalDistance, NormalSpeed));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM6AwakenedKinsectMontageNotifyInterval,
	"MHGZ.M6.AwakenedKinsect.PersistentMontageHasOneLaunchAndTakeoffInterval",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM6AwakenedKinsectMontageNotifyInterval::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr,
		TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_JueChongJi.AM_IG_JueChongJi"));
	if (!TestNotNull(TEXT("persistent Awakened Kinsect Montage loads"), Montage))
	{
		return false;
	}
	float DurationSeconds = 0.0f;
	TestTrue(TEXT("Montage contains exactly one KinsectLaunch before HunterFlightStart"),
		UMHGZAwakenedKinsectAttackAbility::GetAuthoredKinsectFlightDuration(
			*Montage, DurationSeconds));
	TestTrue(TEXT("authored event interval remains the audited 0.88397956 seconds"),
		FMath::IsNearlyEqual(DurationSeconds, 0.88397956f, 0.00005f));
	AddInfo(FString::Printf(TEXT("KinsectLaunch -> HunterFlightStart authored interval: %.8f s"),
		DurationSeconds));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM6AwakenedKinsectHunterFlightRequest,
	"MHGZ.M6.AwakenedKinsect.HunterFlightRequestUsesLockedAimAndAuditedTuning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM6AwakenedKinsectHunterFlightRequest::RunTest(const FString& Parameters)
{
	FWeaponMovementRequest Request;
	const FVector Direction(0.6f, 0.0f, 0.8f);
	TestTrue(TEXT("finite locked aim and F9 candidate tuning build a request"),
		UMHGZAwakenedKinsectAttackAbility::BuildHunterFlightMovementRequest(
			Direction, 4826.0f, 0.38f, 1190.0f, Request));
	TestEqual(TEXT("199 uses the existing swept recorded-curve lease"),
		Request.Mode, EWeaponMovementMode::RecordedLocalTrajectory);
	TestEqual(TEXT("199 protects its measured phase against initial ground contact"),
		Request.TrajectoryGroundPolicy, ERecordedTrajectoryGroundPolicy::ProtectedFlight);
	TestTrue(TEXT("direction is normalized from the locked aim"),
		Request.DirectionSnapshot.Equals(Direction.GetSafeNormal(), 0.001f));
	TestTrue(TEXT("speed magnitude follows the F9 candidate, independent of pitch"),
		FMath::IsNearlyEqual(Request.LaunchVelocity.Size(), 4826.0f, 0.1f));
	TestTrue(TEXT("vertical launch component follows the locked pitch"),
		FMath::IsNearlyEqual(Request.LaunchVelocity.Z, 3860.8f, 0.2f));
	TestTrue(TEXT("request stores measured flight duration and gravity"),
		FMath::IsNearlyEqual(Request.Duration, 0.38f)
			&& FMath::IsNearlyEqual(Request.GravityMagnitude, 1190.0f));
	TestTrue(TEXT("request satisfies existing recorded-curve validation"),
		Request.HasValidRecordedTrajectoryParameters());
	for (const float Time : { 0.0f, 0.008333f, 0.1f, 0.19f, 0.38f })
	{
		const FVector ExpectedWorld = Direction * (4826.0f * Time)
			+ FVector(0.0, 0.0, -0.5 * 1190.0 * Time * Time);
		TestTrue(FString::Printf(TEXT("curve reproduces measured ballistic position at %.6fs"), Time),
			MHGZRecordedLocalTrajectory::LocalToWorld(Request.GetRecordedTrajectoryOffset(Time),
				Request.DirectionSnapshot).Equals(ExpectedWorld, 0.05f));
		const FVector ExpectedVelocity = Time < Request.RecordedTrajectory.CoveredSeconds
			? Direction * 4826.0 + FVector(0.0, 0.0, -1190.0 * Time) : FVector::ZeroVector;
		TestTrue(FString::Printf(TEXT("analytic velocity at %.6fs, bounded by HoldLastKey at phase end"), Time),
			MHGZRecordedLocalTrajectory::LocalToWorld(Request.GetRecordedTrajectoryVelocity(Time),
				Request.DirectionSnapshot).Equals(ExpectedVelocity, 0.1f));
	}
	TestTrue(TEXT("profile preserves the analytic pre-end tangent for explicit handoff"),
		MHGZRecordedLocalTrajectory::LocalToWorld(Request.RecordedTrajectory.TerminalVelocityLocal,
			Request.DirectionSnapshot).Equals(Direction * 4826.0
				+ FVector(0.0, 0.0, -1190.0 * 0.38), 0.1f));
	TestFalse(TEXT("zero speed is rejected"),
		UMHGZAwakenedKinsectAttackAbility::BuildHunterFlightMovementRequest(
			Direction, 0.0f, 0.38f, 1190.0f, Request));
	TestTrue(TEXT("horizontal pitch still creates a valid flight request"),
		UMHGZAwakenedKinsectAttackAbility::BuildHunterFlightMovementRequest(
			FVector::ForwardVector, 4826.0f, 0.38f, 1190.0f, Request));
	TestTrue(TEXT("a vertical tuning extreme still gets a valid planar curve basis"),
		UMHGZAwakenedKinsectAttackAbility::BuildHunterFlightMovementRequest(
			FVector::UpVector, 4826.0f, 0.38f, 1190.0f, Request)
			&& !Request.DirectionSnapshot.GetSafeNormal2D().IsNearlyZero());
	TestTrue(TEXT("vertical basis fallback never introduces sideways displacement"),
		MHGZRecordedLocalTrajectory::LocalToWorld(Request.GetRecordedTrajectoryOffset(0.1f),
			Request.DirectionSnapshot).Equals(FVector(0.0, 0.0, 482.6 - 5.95), 0.05f));
	for (const float Distance : { 0.0f, 200.0f, 549.0f, 550.0f, 551.0f, 800.0f, 1200.0f, 2500.0f })
	{
		TestTrue(TEXT("ground target builds a valid swept curve for the whole distance range"),
			UMHGZAwakenedKinsectAttackAbility::BuildGroundTargetHunterFlightMovementRequest(
				FVector::ForwardVector, FVector(0.0, 0.0, 98.0), FVector(Distance, 0.0, 0.0),
				4826.0f, 0.38f, 1190.0f, 550.0f, 140.0f, Request));
		const float ExpectedDistance = FMath::Clamp(Distance, 550.0f, 4826.0f * 0.38f);
		TestTrue(TEXT("distance changes continuously, not via an angle/speed band"),
			FMath::IsNearlyEqual(Request.GetRecordedTrajectoryOffset(0.38f).X, ExpectedDistance, 0.1f));
		TestTrue(TEXT("the small arc starts upward and peaks around 8cm"),
			FMath::IsNearlyEqual(Request.LaunchVelocity.Z, 140.0, 0.01)
			&& FMath::IsNearlyEqual(Request.GetRecordedTrajectoryOffset(140.0f / 1190.0f).Z, 8.235f, 0.05f));
	}
	TestTrue(TEXT("a reticle landing behind the hunter does not turn the ground dash sideways"),
		UMHGZAwakenedKinsectAttackAbility::BuildGroundTargetHunterFlightMovementRequest(
			FVector::ForwardVector, FVector(0.0, 0.0, 98.0), FVector(-100.0, 0.0, 0.0),
			4826.0f, 0.38f, 1190.0f, 550.0f, 140.0f, Request)
		&& Request.LaunchVelocity.X > 0.0 && FMath::IsNearlyZero(Request.LaunchVelocity.Y));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM6AwakenedKinsectHunterFlightLifecycle,
	"MHGZ.M6.AwakenedKinsect.HunterFlightHandoffInterruptionAndLanding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM6AwakenedKinsectHunterFlightLifecycle::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FGameplayTag FallingTag = M3::Tag(TEXT("Combat.State.Aerial.Falling"));
	for (const int32 FlightCase : { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 })
	{
		const bool bInterruptFlight = FlightCase == 1;
		const bool bWithFloor = FlightCase == 2 || FlightCase == 3 || FlightCase == 6 || FlightCase >= 8;
		const bool bGroundTargetFlight = FlightCase == 6 || FlightCase >= 8;
		const bool bWithWall = FlightCase == 4;
		const bool bImmediateHandoff = FlightCase == 5;
		FMHGZM3Harness H;
		const FString Case = FString::Printf(TEXT("flight case %d (air/interrupt/floor/down-floor/wall/handoff/lowest-floor/lowest-air)"), FlightCase);
		if (!TestTrue(Case + TEXT(" builds a real project character/CMC runtime"),
			H.Setup(false, AMHGZM6AwakenedKinsectTestCharacter::StaticClass())))
		{
			H.Teardown();
			return false;
		}

		UMHGZInstrumentedCharacterMovementComponent* CMC =
			Cast<UMHGZInstrumentedCharacterMovementComponent>(
				H.Character->GetCharacterMovement());
		if (!TestNotNull(Case + TEXT(" has the instrumented production CMC"), CMC))
		{
			H.Teardown();
			return false;
		}
		USkeletalMesh* BodyAsset = LoadObject<USkeletalMesh>(nullptr,
			TEXT("/Game/Characters/Demo/Meshes/Body/SKM_Demo_Body.SKM_Demo_Body"));
		UAnimMontage* AttackMontage = LoadObject<UAnimMontage>(nullptr,
			TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_JueChongJi.AM_IG_JueChongJi"));
		USkeletalMeshComponent* BodyMesh = H.Character->GetMesh();
		if (!TestNotNull(Case + TEXT(" loads the compatible production body mesh"), BodyAsset)
			|| !TestNotNull(Case + TEXT(" loads the persistent awakened-kinsect Montage"), AttackMontage)
			|| !TestNotNull(Case + TEXT(" has the character body mesh component"), BodyMesh))
		{
			H.Teardown();
			return false;
		}
		BodyMesh->SetSkeletalMesh(BodyAsset);
		BodyMesh->SetAnimInstanceClass(UAnimInstance::StaticClass());
		H.ASC->RefreshAbilityActorInfo();

		IGTest::Action<UMHGZAwakenedKinsectAttackAbility>(H.CombatConfig)->AwakenedHunterFlightDurationSeconds = 0.38f;
		IGTest::Action<UMHGZAwakenedKinsectAttackAbility>(H.CombatConfig)->AwakenedHunterFlightSpeed = 4826.0f;
		IGTest::Action<UMHGZAwakenedKinsectAttackAbility>(H.CombatConfig)->AwakenedHunterFlightGravityMagnitude = 1190.0f;
		IGTest::Action<UMHGZAwakenedKinsectAttackAbility>(H.CombatConfig)->AwakenedHunterFallGravityMagnitude = 2885.0f;
		IGTest::Action<UMHGZAwakenedKinsectAttackAbility>(H.CombatConfig)->AwakenedHunterFallHandoffVelocity = FVector(0.0f, 0.0f, 900.0f);
		H.CombatConfig->AerialLandingMontage = LoadObject<UAnimMontage>(nullptr,
			TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_AerialLanding.AM_IG_AerialLanding"));
		if (!TestNotNull(TEXT("loads the shared persistent aerial landing montage"), H.CombatConfig->AerialLandingMontage.Get()))
		{
			H.Teardown();
			return false;
		}
		TestTrue(TEXT("shared landing retains its authored duration without regeneration or trimming"),
			FMath::IsNearlyEqual(H.CombatConfig->AerialLandingMontage->GetPlayLength(), 0.583333f, 0.001f));
		CMC->GravityScale = 1.25f;
		CMC->BrakingDecelerationFalling = 123.0f;
		CMC->FallingLateralFriction = 0.27f;
		H.Host->SetGrounded(true);
		CMC->SetMovementMode(MOVE_Walking);
		CMC->bRunPhysicsWithNoController = true;
		H.Character->SetActorLocation(FVector(0.0, 0.0, 1000.0));
		if (bWithFloor || bWithWall)
		{
			AActor* Obstacle = H.World->SpawnActor<AActor>();
			UBoxComponent* Box = NewObject<UBoxComponent>(Obstacle);
			Obstacle->SetRootComponent(Box);
			Box->SetBoxExtent(bWithFloor ? FVector(4000.0, 4000.0, 50.0)
				: FVector(50.0, 4000.0, 4000.0));
			Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			Box->SetCollisionObjectType(ECC_WorldStatic);
			Box->SetCollisionResponseToAllChannels(ECR_Block);
			Box->RegisterComponent();
			Obstacle->SetActorLocation(bWithFloor ? FVector(0.0, 0.0, -50.0)
				: FVector(400.0, 0.0, 1000.0));
			if (bWithFloor)
			{
				H.Character->SetActorLocation(FVector(0.0, 0.0,
					H.Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.15));
			}
		}
		if (!TestNotNull(Case + TEXT(" has the configured weapon-tail socket"),
			AddKinsectWeaponSocketForTest(*H.Character)))
		{
			H.Teardown();
			return false;
		}

		const FGameplayAbilitySpecHandle AbilityHandle =
			H.GiveAbility(UMHGZM6AwakenedKinsectAbilityTestDouble::StaticClass());
		FWeaponInputSnapshot Input = MakeAwakenedInput(70u + (bInterruptFlight ? 1u : 0u));
		Input.Aim.Direction = FVector(0.8f, 0.0f, 0.6f).GetSafeNormal();
		if (bWithFloor)
		{
			Input.Aim.Direction = FlightCase == 2 ? FVector::ForwardVector
				: FVector(0.98, 0.0, -0.2).GetSafeNormal();
		}
		if (FlightCase == 6 || FlightCase == 7)
		{
			Input.Aim.Direction = FRotator(-66.09f, 0.0f, 0.0f).Vector();
		}
		Input.Aim.TargetPoint = H.Character->GetActorLocation() + Input.Aim.Direction * 1000.f;
		FHitResult GroundHit;
		if (bGroundTargetFlight || FlightCase == 7)
		{
			Input.Aim.TargetPoint = FVector(FlightCase == 8 ? 800.0 : FlightCase == 9 ? 2500.0
				: FlightCase == 10 ? -100.0 : 100.0, 0.0, 0.0);
			GroundHit.bBlockingHit = true;
			GroundHit.Time = 0.5f;
			GroundHit.Location = GroundHit.ImpactPoint = Input.Aim.TargetPoint;
			GroundHit.Normal = GroundHit.ImpactNormal = FVector::UpVector;
		}
		if (!TestTrue(Case + TEXT(" activates the production action transaction"),
			H.TryActivateWithInput(AbilityHandle, Input)))
		{
			H.Teardown();
			return false;
		}

		UMHGZM6AwakenedKinsectAbilityTestDouble* Ability =
			FindAwakenedInstance(*H.ASC, AbilityHandle);
		if (!TestNotNull(Case + TEXT(" resolves its active ability instance"), Ability))
		{
			H.Teardown();
			return false;
		}
		const FWeaponActionToken ActionToken = Ability->GetActionToken();
		Ability->SetReleaseAimSnapshotForTest(Input.ActorForward,
			Input.Aim.Direction, Input.Aim.TargetPoint, bGroundTargetFlight || FlightCase == 7 ? &GroundHit : nullptr);
		if (!TestTrue(Case + TEXT(" assigns the real attack Montage to the ability"),
			Ability->ConfigureAttackMontageForTest(AttackMontage))
			|| !TestTrue(Case + TEXT(" starts and registers the production Montage instance"),
				Ability->StartProductionAttackMontageForTest(
					*H.Character, AttackMontage, FName(TEXT("Startup193")))))
		{
			H.Teardown();
			return false;
		}
		UAnimInstance* AnimInstance = BodyMesh->GetAnimInstance();
		FAnimMontageInstance* AttackMontageInstance = AnimInstance
			? AnimInstance->GetActiveInstanceForMontage(AttackMontage) : nullptr;
		if (!TestNotNull(Case + TEXT(" resolves the active exact Montage instance"),
			AttackMontageInstance)
			|| !TestTrue(Case + TEXT(" production Montage carries root motion before 199"),
				AttackMontage->HasRootMotion()))
		{
			H.Teardown();
			return false;
		}
		TestTrue(Case + TEXT(" begins HunterFlight while Host still reports grounded"),
			H.Host->IsGrounded());
		TestTrue(Case + TEXT(" attaches the real Kinsect to the weapon before launch"),
			Ability->PrepareKinsectAttachmentForTest());
		TestTrue(Case + TEXT(" commits the real kinsect launch request"),
			Ability->CommitKinsectLaunch(ActionToken));
		AttackMontageInstance->OnMontageSectionChanged.ExecuteIfBound(
			AttackMontage, FName(TEXT("HunterFlight199")), false);
		TestTrue(Case + TEXT(" notify remains idempotent after section fallback starts flight"),
			Ability->HandleAwakenedKinsectEvent(ActionToken,
				EAwakenedKinsectAttackEvent::HunterFlightStart));
		UAbilityTask_MHGZWeaponMovement* FlightTask =
			Ability->GetHunterFlightMovementTaskForTest();
		// The existing aim contract clamps pitch to +/-60 degrees. Keep checking
		// the whole frozen direction, not just speed, for the free-air regression.
		FVector ExpectedAirDirection;
		TestTrue(TEXT("resolves the unchanged airborne point-target direction"),
			UMHGZAwakenedKinsectAttackAbility::ResolveLockedTargetDirection(Input.ActorForward,
				Input.Aim.TargetPoint, H.Character->GetActorLocation(), Input.Aim.Direction,
				90.0f, 60.0f, ExpectedAirDirection));
		const float ExpectedGroundDistance = FlightCase == 8 ? 800.0f
			: FlightCase == 9 ? 4826.0f * 0.38f : 550.0f;
		if (!TestNotNull(Case + TEXT(" retains the started flight task"), FlightTask)
			|| !TestTrue(Case + TEXT(" disables root-motion extraction on only the active attack instance"),
				AttackMontageInstance->IsRootMotionDisabled())
			|| !TestTrue(Case + TEXT(" has exactly one Host movement owner"),
				H.Host->IsActionMovementOwnedBy(ActionToken))
			|| !TestTrue(Case + TEXT(" installs the audited 199 gravity"),
				FMath::IsNearlyEqual(FMath::Abs(CMC->GetGravityZ()), 1190.0f, 1.0f))
			|| !TestTrue(Case + TEXT(" launch follows the selected frozen-view model"),
				bGroundTargetFlight ? CMC->Velocity.Equals(FVector(ExpectedGroundDistance / 0.38f, 0.0, 140.0), 0.1f)
					: CMC->Velocity.Equals(ExpectedAirDirection * 4826.0f, 0.1f))
			|| !TestTrue(Case + TEXT(" transitions the grounded CMC into protected curve flight"),
				CMC->MovementMode == MOVE_Flying))
		{
			H.Teardown();
			return false;
		}
		H.Host->SetGrounded(false);
		if (bWithFloor)
		{
			TestTrue(Case + TEXT(" seeds an airborne dance stack for landing cleanup"),
				H.Resource->AddDanceStack(EIGDanceSource::AwakenedKinsect));
		}

		if (bInterruptFlight || bImmediateHandoff)
		{
			if (bImmediateHandoff)
			{
				FWeaponActionToken StaleToken = ActionToken;
				++StaleToken.ActivationSequenceID;
				TestFalse(TEXT("a stale hit cannot retire the current recorded 199"),
					FlightTask->EndForImmediateMovementHandoff(StaleToken));
				TestTrue(TEXT("current-action hit can retire recorded 199 without fake landing"),
					FlightTask->EndForImmediateMovementHandoff(ActionToken));
				TestEqual(TEXT("recorded-to-prep handoff keeps Flying"), CMC->MovementMode, MOVE_Flying);
				TestFalse(TEXT("retiring recorded 199 releases its old movement lease"),
					H.Host->IsActionMovementOwned());
				FWeaponMovementRequest PrepRequest;
				PrepRequest.Mode = EWeaponMovementMode::StationaryHover;
				PrepRequest.Duration = 1.0f;
				PrepRequest.OwnerAction = ActionToken;
				UAbilityTask_MHGZWeaponMovement* Prep = UAbilityTask_MHGZWeaponMovement::StartWeaponMovement(
					Ability, TEXT("Awakened189HandoffRegression"), PrepRequest);
				if (TestNotNull(TEXT("189 preparation task can be installed"), Prep))
				{
					Prep->ReadyForActivation();
					TestTrue(TEXT("189 preparation actually acquires movement after curve retirement"),
						Prep->DidStartMovement() && H.Host->IsActionMovementOwnedBy(ActionToken));
				}
			}
			H.ASC->CancelAbilityHandle(AbilityHandle);
			TestFalse(TEXT("interruption ends the source GA"), Ability->IsActive());
			TestFalse(TEXT("interruption retires the task's Host movement lease"),
				H.Host->IsActionMovementOwnedBy(ActionToken));
			TestFalse(TEXT("interruption unregisters the action"), H.Host->HasRegisteredAction());
			TestEqual(TEXT("interruption recalls the action-owned insect"),
				H.Kinsect->GetState(), EKinsectState::Returning);
			TestTrue(TEXT("interruption returns to Host-owned ordinary falling state"),
				H.ASC->HasMatchingGameplayTag(FallingTag));
		}
		else
		{
			// Advance real swept CMC physics, not just the task timer. The old Falling
			// path ended the floor cases in ~0.033s and never reached the measured tail.
			const FVector Start = H.Character->GetActorLocation();
			for (int32 Step = 0; Step < 24 && Ability->IsActive(); ++Step)
			{
				CMC->PerformMovement(1.0f / 60.0f);
				FlightTask->TickTask(1.0f / 60.0f);
				if (Step < 21)
				{
					TestTrue(Case + TEXT(" retains 199 through the measured phase, including floor/wall contact"),
						Ability->IsActive() && H.Host->IsActionMovementOwnedBy(ActionToken));
				}
			}
			TestFalse(TEXT("completed 199 ends the source GA"), Ability->IsActive());
			TestFalse(TEXT("completed 199 releases the unique movement owner"),
				H.Host->IsActionMovementOwnedBy(ActionToken));
			TestFalse(TEXT("completed 199 unregisters its action"), H.Host->HasRegisteredAction());
			TestEqual(TEXT("completed HunterFlight recalls the insect regardless of hit"),
				H.Kinsect->GetState(), EKinsectState::Returning);
			if (bWithFloor)
			{
				const double Travelled = H.Character->GetActorLocation().X - Start.X;
				TestTrue(Case + TEXT(" matches the continuous capped ground-target or free-flight distance"),
					bGroundTargetFlight ? FMath::IsNearlyEqual(Travelled, static_cast<double>(ExpectedGroundDistance), 50.0)
						: Travelled > 1700.0);
				TestTrue(Case + TEXT(" returns to Walking only at the measured phase end"),
					CMC->IsMovingOnGround());
				TestTrue(Case + TEXT(" ground completion replaces dash inertia with the shared landing speed"),
					FMath::IsNearlyEqual(CMC->Velocity.Size2D(), H.CombatConfig->AerialLandingHorizontalSpeed, 0.1));
				TestTrue(Case + TEXT(" publishes a real landing presentation, not immediate MM idle"), H.Host->IsAerialLanding());
				TestTrue(Case + TEXT(" actually plays AM_IG_AerialLanding through the shared Host"),
					AnimInstance->Montage_IsPlaying(H.CombatConfig->AerialLandingMontage));
				TestFalse(Case + TEXT(" has no frozen Flying/falling gate on the floor"),
					H.ASC->HasMatchingGameplayTag(FallingTag));
				TestEqual(Case + TEXT(" shared landing transaction clears airborne dance stacks"),
					H.Resource->GetDanceStacks(), 0);
				TestFalse(Case + TEXT(" shared landing clears the consumed aerial attack budget"),
					H.ASC->HasMatchingGameplayTag(M3::Tag(TEXT("Combat.State.Aerial.CantAttack"))));
				TestFalse(Case + TEXT(" shared landing clears the consumed aerial dodge budget"),
					H.ASC->HasMatchingGameplayTag(M3::Tag(TEXT("Combat.State.Aerial.CantDodge"))));
			}
			else
			{
				TestTrue(TEXT("206 handoff leaves the CMC in falling mode"),
					CMC->MovementMode == MOVE_Falling);
				TestTrue(TEXT("206 handoff installs the configured vertical entry velocity"),
					FMath::IsNearlyEqual(CMC->Velocity.Z, 900.0f, 0.1f));
				TestTrue(TEXT("206 handoff publishes the Host Falling gate"),
					H.ASC->HasMatchingGameplayTag(FallingTag));
				if (bWithWall)
				{
					TestTrue(TEXT("wall sweeps block forward motion without catch-up/penetration"),
						H.Character->GetActorLocation().X - Start.X < 350.0);
				}
			}
		}

		// Mirror the Character::Landed callback after CMC has entered Walking. This
		// exercises the same production Host cleanup reached by a physical landing.
		CMC->SetMovementMode(MOVE_Walking);
		FHitResult LandingHit;
		CastChecked<AMHGZCharacter>(H.Character)->Landed(LandingHit);
		TestFalse(Case + TEXT(" landing clears Host Falling state"),
			H.ASC->HasMatchingGameplayTag(FallingTag));
		TestTrue(Case + TEXT(" landing restores pre-action gravity"),
			FMath::IsNearlyEqual(CMC->GravityScale, 1.25f, 0.001f));
		TestTrue(Case + TEXT(" landing restores pre-action braking"),
			FMath::IsNearlyEqual(CMC->BrakingDecelerationFalling, 123.0f, 0.001f));
		TestTrue(Case + TEXT(" landing restores pre-action lateral friction"),
			FMath::IsNearlyEqual(CMC->FallingLateralFriction, 0.27f, 0.001f));
		H.Teardown();
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZM6AwakenedHunterFlightMarks,
	"MHGZ.M6.AwakenedKinsect.HunterFlightSuccessfulDamageEstablishesMark",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM6AwakenedHunterFlightMarks::RunTest(const FString& Parameters)
{
	for (const bool bEmpowered : { false, true })
	{
		FMHGZM3Harness H;
		if (!TestTrue(TEXT("Awakened mark harness builds"), H.Setup(false, AMHGZM6AwakenedKinsectTestCharacter::StaticClass())))
		{
			H.Teardown(); return false;
		}
		USkeletalMeshComponent* Mesh = H.Character->GetMesh();
		Mesh->SetSkeletalMesh(LoadObject<USkeletalMesh>(nullptr,
			TEXT("/Game/Characters/Demo/Meshes/Body/SKM_Demo_Body.SKM_Demo_Body")));
		Mesh->SetAnimInstanceClass(UAnimInstance::StaticClass());
		H.ASC->RefreshAbilityActorInfo();
		IGTest::Action<UMHGZAwakenedKinsectAttackAbility>(H.CombatConfig)->AwakenedHunterFlightDurationSeconds = 0.38f;
		IGTest::Action<UMHGZAwakenedKinsectAttackAbility>(H.CombatConfig)->AwakenedHunterFlightSpeed = 4826.0f;
		IGTest::Action<UMHGZAwakenedKinsectAttackAbility>(H.CombatConfig)->AwakenedHunterFlightGravityMagnitude = 1190.0f;
		if (!TestNotNull(TEXT("real weapon-tail socket"), AddKinsectWeaponSocketForTest(*H.Character))) { H.Teardown(); return false; }
		if (bEmpowered) { TestTrue(TEXT("TripleUp branch"), AddTripleUp(*H.Resource)); }
		const FGameplayAbilitySpecHandle Handle = H.GiveAbility(UMHGZM6AwakenedKinsectAbilityTestDouble::StaticClass());
		const FWeaponInputSnapshot Input = MakeAwakenedInput(bEmpowered ? 101 : 100);
		if (!TestTrue(TEXT("production transaction activates"), H.TryActivateWithInput(Handle, Input))) { H.Teardown(); return false; }
		auto* Ability = FindAwakenedInstance(*H.ASC, Handle);
		UClass* TargetClass = LoadClass<AMHGZTrainingDummy>(nullptr,
			TEXT("/Game/Blueprints/Monster/TrainingDummy/BP_TrainingDummy.BP_TrainingDummy_C"));
		AMHGZTrainingDummy* Target = TargetClass
			? H.World->SpawnActor<AMHGZTrainingDummy>(TargetClass, FVector(300, 0, 0), FRotator::ZeroRotator) : nullptr;
		if (!TestNotNull(TEXT("active source"), Ability) || !TestNotNull(TEXT("mark target"), Target)) { H.Teardown(); return false; }
		TestEqual(TEXT("both requested branch modes are actually exercised"),
			Ability->IsEmpoweredKinsectBranch(), bEmpowered);
		if (!Target->HasActorBegunPlay()) { Target->DispatchBeginPlay(); }
		Target->GetAbilitySystemComponent()->InitAbilityActorInfo(Target, Target);
		TArray<UMHGZMonsterHitzoneComponent*> Zones;
		Target->GetComponents(Zones);
		if (!TestTrue(TEXT("target has hitzone metadata"), !Zones.IsEmpty())) { H.Teardown(); return false; }
		UMHGZMonsterHitzoneComponent* Zone = Zones[0];
		const FVector Point = Zone->GetComponentLocation() - FVector(75, 0, 0);
		FHitResult Hit(Target, Zone->GetQueryCollisionComponent(), Point, -FVector::ForwardVector);
		Hit.bBlockingHit = true;
		Hit.Location = Hit.ImpactPoint = Point;
		Hit.TraceStart = Point - FVector(200, 0, 0);
		Hit.TraceEnd = Point + FVector(200, 0, 0);
		Hit.Normal = Hit.ImpactNormal = -FVector::ForwardVector;
		Ability->AttackSegments.SetNum(1);
		Ability->AttackSegments[0].Damage.DamageEffectClass = UMHGZDamageGameplayEffect::StaticClass();
		Ability->AttackSegments[0].Damage.MotionValue = 10.0f;
		TestTrue(TEXT("preflight GE succeeds, but must not mark"), Ability->ApplyDamageForTest(Hit, 0));
		TestFalse(TEXT("no mark before HunterFlight"), H.Resource->HasValidKinsectMark());
		UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr,
			TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_JueChongJi.AM_IG_JueChongJi"));
		Ability->SetReleaseAimSnapshotForTest(Input.ActorForward, Input.Aim.Direction, Input.Aim.TargetPoint);
		if (!TestTrue(TEXT("real attack Montage starts"), Ability->ConfigureAttackMontageForTest(Montage)
			&& Ability->StartProductionAttackMontageForTest(*H.Character, Montage, TEXT("Startup193")))
			|| !TestTrue(TEXT("own kinsect attaches and launches"), Ability->PrepareKinsectAttachmentForTest()
				&& Ability->CommitKinsectLaunch(Ability->GetActionToken()))) { H.Teardown(); return false; }
		Mesh->GetAnimInstance()->Montage_JumpToSection(Ability->HunterFlightSection, Montage);
		TestTrue(TEXT("199 begins via exact production event"),
			Ability->HandleAwakenedKinsectEvent(Ability->GetActionToken(), EAwakenedKinsectAttackEvent::HunterFlightStart));
		Ability->AttackSegments[0].Damage.DamageEffectClass = nullptr;
		TestFalse(TEXT("failed flight GE cannot mark"), Ability->ApplyDamageForTest(Hit, 0));
		TestFalse(TEXT("mark absent after failed flight damage"), H.Resource->HasValidKinsectMark());
		Ability->AttackSegments[0].Damage.DamageEffectClass = UMHGZDamageGameplayEffect::StaticClass();
		TestTrue(TEXT("successful flight GE settles"), Ability->ApplyDamageForTest(Hit, 0));
		TestTrue(TEXT("ordinary and TripleUp hunter hits both mark"), H.Resource->HasValidKinsectMark());
		TestEqual(TEXT("mark identifies struck hitzone"), H.Resource->GetKinsectMarkHitzone(), Zone);
		const uint64 Serial = H.Resource->GetKinsectMarkSerial();
		TestTrue(TEXT("repeated callback GE succeeds"), Ability->ApplyDamageForTest(Hit, 0));
		TestEqual(TEXT("pending Dance prevents mark refresh"), H.Resource->GetKinsectMarkSerial(), Serial);
		H.ASC->CancelAbilityHandle(Handle); // End queued handoff without driving Dance's next tick.
		TestTrue(TEXT("ending action preserves mark"), H.Resource->HasValidKinsectMark());
		H.Teardown();
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
