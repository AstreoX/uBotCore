#include "CoreMinimal.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "UBotCoreSettings.h"
#include "UBotPackageCommandlet.h"
#include "UBotPackageRegistry.h"
#include "UBotPackageService.h"
#include "UBotPackageTypes.h"
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

    FUBotPackageInfo MakeIndexOnlyPackage(const TCHAR* Name, const TCHAR* Repository)
    {
        FUBotPackageInfo Package = MakePackage(Name, false, false);
        Package.bFromIndex = true;
        Package.Repository = Repository;
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
        TEXT("git://example.com/team/uBotLidar.git"),
        TEXT("file:///C:/Repos/uBotLocal.git"),
        TEXT("https://[::1]/team/uBotLocal.git"),
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

#endif // WITH_DEV_AUTOMATION_TESTS
