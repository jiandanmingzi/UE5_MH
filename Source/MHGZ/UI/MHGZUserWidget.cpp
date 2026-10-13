// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZUserWidget.h"

void UMHGZUserWidget::BindToASC(UAbilitySystemComponent* InASC)
{
	UnbindFromASC();
	BoundASC = InASC;
}

void UMHGZUserWidget::UnbindFromASC()
{
	BoundASC = nullptr;
}

void UMHGZUserWidget::NativeDestruct()
{
	UnbindFromASC();
	Super::NativeDestruct();
}

void UMHGZUserWidget::OnValueUpdated_Implementation(float Current, float Max)
{
	// 子类覆写
}
