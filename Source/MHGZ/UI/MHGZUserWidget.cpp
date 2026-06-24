// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZUserWidget.h"

void UMHGZUserWidget::BindToASC(UAbilitySystemComponent* InASC)
{
	BoundASC = InASC;
}

void UMHGZUserWidget::OnValueUpdated_Implementation(float Current, float Max)
{
	// 子类覆写
}
