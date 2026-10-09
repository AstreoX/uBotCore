#include "UBotCoreEditor.h"

#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/Docking/TabManager.h"
#include "Modules/ModuleManager.h"
#include "SUBotPackageManager.h"
#include "Styling/AppStyle.h"
#include "Textures/SlateIcon.h"
#include "ToolMenus.h"
#include "UBotPackageService.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "UBotPackageManager"

const FName FUBotCoreEditorModule::PackageManagerTabName(TEXT("UBotPackageManager"));

void FUBotCoreEditorModule::StartupModule()
{
    // Commandlets and other headless runs only need the package service.
    if (FSlateApplication::IsInitialized())
    {
        FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
                PackageManagerTabName,
                FOnSpawnTab::CreateRaw(this, &FUBotCoreEditorModule::SpawnPackageManagerTab))
            .SetDisplayName(LOCTEXT("TabTitle", "uBot Packages"))
            .SetTooltipText(LOCTEXT("TabTooltip", "Install, update, enable and disable uBot packages."))
            .SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Package"))
            // The explicit Window and Tools entries below replace the automatic Window menu listing.
            .SetMenuType(ETabSpawnerMenuType::Hidden);
        bTabSpawnerRegistered = true;
    }

    // Does not fire for commandlets or when Slate is not used.
    UToolMenus::RegisterStartupCallback(
        FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FUBotCoreEditorModule::RegisterMenus));
}

void FUBotCoreEditorModule::ShutdownModule()
{
    UToolMenus::UnRegisterStartupCallback(this);
    UToolMenus::UnregisterOwner(this);

    if (bTabSpawnerRegistered && FSlateApplication::IsInitialized())
    {
        FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(PackageManagerTabName);
    }
    bTabSpawnerRegistered = false;

    FUBotPackageService::Get().Shutdown();
}

void FUBotCoreEditorModule::OpenPackageManager()
{
    if (FSlateApplication::IsInitialized())
    {
        FGlobalTabmanager::Get()->TryInvokeTab(FTabId(PackageManagerTabName));
    }
}

void FUBotCoreEditorModule::RegisterMenus()
{
    FToolMenuOwnerScoped OwnerScoped(this);

    UToolMenus* ToolMenus = UToolMenus::Get();
    if (!ToolMenus)
    {
        return;
    }

    const FSlateIcon Icon(FAppStyle::GetAppStyleSetName(), "Icons.Package");
    const FUIAction OpenAction(FExecuteAction::CreateStatic(&FUBotCoreEditorModule::OpenPackageManager));

    if (UToolMenu* WindowMenu = ToolMenus->ExtendMenu("LevelEditor.MainMenu.Window"))
    {
        FToolMenuSection& Section = WindowMenu->FindOrAddSection("uBot", LOCTEXT("UBotMenuSection", "uBot"));
        Section.AddMenuEntry(
            "OpenUBotPackageManager",
            LOCTEXT("OpenPackageManagerLabel", "uBot Packages"),
            LOCTEXT("OpenPackageManagerTooltip", "Open the uBot package manager."),
            Icon,
            OpenAction);
    }

    if (UToolMenu* ToolsMenu = ToolMenus->ExtendMenu("LevelEditor.MainMenu.Tools"))
    {
        FToolMenuSection& Section = ToolsMenu->FindOrAddSection("uBot", LOCTEXT("UBotMenuSection", "uBot"));
        Section.AddMenuEntry(
            "OpenUBotPackageManager",
            LOCTEXT("OpenPackageManagerLabel", "uBot Packages"),
            LOCTEXT("OpenPackageManagerTooltip", "Open the uBot package manager."),
            Icon,
            OpenAction);
    }
}

TSharedRef<SDockTab> FUBotCoreEditorModule::SpawnPackageManagerTab(const FSpawnTabArgs& Args)
{
    return SNew(SDockTab)
        .TabRole(ETabRole::NomadTab)
        .Label(LOCTEXT("TabTitle", "uBot Packages"))
        [
            SNew(SUBotPackageManager)
        ];
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FUBotCoreEditorModule, UBotCoreEditor)
