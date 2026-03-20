using UnrealBuildTool;

public class tooncodex573Editor : ModuleRules
{
	public tooncodex573Editor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"tooncodex573",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"AssetRegistry",
			"Json",
			"LevelEditor",
			"MaterialEditor",
			"Networking",
			"RenderCore",
			"Slate",
			"SlateCore",
			"Sockets",
			"ToolMenus",
			"UnrealEd",
			"WorkspaceMenuStructure",
		});
	}
}
