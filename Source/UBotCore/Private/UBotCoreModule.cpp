#include "UBotCore.h"

#include "Misc/CoreDelegates.h"
#include "Modules/ModuleManager.h"
#include "UBotCoreSettings.h"
#include "UBotExtensionRegistry.h"
#include "UBotPackageRegistry.h"
#include "UBotRuntimeStatus.h"

DEFINE_LOG_CATEGORY(LogUBot);

void FUBotCoreModule::StartupModule()
{
    // Plugin discovery is complete before any module loads, so the package list is
    // already accurate here. Validation waits until every plugin module has loaded
    // so that packages registering extensions late are not reported as missing.
    FUBotPackageRegistry::Get().Refresh();
    LoadingCompleteHandle = FCoreDelegates::OnAllModuleLoadingPhasesComplete.AddRaw(
        this, &FUBotCoreModule::HandleAllModuleLoadingPhasesComplete);

    // In the editor, UBotCoreEditor maps PIE sessions instead.
    if (!GIsEditor)
    {
        FUBotRuntimeStatus::Get().StartGameSessionTracking();
    }
}

void FUBotCoreModule::ShutdownModule()
{
    FCoreDelegates::OnAllModuleLoadingPhasesComplete.Remove(LoadingCompleteHandle);

    FUBotRuntimeStatus& RuntimeStatus = FUBotRuntimeStatus::Get();
    RuntimeStatus.StopGameSessionTracking();
    // A session still running now (the process is exiting) is written as it stands.
    RuntimeStatus.EndSession();

    FUBotExtensionRegistry::Get().UnregisterAllFromPackage(TEXT("UBotCore"));
}

void FUBotCoreModule::HandleAllModuleLoadingPhasesComplete()
{
    const UUBotCoreSettings* Settings = GetDefault<UUBotCoreSettings>();
    if (Settings && Settings->bValidatePackagesOnStartup)
    {
        FUBotPackageRegistry::Get().Refresh();
        FUBotPackageRegistry::Get().ValidateAndLog();
    }
}

IMPLEMENT_MODULE(FUBotCoreModule, UBotCore)
