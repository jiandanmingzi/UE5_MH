// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class MHGZEditorTarget : TargetRules
{
	public MHGZEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_6;
		// Several file-local test and action helpers intentionally share descriptive
		// names. Keep them in separate translation units instead of Unity-merging
		// those anonymous namespaces during normal editor builds.
		bUseUnityBuild = false;
		ExtraModuleNames.Add("MHGZ");
	}
}
