#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "UBotCoreSettings.generated.h"

/** Project settings for uBot Core (Project Settings > Plugins > uBot Core, stored in DefaultEngine.ini). */
UCLASS(Config = Engine, DefaultConfig, meta = (DisplayName = "uBot Core"))
class UBOTCORE_API UUBotCoreSettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    //~ Begin UDeveloperSettings interface
    virtual FName GetCategoryName() const override;
    //~ End UDeveloperSettings interface

    /** Switch the engine to a fixed time step when a game or PIE world begins play. */
    UPROPERTY(Config, EditAnywhere, Category = "Clock")
    bool bUseFixedTimeStep = false;

    /** Fixed time step rate used when bUseFixedTimeStep is set. */
    UPROPERTY(Config, EditAnywhere, Category = "Clock", meta = (ClampMin = "1.0", ClampMax = "1000.0", EditCondition = "bUseFixedTimeStep"))
    double FixedTimeStepHz = 60.0;

    /** Validate the installed uBot packages once all modules have loaded and log every problem. */
    UPROPERTY(Config, EditAnywhere, Category = "Packages")
    bool bValidatePackagesOnStartup = true;

    /** Where packages are installed, relative to the project directory. */
    UPROPERTY(Config, EditAnywhere, Category = "Packages")
    FString PackageInstallDirectory = TEXT("Plugins/uBot");

    /**
     * Extra package index files (format 2, absolute or project-relative), loaded after the main
     * index: Saved/uBot/index.json written by uBot Manager, or else UBotCore's Index/index.json.
     */
    UPROPERTY(Config, EditAnywhere, Category = "Packages")
    TArray<FString> AdditionalPackageIndexFiles;

    /** Git executable used to install and update packages. */
    UPROPERTY(Config, EditAnywhere, Category = "Packages")
    FString GitExecutable = TEXT("git");
};
