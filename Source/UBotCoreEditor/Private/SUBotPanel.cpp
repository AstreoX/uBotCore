#include "SUBotPanel.h"

#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateColor.h"
#include "UBotEditorText.h"
#include "UBotManagerLauncher.h"
#include "UBotPanelModel.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace UBotPanelPrivate
{
    constexpr float SimTimeRefreshSeconds = 0.25f;

    /** sRGB bytes to the linear colour Slate expects (the same conversion the editor style uses for its hex colours). */
    FLinearColor FromSRGB(uint8 R, uint8 G, uint8 B)
    {
        return FLinearColor::FromSRGBColor(FColor(R, G, B));
    }

    // The muted palette of the uBot panel design, instead of the saturated Slate status colours.
    FLinearColor GetOkColor() { return FromSRGB(0x4c, 0xaf, 0x50); }
    FLinearColor GetWarningColor() { return FromSRGB(0xf2, 0xb3, 0x3d); }
    FLinearColor GetIdleColor() { return FromSRGB(0x6b, 0x6b, 0x6b); }
    FLinearColor GetErrorColor() { return FromSRGB(0xe5, 0x53, 0x4b); }
    FLinearColor GetLinkColor() { return FromSRGB(0x4d, 0xa3, 0xff); }
    /** Subtle row divider: a little darker than the panel background instead of the recessed near-black. */
    FLinearColor GetRowSeparatorColor() { return FromSRGB(0x1c, 0x1c, 0x1c); }

    FSlateColor GetDotColor(EUBotRuntimeSeverity Severity)
    {
        switch (Severity)
        {
        case EUBotRuntimeSeverity::Ok:
            return GetOkColor();
        case EUBotRuntimeSeverity::Warning:
            return GetWarningColor();
        case EUBotRuntimeSeverity::Error:
            return GetErrorColor();
        case EUBotRuntimeSeverity::Info:
        default:
            return GetIdleColor();
        }
    }
}

void SUBotPanel::Construct(const FArguments& InArgs, const TSharedRef<FUBotPanelModel>& InModel)
{
    namespace Text = UBot::EditorText;

    Model = InModel;
    ModelChangedHandle = Model->OnChanged.AddSP(this, &SUBotPanel::HandleModelChanged);

    ChildSlot
    [
        SNew(SBorder)
        .BorderImage(FAppStyle::Get().GetBrush("Brushes.Panel"))
        .Padding(0.0f)
        [
            SNew(SVerticalBox)

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(8.0f)
            [
                SNew(SHorizontalBox)

                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                [
                    SNew(SButton)
                    .ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("PrimaryButton"))
                    .HAlign(HAlign_Center)
                    .VAlign(VAlign_Center)
                    .ToolTipText(Text::OpenManagerTooltip())
                    .OnClicked(this, &SUBotPanel::HandleOpenManagerClicked)
                    [
                        SNew(STextBlock)
                        .TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("PrimaryButtonText"))
                        .Text(Text::OpenManager())
                    ]
                ]

                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(6.0f, 0.0f, 0.0f, 0.0f)
                [
                    SNew(SButton)
                    .ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("Button"))
                    .ContentPadding(FMargin(6.0f, 4.0f))
                    .VAlign(VAlign_Center)
                    .ToolTipText(Text::Refresh())
                    .OnClicked(this, &SUBotPanel::HandleRefreshClicked)
                    [
                        SNew(SImage)
                        .Image(FAppStyle::Get().GetBrush("Icons.Refresh"))
                        .ColorAndOpacity(FSlateColor::UseForeground())
                    ]
                ]
            ]

            + SVerticalBox::Slot()
            .FillHeight(1.0f)
            [
                SNew(SScrollBox)

                + SScrollBox::Slot()
                [
                    MakeSection(Text::SectionPackages(), Packages)
                ]

                + SScrollBox::Slot()
                [
                    MakeSection(Text::SectionExtensionPoints(), ExtensionPoints)
                ]

                + SScrollBox::Slot()
                [
                    MakeSection(Text::SectionRuntime(), Runtime)
                ]

                + SScrollBox::Slot()
                [
                    MakeSection(Text::SectionProblems(), Problems)
                ]
            ]
        ]
    ];

    HandleModelChanged();
}

SUBotPanel::~SUBotPanel()
{
    if (Model.IsValid())
    {
        Model->OnChanged.Remove(ModelChangedHandle);
    }
}

