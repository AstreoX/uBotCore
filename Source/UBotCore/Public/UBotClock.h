#pragma once

#include "CoreMinimal.h"

class UObject;

namespace UBot::Clock
{
    /**
     * The one time base for every uBot timestamp: UWorld::GetTimeSeconds() (game time; honours
     * pause and time dilation). Returns 0 when WorldContext has no world.
     */
    UBOTCORE_API double GetSimTimeSeconds(const UObject* WorldContext);
}
