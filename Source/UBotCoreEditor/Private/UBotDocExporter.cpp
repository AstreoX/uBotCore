#include "UBotDocExporter.h"

#include "BlueprintEventNodeSpawner.h"
#include "BlueprintFunctionNodeSpawner.h"
#include "BlueprintNodeBinder.h"
#include "BlueprintNodeSpawner.h"
#include "Components/ActorComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/DeveloperSettings.h"
#include "GameFramework/Actor.h"
#include "Interfaces/IPluginManager.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "K2Node.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_CallFunction.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_Event.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_MakeStruct.h"
#include "K2Node_StructOperation.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopeExit.h"
#include "ModuleDescriptor.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Settings/EditorStyleSettings.h"
#include "Subsystems/Subsystem.h"
#include "UBotCore.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"

const TCHAR* const FUBotDocExporter::GeneratorName = TEXT("UBotDocExport 1");

namespace UBotDocExporterPrivate
{
    const FName NAME_ToolTip(TEXT("ToolTip"));
    const FName NAME_Category(TEXT("Category"));
    const FName NAME_ModuleRelativePath(TEXT("ModuleRelativePath"));
    const FName NAME_ClassGroupNames(TEXT("ClassGroupNames"));
    const FName NAME_BlueprintType(TEXT("BlueprintType"));
    const FName NAME_IsBlueprintBase(TEXT("IsBlueprintBase"));
    const FName NAME_BlueprintSpawnableComponent(TEXT("BlueprintSpawnableComponent"));
    const FName NAME_DeprecationMessage(TEXT("DeprecationMessage"));
    const FName NAME_Hidden(TEXT("Hidden"));

    /** Property metadata copied into "meta", in this order. */
    const TCHAR* const PropertyMetaKeys[] = {
        TEXT("ClampMin"), TEXT("ClampMax"), TEXT("UIMin"), TEXT("UIMax"), TEXT("Units"), TEXT("ForceUnits"), TEXT("EditCondition") };

    /** Ordinal comparison, the same order as JavaScript's default sort for these ASCII ids. */
    bool IdLess(const FString& A, const FString& B)
    {
        return A.Compare(B, ESearchCase::CaseSensitive) < 0;
    }

    /** Test-only types (declared in a Tests folder) are not part of the public API. */
    bool IsTestHeader(const FString& ModuleRelativePath)
    {
        TArray<FString> Segments;
        ModuleRelativePath.ParseIntoArray(Segments, TEXT("/"));
        return Segments.ContainsByPredicate([](const FString& Segment) { return Segment.Equals(TEXT("Tests"), ESearchCase::IgnoreCase); });
    }

    /** Same doxygen cleanup as ObjectTools::GetDefaultTooltipForFunction, without its "Target is" suffix. */
    FString CleanFunctionTooltip(const FString& RawTooltip)
    {
        FString Tooltip = RawTooltip;
        Tooltip.Split(TEXT("@param"), &Tooltip, nullptr, ESearchCase::IgnoreCase, ESearchDir::FromStart);
        Tooltip.Split(TEXT("@return"), &Tooltip, nullptr, ESearchCase::IgnoreCase, ESearchDir::FromStart);
        Tooltip.ReplaceInline(TEXT("@see"), TEXT("See:"));
        Tooltip.ReplaceInline(TEXT("@note"), TEXT("Note:"));
        Tooltip.TrimStartAndEndInline();
        return Tooltip;
    }

    void SplitTitle(const FString& FullTitle, FString& OutTitle, FString& OutSubtitle)
    {
        FString Normalized = FullTitle.Replace(TEXT("\r\n"), TEXT("\n"));
        if (!Normalized.Split(TEXT("\n"), &OutTitle, &OutSubtitle, ESearchCase::CaseSensitive, ESearchDir::FromStart))
        {
            OutTitle = MoveTemp(Normalized);
            OutSubtitle.Reset();
        }
    }

    FString GetPinDefault(const UEdGraphPin& Pin)
    {
        if (Pin.DefaultObject)
        {
            return Pin.DefaultObject->GetPathName();
        }
        if (!Pin.DefaultTextValue.IsEmpty())
        {
            return Pin.DefaultTextValue.ToString();
        }
        return Pin.DefaultValue;
    }

    FString GetAccess(const FProperty& Property)
    {
        if (!Property.HasAnyPropertyFlags(CPF_BlueprintVisible))
        {
            return TEXT("none");
        }
        return Property.HasAnyPropertyFlags(CPF_BlueprintReadOnly) ? TEXT("readOnly") : TEXT("readWrite");
    }

    FString GetEditable(const FProperty& Property)
    {
        // Visible* properties carry CPF_Edit | CPF_EditConst: shown in the Details panel, but not editable.
        if (!Property.HasAnyPropertyFlags(CPF_Edit) || Property.HasAnyPropertyFlags(CPF_EditConst))
        {
            return TEXT("none");
        }
        if (Property.HasAnyPropertyFlags(CPF_DisableEditOnInstance))
        {
            return TEXT("defaultsOnly");
        }
        if (Property.HasAnyPropertyFlags(CPF_DisableEditOnTemplate))
        {
            return TEXT("instanceOnly");
        }
        return TEXT("anywhere");
    }

