#pragma once

#include "CoreMinimal.h"
#include "Input/Reply.h"
#include "UBotPackageTypes.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class ITableRow;
class SMultiLineEditableTextBox;
class STableViewBase;
class SVerticalBox;

/** Package manager UI: package list, details of the selection, actions and a message log. */
class SUBotPackageManager : public SCompoundWidget
{
public:
    using FPackageItem = TSharedPtr<FUBotPackageInfo>;

    SLATE_BEGIN_ARGS(SUBotPackageManager) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    TSharedRef<SWidget> BuildToolbar();
    TSharedRef<SWidget> BuildActionButtons();

    void RefreshPackages();
    void RebuildPackageList(bool bReportIndexErrors);
    void HandlePackagesChanged();
    void HandleExtensionsChanged(FName PointName);
    void RebuildDetails();
    void AppendMessage(const FString& Message);
    void AppendMessages(const TArray<FString>& Messages);

    TSharedRef<ITableRow> GenerateRow(FPackageItem Item, const TSharedRef<STableViewBase>& OwnerTable);
    void HandleSelectionChanged(FPackageItem Item, ESelectInfo::Type SelectInfo);
    const FUBotPackageInfo* GetSelectedPackage() const;

    FReply HandleRefreshClicked();
    FReply HandleRestartClicked();
    FReply HandleCancelClicked();
    FReply HandleInstallClicked();
    FReply HandleUpdateClicked();
    FReply HandleEnableClicked();
    FReply HandleDisableClicked();
    FReply HandleOpenFolderClicked();
    FReply HandleOpenRepositoryClicked();

    bool CanRestart() const;
    bool CanInstall() const;
    bool CanUpdate() const;
    bool CanEnable() const;
    bool CanDisable() const;
    bool CanOpenFolder() const;
    bool CanOpenRepository() const;
    EVisibility GetBusyVisibility() const;
    FText GetStatusText() const;

    void HandleAsyncOperationComplete(bool bSuccess, const TArray<FString>& Messages, const FString& Action, const FString& PackageName);

    TArray<FPackageItem> Packages;
    TSharedPtr<SListView<FPackageItem>> PackageListView;
    TSharedPtr<SVerticalBox> DetailsBox;
    TSharedPtr<SMultiLineEditableTextBox> MessageLogBox;
    TArray<FString> LogLines;
    FString SelectedPackageName;
    // Cached on selection change so button state does not hit the file system every frame.
    bool bSelectionIsGitWorkingCopy = false;
    bool bRefreshingRegistry = false;
};
