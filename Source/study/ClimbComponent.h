// Ladder climbing: trace to find a ladder, snap onto it, move up/down, release.
// Add this component to any Character blueprint to make it climb.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ClimbComponent.generated.h"

class ACharacter;

UCLASS(ClassGroup = (Study), meta = (BlueprintSpawnableComponent))
class STUDY_API UClimbComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UClimbComponent();

	/** How far ahead the interaction trace reaches, in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climb")
	float TraceDistance = 600.f;

	/** Climb speed in cm per second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climb")
	float ClimbSpeed = 200.f;

	/** When true the component binds E itself. Set false if your Blueprint calls TryGrabLadder. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climb")
	bool bAutoBindInput = true;

	/** Trace forward, grab the ladder if one is hit. Pressing E again releases. */
	UFUNCTION(BlueprintCallable, Category = "Climb")
	bool TryGrabLadder();

	/** Leave the ladder and go back to walking. */
	UFUNCTION(BlueprintCallable, Category = "Climb")
	void ReleaseLadder();

	UFUNCTION(BlueprintPure, Category = "Climb")
	bool IsClimbing() const { return bClimbing; }

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

private:
	/** Wrapper used by BindKey, which requires a void() signature. */
	void OnInteractPressed();

	void BindInput();

	ACharacter* GetOwnerCharacter() const;

	UPROPERTY(Transient)
	TObjectPtr<AActor> Ladder = nullptr;

	bool bClimbing = false;

	/** XY the character is locked to while climbing. */
	FVector Anchor = FVector::ZeroVector;

	float BottomZ = 0.f;
	float TopZ = 0.f;
};
