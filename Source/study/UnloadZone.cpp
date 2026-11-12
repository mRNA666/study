// Fill out your copyright notice in the Description page of Project Settings.


#include "UnloadZone.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "studyCharacter.h"
#include "studyGameState.h"

AUnloadZone::AUnloadZone()
{
	PrimaryActorTick.bCanEverTick = false;

	Trigger = CreateDefaultSubobject<UBoxComponent>(TEXT("Trigger"));
	SetRootComponent(Trigger);

	Trigger->SetBoxExtent(FVector(150.f, 150.f, 100.f));

	// 触发体：只跟 Pawn 做 Overlap，不挡路
	Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Trigger->SetCollisionObjectType(ECC_WorldDynamic);
	Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Trigger->SetGenerateOverlapEvents(true);

	Marker = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Marker"));
	Marker->SetupAttachment(Trigger);
	Marker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Marker->SetGenerateOverlapEvents(false);
}

void AUnloadZone::BeginPlay()
{
	Super::BeginPlay();

	if (Trigger)
	{
		Trigger->OnComponentBeginOverlap.AddDynamic(this, &AUnloadZone::OnTriggerBeginOverlap);
	}
}

void AUnloadZone::OnTriggerBeginOverlap(UPrimitiveComponent* /*OverlappedComp*/, AActor* OtherActor,
	UPrimitiveComponent* /*OtherComp*/, int32 /*OtherBodyIndex*/, bool /*bFromSweep*/, const FHitResult& /*SweepResult*/)
{
	AstudyCharacter* Character = Cast<AstudyCharacter>(OtherActor);
	if (!Character)
	{
		return;
	}

	// 背包是空的：不重复处理（也避免刷日志）
	if (Character->GetCarriedWeightKg() <= 0.f)
	{
		return;
	}

	// 清空背包并拿到这次交回的黄金克数
	const float Delivered = Character->UnloadAll();
	if (Delivered <= 0.f)
	{
		return;
	}

	// 记到 GameState 上。
	// 现在单机，玩家序号固定 0；将来联机时改成按 PlayerId 映射。
	if (AstudyGameState* GS = GetWorld() ? GetWorld()->GetGameState<AstudyGameState>() : nullptr)
	{
		GS->AddGoldGrams(0, Delivered);
	}
}
