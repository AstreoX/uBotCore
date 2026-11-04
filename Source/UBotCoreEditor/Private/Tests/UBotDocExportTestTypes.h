#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UBotDocExportTestTypes.generated.h"

// A test fixture for deprecated-node export; production APIs need not stay deprecated forever.
UCLASS()
class UUBotDocExportTestLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintPure, Category = "Test", meta = (DisplayName = "Deprecated Test Vector", Keywords = "test deprecated vector", DeprecatedFunction, DeprecationMessage = "Test fixture only."))
    static FVector DeprecatedVector(const FVector& Value) { return Value; }
};
