#include "UBotPackageIndex.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UBotCoreSettings.h"
#include "UBotSemVer.h"

// Named rather than anonymous so unity builds cannot merge it with other files' helpers.
namespace UBot::PackageIndexPrivate
{
    struct FIndexVersion
    {
        FString Text;
        FUBotSemVer SemVer;
        bool bPlanned = false;
        FString Ref;
        TArray<FUBotPackageDependency> Requires;
        TArray<FString> EnginePlugins;
        TArray<FString> Provides;
    };

    void SetError(FString* OutError, const FString& Message)
    {
        if (OutError)
        {
            *OutError = Message;
        }
    }

    // JSON object fields are looked up case-insensitively (FJsonObject keys are FStrings). An optional
    // field may be null (uBot Manager writes absent values as null), which counts as absent.
    bool HasValue(const FJsonObject& Json, const TCHAR* Field)
    {
        const TSharedPtr<FJsonValue> Value = Json.TryGetField(Field);
        return Value.IsValid() && !Value->IsNull();
    }

    void ReadString(const FJsonObject& Json, const TCHAR* Field, FString& OutValue, TArray<FString>& OutIssues)
    {
        if (!HasValue(Json, Field))
        {
            return;
        }
        FString Value;
        if (!Json.TryGetStringField(Field, Value))
        {
            OutIssues.Add(FString::Printf(TEXT("\"%s\" is not a string."), Field));
            return;
        }
        OutValue = Value.TrimStartAndEnd();
    }

    void ReadStringArray(const FJsonObject& Json, const TCHAR* Field, TArray<FString>& OutValues, TArray<FString>& OutIssues)
    {
        if (!HasValue(Json, Field))
        {
            return;
        }
        const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
        if (!Json.TryGetArrayField(Field, Values) || Values == nullptr)
        {
            OutIssues.Add(FString::Printf(TEXT("\"%s\" is not an array."), Field));
            return;
        }

        OutValues.Reset();
        for (int32 ValueIndex = 0; ValueIndex < Values->Num(); ++ValueIndex)
        {
            const TSharedPtr<FJsonValue>& Value = (*Values)[ValueIndex];
            FString Item;
            if (!Value.IsValid() || !Value->TryGetString(Item) || Item.TrimStartAndEnd().IsEmpty())
            {
                OutIssues.Add(FString::Printf(TEXT("\"%s\"[%d] is not a non-empty string."), Field, ValueIndex));
                continue;
            }
            OutValues.AddUnique(Item.TrimStartAndEnd());
        }
    }

    bool ReadBool(const FJsonObject& Json, const TCHAR* Field, bool& OutValue, TArray<FString>& OutIssues)
    {
        if (!HasValue(Json, Field))
        {
            return true;
        }
        if (!Json.TryGetBoolField(Field, OutValue))
        {
            OutIssues.Add(FString::Printf(TEXT("\"%s\" is not a boolean."), Field));
            return false;
        }
        return true;
    }

    void ReadRequires(const FJsonObject& Json, TArray<FUBotPackageDependency>& OutRequires, TArray<FString>& OutIssues)
    {
        if (!HasValue(Json, TEXT("requires")))
        {
            return;
        }
        const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
        if (!Json.TryGetArrayField(TEXT("requires"), Values) || Values == nullptr)
        {
            OutIssues.Add(TEXT("\"requires\" is not an array."));
            return;
        }

        for (int32 ValueIndex = 0; ValueIndex < Values->Num(); ++ValueIndex)
        {
            const TSharedPtr<FJsonValue>& Value = (*Values)[ValueIndex];
            const TSharedPtr<FJsonObject>* Entry = nullptr;
            if (!Value.IsValid() || !Value->TryGetObject(Entry) || Entry == nullptr || !Entry->IsValid())
            {
                OutIssues.Add(FString::Printf(TEXT("\"requires\"[%d] is not an object."), ValueIndex));
                continue;
            }

            FUBotPackageDependency Dependency;
            TArray<FString> EntryIssues;
            ReadString(**Entry, TEXT("name"), Dependency.Name, EntryIssues);
            ReadString(**Entry, TEXT("version"), Dependency.Version, EntryIssues);
            ReadBool(**Entry, TEXT("optional"), Dependency.bOptional, EntryIssues);
            if (Dependency.Name.IsEmpty())
            {
                EntryIssues.Add(TEXT("it has no \"name\"."));
            }

            if (!EntryIssues.IsEmpty())
            {
                OutIssues.Add(FString::Printf(TEXT("\"requires\"[%d] was skipped: %s"), ValueIndex, *FString::Join(EntryIssues, TEXT(" "))));
                continue;
            }
            OutRequires.Add(MoveTemp(Dependency));
        }
    }

