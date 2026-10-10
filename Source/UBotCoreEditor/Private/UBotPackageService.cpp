#include "UBotPackageService.h"

#include "Async/Async.h"
#include "Async/TaskGraphInterfaces.h"
#include "HAL/CriticalSection.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Interfaces/IPluginManager.h"
#include "Interfaces/IProjectManager.h"
#include "Misc/App.h"
#include "Misc/Char.h"
#include "Misc/MonitoredProcess.h"
#include "Misc/Optional.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "UBotCore.h"
#include "UBotCoreSettings.h"
#include "UBotPackageIndex.h"
#include "UBotPackageRegistry.h"
#include "UBotSemVer.h"
#include "UObject/Class.h"

namespace UBotPackageServicePrivate
{
    constexpr int32 MaxReportedGitLines = 40;
    constexpr double CancelGraceSeconds = 30.0;

    const TCHAR* const RestartMessage = TEXT("Restart the editor to apply the change.");

    const FUBotPackageInfo* FindByName(const TArray<FUBotPackageInfo>& Packages, const FString& Name)
    {
        return Packages.FindByPredicate([&Name](const FUBotPackageInfo& Package)
        {
            return Package.Name.Equals(Name, ESearchCase::IgnoreCase);
        });
    }

    // Same meaning as in the FUBotPackageRegistry set helpers.
    bool IsInstalled(const FUBotPackageInfo& Package)
    {
        return Package.bInstalled || Package.State != EUBotPackageState::NotInstalled;
    }

    bool IsEnabled(const FUBotPackageInfo& Package)
    {
        return Package.bEnabled || Package.State == EUBotPackageState::Enabled;
    }

    FString JoinNames(const TArray<FString>& Names)
    {
        return FString::Join(Names, TEXT(", "));
    }

    bool HasUnsafeArgumentCharacters(const FString& Value)
    {
        for (const TCHAR Character : Value)
        {
            if (Character == TEXT('"') || Character < 32 || Character == 127)
            {
                return true;
            }
        }
        return false;
    }

    bool IsGitWorkingCopy(const FString& Directory)
    {
        // ".git" is a file for linked worktrees and submodules.
        const FString GitPath = FPaths::Combine(Directory, TEXT(".git"));
        return IFileManager::Get().DirectoryExists(*GitPath) || IFileManager::Get().FileExists(*GitPath);
    }

    bool PathExists(const FString& Path)
    {
        return IFileManager::Get().DirectoryExists(*Path) || IFileManager::Get().FileExists(*Path);
    }

    // Absolute local repository paths: a drive path ("C:\x", "C:/x"), a UNC path ("\\server\share") or "/x".
    bool IsAbsoluteLocalPath(const FString& Text)
    {
        if (Text.Len() >= 3 && Text[1] == TEXT(':') && (Text[2] == TEXT('\\') || Text[2] == TEXT('/'))
            && ((Text[0] >= TEXT('a') && Text[0] <= TEXT('z')) || (Text[0] >= TEXT('A') && Text[0] <= TEXT('Z'))))
        {
            return true;
        }
        return Text.StartsWith(TEXT("\\\\")) || Text.StartsWith(TEXT("/"));
    }

    // "user@host:path" with a non-empty user, host and path and no slash before the colon.
    bool IsScpLikeAddress(const FString& Text)
    {
        int32 ColonIndex = INDEX_NONE;
        if (!Text.FindChar(TEXT(':'), ColonIndex))
        {
            return false;
        }

        const FString Authority = Text.Left(ColonIndex);
        int32 AtIndex = INDEX_NONE;
        if (!Authority.FindLastChar(TEXT('@'), AtIndex))
        {
            return false;
        }
        return AtIndex > 0 && AtIndex + 1 < Authority.Len() && ColonIndex + 1 < Text.Len()
            && !Authority.Contains(TEXT("/")) && !Authority.Contains(TEXT("\\"));
    }

    bool ResolveGitExecutable(FString& OutExecutable, TArray<FString>& OutMessages)
    {
        const UUBotCoreSettings* Settings = GetDefault<UUBotCoreSettings>();
        FString Executable = Settings ? Settings->GitExecutable.TrimStartAndEnd() : FString();
        if (Executable.IsEmpty())
        {
            Executable = TEXT("git");
        }
        if (HasUnsafeArgumentCharacters(Executable))
        {
            OutMessages.Add(TEXT("The Git Executable setting must not contain quotes or control characters."));
            return false;
        }

        // CreateProcess runs batch files through cmd.exe, which would re-parse the arguments.
        const FString BaseName = FPaths::GetBaseFilename(Executable).ToLower();
        const FString Extension = FPaths::GetExtension(Executable).ToLower();
        static const TCHAR* const ShellNames[] = { TEXT("cmd"), TEXT("powershell"), TEXT("pwsh"), TEXT("sh"), TEXT("bash") };
        bool bIsShell = Extension == TEXT("bat") || Extension == TEXT("cmd");
        for (const TCHAR* ShellName : ShellNames)
        {
            bIsShell |= BaseName.Equals(ShellName, ESearchCase::CaseSensitive);
        }
        if (bIsShell)
        {
            OutMessages.Add(FString::Printf(
                TEXT("The Git Executable setting ('%s') must name git itself, not a shell or a batch script."), *Executable));
            return false;
        }

        OutExecutable = Executable;
        return true;
    }

    void AppendExternalRequirementMessages(const TArray<FUBotPackageInfo>& Packages, const TArray<FString>& PackageNames,
        TArray<FString>& OutMessages)
    {
        for (const FString& PackageName : PackageNames)
        {
            const FUBotPackageInfo* Package = FindByName(Packages, PackageName);
            if (!Package)
            {
                continue;
            }
            for (const FString& External : Package->ExternalRequires)
            {
                const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(External);
                if (!Plugin.IsValid())
                {
                    OutMessages.Add(FString::Printf(
                        TEXT("Note: %s also needs the external plugin %s, which is not installed. uBot never installs external plugins; add it to the project manually."),
                        *Package->Name, *External));
                }
                else if (!Plugin->IsEnabled())
                {
                    OutMessages.Add(FString::Printf(
                        TEXT("Note: %s also needs the external plugin %s, which is installed but not enabled in this session."),
                        *Package->Name, *External));
                }
            }
        }
    }

    void AppendGitOutput(const TArray<FString>& OutputLines, TArray<FString>& OutMessages)
    {
        const int32 FirstLine = FMath::Max(0, OutputLines.Num() - MaxReportedGitLines);
        for (int32 Index = FirstLine; Index < OutputLines.Num(); ++Index)
        {
            const FString Line = OutputLines[Index].TrimStartAndEnd();
            if (!Line.IsEmpty())
            {
                OutMessages.Add(FString::Printf(TEXT("  git: %s"), *Line));
            }
        }
    }

    // Child processes inherit the environment at creation time, so a variable only has to be set
    // while the process is created.
    class FScopedEnvironmentVariable
    {
    public:
        FScopedEnvironmentVariable(const TCHAR* InName, const TCHAR* Value)
            : Name(InName)
            , PreviousValue(FPlatformMisc::GetEnvironmentVariable(InName))
        {
            FPlatformMisc::SetEnvironmentVar(Name, Value);
        }

        ~FScopedEnvironmentVariable()
        {
            FPlatformMisc::SetEnvironmentVar(Name, PreviousValue.IsEmpty() ? nullptr : *PreviousValue);
        }

    private:
        const TCHAR* Name;
        FString PreviousValue;
    };

    // Makes git fail instead of waiting for credentials on a console nobody can see. Headless runs
    // also suppress the Git Credential Manager dialogs.
    class FScopedGitEnvironment
    {
    public:
        FScopedGitEnvironment()
            : TerminalPrompt(TEXT("GIT_TERMINAL_PROMPT"), TEXT("0"))
        {
            if (IsRunningCommandlet() || FApp::IsUnattended())
            {
                CredentialManagerPrompt.Emplace(TEXT("GCM_INTERACTIVE"), TEXT("never"));
            }
        }

    private:
        FScopedEnvironmentVariable TerminalPrompt;
        TOptional<FScopedEnvironmentVariable> CredentialManagerPrompt;
    };
}

struct FUBotPackageService::FCloneTask
{
    FString PackageName;
    FString Version;
    FString Ref;
    FString Repository;
    FString TargetDirectory;
};

struct FUBotPackageService::FGitOperation
{
    enum class EKind : uint8
    {
        Install,
        Update
    };

    // An install clones each package and, for a commit id, checks it out in a second git step.
    enum class EInstallStep : uint8
    {
        Clone,
        Checkout
    };

