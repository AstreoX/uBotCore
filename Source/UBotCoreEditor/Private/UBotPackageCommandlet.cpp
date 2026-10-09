#include "UBotPackageCommandlet.h"

#include "Templates/Function.h"
#include "UBotCore.h"
#include "UBotPackageRegistry.h"
#include "UBotPackageService.h"
#include "UBotPackageTypes.h"

namespace UBotPackageCommandletPrivate
{
    // Clones of large repositories can take a while; a hung git must not block CI forever.
    constexpr double DefaultTimeoutSeconds = 1800.0;

    void PrintUsage()
    {
        UE_LOG(LogUBot, Display, TEXT("Usage: -run=UBotPackage [-List] [-Validate] [-Install=<Name>] [-Update=<Name>]"));
        UE_LOG(LogUBot, Display, TEXT("       [-Enable=<Name>] [-Disable=<Name> [-Force]] [-Timeout=<Seconds>]"));
    }

    void PrintMessages(const TArray<FString>& Messages)
    {
        for (const FString& Message : Messages)
        {
            UE_LOG(LogUBot, Display, TEXT("  %s"), *Message);
        }
    }

    FString GetStateLabel(const FUBotPackageInfo& Package, const FUBotPackageService& Service)
    {
        if (!Package.bInstalled)
        {
            return TEXT("Not installed");
        }
        bool bPendingEnabled = false;
        if (Service.GetPendingEnabledState(Package.Name, bPendingEnabled) && bPendingEnabled != Package.bEnabled)
        {
            return bPendingEnabled ? TEXT("Enabled after restart") : TEXT("Disabled after restart");
        }
        return Package.bEnabled ? TEXT("Enabled") : TEXT("Disabled");
    }

    void PrintTable(const TArray<FUBotPackageInfo>& Packages, const FUBotPackageService& Service)
    {
        constexpr int32 NumColumns = 5;
        TArray<TArray<FString>> Rows;
        Rows.Add({ TEXT("Name"), TEXT("Version"), TEXT("Layer"), TEXT("State"), TEXT("Problems") });
        for (const FUBotPackageInfo& Package : Packages)
        {
            Rows.Add({
                Package.Name,
                Package.Version.IsEmpty() ? FString(TEXT("-")) : Package.Version,
                FString(LexToString(Package.Layer)),
                GetStateLabel(Package, Service),
                FString::FromInt(Package.Problems.Num()) });
        }

        int32 Widths[NumColumns] = {};
        for (const TArray<FString>& Row : Rows)
        {
            for (int32 Column = 0; Column < NumColumns; ++Column)
            {
                Widths[Column] = FMath::Max(Widths[Column], Row[Column].Len());
            }
        }

        auto FormatRow = [&Widths](const TArray<FString>& Row)
        {
            FString Line;
            for (int32 Column = 0; Column < NumColumns; ++Column)
            {
                Line += Column + 1 < NumColumns ? Row[Column].RightPad(Widths[Column] + 2) : Row[Column];
            }
            return Line;
        };

        int32 TotalWidth = 0;
        for (const int32 Width : Widths)
        {
            TotalWidth += Width + 2;
        }

        UE_LOG(LogUBot, Display, TEXT("%s"), *FormatRow(Rows[0]));
        UE_LOG(LogUBot, Display, TEXT("%s"), *FString::ChrN(TotalWidth - 2, TEXT('-')));
        for (int32 RowIndex = 1; RowIndex < Rows.Num(); ++RowIndex)
        {
            UE_LOG(LogUBot, Display, TEXT("%s"), *FormatRow(Rows[RowIndex]));
        }

        for (const FUBotPackageInfo& Package : Packages)
        {
            for (const FString& Problem : Package.Problems)
            {
                UE_LOG(LogUBot, Display, TEXT("  %s: %s"), *Package.Name, *Problem);
            }
        }
    }

