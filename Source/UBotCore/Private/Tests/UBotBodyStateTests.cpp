#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "UBotBodyState.h"
#include "UBotBodyStateTestTypes.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace UBotBodyStateTests
{
    // Transient game world for tests that need spawned actors; destroyed when the scope ends.
    class FScopedTestWorld
    {
    public:
        UE_NONCOPYABLE(FScopedTestWorld);

        FScopedTestWorld()
            : World(UWorld::CreateWorld(EWorldType::Game, false))
        {
        }

        ~FScopedTestWorld()
        {
            if (World)
            {
                World->DestroyWorld(false);
            }
        }

        UWorld* Get() const
        {
            return World;
        }

        template <typename ActorType = AActor>
        ActorType* SpawnActor(const TCHAR* Name) const
        {
            if (!World)
            {
                return nullptr;
            }
            FActorSpawnParameters Params;
            Params.Name = FName(Name);
            Params.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
            return World->SpawnActor<ActorType>(Params);
        }

    private:
        UWorld* World = nullptr;
    };

    FUBotBodyState MakeState(const FVector& PositionMeters, FName Source)
    {
        FUBotBodyState State;
        State.PositionMeters = PositionMeters;
        State.TimestampSeconds = 2.5;
        State.Source = Source;
        return State;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotBodyStateDefaultsTest, "UBotCore.BodyState.Defaults", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotBodyStateDefaultsTest::RunTest(const FString& Parameters)
{
    FUBotBodyState State;
    TestTrue(TEXT("Position starts at the origin"), State.PositionMeters.IsZero());
    TestTrue(TEXT("Orientation starts as identity"), State.Orientation.Equals(FQuat::Identity, 0.0));
    TestTrue(TEXT("Linear velocity starts at zero"), State.LinearVelocityMetersPerSecond.IsZero());
    TestTrue(TEXT("Angular velocity starts at zero"), State.AngularVelocityRadiansPerSecond.IsZero());
    TestTrue(TEXT("Linear acceleration starts at zero"), State.LinearAccelerationMetersPerSecondSquared.IsZero());
    TestTrue(TEXT("Angular acceleration starts at zero"), State.AngularAccelerationRadiansPerSecondSquared.IsZero());
    TestFalse(TEXT("Linear acceleration is not valid until set"), State.bHasLinearAcceleration);
    TestFalse(TEXT("Angular acceleration is not valid until set"), State.bHasAngularAcceleration);
    TestEqual(TEXT("Timestamp starts at zero"), State.TimestampSeconds, 0.0);
    TestTrue(TEXT("Source starts empty"), State.Source.IsNone());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotBodyStateLookupTest, "UBotCore.BodyState.Lookup", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotBodyStateLookupTest::RunTest(const FString& Parameters)
{
    using namespace UBotBodyStateTests;

    // Null inputs: nothing to find, and the outputs are reset rather than left stale.
    {
        const USceneComponent* Sentinel = NewObject<USceneComponent>(GetTransientPackage());
        const USceneComponent* OutBody = Sentinel;
        FUBotBodyState State = MakeState(FVector(9.0, 9.0, 9.0), TEXT("Untouched"));
        const USceneComponent* NearestBody = Sentinel;
        TestNull(TEXT("FindSource(nullptr)"), UBot::BodyState::FindSource(nullptr));
        TestNull(TEXT("FindSourceInParents(nullptr)"), UBot::BodyState::FindSourceInParents(nullptr, OutBody));
        TestNull(TEXT("FindSourceInParents(nullptr) resets the body"), OutBody);
        TestFalse(TEXT("GetNearestBodyState(nullptr)"), UBot::BodyState::GetNearestBodyState(nullptr, State, &NearestBody));
        TestNull(TEXT("GetNearestBodyState(nullptr) resets the body"), NearestBody);
        TestEqual(TEXT("A failed lookup leaves the state alone"), State.PositionMeters, FVector(9.0, 9.0, 9.0));
        TestFalse(TEXT("GetNearestBodyState(nullptr) without a body output"), UBot::BodyState::GetNearestBodyState(nullptr, State));

        // A component without an owner has no actor to look at.
        USceneComponent* Orphan = NewObject<USceneComponent>(GetTransientPackage());
        TestNull(TEXT("A component without an owner has no source"), UBot::BodyState::FindSource(Orphan));
    }

    FScopedTestWorld TestWorld;
    if (!TestNotNull(TEXT("Test world"), TestWorld.Get()))
    {
        return false;
    }

    AActor* Actor = TestWorld.SpawnActor(TEXT("UBotTest-BodyStates"));
    AActor* OtherActor = TestWorld.SpawnActor(TEXT("UBotTest-OtherBodies"));
    if (!TestNotNull(TEXT("Actor"), Actor) || !TestNotNull(TEXT("Other actor"), OtherActor))
    {
        return false;
    }

    // Root > Hull > Arm > Hand, and an unattached Free component.
    USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
    Actor->SetRootComponent(Root);
    USceneComponent* Hull = NewObject<USceneComponent>(Actor, TEXT("Hull"));
    Hull->SetupAttachment(Root);
    USceneComponent* Arm = NewObject<USceneComponent>(Actor, TEXT("Arm"));
    Arm->SetupAttachment(Hull);
    USceneComponent* Hand = NewObject<USceneComponent>(Actor, TEXT("Hand"));
    Hand->SetupAttachment(Arm);
    USceneComponent* Free = NewObject<USceneComponent>(Actor, TEXT("Free"));

    // No source yet.
    TestNull(TEXT("An actor without a source provides nothing"), UBot::BodyState::FindSource(Hull));

    UUBotBodyStateTestSource* First = NewObject<UUBotBodyStateTestSource>(Actor, TEXT("FirstSource"));
    First->State = MakeState(FVector(1.0, 2.0, 3.0), TEXT("First"));
    First->Bodies.Add(Hull);

    // Provided and unprovided bodies of the same actor.
    TestTrue(TEXT("The source provides the Hull"), UBot::BodyState::FindSource(Hull) == First);
    TestNull(TEXT("The Arm is not provided, though its parent is"), UBot::BodyState::FindSource(Arm));
    TestNull(TEXT("The Root is not provided"), UBot::BodyState::FindSource(Root));
    TestNull(TEXT("The Free component is not provided"), UBot::BodyState::FindSource(Free));

    // The source of another actor does not claim this actor's components.
    UUBotBodyStateTestSource* Foreign = NewObject<UUBotBodyStateTestSource>(OtherActor, TEXT("ForeignSource"));
    Foreign->Bodies.Add(Arm);
    TestNull(TEXT("Only sources of the body's own actor are asked"), UBot::BodyState::FindSource(Arm));

    // Through the attach chain: the nearest provided body wins.
    const USceneComponent* Body = nullptr;
    TestTrue(TEXT("The Hand belongs to the Hull's source"), UBot::BodyState::FindSourceInParents(Hand, Body) == First);
    TestTrue(TEXT("The provided body is the Hull"), Body == Hull);
    TestTrue(TEXT("A provided body is its own nearest body"), UBot::BodyState::FindSourceInParents(Hull, Body) == First && Body == Hull);
    Body = Hand;
    TestNull(TEXT("The Root has no provided body above it"), UBot::BodyState::FindSourceInParents(Root, Body));
    TestNull(TEXT("and the output body is reset"), Body);
    Body = Hand;
    TestNull(TEXT("A component outside the chain finds nothing"), UBot::BodyState::FindSourceInParents(Free, Body));
    TestNull(TEXT("and resets the output body"), Body);

    First->Bodies.Add(Arm);
    TestTrue(TEXT("Now the Arm is nearer than the Hull"), UBot::BodyState::FindSourceInParents(Hand, Body) == First && Body == Arm);

    // A chain may cross actors: a sensor of another actor attached to a provided body.
    USceneComponent* Mounted = NewObject<USceneComponent>(OtherActor, TEXT("Mounted"));
    Mounted->SetupAttachment(Hand);
    TestTrue(TEXT("A component of another actor attached to a body finds its source"), UBot::BodyState::FindSourceInParents(Mounted, Body) == First && Body == Arm);
    TestNull(TEXT("while the other actor's own source does not provide the Mounted component itself"), UBot::BodyState::FindSource(Mounted));

    // Component order decides between two sources that provide the same body.
    UUBotBodyStateTestSource* Second = NewObject<UUBotBodyStateTestSource>(Actor, TEXT("SecondSource"));
    Second->State = MakeState(FVector(4.0, 5.0, 6.0), TEXT("Second"));
    Second->Bodies.Add(Hull);
    TestTrue(TEXT("The first source in component order wins"), UBot::BodyState::FindSource(Hull) == First);
    First->Bodies.Reset();
    TestTrue(TEXT("The next source takes over when the first stops providing"), UBot::BodyState::FindSource(Hull) == Second);
    TestNull(TEXT("A body nobody provides"), UBot::BodyState::FindSource(Arm));

    // The state of the nearest provided body.
    FUBotBodyState State;
    const USceneComponent* NearestBody = nullptr;
    if (TestTrue(TEXT("The nearest state is found from the Hand"), UBot::BodyState::GetNearestBodyState(Hand, State, &NearestBody)))
    {
        TestTrue(TEXT("The state belongs to the Hull"), NearestBody == Hull);
        TestEqual(TEXT("Position of the second source"), State.PositionMeters, FVector(4.0, 5.0, 6.0));
        TestEqual(TEXT("Source name of the second source"), State.Source.ToString(), FString(TEXT("Second")));
        TestEqual(TEXT("Timestamp"), State.TimestampSeconds, 2.5);
    }
    State = MakeState(FVector(9.0, 9.0, 9.0), TEXT("Untouched"));
    TestTrue(TEXT("The body output is optional"), UBot::BodyState::GetNearestBodyState(Hull, State));
    TestEqual(TEXT("A provided body reports its own state"), State.Source.ToString(), FString(TEXT("Second")));

    // A provided body without a state yet is still the nearest body; the output state is not written.
    State = MakeState(FVector(9.0, 9.0, 9.0), TEXT("Untouched"));
    Second->bHasState = false;
    NearestBody = nullptr;
    TestFalse(TEXT("No state yet"), UBot::BodyState::GetNearestBodyState(Hand, State, &NearestBody));
    TestTrue(TEXT("The provided body is still reported"), NearestBody == Hull);
    TestEqual(TEXT("The output state is left alone"), State.Source.ToString(), FString(TEXT("Untouched")));
    TestTrue(TEXT("The lookup itself still finds the source"), UBot::BodyState::FindSource(Hull) == Second);
    Second->bHasState = true;

    // Nothing above the component is provided.
    NearestBody = Hull;
    TestFalse(TEXT("The Free component has no state"), UBot::BodyState::GetNearestBodyState(Free, State, &NearestBody));
    TestNull(TEXT("and no body"), NearestBody);

    // An actor that is itself a source, asked before its components.
    AUBotBodyStateTestActor* Vehicle = TestWorld.SpawnActor<AUBotBodyStateTestActor>(TEXT("UBotTest-Vehicle"));
    if (!TestNotNull(TEXT("Vehicle"), Vehicle))
    {
        return false;
    }
    Vehicle->State = MakeState(FVector(7.0, 8.0, 9.0), TEXT("Vehicle"));
    USceneComponent* VehicleRoot = NewObject<USceneComponent>(Vehicle, TEXT("Root"));
    Vehicle->SetRootComponent(VehicleRoot);
    USceneComponent* Payload = NewObject<USceneComponent>(Vehicle, TEXT("Payload"));
    Payload->SetupAttachment(VehicleRoot);
    UUBotBodyStateTestSource* VehicleComponent = NewObject<UUBotBodyStateTestSource>(Vehicle, TEXT("ComponentSource"));
    VehicleComponent->State = MakeState(FVector(1.0, 1.0, 1.0), TEXT("Component"));
    VehicleComponent->Bodies.Add(Payload);

    TestTrue(TEXT("The actor provides its root"), UBot::BodyState::FindSource(VehicleRoot) == Vehicle);
    TestTrue(TEXT("The actor is asked before its components"), UBot::BodyState::FindSource(Payload) == Vehicle);
    NearestBody = nullptr;
    if (TestTrue(TEXT("State through the actor-level source"), UBot::BodyState::GetNearestBodyState(Payload, State, &NearestBody)))
    {
        TestTrue(TEXT("The Payload is its own nearest body"), NearestBody == Payload);
        TestEqual(TEXT("The actor's state"), State.Source.ToString(), FString(TEXT("Vehicle")));
        TestEqual(TEXT("The actor's position"), State.PositionMeters, FVector(7.0, 8.0, 9.0));
    }

    Vehicle->bProvidesOwnBodies = false;
    TestTrue(TEXT("An actor that stops providing hands over to its component"), UBot::BodyState::FindSource(Payload) == VehicleComponent);
    TestNull(TEXT("and the root is nobody's"), UBot::BodyState::FindSource(VehicleRoot));
    TestTrue(TEXT("The Payload's state comes from the component"), UBot::BodyState::GetNearestBodyState(Payload, State) && State.Source == FName(TEXT("Component")));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
