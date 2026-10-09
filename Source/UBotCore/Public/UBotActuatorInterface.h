#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "UBotActuatorInterface.generated.h"

class AActor;

/** Body-frame velocity request. Producers convert from their own conventions before filling it. */
USTRUCT(BlueprintType)
struct UBOTCORE_API FUBotVelocityCommand
{
    GENERATED_BODY()

    /** Body frame in UE axes (X forward, Y right, Z up). Linear in m/s. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "uBot|Actuation")
    FVector LinearVelocityMetersPerSecond = FVector::ZeroVector;

    /** Body frame in UE axes and UE rotation sense (+Z = yaw to the right). rad/s. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "uBot|Actuation")
    FVector AngularVelocityRadiansPerSecond = FVector::ZeroVector;

    /** uBot sim time at which the command was issued. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "uBot|Actuation")
    double TimestampSeconds = 0.0;

    /** Free-form origin tag, e.g. "ROS", "MCP", "Blueprint". */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "uBot|Actuation")
    FName Source;
};

UINTERFACE(BlueprintType)
class UBOTCORE_API UUBotActuator : public UInterface
{
    GENERATED_BODY()
};

/** Describes an actuator (drive, arm, gripper, ...) independently of how it is commanded. */
class UBOTCORE_API IUBotActuator
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "uBot|Actuation")
    FString GetActuatorId() const;

    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "uBot|Actuation")
    FName GetActuatorType() const;

    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "uBot|Actuation")
    bool IsActuatorReady() const;
};

UINTERFACE(BlueprintType)
class UBOTCORE_API UUBotVelocityCommandable : public UInterface
{
    GENERATED_BODY()
};

/** Capability interface for anything that accepts body velocity commands. Independent of IUBotActuator. */
class UBOTCORE_API IUBotVelocityCommandable
{
    GENERATED_BODY()

public:
    /** Returns false when the command was rejected (not ready, invalid values, ...). */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "uBot|Actuation")
    bool ApplyVelocityCommand(const FUBotVelocityCommand& Command);

    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "uBot|Actuation")
    void StopMotion();
};

namespace UBot::Actuation
{
    /** The actor itself if it implements IUBotVelocityCommandable, else its first component that does, else nullptr. */
    UBOTCORE_API UObject* FindVelocityCommandable(const AActor* Actor);
}