    /**
     * Whether the property is declared in a module of a project or external plugin (any uBot plugin),
     * as opposed to the engine or an engine plugin. Inherited engine properties (collision, tick
     * settings, ...) are documented by Epic and are never repeated on a plugin class.
     */
    bool IsDeclaredInPlugin(const FProperty& Property)
    {
        const UClass* Owner = Property.GetOwnerClass();
        const UPackage* Package = Owner ? Owner->GetOutermost() : nullptr;
        if (!Package)
        {
            return false;
        }
        const FString PackageName = Package->GetName();
        static const FString ScriptPrefix(TEXT("/Script/"));
        if (!PackageName.StartsWith(ScriptPrefix, ESearchCase::CaseSensitive))
        {
            return false;
        }
        const FName ModuleName(*PackageName.RightChop(ScriptPrefix.Len()));
        for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetEnabledPlugins())
        {
            if (Plugin->GetType() == EPluginType::Engine || Plugin->GetType() == EPluginType::Enterprise)
            {
                continue;
            }
            if (Plugin->GetDescriptor().Modules.ContainsByPredicate([ModuleName](const FModuleDescriptor& Module) { return Module.Name == ModuleName; }))
            {
                return true;
            }
        }
        return false;
    }

    /** Whether two class default objects hold the same value for every element of the property. */
    bool HasSameDefault(const FProperty& Property, const UObject& A, const UObject& B)
    {
        for (int32 Index = 0; Index < Property.ArrayDim; ++Index)
        {
            if (!Property.Identical_InContainer(&A, &B, Index, PPF_None))
            {
                return false;
            }
        }
        return true;
    }

    /** Blueprint-visible or shown in the Details panel; delegates become nodes instead. */
    bool IsDocumentedProperty(const FProperty& Property)
    {
        if (!Property.HasAnyPropertyFlags(CPF_BlueprintVisible | CPF_Edit) || Property.HasAnyPropertyFlags(CPF_Deprecated))
        {
            return false;
        }
        return !Property.IsA<FMulticastDelegateProperty>() && !Property.IsA<FDelegateProperty>();
    }

    bool IsDocumentedFunction(const UFunction& Function)
    {
        if (Function.HasAnyFunctionFlags(FUNC_Delegate) || !Function.HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintEvent))
        {
            return false;
        }
        return !Function.GetBoolMetaData(FBlueprintMetadata::MD_BlueprintInternalUseOnly);
    }

    FString GetClassKind(const UClass& Class)
    {
        if (Class.HasAnyClassFlags(CLASS_Interface))
        {
            return TEXT("interface");
        }
        if (Class.IsChildOf<AActor>())
        {
            return TEXT("actor");
        }
        if (Class.IsChildOf<UActorComponent>())
        {
            return TEXT("component");
        }
        if (Class.IsChildOf<UBlueprintFunctionLibrary>())
        {
            return TEXT("library");
        }
        if (Class.IsChildOf<USubsystem>())
        {
            return TEXT("subsystem");
        }
        if (Class.IsChildOf<UDeveloperSettings>() || Class.HasAnyClassFlags(CLASS_Config))
        {
            return TEXT("settings");
        }
        return TEXT("object");
    }

    /** UBot.Repository of a .uplugin without ".git"; "" when absent. */
    FString ReadRepository(const FString& DescriptorFileName)
    {
        FString Text;
        TSharedPtr<FJsonObject> Root;
        if (!FFileHelper::LoadFileToString(Text, *DescriptorFileName)
            || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
        {
            return FString();
        }
        const TSharedPtr<FJsonObject>* UBotBlock = nullptr;
        FString Repository;
        if (Root->TryGetObjectField(TEXT("UBot"), UBotBlock) && UBotBlock && (*UBotBlock)->TryGetStringField(TEXT("Repository"), Repository))
        {
            Repository.TrimStartAndEndInline();
            Repository.RemoveFromEnd(TEXT("/"));
            Repository.RemoveFromEnd(TEXT(".git"), ESearchCase::IgnoreCase);
        }
        return Repository;
    }

    UClass* FindClassByPath(const FString& Path)
    {
        if (UClass* Found = FindObject<UClass>(nullptr, *Path))
        {
            return Found;
        }
        return LoadObject<UClass>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
    }

    /** HasNativeMake / HasNativeBreak values are object paths, with or without the /Script/ prefix. */
    UFunction* FindNativeStructFunction(const FString& Path)
    {
        if (UFunction* Found = FindObject<UFunction>(nullptr, *Path))
        {
            return Found;
        }
        return Path.StartsWith(TEXT("/")) ? nullptr : FindObject<UFunction>(nullptr, *(TEXT("/Script/") + Path));
    }

    /** UK2Node_BreakStruct::CanBeBroken, which the engine does not export. */
    bool CanBeBroken(const UScriptStruct& Struct)
    {
        if (Struct.HasMetaData(FBlueprintMetadata::MD_NativeBreakFunction) || !UEdGraphSchema_K2::IsAllowableBlueprintVariableType(&Struct))
        {
            return false;
        }
        const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
        for (TFieldIterator<FProperty> It(&Struct); It; ++It)
        {
            FEdGraphPinType PinType;
            if (It->HasAnyPropertyFlags(CPF_BlueprintVisible) && Schema->ConvertPropertyToPinType(*It, PinType))
            {
                return true;
            }
        }
        return false;
    }

    /**
     * A pin tooltip that only repeats the node description or the node's name adds nothing. UE builds
     * such a tooltip for an undocumented function's return value: the function's display name, which
     * is the node title, or the name without the DisplayName metadata (FallbackName) when that differs.
     */
    void DropRepeatedPinTooltips(FUBotDocNode& Node, const FString& RawTooltip, const FString& FallbackName = FString())
    {
        for (TArray<FUBotDocPin>* Pins : { &Node.Inputs, &Node.Outputs })
        {
            for (FUBotDocPin& Pin : *Pins)
            {
                if (Pin.Tooltip == RawTooltip || Pin.Tooltip == Node.Tooltip || Pin.Tooltip == Node.Title
                    || (!FallbackName.IsEmpty() && Pin.Tooltip == FallbackName))
                {
                    Pin.Tooltip.Reset();
                }
            }
        }
    }

    void FillFunctionInfo(const UFunction& Function, FUBotDocNode& Node)
    {
        const FString RawTooltip = Function.GetMetaData(NAME_ToolTip);
        Node.Owner = FUBotDocExporter::GetCppName(Function.GetOwnerClass());
        Node.Member = Function.GetName();
        Node.Category = Function.GetMetaData(FBlueprintMetadata::MD_FunctionCategory);
        Node.Tooltip = CleanFunctionTooltip(RawTooltip);
        Node.Keywords = Function.GetMetaData(FBlueprintMetadata::MD_FunctionKeywords);
        Node.bStatic = Function.HasAnyFunctionFlags(FUNC_Static);
        Node.bLatent = Function.HasMetaData(FBlueprintMetadata::MD_Latent);
        Node.bDeprecated = Function.HasMetaData(FBlueprintMetadata::MD_DeprecatedFunction);
        Node.bDevelopmentOnly = Function.HasMetaData(FBlueprintMetadata::MD_DevelopmentOnly);
        Node.bCallInEditor = Function.GetBoolMetaData(FBlueprintMetadata::MD_CallInEditor);
        // UField::GetToolTipText falls back to NameToDisplayString(<function name>), ignoring DisplayName.
        DropRepeatedPinTooltips(Node, RawTooltip, FName::NameToDisplayString(Function.GetName(), /*bIsBool*/ false));
    }

    /** Node title, menu category, tooltip and keywords straight from the node (special nodes, macros, make/break). */
    void FillNodeInfo(UEdGraphNode& Node, FUBotDocNode& Out)
    {
        if (const UK2Node* K2Node = Cast<UK2Node>(&Node))
        {
            Out.Category = K2Node->GetMenuCategory().ToString();
        }
        Out.Tooltip = Node.GetTooltipText().ToString();
        Out.Keywords = Node.GetKeywords().ToString();
        DropRepeatedPinTooltips(Out, Out.Tooltip);
    }

    /** Emits JSON exactly like JSON.stringify(Value, null, 2). */
    class FJsonTextWriter
    {
    public:
        void BeginObject()
        {
            BeginValue();
            Text += TEXT('{');
            HasElements.Push(false);
        }

        void EndObject()
        {
            End(TEXT('}'));
        }

        void BeginArray()
        {
            BeginValue();
            Text += TEXT('[');
            HasElements.Push(false);
        }

        void EndArray()
        {
            End(TEXT(']'));
        }

        void Key(const TCHAR* Name)
        {
            NextElement();
            WriteString(Name);
            Text += TEXT(": ");
            bAfterKey = true;
        }

        void Value(const FString& String)
        {
            BeginValue();
            WriteString(String);
        }

        void Value(bool bValue)
        {
            BeginValue();
            Text += bValue ? TEXT("true") : TEXT("false");
        }

        void Value(int64 Number)
        {
            BeginValue();
            Text += FString::Printf(TEXT("%lld"), static_cast<long long>(Number));
        }

        void Null()
        {
            BeginValue();
            Text += TEXT("null");
        }

        template <typename ValueType>
        void Field(const TCHAR* Name, const ValueType& InValue)
        {
            Key(Name);
            Value(InValue);
        }

        /** Writes the field only when Text is not empty (the optional *Zh fields). */
        void OptionalField(const TCHAR* Name, const FString& String)
        {
            if (!String.IsEmpty())
            {
                Field(Name, String);
            }
        }

        FString Finish()
        {
            check(HasElements.IsEmpty());
            Text += TEXT('\n');
            return MoveTemp(Text);
        }

    private:
        void BeginValue()
        {
            if (bAfterKey)
            {
                bAfterKey = false;
            }
            else if (!HasElements.IsEmpty())
            {
                NextElement();
            }
        }

        void NextElement()
        {
            bool& bHasElements = HasElements.Last();
            Text += bHasElements ? TEXT(",\n") : TEXT("\n");
            bHasElements = true;
            Indent();
        }

        void End(TCHAR Close)
        {
            const bool bHadElements = HasElements.Pop();
            if (bHadElements)
            {
                Text += TEXT('\n');
                Indent();
            }
            Text += Close;
        }

        void Indent()
        {
            for (int32 Level = 0; Level < HasElements.Num(); ++Level)
            {
                Text += TEXT("  ");
            }
        }

        void AppendUnicodeEscape(uint32 Code)
        {
            Text += FString::Printf(TEXT("\\u%04x"), Code);
        }

        void WriteString(const FString& String)
        {
            Text += TEXT('"');
            const int32 Length = String.Len();
            for (int32 Index = 0; Index < Length; ++Index)
            {
                const TCHAR Character = String[Index];
                const uint32 Code = static_cast<uint32>(Character);
                switch (Character)
                {
                case TEXT('"'): Text += TEXT("\\\""); break;
                case TEXT('\\'): Text += TEXT("\\\\"); break;
                case TEXT('\b'): Text += TEXT("\\b"); break;
                case TEXT('\f'): Text += TEXT("\\f"); break;
                case TEXT('\n'): Text += TEXT("\\n"); break;
                case TEXT('\r'): Text += TEXT("\\r"); break;
                case TEXT('\t'): Text += TEXT("\\t"); break;
                default:
                    if (Code < 0x20)
                    {
                        AppendUnicodeEscape(Code);
                    }
                    else if (Code >= 0xD800 && Code <= 0xDBFF && Index + 1 < Length
                        && static_cast<uint32>(String[Index + 1]) >= 0xDC00 && static_cast<uint32>(String[Index + 1]) <= 0xDFFF)
                    {
                        Text += Character;
                        Text += String[++Index];
                    }
                    else if (Code >= 0xD800 && Code <= 0xDFFF)
                    {
                        // A lone surrogate cannot be encoded as UTF-8; JSON.stringify escapes it the same way.
                        AppendUnicodeEscape(Code);
                    }
                    else
                    {
                        Text += Character;
                    }
                    break;
                }
            }
            Text += TEXT('"');
        }

        FString Text;
        TArray<bool> HasElements;
        bool bAfterKey = false;
    };

    void WritePinType(FJsonTextWriter& Writer, const FUBotDocPinType& Type)
    {
        Writer.BeginObject();
        Writer.Field(TEXT("category"), Type.Category);
        Writer.Field(TEXT("subtype"), Type.Subtype);
        Writer.Field(TEXT("subtypeDisplay"), Type.SubtypeDisplay);
        Writer.Field(TEXT("container"), Type.Container);
        Writer.Key(TEXT("valueType"));
        if (Type.ValueType.IsValid())
        {
            WritePinType(Writer, *Type.ValueType);
        }
        else
        {
            Writer.Null();
        }
        Writer.Field(TEXT("byRef"), Type.bByRef);
        Writer.Field(TEXT("const"), Type.bConst);
        Writer.EndObject();
    }

    void WritePins(FJsonTextWriter& Writer, const TCHAR* Name, const TArray<FUBotDocPin>& Pins)
    {
        Writer.Key(Name);
        Writer.BeginArray();
        for (const FUBotDocPin& Pin : Pins)
        {
            Writer.BeginObject();
            Writer.Field(TEXT("id"), Pin.Id);
            Writer.Field(TEXT("name"), Pin.Name);
            Writer.OptionalField(TEXT("nameZh"), Pin.NameZh);
            Writer.Key(TEXT("type"));
            WritePinType(Writer, Pin.Type);
            Writer.Field(TEXT("default"), Pin.Default);
            Writer.Field(TEXT("advanced"), Pin.bAdvanced);
            Writer.Field(TEXT("tooltip"), Pin.Tooltip);
            Writer.EndObject();
        }
        Writer.EndArray();
    }

    void WriteClass(FJsonTextWriter& Writer, const FUBotDocClass& Class)
    {
        Writer.BeginObject();
        Writer.Field(TEXT("id"), Class.Id);
        Writer.Field(TEXT("displayName"), Class.DisplayName);
        Writer.OptionalField(TEXT("displayNameZh"), Class.DisplayNameZh);
        Writer.Field(TEXT("kind"), Class.Kind);
        Writer.Field(TEXT("parent"), Class.Parent);
        Writer.Field(TEXT("module"), Class.Module);
        Writer.Field(TEXT("header"), Class.Header);
        Writer.Field(TEXT("category"), Class.Category);
        Writer.Field(TEXT("tooltip"), Class.Tooltip);
        Writer.Key(TEXT("flags"));
        Writer.BeginObject();
        Writer.Field(TEXT("abstract"), Class.bAbstract);
        Writer.Field(TEXT("blueprintType"), Class.bBlueprintType);
        Writer.Field(TEXT("blueprintable"), Class.bBlueprintable);
        Writer.Field(TEXT("spawnable"), Class.bSpawnable);
        Writer.Field(TEXT("deprecated"), Class.bDeprecated);
        Writer.EndObject();
        Writer.Key(TEXT("properties"));
        Writer.BeginArray();
        for (const FUBotDocProperty& Property : Class.Properties)
        {
            Writer.BeginObject();
            Writer.Field(TEXT("id"), Property.Id);
            Writer.Field(TEXT("displayName"), Property.DisplayName);
            Writer.OptionalField(TEXT("displayNameZh"), Property.DisplayNameZh);
            Writer.Key(TEXT("type"));
            WritePinType(Writer, Property.Type);
            Writer.Field(TEXT("default"), Property.Default);
            Writer.Field(TEXT("category"), Property.Category);
            Writer.Field(TEXT("access"), Property.Access);
            Writer.Field(TEXT("editable"), Property.Editable);
            Writer.Field(TEXT("tooltip"), Property.Tooltip);
            Writer.Key(TEXT("meta"));
            Writer.BeginObject();
            for (const TPair<FString, FString>& Meta : Property.Meta)
            {
                Writer.Field(*Meta.Key, Meta.Value);
            }
            Writer.EndObject();
            Writer.EndObject();
        }
        Writer.EndArray();
        Writer.Key(TEXT("nodes"));
        Writer.BeginArray();
        for (const FString& NodeId : Class.Nodes)
        {
            Writer.Value(NodeId);
        }
        Writer.EndArray();
        Writer.EndObject();
    }

    void WriteNode(FJsonTextWriter& Writer, const FUBotDocNode& Node)
    {
        Writer.BeginObject();
        Writer.Field(TEXT("id"), Node.Id);
        Writer.Field(TEXT("kind"), Node.Kind);
        Writer.Field(TEXT("title"), Node.Title);
        Writer.OptionalField(TEXT("titleZh"), Node.TitleZh);
        Writer.Field(TEXT("subtitle"), Node.Subtitle);
        Writer.OptionalField(TEXT("subtitleZh"), Node.SubtitleZh);
        Writer.Field(TEXT("owner"), Node.Owner);
        Writer.Field(TEXT("member"), Node.Member);
        Writer.Field(TEXT("category"), Node.Category);
        Writer.Field(TEXT("tooltip"), Node.Tooltip);
        Writer.Field(TEXT("keywords"), Node.Keywords);
        Writer.Key(TEXT("flags"));
        Writer.BeginObject();
        Writer.Field(TEXT("static"), Node.bStatic);
        Writer.Field(TEXT("latent"), Node.bLatent);
        Writer.Field(TEXT("deprecated"), Node.bDeprecated);
        Writer.Field(TEXT("developmentOnly"), Node.bDevelopmentOnly);
        Writer.Field(TEXT("callInEditor"), Node.bCallInEditor);
        Writer.Field(TEXT("compact"), Node.bCompact);
        Writer.EndObject();
        Writer.Field(TEXT("compactTitle"), Node.CompactTitle);
        WritePins(Writer, TEXT("inputs"), Node.Inputs);
        WritePins(Writer, TEXT("outputs"), Node.Outputs);
        Writer.EndObject();
    }

    void WriteStruct(FJsonTextWriter& Writer, const FUBotDocStruct& Struct)
    {
        Writer.BeginObject();
        Writer.Field(TEXT("id"), Struct.Id);
        Writer.Field(TEXT("displayName"), Struct.DisplayName);
        Writer.OptionalField(TEXT("displayNameZh"), Struct.DisplayNameZh);
        Writer.Field(TEXT("module"), Struct.Module);
        Writer.Field(TEXT("header"), Struct.Header);
        Writer.Field(TEXT("tooltip"), Struct.Tooltip);
        Writer.Key(TEXT("fields"));
        Writer.BeginArray();
        for (const FUBotDocField& Field : Struct.Fields)
        {
            Writer.BeginObject();
            Writer.Field(TEXT("id"), Field.Id);
            Writer.Field(TEXT("displayName"), Field.DisplayName);
            Writer.OptionalField(TEXT("displayNameZh"), Field.DisplayNameZh);
            Writer.Key(TEXT("type"));
            WritePinType(Writer, Field.Type);
            Writer.Field(TEXT("tooltip"), Field.Tooltip);
            Writer.Field(TEXT("access"), Field.Access);
            Writer.EndObject();
        }
        Writer.EndArray();
        Writer.Field(TEXT("make"), Struct.Make);
        Writer.Field(TEXT("break"), Struct.Break);
        Writer.EndObject();
    }

    void WriteEnum(FJsonTextWriter& Writer, const FUBotDocEnum& Enum)
    {
        Writer.BeginObject();
        Writer.Field(TEXT("id"), Enum.Id);
        Writer.Field(TEXT("displayName"), Enum.DisplayName);
        Writer.OptionalField(TEXT("displayNameZh"), Enum.DisplayNameZh);
        Writer.Field(TEXT("module"), Enum.Module);
        Writer.Field(TEXT("header"), Enum.Header);
        Writer.Field(TEXT("tooltip"), Enum.Tooltip);
        Writer.Key(TEXT("values"));
        Writer.BeginArray();
        for (const FUBotDocEnumValue& Value : Enum.Values)
        {
            Writer.BeginObject();
            Writer.Field(TEXT("id"), Value.Id);
            Writer.Field(TEXT("displayName"), Value.DisplayName);
            Writer.OptionalField(TEXT("displayNameZh"), Value.DisplayNameZh);
            Writer.Field(TEXT("value"), Value.Value);
            Writer.Field(TEXT("tooltip"), Value.Tooltip);
            Writer.EndObject();
        }
        Writer.EndArray();
        Writer.EndObject();
    }

    // Localized merge helpers.

    using FJsonObjectsById = TMap<FString, TSharedPtr<FJsonObject>>;

    FJsonObjectsById IndexById(const TSharedPtr<FJsonObject>& Object, const TCHAR* ArrayField)
    {
        FJsonObjectsById Result;
        const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
        if (Object.IsValid() && Object->TryGetArrayField(ArrayField, Values) && Values)
        {
            for (const TSharedPtr<FJsonValue>& Value : *Values)
            {
                const TSharedPtr<FJsonObject>* Element = nullptr;
                FString Id;
                if (Value.IsValid() && Value->TryGetObject(Element) && Element && (*Element)->TryGetStringField(TEXT("id"), Id))
                {
                    // Case-sensitive keys: ids that differ only in case are distinct.
                    Result.Add(Id, *Element);
                }
            }
        }
        return Result;
    }

    const TSharedPtr<FJsonObject>* FindById(const FJsonObjectsById& Index, const FString& Id)
    {
        // TMap<FString> compares case-insensitively; confirm the exact id.
        const TSharedPtr<FJsonObject>* Found = Index.Find(Id);
        FString FoundId;
        return Found && (*Found)->TryGetStringField(TEXT("id"), FoundId) && FoundId.Equals(Id, ESearchCase::CaseSensitive) ? Found : nullptr;
    }

    /** Sets Target to the localized Key of Localized when it is not empty and differs from English; returns whether it did. */
    bool MergeText(const TSharedPtr<FJsonObject>& Localized, const TCHAR* Key, const FString& English, FString& Target)
    {
        FString Value;
        if (!Localized.IsValid() || !Localized->TryGetStringField(Key, Value))
        {
            return false;
        }
        if (Value.IsEmpty() || Value.Equals(English, ESearchCase::CaseSensitive))
        {
            Target.Reset();
            return false;
        }
        Target = Value;
        return true;
    }
}