    EKind Kind = EKind::Install;
    EInstallStep InstallStep = EInstallStep::Clone;
    FString PackageName;
    FString StepDescription;
    FString PreviousVersion;
    TArray<FCloneTask> PendingClones;
    FCloneTask CurrentClone;
    TArray<FString> InstalledPackages;
    TArray<FString> Messages;
    FOnPackageOperationComplete OnComplete;
    bool bCancelRequested = false;

    // Written by the process monitor thread.
    FCriticalSection OutputLock;
    TArray<FString> OutputLines;

    // Declared last and reset first: destroying it joins the monitor thread, whose delegates
    // still use the members above.
    TUniquePtr<FMonitoredProcess> Process;

    ~FGitOperation()
    {
        Process.Reset();
    }
};

FUBotPackageService& FUBotPackageService::Get()
{
    static FUBotPackageService Instance;
    return Instance;
}

FUBotPackageService::FUBotPackageService() = default;

FUBotPackageService::~FUBotPackageService()
{
    Shutdown();
}

TArray<FUBotPackageInfo> FUBotPackageService::BuildPackageView() const
{
    TArray<FString> IndexErrors;
    return BuildPackageView(IndexErrors);
}

TArray<FUBotPackageInfo> FUBotPackageService::BuildPackageView(TArray<FString>& OutIndexErrors) const
{
    const TArray<FUBotPackageInfo> Index = FUBotPackageIndex::LoadConfiguredIndex(&OutIndexErrors);
    TArray<FUBotPackageInfo> View = FUBotPackageIndex::MergeInstalledWithIndex(
        FUBotPackageRegistry::Get().GetInstalledPackages(), Index);
    FUBotPackageRegistry::ValidatePackageSet(View);
    return View;
}

TArray<FUBotPackageInfo> FUBotPackageService::BuildEffectiveView() const
{
    using namespace UBotPackageServicePrivate;

    // IPlugin::IsEnabled() only changes after a restart, so overlay what this session wrote to
    // the project file before planning further changes.
    TArray<FUBotPackageInfo> View = BuildPackageView();
    if (PendingEnabledStates.Num() > 0)
    {
        for (FUBotPackageInfo& Package : View)
        {
            const bool* bPendingEnabled = IsInstalled(Package) ? PendingEnabledStates.Find(Package.Name) : nullptr;
            if (bPendingEnabled)
            {
                Package.bEnabled = *bPendingEnabled;
                Package.State = *bPendingEnabled ? EUBotPackageState::Enabled : EUBotPackageState::Disabled;
            }
        }
        FUBotPackageRegistry::ValidatePackageSet(View);
    }
    return View;
}

bool FUBotPackageService::EnablePackage(const FString& Name, TArray<FString>& OutMessages)
{
    check(IsInGameThread());
    if (IsBusy())
    {
        OutMessages.Add(FString::Printf(TEXT("Cannot enable %s while another package operation is running."), *Name));
        return false;
    }
    return EnablePackageInternal(Name, OutMessages);
}

bool FUBotPackageService::EnablePackageInternal(const FString& Name, TArray<FString>& OutMessages)
{
    using namespace UBotPackageServicePrivate;

    const TArray<FUBotPackageInfo> View = BuildEffectiveView();
    TArray<FString> ToEnable;
    if (!PlanEnable(View, Name, ToEnable, OutMessages))
    {
        return false;
    }

    const FString CanonicalName = FindByName(View, Name)->Name;
    TArray<FString> Order;
    FUBotPackageRegistry::ResolveDependencyOrder(View, CanonicalName, Order, nullptr);
    AppendExternalRequirementMessages(View, Order, OutMessages);

    if (ToEnable.Num() == 0)
    {
        OutMessages.Add(FString::Printf(TEXT("%s and its required packages are already enabled."), *CanonicalName));
        return true;
    }

    if (!ApplyEnabledStates(ToEnable, true, OutMessages))
    {
        return false;
    }

    // Problems that remain once the change is applied, for example version mismatches.
    const TArray<FUBotPackageInfo> After = BuildEffectiveView();
    for (const FString& OrderedName : Order)
    {
        if (const FUBotPackageInfo* Package = FindByName(After, OrderedName))
        {
            for (const FString& Problem : Package->Problems)
            {
                OutMessages.Add(FString::Printf(TEXT("Warning: %s: %s"), *Package->Name, *Problem));
            }
        }
    }

    OutMessages.Add(RestartMessage);
    return true;
}

bool FUBotPackageService::DisablePackage(const FString& Name, bool bForce, TArray<FString>& OutMessages)
{
    using namespace UBotPackageServicePrivate;

    check(IsInGameThread());
    if (IsBusy())
    {
        OutMessages.Add(FString::Printf(TEXT("Cannot disable %s while another package operation is running."), *Name));
        return false;
    }

    const TArray<FUBotPackageInfo> View = BuildEffectiveView();
    TArray<FString> ToDisable;
    if (!PlanDisable(View, Name, bForce, ToDisable, OutMessages))
    {
        return false;
    }

    const FString CanonicalName = FindByName(View, Name)->Name;
    if (ToDisable.Num() == 0)
    {
        OutMessages.Add(FString::Printf(TEXT("%s is already disabled."), *CanonicalName));
        return true;
    }

    if (ToDisable.Num() > 1)
    {
        TArray<FString> Dependents = ToDisable;
        Dependents.Remove(CanonicalName);
        OutMessages.Add(FString::Printf(TEXT("Also disabling packages that depend on %s: %s."), *CanonicalName, *JoinNames(Dependents)));
    }
    if (CanonicalName.Equals(TEXT("UBotCore"), ESearchCase::IgnoreCase))
    {
        OutMessages.Add(TEXT("Warning: disabling UBotCore also removes the uBot package manager after the restart."));
    }

    if (!ApplyEnabledStates(ToDisable, false, OutMessages))
    {
        return false;
    }

    OutMessages.Add(RestartMessage);
    return true;
}

bool FUBotPackageService::ApplyEnabledStates(const TArray<FString>& PackageNames, bool bEnabled, TArray<FString>& OutMessages)
{
    IProjectManager& ProjectManager = IProjectManager::Get();
    if (!ProjectManager.GetCurrentProject())
    {
        OutMessages.Add(TEXT("No project is loaded; plugin states are stored in the .uproject file."));
        return false;
    }

    const TCHAR* const Verb = bEnabled ? TEXT("enable") : TEXT("disable");
    TArray<FString> Changed;

    auto RollBack = [&ProjectManager, &Changed, bEnabled]()
    {
        for (const FString& ChangedName : Changed)
        {
            FText IgnoredReason;
            ProjectManager.SetPluginEnabled(ChangedName, !bEnabled, IgnoredReason);
        }
    };

    for (const FString& PackageName : PackageNames)
    {
        FText FailReason;
        if (!ProjectManager.SetPluginEnabled(PackageName, bEnabled, FailReason))
        {
            OutMessages.Add(FString::Printf(TEXT("Failed to %s %s: %s"), Verb, *PackageName, *FailReason.ToString()));
            RollBack();
            return false;
        }
        Changed.Add(PackageName);
    }

    if (ProjectManager.IsCurrentProjectDirty())
    {
        FText FailReason;
        if (!ProjectManager.SaveCurrentProjectToDisk(FailReason))
        {
            OutMessages.Add(FString::Printf(TEXT("Failed to save the project file: %s"), *FailReason.ToString()));
            RollBack();
            return false;
        }
    }

    for (const FString& PackageName : Changed)
    {
        PendingEnabledStates.Add(PackageName, bEnabled);
        OutMessages.Add(FString::Printf(TEXT("%s %s in the project file."), bEnabled ? TEXT("Enabled") : TEXT("Disabled"), *PackageName));
    }
    bRestartRequired = true;
    return true;
}

void FUBotPackageService::InstallPackageAsync(const FString& Name, FOnPackageOperationComplete OnComplete)
{
    InstallPackageAsync(Name, FString(), MoveTemp(OnComplete));
}

