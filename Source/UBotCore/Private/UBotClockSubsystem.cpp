#include "UBotClockSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "UBotClock.h"
#include "UBotCore.h"
#include "UBotCoreSettings.h"

namespace UBot::Clock::Private
{
    constexpr double MinFixedTimeStepHz = 1.0;
    constexpr double MaxFixedTimeStepHz = 1000.0;

    // FApp's fixed time step is process global while clock subsystems are per world (several PIE
    // worlds can coexist), so the original values are captured by the first subsystem that changes
    // them and restored by the last one to release them. Game thread only.
    struct FFixedTimeStepBackup
    {
        bool bUseFixedTimeStep = false;
        double FixedDeltaTime = 0.0;
        int32 HolderCount = 0;
    };

    FFixedTimeStepBackup& GetFixedTimeStepBackup()
    {
        static FFixedTimeStepBackup Backup;
        return Backup;
    }
}

UUBotClockSubsystem* UUBotClockSubsystem::Get(const UObject* WorldContext)
{
    if (WorldContext == nullptr || GEngine == nullptr)
    {
        return nullptr;
    }
    const UWorld* World = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull);
    return World != nullptr ? World->GetSubsystem<UUBotClockSubsystem>() : nullptr;
}

void UUBotClockSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    SimFrame = 0;
    WorldTickCount = 0;
    StepStartTickCount = 0;
    RemainingStepFrames = 0;
    LastObservedTimeSeconds = GetWorldRef().GetTimeSeconds();
    WorldTickStartHandle = FWorldDelegates::OnWorldTickStart.AddUObject(this, &UUBotClockSubsystem::HandleWorldTickStart);
}

void UUBotClockSubsystem::Deinitialize()
{
    FWorldDelegates::OnWorldTickStart.Remove(WorldTickStartHandle);
    WorldTickStartHandle.Reset();
    RemainingStepFrames = 0;
    ReleaseFixedTimeStepBackup();

    Super::Deinitialize();
}

void UUBotClockSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);

    const UUBotCoreSettings* Settings = GetDefault<UUBotCoreSettings>();
    if (Settings != nullptr && Settings->bUseFixedTimeStep)
    {
        SetFixedTimeStep(true, Settings->FixedTimeStepHz);
    }
}

void UUBotClockSubsystem::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    const UWorld* World = GetWorld();
    if (World == nullptr)
    {
        return;
    }

    // UWorld::Tick advances game time only when the world was unpaused at the start of the frame.
    // Testing the time rather than IsPaused() keeps a frame from counting when StepSimulation
    // unpaused the world earlier in this same, still paused, frame.
    const double TimeSeconds = World->GetTimeSeconds();
    if (TimeSeconds == LastObservedTimeSeconds)
    {
        return;
    }
    LastObservedTimeSeconds = TimeSeconds;
    ++SimFrame;

    // A step requested partway through a running frame must not consume that frame.
    if (RemainingStepFrames > 0 && WorldTickCount > StepStartTickCount)
    {
        --RemainingStepFrames;
        if (RemainingStepFrames == 0)
        {
            ApplyGamePaused(true);
        }
    }
}

void UUBotClockSubsystem::HandleWorldTickStart(UWorld* InWorld, ELevelTick TickType, float DeltaSeconds)
{
    if (InWorld == GetWorld())
    {
        ++WorldTickCount;
    }
}

TStatId UUBotClockSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UUBotClockSubsystem, STATGROUP_Tickables);
}

double UUBotClockSubsystem::GetSimTimeSeconds() const
{
    return UBot::Clock::GetSimTimeSeconds(GetWorld());
}

int64 UUBotClockSubsystem::GetSimFrame() const
{
    return SimFrame;
}

