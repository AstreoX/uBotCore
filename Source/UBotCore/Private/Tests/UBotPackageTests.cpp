#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UBotPackageIndex.h"
#include "UBotPackageLibrary.h"
#include "UBotPackageRegistry.h"
#include "UBotPackageTypes.h"
#include "UBotSemVer.h"

namespace UBotPackageTests
{
    struct FConstraintCase
    {
        const TCHAR* Constraint;
        const TCHAR* Version;
        bool bExpected;
    };

    void CheckConstraintCases(FAutomationTestBase& Test, const TArrayView<const FConstraintCase> Cases)
    {
        for (const FConstraintCase& Case : Cases)
        {
            FUBotVersionConstraint Constraint;
            FString Error;
            if (!FUBotVersionConstraint::Parse(Case.Constraint, Constraint, &Error))
            {
                Test.AddError(FString::Printf(TEXT("Constraint '%s' should parse: %s"), Case.Constraint, *Error));
                continue;
            }

            FUBotSemVer Version;
            if (!FUBotSemVer::Parse(Case.Version, Version))
            {
                Test.AddError(FString::Printf(TEXT("Version '%s' should parse"), Case.Version));
                continue;
            }

            const bool bSatisfied = Constraint.IsSatisfiedBy(Version);
            if (bSatisfied != Case.bExpected)
            {
                Test.AddError(FString::Printf(TEXT("'%s' %s be satisfied by %s"),
                    Case.Constraint, Case.bExpected ? TEXT("should") : TEXT("should not"), Case.Version));
            }
        }
    }

    TSharedPtr<FJsonObject> ParseJsonObject(const FString& Text)
    {
        TSharedPtr<FJsonObject> Object;
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
        FJsonSerializer::Deserialize(Reader, Object);
        return Object;
    }

    FUBotPackageInfo MakePackage(const TCHAR* Name, const TCHAR* Version, EUBotPackageState State)
    {
        FUBotPackageInfo Package;
        Package.Name = Name;
        Package.Version = Version;
        Package.State = State;
        Package.bInstalled = State != EUBotPackageState::NotInstalled;
        Package.bEnabled = State == EUBotPackageState::Enabled;
        return Package;
    }

    void AddRequirement(FUBotPackageInfo& Package, const TCHAR* Name, const TCHAR* Version, bool bOptional = false)
    {
        FUBotPackageDependency& Dependency = Package.Requires.AddDefaulted_GetRef();
        Dependency.Name = Name;
        Dependency.Version = Version;
        Dependency.bOptional = bOptional;
    }

    const FUBotPackageInfo* FindByName(const TArray<FUBotPackageInfo>& Packages, const TCHAR* Name)
    {
        return Packages.FindByPredicate([Name](const FUBotPackageInfo& Package)
        {
            return Package.Name.Equals(Name, ESearchCase::IgnoreCase);
        });
    }

    bool HasProblem(const FUBotPackageInfo& Package, const TCHAR* Fragment)
    {
        return Package.Problems.ContainsByPredicate([Fragment](const FString& Problem)
        {
            return Problem.Contains(Fragment);
        });
    }

    int32 ProblemCount(const TArray<FUBotPackageInfo>& Packages, const TCHAR* Name)
    {
        const FUBotPackageInfo* Package = FindByName(Packages, Name);
        return Package ? Package->Problems.Num() : -1;
    }

