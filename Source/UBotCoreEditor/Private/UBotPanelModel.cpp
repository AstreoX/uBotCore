#include "UBotPanelModel.h"

#include "Async/Async.h"
#include "Editor.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/App.h"
#include "UBotClock.h"
#include "UBotCoreSettings.h"
#include "UBotEditorText.h"
#include "UBotExtensionRegistry.h"
#include "UBotPackageRegistry.h"
#include "UBotSemVer.h"

namespace UBotPanelModelPrivate
{
    bool IsInstalled(const FUBotPackageInfo& Package)
    {
        return Package.bInstalled || Package.State != EUBotPackageState::NotInstalled;
    }

    bool IsEnabled(const FUBotPackageInfo& Package)
    {
        return Package.bEnabled || Package.State == EUBotPackageState::Enabled;
    }

    int32 GetLayerSortKey(EUBotPackageLayer Layer)
    {
        return Layer == EUBotPackageLayer::Unknown ? MAX_int32 : static_cast<int32>(Layer);
    }

    FText JoinWithDot(const FText& First, const FString& Second)
    {
        return Second.IsEmpty() ? First : FText::Format(INVTEXT("{0} · {1}"), First, FText::FromString(Second));
    }

    FText FormatFixedTimeStep(bool bEnabled, double Hz)
    {
        if (!bEnabled || !(Hz > 0.0))
        {
            return UBot::EditorText::FixedTimeStepOff();
        }
        FNumberFormattingOptions Options;
        Options.SetUseGrouping(false).SetMaximumFractionalDigits(2);
        return FText::Format(UBot::EditorText::FixedTimeStepRate(), FText::AsNumber(Hz, &Options));
    }

    FText FormatLocalTime(const FDateTime& UtcTime)
    {
        // FText::AsTime shows the value it is given, so convert to the local clock first.
        const FDateTime LocalTime = UtcTime + (FDateTime::Now() - FDateTime::UtcNow());
        return FText::AsTime(LocalTime, EDateTimeStyle::Short, FText::GetInvariantTimeZone());
    }
}

const FName FUBotPanelModel::SimTimeRowId(TEXT("SimTime"));

FUBotPanelModel::~FUBotPanelModel()
{
    Shutdown();
}

void FUBotPanelModel::Initialize()
{
    PackagesChangedHandle = FUBotPackageRegistry::Get().OnPackagesChanged.AddSP(this, &FUBotPanelModel::HandlePackagesChanged);
    ExtensionsChangedHandle = FUBotExtensionRegistry::Get().OnExtensionsChanged.AddSP(this, &FUBotPanelModel::HandleExtensionsChanged);
    RuntimeStatusChangedHandle = FUBotRuntimeStatus::Get().OnChanged.AddSP(this, &FUBotPanelModel::RequestRebuild);
    bIndexReloadPending = true;
    Rebuild();
}

void FUBotPanelModel::Shutdown()
{
    FUBotPackageRegistry::Get().OnPackagesChanged.Remove(PackagesChangedHandle);
    FUBotExtensionRegistry::Get().OnExtensionsChanged.Remove(ExtensionsChangedHandle);
    FUBotRuntimeStatus::Get().OnChanged.Remove(RuntimeStatusChangedHandle);
    PackagesChangedHandle.Reset();
    ExtensionsChangedHandle.Reset();
    RuntimeStatusChangedHandle.Reset();

    if (RebuildTickerHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(RebuildTickerHandle);
        RebuildTickerHandle.Reset();
    }
}

void FUBotPanelModel::Refresh()
{
    FUBotPackageRegistry::Get().Refresh();
    bIndexReloadPending = true;
    Rebuild();
}

bool FUBotPanelModel::IsPlaying()
{
    return GEditor != nullptr && GEditor->PlayWorld != nullptr;
}

double FUBotPanelModel::GetPlaySimTimeSeconds()
{
    return IsPlaying() ? UBot::Clock::GetSimTimeSeconds(GEditor->PlayWorld) : 0.0;
}

void FUBotPanelModel::RequestRebuild()
{
    if (!RebuildTickerHandle.IsValid())
    {
        // Coalesces bursts, e.g. every factory a package registers at startup.
        RebuildTickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateSP(this, &FUBotPanelModel::HandleDeferredRebuild));
    }
}

