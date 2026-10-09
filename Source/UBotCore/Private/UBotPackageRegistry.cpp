#include "UBotPackageRegistry.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UBotCore.h"
#include "UBotExtensionRegistry.h"
#include "UBotSemVer.h"

// Named rather than anonymous so unity builds cannot merge it with other files' helpers.
namespace UBot::PackageRegistryPrivate
{
    bool IsInstalledEntry(const FUBotPackageInfo& Package)
    {
        return Package.bInstalled || Package.State != EUBotPackageState::NotInstalled;
    }

    bool IsEnabledEntry(const FUBotPackageInfo& Package)
    {
        return Package.bEnabled || Package.State == EUBotPackageState::Enabled;
    }

    // FString keys hash and compare case-insensitively, which matches plugin name rules.
    // The first entry wins when a name appears twice.
    TMap<FString, int32> BuildNameIndex(const TArray<FUBotPackageInfo>& Packages)
    {
        TMap<FString, int32> NameIndex;
        NameIndex.Reserve(Packages.Num());
        for (int32 PackageIndex = 0; PackageIndex < Packages.Num(); ++PackageIndex)
        {
            const FString Name = Packages[PackageIndex].Name.TrimStartAndEnd();
            if (!Name.IsEmpty() && !NameIndex.Contains(Name))
            {
                NameIndex.Add(Name, PackageIndex);
            }
        }
        return NameIndex;
    }

    int32 FindPackageIndex(const TMap<FString, int32>& NameIndex, const FString& Name)
    {
        const int32* Found = NameIndex.Find(Name.TrimStartAndEnd());
        return Found ? *Found : INDEX_NONE;
    }

    // Edges[Index] lists the packages that Packages[Index] requires (non-optional, present in the set).
    TArray<TArray<int32>> BuildRequiredEdges(const TArray<FUBotPackageInfo>& Packages, const TMap<FString, int32>& NameIndex)
    {
        TArray<TArray<int32>> Edges;
        Edges.SetNum(Packages.Num());
        for (int32 PackageIndex = 0; PackageIndex < Packages.Num(); ++PackageIndex)
        {
            for (const FUBotPackageDependency& Dependency : Packages[PackageIndex].Requires)
            {
                if (Dependency.bOptional)
                {
                    continue;
                }
                const int32 TargetIndex = FindPackageIndex(NameIndex, Dependency.Name);
                if (TargetIndex != INDEX_NONE)
                {
                    Edges[PackageIndex].AddUnique(TargetIndex);
                }
            }
        }
        return Edges;
    }

    bool RequiresDirectly(const FUBotPackageInfo& Package, const FString& Name)
    {
        for (const FUBotPackageDependency& Dependency : Package.Requires)
        {
            if (!Dependency.bOptional && Dependency.Name.TrimStartAndEnd().Equals(Name, ESearchCase::IgnoreCase))
            {
                return true;
            }
        }
        return false;
    }

    // Tarjan's strongly connected components over the required edges.
    class FCycleFinder
    {
    public:
        explicit FCycleFinder(const TArray<TArray<int32>>& InEdges)
            : Edges(InEdges)
        {
            const int32 NodeCount = Edges.Num();
            Indices.Init(INDEX_NONE, NodeCount);
            LowLinks.Init(0, NodeCount);
            OnStack.Init(false, NodeCount);
            ComponentOf.Init(INDEX_NONE, NodeCount);
            for (int32 Node = 0; Node < NodeCount; ++Node)
            {
                if (Indices[Node] == INDEX_NONE)
                {
                    StrongConnect(Node);
                }
            }
        }

        /** True when Node is in a component of two or more packages or requires itself. */
        bool IsOnCycle(int32 Node) const
        {
            return ComponentSizes[ComponentOf[Node]] > 1 || Edges[Node].Contains(Node);
        }

