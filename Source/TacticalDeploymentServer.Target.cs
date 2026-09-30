// Copyright TacticalDeployment. All Rights Reserved.

using UnrealBuildTool;

public class TacticalDeploymentServerTarget : TargetRules
{
	public TacticalDeploymentServerTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Server;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;

		bUseIris = true;
		bWithPushModel = true;

		// Keep logs in Shipping dedicated servers for match forensics (hit-reg disputes, cheat reports).
		bUseLoggingInShipping = true;

		ExtraModuleNames.Add("TacticalDeployment");
	}
}
