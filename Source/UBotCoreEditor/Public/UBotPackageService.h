#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"
#include "UBotPackageTypes.h"

/**
 * Editor-side uBot package operations built on FUBotPackageRegistry and FUBotPackageIndex.
 *
 * Every member must be called on the game thread. At most one asynchronous git operation runs
 * at a time. Completion callbacks are always delivered later on the game thread, also when an
 * operation is refused immediately.
 */
class UBOTCOREEDITOR_API FUBotPackageService
{
public:
    using FOnPackageOperationComplete = TFunction<void(bool bSuccess, const TArray<FString>& Messages)>;

    static FUBotPackageService& Get();

    ~FUBotPackageService();

    FUBotPackageService(const FUBotPackageService&) = delete;
    FUBotPackageService& operator=(const FUBotPackageService&) = delete;

    /** Installed packages merged with the configured package index, then validated. */
    TArray<FUBotPackageInfo> BuildPackageView() const;
    /** Same as BuildPackageView() and also reports package index loading errors. */
    TArray<FUBotPackageInfo> BuildPackageView(TArray<FString>& OutIndexErrors) const;

    /**
     * Enables Name and every required uBot package it depends on, dependencies first, through
     * IProjectManager and saves the project file. Fails without changes when a required package
     * is not installed.
     */
    bool EnablePackage(const FString& Name, TArray<FString>& OutMessages);

    /**
     * Disables Name. Refuses when enabled packages depend on it unless bForce is set, in which
     * case those dependents are disabled as well (dependents first).
     */
    bool DisablePackage(const FString& Name, bool bForce, TArray<FString>& OutMessages);

    /**
     * Clones Name and its missing required dependencies (dependencies first) from the repositories
     * listed in the package index into <ProjectDir>/<PackageInstallDirectory>/<RepoFolder>,
     * registers them with the plugin manager and enables Name.
     */
    void InstallPackageAsync(const FString& Name, FOnPackageOperationComplete OnComplete);

    /** Runs "git -C <BaseDir> pull --ff-only" for an installed package that is a git working copy. */
    void UpdatePackageAsync(const FString& Name, FOnPackageOperationComplete OnComplete);

    /** True after any enable, disable, install or code-changing update in this editor session. */
    bool IsRestartRequired() const;

    /** True while an install or update is running. */
    bool IsBusy() const;

    /** Short description of the running step, empty when idle. */
    FString GetBusyDescription() const;

    /** Asks the running git process to terminate. The operation then completes with a failure. */
    void CancelActiveOperation();

    /**
     * For blocking callers such as commandlets (never from Slate callbacks): processes game thread
     * work until the active operation and its completion callback have finished. Cancels the
     * operation when TimeoutSeconds (> 0) elapses. Returns false on timeout.
     */
    bool WaitUntilIdle(double TimeoutSeconds);

    /** Enabled state written to the project file in this session, if it was changed. */
    bool GetPendingEnabledState(const FString& Name, bool& bOutEnabled) const;

    /** Enabled packages (taking pending changes into account) that require Name. */
    TArray<FString> GetEnabledDependents(const FString& Name) const;

    /** Absolute directory new packages are cloned into. */
    static FString GetInstallDirectory();

    /** Cancels any running git process without invoking its callback. Called on module shutdown. */
    void Shutdown();

    // Pure helpers (no engine state), used by the operations above and by the automation tests.

    /** Last path segment of a repository URL without ".git"; empty when no safe folder name results. */
    static FString RepositoryFolderName(const FString& RepositoryUrl);

    /** Rejects URLs that could inject git options or select a command-running transport. */
    static bool IsRepositoryUrlAllowed(const FString& RepositoryUrl, FString* OutReason = nullptr);

    /** Quotes one process argument for FPlatformProcess::CreateProc on the current platform. */
    static FString QuoteArgument(const FString& Argument);

    /**
     * Replaces credentials embedded in URLs found in Text so that it can be logged or shown:
     * "https://user:token@host/repo.git" becomes "https://***@host/repo.git". For http(s) the whole
     * user info is replaced (a bare token is a credential too); for other schemes only user info
     * that contains a password, so "ssh://git@host" and scp-like "git@host:repo.git" stay unchanged.
     */
    static FString RedactUrlCredentials(const FString& Text);

    /**
     * Best-effort removal of the half-written folder a cancelled git clone leaves behind (git cannot
     * clean up after a killed process, and the leftover folder would block every later install).
     * Refuses anything that is not a folder below GetInstallDirectory(). Returns true when the folder
     * is gone afterwards; OutMessage then starts with "Removed the incomplete clone", otherwise it
     * explains what the user has to do.
     */
    static bool RemoveIncompleteClone(const FString& CloneDirectory, FString& OutMessage);

    /** Packages to enable for Name (dependencies first). False when something required is missing. */
    static bool PlanEnable(const TArray<FUBotPackageInfo>& Packages, const FString& Name,
        TArray<FString>& OutToEnable, TArray<FString>& OutMessages);

    /** Packages to disable for Name (dependents first). False when dependents block and !bForce. */
    static bool PlanDisable(const TArray<FUBotPackageInfo>& Packages, const FString& Name, bool bForce,
        TArray<FString>& OutToDisable, TArray<FString>& OutMessages);

    /** Packages to clone for Name (dependencies first). False when one cannot be installed from an index. */
    static bool PlanInstall(const TArray<FUBotPackageInfo>& Packages, const FString& Name,
        TArray<FString>& OutToInstall, TArray<FString>& OutMessages);

private:
    struct FCloneTask;
    struct FGitOperation;

    FUBotPackageService();

    TArray<FUBotPackageInfo> BuildEffectiveView() const;
    bool EnablePackageInternal(const FString& Name, TArray<FString>& OutMessages);
    bool ApplyEnabledStates(const TArray<FString>& PackageNames, bool bEnabled, TArray<FString>& OutMessages);
    bool PrepareCloneTasks(const TArray<FUBotPackageInfo>& Packages, const TArray<FString>& PackageNames,
        TArray<FCloneTask>& OutTasks, TArray<FString>& OutMessages) const;

    void RunNextInstallStep(const TSharedRef<FGitOperation>& Operation);
    bool LaunchGit(const TSharedRef<FGitOperation>& Operation, const FString& Arguments, const FString& WorkingDirectory);
    void HandleGitFinished(const TSharedRef<FGitOperation>& Operation, int32 ReturnCode, bool bCanceled);
    void HandleCloneFinished(const TSharedRef<FGitOperation>& Operation, int32 ReturnCode);
    void HandlePullFinished(const TSharedRef<FGitOperation>& Operation, int32 ReturnCode, const TArray<FString>& OutputLines);
    void FinishOperation(const TSharedRef<FGitOperation>& Operation, bool bSuccess);
    void DispatchCompletion(FOnPackageOperationComplete OnComplete, bool bSuccess, TArray<FString> Messages);

    TSharedPtr<FGitOperation> ActiveOperation;
    TMap<FString, bool> PendingEnabledStates;
    int32 PendingCompletionCount = 0;
    bool bRestartRequired = false;
};
