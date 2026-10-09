#include "SUBotPackageManager.h"

#include "Async/Async.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/DateTime.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Styling/StyleColors.h"
#include "UBotCore.h"
#include "UBotExtensionRegistry.h"
#include "UBotPackageRegistry.h"
#include "UBotPackageService.h"
#include "UnrealEdMisc.h"
#include "Widgets/Images/SThrobber.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/ISlateEditableTextWidget.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "UBotPackageManager"

namespace UBotPackageManagerPrivate
{
    const FName ColumnIdName(TEXT("Name"));
    const FName ColumnIdVersion(TEXT("Version"));
    const FName ColumnIdLayer(TEXT("Layer"));
    const FName ColumnIdState(TEXT("State"));
    const FName ColumnIdProblems(TEXT("Problems"));

    constexpr int32 MaxLogLines = 500;

    bool IsEffectivelyEnabled(const FUBotPackageInfo& Package)
    {
        bool bPendingEnabled = false;
        if (Package.bInstalled && FUBotPackageService::Get().GetPendingEnabledState(Package.Name, bPendingEnabled))
        {
            return bPendingEnabled;
        }
        return Package.bInstalled && Package.bEnabled;
    }

    FText GetStateText(const FUBotPackageInfo& Package)
    {
        if (!Package.bInstalled)
        {
            return LOCTEXT("StateNotInstalled", "Not installed");
        }
        const bool bEffectivelyEnabled = IsEffectivelyEnabled(Package);
        if (bEffectivelyEnabled != Package.bEnabled)
        {
            return bEffectivelyEnabled
                ? LOCTEXT("StateEnabledAfterRestart", "Enabled after restart")
                : LOCTEXT("StateDisabledAfterRestart", "Disabled after restart");
        }
        return Package.bEnabled ? LOCTEXT("StateEnabled", "Enabled") : LOCTEXT("StateDisabled", "Disabled");
    }

    FString GetFullBaseDir(const FUBotPackageInfo& Package)
    {
        if (Package.BaseDir.IsEmpty())
        {
            return FString();
        }
        FString BaseDir = FPaths::ConvertRelativePathToFull(Package.BaseDir);
        FPaths::NormalizeDirectoryName(BaseDir);
        return BaseDir;
    }

    bool IsWebUrl(const FString& Url)
    {
        return Url.StartsWith(TEXT("https://"), ESearchCase::IgnoreCase) || Url.StartsWith(TEXT("http://"), ESearchCase::IgnoreCase);
    }

    int32 GetLayerSortKey(EUBotPackageLayer Layer)
    {
        return Layer == EUBotPackageLayer::Unknown ? MAX_int32 : static_cast<int32>(Layer);
    }

    FString JoinOrNone(const TArray<FString>& Values)
    {
        return Values.Num() > 0 ? FString::Join(Values, TEXT(", ")) : FString(TEXT("-"));
    }
}

/** One row of the package list. */
class SUBotPackageRow : public SMultiColumnTableRow<SUBotPackageManager::FPackageItem>
{
public:
    SLATE_BEGIN_ARGS(SUBotPackageRow) {}
        SLATE_ARGUMENT(SUBotPackageManager::FPackageItem, Package)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView)
    {
        Package = InArgs._Package;
        FSuperRowType::Construct(FTableRowArgs().Padding(FMargin(2.0f, 1.0f)), InOwnerTableView);
    }

    virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& InColumnName) override
    {
        using namespace UBotPackageManagerPrivate;

        if (!Package.IsValid())
        {
            return SNullWidget::NullWidget;
        }

        FText Text;
        FText ToolTip;
        FSlateColor Color = FSlateColor::UseForeground();

        if (InColumnName == ColumnIdName)
        {
            Text = FText::FromString(Package->Name);
            ToolTip = FText::FromString(Package->FriendlyName.IsEmpty()
                ? Package->Description
                : FString::Printf(TEXT("%s\n%s"), *Package->FriendlyName, *Package->Description));
        }
        else if (InColumnName == ColumnIdVersion)
        {
            Text = FText::FromString(Package->Version.IsEmpty() ? FString(TEXT("-")) : Package->Version);
        }
        else if (InColumnName == ColumnIdLayer)
        {
            Text = FText::FromString(LexToString(Package->Layer));
        }
        else if (InColumnName == ColumnIdState)
        {
            Text = GetStateText(*Package);
            if (!Package->bInstalled)
            {
                Color = FSlateColor::UseSubduedForeground();
            }
        }
        else if (InColumnName == ColumnIdProblems)
        {
            Text = FText::AsNumber(Package->Problems.Num());
            if (Package->Problems.Num() > 0)
            {
                Color = FStyleColors::Error;
                ToolTip = FText::FromString(FString::Join(Package->Problems, TEXT("\n")));
            }
        }

        return SNew(SBox)
            .VAlign(VAlign_Center)
            .Padding(FMargin(4.0f, 2.0f))
            [
                SNew(STextBlock)
                .Text(Text)
                .ToolTipText(ToolTip)
                .ColorAndOpacity(Color)
            ];
    }

