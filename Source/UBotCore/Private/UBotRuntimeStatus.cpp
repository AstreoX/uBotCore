#include "UBotRuntimeStatus.h"

#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonWriter.h"
#include "UBotCore.h"
#include "UBotExtensionRegistry.h"
#include "UBotPackageRegistry.h"

namespace UBot::RuntimeStatusPrivate
{
    bool TextEquals(const FText& A, const FText& B)
    {
        return A.ToString().Equals(B.ToString(), ESearchCase::CaseSensitive);
    }

    bool IsSameItem(const FUBotRuntimeItem& A, const FUBotRuntimeItem& B)
    {
        return A.Id.Equals(B.Id, ESearchCase::CaseSensitive)
            && TextEquals(A.Label, B.Label)
            && A.Value.Equals(B.Value, ESearchCase::CaseSensitive)
            && TextEquals(A.ValueText, B.ValueText)
            && A.Detail.Equals(B.Detail, ESearchCase::CaseSensitive)
            && A.Severity == B.Severity;
    }

    bool IsSameProblem(const FUBotRuntimeProblem& A, const FUBotRuntimeProblem& B)
    {
        if (A.Params.Num() != B.Params.Num())
        {
            return false;
        }
        for (int32 Index = 0; Index < A.Params.Num(); ++Index)
        {
            if (!A.Params[Index].Key.Equals(B.Params[Index].Key, ESearchCase::CaseSensitive)
                || !A.Params[Index].Value.Equals(B.Params[Index].Value, ESearchCase::CaseSensitive))
            {
                return false;
            }
        }
        return A.Id.Equals(B.Id, ESearchCase::CaseSensitive)
            && A.Package.Equals(B.Package, ESearchCase::CaseSensitive)
            && A.Severity == B.Severity
            && A.Code.Equals(B.Code, ESearchCase::CaseSensitive)
            && TextEquals(A.Message, B.Message);
    }

    template <typename T>
    int32 FindById(const TArray<T>& Entries, const FString& Id)
    {
        return Entries.IndexOfByPredicate([&Id](const T& Entry)
        {
            return Entry.Id.Equals(Id, ESearchCase::CaseSensitive);
        });
    }

    FString GetEngineVersionText()
    {
        const FEngineVersion& Version = FEngineVersion::Current();
        return FString::Printf(TEXT("%u.%u.%u"), Version.GetMajor(), Version.GetMinor(), Version.GetPatch());
    }
}

const TCHAR* LexToString(EUBotRuntimeSeverity Severity)
{
    switch (Severity)
    {
    case EUBotRuntimeSeverity::Ok:
        return TEXT("ok");
    case EUBotRuntimeSeverity::Warning:
        return TEXT("warning");
    case EUBotRuntimeSeverity::Error:
        return TEXT("error");
    case EUBotRuntimeSeverity::Info:
    default:
        return TEXT("info");
    }
}

const TCHAR* LexToString(EUBotSessionKind Kind)
{
    switch (Kind)
    {
    case EUBotSessionKind::Editor:
        return TEXT("Editor");
    case EUBotSessionKind::Game:
        return TEXT("Game");
    case EUBotSessionKind::PIE:
    default:
        return TEXT("PIE");
    }
}

FUBotRuntimeStatus::~FUBotRuntimeStatus()
{
    StopGameSessionTracking();
}

FUBotRuntimeStatus& FUBotRuntimeStatus::Get()
{
    // Intentionally leaked like the other uBot registries: delegate bindings may belong to modules
    // that are already unloaded when static destructors run.
    static FUBotRuntimeStatus* Instance = new FUBotRuntimeStatus();
    return *Instance;
}

void FUBotRuntimeStatus::SetItem(const FUBotRuntimeItem& Item)
{
    using namespace UBot::RuntimeStatusPrivate;

    check(IsInGameThread());
    const int32 Index = FindById(Items, Item.Id);
    if (Index == INDEX_NONE)
    {
        Items.Add(Item);
    }
    else if (IsSameItem(Items[Index], Item))
    {
        return;
    }
    else
    {
        Items[Index] = Item;
    }
    OnChanged.Broadcast();
}