        /** Shortest cycle through Start, as node indices beginning and ending with Start. */
        TArray<int32> FindCyclePath(int32 Start) const
        {
            TMap<int32, int32> Parents;
            TArray<int32> Queue;
            Queue.Add(Start);
            for (int32 Head = 0; Head < Queue.Num(); ++Head)
            {
                const int32 Node = Queue[Head];
                for (const int32 Next : Edges[Node])
                {
                    if (ComponentOf[Next] != ComponentOf[Start])
                    {
                        continue;
                    }
                    if (Next == Start)
                    {
                        TArray<int32> Path;
                        Path.Add(Start);
                        for (int32 Cursor = Node; Cursor != Start; Cursor = Parents.FindChecked(Cursor))
                        {
                            Path.Insert(Cursor, 1);
                        }
                        Path.Add(Start);
                        return Path;
                    }
                    if (!Parents.Contains(Next))
                    {
                        Parents.Add(Next, Node);
                        Queue.Add(Next);
                    }
                }
            }
            return { Start };
        }

    private:
        void StrongConnect(int32 Node)
        {
            Indices[Node] = NextIndex;
            LowLinks[Node] = NextIndex;
            ++NextIndex;
            Stack.Add(Node);
            OnStack[Node] = true;

            for (const int32 Next : Edges[Node])
            {
                if (Indices[Next] == INDEX_NONE)
                {
                    StrongConnect(Next);
                    LowLinks[Node] = FMath::Min(LowLinks[Node], LowLinks[Next]);
                }
                else if (OnStack[Next])
                {
                    LowLinks[Node] = FMath::Min(LowLinks[Node], Indices[Next]);
                }
            }

            if (LowLinks[Node] == Indices[Node])
            {
                const int32 Component = ComponentSizes.Add(0);
                int32 Member = INDEX_NONE;
                do
                {
                    Member = Stack.Pop(EAllowShrinking::No);
                    OnStack[Member] = false;
                    ComponentOf[Member] = Component;
                    ++ComponentSizes[Component];
                }
                while (Member != Node);
            }
        }

        const TArray<TArray<int32>>& Edges;
        TArray<int32> Indices;
        TArray<int32> LowLinks;
        TArray<bool> OnStack;
        TArray<int32> ComponentOf;
        TArray<int32> ComponentSizes;
        TArray<int32> Stack;
        int32 NextIndex = 0;
    };

    // Depth-first walk over required dependencies that records a post-order (dependencies first).
    struct FDependencyWalker
    {
        FDependencyWalker(const TArray<FUBotPackageInfo>& InPackages, const TMap<FString, int32>& InNameIndex)
            : Packages(InPackages)
            , NameIndex(InNameIndex)
        {
            Marks.Init(EMark::Unvisited, Packages.Num());
        }

        bool Visit(int32 Index)
        {
            if (Marks[Index] == EMark::Done)
            {
                return true;
            }
            if (Marks[Index] == EMark::InProgress)
            {
                TArray<FString> CycleNames;
                for (int32 PathIndex = Path.Find(Index); PathIndex < Path.Num(); ++PathIndex)
                {
                    CycleNames.Add(Packages[Path[PathIndex]].Name);
                }
                CycleNames.Add(Packages[Index].Name);
                CycleError = FString::Printf(TEXT("Dependency cycle: %s."), *FString::Join(CycleNames, TEXT(" -> ")));
                return false;
            }

            Marks[Index] = EMark::InProgress;
            Path.Add(Index);
            for (const FUBotPackageDependency& Dependency : Packages[Index].Requires)
            {
                const FString DependencyName = Dependency.Name.TrimStartAndEnd();
                if (Dependency.bOptional || DependencyName.IsEmpty())
                {
                    continue;
                }

                const int32 Next = FindPackageIndex(NameIndex, DependencyName);
                if (Next == INDEX_NONE)
                {
                    Missing.AddUnique(FString::Printf(TEXT("'%s' (required by '%s')"), *DependencyName, *Packages[Index].Name));
                    continue;
                }
                if (!Visit(Next))
                {
                    return false;
                }
            }
            Path.Pop(EAllowShrinking::No);
            Marks[Index] = EMark::Done;
            Order.Add(Packages[Index].Name);
            return true;
        }

        enum class EMark : uint8
        {
            Unvisited,
            InProgress,
            Done
        };