private:
    SUBotPackageManager::FPackageItem Package;
};

void SUBotPackageManager::Construct(const FArguments& InArgs)
{
    using namespace UBotPackageManagerPrivate;

    ChildSlot
    [
        SNew(SVerticalBox)

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(4.0f)
        [
            BuildToolbar()
        ]

        + SVerticalBox::Slot()
        .FillHeight(1.0f)
        [
            SNew(SSplitter)
            .Orientation(Orient_Vertical)

            + SSplitter::Slot()
            .Value(0.72f)
            [
                SNew(SSplitter)
                .Orientation(Orient_Horizontal)

                + SSplitter::Slot()
                .Value(0.55f)
                [
                    SNew(SBorder)
                    .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
                    .Padding(2.0f)
                    [
                        SAssignNew(PackageListView, SListView<FPackageItem>)
                        .ListItemsSource(&Packages)
                        .SelectionMode(ESelectionMode::Single)
                        .OnGenerateRow(this, &SUBotPackageManager::GenerateRow)
                        .OnSelectionChanged(this, &SUBotPackageManager::HandleSelectionChanged)
                        .HeaderRow
                        (
                            SNew(SHeaderRow)
                            + SHeaderRow::Column(ColumnIdName)
                            .DefaultLabel(LOCTEXT("ColumnName", "Name"))
                            .FillWidth(0.32f)
                            + SHeaderRow::Column(ColumnIdVersion)
                            .DefaultLabel(LOCTEXT("ColumnVersion", "Version"))
                            .FillWidth(0.14f)
                            + SHeaderRow::Column(ColumnIdLayer)
                            .DefaultLabel(LOCTEXT("ColumnLayer", "Layer"))
                            .FillWidth(0.17f)
                            + SHeaderRow::Column(ColumnIdState)
                            .DefaultLabel(LOCTEXT("ColumnState", "State"))
                            .FillWidth(0.25f)
                            + SHeaderRow::Column(ColumnIdProblems)
                            .DefaultLabel(LOCTEXT("ColumnProblems", "Problems"))
                            .FillWidth(0.12f)
                        )
                    ]
                ]

                + SSplitter::Slot()
                .Value(0.45f)
                [
                    SNew(SBorder)
                    .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
                    .Padding(6.0f)
                    [
                        SNew(SVerticalBox)

                        + SVerticalBox::Slot()
                        .FillHeight(1.0f)
                        [
                            SNew(SScrollBox)
                            + SScrollBox::Slot()
                            [
                                SAssignNew(DetailsBox, SVerticalBox)
                            ]
                        ]

                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(0.0f, 6.0f, 0.0f, 0.0f)
                        [
                            BuildActionButtons()
                        ]
                    ]
                ]
            ]

            + SSplitter::Slot()
            .Value(0.28f)
            [
                SNew(SBorder)
                .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
                .Padding(4.0f)
                [
                    SNew(SVerticalBox)

                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(2.0f, 0.0f, 2.0f, 4.0f)
                    [
                        SNew(STextBlock)
                        .Text(LOCTEXT("MessagesHeader", "Messages"))
                        .Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
                    ]

                    + SVerticalBox::Slot()
                    .FillHeight(1.0f)
                    [
                        SAssignNew(MessageLogBox, SMultiLineEditableTextBox)
                        .IsReadOnly(true)
                        .AutoWrapText(true)
                        .AlwaysShowScrollbars(true)
                    ]
                ]
            ]
        ]
    ];

    // Weak (SP) bindings: they go stale with the widget, so the destructor never has to reach
    // into the registries, which may already be gone during editor shutdown.
    FUBotPackageRegistry::Get().OnPackagesChanged.AddSP(this, &SUBotPackageManager::HandlePackagesChanged);
    FUBotExtensionRegistry::Get().OnExtensionsChanged.AddSP(this, &SUBotPackageManager::HandleExtensionsChanged);

    RebuildPackageList(/*bReportIndexErrors*/ true);
}

