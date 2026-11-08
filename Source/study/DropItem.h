// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DropItem.generated.h"

class UStaticMeshComponent;
class UStaticMesh;

/**
 * 挖下来的一块石头。
 * 由 ADigVolume::DigAt 生成，带着重量和含金系数，等着玩家来捡。
 */
UCLASS()
class STUDY_API ADropItem : public AActor
{
	GENERATED_BODY()

public:
	ADropItem();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Drop")
	TObjectPtr<UStaticMeshComponent> MeshComp;

	/** 可用的碎石模型（SM_Rock_A~D 之类）。生成时随机挑一个，所以每块石头长得不一样 */
	UPROPERTY(EditAnywhere, Category = "Drop")
	TArray<TObjectPtr<UStaticMesh>> RockMeshes;

	/** 重量（kg）。由 ADigVolume 按"这一下挖掉的体积"决定 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Drop")
	float WeightKg = 0.0f;

	/** 含金系数 0~1。继承自所在的 chunk */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Drop")
	float GoldContent = 0.0f;

	/** 是不是纯石渣（含金量太低，捡了只占重量） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Drop")
	bool bIsRubble = false;

	/**
	 * 初始化一块掉落的石头。
	 * @param InWeightKg      重量（kg）。同时作为物理质量，越重越难被踢飞
	 * @param InGoldContent   含金系数 0~1
	 * @param TargetSizeCm    目标尺寸（最长边，厘米）。不管源模型多大都会缩放到这个尺寸
	 */
	void InitDrop(float InWeightKg, float InGoldContent, float TargetSizeCm);
};
