// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DropItem.generated.h"

class UStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;

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

	/** 这块石头值多少黄金（克）。生成时就算好，卸货时直接累加，避免各处在重复算公式 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Drop")
	float GoldGrams = 0.0f;

	/** 是不是纯石渣（含金量太低，捡了只占重量） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Drop")
	bool bIsRubble = false;

	/** 废石多少秒后自动消失（含金矿不受影响，永久留在地上） */
	UPROPERTY(EditAnywhere, Category = "Drop")
	float RubbleLifeSpan = 30.f;

	/**
	 * 这块石头用什么材质。留空则用模型自带的（SM_Rock_A~D 里配的 M_Nat_Stone）。
	 * 正常流程是由 ADigVolume 按"这座矿洞是什么岩层"传进来。
	 */
	UPROPERTY(EditAnywhere, Category = "Drop")
	TObjectPtr<UMaterialInterface> RockMaterial;

	/**
	 * 含金系数 → 档位词（石渣 / 微 / 中 / 富）。
	 *
	 * 为什么不直接报百分比：DESIGN-dig §9.6 —— 保住「挖出来才知道」的赌感；
	 * 报精确数字玩家会去算期望，赌局就变成了算术题。
	 *
	 * 阈值按当前分布标定（不是旧的 2%~20%）：
	 *   石渣：无金    微：1%~3%（约 55%）    中：3%~8%（约 36%）    富：8% 以上（约 10%）
	 */
	UFUNCTION(BlueprintPure, Category = "Drop")
	static FString GoldGradeWord(float InGoldContent);

	/**
	 * 初始化一块掉落的石头。
	 * @param InWeightKg      重量（kg）。同时作为物理质量，越重越难被踢飞
	 * @param InGoldContent   含金系数 0~1
	 * @param TargetSizeCm    目标尺寸（最长边，厘米）。不管源模型多大都会缩放到这个尺寸
	 * @param InMaterial      材质（可选）。传 nullptr 就沿用模型自带的材质
	 * @param InGoldGrams     这块石头值多少黄金（克）。由 ADigVolume 按公式算好传进来
	 */
	void InitDrop(float InWeightKg, float InGoldContent, float TargetSizeCm,
		UMaterialInterface* InMaterial = nullptr, float InGoldGrams = 0.0f);
};
