#include "UBotEditorText.h"

#include "Internationalization/PolyglotTextData.h"
#include "Internationalization/TextLocalizationManager.h"

#define LOCTEXT_NAMESPACE "UBotEditor"

namespace UBot::EditorText
{
#define UBOT_DEFINE_EDITOR_TEXT(Key, English, Chinese) FText Key() { return LOCTEXT(#Key, English); }
    UBOT_EDITOR_TEXTS(UBOT_DEFINE_EDITOR_TEXT)
#undef UBOT_DEFINE_EDITOR_TEXT

    void RegisterChineseText()
    {
        TArray<FPolyglotTextData> Translations;
#define UBOT_ADD_EDITOR_TRANSLATION(Key, English, Chinese) \
        Translations.Emplace(ELocalizedTextSourceCategory::Editor, TEXT(LOCTEXT_NAMESPACE), TEXT(#Key), TEXT(English), TEXT("en")); \
        Translations.Last().AddLocalizedString(TEXT("zh-Hans"), TEXT(Chinese));
        UBOT_EDITOR_TEXTS(UBOT_ADD_EDITOR_TRANSLATION)
#undef UBOT_ADD_EDITOR_TRANSLATION

        FTextLocalizationManager::Get().RegisterPolyglotTextData(Translations);
    }
}

#undef LOCTEXT_NAMESPACE
