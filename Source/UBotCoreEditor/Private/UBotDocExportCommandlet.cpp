#include "UBotDocExportCommandlet.h"

#include "HAL/FileManager.h"
#include "Internationalization/Internationalization.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UBotCore.h"
#include "UBotDocExporter.h"

namespace UBotDocExportCommandletPrivate
{
    void PrintUsage()
    {
        UE_LOG(LogUBot, Display, TEXT("Usage: -run=UBotDocExport -Plugin=<PluginName> -Out=<File> [-MergeZh=<File>]"));
        UE_LOG(LogUBot, Display, TEXT("       -run=UBotDocExport -Nodes=<ListFile> -Out=<File> [-MergeZh=<File>]"));
    }

    void PrintErrors(const TArray<FString>& Errors)
    {
        for (const FString& Error : Errors)
        {
            UE_LOG(LogUBot, Error, TEXT("%s"), *Error);
        }
    }
}

UUBotDocExportCommandlet::UUBotDocExportCommandlet()
{
    IsClient = false;
    IsServer = false;
    IsEditor = true;
    LogToConsole = true;
    ShowErrorCount = true;
    // The process exit code is exactly what Main returns, independent of unrelated engine errors.
    UseCommandletResultAsExitCode = true;

    HelpDescription = TEXT("Writes the Blueprint API of a plugin or of a curated engine node list as api.json for the uBot documentation (English unless -culture / -language says otherwise).");
    HelpUsage = TEXT("UnrealEditor-Cmd <Project>.uproject -run=UBotDocExport (-Plugin=<PluginName> | -Nodes=<ListFile>) -Out=<File> [-MergeZh=<File>]");
    HelpParamNames = { TEXT("Plugin"), TEXT("Nodes"), TEXT("Out"), TEXT("MergeZh") };
    HelpParamDescriptions = {
        TEXT("Document every Blueprint-visible class, struct and enum of this plugin."),
        TEXT("Document the nodes of a list file (plugin \"Engine\")."),
        TEXT("The api.json file to write."),
        TEXT("A -culture=zh-Hans export of the same plugin or list; adds the *Zh fields where the Chinese text differs.") };
}

FString UUBotDocExportCommandlet::ResolvePath(const FString& Value)
{
    FString Path = Value.TrimStartAndEnd().TrimQuotes();
    // UnrealEditor-Cmd runs in Engine/Binaries/<Platform>, which is never what a caller means.
    Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), Path);
    FPaths::NormalizeFilename(Path);
    return Path;
}

bool UUBotDocExportCommandlet::HasCultureOverride(const FString& CommandLine)
{
    // The keys and the "non-empty value" rule of the engine's GetRequestedCulture ("CULTURE=", "LANGUAGE=").
    for (const TCHAR* Key : { TEXT("CULTURE="), TEXT("LANGUAGE=") })
    {
        FString Value;
        if (FParse::Value(*CommandLine, Key, Value) && !Value.IsEmpty())
        {
            return true;
        }
    }
    return false;
}

