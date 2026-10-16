#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "UBotBodyState.generated.h"

class USceneComponent;

/** Simulated state of a rigid body at its origin: UE world axes, SI units. Experimental. */
struct UBOTCORE_API FUBotBodyState
{
    /** Body origin, UE world axes, meters. */
    FVector PositionMeters = FVector::ZeroVector;

    /** Body to world. */
    FQuat Orientation = FQuat::Identity;

    /** World frame, m/s. */
    FVector LinearVelocityMetersPerSecond = FVector::ZeroVector;

    /** World frame, UE axes and rotation sense (+Z yaws right), rad/s. */
    FVector AngularVelocityRadiansPerSecond = FVector::ZeroVector;

    /** Kinematic acceleration of the origin (gravity not included), world frame, m/s^2. Valid when bHasLinearAcceleration. */
    FVector LinearAccelerationMetersPerSecondSquared = FVector::ZeroVector;

    /** World frame, rad/s^2. Valid when bHasAngularAcceleration. */
    FVector AngularAccelerationRadiansPerSecondSquared = FVector::ZeroVector;

    bool bHasLinearAcceleration = false;
    bool bHasAngularAcceleration = false;

    /** uBot sim time the state belongs to (never later than the current sim time). */
    double TimestampSeconds = 0.0;

    /** Who simulated the body, e.g. a physics backend name. */
    FName Source;
};

UINTERFACE(meta = (CannotImplementInterfaceInBlueprint))
class UBOTCORE_API UUBotBodyStateSource : public UInterface
{
    GENERATED_BODY()
};

/**
 * Implemented by whatever moves a body and knows its true state: a physics backend binding, a
 * vehicle's own integrator. Providing a body also claims it: nobody else should move it. Sensors may
 * read the state instead of differentiating poses. Experimental: may change between minor versions.
 */
class UBOTCORE_API IUBotBodyStateSource
{
    GENERATED_BODY()

public:
    /** True if this source moves Body (writes its transform) and can report its state. */
    virtual bool ProvidesBodyState(const USceneComponent* Body) const = 0;

    /** Latest state of Body. False if Body is not provided or has no state yet. */
    virtual bool GetBodyState(const USceneComponent* Body, FUBotBodyState& OutState) const = 0;
};

namespace UBot::BodyState
{
    /**
     * The object (Body's owner actor or one of its components) whose IUBotBodyStateSource provides Body,
     * or nullptr. The owner actor is asked first if it implements the interface, then its components in
     * GetComponents order; the first whose ProvidesBodyState(Body) is true wins. A null or invalid Body
     * has no source.
     */
    UBOTCORE_API UObject* FindSource(const USceneComponent* Body);

    /**
     * Walks Component and its attach parents; returns the first source found (see FindSource) and sets
     * OutBody to the body it provides, else returns nullptr and sets OutBody to nullptr.
     */
    UBOTCORE_API UObject* FindSourceInParents(const USceneComponent* Component, const USceneComponent*& OutBody);

    /**
     * State of the nearest provided body at or above Component (see FindSourceInParents). OutBody
     * (optional) receives that body, also when it has no state yet, and nullptr when there is none.
     * OutState is only written when this returns true.
     */
    UBOTCORE_API bool GetNearestBodyState(const USceneComponent* Component, FUBotBodyState& OutState, const USceneComponent** OutBody = nullptr);
}
