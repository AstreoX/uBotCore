using UnrealBuildTool;

public class UBotCoreEditor : ModuleRules
{
    public UBotCoreEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            // Public because UBotPackageCommandlet.h derives from UCommandlet.
            "Engine",
            // Public because UBotEditorSettings.h derives from UDeveloperSettings.
            "DeveloperSettings",
            "UBotCore"
        });

        PrivateDependencyModuleNames.AddRange(new[]
        {
            "UnrealEd",
            // K2 nodes, node spawners and the K2 schema for the UBotDocExport commandlet.
            "BlueprintGraph",
            // The Level Editor layout extension that docks the uBot tab next to Details.
            "LevelEditor",
            "Slate",
            "SlateCore",
            "ToolMenus",
            "Projects",
            "Json"
        });
    }
}