bool FUBotRuntimeStatus::RemoveItem(const FString& Id)
{
    check(IsInGameThread());
    const int32 Index = UBot::RuntimeStatusPrivate::FindById(Items, Id);
    if (Index == INDEX_NONE)
    {
        return false;
    }
    Items.RemoveAt(Index);
    OnChanged.Broadcast();
    return true;
}

const TArray<FUBotRuntimeItem>& FUBotRuntimeStatus::GetItems() const
{
    return Items;
}

const FUBotRuntimeItem* FUBotRuntimeStatus::FindItem(const FString& Id) const
{
    const int32 Index = UBot::RuntimeStatusPrivate::FindById(Items, Id);
    return Index != INDEX_NONE ? &Items[Index] : nullptr;
}

void FUBotRuntimeStatus::ReportProblem(const FUBotRuntimeProblem& Problem)
{
    using namespace UBot::RuntimeStatusPrivate;

    check(IsInGameThread());
    const int32 Index = FindById(Problems, Problem.Id);
    if (Index == INDEX_NONE)
    {
        Problems.Add(Problem);
    }
    else if (IsSameProblem(Problems[Index], Problem))
    {
        return;
    }
    else
    {
        Problems[Index] = Problem;
    }
    OnChanged.Broadcast();
}

bool FUBotRuntimeStatus::ClearProblem(const FString& Id)
{
    check(IsInGameThread());
    const int32 Index = UBot::RuntimeStatusPrivate::FindById(Problems, Id);
    if (Index == INDEX_NONE)
    {
        return false;
    }
    Problems.RemoveAt(Index);
    OnChanged.Broadcast();
    return true;
}

const TArray<FUBotRuntimeProblem>& FUBotRuntimeStatus::GetProblems() const
{
    return Problems;
}

const FUBotRuntimeProblem* FUBotRuntimeStatus::FindProblem(const FString& Id) const
{
    const int32 Index = UBot::RuntimeStatusPrivate::FindById(Problems, Id);
    return Index != INDEX_NONE ? &Problems[Index] : nullptr;
}

void FUBotRuntimeStatus::BeginSession(EUBotSessionKind Kind)
{
    check(IsInGameThread());
    if (ActiveSession.IsSet())
    {
        EndSession();
    }

    FUBotSessionInfo Session;
    Session.Kind = Kind;
    Session.StartedAt = FDateTime::UtcNow();
    ActiveSession = Session;
    UE_LOG(LogUBot, Log, TEXT("uBot: %s session started."), LexToString(Kind));
    OnChanged.Broadcast();
}

bool FUBotRuntimeStatus::EndSession()
{
    check(IsInGameThread());
    if (!ActiveSession.IsSet())
    {
        return false;
    }

    FUBotSessionInfo Session = ActiveSession.GetValue();
    Session.EndedAt = FDateTime::UtcNow();
    ActiveSession.Reset();
    SessionWorld.Reset();

    WriteStatusFile(Session);
    LastSession = MoveTemp(Session);
    OnChanged.Broadcast();
    return true;
}

bool FUBotRuntimeStatus::IsSessionActive() const
{
    return ActiveSession.IsSet();
}

const FUBotSessionInfo* FUBotRuntimeStatus::GetActiveSession() const
{
    return ActiveSession.GetPtrOrNull();
}

const FUBotSessionInfo* FUBotRuntimeStatus::GetLastSession() const
{
    return LastSession.GetPtrOrNull();
}

void FUBotRuntimeStatus::StartGameSessionTracking()
{
    if (PostWorldInitializationHandle.IsValid())
    {
        return;
    }
    PostWorldInitializationHandle = FWorldDelegates::OnPostWorldInitialization.AddLambda(
        [this](UWorld* World, const UWorld::InitializationValues)
        {
            HandlePostWorldInitialization(World);
        });
    WorldBeginTearDownHandle = FWorldDelegates::OnWorldBeginTearDown.AddRaw(this, &FUBotRuntimeStatus::HandleWorldEnding);
    // Worlds destroyed directly (DestroyWorld) skip the tear down notification.
    WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddLambda([this](UWorld* World, bool /*bSessionEnded*/, bool /*bCleanupResources*/)
    {
        HandleWorldEnding(World);
    });
}

