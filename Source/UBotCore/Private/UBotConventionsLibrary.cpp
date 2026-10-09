#include "UBotConventionsLibrary.h"

#include "UBotRosConventions.h"

FVector UUBotConventionsLibrary::PositionUEToROS(const FVector& UECentimeters)
{
    return UBot::Ros::PositionUEToROS(UECentimeters);
}

FVector UUBotConventionsLibrary::PositionROSToUE(const FVector& ROSMeters)
{
    return UBot::Ros::PositionROSToUE(ROSMeters);
}

FVector UUBotConventionsLibrary::VectorUEToROS(const FVector& Value)
{
    return UBot::Ros::VectorUEToROS(Value);
}

FVector UUBotConventionsLibrary::VectorROSToUE(const FVector& Value)
{
    return UBot::Ros::VectorROSToUE(Value);
}

FVector UUBotConventionsLibrary::AngularVelocityUEToROS(const FVector& UERadiansPerSecond)
{
    return UBot::Ros::AngularVelocityUEToROS(UERadiansPerSecond);
}

FVector UUBotConventionsLibrary::AngularVelocityROSToUE(const FVector& ROSRadiansPerSecond)
{
    return UBot::Ros::AngularVelocityROSToUE(ROSRadiansPerSecond);
}

FQuat UUBotConventionsLibrary::RotationUEToROS(const FQuat& UERotation)
{
    return UBot::Ros::RotationUEToROS(UERotation);
}

FQuat UUBotConventionsLibrary::RotationROSToUE(const FQuat& ROSRotation)
{
    return UBot::Ros::RotationROSToUE(ROSRotation);
}

FTransform UUBotConventionsLibrary::TransformUEToROS(const FTransform& UETransform)
{
    return UBot::Ros::TransformUEToROS(UETransform);
}

FTransform UUBotConventionsLibrary::TransformROSToUE(const FTransform& ROSTransform)
{
    return UBot::Ros::TransformROSToUE(ROSTransform);
}

double UUBotConventionsLibrary::YawUEToROS(double UERadians)
{
    return UBot::Ros::YawUEToROS(UERadians);
}

double UUBotConventionsLibrary::YawROSToUE(double ROSRadians)
{
    return UBot::Ros::YawROSToUE(ROSRadians);
}

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
