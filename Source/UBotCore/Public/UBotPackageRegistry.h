#pragma once

#include "CoreMinimal.h"
#include "UBotPackageTypes.h"

class FJsonObject;

/**
 * Installed uBot packages: every discovered plugin whose .uplugin has a top-level "UBot" object.
 *
 * The static helpers are pure functions over package sets so the editor package manager and the
 * tests can use them on synthetic data. In those helpers package names match case-insensitively
 * (like plugin names), "installed" means bInstalled or State != NotInstalled, and "enabled" means
 * bEnabled or State == Enabled. Only non-optional requirements form dependency edges.
 *
 * The engine-backed part is meant to be used from the game thread.
 */
class UBOTCORE_API FUBotPackageRegistry
{
public:
    FUBotPackageRegistry() = default;
    FUBotPackageRegistry(const FUBotPackageRegistry&) = delete;
    FUBotPackageRegistry& operator=(const FUBotPackageRegistry&) = delete;

    static FUBotPackageRegistry& Get();

    // Pure helpers (unit tested, no engine state).

    /**
     * Fills Out from a parsed .uplugin JSON object. Returns false (Out untouched) if there is no
     * "UBot" object or PluginName is empty. On success OutError receives non-fatal descriptor issues
     * such as an unknown layer or a malformed requirement, joined by "; " (empty when clean).
     * Name comes from PluginName; FriendlyName, Description, Version (VersionName) and DocsUrl
     * (DocsURL) from the native fields; the rest from the "UBot" block. State fields are left at
     * their defaults.
     */
    static bool ParseDescriptor(const FJsonObject& DescriptorJson, const FString& PluginName, FUBotPackageInfo& Out, FString* OutError = nullptr);

    /**
     * Reads the fields of a descriptor "UBot" block: Layer, Repository, DocsUrl, Tags, Provides,
     * Requires and EnginePlugins (into ExternalRequires; the older key "ExternalRequires" is still
     * accepted and merged). Absent fields leave Out untouched. Malformed fields or entries are
     * skipped and described in OutIssues; returns false when any issue was added.
     */
    static bool ParseMetadataJson(const FJsonObject& Json, FUBotPackageInfo& Out, TArray<FString>& OutIssues);

    /**
     * Recomputes Problems and ProblemDetails for every entry: missing required package (absent from
     * the set or not installed), version constraint not met, enabled package requiring a disabled
     * one, unparsable constraint, dependency cycle. Optional requirements only produce a problem
     * when the package is installed but version-incompatible (or their constraint is unparsable).
     */
    static void ValidatePackageSet(TArray<FUBotPackageInfo>& Packages);

    /**
     * Topological order of Name plus all its transitive required (non-optional) dependencies,
     * dependencies first, Name last. Dependencies only have to be in the set, not installed, so this
     * works on the merged installed + index view. Returns false on a cycle or a package missing from
     * the set (OutError says which); OutOrder is empty on failure.
     */
    static bool ResolveDependencyOrder(const TArray<FUBotPackageInfo>& Packages, const FString& Name, TArray<FString>& OutOrder, FString* OutError = nullptr);

    /**
     * Names of packages (from the set) that require Name directly or transitively through
     * non-optional requirements, never including Name itself. Ordered dependents first: a package
     * comes before every package it requires, so the result can be disabled front to back. With
     * bOnlyEnabled the result is filtered to enabled packages (the walk still passes through
     * disabled ones).
     */
    static TArray<FString> FindDependents(const TArray<FUBotPackageInfo>& Packages, const FString& Name, bool bOnlyEnabled);

    // Engine-backed API.

    /** Rescans IPluginManager::GetDiscoveredPlugins(), validates the result and broadcasts OnPackagesChanged. */
    void Refresh();

    /** Sorted by name. Problems are current as of the last Refresh or ValidateAndLog. */
    const TArray<FUBotPackageInfo>& GetInstalledPackages() const;

    /** Case-insensitive lookup; nullptr if no installed package has that name. */
    const FUBotPackageInfo* FindPackage(const FString& Name) const;

    bool IsPackageEnabled(const FString& Name) const;

    /** Descriptor problems found by the last Refresh, each naming the package and its descriptor path. */
    const TArray<FString>& GetDescriptorIssues() const;

    /**
     * Validates the installed set and logs every problem and descriptor issue as a Warning. Also
     * warns about registered extensions whose extension point was never declared; those are not
     * counted. Returns the number of package problems plus descriptor issues.
     */
    int32 ValidateAndLog();

    /** Broadcast after every Refresh. */
    FSimpleMulticastDelegate OnPackagesChanged;

private:
    TArray<FUBotPackageInfo> InstalledPackages;

    /** Descriptor parse issues from the last Refresh, already prefixed with the descriptor path. */
    TArray<FString> DescriptorIssues;
};