void FUBotPackageService::InstallPackageAsync(const FString& Name, const FString& Version, FOnPackageOperationComplete OnComplete)
{
    using namespace UBotPackageServicePrivate;

    check(IsInGameThread());
    TArray<FString> Messages;
    if (IsBusy())
    {
        Messages.Add(FString::Printf(TEXT("Cannot install %s while another package operation is running."), *Name));
        DispatchCompletion(MoveTemp(OnComplete), false, MoveTemp(Messages));
        return;
    }

    const TArray<FUBotPackageInfo> View = BuildEffectiveView();
    TArray<FString> ToInstall;
    TArray<FCloneTask> Tasks;
    const FString RequestedVersion = Version.TrimStartAndEnd();
    if (!PlanInstall(View, Name, ToInstall, Messages, RequestedVersion)
        || !PrepareCloneTasks(View, ToInstall, Name, RequestedVersion, Tasks, Messages))
    {
        DispatchCompletion(MoveTemp(OnComplete), false, MoveTemp(Messages));
        return;
    }

    const FString CanonicalName = FindByName(View, Name)->Name;
    TArray<FString> Order;
    FUBotPackageRegistry::ResolveDependencyOrder(View, CanonicalName, Order, nullptr);
    AppendExternalRequirementMessages(View, Order, Messages);

    if (Tasks.Num() == 0)
    {
        Messages.Add(FString::Printf(TEXT("%s and its required packages are already installed."), *CanonicalName));
    }
    else
    {
        TArray<FString> Installing;
        for (const FCloneTask& Task : Tasks)
        {
            Installing.Add(FString::Printf(TEXT("%s %s"), *Task.PackageName, *Task.Version));
        }
        Messages.Add(FString::Printf(TEXT("Installing into %s: %s."), *GetInstallDirectory(), *JoinNames(Installing)));
    }

    const TSharedRef<FGitOperation> Operation = MakeShared<FGitOperation>();
    Operation->Kind = FGitOperation::EKind::Install;
    Operation->PackageName = CanonicalName;
    Operation->PendingClones = MoveTemp(Tasks);
    Operation->Messages = MoveTemp(Messages);
    Operation->OnComplete = MoveTemp(OnComplete);
    ActiveOperation = Operation;

    RunNextInstallStep(Operation);
}

bool FUBotPackageService::PrepareCloneTasks(const TArray<FUBotPackageInfo>& Packages, const TArray<FString>& PackageNames,
    const FString& RootName, const FString& RequestedVersion, TArray<FCloneTask>& OutTasks, TArray<FString>& OutMessages) const
{
    using namespace UBotPackageServicePrivate;

    OutTasks.Reset();
    if (PackageNames.Num() == 0)
    {
        return true;
    }

    const FString InstallDirectory = GetInstallDirectory();
    if (!FPaths::IsUnderDirectory(InstallDirectory, FPaths::ConvertRelativePathToFull(FPaths::ProjectPluginsDir())))
    {
        OutMessages.Add(FString::Printf(
            TEXT("Warning: the install directory %s is outside the project's Plugins folder; the engine will not discover packages there after a restart."),
            *InstallDirectory));
    }

    bool bSuccess = true;
    for (const FString& PackageName : PackageNames)
    {
        const FUBotPackageInfo* Package = FindByName(Packages, PackageName);
        if (!Package)
        {
            OutMessages.Add(FString::Printf(TEXT("Cannot install %s: it is not in the package view."), *PackageName));
            bSuccess = false;
            continue;
        }

        // Only the requested package gets the requested version; what it requires gets the newest one.
        const bool bRoot = Package->Name.Equals(RootName, ESearchCase::IgnoreCase);
        FString Reason;
        const FUBotPackageIndexVersion* Version = FindInstallVersion(*Package, bRoot ? RequestedVersion : FString(), Reason);
        if (!Version)
        {
            OutMessages.Add(FString::Printf(TEXT("Cannot install %s: %s."), *Package->Name, *Reason));
            bSuccess = false;
            continue;
        }
        const FString Folder = ResolveInstallFolder(*Package, &Reason);
        if (Folder.IsEmpty())
        {
            OutMessages.Add(FString::Printf(TEXT("Cannot install %s: %s."), *Package->Name, *Reason));
            bSuccess = false;
            continue;
        }

        FCloneTask Task;
        Task.PackageName = Package->Name;
        Task.Version = Version->Version;
        Task.Ref = Version->Ref;
        Task.Repository = Package->Repository.TrimStartAndEnd();
        Task.TargetDirectory = FPaths::Combine(InstallDirectory, Folder);

        if (PathExists(Task.TargetDirectory))
        {
            OutMessages.Add(FString::Printf(
                TEXT("Cannot install %s: %s already exists. Refusing to clone into an existing folder; move or remove it first."),
                *Task.PackageName, *Task.TargetDirectory));
            bSuccess = false;
            continue;
        }

        const bool bDuplicateTarget = OutTasks.ContainsByPredicate([&Task](const FCloneTask& Other)
        {
            return FPaths::IsSamePath(Other.TargetDirectory, Task.TargetDirectory);
        });
        if (bDuplicateTarget)
        {
            OutMessages.Add(FString::Printf(TEXT("Cannot install %s: another package in this install also clones into %s."),
                *Task.PackageName, *Task.TargetDirectory));
            bSuccess = false;
            continue;
        }

        OutTasks.Add(MoveTemp(Task));
    }

    if (!bSuccess)
    {
        OutTasks.Reset();
    }
    return bSuccess;
}

void FUBotPackageService::RunNextInstallStep(const TSharedRef<FGitOperation>& Operation)
{
    using namespace UBotPackageServicePrivate;

    if (Operation->bCancelRequested)
    {
        Operation->Messages.Add(TEXT("Installation cancelled."));
        FinishOperation(Operation, false);
        return;
    }

    if (Operation->PendingClones.Num() == 0)
    {
        if (Operation->InstalledPackages.Num() > 0)
        {
            FUBotPackageRegistry& Registry = FUBotPackageRegistry::Get();
            Registry.Refresh();
            for (const FString& InstalledName : Operation->InstalledPackages)
            {
                if (!Registry.FindPackage(InstalledName))
                {
                    Operation->Messages.Add(FString::Printf(
                        TEXT("Warning: %s was cloned but its descriptor has no \"UBot\" block, so it is not managed as a uBot package."),
                        *InstalledName));
                }
            }
        }

        Operation->StepDescription = FString::Printf(TEXT("Enabling %s"), *Operation->PackageName);
        const bool bEnabled = EnablePackageInternal(Operation->PackageName, Operation->Messages);
        if (bEnabled && Operation->InstalledPackages.Num() > 0)
        {
            Operation->Messages.Add(TEXT("New packages contain C++ source: on restart the editor offers to build the missing modules (requires a C++ project and a compiler)."));
        }
        FinishOperation(Operation, bEnabled);
        return;
    }

    Operation->CurrentClone = Operation->PendingClones[0];
    Operation->PendingClones.RemoveAt(0);
    const FCloneTask& Task = Operation->CurrentClone;

    // Checked again right before cloning: the folder may have appeared since planning.
    if (PathExists(Task.TargetDirectory))
    {
        Operation->Messages.Add(FString::Printf(TEXT("Cannot install %s: %s already exists. Refusing to clone into an existing folder."),
            *Task.PackageName, *Task.TargetDirectory));
        FinishOperation(Operation, false);
        return;
    }

    const FString ParentDirectory = FPaths::GetPath(Task.TargetDirectory);
    IFileManager::Get().MakeDirectory(*ParentDirectory, true);
    if (!IFileManager::Get().DirectoryExists(*ParentDirectory))
    {
        Operation->Messages.Add(FString::Printf(TEXT("Cannot create the install directory %s."), *ParentDirectory));
        FinishOperation(Operation, false);
        return;
    }

    if (HasUnsafeArgumentCharacters(Task.TargetDirectory))
    {
        Operation->Messages.Add(FString::Printf(TEXT("Cannot install %s: the target path contains quotes or control characters."), *Task.PackageName));
        FinishOperation(Operation, false);
        return;
    }

    const FString Arguments = MakeCloneArguments(Task.Repository, Task.Ref, Task.TargetDirectory);

    Operation->InstallStep = FGitOperation::EInstallStep::Clone;
    Operation->StepDescription = FString::Printf(TEXT("Cloning %s %s"), *Task.PackageName, *Task.Version);
    Operation->Messages.Add(FString::Printf(TEXT("Cloning %s %s (%s) from %s into %s ..."),
        *Task.PackageName, *Task.Version, *Task.Ref, *RedactUrlCredentials(Task.Repository), *Task.TargetDirectory));

    if (!LaunchGit(Operation, Arguments, ParentDirectory))
    {
        FinishOperation(Operation, false);
    }
}

