// Copyright Epic Games, Inc. All Rights Reserved.

#include "MHGZGameMode.h"
#include "MHGZPlayerState.h"
#include "MHGZHUD.h"

AMHGZGameMode::AMHGZGameMode()
{
	PlayerStateClass = AMHGZPlayerState::StaticClass();
	HUDClass = AMHGZHUD::StaticClass();
}