    bool RunAsyncOperation(const TCHAR* Action, const FString& PackageName, double TimeoutSeconds,
        TFunctionRef<void(FUBotPackageService::FOnPackageOperationComplete)> Start)
    {
        struct FResult
        {
            bool bDone = false;
            bool bSuccess = false;
            TArray<FString> Messages;
        };

        UE_LOG(LogUBot, Display, TEXT("%s %s"), Action, *PackageName);

        // Shared so that a callback arriving after a timeout never touches a dead stack frame.
        const TSharedRef<FResult> Result = MakeShared<FResult>();
        Start([Result](bool bSuccess, const TArray<FString>& Messages)
        {
            Result->bDone = true;
            Result->bSuccess = bSuccess;
            Result->Messages = Messages;
        });

        const bool bInTime = FUBotPackageService::Get().WaitUntilIdle(TimeoutSeconds);
        PrintMessages(Result->Messages);

        if (!bInTime)
        {
            UE_LOG(LogUBot, Error, TEXT("%s %s timed out after %.0f seconds."), Action, *PackageName, TimeoutSeconds);
            return false;
        }
        if (!Result->bDone)
        {
            UE_LOG(LogUBot, Error, TEXT("%s %s did not report completion."), Action, *PackageName);
            return false;
        }
        if (!Result->bSuccess)
        {
            UE_LOG(LogUBot, Error, TEXT("%s %s failed."), Action, *PackageName);
        }
        return Result->bSuccess;
    }
}

UUBotPackageCommandlet::UUBotPackageCommandlet()
{
    IsClient = false;
    IsServer = false;
    IsEditor = true;
    LogToConsole = true;
    ShowErrorCount = true;
    // The process exit code is exactly what Main returns, independent of unrelated engine errors.
    UseCommandletResultAsExitCode = true;

    HelpDescription = TEXT("Lists, validates, installs, updates, enables and disables uBot packages.");
    HelpUsage = TEXT("UnrealEditor-Cmd <Project>.uproject -run=UBotPackage [-List] [-Validate] [-Install=<Name>] [-Update=<Name>] [-Enable=<Name>] [-Disable=<Name> [-Force]] [-Timeout=<Seconds>]");
    HelpParamNames = { TEXT("List"), TEXT("Validate"), TEXT("Install"), TEXT("Update"), TEXT("Enable"), TEXT("Disable"), TEXT("Force"), TEXT("Timeout") };
    HelpParamDescriptions = {
        TEXT("Print the installed and indexed packages."),
        TEXT("Validate the installed packages; any problem makes the commandlet fail."),
        TEXT("Clone a package and its missing dependencies from the package index, then enable it."),
        TEXT("Run git pull --ff-only in an installed package."),
        TEXT("Enable a package and its required packages in the project file."),
        TEXT("Disable a package in the project file."),
        TEXT("With -Disable, also disable enabled packages that depend on it."),
        TEXT("Seconds to wait for -Install or -Update before cancelling git (default 1800, 0 waits forever).") };
}

