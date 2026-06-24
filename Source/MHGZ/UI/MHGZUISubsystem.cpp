// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZUISubsystem.h"

UMHGZUISubsystem* UMHGZUISubsystem::Get(const UObject* WorldContext)
{
	if (const UGameInstance* GI = WorldContext->GetWorld()->GetGameInstance())
	{
		return GI->GetSubsystem<UMHGZUISubsystem>();
	}
	return nullptr;
}
