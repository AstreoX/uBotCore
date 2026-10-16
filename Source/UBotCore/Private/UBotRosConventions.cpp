#include "UBotRosConventions.h"

#include "UBotIdentityComponent.h"
#include "UBotRightHandedConventions.h"

namespace UBot::Ros::Private
{
    constexpr int64 NanosecondsPerSecond = 1000000000;
    constexpr uint32 MaxNanoseconds = 999999999;
}

// The whole namespace is deprecated: the math forwards to UBot::RightHanded, the ROS-only functions keep
// their bodies until they are removed.
namespace UBot::Ros
{
    FVector PositionUEToROS(const FVector& UECentimeters)
    {
        return UBot::RightHanded::PositionFromUE(UECentimeters);
    }

    FVector PositionROSToUE(const FVector& ROSMeters)
    {
        return UBot::RightHanded::PositionToUE(ROSMeters);
    }

    FVector VectorUEToROS(const FVector& Value)
    {
        return UBot::RightHanded::VectorFromUE(Value);
    }

    FVector VectorROSToUE(const FVector& Value)
    {
        return UBot::RightHanded::VectorToUE(Value);
    }

    FVector AngularVelocityUEToROS(const FVector& UERadiansPerSecond)
    {
        return UBot::RightHanded::AngularVelocityFromUE(UERadiansPerSecond);
    }

    FVector AngularVelocityROSToUE(const FVector& ROSRadiansPerSecond)
    {
        return UBot::RightHanded::AngularVelocityToUE(ROSRadiansPerSecond);
    }

    FQuat RotationUEToROS(const FQuat& UERotation)
    {
        return UBot::RightHanded::RotationFromUE(UERotation);
    }

    FQuat RotationROSToUE(const FQuat& ROSRotation)
    {
        return UBot::RightHanded::RotationToUE(ROSRotation);
    }

    FTransform TransformUEToROS(const FTransform& UETransform)
    {
        return UBot::RightHanded::TransformFromUE(UETransform);
    }

    FTransform TransformROSToUE(const FTransform& ROSTransform)
    {
        return UBot::RightHanded::TransformToUE(ROSTransform);
    }

    double YawUEToROS(double UERadians)
    {
        return UBot::RightHanded::YawFromUE(UERadians);
    }

    double YawROSToUE(double ROSRadians)
    {
        return UBot::RightHanded::YawToUE(ROSRadians);
    }

    FQuat CameraLinkToOpticalRotation()
    {
        // Columns of the matching rotation matrix are the optical axes in link coordinates:
        // x (right) = -Y, y (down) = -Z, z (forward) = +X.
        return FQuat(-0.5, 0.5, -0.5, 0.5);
    }

    const TCHAR* OpticalFrameSuffix()
    {
        return TEXT("_optical");
    }

    void SecondsToRosTime(double Seconds, int32& OutSec, uint32& OutNanosec)
    {
        OutSec = 0;
        OutNanosec = 0;
        if (!FMath::IsFinite(Seconds) || Seconds <= 0.0)
        {
            return;
        }
        if (Seconds >= static_cast<double>(MAX_int32) + 1.0)
        {
            OutSec = MAX_int32;
            OutNanosec = Private::MaxNanoseconds;
            return;
        }

        const double WholeSeconds = FMath::FloorToDouble(Seconds);
        int64 Sec = static_cast<int64>(WholeSeconds);
        int64 Nanoseconds = FMath::RoundToInt64((Seconds - WholeSeconds) * static_cast<double>(Private::NanosecondsPerSecond));
        if (Nanoseconds >= Private::NanosecondsPerSecond)
        {
            Sec += 1;
            Nanoseconds -= Private::NanosecondsPerSecond;
        }
        Nanoseconds = FMath::Clamp<int64>(Nanoseconds, 0, Private::MaxNanoseconds);

        if (Sec > MAX_int32)
        {
            OutSec = MAX_int32;
            OutNanosec = Private::MaxNanoseconds;
            return;
        }
        OutSec = static_cast<int32>(Sec);
        OutNanosec = static_cast<uint32>(Nanoseconds);
    }

    double RosTimeToSeconds(int32 Sec, uint32 Nanosec)
    {
        // Division keeps exact values exact (500000000 ns is exactly 0.5 s), unlike multiplying by 1e-9.
        return static_cast<double>(Sec) + static_cast<double>(Nanosec) / static_cast<double>(Private::NanosecondsPerSecond);
    }

    FString SanitizeRosName(const FString& Name)
    {
        return UBot::Identity::SanitizeNameToken(Name);
    }
}
