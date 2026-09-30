// Copyright TacticalDeployment. All Rights Reserved.

using UnrealBuildTool;

public class TacticalDeploymentEditorTarget : TargetRules
{
	public TacticalDeploymentEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;

		bWithPushModel = true;

		ExtraModuleNames.Add("TacticalDeployment");
	}
}
