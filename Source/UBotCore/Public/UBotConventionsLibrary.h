#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UBotConventionsLibrary.generated.h"

/** Blueprint access to the UE <-> ROS (REP-103) conventions in UBot::Ros. */
UCLASS()
class UBOTCORE_API UUBotConventionsLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /** UE position in centimeters -> ROS position in meters: (x, -y, z) / 100. */
    UFUNCTION(BlueprintPure, Category = "uBot|Conventions", meta = (DisplayName = "UE To ROS Position", Keywords = "ROS REP103 convert location"))
    static FVector PositionUEToROS(const FVector& UECentimeters);

    /** ROS position in meters -> UE position in centimeters: (x, -y, z) * 100. */
    UFUNCTION(BlueprintPure, Category = "uBot|Conventions", meta = (DisplayName = "ROS To UE Position", Keywords = "ROS REP103 convert location"))
    static FVector PositionROSToUE(const FVector& ROSMeters);

    /** Polar vector in SI units (linear velocity, acceleration, point in meters): (x, -y, z). */
    UFUNCTION(BlueprintPure, Category = "uBot|Conventions", meta = (DisplayName = "UE To ROS Vector", Keywords = "ROS REP103 convert velocity acceleration"))
    static FVector VectorUEToROS(const FVector& Value);

    /** Polar vector in SI units: (x, -y, z). */
    UFUNCTION(BlueprintPure, Category = "uBot|Conventions", meta = (DisplayName = "ROS To UE Vector", Keywords = "ROS REP103 convert velocity acceleration"))
    static FVector VectorROSToUE(const FVector& Value);

    /** Axial vector (angular velocity, torque) in rad/s: (-x, y, -z). */
    UFUNCTION(BlueprintPure, Category = "uBot|Conventions", meta = (DisplayName = "UE To ROS Angular Velocity", Keywords = "ROS REP103 convert torque"))
    static FVector AngularVelocityUEToROS(const FVector& UERadiansPerSecond);

    /** Axial vector (angular velocity, torque) in rad/s: (-x, y, -z). */
    UFUNCTION(BlueprintPure, Category = "uBot|Conventions", meta = (DisplayName = "ROS To UE Angular Velocity", Keywords = "ROS REP103 convert torque"))
    static FVector AngularVelocityROSToUE(const FVector& ROSRadiansPerSecond);

    /** Rotation: (x, y, z, w) -> normalized (-x, y, -z, w). */
    UFUNCTION(BlueprintPure, Category = "uBot|Conventions", meta = (DisplayName = "UE To ROS Rotation", Keywords = "ROS REP103 convert quaternion orientation"))
    static FQuat RotationUEToROS(const FQuat& UERotation);

    /** Rotation: (x, y, z, w) -> normalized (-x, y, -z, w). */
    UFUNCTION(BlueprintPure, Category = "uBot|Conventions", meta = (DisplayName = "ROS To UE Rotation", Keywords = "ROS REP103 convert quaternion orientation"))
    static FQuat RotationROSToUE(const FQuat& ROSRotation);

    /** Rigid transform: rotation mirrored, translation centimeters -> meters, scale dropped. */
    UFUNCTION(BlueprintPure, Category = "uBot|Conventions", meta = (DisplayName = "UE To ROS Transform", Keywords = "ROS REP103 convert pose tf"))
    static FTransform TransformUEToROS(const FTransform& UETransform);

    /** Rigid transform: rotation mirrored, translation meters -> centimeters, scale dropped. */
    UFUNCTION(BlueprintPure, Category = "uBot|Conventions", meta = (DisplayName = "ROS To UE Transform", Keywords = "ROS REP103 convert pose tf"))
    static FTransform TransformROSToUE(const FTransform& ROSTransform);

    /** Planar heading, scan angle or yaw rate in radians: ROS = -UE. */
    UFUNCTION(BlueprintPure, Category = "uBot|Conventions", meta = (DisplayName = "UE To ROS Yaw", Keywords = "ROS REP103 convert heading angle"))
    static double YawUEToROS(double UERadians);

    /** Planar heading, scan angle or yaw rate in radians: UE = -ROS. */
    UFUNCTION(BlueprintPure, Category = "uBot|Conventions", meta = (DisplayName = "ROS To UE Yaw", Keywords = "ROS REP103 convert heading angle"))
    static double YawROSToUE(double ROSRadians);

    /** Rotation of the camera optical frame (z forward, x right, y down) relative to the camera link frame, in ROS conventions. */
    UFUNCTION(BlueprintPure, Category = "uBot|Conventions", meta = (DisplayName = "Camera Link To Optical Rotation", Keywords = "ROS camera optical frame"))
    static FQuat CameraLinkToOpticalRotation();

    /** Suffix appended to a camera frame id to name its optical frame ("_optical"). */
    UFUNCTION(BlueprintPure, Category = "uBot|Conventions", meta = (DisplayName = "Get Optical Frame Suffix", Keywords = "ROS camera optical frame"))
    static FString GetOpticalFrameSuffix();

    /** Seconds -> builtin_interfaces/Time fields. Negative or non-finite input yields (0, 0). */
    UFUNCTION(BlueprintPure, Category = "uBot|Conventions", meta = (DisplayName = "Seconds To ROS Time", Keywords = "ROS stamp nanoseconds"))
    static void SecondsToRosTime(double Seconds, int32& Sec, int32& Nanosec);

    /** builtin_interfaces/Time fields -> seconds. Negative nanoseconds are treated as 0. */
    UFUNCTION(BlueprintPure, Category = "uBot|Conventions", meta = (DisplayName = "ROS Time To Seconds", Keywords = "ROS stamp nanoseconds"))
    static double RosTimeToSeconds(int32 Sec, int32 Nanosec);

    /** Makes a valid ROS name token: [A-Za-z0-9_] only, no repeated underscores, no leading digit, never empty. */
    UFUNCTION(BlueprintPure, Category = "uBot|Conventions", meta = (DisplayName = "Sanitize ROS Name", Keywords = "ROS topic namespace frame"))
    static FString SanitizeRosName(const FString& Name);
};
