#include "UBotSampleScheduler.h"

#include <cmath>
#include <limits>

namespace UBot::Scheduler::Private
{
    // Beyond 2^52 intervals from the anchor, Anchor + Index * Interval no longer resolves single
    // intervals in double precision.
    constexpr double MaxExactGridIntervals = 4503599627370496.0;
}

void FUBotSampleScheduler::Reset(double InRateHz, double FirstDueSeconds)
{
    ApplyRate(InRateHz);
    DroppedSampleCount = 0;
    NextIndex = 0;
    bInitialized = FMath::IsFinite(FirstDueSeconds);
    AnchorSeconds = bInitialized ? FirstDueSeconds : 0.0;
    NextDueSeconds = AnchorSeconds;
}

void FUBotSampleScheduler::SetRateHz(double InRateHz)
{
    ApplyRate(InRateHz);
    // Restart the grid at the pending due time so the phase is kept.
    AnchorSeconds = NextDueSeconds;
    NextIndex = 0;
}

bool FUBotSampleScheduler::ConsumeDue(double NowSeconds)
{
    using UBot::Scheduler::Private::MaxExactGridIntervals;

    if (!bInitialized || !FMath::IsFinite(NowSeconds) || NowSeconds < NextDueSeconds)
    {
        return false;
    }

    const double ElapsedIntervals = (NowSeconds - AnchorSeconds) / IntervalSeconds;
    if (!FMath::IsFinite(ElapsedIntervals) || ElapsedIntervals >= MaxExactGridIntervals)
    {
        // Only reachable with absurd time values: restart the grid one interval after NowSeconds.
        const double Skipped = FMath::IsFinite(ElapsedIntervals)
            ? FMath::FloorToDouble(ElapsedIntervals) - static_cast<double>(NextIndex)
            : MaxExactGridIntervals;
        DroppedSampleCount += static_cast<int64>(FMath::Clamp(Skipped, 0.0, MaxExactGridIntervals));
        AnchorSeconds = NowSeconds;
        NextIndex = 1;
        NextDueSeconds = FMath::Max(DueTimeForIndex(1), std::nextafter(NowSeconds, std::numeric_limits<double>::infinity()));
        return true;
    }

    // Smallest grid index whose due time is strictly after NowSeconds. The floor estimate can be off
    // by one through rounding, so it is corrected against the due times themselves. The value that
    // passed the comparison is the one stored, so a repeated call with the same time cannot fire.
    int64 Index = FMath::Max(static_cast<int64>(FMath::FloorToDouble(ElapsedIntervals)) + 1, NextIndex + 1);
    double Due = DueTimeForIndex(Index);
    while (Due <= NowSeconds)
    {
        ++Index;
        Due = DueTimeForIndex(Index);
    }
    while (Index - 1 > NextIndex)
    {
        const double EarlierDue = DueTimeForIndex(Index - 1);
        if (EarlierDue <= NowSeconds)
        {
            break;
        }
        --Index;
        Due = EarlierDue;
    }

    DroppedSampleCount += Index - NextIndex - 1;
    NextIndex = Index;
    NextDueSeconds = Due;
    return true;
}

void FUBotSampleScheduler::ApplyRate(double InRateHz)
{
    RateHz = FMath::IsNaN(InRateHz) ? MinRateHz : FMath::Clamp(InRateHz, MinRateHz, MaxRateHz);
    IntervalSeconds = 1.0 / RateHz;
}

double FUBotSampleScheduler::DueTimeForIndex(int64 Index) const
{
    return AnchorSeconds + static_cast<double>(Index) * IntervalSeconds;
}
