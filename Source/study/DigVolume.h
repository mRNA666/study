// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Generators/MarchingCubes.h"
#include "DigVolume.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogDig, Log, All);

class UDynamicMeshComponent;
class UMaterialInterface;
class ADropItem;

USTRUCT(BlueprintType)
struct FDigHole
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Dig")
	FVector Center = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = "Dig")
	float RadiusCm = 20.f;
};

UCLASS()
class STUDY_API ADigVolume : public AActor
{
	GENERATED_BODY()
	
public:	
	ADigVolume();
	
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category = "Dig")
	TObjectPtr<UDynamicMeshComponent> MeshComp;//组件成员

	/**
	 * 岩石材质。设了就会套到网格组件上；留空则显示引擎默认材质（灰格子）。
	 *
	 * 为什么必须有这个属性：UDynamicMeshComponent 的材质存在内部 BaseMaterials 数组里，
	 * 那个成员只标了 UPROPERTY()，没有 EditAnywhere —— 所以细节面板里没有材质槽，
	 * 视口里也拖不上去。只能由代码 SetMaterial 设置。
	 */
	UPROPERTY(EditAnywhere, Category = "Dig")
	TObjectPtr<UMaterialInterface> RockMaterial;

	UPROPERTY(EditAnywhere,Category = "Dig")
	float SizeCm = 100.0f;

	UPROPERTY(EditAnywhere, Category = "Dig")
	float SampleStepCm = 4.f;

	UPROPERTY(EditAnywhere, Category = "Dig")
	float NoiseAmplitudeCm = 6.f;

	UPROPERTY(EditAnywhere, Category = "Dig")
	float NoiseScale = 0.03f;

	UPROPERTY(EditAnywhere, Category = "Dig")
	TArray<FDigHole> DigHoles;

	// ---------------- 挖矿产出（DESIGN-dig §9） ----------------
	// 每一下挖成功都单独掷骰：出矿 or 出废石；出矿时含金系数再随机。
	// 玩家看不见这些数，只能靠"挖十下出了几个矿"自己推断这块肥不肥。

	/** 这一下出「矿石」的概率 0~1。玩家看不见 */
	UPROPERTY(EditAnywhere, Category = "Dig", meta = (ClampMin = "0", ClampMax = "1"))
	float OreChance = 0.35f;

	/** 出矿时含金系数的下限 */
	UPROPERTY(EditAnywhere, Category = "Dig", meta = (ClampMin = "0", ClampMax = "1"))
	float MinGrade = 0.02f;

	/** 出矿时含金系数的上限 */
	UPROPERTY(EditAnywhere, Category = "Dig", meta = (ClampMin = "0", ClampMax = "1"))
	float MaxGrade = 0.20f;

	/** 分布偏斜：>1 = 低含金更多（更赌），<1 = 高含金更多 */
	UPROPERTY(EditAnywhere, Category = "Dig", meta = (ClampMin = "0.1", ClampMax = "8"))
	float GradeSkew = 2.5f;

	/**
	 * 岩层倍率（PRD §2.12）：土/砂砾 ×1、砂岩 ×2.5、页岩 ×5、花岗岩 ×9。
	 * 只影响"这块石头值多少黄金"，不影响掉落概率和重量。
	 */
	UPROPERTY(EditAnywhere, Category = "Dig", meta = (ClampMin = "0"))
	float RockMultiplier = 1.0f;

	/** 挖下来的石头用哪个 Actor 类生成。留空则只打日志、不生成实物 */
	UPROPERTY(EditAnywhere, Category = "Dig")
	TSubclassOf<ADropItem> DropItemClass;

	/** 矿石外观：这一下掷骰出了矿时用（DESIGN-dig §9.4） */
	UPROPERTY(EditAnywhere, Category = "Dig")
	TObjectPtr<UMaterialInterface> OreMaterial;

	/** 废石外观：这一下没出矿时用 */
	UPROPERTY(EditAnywhere, Category = "Dig")
	TObjectPtr<UMaterialInterface> RubbleMaterial;

	// 掉落物目标尺寸（最长边，厘米）。洞口直径是 40cm，石头要比它小。
	UPROPERTY(EditAnywhere, Category = "Dig")
	float DropSizeSmallCm = 12.f;

	UPROPERTY(EditAnywhere, Category = "Dig")
	float DropSizeMediumCm = 22.f;

	UPROPERTY(EditAnywhere, Category = "Dig")
	float DropSizeLargeCm = 32.f;

protected :
	virtual void OnConstruction(const FTransform& Transform) override;
	double EvalFBM(const FVector3d& P, double Scale) const;
	double EvalSDF(const FVector3d& P) const;

	void RebuildMesh();

	/** 真正做物理烘焙（很贵，约 30ms@3cm），由计时器延迟调用，避免卡在点击那一帧 */
	void FlushPhysics();

	/** 延迟物理烘焙的计时器，每挖一次就重置 */
	FTimerHandle PhysicsTimerHandle;

	/** 复用的 MC 生成器：每次 Reset() 只清空不释放，内部数组容量得以保留，省掉反复分配 */
	UE::Geometry::FMarchingCubes MarchingCubes;

public:
	/** 掉落物档位（由这一下挖掉的体积决定） */
	enum class EDropSize : uint8
	{
		None,     // 敲空气，什么都不掉
		Small,    // 小块
		Medium,   // 中块
		Large     // 大块
	};

	/** 在世界坐标 WorldLocation 处挖一个半径 RadiusCm 的球洞，并重建网格 */
	void DigAt(const FVector& WorldLocation, float RadiusCm);

	/** 估算挖掉这个球会移除多少体积（cm³）。随机撒 64 个点，用实心比例 × 球体积 */
	double EstimateRemovedVolume(const FVector3d& Center, float RadiusCm) const;

	/** 体积 → 档位。按"挖掉体积 / 整个球体积"的比例判定，见 DESIGN-dig.md §7.3 */
	EDropSize ClassifyDropSize(double RemovedVolume, double SphereVolume) const;
};
