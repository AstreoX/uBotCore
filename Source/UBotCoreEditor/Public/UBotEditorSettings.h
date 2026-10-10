#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "UBotEditorSettings.generated.h"

/** Per-user editor settings for uBot (Editor Preferences > Plugins > uBot). */
UCLASS(Config = EditorPerProjectUserSettings, meta = (DisplayName = "uBot"))
class UBOTCOREEDITOR_API UUBotEditorSettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    //~ Begin UDeveloperSettings interface
    virtual FName GetContainerName() const override;
    virtual FName GetCategoryName() const override;
    //~ End UDeveloperSettings interface

    /**
     * ubot-manager.exe to start from the uBot panel. When empty (or missing) the panel uses the path
     * uBot Manager registered (HKCU\Software\AstreoX\uBot Manager, value Executable), then
     * %LOCALAPPDATA%\uBot Manager\ubot-manager.exe.
     */
    UPROPERTY(Config, EditAnywhere, Category = "uBot Manager", meta = (FilePathFilter = "exe"))
    FFilePath ManagerExecutable;
};
