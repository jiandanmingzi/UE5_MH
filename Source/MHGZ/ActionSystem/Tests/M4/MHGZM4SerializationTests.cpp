// Copyright MHGZ Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Animation/AnimMontage.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

#include "MHGZAttackAbility.h"
#include "MHGZDodgeAbility.h"
#include "MHGZWeaponComboData.h"
#include "MHGZDummyConfig.h"

namespace
{
bool IsAbsent(const UStruct* Type, const FName PropertyName)
{
	return FindFProperty<FProperty>(Type, PropertyName) == nullptr;
}

bool HasNotifyClass(const UAnimMontage& Montage, const FName ClassName)
{
	return Montage.Notifies.ContainsByPredicate([ClassName](const FAnimNotifyEvent& Event)
	{
		return Event.Notify && Event.Notify->GetClass()->GetFName() == ClassName;
	});
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM4SerializationLegacyPropertiesRemoved,
	"MHGZ.M4.Serialization.LegacyPropertiesRemoved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM4SerializationLegacyPropertiesRemoved::RunTest(const FString& Parameters)
{
	(void)Parameters;
	for (const FName PropertyName : {
		FName(TEXT("TraceEndSocketName")),
		FName(TEXT("AttachSocketName")),
		FName(TEXT("TraceStartSocketName")),
		FName(TEXT("Shape")),
		FName(TEXT("ShapeExtent")),
		FName(TEXT("TraceSampleCount")) })
	{
		TestTrue(FString::Printf(TEXT("Attack collision no longer exposes %s"),
			*PropertyName.ToString()),
			IsAbsent(FAttackCollisionConfig::StaticStruct(), PropertyName));
	}

	TestTrue(TEXT("Attack damage no longer exposes DamageSetByCallerTag"),
		IsAbsent(FAttackDamageConfig::StaticStruct(), TEXT("DamageSetByCallerTag")));
	TestTrue(TEXT("Combo transition no longer exposes bRequiresHitToGrantTags"),
		IsAbsent(FComboTransition::StaticStruct(), TEXT("bRequiresHitToGrantTags")));
	TestNotNull(TEXT("Combo transition exposes bRequiresDodgeAcceptWindow"),
		FindFProperty<FProperty>(FComboTransition::StaticStruct(), TEXT("bRequiresDodgeAcceptWindow")));
	TestTrue(TEXT("Dodge no longer exposes the sheathed legacy direction map"),
		IsAbsent(UMHGZDodgeAbility::StaticClass(), TEXT("SheathedDodgeMontages")));
	TestTrue(TEXT("Dodge no longer exposes the unsheathed legacy direction map"),
		IsAbsent(UMHGZDodgeAbility::StaticClass(), TEXT("UnsheathedDodgeMontages")));
	TestTrue(TEXT("Dummy hitzones no longer expose HalfExtent"),
		IsAbsent(FDummyHitzoneConfig::StaticStruct(), TEXT("HalfExtent")));

	for (const TCHAR* MontagePath : {
		TEXT("/Game/Characters/Demo/Anims/Montage/AM_Shth_Dodge.AM_Shth_Dodge"),
		TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_Dodge_Forward.AM_IG_Dodge_Forward") })
	{
		UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, MontagePath);
		TestNotNull(FString::Printf(TEXT("Loads cleaned Dodge montage %s"), MontagePath), Montage);
		if (Montage)
		{
			TestFalse(FString::Printf(TEXT("%s has no DodgeExitDecision Notify"), MontagePath),
				HasNotifyClass(*Montage, TEXT("AnimNotify_DodgeExitDecision")));
			TestFalse(FString::Printf(TEXT("%s has no DodgeMoveExitBegin Notify"), MontagePath),
				HasNotifyClass(*Montage, TEXT("AnimNotify_DodgeMoveExitBegin")));
		}
	}
	return true;
}

#endif
