#include "UBotRightHandedConventions.h"

#include "UBotUnits.h"

namespace UBot::RightHanded::Private
{
    FVector MirrorPolarVector(const FVector& Value)
    {
        return FVector(Value.X, -Value.Y, Value.Z);
    }

    FVector MirrorAxialVector(const FVector& Value)
    {
        return FVector(-Value.X, Value.Y, -Value.Z);
    }

    FQuat MirrorRotation(const FQuat& Rotation)
    {
        FQuat Result(-Rotation.X, Rotation.Y, -Rotation.Z, Rotation.W);
        Result.Normalize();
        return Result;
    }
}

namespace UBot::RightHanded
{
    FVector PositionFromUE(const FVector& UECentimeters)
    {
        return Private::MirrorPolarVector(UBot::Units::CentimetersToMeters(UECentimeters));
    }

    FVector PositionToUE(const FVector& Meters)
    {
        return Private::MirrorPolarVector(UBot::Units::MetersToCentimeters(Meters));
    }

    FVector VectorFromUE(const FVector& Value)
    {
        return Private::MirrorPolarVector(Value);
    }

    FVector VectorToUE(const FVector& Value)
    {
        return Private::MirrorPolarVector(Value);
    }

    FVector AngularVelocityFromUE(const FVector& UERadiansPerSecond)
    {
        return Private::MirrorAxialVector(UERadiansPerSecond);
    }

    FVector AngularVelocityToUE(const FVector& RadiansPerSecond)
    {
        return Private::MirrorAxialVector(RadiansPerSecond);
    }

    FQuat RotationFromUE(const FQuat& UERotation)
    {
        return Private::MirrorRotation(UERotation);
    }

    FQuat RotationToUE(const FQuat& Rotation)
    {
        return Private::MirrorRotation(Rotation);
    }

    FTransform TransformFromUE(const FTransform& UETransform)
    {
        return FTransform(RotationFromUE(UETransform.GetRotation()), PositionFromUE(UETransform.GetTranslation()), FVector::OneVector);
    }

    FTransform TransformToUE(const FTransform& Transform)
    {
        return FTransform(RotationToUE(Transform.GetRotation()), PositionToUE(Transform.GetTranslation()), FVector::OneVector);
    }

    double YawFromUE(double UERadians)
    {
        return -UERadians;
    }

    double YawToUE(double Radians)
    {
        return -Radians;
    }
}