void FUBotPackageService::UpdatePackageAsync(const FString& Name, FOnPackageOperationComplete OnComplete)
{
    using namespace UBotPackageServicePrivate;

    check(IsInGameThread());
    TArray<FString> Messages;
    if (IsBusy())
    {
        Messages.Add(FString::Printf(TEXT("Cannot update %s while another package operation is running."), *Name));
        DispatchCompletion(MoveTemp(OnComplete), false, MoveTemp(Messages));
        return;
    }

    const FUBotPackageInfo* Package = FindByName(FUBotPackageRegistry::Get().GetInstalledPackages(), Name);
    if (!Package || Package->BaseDir.IsEmpty())
    {
        Messages.Add(FString::Printf(TEXT("Cannot update %s: it is not an installed uBot package."), *Name));
        DispatchCompletion(MoveTemp(OnComplete), false, MoveTemp(Messages));
        return;
    }

    FString BaseDir = FPaths::ConvertRelativePathToFull(Package->BaseDir);
    FPaths::NormalizeDirectoryName(BaseDir);
    if (!IsGitWorkingCopy(BaseDir))
    {
        Messages.Add(FString::Printf(TEXT("Cannot update %s: %s has no .git, so it is not a git working copy."), *Package->Name, *BaseDir));
        DispatchCompletion(MoveTemp(OnComplete), false, MoveTemp(Messages));
        return;
    }
    if (HasUnsafeArgumentCharacters(BaseDir))
    {
        Messages.Add(FString::Printf(TEXT("Cannot update %s: its path contains quotes or control characters."), *Package->Name));
        DispatchCompletion(MoveTemp(OnComplete), false, MoveTemp(Messages));
        return;
    }

    const TSharedRef<FGitOperation> Operation = MakeShared<FGitOperation>();
    Operation->Kind = FGitOperation::EKind::Update;
    Operation->PackageName = Package->Name;
    Operation->PreviousVersion = Package->Version;
    Operation->StepDescription = FString::Printf(TEXT("Updating %s"), *Package->Name);
    Operation->Messages = MoveTemp(Messages);
    Operation->Messages.Add(FString::Printf(TEXT("Updating %s in %s ..."), *Package->Name, *BaseDir));
    Operation->OnComplete = MoveTemp(OnComplete);
    ActiveOperation = Operation;

    const FString Arguments = FString::Printf(TEXT("-C %s pull --ff-only"), *QuoteArgument(BaseDir));
    if (!LaunchGit(Operation, Arguments, BaseDir))
    {
        FinishOperation(Operation, false);
    }
}

bool FUBotPackageService::LaunchGit(const TSharedRef<FGitOperation>& Operation, const FString& Arguments, const FString& WorkingDirectory)
{
    using namespace UBotPackageServicePrivate;

    FString GitExecutable;
    if (!ResolveGitExecutable(GitExecutable, Operation->Messages))
    {
        return false;
    }

    {
        FScopeLock Lock(&Operation->OutputLock);
        Operation->OutputLines.Reset();
    }

    // Never goes through a shell: CreateProc starts the executable with this argument string.
    TUniquePtr<FMonitoredProcess> Process = MakeUnique<FMonitoredProcess>(
        GitExecutable, Arguments, WorkingDirectory, /*bHidden*/ true, /*bCreatePipes*/ true);

    // These delegates run on the monitor thread. The raw pointer is safe because the operation
    // joins that thread before its other members are destroyed.
    FGitOperation* RawOperation = &Operation.Get();
    Process->OnOutput().BindLambda([RawOperation](FString Line)
    {
        // Git echoes remote URLs, which may carry credentials; nothing unredacted is kept.
        Line = RedactUrlCredentials(Line);
        UE_LOG(LogUBot, Verbose, TEXT("git: %s"), *Line);
        FScopeLock Lock(&RawOperation->OutputLock);
        RawOperation->OutputLines.Add(MoveTemp(Line));
    });

    const TWeakPtr<FGitOperation> WeakOperation = Operation;
    Process->OnCompleted().BindLambda([WeakOperation](int32 ReturnCode)
    {
        AsyncTask(ENamedThreads::GameThread, [WeakOperation, ReturnCode]()
        {
            if (const TSharedPtr<FGitOperation> PinnedOperation = WeakOperation.Pin())
            {
                FUBotPackageService::Get().HandleGitFinished(PinnedOperation.ToSharedRef(), ReturnCode, false);
            }
        });
    });
    Process->OnCanceled().BindLambda([WeakOperation]()
    {
        AsyncTask(ENamedThreads::GameThread, [WeakOperation]()
        {
            if (const TSharedPtr<FGitOperation> PinnedOperation = WeakOperation.Pin())
            {
                FUBotPackageService::Get().HandleGitFinished(PinnedOperation.ToSharedRef(), -1, true);
            }
        });
    });

    // The clone URL is part of the arguments and may carry credentials.
    UE_LOG(LogUBot, Log, TEXT("uBot packages: running %s %s (in %s)"), *GitExecutable, *RedactUrlCredentials(Arguments), *WorkingDirectory);

    bool bLaunched = false;
    {
        const FScopedGitEnvironment GitEnvironment;
        bLaunched = Process->Launch();
    }
    if (!bLaunched)
    {
        Operation->Messages.Add(FString::Printf(
            TEXT("Failed to start git ('%s'). Install git or set Git Executable in Project Settings > Plugins > uBot Core."),
            *GitExecutable));
        return false;
    }

    Operation->Process = MoveTemp(Process);
    return true;
}

void FUBotPackageService::HandleGitFinished(const TSharedRef<FGitOperation>& Operation, int32 ReturnCode, bool bCanceled)
{
    using namespace UBotPackageServicePrivate;

    if (ActiveOperation.Get() != &Operation.Get())
    {
        return;
    }

    TArray<FString> OutputLines;
    {
        FScopeLock Lock(&Operation->OutputLock);
        OutputLines = MoveTemp(Operation->OutputLines);
        Operation->OutputLines.Reset();
    }
    if (Operation->Process.IsValid())
    {
        // Text after the last line break stays in the process buffer.
        const FString Remainder = Operation->Process->GetFullOutputWithoutDelegate().TrimStartAndEnd();
        if (!Remainder.IsEmpty())
        {
            OutputLines.Add(RedactUrlCredentials(Remainder));
        }
        // The monitor thread is past its last callback; this only joins it.
        Operation->Process.Reset();
    }

    AppendGitOutput(OutputLines, Operation->Messages);

    if (bCanceled)
    {
        Operation->Messages.Add(FString::Printf(TEXT("git was cancelled while processing %s."), *Operation->PackageName));
        if (Operation->Kind == FGitOperation::EKind::Install && !Operation->CurrentClone.TargetDirectory.IsEmpty())
        {
            // The target folder did not exist before this clone started (checked right before launch),
            // so whatever is there now was written by the killed git.
            FString CleanupMessage;
            RemoveIncompleteClone(Operation->CurrentClone.TargetDirectory, CleanupMessage);
            if (!CleanupMessage.IsEmpty())
            {
                Operation->Messages.Add(MoveTemp(CleanupMessage));
            }
        }
        FinishOperation(Operation, false);
        return;
    }

    if (Operation->Kind == FGitOperation::EKind::Install)
    {
        HandleCloneFinished(Operation, ReturnCode);
    }
    else
    {
        HandlePullFinished(Operation, ReturnCode, OutputLines);
    }
}

