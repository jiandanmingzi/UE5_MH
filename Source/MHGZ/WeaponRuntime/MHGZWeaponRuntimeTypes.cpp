// Copyright MHGZ Project. All Rights Reserved.

#include "WeaponRuntime/MHGZWeaponRuntimeTypes.h"

#include "AbilitySystemComponent.h"

void FWeaponRuntimeTagLedger::Initialize(
	const FWeaponRuntimeToken& InRuntimeToken,
	UAbilitySystemComponent* InASC)
{
	if (!Entries.IsEmpty())
	{
		ReleaseAll(ActiveRuntimeToken);
	}

	Entries.Reset();
	ActiveRuntimeToken = InRuntimeToken;
	ASC = InASC;
	NextTokenID = 1;
}

FWeaponOwnedTagToken FWeaponRuntimeTagLedger::Acquire(
	const FWeaponTagOwnerID& OwnerID,
	const FGameplayTagContainer& Tags)
{
	UAbilitySystemComponent* AbilitySystem = ASC.Get();
	if (!AbilitySystem || !ActiveRuntimeToken.IsValid()
		|| OwnerID.RuntimeToken != ActiveRuntimeToken || Tags.IsEmpty())
	{
		return FWeaponOwnedTagToken();
	}

	uint64 TokenID = NextTokenID++;
	if (TokenID == 0)
	{
		TokenID = NextTokenID++;
	}

	FOwnedEntry& Entry = Entries.Add(TokenID);
	Entry.OwnerID = OwnerID;
	Entry.Tags = Tags;
	for (const FGameplayTag& Tag : Tags)
	{
		if (Tag.IsValid())
		{
			AbilitySystem->AddLooseGameplayTag(Tag);
		}
	}

	FWeaponOwnedTagToken Token;
	Token.RuntimeToken = ActiveRuntimeToken;
	Token.TokenID = TokenID;
	return Token;
}

bool FWeaponRuntimeTagLedger::Release(const FWeaponOwnedTagToken& Token)
{
	if (Token.RuntimeToken != ActiveRuntimeToken)
	{
		return false;
	}

	FOwnedEntry Entry;
	if (!Entries.RemoveAndCopyValue(Token.TokenID, Entry))
	{
		return false;
	}

	if (UAbilitySystemComponent* AbilitySystem = ASC.Get())
	{
		for (const FGameplayTag& Tag : Entry.Tags)
		{
			if (Tag.IsValid())
			{
				AbilitySystem->RemoveLooseGameplayTag(Tag);
			}
		}
	}
	return true;
}

int32 FWeaponRuntimeTagLedger::ReleaseOwner(const FWeaponTagOwnerID& OwnerID)
{
	TArray<uint64> MatchingIDs;
	for (const TPair<uint64, FOwnedEntry>& Pair : Entries)
	{
		if (Pair.Value.OwnerID == OwnerID)
		{
			MatchingIDs.Add(Pair.Key);
		}
	}

	int32 ReleasedCount = 0;
	for (const uint64 TokenID : MatchingIDs)
	{
		FWeaponOwnedTagToken Token;
		Token.RuntimeToken = ActiveRuntimeToken;
		Token.TokenID = TokenID;
		ReleasedCount += Release(Token) ? 1 : 0;
	}
	return ReleasedCount;
}

void FWeaponRuntimeTagLedger::ReleaseAll(const FWeaponRuntimeToken& RuntimeToken)
{
	if (RuntimeToken != ActiveRuntimeToken)
	{
		return;
	}

	TArray<uint64> TokenIDs;
	Entries.GetKeys(TokenIDs);
	for (const uint64 TokenID : TokenIDs)
	{
		FWeaponOwnedTagToken Token;
		Token.RuntimeToken = ActiveRuntimeToken;
		Token.TokenID = TokenID;
		Release(Token);
	}
}

bool FWeaponRuntimeTagLedger::IsActive(const FWeaponOwnedTagToken& Token) const
{
	return Token.RuntimeToken == ActiveRuntimeToken && Entries.Contains(Token.TokenID);
}
