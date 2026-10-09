#include "UBotIdentityComponent.h"

#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "UBotRosConventions.h"

namespace UBot::Identity::Private
{
    const TCHAR* const DefaultBaseFrameId = TEXT("base_link");

    FString NormalizeNamespace(const FString& Value)
    {
        // Callers join the namespace with '/', so keep it free of leading/trailing separators.
        FString Result = Value.TrimStartAndEnd();
        while (Result.StartsWith(TEXT("/"), ESearchCase::CaseSensitive))
        {
            Result.RightChopInline(1);
        }
        while (Result.EndsWith(TEXT("/"), ESearchCase::CaseSensitive))
        {
            Result.LeftChopInline(1);
        }
        return Result;
    }
}

UUBotIdentityComponent::UUBotIdentityComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

FString UUBotIdentityComponent::GetResolvedRobotId() const
{
    FString Resolved = RobotId.TrimStartAndEnd();
    if (Resolved.IsEmpty())
    {
        if (const AActor* Owner = GetOwner())
        {
            Resolved = Owner->GetName();
        }
    }
    return Resolved;
}

FString UUBotIdentityComponent::GetResolvedNamespace() const
{
    const FString Configured = UBot::Identity::Private::NormalizeNamespace(Namespace);
    if (!Configured.IsEmpty())
    {
        return Configured;
    }
    return UBot::Ros::SanitizeRosName(GetResolvedRobotId());
}

FString UUBotIdentityComponent::GetBaseFrameId() const
{
    const FString Configured = BaseFrameId.TrimStartAndEnd();
    return Configured.IsEmpty() ? FString(UBot::Identity::Private::DefaultBaseFrameId) : Configured;
}

USceneComponent* UUBotIdentityComponent::GetBaseComponent() const
{
    AActor* Owner = GetOwner();
    // An empty reference already resolves to the owner root; the fallback covers stale or non-scene references.
    if (USceneComponent* Referenced = Cast<USceneComponent>(BaseComponent.GetComponent(Owner)))
    {
        return Referenced;
    }
    return Owner ? Owner->GetRootComponent() : nullptr;
}

namespace UBot::Identity
{
    UUBotIdentityComponent* Find(const AActor* Actor)
    {
        return IsValid(Actor) ? Actor->FindComponentByClass<UUBotIdentityComponent>() : nullptr;
    }

    FString ResolveRobotId(const AActor* Actor)
    {
        if (const UUBotIdentityComponent* Identity = Find(Actor))
        {
            return Identity->GetResolvedRobotId();
        }
        return Actor ? Actor->GetName() : FString();
    }

    FString ResolveNamespace(const AActor* Actor)
    {
        if (const UUBotIdentityComponent* Identity = Find(Actor))
        {
            return Identity->GetResolvedNamespace();
        }
        return UBot::Ros::SanitizeRosName(ResolveRobotId(Actor));
    }

    FString ResolveBaseFrameId(const AActor* Actor)
    {
        if (const UUBotIdentityComponent* Identity = Find(Actor))
        {
            return Identity->GetBaseFrameId();
        }
        return FString(Private::DefaultBaseFrameId);
    }

    USceneComponent* ResolveBaseComponent(const AActor* Actor)
    {
        if (const UUBotIdentityComponent* Identity = Find(Actor))
        {
            return Identity->GetBaseComponent();
        }
        return Actor ? Actor->GetRootComponent() : nullptr;
    }
}
