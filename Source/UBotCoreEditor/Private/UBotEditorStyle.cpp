#include "UBotEditorStyle.h"

#include "Brushes/SlateImageBrush.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Styling/SlateStyleMacros.h"
#include "Styling/SlateStyleRegistry.h"

const FName FUBotEditorStyle::StyleSetName(TEXT("UBotEditorStyle"));
const FName FUBotEditorStyle::TabIconName(TEXT("UBot.TabIcon"));
TUniquePtr<FUBotEditorStyle> FUBotEditorStyle::Instance;

FUBotEditorStyle::FUBotEditorStyle()
    : FSlateStyleSet(StyleSetName)
{
    // This module ships in UBotCore, so the plugin is known whenever the module is loaded.
    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("UBotCore"));
    if (Plugin.IsValid())
    {
        SetContentRoot(FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources")));
    }

    const FVector2D Icon16x16(16.0, 16.0);
    Set(TabIconName, new IMAGE_BRUSH_SVG(TEXT("Icons/UBotTab"), Icon16x16));
}

void FUBotEditorStyle::Register()
{
    if (Instance.IsValid())
    {
        return;
    }
    Instance = TUniquePtr<FUBotEditorStyle>(new FUBotEditorStyle());
    FSlateStyleRegistry::RegisterSlateStyle(*Instance);
}

void FUBotEditorStyle::Unregister()
{
    if (!Instance.IsValid())
    {
        return;
    }
    FSlateStyleRegistry::UnRegisterSlateStyle(*Instance);
    Instance.Reset();
}

bool FUBotEditorStyle::IsRegistered()
{
    return Instance.IsValid();
}

FSlateIcon FUBotEditorStyle::GetTabIcon()
{
    return FSlateIcon(StyleSetName, TabIconName);
}
