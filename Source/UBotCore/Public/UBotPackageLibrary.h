#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UBotPackageTypes.h"
#include "UBotPackageLibrary.generated.h"

/** Blueprint access to the installed uBot packages and the extension registry. */
UCLASS()
class UBOTCORE_API UUBotPackageLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /** Every installed uBot package (enabled or not), sorted by name. */
    UFUNCTION(BlueprintPure, Category = "uBot|Packages", meta = (DisplayName = "Get Installed uBot Packages"))
    static TArray<FUBotPackageInfo> GetInstalledUBotPackages();

    /** Case-insensitive lookup among installed packages. */
    UFUNCTION(BlueprintPure, Category = "uBot|Packages", meta = (DisplayName = "Find uBot Package"))
    static bool FindUBotPackage(const FString& Name, FUBotPackageInfo& OutInfo);

    UFUNCTION(BlueprintPure, Category = "uBot|Packages", meta = (DisplayName = "Is uBot Package Enabled"))
    static bool IsUBotPackageEnabled(const FString& Name);

    /** Installed version, or an empty string when the package is not installed. */
    UFUNCTION(BlueprintPure, Category = "uBot|Packages", meta = (DisplayName = "Get uBot Package Version"))
    static FString GetUBotPackageVersion(const FString& Name);

    /**
     * True when the package is installed and its version satisfies Constraint (e.g. "^0.1.0").
     * False when it is not installed or either version string cannot be parsed.
     */
    UFUNCTION(BlueprintPure, Category = "uBot|Packages", meta = (DisplayName = "Is uBot Package Version Satisfied"))
    static bool IsUBotPackageVersionSatisfied(const FString& Name, const FString& Constraint);

    /** Declared extension points in declaration order. */
    UFUNCTION(BlueprintPure, Category = "uBot|Packages", meta = (DisplayName = "Get uBot Extension Point Names"))
    static TArray<FName> GetUBotExtensionPointNames();

    /** Extensions available at PointName in registration order; empty while the point is not declared. */
    UFUNCTION(BlueprintPure, Category = "uBot|Packages", meta = (DisplayName = "Get uBot Extension Names"))
    static TArray<FName> GetUBotExtensionNames(FName PointName);
};
