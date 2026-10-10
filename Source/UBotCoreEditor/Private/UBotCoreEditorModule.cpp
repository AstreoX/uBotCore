#include "UBotCoreEditor.h"

#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/Docking/LayoutExtender.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/IConsoleManager.h"
#include "LevelEditor.h"
#include "Modules/ModuleManager.h"
#include "SUBotPanel.h"
#include "Styling/AppStyle.h"
#include "Textures/SlateIcon.h"
#include "ToolMenus.h"
#include "UBotCore.h"
#include "UBotEditorStyle.h"
#include "UBotEditorText.h"
#include "UBotPackageService.h"
#include "UBotPanelModel.h"
#include "UBotRuntimeStatus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

const FName FUBotCoreEditorModule::PanelTabName(TEXT("UBotPanel"));
const TCHAR* const FUBotCoreEditorModule::OpenPanelCommandName = TEXT("uBot.OpenPanel");

void FUBotCoreEditorModule::StartupModule()
{
    PostPIEStartedHandle = FEditorDelegates::PostPIEStarted.AddRaw(this, &FUBotCoreEditorModule::HandlePostPIEStarted);
    EndPIEHandle = FEditorDelegates::EndPIE.AddRaw(this, &FUBotCoreEditorModule::HandleEndPIE);

    // Registered before the Slate check so the command always exists; without Slate it only logs.
    OpenPanelCommand = IConsoleManager::Get().RegisterConsoleCommand(
        OpenPanelCommandName,
        TEXT("Opens the uBot panel (packages, extension points, runtime status and problems)."),
        FConsoleCommandDelegate::CreateLambda([]()
        {
            if (!FSlateApplication::IsInitialized())
            {
                UE_LOG(LogUBot, Warning, TEXT("uBot.OpenPanel: the editor UI is not running, so there is no panel to open."));
                return;
            }
            OpenPanel();
        }),
        ECVF_Default);

    // Commandlets and other headless runs only need the package service and the PIE sessions.
    if (!FSlateApplication::IsInitialized())
    {
        return;
    }

    UBot::EditorText::RegisterChineseText();
    FUBotEditorStyle::Register();

    PanelModel = MakeShared<FUBotPanelModel>();
    PanelModel->Initialize();

    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
            PanelTabName,
            FOnSpawnTab::CreateRaw(this, &FUBotCoreEditorModule::SpawnPanelTab))
        .SetDisplayName(UBot::EditorText::TabTitle())
        .SetTooltipText(UBot::EditorText::TabTooltip())
        .SetIcon(FUBotEditorStyle::GetTabIcon())
        // The explicit Window menu entry below replaces the automatic listing.
        .SetMenuType(ETabSpawnerMenuType::Hidden);
    bTabSpawnerRegistered = true;

    // Default placement: a closed tab in the Level Editor stack that holds Details, so the first open docks there.
    FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
    LayoutExtensionHandle = LevelEditorModule.OnRegisterLayoutExtensions().AddRaw(this, &FUBotCoreEditorModule::HandleRegisterLayoutExtensions);

    UToolMenus::RegisterStartupCallback(
        FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FUBotCoreEditorModule::RegisterMenus));
}

void FUBotCoreEditorModule::ShutdownModule()
{
    FEditorDelegates::PostPIEStarted.Remove(PostPIEStartedHandle);
    FEditorDelegates::EndPIE.Remove(EndPIEHandle);

    if (OpenPanelCommand != nullptr)
    {
        IConsoleManager::Get().UnregisterConsoleObject(OpenPanelCommand, /*bKeepState*/ false);
        OpenPanelCommand = nullptr;
    }

    UToolMenus::UnRegisterStartupCallback(this);
    UToolMenus::UnregisterOwner(this);

    if (LayoutExtensionHandle.IsValid())
    {
        if (FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>(TEXT("LevelEditor")))
        {
            LevelEditorModule->OnRegisterLayoutExtensions().Remove(LayoutExtensionHandle);
        }
        LayoutExtensionHandle.Reset();
    }

    if (bTabSpawnerRegistered && FSlateApplication::IsInitialized())
    {
        FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(PanelTabName);
    }
    bTabSpawnerRegistered = false;

    // Only after the tab spawner and the menus that show its icon are gone.
    FUBotEditorStyle::Unregister();

    if (PanelModel.IsValid())
    {
        PanelModel->Shutdown();
        PanelModel.Reset();
    }

    FUBotPackageService::Get().Shutdown();
}

