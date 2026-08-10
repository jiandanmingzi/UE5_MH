// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZGameplayEffectContext.h"

UScriptStruct* FMHGZGameplayEffectContext::GetScriptStruct() const
{
	return StaticStruct();
}

FMHGZGameplayEffectContext* FMHGZGameplayEffectContext::Duplicate() const
{
	FMHGZGameplayEffectContext* NewContext = new FMHGZGameplayEffectContext();
	*NewContext = *this;
	if (const FHitResult* Hit = GetHitResult())
	{
		NewContext->AddHitResult(*Hit, true);
	}
	return NewContext;
}

bool FMHGZGameplayEffectContext::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	bool bParentSuccess = false;
	FGameplayEffectContext::NetSerialize(Ar, Map, bParentSuccess);

	Ar << AttackInstanceID;

	bool bSourceActionSuccess = false;
	bool bHitzoneSuccess = false;
	bool bHitCueSuccess = false;
	bool bElementCueSuccess = false;
	SourceActionTag.NetSerialize(Ar, Map, bSourceActionSuccess);
	HitzoneTag.NetSerialize(Ar, Map, bHitzoneSuccess);
	HitCueTag.NetSerialize(Ar, Map, bHitCueSuccess);
	ElementCueTag.NetSerialize(Ar, Map, bElementCueSuccess);

	uint8 SourceType = static_cast<uint8>(DamageSourceType);
	Ar.SerializeBits(&SourceType, 3);
	if (Ar.IsLoading())
	{
		DamageSourceType = static_cast<EMHGZDamageSourceType>(SourceType);
	}

	bOutSuccess = bParentSuccess && bSourceActionSuccess && bHitzoneSuccess
		&& bHitCueSuccess && bElementCueSuccess && !Ar.IsError();
	return true;
}

FMHGZGameplayEffectContext* FMHGZGameplayEffectContext::ExtractEffectContext(FGameplayEffectContextHandle& Handle)
{
	FGameplayEffectContext* Context = Handle.Get();
	return Context && Context->GetScriptStruct()->IsChildOf(StaticStruct())
		? static_cast<FMHGZGameplayEffectContext*>(Context)
		: nullptr;
}

const FMHGZGameplayEffectContext* FMHGZGameplayEffectContext::ExtractEffectContext(const FGameplayEffectContextHandle& Handle)
{
	const FGameplayEffectContext* Context = Handle.Get();
	return Context && Context->GetScriptStruct()->IsChildOf(StaticStruct())
		? static_cast<const FMHGZGameplayEffectContext*>(Context)
		: nullptr;
}

FGameplayEffectContext* UMHGZAbilitySystemGlobals::AllocGameplayEffectContext() const
{
	return new FMHGZGameplayEffectContext();
}
