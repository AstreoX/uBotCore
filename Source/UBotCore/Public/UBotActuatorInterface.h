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

    /** Body frame in UE axes (X forward, Y right, Z up). Linear in m/s. Receivers whose body tilts (multirotors) read it in the heading frame: X and Y along the current heading, Z up. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actuation")
    FVector LinearVelocityMetersPerSecond = FVector::ZeroVector;

    /** Body frame in UE axes and UE rotation sense (+Z = yaw to the right). rad/s. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actuation")
    FVector AngularVelocityRadiansPerSecond = FVector::ZeroVector;

    /** uBot sim time at which the command was issued. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actuation")
    double TimestampSeconds = 0.0;

    /** Free-form origin tag, e.g. "ROS", "MCP", "Blueprint". */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actuation")
    FName Source;
};

/** Attitude request for an aerial vehicle (attitude plus thrust or climb rate). */
USTRUCT(BlueprintType)
struct UBOTCORE_API FUBotAttitudeCommand
{
    GENERATED_BODY()

    /** Desired attitude, body to world, UE axes. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actuation")
    FQuat Orientation = FQuat::Identity;

    /** Turn the heading at Yaw Rate instead of following the yaw of Orientation. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actuation")
    bool bUseYawRate = false;

    /** Yaw rate when Use Yaw Rate is set, rad/s, UE rotation sense (+ turns right). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actuation")
    double YawRateRadiansPerSecond = 0.0;

    /** Use Normalized Thrust. Otherwise the receiver sets the thrust itself to fly Vertical Velocity. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actuation")
    bool bUseThrust = false;

    /** Collective thrust as a share of the vehicle's maximum, 0..1. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actuation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    double NormalizedThrust = 0.0;

    /** Climb rate when Use Thrust is not set, m/s (+ up). 0 holds the altitude. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actuation")
    double VerticalVelocityMetersPerSecond = 0.0;

    /** uBot sim time at which the command was issued. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actuation")
    double TimestampSeconds = 0.0;

    /** Free-form origin tag, e.g. "ROS", "MCP", "Blueprint". */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actuation")
    FName Source;
};

/** Body-rate request for an aerial vehicle (body rates plus thrust or climb rate). */
USTRUCT(BlueprintType)
struct UBOTCORE_API FUBotBodyRateCommand
{
    GENERATED_BODY()

    /** Desired angular velocity in the body frame, UE axes and UE rotation sense (+Z = yaw to the right), rad/s. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actuation")
    FVector BodyRateRadiansPerSecond = FVector::ZeroVector;

    /** Use Normalized Thrust. Otherwise the receiver sets the thrust itself to fly Vertical Velocity. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actuation")
    bool bUseThrust = false;

    /** Collective thrust as a share of the vehicle's maximum, 0..1. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actuation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    double NormalizedThrust = 0.0;

    /** Climb rate when Use Thrust is not set, m/s (+ up). 0 holds the altitude. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actuation")
    double VerticalVelocityMetersPerSecond = 0.0;

    /** uBot sim time at which the command was issued. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actuation")
    double TimestampSeconds = 0.0;

    /** Free-form origin tag, e.g. "ROS", "MCP", "Blueprint". */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actuation")
    FName Source;
};

/** Meaning of the values of a motor command. Append new values at the end. */
UENUM(BlueprintType)
enum class EUBotMotorCommandType : uint8
{
    /** Share of each motor's maximum, 0..1. */
    Normalized,
    /** Motor speed, rad/s. */
    AngularVelocity
};

/** One value per motor, in the receiver's motor order. */
USTRUCT(BlueprintType)
struct UBOTCORE_API FUBotMotorCommand
{
    GENERATED_BODY()

    /** What the values mean. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actuation")
    EUBotMotorCommandType Type = EUBotMotorCommandType::Normalized;

    /** One value per motor. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actuation")
    TArray<double> Values;

    /** uBot sim time at which the command was issued. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actuation")
    double TimestampSeconds = 0.0;

    /** Free-form origin tag, e.g. "ROS", "MCP", "Blueprint". */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actuation")
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

UINTERFACE(BlueprintType)
class UBOTCORE_API UUBotAttitudeCommandable : public UInterface
{
    GENERATED_BODY()
};

/** Capability interface for aerial vehicles that accept attitude and body-rate commands. */
class UBOTCORE_API IUBotAttitudeCommandable
{
    GENERATED_BODY()

public:
    /** Returns false when the command was rejected (not ready, not accepted by this vehicle, invalid values, ...). */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "uBot|Actuation")
    bool ApplyAttitudeCommand(const FUBotAttitudeCommand& Command);

    /** Returns false when the command was rejected (not ready, not accepted by this vehicle, invalid values, ...). */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "uBot|Actuation")
    bool ApplyBodyRateCommand(const FUBotBodyRateCommand& Command);

    /** The sender stopped sending (for example its commands timed out); the vehicle falls back to its own behavior, such as a hover. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "uBot|Actuation")
    void ReleaseAttitudeCommand();
};

UINTERFACE(BlueprintType)
class UBOTCORE_API UUBotMotorCommandable : public UInterface
{
    GENERATED_BODY()
};

/** Capability interface for vehicles whose motors can be commanded one by one. */
class UBOTCORE_API IUBotMotorCommandable
{
    GENERATED_BODY()

public:
    /** Returns false when the command was rejected (wrong number of values, not ready, invalid values, ...). */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "uBot|Actuation")
    bool ApplyMotorCommand(const FUBotMotorCommand& Command);

    /** Number of motors, the length a motor command must have. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "uBot|Actuation")
    int32 GetMotorCount() const;

    /** The sender stopped sending (for example its commands timed out); the vehicle falls back to its own behavior. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "uBot|Actuation")
    void ReleaseMotorCommand();
};

namespace UBot::Actuation
{
    /** The actor itself if it implements IUBotVelocityCommandable, else its first component that does, else nullptr. */
    UBOTCORE_API UObject* FindVelocityCommandable(const AActor* Actor);

    /** The actor itself if it implements IUBotAttitudeCommandable, else its first component that does, else nullptr. */
    UBOTCORE_API UObject* FindAttitudeCommandable(const AActor* Actor);

    /** The actor itself if it implements IUBotMotorCommandable, else its first component that does, else nullptr. */
    UBOTCORE_API UObject* FindMotorCommandable(const AActor* Actor);
}
