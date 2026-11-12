// Fill out your copyright notice in the Description page of Project Settings.


#include "MineGenerator.h"
#include "DigVolume.h"
#include "DropItem.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"

AMineGenerator::AMineGenerator()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	// 内部空间 8 米见方（4 个 2 米的块）× 4 米高（2 层）；
	// 墙厚 2 圈 = 4 米。想更大就调 RoomSizeCm，但块数会按平方涨。
	RoomSizeCm = 800.f;
	BlockSizeCm = 200.f;
	WallLayers = 2;          // 两层石头高 = 4 米活动空间
	bAddCeiling = true;
	BlockSampleStepCm = 10.f;
	BlocksPerFrame = 2;
}

void AMineGenerator::BeginPlay()
{
	Super::BeginPlay();

	// 掉落物默认指向蓝图子类（和 DigVolume 用同一个）
	if (!DropItemClass)
	{
		if (UClass* Found = LoadClass<ADropItem>(nullptr, TEXT("/Game/Drop/BP_DropItem.BP_DropItem_C")))
		{
			DropItemClass = Found;
		}
	}

	// 材质：代码生成的 Actor 没法在编辑器里指定，所以这里按内容路径兜底加载。
	// 想换材质就在蓝图子类里覆盖这些属性。
	if (!RockMaterial)
	{
		// 岩层：目前矿洞只做砂岩一种（PRD §2.12 的四档岩层以后再加）
		RockMaterial = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Game/Rock/Texture/M_Rock_Sanst.M_Rock_Sanst"));
	}
	if (!OreMaterial)
	{
		OreMaterial = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Game/Rock/Texture/M_Rock_Ore.M_Rock_Ore"));
	}
	if (!RubbleMaterial)
	{
		RubbleMaterial = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Game/Rock/Texture/M_Rock_Rubble.M_Rock_Rubble"));
	}

	if (!RockMaterial || !OreMaterial || !RubbleMaterial)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Mine] 材质没加载全：Rock=%d Ore=%d Rubble=%d"),
			RockMaterial ? 1 : 0, OreMaterial ? 1 : 0, RubbleMaterial ? 1 : 0);
	}

	BuildBlockList();
	BuildOuterShell();

	UE_LOG(LogTemp, Log, TEXT("[Mine] 准备生成 %d 块（房间 %.0fcm / 块 %.0fcm / 步长 %.0fcm）"),
		PendingBlocks.Num(), RoomSizeCm, BlockSizeCm, BlockSampleStepCm);
}

