#pragma once

#include "CoreMinimal.h"

/**
 * Fixed-rate sample scheduler driven by the caller's clock (normally uBot sim time), polled once
 * per tick. It replaces looping FTimerManager timers for sensors: a looping timer fires several
 * times in one frame when the frame time exceeds its interval, producing duplicate timestamps.
 *
 * Due times lie on the grid FirstDueSeconds + k / RateHz and are computed from the index rather
 * than accumulated, so the phase does not drift over long runs.
 */
struct UBOTCORE_API FUBotSampleScheduler
{
public:
    static constexpr double MinRateHz = 0.01;
    static constexpr double MaxRateHz = 10000.0;

    /**
     * Rate is clamped to [0.01, 10000] Hz (NaN becomes the minimum). FirstDueSeconds is when the
     * first sample becomes due. Clears the dropped-sample counter. A non-finite FirstDueSeconds
     * leaves the scheduler uninitialized.
     */
    void Reset(double RateHz, double FirstDueSeconds);

    /** Keeps the current phase: NextDue is unchanged and later samples follow the new interval. */
    void SetRateHz(double RateHz);

    /**
     * Returns true at most once per call. When true, NextDue advances by whole intervals until it is
     * strictly greater than NowSeconds; every interval skipped beyond the first increments the
     * dropped-sample counter. Returns false if NowSeconds < NextDue, if NowSeconds is not finite or
     * if the scheduler is not initialized.
     */
    bool ConsumeDue(double NowSeconds);

    double GetRateHz() const { return RateHz; }
    double GetIntervalSeconds() const { return IntervalSeconds; }
    double GetNextDueSeconds() const { return NextDueSeconds; }
    int64 GetDroppedSampleCount() const { return DroppedSampleCount; }
    bool IsInitialized() const { return bInitialized; }

private:
    void ApplyRate(double InRateHz);
    double DueTimeForIndex(int64 Index) const;

    double RateHz = 1.0;
    double IntervalSeconds = 1.0;
    /** Grid origin: due time of index 0. Moves only on Reset, SetRateHz or a precision restart. */
    double AnchorSeconds = 0.0;
    /** Grid index of NextDueSeconds. */
    int64 NextIndex = 0;
    double NextDueSeconds = 0.0;
    int64 DroppedSampleCount = 0;
    bool bInitialized = false;
};
