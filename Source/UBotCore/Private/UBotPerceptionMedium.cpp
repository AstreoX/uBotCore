#include "UBotPerceptionMedium.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"

namespace UBot::Medium::Private
{
    // Must match the radius used by IntersectSegmentSphere so the bounds early-out never drops a hit.
    float EffectiveRadiusCentimeters(const FUBotMediumCell& Cell)
    {
        return FMath::Max(1.0f, Cell.RadiusCentimeters);
    }

    // Ported from AgentSensorSmoke (AgentSmokeSensorInterference.cpp). The float/double mix is kept
    // on purpose so results match the legacy implementation for equivalent input.
    bool IntersectSegmentSphere(
        const FVector& SegmentStart,
        const FVector& SegmentDirection,
        float SegmentLength,
        const FUBotMediumCell& Cell,
        float& OutDistanceCentimeters)
    {
        const FVector ToStart = SegmentStart - Cell.WorldPositionCentimeters;
        const float Radius = EffectiveRadiusCentimeters(Cell);
        const float C = static_cast<float>(FVector::DotProduct(ToStart, ToStart) - Radius * Radius);
        if (C <= 0.0f)
        {
            OutDistanceCentimeters = 0.0f;
            return true;
        }

        const float B = static_cast<float>(FVector::DotProduct(ToStart, SegmentDirection));
        if (B > 0.0f)
        {
            return false;
        }

        const float Discriminant = B * B - C;
        if (Discriminant < 0.0f)
        {
            return false;
        }

        const float Distance = -B - FMath::Sqrt(Discriminant);
        if (Distance < 0.0f || Distance > SegmentLength)
        {
            return false;
        }

        OutDistanceCentimeters = Distance;
        return true;
    }
}

namespace UBot::Medium
{
    float EffectiveDensity(const FUBotMediumCell& Cell)
    {
        // Same product as the legacy smoke cell; the outer clamp only matters for Density > 1.
        return FMath::Min(1.0f, FMath::Max(0.0f, Cell.Density) * FMath::Clamp(Cell.EdgeFactor, 0.0f, 1.0f));
    }

    void FinalizeSnapshot(FUBotMediumSnapshot& Snapshot)
    {
        Snapshot.BoundsCentimeters = FBox(ForceInit);
        for (const FUBotMediumCell& Cell : Snapshot.Cells)
        {
            const double Radius = Private::EffectiveRadiusCentimeters(Cell);
            Snapshot.BoundsCentimeters += FBox::BuildAABB(Cell.WorldPositionCentimeters, FVector(Radius));
        }
    }

    void CollectSnapshots(UWorld& World, TArray<FUBotMediumSnapshot>& OutSnapshots)
    {
        OutSnapshots.Reset();
        for (TActorIterator<AActor> ActorIt(&World); ActorIt; ++ActorIt)
        {
            AActor* Actor = *ActorIt;
            if (!IsValid(Actor)
                || Actor->IsActorBeingDestroyed()
                || !Actor->Implements<UUBotPerceptionMedium>())
            {
                continue;
            }

            FUBotMediumSnapshot Snapshot;
            if (!IUBotPerceptionMedium::Execute_GetMediumSnapshot(Actor, Snapshot)
                || !Snapshot.bActive
                || Snapshot.Cells.IsEmpty())
            {
                continue;
            }

            if (!Snapshot.Source.IsValid())
            {
                Snapshot.Source = Actor;
            }
            FinalizeSnapshot(Snapshot);
            OutSnapshots.Add(MoveTemp(Snapshot));
        }
    }

    bool FindFirstIntersection(
        const FVector& StartCentimeters,
        const FVector& EndCentimeters,
        const TArray<FUBotMediumSnapshot>& Snapshots,
        float DensityThreshold,
        float& OutDistanceCentimeters,
        float& OutDensity)
    {
        OutDistanceCentimeters = 0.0f;
        OutDensity = 0.0f;
        const FVector Segment = EndCentimeters - StartCentimeters;
        const float SegmentLength = static_cast<float>(Segment.Size());
        if (SegmentLength <= UE_SMALL_NUMBER)
        {
            return false;
        }

        const FVector SegmentDirection = Segment / SegmentLength;
        const FBox SegmentBounds(StartCentimeters.ComponentMin(EndCentimeters), StartCentimeters.ComponentMax(EndCentimeters));
        float BestDistance = TNumericLimits<float>::Max();
        float BestDensity = 0.0f;
        bool bFound = false;
        for (const FUBotMediumSnapshot& Snapshot : Snapshots)
        {
            if (!Snapshot.bActive)
            {
                continue;
            }
            // The bounds are only an early-out, so a snapshot that was never finalized is still tested.
            if (Snapshot.BoundsCentimeters.IsValid && !Snapshot.BoundsCentimeters.Intersect(SegmentBounds))
            {
                continue;
            }

            for (const FUBotMediumCell& Cell : Snapshot.Cells)
            {
                const float CellDensity = EffectiveDensity(Cell);
                if (CellDensity < DensityThreshold)
                {
                    continue;
                }

                float Distance = 0.0f;
                if (Private::IntersectSegmentSphere(StartCentimeters, SegmentDirection, SegmentLength, Cell, Distance)
                    && Distance < BestDistance)
                {
                    BestDistance = Distance;
                    BestDensity = CellDensity;
                    bFound = true;
                }
            }
        }

        if (!bFound)
        {
            return false;
        }

        OutDistanceCentimeters = BestDistance;
        OutDensity = BestDensity;
        return true;
    }
}
