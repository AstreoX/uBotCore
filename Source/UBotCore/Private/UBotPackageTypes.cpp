#include "UBotPackageTypes.h"

EUBotPackageLayer ParsePackageLayer(const FString& Text)
{
    static const EUBotPackageLayer KnownLayers[] =
    {
        EUBotPackageLayer::Foundation,
        EUBotPackageLayer::Capability,
        EUBotPackageLayer::Adapter,
        EUBotPackageLayer::Content
    };

    const FString Trimmed = Text.TrimStartAndEnd();
    for (const EUBotPackageLayer Layer : KnownLayers)
    {
        if (Trimmed.Equals(LexToString(Layer), ESearchCase::IgnoreCase))
        {
            return Layer;
        }
    }
    return EUBotPackageLayer::Unknown;
}

const TCHAR* LexToString(EUBotPackageLayer Layer)
{
    switch (Layer)
    {
    case EUBotPackageLayer::Foundation:
        return TEXT("Foundation");
    case EUBotPackageLayer::Capability:
        return TEXT("Capability");
    case EUBotPackageLayer::Adapter:
        return TEXT("Adapter");
    case EUBotPackageLayer::Content:
        return TEXT("Content");
    case EUBotPackageLayer::Unknown:
    default:
        return TEXT("Unknown");
    }
}

const TCHAR* LexToString(EUBotPackageState State)
{
    switch (State)
    {
    case EUBotPackageState::Disabled:
        return TEXT("Disabled");
    case EUBotPackageState::Enabled:
        return TEXT("Enabled");
    case EUBotPackageState::NotInstalled:
    default:
        return TEXT("NotInstalled");
    }
}
