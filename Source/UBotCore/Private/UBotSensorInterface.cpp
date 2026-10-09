#include "UBotSensorInterface.h"

#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "UObject/Class.h"

namespace UBot::Interfaces
{
    void FindImplementers(const AActor* Actor, const UClass* InterfaceClass, TArray<UObject*>& OutObjects)
    {
        OutObjects.Reset();
        if (!IsValid(Actor) || !InterfaceClass)
        {
            return;
        }

        if (Actor->GetClass()->ImplementsInterface(InterfaceClass))
        {
            OutObjects.Add(const_cast<AActor*>(Actor));
        }

        for (UActorComponent* Component : Actor->GetComponents())
        {
            if (IsValid(Component) && Component->GetClass()->ImplementsInterface(InterfaceClass))
            {
                OutObjects.Add(Component);
            }
        }
    }

    UObject* FindFirstImplementer(const AActor* Actor, const UClass* InterfaceClass)
    {
        if (!IsValid(Actor) || !InterfaceClass)
        {
            return nullptr;
        }

        if (Actor->GetClass()->ImplementsInterface(InterfaceClass))
        {
            return const_cast<AActor*>(Actor);
        }

        for (UActorComponent* Component : Actor->GetComponents())
        {
            if (IsValid(Component) && Component->GetClass()->ImplementsInterface(InterfaceClass))
            {
                return Component;
            }
        }
        return nullptr;
    }
}

namespace UBot::Sensors
{
    void FindSensors(const AActor* Actor, TArray<UObject*>& OutSensors)
    {
        UBot::Interfaces::FindImplementers(Actor, UUBotSensor::StaticClass(), OutSensors);
    }
}
