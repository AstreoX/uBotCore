#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "UBotBodyState.h"
#include "UBotBodyStateTestTypes.generated.h"

// Test-only reflected types for UBotCore.BodyState.* (UHT cannot see types declared in a .cpp, and it does
// not allow WITH_DEV_AUTOMATION_TESTS guards, so these are compiled in every configuration).

// Component state source: provides exactly the bodies in Bodies and reports State for them while
// bHasState is set.
UCLASS(Transient, NotBlueprintable, HideDropdown)
class UUBotBodyStateTestSource : public UActorComponent, public IUBotBodyStateSource
{
    GENERATED_BODY()

public:
    TArray<TWeakObjectPtr<const USceneComponent>> Bodies;
    FUBotBodyState State;
    bool bHasState = true;

    virtual bool ProvidesBodyState(const USceneComponent* Body) const override
    {
        if (Body == nullptr)
        {
            return false;
        }
        for (const TWeakObjectPtr<const USceneComponent>& Provided : Bodies)
        {
            if (Provided.Get() == Body)
            {
                return true;
            }
        }
        return false;
    }

    virtual bool GetBodyState(const USceneComponent* Body, FUBotBodyState& OutState) const override
    {
        if (!bHasState || !ProvidesBodyState(Body))
        {
            return false;
        }
        OutState = State;
        return true;
    }
};

// Actor-level state source (a vehicle that integrates its own body): provides every scene component
// it owns while bProvidesOwnBodies is set, and reports State for them.
UCLASS(Transient, NotBlueprintable, NotPlaceable, HideDropdown)
class AUBotBodyStateTestActor : public AActor, public IUBotBodyStateSource
{
    GENERATED_BODY()

public:
    bool bProvidesOwnBodies = true;
    FUBotBodyState State;

    virtual bool ProvidesBodyState(const USceneComponent* Body) const override
    {
        return bProvidesOwnBodies && Body != nullptr && Body->GetOwner() == this;
    }

    virtual bool GetBodyState(const USceneComponent* Body, FUBotBodyState& OutState) const override
    {
        if (!ProvidesBodyState(Body))
        {
            return false;
        }
        OutState = State;
        return true;
    }
};
