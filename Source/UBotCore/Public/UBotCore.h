#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

UBOTCORE_API DECLARE_LOG_CATEGORY_EXTERN(LogUBot, Log, All);

class FUBotCoreModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    void HandleAllModuleLoadingPhasesComplete();

    FDelegateHandle LoadingCompleteHandle;
};
