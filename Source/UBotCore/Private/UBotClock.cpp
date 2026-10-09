#include "UBotClock.h"

#include "Engine/Engine.h"
#include "Engine/World.h"

namespace UBot::Clock
{
    double GetSimTimeSeconds(const UObject* WorldContext)
    {
        if (WorldContext == nullptr || GEngine == nullptr)
        {
            return 0.0;
        }
        const UWorld* World = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull);
        return World != nullptr ? World->GetTimeSeconds() : 0.0;
    }
}
