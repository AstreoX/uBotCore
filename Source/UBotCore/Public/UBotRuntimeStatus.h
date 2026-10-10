#pragma once

#include "CoreMinimal.h"
#include "Misc/DateTime.h"
#include "Misc/Optional.h"
#include "UObject/WeakObjectPtrTemplates.h"

class UWorld;

/** Severity of a runtime status item or problem. Items may be Ok; problems are Info, Warning or Error. */
enum class EUBotRuntimeSeverity : uint8
{
    Ok,
    Info,
    Warning,
    Error
};

/** What a session is: a Play In Editor session, an editor session or a game (non-editor) run. */
enum class EUBotSessionKind : uint8
{
    PIE,
    Editor,
    Game
};

/** "ok", "info", "warning" or "error", as written to status.json. */
UBOTCORE_API const TCHAR* LexToString(EUBotRuntimeSeverity Severity);

/** "PIE", "Editor" or "Game", as written to status.json. */
UBOTCORE_API const TCHAR* LexToString(EUBotSessionKind Kind);

/** One status line a package publishes while it runs, e.g. the state of the ROS bridge connection. */
struct UBOTCORE_API FUBotRuntimeItem
{
    /** Stable id, unique among the items, e.g. "ros.bridge". */
    FString Id;

    /** Display name, localized. status.json gets its source (English) text. */
    FText Label;

    /** Machine-readable state written to status.json, e.g. "connected". */
    FString Value;

    /** Display form of Value; when empty, Value is shown as it is. */
    FText ValueText;

    /** Extra information such as an address; may be empty. */
    FString Detail;

    EUBotRuntimeSeverity Severity = EUBotRuntimeSeverity::Info;
};

/** A problem a package reports while it runs, e.g. an unreachable ROS bridge. */
struct UBOTCORE_API FUBotRuntimeProblem
{
    /** Stable id, unique among the problems, e.g. "ros.bridge". */
    FString Id;

    /** Plugin name of the reporting package, e.g. "UBotROS". */
    FString Package;

    EUBotRuntimeSeverity Severity = EUBotRuntimeSeverity::Warning;

    /** Message code; uBot Manager shows its own translation of "problem.<Code>" when it has one. */
    FString Code;

    /** Message parameters in order, e.g. { "address", "127.0.0.1:8268" }. */
    TArray<TPair<FString, FString>> Params;

    /** Display message, localized. status.json gets its source (English) text. */
    FText Message;
};

struct UBOTCORE_API FUBotSessionInfo
{
    EUBotSessionKind Kind = EUBotSessionKind::PIE;

    /** UTC. */
    FDateTime StartedAt;

    /** UTC; unset while the session runs. */
    TOptional<FDateTime> EndedAt;
};

/** Everything Saved/uBot/status.json contains. */
struct UBOTCORE_API FUBotStatusDocument
{
    struct FPackage
    {
        FString Name;
        FString Version;
        bool bEnabled = false;
    };

    struct FExtensionPoint
    {
        FString Id;
        int32 Implementations = 0;
    };

    /** UTC. */
    FDateTime WrittenAt;
    FUBotSessionInfo Session;
    /** "Major.Minor.Patch" of the running engine. */
    FString EngineVersion;
    TArray<FPackage> Packages;
    TArray<FExtensionPoint> ExtensionPoints;
    TArray<FUBotRuntimeItem> Runtime;
    TArray<FUBotRuntimeProblem> Problems;
};

/**
 * Runtime status of the uBot packages: status items and problems that packages publish while they
 * run, and the current session. When a session ends (and when UBotCore shuts down during a
 * session) the status is written to <Project>/Saved/uBot/status.json, which uBot Manager reads.
 *
 * Sessions: in the editor, UBotCoreEditor begins one when a PIE session has started and ends it with
 * the PIE session. In a game (GIsEditor false) UBotCore tracks them itself: a session runs from the
 * BeginPlay of the first game world until that world begins tearing down (or is cleaned up).
 *
 * Items and problems are owned by the package that publishes them, which also removes them; they
 * are not cleared when a session begins or ends. All members are game thread only.
 */