int32 UUBotDocExportCommandlet::Main(const FString& Params)
{
    using namespace UBotDocExportCommandletPrivate;

    TArray<FString> Tokens;
    TArray<FString> Switches;
    TMap<FString, FString> ParamValues;
    ParseCommandLine(*Params, Tokens, Switches, ParamValues);

    // ParseCommandLine only turns "-Key=Value" into a value; "-Out" alone would silently be ignored.
    for (const TCHAR* ValueSwitch : { TEXT("Plugin"), TEXT("Nodes"), TEXT("Out"), TEXT("MergeZh") })
    {
        if (Switches.Contains(ValueSwitch))
        {
            UE_LOG(LogUBot, Error, TEXT("-%s requires a value: use -%s=<value>."), ValueSwitch, ValueSwitch);
            PrintUsage();
            return 1;
        }
    }

    const FString* PluginName = ParamValues.Find(TEXT("Plugin"));
    const FString* NodesFile = ParamValues.Find(TEXT("Nodes"));
    const FString* OutFile = ParamValues.Find(TEXT("Out"));
    const FString* MergeZhFile = ParamValues.Find(TEXT("MergeZh"));

    if ((PluginName != nullptr) == (NodesFile != nullptr))
    {
        UE_LOG(LogUBot, Error, TEXT("Pass exactly one of -Plugin=<PluginName> and -Nodes=<ListFile>."));
        PrintUsage();
        return 1;
    }
    for (const FString* Value : { PluginName, NodesFile, OutFile, MergeZhFile })
    {
        if (Value && Value->TrimStartAndEnd().TrimQuotes().IsEmpty())
        {
            UE_LOG(LogUBot, Error, TEXT("-Plugin=, -Nodes=, -Out= and -MergeZh= need a non-empty value."));
            return 1;
        }
    }
    if (!OutFile)
    {
        UE_LOG(LogUBot, Error, TEXT("-Out=<File> is required."));
        PrintUsage();
        return 1;
    }

    // Without -culture / -language the language is the OS or editor one (zh-CN on a Chinese Windows), which
    // would write a Chinese export labelled "zh-Hans" into a file the contract says is English.
    const bool bCultureChosen = HasCultureOverride(FCommandLine::Get());
    const FString StartCulture = FUBotDocExporter::GetCurrentCultureName();
    if (!bCultureChosen && StartCulture != TEXT("en"))
    {
        UE_LOG(LogUBot, Display, TEXT("No -culture or -language given: exporting in English instead of %s."), *StartCulture);
        if (!FInternationalization::Get().SetCurrentLanguageAndLocale(TEXT("en")))
        {
            UE_LOG(LogUBot, Error, TEXT("Cannot switch to English; pass -culture=en."));
            return 1;
        }
    }
    else if (bCultureChosen && MergeZhFile && StartCulture != TEXT("en"))
    {
        UE_LOG(LogUBot, Error, TEXT("-MergeZh adds Chinese text to the English export, but this run's culture is %s. Run with -culture=en (or without -culture)."), *StartCulture);
        return 1;
    }

    const FString OutPath = ResolvePath(*OutFile);
    FUBotDocApi Api;
    TArray<FString> Errors;
    bool bExported = false;
    {
        // The exporter owns a transient Blueprint and editor setting overrides; keep it to this scope.
        FUBotDocExporter Exporter;
        if (PluginName)
        {
            const FString Name = PluginName->TrimStartAndEnd().TrimQuotes();
            UE_LOG(LogUBot, Display, TEXT("Exporting the Blueprint API of plugin %s (culture %s)."), *Name, *FUBotDocExporter::GetCurrentCultureName());
            bExported = Exporter.ExportPlugin(Name, Api, Errors);
        }
        else
        {
            const FString ListPath = ResolvePath(*NodesFile);
            FString ListText;
            if (!FFileHelper::LoadFileToString(ListText, *ListPath))
            {
                UE_LOG(LogUBot, Error, TEXT("Cannot read the node list %s."), *ListPath);
                return 1;
            }
            TArray<FUBotDocNodeListEntry> Entries;
            if (FUBotDocExporter::ParseNodeList(ListText, Entries, Errors))
            {
                UE_LOG(LogUBot, Display, TEXT("Exporting %d node(s) listed in %s (culture %s)."), Entries.Num(), *ListPath, *FUBotDocExporter::GetCurrentCultureName());
                bExported = Exporter.ExportNodeList(Entries, Api, Errors);
            }
        }
    }

    PrintErrors(Errors);
    if (!bExported)
    {
        UE_LOG(LogUBot, Error, TEXT("Export failed with %d error(s); %s was not written."), Errors.Num(), *OutPath);
        return 1;
    }

    if (MergeZhFile)
    {
        const FString MergePath = ResolvePath(*MergeZhFile);
        FString LocalizedJson;
        if (!FFileHelper::LoadFileToString(LocalizedJson, *MergePath))
        {
            UE_LOG(LogUBot, Error, TEXT("Cannot read the localized export %s."), *MergePath);
            return 1;
        }
        int32 MergedFields = 0;
        FString MergeError;
        if (!FUBotDocExporter::MergeLocalized(Api, LocalizedJson, MergedFields, MergeError))
        {
            UE_LOG(LogUBot, Error, TEXT("%s (%s)"), *MergeError, *MergePath);
            return 1;
        }
        UE_LOG(LogUBot, Display, TEXT("Merged %d localized value(s) from %s."), MergedFields, *MergePath);
    }

    IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutPath), /*Tree*/ true);
    if (!FFileHelper::SaveStringToFile(FUBotDocExporter::ToJson(Api), *OutPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        UE_LOG(LogUBot, Error, TEXT("Cannot write %s."), *OutPath);
        return 1;
    }

    UE_LOG(LogUBot, Display, TEXT("Wrote %s: %d classes, %d nodes, %d structs, %d enums (culture %s)."),
        *OutPath, Api.Classes.Num(), Api.Nodes.Num(), Api.Structs.Num(), Api.Enums.Num(), *Api.Culture);
    return 0;
}
