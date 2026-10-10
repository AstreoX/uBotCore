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
     * listed in the package index into <ProjectDir>/<PackageInstallDirectory>/<Folder>, registers them
     * with the plugin manager and enables Name. Each package is cloned at the git ref of its newest
     * installable index version and goes into the index entry's "folder" (default: the last segment of
     * its repository without ".git"). Planned versions are refused.
     */
    void InstallPackageAsync(const FString& Name, FOnPackageOperationComplete OnComplete);

    /**
     * Same as above, but Name itself is installed at the index version Version ("0.1.0", "v0.1.0"; empty
     * means the newest installable one). Required packages still get their newest installable version.
     * If Name is already installed, Version must equal the installed version (the call then succeeds
     * without cloning), otherwise the call fails; use UpdatePackageAsync to change versions.
     */
    void InstallPackageAsync(const FString& Name, const FString& Version, FOnPackageOperationComplete OnComplete);

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

    /**
     * Enforces the repository URL allow-list of the uBot Manager spec (section 5.4), the same policy as
     * the manager: https://, http://, ssh://, file://, scp-like user@host:path and absolute local paths.
     * Rejects everything else (git://, "host:path", relative paths, "<transport>::<address>" remote
     * helpers), whitespace, quotes, control characters and anything that starts with '-' (a git option).
     */
    static bool IsRepositoryUrlAllowed(const FString& RepositoryUrl, FString* OutReason = nullptr);

    /**
     * Name of the folder below GetInstallDirectory() that Package is cloned into: the index entry's
     * "folder" when present, else the last segment of its repository without ".git". Both must be one
     * safe path segment (FUBotPackageIndex::IsSafeFolderName). Empty, with OutReason set, otherwise.
     */
    static FString ResolveInstallFolder(const FUBotPackageInfo& Package, FString* OutReason = nullptr);

    /**
     * The index version of Package that an install clones: the one named RequestedVersion (compared as
     * a semantic version, so "v0.1.0" finds "0.1.0"), or the newest installable one when it is empty.
     * Returns null with OutReason set when there is none, the version is planned, or it lists no safe git ref.
     */
    static const FUBotPackageIndexVersion* FindInstallVersion(const FUBotPackageInfo& Package,
        const FString& RequestedVersion, FString& OutReason);

    /** True for 7 to 40 hexadecimal digits: a commit id, which "git clone --branch" does not accept. */
    static bool IsCommitId(const FString& Ref);

    /**
     * Arguments (without the executable) of the git command that clones Repository at Ref into
     * Destination. A tag or branch is cloned with --branch; a commit id is cloned at the default branch
     * and needs MakeCheckoutArguments() afterwards. "--" keeps the URL from being read as an option.
     */
    static FString MakeCloneArguments(const FString& Repository, const FString& Ref, const FString& Destination);

    /** Arguments of the git command that detaches the clone in Destination at the commit or ref Ref. */
    static FString MakeCheckoutArguments(const FString& Destination, const FString& Ref);

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

    /**
     * Packages to clone for Name (dependencies first). False when one cannot be installed from an index
     * (not listed, planned, no repository, a refused repository URL, no usable version ref or an unsafe
     * install folder). RequestedVersion selects the version of Name itself, see FindInstallVersion().
     * An installed Name with a non-empty RequestedVersion also fails unless RequestedVersion equals the
     * installed version, since installing never changes the version of an installed package.
     */
    static bool PlanInstall(const TArray<FUBotPackageInfo>& Packages, const FString& Name,
        TArray<FString>& OutToInstall, TArray<FString>& OutMessages, const FString& RequestedVersion = FString());

private:
    struct FCloneTask;
    struct FGitOperation;

    FUBotPackageService();

    TArray<FUBotPackageInfo> BuildEffectiveView() const;
    bool EnablePackageInternal(const FString& Name, TArray<FString>& OutMessages);
    bool ApplyEnabledStates(const TArray<FString>& PackageNames, bool bEnabled, TArray<FString>& OutMessages);
    bool PrepareCloneTasks(const TArray<FUBotPackageInfo>& Packages, const TArray<FString>& PackageNames,
        const FString& RootName, const FString& RequestedVersion, TArray<FCloneTask>& OutTasks,
        TArray<FString>& OutMessages) const;

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