        const TArray<FUBotPackageInfo>& Packages;
        const TMap<FString, int32>& NameIndex;
        TArray<EMark> Marks;
        TArray<int32> Path;
        TArray<FString> Order;
        TArray<FString> Missing;
        FString CycleError;
    };

    void PostOrderVisit(int32 Node, const TArray<TArray<int32>>& Edges, const TArray<bool>& bInSet, TArray<bool>& bVisited, TArray<int32>& OutPostOrder)
    {
        bVisited[Node] = true;
        for (const int32 Next : Edges[Node])
        {
            if (bInSet[Next] && !bVisited[Next])
            {
                PostOrderVisit(Next, Edges, bInSet, bVisited, OutPostOrder);
            }
        }
        OutPostOrder.Add(Node);
    }

    void ReadString(const FJsonObject& Json, const TCHAR* Field, FString& OutValue, TArray<FString>& OutIssues)
    {
        if (!Json.HasField(Field))
        {
            return;
        }
        FString Value;
        if (!Json.TryGetStringField(Field, Value))
        {
            OutIssues.Add(FString::Printf(TEXT("\"%s\" is not a string."), Field));
            return;
        }
        OutValue = Value.TrimStartAndEnd();
    }

    void ReadStringArray(const FJsonObject& Json, const TCHAR* Field, TArray<FString>& OutValues, TArray<FString>& OutIssues)
    {
        if (!Json.HasField(Field))
        {
            return;
        }
        const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
        if (!Json.TryGetArrayField(Field, Values) || Values == nullptr)
        {
            OutIssues.Add(FString::Printf(TEXT("\"%s\" is not an array."), Field));
            return;
        }

        OutValues.Reset();
        for (int32 ValueIndex = 0; ValueIndex < Values->Num(); ++ValueIndex)
        {
            const TSharedPtr<FJsonValue>& Value = (*Values)[ValueIndex];
            FString Item;
            if (!Value.IsValid() || !Value->TryGetString(Item) || Item.TrimStartAndEnd().IsEmpty())
            {
                OutIssues.Add(FString::Printf(TEXT("\"%s\"[%d] is not a non-empty string."), Field, ValueIndex));
                continue;
            }
            OutValues.AddUnique(Item.TrimStartAndEnd());
        }
    }

    void ReadRequires(const FJsonObject& Json, TArray<FUBotPackageDependency>& OutRequires, TArray<FString>& OutIssues)
    {
        if (!Json.HasField(TEXT("Requires")))
        {
            return;
        }
        const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
        if (!Json.TryGetArrayField(TEXT("Requires"), Values) || Values == nullptr)
        {
            OutIssues.Add(TEXT("\"Requires\" is not an array."));
            return;
        }

        OutRequires.Reset();
        for (int32 ValueIndex = 0; ValueIndex < Values->Num(); ++ValueIndex)
        {
            const TSharedPtr<FJsonValue>& Value = (*Values)[ValueIndex];
            const TSharedPtr<FJsonObject>* Entry = nullptr;
            if (!Value.IsValid() || !Value->TryGetObject(Entry) || Entry == nullptr || !Entry->IsValid())
            {
                OutIssues.Add(FString::Printf(TEXT("\"Requires\"[%d] is not an object."), ValueIndex));
                continue;
            }

            FUBotPackageDependency Dependency;
            TArray<FString> EntryIssues;
            ReadString(**Entry, TEXT("Name"), Dependency.Name, EntryIssues);
            ReadString(**Entry, TEXT("Version"), Dependency.Version, EntryIssues);
            if ((*Entry)->HasField(TEXT("Optional")) && !(*Entry)->TryGetBoolField(TEXT("Optional"), Dependency.bOptional))
            {
                EntryIssues.Add(TEXT("\"Optional\" is not a boolean."));
            }
            if (Dependency.Name.IsEmpty())
            {
                EntryIssues.Add(TEXT("it has no \"Name\"."));
            }

            if (!EntryIssues.IsEmpty())
            {
                OutIssues.Add(FString::Printf(TEXT("\"Requires\"[%d] was skipped: %s"), ValueIndex, *FString::Join(EntryIssues, TEXT(" "))));
                continue;
            }
            OutRequires.Add(MoveTemp(Dependency));
        }
    }