    bool ParseVersion(const FJsonObject& Json, FIndexVersion& Out, TArray<FString>& OutIssues)
    {
        TArray<FString> Issues;
        ReadString(Json, TEXT("version"), Out.Text, Issues);
        if (Out.Text.IsEmpty())
        {
            OutIssues.Append(Issues);
            OutIssues.Add(TEXT("it has no \"version\"."));
            return false;
        }
        if (!FUBotSemVer::Parse(Out.Text, Out.SemVer))
        {
            OutIssues.Add(FString::Printf(TEXT("'%s' is not a semantic version."), *Out.Text));
            return false;
        }

        ReadBool(Json, TEXT("planned"), Out.bPlanned, Issues);

        FString Ref;
        ReadString(Json, TEXT("ref"), Ref, Issues);
        if (!Out.bPlanned)
        {
            if (Ref.IsEmpty())
            {
                Issues.Add(TEXT("it is not planned but has no \"ref\"."));
            }
            else if (!FUBotPackageIndex::IsSafeGitRef(Ref))
            {
                Issues.Add(FString::Printf(TEXT("\"ref\" '%s' is not a safe git ref."), *Ref));
            }
            // A planned version is not installable, so any ref it lists is ignored.
            Out.Ref = Ref;
        }

        FString EngineConstraint;
        ReadString(Json, TEXT("engine"), EngineConstraint, Issues);
        FUBotVersionConstraint ParsedEngine;
        FString EngineError;
        if (!FUBotVersionConstraint::Parse(EngineConstraint, ParsedEngine, &EngineError))
        {
            Issues.Add(FString::Printf(TEXT("\"engine\" '%s' is not a version constraint: %s."), *EngineConstraint, *EngineError));
        }

        ReadRequires(Json, Out.Requires, Issues);
        ReadStringArray(Json, TEXT("enginePlugins"), Out.EnginePlugins, Issues);
        ReadStringArray(Json, TEXT("provides"), Out.Provides, Issues);
        // "binaries" (prebuilt downloads) is only for uBot Manager, which validates it; Unreal uses the plugin
        // found on disk however it was installed, so the field is deliberately not read. Other unknown fields
        // are ignored the same way.

        OutIssues.Append(MoveTemp(Issues));
        return true;
    }

    /** Sorted newest first; malformed and duplicate versions are reported and left out. */
    TArray<FIndexVersion> ParseVersions(const FJsonObject& Json, TArray<FString>& OutIssues)
    {
        TArray<FIndexVersion> Versions;
        const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
        if (!Json.TryGetArrayField(TEXT("versions"), Values) || Values == nullptr)
        {
            OutIssues.Add(HasValue(Json, TEXT("versions")) ? TEXT("\"versions\" is not an array.") : TEXT("it has no \"versions\"."));
            return Versions;
        }

        for (int32 ValueIndex = 0; ValueIndex < Values->Num(); ++ValueIndex)
        {
            const TSharedPtr<FJsonValue>& Value = (*Values)[ValueIndex];
            const TSharedPtr<FJsonObject>* Entry = nullptr;
            if (!Value.IsValid() || !Value->TryGetObject(Entry) || Entry == nullptr || !Entry->IsValid())
            {
                OutIssues.Add(FString::Printf(TEXT("\"versions\"[%d] is not an object and was skipped."), ValueIndex));
                continue;
            }

            FIndexVersion Version;
            TArray<FString> VersionIssues;
            const bool bParsed = ParseVersion(**Entry, Version, VersionIssues);
            for (const FString& Issue : VersionIssues)
            {
                OutIssues.Add(FString::Printf(TEXT("\"versions\"[%d]: %s"), ValueIndex, *Issue));
            }
            if (!bParsed)
            {
                OutIssues.Add(FString::Printf(TEXT("\"versions\"[%d] was skipped."), ValueIndex));
                continue;
            }

            const bool bDuplicate = Versions.ContainsByPredicate([&Version](const FIndexVersion& Other)
            {
                return Other.SemVer == Version.SemVer;
            });
            if (bDuplicate)
            {
                OutIssues.Add(FString::Printf(TEXT("\"versions\"[%d] duplicates version %s and was skipped."), ValueIndex, *Version.Text));
                continue;
            }
            Versions.Add(MoveTemp(Version));
        }

        Versions.StableSort([](const FIndexVersion& A, const FIndexVersion& B)
        {
            return A.SemVer > B.SemVer;
        });
        return Versions;
    }

