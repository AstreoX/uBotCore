#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"
#include "UObject/StrongObjectPtr.h"

class FMulticastDelegateProperty;
class FProperty;
class UBlueprint;
class UBlueprintNodeSpawner;
class UClass;
class UEdGraph;
class UEdGraphNode;
class UEnum;
class UFunction;
class UScriptStruct;
class UStruct;
struct FEdGraphPinType;

// In-memory form of the api.json contract (format 1) shared with the uBot docs site. Member order
// follows the key order of the file; the optional *Zh members are only written when not empty.

/** PinType of the contract. */
struct FUBotDocPinType
{
    /** exec, bool, byte, int, int64, float, double, name, string, text, vector, rotator, transform, struct, object, class, softobject, softclass, interface, enum, delegate, mcdelegate, wildcard (other UE pin categories are passed through). */
    FString Category;
    /** C++ name of the struct, class, interface or enum; empty otherwise. */
    FString Subtype;
    FString SubtypeDisplay;
    /** none, array, set or map. */
    FString Container = TEXT("none");
    /** Value type of a map (without container); null for everything else. */
    TSharedPtr<FUBotDocPinType> ValueType;
    bool bByRef = false;
    bool bConst = false;
};

struct FUBotDocPin
{
    /** Internal pin name, e.g. "execute", "then", "self", "ReturnValue". */
    FString Id;
    /** Display name exactly as drawn; empty for unnamed exec pins. */
    FString Name;
    FString NameZh;
    FUBotDocPinType Type;
    FString Default;
    bool bAdvanced = false;
    FString Tooltip;
};

struct FUBotDocNode
{
    /** <OwnerCpp>.<Member>, Make:<FStruct>, Break:<FStruct>, <K2NodeClass> or Macro:<MacroName>. */
    FString Id;
    /** call, pure, event, make, break or node. */
    FString Kind;
    FString Title;
    FString TitleZh;
    FString Subtitle;
    FString SubtitleZh;
    FString Owner;
    FString Member;
    FString Category;
    FString Tooltip;
    FString Keywords;
    bool bStatic = false;
    bool bLatent = false;
    bool bDeprecated = false;
    bool bDevelopmentOnly = false;
    bool bCallInEditor = false;
    bool bCompact = false;
    FString CompactTitle;
    TArray<FUBotDocPin> Inputs;
    TArray<FUBotDocPin> Outputs;
};

struct FUBotDocProperty
{
    FString Id;
    FString DisplayName;
    FString DisplayNameZh;
    FUBotDocPinType Type;
    FString Default;
    FString Category;
    /** readWrite, readOnly or none. */
    FString Access;
    /** anywhere, defaultsOnly, instanceOnly or none. */
    FString Editable;
    FString Tooltip;
    /** ClampMin, ClampMax, UIMin, UIMax, Units, ForceUnits, EditCondition (in this order, when present). */
    TArray<TPair<FString, FString>> Meta;
};

struct FUBotDocClass
{
    FString Id;
    FString DisplayName;
    FString DisplayNameZh;
    /** component, object, library, subsystem, settings, interface or actor. */
    FString Kind;
    FString Parent;
    FString Module;
    FString Header;
    FString Category;
    FString Tooltip;
    bool bAbstract = false;
    bool bBlueprintType = false;
    bool bBlueprintable = false;
    bool bSpawnable = false;
    bool bDeprecated = false;
    TArray<FUBotDocProperty> Properties;
    /** Ids of the nodes this class owns. */
    TArray<FString> Nodes;
};

struct FUBotDocField
{
    FString Id;
    FString DisplayName;
    FString DisplayNameZh;
    FUBotDocPinType Type;
    FString Tooltip;
    FString Access;
};

struct FUBotDocStruct
{
    FString Id;
    FString DisplayName;
    FString DisplayNameZh;
    FString Module;
    FString Header;
    FString Tooltip;
    TArray<FUBotDocField> Fields;
    /** Node ids, empty when the editor offers no such node. */
    FString Make;
    FString Break;
};

