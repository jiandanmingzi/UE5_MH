// Copyright MHGZ Project. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "MHGZCoAttackTestFixture.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "ActionSystem/MHGZDescendingThrustAbility.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimTypes.h"

using MHGZCoAttackTest::FCoAttackFixture;

class FMHGZDescendingThrustCoAttackTestAccess
{
public:
	static void Stamina(URes_InsectGlaive* R, float Value) { R->KinsectStamina = Value; }
	static void Pool(URes_InsectGlaive* R, float Maximum, float Value) { R->MaxKinsectStamina = Maximum; R->KinsectStamina = Value; }
	static bool Active(URes_InsectGlaive* R) { return R->DescendingThrustKinsect.IsSet(); }
};

namespace
{
void ReadyOnArm(FCoAttackFixture& F)
{
	F.H.Resource->RecallKinsect(EKinsectCommandSource::System);
	F.H.Kinsect->AttachToPlayer(F.H.Character->GetRootComponent());
}
FVector DiveTarget(FCoAttackFixture& F)
{
	return F.H.Character->GetCapsuleComponent()->GetComponentLocation()
		+ F.H.Character->GetActorForwardVector().GetSafeNormal2D() * 100.f;
}
bool BeginSpecial(FCoAttackFixture& F, const FVector& Origin)
{
	ReadyOnArm(F);
	return F.H.Resource->BeginDescendingThrustKinsect(F.Ability->GetActionToken(), Origin, FGuid::NewGuid(), 10);
}
void ResourceTick(FCoAttackFixture& F, float Dt = 0.f)
{
	F.H.Resource->TickComponent(Dt, LEVELTICK_All, nullptr);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZDescendingCoEntryTest,
	"MHGZ.Maintenance.KinsectCoAttack.DescendingThrust.EntryTenthStaminaAndAway",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZDescendingCoEntryTest::RunTest(const FString&)
{
	for (const float Stamina : {0.f, 9.999f, 10.f, 50.f})
	{
		FCoAttackFixture F;
		if (!TestTrue(TEXT("fixture"), F.Build())) return false;
		ReadyOnArm(F);
		FMHGZDescendingThrustCoAttackTestAccess::Stamina(F.H.Resource, Stamina);
		const FVector Origin = DiveTarget(F) + FVector(0, 0, 1000);
		const auto Token = F.Ability->GetActionToken();
		const bool Started = F.H.Resource->BeginDescendingThrustKinsect(Token, Origin, FGuid::NewGuid(), 10);
		TestEqual(TEXT("special admission requires at least 10%"), Started, Stamina >= 10.f);
		TestTrue(TEXT("admission failure never cancels hunter GA"), F.Ability->IsActive());
		if (!Started)
		{
			TestEqual(TEXT("low stamina leaves insect on arm"), F.H.Kinsect->State, EKinsectState::Attached);
			ResourceTick(F, .01f);
			TestFalse(TEXT("regeneration cannot retroactively admit special"), FMHGZDescendingThrustCoAttackTestAccess::Active(F.H.Resource));
			continue;
		}
		TestTrue(TEXT("detached at exact activation world position"), F.H.Kinsect->GetActorLocation().Equals(Origin, .01));
		const uint64 Serial = F.H.Kinsect->ActiveRequest.CommandSerial;
		F.H.Character->AddActorWorldOffset(FVector(500, 0, 0));
		F.H.Kinsect->Tick(.01);
		TestTrue(TEXT("wait does not follow owner"), F.H.Kinsect->GetActorLocation().Equals(Origin, .01));
		TestFalse(TEXT("already-away insect rejects a new special without teleport"),
			F.H.Resource->BeginDescendingThrustKinsect(Token, Origin + FVector(500, 0, 0), FGuid::NewGuid(), 10));
		TestEqual(TEXT("rejected away entry preserves serial"), F.H.Kinsect->ActiveRequest.CommandSerial, Serial);
		ResourceTick(F, .01f);
		TestEqual(TEXT("waiting remains hovering below admission threshold"), F.H.Kinsect->State, EKinsectState::Hovering);
		TestTrue(TEXT("waiting already consumes stamina"), F.H.Resource->GetKinsectStamina() < Stamina);
		TestTrue(TEXT("admitted dive can start after draining below threshold"), F.H.Resource->StartDescendingThrustKinsectDive(Token, DiveTarget(F)));
		ResourceTick(F, 100.f);
		TestEqual(TEXT("depletion still recalls insect"), F.H.Kinsect->State, EKinsectState::Returning);
		TestTrue(TEXT("depletion never cancels hunter GA"), F.Ability->IsActive());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZDescendingCoPierceTest,
	"MHGZ.Maintenance.KinsectCoAttack.DescendingThrust.PierceCargoAndExactEndpoint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZDescendingCoPierceTest::RunTest(const FString&)
{
	FCoAttackFixture F;
	if (!TestTrue(TEXT("fixture"), F.Build())) return false;
	const FVector Target = DiveTarget(F), Origin = Target + FVector(0, 0, 1000);
	if (!TestTrue(TEXT("special entry"), BeginSpecial(F, Origin))) return false;
	F.H.CombatConfig->AwakenedPierceHitInterval = .25f;
	const auto Token = F.Ability->GetActionToken();
	TestTrue(TEXT("starts one real ToPoint dive"), F.H.Resource->StartDescendingThrustKinsectDive(Token, Target));
	const auto Request = F.H.Kinsect->ActiveRequest;
	TestTrue(TEXT("resource preserves the caller's final-position target"), Request.TargetPointSnapshot.Equals(Target, .01));
	TestEqual(TEXT("shared awakened pierce interval"), Request.RehitInterval, F.H.CombatConfig->AwakenedPierceHitInterval);
	TestEqual(TEXT("species base flight speed"), Request.FlightSpeed, F.H.KinsectData->FlightSpeed);
	TestEqual(TEXT("kinsect MV10"), Request.MotionValue, 10.f);
	TestFalse(TEXT("repeated final-window retirement never restarts dive"), F.H.Resource->StartDescendingThrustKinsectDive(Token, Target));
	F.H.Host->OnWeaponComboWindowsClosed.Broadcast(Token);
	TestEqual(TEXT("early combo closure cannot stop in-flight dive"), F.H.Kinsect->State, EKinsectState::Flying);
	F.H.Character->AddActorWorldOffset(FVector(300, 300, 0));
	TestTrue(TEXT("target does not follow hunter movement"), F.H.Kinsect->ActiveRequest.TargetPointSnapshot.Equals(Target, .01));
	const FGameplayTag Red = M3::Tag(TEXT("WeaponResource.IG.Extract.Red"));
	const FGameplayTag White = M3::Tag(TEXT("WeaponResource.IG.Extract.White"));
	F.Part->ExtractColorTag = Red;
	F.SetTarget(Target + FVector(0, 0, 500));
	F.H.Kinsect->SetActorLocation(Target + FVector(0, 0, 520));
	F.H.World->TimeSeconds = .1; F.H.Kinsect->Tick(0);
	TestEqual(TEXT("first real piercing contact"), F.KinsectHits, 1);
	F.H.World->TimeSeconds = .2; F.H.Kinsect->Tick(0);
	TestEqual(TEXT("interval rejects early repeated contact"), F.KinsectHits, 1);
	F.H.World->TimeSeconds = .36; F.H.Kinsect->Tick(0);
	TestEqual(TEXT("interval permits next real contact"), F.KinsectHits, 2);
	F.Part->ExtractColorTag = White;
	F.H.World->TimeSeconds = .62; F.H.Kinsect->Tick(0);
	TestEqual(TEXT("third successful contact"), F.KinsectHits, 3);
	TestTrue(TEXT("different extracts remain kinsect cargo"),
		F.H.Kinsect->GetPendingExtractColors().HasTagExact(Red) && F.H.Kinsect->GetPendingExtractColors().HasTagExact(White));
	TestFalse(TEXT("hunter is not lit before delivery"), F.H.Resource->HasExtract(Red));
	F.SetTarget(Target - FVector(0, 0, 300)); // beyond the permitted segment
	F.H.Kinsect->SetActorLocation(Target - FVector(0, 0, 600));
	F.H.World->TimeSeconds = .9; F.H.Kinsect->Tick(0);
	TestEqual(TEXT("no damage beyond endpoint"), F.KinsectHits, 3);
	TestTrue(TEXT("overshoot clamps to exact endpoint"), F.H.Kinsect->GetActorLocation().Equals(Target, .01));
	TestEqual(TEXT("completed dive hovers, never auto-returns"), F.H.Kinsect->State, EKinsectState::Hovering);
	ResourceTick(F, .01f);
	TestFalse(TEXT("closed combo releases special admission after arrival"), FMHGZDescendingThrustCoAttackTestAccess::Active(F.H.Resource));
	TestTrue(TEXT("normal hover preserves collected cargo"), F.H.Kinsect->GetPendingExtractColors().HasTagExact(Red));
	F.H.Resource->RecallKinsect();
	F.H.Kinsect->SetActorLocation(F.H.Character->GetRootComponent()->GetComponentLocation());
	F.H.Kinsect->Tick(.01f);
	TestTrue(TEXT("return delivers red"), F.H.Resource->HasExtract(Red));
	TestTrue(TEXT("return delivers white"), F.H.Resource->HasExtract(White));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZDescendingCoHandoffTest,
	"MHGZ.Maintenance.KinsectCoAttack.DescendingThrust.DeferredHandoffSkipsMissedWindows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZDescendingCoHandoffTest::RunTest(const FString&)
{
	for (const bool ExpireBeforeArrival : {false, true})
	{
		FCoAttackFixture F;
		if (!TestTrue(TEXT("fixture"), F.Build())) return false;
		const FVector Target = DiveTarget(F), Origin = Target + FVector(0, 0, 1000);
		if (!TestTrue(TEXT("special entry"), BeginSpecial(F, Origin))) return false;
		const auto Old = F.Ability->GetActionToken();
		F.H.Resource->StartDescendingThrustKinsectDive(Old, Target);
		const auto Dive = F.H.Kinsect->ActiveRequest;
		const auto Failed = F.H.GiveAbility(UMHGZCoAttackRejectedTestAbility::StaticClass());
		TestNull(TEXT("failed activation cannot preempt dive"), F.Activate(Failed));
		TestEqual(TEXT("failure keeps dive serial"), F.H.Kinsect->ActiveRequest.CommandSerial, Dive.CommandSerial);
		F.H.Resource->FinishDescendingThrustKinsect(Old, EWeaponActionEndReason::Superseded);
		F.Ability->RequestEndAction(EWeaponActionEndReason::Superseded);
		F.H.Host->OnWeaponComboWindowsClosed.Broadcast(Old);
		const auto NextHandle = F.H.GiveAbility(UMHGZCoAttackTestAbility::StaticClass());
		auto* Next = F.Activate(NextHandle);
		if (!TestNotNull(TEXT("successor"), Next)) return false;
		FMHGZDescendingThrustCoAttackTestAccess::Stamina(F.H.Resource, 9.f);
		Next->EnableCollision(0); // must be skipped, not queued for later catch-up
		TestFalse(TEXT("no early sampler writer"), F.Sampler->IsRunning());
		TestEqual(TEXT("dive still owns actor"), F.H.Kinsect->State, EKinsectState::Flying);
		TestTrue(TEXT("registration does not teleport insect"), F.H.Kinsect->GetActorLocation().Equals(Origin, .01));
		if (ExpireBeforeArrival) Next->BeginDodgeAcceptWindow(TEXT("AlreadyRecovery"));
		F.H.Kinsect->SetActorLocation(Target); F.H.Kinsect->Tick(0);
		ResourceTick(F);
		TestEqual(TEXT("only valid successor begins following after arrival"), F.Sampler->IsRunning(), !ExpireBeforeArrival);
		if (ExpireBeforeArrival)
		{
			TestEqual(TEXT("expired successor leaves insect on standby"), F.H.Kinsect->State, EKinsectState::Hovering);
		}
		else
		{
			F.H.Resource->FinishDescendingThrustKinsect(Old, EWeaponActionEndReason::Interrupted);
			F.H.Host->OnWeaponComboWindowsClosed.Broadcast(Old);
			TestTrue(TEXT("old source cannot recall new cooperative owner"), F.Sampler->IsRunning());
			F.At(.05, F.Origin); F.At(.1, F.Origin); F.At(.15, F.Origin); F.At(.2, F.Origin); F.At(.3, F.Origin);
			TestEqual(TEXT("missed open window was not backfilled"), F.KinsectHits, 0);
			Next->DisableCollision(0); Next->EnableCollision(1);
			F.At(.35, F.Origin); Next->DisableCollision(1);
			F.At(.51, F.Origin); F.At(.56, F.Origin);
			TestEqual(TEXT("future window still has independent damage"), F.KinsectHits, 1);
		}
		F.H.ASC->ClearAbility(NextHandle);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZDescendingCoPreemptionTest,
	"MHGZ.Maintenance.KinsectCoAttack.DescendingThrust.ManualPreemptionAndStaleCallbacks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZDescendingCoPreemptionTest::RunTest(const FString&)
{
	FCoAttackFixture F;
	if (!TestTrue(TEXT("fixture"), F.Build())) return false;
	const FVector Target = DiveTarget(F);
	if (!TestTrue(TEXT("special entry"), BeginSpecial(F, Target + FVector(0, 0, 1000)))) return false;
	const auto Old = F.Ability->GetActionToken();
	F.H.Resource->StartDescendingThrustKinsectDive(Old, Target);
	const auto OldFlight = F.H.Kinsect->ActiveRequest;
	FKinsectFlightRequest R;
	R.RuntimeToken = Old.RuntimeToken; R.FlightInstanceID = FGuid::NewGuid();
	R.TrajectoryMode = EKinsectTrajectoryMode::ToPoint; R.TargetPointSnapshot = Target + FVector(1000, 0, 0);
	R.FlightSpeed = 2000; R.MaxDistance = 5000; R.ArrivalRadius = 1;
	TestTrue(TEXT("manual command preempts"), F.H.Resource->DeployKinsect(R));
	const auto Manual = F.H.Kinsect->ActiveRequest;
	TestFalse(TEXT("old resource session unbound"), FMHGZDescendingThrustCoAttackTestAccess::Active(F.H.Resource));
	F.H.Resource->FinishDescendingThrustKinsect(Old, EWeaponActionEndReason::Interrupted);
	F.H.Resource->OnKinsectFlightEnded(OldFlight, EKinsectFlightEndReason::Arrival);
	F.H.Host->OnWeaponComboWindowsClosed.Broadcast(Old);
	TestTrue(TEXT("stale callbacks do not overwrite new flight"), F.H.Kinsect->ActiveRequest.FlightInstanceID == Manual.FlightInstanceID);
	TestEqual(TEXT("new command keeps flying"), F.H.Kinsect->State, EKinsectState::Flying);
	// Real damage-GE callbacks can also issue commands synchronously, while the
	// old piercing sweep still has targets and arrival work left in its stack.
	TestTrue(TEXT("new attached entry for reentrant case"), BeginSpecial(F, Target + FVector(0, 0, 1000)));
	const auto ReentrantSource = F.Ability->GetActionToken();
	TestTrue(TEXT("reentrant case dive"), F.H.Resource->StartDescendingThrustKinsectDive(ReentrantSource, Target));
	F.SetTarget(Target + FVector(0, 0, 500));
	F.Part->ExtractColorTag = M3::Tag(TEXT("WeaponResource.IG.Extract.Red"));
	bool ReplacedInDamage = false;
	FGuid ReentrantFlightID;
	const auto DamageHandle = F.Target->GetAbilitySystemComponent()->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
		[&](UAbilitySystemComponent*, const FGameplayEffectSpec&, FActiveGameplayEffectHandle)
		{
			if (ReplacedInDamage) return;
			ReplacedInDamage = true;
			R.FlightInstanceID = FGuid::NewGuid();
			TestTrue(TEXT("manual replacement inside damage GE succeeds"), F.H.Resource->DeployKinsect(R));
			ReentrantFlightID = F.H.Kinsect->ActiveRequest.FlightInstanceID;
		});
	F.H.Kinsect->SetActorLocation(Target + FVector(0, 0, 520));
	F.H.World->TimeSeconds = .1; F.H.Kinsect->Tick(0);
	F.Target->GetAbilitySystemComponent()->OnGameplayEffectAppliedDelegateToSelf.Remove(DamageHandle);
	TestTrue(TEXT("actual damage callback ran"), ReplacedInDamage);
	TestTrue(TEXT("old sweep cannot alter new flight identity"), F.H.Kinsect->ActiveRequest.FlightInstanceID == ReentrantFlightID);
	TestEqual(TEXT("old sweep cannot end new manual flight"), F.H.Kinsect->State, EKinsectState::Flying);
	TestTrue(TEXT("old hit cannot write extract cargo after replacement"), F.H.Kinsect->GetPendingExtractColors().IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZDescendingCoEndpointTest,
	"MHGZ.Maintenance.KinsectCoAttack.DescendingThrust.FinalOverEndpointAcrossYawAndScale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZDescendingCoEndpointTest::RunTest(const FString&)
{
	FCoAttackFixture F;
	if (!TestTrue(TEXT("fixture"), F.Build())) return false;
	UAnimMontage* Over = LoadObject<UAnimMontage>(nullptr,
		TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_JiXiTuCi_Over.AM_IG_JiXiTuCi_Over"));
	if (!TestNotNull(TEXT("real resident 381 Over"), Over)) return false;
	constexpr float LastWindowEnd = 1.366667f; // independently cold-audited actual window
	for (const float Yaw : {0.f, 90.f, 180.f, -90.f})
	for (const float Scale : {1.f, .5f})
	{
		F.H.Character->SetActorLocation(FVector(100, 200, 120));
		F.H.Character->SetActorRotation(FRotator(0, Yaw, 0));
		F.H.Character->GetMesh()->SetRelativeRotation(FRotator(0, -90, 0));
		F.H.Character->GetMesh()->SetRelativeScale3D(FVector(Scale));
		const FVector OldTarget = DiveTarget(F);
		FVector Target;
		TestTrue(TEXT("predict final capsule using real Over"), UMHGZDescendingThrustAbility::ResolveKinsectDiveTarget(
			*F.H.Character, *Over, LastWindowEnd, Target));
		TestTrue(TEXT("remaining root motion materially differs from old endpoint"), FVector::Dist(Target, OldTarget) > 10.f);
		TestTrue(TEXT("prediction does not add vertical offset"), FMath::IsNearlyEqual(Target.Z, 120.0, .01));
		const FTransform WorldDelta = F.H.Character->GetMesh()->ConvertLocalRootMotionToWorld(
			Over->ExtractRootMotionFromTrackRange(LastWindowEnd, Over->GetPlayLength(), FAnimExtractContext()));
		FVector GroundDelta = WorldDelta.GetTranslation(); GroundDelta.Z = 0.f;
		F.H.Character->AddActorWorldOffset(GroundDelta);
		F.H.Character->SetActorRotation(WorldDelta.GetRotation() * F.H.Character->GetActorQuat());
		TestTrue(TEXT("endpoint remains 1m ahead of final capsule, in any yaw/scale"), Target.Equals(DiveTarget(F), .01));
		FVector FinishedTarget;
		TestTrue(TEXT("no remaining motion at end"), UMHGZDescendingThrustAbility::ResolveKinsectDiveTarget(
			*F.H.Character, *Over, Over->GetPlayLength(), FinishedTarget));
		TestTrue(TEXT("zero remainder adds only horizontal 1m"), FinishedTarget.Equals(DiveTarget(F), .01));
	}
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZCoAttackTenthStaminaTest,
	"MHGZ.Maintenance.KinsectCoAttack.Admission.TenthPoolLatchedPerAction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZCoAttackTenthStaminaTest::RunTest(const FString&)
{
	for (const float Maximum : {100.f, 200.f})
	for (const bool AtThreshold : {false, true})
	{
		FCoAttackFixture F;
		if (!TestTrue(TEXT("fixture"), F.Build())) return false;
		ReadyOnArm(F);
		F.Ability->RequestEndAction(EWeaponActionEndReason::Superseded);
		const float EntryStamina = Maximum * .1f - (AtThreshold ? 0.f : .01f);
		FMHGZDescendingThrustCoAttackTestAccess::Pool(F.H.Resource, Maximum, EntryStamina);
		const auto NextHandle = F.H.GiveAbility(UMHGZCoAttackTestAbility::StaticClass());
		auto* Next = F.Activate(NextHandle);
		if (!TestNotNull(TEXT("low insect stamina never blocks hunter GA"), Next)) return false;
		TestEqual(TEXT("ordinary admission uses 10% of actual maximum"), F.Sampler->IsRunning(), AtThreshold);
		TestEqual(TEXT("admission does not charge an extra tenth"), F.H.Resource->GetKinsectStamina(), EntryStamina);
		if (!AtThreshold)
		{
			FMHGZDescendingThrustCoAttackTestAccess::Stamina(F.H.Resource, Maximum * .5f);
			Next->EnableCollision(0);
			TestFalse(TEXT("later regeneration/window cannot join an initially denied GA"), F.Sampler->IsRunning());
		}
		else
		{
			FMHGZDescendingThrustCoAttackTestAccess::Stamina(F.H.Resource, Maximum * .05f);
			Next->EnableCollision(0);
			TestTrue(TEXT("admitted current GA continues below tenth"), F.Sampler->IsRunning());
			TestTrue(TEXT("current source identity preserved"), F.Sampler->GetActionToken() == Next->GetActionToken());
		}
		F.H.ASC->ClearAbility(NextHandle);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZDescendingCoStandbyTest,
	"MHGZ.Maintenance.KinsectCoAttack.DescendingThrust.ComboExpiryReleasesTrueHover",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZDescendingCoStandbyTest::RunTest(const FString&)
{
	for (const bool CloseBeforeArrival : {false, true})
	{
		FCoAttackFixture F;
		if (!TestTrue(TEXT("fixture"), F.Build())) return false;
		const FVector Target = DiveTarget(F), Origin = Target + FVector(0, 0, 1000);
		if (!TestTrue(TEXT("entry"), BeginSpecial(F, Origin))) return false;
		const auto Old = F.Ability->GetActionToken();
		TestTrue(TEXT("dive"), F.H.Resource->StartDescendingThrustKinsectDive(Old, Target));
		if (!CloseBeforeArrival)
		{
			F.H.Kinsect->SetActorLocation(Target); F.H.Kinsect->Tick(0);
			ResourceTick(F);
			TestTrue(TEXT("original open combo still permits immediate chain"), FMHGZDescendingThrustCoAttackTestAccess::Active(F.H.Resource));
		}
		F.H.Host->OnWeaponComboWindowsClosed.Broadcast(Old);
		F.H.Resource->FinishDescendingThrustKinsect(Old, EWeaponActionEndReason::Normal);
		F.Ability->RequestEndAction(EWeaponActionEndReason::Normal);
		ResourceTick(F);
		F.H.Character->AddActorWorldOffset(FVector(700, 300, 0));
		const auto NextHandle = F.H.GiveAbility(UMHGZCoAttackTestAbility::StaticClass());
		auto* Next = F.Activate(NextHandle);
		if (!TestNotNull(TEXT("unrelated later ground attack still activates"), Next)) return false;
		TestFalse(TEXT("expired special cannot admit unrelated later attack"), F.Sampler->IsRunning());
		if (CloseBeforeArrival)
		{
			TestEqual(TEXT("later non-admitted attack does not abort pending dive"), F.H.Kinsect->State, EKinsectState::Flying);
			TestTrue(TEXT("later attack cannot teleport insect"), F.H.Kinsect->GetActorLocation().Equals(Origin, .01));
			F.H.Kinsect->SetActorLocation(Target); F.H.Kinsect->Tick(0);
			ResourceTick(F);
		}
		TestFalse(TEXT("special lease actually retired"), FMHGZDescendingThrustCoAttackTestAccess::Active(F.H.Resource));
		TestEqual(TEXT("normal hover, not a hidden cooperative owner"), F.H.Kinsect->State, EKinsectState::Hovering);
		TestTrue(TEXT("normal hover stays at completed point"), F.H.Kinsect->GetActorLocation().Equals(Target, .01));
		Next->EnableCollision(0);
		TestFalse(TEXT("later attack windows cannot revive old hover admission"), F.Sampler->IsRunning());
		F.H.ASC->ClearAbility(NextHandle);
	}
	return true;
}
#endif