void FUBotPanelModel::HandlePackagesChanged()
{
    bIndexReloadPending = true;
    RequestRebuild();
}

void FUBotPanelModel::HandleExtensionsChanged(FName PointName)
{
    // The extension registry broadcasts on whichever thread changed it.
    if (IsInGameThread())
    {
        RequestRebuild();
        return;
    }
    AsyncTask(ENamedThreads::GameThread, [WeakModel = AsWeak()]()
    {
        if (const TSharedPtr<FUBotPanelModel> Model = WeakModel.Pin())
        {
            Model->RequestRebuild();
        }
    });
}

bool FUBotPanelModel::HandleDeferredRebuild(float DeltaTime)
{
    RebuildTickerHandle.Reset();
    Rebuild();
    return false;
}

void FUBotPanelModel::ReloadIndex()
{
    IndexErrors.Reset();
    IndexPackages = FUBotPackageIndex::LoadConfiguredIndex(&IndexErrors, &Replacements);
    bIndexReloadPending = false;
}

void FUBotPanelModel::Rebuild()
{
    if (RebuildTickerHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(RebuildTickerHandle);
        RebuildTickerHandle.Reset();
    }
    if (bIndexReloadPending)
    {
        ReloadIndex();
    }

    const FUBotPackageRegistry& Registry = FUBotPackageRegistry::Get();
    Sections.Packages = MakePackageRows(FUBotPackageIndex::MergeInstalledWithIndex(Registry.GetInstalledPackages(), IndexPackages));

    const FUBotExtensionRegistry& Extensions = FUBotExtensionRegistry::Get();
    TArray<TPair<FName, int32>> Points;
    for (const FUBotExtensionPointInfo& Point : Extensions.GetExtensionPoints())
    {
        Points.Emplace(Point.Name, Extensions.GetExtensions(Point.Name).Num());
    }
    Sections.ExtensionPoints = MakeExtensionPointRows(Points);

    const FUBotRuntimeStatus& RuntimeStatus = FUBotRuntimeStatus::Get();
    const bool bPlaying = IsPlaying();
    bool bFixedTimeStep = false;
    double FixedTimeStepHz = 0.0;
    if (bPlaying)
    {
        bFixedTimeStep = FApp::UseFixedTimeStep();
        FixedTimeStepHz = FApp::GetFixedDeltaTime() > 0.0 ? 1.0 / FApp::GetFixedDeltaTime() : 0.0;
    }
    else if (const UUBotCoreSettings* Settings = GetDefault<UUBotCoreSettings>())
    {
        bFixedTimeStep = Settings->bUseFixedTimeStep;
        FixedTimeStepHz = Settings->FixedTimeStepHz;
    }
    Sections.Runtime = MakeRuntimeRows(RuntimeStatus.GetItems(), bPlaying ? TOptional<double>(GetPlaySimTimeSeconds()) : TOptional<double>(),
        bFixedTimeStep, FixedTimeStepHz, RuntimeStatus.IsSessionActive() ? nullptr : RuntimeStatus.GetLastSession());

    Sections.Problems = MakeProblemRows(Registry.GetInstalledPackages(), Registry.GetDescriptorIssues(), Replacements,
        [](const FString& PluginName)
        {
            const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(PluginName);
            return Plugin.IsValid() && Plugin->IsEnabled();
        },
        RuntimeStatus.GetProblems(), IndexErrors);

    StatusBarText = MakeStatusBarText(Sections.Packages.Num(), Sections.Problems.Num());
    OnChanged.Broadcast();
}

