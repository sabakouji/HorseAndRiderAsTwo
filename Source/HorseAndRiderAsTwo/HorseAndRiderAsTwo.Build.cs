using UnrealBuildTool;

public class HorseAndRiderAsTwo : ModuleRules
{
	public HorseAndRiderAsTwo(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"PhysicsCore"      // PhysicsConstraintComponent（手綱の物理結合）で使用
		});
	}
}
