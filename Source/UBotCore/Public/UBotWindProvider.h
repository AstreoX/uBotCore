#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "UBotWindProvider.generated.h"

UINTERFACE(meta = (CannotImplementInterfaceInBlueprint))
class UBOTCORE_API UUBotWindProvider : public UInterface
{
    GENERATED_BODY()
};

/** Supplies a world's wind without coupling consumers to its environment implementation. Game thread only. */
class UBOTCORE_API IUBotWindProvider
{
    GENERATED_BODY()

public:
    /** Wind velocity in UE world axes, m/s, at a world position in centimeters and uBot sim time in seconds. */
    virtual FVector GetWindVelocity(const FVector& LocationCentimeters, double TimeSeconds) const = 0;
};
