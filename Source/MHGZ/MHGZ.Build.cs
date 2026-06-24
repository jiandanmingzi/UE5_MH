// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MHGZ : ModuleRules
{
	public MHGZ(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate",
			// GAS 核心模块
			"GameplayAbilities",
			"GameplayTags",
			"GameplayTasks",
			// MotionWarping
			"MotionWarping"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"MHGZ",
			"MHGZ/ActionSystem",
			"MHGZ/AttributeSystem",
			"MHGZ/Inventory",
			"MHGZ/Equipment",
			"MHGZ/InputSystem",
			"MHGZ/InsectGlaive",
			"MHGZ/InsectGlaive/Kinsect",
			"MHGZ/Monster",
			"MHGZ/UI",
			"MHGZ/Data"
		});
	}
}
