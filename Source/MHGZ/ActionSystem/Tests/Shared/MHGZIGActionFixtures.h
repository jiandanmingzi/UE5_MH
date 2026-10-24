// Copyright MHGZ Project. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"
#include "ActionSystem/MHGZAirDodgeAbility.h"
#include "ActionSystem/MHGZKinsectSlashAbility.h"
#include "ActionSystem/MHGZKinsectPiercerAbility.h"
#include "ActionSystem/MHGZDescendingThrustAbility.h"
#include "ActionSystem/MHGZAwakenedKinsectAttackAbility.h"
#include "ActionSystem/MHGZInsectGlaiveKinsectAbilities.h"

namespace IGTest
{
template <typename T> FName PrototypeName()
{
    return FName(*(T::StaticClass()->GetName() + TEXT("_ActionFixture")));
}
/** Test inputs are real GA objects, not obsolete Combat fields or production fallbacks. */
template <typename T> T* Action(const UObject* Owner)
{
    T* Result = FindObject<T>(const_cast<UObject*>(Owner), *PrototypeName<T>().ToString());
    checkf(Result, TEXT("Initialize the scoped GA fixtures before use"));
    return Result;
}
struct FScopedActionFixtures
{
    TArray<TStrongObjectPtr<UObject>> Owners;
    void Initialize(UObject* Owner)
    {
        Owners.Reset();
        Owners.Emplace(NewObject<UMHGZAirDodgeAbility>(Owner, PrototypeName<UMHGZAirDodgeAbility>()));
        Owners.Emplace(NewObject<UMHGZKinsectSlashAbility>(Owner, PrototypeName<UMHGZKinsectSlashAbility>()));
        Owners.Emplace(NewObject<UMHGZKinsectPiercerAbility>(Owner, PrototypeName<UMHGZKinsectPiercerAbility>()));
        Owners.Emplace(NewObject<UMHGZDescendingThrustAbility>(Owner, PrototypeName<UMHGZDescendingThrustAbility>()));
        Owners.Emplace(NewObject<UMHGZAwakenedKinsectAttackAbility>(Owner, PrototypeName<UMHGZAwakenedKinsectAttackAbility>()));
        Owners.Emplace(NewObject<UMHGZDrawAndSendKinsectAbility>(Owner, PrototypeName<UMHGZDrawAndSendKinsectAbility>()));
    }
};
}
