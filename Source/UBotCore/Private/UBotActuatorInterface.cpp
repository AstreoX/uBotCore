#include "UBotActuatorInterface.h"

#include "UBotSensorInterface.h"

namespace UBot::Actuation
{
    UObject* FindVelocityCommandable(const AActor* Actor)
    {
        return UBot::Interfaces::FindFirstImplementer(Actor, UUBotVelocityCommandable::StaticClass());
    }
}