    FString FormatConstraintSuffix(const FString& Constraint)
    {
        const FString Trimmed = Constraint.TrimStartAndEnd();
        return Trimmed.IsEmpty() ? FString() : TEXT(" ") + Trimmed;
    }

    void SetError(FString* OutError, const FString& Message)
    {
        if (OutError)
        {
            *OutError = Message;
        }
    }
}

FUBotPackageRegistry& FUBotPackageRegistry::Get()
{
    // Intentionally leaked: delegate bindings may belong to modules that are already unloaded when
    // static destructors run.
    static FUBotPackageRegistry* Instance = new FUBotPackageRegistry();
    return *Instance;
}

bool FUBotPackageRegistry::ParseDescriptor(const FJsonObject& DescriptorJson, const FString& PluginName, FUBotPackageInfo& Out, FString* OutError)
{
    using namespace UBot::PackageRegistryPrivate;

    const TSharedPtr<FJsonObject>* Block = nullptr;
    if (!DescriptorJson.TryGetObjectField(TEXT("UBot"), Block) || Block == nullptr || !Block->IsValid())
    {
        SetError(OutError, DescriptorJson.HasField(TEXT("UBot"))
            ? TEXT("The \"UBot\" field is not an object.")
            : TEXT("The descriptor has no \"UBot\" block."));
        return false;
    }

    const FString Name = PluginName.TrimStartAndEnd();
    if (Name.IsEmpty())
    {
        SetError(OutError, TEXT("The plugin name is empty."));
        return false;
    }

    TArray<FString> Issues;
    FUBotPackageInfo Info;
    Info.Name = Name;

    ReadString(DescriptorJson, TEXT("FriendlyName"), Info.FriendlyName, Issues);
    if (Info.FriendlyName.IsEmpty())
    {
        Info.FriendlyName = Name;
    }
    ReadString(DescriptorJson, TEXT("Description"), Info.Description, Issues);
    ReadString(DescriptorJson, TEXT("DocsURL"), Info.DocsUrl, Issues);

    ReadString(DescriptorJson, TEXT("VersionName"), Info.Version, Issues);
    FUBotSemVer ParsedVersion;
    if (Info.Version.IsEmpty())
    {
        Issues.Add(TEXT("The descriptor has no VersionName."));
    }
    else if (!FUBotSemVer::Parse(Info.Version, ParsedVersion))
    {
        Issues.Add(FString::Printf(TEXT("VersionName '%s' is not a semantic version."), *Info.Version));
    }

    if (!(*Block)->HasField(TEXT("Layer")))
    {
        Issues.Add(TEXT("The \"UBot\" block has no Layer."));
    }
    ParseMetadataJson(**Block, Info, Issues);

    Out = MoveTemp(Info);
    SetError(OutError, FString::Join(Issues, TEXT("; ")));
    return true;
}

bool FUBotPackageRegistry::ParseMetadataJson(const FJsonObject& Json, FUBotPackageInfo& Out, TArray<FString>& OutIssues)
{
    using namespace UBot::PackageRegistryPrivate;

    const int32 IssueCountBefore = OutIssues.Num();

    if (Json.HasField(TEXT("Layer")))
    {
        FString LayerText;
        const int32 IssueCountBeforeLayer = OutIssues.Num();
        ReadString(Json, TEXT("Layer"), LayerText, OutIssues);
        Out.Layer = ParsePackageLayer(LayerText);
        const bool bLayerWasString = OutIssues.Num() == IssueCountBeforeLayer;
        if (bLayerWasString && Out.Layer == EUBotPackageLayer::Unknown && !LayerText.Equals(TEXT("Unknown"), ESearchCase::IgnoreCase))
        {
            OutIssues.Add(FString::Printf(TEXT("Unknown layer '%s' (expected Foundation, Capability, Composition, Adapter or Content)."), *LayerText));
        }
    }

    ReadString(Json, TEXT("Repository"), Out.Repository, OutIssues);
    ReadString(Json, TEXT("DocsUrl"), Out.DocsUrl, OutIssues);
    ReadStringArray(Json, TEXT("Tags"), Out.Tags, OutIssues);
    ReadStringArray(Json, TEXT("Provides"), Out.Provides, OutIssues);
    ReadStringArray(Json, TEXT("ExternalRequires"), Out.ExternalRequires, OutIssues);
    ReadRequires(Json, Out.Requires, OutIssues);

    return OutIssues.Num() == IssueCountBefore;
}