void FUBotRuntimeStatus::StopGameSessionTracking()
{
    FWorldDelegates::OnPostWorldInitialization.Remove(PostWorldInitializationHandle);
    FWorldDelegates::OnWorldBeginTearDown.Remove(WorldBeginTearDownHandle);
    FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);
    PostWorldInitializationHandle.Reset();
    WorldBeginTearDownHandle.Reset();
    WorldCleanupHandle.Reset();

    for (const FTrackedWorld& Tracked : TrackedWorlds)
    {
        if (UWorld* World = Tracked.World.Get())
        {
            World->OnWorldBeginPlay.Remove(Tracked.BeginPlayHandle);
        }
    }
    TrackedWorlds.Reset();
}

void FUBotRuntimeStatus::HandlePostWorldInitialization(UWorld* World)
{
    if (World == nullptr || !World->IsGameWorld())
    {
        return;
    }

    TrackedWorlds.RemoveAll([](const FTrackedWorld& Tracked)
    {
        return !Tracked.World.IsValid();
    });

    FTrackedWorld& Tracked = TrackedWorlds.AddDefaulted_GetRef();
    Tracked.World = World;
    Tracked.BeginPlayHandle = World->OnWorldBeginPlay.AddRaw(this, &FUBotRuntimeStatus::HandleWorldBeginPlay, TWeakObjectPtr<UWorld>(World));
}

void FUBotRuntimeStatus::HandleWorldBeginPlay(TWeakObjectPtr<UWorld> World)
{
    // The first game world to begin play owns the session until it is torn down.
    if (!World.IsValid() || ActiveSession.IsSet())
    {
        return;
    }
    BeginSession(EUBotSessionKind::Game);
    SessionWorld = World;
}

void FUBotRuntimeStatus::HandleWorldEnding(UWorld* World)
{
    TrackedWorlds.RemoveAll([World](const FTrackedWorld& Tracked)
    {
        return !Tracked.World.IsValid() || Tracked.World.Get() == World;
    });

    if (World != nullptr && SessionWorld.Get() == World && ActiveSession.IsSet() && ActiveSession->Kind == EUBotSessionKind::Game)
    {
        EndSession();
    }
}

FString FUBotRuntimeStatus::GetStatusFilePath() const
{
    if (!StatusFilePathOverride.IsEmpty())
    {
        return StatusFilePathOverride;
    }
    return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("uBot"), TEXT("status.json")));
}

void FUBotRuntimeStatus::SetStatusFilePath(const FString& FilePath)
{
    StatusFilePathOverride = FilePath;
}

FUBotStatusDocument FUBotRuntimeStatus::CaptureStatus(const FUBotSessionInfo& Session) const
{
    FUBotStatusDocument Document;
    Document.WrittenAt = FDateTime::UtcNow();
    Document.Session = Session;
    Document.EngineVersion = UBot::RuntimeStatusPrivate::GetEngineVersionText();

    for (const FUBotPackageInfo& Package : FUBotPackageRegistry::Get().GetInstalledPackages())
    {
        FUBotStatusDocument::FPackage& Entry = Document.Packages.AddDefaulted_GetRef();
        Entry.Name = Package.Name;
        Entry.Version = Package.Version;
        Entry.bEnabled = Package.bEnabled;
    }

    const FUBotExtensionRegistry& Extensions = FUBotExtensionRegistry::Get();
    for (const FUBotExtensionPointInfo& Point : Extensions.GetExtensionPoints())
    {
        FUBotStatusDocument::FExtensionPoint& Entry = Document.ExtensionPoints.AddDefaulted_GetRef();
        Entry.Id = Point.Name.ToString();
        Entry.Implementations = Extensions.GetExtensions(Point.Name).Num();
    }

    Document.Runtime = Items;
    Document.Problems = Problems;
    return Document;
}

