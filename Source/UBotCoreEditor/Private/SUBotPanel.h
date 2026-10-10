#pragma once

#include "CoreMinimal.h"
#include "Input/Reply.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class FActiveTimerHandle;
class FUBotPanelModel;
class STextBlock;
class SVerticalBox;
struct FUBotPanelRow;

/**
 * The uBot tab: an Open uBot Manager button and the collapsible sections Loaded Packages,
 * Extension Points, Runtime and Problems, refreshed whenever the model changes. While PIE runs a
 * 0.25 s active timer keeps the sim time current.
 */
class SUBotPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SUBotPanel) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs, const TSharedRef<FUBotPanelModel>& InModel);
    virtual ~SUBotPanel() override;

private:
    struct FSection
    {
        TSharedPtr<SVerticalBox> Rows;
        TSharedPtr<STextBlock> Count;
    };

    TSharedRef<SWidget> MakeSection(const FText& Title, FSection& OutSection);
    TSharedRef<SWidget> MakeRow(const FUBotPanelRow& Row);
    void FillSection(FSection& Section, const TArray<FUBotPanelRow>& Rows);
    void HandleModelChanged();
    EActiveTimerReturnType UpdateSimTime(double InCurrentTime, float InDeltaTime);
    FReply HandleOpenManagerClicked();
    FReply HandleRefreshClicked();

    TSharedPtr<FUBotPanelModel> Model;
    FDelegateHandle ModelChangedHandle;
    FSection Packages;
    FSection ExtensionPoints;
    FSection Runtime;
    FSection Problems;
    TSharedPtr<STextBlock> SimTimeValue;
    TSharedPtr<FActiveTimerHandle> SimTimeTimer;
};