struct FUBotDocEnumValue
{
    FString Id;
    FString DisplayName;
    FString DisplayNameZh;
    int64 Value = 0;
    FString Tooltip;
};

struct FUBotDocEnum
{
    FString Id;
    FString DisplayName;
    FString DisplayNameZh;
    FString Module;
    FString Header;
    FString Tooltip;
    TArray<FUBotDocEnumValue> Values;
};

struct FUBotDocModule
{
    FString Name;
    FString Type;
};

struct FUBotDocApi
{
    int32 Format = 1;
    FString Plugin;
    FString Version;
    FString Engine;
    FString Repository;
    FString Generator;
    FString Culture;
    TArray<FUBotDocModule> Modules;
    TArray<FUBotDocClass> Classes;
    TArray<FUBotDocNode> Nodes;
    TArray<FUBotDocStruct> Structs;
    TArray<FUBotDocEnum> Enums;
};

/** One line of a -Nodes list file. */
struct FUBotDocNodeListEntry
{
    enum class EKind : uint8
    {
        /** "<ClassPath>:<Function>": a function call node. */
        Function,
        /** "event <ClassPath>:<Function>": an overridable event node. */
        Event,
        /** "node <K2NodeClassPath>": a special K2 node. */
        Node,
        /** "macro <BlueprintPath>:<MacroGraph>": a macro instance. */
        Macro
    };

    EKind Kind = EKind::Function;
    /** Class, K2 node class or macro library object path. */
    FString Path;
    /** Function or macro graph name; empty for EKind::Node. */
    FString Member;
    /** 1-based line in the list file, for messages. */
    int32 Line = 0;
};

/**
 * Builds the api.json documentation of a plugin's Blueprint API (or of a curated list of engine
 * nodes) from real K2 nodes: every node is spawned through the same node spawners the Blueprint
 * action menu uses, into the event graph of a transient Actor Blueprint, and read back with
 * GetNodeTitle(FullTitle), GetPinDisplayName, the pin types and the hidden/advanced flags. So the
 * titles, pin names, pin order and hidden pins are the ones the Blueprint editor draws.
 *
 * Text follows the process culture (UE's -culture=<name>). Friendly names are forced on while an
 * exporter exists, independent of the user's "Show Friendly Variable Names" preference, and pin
 * tooltips are generated in documentation mode (description only). Both are restored afterwards.
 * Localized node and pin names follow the editor setting [Internationalization]
 * ShouldUseLocalizedNodeAndPinNames, exactly like the graph editor.
 *
 * Game thread only. An exporter owns transient objects, so keep it alive only for one export run.
 */
class FUBotDocExporter
{
public:
    /** Value of the "generator" field. */
    static const TCHAR* const GeneratorName;

    FUBotDocExporter();
    ~FUBotDocExporter();

    UE_NONCOPYABLE(FUBotDocExporter);

    /**
     * Exports every Blueprint-visible class, struct and enum whose package is /Script/<Module> for a
     * module of the plugin (missing modules are loaded). Types declared in a Tests folder are skipped.
     * Returns false and fills OutErrors when the plugin is unknown, a module cannot be loaded or a
     * node cannot be built. OutApi is sorted.
     */
    bool ExportPlugin(const FString& PluginName, FUBotDocApi& OutApi, TArray<FString>& OutErrors);

    /** Exports the nodes of a parsed list file as plugin "Engine". Unknown entries are errors. OutApi is sorted. */
    bool ExportNodeList(const TArray<FUBotDocNodeListEntry>& Entries, FUBotDocApi& OutApi, TArray<FString>& OutErrors);

    /** Call or pure node of a Blueprint-callable function, id <OwnerCpp>.<Function>. */
    bool ExportFunctionNode(const UFunction* Function, FUBotDocNode& OutNode, FString& OutError);

    /** Override event node of a BlueprintImplementableEvent / BlueprintNativeEvent that can be placed as an event. */
    bool ExportEventNode(const UFunction* Function, FUBotDocNode& OutNode, FString& OutError);