void FUBotPackageService::HandleCloneFinished(const TSharedRef<FGitOperation>& Operation, int32 ReturnCode)
{
    using namespace UBotPackageServicePrivate;

    const FCloneTask Task = Operation->CurrentClone;
    const bool bCheckoutFinished = Operation->InstallStep == FGitOperation::EInstallStep::Checkout;
    if (ReturnCode != 0)
    {
        if (bCheckoutFinished)
        {
            Operation->Messages.Add(FString::Printf(TEXT("git checkout of %s in %s failed with exit code %d."),
                *Task.Ref, *Task.TargetDirectory, ReturnCode));

            // The clone worked, so the folder exists but holds the wrong revision, and it did not exist
            // before this install created it. Left behind, it would only block the next install.
            FString CleanupMessage;
            RemoveIncompleteClone(Task.TargetDirectory, CleanupMessage);
            if (!CleanupMessage.IsEmpty())
            {
                Operation->Messages.Add(MoveTemp(CleanupMessage));
            }
        }
        else
        {
            Operation->Messages.Add(FString::Printf(TEXT("git clone of %s failed with exit code %d."), *Task.PackageName, ReturnCode));
        }
        if (Operation->InstalledPackages.Num() > 0)
        {
            Operation->Messages.Add(FString::Printf(TEXT("Packages installed before the failure stay installed: %s."),
                *JoinNames(Operation->InstalledPackages)));
        }
        FinishOperation(Operation, false);
        return;
    }

    if (!bCheckoutFinished && IsCommitId(Task.Ref))
    {
        // "git clone --branch" only takes branches and tags, so a commit id is checked out afterwards.
        Operation->InstallStep = FGitOperation::EInstallStep::Checkout;
        Operation->StepDescription = FString::Printf(TEXT("Checking out %s %s"), *Task.PackageName, *Task.Version);
        Operation->Messages.Add(FString::Printf(TEXT("Checking out %s in %s ..."), *Task.Ref, *Task.TargetDirectory));
        if (!LaunchGit(Operation, MakeCheckoutArguments(Task.TargetDirectory, Task.Ref), Task.TargetDirectory))
        {
            FString CleanupMessage;
            RemoveIncompleteClone(Task.TargetDirectory, CleanupMessage);
            if (!CleanupMessage.IsEmpty())
            {
                Operation->Messages.Add(MoveTemp(CleanupMessage));
            }
            FinishOperation(Operation, false);
        }
        return;
    }

    // The plugin manager names a plugin after its descriptor file.
    const FString DescriptorPath = FPaths::Combine(Task.TargetDirectory, Task.PackageName + TEXT(".uplugin"));
    if (!IFileManager::Get().FileExists(*DescriptorPath))
    {
        Operation->Messages.Add(FString::Printf(
            TEXT("The repository of %s was cloned into %s but has no %s.uplugin at its root; it was not registered."),
            *Task.PackageName, *Task.TargetDirectory, *Task.PackageName));
        bRestartRequired = true;
        FinishOperation(Operation, false);
        return;
    }

    FText FailReason;
    if (!IPluginManager::Get().AddToPluginsList(DescriptorPath, &FailReason))
    {
        Operation->Messages.Add(FString::Printf(
            TEXT("%s was cloned but could not be registered with the plugin manager: %s"),
            *Task.PackageName, *FailReason.ToString()));
        bRestartRequired = true;
        FinishOperation(Operation, false);
        return;
    }

    bRestartRequired = true;
    Operation->InstalledPackages.Add(Task.PackageName);
    Operation->Messages.Add(FString::Printf(TEXT("Installed %s into %s."), *Task.PackageName, *Task.TargetDirectory));
    RunNextInstallStep(Operation);
}

void FUBotPackageService::HandlePullFinished(const TSharedRef<FGitOperation>& Operation, int32 ReturnCode, const TArray<FString>& OutputLines)
{
    if (ReturnCode != 0)
    {
        Operation->Messages.Add(FString::Printf(
            TEXT("git pull --ff-only failed for %s with exit code %d. Resolve local changes or diverged history manually."),
            *Operation->PackageName, ReturnCode));
        FinishOperation(Operation, false);
        return;
    }

    FUBotPackageRegistry& Registry = FUBotPackageRegistry::Get();
    Registry.Refresh();

    // Older git prints "up-to-date". Unrecognised (for example localised) output counts as a change.
    const bool bUpToDate = OutputLines.ContainsByPredicate([](const FString& Line)
    {
        return Line.Contains(TEXT("Already up to date")) || Line.Contains(TEXT("Already up-to-date"));
    });

    if (bUpToDate)
    {
        Operation->Messages.Add(FString::Printf(TEXT("%s is already up to date."), *Operation->PackageName));
    }
    else
    {
        bRestartRequired = true;
        const FUBotPackageInfo* Updated = Registry.FindPackage(Operation->PackageName);
        const FString NewVersion = Updated ? Updated->Version : FString();
        if (!NewVersion.IsEmpty() && NewVersion != Operation->PreviousVersion)
        {
            Operation->Messages.Add(FString::Printf(TEXT("Updated %s from %s to %s."),
                *Operation->PackageName, *Operation->PreviousVersion, *NewVersion));
        }
        else
        {
            Operation->Messages.Add(FString::Printf(TEXT("Updated %s."), *Operation->PackageName));
        }
        Operation->Messages.Add(TEXT("Rebuild and restart the editor to load the new code."));
    }

    FinishOperation(Operation, true);
}

void FUBotPackageService::FinishOperation(const TSharedRef<FGitOperation>& Operation, bool bSuccess)
{
    if (ActiveOperation.Get() == &Operation.Get())
    {
        ActiveOperation.Reset();
    }
    Operation->StepDescription.Reset();
    DispatchCompletion(MoveTemp(Operation->OnComplete), bSuccess, Operation->Messages);
}

void FUBotPackageService::DispatchCompletion(FOnPackageOperationComplete OnComplete, bool bSuccess, TArray<FString> Messages)
{
    for (const FString& Message : Messages)
    {
        UE_LOG(LogUBot, Verbose, TEXT("uBot packages: %s"), *Message);
    }

    if (!OnComplete)
    {
        return;
    }

    ++PendingCompletionCount;
    AsyncTask(ENamedThreads::GameThread, [OnComplete = MoveTemp(OnComplete), bSuccess, Messages = MoveTemp(Messages)]()
    {
        --FUBotPackageService::Get().PendingCompletionCount;
        OnComplete(bSuccess, Messages);
    });
}

bool FUBotPackageService::IsRestartRequired() const
{
    return bRestartRequired;
}

bool FUBotPackageService::IsBusy() const
{
    return ActiveOperation.IsValid();
}

FString FUBotPackageService::GetBusyDescription() const
{
    return ActiveOperation.IsValid() ? ActiveOperation->StepDescription : FString();
}

void FUBotPackageService::CancelActiveOperation()
{
    if (ActiveOperation.IsValid())
    {
        ActiveOperation->bCancelRequested = true;
        if (ActiveOperation->Process.IsValid())
        {
            ActiveOperation->Process->Cancel(/*InKillTree*/ true);
        }
    }
}

bool FUBotPackageService::WaitUntilIdle(double TimeoutSeconds)
{
    using namespace UBotPackageServicePrivate;

    check(IsInGameThread());
    const double StartSeconds = FPlatformTime::Seconds();
    double CancelSeconds = 0.0;
    bool bTimedOut = false;

    while (ActiveOperation.IsValid() || PendingCompletionCount > 0)
    {
        // Without threads the monitored process is only advanced by Update().
        if (!FPlatformProcess::SupportsMultithreading() && ActiveOperation.IsValid() && ActiveOperation->Process.IsValid())
        {
            ActiveOperation->Process->Update();
        }

        FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
        if (!ActiveOperation.IsValid() && PendingCompletionCount == 0)
        {
            break;
        }

        const double NowSeconds = FPlatformTime::Seconds();
        if (!bTimedOut && TimeoutSeconds > 0.0 && NowSeconds - StartSeconds > TimeoutSeconds)
        {
            UE_LOG(LogUBot, Warning, TEXT("uBot packages: operation timed out after %.0f s; cancelling it."), TimeoutSeconds);
            bTimedOut = true;
            CancelSeconds = NowSeconds;
            CancelActiveOperation();
        }
        else if (bTimedOut && NowSeconds - CancelSeconds > CancelGraceSeconds)
        {
            UE_LOG(LogUBot, Error, TEXT("uBot packages: the cancelled operation did not stop."));
            break;
        }

        FPlatformProcess::Sleep(0.02f);
    }

    return !bTimedOut;
}

bool FUBotPackageService::GetPendingEnabledState(const FString& Name, bool& bOutEnabled) const
{
    if (const bool* bPendingEnabled = PendingEnabledStates.Find(Name))
    {
        bOutEnabled = *bPendingEnabled;
        return true;
    }
    return false;
}

TArray<FString> FUBotPackageService::GetEnabledDependents(const FString& Name) const
{
    using namespace UBotPackageServicePrivate;

    const TArray<FUBotPackageInfo> View = BuildEffectiveView();
    const FUBotPackageInfo* Package = FindByName(View, Name);
    return Package ? FUBotPackageRegistry::FindDependents(View, Package->Name, /*bOnlyEnabled*/ true) : TArray<FString>();
}

FString FUBotPackageService::GetInstallDirectory()
{
    const UUBotCoreSettings* Settings = GetDefault<UUBotCoreSettings>();
    FString Directory = Settings ? Settings->PackageInstallDirectory.TrimStartAndEnd() : FString();
    if (Directory.IsEmpty())
    {
        Directory = TEXT("Plugins/uBot");
    }

    const FString ProjectDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
    FString Result = FPaths::IsRelative(Directory)
        ? FPaths::ConvertRelativePathToFull(ProjectDirectory, Directory)
        : FPaths::ConvertRelativePathToFull(Directory);
    FPaths::NormalizeDirectoryName(Result);
    return Result;
}

