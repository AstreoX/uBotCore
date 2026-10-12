#include "UBotActuatorInterface.h"

#include "UBotSensorInterface.h"

namespace UBot::Actuation
{
    UObject* FindVelocityCommandable(const AActor* Actor)
    {
        return UBot::Interfaces::FindFirstImplementer(Actor, UUBotVelocityCommandable::StaticClass());
    }

    UObject* FindAttitudeCommandable(const AActor* Actor)
    {
        return UBot::Interfaces::FindFirstImplementer(Actor, UUBotAttitudeCommandable::StaticClass());
    }

    UObject* FindMotorCommandable(const AActor* Actor)
    {
        return UBot::Interfaces::FindFirstImplementer(Actor, UUBotMotorCommandable::StaticClass());
    }
}