    bool ParseEntry(const FJsonObject& Json, int32 EntryIndex, const FString& Culture, FUBotPackageInfo& Out, TArray<FString>& OutIssues)
    {
        FString Name;
        if (!Json.TryGetStringField(TEXT("name"), Name) || Name.TrimStartAndEnd().IsEmpty())
        {
            OutIssues.Add(FString::Printf(TEXT("packages[%d] has no name and was skipped."), EntryIndex));
            return false;
        }

        FUBotPackageInfo Info;
        Info.Name = Name.TrimStartAndEnd();

        TArray<FString> FieldIssues;
        ReadString(Json, TEXT("friendlyName"), Info.FriendlyName, FieldIssues);
        if (Info.FriendlyName.IsEmpty())
        {
            Info.FriendlyName = Info.Name;
        }

        if (HasValue(Json, TEXT("description")))
        {
            const TSharedPtr<FJsonObject>* Texts = nullptr;
            if (!Json.TryGetObjectField(TEXT("description"), Texts) || Texts == nullptr || !Texts->IsValid())
            {
                FieldIssues.Add(TEXT("\"description\" is not an object of texts by language."));
            }
            else
            {
                FString English;
                if (!(*Texts)->TryGetStringField(TEXT("en"), English) || English.TrimStartAndEnd().IsEmpty())
                {
                    FieldIssues.Add(TEXT("\"description\" has no \"en\" text."));
                }
                Info.Description = FUBotPackageIndex::SelectLocalizedText(**Texts, Culture);
            }
        }

        if (HasValue(Json, TEXT("layer")))
        {
            FString LayerText;
            const int32 IssueCountBeforeLayer = FieldIssues.Num();
            ReadString(Json, TEXT("layer"), LayerText, FieldIssues);
            Info.Layer = ParsePackageLayer(LayerText);
            if (FieldIssues.Num() == IssueCountBeforeLayer && Info.Layer == EUBotPackageLayer::Unknown)
            {
                FieldIssues.Add(FString::Printf(TEXT("Unknown layer '%s' (expected Foundation, Capability, Adapter or Content)."), *LayerText));
            }
        }

        ReadString(Json, TEXT("repository"), Info.Repository, FieldIssues);
        ReadString(Json, TEXT("folder"), Info.InstallFolder, FieldIssues);
        if (!Info.InstallFolder.IsEmpty() && !FUBotPackageIndex::IsSafeFolderName(Info.InstallFolder))
        {
            FieldIssues.Add(FString::Printf(
                TEXT("\"folder\" '%s' is not one safe folder name (letters, digits, '.', '_' and '-')."), *Info.InstallFolder));
        }
        ReadString(Json, TEXT("docsUrl"), Info.DocsUrl, FieldIssues);
        ReadStringArray(Json, TEXT("tags"), Info.Tags, FieldIssues);

        const TArray<FIndexVersion> Versions = ParseVersions(Json, FieldIssues);
        const FIndexVersion* Selected = nullptr;
        for (const FIndexVersion& Version : Versions)
        {
            FUBotPackageIndexVersion& IndexVersion = Info.IndexVersions.AddDefaulted_GetRef();
            IndexVersion.Version = Version.Text;
            IndexVersion.Ref = Version.Ref;
            IndexVersion.bPlanned = Version.bPlanned;

            if (Version.bPlanned)
            {
                continue;
            }
            Info.AvailableVersions.Add(Version.Text);
            if (Selected == nullptr)
            {
                Selected = &Version;
            }
        }
        Info.bPlanned = Selected == nullptr && Versions.Num() > 0;
        if (Info.bPlanned)
        {
            Selected = &Versions[0];
        }

        if (Selected == nullptr)
        {
            for (const FString& Issue : FieldIssues)
            {
                OutIssues.Add(FString::Printf(TEXT("Package '%s': %s"), *Info.Name, *Issue));
            }
            OutIssues.Add(FString::Printf(TEXT("packages[%d] ('%s') has no valid version and was skipped."), EntryIndex, *Info.Name));
            return false;
        }

        if (!Info.bPlanned && Info.Repository.IsEmpty())
        {
            FieldIssues.Add(TEXT("it has installable versions but no \"repository\"."));
        }

        Info.Version = Selected->Text;
        Info.Requires = Selected->Requires;
        Info.ExternalRequires = Selected->EnginePlugins;
        Info.Provides = Selected->Provides;

        for (const FString& Issue : FieldIssues)
        {
            OutIssues.Add(FString::Printf(TEXT("Package '%s': %s"), *Info.Name, *Issue));
        }

        Info.State = EUBotPackageState::NotInstalled;
        Info.bInstalled = false;
        Info.bEnabled = false;
        Info.bFromIndex = true;
        Out = MoveTemp(Info);
        return true;
    }

