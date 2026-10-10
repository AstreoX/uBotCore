#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateStyle.h"
#include "Textures/SlateIcon.h"

/**
 * Slate style set of the uBot editor ("UBotEditorStyle"), with its content root in UBotCore's
 * Resources folder. It holds the uBot mark as the 16x16 icon "UBot.TabIcon"
 * (Resources/Icons/UBotTab.svg), used by the uBot tab, its Window menu entry and its status bar entry.
 * Registered with FSlateStyleRegistry by the editor module while Slate runs.
 */
class FUBotEditorStyle final : public FSlateStyleSet
{
public:
    /** Name of the style set in FSlateStyleRegistry. */
    static const FName StyleSetName;

    /** The uBot mark, 16x16, white like the editor's own icons so the tab and menu foreground colours tint it. */
    static const FName TabIconName;

    /** Creates the style set and registers it; does nothing when it is already registered. */
    static void Register();

    /** Unregisters and releases the style set; does nothing when it is not registered. */
    static void Unregister();

    static bool IsRegistered();

    /**
     * The uBot mark as a tab and menu icon (StyleSetName, TabIconName). The icon is only the two names; the
     * brush behind it belongs to the style set and is freed by Unregister(). Keep the icon and call GetIcon()
     * when painting (SImage::Image_Lambda), never a brush pointer taken from it once.
     */
    static FSlateIcon GetTabIcon();

private:
    FUBotEditorStyle();

    static TUniquePtr<FUBotEditorStyle> Instance;
};
