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
            "UBotCore"
        });

        PrivateDependencyModuleNames.AddRange(new[]
        {
            "UnrealEd",
            "Slate",
            "SlateCore",
            "InputCore",
            "ToolMenus",
            "Projects",
            "Json",
            "DeveloperSettings",
            "WorkspaceMenuStructure"
        });
    }
}
