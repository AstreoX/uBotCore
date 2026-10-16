#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"
#include "UBotClock.h"
#include "UBotClockSubsystem.h"
#include "UBotSampleScheduler.h"
#include "UObject/Package.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

namespace UBotClockTests
{
    // Transient game world that is ticked by hand; destroyed when the scope ends.
    class FScopedTickableWorld
    {
    public:
        UE_NONCOPYABLE(FScopedTickableWorld);

        FScopedTickableWorld()
            : World(UWorld::CreateWorld(EWorldType::Game, false))
        {
            // UWorld::Tick looks its world context up in the engine.
            if (World && GEngine)
            {
                GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            }
        }

        ~FScopedTickableWorld()
        {
            if (World)
            {
                if (GEngine)
                {
                    GEngine->DestroyWorldContext(World);
                }
                World->DestroyWorld(false);
            }
        }

        UWorld* Get() const
        {
            return World;
        }

        void Tick(float DeltaSeconds = 1.0f / 60.0f) const
        {
            World->Tick(LEVELTICK_All, DeltaSeconds);
        }

        // What UGameplayStatics::SetGamePaused ends up doing, without needing a local player controller.
        void SetPaused(bool bPaused) const
        {
            AWorldSettings* Settings = World->GetWorldSettings();
            Settings->SetPauserPlayerState(bPaused ? World->SpawnActor<APlayerState>() : nullptr);
        }

