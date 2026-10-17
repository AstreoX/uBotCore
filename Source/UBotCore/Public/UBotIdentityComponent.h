#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "UBotIdentityComponent.generated.h"

class AActor;
class USceneComponent;

/**
 * Names a robot for every uBot consumer (ROS bridge, MCP, tools). All fields are optional; the
 * resolved getters apply the fallbacks. Names come from AActor::GetName(), never the editor
 * label, so results are identical in packaged builds.
 */
UCLASS(ClassGroup = (uBot), meta = (BlueprintSpawnableComponent, DisplayName = "uBot Identity"))
class UBOTCORE_API UUBotIdentityComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UUBotIdentityComponent();

    /** Empty -> owner actor name. Surrounding whitespace is ignored. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
    FString RobotId;

    /** Empty -> sanitized robot id. Surrounding whitespace and leading/trailing '/' are ignored. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
    FString Namespace;

    /** Empty -> "base_link". */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
    FString BaseFrameId = TEXT("base_link");

    /** Empty, unresolved or not a scene component -> owner root component. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity", meta = (UseComponentPicker, AllowedClasses = "/Script/Engine.SceneComponent"))
    FComponentReference BaseComponent;

    /** RobotId, or the owner actor name when empty (empty if there is no owner). */
    UFUNCTION(BlueprintPure, Category = "uBot|Identity")
    FString GetResolvedRobotId() const;

    /** Namespace, or UBot::Identity::SanitizeNameToken(GetResolvedRobotId()) when empty. */
    UFUNCTION(BlueprintPure, Category = "uBot|Identity")
    FString GetResolvedNamespace() const;

    /** BaseFrameId, or "base_link" when empty. */
    UFUNCTION(BlueprintPure, Category = "uBot|Identity")
    FString GetBaseFrameId() const;

    /** The referenced scene component, or the owner root component. May be nullptr. */
    UFUNCTION(BlueprintPure, Category = "uBot|Identity")
    USceneComponent* GetBaseComponent() const;
};

namespace UBot::Identity
{
    /**
     * Portable name token: keeps [A-Za-z0-9_], replaces every other character by '_', collapses repeated
     * underscores, prefixes '_' when the first character is a digit and never returns an empty string
     * (fallback "ubot"). Valid as a ROS name token, a C identifier and a file name.
     */
    UBOTCORE_API FString SanitizeNameToken(const FString& Name);

    /** The actor's identity component, or nullptr. */
    UBOTCORE_API UUBotIdentityComponent* Find(const AActor* Actor);

    // Same resolution rules as the component, with fallbacks when the actor has none.
    // A null actor yields an empty robot id, SanitizeNameToken("") as namespace, "base_link" and nullptr.

    /** Identity -> actor name. */
    UBOTCORE_API FString ResolveRobotId(const AActor* Actor);
    /** Identity -> SanitizeNameToken(robot id). */
    UBOTCORE_API FString ResolveNamespace(const AActor* Actor);
    /** Identity -> "base_link". */
    UBOTCORE_API FString ResolveBaseFrameId(const AActor* Actor);
    /** Identity -> root component. */
    UBOTCORE_API USceneComponent* ResolveBaseComponent(const AActor* Actor);
}
