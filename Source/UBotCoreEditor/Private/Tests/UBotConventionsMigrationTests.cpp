#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "K2Node_CallFunction.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UBotConventionsLibrary.h"
#include "UObject/CoreRedirects.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace UBotConventionsMigrationTests
{
    struct FFunctionMigration
    {
        const TCHAR* OldFunction;
        const TCHAR* NewFunction;
        const TCHAR* OldPin = nullptr;
        const TCHAR* NewPin = nullptr;
    };

    const FFunctionMigration MathMigrations[] =
    {
        { TEXT("PositionUEToROS"), TEXT("PositionUEToRightHanded") },
        { TEXT("PositionROSToUE"), TEXT("PositionRightHandedToUE"), TEXT("ROSMeters"), TEXT("Meters") },
        { TEXT("VectorUEToROS"), TEXT("VectorUEToRightHanded") },
        { TEXT("VectorROSToUE"), TEXT("VectorRightHandedToUE") },
        { TEXT("AngularVelocityUEToROS"), TEXT("AngularVelocityUEToRightHanded") },
        { TEXT("AngularVelocityROSToUE"), TEXT("AngularVelocityRightHandedToUE"), TEXT("ROSRadiansPerSecond"), TEXT("RadiansPerSecond") },
        { TEXT("RotationUEToROS"), TEXT("RotationUEToRightHanded") },
        { TEXT("RotationROSToUE"), TEXT("RotationRightHandedToUE"), TEXT("ROSRotation"), TEXT("Rotation") },
        { TEXT("TransformUEToROS"), TEXT("TransformUEToRightHanded") },
        { TEXT("TransformROSToUE"), TEXT("TransformRightHandedToUE"), TEXT("ROSTransform"), TEXT("Transform") },
        { TEXT("YawUEToROS"), TEXT("YawUEToRightHanded") },
        { TEXT("YawROSToUE"), TEXT("YawRightHandedToUE"), TEXT("ROSRadians"), TEXT("Radians") },
    };

    bool IsInput(const FProperty* Property)
    {
        return Property->HasAnyPropertyFlags(CPF_Parm)
            && (!Property->HasAnyPropertyFlags(CPF_OutParm) || Property->HasAnyPropertyFlags(CPF_ConstParm));
    }

    // Nonzero, asymmetric inputs make a lost default or wire observable after compilation.
    FString SampleDefault(const UEdGraphPin& Pin)
    {
        if (Pin.PinType.PinCategory == UEdGraphSchema_K2::PC_String)
        {
            return TEXT("Robot A/2");
        }
        if (Pin.PinType.PinCategory == UEdGraphSchema_K2::PC_Int)
        {
            return Pin.PinName == TEXT("Nanosec") ? TEXT("250000000") : TEXT("3");
        }
        return TEXT("1.25");
    }

    UEdGraphPin* MakeSampleValue(FAutomationTestBase& Test, UEdGraph* Graph, const FEdGraphPinType& Type)
    {
        // A const-ref struct pin has no editable literal in the real Blueprint editor. Use the same
        // Make nodes an existing asset needs, so the fixture represents a compilable 0.1.x graph.
        FName FunctionName;
        if (Type.PinSubCategoryObject == TBaseStructure<FVector>::Get())
        {
            FunctionName = TEXT("MakeVector");
        }
        else if (Type.PinSubCategoryObject == TBaseStructure<FQuat>::Get())
        {
            FunctionName = TEXT("MakeQuat");
        }
        else if (Type.PinSubCategoryObject == TBaseStructure<FTransform>::Get())
        {
            FunctionName = TEXT("MakeTransform");
        }
        else
        {
            Test.AddError(TEXT("No constant Make node is defined for this reference input type."));
            return nullptr;
        }
        FGraphNodeCreator<UK2Node_CallFunction> Creator(*Graph);
        UK2Node_CallFunction* Make = Creator.CreateNode();
        Make->SetFromFunction(UKismetMathLibrary::StaticClass()->FindFunctionByName(FunctionName));
        Creator.Finalize();
        const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
        for (UEdGraphPin* Pin : Make->Pins)
        {
            if (Pin->Direction != EGPD_Input || Pin->PinName == UEdGraphSchema_K2::PN_Self)
            {
                continue;
            }
            FString Value;
            if (FunctionName == TEXT("MakeVector"))
            {
                Value = Pin->PinName == TEXT("X") ? TEXT("1.25") : Pin->PinName == TEXT("Y") ? TEXT("-2.5") : TEXT("3.75");
            }
            else if (FunctionName == TEXT("MakeQuat"))
            {
                // Binary fractions are exact in both MakeQuat's float inputs and FQuat's doubles.
                Value = Pin->PinName == TEXT("X") ? TEXT("0.125") : Pin->PinName == TEXT("Y") ? TEXT("0.25")
                    : Pin->PinName == TEXT("Z") ? TEXT("0.375") : TEXT("0.875");
            }
            else
            {
                Value = Pin->PinName == TEXT("Location") ? TEXT("1.25,-2.5,3.75")
                    : Pin->PinName == TEXT("Scale") ? TEXT("1,1,1") : TEXT("0,0,0");
            }
            Schema->TrySetDefaultValue(*Pin, Value);
            Test.TestEqual(TEXT("Make node accepts the constant input"), *Pin->DefaultValue, *Value);
        }
        return Make->GetReturnValuePin();
    }

    void SetSampleInput(FProperty* Property, uint8* Parameters, bool bAlternate)
    {
        void* Value = Property->ContainerPtrToValuePtr<void>(Parameters);
        if (const FStructProperty* Struct = CastField<FStructProperty>(Property))
        {
            if (Struct->Struct == TBaseStructure<FVector>::Get())
            {
                *static_cast<FVector*>(Value) = bAlternate ? FVector(10.0, 20.0, 30.0) : FVector(1.25, -2.5, 3.75);
            }
            else if (Struct->Struct == TBaseStructure<FQuat>::Get())
            {
                *static_cast<FQuat*>(Value) = bAlternate ? FQuat::Identity : FQuat(0.125, 0.25, 0.375, 0.875);
            }
            else if (Struct->Struct == TBaseStructure<FTransform>::Get())
            {
                *static_cast<FTransform*>(Value) = FTransform(FQuat::Identity,
                    bAlternate ? FVector(10.0, 20.0, 30.0) : FVector(1.25, -2.5, 3.75));
            }
        }
        else if (const FDoubleProperty* DoubleProperty = CastField<FDoubleProperty>(Property))
        {
            DoubleProperty->SetPropertyValue(Value, bAlternate ? 10.0 : 1.25);
        }
        else if (const FIntProperty* IntProperty = CastField<FIntProperty>(Property))
        {
            IntProperty->SetPropertyValue(Value, bAlternate ? 10 : Property->GetFName() == TEXT("Nanosec") ? 250000000 : 3);
        }
        else if (const FStrProperty* String = CastField<FStrProperty>(Property))
        {
            String->SetPropertyValue(Value, bAlternate ? TEXT("Other") : TEXT("Robot A/2"));
        }
    }

    struct FCompiledFixture
    {
        FName GraphName;
        UFunction* Target;
        bool bWired;
        bool bSplit;
    };

    bool AddLegacyFixture(FAutomationTestBase& Test, UBlueprint* Blueprint, const FFunctionMigration& Migration,
        UFunction* Target, bool bWired, bool bSplit, TArray<FCompiledFixture>& Fixtures)
    {
        const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
        const FName GraphName(*FString::Printf(TEXT("Migrate_%s_%s"), Migration.OldFunction,
            bSplit ? TEXT("Split") : bWired ? TEXT("Wired") : TEXT("Default")));
        UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, GraphName,
            UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
        FBlueprintEditorUtils::AddFunctionGraph(Blueprint, Graph, true, Target);
        TArray<UK2Node_FunctionEntry*> Entries;
        TArray<UK2Node_FunctionResult*> Results;
        Graph->GetNodesOfClass(Entries);
        Graph->GetNodesOfClass(Results);
        if (!Test.TestEqual(TEXT("Fixture has a function entry"), Entries.Num(), 1)
            || !Test.TestEqual(TEXT("Fixture has a function result"), Results.Num(), 1))
        {
            return false;
        }

        FGraphNodeCreator<UK2Node_CallFunction> Creator(*Graph);
        UK2Node_CallFunction* Call = Creator.CreateNode();
        Call->SetFromFunction(Target);
        Creator.Finalize();

        TMap<FName, FString> SavedDefaults;
        TMap<FName, UEdGraphPin*> SavedLinks;
        for (UEdGraphPin* Pin : Call->Pins)
        {
            if (Pin->PinName == UEdGraphSchema_K2::PN_Self || Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
            {
                continue;
            }
            if (Pin->Direction == EGPD_Input)
            {
                if (bWired)
                {
                    UEdGraphPin* Source = Entries[0]->FindPin(Pin->PinName, EGPD_Output);
                    if (!Test.TestNotNull(TEXT("Matching function input exists"), Source))
                    {
                        return false;
                    }
                    Test.TestTrue(TEXT("Fixture input wire connects"), Schema->TryCreateConnection(Source, Pin));
                    SavedLinks.Add(Pin->PinName, Source);
                }
                else if (Pin->PinType.bIsReference && !bSplit)
                {
                    UEdGraphPin* Source = MakeSampleValue(Test, Graph, Pin->PinType);
                    if (!Test.TestNotNull(TEXT("Reference input has a constant producer"), Source))
                    {
                        return false;
                    }
                    Test.TestTrue(TEXT("Constant Make node connects to the reference input"), Schema->TryCreateConnection(Source, Pin));
                    SavedLinks.Add(Pin->PinName, Source);
                }
                else if (!Pin->PinType.bIsReference)
                {
                    const FString Value = SampleDefault(*Pin);
                    Schema->TrySetDefaultValue(*Pin, Value);
                    Test.TestEqual(TEXT("Legacy scalar or string pin accepts its default"), *Pin->DefaultValue, *Value);
                    SavedDefaults.Add(Pin->PinName, Pin->DefaultValue);
                }
            }
            else
            {
                UEdGraphPin* Destination = Results[0]->FindPin(Pin->PinName, EGPD_Input);
                if (!Test.TestNotNull(TEXT("Matching function output exists"), Destination))
                {
                    return false;
                }
                Test.TestTrue(TEXT("Fixture output wire connects"), Schema->TryCreateConnection(Pin, Destination));
                SavedLinks.Add(Pin->PinName, Destination);
            }
        }

        if (bSplit)
        {
            UEdGraphPin* Parent = Call->FindPin(Migration.NewPin, EGPD_Input);
            UEdGraphPin* SourceParent = Entries[0]->FindPin(Migration.NewPin, EGPD_Output);
            Schema->SplitPin(Parent, false);
            Schema->SplitPin(SourceParent, false);
            if (!Test.TestEqual(TEXT("Vector fixture splits into three pins"), Parent->SubPins.Num(), 3))
            {
                return false;
            }
            for (int32 Index = 0; Index < Parent->SubPins.Num(); ++Index)
            {
                UEdGraphPin* SubPin = Parent->SubPins[Index];
                Schema->TrySetDefaultValue(*SubPin, Index == 0 ? TEXT("1.25") : Index == 1 ? TEXT("-2.5") : TEXT("3.75"));
                SavedDefaults.Add(SubPin->PinName, SubPin->DefaultValue);
            }
            Test.TestTrue(TEXT("Split X input wire connects"), Schema->TryCreateConnection(SourceParent->SubPins[0], Parent->SubPins[0]));
            SavedLinks.Add(Parent->SubPins[0]->PinName, SourceParent->SubPins[0]);
        }

        // Reproduce the member and pin names serialized by 0.1.x, without reintroducing old UFunctions.
        // Reconstruction itself must resolve the ini redirects; the test never registers redirects.
        if (Migration.OldPin)
        {
            UEdGraphPin* Pin = Call->FindPin(Migration.NewPin, EGPD_Input);
            for (UEdGraphPin* SubPin : Pin->SubPins)
            {
                SubPin->PinName = FName(*SubPin->PinName.ToString().Replace(Migration.NewPin, Migration.OldPin));
            }
            Pin->PinName = Migration.OldPin;
        }
        Call->FunctionReference.SetExternalMember(Migration.OldFunction, UUBotConventionsLibrary::StaticClass());
        if (UEdGraphPin* Self = Call->FindPin(UEdGraphSchema_K2::PN_Self))
        {
            Self->PinType.PinSubCategoryObject = UUBotConventionsLibrary::StaticClass();
            Self->DefaultObject = UUBotConventionsLibrary::StaticClass()->GetDefaultObject();
        }
        Call->ReconstructNode();
        Test.TestEqual(TEXT("Rebuilt call resolves to the replacement function"), Call->GetTargetFunction(), Target);
        for (const TPair<FName, FString>& Default : SavedDefaults)
        {
            UEdGraphPin* Pin = Call->FindPin(Default.Key, EGPD_Input);
            if (Test.TestNotNull(*FString::Printf(TEXT("Rebuilt input %s exists"), *Default.Key.ToString()), Pin)
                && !SavedLinks.Contains(Default.Key) && Pin->SubPins.IsEmpty())
            {
                Test.TestEqual(TEXT("Legacy default survives reconstruction"), Pin->DefaultValue, Default.Value);
            }
        }
        for (const TPair<FName, UEdGraphPin*>& Link : SavedLinks)
        {
            UEdGraphPin* Pin = Call->FindPin(Link.Key);
            if (Test.TestNotNull(*FString::Printf(TEXT("Rebuilt wired pin %s exists"), *Link.Key.ToString()), Pin))
            {
                Test.TestTrue(TEXT("Legacy wire survives reconstruction"), Pin->LinkedTo.Contains(Link.Value));
            }
        }
        for (const UEdGraphPin* Pin : Call->Pins)
        {
            Test.TestFalse(TEXT("Migration leaves no orphaned pins"), Pin->bOrphanedPin);
        }
        Fixtures.Add({ GraphName, Target, bWired, bSplit });
        return true;
    }

    void CheckCompiledFixture(FAutomationTestBase& Test, UBlueprint* Blueprint, const FCompiledFixture& Fixture)
    {
        UFunction* Compiled = Blueprint->GeneratedClass->FindFunctionByName(Fixture.GraphName);
        if (!Test.TestNotNull(TEXT("Migrated function compiled"), Compiled))
        {
            return;
        }
        FStructOnScope Expected(Fixture.Target);
        FStructOnScope Actual(Compiled);
        for (TFieldIterator<FProperty> It(Fixture.Target); It; ++It)
        {
            if (IsInput(*It))
            {
                SetSampleInput(*It, Expected.GetStructMemory(), false);
                if (Fixture.bSplit)
                {
                    It->ContainerPtrToValuePtr<FVector>(Expected.GetStructMemory())->X = 10.0;
                }
            }
        }
        for (TFieldIterator<FProperty> It(Compiled); It; ++It)
        {
            if (IsInput(*It))
            {
                SetSampleInput(*It, Actual.GetStructMemory(), !Fixture.bWired);
            }
        }
        Fixture.Target->GetOwnerClass()->GetDefaultObject()->ProcessEvent(Fixture.Target, Expected.GetStructMemory());
        Blueprint->GeneratedClass->GetDefaultObject()->ProcessEvent(Compiled, Actual.GetStructMemory());
        for (TFieldIterator<FProperty> It(Fixture.Target); It; ++It)
        {
            if (It->HasAnyPropertyFlags(CPF_OutParm) && !It->HasAnyPropertyFlags(CPF_ConstParm))
            {
                FProperty* Output = FindFProperty<FProperty>(Compiled, It->GetFName());
                if (Test.TestNotNull(TEXT("Compiled output exists"), Output))
                {
                    Test.TestTrue(*FString::Printf(TEXT("%s.%s preserves the result"), *Fixture.GraphName.ToString(), *It->GetName()),
                        It->Identical(It->ContainerPtrToValuePtr<void>(Expected.GetStructMemory()),
                            Output->ContainerPtrToValuePtr<void>(Actual.GetStructMemory())));
                }
            }
        }
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotConventionsBlueprintMigrationTest, "UBotCore.Editor.Conventions.BlueprintMigration", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotConventionsBlueprintMigrationTest::RunTest(const FString& Parameters)
{
    using namespace UBotConventionsMigrationTests;
    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(UObject::StaticClass(), GetTransientPackage(),
        MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("UBotConventionsMigration")),
        BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
    if (!TestNotNull(TEXT("Transient migration Blueprint created"), Blueprint))
    {
        return false;
    }
    TArray<FCompiledFixture> Fixtures;
    const auto AddMigration = [this, Blueprint, &Fixtures](const FFunctionMigration& Migration, const FString& TargetClass)
    {
        const FString OldPath = UUBotConventionsLibrary::StaticClass()->GetPathName() + TEXT(".") + Migration.OldFunction;
        const FString ExpectedPath = FCoreRedirectObjectName(TargetClass + TEXT(".") + Migration.NewFunction).ToString();
        const FString Redirected = FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Function, FCoreRedirectObjectName(OldPath)).ToString();
        TestEqual(TEXT("Plugin config registers the function migration"), *Redirected, *ExpectedPath);
        UFunction* Target = FindObject<UFunction>(nullptr, *Redirected);
        if (TestNotNull(*FString::Printf(TEXT("Migration target %s exists"), *ExpectedPath), Target))
        {
            AddLegacyFixture(*this, Blueprint, Migration, Target, false, false, Fixtures);
            AddLegacyFixture(*this, Blueprint, Migration, Target, true, false, Fixtures);
        }
    };
    for (const FFunctionMigration& Migration : MathMigrations)
    {
        AddMigration(Migration, UUBotConventionsLibrary::StaticClass()->GetPathName());
    }
    AddLegacyFixture(*this, Blueprint, MathMigrations[1],
        UUBotConventionsLibrary::StaticClass()->FindFunctionByName(TEXT("PositionRightHandedToUE")), false, true, Fixtures);

    // Optional cross-class coverage uses reflection only; Core has no ROS build or runtime dependency.
    const FString ROSClassPath(TEXT("/Script/UBotROS.UBotROSConventionsLibrary"));
    if (FindObject<UClass>(nullptr, *ROSClassPath))
    {
        const TCHAR* const Names[] = { TEXT("CameraLinkToOpticalRotation"), TEXT("GetOpticalFrameSuffix"),
            TEXT("SecondsToRosTime"), TEXT("RosTimeToSeconds"), TEXT("SanitizeRosName") };
        for (const TCHAR* Name : Names)
        {
            AddMigration({ Name, Name }, ROSClassPath);
        }
    }
    else
    {
        AddInfo(TEXT("UBotROS is not loaded: five optional cross-class migration cases were not executed."));
    }

    AddInfo(FString::Printf(TEXT("Reconstructed %d legacy function graphs; compiling and executing the migrations."), Fixtures.Num()));
    FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);
    if (TestTrue(TEXT("Migrated Blueprint compiles"), Blueprint->Status == BS_UpToDate))
    {
        for (const FCompiledFixture& Fixture : Fixtures)
        {
            CheckCompiledFixture(*this, Blueprint, Fixture);
        }
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
