// Copyright MHGZ Project. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "MHGZM5KinsectSlashDamageTestTypes.h"
#include "ActionSystem/MHGZAbilitySystemComponent.h"
#include "AttributeSystem/Res_InsectGlaive.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "InsectGlaive/Kinsect/Kinsect.h"
#include "InsectGlaive/Kinsect/InsectGlaiveKinsectData.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZKinsectSlashPreparationTest,
    "MHGZ.Maintenance.KinsectSlash.PreparationReachabilityStaminaAndIsolation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZKinsectSlashPreparationTest::RunTest(const FString& Parameters)
{
    USkeletalMesh* Glaive = LoadObject<USkeletalMesh>(nullptr,
        TEXT("/Game/Weapons/InsectGlaive/Meshes/Glaive/SKM_IG_Glaive.SKM_IG_Glaive"));
    UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr,
        TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_CaoChongZhan.AM_IG_CaoChongZhan"));
    if (!TestNotNull(TEXT("resident glaive"), Glaive) || !TestNotNull(TEXT("resident montage"), Montage)) { return false; }

    enum ECase { ArmReady, ArmLow, RemoteReady, RemoteBoundary, RemoteTooFar, RemoteLow,
        RemoteZero, RemoteLate, RemoteDeadline, RemoteCancelled, RemoteDepleted, LegacyAttach };
    for (int32 Case = ArmReady; Case <= LegacyAttach; ++Case)
    {
        FMHGZM5KinsectSlashDamageTestHarness H;
        if (!TestTrue(TEXT("equipped production resource fixture"), H.Build())
            || !TestTrue(TEXT("committed action fixture"), H.ActivateAbility()))
        {
            H.Teardown(); return false;
        }
        AKinsect* Insect = H.Resource->GetKinsectActor();
        USkeletalMeshComponent* Weapon = NewObject<USkeletalMeshComponent>(H.Character);
        H.Character->AddInstanceComponent(Weapon);
        Weapon->ComponentTags.Add(TEXT("WeaponTrace"));
        Weapon->SetSkeletalMesh(Glaive);
        Weapon->RegisterComponent();
        const FName Socket(TEXT("IG_Kinsect"));
        if (!TestTrue(TEXT("actual weapon socket"), Weapon->DoesSocketExist(Socket))) { H.Teardown(); return false; }
        auto& InsectCollision = H.Ability->AttackSegments[0].Collision;
        InsectCollision.TraceMeshComponentTag = TEXT("WeaponTrace");
        InsectCollision.TraceRegions.Reset();
        InsectCollision.TraceRegions.AddDefaulted_GetRef().EndSocketName = Socket;
        const FWeaponActionToken Token = H.Ability->GetActionToken();
        H.Ability->AttackMontage = DuplicateObject<UAnimMontage>(Montage, H.Ability);
        H.Ability->DefaultEntrySection = H.Ability->HoverSectionName;
        const float Budget = H.Ability->GetKinsectPreparationTime();
        TestTrue(TEXT("resident section has a finite positive pre-dash return budget"), FMath::IsFinite(Budget) && Budget > 0.f);
        H.Ability->AttackMontage->RateScale *= 2.f;
        TestTrue(TEXT("return budget follows montage play rate"),
            FMath::IsNearlyEqual(H.Ability->GetKinsectPreparationTime(), Budget * .5f));
        H.Ability->AttackMontage->RateScale *= .5f;

        const bool bOnArm = Case == ArmReady || Case == ArmLow;
        const bool bLow = Case == ArmLow || Case == RemoteLow || Case == RemoteZero;
        H.Resource->KinsectStamina = bLow ? H.Resource->GetMaxKinsectStamina() * .199f
            : H.Resource->GetMaxKinsectStamina() * .2f;
        if (Case == RemoteZero) { H.Resource->KinsectStamina = 0.f; }
        const float ReturnTime = .2f;
        const float Distance = Insect->GetReturnSpeed() * ReturnTime
            * (Case == RemoteTooFar ? 2.f : Case == RemoteBoundary ? 1.f : .5f);
        if (!bOnArm)
        {
            Insect->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
            Insect->State = EKinsectState::Hovering;
            Insect->SetActorLocation(Weapon->GetSocketLocation(Socket) + FVector(Distance, 0.f, 0.f));
        }
        const FVector InitialPosition = Insect->GetActorLocation();
        const bool bStarted = Case == ArmReady
            ? H.Ability->UMHGZKinsectSlashAbility::PrepareAttackMontage()
            : Case == LegacyAttach ? H.Resource->BeginKinsectActionAttachment(Token, Weapon, Socket)
            : H.Resource->BeginKinsectSlashPreparation(Token, Weapon, Socket, ReturnTime);
        if (!TestTrue(TEXT("valid lease starts even when insect cannot participate"), bStarted)) { H.Teardown(); return false; }
        H.Ability->bKinsectActionAttached = true; // exact lease used by the production GA
        TestTrue(TEXT("preparation is owned by this action"), H.Resource->IsKinsectActionControlCurrent(Token));
        FWeaponActionToken Stale = Token;
        ++Stale.ActivationSequenceID;
        TestFalse(TEXT("foreign action cannot release the insect"), H.Resource->EndKinsectActionAttachmentAndReturn(Stale));

        if (Case == ArmReady)
        {
            TestTrue(TEXT("20 percent on arm participates"), H.Resource->IsKinsectActionAttachmentCurrent(Token));
            TestEqual(TEXT("attached to actual weapon"), Insect->GetRootComponent()->GetAttachParent(), static_cast<USceneComponent*>(Weapon));
            const float Before = H.Resource->GetKinsectStamina();
            H.Resource->TickComponent(.25f, LEVELTICK_All, nullptr);
            TestTrue(TEXT("weapon attachment drains rather than regenerates"),
                FMath::IsNearlyEqual(H.Resource->GetKinsectStamina(), Before - H.Resource->GetKinsectData()->FlightDrainRate * .25f));
        }
        else if (Case == ArmLow)
        {
            TestEqual(TEXT("low stamina stays on arm"), Insect->GetState(), EKinsectState::Attached);
            H.Resource->TickComponent(1.f, LEVELTICK_All, nullptr);
            Insect->Tick(1.f);
            TestFalse(TEXT("regeneration cannot opt into an already rejected action"), H.Resource->IsKinsectActionAttachmentCurrent(Token));
            TestFalse(TEXT("not attached to weapon"), Insect->GetRootComponent()->GetAttachParent() == Weapon);
        }
        else if (Case == RemoteTooFar || Case == RemoteLow || Case == RemoteZero)
        {
            TestEqual(TEXT("unavailable insect holds rather than teleporting"), Insect->GetState(), EKinsectState::ActionWaiting);
            Insect->Tick(.5f);
            TestTrue(TEXT("held world location is unchanged"), Insect->GetActorLocation().Equals(InitialPosition));
            TestFalse(TEXT("no insect hit permission"), H.Resource->IsKinsectActionAttachmentCurrent(Token));
            const float Before = H.Resource->GetKinsectStamina();
            H.Resource->TickComponent(.25f, LEVELTICK_All, nullptr);
            TestTrue(TEXT("remote action hold still drains, clamped at zero"), Before > 0.f
                ? H.Resource->GetKinsectStamina() < Before : H.Resource->GetKinsectStamina() == 0.f);
            H.Ability->EnableCollision(0);
            TestFalse(TEXT("unavailable insect issues no spatial query"), H.Ability->ShouldSweepCollision(0));
            TestTrue(TEXT("weapon collision gate remains independent"), H.Ability->CanApplyCollisionHit(FHitResult(), 1));
            TestTrue(TEXT("second weapon gate remains independent"), H.Ability->CanApplyCollisionHit(FHitResult(), 2));
            H.Ability->Phase = EKinsectSlashPhase::MissFall;
            H.Ability->DisableCollision(0);
            TestFalse(TEXT("authored window closure still releases preparation without an insect sweep"),
                H.Resource->IsKinsectActionControlCurrent(Token));
            if (Case != RemoteZero)
            {
                TestEqual(TEXT("unavailable remote insect remains in place after window closure"),
                    Insect->GetState(), EKinsectState::Hovering);
                TestTrue(TEXT("window closure does not teleport an unavailable insect"),
                    Insect->GetActorLocation().Equals(InitialPosition));
            }
            else
            {
                TestTrue(TEXT("window closure recalls an already exhausted insect"),
                    Insect->GetState() == EKinsectState::Returning || Insect->GetState() == EKinsectState::Attached);
            }
        }
        else if (Case == RemoteReady || Case == RemoteBoundary || Case == RemoteLate
            || Case == RemoteDeadline || Case == RemoteCancelled || Case == RemoteDepleted)
        {
            TestEqual(TEXT("reachable insect moves, does not snap"), Insect->GetState(), EKinsectState::ActionReturning);
            TestTrue(TEXT("initial location preserved"), Insect->GetActorLocation().Equals(InitialPosition));
            Insect->Tick(.02f);
            const float Travel = FVector::Distance(Insect->GetActorLocation(), InitialPosition);
            TestTrue(TEXT("straight return is limited by actual return speed"),
                FMath::IsNearlyEqual(Travel, Insect->GetReturnSpeed() * .02f, .01f));
            if (Case == RemoteCancelled)
            {
                const FVector CancelPosition = Insect->GetActorLocation();
                H.ASC->CancelAbilityHandle(H.AbilityHandle);
                TestFalse(TEXT("actual GA cancellation releases the preparation lease"), H.Resource->IsKinsectActionControlCurrent(Token));
                TestEqual(TEXT("cancelled remote preparation resumes hovering"), Insect->GetState(), EKinsectState::Hovering);
                TestTrue(TEXT("cancelled return cannot snap to either socket"), Insect->GetActorLocation().Equals(CancelPosition));
            }
            else if (Case == RemoteLate)
            {
                Insect->FinalizeSlashPreparation(Token);
                const FVector ClosedPosition = Insect->GetActorLocation();
                Insect->Tick(1.f);
                TestEqual(TEXT("dash boundary closes a pending return"), Insect->GetState(), EKinsectState::ActionWaiting);
                TestTrue(TEXT("late insect cannot resume moving"), Insect->GetActorLocation().Equals(ClosedPosition));
            }
            else if (Case == RemoteDeadline)
            {
                Weapon->SetWorldLocation(Weapon->GetComponentLocation() + FVector(10000.f, 0.f, 0.f));
                Insect->Tick(.5f);
                TestEqual(TEXT("moving socket cannot extend the return deadline"), Insect->GetState(), EKinsectState::ActionWaiting);
                TestTrue(TEXT("deadline caps displacement at speed times remaining budget"),
                    FVector::Distance(Insect->GetActorLocation(), InitialPosition)
                        <= Insect->GetReturnSpeed() * ReturnTime + .01f);
            }
            else if (Case == RemoteDepleted)
            {
                H.Resource->KinsectStamina = .01f;
                H.Resource->TickComponent(.1f, LEVELTICK_All, nullptr);
                TestFalse(TEXT("depletion retires only the insect lease"), H.Resource->IsKinsectActionControlCurrent(Token));
                TestTrue(TEXT("depletion cannot cancel the weapon action"), H.Ability->IsActive());
            }
            else
            {
                Insect->Tick(ReturnTime);
                TestTrue(TEXT("actually arriving insect is weapon-attached"), H.Resource->IsKinsectActionAttachmentCurrent(Token));
                TestTrue(TEXT("arrived at live socket"), Insect->GetActorLocation().Equals(Weapon->GetSocketLocation(Socket), .01f));
            }
        }
        else if (Case == LegacyAttach)
        {
            const float Before = H.Resource->GetKinsectStamina();
            H.Resource->TickComponent(.25f, LEVELTICK_All, nullptr);
            TestTrue(TEXT("non-slash action attachment keeps its original regeneration"), H.Resource->GetKinsectStamina() > Before);
        }
        if (H.Resource->IsKinsectActionControlCurrent(Token))
        {
            TestTrue(TEXT("owning action releases on end"), H.Resource->EndKinsectActionAttachmentAndReturn(Token));
            TestFalse(TEXT("lease retired"), H.Resource->IsKinsectActionControlCurrent(Token));
            if (Case == RemoteTooFar || Case == RemoteLow || Case == RemoteLate || Case == RemoteDeadline)
            {
                TestEqual(TEXT("nonparticipating remote insect remains hovering"), Insect->GetState(), EKinsectState::Hovering);
            }
            if (Case == RemoteZero)
            {
                TestTrue(TEXT("exhausted insect resumes system recall after the action releases it"),
                    Insect->GetState() == EKinsectState::Returning || Insect->GetState() == EKinsectState::Attached);
            }
        }
        H.Teardown();
    }
    return true;
}
#endif