    /** Bound event node of a BlueprintAssignable delegate: title from the property, outputs "then" + signature. */
    bool ExportDelegateNode(const FMulticastDelegateProperty* Property, FUBotDocNode& OutNode, FString& OutError);

    /** Make or Break node of a struct, id Make:<FStruct> / Break:<FStruct>. Uses the native make/break function when the struct declares one. */
    bool ExportStructNode(const UScriptStruct* Struct, bool bMake, FUBotDocNode& OutNode, FString& OutError);

    /** A K2 node class without further setup (Branch, Sequence, ...), id = class name. */
    bool ExportSpecialNode(UClass* NodeClass, FUBotDocNode& OutNode, FString& OutError);

    /** A macro instance of MacroGraph, id Macro:<GraphName>. */
    bool ExportMacroNode(UEdGraph* MacroGraph, FUBotDocNode& OutNode, FString& OutError);

    // Pure helpers.

    /**
     * Parses a -Nodes list file: one entry per line, '#' starts a comment, blank lines are ignored.
     * Returns false when any line is malformed; OutErrors then names the line ("Line 3: ...").
     */
    static bool ParseNodeList(const FString& Text, TArray<FUBotDocNodeListEntry>& OutEntries, TArray<FString>& OutErrors);

    /** The api.json text: 2-space indent, LF, trailing newline, like JSON.stringify(Api, null, 2) + "\n". */
    static FString ToJson(const FUBotDocApi& Api);

    /** Sorts every array of objects by id (ordinal) and the class node lists; pins keep their order. */
    static void SortApi(FUBotDocApi& Api);

    /**
     * Adds titleZh / subtitleZh / nameZh / displayNameZh from LocalizedJson (an export of the same
     * plugin made with -culture=zh-Hans) wherever the localized value is not empty and differs from
     * Api. Existing *Zh values are replaced. Returns false when LocalizedJson is not an export of
     * Api's plugin, when Api is not the English export (culture "en") or when LocalizedJson has no
     * culture or the same culture as Api. OutFieldCount receives the number of *Zh values set.
     */
    static bool MergeLocalized(FUBotDocApi& Api, const FString& LocalizedJson, int32& OutFieldCount, FString& OutError);

    /** Contract pin type of a K2 pin type. */
    static FUBotDocPinType MakePinType(const FEdGraphPinType& PinType);

    /** C++ name of a class (with prefix), struct (F...) or enum; empty for anything else. */
    static FString GetCppName(const UObject* Type);

    /** "Source/<Module>/<ModuleRelativePath>" of a reflected type, "" without that metadata. */
    static FString GetHeaderPath(const UStruct* Type, const FString& ModuleName);
    static FString GetHeaderPath(const UEnum* Enum, const FString& ModuleName);

    /** The api.json "culture" of the current language: "en" for every English variant (en-US, en-GB, ...), otherwise its name, e.g. "zh-Hans". */
    static FString GetCurrentCultureName();

private:
    UEdGraph* GetGraph();

    /** Spawns a node with Spawner (kept alive until it returns), reads it into OutNode (title, pins, compact, flags left as is) and removes it again. */
    bool SpawnAndRead(UBlueprintNodeSpawner* Spawner, const FString& Id, FUBotDocNode& OutNode, FString& OutError,
        TFunctionRef<void(UEdGraphNode& Node, FUBotDocNode& OutNode)> ReadExtra);

    void ExportClass(UClass* Class, const FString& ModuleName, FUBotDocApi& OutApi, TArray<FString>& OutErrors);
    void ExportStruct(UScriptStruct* Struct, const FString& ModuleName, FUBotDocApi& OutApi, TArray<FString>& OutErrors);
    void ExportEnum(UEnum* Enum, const FString& ModuleName, FUBotDocApi& OutApi);

    TStrongObjectPtr<UBlueprint> Blueprint;
    bool bPreviousGeneratingDocumentation = false;
    bool bPreviousShowFriendlyNames = true;
};