    void ParseReplacements(const FJsonObject& Root, TArray<FUBotPackageReplacement>& OutReplacements, TArray<FString>& OutIssues)
    {
        if (!HasValue(Root, TEXT("replacements")))
        {
            return;
        }
        const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
        if (!Root.TryGetArrayField(TEXT("replacements"), Entries) || Entries == nullptr)
        {
            OutIssues.Add(TEXT("\"replacements\" is not an array."));
            return;
        }

        for (int32 EntryIndex = 0; EntryIndex < Entries->Num(); ++EntryIndex)
        {
            const TSharedPtr<FJsonValue>& Value = (*Entries)[EntryIndex];
            const TSharedPtr<FJsonObject>* Entry = nullptr;
            if (!Value.IsValid() || !Value->TryGetObject(Entry) || Entry == nullptr || !Entry->IsValid())
            {
                OutIssues.Add(FString::Printf(TEXT("replacements[%d] is not an object and was skipped."), EntryIndex));
                continue;
            }

            FUBotPackageReplacement Replacement;
            TArray<FString> EntryIssues;
            ReadString(**Entry, TEXT("legacy"), Replacement.Legacy, EntryIssues);
            ReadString(**Entry, TEXT("replacement"), Replacement.Replacement, EntryIssues);
            ReadString(**Entry, TEXT("redirects"), Replacement.Redirects, EntryIssues);
            if (Replacement.Legacy.IsEmpty() || Replacement.Replacement.IsEmpty())
            {
                EntryIssues.Add(TEXT("\"legacy\" and \"replacement\" are required."));
            }
            if (!EntryIssues.IsEmpty())
            {
                OutIssues.Add(FString::Printf(TEXT("replacements[%d] was skipped: %s"), EntryIndex, *FString::Join(EntryIssues, TEXT(" "))));
                continue;
            }

            const bool bDuplicate = OutReplacements.ContainsByPredicate([&Replacement](const FUBotPackageReplacement& Other)
            {
                return Other.Legacy.Equals(Replacement.Legacy, ESearchCase::IgnoreCase);
            });
            if (bDuplicate)
            {
                OutIssues.Add(FString::Printf(TEXT("replacements[%d] duplicates legacy plugin '%s' and was skipped."), EntryIndex, *Replacement.Legacy));
                continue;
            }
            OutReplacements.Add(MoveTemp(Replacement));
        }
    }

    bool IsChineseCulture(const FString& Culture)
    {
        return Culture.StartsWith(TEXT("zh"), ESearchCase::IgnoreCase)
            && (Culture.Len() == 2 || Culture[2] == TEXT('-') || Culture[2] == TEXT('_'));
    }