void FUBotPackageService::Shutdown()
{
    if (const TSharedPtr<FGitOperation> Operation = MoveTemp(ActiveOperation))
    {
        // A clone that is still running leaves a half-written folder behind once it is killed.
        FString IncompleteClone;
        if (Operation->Kind == FGitOperation::EKind::Install && Operation->Process.IsValid() && Operation->Process->Update())
        {
            IncompleteClone = Operation->CurrentClone.TargetDirectory;
        }

        // Destroying a running process cancels it and joins its monitor thread. Queued callbacks
        // then fail to pin the operation and do nothing.
        Operation->Process.Reset();

        if (!IncompleteClone.IsEmpty())
        {
            FString CleanupMessage;
            RemoveIncompleteClone(IncompleteClone, CleanupMessage);
            if (!CleanupMessage.IsEmpty())
            {
                UE_LOG(LogUBot, Log, TEXT("uBot packages: %s"), *CleanupMessage);
            }
        }
    }
    ActiveOperation.Reset();
}

FString FUBotPackageService::RepositoryFolderName(const FString& RepositoryUrl)
{
    FString Url = RepositoryUrl.TrimStartAndEnd();

    int32 CutIndex = INDEX_NONE;
    if (Url.FindChar(TEXT('?'), CutIndex))
    {
        Url.LeftInline(CutIndex);
    }
    if (Url.FindChar(TEXT('#'), CutIndex))
    {
        Url.LeftInline(CutIndex);
    }
    while (Url.EndsWith(TEXT("/")) || Url.EndsWith(TEXT("\\")))
    {
        Url.LeftChopInline(1);
    }

    // The last segment follows '/', '\' or, for scp-like "host:repo.git", ':'.
    int32 SeparatorIndex = INDEX_NONE;
    for (int32 Index = Url.Len() - 1; Index >= 0; --Index)
    {
        const TCHAR Character = Url[Index];
        if (Character == TEXT('/') || Character == TEXT('\\') || Character == TEXT(':'))
        {
            SeparatorIndex = Index;
            break;
        }
    }

    FString Folder = Url.Mid(SeparatorIndex + 1);
    if (Folder.EndsWith(TEXT(".git"), ESearchCase::IgnoreCase))
    {
        Folder.LeftChopInline(4);
    }

    // Only plain folder names; anything that could escape the install directory is rejected.
    if (Folder.IsEmpty() || Folder == TEXT(".") || Folder == TEXT("..") || Folder.EndsWith(TEXT(".")) || Folder.EndsWith(TEXT(" ")))
    {
        return FString();
    }
    for (const TCHAR Character : Folder)
    {
        if (Character < 32 || FCString::Strchr(TEXT("<>:\"/\\|?*"), Character) != nullptr)
        {
            return FString();
        }
    }
    return Folder;
}

bool FUBotPackageService::IsRepositoryUrlAllowed(const FString& RepositoryUrl, FString* OutReason)
{
    using namespace UBotPackageServicePrivate;

    auto Reject = [OutReason](const FString& Reason)
    {
        if (OutReason)
        {
            *OutReason = Reason;
        }
        return false;
    };

    const FString& Url = RepositoryUrl;
    if (Url.TrimStartAndEnd().IsEmpty())
    {
        return Reject(TEXT("the repository URL is empty"));
    }
    if (Url.StartsWith(TEXT("-")) || Url.Contains(TEXT("://-")))
    {
        return Reject(TEXT("the repository URL must not start with '-'"));
    }
    for (const TCHAR Character : Url)
    {
        if (Character == TEXT('"') || Character < 32 || Character == 127 || FChar::IsWhitespace(Character))
        {
            return Reject(TEXT("the repository URL contains whitespace, quotes or control characters"));
        }
    }

    // "<transport>::<address>" selects a git remote helper (ext::, fd::, ...), which can run commands.
    const int32 HelperIndex = Url.Find(TEXT("::"), ESearchCase::CaseSensitive);
    if (HelperIndex > 0)
    {
        bool bTransportName = true;
        for (int32 Index = 0; Index < HelperIndex; ++Index)
        {
            const TCHAR Character = Url[Index];
            if (!FChar::IsAlnum(Character) && Character != TEXT('+') && Character != TEXT('-') && Character != TEXT('.'))
            {
                bTransportName = false;
                break;
            }
        }
        if (bTransportName)
        {
            return Reject(TEXT("remote helper URLs (<transport>::<address>) are not allowed"));
        }
    }

    // SPEC 5.4: https, http, ssh and file URLs, scp-like "user@host:path" and absolute local paths.
    // Anything else, such as git:// (unauthenticated and unencrypted), "host:path" or a relative
    // path, is read by git as a different transport or as a path relative to the working directory.
    const int32 SchemeIndex = Url.Find(TEXT("://"), ESearchCase::CaseSensitive);
    if (SchemeIndex != INDEX_NONE)
    {
        const FString Scheme = Url.Left(SchemeIndex).ToLower();
        static const TCHAR* const AllowedSchemes[] = { TEXT("https"), TEXT("http"), TEXT("ssh"), TEXT("file") };
        bool bAllowedScheme = false;
        for (const TCHAR* AllowedScheme : AllowedSchemes)
        {
            if (Scheme.Equals(AllowedScheme, ESearchCase::CaseSensitive))
            {
                bAllowedScheme = true;
                break;
            }
        }
        if (!bAllowedScheme)
        {
            return Reject(FString::Printf(TEXT("the URL scheme '%s' is not allowed (use https, http, ssh or file)"), *Scheme));
        }
        if (Url.Len() == SchemeIndex + 3)
        {
            return Reject(TEXT("the repository URL has no address"));
        }
    }
    else if (!IsAbsoluteLocalPath(Url) && !IsScpLikeAddress(Url))
    {
        return Reject(TEXT("use an https://, ssh:// or file:// URL, user@host:path or an absolute path"));
    }

    if (RepositoryFolderName(Url).IsEmpty())
    {
        return Reject(TEXT("no folder name can be derived from the repository URL"));
    }
    return true;
}

FString FUBotPackageService::ResolveInstallFolder(const FUBotPackageInfo& Package, FString* OutReason)
{
    auto Reject = [OutReason](const FString& Reason)
    {
        if (OutReason)
        {
            *OutReason = Reason;
        }
        return FString();
    };

    // The index entry's "folder" wins; otherwise the folder follows the repository name. Either way it
    // names a single folder below the install directory.
    const FString Explicit = Package.InstallFolder.TrimStartAndEnd();
    if (!Explicit.IsEmpty())
    {
        return FUBotPackageIndex::IsSafeFolderName(Explicit)
            ? Explicit
            : Reject(FString::Printf(TEXT("the index \"folder\" '%s' is not one safe folder name (letters, digits, '.', '_' and '-')"), *Explicit));
    }

    const FString Derived = RepositoryFolderName(Package.Repository);
    if (Derived.IsEmpty())
    {
        return Reject(TEXT("no folder name can be derived from the repository URL; set \"folder\" in the index entry"));
    }
    return FUBotPackageIndex::IsSafeFolderName(Derived)
        ? Derived
        : Reject(FString::Printf(TEXT("the folder name '%s' derived from the repository URL is not one safe folder name; set \"folder\" in the index entry"), *Derived));
}

