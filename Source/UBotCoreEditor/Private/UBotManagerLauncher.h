#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

/** Finds and starts uBot Manager for the open project. */
class FUBotManagerLauncher
{
public:
    static constexpr const TCHAR* ReleasesUrl = TEXT("https://github.com/AstreoX/uBotManager/releases");

    /**
     * Starts uBot Manager detached with --project "<absolute .uproject>". Shows a notification with a
     * download link when no executable is found, or an error notification when it does not start.
     */
    static bool Launch();

    /**
     * The first candidate that exists: Configured (UUBotEditorSettings::ManagerExecutable; relative
     * paths are taken from the project directory), Registered (HKCU\Software\AstreoX\uBot Manager,
     * value Executable), then <LocalAppData>\uBot Manager\ubot-manager.exe. Empty candidates are
     * skipped; returns an empty string when none exists.
     */
    static FString ResolveExecutable(const FString& Configured, const FString& Registered, const FString& LocalAppData,
        TFunctionRef<bool(const FString&)> FileExists);

    /** --project "<ProjectFile>", quoted for CreateProc. */
    static FString MakeArguments(const FString& ProjectFile);

    /** The Executable value uBot Manager writes at startup; empty when absent or not on Windows. */
    static FString ReadRegisteredExecutable();
};
