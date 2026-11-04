#include "UBotConventionsLibrary.h"

#include "UBotRightHandedConventions.h"

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