TSharedRef<SWidget> SUBotPanel::MakeSection(const FText& Title, FSection& OutSection)
{
    return SNew(SBox)
        .Padding(FMargin(0.0f, 0.0f, 0.0f, 2.0f))
        [
            SNew(SExpandableArea)
            .BorderImage(FAppStyle::Get().GetBrush("DetailsView.CategoryTop"))
            .BodyBorderImage(FAppStyle::Get().GetBrush("NoBorder"))
            .HeaderPadding(FMargin(4.0f, 4.0f))
            .Padding(0.0f)
            .AllowAnimatedTransition(false)
            .HeaderContent()
            [
                SNew(SHorizontalBox)

                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("DetailsView.CategoryTextStyle"))
                    .Text(Title)
                ]

                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                .Padding(6.0f, 0.0f, 0.0f, 0.0f)
                [
                    SAssignNew(OutSection.Count, STextBlock)
                    .ColorAndOpacity(FSlateColor::UseSubduedForeground())
                ]
            ]
            .BodyContent()
            [
                SAssignNew(OutSection.Rows, SVerticalBox)
            ]
        ];
}

TSharedRef<SWidget> SUBotPanel::MakeRow(const FUBotPanelRow& Row)
{
    using namespace UBotPanelPrivate;

    TSharedPtr<STextBlock> ValueText;
    TSharedRef<SWidget> Widget = SNew(SVerticalBox)

        + SVerticalBox::Slot()
        .AutoHeight()
        [
            SNew(SBox)
            .MinDesiredHeight(26.0f)
            .Padding(FMargin(24.0f, 2.0f, 10.0f, 2.0f))
            .ToolTipText(Row.ToolTip.IsEmpty() ? FText::GetEmpty() : Row.ToolTip)
            [
                SNew(SHorizontalBox)

                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                .Padding(0.0f, 0.0f, 8.0f, 0.0f)
                [
                    SNew(SImage)
                    .Image(FAppStyle::Get().GetBrush("Icons.FilledCircle"))
                    .DesiredSizeOverride(FVector2D(8.0, 8.0))
                    .ColorAndOpacity(GetDotColor(Row.Dot))
                ]

                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                .VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(Row.Name)
                    .OverflowPolicy(ETextOverflowPolicy::Ellipsis)
                ]

                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                .Padding(8.0f, 0.0f, 0.0f, 0.0f)
                [
                    SAssignNew(ValueText, STextBlock)
                    .Text(Row.Value)
                    .Font(FCoreStyle::GetDefaultFontStyle("Mono", 9))
                    .ColorAndOpacity(Row.bHighlightValue ? FSlateColor(GetLinkColor()) : FSlateColor::UseSubduedForeground())
                ]
            ]
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        [
            SNew(SSeparator)
            .Orientation(Orient_Horizontal)
            .Thickness(1.0f)
            // The default separator brush is itself near-black and the tint multiplies it, so tint a white brush instead.
            .SeparatorImage(FAppStyle::Get().GetBrush("WhiteBrush"))
            .ColorAndOpacity(FSlateColor(GetRowSeparatorColor()))
        ];

    if (Row.Id == FUBotPanelModel::SimTimeRowId)
    {
        SimTimeValue = ValueText;
    }
    return Widget;
}

void SUBotPanel::FillSection(FSection& Section, const TArray<FUBotPanelRow>& Rows)
{
    Section.Count->SetText(FText::AsNumber(Rows.Num()));
    Section.Rows->ClearChildren();
    for (const FUBotPanelRow& Row : Rows)
    {
        Section.Rows->AddSlot()
            .AutoHeight()
            [
                MakeRow(Row)
            ];
    }
}

void SUBotPanel::HandleModelChanged()
{
    using namespace UBotPanelPrivate;

    const FUBotPanelModel::FSections& Sections = Model->GetSections();
    SimTimeValue.Reset();
    FillSection(Packages, Sections.Packages);
    FillSection(ExtensionPoints, Sections.ExtensionPoints);
    FillSection(Runtime, Sections.Runtime);
    FillSection(Problems, Sections.Problems);

    if (SimTimeValue.IsValid() && !SimTimeTimer.IsValid())
    {
        SimTimeTimer = RegisterActiveTimer(SimTimeRefreshSeconds, FWidgetActiveTimerDelegate::CreateSP(this, &SUBotPanel::UpdateSimTime));
    }
}

EActiveTimerReturnType SUBotPanel::UpdateSimTime(double InCurrentTime, float InDeltaTime)
{
    if (!SimTimeValue.IsValid() || !FUBotPanelModel::IsPlaying())
    {
        SimTimeTimer.Reset();
        return EActiveTimerReturnType::Stop;
    }
    SimTimeValue->SetText(FUBotPanelModel::FormatSimTime(FUBotPanelModel::GetPlaySimTimeSeconds()));
    return EActiveTimerReturnType::Continue;
}

FReply SUBotPanel::HandleOpenManagerClicked()
{
    FUBotManagerLauncher::Launch();
    return FReply::Handled();
}

FReply SUBotPanel::HandleRefreshClicked()
{
    Model->Refresh();
    return FReply::Handled();
}
