// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class MHGZTarget : TargetRules
{
	public MHGZTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_6;
		// Match the editor target: this codebase contains independent file-local
		// helpers whose names collide only after Unity source aggregation.
		bUseUnityBuild = false;
		ExtraModuleNames.Add("MHGZ");
	}
}