void FUBotPackageRegistry::ValidatePackageSet(TArray<FUBotPackageInfo>& Packages)
{
    using namespace UBot::PackageRegistryPrivate;

    const TMap<FString, int32> NameIndex = BuildNameIndex(Packages);

    for (FUBotPackageInfo& Package : Packages)
    {
        Package.Problems.Reset();
        const bool bPackageEnabled = IsEnabledEntry(Package);

        for (const FUBotPackageDependency& Dependency : Package.Requires)
        {
            const FString DependencyName = Dependency.Name.TrimStartAndEnd();
            if (DependencyName.IsEmpty())
            {
                Package.Problems.Add(TEXT("Has a requirement without a package name."));
                continue;
            }

            FUBotVersionConstraint Constraint;
            FString ConstraintError;
            const bool bConstraintValid = FUBotVersionConstraint::Parse(Dependency.Version, Constraint, &ConstraintError);
            if (!bConstraintValid)
            {
                Package.Problems.Add(FString::Printf(TEXT("Requirement '%s' has an unparsable version constraint: %s."), *DependencyName, *ConstraintError));
            }

            const int32 TargetIndex = FindPackageIndex(NameIndex, DependencyName);
            const FUBotPackageInfo* Target = TargetIndex != INDEX_NONE ? &Packages[TargetIndex] : nullptr;
            if (Target == nullptr || !IsInstalledEntry(*Target))
            {
                if (!Dependency.bOptional)
                {
                    Package.Problems.Add(FString::Printf(TEXT("Requires '%s'%s, which is not installed."), *DependencyName, *FormatConstraintSuffix(Dependency.Version)));
                }
                continue;
            }

            if (bConstraintValid && !Constraint.IsAny())
            {
                FUBotSemVer InstalledVersion;
                if (!FUBotSemVer::Parse(Target->Version, InstalledVersion))
                {
                    Package.Problems.Add(FString::Printf(TEXT("Requires '%s' %s, but its installed version '%s' is not a semantic version."),
                        *DependencyName, *Constraint.ToString(), *Target->Version));
                }
                else if (!Constraint.IsSatisfiedBy(InstalledVersion))
                {
                    Package.Problems.Add(FString::Printf(TEXT("Requires '%s' %s, but version %s is installed."),
                        *DependencyName, *Constraint.ToString(), *Target->Version));
                }
            }

            if (!Dependency.bOptional && bPackageEnabled && !IsEnabledEntry(*Target))
            {
                Package.Problems.Add(FString::Printf(TEXT("Is enabled but requires '%s', which is disabled."), *DependencyName));
            }
        }
    }

    const TArray<TArray<int32>> Edges = BuildRequiredEdges(Packages, NameIndex);
    const FCycleFinder CycleFinder(Edges);
    for (int32 PackageIndex = 0; PackageIndex < Packages.Num(); ++PackageIndex)
    {
        if (!CycleFinder.IsOnCycle(PackageIndex))
        {
            continue;
        }

        TArray<FString> CycleNames;
        for (const int32 Node : CycleFinder.FindCyclePath(PackageIndex))
        {
            CycleNames.Add(Packages[Node].Name);
        }
        Packages[PackageIndex].Problems.Add(FString::Printf(TEXT("Dependency cycle: %s."), *FString::Join(CycleNames, TEXT(" -> "))));
    }
}