TSharedRef<SWidget> SUBotPackageManager::BuildToolbar()
{
    return SNew(SHorizontalBox)

        + SHorizontalBox::Slot()
        .AutoWidth()
        .Padding(0.0f, 0.0f, 4.0f, 0.0f)
        [
            SNew(SButton)
            .Text(LOCTEXT("Refresh", "Refresh"))
            .ToolTipText(LOCTEXT("RefreshTooltip", "Rescan installed plugins and reload the package index files."))
            .OnClicked(this, &SUBotPackageManager::HandleRefreshClicked)
        ]

        + SHorizontalBox::Slot()
        .AutoWidth()
        .Padding(0.0f, 0.0f, 4.0f, 0.0f)
        [
            SNew(SButton)
            .Text(LOCTEXT("RestartEditor", "Restart Editor"))
            .ToolTipText(LOCTEXT("RestartEditorTooltip", "Restart the editor so that package changes take effect."))
            .IsEnabled(this, &SUBotPackageManager::CanRestart)
            .OnClicked(this, &SUBotPackageManager::HandleRestartClicked)
        ]

        + SHorizontalBox::Slot()
        .FillWidth(1.0f)
        .VAlign(VAlign_Center)
        .Padding(8.0f, 0.0f)
        [
            SNew(STextBlock)
            .Text(this, &SUBotPackageManager::GetStatusText)
            .ColorAndOpacity(FSlateColor::UseSubduedForeground())
        ]

        + SHorizontalBox::Slot()
        .AutoWidth()
        .VAlign(VAlign_Center)
        .Padding(0.0f, 0.0f, 4.0f, 0.0f)
        [
            SNew(SCircularThrobber)
            .Radius(8.0f)
            .Visibility(this, &SUBotPackageManager::GetBusyVisibility)
        ]

        + SHorizontalBox::Slot()
        .AutoWidth()
        [
            SNew(SButton)
            .Text(LOCTEXT("Cancel", "Cancel"))
            .ToolTipText(LOCTEXT("CancelTooltip", "Stop the running git command."))
            .Visibility(this, &SUBotPackageManager::GetBusyVisibility)
            .OnClicked(this, &SUBotPackageManager::HandleCancelClicked)
        ];
}