    bool TryGetNonEmptyText(const FJsonObject& Texts, const TCHAR* Field, FString& OutText)
    {
        FString Text;
        if (Texts.TryGetStringField(Field, Text) && !Text.TrimStartAndEnd().IsEmpty())
        {
            OutText = Text.TrimStartAndEnd();
            return true;
        }
        return false;
    }
}

bool FUBotPackageIndex::ParseIndexJson(const FString& JsonText, TArray<FUBotPackageInfo>& OutPackages, FString* OutError,
    TArray<FUBotPackageReplacement>* OutReplacements, const FString& Culture)
{
    using namespace UBot::PackageIndexPrivate;

    OutPackages.Reset();
    if (OutReplacements)
    {
        OutReplacements->Reset();
    }

    TSharedPtr<FJsonObject> Root;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        SetError(OutError, FString::Printf(TEXT("Invalid package index JSON: %s"), *Reader->GetErrorMessage()));
        return false;
    }

    // Field lookup is case-insensitive, so a format 1 file ("FormatVersion": 1) lands here too.
    double FormatVersion = 0.0;
    if (!Root->HasField(TEXT("formatVersion")))
    {
        SetError(OutError, FString::Printf(TEXT("The package index has no \"formatVersion\" (expected %d)."), SupportedFormatVersion));
        return false;
    }
    if (!Root->TryGetNumberField(TEXT("formatVersion"), FormatVersion) || FormatVersion != static_cast<double>(SupportedFormatVersion))
    {
        if (FormatVersion == 1.0)
        {
            SetError(OutError, FString::Printf(
                TEXT("Package index format 1 is no longer supported. Convert the file to format %d (\"formatVersion\": %d, camelCase keys, per-package \"versions\"); see Index/README.md in UBotCore."),
                SupportedFormatVersion, SupportedFormatVersion));
        }
        else
        {
            const TSharedPtr<FJsonValue> FormatValue = Root->TryGetField(TEXT("formatVersion"));
            FString FormatText;
            if (FormatValue.IsValid())
            {
                FormatValue->TryGetString(FormatText);
            }
            SetError(OutError, FString::Printf(TEXT("Unsupported package index formatVersion '%s' (expected %d)."), *FormatText, SupportedFormatVersion));
        }
        return false;
    }

    const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
    if (!Root->TryGetArrayField(TEXT("packages"), Entries) || Entries == nullptr)
    {
        SetError(OutError, TEXT("The package index has no \"packages\" array."));
        return false;
    }

    const FString EffectiveCulture = Culture.IsEmpty() ? GetCurrentCulture() : Culture;

    TArray<FString> Issues;
    TSet<FString> SeenNames;
    for (int32 EntryIndex = 0; EntryIndex < Entries->Num(); ++EntryIndex)
    {
        const TSharedPtr<FJsonValue>& Value = (*Entries)[EntryIndex];
        const TSharedPtr<FJsonObject>* EntryObject = nullptr;
        if (!Value.IsValid() || !Value->TryGetObject(EntryObject) || EntryObject == nullptr || !EntryObject->IsValid())
        {
            Issues.Add(FString::Printf(TEXT("packages[%d] is not an object and was skipped."), EntryIndex));
            continue;
        }

        FUBotPackageInfo Info;
        if (!ParseEntry(**EntryObject, EntryIndex, EffectiveCulture, Info, Issues))
        {
            continue;
        }
        // FString hashing and comparison ignore case, like plugin names.
        if (SeenNames.Contains(Info.Name))
        {
            Issues.Add(FString::Printf(TEXT("packages[%d] duplicates package '%s' and was skipped."), EntryIndex, *Info.Name));
            continue;
        }

        SeenNames.Add(Info.Name);
        OutPackages.Add(MoveTemp(Info));
    }

    TArray<FUBotPackageReplacement> Replacements;
    ParseReplacements(*Root, Replacements, Issues);
    if (OutReplacements)
    {
        *OutReplacements = MoveTemp(Replacements);
    }

    if (!Issues.IsEmpty())
    {
        SetError(OutError, FString::Join(Issues, TEXT("; ")));
        return false;
    }

    SetError(OutError, FString());
    return true;
}

