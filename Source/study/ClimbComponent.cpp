#include "ClimbComponent.h"

#include "CollisionShape.h"
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
			// 关键：默认 bConsumeInput = true，会把 E 键"吃掉"，
			// 导致角色身上的其它 E 绑定（比如按 E 捡石头）收不到事件。
			// 这里设成 false，让事件继续往下传。
			FInputKeyBinding& KeyBinding =
				PC->InputComponent->BindKey(EKeys::E, IE_Pressed, this, &UClimbComponent::OnInteractPressed);
			KeyBinding.bConsumeInput = false;

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
	AActor* HitActor = nullptr;

	// Look trace first: what the camera is aiming at.
	if (GetWorld()->LineTraceSingleByChannel(Hit, ViewLocation, End, ECC_Visibility, Params))
	{
		HitActor = Hit.GetActor();
	}

	// Standing on top of a ladder, the view trace usually hits the floor above it.
	// Sweep a sphere down around the character and look for a ladder there.
	if (!HitActor)
	{
		TArray<FHitResult> Nearby;
		const FVector ScanStart = Char->GetActorLocation() + FVector(0.f, 0.f, 20.f);
		const FVector ScanEnd = ScanStart - FVector(0.f, 0.f, 250.f);
		GetWorld()->SweepMultiByChannel(Nearby, ScanStart, ScanEnd, FQuat::Identity,
			ECC_Visibility, FCollisionShape::MakeSphere(GrabScanRadius), Params);

		for (const FHitResult& Candidate : Nearby)
		{
			AActor* CandidateActor = Candidate.GetActor();
			if (CandidateActor &&
				(Cast<ALadderActor>(CandidateActor) != nullptr ||
					CandidateActor->ActorHasTag(TEXT("Ladder"))))
			{
				HitActor = CandidateActor;
				UE_LOG(LogTemp, Warning, TEXT("[Climb] found ladder by nearby scan: %s"),
					*HitActor->GetName());
				break;
			}
		}
	}

	if (!HitActor)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Climb] no ladder in sight or reach"));
		return false;
	}
	const bool bIsLadder = HitActor &&
		(Cast<ALadderActor>(HitActor) != nullptr || HitActor->ActorHasTag(TEXT("Ladder")));

	if (!bIsLadder)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Climb] hit %s, not a ladder"), *GetNameSafe(HitActor));
		return false;
	}

	Ladder = HitActor;

	// Ask the ladder for its own numbers when possible, fall back to the actor transform.
	// Keep the capsule clear of the ladder's own collision box.
	float StandOff = GrabStandOff;
	if (ALadderActor* LadderActor = Cast<ALadderActor>(HitActor))
	{
		StandOff = FMath::Max(LadderActor->StandOff, GrabStandOff);
		BottomZ = LadderActor->GetBottomZ();
		TopZ = LadderActor->GetTopZ();
	}
	else
	{
		BottomZ = HitActor->GetActorLocation().Z;
		TopZ = BottomZ + 300.f;
	}
	Anchor = HitActor->GetActorLocation() - HitActor->GetActorForwardVector() * StandOff;

	// Keep the capsule centre inside the ladder range so feet stay between bottom and top.
	const float HalfHeight = Char->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	BottomZ += HalfHeight;
	TopZ += HalfHeight;

	// Gravity off: Flying has no gravity and no ground friction.
	Char->GetCharacterMovement()->SetMovementMode(MOVE_Flying);

	// Remember where the top of the ladder is in XY, so we can slide onto it.
	LadderTopLocation = HitActor->GetActorLocation();
	LadderTopLocation.Z = TopZ;

	// Face the same way as the ladder, and point the camera at it too.
	Char->SetActorRotation(HitActor->GetActorRotation());
	PC->SetControlRotation(HitActor->GetActorRotation());

	// Grab from the top: start a bit below the top so up and down both work.
	const float CurrentZ = Char->GetActorLocation().Z;
	float StartZ = FMath::Clamp(CurrentZ, BottomZ, TopZ);
	if (CurrentZ >= TopZ - 5.f)
	{
		StartZ = FMath::Max(BottomZ, TopZ - GrabEntryDrop);
	}
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
	if (PC && bLockViewWhileClimbing && Ladder)
	{
		PC->SetControlRotation(Ladder->GetActorRotation());
	}
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

	// The template keeps feeding WASD into the movement component. Clearing it every
	// frame is what removes the "slippery / sliding down" feeling while attached.
	Owner->GetCharacterMovement()->StopMovementImmediately();

	// Only Z moves. X and Y stay pinned to the anchor plane: that is the real "sticking".
	const float NewZ = FMath::Clamp(Owner->GetActorLocation().Z + Axis * ClimbSpeed * DeltaTime,
		BottomZ, TopZ);

	// In the last VaultBlendHeight centimetres, slide from the climb anchor onto the
	// ladder's own footprint. No teleport: the character drifts forward while rising.
	FVector Target(Anchor.X, Anchor.Y, NewZ);
	const float BlendStart = TopZ - FMath::Max(VaultBlendHeight, 1.f);
	if (NewZ > BlendStart)
	{
		const float Alpha = FMath::Clamp((NewZ - BlendStart) / FMath::Max(VaultBlendHeight, 1.f), 0.f, 1.f);
		Target.X = FMath::Lerp(Anchor.X, LadderTopLocation.X, Alpha);
		Target.Y = FMath::Lerp(Anchor.Y, LadderTopLocation.Y, Alpha);
	}

	Owner->SetActorLocation(Target, false, nullptr, ETeleportType::TeleportPhysics);

	// Feet are level with the top: stop climbing and let the character stand/fall naturally.
	if (Axis > 0.f && NewZ >= TopZ - 1.f)
	{
		ReleaseLadder();
		UE_LOG(LogTemp, Warning, TEXT("[Climb] reached the top"));
		return;
	}

	// Feet are level with the bottom while still pushing down: step off the ladder.
	if (Axis < 0.f && NewZ <= BottomZ + 1.f)
	{
		ReleaseLadder();
		UE_LOG(LogTemp, Warning, TEXT("[Climb] reached the bottom"));
		return;
	}
}

ACharacter* UClimbComponent::GetOwnerCharacter() const
{
	return Cast<ACharacter>(GetOwner());
}