TSharedRef<SWidget> SUBotPackageManager::BuildActionButtons()
{
    return SNew(SUniformGridPanel)
        .SlotPadding(FMargin(2.0f))

        + SUniformGridPanel::Slot(0, 0)
        [
            SNew(SButton)
            .HAlign(HAlign_Center)
            .Text(LOCTEXT("Install", "Install"))
            .ToolTipText(LOCTEXT("InstallTooltip", "Clone the package and its missing dependencies with git, then enable it."))
            .IsEnabled(this, &SUBotPackageManager::CanInstall)
            .OnClicked(this, &SUBotPackageManager::HandleInstallClicked)
        ]
        + SUniformGridPanel::Slot(1, 0)
        [
            SNew(SButton)
            .HAlign(HAlign_Center)
            .Text(LOCTEXT("Update", "Update"))
            .ToolTipText(LOCTEXT("UpdateTooltip", "Fast-forward the package's git working copy (git pull --ff-only)."))
            .IsEnabled(this, &SUBotPackageManager::CanUpdate)
            .OnClicked(this, &SUBotPackageManager::HandleUpdateClicked)
        ]
        + SUniformGridPanel::Slot(2, 0)
        [
            SNew(SButton)
            .HAlign(HAlign_Center)
            .Text(LOCTEXT("Enable", "Enable"))
            .ToolTipText(LOCTEXT("EnableTooltip", "Enable the package and its required packages in the project file."))
            .IsEnabled(this, &SUBotPackageManager::CanEnable)
            .OnClicked(this, &SUBotPackageManager::HandleEnableClicked)
        ]
        + SUniformGridPanel::Slot(0, 1)
        [
            SNew(SButton)
            .HAlign(HAlign_Center)
            .Text(LOCTEXT("Disable", "Disable"))
            .ToolTipText(LOCTEXT("DisableTooltip", "Disable the package in the project file."))
            .IsEnabled(this, &SUBotPackageManager::CanDisable)
            .OnClicked(this, &SUBotPackageManager::HandleDisableClicked)
        ]
        + SUniformGridPanel::Slot(1, 1)
        [
            SNew(SButton)
            .HAlign(HAlign_Center)
            .Text(LOCTEXT("OpenFolder", "Open Folder"))
            .ToolTipText(LOCTEXT("OpenFolderTooltip", "Show the package folder in the file browser."))
            .IsEnabled(this, &SUBotPackageManager::CanOpenFolder)
            .OnClicked(this, &SUBotPackageManager::HandleOpenFolderClicked)
        ]
        + SUniformGridPanel::Slot(2, 1)
        [
            SNew(SButton)
            .HAlign(HAlign_Center)
            .Text(LOCTEXT("OpenRepository", "Open Repository"))
            .ToolTipText(LOCTEXT("OpenRepositoryTooltip", "Open the package repository in the web browser."))
            .IsEnabled(this, &SUBotPackageManager::CanOpenRepository)
            .OnClicked(this, &SUBotPackageManager::HandleOpenRepositoryClicked)
        ];
}

void SUBotPackageManager::RefreshPackages()
{
    {
        TGuardValue<bool> RefreshGuard(bRefreshingRegistry, true);
        FUBotPackageRegistry::Get().Refresh();
    }
    RebuildPackageList(/*bReportIndexErrors*/ true);
}

void SUBotPackageManager::RebuildPackageList(bool bReportIndexErrors)
{
    using namespace UBotPackageManagerPrivate;

    TArray<FString> IndexErrors;
    TArray<FUBotPackageInfo> View = FUBotPackageService::Get().BuildPackageView(IndexErrors);
    if (bReportIndexErrors)
    {
        for (const FString& Error : IndexErrors)
        {
            AppendMessage(FString::Printf(TEXT("Package index: %s"), *Error));
        }
    }

    View.Sort([](const FUBotPackageInfo& A, const FUBotPackageInfo& B)
    {
        const int32 LayerA = GetLayerSortKey(A.Layer);
        const int32 LayerB = GetLayerSortKey(B.Layer);
        return LayerA != LayerB ? LayerA < LayerB : A.Name < B.Name;
    });

    Packages.Reset(View.Num());
    FPackageItem ItemToSelect;
    for (FUBotPackageInfo& Package : View)
    {
        FPackageItem Item = MakeShared<FUBotPackageInfo>(MoveTemp(Package));
        if (!SelectedPackageName.IsEmpty() && Item->Name.Equals(SelectedPackageName, ESearchCase::IgnoreCase))
        {
            ItemToSelect = Item;
        }
        Packages.Add(MoveTemp(Item));
    }

    if (PackageListView.IsValid())
    {
        PackageListView->RequestListRefresh();
        if (ItemToSelect.IsValid())
        {
            PackageListView->SetSelection(ItemToSelect, ESelectInfo::Direct);
        }
        else
        {
            SelectedPackageName.Reset();
            PackageListView->ClearSelection();
        }
    }

    RebuildDetails();
}

void SUBotPackageManager::HandlePackagesChanged()
{
    if (!bRefreshingRegistry)
    {
        RebuildPackageList(/*bReportIndexErrors*/ false);
    }
}

void SUBotPackageManager::HandleExtensionsChanged(FName PointName)
{
    if (IsInGameThread())
    {
        RebuildDetails();
        return;
    }

    // The registry may broadcast from another thread; Slate is game thread only.
    const TWeakPtr<SUBotPackageManager> WeakSelf = SharedThis(this);
    AsyncTask(ENamedThreads::GameThread, [WeakSelf]()
    {
        if (const TSharedPtr<SUBotPackageManager> Self = WeakSelf.Pin())
        {
            Self->RebuildDetails();
        }
    });
}

