#include "UBotBodyState.h"

#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"

namespace UBot::BodyState
{
    UObject* FindSource(const USceneComponent* Body)
    {
        if (!IsValid(Body))
        {
            return nullptr;
        }
        AActor* Owner = Body->GetOwner();
        if (!IsValid(Owner))
        {
            return nullptr;
        }

        // The actor first, then its components in component order.
        if (const IUBotBodyStateSource* Source = Cast<IUBotBodyStateSource>(Owner))
        {
            if (Source->ProvidesBodyState(Body))
            {
                return Owner;
            }
        }
        for (UActorComponent* Component : Owner->GetComponents())
        {
            if (!IsValid(Component))
            {
                continue;
            }
            if (const IUBotBodyStateSource* Source = Cast<IUBotBodyStateSource>(Component))
            {
                if (Source->ProvidesBodyState(Body))
                {
                    return Component;
                }
            }
        }
        return nullptr;
    }

    UObject* FindSourceInParents(const USceneComponent* Component, const USceneComponent*& OutBody)
    {
        OutBody = nullptr;
        for (const USceneComponent* Current = Component; IsValid(Current); Current = Current->GetAttachParent())
        {
            if (UObject* Source = FindSource(Current))
            {
                OutBody = Current;
                return Source;
            }
        }
        return nullptr;
    }

    bool GetNearestBodyState(const USceneComponent* Component, FUBotBodyState& OutState, const USceneComponent** OutBody)
    {
        const USceneComponent* Body = nullptr;
        UObject* SourceObject = FindSourceInParents(Component, Body);
        if (OutBody)
        {
            *OutBody = Body;
        }
        if (!SourceObject)
        {
            return false;
        }

        const IUBotBodyStateSource* Source = Cast<IUBotBodyStateSource>(SourceObject);
        FUBotBodyState State;
        if (!Source || !Source->GetBodyState(Body, State))
        {
            return false;
        }
        OutState = State;
        return true;
    }
}