    private:
        UWorld* World = nullptr;
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotSchedulerSteadyRateTest, "UBotCore.Scheduler.SteadyRate", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotSchedulerSteadyRateTest::RunTest(const FString& Parameters)
{
    // 100 Hz sensor polled at 60 fps for one simulated second: frames are longer than the interval,
    // so every call fires exactly once and the excess is reported as dropped, never as a burst.
    {
        FUBotSampleScheduler Scheduler;
        Scheduler.Reset(100.0, 0.0);
        int32 Fires = 0;
        int32 RepeatedFires = 0;
        for (int32 Frame = 0; Frame < 60; ++Frame)
        {
            const double Now = static_cast<double>(Frame) / 60.0;
            Fires += Scheduler.ConsumeDue(Now) ? 1 : 0;
            RepeatedFires += Scheduler.ConsumeDue(Now) ? 1 : 0;
        }
        TestEqual(TEXT("60 fps: one sample per frame"), Fires, 60);
        TestEqual(TEXT("60 fps: a second call in the same frame never fires"), RepeatedFires, 0);
        // Due times 0.00 .. 0.98 fall within the polled span: 99 samples, of which 60 were taken.
        TestEqual(TEXT("60 fps: skipped intervals are counted as dropped"), Scheduler.GetDroppedSampleCount(), int64(39));
        TestEqual(TEXT("60 fps: next due follows the 100 Hz grid"), Scheduler.GetNextDueSeconds(), 0.99, 1.e-12);
    }

    // Polled at 1 kHz the scheduler delivers the full 100 samples per simulated second.
    {
        FUBotSampleScheduler Scheduler;
        Scheduler.Reset(100.0, 0.0);
        int32 Fires = 0;
        for (int32 Tick = 0; Tick < 1000; ++Tick)
        {
            Fires += Scheduler.ConsumeDue(static_cast<double>(Tick) * 0.001) ? 1 : 0;
        }
        TestEqual(TEXT("1 kHz polling: 100 samples per second"), Fires, 100);
        TestEqual(TEXT("1 kHz polling: nothing dropped"), Scheduler.GetDroppedSampleCount(), int64(0));
    }

    // Due times come from the grid index, not from accumulation, so the phase does not drift.
    {
        FUBotSampleScheduler Scheduler;
        Scheduler.Reset(100.0, 0.0);
        constexpr int32 SampleCount = 1000000;
        int32 Fires = 0;
        for (int32 Sample = 0; Sample < SampleCount; ++Sample)
        {
            // Polls land shortly after each due time, as frame times do.
            Fires += Scheduler.ConsumeDue(static_cast<double>(Sample) / 100.0 + 1.e-6) ? 1 : 0;
        }
        TestEqual(TEXT("Long run: every sample fires"), Fires, SampleCount);
        TestEqual(TEXT("Long run: nothing dropped"), Scheduler.GetDroppedSampleCount(), int64(0));
        TestEqual(TEXT("Long run: next due stays on the grid"), Scheduler.GetNextDueSeconds(), static_cast<double>(SampleCount) / 100.0, 1.e-9);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotSchedulerLongFrameTest, "UBotCore.Scheduler.LongFrame", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotSchedulerLongFrameTest::RunTest(const FString& Parameters)
{
    FUBotSampleScheduler Scheduler;
    Scheduler.Reset(100.0, 0.0);
    TestTrue(TEXT("First sample is due at FirstDueSeconds"), Scheduler.ConsumeDue(0.0));
    TestEqual(TEXT("No drops after the first sample"), Scheduler.GetDroppedSampleCount(), int64(0));

    // A single 0.5 s frame covers the 50 due times 0.01 .. 0.50: one fires, 49 are dropped.
    TestTrue(TEXT("Long frame fires"), Scheduler.ConsumeDue(0.5));
    TestEqual(TEXT("Long frame drops 49 samples"), Scheduler.GetDroppedSampleCount(), int64(49));
    TestEqual(TEXT("Next due is the first grid point after the frame"), Scheduler.GetNextDueSeconds(), 0.51, 1.e-12);
    TestFalse(TEXT("Long frame fires only once"), Scheduler.ConsumeDue(0.5));
    TestEqual(TEXT("Repeated call drops nothing more"), Scheduler.GetDroppedSampleCount(), int64(49));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotSchedulerSameTimeTest, "UBotCore.Scheduler.SameTimeFiresOnce", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotSchedulerSameTimeTest::RunTest(const FString& Parameters)
{
    FUBotSampleScheduler Scheduler;
    Scheduler.Reset(30.0, 1.0);
    TestTrue(TEXT("Reset initializes"), Scheduler.IsInitialized());
    TestFalse(TEXT("Not due before FirstDueSeconds"), Scheduler.ConsumeDue(0.5));
    TestTrue(TEXT("Due at FirstDueSeconds"), Scheduler.ConsumeDue(1.0));
    TestFalse(TEXT("Same time does not fire twice"), Scheduler.ConsumeDue(1.0));
    TestTrue(TEXT("Next due is strictly after the consumed time"), Scheduler.GetNextDueSeconds() > 1.0);

    // Exactly at the due time counts as due, and the stored due time is never reached again by the same call time.
    const double NextDue = Scheduler.GetNextDueSeconds();
    TestTrue(TEXT("Due exactly at NextDue"), Scheduler.ConsumeDue(NextDue));
    TestFalse(TEXT("Not due again at the same NextDue"), Scheduler.ConsumeDue(NextDue));
    TestFalse(TEXT("Time going backwards is not due"), Scheduler.ConsumeDue(0.0));
    TestEqual(TEXT("No drops on a steady cadence"), Scheduler.GetDroppedSampleCount(), int64(0));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotSchedulerSetRateTest, "UBotCore.Scheduler.SetRateKeepsPhase", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotSchedulerSetRateTest::RunTest(const FString& Parameters)
{
    FUBotSampleScheduler Scheduler;
    Scheduler.Reset(10.0, 0.0);
    TestTrue(TEXT("First sample"), Scheduler.ConsumeDue(0.0));
    Scheduler.ConsumeDue(0.35);
    const double PendingDue = Scheduler.GetNextDueSeconds();
    const int64 DroppedBefore = Scheduler.GetDroppedSampleCount();
    TestEqual(TEXT("Pending due on the 10 Hz grid"), PendingDue, 0.4, 1.e-12);

    Scheduler.SetRateHz(20.0);
    TestEqual(TEXT("Rate updated"), Scheduler.GetRateHz(), 20.0);
    TestEqual(TEXT("Interval updated"), Scheduler.GetIntervalSeconds(), 0.05, 1.e-15);
    TestTrue(TEXT("Next due unchanged"), Scheduler.GetNextDueSeconds() == PendingDue);
    TestEqual(TEXT("Dropped counter kept"), Scheduler.GetDroppedSampleCount(), DroppedBefore);
    TestFalse(TEXT("Not due before the kept phase"), Scheduler.ConsumeDue(0.39));
    TestTrue(TEXT("Due at the kept phase"), Scheduler.ConsumeDue(PendingDue));
    TestEqual(TEXT("Then follows the new interval"), Scheduler.GetNextDueSeconds(), 0.45, 1.e-12);

    // Before the first sample, a rate change keeps the first due time.
    Scheduler.Reset(10.0, 2.0);
    Scheduler.SetRateHz(50.0);
    TestTrue(TEXT("First due time kept"), Scheduler.GetNextDueSeconds() == 2.0);
    TestFalse(TEXT("Still not due before it"), Scheduler.ConsumeDue(1.99));
    TestTrue(TEXT("Due at it"), Scheduler.ConsumeDue(2.0));
    TestEqual(TEXT("Then 50 Hz"), Scheduler.GetNextDueSeconds(), 2.02, 1.e-12);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotSchedulerInputValidationTest, "UBotCore.Scheduler.InputValidation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotSchedulerInputValidationTest::RunTest(const FString& Parameters)
{
    const double NaN = std::numeric_limits<double>::quiet_NaN();
    const double Infinity = std::numeric_limits<double>::infinity();

    FUBotSampleScheduler Scheduler;
    TestFalse(TEXT("Default scheduler is not initialized"), Scheduler.IsInitialized());
    TestFalse(TEXT("Uninitialized scheduler never fires"), Scheduler.ConsumeDue(1.0));

    Scheduler.Reset(0.0, 0.0);
    TestEqual(TEXT("Zero rate clamps to the minimum"), Scheduler.GetRateHz(), FUBotSampleScheduler::MinRateHz);
    TestEqual(TEXT("Minimum rate interval"), Scheduler.GetIntervalSeconds(), 100.0, 1.e-9);
    Scheduler.Reset(-5.0, 0.0);
    TestEqual(TEXT("Negative rate clamps to the minimum"), Scheduler.GetRateHz(), FUBotSampleScheduler::MinRateHz);
    Scheduler.Reset(NaN, 0.0);
    TestEqual(TEXT("NaN rate falls back to the minimum"), Scheduler.GetRateHz(), FUBotSampleScheduler::MinRateHz);
    Scheduler.Reset(1.e9, 0.0);
    TestEqual(TEXT("Huge rate clamps to the maximum"), Scheduler.GetRateHz(), FUBotSampleScheduler::MaxRateHz);
    Scheduler.Reset(Infinity, 0.0);
    TestEqual(TEXT("Infinite rate clamps to the maximum"), Scheduler.GetRateHz(), FUBotSampleScheduler::MaxRateHz);
    TestEqual(TEXT("Maximum rate interval"), Scheduler.GetIntervalSeconds(), 1.e-4, 1.e-15);
    Scheduler.SetRateHz(0.0);
    TestEqual(TEXT("SetRateHz clamps too"), Scheduler.GetRateHz(), FUBotSampleScheduler::MinRateHz);

    Scheduler.Reset(10.0, 0.0);
    TestFalse(TEXT("NaN time never fires"), Scheduler.ConsumeDue(NaN));
    TestFalse(TEXT("Infinite time never fires"), Scheduler.ConsumeDue(Infinity));
    TestEqual(TEXT("Rejected times leave the schedule alone"), Scheduler.GetNextDueSeconds(), 0.0);
    TestTrue(TEXT("Still fires afterwards"), Scheduler.ConsumeDue(0.0));

    Scheduler.Reset(10.0, NaN);
    TestFalse(TEXT("Non-finite first due leaves the scheduler uninitialized"), Scheduler.IsInitialized());
    TestFalse(TEXT("And it does not fire"), Scheduler.ConsumeDue(1.0));

    // An absurd jump must terminate and still keep the at-most-once guarantee.
    Scheduler.Reset(FUBotSampleScheduler::MaxRateHz, 0.0);
    TestTrue(TEXT("First sample before the jump"), Scheduler.ConsumeDue(0.0));
    TestTrue(TEXT("Absurd jump fires once"), Scheduler.ConsumeDue(1.e300));
    TestFalse(TEXT("Absurd jump does not fire twice"), Scheduler.ConsumeDue(1.e300));
    TestTrue(TEXT("Next due is after the jump"), Scheduler.GetNextDueSeconds() > 1.e300);
    TestTrue(TEXT("Dropped samples are counted"), Scheduler.GetDroppedSampleCount() > 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotSchedulerConsumeDueStepsTest, "UBotCore.Scheduler.ConsumeDueSteps", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotSchedulerConsumeDueStepsTest::RunTest(const FString& Parameters)
{
    const double NaN = std::numeric_limits<double>::quiet_NaN();
    const double Infinity = std::numeric_limits<double>::infinity();
    constexpr double Unset = -1.0;

    // 16 Hz: the interval 0.0625 is exact in binary, so every due time below is exact too.
    {
        FUBotSampleScheduler Scheduler;
        Scheduler.Reset(16.0, 1.0);
        double First = Unset;
        TestEqual(TEXT("Nothing is due before the first due time"), Scheduler.ConsumeDueSteps(0.99, 10, &First), 0);
        TestEqual(TEXT("A call that counts nothing leaves the first due time alone"), First, Unset, 0.0);
        TestEqual(TEXT("The first due time itself counts"), Scheduler.ConsumeDueSteps(1.0, 10, &First), 1);
        TestEqual(TEXT("First due time of the first step"), First, 1.0, 0.0);
        TestEqual(TEXT("Next due follows the grid"), Scheduler.GetNextDueSeconds(), 1.0625, 0.0);
        TestEqual(TEXT("The same time is not due twice"), Scheduler.ConsumeDueSteps(1.0, 10, &First), 0);

        // Due times 1.0625, 1.125, 1.1875 and 1.25.
        TestEqual(TEXT("Four steps in one call"), Scheduler.ConsumeDueSteps(1.25, 10, &First), 4);
        TestEqual(TEXT("First due time of four steps"), First, 1.0625, 0.0);
        TestEqual(TEXT("The k-th due time is first + k * interval"), First + 3.0 * Scheduler.GetIntervalSeconds(), 1.25, 0.0);
        TestEqual(TEXT("Next due is the first grid point after the call"), Scheduler.GetNextDueSeconds(), 1.3125, 0.0);

        TestEqual(TEXT("Not due just before the grid point"), Scheduler.ConsumeDueSteps(1.3124, 10, &First), 0);
        TestEqual(TEXT("One step shortly after a grid point"), Scheduler.ConsumeDueSteps(1.3125 + 0.03, 10, &First), 1);
        TestEqual(TEXT("First due time of that step"), First, 1.3125, 0.0);
        TestEqual(TEXT("Next due after it"), Scheduler.GetNextDueSeconds(), 1.375, 0.0);
        TestEqual(TEXT("The output pointer is optional"), Scheduler.ConsumeDueSteps(1.375, 10), 1);
        TestEqual(TEXT("Nothing is dropped while the cap is not reached"), Scheduler.GetDroppedSampleCount(), int64(0));
    }

    // The cap counts the earliest due times and drops the later ones.
    {
        FUBotSampleScheduler Scheduler;
        Scheduler.Reset(16.0, 0.0);
        double First = Unset;
        // Due times 0, 0.0625, ..., 10: 161 of them.
        TestEqual(TEXT("MaxSteps caps the count"), Scheduler.ConsumeDueSteps(10.0, 5, &First), 5);
        TestEqual(TEXT("The counted steps start at the first due time"), First, 0.0, 0.0);
        TestEqual(TEXT("The remaining 156 due times are dropped"), Scheduler.GetDroppedSampleCount(), int64(156));
        TestEqual(TEXT("Next due is after the call time, not after the counted steps"), Scheduler.GetNextDueSeconds(), 10.0625, 0.0);
        TestEqual(TEXT("Nothing is left over for the same time"), Scheduler.ConsumeDueSteps(10.0, 5, &First), 0);
        TestEqual(TEXT("Counting resumes on the grid"), Scheduler.ConsumeDueSteps(10.0625, 5, &First), 1);
        TestEqual(TEXT("First due time after the drop"), First, 10.0625, 0.0);
        TestEqual(TEXT("The dropped counter keeps its value"), Scheduler.GetDroppedSampleCount(), int64(156));

        // Five due times (0 .. 0.25) against caps of 5, 4 and 1.
        Scheduler.Reset(16.0, 0.0);
        TestEqual(TEXT("A cap equal to the due times drops nothing"), Scheduler.ConsumeDueSteps(0.25, 5), 5);
        TestEqual(TEXT("No drops at the exact cap"), Scheduler.GetDroppedSampleCount(), int64(0));
        Scheduler.Reset(16.0, 0.0);
        TestEqual(TEXT("A cap one below drops one"), Scheduler.ConsumeDueSteps(0.25, 4), 4);
        TestEqual(TEXT("One dropped"), Scheduler.GetDroppedSampleCount(), int64(1));
        TestEqual(TEXT("Next due"), Scheduler.GetNextDueSeconds(), 0.3125, 0.0);
        Scheduler.Reset(16.0, 0.0);
        TestEqual(TEXT("A cap of 1"), Scheduler.ConsumeDueSteps(0.25, 1), 1);
        TestEqual(TEXT("Four dropped"), Scheduler.GetDroppedSampleCount(), int64(4));
    }

    // Calls that count nothing leave the schedule alone.
    {
        FUBotSampleScheduler Scheduler;
        double First = Unset;
        TestEqual(TEXT("An uninitialized scheduler counts nothing"), Scheduler.ConsumeDueSteps(1.0, 5, &First), 0);
        Scheduler.Reset(16.0, NaN);
        TestEqual(TEXT("A non-finite first due time leaves the scheduler uninitialized"), Scheduler.ConsumeDueSteps(1.0, 5, &First), 0);

        Scheduler.Reset(16.0, 1.0);
        TestEqual(TEXT("NaN time"), Scheduler.ConsumeDueSteps(NaN, 5, &First), 0);
        TestEqual(TEXT("Infinite time"), Scheduler.ConsumeDueSteps(Infinity, 5, &First), 0);
        TestEqual(TEXT("Time before the next due time"), Scheduler.ConsumeDueSteps(0.5, 5, &First), 0);
        TestEqual(TEXT("MaxSteps of zero"), Scheduler.ConsumeDueSteps(2.0, 0, &First), 0);
        TestEqual(TEXT("Negative MaxSteps"), Scheduler.ConsumeDueSteps(2.0, -3, &First), 0);
        TestEqual(TEXT("The first due time was never written"), First, Unset, 0.0);
        TestEqual(TEXT("Rejected calls leave next due alone"), Scheduler.GetNextDueSeconds(), 1.0, 0.0);
        TestEqual(TEXT("Rejected calls drop nothing"), Scheduler.GetDroppedSampleCount(), int64(0));
        TestEqual(TEXT("It still counts afterwards"), Scheduler.ConsumeDueSteps(1.0, 1, &First), 1);
        TestEqual(TEXT("From the untouched first due time"), First, 1.0, 0.0);
    }

    // A rate change keeps the phase and the steps follow the new interval; ConsumeDue shares the schedule.
    {
        FUBotSampleScheduler Scheduler;
        Scheduler.Reset(16.0, 0.0);
        double First = Unset;
        TestEqual(TEXT("First step"), Scheduler.ConsumeDueSteps(0.0, 1, &First), 1);
        Scheduler.SetRateHz(32.0);
        TestEqual(TEXT("Three steps at the new rate"), Scheduler.ConsumeDueSteps(0.125, 10, &First), 3);
        TestEqual(TEXT("They start at the kept phase"), First, 0.0625, 0.0);
        TestEqual(TEXT("The last one is first + 2 intervals"), First + 2.0 * Scheduler.GetIntervalSeconds(), 0.125, 0.0);

        Scheduler.Reset(16.0, 0.0);
        TestTrue(TEXT("ConsumeDue takes the first due time"), Scheduler.ConsumeDue(0.0));
        TestEqual(TEXT("ConsumeDueSteps sees it as taken"), Scheduler.ConsumeDueSteps(0.0, 5, &First), 0);
        TestEqual(TEXT("And counts from the next one"), Scheduler.ConsumeDueSteps(0.1875, 5, &First), 3);
        TestEqual(TEXT("First of those"), First, 0.0625, 0.0);
        TestFalse(TEXT("ConsumeDue sees them as taken"), Scheduler.ConsumeDue(0.1875));
    }

    // Irregular frames: a cap of 1 is exactly ConsumeDue, and without a cap the steps are the fires plus the drops.
    {
        FRandomStream Stream(20251015);
        FUBotSampleScheduler Reference;
        FUBotSampleScheduler Capped;
        FUBotSampleScheduler Uncapped;
        Reference.Reset(30.0, 0.5);
        Capped.Reset(30.0, 0.5);
        Uncapped.Reset(30.0, 0.5);

        double Now = 0.0;
        int32 Mismatches = 0;
        int32 Fires = 0;
        int64 UncappedSteps = 0;
        for (int32 Frame = 0; Frame < 2000; ++Frame)
        {
            // Frames from 1 ms to 200 ms: some shorter than the 33 ms interval, many longer.
            Now += Stream.FRandRange(0.001, 0.2);
            const bool bFired = Reference.ConsumeDue(Now);
            const int32 CappedSteps = Capped.ConsumeDueSteps(Now, 1);
            const int32 Steps = Uncapped.ConsumeDueSteps(Now, MAX_int32);
            Fires += bFired ? 1 : 0;
            UncappedSteps += Steps;
            const bool bSame = (CappedSteps == 1) == bFired && (Steps > 0) == bFired
                && Capped.GetNextDueSeconds() == Reference.GetNextDueSeconds() && Uncapped.GetNextDueSeconds() == Reference.GetNextDueSeconds()
                && Capped.GetDroppedSampleCount() == Reference.GetDroppedSampleCount();
            Mismatches += bSame ? 0 : 1;
        }
        TestEqual(TEXT("A cap of 1 behaves exactly like ConsumeDue"), Mismatches, 0);
        TestTrue(TEXT("The run did fire and drop"), Fires > 0 && Reference.GetDroppedSampleCount() > 0);
        TestEqual(TEXT("Uncapped steps are ConsumeDue's fires plus drops"), UncappedSteps, static_cast<int64>(Fires) + Reference.GetDroppedSampleCount());
        TestEqual(TEXT("An uncapped call never drops"), Uncapped.GetDroppedSampleCount(), int64(0));
    }

    // An absurd jump terminates, counts the cap from the old first due time and moves the schedule past it.
    {
        FUBotSampleScheduler Scheduler;
        Scheduler.Reset(FUBotSampleScheduler::MaxRateHz, 0.0);
        double First = Unset;
        TestEqual(TEXT("Absurd jump is capped"), Scheduler.ConsumeDueSteps(1.e300, 8, &First), 8);
        TestEqual(TEXT("From the first due time"), First, 0.0, 0.0);
        TestTrue(TEXT("The rest is dropped"), Scheduler.GetDroppedSampleCount() > 8);
        TestTrue(TEXT("Next due is after the jump"), Scheduler.GetNextDueSeconds() > 1.e300);
        TestEqual(TEXT("Absurd jump does not count twice"), Scheduler.ConsumeDueSteps(1.e300, 8, &First), 0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotClockNoWorldTest, "UBotCore.Clock.NoWorld", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotClockNoWorldTest::RunTest(const FString& Parameters)
{
    TestEqual(TEXT("Null context has sim time 0"), UBot::Clock::GetSimTimeSeconds(nullptr), 0.0);
    TestEqual(TEXT("Context without a world has sim time 0"), UBot::Clock::GetSimTimeSeconds(GetTransientPackage()), 0.0);
    TestNull(TEXT("Null context has no clock subsystem"), UUBotClockSubsystem::Get(nullptr));
    TestNull(TEXT("Context without a world has no clock subsystem"), UUBotClockSubsystem::Get(GetTransientPackage()));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotClockSimFrameTest, "UBotCore.Clock.SimFrameCounting", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotClockSimFrameTest::RunTest(const FString& Parameters)
{
    using namespace UBotClockTests;

    // GetSimFrame counts world ticks in which game time advanced; it stays frozen while paused.
    FScopedTickableWorld Scope;
    UWorld* World = Scope.Get();
    if (!TestNotNull(TEXT("Test world"), World))
    {
        return false;
    }
    UUBotClockSubsystem* Clock = UUBotClockSubsystem::Get(World);
    if (!TestNotNull(TEXT("Game worlds have a clock subsystem"), Clock))
    {
        return false;
    }

    TestEqual(TEXT("No frames before the first tick"), Clock->GetSimFrame(), static_cast<int64>(0));

    for (int32 TickIndex = 0; TickIndex < 3; ++TickIndex)
    {
        Scope.Tick();
    }
    TestEqual(TEXT("Every unpaused tick counts"), Clock->GetSimFrame(), static_cast<int64>(3));
    TestTrue(TEXT("Sim time advanced"), Clock->GetSimTimeSeconds() > 0.0);
    TestEqual(TEXT("Sim time is the world's game time"), Clock->GetSimTimeSeconds(), World->GetTimeSeconds());

    const double TimeBeforePause = Clock->GetSimTimeSeconds();
    Scope.SetPaused(true);
    TestTrue(TEXT("The subsystem reports the pause"), Clock->IsSimulationPaused());
    for (int32 TickIndex = 0; TickIndex < 3; ++TickIndex)
    {
        Scope.Tick();
    }
    TestEqual(TEXT("Paused ticks are not counted"), Clock->GetSimFrame(), static_cast<int64>(3));
    TestEqual(TEXT("Sim time is frozen while paused"), Clock->GetSimTimeSeconds(), TimeBeforePause);

    Scope.SetPaused(false);
    TestFalse(TEXT("The subsystem reports the resume"), Clock->IsSimulationPaused());
    Scope.Tick();
    TestEqual(TEXT("Counting resumes with the next unpaused tick"), Clock->GetSimFrame(), static_cast<int64>(4));
    TestTrue(TEXT("Sim time moves again"), Clock->GetSimTimeSeconds() > TimeBeforePause);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
