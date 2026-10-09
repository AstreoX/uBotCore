#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "UBotPackageCommandlet.generated.h"

/**
 * Headless uBot package management.
 *
 * UnrealEditor-Cmd <Project>.uproject -run=UBotPackage [-List] [-Validate] [-Install=<Name>]
 *     [-Update=<Name>] [-Enable=<Name>] [-Disable=<Name> [-Force]] [-Timeout=<Seconds>]
 *
 * Actions run in the order Install, Update, Enable, Disable, then List and Validate. Returns 0 on
 * success and 1 when any action fails or, with -Validate, when validation reports a problem or when
 * the arguments are malformed (an action or -Timeout without "=<value>", an unparsable timeout).
 */
UCLASS()
class UBOTCOREEDITOR_API UUBotPackageCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:
    UUBotPackageCommandlet();

    virtual int32 Main(const FString& Params) override;

    /**
     * Strict parser for -Timeout=<Seconds>: a plain non-negative decimal number, where 0 means wait
     * forever. Rejects empty text, signs other than '+', exponents and trailing garbage.
     */
    static bool TryParseTimeoutSeconds(const FString& Text, double& OutSeconds);
};
