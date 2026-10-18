#pragma once

#include "CoreMinimal.h"
#include "Engine/World.h"
#include "UBotWindProvider.h"
#include "UBotWindTestTypes.generated.h"

// Test-only reflected type. UHT does not allow WITH_DEV_AUTOMATION_TESTS around UCLASS declarations.
UCLASS(Transient, NotBlueprintable, HideDropdown)
class UUBotWindTestProvider : public UObject, public IUBotWindProvider
{
    GENERATED_BODY()

public:
    FVector Velocity = FVector::ZeroVector;
    mutable FVector LastLocationCentimeters = FVector::ZeroVector;
    mutable double LastTimeSeconds = 0.0;
    mutable int32 QueryCount = 0;

    virtual UWorld* GetWorld() const override
    {
        return GetTypedOuter<UWorld>();
    }

    virtual FVector GetWindVelocity(const FVector& LocationCentimeters, double TimeSeconds) const override
    {
        LastLocationCentimeters = LocationCentimeters;
        LastTimeSeconds = TimeSeconds;
        ++QueryCount;
        return Velocity;
    }
};
