// Copyright TacticalDeployment. All Rights Reserved.

using UnrealBuildTool;

public class TacticalDeploymentServerTarget : TargetRules
{
	public TacticalDeploymentServerTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Server;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;

		bWithPushModel = true;

		// Keep logs in Shipping dedicated servers for match forensics (hit-reg disputes, cheat reports).
		bUseLoggingInShipping = true;

		ExtraModuleNames.Add("TacticalDeployment");
	}
}
