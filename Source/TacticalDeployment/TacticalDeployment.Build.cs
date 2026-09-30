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

		// Adds IrisCore and defines UE_WITH_IRIS when the target enables bUseIris.
		SetupIrisSupport(Target);
	}
}