class UBOTCORE_API FUBotRuntimeStatus
{
public:
    static constexpr int32 StatusFormatVersion = 1;

    /** Standalone instances are only useful for tests; packages use Get(). */
    FUBotRuntimeStatus() = default;
    ~FUBotRuntimeStatus();
    FUBotRuntimeStatus(const FUBotRuntimeStatus&) = delete;
    FUBotRuntimeStatus& operator=(const FUBotRuntimeStatus&) = delete;

    static FUBotRuntimeStatus& Get();

    /** Adds the item or replaces the one with the same Id. */
    void SetItem(const FUBotRuntimeItem& Item);

    /** Returns false if no item has that Id. */
    bool RemoveItem(const FString& Id);

    /** In the order the items were first set. */
    const TArray<FUBotRuntimeItem>& GetItems() const;
    const FUBotRuntimeItem* FindItem(const FString& Id) const;

    /** Adds the problem or replaces the one with the same Id. */
    void ReportProblem(const FUBotRuntimeProblem& Problem);

    /** Returns false if no problem has that Id. */
    bool ClearProblem(const FString& Id);

    /** In the order the problems were first reported. */
    const TArray<FUBotRuntimeProblem>& GetProblems() const;
    const FUBotRuntimeProblem* FindProblem(const FString& Id) const;

    /** Starts a session; a session that is still running is ended (and written) first. */
    void BeginSession(EUBotSessionKind Kind);

    /** Ends the running session and writes the status file. Returns false when no session runs. */
    bool EndSession();

    bool IsSessionActive() const;

    /** The running session, or nullptr. */
    const FUBotSessionInfo* GetActiveSession() const;

    /** The session that ended last, or nullptr. */
    const FUBotSessionInfo* GetLastSession() const;

    /** Begins and ends Game sessions with the game worlds (see the class comment). Idempotent. */
    void StartGameSessionTracking();
    void StopGameSessionTracking();

    /** <Project>/Saved/uBot/status.json unless overridden. */
    FString GetStatusFilePath() const;
    void SetStatusFilePath(const FString& FilePath);

    /** The current status for Session: packages from FUBotPackageRegistry, extension points from FUBotExtensionRegistry. */
    FUBotStatusDocument CaptureStatus(const FUBotSessionInfo& Session) const;

    /** status.json text (format version 1, times as ISO 8601 UTC). */
    static FString StatusToJson(const FUBotStatusDocument& Document);

    /** "2026-10-10T08:00:00Z". */
    static FString FormatTimestamp(const FDateTime& UtcTime);

    /** Broadcast after items, problems or the session changed. */
    FSimpleMulticastDelegate OnChanged;

private:
    bool WriteStatusFile(const FUBotSessionInfo& Session) const;
    void HandlePostWorldInitialization(UWorld* World);
    void HandleWorldBeginPlay(TWeakObjectPtr<UWorld> World);
    void HandleWorldEnding(UWorld* World);

    TArray<FUBotRuntimeItem> Items;
    TArray<FUBotRuntimeProblem> Problems;
    TOptional<FUBotSessionInfo> ActiveSession;
    TOptional<FUBotSessionInfo> LastSession;
    FString StatusFilePathOverride;

    struct FTrackedWorld
    {
        TWeakObjectPtr<UWorld> World;
        FDelegateHandle BeginPlayHandle;
    };
    TArray<FTrackedWorld> TrackedWorlds;
    TWeakObjectPtr<UWorld> SessionWorld;
    FDelegateHandle PostWorldInitializationHandle;
    FDelegateHandle WorldBeginTearDownHandle;
    FDelegateHandle WorldCleanupHandle;
};
