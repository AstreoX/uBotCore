#pragma once

#include "CoreMinimal.h"
#include "UBotPackageTypes.h"

/**
 * Local catalog of known uBot packages, used to offer packages that are not installed yet.
 *
 * Index JSON: { "FormatVersion": 1, "Packages": [ { "Name", "FriendlyName", "Description",
 *   "Layer", "Repository", "DocsUrl", "Version" (latest known), "Tags", "Provides",
 *   "Requires": [ {Name, Version, Optional} ], "ExternalRequires": [ "SomePlugin" ] } ] }
 *
 * Only "Name" is mandatory. Index entries come back with State = NotInstalled and bFromIndex = true.
 */
struct UBOTCORE_API FUBotPackageIndex
{
    /**
     * Returns false when anything was wrong and describes every issue in OutError. A broken document
     * (invalid JSON, unsupported FormatVersion, no "Packages" array) leaves OutPackages empty; a broken
     * entry (not an object, no Name, duplicate Name) is skipped, while the remaining entries are still
     * returned. Malformed optional fields are reported and ignored.
     */
    static bool ParseIndexJson(const FString& JsonText, TArray<FUBotPackageInfo>& OutPackages, FString* OutError = nullptr);

    /** Reads FilePath and parses it with ParseIndexJson. Error messages are prefixed with the path. */
    static bool LoadIndexFile(const FString& FilePath, TArray<FUBotPackageInfo>& OutPackages, FString* OutError = nullptr);

    /**
     * Core's built-in index (<UBotCore plugin dir>/Resources/PackageIndex.json) followed by
     * UUBotCoreSettings::AdditionalPackageIndexFiles (absolute or project-relative). Later files
     * override earlier entries by Name, keeping the earlier position. Files that fail to load or
     * parse contribute what they could and add a message to OutErrors.
     */
    static TArray<FUBotPackageInfo> LoadConfiguredIndex(TArray<FString>* OutErrors = nullptr);

    /**
     * Installed entries win; index-only entries are appended with State = NotInstalled,
     * bFromIndex = true. Installed entries that also appear in the index get bFromIndex = true and
     * keep their own data, except Repository/DocsUrl which are filled from the index when empty.
     * Problems are not recomputed; run FUBotPackageRegistry::ValidatePackageSet on the result.
     */
    static TArray<FUBotPackageInfo> MergeInstalledWithIndex(const TArray<FUBotPackageInfo>& Installed, const TArray<FUBotPackageInfo>& Index);
};
