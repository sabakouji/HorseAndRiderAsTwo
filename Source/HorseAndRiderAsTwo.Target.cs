using UnrealBuildTool;
using System.Collections.Generic;

public class HorseAndRiderAsTwoTarget : TargetRules
{
	public HorseAndRiderAsTwoTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V6;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("HorseAndRiderAsTwo");
	}
}
