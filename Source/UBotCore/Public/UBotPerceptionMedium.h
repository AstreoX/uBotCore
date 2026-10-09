#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "UBotPerceptionMedium.generated.h"

class UWorld;

/** One spherical cell of a perception medium. Positions come straight from UE, so centimeters. */
USTRUCT(BlueprintType)
struct UBOTCORE_API FUBotMediumCell
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "uBot|Medium")
    FVector WorldPositionCentimeters = FVector::ZeroVector;

    /** Values below 1 cm are treated as 1 cm by the intersection math. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "uBot|Medium")
    float RadiusCentimeters = 150.0f;

    /** 0..1 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "uBot|Medium")
    float Density = 0.0f;

    /** 0..1, attenuates Density towards the border of the medium. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "uBot|Medium")
    float EdgeFactor = 1.0f;
};

/** State of one medium (smoke, fog, ...) at the moment it was sampled. */
USTRUCT(BlueprintType)
struct UBOTCORE_API FUBotMediumSnapshot
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "uBot|Medium")
    FName MediumType = TEXT("Smoke");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "uBot|Medium")
    bool bActive = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "uBot|Medium")
    FLinearColor Tint = FLinearColor(0.55f, 0.55f, 0.55f, 1.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "uBot|Medium")
    TArray<FUBotMediumCell> Cells;

    /**
     * Not a UPROPERTY: computed by UBot::Medium::FinalizeSnapshot from the cells. Call it again
     * after editing Cells; stale bounds make FindFirstIntersection skip cells they do not cover.
     */
    FBox BoundsCentimeters = FBox(ForceInit);

    /** Object that produced the snapshot. CollectSnapshots fills it with the actor when left unset. */
    TWeakObjectPtr<UObject> Source;
};

UINTERFACE(BlueprintType)
class UBOTCORE_API UUBotPerceptionMedium : public UInterface
{
    GENERATED_BODY()
};

/** Implemented by actors that degrade perception (smoke, fog, ...). */
class UBOTCORE_API IUBotPerceptionMedium
{
    GENERATED_BODY()

public:
    /** Fills OutSnapshot and returns true when the medium has a snapshot to offer. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "uBot|Medium")
    bool GetMediumSnapshot(FUBotMediumSnapshot& OutSnapshot) const;
};

namespace UBot::Medium
{
    /** Density * EdgeFactor, clamped to 0..1 (negative density counts as 0, EdgeFactor is clamped to 0..1). */
    UBOTCORE_API float EffectiveDensity(const FUBotMediumCell& Cell);

    /** Recomputes BoundsCentimeters from the cells, each expanded by its effective radius. No cells -> invalid box. */
    UBOTCORE_API void FinalizeSnapshot(FUBotMediumSnapshot& Snapshot);

    /**
     * Every actor in World implementing IUBotPerceptionMedium whose snapshot is active and
     * non-empty. Snapshots are finalized. OutSnapshots is reset first.
     */
    UBOTCORE_API void CollectSnapshots(UWorld& World, TArray<FUBotMediumSnapshot>& OutSnapshots);

    /**
     * First point along [Start, End] (cm) where a cell with EffectiveDensity >= DensityThreshold is
     * entered. Returns false when nothing is hit. OutDistanceCentimeters is measured from Start and
     * is 0 when Start already lies inside a cell. Inactive snapshots are ignored; a snapshot whose
     * bounds were never finalized is tested cell by cell. Both outputs are 0 on a miss.
     */
    UBOTCORE_API bool FindFirstIntersection(const FVector& StartCentimeters, const FVector& EndCentimeters,
        const TArray<FUBotMediumSnapshot>& Snapshots, float DensityThreshold,
        float& OutDistanceCentimeters, float& OutDensity);
}
