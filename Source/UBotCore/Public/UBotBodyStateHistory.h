#pragma once

#include "CoreMinimal.h"
#include "UBotBodyState.h"

/** Result of a read at a requested simulation time. No result permits extrapolation. */
enum class EUBotBodyStateQueryResult : uint8
{
    Exact,
    Interpolated,
    NotReady,
    Unavailable
};

struct FUBotBodyStateHistoryRange
{
    double OldestSeconds = 0.0;
    double NewestSeconds = 0.0;
    /** Changes when the timeline is cleared, including a time reset. */
    uint64 Generation = 0;
};

/**
 * Optional, game-thread-only, read-only view owned by a body state provider. Consumers share a
 * handle; holding it requests recording, not ownership of any actor, component or world. Releasing
 * the last handle allows the provider to stop recording. Providers invalidate surviving handles
 * when their body or world ends. Storage and interpolation belong to the provider, not Core.
 */
class UBOTCORE_API IUBotBodyStateHistory
{
public:
    IUBotBodyStateHistory();
    virtual ~IUBotBodyStateHistory();
    virtual bool GetRange(FUBotBodyStateHistoryRange& OutRange) const = 0;
    /** OutState and OutSegment are written only for Exact or Interpolated results. */
    virtual EUBotBodyStateQueryResult Query(double TimeSeconds, FUBotBodyState& OutState, uint64& OutSegment) const = 0;
};
