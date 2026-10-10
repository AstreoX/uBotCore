#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "UBotPackageCommandlet.generated.h"

class FUBotPackageService;
struct FUBotPackageInfo;

/**
 * Headless uBot package management.
 *
 * UnrealEditor-Cmd <Project>.uproject -run=UBotPackage [-List] [-Validate] [-Install=<Name>[@<Version>]]
 *     [-Update=<Name>] [-Enable=<Name>] [-Disable=<Name> [-Force]] [-Timeout=<Seconds>]
 *
 * Actions run in the order Install, Update, Enable, Disable, then List and Validate. Returns 0 on
 * success and 1 when any action fails or, with -Validate, when validation reports a problem or when
 * the arguments are malformed (an action or -Timeout without "=<value>", an unparsable timeout or
 * -Install spec). -Install clones the git ref of the named index version of <Name> (default: the
 * newest installable one) into the index entry's folder. If <Name> is already installed, @<Version>
 * must equal the installed version (nothing is cloned) or the action fails; use -Update to change
 * versions. -List counts the problems of installed packages only.
 */
UCLASS()
class UBOTCOREEDITOR_API UUBotPackageCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:
    UUBotPackageCommandlet();

    virtual int32 Main(const FString& Params) override;

    /**
     * The lines -List prints: a table of Packages (name, version, layer, state and problem count),
     * then one line per problem. Only installed packages report problems, so a package that is
     * merely listed in the index (planned or not) never shows any.
     */
    static TArray<FString> FormatPackageList(const TArray<FUBotPackageInfo>& Packages, const FUBotPackageService& Service);

    /**
     * Splits an -Install value "<Name>" or "<Name>@<Version>". False when the name is empty or "@" is
     * followed by nothing; OutVersion is empty when no version was given. The outputs are only
     * written on success.
     */
    static bool TryParsePackageSpec(const FString& Text, FString& OutName, FString& OutVersion);

    /**
     * Strict parser for -Timeout=<Seconds>: a plain non-negative decimal number, where 0 means wait
     * forever. Rejects empty text, signs other than '+', exponents and trailing garbage.
     */
    static bool TryParseTimeoutSeconds(const FString& Text, double& OutSeconds);
};