bool FUBotPackageIndex::LoadIndexFile(const FString& FilePath, TArray<FUBotPackageInfo>& OutPackages, FString* OutError,
    TArray<FUBotPackageReplacement>* OutReplacements)
{
    using namespace UBot::PackageIndexPrivate;

    OutPackages.Reset();
    if (OutReplacements)
    {
        OutReplacements->Reset();
    }

    FString JsonText;
    if (!FFileHelper::LoadFileToString(JsonText, *FilePath))
    {
        SetError(OutError, FString::Printf(TEXT("Cannot read package index '%s'."), *FilePath));
        return false;
    }

    FString ParseError;
    if (!ParseIndexJson(JsonText, OutPackages, &ParseError, OutReplacements))
    {
        SetError(OutError, FString::Printf(TEXT("Package index '%s': %s"), *FilePath, *ParseError));
        return false;
    }

    SetError(OutError, FString());
    return true;
}

TArray<FString> FUBotPackageIndex::GetConfiguredIndexFiles(TArray<FString>* OutErrors)
{
    TArray<FString> Files;

    const FString MergedIndex = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("uBot"), TEXT("index.json")));
    if (IFileManager::Get().FileExists(*MergedIndex))
    {
        Files.Add(MergedIndex);
    }
    else if (const TSharedPtr<IPlugin> CorePlugin = IPluginManager::Get().FindPlugin(TEXT("UBotCore")))
    {
        Files.Add(FPaths::ConvertRelativePathToFull(FPaths::Combine(CorePlugin->GetBaseDir(), TEXT("Index"), TEXT("index.json"))));
    }
    else if (OutErrors)
    {
        OutErrors->Add(TEXT("The UBotCore plugin was not found, so its package index was skipped."));
    }

    if (const UUBotCoreSettings* Settings = GetDefault<UUBotCoreSettings>())
    {
        for (const FString& ConfiguredPath : Settings->AdditionalPackageIndexFiles)
        {
            FString Path = ConfiguredPath.TrimStartAndEnd();
            if (Path.IsEmpty())
            {
                continue;
            }
            if (FPaths::IsRelative(Path))
            {
                Path = FPaths::Combine(FPaths::ProjectDir(), Path);
            }
            Files.Add(FPaths::ConvertRelativePathToFull(Path));
        }
    }
    return Files;
}

TArray<FUBotPackageInfo> FUBotPackageIndex::LoadConfiguredIndex(TArray<FString>* OutErrors, TArray<FUBotPackageReplacement>* OutReplacements)
{
    TArray<FUBotPackageInfo> Merged;
    TMap<FString, int32> PositionByName;
    TArray<FUBotPackageReplacement> MergedReplacements;

    for (const FString& File : GetConfiguredIndexFiles(OutErrors))
    {
        TArray<FUBotPackageInfo> Entries;
        TArray<FUBotPackageReplacement> Replacements;
        FString Error;
        if (!LoadIndexFile(File, Entries, &Error, &Replacements) && OutErrors)
        {
            OutErrors->Add(Error);
        }

        for (FUBotPackageInfo& Entry : Entries)
        {
            const FString Name = Entry.Name;
            if (const int32* ExistingIndex = PositionByName.Find(Name))
            {
                Merged[*ExistingIndex] = MoveTemp(Entry);
            }
            else
            {
                const int32 NewIndex = Merged.Add(MoveTemp(Entry));
                PositionByName.Add(Name, NewIndex);
            }
        }

        for (FUBotPackageReplacement& Replacement : Replacements)
        {
            FUBotPackageReplacement* Existing = MergedReplacements.FindByPredicate([&Replacement](const FUBotPackageReplacement& Other)
            {
                return Other.Legacy.Equals(Replacement.Legacy, ESearchCase::IgnoreCase);
            });
            if (Existing)
            {
                *Existing = MoveTemp(Replacement);
            }
            else
            {
                MergedReplacements.Add(MoveTemp(Replacement));
            }
        }
    }

    if (OutReplacements)
    {
        *OutReplacements = MoveTemp(MergedReplacements);
    }
    return Merged;
}