void AMineGenerator::BuildBlockList()
{
	PendingBlocks.Reset();
	NextBlockIndex = 0;

	const float B = FMath::Max(BlockSizeCm, 10.f);

	// 内部空间占多少格
	const int32 InnerCells = FMath::Max(FMath::FloorToInt(RoomSizeCm / B), 2);
	// 墙厚几格（最少 1）
	const int32 Thick = FMath::Max(WallThicknessBlocks, 1);
	// 整个网格 = 内部 + 两侧墙厚
	const int32 NumPerSide = InnerCells + Thick * 2;
	const float Half = (static_cast<float>(NumPerSide) * B) * 0.5f;
	const FVector Base = GetActorLocation();
	const int32 LayerCount = FMath::Max(WallLayers, 1);

	// 某个格子离外边界有几格（0 = 最外圈）
	auto RingIndex = [NumPerSide](int32 ix, int32 iy) -> int32
	{
		return FMath::Min(FMath::Min(ix, iy), FMath::Min(NumPerSide - 1 - ix, NumPerSide - 1 - iy));
	};

	// ---- 底面：每块的中心在 z = -B/2，所以顶面正好在 z = 0 ----
	for (int32 ix = 0; ix < NumPerSide; ++ix)
	{
		for (int32 iy = 0; iy < NumPerSide; ++iy)
		{
			const float X = -Half + B * 0.5f + ix * B;
			const float Y = -Half + B * 0.5f + iy * B;

			FBlockSpec Spec;
			Spec.Location = Base + FVector(X, Y, -B * 0.5f);
			Spec.ExposedDir = FVector(0.f, 0.f, 1.f);		// 地板：朝上
			PendingBlocks.Add(Spec);
		}
	}

	// ---- 四面墙：从最外圈往里数 Thick 圈 ----
	for (int32 Layer = 0; Layer < LayerCount; ++Layer)
	{
		const float Z = B * 0.5f + Layer * B;
		for (int32 ix = 0; ix < NumPerSide; ++ix)
		{
			for (int32 iy = 0; iy < NumPerSide; ++iy)
			{
				if (RingIndex(ix, iy) >= Thick)
				{
					continue;	// 这格已经在内部空间里，不是墙
				}

				const float X = -Half + B * 0.5f + ix * B;
				const float Y = -Half + B * 0.5f + iy * B;

				// 暴露方向 = 朝向房间内侧的那一面
				// （厚的几圈里，暴露方向由"离哪边最近"决定；内圈贴不到空腔，
				//   但保留同一个规则，让它和外面那圈一致，挖起来不会突兀）
				FVector Dir;
				if (ix <= iy && ix <= NumPerSide - 1 - ix && ix <= NumPerSide - 1 - iy)
				{
					Dir = FVector(1.f, 0.f, 0.f);
				}
				else if (iy <= NumPerSide - 1 - ix && iy <= NumPerSide - 1 - iy)
				{
					Dir = FVector(0.f, 1.f, 0.f);
				}
				else if (NumPerSide - 1 - ix <= NumPerSide - 1 - iy)
				{
					Dir = FVector(-1.f, 0.f, 0.f);
				}
				else
				{
					Dir = FVector(0.f, -1.f, 0.f);
				}

				FBlockSpec Spec;
				Spec.Location = Base + FVector(X, Y, Z);
				Spec.ExposedDir = Dir;
				PendingBlocks.Add(Spec);
			}
		}
	}

	// ---- 顶面：铺满整个网格 ----
	// 之前只铺"内部 + 最内一圈"，结果最外圈墙头上是空的，从洞里往上能看见一圈缺口。
	if (bAddCeiling)
	{
		const float Z = B * 0.5f + LayerCount * B;
		for (int32 ix = 0; ix < NumPerSide; ++ix)
		{
			for (int32 iy = 0; iy < NumPerSide; ++iy)
			{
				const float X = -Half + B * 0.5f + ix * B;
				const float Y = -Half + B * 0.5f + iy * B;

				FBlockSpec Spec;
				Spec.Location = Base + FVector(X, Y, Z);
				Spec.ExposedDir = FVector(0.f, 0.f, -1.f);		// 天花板：朝下
				PendingBlocks.Add(Spec);
			}
		}
	}
}

void AMineGenerator::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (NextBlockIndex >= PendingBlocks.Num())
	{
		return;
	}

	const int32 Count = FMath::Max(BlocksPerFrame, 1);
	for (int32 i = 0; i < Count && NextBlockIndex < PendingBlocks.Num(); ++i, ++NextBlockIndex)
	{
		SpawnBlock(PendingBlocks[NextBlockIndex]);
	}

	if (NextBlockIndex >= PendingBlocks.Num())
	{
		UE_LOG(LogTemp, Log, TEXT("[Mine] 生成完毕：%d 块"), PendingBlocks.Num());
		SetActorTickEnabled(false);
	}
}

void AMineGenerator::SpawnBlock(const FBlockSpec& Spec)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FTransform Xf(FRotator::ZeroRotator, Spec.Location);

	// 必须用 SpawnActorDeferred：DigVolume 在 OnConstruction 里就建网格，
	// 如果先 Spawn 再改参数，网格已经按默认值建好了。
	ADigVolume* Vol = World->SpawnActorDeferred<ADigVolume>(
		ADigVolume::StaticClass(), Xf, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Vol)
	{
		return;
	}

	Vol->SizeCm = BlockSizeCm + BlockOverlapCm;
	Vol->SampleStepCm = BlockSampleStepCm;

	// 只让朝空腔的那一面起伏，其余五面平整 —— 这样相邻块能严丝合缝贴住
	Vol->bDirectionalNoise = true;
	Vol->NoiseDirection = Spec.ExposedDir;

	Vol->RockMaterial = RockMaterial;
	Vol->DropItemClass = DropItemClass;
	Vol->OreMaterial = OreMaterial;
	Vol->RubbleMaterial = RubbleMaterial;

	Vol->FinishSpawning(Xf);
}

