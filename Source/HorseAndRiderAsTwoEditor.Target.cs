using UnrealBuildTool;
using System.Collections.Generic;

public class HorseAndRiderAsTwoEditorTarget : TargetRules
{
	public HorseAndRiderAsTwoEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V6;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("HorseAndRiderAsTwo");
	}
}
