#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AI/Navigation/NavRelevantInterface.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/WorldSettings.h"
#include "Interfaces/Interface_AssetUserData.h"
#include "UBotActuatorInterface.h"
#include "UBotIdentityComponent.h"
#include "UBotPerceptionMedium.h"
#include "UBotRosConventions.h"
#include "UBotSensorInterface.h"

namespace UBotInterfaceTests
{
    FUBotMediumCell MakeCell(const FVector& Position, float RadiusCentimeters, float Density, float EdgeFactor = 1.0f)
    {
        FUBotMediumCell Cell;
        Cell.WorldPositionCentimeters = Position;
        Cell.RadiusCentimeters = RadiusCentimeters;
        Cell.Density = Density;
        Cell.EdgeFactor = EdgeFactor;
        return Cell;
    }

    FUBotMediumSnapshot MakeSnapshot(std::initializer_list<FUBotMediumCell> Cells, bool bActive = true)
    {
        FUBotMediumSnapshot Snapshot;
        Snapshot.bActive = bActive;
        Snapshot.Cells = Cells;
        UBot::Medium::FinalizeSnapshot(Snapshot);
        return Snapshot;
    }

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

        AActor* SpawnActor(const TCHAR* Name) const
        {
            if (!World)
            {
                return nullptr;
            }
            FActorSpawnParameters Params;
            Params.Name = FName(Name);
            Params.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
            return World->SpawnActor<AActor>(Params);
        }

