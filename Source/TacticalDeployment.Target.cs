// Copyright TacticalDeployment. All Rights Reserved.

using UnrealBuildTool;

public class TacticalDeploymentTarget : TargetRules
{
	public TacticalDeploymentTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;

		// Iris replication + push-model dirtiness for every replicated property we own.
		bUseIris = true;
		bWithPushModel = true;

		ExtraModuleNames.Add("TacticalDeployment");
	}
}
