#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Templates/Function.h"
#include "UBotPackageIndex.h"
#include "UBotPackageTypes.h"
#include "UBotRuntimeStatus.h"

/** One line of the uBot panel: dot, name and value. */
struct FUBotPanelRow
{
    FText Name;
    FText Value;
    FText ToolTip;
    /** Colour of the dot: Ok green, Info grey, Warning amber, Error red. */
    EUBotRuntimeSeverity Dot = EUBotRuntimeSeverity::Info;
    /** Value drawn as a link-coloured hint (an update is available). */
    bool bHighlightValue = false;
    /** Lets the panel find rows it updates on its own, such as the sim time. */
    FName Id;
};

/**
 * What the uBot panel and its status bar entry show, rebuilt (on the next tick) whenever the package
 * registry, the extension registry or the runtime status changes. The package index is reloaded when
 * the installed packages change and on Refresh. Game thread only.
 */
class FUBotPanelModel : public TSharedFromThis<FUBotPanelModel>
{
public:
    struct FSections
    {
        TArray<FUBotPanelRow> Packages;
        TArray<FUBotPanelRow> ExtensionPoints;
        TArray<FUBotPanelRow> Runtime;
        TArray<FUBotPanelRow> Problems;
    };

    static const FName SimTimeRowId;

    ~FUBotPanelModel();

    void Initialize();
    void Shutdown();

    /** Rescans the installed plugins (which reloads the package index) and rebuilds right away. */
    void Refresh();

    const FSections& GetSections() const { return Sections; }
    const FText& GetStatusBarText() const { return StatusBarText; }

    /** True while a PIE world exists. */
    static bool IsPlaying();

    /** Sim time of the PIE world, or 0 when none. */
    static double GetPlaySimTimeSeconds();

    /** Broadcast after the sections were rebuilt. */
    FSimpleMulticastDelegate OnChanged;

    // Pure builders, unit tested.

    /** Enabled installed packages from PackageView (installed packages merged with the index). */
    static TArray<FUBotPanelRow> MakePackageRows(const TArray<FUBotPackageInfo>& PackageView);

    static TArray<FUBotPanelRow> MakeExtensionPointRows(const TArray<TPair<FName, int32>>& PointsWithImplementationCounts);

    /** Runtime items, then the sim time (when given), the fixed time step and, outside a session, the last session. */
    static TArray<FUBotPanelRow> MakeRuntimeRows(const TArray<FUBotRuntimeItem>& Items, TOptional<double> SimTimeSeconds,
        bool bFixedTimeStep, double FixedTimeStepHz, const FUBotSessionInfo* LastSession);

    /**
     * Requirement problems of the enabled installed packages, descriptor issues, legacy plugins enabled
     * next to their replacement, runtime problems and package index errors.
     */
    static TArray<FUBotPanelRow> MakeProblemRows(const TArray<FUBotPackageInfo>& Installed, const TArray<FString>& DescriptorIssues,
        const TArray<FUBotPackageReplacement>& Replacements, TFunctionRef<bool(const FString&)> IsPluginEnabled,
        const TArray<FUBotRuntimeProblem>& RuntimeProblems, const TArray<FString>& IndexErrors);

    static FText MakeStatusBarText(int32 LoadedPackages, int32 Problems);
    static FText FormatSimTime(double Seconds);

    /** Version newer than InstalledVersion among AvailableVersions (newest first), or empty. */
    static FString FindUpdate(const FString& InstalledVersion, const TArray<FString>& AvailableVersions);

private:
    void RequestRebuild();
    void HandlePackagesChanged();
    void HandleExtensionsChanged(FName PointName);
    bool HandleDeferredRebuild(float DeltaTime);
    void ReloadIndex();
    void Rebuild();

    FSections Sections;
    FText StatusBarText;

    TArray<FUBotPackageInfo> IndexPackages;
    TArray<FUBotPackageReplacement> Replacements;
    TArray<FString> IndexErrors;

    FTSTicker::FDelegateHandle RebuildTickerHandle;
    bool bIndexReloadPending = true;
    FDelegateHandle PackagesChangedHandle;
    FDelegateHandle ExtensionsChangedHandle;
    FDelegateHandle RuntimeStatusChangedHandle;
};
