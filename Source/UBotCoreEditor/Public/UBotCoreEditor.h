#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class FSpawnTabArgs;
class SDockTab;

/** Editor module of uBot Core: hosts the uBot package manager tab and its menu entries. */
class UBOTCOREEDITOR_API FUBotCoreEditorModule : public IModuleInterface
{
public:
    /** Identifier of the nomad tab that hosts the package manager. */
    static const FName PackageManagerTabName;

    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

    /** Opens (or focuses) the package manager tab. Does nothing when Slate is not running. */
    static void OpenPackageManager();

private:
    void RegisterMenus();
    TSharedRef<SDockTab> SpawnPackageManagerTab(const FSpawnTabArgs& Args);

    bool bTabSpawnerRegistered = false;
};