bool FUBotPackageRegistry::ResolveDependencyOrder(const TArray<FUBotPackageInfo>& Packages, const FString& Name, TArray<FString>& OutOrder, FString* OutError)
{
    using namespace UBot::PackageRegistryPrivate;

    OutOrder.Reset();

    const TMap<FString, int32> NameIndex = BuildNameIndex(Packages);
    const int32 RootIndex = FindPackageIndex(NameIndex, Name);
    if (RootIndex == INDEX_NONE)
    {
        SetError(OutError, FString::Printf(TEXT("Package '%s' is not known."), *Name.TrimStartAndEnd()));
        return false;
    }

    FDependencyWalker Walker(Packages, NameIndex);
    if (!Walker.Visit(RootIndex))
    {
        SetError(OutError, Walker.CycleError);
        return false;
    }
    if (!Walker.Missing.IsEmpty())
    {
        SetError(OutError, FString::Printf(TEXT("Missing required packages: %s."), *FString::Join(Walker.Missing, TEXT(", "))));
        return false;
    }

    OutOrder = MoveTemp(Walker.Order);
    SetError(OutError, FString());
    return true;
}

TArray<FString> FUBotPackageRegistry::FindDependents(const TArray<FUBotPackageInfo>& Packages, const FString& Name, bool bOnlyEnabled)
{
    using namespace UBot::PackageRegistryPrivate;

    const FString TargetName = Name.TrimStartAndEnd();

    // Breadth-first over names, so packages that require a name missing from the set still count.
    TArray<bool> bIsDependent;
    bIsDependent.Init(false, Packages.Num());
    TArray<FString> Frontier;
    Frontier.Add(TargetName);
    for (int32 Head = 0; Head < Frontier.Num(); ++Head)
    {
        const FString Current = Frontier[Head];
        for (int32 PackageIndex = 0; PackageIndex < Packages.Num(); ++PackageIndex)
        {
            const FString PackageName = Packages[PackageIndex].Name.TrimStartAndEnd();
            if (bIsDependent[PackageIndex] || PackageName.Equals(TargetName, ESearchCase::IgnoreCase))
            {
                continue;
            }
            if (RequiresDirectly(Packages[PackageIndex], Current))
            {
                bIsDependent[PackageIndex] = true;
                Frontier.AddUnique(PackageName);
            }
        }
    }

    // Post-order over required edges lists dependencies before dependents; reversing it puts every
    // package before the packages it requires.
    const TMap<FString, int32> NameIndex = BuildNameIndex(Packages);
    const TArray<TArray<int32>> Edges = BuildRequiredEdges(Packages, NameIndex);
    TArray<bool> bVisited;
    bVisited.Init(false, Packages.Num());
    TArray<int32> PostOrder;
    for (int32 PackageIndex = 0; PackageIndex < Packages.Num(); ++PackageIndex)
    {
        if (bIsDependent[PackageIndex] && !bVisited[PackageIndex])
        {
            PostOrderVisit(PackageIndex, Edges, bIsDependent, bVisited, PostOrder);
        }
    }

    TArray<FString> Result;
    for (int32 OrderIndex = PostOrder.Num() - 1; OrderIndex >= 0; --OrderIndex)
    {
        const FUBotPackageInfo& Package = Packages[PostOrder[OrderIndex]];
        if (!bOnlyEnabled || IsEnabledEntry(Package))
        {
            Result.Add(Package.Name);
        }
    }
    return Result;
}

