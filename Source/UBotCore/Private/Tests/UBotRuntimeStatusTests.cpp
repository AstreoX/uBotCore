#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UBotRuntimeStatus.h"

#define LOCTEXT_NAMESPACE "UBotRuntimeStatusTests"

namespace UBotRuntimeStatusTests
{
    TSharedPtr<FJsonObject> ParseJsonObject(const FString& Text)
    {
        TSharedPtr<FJsonObject> Object;
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
        FJsonSerializer::Deserialize(Reader, Object);
        return Object;
    }

    /** "YYYY-MM-DDTHH:MM:SSZ". */
    bool IsIsoUtcTimestamp(const FString& Text)
    {
        const TCHAR* Pattern = TEXT("dddd-dd-ddTdd:dd:ddZ");
        if (Text.Len() != FCString::Strlen(Pattern))
        {
            return false;
        }
        for (int32 Index = 0; Index < Text.Len(); ++Index)
        {
            const bool bMatches = Pattern[Index] == TEXT('d') ? FChar::IsDigit(Text[Index]) : Text[Index] == Pattern[Index];
            if (!bMatches)
            {
                return false;
            }
        }
        return true;
    }

    /** Keys of a JSON object in the order they appear in Text (all keys must be distinct within Text's object). */
    TArray<FString> KeysInOrder(const FString& Text, const FJsonObject& Object)
    {
        TArray<TPair<int32, FString>> Positions;
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Object.Values)
        {
            Positions.Emplace(Text.Find(FString::Printf(TEXT("\"%s\":"), *Field.Key), ESearchCase::CaseSensitive), Field.Key);
        }
        Positions.Sort([](const TPair<int32, FString>& A, const TPair<int32, FString>& B) { return A.Key < B.Key; });
        TArray<FString> Keys;
        for (const TPair<int32, FString>& Position : Positions)
        {
            Keys.Add(Position.Value);
        }
        return Keys;
    }

    FUBotRuntimeItem MakeItem(const TCHAR* Id, const TCHAR* Value, EUBotRuntimeSeverity Severity = EUBotRuntimeSeverity::Ok)
    {
        FUBotRuntimeItem Item;
        Item.Id = Id;
        Item.Label = FText::FromString(Id);
        Item.Value = Value;
        Item.Severity = Severity;
        return Item;
    }

    FUBotRuntimeProblem MakeProblem(const TCHAR* Id, const TCHAR* Address)
    {
        FUBotRuntimeProblem Problem;
        Problem.Id = Id;
        Problem.Package = TEXT("UBotROS");
        Problem.Severity = EUBotRuntimeSeverity::Info;
        Problem.Code = TEXT("rosBridgeUnreachable");
        Problem.Params.Emplace(TEXT("address"), Address);
        Problem.Message = FText::Format(LOCTEXT("Unreachable", "Could not connect to ubot_ros_bridge at {0}."), FText::FromString(Address));
        return Problem;
    }

    /** Status file below Saved/Automation that is removed again when the scope ends. */
    struct FScopedStatusFile
    {
        FScopedStatusFile()
            : Directory(FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("UBotRuntimeStatusTests"),
                FGuid::NewGuid().ToString(EGuidFormats::Digits))))
            , Path(FPaths::Combine(Directory, TEXT("uBot"), TEXT("status.json")))
        {
        }

        ~FScopedStatusFile()
        {
            IFileManager::Get().DeleteDirectory(*Directory, false, true);
        }

        TSharedPtr<FJsonObject> Read() const
        {
            FString Text;
            return FFileHelper::LoadFileToString(Text, *Path) ? ParseJsonObject(Text) : nullptr;
        }

        const FString Directory;
        const FString Path;
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotRuntimeStatusItemsTest, "UBotCore.RuntimeStatus.ItemsAndProblems", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotRuntimeStatusItemsTest::RunTest(const FString& Parameters)
{
    using namespace UBotRuntimeStatusTests;

    FUBotRuntimeStatus Status;
    int32 Changes = 0;
    Status.OnChanged.AddLambda([&Changes]() { ++Changes; });

    Status.SetItem(MakeItem(TEXT("ros.bridge"), TEXT("connecting"), EUBotRuntimeSeverity::Info));
    Status.SetItem(MakeItem(TEXT("sim.other"), TEXT("on")));
    TestEqual(TEXT("Two items"), Status.GetItems().Num(), 2);
    TestEqual(TEXT("Each new item is a change"), Changes, 2);

    Status.SetItem(MakeItem(TEXT("ros.bridge"), TEXT("connecting"), EUBotRuntimeSeverity::Info));
    TestEqual(TEXT("Setting the same item again is not a change"), Changes, 2);

    Status.SetItem(MakeItem(TEXT("ros.bridge"), TEXT("connected")));
    TestEqual(TEXT("Replacing an item is a change"), Changes, 3);
    TestEqual(TEXT("Replacing keeps the item count"), Status.GetItems().Num(), 2);
    TestEqual(TEXT("Replacing keeps the position"), Status.GetItems()[0].Id, FString(TEXT("ros.bridge")));
    const FUBotRuntimeItem* Bridge = Status.FindItem(TEXT("ros.bridge"));
    TestTrue(TEXT("Item is updated"), Bridge != nullptr && Bridge->Value == TEXT("connected") && Bridge->Severity == EUBotRuntimeSeverity::Ok);
    TestNull(TEXT("Ids are case-sensitive"), Status.FindItem(TEXT("ROS.Bridge")));

    TestTrue(TEXT("Remove an item"), Status.RemoveItem(TEXT("sim.other")));
    TestFalse(TEXT("Remove it again"), Status.RemoveItem(TEXT("sim.other")));
    TestEqual(TEXT("Only a real removal is a change"), Changes, 4);

    Status.ReportProblem(MakeProblem(TEXT("ros.bridge"), TEXT("127.0.0.1:8268")));
    Status.ReportProblem(MakeProblem(TEXT("ros.bridge"), TEXT("127.0.0.1:8268")));
    TestEqual(TEXT("Reporting the same problem twice is one change"), Changes, 5);
    Status.ReportProblem(MakeProblem(TEXT("ros.bridge"), TEXT("10.0.0.2:8268")));
    TestEqual(TEXT("A changed parameter is a change"), Changes, 6);
    TestEqual(TEXT("Still one problem"), Status.GetProblems().Num(), 1);
    const FUBotRuntimeProblem* Problem = Status.FindProblem(TEXT("ros.bridge"));
    TestTrue(TEXT("Problem is updated"), Problem != nullptr && Problem->Params.Num() == 1 && Problem->Params[0].Value == TEXT("10.0.0.2:8268"));

    TestTrue(TEXT("Clear the problem"), Status.ClearProblem(TEXT("ros.bridge")));
    TestFalse(TEXT("Clear it again"), Status.ClearProblem(TEXT("ros.bridge")));
    TestEqual(TEXT("Only a real clear is a change"), Changes, 7);
    TestEqual(TEXT("No problems left"), Status.GetProblems().Num(), 0);

    TestEqual(TEXT("Severity text"), FString(LexToString(EUBotRuntimeSeverity::Warning)), FString(TEXT("warning")));
    TestEqual(TEXT("Session kind text"), FString(LexToString(EUBotSessionKind::PIE)), FString(TEXT("PIE")));
    TestEqual(TEXT("Game session kind text"), FString(LexToString(EUBotSessionKind::Game)), FString(TEXT("Game")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotRuntimeStatusJsonTest, "UBotCore.RuntimeStatus.JsonShape", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotRuntimeStatusJsonTest::RunTest(const FString& Parameters)
{
    using namespace UBotRuntimeStatusTests;

    TestEqual(TEXT("Timestamps are ISO 8601 UTC without fractions"), FUBotRuntimeStatus::FormatTimestamp(FDateTime(2026, 10, 10, 8, 0, 0, 999)),
        FString(TEXT("2026-10-10T08:00:00Z")));

    FUBotStatusDocument Document;
    Document.WrittenAt = FDateTime(2026, 10, 10, 8, 0, 5);
    Document.Session.Kind = EUBotSessionKind::PIE;
    Document.Session.StartedAt = FDateTime(2026, 10, 10, 7, 59, 0);
    Document.Session.EndedAt = FDateTime(2026, 10, 10, 8, 0, 4);
    Document.EngineVersion = TEXT("5.5.4");
    Document.Packages.Add({ TEXT("UBotCore"), TEXT("0.1.0"), true });
    Document.Packages.Add({ TEXT("UBotCombat"), TEXT("0.1.0"), false });
    Document.ExtensionPoints.Add({ TEXT("UBotROS.SensorPublisher"), 7 });

    FUBotRuntimeItem Item = MakeItem(TEXT("ros.bridge"), TEXT("connected"));
    Item.Label = LOCTEXT("BridgeLabel", "ROS bridge");
    Item.ValueText = LOCTEXT("BridgeConnected", "Connected");
    Item.Detail = TEXT("127.0.0.1:8268");
    Document.Runtime.Add(Item);
    Document.Problems.Add(MakeProblem(TEXT("ros.bridge"), TEXT("127.0.0.1:8268")));

    const FString Json = FUBotRuntimeStatus::StatusToJson(Document);
    const TSharedPtr<FJsonObject> Root = ParseJsonObject(Json);
    if (!TestTrue(TEXT("status.json is valid JSON"), Root.IsValid()))
    {
        return false;
    }

    TestEqual(TEXT("Top-level keys, in order"), FString::Join(KeysInOrder(Json, *Root), TEXT(",")),
        FString(TEXT("formatVersion,writtenAt,session,engineVersion,packages,extensionPoints,runtime,problems")));
    TestEqual(TEXT("formatVersion"), static_cast<int32>(Root->GetNumberField(TEXT("formatVersion"))), 1);
    TestEqual(TEXT("writtenAt"), Root->GetStringField(TEXT("writtenAt")), FString(TEXT("2026-10-10T08:00:05Z")));
    TestEqual(TEXT("engineVersion"), Root->GetStringField(TEXT("engineVersion")), FString(TEXT("5.5.4")));

    const TSharedPtr<FJsonObject>* Session = nullptr;
    if (TestTrue(TEXT("session is an object"), Root->TryGetObjectField(TEXT("session"), Session)))
    {
        TestEqual(TEXT("session keys"), (*Session)->Values.Num(), 3);
        TestEqual(TEXT("session.kind"), (*Session)->GetStringField(TEXT("kind")), FString(TEXT("PIE")));
        TestEqual(TEXT("session.startedAt"), (*Session)->GetStringField(TEXT("startedAt")), FString(TEXT("2026-10-10T07:59:00Z")));
        TestEqual(TEXT("session.endedAt"), (*Session)->GetStringField(TEXT("endedAt")), FString(TEXT("2026-10-10T08:00:04Z")));
    }

    const TArray<TSharedPtr<FJsonValue>>* Packages = nullptr;
    if (TestTrue(TEXT("packages is an array"), Root->TryGetArrayField(TEXT("packages"), Packages)) && TestEqual(TEXT("Two packages"), Packages->Num(), 2))
    {
        const TSharedPtr<FJsonObject> First = (*Packages)[0]->AsObject();
        TestEqual(TEXT("package keys"), First->Values.Num(), 3);
        TestEqual(TEXT("package name"), First->GetStringField(TEXT("name")), FString(TEXT("UBotCore")));
        TestEqual(TEXT("package version"), First->GetStringField(TEXT("version")), FString(TEXT("0.1.0")));
        TestTrue(TEXT("package enabled is a boolean"), First->GetBoolField(TEXT("enabled")));
        TestFalse(TEXT("disabled package"), (*Packages)[1]->AsObject()->GetBoolField(TEXT("enabled")));
    }

    const TArray<TSharedPtr<FJsonValue>>* Points = nullptr;
    if (TestTrue(TEXT("extensionPoints is an array"), Root->TryGetArrayField(TEXT("extensionPoints"), Points)) && TestEqual(TEXT("One point"), Points->Num(), 1))
    {
        const TSharedPtr<FJsonObject> Point = (*Points)[0]->AsObject();
        TestEqual(TEXT("extension point keys"), Point->Values.Num(), 2);
        TestEqual(TEXT("extension point id"), Point->GetStringField(TEXT("id")), FString(TEXT("UBotROS.SensorPublisher")));
        TestEqual(TEXT("implementations"), static_cast<int32>(Point->GetNumberField(TEXT("implementations"))), 7);
    }

    const TArray<TSharedPtr<FJsonValue>>* Runtime = nullptr;
    if (TestTrue(TEXT("runtime is an array"), Root->TryGetArrayField(TEXT("runtime"), Runtime)) && TestEqual(TEXT("One runtime item"), Runtime->Num(), 1))
    {
        const TSharedPtr<FJsonObject> Entry = (*Runtime)[0]->AsObject();
        TestEqual(TEXT("runtime keys"), Entry->Values.Num(), 5);
        TestEqual(TEXT("runtime id"), Entry->GetStringField(TEXT("id")), FString(TEXT("ros.bridge")));
        TestEqual(TEXT("runtime label is the source text"), Entry->GetStringField(TEXT("label")), FString(TEXT("ROS bridge")));
        TestEqual(TEXT("runtime value is the machine value"), Entry->GetStringField(TEXT("value")), FString(TEXT("connected")));
        TestEqual(TEXT("runtime detail"), Entry->GetStringField(TEXT("detail")), FString(TEXT("127.0.0.1:8268")));
        TestEqual(TEXT("runtime severity"), Entry->GetStringField(TEXT("severity")), FString(TEXT("ok")));
    }

    const TArray<TSharedPtr<FJsonValue>>* Problems = nullptr;
    if (TestTrue(TEXT("problems is an array"), Root->TryGetArrayField(TEXT("problems"), Problems)) && TestEqual(TEXT("One problem"), Problems->Num(), 1))
    {
        const TSharedPtr<FJsonObject> Entry = (*Problems)[0]->AsObject();
        TestEqual(TEXT("problem keys"), Entry->Values.Num(), 6);
        TestEqual(TEXT("problem id"), Entry->GetStringField(TEXT("id")), FString(TEXT("ros.bridge")));
        TestEqual(TEXT("problem package"), Entry->GetStringField(TEXT("package")), FString(TEXT("UBotROS")));
        TestEqual(TEXT("problem severity"), Entry->GetStringField(TEXT("severity")), FString(TEXT("info")));
        TestEqual(TEXT("problem code"), Entry->GetStringField(TEXT("code")), FString(TEXT("rosBridgeUnreachable")));
        const TSharedPtr<FJsonObject>* Params = nullptr;
        if (TestTrue(TEXT("params is an object"), Entry->TryGetObjectField(TEXT("params"), Params)))
        {
            TestEqual(TEXT("one param"), (*Params)->Values.Num(), 1);
            TestEqual(TEXT("address param"), (*Params)->GetStringField(TEXT("address")), FString(TEXT("127.0.0.1:8268")));
        }
        TestEqual(TEXT("message is the English source text"), Entry->GetStringField(TEXT("message")),
            FString(TEXT("Could not connect to ubot_ros_bridge at 127.0.0.1:8268.")));
    }

    // A session that has not ended yet has a null endedAt; empty arrays stay arrays.
    FUBotStatusDocument Running;
    Running.Session.Kind = EUBotSessionKind::Game;
    Running.Session.StartedAt = FDateTime(2026, 1, 2, 3, 4, 5);
    const TSharedPtr<FJsonObject> RunningRoot = ParseJsonObject(FUBotRuntimeStatus::StatusToJson(Running));
    if (TestTrue(TEXT("Running session JSON is valid"), RunningRoot.IsValid()))
    {
        const TSharedPtr<FJsonObject> RunningSession = RunningRoot->GetObjectField(TEXT("session"));
        TestEqual(TEXT("Game kind"), RunningSession->GetStringField(TEXT("kind")), FString(TEXT("Game")));
        const TSharedPtr<FJsonValue> EndedAt = RunningSession->TryGetField(TEXT("endedAt"));
        TestTrue(TEXT("endedAt is present and null"), EndedAt.IsValid() && EndedAt->IsNull());
        for (const TCHAR* ArrayName : { TEXT("packages"), TEXT("extensionPoints"), TEXT("runtime"), TEXT("problems") })
        {
            const TArray<TSharedPtr<FJsonValue>>* Empty = nullptr;
            TestTrue(FString::Printf(TEXT("%s is an empty array"), ArrayName), RunningRoot->TryGetArrayField(ArrayName, Empty) && Empty->Num() == 0);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotRuntimeStatusSessionsTest, "UBotCore.RuntimeStatus.Sessions", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotRuntimeStatusSessionsTest::RunTest(const FString& Parameters)
{
    using namespace UBotRuntimeStatusTests;

    FScopedStatusFile File;
    FUBotRuntimeStatus Status;
    Status.SetStatusFilePath(File.Path);
    TestEqual(TEXT("Status file path override"), Status.GetStatusFilePath(), File.Path);

    int32 Changes = 0;
    Status.OnChanged.AddLambda([&Changes]() { ++Changes; });

    TestFalse(TEXT("No session at first"), Status.IsSessionActive());
    TestFalse(TEXT("Ending without a session does nothing"), Status.EndSession());
    TestFalse(TEXT("Nothing is written without a session"), FPaths::FileExists(File.Path));
    TestEqual(TEXT("No change without a session"), Changes, 0);

    Status.SetItem(MakeItem(TEXT("ros.bridge"), TEXT("connected")));
    Status.ReportProblem(MakeProblem(TEXT("ros.bridge"), TEXT("127.0.0.1:8268")));
    Changes = 0;

    const FDateTime Before = FDateTime::UtcNow() - FTimespan::FromSeconds(1.0);
    Status.BeginSession(EUBotSessionKind::PIE);
    TestTrue(TEXT("Session is active"), Status.IsSessionActive());
    TestEqual(TEXT("Beginning a session is a change"), Changes, 1);
    if (TestNotNull(TEXT("Active session"), Status.GetActiveSession()))
    {
        TestEqual(TEXT("Active session kind"), Status.GetActiveSession()->Kind, EUBotSessionKind::PIE);
        TestTrue(TEXT("Active session start is recent UTC"), Status.GetActiveSession()->StartedAt >= Before);
        TestFalse(TEXT("Active session has not ended"), Status.GetActiveSession()->EndedAt.IsSet());
    }
    TestFalse(TEXT("Nothing is written when a session begins"), FPaths::FileExists(File.Path));

    TestTrue(TEXT("End the session"), Status.EndSession());
    TestFalse(TEXT("Session is over"), Status.IsSessionActive());
    TestEqual(TEXT("Ending a session is a change"), Changes, 2);
    const FUBotSessionInfo* Last = Status.GetLastSession();
    if (TestNotNull(TEXT("Last session"), Last))
    {
        TestTrue(TEXT("Last session has ended"), Last->EndedAt.IsSet() && Last->EndedAt.GetValue() >= Last->StartedAt);
    }
    TestEqual(TEXT("Items stay after the session"), Status.GetItems().Num(), 1);
    TestEqual(TEXT("Problems stay after the session"), Status.GetProblems().Num(), 1);

    const TSharedPtr<FJsonObject> Written = File.Read();
    if (TestTrue(TEXT("status.json is written when the session ends"), Written.IsValid()))
    {
        TestEqual(TEXT("formatVersion"), static_cast<int32>(Written->GetNumberField(TEXT("formatVersion"))), FUBotRuntimeStatus::StatusFormatVersion);
        TestTrue(TEXT("writtenAt is ISO 8601 UTC"), IsIsoUtcTimestamp(Written->GetStringField(TEXT("writtenAt"))));
        const TSharedPtr<FJsonObject> Session = Written->GetObjectField(TEXT("session"));
        TestEqual(TEXT("Written kind"), Session->GetStringField(TEXT("kind")), FString(TEXT("PIE")));
        TestTrue(TEXT("Written startedAt"), IsIsoUtcTimestamp(Session->GetStringField(TEXT("startedAt"))));
        TestTrue(TEXT("Written endedAt"), IsIsoUtcTimestamp(Session->GetStringField(TEXT("endedAt"))));
        TestTrue(TEXT("Times are in order"), Session->GetStringField(TEXT("startedAt")) <= Session->GetStringField(TEXT("endedAt")));

        const FString EngineVersion = Written->GetStringField(TEXT("engineVersion"));
        TArray<FString> EngineParts;
        EngineVersion.ParseIntoArray(EngineParts, TEXT("."));
        TestEqual(FString::Printf(TEXT("Engine version '%s' is Major.Minor.Patch"), *EngineVersion), EngineParts.Num(), 3);

        bool bHasCore = false;
        for (const TSharedPtr<FJsonValue>& Package : Written->GetArrayField(TEXT("packages")))
        {
            bHasCore |= Package->AsObject()->GetStringField(TEXT("name")) == TEXT("UBotCore");
        }
        TestTrue(TEXT("Packages come from the package registry"), bHasCore);
        TestTrue(TEXT("Extension points are listed"), Written->HasTypedField<EJson::Array>(TEXT("extensionPoints")));
        TestEqual(TEXT("Runtime items are written"), Written->GetArrayField(TEXT("runtime")).Num(), 1);
        TestEqual(TEXT("Problems are written"), Written->GetArrayField(TEXT("problems")).Num(), 1);
    }
    TestFalse(TEXT("No temporary file is left behind"), FPaths::FileExists(File.Path + TEXT(".tmp")));

    // Beginning a session while one runs ends (and writes) the running one first.
    Status.BeginSession(EUBotSessionKind::Editor);
    Status.ClearProblem(TEXT("ros.bridge"));
    Status.BeginSession(EUBotSessionKind::PIE);
    TestTrue(TEXT("The new session runs"), Status.GetActiveSession() && Status.GetActiveSession()->Kind == EUBotSessionKind::PIE);
    TestTrue(TEXT("The replaced session is the last one"), Status.GetLastSession() && Status.GetLastSession()->Kind == EUBotSessionKind::Editor);
    const TSharedPtr<FJsonObject> Replaced = File.Read();
    TestTrue(TEXT("The replaced session was written"), Replaced.IsValid() && Replaced->GetObjectField(TEXT("session"))->GetStringField(TEXT("kind")) == TEXT("Editor"));
    TestTrue(TEXT("It has the problems of its end"), Replaced.IsValid() && Replaced->GetArrayField(TEXT("problems")).Num() == 0);
    Status.EndSession();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBotRuntimeStatusGameSessionTest, "UBotCore.RuntimeStatus.GameSessions", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUBotRuntimeStatusGameSessionTest::RunTest(const FString& Parameters)
{
    using namespace UBotRuntimeStatusTests;

    FScopedStatusFile File;
    FUBotRuntimeStatus Status;
    Status.SetStatusFilePath(File.Path);
    Status.StartGameSessionTracking();
    Status.StartGameSessionTracking();

    struct FScopedGameWorld
    {
        FScopedGameWorld()
            : World(UWorld::CreateWorld(EWorldType::Game, false))
        {
            if (World != nullptr && GEngine != nullptr)
            {
                GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            }
        }

        ~FScopedGameWorld()
        {
            Destroy();
        }

        // What UWorld::BeginPlay broadcasts at its end; a real BeginPlay would also start other
        // packages' world subsystems (the ROS bridge would try to connect).
        void BeginPlay()
        {
            World->OnWorldBeginPlay.Broadcast();
        }

        void Destroy()
        {
            if (World != nullptr)
            {
                if (GEngine != nullptr)
                {
                    GEngine->DestroyWorldContext(World);
                }
                World->DestroyWorld(false);
                World = nullptr;
            }
        }

        UWorld* World = nullptr;
    };

    {
        FScopedGameWorld First;
        FScopedGameWorld Second;
        if (!TestTrue(TEXT("Game worlds exist"), First.World != nullptr && Second.World != nullptr))
        {
            return false;
        }
        TestFalse(TEXT("Creating a world does not begin a session"), Status.IsSessionActive());

        First.BeginPlay();
        TestTrue(TEXT("BeginPlay of a game world begins a session"), Status.GetActiveSession() && Status.GetActiveSession()->Kind == EUBotSessionKind::Game);
        const FDateTime FirstStart = Status.GetActiveSession() ? Status.GetActiveSession()->StartedAt : FDateTime();

        Second.BeginPlay();
        TestTrue(TEXT("Another world does not replace the session"), Status.GetActiveSession() && Status.GetActiveSession()->StartedAt == FirstStart);

        Second.World->BeginTearingDown();
        Second.Destroy();
        TestTrue(TEXT("Tearing down another world keeps the session"), Status.IsSessionActive());

        First.World->BeginTearingDown();
        TestFalse(TEXT("Tearing down the session's world ends the session"), Status.IsSessionActive());
        const TSharedPtr<FJsonObject> Written = File.Read();
        TestTrue(TEXT("The game session was written"), Written.IsValid() && Written->GetObjectField(TEXT("session"))->GetStringField(TEXT("kind")) == TEXT("Game"));
    }

    // A world destroyed without a tear down notification still ends its session.
    {
        FScopedGameWorld World;
        World.BeginPlay();
        TestTrue(TEXT("A new game world begins a new session"), Status.IsSessionActive());
        World.Destroy();
        TestFalse(TEXT("Destroying the world ends the session"), Status.IsSessionActive());
    }

    Status.StopGameSessionTracking();
    {
        FScopedGameWorld Untracked;
        Untracked.BeginPlay();
        TestFalse(TEXT("No sessions once tracking stopped"), Status.IsSessionActive());
    }
    return true;
}

#undef LOCTEXT_NAMESPACE

#endif // WITH_DEV_AUTOMATION_TESTS
