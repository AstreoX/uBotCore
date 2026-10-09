#include "UBotPackageIndex.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UBotCoreSettings.h"
#include "UBotPackageRegistry.h"
#include "UBotSemVer.h"

// Named rather than anonymous so unity builds cannot merge it with other files' helpers.
namespace UBot::PackageIndexPrivate
{
    constexpr int32 SupportedFormatVersion = 1;

    void SetError(FString* OutError, const FString& Message)
    {
        if (OutError)
        {
            *OutError = Message;
        }
    }

    void ReadOptionalString(const FJsonObject& Json, const TCHAR* Field, FString& OutValue, TArray<FString>& OutIssues)
    {
        if (!Json.HasField(Field))
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

    bool ParseEntry(const FJsonObject& Json, int32 EntryIndex, FUBotPackageInfo& Out, TArray<FString>& OutIssues)
    {
        FString Name;
        if (!Json.TryGetStringField(TEXT("Name"), Name) || Name.TrimStartAndEnd().IsEmpty())
        {
            OutIssues.Add(FString::Printf(TEXT("Packages[%d] has no Name and was skipped."), EntryIndex));
            return false;
        }

        FUBotPackageInfo Info;
        Info.Name = Name.TrimStartAndEnd();

        TArray<FString> FieldIssues;
        ReadOptionalString(Json, TEXT("FriendlyName"), Info.FriendlyName, FieldIssues);
        if (Info.FriendlyName.IsEmpty())
        {
            Info.FriendlyName = Info.Name;
        }
        ReadOptionalString(Json, TEXT("Description"), Info.Description, FieldIssues);
        ReadOptionalString(Json, TEXT("Version"), Info.Version, FieldIssues);

        FUBotSemVer ParsedVersion;
        if (!Info.Version.IsEmpty() && !FUBotSemVer::Parse(Info.Version, ParsedVersion))
        {
            FieldIssues.Add(FString::Printf(TEXT("Version '%s' is not a semantic version."), *Info.Version));
        }

        FUBotPackageRegistry::ParseMetadataJson(Json, Info, FieldIssues);

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
}

bool FUBotPackageIndex::ParseIndexJson(const FString& JsonText, TArray<FUBotPackageInfo>& OutPackages, FString* OutError)
{
    using namespace UBot::PackageIndexPrivate;

    OutPackages.Reset();

    TSharedPtr<FJsonObject> Root;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        SetError(OutError, FString::Printf(TEXT("Invalid package index JSON: %s"), *Reader->GetErrorMessage()));
        return false;
    }

    if (Root->HasField(TEXT("FormatVersion")))
    {
        int32 FormatVersion = 0;
        if (!Root->TryGetNumberField(TEXT("FormatVersion"), FormatVersion) || FormatVersion != SupportedFormatVersion)
        {
            FString FormatText;
            Root->TryGetStringField(TEXT("FormatVersion"), FormatText);
            SetError(OutError, FString::Printf(TEXT("Unsupported package index FormatVersion '%s' (expected %d)."), *FormatText, SupportedFormatVersion));
            return false;
        }
    }

    const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
    if (!Root->TryGetArrayField(TEXT("Packages"), Entries) || Entries == nullptr)
    {
        SetError(OutError, TEXT("The package index has no \"Packages\" array."));
        return false;
    }

    TArray<FString> Issues;
    TSet<FString> SeenNames;
    for (int32 EntryIndex = 0; EntryIndex < Entries->Num(); ++EntryIndex)
    {
        const TSharedPtr<FJsonValue>& Value = (*Entries)[EntryIndex];
        const TSharedPtr<FJsonObject>* EntryObject = nullptr;
        if (!Value.IsValid() || !Value->TryGetObject(EntryObject) || EntryObject == nullptr || !EntryObject->IsValid())
        {
            Issues.Add(FString::Printf(TEXT("Packages[%d] is not an object and was skipped."), EntryIndex));
            continue;
        }

        FUBotPackageInfo Info;
        if (!ParseEntry(**EntryObject, EntryIndex, Info, Issues))
        {
            continue;
        }
        if (SeenNames.Contains(Info.Name))
        {
            Issues.Add(FString::Printf(TEXT("Packages[%d] duplicates package '%s' and was skipped."), EntryIndex, *Info.Name));
            continue;
        }

        SeenNames.Add(Info.Name);
        OutPackages.Add(MoveTemp(Info));
    }

    if (!Issues.IsEmpty())
    {
        SetError(OutError, FString::Join(Issues, TEXT("; ")));
        return false;
    }

    SetError(OutError, FString());
    return true;
}

bool FUBotPackageIndex::LoadIndexFile(const FString& FilePath, TArray<FUBotPackageInfo>& OutPackages, FString* OutError)
{
    using namespace UBot::PackageIndexPrivate;

    OutPackages.Reset();

    FString JsonText;
    if (!FFileHelper::LoadFileToString(JsonText, *FilePath))
    {
        SetError(OutError, FString::Printf(TEXT("Cannot read package index '%s'."), *FilePath));
        return false;
    }

    FString ParseError;
    if (!ParseIndexJson(JsonText, OutPackages, &ParseError))
    {
        SetError(OutError, FString::Printf(TEXT("Package index '%s': %s"), *FilePath, *ParseError));
        return false;
    }

    SetError(OutError, FString());
    return true;
}

TArray<FUBotPackageInfo> FUBotPackageIndex::LoadConfiguredIndex(TArray<FString>* OutErrors)
{
    TArray<FString> Files;

    const TSharedPtr<IPlugin> CorePlugin = IPluginManager::Get().FindPlugin(TEXT("UBotCore"));
    if (CorePlugin.IsValid())
    {
        Files.Add(FPaths::ConvertRelativePathToFull(FPaths::Combine(CorePlugin->GetBaseDir(), TEXT("Resources"), TEXT("PackageIndex.json"))));
    }
    else if (OutErrors)
    {
        OutErrors->Add(TEXT("The UBotCore plugin was not found, so its built-in package index was skipped."));
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

    TArray<FUBotPackageInfo> Merged;
    TMap<FString, int32> PositionByName;
    for (const FString& File : Files)
    {
        TArray<FUBotPackageInfo> Entries;
        FString Error;
        if (!LoadIndexFile(File, Entries, &Error) && OutErrors)
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
    }
    return Merged;
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
                if (Target.Repository.IsEmpty())
                {
                    Target.Repository = Entry.Repository;
                }
                if (Target.DocsUrl.IsEmpty())
                {
                    Target.DocsUrl = Entry.DocsUrl;
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
        PositionByName.Add(Name, Result.Num() - 1);
    }

    return Result;
}
