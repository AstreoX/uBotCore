#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "UBotDocExportCommandlet.generated.h"

/**
 * Writes the Blueprint API of a plugin, or of a curated list of engine nodes, as api.json (format 1)
 * for the uBot documentation site.
 *
 * UnrealEditor-Cmd <Project>.uproject -run=UBotDocExport -Plugin=<PluginName> -Out=<File> [-MergeZh=<File>]
 * UnrealEditor-Cmd <Project>.uproject -run=UBotDocExport -Nodes=<ListFile> -Out=<File> [-MergeZh=<File>]
 *
 * -Plugin documents every Blueprint-visible class, struct and enum of the plugin's modules (types in
 * a Tests folder are skipped). The nodes are spawned through the Blueprint editor's own node
 * spawners into a transient Actor Blueprint, so titles, pin names, pin order and hidden pins are the
 * ones the editor draws. A class lists the properties it declares, plus inherited plugin properties
 * whose default it changes (with its own value). -Nodes documents a curated node list as plugin "Engine" (engine.json). Run
 * with -NullRHI; nothing is saved besides -Out.
 *
 * List file: one entry per line, '#' starts a comment, blank lines are ignored.
 *     /Script/Engine.KismetSystemLibrary:PrintString                             function call node
 *     event /Script/Engine.Actor:ReceiveBeginPlay                                 overridable event node
 *     node /Script/BlueprintGraph.K2Node_IfThenElse                               special K2 node class
 *     macro /Engine/EditorBlueprintResources/StandardMacros.StandardMacros:ForLoop   macro instance
 * Node ids: <OwnerCpp>.<Function> (UKismetSystemLibrary.PrintString, AActor.ReceiveBeginPlay), the
 * K2 node class name (K2Node_IfThenElse) and Macro:<MacroName>. A malformed line, an entry that does
 * not resolve and a node listed twice are errors.
 *
 * Text follows the process culture, so a bilingual file takes two runs: -culture=zh-Hans writes a
 * Chinese export, then -culture=en with -MergeZh=<that file> writes the English export plus the
 * optional titleZh, subtitleZh, nameZh and displayNameZh fields wherever the Chinese text differs.
 * A run with neither -culture nor -language exports in English, whatever the OS or editor language
 * is, so the file's "culture" is "en". -MergeZh needs an English run and a localized file of
 * another culture.
 * Relative paths are resolved against the project directory. The file is UTF-8 without BOM with LF
 * line ends; two runs over the same source produce identical bytes.
 *
 * Returns 0 on success and 1 when the arguments are malformed (none or both of -Plugin / -Nodes, a
 * missing -Out, a switch without "=<value>"), the plugin or a list entry cannot be resolved, a node
 * cannot be built, -MergeZh cannot be read, documents another plugin or is not a localized export of
 * an English run, or the output cannot be written.
 */
UCLASS()
class UBOTCOREEDITOR_API UUBotDocExportCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:
    UUBotDocExportCommandlet();

    virtual int32 Main(const FString& Params) override;

    /** Absolute form of a -Out / -Nodes / -MergeZh value: surrounding quotes removed, relative paths taken from the project directory. */
    static FString ResolvePath(const FString& Value);

    /** True when CommandLine picks the language itself (-culture=<name> or -language=<name>), like the engine reads it at startup. */
    static bool HasCultureOverride(const FString& CommandLine);
};
