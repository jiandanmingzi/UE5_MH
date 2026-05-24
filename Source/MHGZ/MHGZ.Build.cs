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
			"Slate"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"MHGZ",
			"MHGZ/Variant_Platforming",
			"MHGZ/Variant_Platforming/Animation",
			"MHGZ/Variant_Combat",
			"MHGZ/Variant_Combat/AI",
			"MHGZ/Variant_Combat/Animation",
			"MHGZ/Variant_Combat/Gameplay",
			"MHGZ/Variant_Combat/Interfaces",
			"MHGZ/Variant_Combat/UI",
			"MHGZ/Variant_SideScrolling",
			"MHGZ/Variant_SideScrolling/AI",
			"MHGZ/Variant_SideScrolling/Gameplay",
			"MHGZ/Variant_SideScrolling/Interfaces",
			"MHGZ/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