void UUBotClockSubsystem::SetFixedTimeStep(bool bEnabled, double StepHz)
{
    using namespace UBot::Clock::Private;

    if (bEnabled && !(FMath::IsFinite(StepHz) && StepHz > 0.0))
    {
        UE_LOG(LogUBot, Warning, TEXT("uBot clock: ignoring fixed time step request with invalid rate %f Hz."), StepHz);
        return;
    }

#if !WITH_FIXED_TIME_STEP_SUPPORT
    if (bEnabled)
    {
        UE_LOG(LogUBot, Warning, TEXT("uBot clock: this build was compiled without fixed time step support; the request has no effect."));
    }
#endif

    AcquireFixedTimeStepBackup();
    if (bEnabled)
    {
        const double ClampedHz = FMath::Clamp(StepHz, MinFixedTimeStepHz, MaxFixedTimeStepHz);
        FApp::SetFixedDeltaTime(1.0 / ClampedHz);
        FApp::SetUseFixedTimeStep(true);
        UE_LOG(LogUBot, Log, TEXT("uBot clock: fixed time step enabled at %.3f Hz."), ClampedHz);
    }
    else
    {
        FApp::SetUseFixedTimeStep(false);
        UE_LOG(LogUBot, Log, TEXT("uBot clock: fixed time step disabled."));
    }
}

bool UUBotClockSubsystem::IsFixedTimeStepEnabled() const
{
    return FApp::UseFixedTimeStep();
}

void UUBotClockSubsystem::PauseSimulation()
{
    RemainingStepFrames = 0;
    ApplyGamePaused(true);
}

void UUBotClockSubsystem::ResumeSimulation()
{
    RemainingStepFrames = 0;
    ApplyGamePaused(false);
}

bool UUBotClockSubsystem::StepSimulation(int32 NumFrames)
{
    if (NumFrames < 1)
    {
        return false;
    }

    const UWorld* World = GetWorld();
    if (World == nullptr)
    {
        return false;
    }
    if (World->IsPaused() && !ApplyGamePaused(false))
    {
        return false;
    }

    if (RemainingStepFrames == 0)
    {
        StepStartTickCount = WorldTickCount;
    }
    RemainingStepFrames =static_cast<int32>(FMath::Min<int64>(static_cast<int64>(RemainingStepFrames) + NumFrames, MAX_int32));
    return true;
}

bool UUBotClockSubsystem::IsSimulationPaused() const
{
    const UWorld* World = GetWorld();
    return World != nullptr && World->IsPaused();
}

int32 UUBotClockSubsystem::GetRemainingStepFrames() const
{
    return RemainingStepFrames;
}

bool UUBotClockSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

bool UUBotClockSubsystem::ApplyGamePaused(bool bPaused)
{
    const UWorld* World = GetWorld();
    if (World == nullptr)
    {
        return false;
    }
    if (World->IsPaused() != bPaused)
    {
        UGameplayStatics::SetGamePaused(this, bPaused);
    }

    // SetGamePaused needs a local player controller and a game mode that allows pausing, and the
    // editor's PIE pause also holds the world paused, so report the state that actually resulted.
    const bool bReached = World->IsPaused() == bPaused;
    if (!bReached)
    {
        UE_LOG(LogUBot, Warning, TEXT("uBot clock: could not %s world %s (no local player controller, game mode not pausable, or paused by the editor)."),
            bPaused ? TEXT("pause") : TEXT("unpause"), *World->GetName());
    }
    return bReached;
}

void UUBotClockSubsystem::AcquireFixedTimeStepBackup()
{
    if (bHoldsFixedTimeStepBackup)
    {
        return;
    }

    UBot::Clock::Private::FFixedTimeStepBackup& Backup = UBot::Clock::Private::GetFixedTimeStepBackup();
    if (Backup.HolderCount == 0)
    {
        Backup.bUseFixedTimeStep = FApp::UseFixedTimeStep();
        Backup.FixedDeltaTime = FApp::GetFixedDeltaTime();
    }
    ++Backup.HolderCount;
    bHoldsFixedTimeStepBackup = true;
}

void UUBotClockSubsystem::ReleaseFixedTimeStepBackup()
{
    if (!bHoldsFixedTimeStepBackup)
    {
        return;
    }
    bHoldsFixedTimeStepBackup = false;

    UBot::Clock::Private::FFixedTimeStepBackup& Backup = UBot::Clock::Private::GetFixedTimeStepBackup();
    if (--Backup.HolderCount == 0)
    {
        FApp::SetUseFixedTimeStep(Backup.bUseFixedTimeStep);
        FApp::SetFixedDeltaTime(Backup.FixedDeltaTime);
        UE_LOG(LogUBot, Log, TEXT("uBot clock: restored the engine time step settings."));
    }
}
