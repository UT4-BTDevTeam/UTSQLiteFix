
namespace UnrealBuildTool.Rules
{
	public class UTSQLiteFix : ModuleRules
	{
		public UTSQLiteFix(TargetInfo Target)
        {
            PrivateIncludePaths.Add("UTSQLiteFix/Private");

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
                    "Engine",
                    "UnrealTournament",
				}
			);
		}
	}
}