void SUBotPackageManager::RebuildDetails()
{
    using namespace UBotPackageManagerPrivate;

    if (!DetailsBox.IsValid())
    {
        return;
    }
    DetailsBox->ClearChildren();
    bSelectionIsGitWorkingCopy = false;

    const FUBotPackageInfo* Package = GetSelectedPackage();
    if (!Package)
    {
        DetailsBox->AddSlot()
        .AutoHeight()
        [
            SNew(STextBlock)
            .Text(LOCTEXT("NoSelection", "Select a package to see its details."))
            .ColorAndOpacity(FSlateColor::UseSubduedForeground())
        ];
        return;
    }

    const FString BaseDir = GetFullBaseDir(*Package);
    if (Package->bInstalled && !BaseDir.IsEmpty())
    {
        const FString GitPath = FPaths::Combine(BaseDir, TEXT(".git"));
        bSelectionIsGitWorkingCopy = IFileManager::Get().DirectoryExists(*GitPath) || IFileManager::Get().FileExists(*GitPath);
    }

    const FString Title = Package->FriendlyName.IsEmpty() || Package->FriendlyName == Package->Name
        ? Package->Name
        : FString::Printf(TEXT("%s (%s)"), *Package->FriendlyName, *Package->Name);

    DetailsBox->AddSlot()
    .AutoHeight()
    .Padding(0.0f, 0.0f, 0.0f, 6.0f)
    [
        SNew(STextBlock)
        .Text(FText::FromString(Title))
        .Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
        .AutoWrapText(true)
    ];

    auto AddRow = [this](const FText& Label, const FString& Value, const FSlateColor& Color = FSlateColor::UseForeground())
    {
        DetailsBox->AddSlot()
        .AutoHeight()
        .Padding(0.0f, 1.0f)
        [
            SNew(SHorizontalBox)

            + SHorizontalBox::Slot()
            .AutoWidth()
            [
                SNew(SBox)
                .WidthOverride(120.0f)
                [
                    SNew(STextBlock)
                    .Text(Label)
                    .ColorAndOpacity(FSlateColor::UseSubduedForeground())
                ]
            ]

            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            [
                SNew(STextBlock)
                .Text(FText::FromString(Value.IsEmpty() ? FString(TEXT("-")) : Value))
                .ColorAndOpacity(Color)
                .AutoWrapText(true)
            ]
        ];
    };

    TArray<FString> Requires;
    for (const FUBotPackageDependency& Dependency : Package->Requires)
    {
        FString Line = FString::Printf(TEXT("%s %s"), *Dependency.Name, Dependency.Version.IsEmpty() ? TEXT("(any)") : *Dependency.Version);
        if (Dependency.bOptional)
        {
            Line += TEXT(" (optional)");
        }
        Requires.Add(MoveTemp(Line));
    }

    const FName OwnerName(*Package->Name);
    TArray<FString> ExtensionPoints;
    for (const FUBotExtensionPointInfo& Point : FUBotExtensionRegistry::Get().GetExtensionPoints())
    {
        if (Point.OwnerPackage == OwnerName)
        {
            ExtensionPoints.Add(Point.Description.IsEmpty()
                ? Point.Name.ToString()
                : FString::Printf(TEXT("%s - %s"), *Point.Name.ToString(), *Point.Description.ToString()));
        }
    }
    TArray<FString> Extensions;
    for (const FUBotExtensionRecord& Record : FUBotExtensionRegistry::Get().GetAllExtensions())
    {
        if (Record.OwnerPackage == OwnerName)
        {
            Extensions.Add(FString::Printf(TEXT("%s -> %s"), *Record.ExtensionName.ToString(), *Record.ExtensionPoint.ToString()));
        }
    }

    AddRow(LOCTEXT("DetailVersion", "Version"), Package->Version);
    AddRow(LOCTEXT("DetailLayer", "Layer"), LexToString(Package->Layer));
    AddRow(LOCTEXT("DetailState", "State"), GetStateText(*Package).ToString());
    AddRow(LOCTEXT("DetailLocation", "Location"), Package->bInstalled ? BaseDir : FString(TEXT("Not installed")));
    AddRow(LOCTEXT("DetailDescription", "Description"), Package->Description);
    AddRow(LOCTEXT("DetailRepository", "Repository"), FUBotPackageService::RedactUrlCredentials(Package->Repository));
    AddRow(LOCTEXT("DetailDocs", "Documentation"), Package->DocsUrl);
    AddRow(LOCTEXT("DetailTags", "Tags"), JoinOrNone(Package->Tags));
    AddRow(LOCTEXT("DetailProvides", "Provides"), FString::Join(Package->Provides, TEXT("\n")));
    AddRow(LOCTEXT("DetailRequires", "Requires"), FString::Join(Requires, TEXT("\n")));
    AddRow(LOCTEXT("DetailExternal", "External plugins"), JoinOrNone(Package->ExternalRequires));
    AddRow(LOCTEXT("DetailExtensionPoints", "Extension points"), FString::Join(ExtensionPoints, TEXT("\n")));
    AddRow(LOCTEXT("DetailExtensions", "Extensions"), FString::Join(Extensions, TEXT("\n")));
    if (Package->Problems.Num() > 0)
    {
        AddRow(LOCTEXT("DetailProblems", "Problems"), FString::Join(Package->Problems, TEXT("\n")), FStyleColors::Error);
    }
    else
    {
        AddRow(LOCTEXT("DetailProblems", "Problems"), TEXT("None"));
    }
}

