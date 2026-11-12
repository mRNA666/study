// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MineGenerator.generated.h"

class ADigVolume;
class ADropItem;
class UMaterialInterface;

/**
 * 程序生成矿洞。
 *
 * 做法：房间是一个空腔，底面 / 四面墙 / 顶面都铺满可挖的 ADigVolume 块。
 * 玩家挖的就是这些块，挖穿了就是洞。
 *
 * 为什么块是 2 米、采样步长是 10 厘米（而不是 1 米 / 4 厘米）：
 *   DigVolume 的成本 ≈ (边长/步长)²。1m@4cm 一块约 12ms，一个 16×16m 的洞要上百块，
 *   一次生成要好几秒。2m@10cm 一块约 6ms、数量少一半，总成本降到能接受的范围。
 *   代价是挖出来的洞边缘更"方"，但对矿洞这种大表面反而无所谓。
 *
 * 生成是**分帧**的（每帧只生成几块），避免进场卡死。
 */
UCLASS()
class STUDY_API AMineGenerator : public AActor
{
	GENERATED_BODY()

public:
	AMineGenerator();

	virtual void Tick(float DeltaSeconds) override;

	// ---------------- 尺寸 ----------------

	/** 矿洞内部边长（cm）。1600 = 16 米见方 */
	UPROPERTY(EditAnywhere, Category = "Mine")
	float RoomSizeCm = 1600.f;

	/** 可挖块的边长（cm） */
	UPROPERTY(EditAnywhere, Category = "Mine")
	float BlockSizeCm = 200.f;

	/**
	 * 块间重叠量（cm）。相邻块中心间距仍是 BlockSizeCm，
	 * 但每块的实际尺寸会加上这个值 —— 让它们互相插进对方身体里。
	 * 这样即使表面还有毫米级的误差，也会被重叠吃掉，看不到缝。
	 */
	UPROPERTY(EditAnywhere, Category = "Mine")
	float BlockOverlapCm = 10.f;

	/** 墙的【垂直】层数（每层 2 米）。2 = 4 米高的活动空间 */
	UPROPERTY(EditAnywhere, Category = "Mine")
	int32 WallLayers = 2;

	/**
	 * 墙的【水平厚度】，单位是块数（每块 2 米）。
	 * 1 = 只有最外一圈（2 米厚，挖约 45 下就穿到虚空）
	 * 2 = 两圈（4 米厚，约 90 下）
	 * 注意：加厚会把整个房间撑大，块数涨得很快 —— 多一圈，四周都要多一圈块。
	 */
	UPROPERTY(EditAnywhere, Category = "Mine", meta = (ClampMin = "1"))
	int32 WallThicknessBlocks = 2;

	/** 是否加顶（关掉就是露天矿坑） */
	UPROPERTY(EditAnywhere, Category = "Mine")
	bool bAddCeiling = true;

	// ---------------- 性能 ----------------

	/** 每个块的采样步长（cm）。越大越省、洞越方 */
	UPROPERTY(EditAnywhere, Category = "Mine")
	float BlockSampleStepCm = 10.f;

	/** 每帧最多生成几块 */
	UPROPERTY(EditAnywhere, Category = "Mine")
	int32 BlocksPerFrame = 2;

	// ---------------- 内容 ----------------

	/** 挖出来的石头用什么类生成（留空则只打日志） */
	UPROPERTY(EditAnywhere, Category = "Mine")
	TSubclassOf<ADropItem> DropItemClass;

	/** 墙体材质（三平面岩石） */
	UPROPERTY(EditAnywhere, Category = "Mine")
	TObjectPtr<UMaterialInterface> RockMaterial;

	/** 掉落物：矿石 / 废石 */
	UPROPERTY(EditAnywhere, Category = "Mine")
	TObjectPtr<UMaterialInterface> OreMaterial;

	UPROPERTY(EditAnywhere, Category = "Mine")
	TObjectPtr<UMaterialInterface> RubbleMaterial;

	/** 已经生成了多少块 / 总共多少块（蓝图/日志用） */
	UFUNCTION(BlueprintPure, Category = "Mine")
	float GetBuildProgress() const;

protected:
	virtual void BeginPlay() override;

private:
	/** 一块的摆放信息：位置 + 哪一面朝空腔 */
	struct FBlockSpec
	{
		FVector Location = FVector::ZeroVector;
		FVector ExposedDir = FVector(0.f, 0.f, 1.f);
	};

	/** 所有待生成的块 */
	TArray<FBlockSpec> PendingBlocks;

	int32 NextBlockIndex = 0;

	/** 把房间拆成一堆块 */
	void BuildBlockList();

	/**
	 * 外围空气墙：6 块薄板把整个矿洞包起来，贴同一套岩石材质。
	 * 作用两个：
	 *   ① 挖穿墙之后看到的是岩石，不是虚空
	 *   ② 挡住玩家，不许跑出矿洞
	 */
	void BuildOuterShell();

	/** 生成一块 */
	void SpawnBlock(const FBlockSpec& Spec);
};
