#include "UBotPackageLibrary.h"

#include "UBotExtensionRegistry.h"
#include "UBotPackageRegistry.h"
#include "UBotSemVer.h"

TArray<FUBotPackageInfo> UUBotPackageLibrary::GetInstalledUBotPackages()
{
    return FUBotPackageRegistry::Get().GetInstalledPackages();
}

bool UUBotPackageLibrary::FindUBotPackage(const FString& Name, FUBotPackageInfo& OutInfo)
{
    if (const FUBotPackageInfo* Package = FUBotPackageRegistry::Get().FindPackage(Name))
    {
        OutInfo = *Package;
        return true;
    }
    OutInfo = FUBotPackageInfo();
    return false;
}

bool UUBotPackageLibrary::IsUBotPackageEnabled(const FString& Name)
{
    return FUBotPackageRegistry::Get().IsPackageEnabled(Name);
}

FString UUBotPackageLibrary::GetUBotPackageVersion(const FString& Name)
{
    const FUBotPackageInfo* Package = FUBotPackageRegistry::Get().FindPackage(Name);
    return Package ? Package->Version : FString();
}

bool UUBotPackageLibrary::IsUBotPackageVersionSatisfied(const FString& Name, const FString& Constraint)
{
    const FUBotPackageInfo* Package = FUBotPackageRegistry::Get().FindPackage(Name);
    if (Package == nullptr)
    {
        return false;
    }

    FUBotVersionConstraint ParsedConstraint;
    if (!FUBotVersionConstraint::Parse(Constraint, ParsedConstraint))
    {
        return false;
    }
    if (ParsedConstraint.IsAny())
    {
        return true;
    }

    FUBotSemVer Version;
    return FUBotSemVer::Parse(Package->Version, Version) && ParsedConstraint.IsSatisfiedBy(Version);
}

TArray<FName> UUBotPackageLibrary::GetUBotExtensionPointNames()
{
    TArray<FName> Names;
    for (const FUBotExtensionPointInfo& Point : FUBotExtensionRegistry::Get().GetExtensionPoints())
    {
        Names.Add(Point.Name);
    }
    return Names;
}

TArray<FName> UUBotPackageLibrary::GetUBotExtensionNames(FName PointName)
{
    // Uses the names captured at registration, which are the keys UnregisterExtension expects.
    const FUBotExtensionRegistry& Registry = FUBotExtensionRegistry::Get();
    TArray<FName> Names;
    if (!Registry.IsExtensionPointDeclared(PointName))
    {
        return Names;
    }
    for (const FUBotExtensionRecord& Record : Registry.GetAllExtensions())
    {
        if (Record.ExtensionPoint == PointName)
        {
            Names.Add(Record.ExtensionName);
        }
    }
    return Names;
}