FUBotDocExporter::FUBotDocExporter()
{
    bPreviousGeneratingDocumentation = UEdGraphSchema_K2::bGeneratingDocumentation;
    UEdGraphSchema_K2::bGeneratingDocumentation = true;

    UEditorStyleSettings* StyleSettings = GetMutableDefault<UEditorStyleSettings>();
    bPreviousShowFriendlyNames = StyleSettings->bShowFriendlyNames;
    StyleSettings->bShowFriendlyNames = true;
}

FUBotDocExporter::~FUBotDocExporter()
{
    if (UBlueprint* TransientBlueprint = Blueprint.Get())
    {
        Blueprint.Reset();
        // Unreachable from now on; the next garbage collection removes it with its generated classes.
        TransientBlueprint->ClearFlags(RF_Public | RF_Standalone);
        TransientBlueprint->MarkAsGarbage();
    }

    UEdGraphSchema_K2::bGeneratingDocumentation = bPreviousGeneratingDocumentation;
    GetMutableDefault<UEditorStyleSettings>()->bShowFriendlyNames = bPreviousShowFriendlyNames;
}

UEdGraph* FUBotDocExporter::GetGraph()
{
    if (!Blueprint.IsValid())
    {
        // An Actor Blueprint is where most nodes are placed: world context pins are hidden there, and
        // calls on other classes show "Target is ..." exactly like in a user's Actor Blueprint.
        UPackage* Outer = GetTransientPackage();
        const FName Name = MakeUniqueObjectName(Outer, UBlueprint::StaticClass(), TEXT("UBotDocExport_Blueprint"));
        UBlueprint* NewBlueprint = FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(), Outer, Name, BPTYPE_Normal,
            UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), TEXT("UBotDocExport"));
        if (!NewBlueprint)
        {
            return nullptr;
        }
        NewBlueprint->SetFlags(RF_Transient);
        Blueprint.Reset(NewBlueprint);
    }
    return FBlueprintEditorUtils::FindEventGraph(Blueprint.Get());
}