TArray<FUBotPanelRow> FUBotPanelModel::MakePackageRows(const TArray<FUBotPackageInfo>& PackageView)
{
    using namespace UBotPanelModelPrivate;

    TArray<const FUBotPackageInfo*> Loaded;
    for (const FUBotPackageInfo& Package : PackageView)
    {
        if (IsInstalled(Package) && IsEnabled(Package))
        {
            Loaded.Add(&Package);
        }
    }
    Loaded.StableSort([](const FUBotPackageInfo& A, const FUBotPackageInfo& B)
    {
        const int32 LayerA = GetLayerSortKey(A.Layer);
        const int32 LayerB = GetLayerSortKey(B.Layer);
        return LayerA != LayerB ? LayerA < LayerB : A.Name.Compare(B.Name, ESearchCase::IgnoreCase) < 0;
    });

    TArray<FUBotPanelRow> Rows;
    for (const FUBotPackageInfo* Package : Loaded)
    {
        FUBotPanelRow& Row = Rows.AddDefaulted_GetRef();
        Row.Name = FText::FromString(Package->Name);
        Row.Dot = Package->Problems.IsEmpty() ? EUBotRuntimeSeverity::Ok : EUBotRuntimeSeverity::Warning;
        Row.ToolTip = FText::FromString(Package->Description.IsEmpty()
            ? Package->FriendlyName
            : FString::Printf(TEXT("%s\n%s"), *Package->FriendlyName, *Package->Description));

        const FText Version = FText::FromString(Package->Version.IsEmpty() ? FString(TEXT("-")) : Package->Version);
        const FString Update = FindUpdate(Package->Version, Package->AvailableVersions);
        if (Update.IsEmpty())
        {
            Row.Value = Version;
        }
        else
        {
            Row.Value = FText::Format(UBot::EditorText::UpdateAvailable(), Version, FText::FromString(Update));
            Row.bHighlightValue = true;
        }
    }
    return Rows;
}

TArray<FUBotPanelRow> FUBotPanelModel::MakeExtensionPointRows(const TArray<TPair<FName, int32>>& PointsWithImplementationCounts)
{
    TArray<FUBotPanelRow> Rows;
    for (const TPair<FName, int32>& Point : PointsWithImplementationCounts)
    {
        FUBotPanelRow& Row = Rows.AddDefaulted_GetRef();
        Row.Name = FText::FromName(Point.Key);
        Row.Value = FText::Format(UBot::EditorText::Implementations(), Point.Value);
        Row.Dot = EUBotRuntimeSeverity::Info;
    }
    return Rows;
}

TArray<FUBotPanelRow> FUBotPanelModel::MakeRuntimeRows(const TArray<FUBotRuntimeItem>& Items, TOptional<double> SimTimeSeconds,
    bool bFixedTimeStep, double FixedTimeStepHz, const FUBotSessionInfo* LastSession)
{
    using namespace UBotPanelModelPrivate;

    TArray<FUBotPanelRow> Rows;
    for (const FUBotRuntimeItem& Item : Items)
    {
        FUBotPanelRow& Row = Rows.AddDefaulted_GetRef();
        Row.Name = Item.Label.IsEmpty() ? FText::FromString(Item.Id) : Item.Label;
        Row.Value = JoinWithDot(Item.ValueText.IsEmpty() ? FText::FromString(Item.Value) : Item.ValueText, Item.Detail);
        Row.Dot = Item.Severity;
    }

    if (SimTimeSeconds.IsSet())
    {
        FUBotPanelRow& Row = Rows.AddDefaulted_GetRef();
        Row.Id = SimTimeRowId;
        Row.Name = UBot::EditorText::SimTime();
        Row.Value = FormatSimTime(SimTimeSeconds.GetValue());
    }

    FUBotPanelRow& FixedStep = Rows.AddDefaulted_GetRef();
    FixedStep.Name = UBot::EditorText::FixedTimeStep();
    FixedStep.Value = FormatFixedTimeStep(bFixedTimeStep, FixedTimeStepHz);

    if (LastSession != nullptr && LastSession->EndedAt.IsSet())
    {
        FUBotPanelRow& Row = Rows.AddDefaulted_GetRef();
        Row.Name = UBot::EditorText::LastSession();
        Row.Value = JoinWithDot(FText::FromString(LexToString(LastSession->Kind)), FormatLocalTime(LastSession->EndedAt.GetValue()).ToString());
    }
    return Rows;
}

