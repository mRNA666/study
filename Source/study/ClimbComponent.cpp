#include "ClimbComponent.h"

#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "LadderActor.h"
#include "TimerManager.h"

UClimbComponent::UClimbComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UClimbComponent::BeginPlay()
{
	Super::BeginPlay();

	if (bAutoBindInput)
	{
		// Wait one tick: the player controller only owns an input component after possession.
		GetWorld()->GetTimerManager().SetTimerForNextTick(this, &UClimbComponent::BindInput);
	}
}

void UClimbComponent::BindInput()
{
	ACharacter* Char = GetOwnerCharacter();
	if (!Char)
	{
		return;
	}

	if (APlayerController* PC = Cast<APlayerController>(Char->GetController()))
	{
		if (PC->InputComponent)
		{
			PC->InputComponent->BindKey(EKeys::E, IE_Pressed, this, &UClimbComponent::OnInteractPressed);
			UE_LOG(LogTemp, Log, TEXT("[Climb] E bound on %s"), *PC->GetName());
		}
	}
}

void UClimbComponent::OnInteractPressed()
{
	TryGrabLadder();
}

bool UClimbComponent::TryGrabLadder()
{
	// E works as a toggle: pressed while climbing means let go.
	if (bClimbing)
	{
		ReleaseLadder();
		return false;
	}

	ACharacter* Char = GetOwnerCharacter();
	APlayerController* PC = Char ? Cast<APlayerController>(Char->GetController()) : nullptr;
	if (!Char || !PC)
	{
		return false;
	}

	// Trace from the actual camera, not the character, so "what I look at" is what counts.
	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector End = ViewLocation + ViewRotation.Vector() * TraceDistance;

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ClimbTrace), false, Char);
	if (!GetWorld()->LineTraceSingleByChannel(Hit, ViewLocation, End, ECC_Visibility, Params))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Climb] trace hit nothing"));
		return false;
	}

	AActor* HitActor = Hit.GetActor();
	const bool bIsLadder = HitActor &&
		(Cast<ALadderActor>(HitActor) != nullptr || HitActor->ActorHasTag(TEXT("Ladder")));

	if (!bIsLadder)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Climb] hit %s, not a ladder"), *GetNameSafe(HitActor));
		return false;
	}

	Ladder = HitActor;

	// Ask the ladder for its own numbers when possible, fall back to the actor transform.
	if (ALadderActor* LadderActor = Cast<ALadderActor>(HitActor))
	{
		Anchor = LadderActor->GetClimbAnchor();
		BottomZ = LadderActor->GetBottomZ();
		TopZ = LadderActor->GetTopZ();
	}
	else
	{
		Anchor = HitActor->GetActorLocation() - HitActor->GetActorForwardVector() * 30.f;
		BottomZ = HitActor->GetActorLocation().Z;
		TopZ = BottomZ + 300.f;
	}

	// Keep the capsule centre inside the ladder range so feet stay between bottom and top.
	const float HalfHeight = Char->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	BottomZ += HalfHeight;
	TopZ += HalfHeight;

	// Gravity off: Flying has no gravity and no ground friction.
	Char->GetCharacterMovement()->SetMovementMode(MOVE_Flying);

	// Face the same way as the ladder.
	Char->SetActorRotation(HitActor->GetActorRotation());

	// Snap onto the anchor plane, keeping the current height inside the allowed range.
	const float StartZ = FMath::Clamp(Char->GetActorLocation().Z, BottomZ, TopZ);
	Char->SetActorLocation(FVector(Anchor.X, Anchor.Y, StartZ), false, nullptr,
		ETeleportType::TeleportPhysics);

	bClimbing = true;
	UE_LOG(LogTemp, Warning, TEXT("[Climb] grabbed %s"), *HitActor->GetName());
	return true;
}

void UClimbComponent::ReleaseLadder()
{
	if (!bClimbing)
	{
		return;
	}

	bClimbing = false;
	Ladder = nullptr;

	if (ACharacter* Char = GetOwnerCharacter())
	{
		Char->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	}

	UE_LOG(LogTemp, Warning, TEXT("[Climb] released"));
}

void UClimbComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bClimbing)
	{
		return;
	}

	ACharacter* Char = GetOwnerCharacter();
	if (!Char || !Ladder)
	{
		ReleaseLadder();
		return;
	}

	ACharacter* Owner = Char;
	APlayerController* PC = Cast<APlayerController>(Owner->GetController());
	float Axis = 0.f;
	if (PC)
	{
		if (PC->IsInputKeyDown(EKeys::W))
		{
			Axis += 1.f;
		}
		if (PC->IsInputKeyDown(EKeys::S))
		{
			Axis -= 1.f;
		}
		if (PC->WasInputKeyJustPressed(EKeys::SpaceBar))
		{
			ReleaseLadder();
			return;
		}
	}

	// Only Z moves. X and Y stay pinned to the anchor plane: that is the real "sticking".
	const float NewZ = FMath::Clamp(Owner->GetActorLocation().Z + Axis * ClimbSpeed * DeltaTime,
		BottomZ, TopZ);
	Owner->SetActorLocation(FVector(Anchor.X, Anchor.Y, NewZ), false, nullptr,
		ETeleportType::TeleportPhysics);
}

ACharacter* UClimbComponent::GetOwnerCharacter() const
{
	return Cast<ACharacter>(GetOwner());
}