const FUBotPackageIndexVersion* FUBotPackageService::FindInstallVersion(const FUBotPackageInfo& Package,
    const FString& RequestedVersion, FString& OutReason)
{
    OutReason.Reset();

    const FString Requested = RequestedVersion.TrimStartAndEnd();
    FUBotSemVer RequestedSemVer;
    if (!Requested.IsEmpty() && !FUBotSemVer::Parse(Requested, RequestedSemVer))
    {
        OutReason = FString::Printf(TEXT("'%s' is not a semantic version"), *Requested);
        return nullptr;
    }

    const FUBotPackageIndexVersion* Chosen = nullptr;
    FUBotSemVer ChosenSemVer;
    for (const FUBotPackageIndexVersion& Candidate : Package.IndexVersions)
    {
        FUBotSemVer CandidateSemVer;
        if (!FUBotSemVer::Parse(Candidate.Version, CandidateSemVer))
        {
            continue;
        }

        if (!Requested.IsEmpty())
        {
            if (CandidateSemVer == RequestedSemVer)
            {
                Chosen = &Candidate;
                break;
            }
        }
        else if (!Candidate.bPlanned && (Chosen == nullptr || CandidateSemVer > ChosenSemVer))
        {
            Chosen = &Candidate;
            ChosenSemVer = CandidateSemVer;
        }
    }

    if (Chosen == nullptr)
    {
        if (Requested.IsEmpty())
        {
            OutReason = TEXT("its package index entry lists no installable version");
        }
        else
        {
            TArray<FString> Installable;
            for (const FUBotPackageIndexVersion& Candidate : Package.IndexVersions)
            {
                if (!Candidate.bPlanned)
                {
                    Installable.Add(Candidate.Version);
                }
            }
            OutReason = FString::Printf(TEXT("the package index has no version '%s' (installable: %s)"), *Requested,
                Installable.Num() > 0 ? *FString::Join(Installable, TEXT(", ")) : TEXT("none"));
        }
        return nullptr;
    }
    if (Chosen->bPlanned)
    {
        OutReason = FString::Printf(TEXT("version %s is planned and cannot be installed yet"), *Chosen->Version);
        return nullptr;
    }
    if (Chosen->Ref.IsEmpty())
    {
        OutReason = FString::Printf(TEXT("its package index entry lists no \"ref\" for version %s"), *Chosen->Version);
        return nullptr;
    }
    if (!FUBotPackageIndex::IsSafeGitRef(Chosen->Ref))
    {
        OutReason = FString::Printf(TEXT("the \"ref\" '%s' of version %s is not a safe git ref"), *Chosen->Ref, *Chosen->Version);
        return nullptr;
    }
    return Chosen;
}

bool FUBotPackageService::IsCommitId(const FString& Ref)
{
    if (Ref.Len() < 7 || Ref.Len() > 40)
    {
        return false;
    }
    for (const TCHAR Character : Ref)
    {
        const bool bHexDigit = (Character >= TEXT('0') && Character <= TEXT('9'))
            || (Character >= TEXT('a') && Character <= TEXT('f'))
            || (Character >= TEXT('A') && Character <= TEXT('F'));
        if (!bHexDigit)
        {
            return false;
        }
    }
    return true;
}

FString FUBotPackageService::MakeCloneArguments(const FString& Repository, const FString& Ref, const FString& Destination)
{
    // Cloning a tag leaves a detached HEAD; the advice git prints about that is only noise here.
    FString Arguments = TEXT("-c advice.detachedHead=false clone");
    if (!Ref.IsEmpty() && !IsCommitId(Ref))
    {
        Arguments += FString::Printf(TEXT(" --branch %s"), *QuoteArgument(Ref));
    }
    // "--" ends option parsing so the URL can never be read as a git option.
    Arguments += FString::Printf(TEXT(" -- %s %s"), *QuoteArgument(Repository), *QuoteArgument(Destination));
    return Arguments;
}

FString FUBotPackageService::MakeCheckoutArguments(const FString& Destination, const FString& Ref)
{
    // The trailing "--" tells git that Ref is a revision and not a path.
    return FString::Printf(TEXT("-c advice.detachedHead=false -C %s checkout --detach %s --"),
        *QuoteArgument(Destination), *QuoteArgument(Ref));
}

FString FUBotPackageService::QuoteArgument(const FString& Argument)
{
    FString Result;
    Result.Reserve(Argument.Len() + 2);
    Result.AppendChar(TEXT('"'));

#if PLATFORM_WINDOWS
    // CommandLineToArgvW rules: backslashes are literal unless they precede a quote, in which case
    // they must be doubled; a literal quote needs one more backslash.
    int32 PendingBackslashes = 0;
    for (const TCHAR Character : Argument)
    {
        if (Character == TEXT('\\'))
        {
            ++PendingBackslashes;
            continue;
        }
        if (Character == TEXT('"'))
        {
            Result.Append(FString::ChrN(PendingBackslashes * 2 + 1, TEXT('\\')));
        }
        else if (PendingBackslashes > 0)
        {
            Result.Append(FString::ChrN(PendingBackslashes, TEXT('\\')));
        }
        Result.AppendChar(Character);
        PendingBackslashes = 0;
    }
    Result.Append(FString::ChrN(PendingBackslashes * 2, TEXT('\\')));
#else
    // FUnixPlatformProcess::CreateProc only toggles on quotes and has no escape character.
    // Callers reject arguments that contain quotes.
    Result.Append(Argument);
#endif

    Result.AppendChar(TEXT('"'));
    return Result;
}

FString FUBotPackageService::RedactUrlCredentials(const FString& Text)
{
    const TCHAR* const SchemeSeparator = TEXT("://");
    constexpr int32 SchemeSeparatorLength = 3;

    auto EndsAuthority = [](TCHAR Character)
    {
        return Character == TEXT('/') || Character == TEXT('\\') || Character == TEXT('?') || Character == TEXT('#')
            || Character == TEXT('"') || Character == TEXT('\'') || FChar::IsWhitespace(Character);
    };

    FString Result;
    Result.Reserve(Text.Len());
    int32 Cursor = 0;
    while (Cursor < Text.Len())
    {
        const int32 SeparatorIndex = Text.Find(SchemeSeparator, ESearchCase::CaseSensitive, ESearchDir::FromStart, Cursor);
        if (SeparatorIndex == INDEX_NONE)
        {
            break;
        }

        const int32 AuthorityStart = SeparatorIndex + SchemeSeparatorLength;
        int32 AuthorityEnd = AuthorityStart;
        while (AuthorityEnd < Text.Len() && !EndsAuthority(Text[AuthorityEnd]))
        {
            ++AuthorityEnd;
        }

        // The user info ends at the last '@' of the authority; a '@' later in the path is not one.
        int32 AtIndex = INDEX_NONE;
        for (int32 Index = AuthorityEnd - 1; Index >= AuthorityStart; --Index)
        {
            if (Text[Index] == TEXT('@'))
            {
                AtIndex = Index;
                break;
            }
        }

        Result.Append(Text.Mid(Cursor, AuthorityStart - Cursor));
        if (AtIndex == INDEX_NONE)
        {
            Cursor = AuthorityStart;
            continue;
        }

        int32 SchemeStart = SeparatorIndex;
        while (SchemeStart > 0)
        {
            const TCHAR Character = Text[SchemeStart - 1];
            if (!FChar::IsAlnum(Character) && Character != TEXT('+') && Character != TEXT('-') && Character != TEXT('.'))
            {
                break;
            }
            --SchemeStart;
        }
        const FString Scheme = Text.Mid(SchemeStart, SeparatorIndex - SchemeStart);
        const FString UserInfo = Text.Mid(AuthorityStart, AtIndex - AuthorityStart);

        // "https://<token>@host" is the usual way to pass a token, so web URLs lose all user info.
        // "ssh://git@host" only names the account and stays readable.
        const bool bWebScheme = Scheme.Equals(TEXT("https"), ESearchCase::IgnoreCase) || Scheme.Equals(TEXT("http"), ESearchCase::IgnoreCase);
        Result.Append(bWebScheme || UserInfo.Contains(TEXT(":")) ? FString(TEXT("***")) : UserInfo);
        Result.AppendChar(TEXT('@'));
        Cursor = AtIndex + 1;
    }
    Result.Append(Text.Mid(Cursor));
    return Result;
}

bool FUBotPackageService::RemoveIncompleteClone(const FString& CloneDirectory, FString& OutMessage)
{
    OutMessage.Reset();

    FString Directory = FPaths::ConvertRelativePathToFull(CloneDirectory);
    FPaths::NormalizeDirectoryName(Directory);
    const FString InstallDirectory = GetInstallDirectory();
    if (Directory.IsEmpty() || FPaths::IsSamePath(Directory, InstallDirectory) || !FPaths::IsUnderDirectory(Directory, InstallDirectory))
    {
        OutMessage = FString::Printf(TEXT("Refusing to remove %s: it is not a folder inside the install directory %s."),
            *Directory, *InstallDirectory);
        return false;
    }
    if (!IFileManager::Get().DirectoryExists(*Directory))
    {
        return true;
    }

    // DeleteDirectory with Tree clears the read-only flag git sets on its object files. The killed
    // process tree may hold file handles for a moment, hence the short retry.
    constexpr int32 MaxAttempts = 5;
    for (int32 Attempt = 0; Attempt < MaxAttempts; ++Attempt)
    {
        if (Attempt > 0)
        {
            FPlatformProcess::Sleep(0.2f);
        }
        IFileManager::Get().DeleteDirectory(*Directory, /*RequireExists*/ false, /*Tree*/ true);
        if (!IFileManager::Get().DirectoryExists(*Directory))
        {
            OutMessage = FString::Printf(TEXT("Removed the incomplete clone %s."), *Directory);
            return true;
        }
    }

    OutMessage = FString::Printf(TEXT("Could not remove the incomplete clone %s; delete it manually before installing again."), *Directory);
    return false;
}