void AMineGenerator::BuildOuterShell()
{
	UWorld* World = GetWorld();
	if (!World || !RockMaterial)
	{
		return;
	}

	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!CubeMesh)
	{
		return;
	}

	const float B = FMath::Max(BlockSizeCm, 10.f);
	const int32 InnerCells = FMath::Max(FMath::FloorToInt(RoomSizeCm / B), 2);
	const int32 Thick = FMath::Max(WallThicknessBlocks, 1);
	const int32 NumPerSide = InnerCells + Thick * 2;
	const float Half = (static_cast<float>(NumPerSide) * B) * 0.5f;

	const float SlabThick = 50.f;		// 外壳板厚
	const float InnerPad = 5.f;			// 离石头表面留 5cm，避免和块面 z-fighting
	const float CenterOffset = Half + InnerPad + SlabThick * 0.5f;

	const float CeilTop = B * 0.5f + FMath::Max(WallLayers, 1) * B + B * 0.5f;
	const float BottomZ = -B - SlabThick;
	const float TopZ = CeilTop + SlabThick;
	const float Height = TopZ - BottomZ;
	const float CenterZ = (TopZ + BottomZ) * 0.5f;
	const float Side = 2.f * CenterOffset;

	// 立方体原始边长 100cm → 缩放 = 目标尺寸 / 100
	struct FSlabSpec
	{
		FVector Offset;
		FVector SizeCm;
	};

	TArray<FSlabSpec> Slabs;
	Slabs.Add({ FVector( CenterOffset, 0.f, CenterZ), FVector(SlabThick, Side, Height) });	// +X
	Slabs.Add({ FVector(-CenterOffset, 0.f, CenterZ), FVector(SlabThick, Side, Height) });	// -X
	Slabs.Add({ FVector(0.f,  CenterOffset, CenterZ), FVector(Side, SlabThick, Height) });	// +Y
	Slabs.Add({ FVector(0.f, -CenterOffset, CenterZ), FVector(Side, SlabThick, Height) });	// -Y
	Slabs.Add({ FVector(0.f, 0.f, BottomZ + SlabThick * 0.5f), FVector(Side, Side, SlabThick) });	// 底
	Slabs.Add({ FVector(0.f, 0.f, TopZ - SlabThick * 0.5f), FVector(Side, Side, SlabThick) });		// 顶

	const FVector Base = GetActorLocation();

	for (const FSlabSpec& Spec : Slabs)
	{
		const FTransform Xf(FRotator::ZeroRotator, Base + Spec.Offset, Spec.SizeCm / 100.f);

		AStaticMeshActor* Slab = World->SpawnActorDeferred<AStaticMeshActor>(
			AStaticMeshActor::StaticClass(), Xf, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Slab)
		{
			continue;
		}

		if (UStaticMeshComponent* Comp = Slab->GetStaticMeshComponent())
		{
			// 运行时生成的组件必须是 Movable，静态光照在运行时不允许创建
			Comp->SetMobility(EComponentMobility::Movable);
			Comp->SetStaticMesh(CubeMesh);

			// 和矿洞的石头用同一套材质，挖穿时看到的是岩石而不是虚空
			Comp->SetMaterial(0, RockMaterial);
		}

		Slab->FinishSpawning(Xf);
	}

	UE_LOG(LogTemp, Log, TEXT("[Mine] 外围空气墙已生成（%d 块，材质=%s）"),
		Slabs.Num(), *GetNameSafe(RockMaterial));
}

float AMineGenerator::GetBuildProgress() const
{
	if (PendingBlocks.Num() <= 0)
	{
		return 1.f;
	}
	return FMath::Clamp(static_cast<float>(NextBlockIndex) / static_cast<float>(PendingBlocks.Num()), 0.f, 1.f);
}
