#include "Misc/AutomationTest.h"
#include "UBotSampleScheduler.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotTimelineSchedulerTest, "UBotCore.Scheduler.TimelineConsumption", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUBotTimelineSchedulerTest::RunTest(const FString& Parameters)
{
    FUBotSampleScheduler Scheduler;
    Scheduler.Reset(200, 0.005);
    double Due = -1;
    for (int32 Index = 1; Index <= 10; ++Index)
    {
        TestTrue(TEXT("A slow frame retains every due time"), Scheduler.ConsumeNextDue(0.05, Due));
        TestTrue(TEXT("Fixed grid"), FMath::Abs(Due - Index * 0.005) < 1.e-12);
    }
    TestFalse(TEXT("No duplicate or future sample"), Scheduler.ConsumeNextDue(0.05, Due));
    TestEqual(TEXT("Batching does not drop"), Scheduler.GetDroppedSampleCount(), int64(0));
    TestEqual(TEXT("Skip only expired times"), Scheduler.SkipBefore(0.1), int64(9));
    TestTrue(TEXT("Earliest retained boundary is consumed"), Scheduler.ConsumeNextDue(0.1, Due));
    TestTrue(TEXT("Boundary is 0.1"), FMath::Abs(Due - 0.1) < 1.e-12);
    TestEqual(TEXT("Expired count"), Scheduler.GetDroppedSampleCount(), int64(9));
    TestEqual(TEXT("Repeated skip is harmless"), Scheduler.SkipBefore(0.1), int64(0));
    return true;
}
#endif
