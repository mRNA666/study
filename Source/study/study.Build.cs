// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class study : ModuleRules
{
	public study(ReadOnlyTargetRules Target) : base(Target)
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

			// Dynamic mesh / SDF digging:
			//   GeometryCore      -> FDynamicMesh3, FMarchingCubes
			//   GeometryFramework -> UDynamicMeshComponent
			"GeometryCore",
			"GeometryFramework"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"study",
			"study/Variant_Horror",
			"study/Variant_Horror/UI",
			"study/Variant_Shooter",
			"study/Variant_Shooter/AI",
			"study/Variant_Shooter/UI",
			"study/Variant_Shooter/Weapons"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
