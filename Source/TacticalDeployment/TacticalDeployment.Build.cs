// Copyright TacticalDeployment. All Rights Reserved.

using UnrealBuildTool;

public class TacticalDeployment : ModuleRules
{
	public TacticalDeployment(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"PhysicsCore",
			"NetCore",           // Push model (MARK_PROPERTY_DIRTY_FROM_NAME)
			"DeveloperSettings", // UTacticalFogOfWarSettings is in a public header
		});

		// Adds IrisCore and defines UE_WITH_IRIS (always 1: Iris is always compiled in on 5.8).
		SetupIrisSupport(Target);
	}
}
