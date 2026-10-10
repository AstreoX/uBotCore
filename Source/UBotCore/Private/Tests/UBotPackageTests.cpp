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
    TestEqual(TEXT("Content"), ParsePackageLayer(TEXT("Content")), EUBotPackageLayer::Content);
    TestEqual(TEXT("Composition is no longer a layer"), ParsePackageLayer(TEXT("composition")), EUBotPackageLayer::Unknown);
    TestEqual(TEXT("Empty is unknown"), ParsePackageLayer(TEXT("")), EUBotPackageLayer::Unknown);
    TestEqual(TEXT("Garbage is unknown"), ParsePackageLayer(TEXT("Layers")), EUBotPackageLayer::Unknown);

    const EUBotPackageLayer AllLayers[] =
    {
        EUBotPackageLayer::Unknown, EUBotPackageLayer::Foundation, EUBotPackageLayer::Capability,
        EUBotPackageLayer::Adapter, EUBotPackageLayer::Content
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
    TestEqual(TEXT("The legacy key ExternalRequires is still read"), FString::Join(Info.ExternalRequires, TEXT(",")), FString(TEXT("TempoROS")));

    const TSharedPtr<FJsonObject> EnginePlugins = ParseJsonObject(TEXT(R"json({ "VersionName": "1.0.0", "UBot": { "Layer": "Capability",
        "EnginePlugins": [ "ChaosVehiclesPlugin", "EnhancedInput" ] } })json"));
    FUBotPackageInfo EngineInfo;
    FString EngineIssues;
    TestTrue(TEXT("Descriptor with EnginePlugins parses"), FUBotPackageRegistry::ParseDescriptor(*EnginePlugins, TEXT("Pkg"), EngineInfo, &EngineIssues));
    TestTrue(TEXT("EnginePlugins is not an issue"), EngineIssues.IsEmpty());
    TestEqual(TEXT("EnginePlugins is read"), FString::Join(EngineInfo.ExternalRequires, TEXT(",")), FString(TEXT("ChaosVehiclesPlugin,EnhancedInput")));

    const TSharedPtr<FJsonObject> BothKeys = ParseJsonObject(TEXT(R"json({ "VersionName": "1.0.0", "UBot": { "Layer": "Capability",
        "EnginePlugins": [ "ChaosVehiclesPlugin" ], "ExternalRequires": [ "chaosvehiclesplugin", "LearningAgents" ] } })json"));
    FUBotPackageInfo BothInfo;
    TestTrue(TEXT("Descriptor with both keys parses"), FUBotPackageRegistry::ParseDescriptor(*BothKeys, TEXT("Pkg"), BothInfo));
    TestEqual(TEXT("Both keys are merged without duplicates"), FString::Join(BothInfo.ExternalRequires, TEXT(",")), FString(TEXT("ChaosVehiclesPlugin,LearningAgents")));

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

    // ProblemDetails mirrors Problems entry by entry.
    for (const FUBotPackageInfo& Package : Packages)
    {
        if (!TestEqual(FString::Printf(TEXT("%s: one detail per problem"), *Package.Name), Package.ProblemDetails.Num(), Package.Problems.Num()))
        {
            continue;
        }
        for (int32 ProblemIndex = 0; ProblemIndex < Package.Problems.Num(); ++ProblemIndex)
        {
            TestEqual(FString::Printf(TEXT("%s: detail %d has the problem text"), *Package.Name, ProblemIndex), Package.ProblemDetails[ProblemIndex].Message, Package.Problems[ProblemIndex]);
        }
    }

    const FUBotPackageInfo* PlannerInfo = FindByName(Packages, TEXT("UBotPlanner"));
    if (PlannerInfo && TestEqual(TEXT("Planner has two details"), PlannerInfo->ProblemDetails.Num(), 2))
    {
        const FUBotPackageProblem& Version = PlannerInfo->ProblemDetails[0];
        TestEqual(TEXT("Version mismatch kind"), Version.Kind, EUBotPackageProblemKind::RequiresVersion);
        TestEqual(TEXT("Version mismatch dependency"), Version.Dependency, FString(TEXT("UBotCore")));
        TestEqual(TEXT("Version mismatch constraint"), Version.Constraint, FString(TEXT("^0.2.0")));
        const FUBotPackageProblem& Missing = PlannerInfo->ProblemDetails[1];
        TestEqual(TEXT("Missing dependency kind"), Missing.Kind, EUBotPackageProblemKind::RequiresMissing);
        TestEqual(TEXT("Missing dependency name"), Missing.Dependency, FString(TEXT("UBotMaps")));
        TestTrue(TEXT("Missing dependency without constraint"), Missing.Constraint.IsEmpty());
    }
    const FUBotPackageInfo* BrokenInfo = FindByName(Packages, TEXT("UBotBroken"));
    if (BrokenInfo && TestEqual(TEXT("Broken has two details"), BrokenInfo->ProblemDetails.Num(), 2))
    {
        TestEqual(TEXT("Unparsable constraint kind"), BrokenInfo->ProblemDetails[0].Kind, EUBotPackageProblemKind::InvalidRequirement);
        TestEqual(TEXT("Nameless requirement kind"), BrokenInfo->ProblemDetails[1].Kind, EUBotPackageProblemKind::InvalidRequirement);
    }

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
    const FUBotPackageInfo* SensorInfo = FindByName(Packages, TEXT("UBotSensor"));
    TestTrue(TEXT("Disabled dependency detail"), SensorInfo && SensorInfo->ProblemDetails.Num() == 1
        && SensorInfo->ProblemDetails[0].Kind == EUBotPackageProblemKind::RequiresDisabled && SensorInfo->ProblemDetails[0].Dependency == TEXT("UBotCore"));
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
    const FUBotPackageInfo* CycleInfo = FindByName(Packages, TEXT("PkgA"));
    TestTrue(TEXT("Cycle detail"), CycleInfo && CycleInfo->ProblemDetails.Num() == 1 && CycleInfo->ProblemDetails[0].Kind == EUBotPackageProblemKind::DependencyCycle);
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
    TArray<FUBotPackageReplacement> Replacements;
    FString Error = TEXT("stale");
    const bool bParsed = FUBotPackageIndex::ParseIndexJson(TEXT(R"json({
        "$schema": "./index.schema.json",
        "formatVersion": 2,
        "name": "test",
        "packages": [
            {
                "name": "UBotCore",
                "layer": "Foundation",
                "repository": "https://example.com/core.git",
                "tags": [ "core" ],
                "versions": [ { "version": "0.1.0", "ref": "v0.1.0" } ]
            },
            {
                "name": "UBotROS",
                "friendlyName": "uBot ROS",
                "description": { "en": "ROS adapter", "zh-CN": "ROS 适配" },
                "layer": "Adapter",
                "repository": "https://example.com/ros.git",
                "folder": "uBotROS",
                "docsUrl": "https://example.com/ros",
                "tags": [ "ros" ],
                "versions": [
                    {
                        "version": "0.2.0",
                        "ref": "v0.2.0",
                        "engine": "~5.5",
                        "requires": [ { "name": "UBotCore", "version": "^0.1.0" }, { "name": "UBotSensor", "version": "^0.1.0", "optional": true } ],
                        "enginePlugins": [ "Sockets" ],
                        "provides": [ "ROS.TF" ],
                        "binaries": null
                    }
                ]
            }
        ],
        "recipes": [ { "id": "ros", "name": { "en": "ROS" }, "packages": [ "UBotROS" ], "checks": [ "rosBridge", "unknownCheck" ] } ],
        "replacements": [ { "legacy": "AgentSensorCore", "replacement": "UBotSensor", "redirects": "Config/DefaultUBotSensor.ini" } ]
    })json"), Packages, &Error, &Replacements, TEXT("en"));

    TestTrue(FString::Printf(TEXT("Valid index parses (%s)"), *Error), bParsed);
    TestTrue(TEXT("Success clears the error"), Error.IsEmpty());
    if (TestEqual(TEXT("Two entries"), Packages.Num(), 2))
    {
        const FUBotPackageInfo& Core = Packages[0];
        TestEqual(TEXT("Core name"), Core.Name, FString(TEXT("UBotCore")));
        TestEqual(TEXT("FriendlyName falls back to the name"), Core.FriendlyName, FString(TEXT("UBotCore")));
        TestTrue(TEXT("No description"), Core.Description.IsEmpty());
        TestEqual(TEXT("Core layer"), Core.Layer, EUBotPackageLayer::Foundation);
        TestEqual(TEXT("Core version"), Core.Version, FString(TEXT("0.1.0")));
        TestEqual(TEXT("Core available versions"), FString::Join(Core.AvailableVersions, TEXT(",")), FString(TEXT("0.1.0")));
        if (TestEqual(TEXT("Core index versions"), Core.IndexVersions.Num(), 1))
        {
            TestEqual(TEXT("Core version entry"), Core.IndexVersions[0].Version, FString(TEXT("0.1.0")));
            TestEqual(TEXT("Core git ref"), Core.IndexVersions[0].Ref, FString(TEXT("v0.1.0")));
            TestFalse(TEXT("Core version is installable"), Core.IndexVersions[0].bPlanned);
        }
        TestTrue(TEXT("No folder means the repository name is used"), Core.InstallFolder.IsEmpty());
        TestFalse(TEXT("Core is not planned"), Core.bPlanned);
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
        TestEqual(TEXT("ROS install folder"), Ros.InstallFolder, FString(TEXT("uBotROS")));
        TestTrue(TEXT("ROS git ref"), Ros.IndexVersions.Num() == 1 && Ros.IndexVersions[0].Ref == TEXT("v0.2.0"));
        TestEqual(TEXT("ROS provides"), FString::Join(Ros.Provides, TEXT(",")), FString(TEXT("ROS.TF")));
        TestEqual(TEXT("enginePlugins become ExternalRequires"), FString::Join(Ros.ExternalRequires, TEXT(",")), FString(TEXT("Sockets")));
        if (TestEqual(TEXT("ROS requirements"), Ros.Requires.Num(), 2))
        {
            TestEqual(TEXT("First requirement"), Ros.Requires[0].Name, FString(TEXT("UBotCore")));
            TestEqual(TEXT("First requirement constraint"), Ros.Requires[0].Version, FString(TEXT("^0.1.0")));
            TestFalse(TEXT("First requirement is required"), Ros.Requires[0].bOptional);
            TestTrue(TEXT("Second requirement is optional"), Ros.Requires[1].bOptional);
        }
    }
    if (TestEqual(TEXT("One replacement"), Replacements.Num(), 1))
    {
        TestEqual(TEXT("Legacy plugin"), Replacements[0].Legacy, FString(TEXT("AgentSensorCore")));
        TestEqual(TEXT("Replacement package"), Replacements[0].Replacement, FString(TEXT("UBotSensor")));
        TestEqual(TEXT("Redirects"), Replacements[0].Redirects, FString(TEXT("Config/DefaultUBotSensor.ini")));
    }

    // uBot Manager writes the merged index (Saved/uBot/index.json) with absent values as null.
    {
        const bool bMergedParsed = FUBotPackageIndex::ParseIndexJson(TEXT(R"json({
            "formatVersion": 2,
            "name": "merged",
            "packages": [
                {
                    "name": "UBotCore", "friendlyName": "uBot Core", "description": { "en": "Core", "zh-CN": null }, "layer": "Foundation",
                    "repository": "https://example.com/core.git", "folder": null, "docsUrl": null, "tags": [],
                    "versions": [ { "version": "0.1.0", "ref": "v0.1.0", "planned": false, "engine": null, "requires": [], "enginePlugins": [], "provides": [], "binaries": null } ]
                },
                {
                    "name": "UBotLearning", "friendlyName": null, "description": null, "layer": null, "repository": null, "tags": null,
                    "versions": [ { "version": "0.1.0", "ref": null, "planned": true, "engine": null,
                                    "requires": [ { "name": "UBotCore", "version": "^0.1.0", "optional": null } ], "enginePlugins": null, "provides": null } ]
                }
            ],
            "recipes": [],
            "replacements": [ { "legacy": "AgentSensorCore", "replacement": "UBotCore", "redirects": null } ]
        })json"), Packages, &Error, &Replacements, TEXT("zh-Hans"));
        TestTrue(FString::Printf(TEXT("Nulls count as absent values (%s)"), *Error), bMergedParsed);
        if (TestEqual(TEXT("Both entries of the merged index"), Packages.Num(), 2))
        {
            TestEqual(TEXT("Missing zh-CN text falls back to en"), Packages[0].Description, FString(TEXT("Core")));
            TestTrue(TEXT("Null-filled planned entry"), Packages[1].bPlanned && Packages[1].Requires.Num() == 1 && !Packages[1].Requires[0].bOptional);
            TestEqual(TEXT("Null friendly name falls back to the name"), Packages[1].FriendlyName, FString(TEXT("UBotLearning")));
            TestTrue(TEXT("Null folder means no folder"), Packages[0].InstallFolder.IsEmpty());
            TestTrue(TEXT("A planned version has no ref"), Packages[1].IndexVersions.Num() == 1 && Packages[1].IndexVersions[0].bPlanned && Packages[1].IndexVersions[0].Ref.IsEmpty());
        }
        TestTrue(TEXT("Null redirects"), Replacements.Num() == 1 && Replacements[0].Redirects.IsEmpty());
    }

    TestTrue(TEXT("Empty package list parses"), FUBotPackageIndex::ParseIndexJson(TEXT(R"json({ "formatVersion": 2, "packages": [] })json"), Packages));
    TestEqual(TEXT("Empty index"), Packages.Num(), 0);

    TestFalse(TEXT("Invalid JSON fails"), FUBotPackageIndex::ParseIndexJson(TEXT("not json"), Packages, &Error));
    TestFalse(TEXT("Invalid JSON is explained"), Error.IsEmpty());
    TestEqual(TEXT("Invalid JSON yields nothing"), Packages.Num(), 0);

    TestFalse(TEXT("Top-level array fails"), FUBotPackageIndex::ParseIndexJson(TEXT("[]"), Packages, &Error));

    TestFalse(TEXT("Missing formatVersion fails"), FUBotPackageIndex::ParseIndexJson(TEXT(R"json({ "packages": [] })json"), Packages, &Error));
    TestTrue(TEXT("Missing formatVersion is explained"), Error.Contains(TEXT("formatVersion")));

    TestFalse(TEXT("Unknown formatVersion fails"), FUBotPackageIndex::ParseIndexJson(TEXT(R"json({ "formatVersion": 3, "packages": [] })json"), Packages, &Error));
    TestTrue(TEXT("Unknown formatVersion is named"), Error.Contains(TEXT("'3'")));

    TestFalse(TEXT("Missing packages fails"), FUBotPackageIndex::ParseIndexJson(TEXT(R"json({ "formatVersion": 2 })json"), Packages, &Error));
    TestTrue(TEXT("Missing packages is explained"), Error.Contains(TEXT("packages")));

    const bool bPartial = FUBotPackageIndex::ParseIndexJson(TEXT(R"json({
        "formatVersion": 2,
        "packages": [
            { "name": "A", "repository": "https://example.com/a.git", "versions": [ { "version": "1.0.0", "ref": "v1.0.0" } ] },
            { "description": { "en": "no name" }, "versions": [] },
            42,
            { "name": "a", "repository": "https://example.com/a2.git", "versions": [ { "version": "2.0.0", "ref": "v2.0.0" } ] },
            { "name": "B", "layer": "Sideways", "repository": "https://example.com/b.git",
              "versions": [ { "version": "one", "ref": "x" }, { "version": "1.0.0" }, { "version": "1.0.0", "ref": "again" } ] },
            { "name": "C", "repository": "https://example.com/c.git", "versions": [] },
            { "name": "D", "versions": [ { "version": "0.1.0", "ref": "v0.1.0", "engine": "five" } ] }
        ],
        "replacements": [ { "legacy": "Old" }, { "legacy": "Old2", "replacement": "B" }, { "legacy": "old2", "replacement": "A" } ]
    })json"), Packages, &Error, &Replacements, TEXT("en"));
    TestFalse(TEXT("Broken entries make the parse fail"), bPartial);
    TestEqual(TEXT("Valid entries are still returned"), Packages.Num(), 3);
    TestTrue(TEXT("Nameless entry is reported"), Error.Contains(TEXT("packages[1]")));
    TestTrue(TEXT("Non-object entry is reported"), Error.Contains(TEXT("packages[2]")));
    TestTrue(TEXT("Duplicate entry is reported"), Error.Contains(TEXT("packages[3]")));
    TestTrue(TEXT("Bad layer is reported"), Error.Contains(TEXT("Sideways")));
    TestTrue(TEXT("Bad version is reported"), Error.Contains(TEXT("'one'")));
    TestTrue(TEXT("Missing ref is reported"), Error.Contains(TEXT("no \"ref\"")));
    TestTrue(TEXT("Duplicate version is reported"), Error.Contains(TEXT("duplicates version 1.0.0")));
    TestTrue(TEXT("Entry without versions is reported"), Error.Contains(TEXT("packages[5]")));
    TestTrue(TEXT("Missing repository is reported"), Error.Contains(TEXT("no \"repository\"")));
    TestTrue(TEXT("Bad engine constraint is reported"), Error.Contains(TEXT("five")));
    TestTrue(TEXT("Incomplete replacement is reported"), Error.Contains(TEXT("replacements[0]")));
    TestTrue(TEXT("Duplicate replacement is reported"), Error.Contains(TEXT("replacements[2]")));
    if (Packages.Num() == 3)
    {
        TestEqual(TEXT("First of the duplicates wins"), Packages[0].Version, FString(TEXT("1.0.0")));
        TestEqual(TEXT("Entry with a bad layer is kept as Unknown"), Packages[1].Layer, EUBotPackageLayer::Unknown);
        TestEqual(TEXT("Valid versions of a partly broken entry remain"), FString::Join(Packages[1].AvailableVersions, TEXT(",")), FString(TEXT("1.0.0")));
    }
    TestEqual(TEXT("Valid replacements are kept"), Replacements.Num(), 1);

    // An unsafe "folder" or "ref" is reported but kept as written, so that installing can refuse it
    // instead of silently falling back to another folder or ref.
    {
        TArray<FUBotPackageInfo> Unsafe;
        FString UnsafeError;
        const bool bUnsafeParsed = FUBotPackageIndex::ParseIndexJson(TEXT(R"json({
            "formatVersion": 2,
            "packages": [
                { "name": "Escape", "repository": "https://example.com/e.git", "folder": "../Escape",
                  "versions": [ { "version": "1.0.0", "ref": "--upload-pack=calc" } ] },
                { "name": "Tidy", "repository": "https://example.com/t.git", "folder": "Tidy_2.x-b",
                  "versions": [ { "version": "1.0.0", "ref": "release/1.0" }, { "version": "2.0.0", "planned": true, "ref": "ignored for planned" } ] }
            ]
        })json"), Unsafe, &UnsafeError, nullptr, TEXT("en"));
        TestFalse(TEXT("Unsafe folder and ref make the parse fail"), bUnsafeParsed);
        TestTrue(TEXT("The unsafe folder is reported"), UnsafeError.Contains(TEXT("\"folder\" '../Escape'")));
        TestTrue(TEXT("The unsafe ref is reported"), UnsafeError.Contains(TEXT("\"ref\" '--upload-pack=calc' is not a safe git ref")));
        TestFalse(TEXT("Safe values are not reported"), UnsafeError.Contains(TEXT("Tidy")));
        if (TestEqual(TEXT("Both entries are kept"), Unsafe.Num(), 2))
        {
            TestEqual(TEXT("The folder is kept as written"), Unsafe[0].InstallFolder, FString(TEXT("../Escape")));
            TestTrue(TEXT("The ref is kept as written"), Unsafe[0].IndexVersions.Num() == 1 && Unsafe[0].IndexVersions[0].Ref == TEXT("--upload-pack=calc"));
            TestEqual(TEXT("A safe folder is kept"), Unsafe[1].InstallFolder, FString(TEXT("Tidy_2.x-b")));
            if (TestEqual(TEXT("Both versions are listed"), Unsafe[1].IndexVersions.Num(), 2))
            {
                TestEqual(TEXT("Newest first, the planned one"), Unsafe[1].IndexVersions[0].Version, FString(TEXT("2.0.0")));
                TestTrue(TEXT("A planned version ignores its ref"), Unsafe[1].IndexVersions[0].bPlanned && Unsafe[1].IndexVersions[0].Ref.IsEmpty());
                TestEqual(TEXT("A branch ref is allowed"), Unsafe[1].IndexVersions[1].Ref, FString(TEXT("release/1.0")));
            }
        }

        const TCHAR* SafeRefs[] = { TEXT("v0.1.0"), TEXT("release/1.0"), TEXT("0123abc"), TEXT("feature_x-2"), TEXT("v1.0.0-rc.1+build") };
        for (const TCHAR* Ref : SafeRefs)
        {
            TestTrue(*FString::Printf(TEXT("'%s' is a safe ref"), Ref), FUBotPackageIndex::IsSafeGitRef(Ref));
        }
        const TCHAR* UnsafeRefs[] = { TEXT(""), TEXT("-x"), TEXT("--upload-pack=calc"), TEXT("a b"), TEXT("a\tb"), TEXT("a..b"), TEXT("a~1"), TEXT("a^"), TEXT("a:b"), TEXT("a?"), TEXT("a*"), TEXT("a["), TEXT("a\\b"), TEXT("a\"b") };
        for (const TCHAR* Ref : UnsafeRefs)
        {
            TestFalse(*FString::Printf(TEXT("'%s' is not a safe ref"), Ref), FUBotPackageIndex::IsSafeGitRef(Ref));
        }

        const TCHAR* SafeFolders[] = { TEXT("uBotSensor"), TEXT("uBot-Sensor_2.x"), TEXT(".hidden"), TEXT("a..b") };
        for (const TCHAR* Folder : SafeFolders)
        {
            TestTrue(*FString::Printf(TEXT("'%s' is a safe folder"), Folder), FUBotPackageIndex::IsSafeFolderName(Folder));
        }
        const TCHAR* UnsafeFolders[] = { TEXT(""), TEXT("."), TEXT(".."), TEXT("../x"), TEXT("a/b"), TEXT("a\\b"), TEXT("C:"), TEXT("a b"), TEXT("a*"), TEXT("a\"b"), TEXT("a\tb") };
        for (const TCHAR* Folder : UnsafeFolders)
        {
            TestFalse(*FString::Printf(TEXT("'%s' is not a safe folder"), Folder), FUBotPackageIndex::IsSafeFolderName(Folder));
        }
    }

    // "Composition" was never a layer of the contract (SPEC, schema and the manager know four).
    {
        TArray<FUBotPackageInfo> Layered;
        FString LayerError;
        TestFalse(TEXT("Composition is not a layer"), FUBotPackageIndex::ParseIndexJson(TEXT(R"json({ "formatVersion": 2, "packages": [
            { "name": "C", "layer": "Composition", "repository": "https://example.com/c.git", "versions": [ { "version": "1.0.0", "ref": "v1.0.0" } ] } ] })json"),
            Layered, &LayerError, nullptr, TEXT("en")));
        TestTrue(TEXT("The unknown layer is named"), LayerError.Contains(TEXT("Unknown layer 'Composition'")));
        TestFalse(TEXT("The expected layers do not list Composition"), LayerError.Contains(TEXT("(expected Foundation, Capability, Composition")));
        TestTrue(TEXT("The expected layers are the four of the contract"), LayerError.Contains(TEXT("(expected Foundation, Capability, Adapter or Content)")));
        TestTrue(TEXT("The entry is kept with an unknown layer"), Layered.Num() == 1 && Layered[0].Layer == EUBotPackageLayer::Unknown);
    }

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotPackageIndexFormatOneTest, "UBotCore.Packages.IndexRejectsFormatOne", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotPackageIndexFormatOneTest::RunTest(const FString& Parameters)
{
    TArray<FUBotPackageInfo> Packages;
    TArray<FUBotPackageReplacement> Replacements;
    Replacements.AddDefaulted();
    FString Error;

    // The layout of the former Resources/PackageIndex.json.
    const bool bParsed = FUBotPackageIndex::ParseIndexJson(TEXT(R"json({
        "FormatVersion": 1,
        "Packages": [ { "Name": "UBotCore", "Version": "0.1.0", "Repository": "https://example.com/core.git" } ]
    })json"), Packages, &Error, &Replacements);

    TestFalse(TEXT("Format 1 is rejected"), bParsed);
    TestTrue(FString::Printf(TEXT("Format 1 gets a clear message (%s)"), *Error), Error.Contains(TEXT("format 1 is no longer supported")));
    TestTrue(TEXT("The message names the expected format"), Error.Contains(TEXT("\"formatVersion\": 2")));
    TestEqual(TEXT("Nothing is returned"), Packages.Num(), 0);
    TestEqual(TEXT("Replacements are cleared"), Replacements.Num(), 0);

    TestFalse(TEXT("camelCase format 1 is rejected too"), FUBotPackageIndex::ParseIndexJson(TEXT(R"json({ "formatVersion": 1, "packages": [] })json"), Packages, &Error));
    TestTrue(TEXT("camelCase format 1 gets the same message"), Error.Contains(TEXT("format 1 is no longer supported")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotPackageIndexVersionSelectionTest, "UBotCore.Packages.IndexVersionSelection", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotPackageIndexVersionSelectionTest::RunTest(const FString& Parameters)
{
    using namespace UBotPackageTests;

    TArray<FUBotPackageInfo> Packages;
    FString Error;
    const bool bParsed = FUBotPackageIndex::ParseIndexJson(TEXT(R"json({
        "formatVersion": 2,
        "packages": [
            {
                "name": "Mixed",
                "repository": "https://example.com/mixed.git",
                "versions": [
                    { "version": "0.2.0", "ref": "v0.2.0", "requires": [ { "name": "UBotCore", "version": "^0.2.0" } ], "provides": [ "Old" ] },
                    { "version": "0.10.0", "ref": "v0.10.0", "requires": [ { "name": "UBotCore", "version": "^0.3.0" } ], "enginePlugins": [ "ChaosVehiclesPlugin" ], "provides": [ "New" ] },
                    { "version": "1.0.0", "planned": true, "requires": [ { "name": "UBotCore", "version": "^1.0.0" } ], "provides": [ "Future" ] },
                    { "version": "0.10.0-rc.1", "ref": "v0.10.0-rc.1" },
                    { "version": "0.9.1", "ref": "v0.9.1" }
                ]
            },
            {
                "name": "Announced",
                "versions": [
                    { "version": "0.1.0", "planned": true, "enginePlugins": [ "LearningAgents" ], "provides": [ "First" ] },
                    { "version": "0.2.0", "planned": true, "requires": [ { "name": "UBotCore", "version": "^0.2.0" } ], "provides": [ "Second" ] }
                ]
            }
        ]
    })json"), Packages, &Error, nullptr, TEXT("en"));

    TestTrue(FString::Printf(TEXT("Index parses (%s)"), *Error), bParsed);

    const FUBotPackageInfo* Mixed = FindByName(Packages, TEXT("Mixed"));
    if (TestNotNull(TEXT("Mixed package"), Mixed))
    {
        TestEqual(TEXT("Latest is the highest non-planned version by SemVer precedence"), Mixed->Version, FString(TEXT("0.10.0")));
        TestEqual(TEXT("Available versions newest first, planned ones left out"), FString::Join(Mixed->AvailableVersions, TEXT(",")),
            FString(TEXT("0.10.0,0.10.0-rc.1,0.9.1,0.2.0")));
        TestFalse(TEXT("A package with an installable version is not planned"), Mixed->bPlanned);
        // Every version with the ref the installer clones, planned ones included.
        TArray<FString> Described;
        for (const FUBotPackageIndexVersion& IndexVersion : Mixed->IndexVersions)
        {
            Described.Add(FString::Printf(TEXT("%s=%s%s"), *IndexVersion.Version, *IndexVersion.Ref, IndexVersion.bPlanned ? TEXT("(planned)") : TEXT("")));
        }
        TestEqual(TEXT("Index versions newest first with their refs"), FString::Join(Described, TEXT(",")),
            FString(TEXT("1.0.0=(planned),0.10.0=v0.10.0,0.10.0-rc.1=v0.10.0-rc.1,0.9.1=v0.9.1,0.2.0=v0.2.0")));
        TestEqual(TEXT("Provides come from the latest version"), FString::Join(Mixed->Provides, TEXT(",")), FString(TEXT("New")));
        TestEqual(TEXT("Engine plugins come from the latest version"), FString::Join(Mixed->ExternalRequires, TEXT(",")), FString(TEXT("ChaosVehiclesPlugin")));
        TestTrue(TEXT("Requirements come from the latest version"), Mixed->Requires.Num() == 1 && Mixed->Requires[0].Version == TEXT("^0.3.0"));
    }

    const FUBotPackageInfo* Announced = FindByName(Packages, TEXT("Announced"));
    if (TestNotNull(TEXT("Planned package"), Announced))
    {
        TestTrue(TEXT("Only planned versions make a planned package"), Announced->bPlanned);
        TestEqual(TEXT("A planned package has no installable versions"), Announced->AvailableVersions.Num(), 0);
        TestTrue(TEXT("All of its versions are listed, as planned"),
            Announced->IndexVersions.Num() == 2 && Announced->IndexVersions[0].bPlanned && Announced->IndexVersions[1].bPlanned);
        TestEqual(TEXT("Version of a planned package is the newest planned one"), Announced->Version, FString(TEXT("0.2.0")));
        TestEqual(TEXT("Provides come from the newest planned version"), FString::Join(Announced->Provides, TEXT(",")), FString(TEXT("Second")));
        TestTrue(TEXT("Requirements come from the newest planned version"), Announced->Requires.Num() == 1 && Announced->Requires[0].Name == TEXT("UBotCore"));
        TestEqual(TEXT("A planned package needs no repository"), Announced->Repository, FString());
    }

    // Merging keeps the installed data but takes what the index knows about versions.
    TArray<FUBotPackageInfo> Installed;
    Installed.Add(MakePackage(TEXT("mixed"), TEXT("0.2.0"), EUBotPackageState::Enabled));
    Installed.Add(MakePackage(TEXT("Announced"), TEXT("0.0.1"), EUBotPackageState::Disabled));
    const TArray<FUBotPackageInfo> Merged = FUBotPackageIndex::MergeInstalledWithIndex(Installed, Packages);
    if (TestEqual(TEXT("Nothing appended"), Merged.Num(), 2))
    {
        TestEqual(TEXT("Installed version is kept"), Merged[0].Version, FString(TEXT("0.2.0")));
        TestEqual(TEXT("Available versions come from the index"), Merged[0].AvailableVersions.Num(), 4);
        TestEqual(TEXT("Index versions come from the index"), Merged[0].IndexVersions.Num(), 5);
        TestTrue(TEXT("A locally built planned package is marked planned"), Merged[1].bPlanned);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotPackageIndexLocalizedTextTest, "UBotCore.Packages.IndexLocalizedDescription", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotPackageIndexLocalizedTextTest::RunTest(const FString& Parameters)
{
    using namespace UBotPackageTests;

    const TSharedPtr<FJsonObject> Both = ParseJsonObject(TEXT(R"json({ "en": " Sensors ", "zh-CN": " 传感器 " })json"));
    const TSharedPtr<FJsonObject> EnglishOnly = ParseJsonObject(TEXT(R"json({ "en": "Sensors", "zh-CN": "" })json"));
    const TSharedPtr<FJsonObject> ChineseOnly = ParseJsonObject(TEXT(R"json({ "zh-CN": "传感器" })json"));
    if (!TestTrue(TEXT("Test JSON parses"), Both.IsValid() && EnglishOnly.IsValid() && ChineseOnly.IsValid()))
    {
        return false;
    }

    struct FCase
    {
        const TCHAR* Culture;
        const TCHAR* Expected;
    };
    const FCase Cases[] =
    {
        { TEXT("en"), TEXT("Sensors") },
        { TEXT("en-US"), TEXT("Sensors") },
        { TEXT("de"), TEXT("Sensors") },
        { TEXT("zh"), TEXT("传感器") },
        { TEXT("zh-Hans"), TEXT("传感器") },
        { TEXT("zh-Hans-CN"), TEXT("传感器") },
        { TEXT("zh-CN"), TEXT("传感器") },
        { TEXT("ZH-TW"), TEXT("传感器") },
        { TEXT("zu"), TEXT("Sensors") },
        { TEXT(""), TEXT("Sensors") },
    };
    for (const FCase& Case : Cases)
    {
        TestEqual(FString::Printf(TEXT("Culture '%s'"), Case.Culture), FUBotPackageIndex::SelectLocalizedText(*Both, Case.Culture), FString(Case.Expected));
    }
    TestEqual(TEXT("Empty zh-CN text falls back to English"), FUBotPackageIndex::SelectLocalizedText(*EnglishOnly, TEXT("zh-Hans")), FString(TEXT("Sensors")));
    TestEqual(TEXT("Missing English falls back to any text"), FUBotPackageIndex::SelectLocalizedText(*ChineseOnly, TEXT("en")), FString(TEXT("传感器")));

    const FString Index = TEXT(R"json({ "formatVersion": 2, "packages": [ { "name": "UBotSensor", "repository": "https://example.com/s.git",
        "description": { "en": "Sensors", "zh-CN": "传感器" }, "versions": [ { "version": "0.1.0", "ref": "v0.1.0" } ] } ] })json");
    TArray<FUBotPackageInfo> Packages;
    TestTrue(TEXT("Chinese editor parses"), FUBotPackageIndex::ParseIndexJson(Index, Packages, nullptr, nullptr, TEXT("zh-Hans")));
    TestTrue(TEXT("Chinese editor gets the zh-CN description"), Packages.Num() == 1 && Packages[0].Description == TEXT("传感器"));
    TestTrue(TEXT("English editor parses"), FUBotPackageIndex::ParseIndexJson(Index, Packages, nullptr, nullptr, TEXT("en")));
    TestTrue(TEXT("English editor gets the en description"), Packages.Num() == 1 && Packages[0].Description == TEXT("Sensors"));

    FString Error;
    TestFalse(TEXT("A description without English is reported"), FUBotPackageIndex::ParseIndexJson(TEXT(R"json({ "formatVersion": 2, "packages": [
        { "name": "X", "repository": "https://example.com/x.git", "description": { "zh-CN": "只有中文" }, "versions": [ { "version": "1.0.0", "ref": "v1" } ] } ] })json"),
        Packages, &Error, nullptr, TEXT("en")));
    TestTrue(TEXT("Missing en text is named"), Error.Contains(TEXT("\"en\"")));
    TestTrue(TEXT("The entry is still used"), Packages.Num() == 1 && Packages[0].Description == TEXT("只有中文"));
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
    IndexCore.AvailableVersions = { TEXT("0.2.0"), TEXT("0.1.0") };
    IndexCore.Description = TEXT("Index description");
    IndexCore.Repository = TEXT("https://index/core.git");
    IndexCore.DocsUrl = TEXT("https://index/docs");
    IndexCore.InstallFolder = TEXT("uBotCore");
    FUBotPackageIndexVersion& IndexCoreVersion = IndexCore.IndexVersions.AddDefaulted_GetRef();
    IndexCoreVersion.Version = TEXT("0.2.0");
    IndexCoreVersion.Ref = TEXT("v0.2.0");
    IndexCore.bFromIndex = true;

    FUBotPackageInfo& IndexSensor = Index.AddDefaulted_GetRef();
    IndexSensor.Name = TEXT("UBotSensor");
    IndexSensor.Version = TEXT("0.1.0");
    IndexSensor.Repository = TEXT("https://index/sensor.git");
    IndexSensor.Problems.Add(TEXT("should not survive"));
    IndexSensor.ProblemDetails.AddDefaulted();
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
    TestEqual(TEXT("Index versions are attached"), FString::Join(Core.AvailableVersions, TEXT(",")), FString(TEXT("0.2.0,0.1.0")));
    TestTrue(TEXT("The refs of the index versions are attached"), Core.IndexVersions.Num() == 1 && Core.IndexVersions[0].Ref == TEXT("v0.2.0"));
    TestEqual(TEXT("Empty install folder is filled from the index"), Core.InstallFolder, FString(TEXT("uBotCore")));
    TestEqual(TEXT("Installed description wins"), Core.Description, FString(TEXT("Installed description")));
    TestEqual(TEXT("Empty repository is filled from the index"), Core.Repository, FString(TEXT("https://index/core.git")));
    TestEqual(TEXT("Existing docs URL is kept"), Core.DocsUrl, FString(TEXT("https://installed/docs")));
    TestEqual(TEXT("Installed state is kept"), Core.State, EUBotPackageState::Enabled);
    TestFalse(TEXT("Installed base dir is kept"), Core.BaseDir.IsEmpty());

    const FUBotPackageInfo& LocalMerged = Merged[1];
    TestEqual(TEXT("Installed-only entry stays"), LocalMerged.Name, FString(TEXT("MyLocalPackage")));
    TestFalse(TEXT("Installed-only entry is not from the index"), LocalMerged.bFromIndex);
    TestEqual(TEXT("Installed-only entry has no index versions"), LocalMerged.AvailableVersions.Num(), 0);

    const FUBotPackageInfo& Sensor = Merged[2];
    TestEqual(TEXT("Index-only entry is appended"), Sensor.Name, FString(TEXT("UBotSensor")));
    TestEqual(TEXT("Index-only entry is not installed"), Sensor.State, EUBotPackageState::NotInstalled);
    TestFalse(TEXT("Index-only entry has bInstalled cleared"), Sensor.bInstalled);
    TestFalse(TEXT("Index-only entry has bEnabled cleared"), Sensor.bEnabled);
    TestTrue(TEXT("Index-only entry is from the index"), Sensor.bFromIndex);
    TestEqual(TEXT("Index-only entry keeps its requirements"), Sensor.Requires.Num(), 1);
    TestEqual(TEXT("Index-only entry has no problems"), Sensor.Problems.Num(), 0);
    TestEqual(TEXT("Index-only entry has no problem details"), Sensor.ProblemDetails.Num(), 0);

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

    TestFalse(TEXT("The format 1 index is gone"), FPaths::FileExists(FPaths::Combine(CorePlugin->GetBaseDir(), TEXT("Resources"), TEXT("PackageIndex.json"))));

    const FString IndexPath = FPaths::Combine(CorePlugin->GetBaseDir(), TEXT("Index"), TEXT("index.json"));
    TArray<FUBotPackageInfo> Index;
    TArray<FUBotPackageReplacement> Replacements;
    FString Error;
    const bool bLoaded = FUBotPackageIndex::LoadIndexFile(IndexPath, Index, &Error, &Replacements);
    if (!TestTrue(FString::Printf(TEXT("UBotCore's Index/index.json loads cleanly (%s)"), *Error), bLoaded))
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
    for (const FUBotPackageInfo* Package : { Core, Sensor, Ros })
    {
        TestFalse(FString::Printf(TEXT("%s is installable"), *Package->Name), Package->bPlanned || Package->AvailableVersions.IsEmpty());
        TestFalse(FString::Printf(TEXT("%s has a description"), *Package->Name), Package->Description.IsEmpty());
    }

    for (const FUBotPackageInfo& Package : Index)
    {
        TestEqual(FString::Printf(TEXT("%s: planned exactly when no version is installable"), *Package.Name), Package.bPlanned, Package.AvailableVersions.IsEmpty());
        TestFalse(FString::Printf(TEXT("%s has a version"), *Package.Name), Package.Version.IsEmpty());

        // What the installer clones: every installable version has a safe ref, planned ones have none.
        TestEqual(FString::Printf(TEXT("%s lists all its versions"), *Package.Name), Package.IndexVersions.Num(),
            Package.AvailableVersions.Num() + Package.IndexVersions.FilterByPredicate([](const FUBotPackageIndexVersion& IndexVersion) { return IndexVersion.bPlanned; }).Num());
        for (const FUBotPackageIndexVersion& IndexVersion : Package.IndexVersions)
        {
            TestEqual(FString::Printf(TEXT("%s %s has a ref exactly when it is installable"), *Package.Name, *IndexVersion.Version), !IndexVersion.Ref.IsEmpty(), !IndexVersion.bPlanned);
            TestTrue(FString::Printf(TEXT("%s %s has a safe ref"), *Package.Name, *IndexVersion.Version), IndexVersion.Ref.IsEmpty() || FUBotPackageIndex::IsSafeGitRef(IndexVersion.Ref));
        }
        TestTrue(FString::Printf(TEXT("%s has a safe install folder"), *Package.Name), Package.InstallFolder.IsEmpty() || FUBotPackageIndex::IsSafeFolderName(Package.InstallFolder));
    }

    // Installed packages must agree with the index entry for their own version: the index is what
    // uBot Manager plans with before anything is cloned.
    {
        FUBotPackageRegistry Registry;
        Registry.Refresh();
        for (const FUBotPackageInfo& Installed : Registry.GetInstalledPackages())
        {
            const FUBotPackageInfo* Entry = FindByName(Index, *Installed.Name);
            if (Entry == nullptr || !Installed.Name.StartsWith(TEXT("UBot")))
            {
                continue;
            }
            TestTrue(FString::Printf(TEXT("%s %s is listed in the index"), *Installed.Name, *Installed.Version), Entry->AvailableVersions.Contains(Installed.Version));
            if (Entry->Version != Installed.Version)
            {
                continue;
            }
            TestEqual(FString::Printf(TEXT("%s provides match the descriptor"), *Installed.Name),
                FString::Join(Entry->Provides, TEXT(",")), FString::Join(Installed.Provides, TEXT(",")));
            TestEqual(FString::Printf(TEXT("%s tags match the descriptor"), *Installed.Name),
                FString::Join(Entry->Tags, TEXT(",")), FString::Join(Installed.Tags, TEXT(",")));
            TestEqual(FString::Printf(TEXT("%s layer matches the descriptor"), *Installed.Name), Entry->Layer, Installed.Layer);
            TestEqual(FString::Printf(TEXT("%s engine plugins match the descriptor"), *Installed.Name),
                FString::Join(Entry->ExternalRequires, TEXT(",")), FString::Join(Installed.ExternalRequires, TEXT(",")));
            if (TestEqual(FString::Printf(TEXT("%s has the descriptor's requirements"), *Installed.Name), Entry->Requires.Num(), Installed.Requires.Num()))
            {
                for (int32 RequirementIndex = 0; RequirementIndex < Entry->Requires.Num(); ++RequirementIndex)
                {
                    const FUBotPackageDependency& FromIndex = Entry->Requires[RequirementIndex];
                    const FUBotPackageDependency& FromDescriptor = Installed.Requires[RequirementIndex];
                    TestTrue(FString::Printf(TEXT("%s requirement %d matches the descriptor"), *Installed.Name, RequirementIndex),
                        FromIndex.Name == FromDescriptor.Name && FromIndex.Version == FromDescriptor.Version && FromIndex.bOptional == FromDescriptor.bOptional);
                }
            }
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

    for (const FUBotPackageReplacement& Replacement : Replacements)
    {
        TestNotNull(FString::Printf(TEXT("Replacement %s of %s is in the index"), *Replacement.Replacement, *Replacement.Legacy), FindByName(Index, *Replacement.Replacement));
    }

    TArray<FString> ConfigErrors;
    const TArray<FString> Files = FUBotPackageIndex::GetConfiguredIndexFiles(&ConfigErrors);
    if (TestTrue(TEXT("At least one index file is configured"), Files.Num() > 0))
    {
        const FString MergedIndex = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("uBot"), TEXT("index.json")));
        const FString ExpectedFirst = FPaths::FileExists(MergedIndex) ? MergedIndex : FPaths::ConvertRelativePathToFull(IndexPath);
        TestTrue(TEXT("The manager's merged index, else UBotCore's own index, comes first"), FPaths::IsSamePath(Files[0], ExpectedFirst));
    }
    const TArray<FUBotPackageInfo> Configured = FUBotPackageIndex::LoadConfiguredIndex();
    TestNotNull(TEXT("Configured index includes UBotCore"), FindByName(Configured, TEXT("UBotCore")));
    TestNotNull(TEXT("Configured index includes UBotROS"), FindByName(Configured, TEXT("UBotROS")));

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
