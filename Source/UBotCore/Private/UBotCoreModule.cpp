#include "UBotCore.h"

#include "Misc/CoreDelegates.h"
#include "Modules/ModuleManager.h"
#include "UBotCoreSettings.h"
#include "UBotExtensionRegistry.h"
#include "UBotPackageRegistry.h"

DEFINE_LOG_CATEGORY(LogUBot);

void FUBotCoreModule::StartupModule()
{
    // Plugin discovery is complete before any module loads, so the package list is
    // already accurate here. Validation waits until every plugin module has loaded
    // so that packages registering extensions late are not reported as missing.
    FUBotPackageRegistry::Get().Refresh();
    LoadingCompleteHandle = FCoreDelegates::OnAllModuleLoadingPhasesComplete.AddRaw(
        this, &FUBotCoreModule::HandleAllModuleLoadingPhasesComplete);
}

void FUBotCoreModule::ShutdownModule()
{
    FCoreDelegates::OnAllModuleLoadingPhasesComplete.Remove(LoadingCompleteHandle);
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
