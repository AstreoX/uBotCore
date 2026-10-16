#include "UBotIdentityComponent.h"

#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"

namespace UBot::Identity::Private
{
    const TCHAR* const DefaultBaseFrameId = TEXT("base_link");

    bool IsNameTokenCharacter(TCHAR Character)
    {
        return (Character >= TEXT('a') && Character <= TEXT('z'))
            || (Character >= TEXT('A') && Character <= TEXT('Z'))
            || (Character >= TEXT('0') && Character <= TEXT('9'))
            || Character == TEXT('_');
    }

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
    return UBot::Identity::SanitizeNameToken(GetResolvedRobotId());
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
    FString SanitizeNameToken(const FString& Name)
    {
        FString Result;
        Result.Reserve(Name.Len() + 1);
        for (int32 Index = 0; Index < Name.Len(); ++Index)
        {
            const TCHAR Character = Name[Index];
            const TCHAR Output = Private::IsNameTokenCharacter(Character) ? Character : TEXT('_');
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
        return SanitizeNameToken(ResolveRobotId(Actor));
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