FString FUBotRuntimeStatus::StatusToJson(const FUBotStatusDocument& Document)
{
    FString Json;
    const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Json);

    Writer->WriteObjectStart();
    Writer->WriteValue(TEXT("formatVersion"), StatusFormatVersion);
    Writer->WriteValue(TEXT("writtenAt"), FormatTimestamp(Document.WrittenAt));

    Writer->WriteObjectStart(TEXT("session"));
    Writer->WriteValue(TEXT("kind"), FString(LexToString(Document.Session.Kind)));
    Writer->WriteValue(TEXT("startedAt"), FormatTimestamp(Document.Session.StartedAt));
    if (Document.Session.EndedAt.IsSet())
    {
        Writer->WriteValue(TEXT("endedAt"), FormatTimestamp(Document.Session.EndedAt.GetValue()));
    }
    else
    {
        Writer->WriteNull(TEXT("endedAt"));
    }
    Writer->WriteObjectEnd();

    Writer->WriteValue(TEXT("engineVersion"), Document.EngineVersion);

    Writer->WriteArrayStart(TEXT("packages"));
    for (const FUBotStatusDocument::FPackage& Package : Document.Packages)
    {
        Writer->WriteObjectStart();
        Writer->WriteValue(TEXT("name"), Package.Name);
        Writer->WriteValue(TEXT("version"), Package.Version);
        Writer->WriteValue(TEXT("enabled"), Package.bEnabled);
        Writer->WriteObjectEnd();
    }
    Writer->WriteArrayEnd();

    Writer->WriteArrayStart(TEXT("extensionPoints"));
    for (const FUBotStatusDocument::FExtensionPoint& Point : Document.ExtensionPoints)
    {
        Writer->WriteObjectStart();
        Writer->WriteValue(TEXT("id"), Point.Id);
        Writer->WriteValue(TEXT("implementations"), Point.Implementations);
        Writer->WriteObjectEnd();
    }
    Writer->WriteArrayEnd();

    Writer->WriteArrayStart(TEXT("runtime"));
    for (const FUBotRuntimeItem& Item : Document.Runtime)
    {
        Writer->WriteObjectStart();
        Writer->WriteValue(TEXT("id"), Item.Id);
        Writer->WriteValue(TEXT("label"), Item.Label.BuildSourceString());
        Writer->WriteValue(TEXT("value"), Item.Value);
        Writer->WriteValue(TEXT("detail"), Item.Detail);
        Writer->WriteValue(TEXT("severity"), FString(LexToString(Item.Severity)));
        Writer->WriteObjectEnd();
    }
    Writer->WriteArrayEnd();

    Writer->WriteArrayStart(TEXT("problems"));
    for (const FUBotRuntimeProblem& Problem : Document.Problems)
    {
        Writer->WriteObjectStart();
        Writer->WriteValue(TEXT("id"), Problem.Id);
        Writer->WriteValue(TEXT("package"), Problem.Package);
        Writer->WriteValue(TEXT("severity"), FString(LexToString(Problem.Severity)));
        Writer->WriteValue(TEXT("code"), Problem.Code);
        Writer->WriteObjectStart(TEXT("params"));
        for (const TPair<FString, FString>& Param : Problem.Params)
        {
            Writer->WriteValue(Param.Key, Param.Value);
        }
        Writer->WriteObjectEnd();
        Writer->WriteValue(TEXT("message"), Problem.Message.BuildSourceString());
        Writer->WriteObjectEnd();
    }
    Writer->WriteArrayEnd();

    Writer->WriteObjectEnd();
    Writer->Close();
    return Json;
}

FString FUBotRuntimeStatus::FormatTimestamp(const FDateTime& UtcTime)
{
    return UtcTime.ToString(TEXT("%Y-%m-%dT%H:%M:%SZ"));
}

bool FUBotRuntimeStatus::WriteStatusFile(const FUBotSessionInfo& Session) const
{
    const FString FilePath = GetStatusFilePath();
    const FString TempPath = FilePath + TEXT(".tmp");
    const FString Json = StatusToJson(CaptureStatus(Session));

    // Written next to the target and moved over it, so a reader never sees a partial file.
    IFileManager& FileManager = IFileManager::Get();
    if (!FFileHelper::SaveStringToFile(Json, *TempPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
        || !FileManager.Move(*FilePath, *TempPath, /*bReplace*/ true, /*bEvenIfReadOnly*/ true))
    {
        FileManager.Delete(*TempPath, /*bRequireExists*/ false, /*bEvenReadOnly*/ true, /*bQuiet*/ true);
        UE_LOG(LogUBot, Warning, TEXT("uBot: could not write the session status to %s."), *FilePath);
        return false;
    }

    UE_LOG(LogUBot, Log, TEXT("uBot: %s session status written to %s."), LexToString(Session.Kind), *FilePath);
    return true;
}
