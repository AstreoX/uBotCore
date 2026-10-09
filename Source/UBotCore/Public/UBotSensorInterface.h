#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "UBotTypes.h"
#include "UBotSensorInterface.generated.h"

class AActor;
class USceneComponent;

UINTERFACE(BlueprintType)
class UBOTCORE_API UUBotSensor : public UInterface
{
    GENERATED_BODY()
};

/**
 * Implemented by anything that produces uBot sensor samples: C++ sensor components as well as
 * Blueprint actors or components. Callers check Implements<UUBotSensor>() and then use the
 * IUBotSensor::Execute_* wrappers so both native and Blueprint implementations are reached.
 */
class UBOTCORE_API IUBotSensor
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "uBot|Sensor")
    FString GetSensorId() const;

    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "uBot|Sensor")
    FString GetSensorFrameId() const;

    /** Short stable type key, e.g. "Pose", "Odometry", "IMU", "Lidar2D", "Lidar3D", "RGBCamera", "DepthCamera". */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "uBot|Sensor")
    FName GetSensorType() const;

    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "uBot|Sensor")
    bool IsSensorRunning() const;

    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "uBot|Sensor")
    FUBotFrameHeader GetLatestSensorHeader() const;

    /** Scene component whose transform is the sensor frame (for TF). May be nullptr. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "uBot|Sensor")
    USceneComponent* GetSensorFrameComponent() const;
};

namespace UBot::Interfaces
{
    /**
     * Collects Actor itself if its class implements InterfaceClass, followed by every component
     * owned by Actor that implements it, in component order. Child actor components are not
     * searched. OutObjects is reset first; it stays empty for a null actor or interface.
     */
    UBOTCORE_API void FindImplementers(const AActor* Actor, const UClass* InterfaceClass, TArray<UObject*>& OutObjects);

    /** First object FindImplementers would return, or nullptr. */
    UBOTCORE_API UObject* FindFirstImplementer(const AActor* Actor, const UClass* InterfaceClass);
}

namespace UBot::Sensors
{
    /**
     * Returns the actor itself if it implements IUBotSensor plus every component that does, in
     * component order. OutSensors is reset first.
     */
    UBOTCORE_API void FindSensors(const AActor* Actor, TArray<UObject*>& OutSensors);
}
