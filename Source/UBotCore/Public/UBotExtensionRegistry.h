#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include <type_traits>

/**
 * Base of every object registered at an extension point. The package that declares the point
 * documents the concrete interface (derived from IUBotExtension) that extensions must implement.
 */
class UBOTCORE_API IUBotExtension
{
public:
    virtual ~IUBotExtension() = default;

    /** Unique within its extension point. */
    virtual FName GetExtensionName() const = 0;
};

struct UBOTCORE_API FUBotExtensionPointInfo
{
    FName Name;
    FName OwnerPackage;
    FText Description;
};

struct UBOTCORE_API FUBotExtensionRecord
{
    FName ExtensionPoint;
    FName ExtensionName;
    FName OwnerPackage;
    TSharedPtr<IUBotExtension> Extension;
};

/**
 * Runtime plug-in points that let one package extend another without a hard module dependency
 * (e.g. UBotROS declares "UBotROS.SensorPublisher" and a sensor package registers a publisher
 * factory for its sensor class).
 *
 * Extensions may be registered before their point is declared (module load order); they are kept
 * and returned once the point exists. Packages must call UnregisterAllFromPackage in
 * ShutdownModule so no extension outlives the code that implements it.
 *
 * Registration, removal and queries are thread-safe. The change notification is broadcast on the
 * calling thread after the internal data lock is released, so handlers may call back into the
 * registry. Broadcasts from different threads are serialized (the delegate itself is not
 * thread-safe), so a handler must not block on work that another thread can only finish by changing
 * the registry. Add and remove handlers with AddExtensionsChangedHandler and
 * RemoveExtensionsChangedHandler: they take the lock that serializes the broadcasts, so they are safe
 * while another thread changes the registry, and when a removal returns no thread is running the
 * handler and none will call it again.
 */
class UBOTCORE_API FUBotExtensionRegistry
{
public:
    /** Standalone instances are only useful for tests; packages use Get(). */
    FUBotExtensionRegistry() = default;
    FUBotExtensionRegistry(const FUBotExtensionRegistry&) = delete;
    FUBotExtensionRegistry& operator=(const FUBotExtensionRegistry&) = delete;

    static FUBotExtensionRegistry& Get();

    /**
     * Declares PointName. Returns false if PointName is None or already declared by another owner;
     * declaring it again from the same owner updates the description and returns true.
     */
    bool RegisterExtensionPoint(FName PointName, FName OwnerPackage, const FText& Description);

    /**
     * Adds Extension under its GetExtensionName(). Returns false if PointName or the extension name
     * is None, or on a duplicate (point, extension name).
     */
    bool RegisterExtension(FName PointName, FName OwnerPackage, const TSharedRef<IUBotExtension>& Extension);

    /** Returns false if no such extension was registered. */
    bool UnregisterExtension(FName PointName, FName ExtensionName);

    /**
     * Removes the points and the extensions owned by OwnerPackage. Extensions other packages
     * registered at a removed point are kept and reappear if the point is declared again.
     */
    void UnregisterAllFromPackage(FName OwnerPackage);

    /** Extensions at PointName in registration order; empty while the point is not declared. */
    TArray<TSharedRef<IUBotExtension>> GetExtensions(FName PointName) const;

    /** GetExtensions with a StaticCastSharedRef to T; the point owner documents T. */
    template <typename T>
    TArray<TSharedRef<T>> GetExtensionsAs(FName PointName) const
    {
        static_assert(std::is_base_of_v<IUBotExtension, T>, "T must derive from IUBotExtension");

        TArray<TSharedRef<T>> Result;
        for (const TSharedRef<IUBotExtension>& Extension : GetExtensions(PointName))
        {
            Result.Add(StaticCastSharedRef<T>(Extension));
        }
        return Result;
    }

    /** Declared points in declaration order. */
    TArray<FUBotExtensionPointInfo> GetExtensionPoints() const;

    /** Every registered extension in registration order, including those whose point is not declared. */
    TArray<FUBotExtensionRecord> GetAllExtensions() const;

    bool IsExtensionPointDeclared(FName PointName) const;

    DECLARE_MULTICAST_DELEGATE_OneParam(FOnExtensionsChanged, FName /*PointName*/);

    /**
     * Calls Handler, on the thread that changed the registry, with the affected point after a point or an
     * extension is added or removed. Safe to call from any thread, also while another thread changes the
     * registry; waits for a broadcast in progress. Do not call it, or RemoveExtensionsChangedHandler, from
     * within a handler of this registry. Returns the handle for RemoveExtensionsChangedHandler.
     */
    FDelegateHandle AddExtensionsChangedHandler(FOnExtensionsChanged::FDelegate Handler);

    /**
     * Removes a handler added by AddExtensionsChangedHandler. Waits for a broadcast in progress on another
     * thread, so when it returns the handler is not running and is never called again. Does nothing for an
     * invalid or unknown handle.
     */
    void RemoveExtensionsChangedHandler(FDelegateHandle Handle);

    /**
     * Broadcast with the affected point after a point or an extension is added or removed. Public for
     * compatibility; binding and unbinding it directly is not synchronized with a broadcast on another
     * thread. Use AddExtensionsChangedHandler and RemoveExtensionsChangedHandler instead.
     */
    FOnExtensionsChanged OnExtensionsChanged;

private:
    int32 FindPointIndexLocked(FName PointName) const;
    int32 FindExtensionIndexLocked(FName PointName, FName ExtensionName) const;
    // Never call with Lock held.
    void BroadcastChanged(FName PointName);

    mutable FCriticalSection Lock;
    // Serializes broadcasts and changes to the handler list (OnExtensionsChanged is not thread-safe).
    // Recursive: a handler may register something, which broadcasts again on the same thread.
    FCriticalSection BroadcastLock;
    TArray<FUBotExtensionPointInfo> Points;
    TArray<FUBotExtensionRecord> Extensions;
};