TArray<FUBotPanelRow> FUBotPanelModel::MakeProblemRows(const TArray<FUBotPackageInfo>& Installed, const TArray<FString>& DescriptorIssues,
    const TArray<FUBotPackageReplacement>& Replacements, TFunctionRef<bool(const FString&)> IsPluginEnabled,
    const TArray<FUBotRuntimeProblem>& RuntimeProblems, const TArray<FString>& IndexErrors)
{
    using namespace UBotPanelModelPrivate;
    namespace Text = UBot::EditorText;

    TArray<FUBotPanelRow> Rows;
    auto AddRow = [&Rows](const FText& Name, EUBotRuntimeSeverity Severity, const FText& ToolTip)
    {
        FUBotPanelRow& Row = Rows.AddDefaulted_GetRef();
        Row.Name = Name;
        Row.Dot = Severity;
        Row.ToolTip = ToolTip;
    };

    // Like uBot Manager: only enabled packages produce requirement problems.
    for (const FUBotPackageInfo& Package : Installed)
    {
        if (!IsInstalled(Package) || !IsEnabled(Package))
        {
            continue;
        }
        const FText PackageName = FText::FromString(Package.Name);
        for (const FUBotPackageProblem& Problem : Package.ProblemDetails)
        {
            const FText Requirement = FText::FromString(Problem.Constraint.IsEmpty()
                ? Problem.Dependency
                : FString::Printf(TEXT("%s %s"), *Problem.Dependency, *Problem.Constraint));
            const FText Message = FText::FromString(Problem.Message);
            switch (Problem.Kind)
            {
            case EUBotPackageProblemKind::RequiresVersion:
                AddRow(FText::Format(Text::ProblemRequires(), PackageName, Requirement), EUBotRuntimeSeverity::Warning, Message);
                break;
            case EUBotPackageProblemKind::RequiresMissing:
                AddRow(FText::Format(Text::ProblemRequiresMissing(), PackageName, Requirement), EUBotRuntimeSeverity::Warning, Message);
                break;
            case EUBotPackageProblemKind::RequiresDisabled:
                AddRow(FText::Format(Text::ProblemRequiresDisabled(), PackageName, FText::FromString(Problem.Dependency)), EUBotRuntimeSeverity::Warning, Message);
                break;
            case EUBotPackageProblemKind::DependencyCycle:
                AddRow(FText::Format(Text::ProblemOfPackage(), PackageName, Message), EUBotRuntimeSeverity::Error, Message);
                break;
            case EUBotPackageProblemKind::InvalidRequirement:
            default:
                AddRow(FText::Format(Text::ProblemOfPackage(), PackageName, Message), EUBotRuntimeSeverity::Warning, Message);
                break;
            }
        }
    }

    for (const FString& Issue : DescriptorIssues)
    {
        AddRow(FText::FromString(Issue), EUBotRuntimeSeverity::Warning, FText::FromString(Issue));
    }

    for (const FUBotPackageReplacement& Replacement : Replacements)
    {
        if (IsPluginEnabled(Replacement.Legacy) && IsPluginEnabled(Replacement.Replacement))
        {
            const FText Name = FText::Format(Text::ProblemLegacy(), FText::FromString(Replacement.Legacy), FText::FromString(Replacement.Replacement));
            AddRow(Name, EUBotRuntimeSeverity::Warning, Name);
        }
    }

    for (const FUBotRuntimeProblem& Problem : RuntimeProblems)
    {
        const FText Name = Problem.Message.IsEmpty() ? FText::FromString(Problem.Code) : Problem.Message;
        AddRow(Name, Problem.Severity == EUBotRuntimeSeverity::Ok ? EUBotRuntimeSeverity::Info : Problem.Severity, Name);
    }

    for (const FString& Error : IndexErrors)
    {
        const FText Name = FText::Format(Text::ProblemIndex(), FText::FromString(Error));
        AddRow(Name, EUBotRuntimeSeverity::Warning, Name);
    }
    return Rows;
}

FText FUBotPanelModel::MakeStatusBarText(int32 LoadedPackages, int32 Problems)
{
    return FText::FormatNamed(UBot::EditorText::StatusBar(), TEXT("Packages"), LoadedPackages, TEXT("Problems"), Problems);
}

FText FUBotPanelModel::FormatSimTime(double Seconds)
{
    FNumberFormattingOptions Options;
    Options.SetUseGrouping(false).SetMinimumFractionalDigits(2).SetMaximumFractionalDigits(2);
    return FText::Format(UBot::EditorText::SimTimeValue(), FText::AsNumber(Seconds, &Options));
}

FString FUBotPanelModel::FindUpdate(const FString& InstalledVersion, const TArray<FString>& AvailableVersions)
{
    FUBotSemVer Installed;
    FUBotSemVer Latest;
    if (AvailableVersions.IsEmpty() || !FUBotSemVer::Parse(InstalledVersion, Installed) || !FUBotSemVer::Parse(AvailableVersions[0], Latest))
    {
        return FString();
    }
    return Latest > Installed ? AvailableVersions[0] : FString();
}
