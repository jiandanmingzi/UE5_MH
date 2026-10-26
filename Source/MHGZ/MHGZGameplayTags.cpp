// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZGameplayTags.h"

namespace MHGZGameplayTags
{
const FGameplayTag& Grounded()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Grounded"));
	return Tag;
}

const FGameplayTag& Aerial()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Aerial"));
	return Tag;
}

const FGameplayTag& AerialFalling()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Aerial.Falling"));
	return Tag;
}

const FGameplayTag& AerialCantAttack()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Aerial.CantAttack"));
	return Tag;
}

const FGameplayTag& Unsheathed()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Unsheathed"));
	return Tag;
}

const FGameplayTag& Sheathed()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Sheathed"));
	return Tag;
}

const FGameplayTag& Dead()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Dead"));
	return Tag;
}

const FGameplayTag& Hitstun()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Hitstun"));
	return Tag;
}

const FGameplayTag& Knockdown()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Knockdown"));
	return Tag;
}

const FGameplayTag& IGExtractWhite()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("WeaponResource.IG.Extract.White"));
	return Tag;
}

const FGameplayTag& IGExtractRed()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("WeaponResource.IG.Extract.Red"));
	return Tag;
}

const FGameplayTag& IGExtractOrange()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("WeaponResource.IG.Extract.Orange"));
	return Tag;
}

const FGameplayTag& IGTripleUpCost()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Cost.IG.TripleUp"));
	return Tag;
}

const FGameplayTag& WeaponBInput()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Input.Weapon.B"));
	return Tag;
}

const FGameplayTag& IGAirDodgeSource()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Aerial.Source.IG_AirDodge"));
	return Tag;
}

const FGameplayTag& IGBackVaultSource()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Aerial.Source.IG_BackVault"));
	return Tag;
}

const FGameplayTag& IGAdvancingCounterSource()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Aerial.Source.IG_AdvancingCounter"));
	return Tag;
}

const FGameplayTag& IGKinsectPiercerSource()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Aerial.Source.IG_KinsectPiercer"));
	return Tag;
}

const FGameplayTag& IGKinsectSlashSource()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Aerial.Source.IG_KinsectSlash"));
	return Tag;
}

const FGameplayTag& IGKinsectSlashHitSource()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Aerial.Source.IG_KinsectSlash.Hit"));
	return Tag;
}

const FGameplayTag& IGKinsectSlashMissSource()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Aerial.Source.IG_KinsectSlash.Miss"));
	return Tag;
}

const FGameplayTag& DamageMotionValue()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Damage.MotionValue"));
	return Tag;
}
}
