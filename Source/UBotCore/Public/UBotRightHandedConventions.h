#pragma once

#include "CoreMinimal.h"

/**
 * Conventions between UE and the external right-handed world. Pure math, no protocol or engine types.
 *
 * UE:       left-handed,  X forward, Y right, Z up, centimeters.
 * External: right-handed, X forward, Y left,  Z up, SI units (FLU). ROS uses this frame (REP-103), and so do
 *           right-handed Z-up engines such as MuJoCo when the model's X axis points forward.
 *
 * A physical point with UE coordinates u has external coordinates M u (after unit scaling), where
 * M = diag(1, -1, 1) is a mirror (det M = -1). Each quantity transforms according to its kind:
 *  - polar vectors (positions, linear velocity, acceleration, force): v' = M v = (x, -y, z)
 *  - rotation matrices: R' = M R M. For any orthogonal M, M [n]x M^T = det(M) [M n]x, so with
 *    det M = -1 the rotation about axis n by angle a becomes the rotation about -M n by the same
 *    angle. The quaternion (n sin(a/2), cos(a/2)) therefore maps to (-x, y, -z, w).
 *  - axial vectors (angular velocity, torque): from dR'/dt = M [w]x R M = [-M w]x R' the
 *    angular velocity maps to w' = -M w = (-x, y, -z). This holds in both the world and body frame
 *    and matches finite differences of the mapped quaternions, because q -> (-x, y, -z, w) is a
 *    quaternion automorphism (a rotation of the vector part by pi about Y).
 * Every mapping is its own inverse, so each ...ToUE function equals its ...FromUE counterpart apart
 * from the unit scaling.
 */
namespace UBot::RightHanded
{
    /** Position: UE centimeters -> meters in the right-handed frame, (x, -y, z) / 100. */
    UBOTCORE_API FVector PositionFromUE(const FVector& UECentimeters);

    /** Position: meters in the right-handed frame -> UE centimeters, (x, -y, z) * 100. */
    UBOTCORE_API FVector PositionToUE(const FVector& Meters);

    /** Polar vector already in SI units (linear velocity, acceleration, points in meters): (x, -y, z). */
    UBOTCORE_API FVector VectorFromUE(const FVector& Value);

    /** Polar vector already in SI units: (x, -y, z). */
    UBOTCORE_API FVector VectorToUE(const FVector& Value);

    /** Axial vector (angular velocity, torque): (-x, y, -z). Consistent with the quaternion mapping. */
    UBOTCORE_API FVector AngularVelocityFromUE(const FVector& UERadiansPerSecond);

    /** Axial vector (angular velocity, torque): (-x, y, -z). */
    UBOTCORE_API FVector AngularVelocityToUE(const FVector& RadiansPerSecond);

    /** Rotation: q = (x, y, z, w) -> normalized (-x, y, -z, w). A degenerate input yields identity. */
    UBOTCORE_API FQuat RotationFromUE(const FQuat& UERotation);

    /** Rotation: same mapping as RotationFromUE. */
    UBOTCORE_API FQuat RotationToUE(const FQuat& Rotation);

    /** Rigid transform: rotation mirrored, translation centimeters -> meters, scale dropped (output scale is 1). */
    UBOTCORE_API FTransform TransformFromUE(const FTransform& UETransform);

    /** Rigid transform: rotation mirrored, translation meters -> centimeters, scale dropped (output scale is 1). */
    UBOTCORE_API FTransform TransformToUE(const FTransform& Transform);

    /** Planar heading, scan angle or yaw rate: right-handed = -UE. */
    UBOTCORE_API double YawFromUE(double UERadians);

    /** Planar heading, scan angle or yaw rate: UE = -right-handed. */
    UBOTCORE_API double YawToUE(double Radians);
}