void FUBotPackageRegistry::Refresh()
{
    TArray<FUBotPackageInfo> NewPackages;
    TArray<FString> NewIssues;

    for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetDiscoveredPlugins())
    {
        const FString& DescriptorFile = Plugin->GetDescriptorFileName();
        FString JsonText;
        if (DescriptorFile.IsEmpty() || !FFileHelper::LoadFileToString(JsonText, *DescriptorFile))
        {
            continue;
        }

        // Most discovered plugins are engine plugins; skip the JSON parse when the key cannot be there.
        if (!JsonText.Contains(TEXT("\"UBot\"")))
        {
            continue;
        }

        const FString DescriptorPath = FPaths::ConvertRelativePathToFull(DescriptorFile);
        TSharedPtr<FJsonObject> DescriptorJson;
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
        if (!FJsonSerializer::Deserialize(Reader, DescriptorJson) || !DescriptorJson.IsValid())
        {
            NewIssues.Add(FString::Printf(TEXT("Cannot parse plugin descriptor '%s': %s"), *DescriptorPath, *Reader->GetErrorMessage()));
            continue;
        }

        FUBotPackageInfo Info;
        FString ParseIssues;
        if (!ParseDescriptor(*DescriptorJson, Plugin->GetName(), Info, &ParseIssues))
        {
            // A "UBot" key that is not an object is a broken uBot descriptor; no key at all is just
            // a plugin that mentions uBot somewhere else (e.g. its category).
            if (DescriptorJson->HasField(TEXT("UBot")))
            {
                NewIssues.Add(FString::Printf(TEXT("uBot package '%s' (%s): %s"), *Plugin->GetName(), *DescriptorPath, *ParseIssues));
            }
            continue;
        }
        if (!ParseIssues.IsEmpty())
        {
            NewIssues.Add(FString::Printf(TEXT("uBot package '%s' (%s): %s"), *Info.Name, *DescriptorPath, *ParseIssues));
        }

        Info.bInstalled = true;
        Info.bEnabled = Plugin->IsEnabled();
        Info.State = Info.bEnabled ? EUBotPackageState::Enabled : EUBotPackageState::Disabled;
        Info.BaseDir = FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir());
        Info.DescriptorPath = DescriptorPath;
        NewPackages.Add(MoveTemp(Info));
    }

    NewPackages.Sort([](const FUBotPackageInfo& A, const FUBotPackageInfo& B)
    {
        return A.Name.Compare(B.Name, ESearchCase::IgnoreCase) < 0;
    });
    ValidatePackageSet(NewPackages);

    InstalledPackages = MoveTemp(NewPackages);
    DescriptorIssues = MoveTemp(NewIssues);
    OnPackagesChanged.Broadcast();
}

const TArray<FUBotPackageInfo>& FUBotPackageRegistry::GetInstalledPackages() const
{
    return InstalledPackages;
}

const FUBotPackageInfo* FUBotPackageRegistry::FindPackage(const FString& Name) const
{
    const FString TrimmedName = Name.TrimStartAndEnd();
    return InstalledPackages.FindByPredicate([&TrimmedName](const FUBotPackageInfo& Package)
    {
        return Package.Name.Equals(TrimmedName, ESearchCase::IgnoreCase);
    });
}

bool FUBotPackageRegistry::IsPackageEnabled(const FString& Name) const
{
    const FUBotPackageInfo* Package = FindPackage(Name);
    return Package != nullptr && UBot::PackageRegistryPrivate::IsEnabledEntry(*Package);
}

int32 FUBotPackageRegistry::ValidateAndLog()
{
    ValidatePackageSet(InstalledPackages);

    int32 ProblemCount = 0;
    for (const FString& Issue : DescriptorIssues)
    {
        UE_LOG(LogUBot, Warning, TEXT("%s"), *Issue);
        ++ProblemCount;
    }

    int32 EnabledCount = 0;
    for (const FUBotPackageInfo& Package : InstalledPackages)
    {
        if (UBot::PackageRegistryPrivate::IsEnabledEntry(Package))
        {
            ++EnabledCount;
        }
        for (const FString& Problem : Package.Problems)
        {
            UE_LOG(LogUBot, Warning, TEXT("uBot package '%s': %s"), *Package.Name, *Problem);
            ++ProblemCount;
        }
    }

    // Extensions waiting for a point that never appeared usually mean the owning package is
    // disabled or not installed. Worth a warning, but not a broken package set.
    const FUBotExtensionRegistry& Extensions = FUBotExtensionRegistry::Get();
    for (const FUBotExtensionRecord& Record : Extensions.GetAllExtensions())
    {
        if (!Extensions.IsExtensionPointDeclared(Record.ExtensionPoint))
        {
            UE_LOG(LogUBot, Warning, TEXT("uBot extension '%s' from package '%s' targets extension point '%s', which was never declared."),
                *Record.ExtensionName.ToString(), *Record.OwnerPackage.ToString(), *Record.ExtensionPoint.ToString());
        }
    }

    UE_LOG(LogUBot, Log, TEXT("uBot packages: %d installed, %d enabled, %d problem(s)."), InstalledPackages.Num(), EnabledCount, ProblemCount);
    return ProblemCount;
}
