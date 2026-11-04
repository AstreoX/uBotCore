#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UBotConventionsLibrary.generated.h"

/** Blueprint access to the uBot coordinate conventions: UE (left-handed, centimeters) <-> right-handed FLU in SI units (ROS REP-103, MuJoCo, ...). C++: UBot::RightHanded. */
UCLASS()
class UBOTCORE_API UUBotConventionsLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /** UE position in centimeters -> right-handed position in meters: (x, -y, z) / 100. */
    UFUNCTION(BlueprintPure, Category = "uBot|Conventions", meta = (DisplayName = "UE To Right-Handed Position", Keywords = "right handed FLU SI ROS REP103 MuJoCo convert location"))
    static FVector PositionUEToRightHanded(const FVector& UECentimeters);

    /** Right-handed position in meters -> UE position in centimeters: (x, -y, z) * 100. */
    UFUNCTION(BlueprintPure, Category = "uBot|Conventions", meta = (DisplayName = "Right-Handed To UE Position", Keywords = "right handed FLU SI ROS REP103 MuJoCo convert location"))
    static FVector PositionRightHandedToUE(const FVector& Meters);

    /** Polar vector in SI units (linear velocity, acceleration, point in meters): (x, -y, z). */
    UFUNCTION(BlueprintPure, Category = "uBot|Conventions", meta = (DisplayName = "UE To Right-Handed Vector", Keywords = "right handed FLU SI ROS REP103 MuJoCo convert velocity acceleration"))
    static FVector VectorUEToRightHanded(const FVector& Value);

    /** Polar vector in SI units: (x, -y, z). */
    UFUNCTION(BlueprintPure, Category = "uBot|Conventions", meta = (DisplayName = "Right-Handed To UE Vector", Keywords = "right handed FLU SI ROS REP103 MuJoCo convert velocity acceleration"))
    static FVector VectorRightHandedToUE(const FVector& Value);

    /** Axial vector (angular velocity, torque) in rad/s: (-x, y, -z). */
    UFUNCTION(BlueprintPure, Category = "uBot|Conventions", meta = (DisplayName = "UE To Right-Handed Angular Velocity", Keywords = "right handed FLU SI ROS REP103 MuJoCo convert torque"))
    static FVector AngularVelocityUEToRightHanded(const FVector& UERadiansPerSecond);

    /** Axial vector (angular velocity, torque) in rad/s: (-x, y, -z). */
    UFUNCTION(BlueprintPure, Category = "uBot|Conventions", meta = (DisplayName = "Right-Handed To UE Angular Velocity", Keywords = "right handed FLU SI ROS REP103 MuJoCo convert torque"))
    static FVector AngularVelocityRightHandedToUE(const FVector& RadiansPerSecond);

    /** Rotation: (x, y, z, w) -> normalized (-x, y, -z, w). */
    UFUNCTION(BlueprintPure, Category = "uBot|Conventions", meta = (DisplayName = "UE To Right-Handed Rotation", Keywords = "right handed FLU SI ROS REP103 MuJoCo convert quaternion orientation"))
    static FQuat RotationUEToRightHanded(const FQuat& UERotation);

    /** Rotation: (x, y, z, w) -> normalized (-x, y, -z, w). */
    UFUNCTION(BlueprintPure, Category = "uBot|Conventions", meta = (DisplayName = "Right-Handed To UE Rotation", Keywords = "right handed FLU SI ROS REP103 MuJoCo convert quaternion orientation"))
    static FQuat RotationRightHandedToUE(const FQuat& Rotation);

    /** Rigid transform: rotation mirrored, translation centimeters -> meters, scale dropped. */
    UFUNCTION(BlueprintPure, Category = "uBot|Conventions", meta = (DisplayName = "UE To Right-Handed Transform", Keywords = "right handed FLU SI ROS REP103 MuJoCo convert pose tf"))
    static FTransform TransformUEToRightHanded(const FTransform& UETransform);

    /** Rigid transform: rotation mirrored, translation meters -> centimeters, scale dropped. */
    UFUNCTION(BlueprintPure, Category = "uBot|Conventions", meta = (DisplayName = "Right-Handed To UE Transform", Keywords = "right handed FLU SI ROS REP103 MuJoCo convert pose tf"))
    static FTransform TransformRightHandedToUE(const FTransform& Transform);

    /** Planar heading, scan angle or yaw rate in radians: right-handed = -UE. */
    UFUNCTION(BlueprintPure, Category = "uBot|Conventions", meta = (DisplayName = "UE To Right-Handed Yaw", Keywords = "right handed FLU SI ROS REP103 MuJoCo convert heading angle"))
    static double YawUEToRightHanded(double UERadians);

    /** Planar heading, scan angle or yaw rate in radians: UE = -right-handed. */
    UFUNCTION(BlueprintPure, Category = "uBot|Conventions", meta = (DisplayName = "Right-Handed To UE Yaw", Keywords = "right handed FLU SI ROS REP103 MuJoCo convert heading angle"))
    static double YawRightHandedToUE(double Radians);

};
