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
    if (!bInitialized || !FMath::IsFinite(NowSeconds) || NowSeconds < NextDueSeconds)
    {
        return false;
    }

    // The first due time fires; every further one that has passed is skipped.
    DroppedSampleCount += AdvancePastNow(NowSeconds) - 1;
    return true;
}

int32 FUBotSampleScheduler::ConsumeDueSteps(double NowSeconds, int32 MaxSteps, double* OutFirstDueSeconds)
{
    if (!bInitialized || MaxSteps < 1 || !FMath::IsFinite(NowSeconds) || NowSeconds < NextDueSeconds)
    {
        return 0;
    }

    const double FirstDueSeconds = NextDueSeconds;
    const int64 Passed = AdvancePastNow(NowSeconds);
    // The earliest due times are the ones counted; the later ones are skipped.
    const int32 Steps = static_cast<int32>(FMath::Min<int64>(Passed, MaxSteps));
    DroppedSampleCount += Passed - Steps;
    if (OutFirstDueSeconds)
    {
        *OutFirstDueSeconds = FirstDueSeconds;
    }
    return Steps;
}

int64 FUBotSampleScheduler::AdvancePastNow(double NowSeconds)
{
    using UBot::Scheduler::Private::MaxExactGridIntervals;

    const double ElapsedIntervals = (NowSeconds - AnchorSeconds) / IntervalSeconds;
    if (!FMath::IsFinite(ElapsedIntervals) || ElapsedIntervals >= MaxExactGridIntervals)
    {
        // Only reachable with absurd time values: restart the grid one interval after NowSeconds.
        const double Skipped = FMath::IsFinite(ElapsedIntervals)
            ? FMath::FloorToDouble(ElapsedIntervals) - static_cast<double>(NextIndex)
            : MaxExactGridIntervals;
        const int64 SkippedCount = static_cast<int64>(FMath::Clamp(Skipped, 0.0, MaxExactGridIntervals));
        AnchorSeconds = NowSeconds;
        NextIndex = 1;
        NextDueSeconds = FMath::Max(DueTimeForIndex(1), std::nextafter(NowSeconds, std::numeric_limits<double>::infinity()));
        return SkippedCount + 1;
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

    const int64 Passed = Index - NextIndex;
    NextIndex = Index;
    NextDueSeconds = Due;
    return Passed;
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