bool FUBotPackageIndex::IsSafeFolderName(const FString& Folder)
{
    if (Folder.IsEmpty() || Folder == TEXT(".") || Folder == TEXT(".."))
    {
        return false;
    }
    for (const TCHAR Character : Folder)
    {
        const bool bAsciiLetterOrDigit = (Character >= TEXT('a') && Character <= TEXT('z'))
            || (Character >= TEXT('A') && Character <= TEXT('Z'))
            || (Character >= TEXT('0') && Character <= TEXT('9'));
        if (!bAsciiLetterOrDigit && Character != TEXT('.') && Character != TEXT('_') && Character != TEXT('-'))
        {
            return false;
        }
    }
    return true;
}

bool FUBotPackageIndex::IsSafeGitRef(const FString& Ref)
{
    if (Ref.IsEmpty() || Ref.StartsWith(TEXT("-")) || Ref.Contains(TEXT("..")))
    {
        return false;
    }
    for (const TCHAR Character : Ref)
    {
        // Quotes would break out of the quoted process argument.
        if (Character < 32 || Character == 127 || FChar::IsWhitespace(Character)
            || FCString::Strchr(TEXT("~^:?*[\\\""), Character) != nullptr)
        {
            return false;
        }
    }
    return true;
}

FString FUBotPackageIndex::SelectLocalizedText(const FJsonObject& Texts, const FString& Culture)
{
    using namespace UBot::PackageIndexPrivate;

    FString Text;
    if (IsChineseCulture(Culture.TrimStartAndEnd()) && TryGetNonEmptyText(Texts, TEXT("zh-CN"), Text))
    {
        return Text;
    }
    if (TryGetNonEmptyText(Texts, TEXT("en"), Text))
    {
        return Text;
    }
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Texts.Values)
    {
        if (TryGetNonEmptyText(Texts, *Field.Key, Text))
        {
            return Text;
        }
    }
    return FString();
}

FString FUBotPackageIndex::GetCurrentCulture()
{
    return FInternationalization::Get().GetCurrentLanguage()->GetName();
}

TArray<FUBotPackageInfo> FUBotPackageIndex::MergeInstalledWithIndex(const TArray<FUBotPackageInfo>& Installed, const TArray<FUBotPackageInfo>& Index)
{
    TArray<FUBotPackageInfo> Result = Installed;
    const int32 InstalledCount = Result.Num();

    // FString keys compare case-insensitively, like plugin names.
    TMap<FString, int32> PositionByName;
    for (int32 PackageIndex = 0; PackageIndex < InstalledCount; ++PackageIndex)
    {
        const FString Name = Result[PackageIndex].Name.TrimStartAndEnd();
        if (!Name.IsEmpty() && !PositionByName.Contains(Name))
        {
            PositionByName.Add(Name, PackageIndex);
        }
    }

    for (const FUBotPackageInfo& Entry : Index)
    {
        const FString Name = Entry.Name.TrimStartAndEnd();
        if (Name.IsEmpty())
        {
            continue;
        }

        if (const int32* ExistingIndex = PositionByName.Find(Name))
        {
            // A repeated index entry for a package already appended from the index is ignored.
            if (*ExistingIndex < InstalledCount)
            {
                FUBotPackageInfo& Target = Result[*ExistingIndex];
                Target.bFromIndex = true;
                Target.AvailableVersions = Entry.AvailableVersions;
                Target.IndexVersions = Entry.IndexVersions;
                Target.bPlanned = Entry.bPlanned;
                if (Target.Repository.IsEmpty())
                {
                    Target.Repository = Entry.Repository;
                }
                if (Target.DocsUrl.IsEmpty())
                {
                    Target.DocsUrl = Entry.DocsUrl;
                }
                if (Target.InstallFolder.IsEmpty())
                {
                    Target.InstallFolder = Entry.InstallFolder;
                }
            }
            continue;
        }

        FUBotPackageInfo& Added = Result.Add_GetRef(Entry);
        Added.State = EUBotPackageState::NotInstalled;
        Added.bInstalled = false;
        Added.bEnabled = false;
        Added.bFromIndex = true;
        Added.BaseDir.Reset();
        Added.DescriptorPath.Reset();
        Added.Problems.Reset();
        Added.ProblemDetails.Reset();
        PositionByName.Add(Name, Result.Num() - 1);
    }

    return Result;
}