int32 UUBotPackageCommandlet::Main(const FString& Params)
{
    using namespace UBotPackageCommandletPrivate;

    TArray<FString> Tokens;
    TArray<FString> Switches;
    TMap<FString, FString> ParamValues;
    ParseCommandLine(*Params, Tokens, Switches, ParamValues);

    bool bList = Switches.Contains(TEXT("List"));
    const bool bValidate = Switches.Contains(TEXT("Validate"));
    const bool bForce = Switches.Contains(TEXT("Force"));
    const FString* InstallName = ParamValues.Find(TEXT("Install"));
    const FString* UpdateName = ParamValues.Find(TEXT("Update"));
    const FString* EnableName = ParamValues.Find(TEXT("Enable"));
    const FString* DisableName = ParamValues.Find(TEXT("Disable"));

    // ParseCommandLine only turns "-Key=Value" into a value; "-Install" or "-Install Name" ends up in
    // Switches / Tokens. Treating that as "no action" would exit 0 without doing what a script asked.
    for (const TCHAR* ValueSwitch : { TEXT("Install"), TEXT("Update"), TEXT("Enable"), TEXT("Disable"), TEXT("Timeout") })
    {
        if (Switches.Contains(ValueSwitch))
        {
            UE_LOG(LogUBot, Error, TEXT("-%s requires a value: use -%s=<%s>."), ValueSwitch, ValueSwitch,
                FCString::Strcmp(ValueSwitch, TEXT("Timeout")) == 0 ? TEXT("Seconds") : TEXT("Name"));
            return 1;
        }
    }

    double TimeoutSeconds = DefaultTimeoutSeconds;
    if (const FString* TimeoutText = ParamValues.Find(TEXT("Timeout")))
    {
        if (!TryParseTimeoutSeconds(*TimeoutText, TimeoutSeconds))
        {
            UE_LOG(LogUBot, Error, TEXT("-Timeout expects a non-negative number of seconds (0 waits forever), got '%s'."), **TimeoutText);
            return 1;
        }
    }

    const bool bHasAction = InstallName || UpdateName || EnableName || DisableName;
    if (!bHasAction && !bList && !bValidate)
    {
        PrintUsage();
        bList = true;
    }

    for (const FString* Name : { InstallName, UpdateName, EnableName, DisableName })
    {
        if (Name && Name->IsEmpty())
        {
            UE_LOG(LogUBot, Error, TEXT("A package name is required after -Install=, -Update=, -Enable= and -Disable=."));
            return 1;
        }
    }

    FUBotPackageRegistry::Get().Refresh();
    FUBotPackageService& Service = FUBotPackageService::Get();
    bool bSuccess = true;

    if (InstallName)
    {
        bSuccess &= RunAsyncOperation(TEXT("Installing"), *InstallName, TimeoutSeconds,
            [&Service, InstallName](FUBotPackageService::FOnPackageOperationComplete OnComplete)
            {
                Service.InstallPackageAsync(*InstallName, MoveTemp(OnComplete));
            });
    }

    if (UpdateName)
    {
        bSuccess &= RunAsyncOperation(TEXT("Updating"), *UpdateName, TimeoutSeconds,
            [&Service, UpdateName](FUBotPackageService::FOnPackageOperationComplete OnComplete)
            {
                Service.UpdatePackageAsync(*UpdateName, MoveTemp(OnComplete));
            });
    }

    if (EnableName)
    {
        UE_LOG(LogUBot, Display, TEXT("Enabling %s"), **EnableName);
        TArray<FString> Messages;
        const bool bEnabled = Service.EnablePackage(*EnableName, Messages);
        PrintMessages(Messages);
        if (!bEnabled)
        {
            UE_LOG(LogUBot, Error, TEXT("Enabling %s failed."), **EnableName);
            bSuccess = false;
        }
    }

    if (DisableName)
    {
        UE_LOG(LogUBot, Display, TEXT("Disabling %s%s"), **DisableName, bForce ? TEXT(" (forced)") : TEXT(""));
        TArray<FString> Messages;
        const bool bDisabled = Service.DisablePackage(*DisableName, bForce, Messages);
        PrintMessages(Messages);
        if (!bDisabled)
        {
            UE_LOG(LogUBot, Error, TEXT("Disabling %s failed."), **DisableName);
            bSuccess = false;
        }
    }

    TArray<FString> IndexErrors;
    const TArray<FUBotPackageInfo> View = Service.BuildPackageView(IndexErrors);

    if (bList)
    {
        PrintTable(View, Service);
    }

    for (const FString& IndexError : IndexErrors)
    {
        UE_LOG(LogUBot, Warning, TEXT("Package index: %s"), *IndexError);
    }

    if (bValidate)
    {
        const int32 ProblemCount = FUBotPackageRegistry::Get().ValidateAndLog() + IndexErrors.Num();
        if (ProblemCount > 0)
        {
            UE_LOG(LogUBot, Error, TEXT("Validation found %d problem(s)."), ProblemCount);
            bSuccess = false;
        }
        else
        {
            UE_LOG(LogUBot, Display, TEXT("Validation passed."));
        }
    }

    if (Service.IsRestartRequired())
    {
        UE_LOG(LogUBot, Display, TEXT("The project changed; start the editor again to load the new package state."));
    }

    return bSuccess ? 0 : 1;
}

bool UUBotPackageCommandlet::TryParseTimeoutSeconds(const FString& Text, double& OutSeconds)
{
    // FCString::Atod would turn "abc" into 0, which means "wait forever" and silently drops the hang guard.
    const FString Trimmed = Text.TrimStartAndEnd();
    bool bHasDigit = false;
    bool bHasDot = false;
    for (int32 Index = 0; Index < Trimmed.Len(); ++Index)
    {
        const TCHAR Character = Trimmed[Index];
        if (FChar::IsDigit(Character))
        {
            bHasDigit = true;
        }
        else if (Character == TEXT('.') && !bHasDot)
        {
            bHasDot = true;
        }
        else if (!(Character == TEXT('+') && Index == 0))
        {
            return false;
        }
    }
    if (!bHasDigit)
    {
        return false;
    }

    const double Parsed = FCString::Atod(*Trimmed);
    if (!FMath::IsFinite(Parsed))
    {
        return false;
    }
    OutSeconds = Parsed;
    return true;
}
