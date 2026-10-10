#pragma once

#include "CoreMinimal.h"
#include "UBotPackageTypes.generated.h"

/** Architectural layer of a uBot package, from the "UBot" block of its descriptor. */
UENUM(BlueprintType)
enum class EUBotPackageLayer : uint8
{
    Unknown,
    Foundation,
    Capability,
    Adapter,
    Content
};

UENUM(BlueprintType)
enum class EUBotPackageState : uint8
{
    NotInstalled,
    Disabled,
    Enabled
};

/** What a package problem is about; see FUBotPackageRegistry::ValidatePackageSet. */
UENUM(BlueprintType)
enum class EUBotPackageProblemKind : uint8
{
    /** A required package is not installed. */
    RequiresMissing,
    /** An installed package does not satisfy a version constraint, or its version is not a semantic version. */
    RequiresVersion,
    /** An enabled package requires a disabled one. */
    RequiresDisabled,
    /** A requirement without a package name or with an unparsable constraint. */
    InvalidRequirement,
    /** The package is on a dependency cycle. */
    DependencyCycle
};

/** Structured form of one entry of FUBotPackageInfo::Problems. */
USTRUCT(BlueprintType)
struct UBOTCORE_API FUBotPackageProblem
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "uBot|Packages")
    EUBotPackageProblemKind Kind = EUBotPackageProblemKind::InvalidRequirement;

    /** The required package; empty for cycles and requirements without a name. */
    UPROPERTY(BlueprintReadOnly, Category = "uBot|Packages")
    FString Dependency;

    /** The requirement's version constraint as written; empty when any version is accepted. */
    UPROPERTY(BlueprintReadOnly, Category = "uBot|Packages")
    FString Constraint;

    /** The same text as the matching entry of FUBotPackageInfo::Problems. */
    UPROPERTY(BlueprintReadOnly, Category = "uBot|Packages")
    FString Message;
};

/** One entry of a package's "Requires" list. */
USTRUCT(BlueprintType)
struct UBOTCORE_API FUBotPackageDependency
{
    GENERATED_BODY()

    /** Plugin name of the required package, e.g. "UBotCore". */
    UPROPERTY(BlueprintReadOnly, Category = "uBot|Packages")
    FString Name;

    /** Version constraint (see FUBotVersionConstraint). Empty means any version. */
    UPROPERTY(BlueprintReadOnly, Category = "uBot|Packages")
    FString Version;

    /** Optional requirements are only checked when the package is present. */
    UPROPERTY(BlueprintReadOnly, Category = "uBot|Packages")
    bool bOptional = false;
};

/** One entry of a package's "versions" list in the package index (format 2). */
USTRUCT(BlueprintType)
struct UBOTCORE_API FUBotPackageIndexVersion
{
    GENERATED_BODY()

    /** Semantic version as written in the index, e.g. "0.1.0". */
    UPROPERTY(BlueprintReadOnly, Category = "uBot|Packages")
    FString Version;

    /**
     * Git tag, branch or commit id that is checked out to install this version. Empty for planned
     * versions, and for an entry that (wrongly) lists none; such a version cannot be installed.
     */
    UPROPERTY(BlueprintReadOnly, Category = "uBot|Packages")
    FString Ref;

    /** True when the version is announced but not installable yet. */
    UPROPERTY(BlueprintReadOnly, Category = "uBot|Packages")
    bool bPlanned = false;
};

/** Everything known about a uBot package, merged from its descriptor and the package index. */
USTRUCT(BlueprintType)
struct UBOTCORE_API FUBotPackageInfo
{
    GENERATED_BODY()

    /** Plugin name, e.g. "UBotSensor". */
    UPROPERTY(BlueprintReadOnly, Category = "uBot|Packages")
    FString Name;

    UPROPERTY(BlueprintReadOnly, Category = "uBot|Packages")
    FString FriendlyName;

    UPROPERTY(BlueprintReadOnly, Category = "uBot|Packages")
    FString Description;

