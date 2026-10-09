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
    Composition,
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

    /** Semantic version: the descriptor's VersionName, or the latest known version for index entries. */
    UPROPERTY(BlueprintReadOnly, Category = "uBot|Packages")
    FString Version;

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

    /** Non-uBot plugins the package needs (e.g. a third-party UE plugin). Informational; never installed automatically. */
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
};

/** Case-insensitive, surrounding whitespace ignored. Returns Unknown when Text names no layer. */
UBOTCORE_API EUBotPackageLayer ParsePackageLayer(const FString& Text);

UBOTCORE_API const TCHAR* LexToString(EUBotPackageLayer Layer);
UBOTCORE_API const TCHAR* LexToString(EUBotPackageState State);
