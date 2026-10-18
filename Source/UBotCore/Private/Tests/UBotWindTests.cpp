#include "CoreMinimal.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "UBotWindQuerySubsystem.h"
#include "UBotWindTestTypes.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace UBotWindTests
{
    class FScopedWindQuery
    {
    public:
        UE_NONCOPYABLE(FScopedWindQuery);

        FScopedWindQuery()
            : World(NewObject<UWorld>(GetTransientPackage()))
            , Query(NewObject<UUBotWindQuerySubsystem>(World.Get()))
        {
            // Do not initialize the whole world: host plugins may register their own wind provider.
            World->WorldType = EWorldType::Game;
        }

        UUBotWindTestProvider* NewProvider() const
        {
            return NewObject<UUBotWindTestProvider>(World.Get());
        }

        TStrongObjectPtr<UWorld> World;
        TStrongObjectPtr<UUBotWindQuerySubsystem> Query;
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotWindQueryTest, "UBotCore.Wind.Query", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotWindQueryTest::RunTest(const FString& Parameters)
{
    UBotWindTests::FScopedWindQuery Fixture;
    const FVector Position(125.5, -207.0, 340.25);
    constexpr double TimeSeconds = 12.875;
    TestEqual(TEXT("No provider means still air"), Fixture.Query->GetWindVelocity(Position, TimeSeconds), FVector::ZeroVector);

    UUBotWindTestProvider* Provider = Fixture.NewProvider();
    Provider->Velocity = FVector(-2.5, 4.75, 0.125);
    TestTrue(TEXT("A provider in this world registers"), Fixture.Query->RegisterProvider(Provider));
    TestEqual(TEXT("Velocity passes through in UE axes and m/s"), Fixture.Query->GetWindVelocity(Position, TimeSeconds), Provider->Velocity);
    TestEqual(TEXT("Position passes through without unit conversion"), Provider->LastLocationCentimeters, Position);
    TestEqual(TEXT("Simulation time passes through without rounding"), Provider->LastTimeSeconds, TimeSeconds);
    TestEqual(TEXT("One request makes one provider query"), Provider->QueryCount, 1);

    Provider->Velocity = FVector(3.0, -1.0, 2.0);
    TestEqual(TEXT("Queries read current data rather than a cache"), Fixture.Query->GetWindVelocity(FVector::ZeroVector, 0.0), Provider->Velocity);
    TestEqual(TEXT("New position reaches the provider"), Provider->LastLocationCentimeters, FVector::ZeroVector);
    TestEqual(TEXT("New time reaches the provider"), Provider->LastTimeSeconds, 0.0);

    Fixture.Query->UnregisterProvider(Provider);
    TestEqual(TEXT("Unregistered provider means still air"), Fixture.Query->GetWindVelocity(Position, TimeSeconds), FVector::ZeroVector);
    TestEqual(TEXT("Unregistered provider is not queried"), Provider->QueryCount, 2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotWindRegistrationTest, "UBotCore.Wind.Registration", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotWindRegistrationTest::RunTest(const FString& Parameters)
{
    UBotWindTests::FScopedWindQuery Fixture;
    UBotWindTests::FScopedWindQuery OtherWorld;
    UUBotWindTestProvider* First = Fixture.NewProvider();
    UUBotWindTestProvider* Second = Fixture.NewProvider();
    UUBotWindTestProvider* Foreign = OtherWorld.NewProvider();
    UUBotWindTestProvider* Worldless = NewObject<UUBotWindTestProvider>();
    UObject* WrongType = NewObject<UUBotWindQuerySubsystem>(Fixture.World.Get());
    First->Velocity = FVector(1.0, 2.0, 3.0);
    Second->Velocity = FVector(4.0, 5.0, 6.0);

    TestFalse(TEXT("Null provider is rejected"), Fixture.Query->RegisterProvider(nullptr));
    TestFalse(TEXT("Object without wind interface is rejected"), Fixture.Query->RegisterProvider(WrongType));
    TestFalse(TEXT("Provider without a world is rejected"), Fixture.Query->RegisterProvider(Worldless));
    TestFalse(TEXT("Provider from another world is rejected"), Fixture.Query->RegisterProvider(Foreign));
    TestTrue(TEXT("The first provider registers"), Fixture.Query->RegisterProvider(First));
    TestTrue(TEXT("Registering the same provider is idempotent"), Fixture.Query->RegisterProvider(First));
    TestFalse(TEXT("Another live provider cannot silently replace it"), Fixture.Query->RegisterProvider(Second));

    Fixture.Query->UnregisterProvider(nullptr);
    Fixture.Query->UnregisterProvider(WrongType);
    Fixture.Query->UnregisterProvider(Foreign);
    Fixture.Query->UnregisterProvider(Second);
    TestEqual(TEXT("Wrong unregister requests retain the first provider"), Fixture.Query->GetWindVelocity(FVector::ZeroVector, 0.0), First->Velocity);
    TestEqual(TEXT("Rejected provider was not queried"), Second->QueryCount, 0);

    Fixture.Query->UnregisterProvider(First);
    TestTrue(TEXT("A replacement registers after explicit unregister"), Fixture.Query->RegisterProvider(Second));
    Fixture.Query->UnregisterProvider(First);
    TestEqual(TEXT("An old provider cannot unregister its replacement"), Fixture.Query->GetWindVelocity(FVector::ZeroVector, 0.0), Second->Velocity);
    Fixture.Query->Deinitialize();
    TestEqual(TEXT("Deinitialization clears the provider"), Fixture.Query->GetWindVelocity(FVector::ZeroVector, 0.0), FVector::ZeroVector);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotWindProviderLifetimeTest, "UBotCore.Wind.ProviderLifetime", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotWindProviderLifetimeTest::RunTest(const FString& Parameters)
{
    UBotWindTests::FScopedWindQuery Fixture;
    UUBotWindTestProvider* Provider = Fixture.NewProvider();
    Provider->Velocity = FVector(2.0, 0.0, 0.0);
    TWeakObjectPtr<UUBotWindTestProvider> WeakProvider(Provider);
    TestTrue(TEXT("Transient provider registers"), Fixture.Query->RegisterProvider(Provider));
    Provider = nullptr;
    CollectGarbage(RF_NoFlags);
    TestFalse(TEXT("The query subsystem does not keep its provider alive"), WeakProvider.IsValid());
    TestEqual(TEXT("Collected provider means still air"), Fixture.Query->GetWindVelocity(FVector::ZeroVector, 1.0), FVector::ZeroVector);

    UUBotWindTestProvider* Replacement = Fixture.NewProvider();
    Replacement->Velocity = FVector(0.0, 3.0, 0.0);
    TestTrue(TEXT("A collected provider can be replaced"), Fixture.Query->RegisterProvider(Replacement));
    TestEqual(TEXT("The replacement supplies wind"), Fixture.Query->GetWindVelocity(FVector::ZeroVector, 1.0), Replacement->Velocity);
    Replacement->MarkAsGarbage();
    TestFalse(TEXT("A destroyed provider cannot register"), Fixture.Query->RegisterProvider(Replacement));
    TestEqual(TEXT("A destroyed provider is ignored before collection"), Fixture.Query->GetWindVelocity(FVector::ZeroVector, 1.0), FVector::ZeroVector);

    UUBotWindTestProvider* FinalProvider = Fixture.NewProvider();
    TestTrue(TEXT("A destroyed provider can be replaced before collection"), Fixture.Query->RegisterProvider(FinalProvider));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotWindWorldIsolationTest, "UBotCore.Wind.WorldIsolation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotWindWorldIsolationTest::RunTest(const FString& Parameters)
{
    UBotWindTests::FScopedWindQuery FirstWorld;
    UBotWindTests::FScopedWindQuery SecondWorld;
    UUBotWindTestProvider* First = FirstWorld.NewProvider();
    UUBotWindTestProvider* Second = SecondWorld.NewProvider();
    First->Velocity = FVector(1.0, 0.0, 0.0);
    Second->Velocity = FVector(0.0, 2.0, 0.0);
    TestTrue(TEXT("First world registers its provider"), FirstWorld.Query->RegisterProvider(First));
    TestTrue(TEXT("Second world registers its provider"), SecondWorld.Query->RegisterProvider(Second));
    TestEqual(TEXT("First world reads only its provider"), FirstWorld.Query->GetWindVelocity(FVector::ZeroVector, 1.0), First->Velocity);
    TestEqual(TEXT("Second world reads only its provider"), SecondWorld.Query->GetWindVelocity(FVector::ZeroVector, 2.0), Second->Velocity);
    TestEqual(TEXT("First world's query time is independent"), First->LastTimeSeconds, 1.0);
    TestEqual(TEXT("Second world's query time is independent"), Second->LastTimeSeconds, 2.0);
    FirstWorld.Query->UnregisterProvider(First);
    TestEqual(TEXT("Removing first world's provider does not affect the second"), SecondWorld.Query->GetWindVelocity(FVector::ZeroVector, 3.0), Second->Velocity);

    TestTrue(TEXT("Game worlds support wind queries"), FirstWorld.Query->ShouldCreateSubsystem(FirstWorld.World.Get()));
    FirstWorld.World->WorldType = EWorldType::PIE;
    TestTrue(TEXT("PIE worlds support wind queries"), FirstWorld.Query->ShouldCreateSubsystem(FirstWorld.World.Get()));
    FirstWorld.World->WorldType = EWorldType::Editor;
    TestFalse(TEXT("Editor worlds do not create a wind query subsystem"), FirstWorld.Query->ShouldCreateSubsystem(FirstWorld.World.Get()));
    FirstWorld.World->WorldType = EWorldType::EditorPreview;
    TestFalse(TEXT("Preview worlds do not create a wind query subsystem"), FirstWorld.Query->ShouldCreateSubsystem(FirstWorld.World.Get()));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
