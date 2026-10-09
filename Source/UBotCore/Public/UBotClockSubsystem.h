#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "UBotClockSubsystem.generated.h"

/**
 * Simulation clock control for one game or PIE world: sim time and frame queries, process-wide
 * fixed time step, pause, resume and frame stepping.
 */
UCLASS()
class UBOTCORE_API UUBotClockSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    /** Clock subsystem of WorldContext's world; nullptr when there is no world or it is not a game/PIE world. */
    static UUBotClockSubsystem* Get(const UObject* WorldContext);

    //~ Begin USubsystem interface
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    //~ End USubsystem interface

    //~ Begin UWorldSubsystem interface
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;
    //~ End UWorldSubsystem interface

    //~ Begin FTickableGameObject interface
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    // Stepping has to observe paused frames to know when the world runs again.
    virtual bool IsTickableWhenPaused() const override { return true; }
    //~ End FTickableGameObject interface

    /** Same value as UBot::Clock::GetSimTimeSeconds for this world. */
    UFUNCTION(BlueprintPure, Category = "uBot|Clock")
    double GetSimTimeSeconds() const;

    /** Number of world ticks seen by this subsystem in which game time advanced (paused ticks are not counted). */
    UFUNCTION(BlueprintPure, Category = "uBot|Clock")
    int64 GetSimFrame() const;

    /**
     * Enables or disables the engine fixed time step (FApp, process global). StepHz is clamped to
     * [1, 1000]. The previous FApp values are restored when the last clock subsystem that changed
     * them deinitializes.
     */
    UFUNCTION(BlueprintCallable, Category = "uBot|Clock")
    void SetFixedTimeStep(bool bEnabled, double StepHz = 60.0);

    UFUNCTION(BlueprintPure, Category = "uBot|Clock")
    bool IsFixedTimeStepEnabled() const;

    /** Pauses the game (UGameplayStatics::SetGamePaused) and cancels any pending step. */
    UFUNCTION(BlueprintCallable, Category = "uBot|Clock")
    void PauseSimulation();

    /** Unpauses the game and cancels any pending step, so the world runs freely. */
    UFUNCTION(BlueprintCallable, Category = "uBot|Clock")
    void ResumeSimulation();

    /**
     * Unpauses for exactly NumFrames world ticks, then pauses again. Only ticks that start after the
     * call count. Ignored (returns false) if NumFrames < 1 or the world cannot be unpaused. Calling
     * while a step is running adds to the remaining frames.
     */
    UFUNCTION(BlueprintCallable, Category = "uBot|Clock")
    bool StepSimulation(int32 NumFrames = 1);

    UFUNCTION(BlueprintPure, Category = "uBot|Clock")
    bool IsSimulationPaused() const;

    UFUNCTION(BlueprintPure, Category = "uBot|Clock")
    int32 GetRemainingStepFrames() const;

protected:
    //~ Begin UWorldSubsystem interface
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
    //~ End UWorldSubsystem interface

private:
    void HandleWorldTickStart(UWorld* InWorld, ELevelTick TickType, float DeltaSeconds);

    /** Requests the pause state and returns whether the world actually reached it. */
    bool ApplyGamePaused(bool bPaused);

    void AcquireFixedTimeStepBackup();
    void ReleaseFixedTimeStepBackup();

    FDelegateHandle WorldTickStartHandle;
    int64 SimFrame = 0;
    /** World ticks started so far, paused or not. */
    int64 WorldTickCount = 0;
    /** WorldTickCount when the running step began; only later ticks count towards it. */
    int64 StepStartTickCount = 0;
    double LastObservedTimeSeconds = 0.0;
    int32 RemainingStepFrames = 0;
    bool bHoldsFixedTimeStepBackup = false;
};
