// A climbable ladder. Drop it in a level, set Height, done.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LadderActor.generated.h"

class UStaticMeshComponent;
class UBoxComponent;

UCLASS(Blueprintable)
class STUDY_API ALadderActor : public AActor
{
	GENERATED_BODY()

public:
	ALadderActor();

	/** Unscaled root. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ladder")
	TObjectPtr<USceneComponent> SceneRoot;

	/** Visual only, no collision. Assign SM_Ladder in the Blueprint or in level. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ladder")
	TObjectPtr<UStaticMeshComponent> Frame;

	/** Real collision: blocks the character and is the target of the interaction trace. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ladder")
	TObjectPtr<UBoxComponent> CollisionBox;

	/** Climbable height in cm. The source mesh is 300 cm tall. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ladder")
	float Height = 300.f;

	/** How far in front of the ladder the character stands, in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ladder")
	float StandOff = 30.f;

	virtual void OnConstruction(const FTransform& Transform) override;

	/** Point the character is snapped to while climbing (in front of the ladder). */
	UFUNCTION(BlueprintPure, Category = "Ladder")
	FVector GetClimbAnchor() const;

	UFUNCTION(BlueprintPure, Category = "Ladder")
	float GetBottomZ() const;

	UFUNCTION(BlueprintPure, Category = "Ladder")
	float GetTopZ() const;

protected:
	virtual void BeginPlay() override;
};