bool FUBotDocExporter::SpawnAndRead(UBlueprintNodeSpawner* Spawner, const FString& Id, FUBotDocNode& OutNode, FString& OutError,
    TFunctionRef<void(UEdGraphNode& Node, FUBotDocNode& OutNode)> ReadExtra)
{
    using namespace UBotDocExporterPrivate;

    // The spawner is a transient object nothing else references. The first call of GetGraph creates the
    // Blueprint, and a compile that does not skip garbage collection (CreateBlueprint's skips it today)
    // would free the spawner before Invoke. Keep it alive for the whole call.
    const TStrongObjectPtr<UBlueprintNodeSpawner> SpawnerGuard(Spawner);

    UEdGraph* Graph = GetGraph();
    if (!Graph)
    {
        OutError = FString::Printf(TEXT("%s: could not create the transient Blueprint the nodes are placed in."), *Id);
        return false;
    }

    UEdGraphNode* Node = Spawner ? Spawner->Invoke(Graph, IBlueprintNodeBinder::FBindingSet(), FVector2D::ZeroVector) : nullptr;
    if (!Node)
    {
        OutError = FString::Printf(TEXT("%s: the Blueprint editor could not spawn this node."), *Id);
        return false;
    }
    ON_SCOPE_EXIT
    {
        Graph->RemoveNode(Node, /*bBreakAllLinks*/ true, /*bAlwaysMarkDirty*/ false);
        Node->MarkAsGarbage();
    };

    OutNode = FUBotDocNode();
    OutNode.Id = Id;
    SplitTitle(Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString(), OutNode.Title, OutNode.Subtitle);

    if (const UK2Node* K2Node = Cast<UK2Node>(Node))
    {
        OutNode.bCompact = K2Node->ShouldDrawCompact();
        if (OutNode.bCompact)
        {
            OutNode.CompactTitle = K2Node->GetCompactNodeTitle().ToString();
        }
    }

    const UEdGraphSchema* Schema = Node->GetSchema();
    const bool bIsEvent = Node->IsA<UK2Node_Event>();
    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (!Pin || Pin->bHidden)
        {
            continue;
        }
        // SGraphNodeK2Event draws the delegate output as the small square in the title bar, not as a pin row.
        if (bIsEvent && Pin->PinName == UK2Node_Event::DelegateOutputName)
        {
            continue;
        }

        FUBotDocPin DocPin;
        DocPin.Id = Pin->PinName.ToString();
        DocPin.Name = Node->GetPinDisplayName(Pin).ToString();
        DocPin.Type = MakePinType(Pin->PinType);
        DocPin.bAdvanced = Pin->bAdvancedView;
        if (Pin->Direction == EGPD_Input && Schema && !Schema->ShouldHidePinDefaultValue(Pin))
        {
            DocPin.Default = GetPinDefault(*Pin);
        }
        // Some nodes only build their pin tooltips on request (UK2Node_CallFunction does).
        Node->GetPinHoverText(*Pin, DocPin.Tooltip);
        DocPin.Tooltip.TrimStartAndEndInline();
        (Pin->Direction == EGPD_Input ? OutNode.Inputs : OutNode.Outputs).Add(MoveTemp(DocPin));
    }

    ReadExtra(*Node, OutNode);
    return true;
}

bool FUBotDocExporter::ExportFunctionNode(const UFunction* Function, FUBotDocNode& OutNode, FString& OutError)
{
    if (!Function || !Function->GetOwnerClass())
    {
        OutError = TEXT("No function.");
        return false;
    }
    const FString Id = GetCppName(Function->GetOwnerClass()) + TEXT(".") + Function->GetName();
    // The same spawner the action menu uses, so special call nodes (array, promotable operator, ...) are kept.
    UBlueprintFunctionNodeSpawner* Spawner = UBlueprintFunctionNodeSpawner::Create(Function);
    const bool bSpawned = SpawnAndRead(Spawner, Id, OutNode, OutError, [](UEdGraphNode& Node, FUBotDocNode& Out)
    {
        const UK2Node* K2Node = Cast<UK2Node>(&Node);
        Out.Kind = K2Node && K2Node->IsNodePure() ? TEXT("pure") : TEXT("call");
    });
    if (bSpawned)
    {
        UBotDocExporterPrivate::FillFunctionInfo(*Function, OutNode);
    }
    return bSpawned;
}

bool FUBotDocExporter::ExportEventNode(const UFunction* Function, FUBotDocNode& OutNode, FString& OutError)
{
    if (!Function || !Function->GetOwnerClass())
    {
        OutError = TEXT("No function.");
        return false;
    }
    const FString Id = GetCppName(Function->GetOwnerClass()) + TEXT(".") + Function->GetName();
    if (!Function->HasAnyFunctionFlags(FUNC_BlueprintEvent) || !UEdGraphSchema_K2::FunctionCanBePlacedAsEvent(Function))
    {
        OutError = FString::Printf(TEXT("%s is not an event that can be placed in a graph (BlueprintImplementableEvent / BlueprintNativeEvent without outputs)."), *Id);
        return false;
    }
    UBlueprintEventNodeSpawner* Spawner = UBlueprintEventNodeSpawner::Create(Function);
    const bool bSpawned = SpawnAndRead(Spawner, Id, OutNode, OutError, [](UEdGraphNode&, FUBotDocNode& Out)
    {
        Out.Kind = TEXT("event");
    });
    if (bSpawned)
    {
        UBotDocExporterPrivate::FillFunctionInfo(*Function, OutNode);
    }
    return bSpawned;
}

bool FUBotDocExporter::ExportDelegateNode(const FMulticastDelegateProperty* Property, FUBotDocNode& OutNode, FString& OutError)
{
    using namespace UBotDocExporterPrivate;

    UClass* OwnerClass = Property ? Property->GetOwnerClass() : nullptr;
    UFunction* Signature = Property ? Property->SignatureFunction.Get() : nullptr;
    if (!OwnerClass || !Signature)
    {
        OutError = FString::Printf(TEXT("%s: delegate without owner class or signature."), Property ? *Property->GetName() : TEXT("<null>"));
        return false;
    }

    const FString Id = GetCppName(OwnerClass) + TEXT(".") + Property->GetName();
    // UK2Node_ComponentBoundEvent::InitializeComponentBoundEventParams without a component variable:
    // the pins are the bound event's ("then" + the signature parameters).
    UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(UK2Node_ComponentBoundEvent::StaticClass(), nullptr,
        UBlueprintNodeSpawner::FCustomizeNodeDelegate::CreateLambda([Property, OwnerClass, Signature](UEdGraphNode* NewNode, bool /*bIsTemplateNode*/)
        {
            UK2Node_ComponentBoundEvent* EventNode = CastChecked<UK2Node_ComponentBoundEvent>(NewNode);
            EventNode->DelegatePropertyName = Property->GetFName();
            EventNode->DelegateOwnerClass = OwnerClass->GetAuthoritativeClass();
            EventNode->EventReference.SetFromField<UFunction>(Signature, /*bIsConsideredSelfContext*/ false);
            EventNode->CustomFunctionName = FName(*FString::Printf(TEXT("BndEvt__UBotDocExport_%s_%s"), *Property->GetName(), *Signature->GetName()));
            EventNode->bOverrideFunction = false;
            EventNode->bInternalEvent = true;
        }));

    const bool bSpawned = SpawnAndRead(Spawner, Id, OutNode, OutError, [](UEdGraphNode& Node, FUBotDocNode& Out)
    {
        // The editor title is "<Delegate> (<ComponentVariable>)"; the documented node has no variable.
        Out.Kind = TEXT("event");
        Out.Title = CastChecked<UK2Node_ComponentBoundEvent>(&Node)->GetTargetDelegateDisplayName().ToString();
        Out.Subtitle.Reset();
    });
    if (bSpawned)
    {
        const FString RawTooltip = Property->GetMetaData(NAME_ToolTip);
        OutNode.Owner = GetCppName(OwnerClass);
        OutNode.Member = Property->GetName();
        OutNode.Category = Property->GetMetaData(NAME_Category);
        OutNode.Tooltip = RawTooltip.TrimStartAndEnd();
        OutNode.Keywords = Property->GetMetaData(FBlueprintMetadata::MD_FunctionKeywords);
        OutNode.bDeprecated = Property->HasAnyPropertyFlags(CPF_Deprecated);
        DropRepeatedPinTooltips(OutNode, Signature->GetMetaData(NAME_ToolTip));
    }
    return bSpawned;
}

