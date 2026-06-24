// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZWeaponComboData.h"

FPrimaryAssetId UMHGZWeaponComboData::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("WeaponComboData"), GetFName());
}
