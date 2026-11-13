#include "LadderActor.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

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
		// 缩放按【实际网格高度】算，不再写死 300 —— 以后换模型不会被拉长或压扁。
		float SourceHeight = 300.f;
		float BottomOffset = 0.f;
		if (const UStaticMesh* LadderMesh = Frame->GetStaticMesh())
		{
			const FBoxSphereBounds Bounds = LadderMesh->GetBounds();
			SourceHeight = FMath::Max(Bounds.BoxExtent.Z * 2.f, 1.f);
			BottomOffset = Bounds.Origin.Z - Bounds.BoxExtent.Z;
		}

		const float ScaleZ = Height / SourceHeight;
		Frame->SetRelativeScale3D(FVector(1.f, 1.f, ScaleZ));
		// 把网格底面对齐到 actor 原点：pivot 本来就在底部时这一项正好是 0
		Frame->SetRelativeLocation(FVector(0.f, 0.f, -BottomOffset * ScaleZ));
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