void FUBotCoreEditorModule::OpenPanel()
{
    if (!FSlateApplication::IsInitialized())
    {
        return;
    }

    // Invoked through the Level Editor's tab manager the tab opens where its layout puts it (the closed
    // tab next to Details, or wherever the user moved it). The global tab manager has no layout for it
    // and would open a floating window, so it is only the fallback while there is no Level Editor.
    if (FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>(TEXT("LevelEditor")))
    {
        if (const TSharedPtr<FTabManager> LevelEditorTabManager = LevelEditorModule->GetLevelEditorTabManager())
        {
            LevelEditorTabManager->TryInvokeTab(FTabId(PanelTabName));
            return;
        }
    }
    FGlobalTabmanager::Get()->TryInvokeTab(FTabId(PanelTabName));
}

void FUBotCoreEditorModule::HandleRegisterLayoutExtensions(FLayoutExtender& Extender)
{
    // Skipped by the layout code when a saved layout already places the tab.
    Extender.ExtendLayout(
        LevelEditorTabIds::LevelEditorSelectionDetails,
        ELayoutExtensionPosition::After,
        FTabManager::FTab(FTabId(PanelTabName), ETabState::ClosedTab));
}

void FUBotCoreEditorModule::RegisterMenus()
{
    FToolMenuOwnerScoped OwnerScoped(this);

    UToolMenus* ToolMenus = UToolMenus::Get();
    if (!ToolMenus)
    {
        return;
    }

    if (UToolMenu* WindowMenu = ToolMenus->ExtendMenu("LevelEditor.MainMenu.Window"))
    {
        FToolMenuSection& Section = WindowMenu->FindOrAddSection("uBot", UBot::EditorText::TabTitle());
        Section.AddMenuEntry(
            "OpenUBotPanel",
            UBot::EditorText::TabTitle(),
            UBot::EditorText::OpenPanelTooltip(),
            FUBotEditorStyle::GetTabIcon(),
            FUIAction(FExecuteAction::CreateStatic(&FUBotCoreEditorModule::OpenPanel)));
    }

    if (UToolMenu* StatusBar = ToolMenus->ExtendMenu("LevelEditor.StatusBar.ToolBar"))
    {
        FToolMenuSection& Section = StatusBar->AddSection("uBot", FText::GetEmpty());
        Section.AddEntry(FToolMenuEntry::InitWidget("UBotStatus", MakeStatusBarEntry(), FText::GetEmpty(), /*bNoIndent*/ true, /*bSearchable*/ false));
    }
}

TSharedRef<SDockTab> FUBotCoreEditorModule::SpawnPanelTab(const FSpawnTabArgs& Args)
{
    return SNew(SDockTab)
        .TabRole(ETabRole::NomadTab)
        .Label(UBot::EditorText::TabTitle())
        [
            SNew(SUBotPanel, PanelModel.ToSharedRef())
        ];
}

TSharedRef<SWidget> FUBotCoreEditorModule::MakeStatusBarEntry()
{
    const TWeakPtr<FUBotPanelModel> WeakModel = PanelModel;
    // The FSlateIcon holds the style set's and the brush's names, not the brush. The image looks the brush up
    // again whenever it is painted, so it can never keep a brush of the style set after Unregister() freed it
    // (a hot unload or reload of this module while the editor runs).
    const FSlateIcon Icon = FUBotEditorStyle::GetTabIcon();
    return SNew(SButton)
        .ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton"))
        .ContentPadding(FMargin(6.0f, 0.0f))
        .VAlign(VAlign_Center)
        .ToolTipText(UBot::EditorText::OpenPanelTooltip())
        .OnClicked_Lambda([]()
        {
            OpenPanel();
            return FReply::Handled();
        })
        [
            SNew(SHorizontalBox)

            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            .Padding(0.0f, 0.0f, 4.0f, 0.0f)
            [
                SNew(SImage)
                .Image_Lambda([Icon]()
                {
                    return Icon.GetIcon();
                })
                .ColorAndOpacity(FSlateColor::UseSubduedForeground())
            ]

            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .ColorAndOpacity(FSlateColor::UseSubduedForeground())
                .Text_Lambda([WeakModel]()
                {
                    const TSharedPtr<FUBotPanelModel> Model = WeakModel.Pin();
                    return Model.IsValid() ? Model->GetStatusBarText() : FText::GetEmpty();
                })
            ]
        ];
}

void FUBotCoreEditorModule::HandlePostPIEStarted(bool bIsSimulating)
{
    FUBotRuntimeStatus::Get().BeginSession(EUBotSessionKind::PIE);
}

void FUBotCoreEditorModule::HandleEndPIE(bool bIsSimulating)
{
    // EndPIE is also broadcast when a PIE session fails to start, without PostPIEStarted.
    FUBotRuntimeStatus& RuntimeStatus = FUBotRuntimeStatus::Get();
    const FUBotSessionInfo* Session = RuntimeStatus.GetActiveSession();
    if (Session != nullptr && Session->Kind == EUBotSessionKind::PIE)
    {
        RuntimeStatus.EndSession();
    }
}

IMPLEMENT_MODULE(FUBotCoreEditorModule, UBotCoreEditor)
