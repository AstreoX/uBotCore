#pragma once

#include "CoreMinimal.h"

/**
 * Unit helpers. Every uBot public API is SI (meters, seconds, radians) unless a name says
 * Centimeters or Degrees; UE transforms are centimeters.
 */
namespace UBot::Units
{
    inline constexpr double CentimetersPerMeter = 100.0;

    constexpr double CentimetersToMeters(double Centimeters)
    {
        return Centimeters / CentimetersPerMeter;
    }

    constexpr double MetersToCentimeters(double Meters)
    {
        return Meters * CentimetersPerMeter;
    }

    inline FVector CentimetersToMeters(const FVector& Centimeters)
    {
        return Centimeters / CentimetersPerMeter;
    }

    inline FVector MetersToCentimeters(const FVector& Meters)
    {
        return Meters * CentimetersPerMeter;
    }

    constexpr double DegreesToRadians(double Degrees)
    {
        return Degrees * (UE_DOUBLE_PI / 180.0);
    }

    constexpr double RadiansToDegrees(double Radians)
    {
        return Radians * (180.0 / UE_DOUBLE_PI);
    }
}
