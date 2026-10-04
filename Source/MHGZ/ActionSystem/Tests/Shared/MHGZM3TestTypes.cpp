// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZM3TestTypes.h"

#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
#include "GameplayEffectComponents/TargetTagRequirementsGameplayEffectComponent.h"
#include "GameplayTagContainer.h"

namespace
{
/** UE5.6 原生 GE 通过 TargetTagsGameplayEffectComponent 授予目标标签。 */
void GrantTargetTag(UTargetTagsGameplayEffectComponent& TagsComponent,
	const FGameplayTag& Tag)
{
	FInheritedTagContainer Tags;
	Tags.AddTag(Tag);
	TagsComponent.SetAndApplyTargetTagChanges(Tags);
}
}

UMHGZM3WhiteExtractGE::UMHGZM3WhiteExtractGE()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(90.f));
	UTargetTagsGameplayEffectComponent* TagsComponent =
		CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("TargetTags"));
	GEComponents.Add(TagsComponent);
	GrantTargetTag(*TagsComponent, FGameplayTag::RequestGameplayTag(
		TEXT("WeaponResource.IG.Extract.White")));
}

UMHGZM3OrangeExtractGE::UMHGZM3OrangeExtractGE()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(120.f));
	UTargetTagsGameplayEffectComponent* TagsComponent =
		CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("TargetTags"));
	GEComponents.Add(TagsComponent);
	GrantTargetTag(*TagsComponent, FGameplayTag::RequestGameplayTag(
		TEXT("WeaponResource.IG.Extract.Orange")));
}

UMHGZM3RedExtractGE::UMHGZM3RedExtractGE()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(60.f));
	UTargetTagsGameplayEffectComponent* TagsComponent =
		CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("TargetTags"));
	GEComponents.Add(TagsComponent);
	GrantTargetTag(*TagsComponent, FGameplayTag::RequestGameplayTag(
		TEXT("WeaponResource.IG.Extract.Red")));
}

UMHGZM3TripleUpGE::UMHGZM3TripleUpGE()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(90.f));
	UTargetTagsGameplayEffectComponent* TagsComponent =
		CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("TargetTags"));
	GEComponents.Add(TagsComponent);
	GrantTargetTag(*TagsComponent, FGameplayTag::RequestGameplayTag(
		TEXT("WeaponResource.IG.TripleUp")));
}

UMHGZM3BlockedTripleUpGE::UMHGZM3BlockedTripleUpGE()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(90.f));
	UTargetTagRequirementsGameplayEffectComponent* Requirements =
		CreateDefaultSubobject<UTargetTagRequirementsGameplayEffectComponent>(
			TEXT("ApplicationRequirements"));
	Requirements->ApplicationTagRequirements.RequireTags.AddTag(
		FGameplayTag::RequestGameplayTag(TEXT("Cost.IG.TripleUp")));
	GEComponents.Add(Requirements);
}
