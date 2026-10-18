#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "UBotWindQuerySubsystem.generated.h"

/** One game or PIE world's optional wind provider. Does not tick or keep the provider alive. Game thread only. */
UCLASS()
class UBOTCORE_API UUBotWindQuerySubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Deinitialize() override;

    /**
     * Registers an IUBotWindProvider belonging to this world. Re-registering the same object succeeds;
     * an invalid object, another world's provider, or a second live provider is rejected.
     */
    bool RegisterProvider(UObject* Provider);

    /** Removes only this provider; passing any other object has no effect. */
    void UnregisterProvider(const UObject* Provider);

    /** UE world axes, m/s. Returns zero without a live provider. Position is in centimeters; time is uBot sim seconds. */
    FVector GetWindVelocity(const FVector& LocationCentimeters, double TimeSeconds) const;

protected:
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
    UPROPERTY(Transient)
    TWeakObjectPtr<UObject> ActiveProvider;
};