    /**
     * Semantic version: the descriptor's VersionName for installed packages. For index entries the
     * latest installable version, or the newest planned version when the package is planned.
     */
    UPROPERTY(BlueprintReadOnly, Category = "uBot|Packages")
    FString Version;

    /** Installable (non-planned) versions listed in the package index, newest first. */
    UPROPERTY(BlueprintReadOnly, Category = "uBot|Packages")
    TArray<FString> AvailableVersions;

    /** True when the package index lists only planned versions: announced, but not installable yet. */
    UPROPERTY(BlueprintReadOnly, Category = "uBot|Packages")
    bool bPlanned = false;

    /**
     * Every version the package index lists (planned ones included), newest first, with the git ref
     * each installable version is cloned at. Empty for packages that are not in an index.
     */
    UPROPERTY(BlueprintReadOnly, Category = "uBot|Packages")
    TArray<FUBotPackageIndexVersion> IndexVersions;

    /**
     * "folder" of the index entry: the name of the folder below the install directory the package is
     * cloned into. Empty means the last segment of Repository without ".git". Kept as written, so
     * FUBotPackageService can refuse a value that is not one safe path segment.
     */
    UPROPERTY(BlueprintReadOnly, Category = "uBot|Packages")
    FString InstallFolder;

    UPROPERTY(BlueprintReadOnly, Category = "uBot|Packages")
    EUBotPackageLayer Layer = EUBotPackageLayer::Unknown;

    /** Git repository URL used to install the package. */
    UPROPERTY(BlueprintReadOnly, Category = "uBot|Packages")
    FString Repository;

    UPROPERTY(BlueprintReadOnly, Category = "uBot|Packages")
    FString DocsUrl;

    UPROPERTY(BlueprintReadOnly, Category = "uBot|Packages")
    TArray<FString> Tags;

    /** Capability keys offered by the package, e.g. "Sensor.IMU". */
    UPROPERTY(BlueprintReadOnly, Category = "uBot|Packages")
    TArray<FString> Provides;

    UPROPERTY(BlueprintReadOnly, Category = "uBot|Packages")
    TArray<FUBotPackageDependency> Requires;

    /**
     * Engine or other non-uBot plugins the package needs ("EnginePlugins" in descriptors and the
     * index; descriptors may still use the older key "ExternalRequires"). Never installed automatically.
     */
    UPROPERTY(BlueprintReadOnly, Category = "uBot|Packages")
    TArray<FString> ExternalRequires;

    UPROPERTY(BlueprintReadOnly, Category = "uBot|Packages")
    EUBotPackageState State = EUBotPackageState::NotInstalled;

    UPROPERTY(BlueprintReadOnly, Category = "uBot|Packages")
    bool bInstalled = false;

    UPROPERTY(BlueprintReadOnly, Category = "uBot|Packages")
    bool bEnabled = false;

    /** True when the package is listed in a package index. */
    UPROPERTY(BlueprintReadOnly, Category = "uBot|Packages")
    bool bFromIndex = false;

    /** Absolute plugin directory. Empty when not installed. */
    UPROPERTY(BlueprintReadOnly, Category = "uBot|Packages")
    FString BaseDir;

    /** Absolute path of the .uplugin file. Empty when not installed. */
    UPROPERTY(BlueprintReadOnly, Category = "uBot|Packages")
    FString DescriptorPath;

    /** Human readable problems, filled by FUBotPackageRegistry::ValidatePackageSet. */
    UPROPERTY(BlueprintReadOnly, Category = "uBot|Packages")
    TArray<FString> Problems;

    /** Structured form of Problems: same order and count. */
    UPROPERTY(BlueprintReadOnly, Category = "uBot|Packages")
    TArray<FUBotPackageProblem> ProblemDetails;
};

/** Case-insensitive, surrounding whitespace ignored. Returns Unknown when Text names no layer. */
UBOTCORE_API EUBotPackageLayer ParsePackageLayer(const FString& Text);

UBOTCORE_API const TCHAR* LexToString(EUBotPackageLayer Layer);
UBOTCORE_API const TCHAR* LexToString(EUBotPackageState State);
