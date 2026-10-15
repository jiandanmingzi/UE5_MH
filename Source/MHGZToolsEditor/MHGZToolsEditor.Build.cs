// Copyright MHGZ Project. All Rights Reserved.

using UnrealBuildTool;

public class MHGZToolsEditor : ModuleRules
{
	public MHGZToolsEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
		PrivateDependencyModuleNames.AddRange(new string[] {
			"MHGZ", // One-way dependency for the migrated asset tools.
			"AssetRegistry", "AnimationBlueprintLibrary", "BlueprintGraph", "UnrealEd",
			"AnimGraph", "AnimGraphRuntime", "Kismet", "Json", "PoseSearch", "PoseSearchEditor", "BlendStack", "Chooser",
			"UMG", "UMGEditor", "Slate", "SlateCore", "RenderCore", "GameplayTags", "GameplayAbilities",
			"TraceAnalysis", "TraceServices"
		});
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicSystemLibraries.Add("Bcrypt.lib");
		}
		// The runtime module must not depend on this editor-only module.
	}
}
