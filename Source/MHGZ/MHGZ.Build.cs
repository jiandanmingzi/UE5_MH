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
			"MotionWarping",
			"PoseSearch", 
			"MotionTrajectory"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[] {
				"AnimationBlueprintLibrary",
				"BlueprintGraph",
				"Kismet",
				"PoseSearchEditor",
				"TraceAnalysis",
				"TraceServices",
				"UnrealEd"
			});
		}

		PublicIncludePaths.AddRange(new string[] {
			"MHGZ",
			"MHGZ/ActionSystem",
			"MHGZ/AttributeSystem",
			"MHGZ/Inventory",
			"MHGZ/Equipment",
			"MHGZ/InputSystem",
			"MHGZ/InsectGlaive",
			"MHGZ/InsectGlaive/Kinsect",
			"MHGZ/WeaponRuntime",
			"MHGZ/Monster",
			"MHGZ/UI",
			"MHGZ/Data"
		});
	}
}