bool FUBotPackageService::PlanEnable(const TArray<FUBotPackageInfo>& Packages, const FString& Name,
    TArray<FString>& OutToEnable, TArray<FString>& OutMessages)
{
    using namespace UBotPackageServicePrivate;

    OutToEnable.Reset();
    const FUBotPackageInfo* Package = FindByName(Packages, Name);
    if (!Package)
    {
        OutMessages.Add(FString::Printf(TEXT("Unknown uBot package '%s'."), *Name));
        return false;
    }
    if (!IsInstalled(*Package))
    {
        OutMessages.Add(FString::Printf(TEXT("Cannot enable %s: it is not installed. Install it first."), *Package->Name));
        return false;
    }

    TArray<FString> Order;
    FString Error;
    if (!FUBotPackageRegistry::ResolveDependencyOrder(Packages, Package->Name, Order, &Error))
    {
        OutMessages.Add(FString::Printf(TEXT("Cannot enable %s: %s"), *Package->Name, *Error));
        return false;
    }

    TArray<FString> Missing;
    for (const FString& OrderedName : Order)
    {
        const FUBotPackageInfo* Entry = FindByName(Packages, OrderedName);
        if (!Entry || !IsInstalled(*Entry))
        {
            Missing.Add(OrderedName);
        }
        else if (!IsEnabled(*Entry))
        {
            OutToEnable.Add(Entry->Name);
        }
    }

    if (Missing.Num() > 0)
    {
        OutToEnable.Reset();
        OutMessages.Add(FString::Printf(TEXT("Cannot enable %s: required packages are not installed: %s. Install them first."),
            *Package->Name, *JoinNames(Missing)));
        return false;
    }
    return true;
}

bool FUBotPackageService::PlanDisable(const TArray<FUBotPackageInfo>& Packages, const FString& Name, bool bForce,
    TArray<FString>& OutToDisable, TArray<FString>& OutMessages)
{
    using namespace UBotPackageServicePrivate;

    OutToDisable.Reset();
    const FUBotPackageInfo* Package = FindByName(Packages, Name);
    if (!Package || !IsInstalled(*Package))
    {
        OutMessages.Add(FString::Printf(TEXT("Cannot disable %s: it is not an installed uBot package."), *Name));
        return false;
    }

    const TArray<FString> Dependents = FUBotPackageRegistry::FindDependents(Packages, Package->Name, /*bOnlyEnabled*/ true);
    if (Dependents.Num() > 0 && !bForce)
    {
        OutMessages.Add(FString::Printf(TEXT("Cannot disable %s: enabled packages depend on it: %s. Disable them first or force the operation."),
            *Package->Name, *JoinNames(Dependents)));
        return false;
    }

    TArray<FString> Affected = Dependents;
    Affected.AddUnique(Package->Name);

    // Merge the dependency orders of all affected packages (dependencies first), then walk it
    // backwards so that dependents are disabled before what they require. A package whose order
    // cannot be resolved is treated as a leaf dependent and disabled first.
    TArray<FString> Ordered;
    TArray<FString> Unresolved;
    for (const FString& AffectedName : Affected)
    {
        TArray<FString> Order;
        if (FUBotPackageRegistry::ResolveDependencyOrder(Packages, AffectedName, Order, nullptr))
        {
            for (const FString& OrderedName : Order)
            {
                Ordered.AddUnique(OrderedName);
            }
        }
        else
        {
            Unresolved.AddUnique(AffectedName);
        }
    }
    for (const FString& UnresolvedName : Unresolved)
    {
        Ordered.Remove(UnresolvedName);
        Ordered.Add(UnresolvedName);
    }

    for (int32 Index = Ordered.Num() - 1; Index >= 0; --Index)
    {
        if (!Affected.Contains(Ordered[Index]))
        {
            continue;
        }
        const FUBotPackageInfo* Entry = FindByName(Packages, Ordered[Index]);
        if (Entry && IsInstalled(*Entry) && IsEnabled(*Entry))
        {
            OutToDisable.Add(Entry->Name);
        }
    }
    return true;
}

bool FUBotPackageService::PlanInstall(const TArray<FUBotPackageInfo>& Packages, const FString& Name,
    TArray<FString>& OutToInstall, TArray<FString>& OutMessages, const FString& RequestedVersion)
{
    using namespace UBotPackageServicePrivate;

    OutToInstall.Reset();
    const FUBotPackageInfo* Package = FindByName(Packages, Name);
    if (!Package)
    {
        OutMessages.Add(FString::Printf(TEXT("Unknown uBot package '%s': it is neither installed nor listed in a package index."), *Name));
        return false;
    }

    TArray<FString> Order;
    FString Error;
    if (!FUBotPackageRegistry::ResolveDependencyOrder(Packages, Package->Name, Order, &Error))
    {
        OutMessages.Add(FString::Printf(TEXT("Cannot install %s: %s"), *Package->Name, *Error));
        return false;
    }

    bool bSuccess = true;
    for (const FString& OrderedName : Order)
    {
        const FUBotPackageInfo* Entry = FindByName(Packages, OrderedName);
        if (!Entry)
        {
            OutMessages.Add(FString::Printf(TEXT("Cannot install %s: required package %s is unknown."), *Package->Name, *OrderedName));
            bSuccess = false;
            continue;
        }
        if (IsInstalled(*Entry))
        {
            // An installed dependency is left alone, but a requested version of the root has to be the
            // installed one: this call never changes versions (that is UpdatePackageAsync's job), and
            // reporting success for another version would hide that nothing was cloned.
            const FString Requested = RequestedVersion.TrimStartAndEnd();
            if (!Requested.IsEmpty() && Entry->Name.Equals(Package->Name, ESearchCase::IgnoreCase))
            {
                FUBotSemVer RequestedSemVer;
                FUBotSemVer InstalledSemVer;
                if (!FUBotSemVer::Parse(Requested, RequestedSemVer))
                {
                    OutMessages.Add(FString::Printf(TEXT("Cannot install %s: '%s' is not a semantic version."), *Entry->Name, *Requested));
                    bSuccess = false;
                }
                else if (!FUBotSemVer::Parse(Entry->Version, InstalledSemVer) || InstalledSemVer != RequestedSemVer)
                {
                    const FString InstalledVersion = Entry->Version.IsEmpty() ? FString(TEXT("(unknown version)")) : Entry->Version;
                    OutMessages.Add(FString::Printf(
                        TEXT("Cannot install %s %s: version %s is already installed. Use -Update=%s or uBot Manager to change versions."),
                        *Entry->Name, *Requested, *InstalledVersion, *Entry->Name));
                    bSuccess = false;
                }
            }
            continue;
        }

        // Repository URLs are only taken from package index entries.
        const FString Repository = Entry->Repository.TrimStartAndEnd();
        FString Reason;
        if (!Entry->bFromIndex)
        {
            OutMessages.Add(FString::Printf(TEXT("Cannot install %s: it is not listed in a package index."), *Entry->Name));
            bSuccess = false;
        }
        else if (Entry->bPlanned)
        {
            OutMessages.Add(FString::Printf(TEXT("Cannot install %s: it is planned and cannot be installed yet."), *Entry->Name));
            bSuccess = false;
        }
        else if (Repository.IsEmpty())
        {
            OutMessages.Add(FString::Printf(TEXT("Cannot install %s: its package index entry has no repository."), *Entry->Name));
            bSuccess = false;
        }
        else if (!IsRepositoryUrlAllowed(Repository, &Reason))
        {
            OutMessages.Add(FString::Printf(TEXT("Cannot install %s from '%s': %s."), *Entry->Name, *RedactUrlCredentials(Repository), *Reason));
            bSuccess = false;
        }
        else if (!FindInstallVersion(*Entry, Entry->Name.Equals(Package->Name, ESearchCase::IgnoreCase) ? RequestedVersion : FString(), Reason))
        {
            OutMessages.Add(FString::Printf(TEXT("Cannot install %s: %s."), *Entry->Name, *Reason));
            bSuccess = false;
        }
        else if (ResolveInstallFolder(*Entry, &Reason).IsEmpty())
        {
            OutMessages.Add(FString::Printf(TEXT("Cannot install %s: %s."), *Entry->Name, *Reason));
            bSuccess = false;
        }
        else
        {
            OutToInstall.Add(Entry->Name);
        }
    }

    if (!bSuccess)
    {
        OutToInstall.Reset();
    }
    return bSuccess;
}
