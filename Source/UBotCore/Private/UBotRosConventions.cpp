#include "UBotRosConventions.h"

#include "UBotUnits.h"

namespace UBot::Ros::Private
{
    constexpr int64 NanosecondsPerSecond = 1000000000;
    constexpr uint32 MaxNanoseconds = 999999999;

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

    bool IsRosNameCharacter(TCHAR Character)
    {
        return (Character >= TEXT('a') && Character <= TEXT('z'))
            || (Character >= TEXT('A') && Character <= TEXT('Z'))
            || (Character >= TEXT('0') && Character <= TEXT('9'))
            || Character == TEXT('_');
    }
}

namespace UBot::Ros
{
    FVector PositionUEToROS(const FVector& UECentimeters)
    {
        return Private::MirrorPolarVector(UBot::Units::CentimetersToMeters(UECentimeters));
    }

    FVector PositionROSToUE(const FVector& ROSMeters)
    {
        return Private::MirrorPolarVector(UBot::Units::MetersToCentimeters(ROSMeters));
    }

    FVector VectorUEToROS(const FVector& Value)
    {
        return Private::MirrorPolarVector(Value);
    }

    FVector VectorROSToUE(const FVector& Value)
    {
        return Private::MirrorPolarVector(Value);
    }

    FVector AngularVelocityUEToROS(const FVector& UERadiansPerSecond)
    {
        return Private::MirrorAxialVector(UERadiansPerSecond);
    }

    FVector AngularVelocityROSToUE(const FVector& ROSRadiansPerSecond)
    {
        return Private::MirrorAxialVector(ROSRadiansPerSecond);
    }

    FQuat RotationUEToROS(const FQuat& UERotation)
    {
        return Private::MirrorRotation(UERotation);
    }

    FQuat RotationROSToUE(const FQuat& ROSRotation)
    {
        return Private::MirrorRotation(ROSRotation);
    }

    FTransform TransformUEToROS(const FTransform& UETransform)
    {
        return FTransform(RotationUEToROS(UETransform.GetRotation()), PositionUEToROS(UETransform.GetTranslation()), FVector::OneVector);
    }

    FTransform TransformROSToUE(const FTransform& ROSTransform)
    {
        return FTransform(RotationROSToUE(ROSTransform.GetRotation()), PositionROSToUE(ROSTransform.GetTranslation()), FVector::OneVector);
    }

    double YawUEToROS(double UERadians)
    {
        return -UERadians;
    }

    double YawROSToUE(double ROSRadians)
    {
        return -ROSRadians;
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
        FString Result;
        Result.Reserve(Name.Len() + 1);
        for (int32 Index = 0; Index < Name.Len(); ++Index)
        {
            const TCHAR Character = Name[Index];
            const TCHAR Output = Private::IsRosNameCharacter(Character) ? Character : TEXT('_');
            // ROS 2 forbids repeated underscores, so runs collapse whether or not they were in the input.
            if (Output == TEXT('_') && Result.Len() > 0 && Result[Result.Len() - 1] == TEXT('_'))
            {
                continue;
            }
            Result.AppendChar(Output);
        }

        if (Result.IsEmpty())
        {
            return TEXT("ubot");
        }
        if (Result[0] >= TEXT('0') && Result[0] <= TEXT('9'))
        {
            Result.InsertAt(0, TEXT('_'));
        }
        return Result;
    }
}
