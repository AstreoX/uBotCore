#include "UBotExtensionRegistry.h"

#include "Misc/ScopeLock.h"
#include "UBotCore.h"

FUBotExtensionRegistry& FUBotExtensionRegistry::Get()
{
    // Intentionally leaked: registered extensions and delegate bindings may live in modules that are
    // already unloaded when static destructors run.
    static FUBotExtensionRegistry* Instance = new FUBotExtensionRegistry();
    return *Instance;
}

bool FUBotExtensionRegistry::RegisterExtensionPoint(FName PointName, FName OwnerPackage, const FText& Description)
{
    if (PointName.IsNone())
    {
        UE_LOG(LogUBot, Warning, TEXT("Ignoring an extension point without a name from package '%s'."), *OwnerPackage.ToString());
        return false;
    }

    FName ExistingOwner;
    bool bConflict = false;
    bool bAdded = false;
    {
        FScopeLock ScopeLock(&Lock);
        const int32 PointIndex = FindPointIndexLocked(PointName);
        if (PointIndex == INDEX_NONE)
        {
            FUBotExtensionPointInfo& Point = Points.AddDefaulted_GetRef();
            Point.Name = PointName;
            Point.OwnerPackage = OwnerPackage;
            Point.Description = Description;
            bAdded = true;
        }
        else if (Points[PointIndex].OwnerPackage == OwnerPackage)
        {
            Points[PointIndex].Description = Description;
        }
        else
        {
            ExistingOwner = Points[PointIndex].OwnerPackage;
            bConflict = true;
        }
    }

    if (bConflict)
    {
        UE_LOG(LogUBot, Warning, TEXT("Extension point '%s' is already declared by package '%s'; package '%s' cannot declare it."),
            *PointName.ToString(), *ExistingOwner.ToString(), *OwnerPackage.ToString());
        return false;
    }

    // Extensions registered before the point existed become visible now.
    if (bAdded)
    {
        BroadcastChanged(PointName);
    }
    return true;
}

bool FUBotExtensionRegistry::RegisterExtension(FName PointName, FName OwnerPackage, const TSharedRef<IUBotExtension>& Extension)
{
    // Called before taking the lock: it is foreign code.
    const FName ExtensionName = Extension->GetExtensionName();
    if (PointName.IsNone() || ExtensionName.IsNone())
    {
        UE_LOG(LogUBot, Warning, TEXT("Ignoring extension '%s' from package '%s': the extension point and the extension need a name."),
            *ExtensionName.ToString(), *OwnerPackage.ToString());
        return false;
    }

    bool bDuplicate = false;
    {
        FScopeLock ScopeLock(&Lock);
        if (FindExtensionIndexLocked(PointName, ExtensionName) != INDEX_NONE)
        {
            bDuplicate = true;
        }
        else
        {
            FUBotExtensionRecord& Record = Extensions.AddDefaulted_GetRef();
            Record.ExtensionPoint = PointName;
            Record.ExtensionName = ExtensionName;
            Record.OwnerPackage = OwnerPackage;
            Record.Extension = Extension;
        }
    }

    if (bDuplicate)
    {
        UE_LOG(LogUBot, Warning, TEXT("Extension '%s' is already registered at extension point '%s'; the registration from package '%s' was ignored."),
            *ExtensionName.ToString(), *PointName.ToString(), *OwnerPackage.ToString());
        return false;
    }

    BroadcastChanged(PointName);
    return true;
}

bool FUBotExtensionRegistry::UnregisterExtension(FName PointName, FName ExtensionName)
{
    // Released after the lock so an extension's destructor may use the registry.
    FUBotExtensionRecord Removed;
    {
        FScopeLock ScopeLock(&Lock);
        const int32 ExtensionIndex = FindExtensionIndexLocked(PointName, ExtensionName);
        if (ExtensionIndex == INDEX_NONE)
        {
            return false;
        }
        Removed = MoveTemp(Extensions[ExtensionIndex]);
        Extensions.RemoveAt(ExtensionIndex);
    }

    BroadcastChanged(PointName);
    return true;
}

void FUBotExtensionRegistry::UnregisterAllFromPackage(FName OwnerPackage)
{
    TArray<FName> ChangedPoints;
    // Released after the lock so an extension's destructor may use the registry.
    TArray<FUBotExtensionRecord> Removed;
    {
        FScopeLock ScopeLock(&Lock);
        for (const FUBotExtensionPointInfo& Point : Points)
        {
            if (Point.OwnerPackage == OwnerPackage)
            {
                ChangedPoints.AddUnique(Point.Name);
            }
        }
        Points.RemoveAll([OwnerPackage](const FUBotExtensionPointInfo& Point)
        {
            return Point.OwnerPackage == OwnerPackage;
        });

        for (const FUBotExtensionRecord& Record : Extensions)
        {
            if (Record.OwnerPackage == OwnerPackage)
            {
                ChangedPoints.AddUnique(Record.ExtensionPoint);
                Removed.Add(Record);
            }
        }
        Extensions.RemoveAll([OwnerPackage](const FUBotExtensionRecord& Record)
        {
            return Record.OwnerPackage == OwnerPackage;
        });
    }

    for (const FName PointName : ChangedPoints)
    {
        BroadcastChanged(PointName);
    }
}

TArray<TSharedRef<IUBotExtension>> FUBotExtensionRegistry::GetExtensions(FName PointName) const
{
    TArray<TSharedRef<IUBotExtension>> Result;
    FScopeLock ScopeLock(&Lock);
    if (FindPointIndexLocked(PointName) == INDEX_NONE)
    {
        return Result;
    }
    for (const FUBotExtensionRecord& Record : Extensions)
    {
        if (Record.ExtensionPoint == PointName && Record.Extension.IsValid())
        {
            Result.Add(Record.Extension.ToSharedRef());
        }
    }
    return Result;
}

TArray<FUBotExtensionPointInfo> FUBotExtensionRegistry::GetExtensionPoints() const
{
    FScopeLock ScopeLock(&Lock);
    return Points;
}

TArray<FUBotExtensionRecord> FUBotExtensionRegistry::GetAllExtensions() const
{
    FScopeLock ScopeLock(&Lock);
    return Extensions;
}

bool FUBotExtensionRegistry::IsExtensionPointDeclared(FName PointName) const
{
    FScopeLock ScopeLock(&Lock);
    return FindPointIndexLocked(PointName) != INDEX_NONE;
}

void FUBotExtensionRegistry::BroadcastChanged(FName PointName)
{
    // Multicast delegates are not thread-safe: concurrent Broadcast calls trip the engine's access
    // detector even when nobody is binding.
    FScopeLock ScopeLock(&BroadcastLock);
    OnExtensionsChanged.Broadcast(PointName);
}

int32 FUBotExtensionRegistry::FindPointIndexLocked(FName PointName) const
{
    return Points.IndexOfByPredicate([PointName](const FUBotExtensionPointInfo& Point)
    {
        return Point.Name == PointName;
    });
}

int32 FUBotExtensionRegistry::FindExtensionIndexLocked(FName PointName, FName ExtensionName) const
{
    return Extensions.IndexOfByPredicate([PointName, ExtensionName](const FUBotExtensionRecord& Record)
    {
        return Record.ExtensionPoint == PointName && Record.ExtensionName == ExtensionName;
    });
}
