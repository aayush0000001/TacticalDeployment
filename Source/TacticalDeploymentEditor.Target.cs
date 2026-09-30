// Copyright TacticalDeployment. All Rights Reserved.

using UnrealBuildTool;

public class TacticalDeploymentEditorTarget : TargetRules
{
	public TacticalDeploymentEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;

		bUseIris = true;
		bWithPushModel = true;

		ExtraModuleNames.Add("TacticalDeployment");
	}
}
