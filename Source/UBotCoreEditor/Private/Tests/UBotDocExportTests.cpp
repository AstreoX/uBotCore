#include "BlueprintActionDatabase.h"
#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Internationalization/Internationalization.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UBotActuatorInterface.h"
#include "UBotClockSubsystem.h"
#include "UBotConventionsLibrary.h"
#include "UBotDocExportCommandlet.h"
#include "UBotDocExporter.h"
#include "UBotIdentityComponent.h"
#include "UBotPackageLibrary.h"
#include "UBotTypes.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace UBotDocExportTestsPrivate
{
    // Sets the process language for one test and puts back whatever it was (a test, or the code under
    // test, may change it). The editor may have been started in any language.
    class FScopedLanguage
    {
    public:
        UE_NONCOPYABLE(FScopedLanguage);

        explicit FScopedLanguage(const TCHAR* Language)
        {
            FInternationalization::Get().BackupCultureState(Snapshot);
            FInternationalization::Get().SetCurrentLanguage(Language);
        }

        ~FScopedLanguage()
        {
            FInternationalization::Get().RestoreCultureState(Snapshot);
        }

    private:
        FInternationalization::FCultureStateSnapshot Snapshot;
    };

    // The expectations of most tests are English.
    class FScopedEnglishCulture : public FScopedLanguage
    {
    public:
        FScopedEnglishCulture()
            : FScopedLanguage(TEXT("en"))
        {
        }
    };

    /** "category[:subtype]" of a pin type, "<container> of ..." around it and " -> <value type>" for a map. */
    FString DescribeType(const FUBotDocPinType& Type)
    {
        FString Text = Type.Category;
        if (!Type.Subtype.IsEmpty())
        {
            Text += TEXT(":") + Type.Subtype;
        }
        if (Type.ValueType.IsValid())
        {
            Text += TEXT(" -> ") + DescribeType(*Type.ValueType);
        }
        return Type.Container == TEXT("none") ? Text : Type.Container + TEXT(" of ") + Text;
    }

    /** "id|name|category[:subtype]" per pin, joined with ", ", so a whole pin list fits in one TestEqual. */
    FString DescribePins(const TArray<FUBotDocPin>& Pins)
    {
        TArray<FString> Parts;
        for (const FUBotDocPin& Pin : Pins)
        {
            FString Part = FString::Printf(TEXT("%s|%s|%s"), *Pin.Id, *Pin.Name, *Pin.Type.Category);
            if (!Pin.Type.Subtype.IsEmpty())
            {
                Part += TEXT(":") + Pin.Type.Subtype;
            }
            Parts.Add(Part);
        }
        return FString::Join(Parts, TEXT(", "));
    }

    const FUBotDocNode* FindNode(const FUBotDocApi& Api, const TCHAR* Id)
    {
        return Api.Nodes.FindByPredicate([Id](const FUBotDocNode& Node) { return Node.Id == Id; });
    }

    const FUBotDocPin* FindPin(const TArray<FUBotDocPin>& Pins, const TCHAR* Id)
    {
        return Pins.FindByPredicate([Id](const FUBotDocPin& Pin) { return Pin.Id == Id; });
    }

    UFunction* FindFunction(UClass* Class, const TCHAR* Name)
    {
        return Class->FindFunctionByName(Name);
    }

    template <typename ElementType>
    bool IsSortedById(const TArray<ElementType>& Elements)
    {
        for (int32 Index = 1; Index < Elements.Num(); ++Index)
        {
            if (Elements[Index - 1].Id.Compare(Elements[Index].Id, ESearchCase::CaseSensitive) >= 0)
            {
                return false;
            }
        }
        return true;
    }

    /** A small hand-made document for the format and merge tests. */
    FUBotDocApi MakeSampleApi()
    {
        FUBotDocApi Api;
        Api.Plugin = TEXT("Demo");
        Api.Version = TEXT("1.0.0");
        Api.Engine = TEXT("5.5");
        Api.Generator = FUBotDocExporter::GeneratorName;
        Api.Culture = TEXT("en");
        Api.Modules.Add({ TEXT("Demo"), TEXT("Runtime") });

        FUBotDocClass& Class = Api.Classes.AddDefaulted_GetRef();
        Class.Id = TEXT("UDemo");
        Class.DisplayName = TEXT("Demo");
        Class.Kind = TEXT("object");
        Class.Parent = TEXT("UObject");
        FUBotDocProperty& Property = Class.Properties.AddDefaulted_GetRef();
        Property.Id = TEXT("Rate");
        Property.DisplayName = TEXT("Rate");
        Property.Type.Category = TEXT("float");
        Property.Default = TEXT("1.000000");
        Property.Access = TEXT("readWrite");
        Property.Editable = TEXT("anywhere");
        Property.Meta.Emplace(TEXT("ClampMin"), TEXT("0"));
        Class.Nodes.Add(TEXT("UDemo.Run"));

        FUBotDocNode& Node = Api.Nodes.AddDefaulted_GetRef();
        Node.Id = TEXT("UDemo.Run");
        Node.Kind = TEXT("call");
        Node.Title = TEXT("Run \"fast\"");
        Node.Subtitle = TEXT("Target is Demo");
        Node.Owner = TEXT("UDemo");
        Node.Member = TEXT("Run");
        Node.Tooltip = TEXT("Line1\nLine2\x01");
        FUBotDocPin& Execute = Node.Inputs.AddDefaulted_GetRef();
        Execute.Id = TEXT("execute");
        Execute.Type.Category = TEXT("exec");
        FUBotDocPin& Self = Node.Inputs.AddDefaulted_GetRef();
        Self.Id = TEXT("self");
        Self.Name = TEXT("Target");
        Self.Type.Category = TEXT("object");
        Self.Type.Subtype = TEXT("UDemo");
        Self.Type.SubtypeDisplay = TEXT("Demo");
        FUBotDocPin& Result = Node.Outputs.AddDefaulted_GetRef();
        Result.Id = TEXT("ReturnValue");
        Result.Name = TEXT("Return Value");
        Result.Type.Category = TEXT("string");
        Result.Type.Container = TEXT("map");
        Result.Type.ValueType = MakeShared<FUBotDocPinType>();
        Result.Type.ValueType->Category = TEXT("int");

        FUBotDocEnum& Enum = Api.Enums.AddDefaulted_GetRef();
        Enum.Id = TEXT("EDemoState");
        Enum.DisplayName = TEXT("Demo State");
        FUBotDocEnumValue& Value = Enum.Values.AddDefaulted_GetRef();
        Value.Id = TEXT("Valid");
        Value.DisplayName = TEXT("Valid");
        return Api;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotDocExportFunctionNodesTest, "UBotCore.Editor.DocExport.FunctionNodes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotDocExportFunctionNodesTest::RunTest(const FString& Parameters)
{
    using namespace UBotDocExportTestsPrivate;

    FScopedEnglishCulture English;
    FUBotDocExporter Exporter;
    FUBotDocNode Node;
    FString Error;

    // A member function with an exec pin, a target and a parameter default.
    if (!TestTrue(TEXT("SetFixedTimeStep exports"), Exporter.ExportFunctionNode(FindFunction(UUBotClockSubsystem::StaticClass(), TEXT("SetFixedTimeStep")), Node, Error)))
    {
        AddError(Error);
        return false;
    }
    TestEqual(TEXT("Id"), Node.Id, FString(TEXT("UUBotClockSubsystem.SetFixedTimeStep")));
    TestEqual(TEXT("Kind"), Node.Kind, FString(TEXT("call")));
    TestEqual(TEXT("Title"), Node.Title, FString(TEXT("Set Fixed Time Step")));
    TestEqual(TEXT("Subtitle"), Node.Subtitle, FString(TEXT("Target is UBot Clock Subsystem")));
    TestEqual(TEXT("Owner"), Node.Owner, FString(TEXT("UUBotClockSubsystem")));
    TestEqual(TEXT("Member"), Node.Member, FString(TEXT("SetFixedTimeStep")));
    TestEqual(TEXT("Category"), Node.Category, FString(TEXT("uBot|Clock")));
    TestTrue(TEXT("Tooltip is the comment"), Node.Tooltip.StartsWith(TEXT("Enables or disables the engine fixed time step")));
    TestFalse(TEXT("Not static"), Node.bStatic);
    TestEqual(TEXT("Inputs"), DescribePins(Node.Inputs),
        FString(TEXT("execute||exec, self|Target|object:UUBotClockSubsystem, bEnabled|Enabled|bool, StepHz|Step Hz|double")));
    TestEqual(TEXT("Outputs"), DescribePins(Node.Outputs), FString(TEXT("then||exec")));
    if (const FUBotDocPin* StepHz = FindPin(Node.Inputs, TEXT("StepHz")))
    {
        TestEqual(TEXT("StepHz default"), StepHz->Default, FString(TEXT("60.000000")));
    }

    // A static pure library function: no target, DisplayName and Keywords metadata, const reference input.
    if (!TestTrue(TEXT("PositionUEToROS exports"), Exporter.ExportFunctionNode(FindFunction(UUBotConventionsLibrary::StaticClass(), TEXT("PositionUEToROS")), Node, Error)))
    {
        AddError(Error);
        return false;
    }
    TestEqual(TEXT("Library id"), Node.Id, FString(TEXT("UUBotConventionsLibrary.PositionUEToROS")));
    TestEqual(TEXT("Library kind"), Node.Kind, FString(TEXT("pure")));
    TestEqual(TEXT("Library title"), Node.Title, FString(TEXT("UE To ROS Position")));
    TestEqual(TEXT("Library subtitle"), Node.Subtitle, FString());
    TestTrue(TEXT("Library static"), Node.bStatic);
    TestEqual(TEXT("Library keywords"), Node.Keywords, FString(TEXT("ROS REP103 convert location")));
    TestEqual(TEXT("Library inputs"), DescribePins(Node.Inputs), FString(TEXT("UECentimeters|UECentimeters|vector")));
    TestEqual(TEXT("Library outputs"), DescribePins(Node.Outputs), FString(TEXT("ReturnValue|Return Value|vector")));
    if (Node.Inputs.Num() == 1)
    {
        TestTrue(TEXT("const FVector& is a const reference"), Node.Inputs[0].Type.bByRef && Node.Inputs[0].Type.bConst);
    }
    // DeprecatedFunction metadata is what the exporter reads.
    TestTrue(TEXT("The UE To ROS node is deprecated"), Node.bDeprecated);

    // The neutral node that replaced it: same pin, not deprecated, found by the old and the new keywords.
    if (!TestTrue(TEXT("PositionUEToRightHanded exports"), Exporter.ExportFunctionNode(FindFunction(UUBotConventionsLibrary::StaticClass(), TEXT("PositionUEToRightHanded")), Node, Error)))
    {
        AddError(Error);
        return false;
    }
    TestEqual(TEXT("Right-handed id"), Node.Id, FString(TEXT("UUBotConventionsLibrary.PositionUEToRightHanded")));
    TestEqual(TEXT("Right-handed kind"), Node.Kind, FString(TEXT("pure")));
    TestEqual(TEXT("Right-handed title"), Node.Title, FString(TEXT("UE To Right-Handed Position")));
    TestEqual(TEXT("Right-handed category"), Node.Category, FString(TEXT("uBot|Conventions")));
    TestTrue(TEXT("Right-handed static"), Node.bStatic);
    TestEqual(TEXT("Right-handed keywords"), Node.Keywords, FString(TEXT("right handed FLU SI ROS REP103 MuJoCo convert location")));
    TestEqual(TEXT("Right-handed inputs"), DescribePins(Node.Inputs), FString(TEXT("UECentimeters|UECentimeters|vector")));
    TestEqual(TEXT("Right-handed outputs"), DescribePins(Node.Outputs), FString(TEXT("ReturnValue|Return Value|vector")));
    TestFalse(TEXT("The right-handed node is not deprecated"), Node.bDeprecated);

    // A const pure member function keeps its target pin.
    if (!TestTrue(TEXT("GetResolvedRobotId exports"), Exporter.ExportFunctionNode(FindFunction(UUBotIdentityComponent::StaticClass(), TEXT("GetResolvedRobotId")), Node, Error)))
    {
        AddError(Error);
        return false;
    }
    TestEqual(TEXT("Component title"), Node.Title, FString(TEXT("Get Resolved Robot Id")));
    TestEqual(TEXT("Component subtitle"), Node.Subtitle, FString(TEXT("Target is uBot Identity")));
    TestEqual(TEXT("Component inputs"), DescribePins(Node.Inputs), FString(TEXT("self|Target|object:UUBotIdentityComponent")));
    TestEqual(TEXT("Component outputs"), DescribePins(Node.Outputs), FString(TEXT("ReturnValue|Return Value|string")));

    // Undocumented functions: UE fills the return value tooltip with the function name, as
    // NameToDisplayString does ("Is UBot Package Enabled" for the DisplayName "Is uBot Package Enabled").
    // Neither the title nor that name is documentation.
    const struct
    {
        UClass* Class;
        const TCHAR* Function;
        const TCHAR* Title;
    } Undocumented[] =
    {
        { UUBotClockSubsystem::StaticClass(), TEXT("IsSimulationPaused"), TEXT("Is Simulation Paused") },
        { UUBotPackageLibrary::StaticClass(), TEXT("IsUBotPackageEnabled"), TEXT("Is uBot Package Enabled") },
    };
    for (const auto& Case : Undocumented)
    {
        if (!TestTrue(*FString::Printf(TEXT("%s exports"), Case.Function), Exporter.ExportFunctionNode(FindFunction(Case.Class, Case.Function), Node, Error)))
        {
            AddError(Error);
            return false;
        }
        TestEqual(*FString::Printf(TEXT("%s title"), Case.Function), Node.Title, FString(Case.Title));
        TestEqual(*FString::Printf(TEXT("%s has no description"), Case.Function), Node.Tooltip, FString());
        const FUBotDocPin* ReturnValue = FindPin(Node.Outputs, TEXT("ReturnValue"));
        if (TestNotNull(*FString::Printf(TEXT("%s return value"), Case.Function), ReturnValue))
        {
            TestEqual(*FString::Printf(TEXT("%s return value tooltip"), Case.Function), ReturnValue->Tooltip, FString());
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotDocExportDelegateNodeTest, "UBotCore.Editor.DocExport.DelegateNode",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotDocExportDelegateNodeTest::RunTest(const FString& Parameters)
{
    using namespace UBotDocExportTestsPrivate;

    FScopedEnglishCulture English;
    FUBotDocExporter Exporter;
    FUBotDocNode Node;
    FString Error;

    // A (sparse) BlueprintAssignable delegate becomes a bound event: then + signature parameters.
    const FMulticastDelegateProperty* Property = FindFProperty<FMulticastDelegateProperty>(UActorComponent::StaticClass(), TEXT("OnComponentActivated"));
    if (!TestNotNull(TEXT("OnComponentActivated"), Property))
    {
        return false;
    }
    if (!TestTrue(TEXT("Delegate exports"), Exporter.ExportDelegateNode(Property, Node, Error)))
    {
        AddError(Error);
        return false;
    }
    TestEqual(TEXT("Id"), Node.Id, FString(TEXT("UActorComponent.OnComponentActivated")));
    TestEqual(TEXT("Kind"), Node.Kind, FString(TEXT("event")));
    TestEqual(TEXT("Title from the property"), Node.Title, FString(TEXT("On Component Activated")));
    TestEqual(TEXT("No subtitle"), Node.Subtitle, FString());
    TestEqual(TEXT("Owner"), Node.Owner, FString(TEXT("UActorComponent")));
    TestEqual(TEXT("Member"), Node.Member, FString(TEXT("OnComponentActivated")));
    TestEqual(TEXT("No inputs"), Node.Inputs.Num(), 0);
    TestEqual(TEXT("Outputs"), DescribePins(Node.Outputs), FString(TEXT("then||exec, Component|Component|object:UActorComponent, bReset|Reset|bool")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotDocExportStructMakeBreakTest, "UBotCore.Editor.DocExport.StructMakeBreak",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotDocExportStructMakeBreakTest::RunTest(const FString& Parameters)
{
    using namespace UBotDocExportTestsPrivate;

    FScopedEnglishCulture English;
    FUBotDocExporter Exporter;
    FUBotDocNode Node;
    FString Error;
    const UScriptStruct* Struct = FUBotVelocityCommand::StaticStruct();
    const FString Fields = TEXT("LinearVelocityMetersPerSecond|Linear Velocity Meters Per Second|vector, ")
        TEXT("AngularVelocityRadiansPerSecond|Angular Velocity Radians Per Second|vector, ")
        TEXT("TimestampSeconds|Timestamp Seconds|double, Source|Source|name");
    const FString StructPin = TEXT("UBotVelocityCommand|UBot Velocity Command|struct:FUBotVelocityCommand");

    if (!TestTrue(TEXT("Make exports"), Exporter.ExportStructNode(Struct, /*bMake*/ true, Node, Error)))
    {
        AddError(Error);
        return false;
    }
    TestEqual(TEXT("Make id"), Node.Id, FString(TEXT("Make:FUBotVelocityCommand")));
    TestEqual(TEXT("Make kind"), Node.Kind, FString(TEXT("make")));
    TestEqual(TEXT("Make title"), Node.Title, FString(TEXT("Make UBot Velocity Command")));
    TestEqual(TEXT("Make owner"), Node.Owner, FString());
    TestEqual(TEXT("Make inputs are the fields in declaration order"), DescribePins(Node.Inputs), Fields);
    TestEqual(TEXT("Make output"), DescribePins(Node.Outputs), StructPin);

    if (!TestTrue(TEXT("Break exports"), Exporter.ExportStructNode(Struct, /*bMake*/ false, Node, Error)))
    {
        AddError(Error);
        return false;
    }
    TestEqual(TEXT("Break id"), Node.Id, FString(TEXT("Break:FUBotVelocityCommand")));
    TestEqual(TEXT("Break kind"), Node.Kind, FString(TEXT("break")));
    TestEqual(TEXT("Break title"), Node.Title, FString(TEXT("Break UBot Velocity Command")));
    TestEqual(TEXT("Break input"), DescribePins(Node.Inputs), StructPin);
    TestEqual(TEXT("Break outputs"), DescribePins(Node.Outputs), Fields);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotDocExportPluginTest, "UBotCore.Editor.DocExport.PluginExport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotDocExportPluginTest::RunTest(const FString& Parameters)
{
    using namespace UBotDocExportTestsPrivate;

    FScopedEnglishCulture English;
    FUBotDocApi Api;
    TArray<FString> Errors;
    {
        FUBotDocExporter Exporter;
        if (!TestTrue(TEXT("UBotCore exports"), Exporter.ExportPlugin(TEXT("UBotCore"), Api, Errors)))
        {
            for (const FString& Message : Errors)
            {
                AddError(Message);
            }
            return false;
        }
    }

    TestEqual(TEXT("Plugin"), Api.Plugin, FString(TEXT("UBotCore")));
    TestEqual(TEXT("Engine"), Api.Engine, FString(TEXT("5.5")));
    TestEqual(TEXT("Repository without .git"), Api.Repository, FString(TEXT("https://github.com/AstreoX/uBotCore")));
    TestEqual(TEXT("Generator"), Api.Generator, FString(TEXT("UBotDocExport 1")));
    TestEqual(TEXT("Culture"), Api.Culture, FString(TEXT("en")));
    TestEqual(TEXT("Modules"), Api.Modules.Num(), 2);
    if (Api.Modules.Num() == 2)
    {
        TestEqual(TEXT("Runtime module"), Api.Modules[0].Name + TEXT("/") + Api.Modules[0].Type, FString(TEXT("UBotCore/Runtime")));
        TestEqual(TEXT("Editor module"), Api.Modules[1].Name + TEXT("/") + Api.Modules[1].Type, FString(TEXT("UBotCoreEditor/Editor")));
    }
    TestTrue(TEXT("Classes sorted"), IsSortedById(Api.Classes));
    TestTrue(TEXT("Nodes sorted"), IsSortedById(Api.Nodes));
    TestTrue(TEXT("Structs sorted"), IsSortedById(Api.Structs));
    TestTrue(TEXT("Enums sorted"), IsSortedById(Api.Enums));

    const FUBotDocClass* Identity = Api.Classes.FindByPredicate([](const FUBotDocClass& Class) { return Class.Id == TEXT("UUBotIdentityComponent"); });
    if (TestNotNull(TEXT("Identity component"), Identity))
    {
        TestEqual(TEXT("Display name"), Identity->DisplayName, FString(TEXT("uBot Identity")));
        TestEqual(TEXT("Kind"), Identity->Kind, FString(TEXT("component")));
        TestEqual(TEXT("Parent"), Identity->Parent, FString(TEXT("UActorComponent")));
        TestEqual(TEXT("Header"), Identity->Header, FString(TEXT("Source/UBotCore/Public/UBotIdentityComponent.h")));
        TestEqual(TEXT("Category from ClassGroup"), Identity->Category, FString(TEXT("uBot")));
        TestTrue(TEXT("Spawnable"), Identity->bSpawnable);
        TestTrue(TEXT("Owns GetResolvedRobotId"), Identity->Nodes.Contains(TEXT("UUBotIdentityComponent.GetResolvedRobotId")));
        TestTrue(TEXT("Properties sorted"), IsSortedById(Identity->Properties));
        const FUBotDocProperty* RobotId = Identity->Properties.FindByPredicate([](const FUBotDocProperty& Property) { return Property.Id == TEXT("RobotId"); });
        if (TestNotNull(TEXT("RobotId"), RobotId))
        {
            TestEqual(TEXT("RobotId access"), RobotId->Access, FString(TEXT("readOnly")));
            TestEqual(TEXT("RobotId editable"), RobotId->Editable, FString(TEXT("anywhere")));
            TestEqual(TEXT("RobotId type"), RobotId->Type.Category, FString(TEXT("string")));
        }
        const FUBotDocProperty* BaseFrameId = Identity->Properties.FindByPredicate([](const FUBotDocProperty& Property) { return Property.Id == TEXT("BaseFrameId"); });
        if (TestNotNull(TEXT("BaseFrameId"), BaseFrameId))
        {
            TestEqual(TEXT("BaseFrameId default from the CDO"), BaseFrameId->Default, FString(TEXT("base_link")));
        }
    }
    TestNotNull(TEXT("Every owned node is exported"), FindNode(Api, TEXT("UUBotIdentityComponent.GetResolvedRobotId")));

    const FUBotDocClass* Settings = Api.Classes.FindByPredicate([](const FUBotDocClass& Class) { return Class.Id == TEXT("UUBotCoreSettings"); });
    if (TestNotNull(TEXT("Core settings"), Settings))
    {
        TestEqual(TEXT("Settings kind"), Settings->Kind, FString(TEXT("settings")));
        const FUBotDocProperty* UseFixedStep = Settings->Properties.FindByPredicate([](const FUBotDocProperty& Property) { return Property.Id == TEXT("bUseFixedTimeStep"); });
        if (TestNotNull(TEXT("bUseFixedTimeStep"), UseFixedStep))
        {
            // A zero value is still written.
            TestEqual(TEXT("false default"), UseFixedStep->Default, FString(TEXT("False")));
            TestEqual(TEXT("Edit-only property"), UseFixedStep->Access, FString(TEXT("none")));
        }
        const FUBotDocProperty* StepHz = Settings->Properties.FindByPredicate([](const FUBotDocProperty& Property) { return Property.Id == TEXT("FixedTimeStepHz"); });
        if (TestNotNull(TEXT("FixedTimeStepHz"), StepHz))
        {
            TestEqual(TEXT("double default"), StepHz->Default, FString(TEXT("60.000000")));
            TestEqual(TEXT("meta keys in contract order"), StepHz->Meta.Num(), 3);
            if (StepHz->Meta.Num() == 3)
            {
                TestEqual(TEXT("ClampMin first"), StepHz->Meta[0].Key + TEXT("=") + StepHz->Meta[0].Value, FString(TEXT("ClampMin=1.0")));
                TestEqual(TEXT("EditCondition last"), StepHz->Meta[2].Key + TEXT("=") + StepHz->Meta[2].Value, FString(TEXT("EditCondition=bUseFixedTimeStep")));
            }
        }
    }

    // Classes that are Blueprint types only by inheritance and add nothing are not documented.
    TestFalse(TEXT("No commandlets"), Api.Classes.ContainsByPredicate([](const FUBotDocClass& Class) { return Class.Id.EndsWith(TEXT("Commandlet")); }));

    const FUBotDocStruct* Header = Api.Structs.FindByPredicate([](const FUBotDocStruct& Struct) { return Struct.Id == TEXT("FUBotFrameHeader"); });
    if (TestNotNull(TEXT("FUBotFrameHeader"), Header))
    {
        TestEqual(TEXT("Header fields"), Header->Fields.Num(), 6);
        TestTrue(TEXT("Fields sorted"), IsSortedById(Header->Fields));
        TestEqual(TEXT("Break node"), Header->Break, FString(TEXT("Break:FUBotFrameHeader")));
        TestNotNull(TEXT("Break node exported"), FindNode(Api, TEXT("Break:FUBotFrameHeader")));
    }

    const FUBotDocEnum* Status = Api.Enums.FindByPredicate([](const FUBotDocEnum& Enum) { return Enum.Id == TEXT("EUBotSampleStatus"); });
    if (TestNotNull(TEXT("EUBotSampleStatus"), Status))
    {
        TestEqual(TEXT("Six values, no _MAX"), Status->Values.Num(), 6);
        const FUBotDocEnumValue* Valid = Status->Values.FindByPredicate([](const FUBotDocEnumValue& Value) { return Value.Id == TEXT("Valid"); });
        if (TestNotNull(TEXT("Valid"), Valid))
        {
            TestEqual(TEXT("Valid value"), Valid->Value, static_cast<int64>(0));
        }
        const FUBotDocEnumValue* WarmingUp = Status->Values.FindByPredicate([](const FUBotDocEnumValue& Value) { return Value.Id == TEXT("WarmingUp"); });
        if (TestNotNull(TEXT("WarmingUp"), WarmingUp))
        {
            TestEqual(TEXT("WarmingUp display name"), WarmingUp->DisplayName, FString(TEXT("Warming Up")));
        }
    }

    TArray<FString> UnknownErrors;
    FUBotDocExporter Exporter;
    TestFalse(TEXT("Unknown plugin fails"), Exporter.ExportPlugin(TEXT("UBotNoSuchPlugin"), Api, UnknownErrors));
    TestTrue(TEXT("Unknown plugin is named"), UnknownErrors.Num() == 1 && UnknownErrors[0].Contains(TEXT("UBotNoSuchPlugin")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotDocExportInheritedDefaultsTest, "UBotCore.Editor.DocExport.InheritedDefaults",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotDocExportInheritedDefaultsTest::RunTest(const FString& Parameters)
{
    using namespace UBotDocExportTestsPrivate;

    // UBotSensor's components change defaults declared on UUBotSensorComponent; UBotCore has no such
    // hierarchy. The test needs the sensor plugin, which UBotDev enables.
    const TSharedPtr<IPlugin> SensorPlugin = IPluginManager::Get().FindPlugin(TEXT("UBotSensor"));
    if (!SensorPlugin.IsValid() || !SensorPlugin->IsEnabled())
    {
        AddWarning(TEXT("UBotSensor is not enabled in this project; inherited defaults were not checked."));
        return true;
    }

    FScopedEnglishCulture English;
    FUBotDocApi Api;
    TArray<FString> Errors;
    {
        FUBotDocExporter Exporter;
        if (!TestTrue(TEXT("UBotSensor exports"), Exporter.ExportPlugin(TEXT("UBotSensor"), Api, Errors)))
        {
            for (const FString& Message : Errors)
            {
                AddError(Message);
            }
            return false;
        }
    }

    const auto FindClass = [&Api](const TCHAR* Id) { return Api.Classes.FindByPredicate([Id](const FUBotDocClass& Class) { return Class.Id == Id; }); };
    const auto FindProperty = [](const FUBotDocClass* Class, const TCHAR* Id) -> const FUBotDocProperty*
    {
        return Class ? Class->Properties.FindByPredicate([Id](const FUBotDocProperty& Property) { return Property.Id == Id; }) : nullptr;
    };

    const FUBotDocClass* Base = FindClass(TEXT("UUBotSensorComponent"));
    const FUBotDocClass* Imu = FindClass(TEXT("UUBotImuSensorComponent"));
    if (!TestNotNull(TEXT("Sensor base class"), Base) || !TestNotNull(TEXT("IMU class"), Imu))
    {
        return false;
    }

    // Declared on the base class: documented there with the base value.
    const FUBotDocProperty* BaseRate = FindProperty(Base, TEXT("UpdateRateHz"));
    if (TestNotNull(TEXT("Base Update Rate Hz"), BaseRate))
    {
        TestEqual(TEXT("Base default"), BaseRate->Default, FString(TEXT("10.000000")));
    }
    // Changed by the IMU constructor: listed again on the IMU with the IMU's value.
    const FUBotDocProperty* ImuRate = FindProperty(Imu, TEXT("UpdateRateHz"));
    if (TestNotNull(TEXT("IMU lists the inherited Update Rate Hz"), ImuRate))
    {
        TestEqual(TEXT("IMU default"), ImuRate->Default, FString(TEXT("100.000000")));
        TestEqual(TEXT("Same display name"), ImuRate->DisplayName, BaseRate ? BaseRate->DisplayName : FString());
        TestEqual(TEXT("Same category"), ImuRate->Category, BaseRate ? BaseRate->Category : FString());
    }
    const FUBotDocProperty* ImuSensorId = FindProperty(Imu, TEXT("SensorId"));
    if (TestNotNull(TEXT("IMU lists the inherited Sensor Id"), ImuSensorId))
    {
        TestEqual(TEXT("IMU sensor id"), ImuSensorId->Default, FString(TEXT("imu_sensor")));
    }
    // Unchanged inherited properties stay on the declaring class only.
    TestNull(TEXT("Unchanged inherited property is not repeated"), FindProperty(Imu, TEXT("bAutoStart")));
    // Engine properties the constructor changes (mesh, collision, tick) are never documented here.
    TestNull(TEXT("No engine property"), FindProperty(Imu, TEXT("PrimaryComponentTick")));
    TestNull(TEXT("No engine property on the base"), FindProperty(Base, TEXT("BodyInstance")));
    TestTrue(TEXT("IMU properties sorted"), IsSortedById(Imu->Properties));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotDocExportDeterministicTest, "UBotCore.Editor.DocExport.Deterministic",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotDocExportDeterministicTest::RunTest(const FString& Parameters)
{
    FString Json[2];
    for (FString& Text : Json)
    {
        // A fresh exporter each time: a new transient Blueprint must not change anything.
        FUBotDocExporter Exporter;
        FUBotDocApi Api;
        TArray<FString> Errors;
        if (!TestTrue(TEXT("Export"), Exporter.ExportPlugin(TEXT("UBotCore"), Api, Errors)))
        {
            return false;
        }
        Text = FUBotDocExporter::ToJson(Api);
    }

    TestTrue(TEXT("Two exports are identical"), Json[0].Equals(Json[1], ESearchCase::CaseSensitive));
    TestTrue(TEXT("Starts with the format"), Json[0].StartsWith(TEXT("{\n  \"format\": 1,\n  \"plugin\": \"UBotCore\",\n"), ESearchCase::CaseSensitive));
    TestTrue(TEXT("Ends with a newline"), Json[0].EndsWith(TEXT("}\n"), ESearchCase::CaseSensitive));
    TestFalse(TEXT("LF only"), Json[0].Contains(TEXT("\r")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotDocExportNodeListParsingTest, "UBotCore.Editor.DocExport.NodeListParsing",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotDocExportNodeListParsingTest::RunTest(const FString& Parameters)
{
    using EKind = FUBotDocNodeListEntry::EKind;

    const FString Valid =
        TEXT("# Curated engine nodes\r\n")
        TEXT("/Script/Engine.KismetSystemLibrary:PrintString\r\n")
        TEXT("\r\n")
        TEXT("  event /Script/Engine.Actor:ReceiveBeginPlay   # trailing comment\n")
        TEXT("node /Script/BlueprintGraph.K2Node_IfThenElse\n")
        TEXT("macro /Engine/EditorBlueprintResources/StandardMacros.StandardMacros:ForLoop\n")
        TEXT("node  /Script/BlueprintGraph.K2Node_GetSubsystem\t/Script/UBotCore.UBotClockSubsystem  # Get Subsystem");

    TArray<FUBotDocNodeListEntry> Entries;
    TArray<FString> Errors;
    TestTrue(TEXT("Valid list parses"), FUBotDocExporter::ParseNodeList(Valid, Entries, Errors));
    TestEqual(TEXT("No errors"), Errors.Num(), 0);
    if (TestEqual(TEXT("Five entries"), Entries.Num(), 5))
    {
        TestTrue(TEXT("Function"), Entries[0].Kind == EKind::Function && Entries[0].Path == TEXT("/Script/Engine.KismetSystemLibrary")
            && Entries[0].Member == TEXT("PrintString") && Entries[0].Line == 2);
        TestTrue(TEXT("Event"), Entries[1].Kind == EKind::Event && Entries[1].Path == TEXT("/Script/Engine.Actor")
            && Entries[1].Member == TEXT("ReceiveBeginPlay") && Entries[1].Line == 4);
        TestTrue(TEXT("Node"), Entries[2].Kind == EKind::Node && Entries[2].Path == TEXT("/Script/BlueprintGraph.K2Node_IfThenElse")
            && Entries[2].Member.IsEmpty() && Entries[2].ClassPath.IsEmpty() && Entries[2].Line == 5);
        TestTrue(TEXT("Macro: the last ':' separates the macro"), Entries[3].Kind == EKind::Macro
            && Entries[3].Path == TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros") && Entries[3].Member == TEXT("ForLoop"));
        TestTrue(TEXT("Node with a class"), Entries[4].Kind == EKind::Node && Entries[4].Path == TEXT("/Script/BlueprintGraph.K2Node_GetSubsystem")
            && Entries[4].ClassPath == TEXT("/Script/UBotCore.UBotClockSubsystem") && Entries[4].Member.IsEmpty() && Entries[4].Line == 7);
    }

    const TCHAR* const InvalidLines[] =
    {
        TEXT("function /Script/Engine.Actor:K2_GetActorLocation"),
        TEXT("/Script/Engine.KismetSystemLibrary"),
        TEXT("event /Script/Engine.Actor:"),
        TEXT("node /Script/BlueprintGraph.K2Node_IfThenElse:Branch"),
        TEXT("Script/Engine.KismetSystemLibrary:PrintString"),
        TEXT("event node /Script/Engine.Actor:ReceiveBeginPlay"),
        TEXT("macro /Engine/EditorBlueprintResources/StandardMacros.StandardMacros"),
        // Only a node entry takes a class, and only one.
        TEXT("event /Script/Engine.Actor:ReceiveTick /Script/Engine.Actor"),
        TEXT("node /Script/BlueprintGraph.K2Node_GetSubsystem UBotClockSubsystem"),
        TEXT("node /Script/BlueprintGraph.K2Node_GetSubsystem /Script/UBotCore.UBotClockSubsystem:Tick"),
        TEXT("node /Script/BlueprintGraph.K2Node_GetSubsystem /Script/UBotCore.UBotClockSubsystem /Script/Engine.Actor"),
    };
    for (const TCHAR* Line : InvalidLines)
    {
        TArray<FUBotDocNodeListEntry> BadEntries;
        TArray<FString> BadErrors;
        const FString Text = FString(TEXT("# header\n")) + Line + TEXT("\n");
        TestFalse(*FString::Printf(TEXT("'%s' is rejected"), Line), FUBotDocExporter::ParseNodeList(Text, BadEntries, BadErrors));
        TestTrue(*FString::Printf(TEXT("'%s' names line 2"), Line), BadErrors.Num() == 1 && BadErrors[0].StartsWith(TEXT("Line 2: ")));
        TestEqual(*FString::Printf(TEXT("'%s' yields no entry"), Line), BadEntries.Num(), 0);
    }

    // Entries that parse but do not resolve fail the export with their line number.
    FUBotDocExporter Exporter;
    FUBotDocApi Api;
    TArray<FUBotDocNodeListEntry> Unresolved;
    TArray<FString> ParseErrors;
    FUBotDocExporter::ParseNodeList(TEXT("/Script/Engine.KismetSystemLibrary:NoSuchFunction\nnode /Script/Engine.NoSuchNode\n")
        TEXT("/Script/Engine.Actor:ReceiveBeginPlay\n"), Unresolved, ParseErrors);
    TArray<FString> ResolveErrors;
    TestFalse(TEXT("Unknown entries fail"), Exporter.ExportNodeList(Unresolved, Api, ResolveErrors));
    if (TestEqual(TEXT("Three resolve errors"), ResolveErrors.Num(), 3))
    {
        TestTrue(TEXT("Unknown function"), ResolveErrors[0].StartsWith(TEXT("Line 1: ")) && ResolveErrors[0].Contains(TEXT("NoSuchFunction")));
        TestTrue(TEXT("Unknown node class"), ResolveErrors[1].StartsWith(TEXT("Line 2: ")) && ResolveErrors[1].Contains(TEXT("NoSuchNode")));
        TestTrue(TEXT("An event is not a function call"), ResolveErrors[2].StartsWith(TEXT("Line 3: ")) && ResolveErrors[2].Contains(TEXT("BlueprintCallable")));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotDocExportNodeListTest, "UBotCore.Editor.DocExport.EngineNodeList",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotDocExportNodeListTest::RunTest(const FString& Parameters)
{
    using namespace UBotDocExportTestsPrivate;

    FScopedEnglishCulture English;
    TArray<FUBotDocNodeListEntry> Entries;
    TArray<FString> Errors;
    FUBotDocExporter::ParseNodeList(
        TEXT("/Script/Engine.KismetSystemLibrary:PrintString\n")
        TEXT("event /Script/Engine.Actor:ReceiveBeginPlay\n")
        TEXT("node /Script/BlueprintGraph.K2Node_IfThenElse\n")
        TEXT("macro /Engine/EditorBlueprintResources/StandardMacros.StandardMacros:ForLoop\n"), Entries, Errors);

    FUBotDocApi Api;
    {
        FUBotDocExporter Exporter;
        if (!TestTrue(TEXT("Engine nodes export"), Exporter.ExportNodeList(Entries, Api, Errors)))
        {
            for (const FString& Message : Errors)
            {
                AddError(Message);
            }
            return false;
        }
    }
    TestEqual(TEXT("Plugin"), Api.Plugin, FString(TEXT("Engine")));
    TestEqual(TEXT("No repository"), Api.Repository, FString());
    TestEqual(TEXT("Four nodes"), Api.Nodes.Num(), 4);
    TestTrue(TEXT("Nodes sorted"), IsSortedById(Api.Nodes));

    if (const FUBotDocNode* Print = FindNode(Api, TEXT("UKismetSystemLibrary.PrintString")); TestNotNull(TEXT("Print String"), Print))
    {
        TestEqual(TEXT("Print title"), Print->Title, FString(TEXT("Print String")));
        TestTrue(TEXT("Development only"), Print->bDevelopmentOnly);
        // The world context pin is hidden in an Actor Blueprint.
        TestNull(TEXT("No world context pin"), FindPin(Print->Inputs, TEXT("WorldContextObject")));
        const FUBotDocPin* InString = FindPin(Print->Inputs, TEXT("InString"));
        if (TestNotNull(TEXT("In String"), InString))
        {
            TestEqual(TEXT("In String name"), InString->Name, FString(TEXT("In String")));
            TestEqual(TEXT("In String default"), InString->Default, FString(TEXT("Hello")));
            TestFalse(TEXT("In String is not advanced"), InString->bAdvanced);
        }
        const FUBotDocPin* PrintToScreen = FindPin(Print->Inputs, TEXT("bPrintToScreen"));
        if (TestNotNull(TEXT("Print to Screen"), PrintToScreen))
        {
            TestEqual(TEXT("Print to Screen name"), PrintToScreen->Name, FString(TEXT("Print to Screen")));
            TestTrue(TEXT("AdvancedDisplay = 2"), PrintToScreen->bAdvanced);
        }
    }

    if (const FUBotDocNode* BeginPlay = FindNode(Api, TEXT("AActor.ReceiveBeginPlay")); TestNotNull(TEXT("BeginPlay"), BeginPlay))
    {
        TestEqual(TEXT("Event title"), BeginPlay->Title, FString(TEXT("Event BeginPlay")));
        TestEqual(TEXT("Event kind"), BeginPlay->Kind, FString(TEXT("event")));
        TestEqual(TEXT("Event outputs (no delegate pin)"), DescribePins(BeginPlay->Outputs), FString(TEXT("then||exec")));
    }

    if (const FUBotDocNode* Branch = FindNode(Api, TEXT("K2Node_IfThenElse")); TestNotNull(TEXT("Branch"), Branch))
    {
        TestEqual(TEXT("Branch title"), Branch->Title, FString(TEXT("Branch")));
        TestEqual(TEXT("Branch kind"), Branch->Kind, FString(TEXT("node")));
        TestEqual(TEXT("Branch inputs"), DescribePins(Branch->Inputs), FString(TEXT("execute||exec, Condition|Condition|bool")));
        TestEqual(TEXT("Branch outputs"), DescribePins(Branch->Outputs), FString(TEXT("then|True|exec, else|False|exec")));
    }

    if (const FUBotDocNode* ForLoop = FindNode(Api, TEXT("Macro:ForLoop")); TestNotNull(TEXT("For Loop"), ForLoop))
    {
        TestEqual(TEXT("Macro title"), ForLoop->Title, FString(TEXT("For Loop")));
        TestTrue(TEXT("Macro has inputs"), ForLoop->Inputs.Num() >= 3);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotDocExportSubsystemNodeTest, "UBotCore.Editor.DocExport.SubsystemNode",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotDocExportSubsystemNodeTest::RunTest(const FString& Parameters)
{
    using namespace UBotDocExportTestsPrivate;

    FScopedEnglishCulture English;
    TArray<FUBotDocNodeListEntry> Entries;
    TArray<FString> Errors;
    FUBotDocExporter::ParseNodeList(TEXT("node /Script/BlueprintGraph.K2Node_GetSubsystem /Script/UBotCore.UBotClockSubsystem\n"), Entries, Errors);

    FUBotDocApi Api;
    {
        FUBotDocExporter Exporter;
        if (!TestTrue(TEXT("Get Subsystem node exports"), Exporter.ExportNodeList(Entries, Api, Errors)))
        {
            for (const FString& Message : Errors)
            {
                AddError(Message);
            }
            return false;
        }
    }

    // The id names the node class and the subsystem, so it stays the same however the class is displayed.
    const FUBotDocNode* Node = FindNode(Api, TEXT("K2Node_GetSubsystem:UUBotClockSubsystem"));
    if (!TestNotNull(TEXT("Node id <K2NodeClass>:<SubsystemCpp>"), Node))
    {
        return false;
    }
    TestEqual(TEXT("One node"), Api.Nodes.Num(), 1);
    TestEqual(TEXT("Kind"), Node->Kind, FString(TEXT("node")));
    // The menu offers "Get UBotClockSubsystem"; the node in the graph shows the class's display name, compact.
    TestEqual(TEXT("Title as drawn in the graph"), Node->Title, FString(TEXT("UBot Clock Subsystem")));
    TestEqual(TEXT("Title is the class display name"), Node->Title, UUBotClockSubsystem::StaticClass()->GetDisplayNameText().ToString());
    TestEqual(TEXT("No subtitle"), Node->Subtitle, FString());
    TestTrue(TEXT("Compact"), Node->bCompact);
    TestEqual(TEXT("Compact title"), Node->CompactTitle, Node->Title);
    TestEqual(TEXT("Menu category"), Node->Category, FString(TEXT("World Subsystems")));
    TestTrue(TEXT("Tooltip names the subsystem kind"), Node->Tooltip.StartsWith(TEXT("Get UBot Clock Subsystem (World Subsystem)")));
    TestTrue(TEXT("No owner or member, like other special nodes"), Node->Owner.IsEmpty() && Node->Member.IsEmpty());
    // An Actor Blueprint shows no world context pin.
    TestEqual(TEXT("No inputs"), Node->Inputs.Num(), 0);
    if (TestEqual(TEXT("One output"), Node->Outputs.Num(), 1))
    {
        TestEqual(TEXT("Output pin id"), Node->Outputs[0].Id, FString(TEXT("ReturnValue")));
        TestEqual(TEXT("Output is the subsystem"), DescribeType(Node->Outputs[0].Type), FString(TEXT("object:UUBotClockSubsystem")));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotDocExportNodesNeedingSetupTest, "UBotCore.Editor.DocExport.NodesNeedingSetup",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotDocExportNodesNeedingSetupTest::RunTest(const FString& Parameters)
{
    using namespace UBotDocExportTestsPrivate;

    FScopedEnglishCulture English;
    // Line 1 failed an engine assertion before it was checked: the bare node reads its missing class.
    const FString List =
        TEXT("node /Script/BlueprintGraph.K2Node_GetSubsystem\n")
        TEXT("node /Script/BlueprintGraph.K2Node_DynamicCast\n")
        TEXT("node /Script/BlueprintGraph.K2Node_IfThenElse /Script/UBotCore.UBotClockSubsystem\n")
        TEXT("node /Script/BlueprintGraph.K2Node_GetSubsystem /Script/Engine.Actor\n")
        TEXT("node /Script/BlueprintGraph.K2Node_GetSubsystemFromPC /Script/UBotCore.UBotClockSubsystem\n")
        TEXT("node /Script/BlueprintGraph.K2Node_GetSubsystem /Script/UBotCore.NoSuchSubsystem\n")
        TEXT("node /Script/BlueprintGraph.K2Node_IfThenElse\n");
    TArray<FUBotDocNodeListEntry> Entries;
    TArray<FString> ParseErrors;
    if (!TestTrue(TEXT("The list parses"), FUBotDocExporter::ParseNodeList(List, Entries, ParseErrors)) || !TestEqual(TEXT("Seven entries"), Entries.Num(), 7))
    {
        return false;
    }

    FUBotDocExporter Exporter;
    FUBotDocApi Api;
    TArray<FString> Errors;
    TestFalse(TEXT("Entries that need a setup fail"), Exporter.ExportNodeList(Entries, Api, Errors));
    const TCHAR* const Expected[][2] =
    {
        { TEXT("Line 1: "), TEXT("needs the subsystem class it gets: write 'node /Script/BlueprintGraph.K2Node_GetSubsystem /Script/<Module>.<SubsystemClass>'") },
        { TEXT("Line 2: "), TEXT("offers K2Node_DynamicCast only set up for") },
        { TEXT("Line 3: "), TEXT("K2Node_IfThenElse takes no class") },
        { TEXT("Line 4: "), TEXT("offers no K2Node_GetSubsystem for /Script/Engine.Actor") },
        { TEXT("Line 5: "), TEXT("offers no K2Node_GetSubsystemFromPC for /Script/UBotCore.UBotClockSubsystem (it gets Blueprint-visible subclasses of ULocalPlayerSubsystem)") },
        { TEXT("Line 6: "), TEXT("class '/Script/UBotCore.NoSuchSubsystem' not found") },
    };
    if (TestEqual(TEXT("One error per bad entry"), Errors.Num(), static_cast<int32>(UE_ARRAY_COUNT(Expected))))
    {
        for (int32 Index = 0; Index < Errors.Num(); ++Index)
        {
            TestTrue(*FString::Printf(TEXT("Error %d: %s"), Index, *Errors[Index]),
                Errors[Index].StartsWith(Expected[Index][0]) && Errors[Index].Contains(Expected[Index][1]));
        }
    }
    // The other entries still export.
    TestEqual(TEXT("Only Branch is exported"), Api.Nodes.Num(), 1);
    TestNotNull(TEXT("Branch"), FindNode(Api, TEXT("K2Node_IfThenElse")));

    // The same check guards a direct call.
    FUBotDocNode Node;
    FString Error;
    TestFalse(TEXT("A bare Get Subsystem node is refused"), Exporter.ExportSpecialNode(FindObject<UClass>(nullptr, TEXT("/Script/BlueprintGraph.K2Node_GetSubsystem")), Node, Error));
    TestTrue(TEXT("with the reason"), Error.Contains(TEXT("needs the subsystem class")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotDocExportOperatorNodesTest, "UBotCore.Editor.DocExport.OperatorNodes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotDocExportOperatorNodesTest::RunTest(const FString& Parameters)
{
    using namespace UBotDocExportTestsPrivate;

    FScopedEnglishCulture English;
    // Checking a bare node builds the action menu, which caches one spawner per promotable operator,
    // bound to the first of its functions it met (FrameNumber - Int for Subtract). Every function must
    // still be documented as itself, also the second function of an operator in one list.
    FBlueprintActionDatabase::Get().GetAllActions();
    TArray<FUBotDocNodeListEntry> Entries;
    TArray<FString> Errors;
    FUBotDocExporter::ParseNodeList(
        TEXT("node /Script/BlueprintGraph.K2Node_IfThenElse\n")
        TEXT("/Script/Engine.KismetMathLibrary:Subtract_IntInt\n")
        TEXT("/Script/Engine.KismetMathLibrary:Subtract_Int64Int64\n")
        TEXT("/Script/Engine.KismetMathLibrary:NotEqual_Int64Int64\n"), Entries, Errors);

    FUBotDocApi Api;
    {
        FUBotDocExporter Exporter;
        if (!TestTrue(TEXT("Operator nodes export"), Exporter.ExportNodeList(Entries, Api, Errors)))
        {
            for (const FString& Message : Errors)
            {
                AddError(Message);
            }
            return false;
        }
    }

    const TCHAR* const Expected[][3] =
    {
        { TEXT("UKismetMathLibrary.Subtract_IntInt"), TEXT("int - int"), TEXT("-") },
        { TEXT("UKismetMathLibrary.Subtract_Int64Int64"), TEXT("integer64 - integer64"), TEXT("-") },
        { TEXT("UKismetMathLibrary.NotEqual_Int64Int64"), TEXT("Not Equal (Integer64)"), TEXT("!=") },
    };
    for (const auto& Row : Expected)
    {
        if (const FUBotDocNode* Node = FindNode(Api, Row[0]); TestNotNull(Row[0], Node))
        {
            TestEqual(*FString::Printf(TEXT("%s title"), Row[0]), Node->Title, FString(Row[1]));
            TestEqual(*FString::Printf(TEXT("%s compact title"), Row[0]), Node->CompactTitle, FString(Row[2]));
            TestEqual(*FString::Printf(TEXT("%s kind"), Row[0]), Node->Kind, FString(TEXT("pure")));
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotDocExportJsonFormatTest, "UBotCore.Editor.DocExport.JsonFormat",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotDocExportJsonFormatTest::RunTest(const FString& Parameters)
{
    using namespace UBotDocExportTestsPrivate;

    FUBotDocApi Api = MakeSampleApi();
    Api.Classes.Reset();
    Api.Enums.Reset();
    Api.Nodes[0].Inputs.SetNum(1);

    // JSON.stringify(Api, null, 2) + "\n": the docs site can rewrite the file without a diff.
    const FString Expected =
        TEXT("{\n")
        TEXT("  \"format\": 1,\n")
        TEXT("  \"plugin\": \"Demo\",\n")
        TEXT("  \"version\": \"1.0.0\",\n")
        TEXT("  \"engine\": \"5.5\",\n")
        TEXT("  \"repository\": \"\",\n")
        TEXT("  \"generator\": \"UBotDocExport 1\",\n")
        TEXT("  \"culture\": \"en\",\n")
        TEXT("  \"modules\": [\n")
        TEXT("    {\n")
        TEXT("      \"name\": \"Demo\",\n")
        TEXT("      \"type\": \"Runtime\"\n")
        TEXT("    }\n")
        TEXT("  ],\n")
        TEXT("  \"classes\": [],\n")
        TEXT("  \"nodes\": [\n")
        TEXT("    {\n")
        TEXT("      \"id\": \"UDemo.Run\",\n")
        TEXT("      \"kind\": \"call\",\n")
        TEXT("      \"title\": \"Run \\\"fast\\\"\",\n")
        TEXT("      \"subtitle\": \"Target is Demo\",\n")
        TEXT("      \"owner\": \"UDemo\",\n")
        TEXT("      \"member\": \"Run\",\n")
        TEXT("      \"category\": \"\",\n")
        TEXT("      \"tooltip\": \"Line1\\nLine2\\u0001\",\n")
        TEXT("      \"keywords\": \"\",\n")
        TEXT("      \"flags\": {\n")
        TEXT("        \"static\": false,\n")
        TEXT("        \"latent\": false,\n")
        TEXT("        \"deprecated\": false,\n")
        TEXT("        \"developmentOnly\": false,\n")
        TEXT("        \"callInEditor\": false,\n")
        TEXT("        \"compact\": false\n")
        TEXT("      },\n")
        TEXT("      \"compactTitle\": \"\",\n")
        TEXT("      \"inputs\": [\n")
        TEXT("        {\n")
        TEXT("          \"id\": \"execute\",\n")
        TEXT("          \"name\": \"\",\n")
        TEXT("          \"type\": {\n")
        TEXT("            \"category\": \"exec\",\n")
        TEXT("            \"subtype\": \"\",\n")
        TEXT("            \"subtypeDisplay\": \"\",\n")
        TEXT("            \"container\": \"none\",\n")
        TEXT("            \"valueType\": null,\n")
        TEXT("            \"byRef\": false,\n")
        TEXT("            \"const\": false\n")
        TEXT("          },\n")
        TEXT("          \"default\": \"\",\n")
        TEXT("          \"advanced\": false,\n")
        TEXT("          \"tooltip\": \"\"\n")
        TEXT("        }\n")
        TEXT("      ],\n")
        TEXT("      \"outputs\": [\n")
        TEXT("        {\n")
        TEXT("          \"id\": \"ReturnValue\",\n")
        TEXT("          \"name\": \"Return Value\",\n")
        TEXT("          \"type\": {\n")
        TEXT("            \"category\": \"string\",\n")
        TEXT("            \"subtype\": \"\",\n")
        TEXT("            \"subtypeDisplay\": \"\",\n")
        TEXT("            \"container\": \"map\",\n")
        TEXT("            \"valueType\": {\n")
        TEXT("              \"category\": \"int\",\n")
        TEXT("              \"subtype\": \"\",\n")
        TEXT("              \"subtypeDisplay\": \"\",\n")
        TEXT("              \"container\": \"none\",\n")
        TEXT("              \"valueType\": null,\n")
        TEXT("              \"byRef\": false,\n")
        TEXT("              \"const\": false\n")
        TEXT("            },\n")
        TEXT("            \"byRef\": false,\n")
        TEXT("            \"const\": false\n")
        TEXT("          },\n")
        TEXT("          \"default\": \"\",\n")
        TEXT("          \"advanced\": false,\n")
        TEXT("          \"tooltip\": \"\"\n")
        TEXT("        }\n")
        TEXT("      ]\n")
        TEXT("    }\n")
        TEXT("  ],\n")
        TEXT("  \"structs\": [],\n")
        TEXT("  \"enums\": []\n")
        TEXT("}\n");

    const FString Actual = FUBotDocExporter::ToJson(Api);
    if (!TestEqual(TEXT("api.json text"), Actual, Expected))
    {
        AddInfo(Actual);
    }

    // Sorting is ordinal and leaves pin order alone.
    FUBotDocApi Unsorted = MakeSampleApi();
    FUBotDocNode Second = Unsorted.Nodes[0];
    Second.Id = TEXT("UDemo.Apply");
    Unsorted.Nodes.Add(Second);
    FUBotDocNode Lower = Unsorted.Nodes[0];
    Lower.Id = TEXT("UDemo.apply");
    Unsorted.Nodes.Add(Lower);
    FUBotDocExporter::SortApi(Unsorted);
    TestEqual(TEXT("Ordinal order"), Unsorted.Nodes[0].Id + TEXT(",") + Unsorted.Nodes[1].Id + TEXT(",") + Unsorted.Nodes[2].Id,
        FString(TEXT("UDemo.Apply,UDemo.Run,UDemo.apply")));
    TestEqual(TEXT("Pins keep their order"), Unsorted.Nodes[1].Inputs[0].Id, FString(TEXT("execute")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotDocExportMergeZhTest, "UBotCore.Editor.DocExport.MergeZh",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotDocExportMergeZhTest::RunTest(const FString& Parameters)
{
    using namespace UBotDocExportTestsPrivate;

    // "目标", "目标是 Demo", "有效"
    const FString Target = TEXT("\u76EE\u6807");
    const FString TargetIs = TEXT("\u76EE\u6807\u662F Demo");
    const FString ValidZh = TEXT("\u6709\u6548");

    FUBotDocApi Localized = MakeSampleApi();
    Localized.Culture = TEXT("zh-Hans");
    Localized.Nodes[0].Subtitle = TargetIs;
    Localized.Nodes[0].Inputs[1].Name = Target;
    Localized.Enums[0].Values[0].DisplayName = ValidZh;
    // Missing localized text never produces a *Zh field.
    Localized.Classes[0].Properties[0].DisplayName.Reset();

    FUBotDocApi Api = MakeSampleApi();
    int32 MergedFields = 0;
    FString Error;
    if (!TestTrue(TEXT("Merge succeeds"), FUBotDocExporter::MergeLocalized(Api, FUBotDocExporter::ToJson(Localized), MergedFields, Error)))
    {
        AddError(Error);
        return false;
    }
    TestEqual(TEXT("Three values differ"), MergedFields, 3);
    TestEqual(TEXT("Culture stays English"), Api.Culture, FString(TEXT("en")));
    TestEqual(TEXT("Same title, no titleZh"), Api.Nodes[0].TitleZh, FString());
    TestEqual(TEXT("subtitleZh"), Api.Nodes[0].SubtitleZh, TargetIs);
    TestEqual(TEXT("Unnamed exec pin"), Api.Nodes[0].Inputs[0].NameZh, FString());
    TestEqual(TEXT("nameZh"), Api.Nodes[0].Inputs[1].NameZh, Target);
    TestEqual(TEXT("Same output name"), Api.Nodes[0].Outputs[0].NameZh, FString());
    TestEqual(TEXT("Empty localized property name"), Api.Classes[0].Properties[0].DisplayNameZh, FString());
    TestEqual(TEXT("Enum value displayNameZh"), Api.Enums[0].Values[0].DisplayNameZh, ValidZh);

    const FString Json = FUBotDocExporter::ToJson(Api);
    TestTrue(TEXT("subtitleZh follows subtitle"), Json.Contains(FString::Printf(TEXT("\"subtitle\": \"Target is Demo\",\n      \"subtitleZh\": \"%s\",\n"), *TargetIs)));
    TestTrue(TEXT("nameZh follows name"), Json.Contains(FString::Printf(TEXT("\"name\": \"Target\",\n          \"nameZh\": \"%s\",\n"), *Target)));
    TestFalse(TEXT("No titleZh key"), Json.Contains(TEXT("\"titleZh\"")));

    FUBotDocApi Other = MakeSampleApi();
    Other.Plugin = TEXT("Other");
    FUBotDocApi Unchanged = MakeSampleApi();
    TestFalse(TEXT("Another plugin's export is refused"), FUBotDocExporter::MergeLocalized(Unchanged, FUBotDocExporter::ToJson(Other), MergedFields, Error));
    TestTrue(TEXT("The refusal names both plugins"), Error.Contains(TEXT("Other")) && Error.Contains(TEXT("Demo")));
    TestFalse(TEXT("Invalid JSON is refused"), FUBotDocExporter::MergeLocalized(Unchanged, TEXT("{ not json"), MergedFields, Error));

    // The *Zh fields belong to the English export, and the localized file must be of another culture.
    FUBotDocApi SameCulture = MakeSampleApi();
    TestFalse(TEXT("A localized export of the same culture is refused"), FUBotDocExporter::MergeLocalized(Unchanged, FUBotDocExporter::ToJson(SameCulture), MergedFields, Error));
    TestTrue(TEXT("The refusal names the culture"), Error.Contains(TEXT("same culture")) && Error.Contains(TEXT("'en'")));
    FUBotDocApi NoCulture = MakeSampleApi();
    NoCulture.Culture.Reset();
    TestFalse(TEXT("A localized export without culture is refused"), FUBotDocExporter::MergeLocalized(Unchanged, FUBotDocExporter::ToJson(NoCulture), MergedFields, Error));
    TestTrue(TEXT("The refusal names the field"), Error.Contains(TEXT("culture")));
    FUBotDocApi NotEnglish = MakeSampleApi();
    NotEnglish.Culture = TEXT("zh-Hans");
    TestFalse(TEXT("A main export that is not English is refused"), FUBotDocExporter::MergeLocalized(NotEnglish, FUBotDocExporter::ToJson(Localized), MergedFields, Error));
    TestTrue(TEXT("The refusal names both cultures"), Error.Contains(TEXT("zh-Hans")) && Error.Contains(TEXT("'en'")));
    TestEqual(TEXT("A refused merge sets nothing"), NotEnglish.Nodes[0].SubtitleZh, FString());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotDocExportCommandletPathsTest, "UBotCore.Editor.DocExport.CommandletPaths",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotDocExportCommandletPathsTest::RunTest(const FString& Parameters)
{
    const FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
    TestTrue(TEXT("Relative paths start at the project"), FPaths::IsSamePath(UUBotDocExportCommandlet::ResolvePath(TEXT("Saved/Docs/api.json")),
        FPaths::Combine(ProjectDir, TEXT("Saved/Docs/api.json"))));
    TestTrue(TEXT("Quotes are removed"), FPaths::IsSamePath(UUBotDocExportCommandlet::ResolvePath(TEXT("\"Saved/My Docs/api.json\"")),
        FPaths::Combine(ProjectDir, TEXT("Saved/My Docs/api.json"))));
#if PLATFORM_WINDOWS
    TestEqual(TEXT("Absolute paths are kept"), UUBotDocExportCommandlet::ResolvePath(TEXT("D:\\Docs\\api.json")), FString(TEXT("D:/Docs/api.json")));
#endif
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotDocExportPinTypesTest, "UBotCore.Editor.DocExport.PinTypes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotDocExportPinTypesTest::RunTest(const FString& Parameters)
{
    using namespace UBotDocExportTestsPrivate;

    FScopedEnglishCulture English;
    const auto Pin = [](FName Category, FName SubCategory = NAME_None, UObject* Object = nullptr,
        EPinContainerType Container = EPinContainerType::None, const FEdGraphTerminalType& Value = FEdGraphTerminalType())
    {
        return FEdGraphPinType(Category, SubCategory, Object, Container, /*bIsReference*/ false, Value);
    };
    const auto Terminal = [](FName Category, UObject* Object = nullptr, FName SubCategory = NAME_None)
    {
        FEdGraphTerminalType Type;
        Type.TerminalCategory = Category;
        Type.TerminalSubCategory = SubCategory;
        Type.TerminalSubCategoryObject = Object;
        return Type;
    };
    const auto Check = [this](const TCHAR* What, const FEdGraphPinType& PinType, const TCHAR* Expected)
    {
        TestEqual(What, DescribeType(FUBotDocExporter::MakePinType(PinType)), FString(Expected));
    };

    using K2 = UEdGraphSchema_K2;
    UEnum* const StatusEnum = StaticEnum<EUBotSampleStatus>();
    UScriptStruct* const CommandStruct = FUBotVelocityCommand::StaticStruct();

    // UE5 "real" pins carry the precision in the sub-category; the old float and double categories stay valid.
    Check(TEXT("real/float"), Pin(K2::PC_Real, K2::PC_Float), TEXT("float"));
    Check(TEXT("real/double"), Pin(K2::PC_Real, K2::PC_Double), TEXT("double"));
    Check(TEXT("legacy float"), Pin(K2::PC_Float), TEXT("float"));
    Check(TEXT("legacy double"), Pin(K2::PC_Double), TEXT("double"));

    // Enums: a byte pin bound to an enum and an enum pin are the same thing; a plain byte stays a byte.
    Check(TEXT("byte with an enum"), Pin(K2::PC_Byte, NAME_None, StatusEnum), TEXT("enum:EUBotSampleStatus"));
    Check(TEXT("enum pin"), Pin(K2::PC_Enum, NAME_None, StatusEnum), TEXT("enum:EUBotSampleStatus"));
    Check(TEXT("plain byte"), Pin(K2::PC_Byte), TEXT("byte"));
    TestFalse(TEXT("The enum has a display name"), FUBotDocExporter::MakePinType(Pin(K2::PC_Byte, NAME_None, StatusEnum)).SubtypeDisplay.IsEmpty());

    // Structs: the math structs have their own categories, every other struct keeps its C++ name.
    Check(TEXT("FVector"), Pin(K2::PC_Struct, NAME_None, TBaseStructure<FVector>::Get()), TEXT("vector"));
    Check(TEXT("FRotator"), Pin(K2::PC_Struct, NAME_None, TBaseStructure<FRotator>::Get()), TEXT("rotator"));
    Check(TEXT("FTransform"), Pin(K2::PC_Struct, NAME_None, TBaseStructure<FTransform>::Get()), TEXT("transform"));
    Check(TEXT("Other struct"), Pin(K2::PC_Struct, NAME_None, CommandStruct), TEXT("struct:FUBotVelocityCommand"));
    TestEqual(TEXT("Struct display name"), FUBotDocExporter::MakePinType(Pin(K2::PC_Struct, NAME_None, CommandStruct)).SubtypeDisplay, FString(TEXT("UBot Velocity Command")));

    // Objects, classes, soft references and interfaces name their class with its prefix.
    Check(TEXT("object"), Pin(K2::PC_Object, NAME_None, AActor::StaticClass()), TEXT("object:AActor"));
    Check(TEXT("class"), Pin(K2::PC_Class, NAME_None, AActor::StaticClass()), TEXT("class:AActor"));
    Check(TEXT("soft object"), Pin(K2::PC_SoftObject, NAME_None, UActorComponent::StaticClass()), TEXT("softobject:UActorComponent"));
    Check(TEXT("soft class"), Pin(K2::PC_SoftClass, NAME_None, UActorComponent::StaticClass()), TEXT("softclass:UActorComponent"));
    Check(TEXT("interface"), Pin(K2::PC_Interface, NAME_None, UUBotActuator::StaticClass()), TEXT("interface:UUBotActuator"));

    // Categories without further information pass through under their pin category name.
    Check(TEXT("bool"), Pin(K2::PC_Boolean), TEXT("bool"));
    Check(TEXT("int64"), Pin(K2::PC_Int64), TEXT("int64"));
    Check(TEXT("text"), Pin(K2::PC_Text), TEXT("text"));
    Check(TEXT("exec"), Pin(K2::PC_Exec), TEXT("exec"));
    Check(TEXT("wildcard"), Pin(K2::PC_Wildcard), TEXT("wildcard"));
    Check(TEXT("delegate"), Pin(K2::PC_Delegate), TEXT("delegate"));
    Check(TEXT("multicast delegate"), Pin(K2::PC_MCDelegate), TEXT("mcdelegate"));

    // Containers.
    Check(TEXT("array"), Pin(K2::PC_Int, NAME_None, nullptr, EPinContainerType::Array), TEXT("array of int"));
    Check(TEXT("set"), Pin(K2::PC_Name, NAME_None, nullptr, EPinContainerType::Set), TEXT("set of name"));
    Check(TEXT("array of enums"), Pin(K2::PC_Byte, NAME_None, StatusEnum, EPinContainerType::Array), TEXT("array of enum:EUBotSampleStatus"));
    Check(TEXT("map to vector"),
        Pin(K2::PC_String, NAME_None, nullptr, EPinContainerType::Map, Terminal(K2::PC_Struct, TBaseStructure<FVector>::Get())),
        TEXT("map of string -> vector"));
    Check(TEXT("map to object"),
        Pin(K2::PC_Int64, NAME_None, nullptr, EPinContainerType::Map, Terminal(K2::PC_Object, UActorComponent::StaticClass())),
        TEXT("map of int64 -> object:UActorComponent"));
    Check(TEXT("map to real"),
        Pin(K2::PC_Name, NAME_None, nullptr, EPinContainerType::Map, Terminal(K2::PC_Real, nullptr, K2::PC_Double)),
        TEXT("map of name -> double"));

    // Reference and const flags are kept; only a map has a value type.
    FEdGraphPinType Reference = Pin(K2::PC_Int);
    Reference.bIsReference = true;
    Reference.bIsConst = true;
    const FUBotDocPinType ReferenceType = FUBotDocExporter::MakePinType(Reference);
    TestTrue(TEXT("byRef and const"), ReferenceType.bByRef && ReferenceType.bConst);
    const FUBotDocPinType PlainType = FUBotDocExporter::MakePinType(Pin(K2::PC_Int));
    TestFalse(TEXT("Not byRef, not const"), PlainType.bByRef || PlainType.bConst);
    TestFalse(TEXT("An array has no value type"), FUBotDocExporter::MakePinType(Pin(K2::PC_Int, NAME_None, nullptr, EPinContainerType::Array)).ValueType.IsValid());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotDocExportJsonEscapingTest, "UBotCore.Editor.DocExport.JsonEscaping",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotDocExportJsonEscapingTest::RunTest(const FString& Parameters)
{
    using namespace UBotDocExportTestsPrivate;

    // U+1F600 is the surrogate pair D83D DE00. Built from code units so the source encoding does not matter.
    FString Pair;
    Pair.AppendChar(static_cast<TCHAR>(0xD83D));
    Pair.AppendChar(static_cast<TCHAR>(0xDE00));
    FString LoneHigh;
    LoneHigh.AppendChar(static_cast<TCHAR>(0xD83D));
    FString LoneLow;
    LoneLow.AppendChar(static_cast<TCHAR>(0xDE00));

    FUBotDocApi Api = MakeSampleApi();
    // A pair stays as it is. A lone high surrogate (before another character, before another lone one
    // and at the end of the string) and a lone low surrogate become \u escapes, like JSON.stringify.
    Api.Nodes[0].Tooltip = TEXT("a") + Pair + TEXT("b") + LoneHigh + TEXT("c") + LoneLow + TEXT("d") + LoneHigh + LoneHigh + Pair;
    Api.Nodes[0].Subtitle = TEXT("end") + LoneHigh;
    Api.Nodes[0].Title = TEXT("\\ \"q\" \t\b\f\r");

    const FString Json = FUBotDocExporter::ToJson(Api);
    TestTrue(TEXT("Pair kept, lone surrogates escaped"),
        Json.Contains(TEXT("\"tooltip\": \"a") + Pair + TEXT("b\\ud83dc\\ude00d\\ud83d\\ud83d") + Pair + TEXT("\",\n"), ESearchCase::CaseSensitive));
    TestTrue(TEXT("A lone surrogate at the end"), Json.Contains(TEXT("\"subtitle\": \"end\\ud83d\",\n"), ESearchCase::CaseSensitive));
    TestTrue(TEXT("Control characters"), Json.Contains(TEXT("\"title\": \"\\\\ \\\"q\\\" \\t\\b\\f\\r\",\n"), ESearchCase::CaseSensitive));

    // What the commandlet writes is UTF-8: a pair is one 4-byte sequence.
    const FTCHARToUTF8 Utf8(*Pair);
    TestEqual(TEXT("A pair is one 4-byte UTF-8 sequence"), static_cast<int32>(Utf8.Length()), 4);

    // The text is valid JSON and the strings survive a round trip (the lone surrogates come back as code units).
    TSharedPtr<FJsonObject> Parsed;
    if (TestTrue(TEXT("The text parses"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Parsed)) && Parsed.IsValid())
    {
        const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
        if (TestTrue(TEXT("Nodes"), Parsed->TryGetArrayField(TEXT("nodes"), Nodes)) && Nodes->Num() == 1)
        {
            const TSharedPtr<FJsonObject> Node = (*Nodes)[0]->AsObject();
            FString Tooltip;
            FString Title;
            Node->TryGetStringField(TEXT("tooltip"), Tooltip);
            Node->TryGetStringField(TEXT("title"), Title);
            TestEqual(TEXT("The tooltip survives a round trip"), Tooltip, Api.Nodes[0].Tooltip);
            TestEqual(TEXT("The title survives a round trip"), Title, Api.Nodes[0].Title);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotDocExportCultureTest, "UBotCore.Editor.DocExport.Culture",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotDocExportCultureTest::RunTest(const FString& Parameters)
{
    using namespace UBotDocExportTestsPrivate;

    // The "culture" of a file is "en" for every English, never "en-US".
    for (const TCHAR* English : { TEXT("en"), TEXT("en-US"), TEXT("en-GB") })
    {
        FScopedLanguage Language(English);
        TestEqual(*FString::Printf(TEXT("%s is en"), English), FUBotDocExporter::GetCurrentCultureName(), FString(TEXT("en")));
    }
    {
        FScopedLanguage Language(TEXT("zh-Hans"));
        TestEqual(TEXT("Chinese keeps its name"), FUBotDocExporter::GetCurrentCultureName(), FString(TEXT("zh-Hans")));
    }

    // Which command lines choose the language themselves (the engine reads the same two keys).
    TestFalse(TEXT("Empty"), UUBotDocExportCommandlet::HasCultureOverride(FString()));
    TestFalse(TEXT("No culture"), UUBotDocExportCommandlet::HasCultureOverride(TEXT("UBotDev.uproject -run=UBotDocExport -Plugin=UBotCore -Out=a.json -NullRHI")));
    TestTrue(TEXT("-culture"), UUBotDocExportCommandlet::HasCultureOverride(TEXT("-run=UBotDocExport -culture=en -Out=a.json")));
    TestTrue(TEXT("-CULTURE is not case sensitive"), UUBotDocExportCommandlet::HasCultureOverride(TEXT("-run=UBotDocExport -CULTURE=zh-Hans")));
    TestTrue(TEXT("-language"), UUBotDocExportCommandlet::HasCultureOverride(TEXT("-run=UBotDocExport -language=zh-Hans")));
    // FParse::Value skips whitespace after "=", so only a trailing "-culture=" has no value (the engine reads it the same way).
    TestFalse(TEXT("-culture= without a value"), UUBotDocExportCommandlet::HasCultureOverride(TEXT("-run=UBotDocExport -Out=a.json -culture=")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotDocExportCommandletMainTest, "UBotCore.Editor.DocExport.CommandletMain",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotDocExportCommandletMainTest::RunTest(const FString& Parameters)
{
    using namespace UBotDocExportTestsPrivate;

    // Main logs every refusal as an error.
    for (const TCHAR* Expected : { TEXT("requires a value"), TEXT("Pass exactly one of"), TEXT("-Out=<File> is required"), TEXT("was not found"),
        TEXT("Export failed with"), TEXT("Cannot read the node list"), TEXT("Cannot read the localized export"), TEXT("not valid JSON"), TEXT("same culture") })
    {
        AddExpectedErrorPlain(Expected, EAutomationExpectedErrorFlags::Contains, 0);
    }

    // Starts in Chinese: Main must not take the process language for an English file. (A process started
    // with -culture or -language keeps what it was told, so there it starts in English.) Whatever Main
    // switches to is undone when this scope ends.
    const bool bProcessChoosesCulture = UUBotDocExportCommandlet::HasCultureOverride(FCommandLine::Get());
    FScopedLanguage StartLanguage(bProcessChoosesCulture ? TEXT("en") : TEXT("zh-Hans"));
    if (bProcessChoosesCulture)
    {
        AddInfo(TEXT("The editor was started with -culture or -language; the default-to-English rule was not checked."));
    }

    const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("UBotDocExportTests"), FGuid::NewGuid().ToString(EGuidFormats::Digits));
    ON_SCOPE_EXIT
    {
        IFileManager::Get().DeleteDirectory(*Dir, /*RequireExists*/ false, /*Tree*/ true);
    };
    const FString ListPath = FPaths::Combine(Dir, TEXT("nodes.txt"));
    const FString OutPath = FPaths::Combine(Dir, TEXT("out"), TEXT("api.json"));
    const FString MissingPath = FPaths::Combine(Dir, TEXT("missing"));
    const FString LocalizedPath = FPaths::Combine(Dir, TEXT("zh.json"));
    if (!TestTrue(TEXT("Write the node list"), FFileHelper::SaveStringToFile(TEXT("# one node\n/Script/Engine.KismetSystemLibrary:PrintString\n"), *ListPath)))
    {
        return false;
    }

    TStrongObjectPtr<UUBotDocExportCommandlet> Commandlet(NewObject<UUBotDocExportCommandlet>());
    const auto Quote = [](const FString& Path) { return FString::Printf(TEXT("\"%s\""), *Path); };
    const FString Out = TEXT("-Out=") + Quote(OutPath);
    const FString List = TEXT("-Nodes=") + Quote(ListPath);

    // Arguments and inputs that are refused: exit code 1 and nothing is written.
    const struct
    {
        const TCHAR* Name;
        FString Params;
    } Refused[] =
    {
        { TEXT("neither -Plugin nor -Nodes"), Out },
        { TEXT("both -Plugin and -Nodes"), TEXT("-Plugin=UBotCore ") + List + TEXT(" ") + Out },
        { TEXT("no -Out"), List },
        { TEXT("-Out without a value"), List + TEXT(" -Out") },
        { TEXT("unknown plugin"), TEXT("-Plugin=UBotNoSuchPlugin ") + Out },
        { TEXT("missing node list"), TEXT("-Nodes=") + Quote(MissingPath) + TEXT(" ") + Out },
        { TEXT("missing -MergeZh file"), List + TEXT(" ") + Out + TEXT(" -MergeZh=") + Quote(MissingPath) },
        { TEXT("-MergeZh that is not JSON"), List + TEXT(" ") + Out + TEXT(" -MergeZh=") + Quote(ListPath) },
    };
    for (const auto& Case : Refused)
    {
        TestEqual(*FString::Printf(TEXT("%s exits with 1"), Case.Name), Commandlet->Main(Case.Params), 1);
        TestFalse(*FString::Printf(TEXT("%s writes nothing"), Case.Name), IFileManager::Get().FileExists(*OutPath));
    }

    // A good run exits with 0 and writes the English file whatever the process language was.
    if (!TestEqual(TEXT("Export exits with 0"), Commandlet->Main(List + TEXT(" ") + Out), 0))
    {
        return false;
    }
    TArray<uint8> Bytes;
    if (!TestTrue(TEXT("The file is written"), FFileHelper::LoadFileToArray(Bytes, *OutPath)))
    {
        return false;
    }
    TestFalse(TEXT("No byte order mark"), Bytes.Num() >= 3 && Bytes[0] == 0xEF && Bytes[1] == 0xBB && Bytes[2] == 0xBF);
    FString English;
    FFileHelper::LoadFileToString(English, *OutPath);
    TestTrue(TEXT("Starts like the contract"), English.StartsWith(TEXT("{\n  \"format\": 1,\n  \"plugin\": \"Engine\",\n"), ESearchCase::CaseSensitive));
    TestTrue(TEXT("Ends with a newline"), English.EndsWith(TEXT("}\n"), ESearchCase::CaseSensitive));
    TestTrue(TEXT("Culture is en"), English.Contains(TEXT("\n  \"culture\": \"en\",\n"), ESearchCase::CaseSensitive));
    TestTrue(TEXT("The title is the English one"), English.Contains(TEXT("\"title\": \"Print String\""), ESearchCase::CaseSensitive));

    // -MergeZh refuses an export of the English culture (nothing to localize) and keeps the file as it was.
    TestEqual(TEXT("-MergeZh with an English file exits with 1"), Commandlet->Main(List + TEXT(" ") + Out + TEXT(" -MergeZh=") + Quote(OutPath)), 1);
    FString Unchanged;
    FFileHelper::LoadFileToString(Unchanged, *OutPath);
    TestTrue(TEXT("The refused merge leaves the file alone"), Unchanged.Equals(English, ESearchCase::CaseSensitive));

    // -MergeZh with a Chinese export adds the *Zh fields.
    FUBotDocApi Localized;
    Localized.Plugin = TEXT("Engine");
    Localized.Culture = TEXT("zh-Hans");
    FUBotDocNode& Node = Localized.Nodes.AddDefaulted_GetRef();
    Node.Id = TEXT("UKismetSystemLibrary.PrintString");
    Node.Title = TEXT("打印字符串");
    if (!TestTrue(TEXT("Write the localized export"), FFileHelper::SaveStringToFile(FUBotDocExporter::ToJson(Localized), *LocalizedPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)))
    {
        return false;
    }
    TestEqual(TEXT("-MergeZh exits with 0"), Commandlet->Main(List + TEXT(" ") + Out + TEXT(" -MergeZh=") + Quote(LocalizedPath)), 0);
    FString Merged;
    FFileHelper::LoadFileToString(Merged, *OutPath);
    TestTrue(TEXT("titleZh is added"), Merged.Contains(TEXT("\"titleZh\": \"打印字符串\""), ESearchCase::CaseSensitive));
    TestTrue(TEXT("The file stays English"), Merged.Contains(TEXT("\"title\": \"Print String\""), ESearchCase::CaseSensitive));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
