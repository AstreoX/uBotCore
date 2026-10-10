#pragma once

#include "CoreMinimal.h"
#include "UBotPackageTypes.h"

class FJsonObject;

/** A legacy plugin that a uBot package replaces (package index "replacements"). */
struct UBOTCORE_API FUBotPackageReplacement
{
    /** Plugin name of the legacy plugin, e.g. "AgentSensorCore". */
    FString Legacy;

    /** Plugin name of the uBot package that replaces it, e.g. "UBotSensor". */
    FString Replacement;

    /** Redirect config inside the replacement plugin (e.g. "Config/DefaultUBotSensor.ini"); may be empty. */
    FString Redirects;
};

/**
 * Package index, format 2 (Index/index.json in the UBotCore repository): the known uBot packages,
 * including those not installed yet, and the legacy plugins they replace.
 *
 *   { "formatVersion": 2, "name": "uBot",
 *     "packages": [ { "name", "friendlyName", "description": { "en", "zh-CN" }, "layer", "repository",
 *                     "folder", "docsUrl", "tags",
 *                     "versions": [ { "version", "ref", "planned", "engine", "requires": [ { "name",
 *                                     "version", "optional" } ], "enginePlugins", "provides" } ] } ],
 *     "recipes": [ ... ], "replacements": [ { "legacy", "replacement", "redirects" } ] }
 *
 * A package entry becomes one FUBotPackageInfo with State = NotInstalled and bFromIndex = true:
 * Version is the latest non-planned version (by SemVer precedence) and AvailableVersions lists the
 * non-planned versions newest first. A package without non-planned versions is planned (bPlanned);
 * its Version is then the newest planned version. Requires, Provides and ExternalRequires (from
 * "enginePlugins") come from that same version. IndexVersions lists every version newest first with
 * its git "ref" (what FUBotPackageService clones), and InstallFolder is the entry's "folder".
 * Description is the text for the current UI language (see SelectLocalizedText). Recipes are not
 * used by Unreal and are ignored.
 */
struct UBOTCORE_API FUBotPackageIndex
{
    static constexpr int32 SupportedFormatVersion = 2;

    /**
     * Returns false when anything was wrong and describes every issue in OutError. A broken document
     * (invalid JSON, a formatVersion other than 2, no "packages" array) leaves the outputs empty; a
     * broken entry (not an object, no name, duplicate name, no usable version) is skipped while the
     * remaining entries are still returned. Malformed optional fields are reported and ignored.
     * Culture selects the description text; empty means the current UI language.
     */
    static bool ParseIndexJson(const FString& JsonText, TArray<FUBotPackageInfo>& OutPackages, FString* OutError = nullptr,
        TArray<FUBotPackageReplacement>* OutReplacements = nullptr, const FString& Culture = FString());

    /** Reads FilePath and parses it with ParseIndexJson. Error messages are prefixed with the path. */
    static bool LoadIndexFile(const FString& FilePath, TArray<FUBotPackageInfo>& OutPackages, FString* OutError = nullptr,
        TArray<FUBotPackageReplacement>* OutReplacements = nullptr);

    /**
     * Absolute paths of the index files Unreal reads, in load order: <Project>/Saved/uBot/index.json
     * (the merged index uBot Manager writes) when it exists, otherwise <UBotCore>/Index/index.json;
     * then UUBotCoreSettings::AdditionalPackageIndexFiles (absolute or project-relative).
     */
    static TArray<FString> GetConfiguredIndexFiles(TArray<FString>* OutErrors = nullptr);

    /**
     * Loads GetConfiguredIndexFiles() in order. Later files override earlier entries by name (and
     * replacements by legacy name), keeping the earlier position. Files that fail to load or parse
     * contribute what they could and add a message to OutErrors.
     */
    static TArray<FUBotPackageInfo> LoadConfiguredIndex(TArray<FString>* OutErrors = nullptr,
        TArray<FUBotPackageReplacement>* OutReplacements = nullptr);

    /**
     * One path segment of ASCII letters, digits, '.', '_' and '-' that is not "." or "..": what an
     * index "folder" must be, because it names a folder below the install directory.
     */
    static bool IsSafeFolderName(const FString& Folder);

    /**
     * A git ref (tag, branch or commit id) that git cannot read as an option and that has no
     * whitespace, control characters, quotes, "..", or any of ~ ^ : ? * [ and backslash.
     */
    static bool IsSafeGitRef(const FString& Ref);

    /**
     * Picks the text of a localized index field ({ "en": ..., "zh-CN": ... }) for Culture: Chinese
     * cultures ("zh", "zh-Hans", "zh-CN", ...) take "zh-CN" when present, everything else takes "en".
     * Falls back to "en", then to any non-empty text.
     */
    static FString SelectLocalizedText(const FJsonObject& Texts, const FString& Culture);

    /** Name of the current UI language, e.g. "en" or "zh-Hans". */
    static FString GetCurrentCulture();

    /**
     * Installed entries win; index-only entries are appended with State = NotInstalled,
     * bFromIndex = true. Installed entries that also appear in the index get bFromIndex = true,
     * AvailableVersions, IndexVersions and bPlanned from the index and keep their own data otherwise,
     * except Repository/DocsUrl/InstallFolder which are filled from the index when empty. Problems
     * are not recomputed; run FUBotPackageRegistry::ValidatePackageSet on the result.
     */
    static TArray<FUBotPackageInfo> MergeInstalledWithIndex(const TArray<FUBotPackageInfo>& Installed, const TArray<FUBotPackageInfo>& Index);
};
