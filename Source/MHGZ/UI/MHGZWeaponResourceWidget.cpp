// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZWeaponResourceWidget.h"

void UMHGZWeaponResourceWidget::BindToResourceComponent(UMHGZWeaponResourceComponent* InRC)
{
	UnbindFromResourceComponent();
	ResourceComponent = InRC;
}

void UMHGZWeaponResourceWidget::UnbindFromResourceComponent()
{
	ResourceComponent = nullptr;
}

void UMHGZWeaponResourceWidget::NativeDestruct()
{
	UnbindFromResourceComponent();
	Super::NativeDestruct();
}
