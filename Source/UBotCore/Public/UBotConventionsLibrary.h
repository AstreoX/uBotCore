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

    /** UE position in centimeters -> ROS position in meters: (x, -y, z) / 100. */
    UFUNCTION(BlueprintPure, Category = "uBot|Conventions", meta = (DisplayName = "UE To ROS Position", Keywords = "ROS REP103 convert location", DeprecatedFunction, DeprecationMessage = "Use 'UE To Right-Handed Position'; it gives the same result."))
    static FVector PositionUEToROS(const FVector& UECentimeters);

    /** ROS position in meters -> UE position in centimeters: (x, -y, z) * 100. */
    UFUNCTION(BlueprintPure, Category = "uBot|Conventions", meta = (DisplayName = "ROS To UE Position", Keywords = "ROS REP103 convert location", DeprecatedFunction, DeprecationMessage = "Use 'Right-Handed To UE Position'; it gives the same result."))
    static FVector PositionROSToUE(const FVector& ROSMeters);

    /** Polar vector in SI units (linear velocity, acceleration, point in meters): (x, -y, z). */
    UFUNCTION(BlueprintPure, Category = "uBot|Conventions", meta = (DisplayName = "UE To ROS Vector", Keywords = "ROS REP103 convert velocity acceleration", DeprecatedFunction, DeprecationMessage = "Use 'UE To Right-Handed Vector'; it gives the same result."))
    static FVector VectorUEToROS(const FVector& Value);

    /** Polar vector in SI units: (x, -y, z). */
    UFUNCTION(BlueprintPure, Category = "uBot|Conventions", meta = (DisplayName = "ROS To UE Vector", Keywords = "ROS REP103 convert velocity acceleration", DeprecatedFunction, DeprecationMessage = "Use 'Right-Handed To UE Vector'; it gives the same result."))
    static FVector VectorROSToUE(const FVector& Value);

    /** Axial vector (angular velocity, torque) in rad/s: (-x, y, -z). */
    UFUNCTION(BlueprintPure, Category = "uBot|Conventions", meta = (DisplayName = "UE To ROS Angular Velocity", Keywords = "ROS REP103 convert torque", DeprecatedFunction, DeprecationMessage = "Use 'UE To Right-Handed Angular Velocity'; it gives the same result."))
    static FVector AngularVelocityUEToROS(const FVector& UERadiansPerSecond);

    /** Axial vector (angular velocity, torque) in rad/s: (-x, y, -z). */
    UFUNCTION(BlueprintPure, Category = "uBot|Conventions", meta = (DisplayName = "ROS To UE Angular Velocity", Keywords = "ROS REP103 convert torque", DeprecatedFunction, DeprecationMessage = "Use 'Right-Handed To UE Angular Velocity'; it gives the same result."))
    static FVector AngularVelocityROSToUE(const FVector& ROSRadiansPerSecond);

    /** Rotation: (x, y, z, w) -> normalized (-x, y, -z, w). */
    UFUNCTION(BlueprintPure, Category = "uBot|Conventions", meta = (DisplayName = "UE To ROS Rotation", Keywords = "ROS REP103 convert quaternion orientation", DeprecatedFunction, DeprecationMessage = "Use 'UE To Right-Handed Rotation'; it gives the same result."))
    static FQuat RotationUEToROS(const FQuat& UERotation);

    /** Rotation: (x, y, z, w) -> normalized (-x, y, -z, w). */
    UFUNCTION(BlueprintPure, Category = "uBot|Conventions", meta = (DisplayName = "ROS To UE Rotation", Keywords = "ROS REP103 convert quaternion orientation", DeprecatedFunction, DeprecationMessage = "Use 'Right-Handed To UE Rotation'; it gives the same result."))
    static FQuat RotationROSToUE(const FQuat& ROSRotation);

    /** Rigid transform: rotation mirrored, translation centimeters -> meters, scale dropped. */
    UFUNCTION(BlueprintPure, Category = "uBot|Conventions", meta = (DisplayName = "UE To ROS Transform", Keywords = "ROS REP103 convert pose tf", DeprecatedFunction, DeprecationMessage = "Use 'UE To Right-Handed Transform'; it gives the same result."))
    static FTransform TransformUEToROS(const FTransform& UETransform);

    /** Rigid transform: rotation mirrored, translation meters -> centimeters, scale dropped. */
    UFUNCTION(BlueprintPure, Category = "uBot|Conventions", meta = (DisplayName = "ROS To UE Transform", Keywords = "ROS REP103 convert pose tf", DeprecatedFunction, DeprecationMessage = "Use 'Right-Handed To UE Transform'; it gives the same result."))
    static FTransform TransformROSToUE(const FTransform& ROSTransform);

    /** Planar heading, scan angle or yaw rate in radians: ROS = -UE. */
    UFUNCTION(BlueprintPure, Category = "uBot|Conventions", meta = (DisplayName = "UE To ROS Yaw", Keywords = "ROS REP103 convert heading angle", DeprecatedFunction, DeprecationMessage = "Use 'UE To Right-Handed Yaw'; it gives the same result."))
    static double YawUEToROS(double UERadians);

    /** Planar heading, scan angle or yaw rate in radians: UE = -ROS. */
    UFUNCTION(BlueprintPure, Category = "uBot|Conventions", meta = (DisplayName = "ROS To UE Yaw", Keywords = "ROS REP103 convert heading angle", DeprecatedFunction, DeprecationMessage = "Use 'Right-Handed To UE Yaw'; it gives the same result."))
    static double YawROSToUE(double ROSRadians);

    /** Rotation of the camera optical frame (z forward, x right, y down) relative to the camera link frame, in ROS conventions. */
    UFUNCTION(BlueprintPure, Category = "uBot|Conventions", meta = (DisplayName = "Camera Link To Optical Rotation", Keywords = "ROS camera optical frame", DeprecatedFunction, DeprecationMessage = "ROS-specific: moved to the uBot ROS plugin as 'Camera Link To Optical Rotation' (category uBot|ROS|Conventions)."))
    static FQuat CameraLinkToOpticalRotation();

    /** Suffix appended to a camera frame id to name its optical frame ("_optical"). */
    UFUNCTION(BlueprintPure, Category = "uBot|Conventions", meta = (DisplayName = "Get Optical Frame Suffix", Keywords = "ROS camera optical frame", DeprecatedFunction, DeprecationMessage = "ROS-specific: moved to the uBot ROS plugin as 'Get Optical Frame Suffix' (category uBot|ROS|Conventions)."))
    static FString GetOpticalFrameSuffix();

    /** Seconds -> builtin_interfaces/Time fields. Negative or non-finite input yields (0, 0). */
    UFUNCTION(BlueprintPure, Category = "uBot|Conventions", meta = (DisplayName = "Seconds To ROS Time", Keywords = "ROS stamp nanoseconds", DeprecatedFunction, DeprecationMessage = "ROS-specific: moved to the uBot ROS plugin as 'Seconds To ROS Time' (category uBot|ROS|Conventions)."))
    static void SecondsToRosTime(double Seconds, int32& Sec, int32& Nanosec);

    /** builtin_interfaces/Time fields -> seconds. Negative nanoseconds are treated as 0. */
    UFUNCTION(BlueprintPure, Category = "uBot|Conventions", meta = (DisplayName = "ROS Time To Seconds", Keywords = "ROS stamp nanoseconds", DeprecatedFunction, DeprecationMessage = "ROS-specific: moved to the uBot ROS plugin as 'ROS Time To Seconds' (category uBot|ROS|Conventions)."))
    static double RosTimeToSeconds(int32 Sec, int32 Nanosec);

    /** Makes a valid ROS name token: [A-Za-z0-9_] only, no repeated underscores, no leading digit, never empty. */
    UFUNCTION(BlueprintPure, Category = "uBot|Conventions", meta = (DisplayName = "Sanitize ROS Name", Keywords = "ROS topic namespace frame", DeprecatedFunction, DeprecationMessage = "ROS-specific: moved to the uBot ROS plugin as 'Sanitize ROS Name' (category uBot|ROS|Conventions)."))
    static FString SanitizeRosName(const FString& Name);
};