bool FUBotDocExporter::ExportStructNode(const UScriptStruct* Struct, bool bMake, FUBotDocNode& OutNode, FString& OutError)
{
    using namespace UBotDocExporterPrivate;

    if (!Struct)
    {
        OutError = TEXT("No struct.");
        return false;
    }
    const FString Id = FString(bMake ? TEXT("Make:") : TEXT("Break:")) + GetCppName(Struct);
    const TCHAR* const Kind = bMake ? TEXT("make") : TEXT("break");

    // Structs such as FVector name a native function the editor offers instead of the generic node.
    if (const FString* NativeFunctionPath = Struct->FindMetaData(bMake ? FBlueprintMetadata::MD_NativeMakeFunction : FBlueprintMetadata::MD_NativeBreakFunction))
    {
        const UFunction* NativeFunction = FindNativeStructFunction(*NativeFunctionPath);
        if (!NativeFunction)
        {
            OutError = FString::Printf(TEXT("%s: native function '%s' not found."), *Id, **NativeFunctionPath);
            return false;
        }
        if (!ExportFunctionNode(NativeFunction, OutNode, OutError))
        {
            return false;
        }
        OutNode.Id = Id;
        OutNode.Kind = Kind;
        OutNode.Owner.Reset();
        OutNode.Member.Reset();
        return true;
    }

    const TSubclassOf<UK2Node> NodeClass = bMake ? UK2Node_MakeStruct::StaticClass() : UK2Node_BreakStruct::StaticClass();
    UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(NodeClass, nullptr,
        UBlueprintNodeSpawner::FCustomizeNodeDelegate::CreateLambda([Struct](UEdGraphNode* NewNode, bool /*bIsTemplateNode*/)
        {
            CastChecked<UK2Node_StructOperation>(NewNode)->StructType = const_cast<UScriptStruct*>(Struct);
        }));
    return SpawnAndRead(Spawner, Id, OutNode, OutError, [Kind](UEdGraphNode& Node, FUBotDocNode& Out)
    {
        Out.Kind = Kind;
        FillNodeInfo(Node, Out);
    });
}

bool FUBotDocExporter::ExportSpecialNode(UClass* NodeClass, FUBotDocNode& OutNode, FString& OutError)
{
    if (!NodeClass || !NodeClass->IsChildOf<UK2Node>() || NodeClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated))
    {
        OutError = FString::Printf(TEXT("%s is not a placeable K2 node class."), NodeClass ? *NodeClass->GetPathName() : TEXT("<null>"));
        return false;
    }
    return SpawnAndRead(UBlueprintNodeSpawner::Create(NodeClass), NodeClass->GetName(), OutNode, OutError, [](UEdGraphNode& Node, FUBotDocNode& Out)
    {
        Out.Kind = TEXT("node");
        UBotDocExporterPrivate::FillNodeInfo(Node, Out);
    });
}

bool FUBotDocExporter::ExportMacroNode(UEdGraph* MacroGraph, FUBotDocNode& OutNode, FString& OutError)
{
    if (!MacroGraph)
    {
        OutError = TEXT("No macro graph.");
        return false;
    }
    UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(UK2Node_MacroInstance::StaticClass(), nullptr,
        UBlueprintNodeSpawner::FCustomizeNodeDelegate::CreateLambda([MacroGraph](UEdGraphNode* NewNode, bool /*bIsTemplateNode*/)
        {
            CastChecked<UK2Node_MacroInstance>(NewNode)->SetMacroGraph(MacroGraph);
        }));
    return SpawnAndRead(Spawner, TEXT("Macro:") + MacroGraph->GetName(), OutNode, OutError, [](UEdGraphNode& Node, FUBotDocNode& Out)
    {
        Out.Kind = TEXT("node");
        UBotDocExporterPrivate::FillNodeInfo(Node, Out);
    });
}

void FUBotDocExporter::ExportClass(UClass* Class, const FString& ModuleName, FUBotDocApi& OutApi, TArray<FString>& OutErrors)
{
    using namespace UBotDocExporterPrivate;

    if (Class->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists) || IsTestHeader(Class->GetMetaData(NAME_ModuleRelativePath)))
    {
        return;
    }

    const FString ClassId = GetCppName(Class);
    TArray<FUBotDocNode> ClassNodes;

    for (TFieldIterator<UFunction> It(Class, EFieldIteratorFlags::ExcludeSuper); It; ++It)
    {
        const UFunction* Function = *It;
        if (!IsDocumentedFunction(*Function))
        {
            continue;
        }

        FUBotDocNode Node;
        FString Error;
        bool bExported = false;
        if (Function->HasAnyFunctionFlags(FUNC_BlueprintCallable))
        {
            bExported = ExportFunctionNode(Function, Node, Error);
        }
        else if (UEdGraphSchema_K2::FunctionCanBePlacedAsEvent(Function))
        {
            bExported = ExportEventNode(Function, Node, Error);
        }
        else
        {
            // Overridable functions with outputs are implemented as function graphs, not as nodes.
            UE_LOG(LogUBot, Display, TEXT("%s.%s is an overridable function with outputs and has no node; skipped."), *ClassId, *Function->GetName());
            continue;
        }

        if (bExported)
        {
            ClassNodes.Add(MoveTemp(Node));
        }
        else
        {
            OutErrors.Add(Error);
        }
    }

    for (TFieldIterator<FMulticastDelegateProperty> It(Class, EFieldIteratorFlags::ExcludeSuper); It; ++It)
    {
        const FMulticastDelegateProperty* Property = *It;
        if (!Property->HasAnyPropertyFlags(CPF_BlueprintAssignable) || Property->HasAnyPropertyFlags(CPF_Deprecated))
        {
            continue;
        }
        FUBotDocNode Node;
        FString Error;
        if (ExportDelegateNode(Property, Node, Error))
        {
            ClassNodes.Add(MoveTemp(Node));
        }
        else
        {
            OutErrors.Add(Error);
        }
    }

    FUBotDocClass Doc;
    const UObject* Defaults = Class->GetDefaultObject();
    const auto AddProperty = [&Doc, Defaults, &ClassId](const FProperty* Property)
    {
        FUBotDocProperty& DocProperty = Doc.Properties.AddDefaulted_GetRef();
        DocProperty.Id = Property->GetName();
        DocProperty.DisplayName = Property->GetDisplayNameText().ToString();
        FEdGraphPinType PinType;
        if (!GetDefault<UEdGraphSchema_K2>()->ConvertPropertyToPinType(Property, PinType))
        {
            UE_LOG(LogUBot, Warning, TEXT("%s.%s has no Blueprint pin type."), *ClassId, *Property->GetName());
        }
        DocProperty.Type = MakePinType(PinType);
        if (Defaults)
        {
            if (const FTextProperty* TextProperty = CastField<FTextProperty>(Property))
            {
                DocProperty.Default = TextProperty->GetPropertyValue_InContainer(Defaults).ToString();
            }
            else
            {
                // ExportText_Direct skips a value identical to its delta, and a null delta means "zero":
                // pass the value itself so false, 0 and None are written. Structs and containers keep
                // the null delta, which writes their non-zero members.
                const void* Value = Property->ContainerPtrToValuePtr<void>(Defaults);
                const bool bAggregate = Property->IsA<FStructProperty>() || Property->IsA<FArrayProperty>()
                    || Property->IsA<FSetProperty>() || Property->IsA<FMapProperty>();
                Property->ExportText_Direct(DocProperty.Default, Value, bAggregate ? nullptr : Value, nullptr, PPF_None);
            }
        }
        DocProperty.Category = Property->GetMetaData(NAME_Category);
        DocProperty.Access = GetAccess(*Property);
        DocProperty.Editable = GetEditable(*Property);
        DocProperty.Tooltip = Property->GetMetaData(NAME_ToolTip);
        for (const TCHAR* MetaKey : PropertyMetaKeys)
        {
            if (const FString* MetaValue = Property->FindMetaData(MetaKey))
            {
                DocProperty.Meta.Emplace(MetaKey, *MetaValue);
            }
        }
    };

    for (TFieldIterator<FProperty> It(Class, EFieldIteratorFlags::ExcludeSuper); It; ++It)
    {
        if (IsDocumentedProperty(**It))
        {
            AddProperty(*It);
        }
    }
    const bool bHasOwnProperties = !Doc.Properties.IsEmpty();

    // Inherited plugin properties whose default this class changes (a sensor's Update Rate Hz, Sensor
    // Id, ...) are listed again with this class's value; the parent's entry keeps the parent's value.
    // Unchanged inherited properties stay documented on the class that declares them only.
    const UClass* SuperClass = Class->GetSuperClass();
    const UObject* SuperDefaults = SuperClass ? SuperClass->GetDefaultObject() : nullptr;
    if (Defaults && SuperDefaults)
    {
        for (TFieldIterator<FProperty> It(SuperClass, EFieldIteratorFlags::IncludeSuper); It; ++It)
        {
            const FProperty* Property = *It;
            if (IsDocumentedProperty(*Property) && IsDeclaredInPlugin(*Property) && !HasSameDefault(*Property, *Defaults, *SuperDefaults))
            {
                AddProperty(Property);
            }
        }
    }

    // Classes that are Blueprint types only by inheritance (commandlets, internal helpers) are left
    // out unless they add nodes or properties of their own.
    const bool bMarkedForBlueprints = Class->GetBoolMetaData(NAME_BlueprintType) || Class->GetBoolMetaData(NAME_IsBlueprintBase)
        || Class->HasMetaData(NAME_BlueprintSpawnableComponent);
    if (!bMarkedForBlueprints && ClassNodes.IsEmpty() && !bHasOwnProperties)
    {
        return;
    }

    const bool bIsInterface = Class->HasAnyClassFlags(CLASS_Interface);
    Doc.Id = ClassId;
    Doc.DisplayName = Class->GetDisplayNameText().ToString();
    Doc.Kind = GetClassKind(*Class);
    Doc.Parent = GetCppName(Class->GetSuperClass());
    Doc.Module = ModuleName;
    Doc.Header = GetHeaderPath(Class, ModuleName);
    Doc.Category = Class->HasMetaData(NAME_ClassGroupNames) ? Class->GetMetaData(NAME_ClassGroupNames) : Class->GetMetaData(NAME_Category);
    Doc.Tooltip = Class->GetMetaData(NAME_ToolTip);
    Doc.bAbstract = Class->HasAnyClassFlags(CLASS_Abstract);
    Doc.bBlueprintType = UEdGraphSchema_K2::IsAllowableBlueprintVariableType(Class);
    Doc.bBlueprintable = bIsInterface
        ? !Class->GetBoolMetaData(FBlueprintMetadata::MD_CannotImplementInterfaceInBlueprint)
        : FKismetEditorUtilities::CanCreateBlueprintOfClass(Class);
    // What "Add Component" offers (abstract classes never), or for actors what can be placed.
    Doc.bSpawnable = Class->IsChildOf<UActorComponent>()
        ? (Class->HasMetaData(NAME_BlueprintSpawnableComponent) && !Doc.bAbstract)
        : (Class->IsChildOf<AActor>() && !Class->HasAnyClassFlags(CLASS_Abstract | CLASS_NotPlaceable));
    Doc.bDeprecated = Class->HasMetaData(NAME_DeprecationMessage);

    for (FUBotDocNode& Node : ClassNodes)
    {
        Doc.Nodes.Add(Node.Id);
        OutApi.Nodes.Add(MoveTemp(Node));
    }
    OutApi.Classes.Add(MoveTemp(Doc));
}

