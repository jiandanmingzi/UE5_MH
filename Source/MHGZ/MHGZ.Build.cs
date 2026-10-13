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
			"SlateCore",
			// GAS 核心模块
			"GameplayAbilities",
			"GameplayTags",
			"GameplayTasks",
			// MotionWarping
			"MotionWarping",
			"PoseSearch", 
			"MotionTrajectory",
			"AnimGraphRuntime"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		if (Target.bBuildEditor)
		{
			if (Target.Platform == UnrealTargetPlatform.Win64)
			{
				PublicSystemLibraries.Add("Bcrypt.lib");
			}
			PrivateDependencyModuleNames.AddRange(new string[] {
				"AssetRegistry",
				"AnimGraph",
				"AnimationBlueprintLibrary",
				"BlueprintGraph",
				"Kismet",
				"Json",
				"PoseSearchEditor",
				"TraceAnalysis",
				"TraceServices",
				"RenderCore", // Opt-in HUD render regression captures.
				"UnrealEd",
				"UMGEditor"
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
