#include "UBotManagerLauncher.h"

#include "Framework/Notifications/NotificationManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "UBotCore.h"
#include "UBotEditorSettings.h"
#include "UBotEditorText.h"
#include "UBotPackageService.h"
#include "Widgets/Notifications/SNotificationList.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

namespace UBotManagerLauncherPrivate
{
    FString CleanPath(const FString& Path)
    {
        FString Result = Path.TrimStartAndEnd().TrimQuotes();
        FPaths::NormalizeFilename(Result);
        return Result;
    }

    FString ResolveConfiguredPath(const FString& Configured)
    {
        FString Path = CleanPath(Configured);
        if (!Path.IsEmpty() && FPaths::IsRelative(Path))
        {
            Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), Path);
        }
        return Path;
    }

    void Notify(const FText& Text, const FText& SubText, bool bOfferDownload)
    {
        FNotificationInfo Info(Text);
        Info.SubText = SubText;
        Info.ExpireDuration = 10.0f;
        Info.bFireAndForget = true;
        if (bOfferDownload)
        {
            Info.HyperlinkText = UBot::EditorText::ManagerDownload();
            Info.Hyperlink = FSimpleDelegate::CreateLambda([]()
            {
                FPlatformProcess::LaunchURL(FUBotManagerLauncher::ReleasesUrl, nullptr, nullptr);
            });
        }

        if (const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
        {
            Item->SetCompletionState(SNotificationItem::CS_Fail);
        }
    }
}

bool FUBotManagerLauncher::Launch()
{
    using namespace UBotManagerLauncherPrivate;

    const UUBotEditorSettings* Settings = GetDefault<UUBotEditorSettings>();
    const FString Configured = ResolveConfiguredPath(Settings ? Settings->ManagerExecutable.FilePath : FString());
    if (!Configured.IsEmpty() && !FPaths::FileExists(Configured))
    {
        UE_LOG(LogUBot, Warning, TEXT("uBot: the configured uBot Manager '%s' does not exist; looking for an installed one."), *Configured);
    }

    const FString Executable = ResolveExecutable(Configured, ReadRegisteredExecutable(),
        FPlatformMisc::GetEnvironmentVariable(TEXT("LOCALAPPDATA")),
        [](const FString& Path) { return FPaths::FileExists(Path); });
    if (Executable.IsEmpty())
    {
        UE_LOG(LogUBot, Warning, TEXT("uBot: uBot Manager was not found (see Editor Preferences > Plugins > uBot)."));
        Notify(UBot::EditorText::ManagerNotFound(), UBot::EditorText::ManagerNotFoundHint(), /*bOfferDownload*/ true);
        return false;
    }

    const FString ProjectFile = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
    const FString Arguments = MakeArguments(ProjectFile);
    uint32 ProcessId = 0;
    FProcHandle Process = FPlatformProcess::CreateProc(*Executable, *Arguments, /*bLaunchDetached*/ true, /*bLaunchHidden*/ false,
        /*bLaunchReallyHidden*/ false, &ProcessId, /*PriorityModifier*/ 0, *FPaths::GetPath(Executable), /*PipeWriteChild*/ nullptr);
    if (!Process.IsValid())
    {
        UE_LOG(LogUBot, Warning, TEXT("uBot: could not start uBot Manager '%s'."), *Executable);
        Notify(UBot::EditorText::ManagerLaunchFailed(), FText::FromString(Executable), /*bOfferDownload*/ false);
        return false;
    }

    FPlatformProcess::CloseProc(Process);
    UE_LOG(LogUBot, Log, TEXT("uBot: started uBot Manager '%s' (pid %u) for %s."), *Executable, ProcessId, *ProjectFile);
    return true;
}

FString FUBotManagerLauncher::ResolveExecutable(const FString& Configured, const FString& Registered, const FString& LocalAppData,
    TFunctionRef<bool(const FString&)> FileExists)
{
    using namespace UBotManagerLauncherPrivate;

    TArray<FString> Candidates;
    if (const FString ConfiguredPath = ResolveConfiguredPath(Configured); !ConfiguredPath.IsEmpty())
    {
        Candidates.Add(ConfiguredPath);
    }
    if (const FString RegisteredPath = CleanPath(Registered); !RegisteredPath.IsEmpty())
    {
        Candidates.Add(RegisteredPath);
    }
    if (const FString LocalAppDataPath = CleanPath(LocalAppData); !LocalAppDataPath.IsEmpty())
    {
        Candidates.Add(FPaths::Combine(LocalAppDataPath, TEXT("uBot Manager"), TEXT("ubot-manager.exe")));
    }

    for (const FString& Candidate : Candidates)
    {
        if (FileExists(Candidate))
        {
            return Candidate;
        }
    }
    return FString();
}

FString FUBotManagerLauncher::MakeArguments(const FString& ProjectFile)
{
    FString Path = FPaths::ConvertRelativePathToFull(ProjectFile);
    FPaths::MakePlatformFilename(Path);
    return FString::Printf(TEXT("--project %s"), *FUBotPackageService::QuoteArgument(Path));
}

FString FUBotManagerLauncher::ReadRegisteredExecutable()
{
    FString Value;
#if PLATFORM_WINDOWS
    if (!FPlatformMisc::QueryRegKey(HKEY_CURRENT_USER, TEXT("Software\\AstreoX\\uBot Manager"), TEXT("Executable"), Value))
    {
        Value.Reset();
    }
#endif
    return Value;
}