void SUBotPackageManager::AppendMessage(const FString& Message)
{
    using namespace UBotPackageManagerPrivate;

    UE_LOG(LogUBot, Log, TEXT("uBot packages: %s"), *Message);

    LogLines.Add(FString::Printf(TEXT("[%s] %s"), *FDateTime::Now().ToString(TEXT("%H:%M:%S")), *Message));
    if (LogLines.Num() > MaxLogLines)
    {
        LogLines.RemoveAt(0, LogLines.Num() - MaxLogLines);
    }

    if (MessageLogBox.IsValid())
    {
        MessageLogBox->SetText(FText::FromString(FString::Join(LogLines, TEXT("\n"))));
        MessageLogBox->ScrollTo(ETextLocation::EndOfDocument);
    }
}

void SUBotPackageManager::AppendMessages(const TArray<FString>& Messages)
{
    for (const FString& Message : Messages)
    {
        AppendMessage(Message);
    }
}

TSharedRef<ITableRow> SUBotPackageManager::GenerateRow(FPackageItem Item, const TSharedRef<STableViewBase>& OwnerTable)
{
    return SNew(SUBotPackageRow, OwnerTable).Package(Item);
}

void SUBotPackageManager::HandleSelectionChanged(FPackageItem Item, ESelectInfo::Type SelectInfo)
{
    SelectedPackageName = Item.IsValid() ? Item->Name : FString();
    RebuildDetails();
}

const FUBotPackageInfo* SUBotPackageManager::GetSelectedPackage() const
{
    if (SelectedPackageName.IsEmpty())
    {
        return nullptr;
    }
    for (const FPackageItem& Item : Packages)
    {
        if (Item.IsValid() && Item->Name.Equals(SelectedPackageName, ESearchCase::IgnoreCase))
        {
            return Item.Get();
        }
    }
    return nullptr;
}

FReply SUBotPackageManager::HandleRefreshClicked()
{
    RefreshPackages();
    AppendMessage(TEXT("Package list refreshed."));
    return FReply::Handled();
}

FReply SUBotPackageManager::HandleRestartClicked()
{
    const EAppReturnType::Type Answer = FMessageDialog::Open(
        EAppMsgType::YesNo,
        LOCTEXT("RestartPrompt", "Restart the editor now to apply the package changes?\n\nYou will be asked to save unsaved changes first."),
        LOCTEXT("RestartTitle", "Restart Editor"));
    if (Answer == EAppReturnType::Yes)
    {
        FUnrealEdMisc::Get().RestartEditor(false);
    }
    return FReply::Handled();
}

FReply SUBotPackageManager::HandleCancelClicked()
{
    FUBotPackageService::Get().CancelActiveOperation();
    AppendMessage(TEXT("Cancelling the running git command..."));
    return FReply::Handled();
}

