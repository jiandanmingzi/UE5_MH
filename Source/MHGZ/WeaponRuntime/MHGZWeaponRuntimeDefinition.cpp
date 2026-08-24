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
	// A resource component is gameplay state and may exist before its HUD is
	// implemented.  The reverse is not meaningful: a resource widget has
	// nothing to bind to without a component.
	if (ResourceWidgetClass != nullptr && ResourceComponentClass == nullptr)
	{
		AddError(LOCTEXT("ResourceWidgetRequiresComponent", "ResourceWidgetClass requires ResourceComponentClass."));
	}

	return bInvalid ? EDataValidationResult::Invalid
		: (Result == EDataValidationResult::NotValidated ? EDataValidationResult::Valid : Result);
}
#endif

#undef LOCTEXT_NAMESPACE
