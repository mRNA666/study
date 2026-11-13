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
	float NoiseAmplitudeCm = 12.f;

	UPROPERTY(EditAnywhere, Category = "Dig")
	float NoiseScale = 0.03f;

	/**
	 * 噪声叠几层（FBM 的八度数）。
	 * 第 1 层 = 大起伏，第 2 层 = 中棱角，第 3 层 = 8cm 级细颗粒。
	 *
	 * 现在步长是 10cm，第 3 层（~8cm 粒度）根本采样不出来，纯属白算 ——
	 * 所以用 2 层：视觉几乎无差别，成本直接省 1/3。
	 */
	UPROPERTY(EditAnywhere, Category = "Dig", meta = (ClampMin = "1", ClampMax = "4"))
	int32 NoiseOctaves = 2;

	/**
	 * 只在"朝向 NoiseDirection 的那一面"加噪声起伏，其余五面保持平整。
	 *
	 * 为什么要这个：分块拼矿洞时，如果每块六面都起伏，相邻块在交界面上
	 * 各自算出不同的高度 —— 要么露缝要么重叠错位。只让暴露面起伏，
	 * 其余五面就是纯粹的盒面（BoxDist = 0），两块能严丝合缝贴住。
	 */
	UPROPERTY(EditAnywhere, Category = "Dig")
	bool bDirectionalNoise = false;

	/** 暴露面的朝向（局部空间，填 ±X / ±Y / ±Z 之一）。配合 bDirectionalNoise 用 */
	UPROPERTY(EditAnywhere, Category = "Dig")
	FVector NoiseDirection = FVector(0.f, 0.f, 1.f);

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

	/** 每公斤满含金矿石能炼出多少克黄金（DESIGN-dig §9.5，默认 20） */
	UPROPERTY(EditAnywhere, Category = "Dig", meta = (ClampMin = "0"))
	float GramsPerKgOre = 20.f;

	/**
	 * 剩余实体的最长边小于这个值（cm）就当作已经挖空。
	 * 挖到最后会剩下一些噪声造成的孤立小碎渣，玩家打不着，表现为"永远挖不完"。
	 * 设 0 = 永远保留（调试用）。
	 *
	 * 注意别设得比 DropSizeSmallCm 还大：那样"最后一下"会直接判定为空、
	 * 连小块都不掉，小块的石头就几乎不会出现。现在 15 < 小块 20，OK。
	 */
	UPROPERTY(EditAnywhere, Category = "Dig", meta = (ClampMin = "0"))
	float DepletedSizeCm = 15.f;

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
	float DropSizeSmallCm = 20.f;

	UPROPERTY(EditAnywhere, Category = "Dig")
	float DropSizeMediumCm = 30.f;

	UPROPERTY(EditAnywhere, Category = "Dig")
	float DropSizeLargeCm = 40.f;

protected :
	/**
	 * 缓存的"本块 → 世界"变换。
	 * EvalSDF 里每个采样点都要把 P 变到世界坐标（噪声要用世界坐标才能跨块连续），
	 * 而 GetActorTransform() 每调一次都有开销 —— 一次重建要调两万多次。
	 * 所以在 RebuildMesh 开头存一份，采样时直接用。
	 */
	FTransform CachedToWorld;
	virtual void OnConstruction(const FTransform& Transform) override;
	double EvalFBM(const FVector3d& P, double Scale) const;
	double EvalSDF(const FVector3d& P) const;

	void RebuildMesh();

	/**
	 * 标记为"已挖空"：清空网格 + 关碰撞，后续挖掘直接忽略。
	 * 两种情况会调用：MC 一个三角形都没生成；剩下的连通块小于 DepletedSizeCm。
	 */
	void MarkDepleted();

	/** 已经挖空（网格为空） */
	bool bDepleted = false;

	/**
	 * 上一次保留的连通块包围盒中心。
	 * 用来稳定"哪一块是主体"：单纯选"三角形最多的"，挖的过程中会来回跳，
	 * 上一帧被删掉的碎块就会在下一帧重新出现。
	 */
	FVector LastKeptCenter = FVector::ZeroVector;
	bool bHasLastKeptCenter = false;

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
	/**
	 * 只挖，不产生掉落。返回这一块被挖掉的体积（cm³）。
	 * 一次挥击的球会切到相邻好几块，调用方要把各块的返回值加起来，
	 * 最后统一调一次 SpawnDropForRemovedVolume —— 这样一刀只掉一块石头。
	 */
	float CarveAt(const FVector& WorldLocation, float RadiusCm);

	/**
	 * 按"这一次总共挖掉了多少体积"生成一个掉落物。
	 * @param RemovedVolumeCm3  本次挥击在所有块上挖掉的体积总和
	 * @param SphereVolumeCm3   挖掘球的体积（用来算"实心比"）
	 */
	void SpawnDropForRemovedVolume(float RemovedVolumeCm3, float SphereVolumeCm3, const FVector& WorldLocation);

	/**
	 * 挖掉一个球（自己挖自己掉，单块用法）。
	 * @param bSpawnDrop  false = 只挖不掉
	 */
	void DigAt(const FVector& WorldLocation, float RadiusCm, bool bSpawnDrop = true);

	/** 估算挖掉这个球会移除多少体积（cm³）。随机撒 64 个点，用实心比例 × 球体积 */
	double EstimateRemovedVolume(const FVector3d& Center, float RadiusCm) const;

	/** 体积 → 档位。按"挖掉体积 / 整个球体积"的比例判定，见 DESIGN-dig.md §7.3 */
	EDropSize ClassifyDropSize(double RemovedVolume, double SphereVolume) const;
};
