// Copyright MHGZ Project. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"
#include "InsectGlaive/InsectGlaiveCombatConfig.h"
#include "ActionSystem/MHGZAirDodgeAbility.h"
#include "ActionSystem/MHGZPoleVaultAbility.h"
#include "ActionSystem/MHGZKinsectSlashAbility.h"
#include "ActionSystem/MHGZKinsectPiercerAbility.h"
#include "ActionSystem/MHGZDescendingThrustAbility.h"
#include "ActionSystem/MHGZAwakenedKinsectAttackAbility.h"
#include "ActionSystem/MHGZInsectGlaiveKinsectAbilities.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZIGActionConfigOwnership,
    "MHGZ.Maintenance.IGActionConfig.ResidentOwnershipAndValidation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZIGActionConfigOwnership::RunTest(const FString& Parameters)
{
    const UInsectGlaiveCombatConfig* Combat = LoadObject<UInsectGlaiveCombatConfig>(nullptr,
        TEXT("/Game/Weapons/InsectGlaive/Data/DA_IG_Combat.DA_IG_Combat"));
    if (!TestNotNull(TEXT("existing shared Combat loads"), Combat)) return false;
    struct FCase { UClass* Type; const TCHAR* Fields; TArray<FString> Assets; };
    const FCase Cases[] = {
        { UMHGZAirDodgeAbility::StaticClass(), TEXT("AirDodgeHorizontalSpeed AirDodgeVerticalSpeed"), { TEXT("GA_IG_AirDodge") } },
        { UMHGZPoleVaultAbility::StaticClass(), TEXT("VaultTunings"), { TEXT("GA_IG_QianChengGanTiao"), TEXT("GA_IG_HouChengGanTiao"), TEXT("GA_IG_ZuoChengGanTiao"), TEXT("GA_IG_YouChengGanTiao") } },
        { UMHGZKinsectSlashAbility::StaticClass(), TEXT("KinsectSlashDashProfile bUseCapturedKinsectSlashDash KinsectSlashMaxDistance KinsectSlashDashDuration KinsectSlashMaxUpwardLaunchPitchDegrees KinsectSlashDistanceCurve KinsectSlashDanceDamageMultipliers"), { TEXT("GA_IG_CaoChongZhan") } },
        { UMHGZKinsectPiercerAbility::StaticClass(), TEXT("KinsectPiercerStartProfile KinsectPiercerDiveProfile KinsectPiercerLandingProfile KinsectPiercerDiveWatchdogSeconds KinsectPiercerLandingBlendInTime KinsectPiercerDanceDamageMultipliers"), { TEXT("GA_IG_QiangHuaCaoChongChuanCi") } },
        { UMHGZDescendingThrustAbility::StaticClass(), TEXT("DescendingThrustAirProfile DescendingThrustAirWatchdogSeconds DescendingThrustLandingBlendInTime DescendingThrustAirControl DescendingThrustDanceDamageMultipliers"), { TEXT("GA_IG_JiXiTuCi") } },
        { UMHGZAwakenedKinsectAttackAbility::StaticClass(), TEXT("AwakenedKinsectMotionValue AwakenedRedKinsectMotionValue AwakenedTripleKinsectMotionValue AwakenedHunterFlightDurationSeconds AwakenedHunterFlightSpeed AwakenedHunterFlightGravityMagnitude AwakenedGroundTargetMinimumDistance AwakenedGroundTargetVerticalLaunchSpeed AwakenedHunterFallHandoffVelocity AwakenedHunterFallGravityMagnitude AwakenedHunterFallMontage AwakenedAimHorizontalCorrectionAngle AwakenedAimCorrectionAngle"), { TEXT("GA_IG_JueChongJi") } },
        { UMHGZDrawAndSendKinsectAbility::StaticClass(), TEXT("DrawSendKinsectMotionValue"), { TEXT("GA_IG_DrawAndSendKinsect") } },
    };
    int32 CheckedValues = 0;
    for (const FCase& Item : Cases)
    {
        TArray<FString> Fields; FString(Item.Fields).ParseIntoArrayWS(Fields);
        for (const FString& Asset : Item.Assets)
        {
            const FString Path = "/Game/Blueprints/Ability/InsectGlaive/" + Asset + "." + Asset + "_C";
            UClass* Class = LoadClass<UGameplayAbility>(nullptr, *Path);
            if (!TestNotNull(*Path, Class)) continue;
            TestTrue(TEXT("resident class has the expected native owner"), Class->IsChildOf(Item.Type));
            const UObject* GA = Class->GetDefaultObject();
            for (const FString& Field : Fields)
            {
                const FProperty* Old = Combat->GetClass()->FindPropertyByName(*Field);
                const FProperty* New = Class->FindPropertyByName(*Field);
                TestNull(TEXT("obsolete duplicate is removed from Combat"), Old);
                if (!TestNotNull(*Field, New)) continue;
                TestTrue(TEXT("GA field remains editable in defaults"), New->HasAnyPropertyFlags(CPF_Edit));
                TestFalse(TEXT("Chinese hover documentation is retained"), New->GetMetaData(TEXT("ToolTip")).IsEmpty());
                ++CheckedValues;
            }
            FDataValidationContext Context;
            const auto Result = GA->IsDataValid(Context);
            for (const auto& Issue : Context.GetIssues())
                if (Issue.Severity == EMessageSeverity::Error) AddError(Asset + ": " + Issue.Message.ToString());
            TestTrue(TEXT("migrated resident GA passes its existing and new validation"), Result != EDataValidationResult::Invalid);
        }
    }
    TestEqual(TEXT("all ten GAs and 38 field values were checked"), CheckedValues, 38);
    for (const TCHAR* Field : { TEXT("KinsectSlashAttachSocket"), TEXT("AwakenedPierceHitInterval"), TEXT("SendKinsectMotionValue"), TEXT("KinsectMarkProjectileSpeed"), TEXT("AerialLandingMontage"), TEXT("MarkedHitPowderCooldown") })
    {
        const FProperty* Property = Combat->GetClass()->FindPropertyByName(Field);
        TestTrue(TEXT("shared rules and excluded mark projectile remain editable in Combat"), Property && Property->HasAnyPropertyFlags(CPF_Edit));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZIGActionConfigIsolation,
    "MHGZ.Maintenance.IGActionConfig.AbilityValuesAndSharedPhysics",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZIGActionConfigIsolation::RunTest(const FString& Parameters)
{
    UInsectGlaiveCombatConfig* Old = NewObject<UInsectGlaiveCombatConfig>();
    Old->AerialFallGravityScale = 3.0f;
    UMHGZAirDodgeAbility* GA = NewObject<UMHGZAirDodgeAbility>();
    GA->AirDodgeHorizontalSpeed = 1234.0f; GA->AirDodgeVerticalSpeed = 567.0f;
    const auto Request = GA->BuildAirDodgeRequest({}, FVector::ForwardVector, FVector::RightVector, *GA);
    TestEqual(TEXT("production request uses GA horizontal speed"), Request.LaunchVelocity.X, 1234.0);
    TestEqual(TEXT("production request uses GA vertical speed"), Request.LaunchVelocity.Z, 567.0);
    TestTrue(TEXT("shared physical request contract stays valid"), Request.HasValidBallisticParameters());
    TestEqual(TEXT("native vault default generation remains intact"), MHGZ::IGActionConfig::MakeDefaultVaultTunings().Num(), 8);
    const TArray<float> Values = {1.0f, 1.15f, 1.25f, 1.35f};
    TestEqual(TEXT("action-specific bonus stays GA configurable"), MHGZ::IGActionConfig::DanceDamageMultiplier(Values,3,3),1.35f);
    TestEqual(TEXT("shared resource maximum still caps layers"), MHGZ::IGActionConfig::DanceDamageMultiplier(Values,3,1),1.15f);
    return true;
}
#endif
