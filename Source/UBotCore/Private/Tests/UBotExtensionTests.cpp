#if WITH_DEV_AUTOMATION_TESTS

#include "Async/ParallelFor.h"
#include "Misc/AutomationTest.h"
#include "UBotExtensionRegistry.h"
#include "UBotPackageLibrary.h"
#include <atomic>

namespace UBotExtensionTests
{
    constexpr int32 ConcurrentTaskCount = 8;
    constexpr int32 ExtensionsPerTask = 64;

    class FTestExtension : public IUBotExtension
    {
    public:
        explicit FTestExtension(FName InName, int32 InValue = 0)
            : Name(InName)
            , Value(InValue)
        {
        }

        virtual FName GetExtensionName() const override
        {
            return Name;
        }

        FName Name;
        int32 Value = 0;
    };

    TSharedRef<FTestExtension> MakeExtension(FName Name, int32 Value = 0)
    {
        return MakeShared<FTestExtension>(Name, Value);
    }

    FString JoinNames(const TArray<TSharedRef<IUBotExtension>>& Extensions)
    {
        TArray<FString> Names;
        for (const TSharedRef<IUBotExtension>& Extension : Extensions)
        {
            Names.Add(Extension->GetExtensionName().ToString());
        }
        return FString::Join(Names, TEXT(","));
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotExtensionRegisterTest, "UBotCore.Extensions.DeclareAndRegister", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotExtensionRegisterTest::RunTest(const FString& Parameters)
{
    using namespace UBotExtensionTests;

    FUBotExtensionRegistry Registry;
    const FName Point(TEXT("Test.Publisher"));

    TestFalse(TEXT("Point is not declared initially"), Registry.IsExtensionPointDeclared(Point));
    TestTrue(TEXT("Point declaration succeeds"), Registry.RegisterExtensionPoint(Point, TEXT("PkgOwner"), INVTEXT("Test point")));
    TestTrue(TEXT("Point is declared"), Registry.IsExtensionPointDeclared(Point));

    TestTrue(TEXT("First extension registers"), Registry.RegisterExtension(Point, TEXT("PkgA"), MakeExtension(TEXT("First"), 1)));
    TestTrue(TEXT("Second extension registers"), Registry.RegisterExtension(Point, TEXT("PkgB"), MakeExtension(TEXT("Second"), 2)));

    TestEqual(TEXT("Extensions come back in registration order"), JoinNames(Registry.GetExtensions(Point)), FString(TEXT("First,Second")));
    TestEqual(TEXT("Unknown point has no extensions"), Registry.GetExtensions(TEXT("Test.Unknown")).Num(), 0);

    const TArray<TSharedRef<FTestExtension>> Typed = Registry.GetExtensionsAs<FTestExtension>(Point);
    if (TestEqual(TEXT("Typed access returns both"), Typed.Num(), 2))
    {
        TestEqual(TEXT("Typed access keeps the object (first)"), Typed[0]->Value, 1);
        TestEqual(TEXT("Typed access keeps the object (second)"), Typed[1]->Value, 2);
    }

    const TArray<FUBotExtensionPointInfo> Points = Registry.GetExtensionPoints();
    if (TestEqual(TEXT("One declared point"), Points.Num(), 1))
    {
        TestEqual(TEXT("Point name"), Points[0].Name, Point);
        TestEqual(TEXT("Point owner"), Points[0].OwnerPackage, FName(TEXT("PkgOwner")));
        TestEqual(TEXT("Point description"), Points[0].Description.ToString(), FString(TEXT("Test point")));
    }

    const TArray<FUBotExtensionRecord> Records = Registry.GetAllExtensions();
    if (TestEqual(TEXT("Two records"), Records.Num(), 2))
    {
        TestEqual(TEXT("Record point"), Records[0].ExtensionPoint, Point);
        TestEqual(TEXT("Record name"), Records[0].ExtensionName, FName(TEXT("First")));
        TestEqual(TEXT("Record owner"), Records[0].OwnerPackage, FName(TEXT("PkgA")));
        TestTrue(TEXT("Record keeps the extension"), Records[0].Extension.IsValid());
    }

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotExtensionPointOwnershipTest, "UBotCore.Extensions.PointOwnership", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotExtensionPointOwnershipTest::RunTest(const FString& Parameters)
{
    FUBotExtensionRegistry Registry;
    const FName Point(TEXT("Test.Owned"));

    TestTrue(TEXT("Owner declares the point"), Registry.RegisterExtensionPoint(Point, TEXT("Owner"), INVTEXT("One")));
    TestTrue(TEXT("Owner may declare it again"), Registry.RegisterExtensionPoint(Point, TEXT("Owner"), INVTEXT("Two")));

    AddExpectedMessagePlain(TEXT("is already declared by package"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
    TestFalse(TEXT("Another package cannot take the point"), Registry.RegisterExtensionPoint(Point, TEXT("Intruder"), INVTEXT("Three")));

    const TArray<FUBotExtensionPointInfo> Points = Registry.GetExtensionPoints();
    if (TestEqual(TEXT("Still one point"), Points.Num(), 1))
    {
        TestEqual(TEXT("Owner is unchanged"), Points[0].OwnerPackage, FName(TEXT("Owner")));
        TestEqual(TEXT("Re-declaration updated the description"), Points[0].Description.ToString(), FString(TEXT("Two")));
    }

    AddExpectedMessagePlain(TEXT("extension point without a name"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
    TestFalse(TEXT("Nameless point is rejected"), Registry.RegisterExtensionPoint(NAME_None, TEXT("Owner"), INVTEXT("None")));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotExtensionDuplicatesTest, "UBotCore.Extensions.DuplicatesAndNames", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotExtensionDuplicatesTest::RunTest(const FString& Parameters)
{
    using namespace UBotExtensionTests;

    FUBotExtensionRegistry Registry;
    const FName PointA(TEXT("Test.A"));
    const FName PointB(TEXT("Test.B"));
    Registry.RegisterExtensionPoint(PointA, TEXT("Owner"), INVTEXT("A"));
    Registry.RegisterExtensionPoint(PointB, TEXT("Owner"), INVTEXT("B"));

    TestTrue(TEXT("First registration succeeds"), Registry.RegisterExtension(PointA, TEXT("Pkg"), MakeExtension(TEXT("Same"), 1)));

    AddExpectedMessagePlain(TEXT("is already registered at extension point"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
    TestFalse(TEXT("Duplicate (point, name) is rejected"), Registry.RegisterExtension(PointA, TEXT("OtherPkg"), MakeExtension(TEXT("Same"), 2)));

    const TArray<TSharedRef<FTestExtension>> AtA = Registry.GetExtensionsAs<FTestExtension>(PointA);
    if (TestEqual(TEXT("Only one extension at A"), AtA.Num(), 1))
    {
        TestEqual(TEXT("The first registration is kept"), AtA[0]->Value, 1);
    }

    TestTrue(TEXT("Same name at another point is fine"), Registry.RegisterExtension(PointB, TEXT("Pkg"), MakeExtension(TEXT("Same"))));

    AddExpectedMessagePlain(TEXT("need a name"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 2);
    TestFalse(TEXT("Nameless extension is rejected"), Registry.RegisterExtension(PointA, TEXT("Pkg"), MakeExtension(NAME_None)));
    TestFalse(TEXT("Nameless point is rejected"), Registry.RegisterExtension(NAME_None, TEXT("Pkg"), MakeExtension(TEXT("Named"))));

    TestEqual(TEXT("Rejected registrations leave no records"), Registry.GetAllExtensions().Num(), 2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotExtensionEarlyRegistrationTest, "UBotCore.Extensions.EarlyRegistration", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotExtensionEarlyRegistrationTest::RunTest(const FString& Parameters)
{
    using namespace UBotExtensionTests;

    FUBotExtensionRegistry Registry;
    const FName Point(TEXT("Test.Late"));

    TestTrue(TEXT("Extension registers before its point exists"), Registry.RegisterExtension(Point, TEXT("EarlyPkg"), MakeExtension(TEXT("Early"))));
    TestFalse(TEXT("Point is still undeclared"), Registry.IsExtensionPointDeclared(Point));
    TestEqual(TEXT("Undeclared point returns nothing"), Registry.GetExtensions(Point).Num(), 0);
    TestEqual(TEXT("The pending extension is kept"), Registry.GetAllExtensions().Num(), 1);

    TestTrue(TEXT("Point is declared later"), Registry.RegisterExtensionPoint(Point, TEXT("LatePkg"), INVTEXT("Late")));
    TestEqual(TEXT("Pending extension becomes visible"), JoinNames(Registry.GetExtensions(Point)), FString(TEXT("Early")));

    TestTrue(TEXT("Later registrations append"), Registry.RegisterExtension(Point, TEXT("EarlyPkg"), MakeExtension(TEXT("Later"))));
    TestEqual(TEXT("Registration order is kept"), JoinNames(Registry.GetExtensions(Point)), FString(TEXT("Early,Later")));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotExtensionUnregisterTest, "UBotCore.Extensions.Unregister", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotExtensionUnregisterTest::RunTest(const FString& Parameters)
{
    using namespace UBotExtensionTests;

    FUBotExtensionRegistry Registry;
    const FName Owned(TEXT("Test.Owned"));
    const FName Other(TEXT("Test.Other"));
    Registry.RegisterExtensionPoint(Owned, TEXT("Owner"), INVTEXT("Owned"));
    Registry.RegisterExtensionPoint(Other, TEXT("Other"), INVTEXT("Other"));
    Registry.RegisterExtension(Owned, TEXT("Owner"), MakeExtension(TEXT("OwnerExt")));
    Registry.RegisterExtension(Owned, TEXT("Guest"), MakeExtension(TEXT("GuestExt")));
    Registry.RegisterExtension(Other, TEXT("Owner"), MakeExtension(TEXT("OwnerElsewhere")));
    Registry.RegisterExtension(Other, TEXT("Guest"), MakeExtension(TEXT("GuestElsewhere")));

    TestTrue(TEXT("Registered extension is removed"), Registry.UnregisterExtension(Other, TEXT("GuestElsewhere")));
    TestFalse(TEXT("Removing it twice fails"), Registry.UnregisterExtension(Other, TEXT("GuestElsewhere")));
    TestFalse(TEXT("Unknown extension is not removed"), Registry.UnregisterExtension(Other, TEXT("Nope")));
    TestFalse(TEXT("Name must match the point"), Registry.UnregisterExtension(Other, TEXT("GuestExt")));

    Registry.UnregisterAllFromPackage(TEXT("Owner"));
    TestFalse(TEXT("Owned point is removed"), Registry.IsExtensionPointDeclared(Owned));
    TestTrue(TEXT("Other package's point stays"), Registry.IsExtensionPointDeclared(Other));
    TestEqual(TEXT("Owner's extension at another point is removed"), Registry.GetExtensions(Other).Num(), 0);
    TestEqual(TEXT("Removed point returns nothing"), Registry.GetExtensions(Owned).Num(), 0);

    const TArray<FUBotExtensionRecord> Remaining = Registry.GetAllExtensions();
    if (TestEqual(TEXT("Only the guest's extension at the removed point remains"), Remaining.Num(), 1))
    {
        TestEqual(TEXT("Guest extension is kept"), Remaining[0].ExtensionName, FName(TEXT("GuestExt")));
    }

    TestTrue(TEXT("Point can be declared again"), Registry.RegisterExtensionPoint(Owned, TEXT("NewOwner"), INVTEXT("Owned again")));
    TestEqual(TEXT("Kept extension reappears"), JoinNames(Registry.GetExtensions(Owned)), FString(TEXT("GuestExt")));

    Registry.UnregisterAllFromPackage(TEXT("Nobody"));
    TestEqual(TEXT("Unknown package changes nothing"), Registry.GetAllExtensions().Num(), 1);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotExtensionNotificationTest, "UBotCore.Extensions.ChangeNotifications", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotExtensionNotificationTest::RunTest(const FString& Parameters)
{
    using namespace UBotExtensionTests;

    FUBotExtensionRegistry Registry;
    const FName Point(TEXT("Test.Notify"));

    TArray<FName> Notified;
    int32 VisibleAtNotify = INDEX_NONE;
    // The handler calls back into the registry: the broadcast must happen after the state change
    // and outside the internal lock.
    Registry.OnExtensionsChanged.AddLambda([&Registry, &Notified, &VisibleAtNotify](FName PointName)
    {
        Notified.Add(PointName);
        VisibleAtNotify = Registry.GetExtensions(PointName).Num();
    });

    Registry.RegisterExtension(Point, TEXT("Pkg"), MakeExtension(TEXT("A")));
    TestEqual(TEXT("Registration notifies"), Notified.Num(), 1);
    TestEqual(TEXT("Notification names the point"), Notified.Last(), Point);
    TestEqual(TEXT("Pending extension is not visible yet"), VisibleAtNotify, 0);

    Registry.RegisterExtensionPoint(Point, TEXT("Owner"), INVTEXT("Notify"));
    TestEqual(TEXT("Declaring the point notifies"), Notified.Num(), 2);
    TestEqual(TEXT("Handler sees the new state"), VisibleAtNotify, 1);

    Registry.RegisterExtensionPoint(Point, TEXT("Owner"), INVTEXT("Notify again"));
    TestEqual(TEXT("Re-declaring does not notify"), Notified.Num(), 2);

    AddExpectedMessagePlain(TEXT("is already registered at extension point"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
    Registry.RegisterExtension(Point, TEXT("Pkg"), MakeExtension(TEXT("A")));
    TestEqual(TEXT("Rejected registration does not notify"), Notified.Num(), 2);

    Registry.UnregisterExtension(Point, TEXT("A"));
    TestEqual(TEXT("Unregistering notifies"), Notified.Num(), 3);
    TestEqual(TEXT("Handler sees the removal"), VisibleAtNotify, 0);

    Registry.UnregisterExtension(Point, TEXT("A"));
    TestEqual(TEXT("Failed unregistration does not notify"), Notified.Num(), 3);

    Registry.UnregisterAllFromPackage(TEXT("Owner"));
    TestEqual(TEXT("Removing a package notifies once per affected point"), Notified.Num(), 4);

    Registry.UnregisterAllFromPackage(TEXT("Owner"));
    TestEqual(TEXT("Removing an empty package does not notify"), Notified.Num(), 4);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotExtensionConcurrencyTest, "UBotCore.Extensions.ConcurrentRegistration", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotExtensionConcurrencyTest::RunTest(const FString& Parameters)
{
    using namespace UBotExtensionTests;

    FUBotExtensionRegistry Registry;
    const FName Point(TEXT("Test.Concurrent"));
    Registry.RegisterExtensionPoint(Point, TEXT("Owner"), INVTEXT("Concurrent"));

    std::atomic<int32> Failures{ 0 };

    ParallelFor(ConcurrentTaskCount, [&Registry, &Failures, Point](int32 TaskIndex)
    {
        for (int32 Index = 0; Index < ExtensionsPerTask; ++Index)
        {
            const FName Name(*FString::Printf(TEXT("Ext_%d_%d"), TaskIndex, Index));
            if (!Registry.RegisterExtension(Point, TEXT("Owner"), MakeExtension(Name, TaskIndex)))
            {
                ++Failures;
            }
            Registry.GetExtensions(Point);
            if (Index % 2 == 1 && !Registry.UnregisterExtension(Point, Name))
            {
                ++Failures;
            }
        }
    });

    TestEqual(TEXT("Every concurrent operation succeeded"), Failures.load(), 0);
    TestEqual(TEXT("Half of the extensions remain"), Registry.GetExtensions(Point).Num(), ConcurrentTaskCount * ExtensionsPerTask / 2);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotExtensionLibraryTest, "UBotCore.Extensions.BlueprintLibrary", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotExtensionLibraryTest::RunTest(const FString& Parameters)
{
    using namespace UBotExtensionTests;

    FUBotExtensionRegistry& Registry = FUBotExtensionRegistry::Get();
    TestTrue(TEXT("Get returns one shared instance"), &Registry == &FUBotExtensionRegistry::Get());

    // Uses the shared registry, so everything is owned by a test package and removed at the end.
    const FName Package(TEXT("UBotCoreAutomationTests"));
    const FName Point(TEXT("UBotCoreAutomationTests.Point"));

    Registry.RegisterExtension(Point, Package, MakeExtension(TEXT("Pending")));
    TestFalse(TEXT("Undeclared point is not listed"), UUBotPackageLibrary::GetUBotExtensionPointNames().Contains(Point));
    TestEqual(TEXT("Undeclared point has no extension names"), UUBotPackageLibrary::GetUBotExtensionNames(Point).Num(), 0);

    Registry.RegisterExtensionPoint(Point, Package, INVTEXT("Automation test point"));
    TestTrue(TEXT("Declared point is listed"), UUBotPackageLibrary::GetUBotExtensionPointNames().Contains(Point));
    const TArray<FName> Names = UUBotPackageLibrary::GetUBotExtensionNames(Point);
    if (TestEqual(TEXT("One extension name"), Names.Num(), 1))
    {
        TestEqual(TEXT("Extension name"), Names[0], FName(TEXT("Pending")));
    }

    Registry.UnregisterAllFromPackage(Package);
    TestFalse(TEXT("Test point is cleaned up"), Registry.IsExtensionPointDeclared(Point));
    TestFalse(TEXT("Test extensions are cleaned up"), Registry.GetAllExtensions().ContainsByPredicate([Package](const FUBotExtensionRecord& Record)
    {
        return Record.OwnerPackage == Package;
    }));

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
