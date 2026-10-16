#pragma once

#include "CoreMinimal.h"
#include "UBotRightHandedConventions.h"

/**
 * Deprecated namespace, kept so that released binaries and existing code keep working. Pure math,
 * no ROS types. Where each part went:
 *  - the axis, vector, rotation, transform and yaw mappings: UBot::RightHanded (UBotRightHandedConventions.h);
 *    these functions forward to it and give exactly the same results
 *  - CameraLinkToOpticalRotation, OpticalFrameSuffix, SecondsToRosTime, RosTimeToSeconds: UBotROS,
 *    UBot::ROSConventions (UBotROSConventionHelpers.h)
 *  - SanitizeRosName: UBot::Identity::SanitizeNameToken for a portable name token, or
 *    UBot::ROSConventions::SanitizeRosName in UBotROS
 * UE is left-handed (X forward, Y right, Z up, centimeters); ROS (REP-103) is right-handed (X forward,
 * Y left, Z up, meters). The derivation of the mappings is documented in UBotRightHandedConventions.h.
 */
namespace UBot::Ros
{
    /** Position: UE centimeters -> ROS meters, (x, -y, z) / 100. */
    UE_DEPRECATED(5.5, "Use UBot::RightHanded::PositionFromUE (UBotRightHandedConventions.h).")
    UBOTCORE_API FVector PositionUEToROS(const FVector& UECentimeters);

    /** Position: ROS meters -> UE centimeters, (x, -y, z) * 100. */
    UE_DEPRECATED(5.5, "Use UBot::RightHanded::PositionToUE (UBotRightHandedConventions.h).")
    UBOTCORE_API FVector PositionROSToUE(const FVector& ROSMeters);

    /** Polar vector already in SI units (linear velocity, acceleration, points in meters): (x, -y, z). */
    UE_DEPRECATED(5.5, "Use UBot::RightHanded::VectorFromUE (UBotRightHandedConventions.h).")
    UBOTCORE_API FVector VectorUEToROS(const FVector& Value);

    /** Polar vector already in SI units: (x, -y, z). */
    UE_DEPRECATED(5.5, "Use UBot::RightHanded::VectorToUE (UBotRightHandedConventions.h).")
    UBOTCORE_API FVector VectorROSToUE(const FVector& Value);

    /** Axial vector (angular velocity, torque): (-x, y, -z). Consistent with the quaternion mapping. */
    UE_DEPRECATED(5.5, "Use UBot::RightHanded::AngularVelocityFromUE (UBotRightHandedConventions.h).")
    UBOTCORE_API FVector AngularVelocityUEToROS(const FVector& UERadiansPerSecond);

    /** Axial vector (angular velocity, torque): (-x, y, -z). */
    UE_DEPRECATED(5.5, "Use UBot::RightHanded::AngularVelocityToUE (UBotRightHandedConventions.h).")
    UBOTCORE_API FVector AngularVelocityROSToUE(const FVector& ROSRadiansPerSecond);

    /** Rotation: q = (x, y, z, w) -> normalized (-x, y, -z, w). A degenerate input yields identity. */
    UE_DEPRECATED(5.5, "Use UBot::RightHanded::RotationFromUE (UBotRightHandedConventions.h).")
    UBOTCORE_API FQuat RotationUEToROS(const FQuat& UERotation);

    /** Rotation: same mapping as RotationUEToROS. */
    UE_DEPRECATED(5.5, "Use UBot::RightHanded::RotationToUE (UBotRightHandedConventions.h).")
    UBOTCORE_API FQuat RotationROSToUE(const FQuat& ROSRotation);

    /** Rigid transform: rotation mirrored, translation centimeters -> meters, scale dropped (output scale is 1). */
    UE_DEPRECATED(5.5, "Use UBot::RightHanded::TransformFromUE (UBotRightHandedConventions.h).")
    UBOTCORE_API FTransform TransformUEToROS(const FTransform& UETransform);

    /** Rigid transform: rotation mirrored, translation meters -> centimeters, scale dropped (output scale is 1). */
    UE_DEPRECATED(5.5, "Use UBot::RightHanded::TransformToUE (UBotRightHandedConventions.h).")
    UBOTCORE_API FTransform TransformROSToUE(const FTransform& ROSTransform);

    /** Planar heading, scan angle or yaw rate: ROS = -UE. */
    UE_DEPRECATED(5.5, "Use UBot::RightHanded::YawFromUE (UBotRightHandedConventions.h).")
    UBOTCORE_API double YawUEToROS(double UERadians);

    /** Planar heading, scan angle or yaw rate: UE = -ROS. */
    UE_DEPRECATED(5.5, "Use UBot::RightHanded::YawToUE (UBotRightHandedConventions.h).")
    UBOTCORE_API double YawROSToUE(double ROSRadians);

    /**
     * Rotation of the camera optical frame (z forward, x right, y down) relative to the camera link
     * frame, in ROS conventions: (x=-0.5, y=0.5, z=-0.5, w=0.5).
     */
    UE_DEPRECATED(5.5, "ROS-specific: moved to UBotROS, use UBot::ROSConventions::CameraLinkToOpticalRotation (UBotROSConventionHelpers.h).")
    UBOTCORE_API FQuat CameraLinkToOpticalRotation();

    /** Suffix appended to a camera frame id to name its optical frame: "_optical". */
    UE_DEPRECATED(5.5, "ROS-specific: moved to UBotROS, use UBot::ROSConventions::OpticalFrameSuffix (UBotROSConventionHelpers.h).")
    UBOTCORE_API const TCHAR* OpticalFrameSuffix();

    /**
     * Seconds -> (sec, nanosec) as used by builtin_interfaces/Time. Negative or non-finite input
     * yields (0, 0). Nanoseconds are rounded and clamped to [0, 999999999]; the rounding carry goes
     * into sec. Values beyond the int32 range saturate at (MAX_int32, 999999999).
     */
    UE_DEPRECATED(5.5, "ROS-specific: moved to UBotROS, use UBot::ROSConventions::SecondsToRosTime (UBotROSConventionHelpers.h).")
    UBOTCORE_API void SecondsToRosTime(double Seconds, int32& OutSec, uint32& OutNanosec);

    /** (sec, nanosec) -> seconds. */
    UE_DEPRECATED(5.5, "ROS-specific: moved to UBotROS, use UBot::ROSConventions::RosTimeToSeconds (UBotROSConventionHelpers.h).")
    UBOTCORE_API double RosTimeToSeconds(int32 Sec, uint32 Nanosec);

    /**
     * ROS name token: keeps [A-Za-z0-9_], replaces everything else by '_', collapses repeated
     * underscores, prefixes '_' when the first character is a digit and never returns an empty
     * string (fallback "ubot"). Same as UBot::Identity::SanitizeNameToken.
     */
    UE_DEPRECATED(5.5, "Use UBot::Identity::SanitizeNameToken for names, or UBot::ROSConventions::SanitizeRosName in UBotROS.")
    UBOTCORE_API FString SanitizeRosName(const FString& Name);
}