    private:
        UWorld* World = nullptr;
    };

    const FVector Origin(0.0, 0.0, 0.0);
    const FVector AlongX(1000.0, 0.0, 0.0);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotMediumEffectiveDensityTest, "UBotCore.Medium.EffectiveDensity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotMediumEffectiveDensityTest::RunTest(const FString& Parameters)
{
    using namespace UBotInterfaceTests;

    TestEqual(TEXT("Density is scaled by the edge factor"), UBot::Medium::EffectiveDensity(MakeCell(Origin, 100.0f, 0.8f, 0.5f)), 0.4f);
    TestEqual(TEXT("Density above 1 is clamped"), UBot::Medium::EffectiveDensity(MakeCell(Origin, 100.0f, 1.5f, 1.0f)), 1.0f);
    TestEqual(TEXT("Negative density counts as 0"), UBot::Medium::EffectiveDensity(MakeCell(Origin, 100.0f, -0.5f, 1.0f)), 0.0f);
    TestEqual(TEXT("Edge factor above 1 is clamped"), UBot::Medium::EffectiveDensity(MakeCell(Origin, 100.0f, 0.6f, 2.0f)), 0.6f);
    TestEqual(TEXT("Negative edge factor counts as 0"), UBot::Medium::EffectiveDensity(MakeCell(Origin, 100.0f, 0.6f, -1.0f)), 0.0f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotMediumFinalizeSnapshotTest, "UBotCore.Medium.FinalizeSnapshotBounds", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotMediumFinalizeSnapshotTest::RunTest(const FString& Parameters)
{
    using namespace UBotInterfaceTests;

    const FUBotMediumSnapshot Mixed = MakeSnapshot({
        MakeCell(FVector(0.0, 0.0, 0.0), 100.0f, 1.0f),
        MakeCell(FVector(500.0, 0.0, 0.0), 50.0f, 1.0f) });
    TestTrue(TEXT("Bounds are valid with cells"), Mixed.BoundsCentimeters.IsValid != 0);
    TestEqual(TEXT("Bounds min covers every cell radius"), Mixed.BoundsCentimeters.Min, FVector(-100.0, -100.0, -100.0));
    TestEqual(TEXT("Bounds max covers every cell radius"), Mixed.BoundsCentimeters.Max, FVector(550.0, 100.0, 100.0));

    // Legacy fields used one radius for all cells and expanded the position box by it.
    const TArray<FVector> Positions = { FVector(0.0, 0.0, 0.0), FVector(200.0, -50.0, 10.0), FVector(-30.0, 80.0, 0.0) };
    FUBotMediumSnapshot Uniform;
    FBox LegacyBounds(ForceInit);
    for (const FVector& Position : Positions)
    {
        Uniform.Cells.Add(MakeCell(Position, 75.0f, 1.0f));
        LegacyBounds += Position;
    }
    LegacyBounds = LegacyBounds.ExpandBy(75.0f);
    UBot::Medium::FinalizeSnapshot(Uniform);
    TestEqual(TEXT("Uniform radius matches legacy bounds min"), Uniform.BoundsCentimeters.Min, LegacyBounds.Min);
    TestEqual(TEXT("Uniform radius matches legacy bounds max"), Uniform.BoundsCentimeters.Max, LegacyBounds.Max);

    const FUBotMediumSnapshot Tiny = MakeSnapshot({ MakeCell(FVector(10.0, 20.0, 30.0), 0.25f, 1.0f) });
    TestEqual(TEXT("Sub-centimeter radius expands by 1 cm (min)"), Tiny.BoundsCentimeters.Min, FVector(9.0, 19.0, 29.0));
    TestEqual(TEXT("Sub-centimeter radius expands by 1 cm (max)"), Tiny.BoundsCentimeters.Max, FVector(11.0, 21.0, 31.0));

    FUBotMediumSnapshot Empty;
    Empty.BoundsCentimeters = FBox(FVector(0.0), FVector(1.0));
    UBot::Medium::FinalizeSnapshot(Empty);
    TestFalse(TEXT("No cells -> invalid bounds"), Empty.BoundsCentimeters.IsValid != 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotMediumSegmentHitTest, "UBotCore.Medium.SegmentHit", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotMediumSegmentHitTest::RunTest(const FString& Parameters)
{
    using namespace UBotInterfaceTests;

    float Distance = -1.0f;
    float Density = -1.0f;

    // Same case as AgentSensorCore.SmokeInterference.LidarStopsAtEffectiveSmoke.
    const TArray<FUBotMediumSnapshot> Ahead = { MakeSnapshot({ MakeCell(FVector(300.0, 0.0, 0.0), 100.0f, 1.0f) }) };
    TestTrue(TEXT("Ray hits the cell ahead"), UBot::Medium::FindFirstIntersection(Origin, AlongX, Ahead, 0.1f, Distance, Density));
    TestEqual(TEXT("Entry distance is the near sphere boundary"), Distance, 200.0f, 0.001f);
    TestEqual(TEXT("Effective density is reported"), Density, 1.0f, 0.001f);

    const TArray<FUBotMediumSnapshot> Offset = { MakeSnapshot({ MakeCell(FVector(500.0, 60.0, 0.0), 100.0f, 1.0f) }) };
    TestTrue(TEXT("Ray hits an off-axis cell"), UBot::Medium::FindFirstIntersection(Origin, AlongX, Offset, 0.1f, Distance, Density));
    TestEqual(TEXT("Off-axis entry distance"), Distance, 420.0f, 0.001f);

    // Direction (0.6, 0.8, 0); the cell sits 500 cm along the ray and 60 cm to the side.
    const FVector ObliqueStart(100.0, 100.0, 0.0);
    const FVector ObliqueEnd(700.0, 900.0, 0.0);
    const TArray<FUBotMediumSnapshot> Oblique = { MakeSnapshot({ MakeCell(FVector(352.0, 536.0, 0.0), 100.0f, 1.0f) }) };
    TestTrue(TEXT("Oblique ray hits"), UBot::Medium::FindFirstIntersection(ObliqueStart, ObliqueEnd, Oblique, 0.1f, Distance, Density));
    TestEqual(TEXT("Oblique entry distance"), Distance, 420.0f, 0.01f);

    TestTrue(TEXT("Start inside a cell is a hit"), UBot::Medium::FindFirstIntersection(FVector(300.0, 50.0, 0.0), AlongX, Ahead, 0.1f, Distance, Density));
    TestEqual(TEXT("Start inside a cell reports distance 0"), Distance, 0.0f);

    // Radius is raised to 1 cm by the math; the bounds must follow or the early-out would drop this hit.
    const TArray<FUBotMediumSnapshot> Tiny = { MakeSnapshot({ MakeCell(FVector(100.0, 0.5, 0.0), 0.25f, 1.0f) }) };
    TestTrue(TEXT("Sub-centimeter cell is hit with a 1 cm radius"), UBot::Medium::FindFirstIntersection(Origin, AlongX, Tiny, 0.1f, Distance, Density));
    TestEqual(TEXT("Sub-centimeter cell entry distance"), Distance, 100.0f - FMath::Sqrt(0.75f), 0.001f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotMediumSegmentMissTest, "UBotCore.Medium.SegmentMiss", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotMediumSegmentMissTest::RunTest(const FString& Parameters)
{
    using namespace UBotInterfaceTests;

    float Distance = -1.0f;
    float Density = -1.0f;

    const TArray<FUBotMediumSnapshot> NoSnapshots;
    TestFalse(TEXT("No snapshots is a miss"), UBot::Medium::FindFirstIntersection(Origin, AlongX, NoSnapshots, 0.1f, Distance, Density));
    TestEqual(TEXT("Miss resets the distance"), Distance, 0.0f);
    TestEqual(TEXT("Miss resets the density"), Density, 0.0f);

    // Bounding boxes overlap but the sphere stays 127 cm off the ray.
    const TArray<FUBotMediumSnapshot> Beside = { MakeSnapshot({ MakeCell(FVector(500.0, 90.0, 90.0), 100.0f, 1.0f) }) };
    TestFalse(TEXT("Cell beside the ray is missed"), UBot::Medium::FindFirstIntersection(Origin, AlongX, Beside, 0.1f, Distance, Density));

    // Not finalized, so the sphere test itself has to reject these.
    FUBotMediumSnapshot Behind;
    Behind.Cells.Add(MakeCell(FVector(-50.0, 0.0, 0.0), 40.0f, 1.0f));
    TestFalse(TEXT("Cell behind the start is missed"), UBot::Medium::FindFirstIntersection(Origin, AlongX, { Behind }, 0.1f, Distance, Density));

    FUBotMediumSnapshot BeyondEnd;
    BeyondEnd.Cells.Add(MakeCell(FVector(300.0, 0.0, 0.0), 100.0f, 1.0f));
    TestFalse(TEXT("Cell beyond the segment end is missed"), UBot::Medium::FindFirstIntersection(Origin, FVector(150.0, 0.0, 0.0), { BeyondEnd }, 0.1f, Distance, Density));
    UBot::Medium::FinalizeSnapshot(BeyondEnd);
    TestFalse(TEXT("Cell beyond the segment end is missed with bounds"), UBot::Medium::FindFirstIntersection(Origin, FVector(150.0, 0.0, 0.0), { BeyondEnd }, 0.1f, Distance, Density));

    TestFalse(TEXT("Zero-length segment is a miss even inside a cell"),
        UBot::Medium::FindFirstIntersection(FVector(300.0, 0.0, 0.0), FVector(300.0, 0.0, 0.0), { BeyondEnd }, 0.1f, Distance, Density));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotMediumDensityThresholdTest, "UBotCore.Medium.DensityThreshold", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotMediumDensityThresholdTest::RunTest(const FString& Parameters)
{
    using namespace UBotInterfaceTests;

    float Distance = 0.0f;
    float Density = 0.0f;

    const TArray<FUBotMediumSnapshot> Thin = { MakeSnapshot({ MakeCell(FVector(300.0, 0.0, 0.0), 100.0f, 0.05f) }) };
    TestFalse(TEXT("Density below the threshold is ignored"), UBot::Medium::FindFirstIntersection(Origin, AlongX, Thin, 0.1f, Distance, Density));

    const TArray<FUBotMediumSnapshot> Exact = { MakeSnapshot({ MakeCell(FVector(300.0, 0.0, 0.0), 100.0f, 0.5f, 0.5f) }) };
    TestTrue(TEXT("Density equal to the threshold is a hit"), UBot::Medium::FindFirstIntersection(Origin, AlongX, Exact, 0.25f, Distance, Density));
    TestEqual(TEXT("Reported density at the threshold"), Density, 0.25f);
    TestFalse(TEXT("Density just under the threshold is ignored"), UBot::Medium::FindFirstIntersection(Origin, AlongX, Exact, 0.26f, Distance, Density));

    const TArray<FUBotMediumSnapshot> Layered = { MakeSnapshot({
        MakeCell(FVector(150.0, 0.0, 0.0), 20.0f, 0.01f),
        MakeCell(FVector(300.0, 0.0, 0.0), 100.0f, 1.0f) }) };
    TestTrue(TEXT("Thin cell does not hide a dense one behind it"), UBot::Medium::FindFirstIntersection(Origin, AlongX, Layered, 0.05f, Distance, Density));
    TestEqual(TEXT("Hit is at the dense cell"), Distance, 200.0f, 0.001f);
    TestEqual(TEXT("Dense cell density"), Density, 1.0f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotMediumEdgeFactorTest, "UBotCore.Medium.EdgeFactor", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotMediumEdgeFactorTest::RunTest(const FString& Parameters)
{
    using namespace UBotInterfaceTests;

    float Distance = 0.0f;
    float Density = 0.0f;

    const TArray<FUBotMediumSnapshot> FadedEdge = { MakeSnapshot({ MakeCell(FVector(300.0, 0.0, 0.0), 100.0f, 1.0f, 0.2f) }) };
    TestFalse(TEXT("Edge factor can push a dense cell below the threshold"), UBot::Medium::FindFirstIntersection(Origin, AlongX, FadedEdge, 0.25f, Distance, Density));

    const TArray<FUBotMediumSnapshot> HalfEdge = { MakeSnapshot({ MakeCell(FVector(300.0, 0.0, 0.0), 100.0f, 1.0f, 0.5f) }) };
    TestTrue(TEXT("Attenuated cell above the threshold is hit"), UBot::Medium::FindFirstIntersection(Origin, AlongX, HalfEdge, 0.25f, Distance, Density));
    TestEqual(TEXT("Reported density includes the edge factor"), Density, 0.5f);

    const TArray<FUBotMediumSnapshot> OverEdge = { MakeSnapshot({ MakeCell(FVector(300.0, 0.0, 0.0), 100.0f, 0.6f, 2.0f) }) };
    TestTrue(TEXT("Edge factor above 1 still hits"), UBot::Medium::FindFirstIntersection(Origin, AlongX, OverEdge, 0.25f, Distance, Density));
    TestEqual(TEXT("Edge factor above 1 does not amplify density"), Density, 0.6f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotMediumNearestSnapshotTest, "UBotCore.Medium.NearestAcrossSnapshots", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotMediumNearestSnapshotTest::RunTest(const FString& Parameters)
{
    using namespace UBotInterfaceTests;

    const FUBotMediumSnapshot Far = MakeSnapshot({ MakeCell(FVector(600.0, 0.0, 0.0), 100.0f, 1.0f) });            // entry 500
    const FUBotMediumSnapshot Near = MakeSnapshot({ MakeCell(FVector(300.0, 0.0, 0.0), 50.0f, 0.75f) });           // entry 250
    const FUBotMediumSnapshot Inactive = MakeSnapshot({ MakeCell(FVector(100.0, 0.0, 0.0), 50.0f, 1.0f) }, false); // entry 50
    const FUBotMediumSnapshot Thin = MakeSnapshot({ MakeCell(FVector(150.0, 0.0, 0.0), 20.0f, 0.01f) });           // entry 130
    const FUBotMediumSnapshot SameEntry = MakeSnapshot({ MakeCell(FVector(300.0, 0.0, 0.0), 50.0f, 0.5f) });       // entry 250

    float Distance = 0.0f;
    float Density = 0.0f;
    TestTrue(TEXT("Hit across several snapshots"), UBot::Medium::FindFirstIntersection(Origin, AlongX, { Far, Near, Inactive, Thin }, 0.05f, Distance, Density));
    TestEqual(TEXT("Nearest active, dense entry wins"), Distance, 250.0f, 0.001f);
    TestEqual(TEXT("Density comes from the nearest cell"), Density, 0.75f);

    TestTrue(TEXT("Snapshot order does not matter"), UBot::Medium::FindFirstIntersection(Origin, AlongX, { Near, Far }, 0.05f, Distance, Density));
    TestEqual(TEXT("Reordered nearest distance"), Distance, 250.0f, 0.001f);

    TestTrue(TEXT("Tie is a hit"), UBot::Medium::FindFirstIntersection(Origin, AlongX, { Near, SameEntry }, 0.05f, Distance, Density));
    TestEqual(TEXT("On a tie the first snapshot wins"), Density, 0.75f);
    TestTrue(TEXT("Tie is a hit (reversed)"), UBot::Medium::FindFirstIntersection(Origin, AlongX, { SameEntry, Near }, 0.05f, Distance, Density));
    TestEqual(TEXT("On a tie the first snapshot wins (reversed)"), Density, 0.5f);

    TestFalse(TEXT("Inactive snapshots are ignored"), UBot::Medium::FindFirstIntersection(Origin, AlongX, { Inactive }, 0.05f, Distance, Density));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotMediumBoundsEarlyOutTest, "UBotCore.Medium.BoundsEarlyOut", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotMediumBoundsEarlyOutTest::RunTest(const FString& Parameters)
{
    using namespace UBotInterfaceTests;

    float Distance = 0.0f;
    float Density = 0.0f;

    FUBotMediumSnapshot Stale = MakeSnapshot({ MakeCell(FVector(300.0, 0.0, 0.0), 100.0f, 1.0f) });
    Stale.BoundsCentimeters = FBox(FVector(10000.0), FVector(10100.0));
    TestFalse(TEXT("Bounds that miss the segment skip the snapshot"), UBot::Medium::FindFirstIntersection(Origin, AlongX, { Stale }, 0.1f, Distance, Density));

    FUBotMediumSnapshot Unfinalized;
    Unfinalized.Cells.Add(MakeCell(FVector(300.0, 0.0, 0.0), 100.0f, 1.0f));
    TestFalse(TEXT("Bounds start invalid"), Unfinalized.BoundsCentimeters.IsValid != 0);
    TestTrue(TEXT("Snapshot without bounds is tested cell by cell"), UBot::Medium::FindFirstIntersection(Origin, AlongX, { Unfinalized }, 0.1f, Distance, Density));
    TestEqual(TEXT("Unfinalized entry distance"), Distance, 200.0f, 0.001f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotIdentityComponentWithoutOwnerTest, "UBotCore.Identity.ComponentWithoutOwner", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotIdentityComponentWithoutOwnerTest::RunTest(const FString& Parameters)
{
    UUBotIdentityComponent* Identity = NewObject<UUBotIdentityComponent>();
    if (!TestNotNull(TEXT("Identity component"), Identity))
    {
        return false;
    }

    TestEqualSensitive(TEXT("No robot id and no owner -> empty"), *Identity->GetResolvedRobotId(), TEXT(""));
    TestEqualSensitive(TEXT("Namespace of an empty robot id"), *Identity->GetResolvedNamespace(), *UBot::Ros::SanitizeRosName(FString()));
    TestEqualSensitive(TEXT("Default base frame"), *Identity->GetBaseFrameId(), TEXT("base_link"));
    TestNull(TEXT("No owner -> no base component"), Identity->GetBaseComponent());

    Identity->RobotId = TEXT("  Rover-01 ");
    TestEqualSensitive(TEXT("Robot id is trimmed"), *Identity->GetResolvedRobotId(), TEXT("Rover-01"));
    TestEqualSensitive(TEXT("Empty namespace -> sanitized robot id"), *Identity->GetResolvedNamespace(), *UBot::Ros::SanitizeRosName(TEXT("Rover-01")));

    Identity->Namespace = TEXT(" /fleet/rover_a/ ");
    TestEqualSensitive(TEXT("Explicit namespace keeps inner separators"), *Identity->GetResolvedNamespace(), TEXT("fleet/rover_a"));
    Identity->Namespace = TEXT("/");
    TestEqualSensitive(TEXT("Separator-only namespace counts as empty"), *Identity->GetResolvedNamespace(), *UBot::Ros::SanitizeRosName(TEXT("Rover-01")));

    Identity->BaseFrameId = TEXT("");
    TestEqualSensitive(TEXT("Empty base frame -> base_link"), *Identity->GetBaseFrameId(), TEXT("base_link"));
    Identity->BaseFrameId = TEXT("chassis_link");
    TestEqualSensitive(TEXT("Explicit base frame"), *Identity->GetBaseFrameId(), TEXT("chassis_link"));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotIdentityNullActorTest, "UBotCore.Identity.NullActorFallbacks", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotIdentityNullActorTest::RunTest(const FString& Parameters)
{
    TestNull(TEXT("Find(nullptr)"), UBot::Identity::Find(nullptr));
    TestEqualSensitive(TEXT("ResolveRobotId(nullptr)"), *UBot::Identity::ResolveRobotId(nullptr), TEXT(""));
    TestEqualSensitive(TEXT("ResolveNamespace(nullptr)"), *UBot::Identity::ResolveNamespace(nullptr), *UBot::Ros::SanitizeRosName(FString()));
    TestEqualSensitive(TEXT("ResolveBaseFrameId(nullptr)"), *UBot::Identity::ResolveBaseFrameId(nullptr), TEXT("base_link"));
    TestNull(TEXT("ResolveBaseComponent(nullptr)"), UBot::Identity::ResolveBaseComponent(nullptr));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotIdentityActorFallbacksTest, "UBotCore.Identity.ActorFallbacks", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotIdentityActorFallbacksTest::RunTest(const FString& Parameters)
{
    using namespace UBotInterfaceTests;

    FScopedTestWorld TestWorld;
    if (!TestNotNull(TEXT("Test world"), TestWorld.Get()))
    {
        return false;
    }

    // Actor without an identity component.
    AActor* Bare = TestWorld.SpawnActor(TEXT("UBotTest-Bare"));
    if (!TestNotNull(TEXT("Bare actor"), Bare))
    {
        return false;
    }
    USceneComponent* BareRoot = NewObject<USceneComponent>(Bare, TEXT("Root"));
    Bare->SetRootComponent(BareRoot);

    TestNull(TEXT("Bare actor has no identity"), UBot::Identity::Find(Bare));
    TestEqualSensitive(TEXT("Robot id falls back to the actor name"), *UBot::Identity::ResolveRobotId(Bare), *Bare->GetName());
    TestEqualSensitive(TEXT("Namespace falls back to the sanitized actor name"), *UBot::Identity::ResolveNamespace(Bare), *UBot::Ros::SanitizeRosName(Bare->GetName()));
    TestFalse(TEXT("Sanitized namespace has no '-'"), UBot::Identity::ResolveNamespace(Bare).Contains(TEXT("-")));
    TestEqualSensitive(TEXT("Base frame falls back to base_link"), *UBot::Identity::ResolveBaseFrameId(Bare), TEXT("base_link"));
    TestTrue(TEXT("Base component falls back to the root"), UBot::Identity::ResolveBaseComponent(Bare) == BareRoot);

    // Actor with an identity component.
    AActor* Robot = TestWorld.SpawnActor(TEXT("UBotTest-Robot"));
    if (!TestNotNull(TEXT("Robot actor"), Robot))
    {
        return false;
    }
    USceneComponent* Root = NewObject<USceneComponent>(Robot, TEXT("Root"));
    Robot->SetRootComponent(Root);
    USceneComponent* Mast = NewObject<USceneComponent>(Robot, TEXT("Mast"));
    UUBotIdentityComponent* Identity = NewObject<UUBotIdentityComponent>(Robot, TEXT("Identity"));

    TestTrue(TEXT("Find returns the identity component"), UBot::Identity::Find(Robot) == Identity);
    TestEqualSensitive(TEXT("Empty robot id -> owner name"), *UBot::Identity::ResolveRobotId(Robot), *Robot->GetName());
    TestEqualSensitive(TEXT("Empty namespace -> sanitized owner name"), *UBot::Identity::ResolveNamespace(Robot), *UBot::Ros::SanitizeRosName(Robot->GetName()));
    TestEqualSensitive(TEXT("Default base frame"), *UBot::Identity::ResolveBaseFrameId(Robot), TEXT("base_link"));
    TestTrue(TEXT("Empty base component -> owner root"), UBot::Identity::ResolveBaseComponent(Robot) == Root);

    Identity->RobotId = TEXT("Rover 7");
    Identity->BaseFrameId = TEXT("chassis_link");
    Identity->BaseComponent.PathToComponent = Mast->GetName();
    TestEqualSensitive(TEXT("Configured robot id"), *UBot::Identity::ResolveRobotId(Robot), TEXT("Rover 7"));
    TestEqualSensitive(TEXT("Namespace from the configured robot id"), *UBot::Identity::ResolveNamespace(Robot), *UBot::Ros::SanitizeRosName(TEXT("Rover 7")));
    TestEqualSensitive(TEXT("Configured base frame"), *UBot::Identity::ResolveBaseFrameId(Robot), TEXT("chassis_link"));
    TestTrue(TEXT("Configured base component"), UBot::Identity::ResolveBaseComponent(Robot) == Mast);

    Identity->Namespace = TEXT("fleet/rover_7");
    TestEqualSensitive(TEXT("Configured namespace"), *UBot::Identity::ResolveNamespace(Robot), TEXT("fleet/rover_7"));

    Identity->BaseComponent.PathToComponent = Identity->GetName();
    TestTrue(TEXT("Non-scene reference falls back to the root"), UBot::Identity::ResolveBaseComponent(Robot) == Root);
    Identity->BaseComponent.PathToComponent = TEXT("Missing");
    TestTrue(TEXT("Unresolved reference falls back to the root"), UBot::Identity::ResolveBaseComponent(Robot) == Root);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotFindSensorsOrderingTest, "UBotCore.Interfaces.FindSensorsOrdering", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotFindSensorsOrderingTest::RunTest(const FString& Parameters)
{
    using namespace UBotInterfaceTests;

    FScopedTestWorld TestWorld;
    if (!TestNotNull(TEXT("Test world"), TestWorld.Get()))
    {
        return false;
    }

    AActor* Actor = TestWorld.SpawnActor(TEXT("UBotTest-Components"));
    if (!TestNotNull(TEXT("Actor"), Actor))
    {
        return false;
    }
    USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
    Actor->SetRootComponent(Root);
    UBoxComponent* Box = NewObject<UBoxComponent>(Actor, TEXT("Box"));
    USceneComponent* Middle = NewObject<USceneComponent>(Actor, TEXT("Middle"));
    USphereComponent* Sphere = NewObject<USphereComponent>(Actor, TEXT("Sphere"));

    // No test-only UCLASS can implement IUBotSensor from a .cpp, so the shared lookup that
    // FindSensors and FindVelocityCommandable delegate to is exercised with engine interfaces.
    // Primitive components implement INavRelevantInterface; plain scene components and AActor do not.
    TArray<UObject*> Found;
    UBot::Interfaces::FindImplementers(Actor, UNavRelevantInterface::StaticClass(), Found);
    if (TestEqual(TEXT("Only implementing components are returned"), Found.Num(), 2))
    {
        TestTrue(TEXT("First implementer in component order"), Found[0] == Box);
        TestTrue(TEXT("Second implementer in component order"), Found[1] == Sphere);
    }
    TestTrue(TEXT("First implementer lookup"), UBot::Interfaces::FindFirstImplementer(Actor, UNavRelevantInterface::StaticClass()) == Box);

    // Every component implements IInterface_AssetUserData.
    UBot::Interfaces::FindImplementers(Actor, UInterface_AssetUserData::StaticClass(), Found);
    if (TestEqual(TEXT("Every component is returned"), Found.Num(), 4))
    {
        TestTrue(TEXT("Component order [0]"), Found[0] == Root);
        TestTrue(TEXT("Component order [1]"), Found[1] == Box);
        TestTrue(TEXT("Component order [2]"), Found[2] == Middle);
        TestTrue(TEXT("Component order [3]"), Found[3] == Sphere);
    }

    // AWorldSettings implements IInterface_AssetUserData itself, so it must come before its components.
    AWorldSettings* WorldSettings = TestWorld.Get()->GetWorldSettings(false, false);
    if (TestNotNull(TEXT("World settings"), WorldSettings))
    {
        USceneComponent* Extra = NewObject<USceneComponent>(WorldSettings, TEXT("UBotTestExtra"));
        UBot::Interfaces::FindImplementers(WorldSettings, UInterface_AssetUserData::StaticClass(), Found);
        TestTrue(TEXT("Implementing actor comes first"), Found.Num() >= 2 && Found[0] == WorldSettings);
        TestTrue(TEXT("Implementing components follow the actor"), Found.Find(Extra) > 0);
        TestTrue(TEXT("First implementer is the actor"), UBot::Interfaces::FindFirstImplementer(WorldSettings, UInterface_AssetUserData::StaticClass()) == WorldSettings);
    }

    UBot::Interfaces::FindImplementers(Actor, nullptr, Found);
    TestEqual(TEXT("Null interface -> nothing"), Found.Num(), 0);

    TArray<UObject*> Sensors;
    Sensors.Add(Actor);
    UBot::Sensors::FindSensors(Actor, Sensors);
    TestEqual(TEXT("FindSensors resets the output and finds no sensor"), Sensors.Num(), 0);
    Sensors.Add(Actor);
    UBot::Sensors::FindSensors(nullptr, Sensors);
    TestEqual(TEXT("FindSensors(nullptr) is empty"), Sensors.Num(), 0);

    TestNull(TEXT("No velocity commandable"), UBot::Actuation::FindVelocityCommandable(Actor));
    TestNull(TEXT("FindVelocityCommandable(nullptr)"), UBot::Actuation::FindVelocityCommandable(nullptr));

    TArray<FUBotMediumSnapshot> Snapshots;
    Snapshots.AddDefaulted();
    UBot::Medium::CollectSnapshots(*TestWorld.Get(), Snapshots);
    TestEqual(TEXT("World without media yields no snapshots"), Snapshots.Num(), 0);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
