#pragma once

#include "CoreMinimal.h"

/**
 * UE <-> ROS conventions. Pure math, no ROS types.
 *
 * UE:  left-handed,  X forward, Y right, Z up, centimeters.
 * ROS: right-handed, X forward, Y left,  Z up, meters (REP-103).
 *
 * A physical point with UE coordinates u has ROS coordinates M u (after unit scaling), where
 * M = diag(1, -1, 1) is a mirror (det M = -1). Each quantity transforms according to its kind:
 *  - polar vectors (positions, linear velocity, acceleration, force): v' = M v = (x, -y, z)
 *  - rotation matrices: R' = M R M. For any orthogonal M, M [n]x M^T = det(M) [M n]x, so with
 *    det M = -1 the rotation about axis n by angle a becomes the rotation about -M n by the same
 *    angle. The quaternion (n sin(a/2), cos(a/2)) therefore maps to (-x, y, -z, w).
 *  - axial vectors (angular velocity, torque): from dR'/dt = M [w]x R M = [-M w]x R' the
 *    angular velocity maps to w' = -M w = (-x, y, -z). This holds in both the world and body frame
 *    and matches finite differences of the mapped quaternions, because q -> (-x, y, -z, w) is a
 *    quaternion automorphism (a rotation of the vector part by pi about Y).
 * Every mapping is its own inverse, so each ROSToUE function equals its UEToROS counterpart apart
 * from the unit scaling.
 */
namespace UBot::Ros
{
    /** Position: UE centimeters -> ROS meters, (x, -y, z) / 100. */
    UBOTCORE_API FVector PositionUEToROS(const FVector& UECentimeters);

    /** Position: ROS meters -> UE centimeters, (x, -y, z) * 100. */
    UBOTCORE_API FVector PositionROSToUE(const FVector& ROSMeters);

    /** Polar vector already in SI units (linear velocity, acceleration, points in meters): (x, -y, z). */
    UBOTCORE_API FVector VectorUEToROS(const FVector& Value);

    /** Polar vector already in SI units: (x, -y, z). */
    UBOTCORE_API FVector VectorROSToUE(const FVector& Value);

    /** Axial vector (angular velocity, torque): (-x, y, -z). Consistent with the quaternion mapping. */
    UBOTCORE_API FVector AngularVelocityUEToROS(const FVector& UERadiansPerSecond);

    /** Axial vector (angular velocity, torque): (-x, y, -z). */
    UBOTCORE_API FVector AngularVelocityROSToUE(const FVector& ROSRadiansPerSecond);

    /** Rotation: q = (x, y, z, w) -> normalized (-x, y, -z, w). A degenerate input yields identity. */
    UBOTCORE_API FQuat RotationUEToROS(const FQuat& UERotation);

    /** Rotation: same mapping as RotationUEToROS. */
    UBOTCORE_API FQuat RotationROSToUE(const FQuat& ROSRotation);

    /** Rigid transform: rotation mirrored, translation centimeters -> meters, scale dropped (output scale is 1). */
    UBOTCORE_API FTransform TransformUEToROS(const FTransform& UETransform);

    /** Rigid transform: rotation mirrored, translation meters -> centimeters, scale dropped (output scale is 1). */
    UBOTCORE_API FTransform TransformROSToUE(const FTransform& ROSTransform);

    /** Planar heading, scan angle or yaw rate: ROS = -UE. */
    UBOTCORE_API double YawUEToROS(double UERadians);

    /** Planar heading, scan angle or yaw rate: UE = -ROS. */
    UBOTCORE_API double YawROSToUE(double ROSRadians);

    /**
     * Rotation of the camera optical frame (z forward, x right, y down) relative to the camera link
     * frame, in ROS conventions: (x=-0.5, y=0.5, z=-0.5, w=0.5).
     */
    UBOTCORE_API FQuat CameraLinkToOpticalRotation();

    /** Suffix appended to a camera frame id to name its optical frame: "_optical". */
    UBOTCORE_API const TCHAR* OpticalFrameSuffix();

    /**
     * Seconds -> (sec, nanosec) as used by builtin_interfaces/Time. Negative or non-finite input
     * yields (0, 0). Nanoseconds are rounded and clamped to [0, 999999999]; the rounding carry goes
     * into sec. Values beyond the int32 range saturate at (MAX_int32, 999999999).
     */
    UBOTCORE_API void SecondsToRosTime(double Seconds, int32& OutSec, uint32& OutNanosec);

    /** (sec, nanosec) -> seconds. */
    UBOTCORE_API double RosTimeToSeconds(int32 Sec, uint32 Nanosec);

    /**
     * ROS name token: keeps [A-Za-z0-9_], replaces everything else by '_', collapses repeated
     * underscores, prefixes '_' when the first character is a digit and never returns an empty
     * string (fallback "ubot").
     */
    UBOTCORE_API FString SanitizeRosName(const FString& Name);
}