    bool PackageHasProblem(const TArray<FUBotPackageInfo>& Packages, const TCHAR* Name, const TCHAR* Fragment)
    {
        const FUBotPackageInfo* Package = FindByName(Packages, Name);
        return Package && HasProblem(*Package, Fragment);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotSemVerParseTest, "UBotCore.Packages.SemVerParse", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotSemVerParseTest::RunTest(const FString& Parameters)
{
    struct FValidCase
    {
        const TCHAR* Text;
        int32 Major;
        int32 Minor;
        int32 Patch;
        const TCHAR* PreRelease;
        const TCHAR* Canonical;
    };

    const FValidCase ValidCases[] =
    {
        { TEXT("1"), 1, 0, 0, TEXT(""), TEXT("1.0.0") },
        { TEXT("1.2"), 1, 2, 0, TEXT(""), TEXT("1.2.0") },
        { TEXT("1.2.3"), 1, 2, 3, TEXT(""), TEXT("1.2.3") },
        { TEXT("v1.2.3"), 1, 2, 3, TEXT(""), TEXT("1.2.3") },
        { TEXT("V0.1"), 0, 1, 0, TEXT(""), TEXT("0.1.0") },
        { TEXT("  2.0.1  "), 2, 0, 1, TEXT(""), TEXT("2.0.1") },
        { TEXT("1.2.3-beta.1"), 1, 2, 3, TEXT("beta.1"), TEXT("1.2.3-beta.1") },
        { TEXT("1.2.3+build.7"), 1, 2, 3, TEXT(""), TEXT("1.2.3") },
        { TEXT("1.2.3-rc.1+exp.sha.5114f85"), 1, 2, 3, TEXT("rc.1"), TEXT("1.2.3-rc.1") },
        { TEXT("1.0.0-x-y-z.0"), 1, 0, 0, TEXT("x-y-z.0"), TEXT("1.0.0-x-y-z.0") },
        { TEXT("1.2-alpha"), 1, 2, 0, TEXT("alpha"), TEXT("1.2.0-alpha") },
        { TEXT("2147483647.0.0"), MAX_int32, 0, 0, TEXT(""), TEXT("2147483647.0.0") },
    };

    for (const FValidCase& Case : ValidCases)
    {
        FUBotSemVer Version;
        if (!TestTrue(FString::Printf(TEXT("'%s' parses"), Case.Text), FUBotSemVer::Parse(Case.Text, Version)))
        {
            continue;
        }
        TestEqual(FString::Printf(TEXT("'%s' major"), Case.Text), Version.Major, Case.Major);
        TestEqual(FString::Printf(TEXT("'%s' minor"), Case.Text), Version.Minor, Case.Minor);
        TestEqual(FString::Printf(TEXT("'%s' patch"), Case.Text), Version.Patch, Case.Patch);
        TestEqualSensitive(*FString::Printf(TEXT("'%s' prerelease"), Case.Text), *Version.PreRelease, Case.PreRelease);
        TestEqualSensitive(*FString::Printf(TEXT("'%s' canonical text"), Case.Text), *Version.ToString(), Case.Canonical);
        TestEqual(FString::Printf(TEXT("'%s' IsPreRelease"), Case.Text), Version.IsPreRelease(), FCString::Strlen(Case.PreRelease) > 0);
    }

    const TCHAR* InvalidCases[] =
    {
        TEXT(""), TEXT("   "), TEXT("v"), TEXT("1.2.3.4"), TEXT("1..2"), TEXT("1."), TEXT(".1"), TEXT("a"), TEXT("1.a"),
        TEXT("1.2.x"), TEXT("1.2.3-"), TEXT("1.2.3-beta..1"), TEXT("1.2.3-beta_1"), TEXT("-1"), TEXT("+build"),
        TEXT("1 .2"), TEXT("2147483648"), TEXT("1.-2.3"),
    };

    for (const TCHAR* Text : InvalidCases)
    {
        FUBotSemVer Version(9, 9, 9);
        TestFalse(FString::Printf(TEXT("'%s' is rejected"), Text), FUBotSemVer::Parse(Text, Version));
        TestEqual(FString::Printf(TEXT("'%s' leaves the output untouched"), Text), Version.ToString(), FString(TEXT("9.9.9")));
    }

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotSemVerOrderingTest, "UBotCore.Packages.SemVerOrdering", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotSemVerOrderingTest::RunTest(const FString& Parameters)
{
    // Ascending precedence, including the SemVer 2.0 specification example.
    const TCHAR* Ascending[] =
    {
        TEXT("0.9.9"),
        TEXT("1.0.0-alpha"),
        TEXT("1.0.0-alpha.1"),
        TEXT("1.0.0-alpha.beta"),
        TEXT("1.0.0-beta"),
        TEXT("1.0.0-beta.2"),
        TEXT("1.0.0-beta.11"),
        TEXT("1.0.0-rc.1"),
        TEXT("1.0.0"),
        TEXT("1.0.1"),
        TEXT("1.1.0"),
        TEXT("2.0.0"),
        TEXT("10.0.0"),
    };

    TArray<FUBotSemVer> Versions;
    for (const TCHAR* Text : Ascending)
    {
        FUBotSemVer Version;
        TestTrue(FString::Printf(TEXT("'%s' parses"), Text), FUBotSemVer::Parse(Text, Version));
        Versions.Add(Version);
    }

    for (int32 Index = 0; Index < Versions.Num(); ++Index)
    {
        TestEqual(FString::Printf(TEXT("%s equals itself"), Ascending[Index]), Versions[Index].Compare(Versions[Index]), 0);
        for (int32 Later = Index + 1; Later < Versions.Num(); ++Later)
        {
            TestEqual(FString::Printf(TEXT("%s < %s"), Ascending[Index], Ascending[Later]), Versions[Index].Compare(Versions[Later]), -1);
            TestEqual(FString::Printf(TEXT("%s > %s"), Ascending[Later], Ascending[Index]), Versions[Later].Compare(Versions[Index]), 1);
        }
    }

    FUBotSemVer A;
    FUBotSemVer B;
    FUBotSemVer::Parse(TEXT("1.0"), A);
    FUBotSemVer::Parse(TEXT("1.0.0+build.5"), B);
    TestTrue(TEXT("Partial and build metadata compare equal"), A == B);

    FUBotSemVer::Parse(TEXT("1.0.0-beta.01"), A);
    FUBotSemVer::Parse(TEXT("1.0.0-beta.1"), B);
    TestTrue(TEXT("Numeric identifiers compare by value"), A == B);

    FUBotSemVer::Parse(TEXT("1.0.0-Beta"), A);
    FUBotSemVer::Parse(TEXT("1.0.0-beta"), B);
    TestTrue(TEXT("Alphanumeric identifiers compare in ASCII order"), A < B);

    FUBotSemVer::Parse(TEXT("1.0.0-beta.99999999999999999999"), A);
    FUBotSemVer::Parse(TEXT("1.0.0-beta.100000000000000000000"), B);
    TestTrue(TEXT("Huge numeric identifiers do not overflow"), A < B);

    const FUBotSemVer Low(1, 2, 3);
    const FUBotSemVer High(1, 3, 0);
    TestTrue(TEXT("operator<"), Low < High);
    TestTrue(TEXT("operator<="), Low <= High && Low <= Low);
    TestTrue(TEXT("operator>"), High > Low);
    TestTrue(TEXT("operator>="), High >= Low && High >= High);
    TestTrue(TEXT("operator!="), Low != High);
    TestTrue(TEXT("operator=="), Low == FUBotSemVer(1, 2, 3));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotConstraintSyntaxTest, "UBotCore.Packages.ConstraintSyntax", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotConstraintSyntaxTest::RunTest(const FString& Parameters)
{
    using namespace UBotPackageTests;

    FUBotVersionConstraint Constraint;
    FString Error = TEXT("stale");

    TestTrue(TEXT("Empty constraint parses"), FUBotVersionConstraint::Parse(TEXT(""), Constraint, &Error));
    TestTrue(TEXT("Empty constraint is any"), Constraint.IsAny());
    TestTrue(TEXT("Successful parse clears the error"), Error.IsEmpty());
    TestTrue(TEXT("Empty constraint accepts a prerelease"), Constraint.IsSatisfiedBy(FUBotSemVer(0, 0, 1, TEXT("alpha"))));

    TestTrue(TEXT("Whitespace constraint parses"), FUBotVersionConstraint::Parse(TEXT("   "), Constraint));
    TestTrue(TEXT("Whitespace constraint is any"), Constraint.IsAny());

    TestTrue(TEXT("'*' parses"), FUBotVersionConstraint::Parse(TEXT("*"), Constraint));
    TestTrue(TEXT("'*' is any"), Constraint.IsAny());
    TestEqual(TEXT("'*' text"), Constraint.ToString(), FString(TEXT("*")));

    TestTrue(TEXT("Spaced comparators parse"), FUBotVersionConstraint::Parse(TEXT("  >=1.0    <2.0 "), Constraint));
    TestFalse(TEXT("Comparators are not any"), Constraint.IsAny());
    TestEqual(TEXT("Whitespace is normalised"), Constraint.ToString(), FString(TEXT(">=1.0 <2.0")));

    TestTrue(TEXT("Operator separated from its version parses"), FUBotVersionConstraint::Parse(TEXT(">= 1.0 < 2.0"), Constraint));
    TestEqual(TEXT("Operators are joined to their version"), Constraint.ToString(), FString(TEXT(">=1.0 <2.0")));

    const FConstraintCase Cases[] =
    {
        { TEXT(">= 1.0 < 2.0"), TEXT("1.5.0"), true },
        { TEXT(">= 1.0 < 2.0"), TEXT("2.0.0"), false },
        { TEXT("* >=1.0"), TEXT("0.9.0"), false },
        { TEXT("* >=1.0"), TEXT("1.0.0"), true },
        { TEXT("\t^v1.2\n"), TEXT("1.9.0"), true },
    };
    CheckConstraintCases(*this, MakeArrayView(Cases));

    const TCHAR* InvalidConstraints[] =
    {
        TEXT(">="), TEXT("^"), TEXT("~ "), TEXT(">=1.0 <"), TEXT(">=abc"), TEXT("1.2.3.4"), TEXT("!=1.0"),
        TEXT("=>1.0"), TEXT("==1.0"), TEXT("1.x"), TEXT(">=*"), TEXT(">=1.0,<2.0"), TEXT("latest"),
    };

    for (const TCHAR* Text : InvalidConstraints)
    {
        FUBotVersionConstraint Previous;
        FUBotVersionConstraint::Parse(TEXT("^1.0.0"), Previous);
        FString InvalidError;
        TestFalse(FString::Printf(TEXT("'%s' is rejected"), Text), FUBotVersionConstraint::Parse(Text, Previous, &InvalidError));
        TestFalse(FString::Printf(TEXT("'%s' reports an error"), Text), InvalidError.IsEmpty());
        TestEqual(FString::Printf(TEXT("'%s' leaves the output untouched"), Text), Previous.ToString(), FString(TEXT("^1.0.0")));
    }

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotConstraintCaretTildeTest, "UBotCore.Packages.ConstraintCaretTilde", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotConstraintCaretTildeTest::RunTest(const FString& Parameters)
{
    using namespace UBotPackageTests;

    const FConstraintCase Cases[] =
    {
        // ^1.2.3 := >=1.2.3 <2.0.0
        { TEXT("^1.2.3"), TEXT("1.2.3"), true },
        { TEXT("^1.2.3"), TEXT("1.9.9"), true },
        { TEXT("^1.2.3"), TEXT("1.3.0-beta"), true },
        { TEXT("^1.2.3"), TEXT("1.2.2"), false },
        { TEXT("^1.2.3"), TEXT("1.2.3-rc.1"), false },
        { TEXT("^1.2.3"), TEXT("2.0.0"), false },
        { TEXT("^1.2.3"), TEXT("2.0.0-beta"), false },
        // ^0.2.3 := >=0.2.3 <0.3.0
        { TEXT("^0.2.3"), TEXT("0.2.3"), true },
        { TEXT("^0.2.3"), TEXT("0.2.9"), true },
        { TEXT("^0.2.3"), TEXT("0.2.2"), false },
        { TEXT("^0.2.3"), TEXT("0.3.0"), false },
        { TEXT("^0.2.3"), TEXT("1.0.0"), false },
        // ^0.0.3 := >=0.0.3 <0.0.4
        { TEXT("^0.0.3"), TEXT("0.0.3"), true },
        { TEXT("^0.0.3"), TEXT("0.0.4"), false },
        { TEXT("^0.0.3"), TEXT("0.0.2"), false },
        // The requirement used by the uBot packages.
        { TEXT("^0.1.0"), TEXT("0.1.0"), true },
        { TEXT("^0.1.0"), TEXT("0.1.7"), true },
        { TEXT("^0.1.0"), TEXT("0.2.0"), false },
        { TEXT("^0.1.0"), TEXT("0.2.0-dev"), false },
        // Partial carets.
        { TEXT("^1"), TEXT("1.0.0"), true },
        { TEXT("^1"), TEXT("1.99.0"), true },
        { TEXT("^1"), TEXT("2.0.0"), false },
        { TEXT("^1.2"), TEXT("1.2.0"), true },
        { TEXT("^1.2"), TEXT("1.9.0"), true },
        { TEXT("^1.2"), TEXT("1.1.9"), false },
        { TEXT("^0"), TEXT("0.9.9"), true },
        { TEXT("^0"), TEXT("1.0.0"), false },
        { TEXT("^0.0"), TEXT("0.0.9"), true },
        { TEXT("^0.0"), TEXT("0.1.0"), false },
        { TEXT("^0.0.0"), TEXT("0.0.0"), true },
        { TEXT("^0.0.0"), TEXT("0.0.1"), false },
        { TEXT("^0.2"), TEXT("0.2.5"), true },
        { TEXT("^0.2"), TEXT("0.3.0"), false },
        { TEXT("^1.0.0-rc.1"), TEXT("1.0.0-rc.2"), true },
        { TEXT("^1.0.0-rc.1"), TEXT("1.0.0-beta"), false },
        { TEXT("^1.0.0-rc.1"), TEXT("1.0.0"), true },
        { TEXT("^1.0.0-rc.1"), TEXT("2.0.0-rc.1"), false },
        // ~1.2.3 := >=1.2.3 <1.3.0
        { TEXT("~1.2.3"), TEXT("1.2.3"), true },
        { TEXT("~1.2.3"), TEXT("1.2.9"), true },
        { TEXT("~1.2.3"), TEXT("1.2.2"), false },
        { TEXT("~1.2.3"), TEXT("1.3.0"), false },
        { TEXT("~1.2.3"), TEXT("1.3.0-alpha"), false },
        // ~1.2 := >=1.2.0 <1.3.0 ; ~1 := >=1.0.0 <2.0.0
        { TEXT("~1.2"), TEXT("1.2.0"), true },
        { TEXT("~1.2"), TEXT("1.2.9"), true },
        { TEXT("~1.2"), TEXT("1.1.9"), false },
        { TEXT("~1.2"), TEXT("1.3.0"), false },
        { TEXT("~1"), TEXT("1.0.0"), true },
        { TEXT("~1"), TEXT("1.9.0"), true },
        { TEXT("~1"), TEXT("0.9.9"), false },
        { TEXT("~1"), TEXT("2.0.0"), false },
        { TEXT("~0.0.3"), TEXT("0.0.9"), true },
        { TEXT("~0.0.3"), TEXT("0.1.0"), false },
    };
    CheckConstraintCases(*this, MakeArrayView(Cases));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotConstraintComparatorsTest, "UBotCore.Packages.ConstraintComparators", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotConstraintComparatorsTest::RunTest(const FString& Parameters)
{
    using namespace UBotPackageTests;

    const FConstraintCase Cases[] =
    {
        // Full versions.
        { TEXT("=1.2.3"), TEXT("1.2.3"), true },
        { TEXT("=1.2.3"), TEXT("1.2.4"), false },
        { TEXT("=1.2.3"), TEXT("1.2.3-beta"), false },
        { TEXT("1.2.3"), TEXT("1.2.3"), true },
        { TEXT("1.2.3"), TEXT("1.2.2"), false },
        { TEXT("=1.2.3-beta"), TEXT("1.2.3-beta"), true },
        { TEXT("=1.2.3-beta"), TEXT("1.2.3"), false },
        { TEXT(">=1.2.3"), TEXT("1.2.3"), true },
        { TEXT(">=1.2.3"), TEXT("1.2.2"), false },
        { TEXT(">=1.2.3"), TEXT("1.2.3-rc.1"), false },
        { TEXT(">1.2.3"), TEXT("1.2.3"), false },
        { TEXT(">1.2.3"), TEXT("1.2.4"), true },
        { TEXT(">1.2.3"), TEXT("1.2.4-alpha"), true },
        { TEXT("<=1.2.3"), TEXT("1.2.3"), true },
        { TEXT("<=1.2.3"), TEXT("1.2.4"), false },
        { TEXT("<1.2.3"), TEXT("1.2.3"), false },
        { TEXT("<1.2.3"), TEXT("1.2.2"), true },
        { TEXT("<1.2.3"), TEXT("1.2.3-beta"), true },
        // Partial versions are X-ranges.
        { TEXT("1.2"), TEXT("1.2.0"), true },
        { TEXT("1.2"), TEXT("1.2.7"), true },
        { TEXT("1.2"), TEXT("1.1.9"), false },
        { TEXT("1.2"), TEXT("1.3.0"), false },
        { TEXT("1.2"), TEXT("1.3.0-alpha"), false },
        { TEXT("=1"), TEXT("1.5.0"), true },
        { TEXT("=1"), TEXT("2.0.0"), false },
        { TEXT(">1.2"), TEXT("1.2.9"), false },
        { TEXT(">1.2"), TEXT("1.3.0"), true },
        { TEXT(">1"), TEXT("1.9.9"), false },
        { TEXT(">1"), TEXT("2.0.0"), true },
        { TEXT(">=1.2"), TEXT("1.2.0"), true },
        { TEXT(">=1.2"), TEXT("1.1.9"), false },
        { TEXT(">=1.0"), TEXT("1.0.0"), true },
        { TEXT(">=1.0"), TEXT("7.3.1"), true },
        { TEXT("<1.2"), TEXT("1.1.9"), true },
        { TEXT("<1.2"), TEXT("1.2.0"), false },
        { TEXT("<1.2"), TEXT("1.2.0-beta"), false },
        { TEXT("<=1.2"), TEXT("1.2.9"), true },
        { TEXT("<=1.2"), TEXT("1.3.0"), false },
        { TEXT("<=1"), TEXT("1.9.9"), true },
        { TEXT("<=1"), TEXT("2.0.0"), false },
        // Several comparators must all hold.
        { TEXT(">=1.2.0 <1.5.0"), TEXT("1.2.0"), true },
        { TEXT(">=1.2.0 <1.5.0"), TEXT("1.4.9"), true },
        { TEXT(">=1.2.0 <1.5.0"), TEXT("1.5.0"), false },
        { TEXT(">=1.2.0 <1.5.0"), TEXT("1.1.0"), false },
        { TEXT("^1.2 <1.4"), TEXT("1.3.9"), true },
        { TEXT("^1.2 <1.4"), TEXT("1.4.0"), false },
        { TEXT(">1.0.0 <=1.0.5 1.0"), TEXT("1.0.5"), true },
        { TEXT(">1.0.0 <=1.0.5 1.0"), TEXT("1.0.0"), false },
        { TEXT(">=2.0 <1.0"), TEXT("1.5.0"), false },
        // Prerelease ordering inside ranges.
        { TEXT(">=1.0.0-beta.2"), TEXT("1.0.0-beta.11"), true },
        { TEXT(">=1.0.0-beta.2"), TEXT("1.0.0-beta.1"), false },
        { TEXT(">=1.0.0-beta.2"), TEXT("1.0.0-alpha"), false },
        { TEXT(">=1.0.0-beta.2"), TEXT("1.0.0"), true },
        { TEXT(">1.0.0-alpha <1.0.0"), TEXT("1.0.0-alpha.1"), true },
        { TEXT(">1.0.0-alpha <1.0.0"), TEXT("1.0.0-rc.1"), true },
        { TEXT(">1.0.0-alpha <1.0.0"), TEXT("1.0.0"), false },
    };
    CheckConstraintCases(*this, MakeArrayView(Cases));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotPackageLayerNamesTest, "UBotCore.Packages.LayerNames", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotPackageLayerNamesTest::RunTest(const FString& Parameters)
{
    TestEqual(TEXT("Exact name"), ParsePackageLayer(TEXT("Foundation")), EUBotPackageLayer::Foundation);
    TestEqual(TEXT("Lower case and whitespace"), ParsePackageLayer(TEXT(" capability ")), EUBotPackageLayer::Capability);
    TestEqual(TEXT("Upper case"), ParsePackageLayer(TEXT("ADAPTER")), EUBotPackageLayer::Adapter);
    TestEqual(TEXT("Composition"), ParsePackageLayer(TEXT("composition")), EUBotPackageLayer::Composition);
    TestEqual(TEXT("Content"), ParsePackageLayer(TEXT("Content")), EUBotPackageLayer::Content);
    TestEqual(TEXT("Empty is unknown"), ParsePackageLayer(TEXT("")), EUBotPackageLayer::Unknown);
    TestEqual(TEXT("Garbage is unknown"), ParsePackageLayer(TEXT("Layers")), EUBotPackageLayer::Unknown);

    const EUBotPackageLayer AllLayers[] =
    {
        EUBotPackageLayer::Unknown, EUBotPackageLayer::Foundation, EUBotPackageLayer::Capability,
        EUBotPackageLayer::Composition, EUBotPackageLayer::Adapter, EUBotPackageLayer::Content
    };
    for (const EUBotPackageLayer Layer : AllLayers)
    {
        TestEqual(FString::Printf(TEXT("%s round trips"), LexToString(Layer)), ParsePackageLayer(LexToString(Layer)), Layer);
    }

    TestEqual(TEXT("State text"), FString(LexToString(EUBotPackageState::NotInstalled)), FString(TEXT("NotInstalled")));
    TestEqual(TEXT("State text"), FString(LexToString(EUBotPackageState::Enabled)), FString(TEXT("Enabled")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotPackageDescriptorTest, "UBotCore.Packages.DescriptorParsing", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotPackageDescriptorTest::RunTest(const FString& Parameters)
{
    using namespace UBotPackageTests;

    const TSharedPtr<FJsonObject> Full = ParseJsonObject(TEXT(R"json({
        "FileVersion": 3,
        "Version": 1,
        "VersionName": "0.1.0",
        "FriendlyName": "uBot Sensor",
        "Description": "Simulated sensors.",
        "DocsURL": "https://example.com/docs",
        "UBot": {
            "Layer": "Capability",
            "Repository": "https://github.com/AstreoX/uBotSensor.git",
            "Requires": [ { "Name": "UBotCore", "Version": "^0.1.0" },
                          { "Name": "SomeOptional", "Version": ">=1.0", "Optional": true } ],
            "Provides": [ "Sensor.IMU" ],
            "Tags": [ "sensor" ]
        }
    })json"));
    if (!TestTrue(TEXT("Test JSON parses"), Full.IsValid()))
    {
        return false;
    }

    FUBotPackageInfo Info;
    FString Issues = TEXT("stale");
    TestTrue(TEXT("Descriptor with a UBot block parses"), FUBotPackageRegistry::ParseDescriptor(*Full, TEXT("UBotSensor"), Info, &Issues));
    TestTrue(FString::Printf(TEXT("Clean descriptor has no issues (got '%s')"), *Issues), Issues.IsEmpty());
    TestEqual(TEXT("Name comes from the plugin"), Info.Name, FString(TEXT("UBotSensor")));
    TestEqual(TEXT("FriendlyName"), Info.FriendlyName, FString(TEXT("uBot Sensor")));
    TestEqual(TEXT("Description"), Info.Description, FString(TEXT("Simulated sensors.")));
    TestEqual(TEXT("Version comes from VersionName"), Info.Version, FString(TEXT("0.1.0")));
    TestEqual(TEXT("DocsUrl comes from DocsURL"), Info.DocsUrl, FString(TEXT("https://example.com/docs")));
    TestEqual(TEXT("Layer"), Info.Layer, EUBotPackageLayer::Capability);
    TestEqual(TEXT("Repository"), Info.Repository, FString(TEXT("https://github.com/AstreoX/uBotSensor.git")));
    TestEqual(TEXT("Provides"), FString::Join(Info.Provides, TEXT(",")), FString(TEXT("Sensor.IMU")));
    TestEqual(TEXT("Tags"), FString::Join(Info.Tags, TEXT(",")), FString(TEXT("sensor")));
    TestEqual(TEXT("State is left at its default"), Info.State, EUBotPackageState::NotInstalled);
    TestFalse(TEXT("Not marked installed by parsing"), Info.bInstalled);
    if (TestEqual(TEXT("Two requirements"), Info.Requires.Num(), 2))
    {
        TestEqual(TEXT("First requirement name"), Info.Requires[0].Name, FString(TEXT("UBotCore")));
        TestEqual(TEXT("First requirement version"), Info.Requires[0].Version, FString(TEXT("^0.1.0")));
        TestFalse(TEXT("First requirement is required"), Info.Requires[0].bOptional);
        TestEqual(TEXT("Second requirement name"), Info.Requires[1].Name, FString(TEXT("SomeOptional")));
        TestEqual(TEXT("Second requirement version"), Info.Requires[1].Version, FString(TEXT(">=1.0")));
        TestTrue(TEXT("Second requirement is optional"), Info.Requires[1].bOptional);
    }

    // Missing or malformed block.
    {
        const TSharedPtr<FJsonObject> NoBlock = ParseJsonObject(TEXT(R"json({ "FileVersion": 3, "VersionName": "1.0.0", "Category": "uBot" })json"));
        FUBotPackageInfo Untouched;
        Untouched.Name = TEXT("Previous");
        FString Error;
        TestFalse(TEXT("Descriptor without a UBot block is not a package"), FUBotPackageRegistry::ParseDescriptor(*NoBlock, TEXT("Other"), Untouched, &Error));
        TestTrue(TEXT("Missing block is explained"), Error.Contains(TEXT("UBot")));
        TestEqual(TEXT("Output untouched on failure"), Untouched.Name, FString(TEXT("Previous")));

        const TSharedPtr<FJsonObject> NotObject = ParseJsonObject(TEXT(R"json({ "VersionName": "1.0.0", "UBot": "yes" })json"));
        TestFalse(TEXT("UBot field that is not an object is rejected"), FUBotPackageRegistry::ParseDescriptor(*NotObject, TEXT("Other"), Untouched, &Error));
        TestTrue(TEXT("Non-object block is explained"), Error.Contains(TEXT("not an object")));

        const TSharedPtr<FJsonObject> Minimal = ParseJsonObject(TEXT(R"json({ "VersionName": "1.0.0", "UBot": { "Layer": "Content" } })json"));
        TestFalse(TEXT("Empty plugin name is rejected"), FUBotPackageRegistry::ParseDescriptor(*Minimal, TEXT("  "), Untouched, &Error));
    }

    // Bad, missing and differently cased layers.
    {
        const TSharedPtr<FJsonObject> BadLayer = ParseJsonObject(TEXT(R"json({ "VersionName": "1.0.0", "UBot": { "Layer": "Middleware" } })json"));
        FUBotPackageInfo BadInfo;
        FString BadIssues;
        TestTrue(TEXT("Unknown layer still yields a package"), FUBotPackageRegistry::ParseDescriptor(*BadLayer, TEXT("Pkg"), BadInfo, &BadIssues));
        TestEqual(TEXT("Unknown layer maps to Unknown"), BadInfo.Layer, EUBotPackageLayer::Unknown);
        TestTrue(TEXT("Unknown layer is reported"), BadIssues.Contains(TEXT("Middleware")));

        const TSharedPtr<FJsonObject> NoLayer = ParseJsonObject(TEXT(R"json({ "VersionName": "1.0.0", "UBot": {} })json"));
        FUBotPackageInfo NoLayerInfo;
        FString NoLayerIssues;
        TestTrue(TEXT("Empty block still yields a package"), FUBotPackageRegistry::ParseDescriptor(*NoLayer, TEXT("Pkg"), NoLayerInfo, &NoLayerIssues));
        TestEqual(TEXT("Missing layer maps to Unknown"), NoLayerInfo.Layer, EUBotPackageLayer::Unknown);
        TestTrue(TEXT("Missing layer is reported"), NoLayerIssues.Contains(TEXT("Layer")));
        TestEqual(TEXT("FriendlyName falls back to the plugin name"), NoLayerInfo.FriendlyName, FString(TEXT("Pkg")));

        const TSharedPtr<FJsonObject> ObjectLayer = ParseJsonObject(TEXT(R"json({ "VersionName": "1.0.0", "UBot": { "Layer": { "Name": "Adapter" } } })json"));
        FUBotPackageInfo ObjectLayerInfo;
        FString ObjectLayerIssues;
        TestTrue(TEXT("Non-string layer still yields a package"), FUBotPackageRegistry::ParseDescriptor(*ObjectLayer, TEXT("Pkg"), ObjectLayerInfo, &ObjectLayerIssues));
        TestTrue(TEXT("Non-string layer is reported"), ObjectLayerIssues.Contains(TEXT("not a string")));

        const TSharedPtr<FJsonObject> LowerLayer = ParseJsonObject(TEXT(R"json({ "VersionName": "1.0.0", "UBot": { "Layer": "adapter" } })json"));
        FUBotPackageInfo LowerInfo;
        FString LowerIssues;
        TestTrue(TEXT("Lower case layer parses"), FUBotPackageRegistry::ParseDescriptor(*LowerLayer, TEXT("Pkg"), LowerInfo, &LowerIssues));
        TestEqual(TEXT("Lower case layer is recognised"), LowerInfo.Layer, EUBotPackageLayer::Adapter);
        TestTrue(TEXT("Lower case layer is not an issue"), LowerIssues.IsEmpty());
    }

    // Version names.
    {
        const TSharedPtr<FJsonObject> BadVersion = ParseJsonObject(TEXT(R"json({ "VersionName": "beta", "UBot": { "Layer": "Content" } })json"));
        FUBotPackageInfo BadVersionInfo;
        FString BadVersionIssues;
        TestTrue(TEXT("Bad VersionName still yields a package"), FUBotPackageRegistry::ParseDescriptor(*BadVersion, TEXT("Pkg"), BadVersionInfo, &BadVersionIssues));
        TestTrue(TEXT("Bad VersionName is reported"), BadVersionIssues.Contains(TEXT("beta")));

        const TSharedPtr<FJsonObject> NoVersion = ParseJsonObject(TEXT(R"json({ "UBot": { "Layer": "Content" } })json"));
        FUBotPackageInfo NoVersionInfo;
        FString NoVersionIssues;
        TestTrue(TEXT("Missing VersionName still yields a package"), FUBotPackageRegistry::ParseDescriptor(*NoVersion, TEXT("Pkg"), NoVersionInfo, &NoVersionIssues));
        TestTrue(TEXT("Missing VersionName is reported"), NoVersionIssues.Contains(TEXT("VersionName")));
        TestTrue(TEXT("Missing VersionName leaves Version empty"), NoVersionInfo.Version.IsEmpty());
    }

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotPackageDescriptorRequiresTest, "UBotCore.Packages.DescriptorRequires", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotPackageDescriptorRequiresTest::RunTest(const FString& Parameters)
{
    using namespace UBotPackageTests;

    const TSharedPtr<FJsonObject> Json = ParseJsonObject(TEXT(R"json({
        "VersionName": "1.0.0",
        "UBot": {
            "Layer": "Content",
            "Requires": [
                { "Name": "A" },
                { "Name": " B ", "Version": " >=1.0 ", "Optional": true },
                { "Version": "^1.0" },
                "C",
                { "Name": "D", "Optional": [ true ] },
                { "Name": "E", "Version": "~2.1", "Optional": false }
            ],
            "Tags": [ "a", {}, "", "a" ],
            "ExternalRequires": [ "TempoROS" ]
        }
    })json"));
    if (!TestTrue(TEXT("Test JSON parses"), Json.IsValid()))
    {
        return false;
    }

    FUBotPackageInfo Info;
    FString Issues;
    TestTrue(TEXT("Descriptor parses despite malformed requirements"), FUBotPackageRegistry::ParseDescriptor(*Json, TEXT("Pkg"), Info, &Issues));

    if (TestEqual(TEXT("Three valid requirements are kept"), Info.Requires.Num(), 3))
    {
        TestEqual(TEXT("A name"), Info.Requires[0].Name, FString(TEXT("A")));
        TestTrue(TEXT("A has no constraint"), Info.Requires[0].Version.IsEmpty());
        TestFalse(TEXT("A is required by default"), Info.Requires[0].bOptional);
        TestEqualSensitive(TEXT("B name is trimmed"), *Info.Requires[1].Name, TEXT("B"));
        TestEqualSensitive(TEXT("B constraint is trimmed"), *Info.Requires[1].Version, TEXT(">=1.0"));
        TestTrue(TEXT("B is optional"), Info.Requires[1].bOptional);
        TestEqual(TEXT("E name"), Info.Requires[2].Name, FString(TEXT("E")));
        TestEqual(TEXT("E constraint"), Info.Requires[2].Version, FString(TEXT("~2.1")));
        TestFalse(TEXT("E explicitly required"), Info.Requires[2].bOptional);
    }

    TestTrue(TEXT("Requirement without a name is reported"), Issues.Contains(TEXT("\"Requires\"[2]")));
    TestTrue(TEXT("Non-object requirement is reported"), Issues.Contains(TEXT("\"Requires\"[3]")));
    TestTrue(TEXT("Non-boolean Optional is reported"), Issues.Contains(TEXT("\"Requires\"[4]")));
    TestEqual(TEXT("Only valid, unique tags are kept"), FString::Join(Info.Tags, TEXT(",")), FString(TEXT("a")));
    TestTrue(TEXT("Malformed tags are reported"), Issues.Contains(TEXT("\"Tags\"[1]")) && Issues.Contains(TEXT("\"Tags\"[2]")));
    TestEqual(TEXT("ExternalRequires is read from the block"), FString::Join(Info.ExternalRequires, TEXT(",")), FString(TEXT("TempoROS")));

    const TSharedPtr<FJsonObject> NotArray = ParseJsonObject(TEXT(R"json({ "VersionName": "1.0.0", "UBot": { "Layer": "Content", "Requires": "UBotCore" } })json"));
    FUBotPackageInfo NotArrayInfo;
    FString NotArrayIssues;
    TestTrue(TEXT("Descriptor with a non-array Requires parses"), FUBotPackageRegistry::ParseDescriptor(*NotArray, TEXT("Pkg"), NotArrayInfo, &NotArrayIssues));
    TestEqual(TEXT("Non-array Requires yields no requirements"), NotArrayInfo.Requires.Num(), 0);
    TestTrue(TEXT("Non-array Requires is reported"), NotArrayIssues.Contains(TEXT("\"Requires\" is not an array")));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotPackageValidateMissingTest, "UBotCore.Packages.ValidateMissingAndVersion", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotPackageValidateMissingTest::RunTest(const FString& Parameters)
{
    using namespace UBotPackageTests;

    TArray<FUBotPackageInfo> Packages;

    FUBotPackageInfo& Core = Packages.Add_GetRef(MakePackage(TEXT("UBotCore"), TEXT("0.1.0"), EUBotPackageState::Enabled));
    Core.Problems.Add(TEXT("stale problem"));

    FUBotPackageInfo& Sensor = Packages.Add_GetRef(MakePackage(TEXT("UBotSensor"), TEXT("0.1.0"), EUBotPackageState::Enabled));
    AddRequirement(Sensor, TEXT("ubotcore"), TEXT("^0.1.0"));

    FUBotPackageInfo& Ros = Packages.Add_GetRef(MakePackage(TEXT("UBotROS"), TEXT("0.1.0"), EUBotPackageState::Enabled));
    AddRequirement(Ros, TEXT("UBotCore"), TEXT("^0.1.0"));
    AddRequirement(Ros, TEXT("UBotSensor"), TEXT("^0.1.0"));
    AddRequirement(Ros, TEXT("UBotNav"), TEXT("^1.0"));

    FUBotPackageInfo& Planner = Packages.Add_GetRef(MakePackage(TEXT("UBotPlanner"), TEXT("0.1.0"), EUBotPackageState::Enabled));
    AddRequirement(Planner, TEXT("UBotCore"), TEXT("^0.2.0"));
    AddRequirement(Planner, TEXT("UBotMaps"), TEXT(""));

    Packages.Add(MakePackage(TEXT("UBotMaps"), TEXT("1.0.0"), EUBotPackageState::NotInstalled));

    FUBotPackageInfo& Broken = Packages.Add_GetRef(MakePackage(TEXT("UBotBroken"), TEXT("0.1.0"), EUBotPackageState::Disabled));
    AddRequirement(Broken, TEXT("UBotCore"), TEXT(">=abc"));
    AddRequirement(Broken, TEXT(""), TEXT("^1.0"));

    Packages.Add(MakePackage(TEXT("UBotWeird"), TEXT("not-a-version"), EUBotPackageState::Disabled));

    FUBotPackageInfo& Uses = Packages.Add_GetRef(MakePackage(TEXT("UBotUses"), TEXT("1.0.0"), EUBotPackageState::Disabled));
    AddRequirement(Uses, TEXT("UBotWeird"), TEXT(">=1.0"));

    FUBotPackageInfo& UsesAny = Packages.Add_GetRef(MakePackage(TEXT("UBotUsesAny"), TEXT("1.0.0"), EUBotPackageState::Disabled));
    AddRequirement(UsesAny, TEXT("UBotWeird"), TEXT("*"));

    FUBotPackageRegistry::ValidatePackageSet(Packages);

    TestEqual(TEXT("Stale problems are cleared"), ProblemCount(Packages, TEXT("UBotCore")), 0);
    TestEqual(TEXT("Satisfied requirement (matched case-insensitively) has no problem"), ProblemCount(Packages, TEXT("UBotSensor")), 0);

    TestEqual(TEXT("Missing package is one problem"), ProblemCount(Packages, TEXT("UBotROS")), 1);
    TestTrue(TEXT("Missing package is named"), PackageHasProblem(Packages, TEXT("UBotROS"), TEXT("UBotNav")));

    TestEqual(TEXT("Version mismatch and not-installed dependency"), ProblemCount(Packages, TEXT("UBotPlanner")), 2);
    TestTrue(TEXT("Version mismatch names the constraint"), PackageHasProblem(Packages, TEXT("UBotPlanner"), TEXT("^0.2.0")));
    TestTrue(TEXT("Version mismatch names the installed version"), PackageHasProblem(Packages, TEXT("UBotPlanner"), TEXT("version 0.1.0 is installed")));
    TestTrue(TEXT("Index-only dependency counts as missing"), PackageHasProblem(Packages, TEXT("UBotPlanner"), TEXT("'UBotMaps', which is not installed")));

    TestEqual(TEXT("Not installed package without requirements has no problem"), ProblemCount(Packages, TEXT("UBotMaps")), 0);

    TestEqual(TEXT("Unparsable constraint and nameless requirement"), ProblemCount(Packages, TEXT("UBotBroken")), 2);
    TestTrue(TEXT("Unparsable constraint is reported"), PackageHasProblem(Packages, TEXT("UBotBroken"), TEXT("unparsable")));
    TestTrue(TEXT("Nameless requirement is reported"), PackageHasProblem(Packages, TEXT("UBotBroken"), TEXT("without a package name")));

    TestEqual(TEXT("Package with a bad version but no requirements"), ProblemCount(Packages, TEXT("UBotWeird")), 0);
    TestEqual(TEXT("Constraint against an unparsable version"), ProblemCount(Packages, TEXT("UBotUses")), 1);
    TestTrue(TEXT("Unparsable installed version is reported"), PackageHasProblem(Packages, TEXT("UBotUses"), TEXT("not a semantic version")));
    TestEqual(TEXT("Any-version requirement ignores the version"), ProblemCount(Packages, TEXT("UBotUsesAny")), 0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotPackageValidateEnabledTest, "UBotCore.Packages.ValidateEnabledAndOptional", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotPackageValidateEnabledTest::RunTest(const FString& Parameters)
{
    using namespace UBotPackageTests;

    TArray<FUBotPackageInfo> Packages;
    Packages.Add(MakePackage(TEXT("UBotCore"), TEXT("0.1.0"), EUBotPackageState::Disabled));

    FUBotPackageInfo& Sensor = Packages.Add_GetRef(MakePackage(TEXT("UBotSensor"), TEXT("0.1.0"), EUBotPackageState::Enabled));
    AddRequirement(Sensor, TEXT("UBotCore"), TEXT("^0.1.0"));

    FUBotPackageInfo& Offline = Packages.Add_GetRef(MakePackage(TEXT("UBotOffline"), TEXT("0.1.0"), EUBotPackageState::Disabled));
    AddRequirement(Offline, TEXT("UBotCore"), TEXT("^0.1.0"));

    FUBotPackageInfo& Optional = Packages.Add_GetRef(MakePackage(TEXT("UBotOpt"), TEXT("1.0.0"), EUBotPackageState::Enabled));
    AddRequirement(Optional, TEXT("UBotMissing"), TEXT(">=1.0"), true);
    AddRequirement(Optional, TEXT("UBotCore"), TEXT("^0.1.0"), true);
    AddRequirement(Optional, TEXT("UBotSensor"), TEXT("^2.0"), true);
    AddRequirement(Optional, TEXT("UBotOffline"), TEXT("^0.1.0"), true);
    AddRequirement(Optional, TEXT("UBotBad"), TEXT("1.x"), true);

    // Only State set: still installed and enabled.
    FUBotPackageInfo& StateOnly = Packages.AddDefaulted_GetRef();
    StateOnly.Name = TEXT("UBotStateOnly");
    StateOnly.Version = TEXT("1.0.0");
    StateOnly.State = EUBotPackageState::Enabled;
    AddRequirement(StateOnly, TEXT("UBotCore"), TEXT(""));

    FUBotPackageInfo& NeedsStateOnly = Packages.Add_GetRef(MakePackage(TEXT("UBotNeedsStateOnly"), TEXT("1.0.0"), EUBotPackageState::Enabled));
    AddRequirement(NeedsStateOnly, TEXT("UBotStateOnly"), TEXT("^1.0"));

    FUBotPackageRegistry::ValidatePackageSet(Packages);

    TestEqual(TEXT("Enabled package requiring a disabled one"), ProblemCount(Packages, TEXT("UBotSensor")), 1);
    TestTrue(TEXT("Disabled dependency is reported"), PackageHasProblem(Packages, TEXT("UBotSensor"), TEXT("which is disabled")));
    TestEqual(TEXT("Disabled package requiring a disabled one is fine"), ProblemCount(Packages, TEXT("UBotOffline")), 0);

    TestEqual(TEXT("Optional requirements: incompatible and unparsable only"), ProblemCount(Packages, TEXT("UBotOpt")), 2);
    TestTrue(TEXT("Present but incompatible optional requirement is reported"), PackageHasProblem(Packages, TEXT("UBotOpt"), TEXT("'UBotSensor' ^2.0")));
    TestTrue(TEXT("Unparsable optional constraint is reported"), PackageHasProblem(Packages, TEXT("UBotOpt"), TEXT("unparsable")));
    TestFalse(TEXT("Missing optional requirement is not reported"), PackageHasProblem(Packages, TEXT("UBotOpt"), TEXT("UBotMissing")));
    TestFalse(TEXT("Disabled optional requirement is not reported"), PackageHasProblem(Packages, TEXT("UBotOpt"), TEXT("disabled")));

    TestEqual(TEXT("State alone marks a package enabled"), ProblemCount(Packages, TEXT("UBotStateOnly")), 1);
    TestTrue(TEXT("State-only package sees its disabled dependency"), PackageHasProblem(Packages, TEXT("UBotStateOnly"), TEXT("which is disabled")));
    TestEqual(TEXT("State alone marks a package installed and enabled"), ProblemCount(Packages, TEXT("UBotNeedsStateOnly")), 0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotPackageValidateCyclesTest, "UBotCore.Packages.ValidateCycles", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotPackageValidateCyclesTest::RunTest(const FString& Parameters)
{
    using namespace UBotPackageTests;

    TArray<FUBotPackageInfo> Packages;
    FUBotPackageInfo& A = Packages.Add_GetRef(MakePackage(TEXT("PkgA"), TEXT("1.0.0"), EUBotPackageState::Enabled));
    AddRequirement(A, TEXT("PkgB"), TEXT(""));
    FUBotPackageInfo& B = Packages.Add_GetRef(MakePackage(TEXT("PkgB"), TEXT("1.0.0"), EUBotPackageState::Enabled));
    AddRequirement(B, TEXT("PkgC"), TEXT(""));
    FUBotPackageInfo& C = Packages.Add_GetRef(MakePackage(TEXT("PkgC"), TEXT("1.0.0"), EUBotPackageState::Enabled));
    AddRequirement(C, TEXT("PkgA"), TEXT(""));
    FUBotPackageInfo& D = Packages.Add_GetRef(MakePackage(TEXT("PkgD"), TEXT("1.0.0"), EUBotPackageState::Enabled));
    AddRequirement(D, TEXT("PkgA"), TEXT(""));
    FUBotPackageInfo& E = Packages.Add_GetRef(MakePackage(TEXT("PkgE"), TEXT("1.0.0"), EUBotPackageState::Enabled));
    AddRequirement(E, TEXT("PkgE"), TEXT(""));
    FUBotPackageInfo& F = Packages.Add_GetRef(MakePackage(TEXT("PkgF"), TEXT("1.0.0"), EUBotPackageState::Enabled));
    AddRequirement(F, TEXT("PkgG"), TEXT(""), true);
    FUBotPackageInfo& G = Packages.Add_GetRef(MakePackage(TEXT("PkgG"), TEXT("1.0.0"), EUBotPackageState::Enabled));
    AddRequirement(G, TEXT("PkgF"), TEXT(""));

    FUBotPackageRegistry::ValidatePackageSet(Packages);

    TestEqual(TEXT("A is on one cycle"), ProblemCount(Packages, TEXT("PkgA")), 1);
    TestTrue(TEXT("A's cycle is spelled out"), PackageHasProblem(Packages, TEXT("PkgA"), TEXT("Dependency cycle: PkgA -> PkgB -> PkgC -> PkgA.")));
    TestTrue(TEXT("B's cycle starts at B"), PackageHasProblem(Packages, TEXT("PkgB"), TEXT("Dependency cycle: PkgB -> PkgC -> PkgA -> PkgB.")));
    TestEqual(TEXT("C is on one cycle"), ProblemCount(Packages, TEXT("PkgC")), 1);
    TestEqual(TEXT("D only depends on the cycle"), ProblemCount(Packages, TEXT("PkgD")), 0);
    TestTrue(TEXT("Self requirement is a cycle"), PackageHasProblem(Packages, TEXT("PkgE"), TEXT("PkgE -> PkgE")));
    TestEqual(TEXT("Optional edges do not form cycles (F)"), ProblemCount(Packages, TEXT("PkgF")), 0);
    TestEqual(TEXT("Optional edges do not form cycles (G)"), ProblemCount(Packages, TEXT("PkgG")), 0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotPackageResolveOrderTest, "UBotCore.Packages.ResolveDependencyOrder", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotPackageResolveOrderTest::RunTest(const FString& Parameters)
{
    using namespace UBotPackageTests;

    // Diamond: Top -> Left, Right -> Base. Listed out of order on purpose.
    TArray<FUBotPackageInfo> Packages;
    Packages.Add(MakePackage(TEXT("Base"), TEXT("1.0.0"), EUBotPackageState::Enabled));
    FUBotPackageInfo& Right = Packages.Add_GetRef(MakePackage(TEXT("Right"), TEXT("1.0.0"), EUBotPackageState::NotInstalled));
    AddRequirement(Right, TEXT("Base"), TEXT("^1.0"));
    FUBotPackageInfo& Top = Packages.Add_GetRef(MakePackage(TEXT("Top"), TEXT("1.0.0"), EUBotPackageState::Disabled));
    AddRequirement(Top, TEXT("Left"), TEXT(""));
    AddRequirement(Top, TEXT("right"), TEXT(""));
    AddRequirement(Top, TEXT("Extra"), TEXT(""), true);
    FUBotPackageInfo& Left = Packages.Add_GetRef(MakePackage(TEXT("Left"), TEXT("1.0.0"), EUBotPackageState::Enabled));
    AddRequirement(Left, TEXT("Base"), TEXT(""));
    Packages.Add(MakePackage(TEXT("Extra"), TEXT("1.0.0"), EUBotPackageState::Enabled));

    TArray<FString> Order;
    FString Error = TEXT("stale");
    TestTrue(TEXT("Diamond resolves"), FUBotPackageRegistry::ResolveDependencyOrder(Packages, TEXT("Top"), Order, &Error));
    TestEqual(TEXT("Dependencies first, each once, optional excluded"), FString::Join(Order, TEXT(",")), FString(TEXT("Base,Left,Right,Top")));
    TestTrue(TEXT("Success clears the error"), Error.IsEmpty());

    TestTrue(TEXT("Lookup is case-insensitive"), FUBotPackageRegistry::ResolveDependencyOrder(Packages, TEXT("left"), Order));
    TestEqual(TEXT("Canonical names are returned"), FString::Join(Order, TEXT(",")), FString(TEXT("Base,Left")));

    TestTrue(TEXT("Leaf resolves to itself"), FUBotPackageRegistry::ResolveDependencyOrder(Packages, TEXT("Base"), Order));
    TestEqual(TEXT("Leaf order"), FString::Join(Order, TEXT(",")), FString(TEXT("Base")));

    // Cycle.
    TArray<FUBotPackageInfo> Cyclic;
    FUBotPackageInfo& Loop1 = Cyclic.Add_GetRef(MakePackage(TEXT("Loop1"), TEXT("1.0.0"), EUBotPackageState::Enabled));
    AddRequirement(Loop1, TEXT("Loop2"), TEXT(""));
    FUBotPackageInfo& Loop2 = Cyclic.Add_GetRef(MakePackage(TEXT("Loop2"), TEXT("1.0.0"), EUBotPackageState::Enabled));
    AddRequirement(Loop2, TEXT("Loop1"), TEXT(""));

    Order.Reset();
    Order.Add(TEXT("junk"));
    TestFalse(TEXT("Cycle fails"), FUBotPackageRegistry::ResolveDependencyOrder(Cyclic, TEXT("Loop1"), Order, &Error));
    TestTrue(TEXT("Cycle is described"), Error.Contains(TEXT("Loop1 -> Loop2 -> Loop1")));
    TestEqual(TEXT("Order is empty on a cycle"), Order.Num(), 0);

    // Missing packages.
    TArray<FUBotPackageInfo> Incomplete;
    FUBotPackageInfo& Needs = Incomplete.Add_GetRef(MakePackage(TEXT("Needs"), TEXT("1.0.0"), EUBotPackageState::Enabled));
    AddRequirement(Needs, TEXT("Ghost"), TEXT(""));
    AddRequirement(Needs, TEXT("Present"), TEXT(""));
    AddRequirement(Needs, TEXT("Optional"), TEXT(""), true);
    FUBotPackageInfo& Present = Incomplete.Add_GetRef(MakePackage(TEXT("Present"), TEXT("1.0.0"), EUBotPackageState::Enabled));
    AddRequirement(Present, TEXT("Phantom"), TEXT(""));

    Order.Reset();
    Order.Add(TEXT("junk"));
    TestFalse(TEXT("Missing packages fail"), FUBotPackageRegistry::ResolveDependencyOrder(Incomplete, TEXT("Needs"), Order, &Error));
    TestTrue(TEXT("Direct missing package is named"), Error.Contains(TEXT("'Ghost' (required by 'Needs')")));
    TestTrue(TEXT("Transitive missing package is named"), Error.Contains(TEXT("'Phantom' (required by 'Present')")));
    TestFalse(TEXT("Missing optional package is ignored"), Error.Contains(TEXT("Optional")));
    TestEqual(TEXT("Order is empty when packages are missing"), Order.Num(), 0);

    TestFalse(TEXT("Unknown root fails"), FUBotPackageRegistry::ResolveDependencyOrder(Packages, TEXT("Nobody"), Order, &Error));
    TestTrue(TEXT("Unknown root is named"), Error.Contains(TEXT("Nobody")));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotPackageFindDependentsTest, "UBotCore.Packages.FindDependents", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotPackageFindDependentsTest::RunTest(const FString& Parameters)
{
    using namespace UBotPackageTests;

    TArray<FUBotPackageInfo> Packages;
    Packages.Add(MakePackage(TEXT("Core"), TEXT("1.0.0"), EUBotPackageState::Enabled));
    FUBotPackageInfo& Sensor = Packages.Add_GetRef(MakePackage(TEXT("Sensor"), TEXT("1.0.0"), EUBotPackageState::Enabled));
    AddRequirement(Sensor, TEXT("Core"), TEXT(""));
    FUBotPackageInfo& Ros = Packages.Add_GetRef(MakePackage(TEXT("ROS"), TEXT("1.0.0"), EUBotPackageState::Disabled));
    AddRequirement(Ros, TEXT("Sensor"), TEXT(""));
    FUBotPackageInfo& App = Packages.Add_GetRef(MakePackage(TEXT("App"), TEXT("1.0.0"), EUBotPackageState::Enabled));
    AddRequirement(App, TEXT("ROS"), TEXT(""));
    FUBotPackageInfo& Viewer = Packages.Add_GetRef(MakePackage(TEXT("Viewer"), TEXT("1.0.0"), EUBotPackageState::Enabled));
    AddRequirement(Viewer, TEXT("Core"), TEXT(""), true);
    FUBotPackageInfo& Tool = Packages.Add_GetRef(MakePackage(TEXT("Tool"), TEXT("1.0.0"), EUBotPackageState::Enabled));
    AddRequirement(Tool, TEXT("core"), TEXT(""));
    Packages.Add(MakePackage(TEXT("Island"), TEXT("1.0.0"), EUBotPackageState::Enabled));

    const TArray<FString> All = FUBotPackageRegistry::FindDependents(Packages, TEXT("Core"), false);
    TestEqual(TEXT("Direct and transitive dependents"), All.Num(), 4);
    TestTrue(TEXT("Sensor depends on Core"), All.Contains(TEXT("Sensor")));
    TestTrue(TEXT("ROS depends on Core through Sensor"), All.Contains(TEXT("ROS")));
    TestTrue(TEXT("App depends on Core through ROS"), All.Contains(TEXT("App")));
    TestTrue(TEXT("Tool depends on Core (case-insensitive)"), All.Contains(TEXT("Tool")));
    TestFalse(TEXT("Optional dependents are not included"), All.Contains(TEXT("Viewer")));
    TestFalse(TEXT("Unrelated packages are not included"), All.Contains(TEXT("Island")));
    TestFalse(TEXT("The package itself is not included"), All.Contains(TEXT("Core")));
    TestTrue(TEXT("Dependents come first: App before ROS"), All.IndexOfByKey(TEXT("App")) < All.IndexOfByKey(TEXT("ROS")));
    TestTrue(TEXT("Dependents come first: ROS before Sensor"), All.IndexOfByKey(TEXT("ROS")) < All.IndexOfByKey(TEXT("Sensor")));

    const TArray<FString> Enabled = FUBotPackageRegistry::FindDependents(Packages, TEXT("Core"), true);
    TestEqual(TEXT("Only enabled dependents"), Enabled.Num(), 3);
    TestFalse(TEXT("Disabled ROS is filtered out"), Enabled.Contains(TEXT("ROS")));
    TestTrue(TEXT("App is still found through disabled ROS"), Enabled.Contains(TEXT("App")));
    TestTrue(TEXT("Enabled order keeps App before Sensor"), Enabled.IndexOfByKey(TEXT("App")) < Enabled.IndexOfByKey(TEXT("Sensor")));

    TestEqual(TEXT("Leaf package has no dependents"), FUBotPackageRegistry::FindDependents(Packages, TEXT("App"), false).Num(), 0);

    FUBotPackageInfo& Haunted = Packages.Add_GetRef(MakePackage(TEXT("Haunted"), TEXT("1.0.0"), EUBotPackageState::Enabled));
    AddRequirement(Haunted, TEXT("Ghost"), TEXT(""));
    const TArray<FString> GhostDependents = FUBotPackageRegistry::FindDependents(Packages, TEXT("Ghost"), false);
    TestEqual(TEXT("Dependents of a package missing from the set"), FString::Join(GhostDependents, TEXT(",")), FString(TEXT("Haunted")));

    TArray<FUBotPackageInfo> Cyclic;
    FUBotPackageInfo& X = Cyclic.Add_GetRef(MakePackage(TEXT("X"), TEXT("1.0.0"), EUBotPackageState::Enabled));
    AddRequirement(X, TEXT("Y"), TEXT(""));
    FUBotPackageInfo& Y = Cyclic.Add_GetRef(MakePackage(TEXT("Y"), TEXT("1.0.0"), EUBotPackageState::Enabled));
    AddRequirement(Y, TEXT("X"), TEXT(""));
    TestEqual(TEXT("Cycles never report the package itself"), FString::Join(FUBotPackageRegistry::FindDependents(Cyclic, TEXT("X"), false), TEXT(",")), FString(TEXT("Y")));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotPackageIndexParsingTest, "UBotCore.Packages.IndexParsing", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotPackageIndexParsingTest::RunTest(const FString& Parameters)
{
    TArray<FUBotPackageInfo> Packages;
    FString Error = TEXT("stale");
    const bool bParsed = FUBotPackageIndex::ParseIndexJson(TEXT(R"json({
        "FormatVersion": 1,
        "Packages": [
            { "Name": "UBotCore", "Layer": "Foundation", "Repository": "https://example.com/core.git", "Version": "0.1.0", "Tags": [ "core" ] },
            {
                "Name": "UBotROS",
                "FriendlyName": "uBot ROS",
                "Description": "ROS adapter",
                "Layer": "Adapter",
                "Repository": "https://example.com/ros.git",
                "DocsUrl": "https://example.com/ros",
                "Version": "0.2.0",
                "Tags": [ "ros" ],
                "Provides": [ "ROS.TF" ],
                "Requires": [ { "Name": "UBotCore", "Version": "^0.1.0" }, { "Name": "UBotSensor", "Version": "^0.1.0", "Optional": true } ],
                "ExternalRequires": [ "TempoROS" ]
            }
        ]
    })json"), Packages, &Error);

    TestTrue(FString::Printf(TEXT("Valid index parses (%s)"), *Error), bParsed);
    TestTrue(TEXT("Success clears the error"), Error.IsEmpty());
    if (TestEqual(TEXT("Two entries"), Packages.Num(), 2))
    {
        const FUBotPackageInfo& Core = Packages[0];
        TestEqual(TEXT("Core name"), Core.Name, FString(TEXT("UBotCore")));
        TestEqual(TEXT("FriendlyName falls back to Name"), Core.FriendlyName, FString(TEXT("UBotCore")));
        TestEqual(TEXT("Core layer"), Core.Layer, EUBotPackageLayer::Foundation);
        TestEqual(TEXT("Index entries are not installed"), Core.State, EUBotPackageState::NotInstalled);
        TestFalse(TEXT("Index entries are not marked installed"), Core.bInstalled);
        TestTrue(TEXT("Index entries are marked as indexed"), Core.bFromIndex);

        const FUBotPackageInfo& Ros = Packages[1];
        TestEqual(TEXT("ROS friendly name"), Ros.FriendlyName, FString(TEXT("uBot ROS")));
        TestEqual(TEXT("ROS description"), Ros.Description, FString(TEXT("ROS adapter")));
        TestEqual(TEXT("ROS layer"), Ros.Layer, EUBotPackageLayer::Adapter);
        TestEqual(TEXT("ROS repository"), Ros.Repository, FString(TEXT("https://example.com/ros.git")));
        TestEqual(TEXT("ROS docs"), Ros.DocsUrl, FString(TEXT("https://example.com/ros")));
        TestEqual(TEXT("ROS latest version"), Ros.Version, FString(TEXT("0.2.0")));
        TestEqual(TEXT("ROS provides"), FString::Join(Ros.Provides, TEXT(",")), FString(TEXT("ROS.TF")));
        TestEqual(TEXT("ROS external requirements"), FString::Join(Ros.ExternalRequires, TEXT(",")), FString(TEXT("TempoROS")));
        if (TestEqual(TEXT("ROS requirements"), Ros.Requires.Num(), 2))
        {
            TestEqual(TEXT("First requirement"), Ros.Requires[0].Name, FString(TEXT("UBotCore")));
            TestFalse(TEXT("First requirement is required"), Ros.Requires[0].bOptional);
            TestTrue(TEXT("Second requirement is optional"), Ros.Requires[1].bOptional);
        }
    }

    TestTrue(TEXT("FormatVersion may be omitted"), FUBotPackageIndex::ParseIndexJson(TEXT(R"json({ "Packages": [] })json"), Packages));
    TestEqual(TEXT("Empty index"), Packages.Num(), 0);

    TestFalse(TEXT("Invalid JSON fails"), FUBotPackageIndex::ParseIndexJson(TEXT("not json"), Packages, &Error));
    TestFalse(TEXT("Invalid JSON is explained"), Error.IsEmpty());
    TestEqual(TEXT("Invalid JSON yields nothing"), Packages.Num(), 0);

    TestFalse(TEXT("Top-level array fails"), FUBotPackageIndex::ParseIndexJson(TEXT("[]"), Packages, &Error));

    TestFalse(TEXT("Unsupported FormatVersion fails"), FUBotPackageIndex::ParseIndexJson(TEXT(R"json({ "FormatVersion": 2, "Packages": [ { "Name": "A" } ] })json"), Packages, &Error));
    TestTrue(TEXT("Unsupported FormatVersion is explained"), Error.Contains(TEXT("FormatVersion")));
    TestEqual(TEXT("Unsupported FormatVersion yields nothing"), Packages.Num(), 0);

    TestFalse(TEXT("Missing Packages fails"), FUBotPackageIndex::ParseIndexJson(TEXT(R"json({ "FormatVersion": 1 })json"), Packages, &Error));
    TestTrue(TEXT("Missing Packages is explained"), Error.Contains(TEXT("Packages")));

    const bool bPartial = FUBotPackageIndex::ParseIndexJson(TEXT(R"json({
        "FormatVersion": 1,
        "Packages": [
            { "Name": "A" },
            { "Description": "no name" },
            42,
            { "Name": "a" },
            { "Name": "B", "Layer": "Sideways", "Version": "one" }
        ]
    })json"), Packages, &Error);
    TestFalse(TEXT("Broken entries make the parse fail"), bPartial);
    TestEqual(TEXT("Valid entries are still returned"), Packages.Num(), 2);
    TestTrue(TEXT("Nameless entry is reported"), Error.Contains(TEXT("Packages[1]")));
    TestTrue(TEXT("Non-object entry is reported"), Error.Contains(TEXT("Packages[2]")));
    TestTrue(TEXT("Duplicate entry is reported"), Error.Contains(TEXT("Packages[3]")));
    TestTrue(TEXT("Bad layer is reported"), Error.Contains(TEXT("Sideways")));
    TestTrue(TEXT("Bad version is reported"), Error.Contains(TEXT("'one'")));
    if (Packages.Num() == 2)
    {
        TestEqual(TEXT("First of the duplicates wins"), Packages[0].Name, FString(TEXT("A")));
        TestEqual(TEXT("Entry with a bad layer is kept as Unknown"), Packages[1].Layer, EUBotPackageLayer::Unknown);
    }

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotPackageIndexMergeTest, "UBotCore.Packages.IndexMerge", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotPackageIndexMergeTest::RunTest(const FString& Parameters)
{
    using namespace UBotPackageTests;

    TArray<FUBotPackageInfo> Installed;
    FUBotPackageInfo& InstalledCore = Installed.Add_GetRef(MakePackage(TEXT("UBotCore"), TEXT("0.1.0"), EUBotPackageState::Enabled));
    InstalledCore.Description = TEXT("Installed description");
    InstalledCore.DocsUrl = TEXT("https://installed/docs");
    InstalledCore.BaseDir = TEXT("C:/Project/Plugins/uBot/uBotCore");
    FUBotPackageInfo& Local = Installed.Add_GetRef(MakePackage(TEXT("MyLocalPackage"), TEXT("1.0.0"), EUBotPackageState::Disabled));
    Local.Repository = TEXT("https://local/repo.git");

    TArray<FUBotPackageInfo> Index;
    FUBotPackageInfo& IndexCore = Index.AddDefaulted_GetRef();
    IndexCore.Name = TEXT("ubotcore");
    IndexCore.Version = TEXT("0.2.0");
    IndexCore.Description = TEXT("Index description");
    IndexCore.Repository = TEXT("https://index/core.git");
    IndexCore.DocsUrl = TEXT("https://index/docs");
    IndexCore.bFromIndex = true;

    FUBotPackageInfo& IndexSensor = Index.AddDefaulted_GetRef();
    IndexSensor.Name = TEXT("UBotSensor");
    IndexSensor.Version = TEXT("0.1.0");
    IndexSensor.Repository = TEXT("https://index/sensor.git");
    IndexSensor.Problems.Add(TEXT("should not survive"));
    // Deliberately inconsistent flags: the merge must normalise index-only entries.
    IndexSensor.bInstalled = true;
    IndexSensor.State = EUBotPackageState::Enabled;
    AddRequirement(IndexSensor, TEXT("UBotCore"), TEXT("^0.1.0"));

    const TArray<FUBotPackageInfo> Merged = FUBotPackageIndex::MergeInstalledWithIndex(Installed, Index);
    if (!TestEqual(TEXT("Installed entries plus index-only entries"), Merged.Num(), 3))
    {
        return false;
    }

    const FUBotPackageInfo& Core = Merged[0];
    TestEqual(TEXT("Installed entries keep their position"), Core.Name, FString(TEXT("UBotCore")));
    TestTrue(TEXT("Installed entry found in the index is marked"), Core.bFromIndex);
    TestEqual(TEXT("Installed version wins"), Core.Version, FString(TEXT("0.1.0")));
    TestEqual(TEXT("Installed description wins"), Core.Description, FString(TEXT("Installed description")));
    TestEqual(TEXT("Empty repository is filled from the index"), Core.Repository, FString(TEXT("https://index/core.git")));
    TestEqual(TEXT("Existing docs URL is kept"), Core.DocsUrl, FString(TEXT("https://installed/docs")));
    TestEqual(TEXT("Installed state is kept"), Core.State, EUBotPackageState::Enabled);
    TestFalse(TEXT("Installed base dir is kept"), Core.BaseDir.IsEmpty());

    const FUBotPackageInfo& LocalMerged = Merged[1];
    TestEqual(TEXT("Installed-only entry stays"), LocalMerged.Name, FString(TEXT("MyLocalPackage")));
    TestFalse(TEXT("Installed-only entry is not from the index"), LocalMerged.bFromIndex);

    const FUBotPackageInfo& Sensor = Merged[2];
    TestEqual(TEXT("Index-only entry is appended"), Sensor.Name, FString(TEXT("UBotSensor")));
    TestEqual(TEXT("Index-only entry is not installed"), Sensor.State, EUBotPackageState::NotInstalled);
    TestFalse(TEXT("Index-only entry has bInstalled cleared"), Sensor.bInstalled);
    TestFalse(TEXT("Index-only entry has bEnabled cleared"), Sensor.bEnabled);
    TestTrue(TEXT("Index-only entry is from the index"), Sensor.bFromIndex);
    TestEqual(TEXT("Index-only entry keeps its requirements"), Sensor.Requires.Num(), 1);
    TestEqual(TEXT("Index-only entry has no problems"), Sensor.Problems.Num(), 0);

    TArray<FUBotPackageInfo> View = Merged;
    FUBotPackageRegistry::ValidatePackageSet(View);
    TestEqual(TEXT("Merged view validates cleanly"), View[2].Problems.Num(), 0);

    TArray<FString> InstallOrder;
    TestTrue(TEXT("Merged view resolves index-only packages"), FUBotPackageRegistry::ResolveDependencyOrder(View, TEXT("UBotSensor"), InstallOrder));
    TestEqual(TEXT("Install order"), FString::Join(InstallOrder, TEXT(",")), FString(TEXT("UBotCore,UBotSensor")));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotPackageBuiltInIndexTest, "UBotCore.Packages.BuiltInIndex", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotPackageBuiltInIndexTest::RunTest(const FString& Parameters)
{
    using namespace UBotPackageTests;

    const TSharedPtr<IPlugin> CorePlugin = IPluginManager::Get().FindPlugin(TEXT("UBotCore"));
    if (!TestTrue(TEXT("UBotCore plugin is discovered"), CorePlugin.IsValid()))
    {
        return false;
    }

    const FString IndexPath = FPaths::Combine(CorePlugin->GetBaseDir(), TEXT("Resources"), TEXT("PackageIndex.json"));
    TArray<FUBotPackageInfo> Index;
    FString Error;
    const bool bLoaded = FUBotPackageIndex::LoadIndexFile(IndexPath, Index, &Error);
    if (!TestTrue(FString::Printf(TEXT("Built-in index loads cleanly (%s)"), *Error), bLoaded))
    {
        return false;
    }

    const FUBotPackageInfo* Core = FindByName(Index, TEXT("UBotCore"));
    const FUBotPackageInfo* Sensor = FindByName(Index, TEXT("UBotSensor"));
    const FUBotPackageInfo* Ros = FindByName(Index, TEXT("UBotROS"));
    if (!TestTrue(TEXT("Index lists UBotCore, UBotSensor and UBotROS"), Core && Sensor && Ros))
    {
        return false;
    }

    TestEqual(TEXT("Core repository"), Core->Repository, FString(TEXT("https://github.com/AstreoX/uBotCore.git")));
    TestEqual(TEXT("Sensor repository"), Sensor->Repository, FString(TEXT("https://github.com/AstreoX/uBotSensor.git")));
    TestEqual(TEXT("ROS repository"), Ros->Repository, FString(TEXT("https://github.com/AstreoX/uBotROS.git")));
    TestEqual(TEXT("Core layer"), Core->Layer, EUBotPackageLayer::Foundation);
    TestEqual(TEXT("Core has no requirements"), Core->Requires.Num(), 0);

    if (TestEqual(TEXT("Sensor has one requirement"), Sensor->Requires.Num(), 1))
    {
        TestEqual(TEXT("Sensor requires Core"), Sensor->Requires[0].Name, FString(TEXT("UBotCore")));
        TestEqual(TEXT("Sensor requires Core ^0.1.0"), Sensor->Requires[0].Version, FString(TEXT("^0.1.0")));
    }
    if (TestEqual(TEXT("ROS has two requirements"), Ros->Requires.Num(), 2))
    {
        TestEqual(TEXT("ROS requires Core"), Ros->Requires[0].Name, FString(TEXT("UBotCore")));
        TestEqual(TEXT("ROS requires Core ^0.1.0"), Ros->Requires[0].Version, FString(TEXT("^0.1.0")));
        TestEqual(TEXT("ROS requires Sensor"), Ros->Requires[1].Name, FString(TEXT("UBotSensor")));
        TestEqual(TEXT("ROS requires Sensor ^0.1.0"), Ros->Requires[1].Version, FString(TEXT("^0.1.0")));
    }
    TestEqual(TEXT("ROS has no external requirements"), Ros->ExternalRequires.Num(), 0);

    // Capability keys of packages that are not installed come from this index only, so they have to
    // match the "UBot" blocks in the packages' own descriptors (see the sensor and ROS contracts).
    TestEqual(TEXT("Sensor provides"), FString::Join(Sensor->Provides, TEXT(",")),
        FString(TEXT("Sensor.Pose,Sensor.Odometry,Sensor.IMU,Sensor.Lidar2D,Sensor.Lidar3D,Sensor.RGBCamera,Sensor.DepthCamera,Sensor.Medium")));
    TestEqual(TEXT("Sensor tags"), FString::Join(Sensor->Tags, TEXT(",")), FString(TEXT("sensor")));
    TestEqual(TEXT("ROS provides"), FString::Join(Ros->Provides, TEXT(",")),
        FString(TEXT("ROS.SensorPublishing,ROS.TF,ROS.Clock,ROS.VelocityCommand")));
    TestEqual(TEXT("ROS tags"), FString::Join(Ros->Tags, TEXT(",")), FString(TEXT("ros,ros2,adapter")));

    // UBotCore is installed here, so its index entry can be compared with the descriptor it mirrors.
    {
        FUBotPackageRegistry Registry;
        Registry.Refresh();
        if (const FUBotPackageInfo* InstalledCore = Registry.FindPackage(TEXT("UBotCore")))
        {
            TestEqual(TEXT("Core index provides match the descriptor"),
                FString::Join(Core->Provides, TEXT(",")), FString::Join(InstalledCore->Provides, TEXT(",")));
            TestEqual(TEXT("Core index tags match the descriptor"),
                FString::Join(Core->Tags, TEXT(",")), FString::Join(InstalledCore->Tags, TEXT(",")));
            TestEqual(TEXT("Core index version matches the descriptor"), Core->Version, InstalledCore->Version);
        }
    }

    TArray<FString> Order;
    TestTrue(TEXT("ROS resolves over the index"), FUBotPackageRegistry::ResolveDependencyOrder(Index, TEXT("UBotROS"), Order));
    TestEqual(TEXT("ROS install order"), FString::Join(Order, TEXT(",")), FString(TEXT("UBotCore,UBotSensor,UBotROS")));

    TArray<FUBotPackageInfo> AllEnabled = Index;
    for (FUBotPackageInfo& Package : AllEnabled)
    {
        Package.State = EUBotPackageState::Enabled;
    }
    FUBotPackageRegistry::ValidatePackageSet(AllEnabled);
    for (const FUBotPackageInfo& Package : AllEnabled)
    {
        TestEqual(FString::Printf(TEXT("%s is consistent with the rest of the index"), *Package.Name), Package.Problems.Num(), 0);
    }

    const TArray<FUBotPackageInfo> Configured = FUBotPackageIndex::LoadConfiguredIndex();
    TestNotNull(TEXT("Configured index includes the built-in UBotCore entry"), FindByName(Configured, TEXT("UBotCore")));
    TestNotNull(TEXT("Configured index includes the built-in UBotROS entry"), FindByName(Configured, TEXT("UBotROS")));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotPackageRegistryScanTest, "UBotCore.Packages.RegistryFindsCore", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotPackageRegistryScanTest::RunTest(const FString& Parameters)
{
    // A private instance so the shared registry and its listeners are left alone.
    FUBotPackageRegistry Registry;
    Registry.Refresh();

    const FUBotPackageInfo* Core = Registry.FindPackage(TEXT("ubotcore"));
    if (!TestNotNull(TEXT("UBotCore is an installed uBot package"), Core))
    {
        return false;
    }

    TestEqual(TEXT("Canonical name"), Core->Name, FString(TEXT("UBotCore")));
    TestEqual(TEXT("Layer from the descriptor"), Core->Layer, EUBotPackageLayer::Foundation);
    TestTrue(TEXT("Installed"), Core->bInstalled);
    TestTrue(TEXT("Enabled"), Core->bEnabled);
    TestEqual(TEXT("State"), Core->State, EUBotPackageState::Enabled);
    TestTrue(TEXT("Enabled through the registry API"), Registry.IsPackageEnabled(TEXT("UBotCore")));
    TestEqual(TEXT("Repository from the descriptor"), Core->Repository, FString(TEXT("https://github.com/AstreoX/uBotCore.git")));
    TestTrue(TEXT("Descriptor path points at the .uplugin"), Core->DescriptorPath.EndsWith(TEXT("UBotCore.uplugin")));
    TestFalse(TEXT("Base dir is known"), Core->BaseDir.IsEmpty());
    TestTrue(TEXT("Provides the package manager"), Core->Provides.Contains(TEXT("Core.PackageManager")));
    TestEqual(TEXT("Core validates cleanly"), Core->Problems.Num(), 0);

    FUBotSemVer Version;
    TestTrue(TEXT("Core version is a semantic version"), FUBotSemVer::Parse(Core->Version, Version));

    TestNull(TEXT("Plain engine plugins are not uBot packages"), Registry.FindPackage(TEXT("Paper2D")));
    TestFalse(TEXT("Unknown packages are not enabled"), Registry.IsPackageEnabled(TEXT("NoSuchPackage")));

    const TArray<FUBotPackageInfo>& Installed = Registry.GetInstalledPackages();
    for (int32 Index = 1; Index < Installed.Num(); ++Index)
    {
        TestTrue(TEXT("Installed packages are sorted by name"), Installed[Index - 1].Name.Compare(Installed[Index].Name, ESearchCase::IgnoreCase) <= 0);
    }

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotPackageLibraryTest, "UBotCore.Packages.BlueprintLibrary", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotPackageLibraryTest::RunTest(const FString& Parameters)
{
    using namespace UBotPackageTests;

    // The shared registry is refreshed when UBotCore starts up.
    TestNotNull(TEXT("Installed packages include UBotCore"), FindByName(UUBotPackageLibrary::GetInstalledUBotPackages(), TEXT("UBotCore")));
    TestTrue(TEXT("UBotCore is enabled"), UUBotPackageLibrary::IsUBotPackageEnabled(TEXT("UBotCore")));
    TestFalse(TEXT("Unknown package is not enabled"), UUBotPackageLibrary::IsUBotPackageEnabled(TEXT("NoSuchPackage")));

    FUBotPackageInfo Info;
    TestTrue(TEXT("Find succeeds for UBotCore"), UUBotPackageLibrary::FindUBotPackage(TEXT("ubotcore"), Info));
    TestEqual(TEXT("Find returns the package"), Info.Name, FString(TEXT("UBotCore")));
    TestFalse(TEXT("Find fails for an unknown package"), UUBotPackageLibrary::FindUBotPackage(TEXT("NoSuchPackage"), Info));
    TestTrue(TEXT("Failed find resets the output"), Info.Name.IsEmpty());

    TestFalse(TEXT("UBotCore has a version"), UUBotPackageLibrary::GetUBotPackageVersion(TEXT("UBotCore")).IsEmpty());
    TestTrue(TEXT("Unknown package has no version"), UUBotPackageLibrary::GetUBotPackageVersion(TEXT("NoSuchPackage")).IsEmpty());

    TestTrue(TEXT("Lower bound is satisfied"), UUBotPackageLibrary::IsUBotPackageVersionSatisfied(TEXT("UBotCore"), TEXT(">=0.0.1")));
    TestTrue(TEXT("Any version is satisfied"), UUBotPackageLibrary::IsUBotPackageVersionSatisfied(TEXT("UBotCore"), TEXT("*")));
    TestFalse(TEXT("Impossible bound is not satisfied"), UUBotPackageLibrary::IsUBotPackageVersionSatisfied(TEXT("UBotCore"), TEXT("<0.0.1")));
    TestFalse(TEXT("Unparsable constraint is not satisfied"), UUBotPackageLibrary::IsUBotPackageVersionSatisfied(TEXT("UBotCore"), TEXT("not a constraint")));
    TestFalse(TEXT("Missing package never satisfies"), UUBotPackageLibrary::IsUBotPackageVersionSatisfied(TEXT("NoSuchPackage"), TEXT("*")));

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
