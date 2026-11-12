// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UnloadZone.generated.h"

class UBoxComponent;
class UStaticMeshComponent;

/**
 * 卸货区：玩家背着石头走进来，自动把背包清空、黄金记到账上。
 *
 * 放在 GameMode/GameState 之外做成独立 Actor，是因为"基地在哪"是关卡数据，
 * 不是规则 —— 同一个 GameMode 可以配不同摆放的关卡。
 */
UCLASS()
class STUDY_API AUnloadZone : public AActor
{
	GENERATED_BODY()

public:
	AUnloadZone();

	/** 触发体积（默认 300×300×200 cm，可调） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Unload")
	TObjectPtr<UBoxComponent> Trigger;

	/** 可选：地面标记用的网格（比如一块平台/旗子）。留空则看不见，只靠触发器 */
	UPROPERTY(EditAnywhere, Category = "Unload")
	TObjectPtr<UStaticMeshComponent> Marker;

protected:
	virtual void BeginPlay() override;

	/** 玩家进入 → 卸货 */
	UFUNCTION()
	void OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);
};