void FUBotDocExporter::ExportStruct(UScriptStruct* Struct, const FString& ModuleName, FUBotDocApi& OutApi, TArray<FString>& OutErrors)
{
    using namespace UBotDocExporterPrivate;

    if (IsTestHeader(Struct->GetMetaData(NAME_ModuleRelativePath)) || !UEdGraphSchema_K2::IsAllowableBlueprintVariableType(Struct))
    {
        return;
    }

    FUBotDocStruct& Doc = OutApi.Structs.AddDefaulted_GetRef();
    Doc.Id = GetCppName(Struct);
    Doc.DisplayName = Struct->GetDisplayNameText().ToString();
    Doc.Module = ModuleName;
    Doc.Header = GetHeaderPath(Struct, ModuleName);
    Doc.Tooltip = Struct->GetMetaData(NAME_ToolTip);

    const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
    for (TFieldIterator<FProperty> It(Struct); It; ++It)
    {
        const FProperty* Property = *It;
        if (!IsDocumentedProperty(*Property))
        {
            continue;
        }
        FUBotDocField& Field = Doc.Fields.AddDefaulted_GetRef();
        Field.Id = Property->GetName();
        Field.DisplayName = Property->GetDisplayNameText().ToString();
        FEdGraphPinType PinType;
        Schema->ConvertPropertyToPinType(Property, PinType);
        Field.Type = MakePinType(PinType);
        Field.Tooltip = Property->GetMetaData(NAME_ToolTip);
        Field.Access = GetAccess(*Property);
    }

    const bool bHasMake = Struct->HasMetaData(FBlueprintMetadata::MD_NativeMakeFunction) || UK2Node_MakeStruct::CanBeMade(Struct);
    const bool bHasBreak = Struct->HasMetaData(FBlueprintMetadata::MD_NativeBreakFunction) || CanBeBroken(*Struct);
    for (const bool bMake : { true, false })
    {
        if (bMake ? !bHasMake : !bHasBreak)
        {
            continue;
        }
        FUBotDocNode Node;
        FString Error;
        if (ExportStructNode(Struct, bMake, Node, Error))
        {
            (bMake ? Doc.Make : Doc.Break) = Node.Id;
            OutApi.Nodes.Add(MoveTemp(Node));
        }
        else
        {
            OutErrors.Add(Error);
        }
    }
}

void FUBotDocExporter::ExportEnum(UEnum* Enum, const FString& ModuleName, FUBotDocApi& OutApi)
{
    using namespace UBotDocExporterPrivate;

    if (IsTestHeader(Enum->GetMetaData(*NAME_ModuleRelativePath.ToString())) || !UEdGraphSchema_K2::IsAllowableBlueprintVariableType(Enum))
    {
        return;
    }

    FUBotDocEnum& Doc = OutApi.Enums.AddDefaulted_GetRef();
    Doc.Id = GetCppName(Enum);
    Doc.DisplayName = Enum->GetDisplayNameText().ToString();
    Doc.Module = ModuleName;
    Doc.Header = GetHeaderPath(Enum, ModuleName);
    Doc.Tooltip = Enum->GetMetaData(*NAME_ToolTip.ToString());

    const int32 NumEnums = Enum->NumEnums();
    for (int32 Index = 0; Index < NumEnums; ++Index)
    {
        // UHT always appends <Enum>_MAX (or keeps a declared MAX) as the last entry.
        const FString Name = Enum->GetNameStringByIndex(Index);
        if ((Index == NumEnums - 1 && Name.EndsWith(TEXT("MAX"), ESearchCase::CaseSensitive)) || Enum->HasMetaData(*NAME_Hidden.ToString(), Index))
        {
            continue;
        }
        FUBotDocEnumValue& Value = Doc.Values.AddDefaulted_GetRef();
        Value.Id = Name;
        Value.DisplayName = Enum->GetDisplayNameTextByIndex(Index).ToString();
        Value.Value = Enum->GetValueByIndex(Index);
        Value.Tooltip = Enum->GetMetaData(*NAME_ToolTip.ToString(), Index);
    }
}

bool FUBotDocExporter::ExportPlugin(const FString& PluginName, FUBotDocApi& OutApi, TArray<FString>& OutErrors)
{
    using namespace UBotDocExporterPrivate;

    const int32 ErrorsBefore = OutErrors.Num();
    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(PluginName);
    if (!Plugin.IsValid())
    {
        OutErrors.Add(FString::Printf(TEXT("Plugin '%s' was not found."), *PluginName));
        return false;
    }
    if (!Plugin->IsEnabled())
    {
        OutErrors.Add(FString::Printf(TEXT("Plugin '%s' is not enabled in this project."), *PluginName));
        return false;
    }

    OutApi = FUBotDocApi();
    OutApi.Plugin = Plugin->GetName();
    OutApi.Version = Plugin->GetDescriptor().VersionName;
    OutApi.Engine = FString::Printf(TEXT("%d.%d"), ENGINE_MAJOR_VERSION, ENGINE_MINOR_VERSION);
    OutApi.Repository = ReadRepository(Plugin->GetDescriptorFileName());
    OutApi.Generator = GeneratorName;
    OutApi.Culture = GetCurrentCultureName();

    FModuleManager& ModuleManager = FModuleManager::Get();
    for (const FModuleDescriptor& Module : Plugin->GetDescriptor().Modules)
    {
        const FString ModuleName = Module.Name.ToString();
        OutApi.Modules.Add({ ModuleName, EHostType::ToString(Module.Type) });

        if (!ModuleManager.IsModuleLoaded(Module.Name) && !ModuleManager.LoadModule(Module.Name))
        {
            OutErrors.Add(FString::Printf(TEXT("Module %s of plugin %s could not be loaded."), *ModuleName, *OutApi.Plugin));
            continue;
        }

        // A module without reflected types has no script package.
        const UPackage* Package = FindPackage(nullptr, *(TEXT("/Script/") + ModuleName));
        if (!Package)
        {
            continue;
        }

        TArray<UObject*> Objects;
        GetObjectsWithPackage(Package, Objects, /*bIncludeNestedObjects*/ false);
        for (UObject* Object : Objects)
        {
            if (UClass* Class = Cast<UClass>(Object))
            {
                ExportClass(Class, ModuleName, OutApi, OutErrors);
            }
            else if (UScriptStruct* Struct = Cast<UScriptStruct>(Object))
            {
                ExportStruct(Struct, ModuleName, OutApi, OutErrors);
            }
            else if (UEnum* Enum = Cast<UEnum>(Object))
            {
                ExportEnum(Enum, ModuleName, OutApi);
            }
        }
    }

    SortApi(OutApi);
    return OutErrors.Num() == ErrorsBefore;
}

