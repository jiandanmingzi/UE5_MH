// Copyright MHGZ Project. All Rights Reserved.

#include "WeaponRuntime/MHGZWeaponRuntimeDefinition.h"

#include "AttributeSystem/MHGZWeaponResourceComponent.h"
#include "UI/MHGZWeaponResourceWidget.h"

#define LOCTEXT_NAMESPACE "MHGZWeaponRuntimeDefinition"

FPrimaryAssetId UWeaponRuntimeDefinition::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("WeaponRuntimeDefinition"), GetFName());
}

#if WITH_EDITOR
EDataValidationResult UWeaponRuntimeDefinition::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	bool bInvalid = false;

	auto AddError = [&Context, &bInvalid](const FText& Error)
	{
		Context.AddError(Error);
		bInvalid = true;
	};

	if (!WeaponTypeTag.IsValid())
	{
		AddError(LOCTEXT("InvalidWeaponTypeTag", "WeaponTypeTag must be a valid tag."));
	}
	if (InputProfile == nullptr)
	{
		AddError(LOCTEXT("MissingInputProfile", "InputProfile must be assigned."));
	}
	if (CombatConfig == nullptr)
	{
		AddError(LOCTEXT("MissingCombatConfig", "CombatConfig must be assigned."));
	}
	if ((ResourceComponentClass == nullptr) != (ResourceWidgetClass == nullptr))
	{
		AddError(LOCTEXT("IncompleteResourceUI", "ResourceComponentClass and ResourceWidgetClass must either both be assigned or both be empty."));
	}

	return bInvalid ? EDataValidationResult::Invalid
		: (Result == EDataValidationResult::NotValidated ? EDataValidationResult::Valid : Result);
}
#endif

#undef LOCTEXT_NAMESPACE
