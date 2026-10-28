#include "LadderActor.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"

ALadderActor::ALadderActor()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	// Visual mesh: no collision, the box below does the blocking.
	Frame = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Frame"));
	Frame->SetupAttachment(SceneRoot);
	Frame->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// One thin box covers the whole climbable height. Cheap and stable.
	CollisionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("CollisionBox"));
	CollisionBox->SetupAttachment(SceneRoot);
	CollisionBox->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	CollisionBox->SetCollisionResponseToAllChannels(ECR_Block);

	// The tag lets Blueprint traces find ladders too.
	Tags.Add(TEXT("Ladder"));
}

void ALadderActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// Runs in the editor as well, so the ladder resizes while you drag the slider.
	if (Frame)
	{
		Frame->SetRelativeScale3D(FVector(1.f, 1.f, Height / 300.f));
		Frame->SetRelativeLocation(FVector::ZeroVector);
	}

	if (CollisionBox)
	{
		CollisionBox->SetBoxExtent(FVector(12.f, 30.f, Height * 0.5f));
		CollisionBox->SetRelativeLocation(FVector(0.f, 0.f, Height * 0.5f));
	}
}

void ALadderActor::BeginPlay()
{
	Super::BeginPlay();
}

FVector ALadderActor::GetClimbAnchor() const
{
	// The climbable face is the -X side of the actor, matching the SM_Ladder model.
	return GetActorLocation() - GetActorForwardVector() * StandOff;
}

float ALadderActor::GetBottomZ() const
{
	return GetActorLocation().Z;
}

float ALadderActor::GetTopZ() const
{
	return GetActorLocation().Z + Height;
}
