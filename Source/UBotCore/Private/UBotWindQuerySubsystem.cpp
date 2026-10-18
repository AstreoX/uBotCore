#include "UBotWindQuerySubsystem.h"

#include "UBotWindProvider.h"

void UUBotWindQuerySubsystem::Deinitialize()
{
    check(IsInGameThread());
    ActiveProvider.Reset();
    Super::Deinitialize();
}

bool UUBotWindQuerySubsystem::RegisterProvider(UObject* Provider)
{
    check(IsInGameThread());
    if (!IsValid(Provider) || Cast<IUBotWindProvider>(Provider) == nullptr || Provider->GetWorld() != GetWorld())
    {
        return false;
    }
    if (ActiveProvider.IsValid() && ActiveProvider.Get() != Provider)
    {
        return false;
    }
    ActiveProvider = Provider;
    return true;
}

void UUBotWindQuerySubsystem::UnregisterProvider(const UObject* Provider)
{
    check(IsInGameThread());
    if (ActiveProvider.Get() == Provider)
    {
        ActiveProvider.Reset();
    }
}

FVector UUBotWindQuerySubsystem::GetWindVelocity(const FVector& LocationCentimeters, double TimeSeconds) const
{
    check(IsInGameThread());
    const IUBotWindProvider* Provider = Cast<IUBotWindProvider>(ActiveProvider.Get());
    return Provider != nullptr ? Provider->GetWindVelocity(LocationCentimeters, TimeSeconds) : FVector::ZeroVector;
}

bool UUBotWindQuerySubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}
