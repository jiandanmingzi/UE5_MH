// Copyright MHGZ Project. All Rights Reserved.

#include "WeaponRuntime/MHGZWeaponCombatConfig.h"

#define LOCTEXT_NAMESPACE "MHGZWeaponCombatConfig"

FPrimaryAssetId UWeaponCombatConfigBase::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("WeaponCombatConfig"), GetFName());
}

#if WITH_EDITOR
EDataValidationResult UWeaponCombatConfigBase::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	bool bInvalid = false;

	auto AddError = [&Context, &bInvalid](const FText& Error)
	{
		Context.AddError(Error);
		bInvalid = true;
	};

	if (ComboData == nullptr)
	{
		AddError(LOCTEXT("MissingComboData", "ComboData must be assigned."));
	}

	return bInvalid ? EDataValidationResult::Invalid
		: (Result == EDataValidationResult::NotValidated ? EDataValidationResult::Valid : Result);
}
#endif

#undef LOCTEXT_NAMESPACE