FReply SUBotPackageManager::HandleInstallClicked()
{
    const FUBotPackageInfo* Package = GetSelectedPackage();
    if (!Package)
    {
        return FReply::Handled();
    }

    const FString PackageName = Package->Name;
    const FText Prompt = FText::Format(
        LOCTEXT("InstallPrompt", "Install {0} and its missing required packages?\n\nRepositories listed in the package index are cloned with git into:\n{1}"),
        FText::FromString(PackageName),
        FText::FromString(FUBotPackageService::GetInstallDirectory()));
    if (FMessageDialog::Open(EAppMsgType::YesNo, Prompt, LOCTEXT("InstallTitle", "Install uBot Package")) != EAppReturnType::Yes)
    {
        return FReply::Handled();
    }

    AppendMessage(FString::Printf(TEXT("Installing %s ..."), *PackageName));
    const TWeakPtr<SUBotPackageManager> WeakSelf = SharedThis(this);
    FUBotPackageService::Get().InstallPackageAsync(PackageName, [WeakSelf, PackageName](bool bSuccess, const TArray<FString>& Messages)
    {
        if (const TSharedPtr<SUBotPackageManager> Self = WeakSelf.Pin())
        {
            Self->HandleAsyncOperationComplete(bSuccess, Messages, TEXT("Install"), PackageName);
        }
        else
        {
            for (const FString& Message : Messages)
            {
                UE_LOG(LogUBot, Log, TEXT("uBot packages: %s"), *Message);
            }
        }
    });
    return FReply::Handled();
}

FReply SUBotPackageManager::HandleUpdateClicked()
{
    const FUBotPackageInfo* Package = GetSelectedPackage();
    if (!Package)
    {
        return FReply::Handled();
    }

    const FString PackageName = Package->Name;
    AppendMessage(FString::Printf(TEXT("Updating %s ..."), *PackageName));
    const TWeakPtr<SUBotPackageManager> WeakSelf = SharedThis(this);
    FUBotPackageService::Get().UpdatePackageAsync(PackageName, [WeakSelf, PackageName](bool bSuccess, const TArray<FString>& Messages)
    {
        if (const TSharedPtr<SUBotPackageManager> Self = WeakSelf.Pin())
        {
            Self->HandleAsyncOperationComplete(bSuccess, Messages, TEXT("Update"), PackageName);
        }
        else
        {
            for (const FString& Message : Messages)
            {
                UE_LOG(LogUBot, Log, TEXT("uBot packages: %s"), *Message);
            }
        }
    });
    return FReply::Handled();
}

FReply SUBotPackageManager::HandleEnableClicked()
{
    const FUBotPackageInfo* Package = GetSelectedPackage();
    if (!Package)
    {
        return FReply::Handled();
    }

    const FString PackageName = Package->Name;
    TArray<FString> Messages;
    FUBotPackageService::Get().EnablePackage(PackageName, Messages);
    AppendMessages(Messages);
    RebuildPackageList(/*bReportIndexErrors*/ false);
    return FReply::Handled();
}

FReply SUBotPackageManager::HandleDisableClicked()
{
    const FUBotPackageInfo* Package = GetSelectedPackage();
    if (!Package)
    {
        return FReply::Handled();
    }

    const FString PackageName = Package->Name;
    FUBotPackageService& Service = FUBotPackageService::Get();
    const TArray<FString> Dependents = Service.GetEnabledDependents(PackageName);

    bool bForce = false;
    if (Dependents.Num() > 0)
    {
        const FText Prompt = FText::Format(
            LOCTEXT("DisablePrompt", "These enabled packages depend on {0} and will be disabled too:\n\n{1}\n\nContinue?"),
            FText::FromString(PackageName),
            FText::FromString(FString::Join(Dependents, TEXT("\n"))));
        if (FMessageDialog::Open(EAppMsgType::YesNo, Prompt, LOCTEXT("DisableTitle", "Disable uBot Package")) != EAppReturnType::Yes)
        {
            return FReply::Handled();
        }
        bForce = true;
    }

    TArray<FString> Messages;
    Service.DisablePackage(PackageName, bForce, Messages);
    AppendMessages(Messages);
    RebuildPackageList(/*bReportIndexErrors*/ false);
    return FReply::Handled();
}

