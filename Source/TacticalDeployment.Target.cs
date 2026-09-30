// Copyright TacticalDeployment. All Rights Reserved.

using UnrealBuildTool;

public class TacticalDeploymentTarget : TargetRules
{
	public TacticalDeploymentTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;

		// No bWithPushModel here: installed engines reject it for Game targets (they share
		// UnrealGame's build environment), and push model only saves work on the sending side,
		// which is the dedicated server (TacticalDeploymentServer.Target.cs enables it).

		ExtraModuleNames.Add("TacticalDeployment");
	}
}
