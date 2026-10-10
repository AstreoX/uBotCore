#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class FLayoutExtender;
class FSpawnTabArgs;
class FUBotPanelModel;
class SDockTab;
class SWidget;
struct IConsoleCommand;

/**
 * Editor module of uBot Core: the uBot panel (nomad tab, docked next to Details in the Level Editor
 * by default, Window menu entry, status bar entry and the "uBot.OpenPanel" console command), PIE
 * sessions for the runtime status, the UBotPackage commandlet and FUBotPackageService.
 */
class UBOTCOREEDITOR_API FUBotCoreEditorModule : public IModuleInterface
{
public:
    /** Identifier of the nomad tab that hosts the uBot panel. */
    static const FName PanelTabName;

    /**
     * Console command that opens the uBot panel, so headless scripts and tooling can do it, for example
     * UnrealEditor.exe <Project>.uproject -ExecCmds="uBot.OpenPanel". Registered for the lifetime of the module.
     */
    static const TCHAR* const OpenPanelCommandName;

    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

    /**
     * Opens (or focuses) the uBot panel. It opens in the Level Editor's tab stack next to Details unless
     * the user has placed it elsewhere before. Does nothing when Slate is not running.
     */
    static void OpenPanel();

private:
    void RegisterMenus();
    void HandleRegisterLayoutExtensions(FLayoutExtender& Extender);
    TSharedRef<SDockTab> SpawnPanelTab(const FSpawnTabArgs& Args);
    TSharedRef<SWidget> MakeStatusBarEntry();
    void HandlePostPIEStarted(bool bIsSimulating);
    void HandleEndPIE(bool bIsSimulating);

    TSharedPtr<FUBotPanelModel> PanelModel;
    FDelegateHandle PostPIEStartedHandle;
    FDelegateHandle EndPIEHandle;
    FDelegateHandle LayoutExtensionHandle;
    IConsoleCommand* OpenPanelCommand = nullptr;
    bool bTabSpawnerRegistered = false;
};