FReply SUBotPackageManager::HandleOpenFolderClicked()
{
    if (const FUBotPackageInfo* Package = GetSelectedPackage())
    {
        const FString BaseDir = UBotPackageManagerPrivate::GetFullBaseDir(*Package);
        if (!BaseDir.IsEmpty())
        {
            FPlatformProcess::ExploreFolder(*BaseDir);
        }
    }
    return FReply::Handled();
}

FReply SUBotPackageManager::HandleOpenRepositoryClicked()
{
    if (const FUBotPackageInfo* Package = GetSelectedPackage())
    {
        const FString Url = Package->Repository.TrimStartAndEnd();
        // Only web URLs are handed to the OS so no other protocol handler can be triggered.
        if (UBotPackageManagerPrivate::IsWebUrl(Url))
        {
            FString Error;
            FPlatformProcess::LaunchURL(*Url, nullptr, &Error);
            if (!Error.IsEmpty())
            {
                AppendMessage(FString::Printf(TEXT("Could not open %s: %s"), *FUBotPackageService::RedactUrlCredentials(Url), *Error));
            }
        }
    }
    return FReply::Handled();
}

bool SUBotPackageManager::CanRestart() const
{
    return FUBotPackageService::Get().IsRestartRequired() && !FUBotPackageService::Get().IsBusy();
}

bool SUBotPackageManager::CanInstall() const
{
    const FUBotPackageInfo* Package = GetSelectedPackage();
    return Package && !Package->bInstalled && Package->bFromIndex && !Package->Repository.IsEmpty()
        && !FUBotPackageService::Get().IsBusy();
}

bool SUBotPackageManager::CanUpdate() const
{
    const FUBotPackageInfo* Package = GetSelectedPackage();
    return Package && Package->bInstalled && bSelectionIsGitWorkingCopy && !FUBotPackageService::Get().IsBusy();
}

bool SUBotPackageManager::CanEnable() const
{
    const FUBotPackageInfo* Package = GetSelectedPackage();
    return Package && Package->bInstalled && !UBotPackageManagerPrivate::IsEffectivelyEnabled(*Package)
        && !FUBotPackageService::Get().IsBusy();
}

bool SUBotPackageManager::CanDisable() const
{
    const FUBotPackageInfo* Package = GetSelectedPackage();
    return Package && Package->bInstalled && UBotPackageManagerPrivate::IsEffectivelyEnabled(*Package)
        && !FUBotPackageService::Get().IsBusy();
}

bool SUBotPackageManager::CanOpenFolder() const
{
    const FUBotPackageInfo* Package = GetSelectedPackage();
    return Package && Package->bInstalled && !Package->BaseDir.IsEmpty();
}

bool SUBotPackageManager::CanOpenRepository() const
{
    const FUBotPackageInfo* Package = GetSelectedPackage();
    return Package && UBotPackageManagerPrivate::IsWebUrl(Package->Repository.TrimStartAndEnd());
}

EVisibility SUBotPackageManager::GetBusyVisibility() const
{
    return FUBotPackageService::Get().IsBusy() ? EVisibility::Visible : EVisibility::Collapsed;
}

FText SUBotPackageManager::GetStatusText() const
{
    const FUBotPackageService& Service = FUBotPackageService::Get();
    if (Service.IsBusy())
    {
        return FText::FromString(Service.GetBusyDescription() + TEXT(" ..."));
    }
    if (Service.IsRestartRequired())
    {
        return LOCTEXT("StatusRestartRequired", "Restart the editor to apply package changes.");
    }
    return FText::Format(LOCTEXT("StatusPackageCount", "{0} packages"), FText::AsNumber(Packages.Num()));
}

void SUBotPackageManager::HandleAsyncOperationComplete(bool bSuccess, const TArray<FString>& Messages, const FString& Action, const FString& PackageName)
{
    AppendMessages(Messages);
    AppendMessage(FString::Printf(TEXT("%s of %s %s."), *Action, *PackageName, bSuccess ? TEXT("finished") : TEXT("failed")));
    RebuildPackageList(/*bReportIndexErrors*/ false);
}

#undef LOCTEXT_NAMESPACE
