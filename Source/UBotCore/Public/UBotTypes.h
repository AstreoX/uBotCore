#pragma once

#include "CoreMinimal.h"
#include "UBotTypes.generated.h"

/** Outcome of a single sensor sample. Values match the legacy EAgentSensorSampleStatus one to one. */
UENUM(BlueprintType)
enum class EUBotSampleStatus : uint8
{
    Valid,
    WarmingUp,
    InvalidDeltaTime,
    TeleportDetected,
    Disabled,
    Error
};

/** Common header carried by every uBot sensor frame. */
USTRUCT(BlueprintType)
struct UBOTCORE_API FUBotFrameHeader
{
    GENERATED_BODY()

    // Field names are intentionally identical to the legacy FAgentSensorFrameHeader so that a
    // struct CoreRedirect needs no property redirects.

    UPROPERTY(BlueprintReadOnly, Category = "uBot|Frame")
    FString SensorId;

    UPROPERTY(BlueprintReadOnly, Category = "uBot|Frame")
    FString FrameId;

    UPROPERTY(BlueprintReadOnly, Category = "uBot|Frame")
    int64 Sequence = 0;

    /** uBot simulation time (UBot::Clock::GetSimTimeSeconds) at which the sample was taken. */
    UPROPERTY(BlueprintReadOnly, Category = "uBot|Frame")
    double TimestampSeconds = 0.0;

    UPROPERTY(BlueprintReadOnly, Category = "uBot|Frame")
    bool bValid = false;

    UPROPERTY(BlueprintReadOnly, Category = "uBot|Frame")
    EUBotSampleStatus Status = EUBotSampleStatus::Error;
};
