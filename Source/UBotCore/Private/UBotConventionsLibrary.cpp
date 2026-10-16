#include "UBotConventionsLibrary.h"

#include "UBotRightHandedConventions.h"
#include "UBotRosConventions.h"

FVector UUBotConventionsLibrary::PositionUEToRightHanded(const FVector& UECentimeters)
{
    return UBot::RightHanded::PositionFromUE(UECentimeters);
}

FVector UUBotConventionsLibrary::PositionRightHandedToUE(const FVector& Meters)
{
    return UBot::RightHanded::PositionToUE(Meters);
}

FVector UUBotConventionsLibrary::VectorUEToRightHanded(const FVector& Value)
{
    return UBot::RightHanded::VectorFromUE(Value);
}

FVector UUBotConventionsLibrary::VectorRightHandedToUE(const FVector& Value)
{
    return UBot::RightHanded::VectorToUE(Value);
}

FVector UUBotConventionsLibrary::AngularVelocityUEToRightHanded(const FVector& UERadiansPerSecond)
{
    return UBot::RightHanded::AngularVelocityFromUE(UERadiansPerSecond);
}

FVector UUBotConventionsLibrary::AngularVelocityRightHandedToUE(const FVector& RadiansPerSecond)
{
    return UBot::RightHanded::AngularVelocityToUE(RadiansPerSecond);
}

FQuat UUBotConventionsLibrary::RotationUEToRightHanded(const FQuat& UERotation)
{
    return UBot::RightHanded::RotationFromUE(UERotation);
}

FQuat UUBotConventionsLibrary::RotationRightHandedToUE(const FQuat& Rotation)
{
    return UBot::RightHanded::RotationToUE(Rotation);
}

FTransform UUBotConventionsLibrary::TransformUEToRightHanded(const FTransform& UETransform)
{
    return UBot::RightHanded::TransformFromUE(UETransform);
}

FTransform UUBotConventionsLibrary::TransformRightHandedToUE(const FTransform& Transform)
{
    return UBot::RightHanded::TransformToUE(Transform);
}

double UUBotConventionsLibrary::YawUEToRightHanded(double UERadians)
{
    return UBot::RightHanded::YawFromUE(UERadians);
}

double UUBotConventionsLibrary::YawRightHandedToUE(double Radians)
{
    return UBot::RightHanded::YawToUE(Radians);
}

// Deprecated nodes. They keep their names, pins, display names and keywords so existing Blueprints still
// compile. The math nodes call the same functions as the right-handed nodes above; the ROS-only nodes keep
// their behavior through the deprecated UBot::Ros functions, which is why they sit in a pragma pair.

FVector UUBotConventionsLibrary::PositionUEToROS(const FVector& UECentimeters)
{
    return UBot::RightHanded::PositionFromUE(UECentimeters);
}

FVector UUBotConventionsLibrary::PositionROSToUE(const FVector& ROSMeters)
{
    return UBot::RightHanded::PositionToUE(ROSMeters);
}

FVector UUBotConventionsLibrary::VectorUEToROS(const FVector& Value)
{
    return UBot::RightHanded::VectorFromUE(Value);
}

FVector UUBotConventionsLibrary::VectorROSToUE(const FVector& Value)
{
    return UBot::RightHanded::VectorToUE(Value);
}

FVector UUBotConventionsLibrary::AngularVelocityUEToROS(const FVector& UERadiansPerSecond)
{
    return UBot::RightHanded::AngularVelocityFromUE(UERadiansPerSecond);
}

FVector UUBotConventionsLibrary::AngularVelocityROSToUE(const FVector& ROSRadiansPerSecond)
{
    return UBot::RightHanded::AngularVelocityToUE(ROSRadiansPerSecond);
}

FQuat UUBotConventionsLibrary::RotationUEToROS(const FQuat& UERotation)
{
    return UBot::RightHanded::RotationFromUE(UERotation);
}

FQuat UUBotConventionsLibrary::RotationROSToUE(const FQuat& ROSRotation)
{
    return UBot::RightHanded::RotationToUE(ROSRotation);
}

FTransform UUBotConventionsLibrary::TransformUEToROS(const FTransform& UETransform)
{
    return UBot::RightHanded::TransformFromUE(UETransform);
}

FTransform UUBotConventionsLibrary::TransformROSToUE(const FTransform& ROSTransform)
{
    return UBot::RightHanded::TransformToUE(ROSTransform);
}

double UUBotConventionsLibrary::YawUEToROS(double UERadians)
{
    return UBot::RightHanded::YawFromUE(UERadians);
}

double UUBotConventionsLibrary::YawROSToUE(double ROSRadians)
{
    return UBot::RightHanded::YawToUE(ROSRadians);
}

PRAGMA_DISABLE_DEPRECATION_WARNINGS

FQuat UUBotConventionsLibrary::CameraLinkToOpticalRotation()
{
    return UBot::Ros::CameraLinkToOpticalRotation();
}

FString UUBotConventionsLibrary::GetOpticalFrameSuffix()
{
    return FString(UBot::Ros::OpticalFrameSuffix());
}

void UUBotConventionsLibrary::SecondsToRosTime(double Seconds, int32& Sec, int32& Nanosec)
{
    uint32 UnsignedNanosec = 0;
    UBot::Ros::SecondsToRosTime(Seconds, Sec, UnsignedNanosec);
    // Always below 1e9, so it fits an int32.
    Nanosec = static_cast<int32>(UnsignedNanosec);
}

double UUBotConventionsLibrary::RosTimeToSeconds(int32 Sec, int32 Nanosec)
{
    return UBot::Ros::RosTimeToSeconds(Sec, static_cast<uint32>(FMath::Max(Nanosec, 0)));
}

FString UUBotConventionsLibrary::SanitizeRosName(const FString& Name)
{
    return UBot::Ros::SanitizeRosName(Name);
}

PRAGMA_ENABLE_DEPRECATION_WARNINGS
