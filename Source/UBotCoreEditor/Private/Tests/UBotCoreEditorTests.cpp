#include "CoreMinimal.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformFileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Internationalization/Internationalization.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Styling/SlateStyleRegistry.h"
#include "ToolMenus.h"
#include "UBotCoreEditor.h"
#include "UBotCoreSettings.h"
#include "UBotEditorStyle.h"
#include "UBotEditorText.h"
#include "UBotManagerLauncher.h"
#include "UBotPackageCommandlet.h"
#include "UBotPackageIndex.h"
#include "UBotPackageRegistry.h"
#include "UBotPackageService.h"
#include "UBotPackageTypes.h"
#include "UBotPanelModel.h"
#include "UBotRuntimeStatus.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace UBotCoreEditorTestsPrivate
{
    // Scratch folder below Saved/Automation that is removed again when the scope ends.
    class FScopedScratchDirectory
    {
    public:
        UE_NONCOPYABLE(FScopedScratchDirectory);

        FScopedScratchDirectory()
            : Path(FPaths::ConvertRelativePathToFull(
                FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("UBotPackageManagerTests"), FGuid::NewGuid().ToString(EGuidFormats::Digits))))
        {
            IFileManager::Get().MakeDirectory(*Path, true);
        }

        ~FScopedScratchDirectory()
        {
            IFileManager::Get().DeleteDirectory(*Path, false, true);
        }

        FString GetFilePath(const TCHAR* RelativePath) const
        {
            return FPaths::Combine(Path, RelativePath);
        }

        // Creates a file (and its folders) below the scratch directory.
        bool WriteFile(const TCHAR* RelativePath, bool bReadOnly = false) const
        {
            const FString FilePath = GetFilePath(RelativePath);
            if (!FFileHelper::SaveStringToFile(TEXT("x"), *FilePath))
            {
                return false;
            }
            if (bReadOnly)
            {
                FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*FilePath, true);
            }
            return true;
        }

        bool DirectoryExists(const TCHAR* RelativePath) const
        {
            return IFileManager::Get().DirectoryExists(*GetFilePath(RelativePath));
        }

        bool FileExists(const TCHAR* RelativePath) const
        {
            return IFileManager::Get().FileExists(*GetFilePath(RelativePath));
        }

        const FString Path;
    };

    // Points the uBot install directory somewhere harmless for the duration of a test.
    class FScopedInstallDirectory
    {
    public:
        UE_NONCOPYABLE(FScopedInstallDirectory);

        explicit FScopedInstallDirectory(const FString& Directory)
            : Settings(GetMutableDefault<UUBotCoreSettings>())
            , PreviousDirectory(Settings->PackageInstallDirectory)
        {
            Settings->PackageInstallDirectory = Directory;
        }

        ~FScopedInstallDirectory()
        {
            Settings->PackageInstallDirectory = PreviousDirectory;
        }

    private:
        UUBotCoreSettings* Settings;
        FString PreviousDirectory;
    };

    FUBotPackageInfo MakePackage(const TCHAR* Name, bool bInstalled, bool bEnabled, EUBotPackageLayer Layer = EUBotPackageLayer::Capability)
    {
        FUBotPackageInfo Package;
        Package.Name = Name;
        Package.Version = TEXT("0.1.0");
        Package.Layer = Layer;
        Package.bInstalled = bInstalled;
        Package.bEnabled = bInstalled && bEnabled;
        Package.State = !bInstalled ? EUBotPackageState::NotInstalled
            : (bEnabled ? EUBotPackageState::Enabled : EUBotPackageState::Disabled);
        return Package;
    }

    // Appends a version the way FUBotPackageIndex records it (newest first is the caller's business).
    void AddIndexVersion(FUBotPackageInfo& Package, const TCHAR* Version, const TCHAR* Ref, bool bPlanned = false)
    {
        FUBotPackageIndexVersion& IndexVersion = Package.IndexVersions.AddDefaulted_GetRef();
        IndexVersion.Version = Version;
        IndexVersion.Ref = Ref;
        IndexVersion.bPlanned = bPlanned;
        if (!bPlanned)
        {
            Package.AvailableVersions.Add(Version);
        }
    }

    // An index entry with one installable version, 0.1.0 at the tag v0.1.0.
    FUBotPackageInfo MakeIndexOnlyPackage(const TCHAR* Name, const TCHAR* Repository)
    {
        FUBotPackageInfo Package = MakePackage(Name, false, false);
        Package.bFromIndex = true;
        Package.Repository = Repository;
        AddIndexVersion(Package, TEXT("0.1.0"), TEXT("v0.1.0"));
        return Package;
    }

    void AddRequire(FUBotPackageInfo& Package, const TCHAR* Name, const TCHAR* Version = TEXT("^0.1.0"), bool bOptional = false)
    {
        FUBotPackageDependency Dependency;
        Dependency.Name = Name;
        Dependency.Version = Version;
        Dependency.bOptional = bOptional;
        Package.Requires.Add(Dependency);
    }

    /** UBotCore <- UBotSensor <- UBotROS, plus an optional UBotExtras that is not in the set. */
    TArray<FUBotPackageInfo> MakeChain(const FUBotPackageInfo& Sensor, const FUBotPackageInfo& Ros)
    {
        FUBotPackageInfo Core = MakePackage(TEXT("UBotCore"), true, true, EUBotPackageLayer::Foundation);

        FUBotPackageInfo SensorWithRequires = Sensor;
        SensorWithRequires.Requires.Reset();
        AddRequire(SensorWithRequires, TEXT("UBotCore"));

        FUBotPackageInfo RosWithRequires = Ros;
        RosWithRequires.Requires.Reset();
        AddRequire(RosWithRequires, TEXT("UBotCore"));
        AddRequire(RosWithRequires, TEXT("UBotSensor"));
        AddRequire(RosWithRequires, TEXT("UBotExtras"), TEXT(">=1.0"), /*bOptional*/ true);

        // Dependents first in the array so that a correct order cannot come from array order.
        return { RosWithRequires, SensorWithRequires, Core };
    }

    TArray<FUBotPackageInfo> MakeInstalledChain(bool bSensorEnabled, bool bRosEnabled)
    {
        return MakeChain(
            MakePackage(TEXT("UBotSensor"), true, bSensorEnabled),
            MakePackage(TEXT("UBotROS"), true, bRosEnabled, EUBotPackageLayer::Adapter));
    }

    TArray<FString> Names(std::initializer_list<const TCHAR*> InNames)
    {
        TArray<FString> Result;
        for (const TCHAR* Name : InNames)
        {
            Result.Add(Name);
        }
        return Result;
    }

    void TestNames(FAutomationTestBase& Test, const TCHAR* What, const TArray<FString>& Actual, std::initializer_list<const TCHAR*> Expected)
    {
        Test.TestEqualSensitive(What, *FString::Join(Actual, TEXT(", ")), *FString::Join(Names(Expected), TEXT(", ")));
    }

    bool MessagesMention(const TArray<FString>& Messages, const TCHAR* Text)
    {
        return Messages.ContainsByPredicate([Text](const FString& Message)
        {
            return Message.Contains(Text);
        });
    }

    // The first widget of the given type in the widget tree below Root (Root included), or null.
    TSharedPtr<SWidget> FindWidgetOfType(const TSharedRef<SWidget>& Root, const FName& TypeName)
    {
        if (Root->GetType() == TypeName)
        {
            return Root;
        }
        if (FChildren* Children = Root->GetChildren())
        {
            for (int32 Index = 0; Index < Children->Num(); ++Index)
            {
                if (const TSharedPtr<SWidget> Found = FindWidgetOfType(Children->GetChildAt(Index), TypeName))
                {
                    return Found;
                }
            }
        }
        return nullptr;
    }

    // Matches the "/"-separated segments of a path against those of a UAT FileFilter pattern: "..." stands
    // for any number of folders, "*" and "?" work inside one name. Case-insensitive like UAT on Windows.
    bool MatchesFilterSegments(const TArray<FString>& Pattern, int32 PatternIndex, const TArray<FString>& Path, int32 PathIndex)
    {
        if (PatternIndex == Pattern.Num())
        {
            return PathIndex == Path.Num();
        }
        if (Pattern[PatternIndex] == TEXT("..."))
        {
            for (int32 Skipped = PathIndex; Skipped <= Path.Num(); ++Skipped)
            {
                if (MatchesFilterSegments(Pattern, PatternIndex + 1, Path, Skipped))
                {
                    return true;
                }
            }
            return false;
        }
        return PathIndex < Path.Num()
            && Path[PathIndex].MatchesWildcard(Pattern[PatternIndex])
            && MatchesFilterSegments(Pattern, PatternIndex + 1, Path, PathIndex + 1);
    }

    bool MatchesFilterRule(FString Rule, const FString& RelativePath)
    {
        Rule.ReplaceInline(TEXT("\\"), TEXT("/"));
        if (Rule.StartsWith(TEXT("/")))
        {
            Rule.RightChopInline(1);
        }
        else if (!Rule.Contains(TEXT("/")) && !Rule.StartsWith(TEXT("...")))
        {
            // Without a folder the rule applies in every folder.
            Rule = TEXT(".../") + Rule;
        }
        if (Rule.EndsWith(TEXT("/")))
        {
            Rule += TEXT("...");
        }
        TArray<FString> PatternSegments;
        Rule.ParseIntoArray(PatternSegments, TEXT("/"), /*InCullEmpty*/ true);
        TArray<FString> PathSegments;
        RelativePath.ParseIntoArray(PathSegments, TEXT("/"), /*InCullEmpty*/ true);
        return MatchesFilterSegments(PatternSegments, 0, PathSegments, 0);
    }

    // Whether UAT's BuildPlugin copies the file (path relative to the plugin root, "/" separators) into the
    // plugin it packages: its built-in include rules, then the rules of the [FilterPlugin] section of the
    // plugin's Config/FilterPlugin.ini, where a leading "-" excludes and the last matching rule wins.
    bool IsStagedByBuildPlugin(const FString& RelativePath, const TArray<FString>& FilterPluginLines)
    {
        bool bStaged = false;
        for (const TCHAR* Rule : { TEXT("/Resources/..."), TEXT("/Content/..."), TEXT("/Source/..."), TEXT("/Shaders/...") })
        {
            bStaged = bStaged || MatchesFilterRule(Rule, RelativePath);
        }
        if (MatchesFilterRule(TEXT("/Tests/..."), RelativePath))
        {
            bStaged = false;
        }

        bool bInSection = false;
        for (const FString& RawLine : FilterPluginLines)
        {
            FString Line = RawLine.TrimStartAndEnd();
            if (Line.IsEmpty() || Line.StartsWith(TEXT(";")))
            {
                continue;
            }
            if (Line.StartsWith(TEXT("[")))
            {
                bInSection = Line == TEXT("[FilterPlugin]");
                continue;
            }
            if (!bInSection)
            {
                continue;
            }
            const bool bExclude = Line.StartsWith(TEXT("-"));
            if (bExclude)
            {
                Line.RightChopInline(1);
                Line.TrimStartInline();
            }
            if (MatchesFilterRule(Line, RelativePath))
            {
                bStaged = !bExclude;
            }
        }
        return bStaged;
    }

    // The relative targets of the src="..." and srcset="..." attributes of the README's HTML (the first
    // entry of a srcset), without web links.
    TArray<FString> GetReadmeImagePaths(const FString& Readme)
    {
        TArray<FString> Paths;
        for (const TCHAR* Attribute : { TEXT("src=\""), TEXT("srcset=\"") })
        {
            int32 From = 0;
            while (true)
            {
                const int32 Found = Readme.Find(Attribute, ESearchCase::CaseSensitive, ESearchDir::FromStart, From);
                if (Found == INDEX_NONE)
                {
                    break;
                }
                const int32 ValueStart = Found + FCString::Strlen(Attribute);
                const int32 ValueEnd = Readme.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, ValueStart);
                if (ValueEnd == INDEX_NONE)
                {
                    break;
                }
                FString Value = Readme.Mid(ValueStart, ValueEnd - ValueStart).TrimStartAndEnd();
                int32 Space = INDEX_NONE;
                if (Value.FindChar(TEXT(' '), Space))
                {
                    Value.LeftInline(Space);
                }
                if (!Value.IsEmpty() && !Value.Contains(TEXT("://")) && !Value.StartsWith(TEXT("#")))
                {
                    Paths.AddUnique(Value);
                }
                From = ValueEnd + 1;
            }
        }
        return Paths;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotPackageManagerRepositoryFolderNameTest, "UBotCore.PackageManager.RepositoryFolderName",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotPackageManagerRepositoryFolderNameTest::RunTest(const FString& Parameters)
{
    struct FCase
    {
        const TCHAR* Url;
        const TCHAR* Expected;
    };

    const FCase Cases[] =
    {
        { TEXT("https://github.com/AstreoX/uBotCore.git"), TEXT("uBotCore") },
        { TEXT("https://github.com/AstreoX/uBotSensor.git"), TEXT("uBotSensor") },
        { TEXT("https://github.com/AstreoX/uBotROS"), TEXT("uBotROS") },
        { TEXT("https://github.com/AstreoX/uBotROS.git/"), TEXT("uBotROS") },
        { TEXT("  https://github.com/AstreoX/uBotROS.git  "), TEXT("uBotROS") },
        { TEXT("https://example.com/team/uBotSonar.GIT"), TEXT("uBotSonar") },
        { TEXT("https://example.com/team/uBotSonar.git?ref=main#readme"), TEXT("uBotSonar") },
        { TEXT("git@github.com:AstreoX/uBotROS.git"), TEXT("uBotROS") },
        { TEXT("git@example.com:uBotLidar.git"), TEXT("uBotLidar") },
        { TEXT("ssh://git@example.com:2222/team/uBotLidar.git"), TEXT("uBotLidar") },
        { TEXT("file:///C:/Repos/My Package.git"), TEXT("My Package") },
        { TEXT("C:\\Repos\\uBotLocal.git"), TEXT("uBotLocal") },
        { TEXT("/srv/git/uBotLocal.git"), TEXT("uBotLocal") },
        { TEXT("uBotPlain"), TEXT("uBotPlain") },
        { TEXT(""), TEXT("") },
        { TEXT("   "), TEXT("") },
        { TEXT("https://example.com/.git"), TEXT("") },
        { TEXT("https://example.com/team/.."), TEXT("") },
        { TEXT("https://example.com/team/repo."), TEXT("") },
        { TEXT("https://example.com/team/re*po.git"), TEXT("") },
    };

    for (const FCase& Case : Cases)
    {
        TestEqualSensitive(*FString::Printf(TEXT("RepositoryFolderName('%s')"), Case.Url),
            *FUBotPackageService::RepositoryFolderName(Case.Url), Case.Expected);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotPackageManagerRepositoryUrlPolicyTest, "UBotCore.PackageManager.RepositoryUrlPolicy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotPackageManagerRepositoryUrlPolicyTest::RunTest(const FString& Parameters)
{
    const TCHAR* Allowed[] =
    {
        TEXT("https://github.com/AstreoX/uBotSensor.git"),
        TEXT("http://example.com/team/uBotSonar.git"),
        TEXT("git@github.com:AstreoX/uBotROS.git"),
        TEXT("ssh://git@example.com/team/uBotLidar.git"),
        TEXT("file:///C:/Repos/uBotLocal.git"),
        TEXT("https://[::1]/team/uBotLocal.git"),
        TEXT("C:/Repos/uBotLocal.git"),
        TEXT("C:\\Repos\\uBotLocal.git"),
        TEXT("/srv/git/uBotLocal.git"),
        TEXT("\\\\server\\share\\uBotLocal.git"),
    };
    for (const TCHAR* Url : Allowed)
    {
        FString Reason;
        TestTrue(FString::Printf(TEXT("'%s' is allowed"), Url), FUBotPackageService::IsRepositoryUrlAllowed(Url, &Reason));
    }

    const TCHAR* Rejected[] =
    {
        TEXT(""),
        TEXT("   "),
        TEXT("-uBotSensor"),
        TEXT("--upload-pack=touch /tmp/pwned"),
        TEXT("ext::sh -c touch% /tmp/pwned"),
        TEXT("fd::17"),
        TEXT("ftp://example.com/team/uBotSonar.git"),
        TEXT("ssh://-oProxyCommand=calc/uBotSonar.git"),
        TEXT("https://example.com/team/uBot\"Sonar.git"),
        TEXT("https://example.com/team/uBotSonar.git\n--config=x"),
        TEXT("https://example.com/team/.git"),
        // SPEC 5.4 allows no other transports: git:// is unauthenticated and unencrypted, and a bare
        // "host:path", a relative path or a path with whitespace is read by git as something else.
        TEXT("git://example.com/team/uBotLidar.git"),
        TEXT("relative/path.git"),
        TEXT("..\\..\\other"),
        TEXT("host:path"),
        TEXT("@host:path"),
        TEXT("git@host:"),
        TEXT("git@:path"),
        TEXT("https://example.com/team/uBot Sonar.git"),
        TEXT("https://example.com/team/uBot\tSonar.git"),
        TEXT(" https://example.com/team/uBotSonar.git"),
        TEXT("https://"),
        TEXT("ssh://"),
    };
    for (const TCHAR* Url : Rejected)
    {
        FString Reason;
        TestFalse(FString::Printf(TEXT("'%s' is rejected"), Url), FUBotPackageService::IsRepositoryUrlAllowed(Url, &Reason));
        TestFalse(FString::Printf(TEXT("'%s' has a reason"), Url), Reason.IsEmpty());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotPackageManagerQuoteArgumentTest, "UBotCore.PackageManager.QuoteArgument",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotPackageManagerQuoteArgumentTest::RunTest(const FString& Parameters)
{
    TestEqualSensitive(TEXT("Plain"), *FUBotPackageService::QuoteArgument(TEXT("pull")), TEXT("\"pull\""));
    TestEqualSensitive(TEXT("Spaces"), *FUBotPackageService::QuoteArgument(TEXT("C:/My Project/Plugins/uBot/uBotSensor")),
        TEXT("\"C:/My Project/Plugins/uBot/uBotSensor\""));
    TestEqualSensitive(TEXT("Empty"), *FUBotPackageService::QuoteArgument(FString()), TEXT("\"\""));

#if PLATFORM_WINDOWS
    // A trailing backslash must not escape the closing quote.
    TestEqualSensitive(TEXT("Trailing backslash"), *FUBotPackageService::QuoteArgument(TEXT("C:\\Dir\\")), TEXT("\"C:\\Dir\\\\\""));
    TestEqualSensitive(TEXT("Inner backslash"), *FUBotPackageService::QuoteArgument(TEXT("C:\\Dir\\Sub")), TEXT("\"C:\\Dir\\Sub\""));
    TestEqualSensitive(TEXT("Embedded quote"), *FUBotPackageService::QuoteArgument(TEXT("a\"b")), TEXT("\"a\\\"b\""));
#endif
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotPackageManagerEnableOrderTest, "UBotCore.PackageManager.EnableOrder",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotPackageManagerEnableOrderTest::RunTest(const FString& Parameters)
{
    using namespace UBotCoreEditorTestsPrivate;

    // Registry helper: dependencies first, optional requirements ignored.
    {
        const TArray<FUBotPackageInfo> Packages = MakeInstalledChain(false, false);
        TArray<FString> Order;
        FString Error;
        TestTrue(TEXT("Order resolves"), FUBotPackageRegistry::ResolveDependencyOrder(Packages, TEXT("UBotROS"), Order, &Error));
        TestNames(*this, TEXT("Order"), Order, { TEXT("UBotCore"), TEXT("UBotSensor"), TEXT("UBotROS") });
    }

    // Only packages that are not enabled yet, dependencies first.
    {
        const TArray<FUBotPackageInfo> Packages = MakeInstalledChain(false, false);
        TArray<FString> ToEnable;
        TArray<FString> Messages;
        TestTrue(TEXT("Enable plan succeeds"), FUBotPackageService::PlanEnable(Packages, TEXT("UBotROS"), ToEnable, Messages));
        TestNames(*this, TEXT("Enable plan"), ToEnable, { TEXT("UBotSensor"), TEXT("UBotROS") });

        TestTrue(TEXT("Names match case-insensitively"), FUBotPackageService::PlanEnable(Packages, TEXT("ubotros"), ToEnable, Messages));
        TestNames(*this, TEXT("Case-insensitive plan"), ToEnable, { TEXT("UBotSensor"), TEXT("UBotROS") });
    }

    // Everything enabled already: success without work.
    {
        const TArray<FUBotPackageInfo> Packages = MakeInstalledChain(true, true);
        TArray<FString> ToEnable;
        TArray<FString> Messages;
        TestTrue(TEXT("Already enabled succeeds"), FUBotPackageService::PlanEnable(Packages, TEXT("UBotROS"), ToEnable, Messages));
        TestEqual(TEXT("Nothing to enable"), ToEnable.Num(), 0);
    }

    // A required package that is only in the index blocks the whole plan.
    {
        const TArray<FUBotPackageInfo> Packages = MakeChain(
            MakeIndexOnlyPackage(TEXT("UBotSensor"), TEXT("https://github.com/AstreoX/uBotSensor.git")),
            MakePackage(TEXT("UBotROS"), true, false));
        TArray<FString> ToEnable;
        TArray<FString> Messages;
        TestFalse(TEXT("Missing dependency fails"), FUBotPackageService::PlanEnable(Packages, TEXT("UBotROS"), ToEnable, Messages));
        TestEqual(TEXT("No partial plan"), ToEnable.Num(), 0);
        TestTrue(TEXT("Message names the missing package"), MessagesMention(Messages, TEXT("UBotSensor")));
    }

    // A required package absent from the set entirely.
    {
        TArray<FUBotPackageInfo> Packages = MakeInstalledChain(false, false);
        Packages.RemoveAll([](const FUBotPackageInfo& Package) { return Package.Name == TEXT("UBotSensor"); });
        TArray<FString> ToEnable;
        TArray<FString> Messages;
        TestFalse(TEXT("Unknown dependency fails"), FUBotPackageService::PlanEnable(Packages, TEXT("UBotROS"), ToEnable, Messages));
        TestFalse(TEXT("Unknown dependency is explained"), Messages.IsEmpty());
    }

    // Unknown and not installed targets.
    {
        const TArray<FUBotPackageInfo> Packages = MakeChain(
            MakePackage(TEXT("UBotSensor"), true, true),
            MakeIndexOnlyPackage(TEXT("UBotROS"), TEXT("https://github.com/AstreoX/uBotROS.git")));
        TArray<FString> ToEnable;
        TArray<FString> Messages;
        TestFalse(TEXT("Unknown package fails"), FUBotPackageService::PlanEnable(Packages, TEXT("UBotMissing"), ToEnable, Messages));
        TestFalse(TEXT("Not installed package fails"), FUBotPackageService::PlanEnable(Packages, TEXT("UBotROS"), ToEnable, Messages));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotPackageManagerDisableOrderTest, "UBotCore.PackageManager.DisableOrder",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotPackageManagerDisableOrderTest::RunTest(const FString& Parameters)
{
    using namespace UBotCoreEditorTestsPrivate;

    // Registry helper: transitive dependents.
    {
        const TArray<FUBotPackageInfo> Packages = MakeInstalledChain(true, true);
        const TArray<FString> Dependents = FUBotPackageRegistry::FindDependents(Packages, TEXT("UBotCore"), true);
        TestEqual(TEXT("Two dependents"), Dependents.Num(), 2);
        TestTrue(TEXT("Sensor depends on Core"), Dependents.Contains(FString(TEXT("UBotSensor"))));
        TestTrue(TEXT("ROS depends on Core"), Dependents.Contains(FString(TEXT("UBotROS"))));
    }

    // Enabled dependents block a plain disable.
    {
        const TArray<FUBotPackageInfo> Packages = MakeInstalledChain(true, true);
        TArray<FString> ToDisable;
        TArray<FString> Messages;
        TestFalse(TEXT("Blocked by dependents"), FUBotPackageService::PlanDisable(Packages, TEXT("UBotCore"), false, ToDisable, Messages));
        TestEqual(TEXT("No partial plan"), ToDisable.Num(), 0);
        TestTrue(TEXT("Message names ROS"), MessagesMention(Messages, TEXT("UBotROS")));
        TestTrue(TEXT("Message names Sensor"), MessagesMention(Messages, TEXT("UBotSensor")));
    }

    // Forced: dependents first.
    {
        const TArray<FUBotPackageInfo> Packages = MakeInstalledChain(true, true);
        TArray<FString> ToDisable;
        TArray<FString> Messages;
        TestTrue(TEXT("Forced disable succeeds"), FUBotPackageService::PlanDisable(Packages, TEXT("UBotCore"), true, ToDisable, Messages));
        TestNames(*this, TEXT("Dependents first"), ToDisable, { TEXT("UBotROS"), TEXT("UBotSensor"), TEXT("UBotCore") });
    }

    // A leaf disables alone.
    {
        const TArray<FUBotPackageInfo> Packages = MakeInstalledChain(true, true);
        TArray<FString> ToDisable;
        TArray<FString> Messages;
        TestTrue(TEXT("Leaf disable succeeds"), FUBotPackageService::PlanDisable(Packages, TEXT("UBotROS"), false, ToDisable, Messages));
        TestNames(*this, TEXT("Leaf only"), ToDisable, { TEXT("UBotROS") });
    }

    // Disabled dependents do not block and are not touched.
    {
        const TArray<FUBotPackageInfo> Packages = MakeInstalledChain(true, false);
        TArray<FString> ToDisable;
        TArray<FString> Messages;
        TestTrue(TEXT("Disabled dependent does not block"), FUBotPackageService::PlanDisable(Packages, TEXT("UBotSensor"), false, ToDisable, Messages));
        TestNames(*this, TEXT("Only Sensor"), ToDisable, { TEXT("UBotSensor") });
    }

    // Already disabled: success without work. Not installed: failure.
    {
        const TArray<FUBotPackageInfo> Packages = MakeChain(
            MakePackage(TEXT("UBotSensor"), true, false),
            MakeIndexOnlyPackage(TEXT("UBotROS"), TEXT("https://github.com/AstreoX/uBotROS.git")));
        TArray<FString> ToDisable;
        TArray<FString> Messages;
        TestTrue(TEXT("Already disabled succeeds"), FUBotPackageService::PlanDisable(Packages, TEXT("UBotSensor"), false, ToDisable, Messages));
        TestEqual(TEXT("Nothing to disable"), ToDisable.Num(), 0);
        TestFalse(TEXT("Not installed fails"), FUBotPackageService::PlanDisable(Packages, TEXT("UBotROS"), false, ToDisable, Messages));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotPackageManagerInstallPlanTest, "UBotCore.PackageManager.InstallPlan",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotPackageManagerInstallPlanTest::RunTest(const FString& Parameters)
{
    using namespace UBotCoreEditorTestsPrivate;

    const TCHAR* SensorRepository = TEXT("https://github.com/AstreoX/uBotSensor.git");
    const TCHAR* RosRepository = TEXT("https://github.com/AstreoX/uBotROS.git");

    // Missing dependencies are cloned first.
    {
        const TArray<FUBotPackageInfo> Packages = MakeChain(
            MakeIndexOnlyPackage(TEXT("UBotSensor"), SensorRepository),
            MakeIndexOnlyPackage(TEXT("UBotROS"), RosRepository));
        TArray<FString> ToInstall;
        TArray<FString> Messages;
        TestTrue(TEXT("Install plan succeeds"), FUBotPackageService::PlanInstall(Packages, TEXT("UBotROS"), ToInstall, Messages));
        TestNames(*this, TEXT("Dependencies first"), ToInstall, { TEXT("UBotSensor"), TEXT("UBotROS") });
    }

    // Installed dependencies are skipped; an installed target needs nothing.
    {
        const TArray<FUBotPackageInfo> Packages = MakeChain(
            MakePackage(TEXT("UBotSensor"), true, false),
            MakeIndexOnlyPackage(TEXT("UBotROS"), RosRepository));
        TArray<FString> ToInstall;
        TArray<FString> Messages;
        TestTrue(TEXT("Partial install plan succeeds"), FUBotPackageService::PlanInstall(Packages, TEXT("UBotROS"), ToInstall, Messages));
        TestNames(*this, TEXT("Only ROS"), ToInstall, { TEXT("UBotROS") });

        TestTrue(TEXT("Installed target succeeds"), FUBotPackageService::PlanInstall(Packages, TEXT("UBotSensor"), ToInstall, Messages));
        TestEqual(TEXT("Nothing to install"), ToInstall.Num(), 0);
    }

    // A requested version of an installed target must be the installed one: installing never changes
    // versions, so anything else has to fail instead of reporting "already installed".
    {
        FUBotPackageInfo InstalledSensor = MakePackage(TEXT("UBotSensor"), true, false);
        InstalledSensor.Version = TEXT("0.1.0");
        AddIndexVersion(InstalledSensor, TEXT("0.1.0"), TEXT("v0.1.0"));
        AddIndexVersion(InstalledSensor, TEXT("0.2.0"), TEXT("v0.2.0"));
        const TArray<FUBotPackageInfo> Packages = MakeChain(InstalledSensor, MakeIndexOnlyPackage(TEXT("UBotROS"), RosRepository));

        for (const TCHAR* Same : { TEXT("0.1.0"), TEXT("v0.1.0"), TEXT(" 0.1 ") })
        {
            TArray<FString> ToInstall;
            TArray<FString> Messages;
            TestTrue(*FString::Printf(TEXT("The installed version '%s' succeeds"), Same),
                FUBotPackageService::PlanInstall(Packages, TEXT("UBotSensor"), ToInstall, Messages, Same));
            TestEqual(TEXT("Nothing to clone"), ToInstall.Num(), 0);
        }

        // Another version fails whether or not the index lists it, and the message says what to do.
        for (const TCHAR* Other : { TEXT("0.2.0"), TEXT("9.9.9") })
        {
            TArray<FString> ToInstall;
            TArray<FString> Messages;
            TestFalse(*FString::Printf(TEXT("Version '%s' of an installed package fails"), Other),
                FUBotPackageService::PlanInstall(Packages, TEXT("UBotSensor"), ToInstall, Messages, Other));
            TestEqual(TEXT("No plan"), ToInstall.Num(), 0);
            TestTrue(TEXT("The message names the package and the requested version"),
                MessagesMention(Messages, TEXT("Cannot install UBotSensor")) && MessagesMention(Messages, Other));
            TestTrue(TEXT("The message says the installed version is in the way"),
                MessagesMention(Messages, TEXT("0.1.0 is already installed")) && MessagesMention(Messages, TEXT("-Update=UBotSensor")));
        }

        {
            TArray<FString> ToInstall;
            TArray<FString> Messages;
            TestFalse(TEXT("Text that is no version fails"), FUBotPackageService::PlanInstall(Packages, TEXT("UBotSensor"), ToInstall, Messages, TEXT("junk")));
            TestTrue(TEXT("The message says why"), MessagesMention(Messages, TEXT("semantic version")));
        }

        // An installed package whose own version is unreadable cannot match any request.
        {
            FUBotPackageInfo UnreadableSensor = InstalledSensor;
            UnreadableSensor.Version = TEXT("not-a-version-at-all!");
            const TArray<FUBotPackageInfo> Unreadable = MakeChain(UnreadableSensor, MakeIndexOnlyPackage(TEXT("UBotROS"), RosRepository));
            TArray<FString> ToInstall;
            TArray<FString> Messages;
            TestFalse(TEXT("An unreadable installed version fails"), FUBotPackageService::PlanInstall(Unreadable, TEXT("UBotSensor"), ToInstall, Messages, TEXT("0.1.0")));
        }

        // The request belongs to the target only: an installed dependency keeps its own version.
        {
            FUBotPackageInfo OlderSensor = InstalledSensor;
            OlderSensor.Version = TEXT("0.0.5");
            const TArray<FUBotPackageInfo> WithOlderSensor = MakeChain(OlderSensor, MakeIndexOnlyPackage(TEXT("UBotROS"), RosRepository));
            TArray<FString> ToInstall;
            TArray<FString> Messages;
            TestTrue(TEXT("An installed dependency is not compared with the request"),
                FUBotPackageService::PlanInstall(WithOlderSensor, TEXT("UBotROS"), ToInstall, Messages, TEXT("0.1.0")));
            TestNames(*this, TEXT("Only ROS is cloned"), ToInstall, { TEXT("UBotROS") });
        }

        // Without a requested version an installed target stays an idempotent success.
        {
            TArray<FString> ToInstall;
            TArray<FString> Messages;
            TestTrue(TEXT("No version requested succeeds"), FUBotPackageService::PlanInstall(Packages, TEXT("UBotSensor"), ToInstall, Messages, FString()));
            TestTrue(TEXT("Blank version requested succeeds"), FUBotPackageService::PlanInstall(Packages, TEXT("UBotSensor"), ToInstall, Messages, TEXT("  ")));
        }
    }

    // Entries that cannot be cloned safely block the plan.
    {
        FUBotPackageInfo NoRepository = MakeIndexOnlyPackage(TEXT("UBotSensor"), TEXT(""));
        FUBotPackageInfo UnsafeRepository = MakeIndexOnlyPackage(TEXT("UBotSensor"), TEXT("ext::sh -c touch% /tmp/pwned"));
        FUBotPackageInfo NotFromIndex = MakeIndexOnlyPackage(TEXT("UBotSensor"), SensorRepository);
        NotFromIndex.bFromIndex = false;

        for (const FUBotPackageInfo& Sensor : { NoRepository, UnsafeRepository, NotFromIndex })
        {
            const TArray<FUBotPackageInfo> Packages = MakeChain(Sensor, MakeIndexOnlyPackage(TEXT("UBotROS"), RosRepository));
            TArray<FString> ToInstall;
            TArray<FString> Messages;
            TestFalse(TEXT("Uninstallable dependency fails"), FUBotPackageService::PlanInstall(Packages, TEXT("UBotROS"), ToInstall, Messages));
            TestEqual(TEXT("No partial plan"), ToInstall.Num(), 0);
            TestTrue(TEXT("Message names Sensor"), MessagesMention(Messages, TEXT("UBotSensor")));
        }
    }

    // Cycles and unknown packages.
    {
        FUBotPackageInfo A = MakeIndexOnlyPackage(TEXT("UBotCycleA"), TEXT("https://example.com/UBotCycleA.git"));
        FUBotPackageInfo B = MakeIndexOnlyPackage(TEXT("UBotCycleB"), TEXT("https://example.com/UBotCycleB.git"));
        AddRequire(A, TEXT("UBotCycleB"));
        AddRequire(B, TEXT("UBotCycleA"));
        const TArray<FUBotPackageInfo> Packages = { A, B };
        TArray<FString> ToInstall;
        TArray<FString> Messages;
        TestFalse(TEXT("Cycle fails"), FUBotPackageService::PlanInstall(Packages, TEXT("UBotCycleA"), ToInstall, Messages));
        TestFalse(TEXT("Unknown fails"), FUBotPackageService::PlanInstall(Packages, TEXT("UBotMissing"), ToInstall, Messages));
    }

    // The refusal message must not echo credentials from the index entry.
    {
        const FUBotPackageInfo Credentialed = MakeIndexOnlyPackage(TEXT("UBotSensor"), TEXT("ext::https://user:secret-token@example.com/uBotSensor.git"));
        const TArray<FUBotPackageInfo> Packages = MakeChain(Credentialed, MakeIndexOnlyPackage(TEXT("UBotROS"), RosRepository));
        TArray<FString> ToInstall;
        TArray<FString> Messages;
        TestFalse(TEXT("Remote helper URL fails"), FUBotPackageService::PlanInstall(Packages, TEXT("UBotROS"), ToInstall, Messages));
        TestFalse(TEXT("Messages do not contain the token"), MessagesMention(Messages, TEXT("secret-token")));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotPackageManagerRedactUrlCredentialsTest, "UBotCore.PackageManager.RedactUrlCredentials",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotPackageManagerRedactUrlCredentialsTest::RunTest(const FString& Parameters)
{
    struct FCase
    {
        const TCHAR* Text;
        const TCHAR* Expected;
    };

    const FCase Cases[] =
    {
        // User and password, or a bare token, in http(s) URLs.
        { TEXT("https://u:tok@h/r.git"), TEXT("https://***@h/r.git") },
        { TEXT("https://tok@h/r.git"), TEXT("https://***@h/r.git") },
        { TEXT("http://u:tok@h/r.git"), TEXT("http://***@h/r.git") },
        { TEXT("HTTPS://tok@h/r.git"), TEXT("HTTPS://***@h/r.git") },
        { TEXT("https://u:p@h:8443/r.git"), TEXT("https://***@h:8443/r.git") },
        { TEXT("https://u:p@w@h/r.git"), TEXT("https://***@h/r.git") },
        // Account names without a password stay readable; scp-like URLs have no "://".
        { TEXT("ssh://git@h/r.git"), TEXT("ssh://git@h/r.git") },
        { TEXT("ssh://git@h:2222/r.git"), TEXT("ssh://git@h:2222/r.git") },
        { TEXT("ssh://u:pw@h/r.git"), TEXT("ssh://***@h/r.git") },
        { TEXT("git@h:r.git"), TEXT("git@h:r.git") },
        // Nothing to redact: no user info, '@' only after the authority, no URL at all.
        { TEXT("https://h/r.git"), TEXT("https://h/r.git") },
        { TEXT("https://h/team@x/r.git"), TEXT("https://h/team@x/r.git") },
        { TEXT("https://h/r.git?mail=a@b"), TEXT("https://h/r.git?mail=a@b") },
        { TEXT("file:///C:/Repos/r.git"), TEXT("file:///C:/Repos/r.git") },
        { TEXT("Already up to date."), TEXT("Already up to date.") },
        { TEXT(""), TEXT("") },
        // Free text as printed by git and in the argument string given to the process.
        { TEXT("clone -- \"https://u:tok@h/r.git\" \"C:/x\""), TEXT("clone -- \"https://***@h/r.git\" \"C:/x\"") },
        { TEXT("fatal: unable to access 'https://tok@h/r.git/': Could not resolve host"),
          TEXT("fatal: unable to access 'https://***@h/r.git/': Could not resolve host") },
        { TEXT("a https://t1@h1/r.git b ssh://git@h2/r.git c https://u:t2@h3/r.git"),
          TEXT("a https://***@h1/r.git b ssh://git@h2/r.git c https://***@h3/r.git") },
    };

    for (const FCase& Case : Cases)
    {
        const FString Redacted = FUBotPackageService::RedactUrlCredentials(Case.Text);
        TestEqualSensitive(*FString::Printf(TEXT("RedactUrlCredentials('%s')"), Case.Text), *Redacted, Case.Expected);
        TestEqualSensitive(*FString::Printf(TEXT("RedactUrlCredentials is idempotent for '%s'"), Case.Text),
            *FUBotPackageService::RedactUrlCredentials(Redacted), *Redacted);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotPackageManagerRemoveIncompleteCloneTest, "UBotCore.PackageManager.RemoveIncompleteClone",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotPackageManagerRemoveIncompleteCloneTest::RunTest(const FString& Parameters)
{
    using namespace UBotCoreEditorTestsPrivate;

    FScopedScratchDirectory Scratch;
    const FScopedInstallDirectory InstallDirectoryOverride(Scratch.GetFilePath(TEXT("Install")));
    IFileManager::Get().MakeDirectory(*Scratch.GetFilePath(TEXT("Install")), true);

    // What a killed "git clone" leaves behind: read-only object files below a partial .git.
    {
        TestTrue(TEXT("Partial pack file written"), Scratch.WriteFile(TEXT("Install/uBotPartial/.git/objects/pack/pack-1.pack"), /*bReadOnly*/ true));
        TestTrue(TEXT("Partial working tree written"), Scratch.WriteFile(TEXT("Install/uBotPartial/Source/Part.cpp")));

        FString Message;
        TestTrue(TEXT("Partial clone is removed"), FUBotPackageService::RemoveIncompleteClone(Scratch.GetFilePath(TEXT("Install/uBotPartial")), Message));
        TestTrue(TEXT("Message reports the removal"), Message.StartsWith(TEXT("Removed the incomplete clone")));
        TestFalse(TEXT("Partial clone folder is gone"), Scratch.DirectoryExists(TEXT("Install/uBotPartial")));
        TestTrue(TEXT("Install directory itself stays"), Scratch.DirectoryExists(TEXT("Install")));
    }

    // A clone that never created its folder has nothing to clean up.
    {
        FString Message;
        TestTrue(TEXT("Missing folder counts as clean"), FUBotPackageService::RemoveIncompleteClone(Scratch.GetFilePath(TEXT("Install/uBotNone")), Message));
        TestTrue(TEXT("No message for a missing folder"), Message.IsEmpty());
    }

    // Only folders inside the install directory may ever be deleted.
    {
        TestTrue(TEXT("Outside file written"), Scratch.WriteFile(TEXT("Outside/keep.txt")));
        TestTrue(TEXT("Prefix sibling file written"), Scratch.WriteFile(TEXT("InstallSibling/keep.txt")));
        TestTrue(TEXT("Install file written"), Scratch.WriteFile(TEXT("Install/uBotKeep/keep.txt")));

        FString Message;
        TestFalse(TEXT("Outside folder is refused"), FUBotPackageService::RemoveIncompleteClone(Scratch.GetFilePath(TEXT("Outside")), Message));
        TestTrue(TEXT("Refusal is explained"), Message.Contains(TEXT("Refusing")));
        TestTrue(TEXT("Outside file survives"), Scratch.FileExists(TEXT("Outside/keep.txt")));

        TestFalse(TEXT("Sibling with the same name prefix is refused"),
            FUBotPackageService::RemoveIncompleteClone(Scratch.GetFilePath(TEXT("InstallSibling")), Message));
        TestTrue(TEXT("Sibling file survives"), Scratch.FileExists(TEXT("InstallSibling/keep.txt")));

        TestFalse(TEXT("The install directory itself is refused"),
            FUBotPackageService::RemoveIncompleteClone(Scratch.GetFilePath(TEXT("Install")), Message));
        TestTrue(TEXT("Existing sibling package survives"), Scratch.FileExists(TEXT("Install/uBotKeep/keep.txt")));

        TestFalse(TEXT("A parent escape is refused"),
            FUBotPackageService::RemoveIncompleteClone(Scratch.GetFilePath(TEXT("Install/../Outside")), Message));
        TestTrue(TEXT("Outside file survives the escape attempt"), Scratch.FileExists(TEXT("Outside/keep.txt")));

        TestFalse(TEXT("An empty path is refused"), FUBotPackageService::RemoveIncompleteClone(FString(), Message));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotPackageManagerCommandletArgumentsTest, "UBotCore.PackageManager.CommandletArguments",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotPackageManagerCommandletArgumentsTest::RunTest(const FString& Parameters)
{
    // -Timeout=<Seconds>: plain non-negative numbers only; 0 is the documented "wait forever".
    {
        struct FCase
        {
            const TCHAR* Text;
            bool bValid;
            double Expected;
        };

        const FCase Cases[] =
        {
            { TEXT("60"), true, 60.0 },
            { TEXT("0"), true, 0.0 },
            { TEXT("2.5"), true, 2.5 },
            { TEXT(".5"), true, 0.5 },
            { TEXT("5."), true, 5.0 },
            { TEXT("+5"), true, 5.0 },
            { TEXT(" 30 "), true, 30.0 },
            { TEXT(""), false, 0.0 },
            { TEXT("  "), false, 0.0 },
            { TEXT("abc"), false, 0.0 },
            { TEXT("-5"), false, 0.0 },
            { TEXT("--5"), false, 0.0 },
            { TEXT("5s"), false, 0.0 },
            { TEXT("1e3"), false, 0.0 },
            { TEXT("1.2.3"), false, 0.0 },
            { TEXT("."), false, 0.0 },
            { TEXT("+"), false, 0.0 },
            { TEXT("nan"), false, 0.0 },
            { TEXT("inf"), false, 0.0 },
        };

        for (const FCase& Case : Cases)
        {
            double Seconds = -1.0;
            const bool bParsed = UUBotPackageCommandlet::TryParseTimeoutSeconds(Case.Text, Seconds);
            TestEqual(*FString::Printf(TEXT("TryParseTimeoutSeconds('%s') validity"), Case.Text), bParsed, Case.bValid);
            if (Case.bValid)
            {
                TestEqual(*FString::Printf(TEXT("TryParseTimeoutSeconds('%s') value"), Case.Text), Seconds, Case.Expected);
            }
            else
            {
                TestEqual(*FString::Printf(TEXT("TryParseTimeoutSeconds('%s') leaves the output alone"), Case.Text), Seconds, -1.0);
            }
        }
    }

    // -Install=<Name>[@<Version>]
    {
        struct FCase
        {
            const TCHAR* Text;
            bool bValid;
            const TCHAR* Name;
            const TCHAR* Version;
        };

        const FCase Cases[] =
        {
            { TEXT("UBotSensor"), true, TEXT("UBotSensor"), TEXT("") },
            { TEXT("UBotSensor@0.1.0"), true, TEXT("UBotSensor"), TEXT("0.1.0") },
            { TEXT(" UBotSensor @ v0.1.0 "), true, TEXT("UBotSensor"), TEXT("v0.1.0") },
            { TEXT("UBotSensor@1.0.0-rc.1"), true, TEXT("UBotSensor"), TEXT("1.0.0-rc.1") },
            { TEXT(""), false, TEXT(""), TEXT("") },
            { TEXT("   "), false, TEXT(""), TEXT("") },
            { TEXT("@0.1.0"), false, TEXT(""), TEXT("") },
            { TEXT("UBotSensor@"), false, TEXT(""), TEXT("") },
            { TEXT("UBotSensor@  "), false, TEXT(""), TEXT("") },
        };
        for (const FCase& Case : Cases)
        {
            FString Name = TEXT("untouched");
            FString Version = TEXT("untouched");
            const bool bParsed = UUBotPackageCommandlet::TryParsePackageSpec(Case.Text, Name, Version);
            TestEqual(*FString::Printf(TEXT("TryParsePackageSpec('%s') validity"), Case.Text), bParsed, Case.bValid);
            if (Case.bValid)
            {
                TestEqualSensitive(*FString::Printf(TEXT("TryParsePackageSpec('%s') name"), Case.Text), *Name, Case.Name);
                TestEqualSensitive(*FString::Printf(TEXT("TryParsePackageSpec('%s') version"), Case.Text), *Version, Case.Version);
            }
            else
            {
                TestEqualSensitive(*FString::Printf(TEXT("TryParsePackageSpec('%s') leaves the outputs alone"), Case.Text), *Name, TEXT("untouched"));
            }
        }

        AddExpectedErrorPlain(TEXT("-Install expects <Name> or <Name>@<Version>"), EAutomationExpectedErrorFlags::Contains, 2);
        UUBotPackageCommandlet* Commandlet = NewObject<UUBotPackageCommandlet>(GetTransientPackage());
        TestEqual(TEXT("Main('-Install=@0.1.0') fails"), Commandlet->Main(TEXT("-Install=@0.1.0")), 1);
        TestEqual(TEXT("Main('-Install=UBotSensor@') fails"), Commandlet->Main(TEXT("-Install=UBotSensor@")), 1);
    }

    // Malformed arguments fail before anything runs: a bare action switch ("-Install" or
    // "-Install Name") and an unusable timeout must not exit 0 as if nothing was asked.
    {
        const TCHAR* ValueErrors[] =
        {
            TEXT("-Install"),
            TEXT("-Install UBotSensor"),
            TEXT("-Update"),
            TEXT("-Enable"),
            TEXT("-Disable -Force"),
            TEXT("-List -Timeout"),
            TEXT("-Timeout 60 -Validate"),
        };
        const TCHAR* TimeoutErrors[] =
        {
            TEXT("-Install=UBotSensor -Timeout=abc"),
            TEXT("-Validate -Timeout=-5"),
            TEXT("-Validate -Timeout="),
        };

        AddExpectedErrorPlain(TEXT("requires a value"), EAutomationExpectedErrorFlags::Contains, static_cast<int32>(UE_ARRAY_COUNT(ValueErrors)));
        AddExpectedErrorPlain(TEXT("-Timeout expects a non-negative number"), EAutomationExpectedErrorFlags::Contains, static_cast<int32>(UE_ARRAY_COUNT(TimeoutErrors)));

        UUBotPackageCommandlet* Commandlet = NewObject<UUBotPackageCommandlet>(GetTransientPackage());
        for (const TCHAR* Params : ValueErrors)
        {
            TestEqual(*FString::Printf(TEXT("Main('%s') fails"), Params), Commandlet->Main(Params), 1);
        }
        for (const TCHAR* Params : TimeoutErrors)
        {
            TestEqual(*FString::Printf(TEXT("Main('%s') fails"), Params), Commandlet->Main(Params), 1);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotPackageManagerPlannedInstallTest, "UBotCore.PackageManager.PlannedPackagesAreNotInstalled",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotPackageManagerPlannedInstallTest::RunTest(const FString& Parameters)
{
    using namespace UBotCoreEditorTestsPrivate;

    FUBotPackageInfo PlannedSensor = MakeIndexOnlyPackage(TEXT("UBotSensor"), TEXT("https://github.com/AstreoX/uBotSensor.git"));
    PlannedSensor.bPlanned = true;
    PlannedSensor.IndexVersions.Reset();
    PlannedSensor.AvailableVersions.Reset();
    AddIndexVersion(PlannedSensor, TEXT("0.1.0"), TEXT(""), /*bPlanned*/ true);
    const TArray<FUBotPackageInfo> Packages = MakeChain(PlannedSensor, MakeIndexOnlyPackage(TEXT("UBotROS"), TEXT("https://github.com/AstreoX/uBotROS.git")));

    TArray<FString> ToInstall;
    TArray<FString> Messages;
    TestFalse(TEXT("A planned dependency blocks the install"), FUBotPackageService::PlanInstall(Packages, TEXT("UBotROS"), ToInstall, Messages));
    TestEqual(TEXT("No partial plan"), ToInstall.Num(), 0);
    TestTrue(TEXT("The planned package is named"), MessagesMention(Messages, TEXT("Cannot install UBotSensor: it is planned")));

    Messages.Reset();
    TestFalse(TEXT("A planned package cannot be installed"), FUBotPackageService::PlanInstall(Packages, TEXT("UBotSensor"), ToInstall, Messages));
    TestTrue(TEXT("The refusal says why"), MessagesMention(Messages, TEXT("planned")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotPackageManagerInstallVersionTest, "UBotCore.PackageManager.InstallVersion",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotPackageManagerInstallVersionTest::RunTest(const FString& Parameters)
{
    using namespace UBotCoreEditorTestsPrivate;

    // Versions in no particular order, as a hand-built entry may list them; a planned one is the highest.
    FUBotPackageInfo Sensor = MakeIndexOnlyPackage(TEXT("UBotSensor"), TEXT("https://github.com/AstreoX/uBotSensor.git"));
    Sensor.IndexVersions.Reset();
    Sensor.AvailableVersions.Reset();
    AddIndexVersion(Sensor, TEXT("0.2.0"), TEXT("v0.2.0"));
    AddIndexVersion(Sensor, TEXT("1.0.0"), TEXT(""), /*bPlanned*/ true);
    AddIndexVersion(Sensor, TEXT("0.10.0"), TEXT("v0.10.0"));
    AddIndexVersion(Sensor, TEXT("0.9.1"), TEXT("0123abc"));

    FString Reason;
    {
        const FUBotPackageIndexVersion* Newest = FUBotPackageService::FindInstallVersion(Sensor, FString(), Reason);
        if (TestNotNull(TEXT("The newest installable version is chosen"), Newest))
        {
            TestEqualSensitive(TEXT("Version by SemVer precedence, not by text or position"), *Newest->Version, TEXT("0.10.0"));
            TestEqualSensitive(TEXT("Its tag is what gets cloned"), *Newest->Ref, TEXT("v0.10.0"));
        }
    }
    {
        const FUBotPackageIndexVersion* Exact = FUBotPackageService::FindInstallVersion(Sensor, TEXT("0.9.1"), Reason);
        if (TestNotNull(TEXT("A named version is chosen"), Exact))
        {
            TestEqualSensitive(TEXT("Its ref is a commit id here"), *Exact->Ref, TEXT("0123abc"));
        }
        const FUBotPackageIndexVersion* Prefixed = FUBotPackageService::FindInstallVersion(Sensor, TEXT(" v0.2.0 "), Reason);
        if (TestNotNull(TEXT("The version is compared as a semantic version"), Prefixed))
        {
            TestEqualSensitive(TEXT("v0.2.0 finds 0.2.0"), *Prefixed->Version, TEXT("0.2.0"));
        }
    }

    TestNull(TEXT("A planned version is refused"), FUBotPackageService::FindInstallVersion(Sensor, TEXT("1.0.0"), Reason));
    TestTrue(TEXT("The refusal says planned"), Reason.Contains(TEXT("planned")));
    TestNull(TEXT("An unknown version is refused"), FUBotPackageService::FindInstallVersion(Sensor, TEXT("0.3.0"), Reason));
    TestTrue(TEXT("The refusal names the installable versions"), Reason.Contains(TEXT("0.3.0")) && Reason.Contains(TEXT("0.10.0")) && !Reason.Contains(TEXT("1.0.0")));
    TestNull(TEXT("Text that is no version is refused"), FUBotPackageService::FindInstallVersion(Sensor, TEXT("latest"), Reason));
    TestTrue(TEXT("The refusal says why"), Reason.Contains(TEXT("semantic version")));

    // Refs git must never see.
    for (const TCHAR* Ref : { TEXT(""), TEXT("--upload-pack=calc"), TEXT("-x"), TEXT("v1 .0"), TEXT("v1\t0"), TEXT("a..b"), TEXT("v1\"0"), TEXT("a:b"), TEXT("a~1"), TEXT("a^") })
    {
        FUBotPackageInfo Bad = MakeIndexOnlyPackage(TEXT("UBotSensor"), TEXT("https://github.com/AstreoX/uBotSensor.git"));
        Bad.IndexVersions[0].Ref = Ref;
        TestNull(*FString::Printf(TEXT("Ref '%s' is refused"), Ref), FUBotPackageService::FindInstallVersion(Bad, FString(), Reason));
        TestTrue(*FString::Printf(TEXT("Ref '%s' is explained"), Ref), Reason.Contains(TEXT("ref")));
    }

    FUBotPackageInfo NoVersions = MakeIndexOnlyPackage(TEXT("UBotSensor"), TEXT("https://github.com/AstreoX/uBotSensor.git"));
    NoVersions.IndexVersions.Reset();
    TestNull(TEXT("An entry without versions has nothing to install"), FUBotPackageService::FindInstallVersion(NoVersions, FString(), Reason));

    // PlanInstall: the requested version applies to the requested package only.
    {
        const FUBotPackageInfo Ros = MakeIndexOnlyPackage(TEXT("UBotROS"), TEXT("https://github.com/AstreoX/uBotROS.git"));
        const TArray<FUBotPackageInfo> Packages = MakeChain(Sensor, Ros);

        TArray<FString> ToInstall;
        TArray<FString> Messages;
        TestTrue(TEXT("The newest versions install"), FUBotPackageService::PlanInstall(Packages, TEXT("UBotROS"), ToInstall, Messages));

        // UBotROS has only 0.1.0; asking for another version of it fails and names the package.
        Messages.Reset();
        TestFalse(TEXT("An unknown version of the target blocks the plan"),
            FUBotPackageService::PlanInstall(Packages, TEXT("UBotROS"), ToInstall, Messages, TEXT("0.2.0")));
        TestEqual(TEXT("No partial plan"), ToInstall.Num(), 0);
        TestTrue(TEXT("The message names ROS and the version"), MessagesMention(Messages, TEXT("Cannot install UBotROS")) && MessagesMention(Messages, TEXT("0.2.0")));

        // The same request does not leak into the dependency: UBotSensor still gets its newest version.
        Messages.Reset();
        TestTrue(TEXT("The requested version exists"), FUBotPackageService::PlanInstall(Packages, TEXT("UBotROS"), ToInstall, Messages, TEXT("0.1.0")));
        TestNames(*this, TEXT("Dependencies first"), ToInstall, { TEXT("UBotSensor"), TEXT("UBotROS") });

        // Installing the sensor at a planned version is refused, at an installable one it works.
        Messages.Reset();
        TestFalse(TEXT("A planned version blocks the plan"), FUBotPackageService::PlanInstall(Packages, TEXT("UBotSensor"), ToInstall, Messages, TEXT("1.0.0")));
        TestTrue(TEXT("The refusal says planned"), MessagesMention(Messages, TEXT("planned")));
        Messages.Reset();
        TestTrue(TEXT("An older installable version works"), FUBotPackageService::PlanInstall(Packages, TEXT("UBotSensor"), ToInstall, Messages, TEXT("0.2.0")));
    }

    // An entry that lists no ref for its version blocks the install.
    {
        FUBotPackageInfo NoRef = MakeIndexOnlyPackage(TEXT("UBotSensor"), TEXT("https://github.com/AstreoX/uBotSensor.git"));
        NoRef.IndexVersions[0].Ref.Reset();
        const TArray<FUBotPackageInfo> Packages = MakeChain(NoRef, MakeIndexOnlyPackage(TEXT("UBotROS"), TEXT("https://github.com/AstreoX/uBotROS.git")));
        TArray<FString> ToInstall;
        TArray<FString> Messages;
        TestFalse(TEXT("A version without a ref blocks the plan"), FUBotPackageService::PlanInstall(Packages, TEXT("UBotROS"), ToInstall, Messages));
        TestTrue(TEXT("The message names the package and the ref"), MessagesMention(Messages, TEXT("UBotSensor")) && MessagesMention(Messages, TEXT("\"ref\"")));
    }

    // The install folder: the index "folder", else the repository name; one safe segment either way.
    {
        FUBotPackageInfo Package = MakeIndexOnlyPackage(TEXT("UBotSensor"), TEXT("https://github.com/AstreoX/uBotSensor-fork.git"));
        TestEqualSensitive(TEXT("The folder follows the repository"), *FUBotPackageService::ResolveInstallFolder(Package, &Reason), TEXT("uBotSensor-fork"));

        Package.InstallFolder = TEXT("uBotSensor");
        TestEqualSensitive(TEXT("The index folder wins"), *FUBotPackageService::ResolveInstallFolder(Package, &Reason), TEXT("uBotSensor"));

        Package.InstallFolder = TEXT("  uBot_Sensor.v2  ");
        TestEqualSensitive(TEXT("Surrounding whitespace is ignored"), *FUBotPackageService::ResolveInstallFolder(Package, &Reason), TEXT("uBot_Sensor.v2"));

        for (const TCHAR* Folder : { TEXT(".."), TEXT("."), TEXT("../Escape"), TEXT("a/b"), TEXT("a\\b"), TEXT("C:"), TEXT("a b"), TEXT("a*"), TEXT("a\"b") })
        {
            Package.InstallFolder = Folder;
            TestTrue(*FString::Printf(TEXT("Folder '%s' is refused"), Folder), FUBotPackageService::ResolveInstallFolder(Package, &Reason).IsEmpty());
            TestTrue(*FString::Printf(TEXT("Folder '%s' is explained"), Folder), Reason.Contains(TEXT("folder")));
        }

        // A folder derived from the repository must be safe as well.
        FUBotPackageInfo Plus = MakeIndexOnlyPackage(TEXT("UBotSensor"), TEXT("https://example.com/team/uBot+Sonar.git"));
        TestTrue(TEXT("An unsafe derived folder is refused"), FUBotPackageService::ResolveInstallFolder(Plus, &Reason).IsEmpty());
        TestTrue(TEXT("The refusal points at the index folder"), Reason.Contains(TEXT("\"folder\"")));
        Plus.InstallFolder = TEXT("uBotSonar");
        TestEqualSensitive(TEXT("Setting the index folder fixes it"), *FUBotPackageService::ResolveInstallFolder(Plus, &Reason), TEXT("uBotSonar"));

        // PlanInstall refuses an unsafe index folder before anything is cloned.
        FUBotPackageInfo Escaping = MakeIndexOnlyPackage(TEXT("UBotSensor"), TEXT("https://github.com/AstreoX/uBotSensor.git"));
        Escaping.InstallFolder = TEXT("../../Source");
        const TArray<FUBotPackageInfo> Packages = MakeChain(Escaping, MakeIndexOnlyPackage(TEXT("UBotROS"), TEXT("https://github.com/AstreoX/uBotROS.git")));
        TArray<FString> ToInstall;
        TArray<FString> Messages;
        TestFalse(TEXT("An escaping folder blocks the plan"), FUBotPackageService::PlanInstall(Packages, TEXT("UBotROS"), ToInstall, Messages));
        TestEqual(TEXT("No partial plan"), ToInstall.Num(), 0);
        TestTrue(TEXT("The message names the package and the folder"), MessagesMention(Messages, TEXT("UBotSensor")) && MessagesMention(Messages, TEXT("\"folder\"")));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotPackageManagerCloneArgumentsTest, "UBotCore.PackageManager.CloneArguments",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotPackageManagerCloneArgumentsTest::RunTest(const FString& Parameters)
{
    const TCHAR* Repository = TEXT("https://example.com/team/uBotSensor.git");
    const TCHAR* Destination = TEXT("C:/Project/Plugins/uBot/uBotSensor");

    // A tag or branch is cloned directly, and "--" keeps the URL from being read as an option.
    TestEqualSensitive(TEXT("Tag"), *FUBotPackageService::MakeCloneArguments(Repository, TEXT("v0.1.0"), Destination),
        TEXT("-c advice.detachedHead=false clone --branch \"v0.1.0\" -- \"https://example.com/team/uBotSensor.git\" \"C:/Project/Plugins/uBot/uBotSensor\""));
    TestEqualSensitive(TEXT("Branch"), *FUBotPackageService::MakeCloneArguments(Repository, TEXT("release/0.1"), Destination),
        TEXT("-c advice.detachedHead=false clone --branch \"release/0.1\" -- \"https://example.com/team/uBotSensor.git\" \"C:/Project/Plugins/uBot/uBotSensor\""));

    // A commit id is not a branch or tag: "git clone --branch" rejects it, so it is checked out afterwards.
    TestEqualSensitive(TEXT("Commit id"), *FUBotPackageService::MakeCloneArguments(Repository, TEXT("0123abcdef"), Destination),
        TEXT("-c advice.detachedHead=false clone -- \"https://example.com/team/uBotSensor.git\" \"C:/Project/Plugins/uBot/uBotSensor\""));
    TestEqualSensitive(TEXT("Checkout"), *FUBotPackageService::MakeCheckoutArguments(Destination, TEXT("0123abcdef")),
        TEXT("-c advice.detachedHead=false -C \"C:/Project/Plugins/uBot/uBotSensor\" checkout --detach \"0123abcdef\" --"));

    struct FCase
    {
        const TCHAR* Ref;
        bool bCommitId;
    };
    const FCase Cases[] =
    {
        { TEXT("0123abc"), true },
        { TEXT("0123456789abcdef0123456789abcdef01234567"), true },
        { TEXT("ABCDEF1234"), true },
        { TEXT("0123ab"), false },
        { TEXT("0123456789abcdef0123456789abcdef012345678"), false },
        { TEXT("0123abg"), false },
        { TEXT("v0.1.0"), false },
        { TEXT("main"), false },
        { TEXT("release/0.1"), false },
        { TEXT(""), false },
    };
    for (const FCase& Case : Cases)
    {
        TestEqual(*FString::Printf(TEXT("IsCommitId('%s')"), Case.Ref), FUBotPackageService::IsCommitId(Case.Ref), Case.bCommitId);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotPackageManagerListOutputTest, "UBotCore.PackageManager.ListOutput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotPackageManagerListOutputTest::RunTest(const FString& Parameters)
{
    using namespace UBotCoreEditorTestsPrivate;

    // An installed package with a problem, and index entries that require things that are not installed.
    TArray<FUBotPackageInfo> View;
    View.Add(MakePackage(TEXT("UBotCore"), true, true, EUBotPackageLayer::Foundation));
    FUBotPackageInfo& Environment = View.Add_GetRef(MakePackage(TEXT("UBotEnvironment"), true, true));
    AddRequire(Environment, TEXT("UBotCore"), TEXT("^0.2.0"));
    FUBotPackageInfo& Combat = View.Add_GetRef(MakeIndexOnlyPackage(TEXT("UBotCombat"), TEXT("https://github.com/AstreoX/uBotCombat.git")));
    Combat.bPlanned = true;
    AddRequire(Combat, TEXT("UBotMissing"));
    FUBotPackageInfo& Vehicles = View.Add_GetRef(MakeIndexOnlyPackage(TEXT("UBotVehicles"), TEXT("https://github.com/AstreoX/uBotVehicles.git")));
    AddRequire(Vehicles, TEXT("UBotMissing"));
    FUBotPackageRegistry::ValidatePackageSet(View);

    // ValidatePackageSet does report the index-only entries; -List has to leave those out.
    for (const TCHAR* Name : { TEXT("UBotCombat"), TEXT("UBotVehicles") })
    {
        const FUBotPackageInfo* Validated = View.FindByPredicate([Name](const FUBotPackageInfo& Package) { return Package.Name == Name; });
        if (!TestNotNull(*FString::Printf(TEXT("%s entry"), Name), Validated)
            || !TestTrue(*FString::Printf(TEXT("The validated index-only entry %s has a problem"), Name), Validated->Problems.Num() > 0))
        {
            return false;
        }
    }

    const TArray<FString> Lines = UUBotPackageCommandlet::FormatPackageList(View, FUBotPackageService::Get());
    auto FindLine = [&Lines](const TCHAR* Text)
    {
        return Lines.FindByPredicate([Text](const FString& Line) { return Line.Contains(Text); });
    };

    const FString* EnvironmentRow = FindLine(TEXT("UBotEnvironment  "));
    if (TestNotNull(TEXT("Installed package row"), EnvironmentRow))
    {
        TestTrue(TEXT("An installed package counts its problem"), EnvironmentRow->EndsWith(TEXT("1")));
    }
    const FString* CombatRow = FindLine(TEXT("UBotCombat  "));
    if (TestNotNull(TEXT("Planned package row"), CombatRow))
    {
        TestTrue(TEXT("The planned package is listed as planned"), CombatRow->Contains(TEXT("Planned")));
        TestTrue(TEXT("A planned package reports no problems"), CombatRow->EndsWith(TEXT("0")));
    }
    const FString* VehiclesRow = FindLine(TEXT("UBotVehicles  "));
    if (TestNotNull(TEXT("Not installed package row"), VehiclesRow))
    {
        TestTrue(TEXT("A package that is not installed reports no problems"), VehiclesRow->EndsWith(TEXT("0")));
    }

    TestNotNull(TEXT("The installed package's problem is listed"), FindLine(TEXT("  UBotEnvironment: ")));
    TestNull(TEXT("No problem line for the planned package"), FindLine(TEXT("  UBotCombat: ")));
    TestNull(TEXT("No problem line for the package that is not installed"), FindLine(TEXT("  UBotVehicles: ")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotOpenPanelCommandTest, "UBotCore.Panel.OpenPanelCommand",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotOpenPanelCommandTest::RunTest(const FString& Parameters)
{
    // Headless scripts open the panel with -ExecCmds="uBot.OpenPanel".
    TestEqualSensitive(TEXT("Command name"), FUBotCoreEditorModule::OpenPanelCommandName, TEXT("uBot.OpenPanel"));

    IConsoleObject* Object = IConsoleManager::Get().FindConsoleObject(FUBotCoreEditorModule::OpenPanelCommandName);
    if (TestNotNull(TEXT("The module registers uBot.OpenPanel"), Object))
    {
        TestNotNull(TEXT("It is a console command"), Object->AsCommand());
        TestFalse(TEXT("It has a help text"), FString(Object->GetHelp()).IsEmpty());
    }

    // Without Slate there is no panel; the command says so instead of failing. With Slate it would open a
    // tab in the editor running the tests, so it is not executed there.
    if (!FSlateApplication::IsInitialized())
    {
        AddExpectedErrorPlain(TEXT("there is no panel to open"), EAutomationExpectedErrorFlags::Contains, 1);
        TestTrue(TEXT("The command is handled"), IConsoleManager::Get().ProcessUserConsoleInput(FUBotCoreEditorModule::OpenPanelCommandName, *GLog, nullptr));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotPanelIconsTest, "UBotCore.Panel.Icons",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotPanelIconsTest::RunTest(const FString& Parameters)
{
    const TSharedPtr<IPlugin> CorePlugin = IPluginManager::Get().FindPlugin(TEXT("UBotCore"));
    if (!TestTrue(TEXT("UBotCore plugin is discovered"), CorePlugin.IsValid()))
    {
        return false;
    }
    const FString ResourcesDir = FPaths::Combine(CorePlugin->GetBaseDir(), TEXT("Resources"));

    // The Plugins browser icon: a 128x128 PNG (signature, then width and height big-endian in IHDR).
    TArray<uint8> Png;
    if (TestTrue(TEXT("Resources/Icon128.png loads"), FFileHelper::LoadFileToArray(Png, *FPaths::Combine(ResourcesDir, TEXT("Icon128.png")), FILEREAD_Silent))
        && TestTrue(TEXT("Icon128.png has an IHDR"), Png.Num() >= 24))
    {
        static const uint8 PngSignature[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
        TestTrue(TEXT("Icon128.png is a PNG"), FMemory::Memcmp(Png.GetData(), PngSignature, sizeof(PngSignature)) == 0);
        const auto ReadBigEndian = [&Png](int32 Offset)
        {
            return int32((uint32(Png[Offset]) << 24) | (uint32(Png[Offset + 1]) << 16) | (uint32(Png[Offset + 2]) << 8) | uint32(Png[Offset + 3]));
        };
        TestEqual(TEXT("Icon128.png width"), ReadBigEndian(16), 128);
        TestEqual(TEXT("Icon128.png height"), ReadBigEndian(20), 128);
    }

    // The tab icon: the mark centred in a square viewBox, so the 16x16 brush does not squash it.
    const FString SvgPath = FPaths::Combine(ResourcesDir, TEXT("Icons"), TEXT("UBotTab.svg"));
    FString Svg;
    FString ViewBox;
    if (TestTrue(TEXT("Resources/Icons/UBotTab.svg loads"), FFileHelper::LoadFileToString(Svg, *SvgPath))
        && TestTrue(TEXT("UBotTab.svg has a viewBox"), FParse::Value(*Svg, TEXT("viewBox="), ViewBox, /*bShouldStopOnSeparator*/ false)))
    {
        TArray<FString> Numbers;
        ViewBox.ParseIntoArrayWS(Numbers);
        if (TestEqual(TEXT("viewBox has four numbers"), Numbers.Num(), 4))
        {
            TestEqual(TEXT("viewBox is square"), FCString::Atod(*Numbers[2]), FCString::Atod(*Numbers[3]));
        }
    }

    // The style set exists only with the editor UI. Say so in the report when the rest is skipped.
    if (!FSlateApplication::IsInitialized())
    {
        AddInfo(TEXT("The editor UI is not running: the style set, tab, Window menu and status bar checks were skipped."));
        return true;
    }

    const ISlateStyle* Style = FSlateStyleRegistry::FindSlateStyle(FUBotEditorStyle::StyleSetName);
    if (TestNotNull(TEXT("UBotEditorStyle is registered"), Style))
    {
        const FSlateBrush* Brush = Style->GetOptionalBrush(FUBotEditorStyle::TabIconName, nullptr, nullptr);
        if (TestNotNull(TEXT("UBot.TabIcon exists"), Brush))
        {
            TestTrue(TEXT("UBot.TabIcon is Resources/Icons/UBotTab.svg"), FPaths::IsSamePath(Brush->GetResourceName().ToString(), SvgPath));
            TestTrue(TEXT("UBot.TabIcon is a vector image"), Brush->GetImageType() == ESlateBrushImageType::Vector);
            const FVector2f ImageSize = Brush->GetImageSize();
            TestEqual(TEXT("UBot.TabIcon width"), ImageSize.X, 16.0f);
            TestEqual(TEXT("UBot.TabIcon height"), ImageSize.Y, 16.0f);
        }
    }

    const auto IsTabIcon = [](const FSlateIcon& Icon)
    {
        return Icon.GetStyleSetName() == FUBotEditorStyle::StyleSetName && Icon.GetStyleName() == FUBotEditorStyle::TabIconName;
    };

    const TSharedPtr<FTabSpawnerEntry> Spawner = FGlobalTabmanager::Get()->FindTabSpawnerFor(FUBotCoreEditorModule::PanelTabName);
    if (TestTrue(TEXT("The uBot tab spawner is registered"), Spawner.IsValid()))
    {
        TestTrue(TEXT("The uBot tab shows the uBot mark"), IsTabIcon(Spawner->GetIcon()));
    }

    // With the editor UI the module has registered both entries, so a missing menu is a failure.
    UToolMenus* ToolMenus = UToolMenus::Get();
    if (!TestNotNull(TEXT("UToolMenus is available with the editor UI"), ToolMenus))
    {
        return false;
    }

    UToolMenu* WindowMenu = ToolMenus->FindMenu("LevelEditor.MainMenu.Window");
    if (TestNotNull(TEXT("LevelEditor.MainMenu.Window exists"), WindowMenu))
    {
        const FToolMenuSection* Section = WindowMenu->FindSection("uBot");
        const FToolMenuEntry* Entry = Section != nullptr ? Section->FindEntry("OpenUBotPanel") : nullptr;
        if (TestNotNull(TEXT("Window > uBot entry"), Entry))
        {
            TestTrue(TEXT("Window > uBot shows the uBot mark"), IsTabIcon(Entry->Icon.Get()));
        }
    }

    // The status bar entry draws the mark next to its text. It must look the brush up whenever it paints and
    // not keep the brush pointer of the style set, which Unregister() frees.
    UToolMenu* StatusBarMenu = ToolMenus->FindMenu("LevelEditor.StatusBar.ToolBar");
    if (TestNotNull(TEXT("LevelEditor.StatusBar.ToolBar exists"), StatusBarMenu))
    {
        const FToolMenuSection* Section = StatusBarMenu->FindSection("uBot");
        const FToolMenuEntry* Entry = Section != nullptr ? Section->FindEntry("UBotStatus") : nullptr;
        if (TestNotNull(TEXT("Status bar uBot entry"), Entry) && TestTrue(TEXT("The status bar entry makes its widget"), Entry->MakeCustomWidget.IsBound()))
        {
            const TSharedRef<SWidget> StatusWidget = Entry->MakeCustomWidget.Execute(FToolMenuContext(), FToolMenuCustomWidgetContext());
            const TSharedPtr<SWidget> ImageWidget = UBotCoreEditorTestsPrivate::FindWidgetOfType(StatusWidget, TEXT("SImage"));
            if (TestTrue(TEXT("The status bar entry shows an image"), ImageWidget.IsValid()))
            {
                // Its colour is a plain value, so the one attribute that can be bound to a getter is its brush.
                TestTrue(TEXT("The status bar image gets its brush from a getter, not from a brush pointer kept once"), ImageWidget->HasRegisteredSlateAttribute());
            }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotPackagedReadmeImagesTest, "UBotCore.Package.ReadmeImages",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotPackagedReadmeImagesTest::RunTest(const FString& Parameters)
{
    using namespace UBotCoreEditorTestsPrivate;

    // UAT's BuildPlugin (the Plugins browser's Package action, or RunUAT BuildPlugin for a distribution) stages
    // Resources/ and Source/, but docs/ only when Config/FilterPlugin.ini lists it. The README.md links its
    // logo relative to the plugin root, so the packaged README shows a broken image unless the logo is staged.
    int32 PluginsChecked = 0;
    const TCHAR* const PluginNames[] = { TEXT("UBotCore"), TEXT("UBotSensor"), TEXT("UBotROS") };
    for (const TCHAR* PluginName : PluginNames)
    {
        const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(PluginName);
        if (!Plugin.IsValid())
        {
            AddInfo(FString::Printf(TEXT("%s is not part of this project, so its README is not checked."), PluginName));
            continue;
        }
        ++PluginsChecked;

        const FString BaseDir = Plugin->GetBaseDir();
        FString Readme;
        if (!TestTrue(*FString::Printf(TEXT("%s has a README.md"), PluginName), FFileHelper::LoadFileToString(Readme, *FPaths::Combine(BaseDir, TEXT("README.md")))))
        {
            continue;
        }

        TArray<FString> FilterLines;
        FFileHelper::LoadFileToStringArray(FilterLines, *FPaths::Combine(BaseDir, TEXT("Config"), TEXT("FilterPlugin.ini")));
        TestTrue(*FString::Printf(TEXT("%s stages its README.md"), PluginName), IsStagedByBuildPlugin(TEXT("README.md"), FilterLines));

        const TArray<FString> ImagePaths = GetReadmeImagePaths(Readme);
        TestTrue(*FString::Printf(TEXT("The README.md of %s shows an image"), PluginName), ImagePaths.Num() > 0);
        for (const FString& ImagePath : ImagePaths)
        {
            TestTrue(*FString::Printf(TEXT("%s: README image %s exists"), PluginName, *ImagePath), FPaths::FileExists(FPaths::Combine(BaseDir, ImagePath)));
            TestTrue(*FString::Printf(TEXT("%s: README image %s is staged by BuildPlugin"), PluginName, *ImagePath), IsStagedByBuildPlugin(ImagePath, FilterLines));
        }
    }

    // The staging rules themselves, on what the READMEs rely on.
    const TArray<FString> Rules = { TEXT("[FilterPlugin]"), TEXT("; comment"), TEXT("/README.md"), TEXT("/docs/brand/..."), TEXT("-/docs/brand/secret.svg") };
    TestTrue(TEXT("A listed file is staged"), IsStagedByBuildPlugin(TEXT("README.md"), Rules));
    TestTrue(TEXT("A file below a listed folder is staged"), IsStagedByBuildPlugin(TEXT("docs/brand/ubot-mark-dark.svg"), Rules));
    TestFalse(TEXT("An excluded file is not staged"), IsStagedByBuildPlugin(TEXT("docs/brand/secret.svg"), Rules));
    TestFalse(TEXT("A sibling folder is not staged"), IsStagedByBuildPlugin(TEXT("docs/PROTOCOL.md"), Rules));
    TestFalse(TEXT("docs/ is not staged by default"), IsStagedByBuildPlugin(TEXT("docs/brand/ubot-mark-dark.svg"), TArray<FString>{ TEXT("[FilterPlugin]"), TEXT("/README.md") }));
    TestTrue(TEXT("Resources/ is staged by default"), IsStagedByBuildPlugin(TEXT("Resources/Icons/UBotTab.svg"), TArray<FString>()));
    TestFalse(TEXT("A rule outside [FilterPlugin] does not count"), IsStagedByBuildPlugin(TEXT("README.md"), TArray<FString>{ TEXT("[Other]"), TEXT("/README.md") }));

    TestTrue(TEXT("At least UBotCore was checked"), PluginsChecked > 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotPanelPackageRowsTest, "UBotCore.Panel.PackageRows",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotPanelPackageRowsTest::RunTest(const FString& Parameters)
{
    using namespace UBotCoreEditorTestsPrivate;

    TArray<FUBotPackageInfo> View;
    FUBotPackageInfo& Ros = View.Add_GetRef(MakePackage(TEXT("UBotROS"), true, true, EUBotPackageLayer::Adapter));
    Ros.AvailableVersions = { TEXT("0.1.0") };
    FUBotPackageInfo& Sensor = View.Add_GetRef(MakePackage(TEXT("UBotSensor"), true, true, EUBotPackageLayer::Capability));
    Sensor.AvailableVersions = { TEXT("0.1.1"), TEXT("0.1.0") };
    FUBotPackageInfo& Core = View.Add_GetRef(MakePackage(TEXT("UBotCore"), true, true, EUBotPackageLayer::Foundation));
    Core.AvailableVersions = { TEXT("0.1.0-rc.1") };
    FUBotPackageInfo& Broken = View.Add_GetRef(MakePackage(TEXT("UBotEnvironment"), true, true, EUBotPackageLayer::Capability));
    Broken.Version = TEXT("0.2.0");
    Broken.Problems.Add(TEXT("Requires 'UBotCore' ^0.2.0, but version 0.1.0 is installed."));
    View.Add(MakePackage(TEXT("UBotCombat"), true, false, EUBotPackageLayer::Content));
    View.Add(MakeIndexOnlyPackage(TEXT("UBotVehicles"), TEXT("https://github.com/AstreoX/uBotVehicles.git")));

    const TArray<FUBotPanelRow> Rows = FUBotPanelModel::MakePackageRows(View);
    TArray<FString> Names;
    for (const FUBotPanelRow& Row : Rows)
    {
        Names.Add(Row.Name.ToString());
    }
    TestNames(*this, TEXT("Enabled installed packages in layer order, then by name"), Names,
        { TEXT("UBotCore"), TEXT("UBotEnvironment"), TEXT("UBotSensor"), TEXT("UBotROS") });

    if (Rows.Num() == 4)
    {
        TestEqual(TEXT("Plain version"), Rows[0].Value.BuildSourceString(), FString(TEXT("0.1.0")));
        TestFalse(TEXT("An older prerelease is no update"), Rows[0].bHighlightValue);
        TestEqual(TEXT("Clean package dot"), Rows[0].Dot, EUBotRuntimeSeverity::Ok);
        TestEqual(TEXT("Package with problems"), Rows[1].Dot, EUBotRuntimeSeverity::Warning);
        TestEqual(TEXT("Update available"), Rows[2].Value.BuildSourceString(), FString(TEXT("0.1.0 · 0.1.1 available")));
        TestTrue(TEXT("Update is highlighted"), Rows[2].bHighlightValue);
        TestFalse(TEXT("Up to date"), Rows[3].bHighlightValue);
    }

    TestEqual(TEXT("Newer version"), FUBotPanelModel::FindUpdate(TEXT("0.1.0"), { TEXT("0.2.0"), TEXT("0.1.0") }), FString(TEXT("0.2.0")));
    TestTrue(TEXT("Same version"), FUBotPanelModel::FindUpdate(TEXT("0.2.0"), { TEXT("0.2.0") }).IsEmpty());
    TestTrue(TEXT("Installed newer than the index"), FUBotPanelModel::FindUpdate(TEXT("0.3.0-dev"), { TEXT("0.2.0") }).IsEmpty());
    TestTrue(TEXT("Nothing in the index"), FUBotPanelModel::FindUpdate(TEXT("0.1.0"), {}).IsEmpty());
    TestTrue(TEXT("Unparsable installed version"), FUBotPanelModel::FindUpdate(TEXT("dev"), { TEXT("0.2.0") }).IsEmpty());

    const TArray<FUBotPanelRow> Points = FUBotPanelModel::MakeExtensionPointRows({
        TPair<FName, int32>(FName(TEXT("UBotROS.SensorPublisher")), 7), TPair<FName, int32>(FName(TEXT("Other.Point")), 1) });
    if (TestEqual(TEXT("Two extension points"), Points.Num(), 2))
    {
        TestEqual(TEXT("Point id"), Points[0].Name.ToString(), FString(TEXT("UBotROS.SensorPublisher")));
        TestTrue(TEXT("Implementation count"), Points[0].Value.BuildSourceString().StartsWith(TEXT("7 implementation")));
        TestTrue(TEXT("Singular count"), Points[1].Value.BuildSourceString().StartsWith(TEXT("1 implementation")));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotPanelProblemRowsTest, "UBotCore.Panel.ProblemRows",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotPanelProblemRowsTest::RunTest(const FString& Parameters)
{
    using namespace UBotCoreEditorTestsPrivate;

    TArray<FUBotPackageInfo> Installed;
    Installed.Add(MakePackage(TEXT("UBotCore"), true, true, EUBotPackageLayer::Foundation));
    FUBotPackageInfo& Environment = Installed.Add_GetRef(MakePackage(TEXT("UBotEnvironment"), true, true));
    AddRequire(Environment, TEXT("UBotCore"), TEXT("^0.2.0"));
    AddRequire(Environment, TEXT("UBotMaps"), TEXT(""));
    FUBotPackageInfo& Combat = Installed.Add_GetRef(MakePackage(TEXT("UBotCombat"), true, true, EUBotPackageLayer::Content));
    AddRequire(Combat, TEXT("UBotSensor"), TEXT("^0.1.0"));
    Installed.Add(MakePackage(TEXT("UBotSensor"), true, false));
    FUBotPackageInfo& Disabled = Installed.Add_GetRef(MakePackage(TEXT("UBotDisabled"), true, false));
    AddRequire(Disabled, TEXT("UBotNowhere"), TEXT("^1.0"));
    FUBotPackageRegistry::ValidatePackageSet(Installed);

    TArray<FUBotPackageReplacement> Replacements;
    Replacements.Add({ TEXT("AgentSensorCore"), TEXT("UBotSensor"), TEXT("Config/DefaultUBotSensor.ini") });
    Replacements.Add({ TEXT("AgentCombatCore"), TEXT("UBotCombat"), FString() });

    FUBotRuntimeProblem Runtime;
    Runtime.Id = TEXT("ros.bridge");
    Runtime.Package = TEXT("UBotROS");
    Runtime.Severity = EUBotRuntimeSeverity::Info;
    Runtime.Code = TEXT("rosBridgeUnreachable");
    Runtime.Message = FText::FromString(TEXT("Could not connect to ubot_ros_bridge at 127.0.0.1:8268."));

    const TArray<FUBotPanelRow> Rows = FUBotPanelModel::MakeProblemRows(Installed, { TEXT("uBot package 'X' (X.uplugin): bad layer") }, Replacements,
        [](const FString& Plugin) { return Plugin == TEXT("AgentSensorCore") || Plugin == TEXT("UBotSensor") || Plugin == TEXT("UBotCombat"); },
        { Runtime }, { TEXT("Cannot read package index 'x.json'.") });

    TArray<FString> Names;
    for (const FUBotPanelRow& Row : Rows)
    {
        Names.Add(Row.Name.BuildSourceString());
    }
    TestNames(*this, TEXT("Problem rows"), Names, {
        TEXT("UBotEnvironment requires UBotCore ^0.2.0"),
        TEXT("UBotEnvironment requires UBotMaps, which is not installed"),
        TEXT("UBotCombat requires UBotSensor, which is disabled"),
        TEXT("uBot package 'X' (X.uplugin): bad layer"),
        TEXT("AgentSensorCore and UBotSensor both enabled"),
        TEXT("Could not connect to ubot_ros_bridge at 127.0.0.1:8268."),
        TEXT("Package index: Cannot read package index 'x.json'."),
    });
    if (Rows.Num() == 7)
    {
        TestEqual(TEXT("Requirement problems are warnings"), Rows[0].Dot, EUBotRuntimeSeverity::Warning);
        TestTrue(TEXT("The tooltip has the full problem"), Rows[0].ToolTip.ToString().Contains(TEXT("version 0.1.0 is installed")));
        TestEqual(TEXT("Runtime problems keep their severity"), Rows[5].Dot, EUBotRuntimeSeverity::Info);
    }

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotPanelRuntimeRowsTest, "UBotCore.Panel.RuntimeRows",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotPanelRuntimeRowsTest::RunTest(const FString& Parameters)
{
    FUBotRuntimeItem Bridge;
    Bridge.Id = TEXT("ros.bridge");
    Bridge.Label = FText::FromString(TEXT("ROS bridge"));
    Bridge.Value = TEXT("connected");
    Bridge.ValueText = FText::FromString(TEXT("Connected"));
    Bridge.Detail = TEXT("127.0.0.1:8268");
    Bridge.Severity = EUBotRuntimeSeverity::Ok;
    FUBotRuntimeItem Plain;
    Plain.Id = TEXT("other");
    Plain.Value = TEXT("on");

    const TArray<FUBotPanelRow> Playing = FUBotPanelModel::MakeRuntimeRows({ Bridge, Plain }, 12.4812, false, 60.0, nullptr);
    if (TestEqual(TEXT("Items, sim time and fixed time step"), Playing.Num(), 4))
    {
        TestEqual(TEXT("Item label"), Playing[0].Name.ToString(), FString(TEXT("ROS bridge")));
        TestEqual(TEXT("Item value and detail"), Playing[0].Value.ToString(), FString(TEXT("Connected · 127.0.0.1:8268")));
        TestEqual(TEXT("Item severity"), Playing[0].Dot, EUBotRuntimeSeverity::Ok);
        TestEqual(TEXT("Label falls back to the id"), Playing[1].Name.ToString(), FString(TEXT("other")));
        TestEqual(TEXT("Value without display text or detail"), Playing[1].Value.ToString(), FString(TEXT("on")));
        TestEqual(TEXT("Sim time row id"), Playing[2].Id, FUBotPanelModel::SimTimeRowId);
        TestEqual(TEXT("Sim time"), Playing[2].Value.BuildSourceString(), FString(TEXT("12.48 s")));
        TestEqual(TEXT("Fixed time step off"), Playing[3].Value.BuildSourceString(), FString(TEXT("Off")));
    }

    FUBotSessionInfo Last;
    Last.Kind = EUBotSessionKind::PIE;
    Last.StartedAt = FDateTime(2026, 10, 10, 8, 0, 0);
    Last.EndedAt = FDateTime(2026, 10, 10, 8, 5, 0);
    const TArray<FUBotPanelRow> Idle = FUBotPanelModel::MakeRuntimeRows({}, TOptional<double>(), true, 60.0, &Last);
    if (TestEqual(TEXT("Fixed time step and last session"), Idle.Num(), 2))
    {
        TestEqual(TEXT("Fixed time step rate"), Idle[0].Value.BuildSourceString(), FString(TEXT("60 Hz")));
        TestTrue(TEXT("Last session kind"), Idle[1].Value.ToString().StartsWith(TEXT("PIE · ")));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotPanelLocalizationTest, "UBotCore.Panel.ChineseText",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotPanelLocalizationTest::RunTest(const FString& Parameters)
{
    namespace Text = UBot::EditorText;

    FInternationalization& I18N = FInternationalization::Get();
    FInternationalization::FCultureStateSnapshot Snapshot;
    I18N.BackupCultureState(Snapshot);
    ON_SCOPE_EXIT
    {
        I18N.RestoreCultureState(Snapshot);
    };

    // Registered again so the test does not depend on the module having started with Slate.
    Text::RegisterChineseText();

    if (!TestTrue(TEXT("Switch to zh-Hans"), I18N.SetCurrentLanguage(TEXT("zh-Hans"))))
    {
        return false;
    }
    TestEqual(TEXT("Open uBot Manager"), Text::OpenManager().ToString(), FString(TEXT("打开 uBot Manager")));
    TestEqual(TEXT("Loaded Packages keeps Package in English"), Text::SectionPackages().ToString(), FString(TEXT("已加载的 Package")));
    TestEqual(TEXT("Update hint"), FText::Format(Text::UpdateAvailable(), FText::FromString(TEXT("0.1.0")), FText::FromString(TEXT("0.1.1"))).ToString(),
        FString(TEXT("0.1.0 · 可更新到 0.1.1")));
    TestEqual(TEXT("Status bar"), FUBotPanelModel::MakeStatusBarText(3, 2).ToString(), FString(TEXT("uBot：3 个 Package · 2 个问题")));
    TestEqual(TEXT("Implementations"), FText::Format(Text::Implementations(), 7).ToString(), FString(TEXT("7 个实现")));

    TestTrue(TEXT("Switch back to English"), I18N.SetCurrentLanguage(TEXT("en")));
    TestEqual(TEXT("English status bar, singular"), FUBotPanelModel::MakeStatusBarText(1, 1).ToString(), FString(TEXT("uBot: 1 package · 1 problem")));
    TestEqual(TEXT("English status bar, plural"), FUBotPanelModel::MakeStatusBarText(3, 2).ToString(), FString(TEXT("uBot: 3 packages · 2 problems")));
    TestEqual(TEXT("English implementations"), FText::Format(Text::Implementations(), 7).ToString(), FString(TEXT("7 implementations")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotManagerLauncherTest, "UBotCore.Panel.ManagerDiscovery",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotManagerLauncherTest::RunTest(const FString& Parameters)
{
    const FString Configured = TEXT("D:/Tools/uBot/ubot-manager.exe");
    const FString Registered = TEXT("C:/Users/Me/AppData/Local/uBot Manager/ubot-manager.exe");
    const FString LocalAppData = TEXT("C:/Users/Me/AppData/Local");
    const FString Installed = TEXT("C:/Users/Me/AppData/Local/uBot Manager/ubot-manager.exe");

    auto Resolve = [&](const FString& InConfigured, const FString& InRegistered, std::initializer_list<FString> Existing)
    {
        TArray<FString> Files(Existing);
        return FUBotManagerLauncher::ResolveExecutable(InConfigured, InRegistered, LocalAppData, [&Files](const FString& Path)
        {
            return Files.ContainsByPredicate([&Path](const FString& File) { return FPaths::IsSamePath(File, Path); });
        });
    };

    TestTrue(TEXT("The setting wins"), FPaths::IsSamePath(Resolve(Configured, TEXT("D:/Other/ubot-manager.exe"), { Configured, TEXT("D:/Other/ubot-manager.exe") }), Configured));
    TestTrue(TEXT("Then the registered path"), FPaths::IsSamePath(Resolve(FString(), TEXT("D:/Other/ubot-manager.exe"), { TEXT("D:/Other/ubot-manager.exe"), Installed }), TEXT("D:/Other/ubot-manager.exe")));
    TestTrue(TEXT("A missing configured file is skipped"), FPaths::IsSamePath(Resolve(Configured, FString(), { Installed }), Installed));
    TestTrue(TEXT("Quotes around the registered value are ignored"), FPaths::IsSamePath(Resolve(FString(), TEXT("\"D:\\Other\\ubot-manager.exe\""), { TEXT("D:/Other/ubot-manager.exe") }), TEXT("D:/Other/ubot-manager.exe")));
    TestTrue(TEXT("Then LOCALAPPDATA"), FPaths::IsSamePath(Resolve(FString(), FString(), { Installed }), Installed));
    TestTrue(TEXT("Nothing found"), Resolve(Configured, Registered, {}).IsEmpty());
    TestTrue(TEXT("A relative setting is taken from the project directory"), FPaths::IsSamePath(
        Resolve(TEXT("Tools/ubot-manager.exe"), FString(), { FPaths::Combine(FPaths::ProjectDir(), TEXT("Tools/ubot-manager.exe")) }),
        FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Tools/ubot-manager.exe")))));

    const FString Arguments = FUBotManagerLauncher::MakeArguments(TEXT("C:/My Projects/Robot Sim/RobotSim.uproject"));
#if PLATFORM_WINDOWS
    TestEqual(TEXT("--project with the quoted absolute path"), Arguments, FString(TEXT("--project \"C:\\My Projects\\Robot Sim\\RobotSim.uproject\"")));
#else
    TestTrue(TEXT("--project with the quoted path"), Arguments.StartsWith(TEXT("--project \"")));
#endif
    TestEqual(TEXT("Releases page"), FString(FUBotManagerLauncher::ReleasesUrl), FString(TEXT("https://github.com/AstreoX/uBotManager/releases")));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
