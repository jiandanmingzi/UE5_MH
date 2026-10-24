// Copyright MHGZ Project. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Engine/Blueprint.h"
#include "ActionSystem/MHGZKinsectSlashAbility.h"
#include "ActionSystem/MHGZKinsectPiercerAbility.h"
#include "ActionSystem/MHGZDescendingThrustAbility.h"
#include "ActionSystem/MHGZAwakenedKinsectAttackAbility.h"
#include "ActionSystem/MHGZPowderVortexAbility.h"

namespace MHGZ::IGEditor
{
/** Historical setup readers must address the resident GA, never deleted Combat fields. */
template <typename T> T& RequireAction(const TCHAR* Name)
{
    const FString Path = FString(TEXT("/Game/Blueprints/Ability/InsectGlaive/GA_IG_")) + Name;
    UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *(Path + ".GA_IG_" + Name));
    checkf(BP && BP->GeneratedClass && BP->GeneratedClass->IsChildOf(T::StaticClass()),
        TEXT("Resident GA %s is required; historical setup cannot bootstrap old Combat schemas."), *Path);
    return *CastChecked<T>(BP->GeneratedClass->GetDefaultObject());
}
inline UObject* OwnerAsset(UObject& CDO)
{
    return CDO.GetClass()->ClassGeneratedBy;
}
}