bool FUBotDocExporter::ExportNodeList(const TArray<FUBotDocNodeListEntry>& Entries, FUBotDocApi& OutApi, TArray<FString>& OutErrors)
{
    using namespace UBotDocExporterPrivate;
    using EKind = FUBotDocNodeListEntry::EKind;

    const int32 ErrorsBefore = OutErrors.Num();
    OutApi = FUBotDocApi();
    OutApi.Plugin = TEXT("Engine");
    OutApi.Version = FEngineVersion::Current().ToString(EVersionComponent::Patch);
    OutApi.Engine = FString::Printf(TEXT("%d.%d"), ENGINE_MAJOR_VERSION, ENGINE_MINOR_VERSION);
    OutApi.Generator = GeneratorName;
    OutApi.Culture = GetCurrentCultureName();

    TSet<FString> SeenIds;
    for (const FUBotDocNodeListEntry& Entry : Entries)
    {
        const FString Spec = Entry.Member.IsEmpty() ? Entry.Path : Entry.Path + TEXT(":") + Entry.Member;
        FUBotDocNode Node;
        FString Error;
        bool bExported = false;

        switch (Entry.Kind)
        {
        case EKind::Function:
        case EKind::Event:
        {
            const UClass* Class = FindClassByPath(Entry.Path);
            const UFunction* Function = Class ? Class->FindFunctionByName(*Entry.Member) : nullptr;
            if (!Class)
            {
                Error = FString::Printf(TEXT("class '%s' not found"), *Entry.Path);
            }
            else if (!Function)
            {
                Error = FString::Printf(TEXT("function '%s' not found in %s"), *Entry.Member, *Entry.Path);
            }
            else if (Entry.Kind == EKind::Function && !Function->HasAnyFunctionFlags(FUNC_BlueprintCallable))
            {
                Error = FString::Printf(TEXT("'%s' is not BlueprintCallable (use 'event' for overridable events)"), *Spec);
            }
            else
            {
                bExported = Entry.Kind == EKind::Function ? ExportFunctionNode(Function, Node, Error) : ExportEventNode(Function, Node, Error);
            }
            break;
        }
        case EKind::Node:
        {
            UClass* NodeClass = FindClassByPath(Entry.Path);
            if (!NodeClass)
            {
                Error = FString::Printf(TEXT("node class '%s' not found"), *Entry.Path);
            }
            else
            {
                bExported = ExportSpecialNode(NodeClass, Node, Error);
            }
            break;
        }
        case EKind::Macro:
        {
            const UBlueprint* Library = LoadObject<UBlueprint>(nullptr, *Entry.Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
            UEdGraph* MacroGraph = nullptr;
            if (Library)
            {
                for (UEdGraph* Graph : Library->MacroGraphs)
                {
                    if (Graph && Graph->GetName() == Entry.Member)
                    {
                        MacroGraph = Graph;
                        break;
                    }
                }
            }
            if (!Library)
            {
                Error = FString::Printf(TEXT("macro library '%s' not found"), *Entry.Path);
            }
            else if (!MacroGraph)
            {
                Error = FString::Printf(TEXT("macro '%s' not found in %s"), *Entry.Member, *Entry.Path);
            }
            else
            {
                bExported = ExportMacroNode(MacroGraph, Node, Error);
            }
            break;
        }
        }

        if (!bExported)
        {
            OutErrors.Add(FString::Printf(TEXT("Line %d: %s: %s."), Entry.Line, *Spec, *Error.TrimChar(TEXT('.'))));
            continue;
        }
        if (SeenIds.Contains(Node.Id))
        {
            OutErrors.Add(FString::Printf(TEXT("Line %d: %s: node %s is listed twice."), Entry.Line, *Spec, *Node.Id));
            continue;
        }
        SeenIds.Add(Node.Id);
        OutApi.Nodes.Add(MoveTemp(Node));
    }

    SortApi(OutApi);
    return OutErrors.Num() == ErrorsBefore;
}

bool FUBotDocExporter::ParseNodeList(const FString& Text, TArray<FUBotDocNodeListEntry>& OutEntries, TArray<FString>& OutErrors)
{
    using EKind = FUBotDocNodeListEntry::EKind;

    const int32 ErrorsBefore = OutErrors.Num();
    TArray<FString> Lines;
    Text.ParseIntoArray(Lines, TEXT("\n"), /*InCullEmpty*/ false);

    for (int32 LineIndex = 0; LineIndex < Lines.Num(); ++LineIndex)
    {
        const int32 LineNumber = LineIndex + 1;
        FString Line = Lines[LineIndex];
        int32 CommentStart = INDEX_NONE;
        if (Line.FindChar(TEXT('#'), CommentStart))
        {
            Line.LeftInline(CommentStart);
        }
        Line.TrimStartAndEndInline();
        if (Line.IsEmpty())
        {
            continue;
        }

        auto AddError = [&OutErrors, LineNumber, &Line](const FString& Message)
        {
            OutErrors.Add(FString::Printf(TEXT("Line %d: %s: '%s'."), LineNumber, *Message, *Line));
        };

        TArray<FString> Tokens;
        Line.ParseIntoArrayWS(Tokens);

        FUBotDocNodeListEntry Entry;
        Entry.Line = LineNumber;
        FString Spec;
        if (Tokens.Num() == 1)
        {
            Entry.Kind = EKind::Function;
            Spec = Tokens[0];
        }
        else if (Tokens.Num() == 2)
        {
            if (Tokens[0].Equals(TEXT("event"), ESearchCase::IgnoreCase))
            {
                Entry.Kind = EKind::Event;
            }
            else if (Tokens[0].Equals(TEXT("node"), ESearchCase::IgnoreCase))
            {
                Entry.Kind = EKind::Node;
            }
            else if (Tokens[0].Equals(TEXT("macro"), ESearchCase::IgnoreCase))
            {
                Entry.Kind = EKind::Macro;
            }
            else
            {
                AddError(FString::Printf(TEXT("unknown entry kind '%s' (expected event, node or macro)"), *Tokens[0]));
                continue;
            }
            Spec = Tokens[1];
        }
        else
        {
            AddError(TEXT("expected '[event|node|macro] <path>'"));
            continue;
        }

        if (!Spec.StartsWith(TEXT("/")))
        {
            AddError(TEXT("the path must start with '/'"));
            continue;
        }

        if (Entry.Kind == EKind::Node)
        {
            if (Spec.Contains(TEXT(":")))
            {
                AddError(TEXT("a node entry is a K2 node class path without ':'"));
                continue;
            }
            Entry.Path = Spec;
        }
        else
        {
            int32 Colon = INDEX_NONE;
            if (!Spec.FindLastChar(TEXT(':'), Colon) || Colon == 0 || Colon == Spec.Len() - 1)
            {
                AddError(Entry.Kind == EKind::Macro ? TEXT("expected <BlueprintPath>:<MacroName>") : TEXT("expected <ClassPath>:<FunctionName>"));
                continue;
            }
            Entry.Path = Spec.Left(Colon);
            Entry.Member = Spec.Mid(Colon + 1);
        }
        OutEntries.Add(MoveTemp(Entry));
    }

    return OutErrors.Num() == ErrorsBefore;
}

FString FUBotDocExporter::ToJson(const FUBotDocApi& Api)
{
    using namespace UBotDocExporterPrivate;

    FJsonTextWriter Writer;
    Writer.BeginObject();
    Writer.Field(TEXT("format"), static_cast<int64>(Api.Format));
    Writer.Field(TEXT("plugin"), Api.Plugin);
    Writer.Field(TEXT("version"), Api.Version);
    Writer.Field(TEXT("engine"), Api.Engine);
    Writer.Field(TEXT("repository"), Api.Repository);
    Writer.Field(TEXT("generator"), Api.Generator);
    Writer.Field(TEXT("culture"), Api.Culture);

    Writer.Key(TEXT("modules"));
    Writer.BeginArray();
    for (const FUBotDocModule& Module : Api.Modules)
    {
        Writer.BeginObject();
        Writer.Field(TEXT("name"), Module.Name);
        Writer.Field(TEXT("type"), Module.Type);
        Writer.EndObject();
    }
    Writer.EndArray();

    Writer.Key(TEXT("classes"));
    Writer.BeginArray();
    for (const FUBotDocClass& Class : Api.Classes)
    {
        WriteClass(Writer, Class);
    }
    Writer.EndArray();

    Writer.Key(TEXT("nodes"));
    Writer.BeginArray();
    for (const FUBotDocNode& Node : Api.Nodes)
    {
        WriteNode(Writer, Node);
    }
    Writer.EndArray();

    Writer.Key(TEXT("structs"));
    Writer.BeginArray();
    for (const FUBotDocStruct& Struct : Api.Structs)
    {
        WriteStruct(Writer, Struct);
    }
    Writer.EndArray();

    Writer.Key(TEXT("enums"));
    Writer.BeginArray();
    for (const FUBotDocEnum& Enum : Api.Enums)
    {
        WriteEnum(Writer, Enum);
    }
    Writer.EndArray();

    Writer.EndObject();
    return Writer.Finish();
}

void FUBotDocExporter::SortApi(FUBotDocApi& Api)
{
    using UBotDocExporterPrivate::IdLess;

    const auto ById = [](const auto& A, const auto& B) { return IdLess(A.Id, B.Id); };

    Api.Classes.StableSort(ById);
    for (FUBotDocClass& Class : Api.Classes)
    {
        Class.Properties.StableSort(ById);
        Class.Nodes.StableSort(&IdLess);
    }
    Api.Nodes.StableSort(ById);
    Api.Structs.StableSort(ById);
    for (FUBotDocStruct& Struct : Api.Structs)
    {
        Struct.Fields.StableSort(ById);
    }
    Api.Enums.StableSort(ById);
    for (FUBotDocEnum& Enum : Api.Enums)
    {
        Enum.Values.StableSort(ById);
    }
}

bool FUBotDocExporter::MergeLocalized(FUBotDocApi& Api, const FString& LocalizedJson, int32& OutFieldCount, FString& OutError)
{
    using namespace UBotDocExporterPrivate;

    OutFieldCount = 0;
    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(LocalizedJson), Root) || !Root.IsValid())
    {
        OutError = TEXT("The localized export is not valid JSON.");
        return false;
    }
    FString LocalizedPlugin;
    if (!Root->TryGetStringField(TEXT("plugin"), LocalizedPlugin) || !LocalizedPlugin.Equals(Api.Plugin, ESearchCase::CaseSensitive))
    {
        OutError = FString::Printf(TEXT("The localized export documents plugin '%s', expected '%s'."), *LocalizedPlugin, *Api.Plugin);
        return false;
    }
    // The *Zh fields extend the English document (the contract's main api.json), so neither side may be mixed up.
    if (Api.Culture != TEXT("en"))
    {
        OutError = FString::Printf(TEXT("The export to merge into has culture '%s'; it must be the English export (culture 'en', run with -culture=en)."), *Api.Culture);
        return false;
    }
    FString LocalizedCulture;
    if (!Root->TryGetStringField(TEXT("culture"), LocalizedCulture) || LocalizedCulture.IsEmpty())
    {
        OutError = TEXT("The localized export has no \"culture\" field.");
        return false;
    }
    if (LocalizedCulture == Api.Culture)
    {
        OutError = FString::Printf(TEXT("The localized export has the same culture ('%s') as the English export; make it with -culture=zh-Hans."), *LocalizedCulture);
        return false;
    }

    int32 Count = 0;
    auto Merge = [&Count](const TSharedPtr<FJsonObject>& Localized, const TCHAR* Key, const FString& English, FString& Target)
    {
        Count += MergeText(Localized, Key, English, Target) ? 1 : 0;
    };

    const FJsonObjectsById Classes = IndexById(Root, TEXT("classes"));
    for (FUBotDocClass& Class : Api.Classes)
    {
        if (const TSharedPtr<FJsonObject>* Localized = FindById(Classes, Class.Id))
        {
            Merge(*Localized, TEXT("displayName"), Class.DisplayName, Class.DisplayNameZh);
            const FJsonObjectsById Properties = IndexById(*Localized, TEXT("properties"));
            for (FUBotDocProperty& Property : Class.Properties)
            {
                if (const TSharedPtr<FJsonObject>* LocalizedProperty = FindById(Properties, Property.Id))
                {
                    Merge(*LocalizedProperty, TEXT("displayName"), Property.DisplayName, Property.DisplayNameZh);
                }
            }
        }
    }

    const FJsonObjectsById Nodes = IndexById(Root, TEXT("nodes"));
    for (FUBotDocNode& Node : Api.Nodes)
    {
        const TSharedPtr<FJsonObject>* Localized = FindById(Nodes, Node.Id);
        if (!Localized)
        {
            continue;
        }
        Merge(*Localized, TEXT("title"), Node.Title, Node.TitleZh);
        Merge(*Localized, TEXT("subtitle"), Node.Subtitle, Node.SubtitleZh);
        auto MergePins = [&Merge, Localized](const TCHAR* Field, TArray<FUBotDocPin>& Pins)
        {
            const FJsonObjectsById LocalizedPins = IndexById(*Localized, Field);
            for (FUBotDocPin& Pin : Pins)
            {
                if (const TSharedPtr<FJsonObject>* LocalizedPin = FindById(LocalizedPins, Pin.Id))
                {
                    Merge(*LocalizedPin, TEXT("name"), Pin.Name, Pin.NameZh);
                }
            }
        };
        MergePins(TEXT("inputs"), Node.Inputs);
        MergePins(TEXT("outputs"), Node.Outputs);
    }

    const FJsonObjectsById Structs = IndexById(Root, TEXT("structs"));
    for (FUBotDocStruct& Struct : Api.Structs)
    {
        if (const TSharedPtr<FJsonObject>* Localized = FindById(Structs, Struct.Id))
        {
            Merge(*Localized, TEXT("displayName"), Struct.DisplayName, Struct.DisplayNameZh);
            const FJsonObjectsById Fields = IndexById(*Localized, TEXT("fields"));
            for (FUBotDocField& Field : Struct.Fields)
            {
                if (const TSharedPtr<FJsonObject>* LocalizedField = FindById(Fields, Field.Id))
                {
                    Merge(*LocalizedField, TEXT("displayName"), Field.DisplayName, Field.DisplayNameZh);
                }
            }
        }
    }

    const FJsonObjectsById Enums = IndexById(Root, TEXT("enums"));
    for (FUBotDocEnum& Enum : Api.Enums)
    {
        if (const TSharedPtr<FJsonObject>* Localized = FindById(Enums, Enum.Id))
        {
            Merge(*Localized, TEXT("displayName"), Enum.DisplayName, Enum.DisplayNameZh);
            const FJsonObjectsById Values = IndexById(*Localized, TEXT("values"));
            for (FUBotDocEnumValue& Value : Enum.Values)
            {
                if (const TSharedPtr<FJsonObject>* LocalizedValue = FindById(Values, Value.Id))
                {
                    Merge(*LocalizedValue, TEXT("displayName"), Value.DisplayName, Value.DisplayNameZh);
                }
            }
        }
    }

    OutFieldCount = Count;
    return true;
}

FUBotDocPinType FUBotDocExporter::MakePinType(const FEdGraphPinType& PinType)
{
    FUBotDocPinType Out;
    const FName Category = PinType.PinCategory;
    const UObject* SubObject = PinType.PinSubCategoryObject.Get();

    if (Category == UEdGraphSchema_K2::PC_Real || Category == UEdGraphSchema_K2::PC_Float || Category == UEdGraphSchema_K2::PC_Double)
    {
        // UE5 "real" pins carry the precision in the sub-category.
        const FName Precision = Category == UEdGraphSchema_K2::PC_Real ? PinType.PinSubCategory : Category;
        Out.Category = Precision == UEdGraphSchema_K2::PC_Float ? TEXT("float") : TEXT("double");
    }
    else if ((Category == UEdGraphSchema_K2::PC_Byte || Category == UEdGraphSchema_K2::PC_Enum) && Cast<UEnum>(SubObject))
    {
        const UEnum* Enum = CastChecked<UEnum>(SubObject);
        Out.Category = TEXT("enum");
        Out.Subtype = GetCppName(Enum);
        Out.SubtypeDisplay = Enum->GetDisplayNameText().ToString();
    }
    else if (Category == UEdGraphSchema_K2::PC_Struct)
    {
        const UScriptStruct* Struct = Cast<UScriptStruct>(SubObject);
        if (Struct && Struct == TBaseStructure<FVector>::Get())
        {
            Out.Category = TEXT("vector");
        }
        else if (Struct && Struct == TBaseStructure<FRotator>::Get())
        {
            Out.Category = TEXT("rotator");
        }
        else if (Struct && Struct == TBaseStructure<FTransform>::Get())
        {
            Out.Category = TEXT("transform");
        }
        else
        {
            Out.Category = TEXT("struct");
            if (Struct)
            {
                Out.Subtype = GetCppName(Struct);
                Out.SubtypeDisplay = Struct->GetDisplayNameText().ToString();
            }
        }
    }
    else
    {
        Out.Category = Category.ToString();
        if (const UClass* Class = Cast<UClass>(SubObject))
        {
            Out.Subtype = GetCppName(Class);
            Out.SubtypeDisplay = Class->GetDisplayNameText().ToString();
        }
    }

    switch (PinType.ContainerType)
    {
    case EPinContainerType::Array:
        Out.Container = TEXT("array");
        break;
    case EPinContainerType::Set:
        Out.Container = TEXT("set");
        break;
    case EPinContainerType::Map:
        Out.Container = TEXT("map");
        Out.ValueType = MakeShared<FUBotDocPinType>(MakePinType(FEdGraphPinType::GetPinTypeForTerminalType(PinType.PinValueType)));
        break;
    default:
        Out.Container = TEXT("none");
        break;
    }

    Out.bByRef = PinType.bIsReference;
    Out.bConst = PinType.bIsConst;
    return Out;
}

FString FUBotDocExporter::GetCppName(const UObject* Type)
{
    if (const UClass* Class = Cast<UClass>(Type))
    {
        return FString(Class->GetPrefixCPP()) + Class->GetName();
    }
    if (const UScriptStruct* Struct = Cast<UScriptStruct>(Type))
    {
        return Struct->GetStructCPPName();
    }
    if (const UEnum* Enum = Cast<UEnum>(Type))
    {
        return Enum->GetName();
    }
    return FString();
}

FString FUBotDocExporter::GetHeaderPath(const UStruct* Type, const FString& ModuleName)
{
    const FString* RelativePath = Type ? Type->FindMetaData(UBotDocExporterPrivate::NAME_ModuleRelativePath) : nullptr;
    return RelativePath && !RelativePath->IsEmpty()
        ? FString::Printf(TEXT("Source/%s/%s"), *ModuleName, *RelativePath->Replace(TEXT("\\"), TEXT("/")))
        : FString();
}

FString FUBotDocExporter::GetHeaderPath(const UEnum* Enum, const FString& ModuleName)
{
    const FString RelativePath = Enum ? Enum->GetMetaData(*UBotDocExporterPrivate::NAME_ModuleRelativePath.ToString()) : FString();
    return RelativePath.IsEmpty() ? FString() : FString::Printf(TEXT("Source/%s/%s"), *ModuleName, *RelativePath.Replace(TEXT("\\"), TEXT("/")));
}

FString FUBotDocExporter::GetCurrentCultureName()
{
    const FCultureRef Language = FInternationalization::Get().GetCurrentLanguage();
    // The contract has one English: "en-US" or "en-GB" would make the same text look like another file.
    return Language->GetTwoLetterISOLanguageName() == TEXT("en") ? FString(TEXT("en")) : Language->GetName();
}
