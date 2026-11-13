// Fill out your copyright notice in the Description page of Project Settings.


#include "MineGenerator.h"
#include "DigVolume.h"
#include "DropItem.h"
#include "LadderActor.h"
#include "UnloadZone.h"
#include "Merchant.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"

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

	if (PendingBlocks.Num() <= 0)
	{
		bBuildFinished = true;
	}
	BuildStashArea();
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

	// ---- 顶面：铺满整个网格（中间留一格给梯子） ----
	if (bAddCeiling)
	{
		const float Z = B * 0.5f + LayerCount * B;
		const int32 HoleIx = bHasStashArea ? NumPerSide / 2 : -1;
		const int32 HoleIy = HoleIx;

		for (int32 ix = 0; ix < NumPerSide; ++ix)
		{
			for (int32 iy = 0; iy < NumPerSide; ++iy)
			{
				const float X = -Half + B * 0.5f + ix * B;
				const float Y = -Half + B * 0.5f + iy * B;

				// 头顶这一格留给梯子：不铺石头，玩家从这儿爬上去
				if (ix == HoleIx && iy == HoleIy)
				{
					HoleCenter = Base + FVector(X, Y, 0.f);
					continue;
				}

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
		bBuildFinished = true;
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

	// 头顶有暂存平台时，外壳的顶板要让到平台上方
	const float TopZ = CeilTop + GetStashTopExtra() + SlabThick;
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

float AMineGenerator::GetStashTopExtra() const
{
	// 平台板厚 50 + 平台上方净空
	return bHasStashArea ? (50.f + FMath::Max(StashAlcoveHeightCm, 100.f)) : 0.f;
}

void AMineGenerator::SpawnRockSlab(const FVector& WorldOffset, const FVector& SizeCm)
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

	// 立方体原始边长 100cm → 缩放 = 目标尺寸 / 100
	const FTransform Xf(FRotator::ZeroRotator, GetActorLocation() + WorldOffset, SizeCm / 100.f);

	AStaticMeshActor* Slab = World->SpawnActorDeferred<AStaticMeshActor>(
		AStaticMeshActor::StaticClass(), Xf, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Slab)
	{
		return;
	}

	if (UStaticMeshComponent* Comp = Slab->GetStaticMeshComponent())
	{
		// 运行时生成的组件必须是 Movable
		Comp->SetMobility(EComponentMobility::Movable);
		Comp->SetStaticMesh(CubeMesh);
		Comp->SetMaterial(0, RockMaterial);
	}

	Slab->FinishSpawning(Xf);
}

void AMineGenerator::BuildStashArea()
{
	if (!bHasStashArea || !RockMaterial)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float B = FMath::Max(BlockSizeCm, 10.f);
	const float CeilTopZ = (static_cast<float>(FMath::Max(WallLayers, 1)) + 1.f) * B;
	const float PlatformThick = 50.f;
	const float PlatformTopZ = CeilTopZ + PlatformThick;
	const float HalfSize = FMath::Max(StashPlatformSizeCm, B * 2.f) * 0.5f;
	const float WallThick = 40.f;
	const float AlcoveH = FMath::Max(StashAlcoveHeightCm, 100.f);

	// HoleCenter 是世界坐标；换算成相对本 Actor 的偏移
	const FVector Local = HoleCenter - GetActorLocation();

	(void)HalfSize;

	// ---- 梯子脚下 1×1 的岩石小平台 ----
	// 盖住梯子模型底部那圈灰底座，也让梯子看起来是"插在地上"的
	SpawnRockSlab(FVector(Local.X, Local.Y, -10.f), FVector(100.f, 100.f, 20.f));

	// ---- 平台：2m 宽 × 4m 长，从梯子旁边 20cm 处开始铺 ----
	// 爬梯子时人被锁在梯子 -X 侧 55cm（胶囊半径 34 → +X 边界在 -21），
	// 平台近边放到 +20：既不会压头，也不会和梯子撞（梯子 box 到 +12 为止）。
	const float PlatWidth = 200.f;		// Y 方向宽度
	const float PlatDepth = 400.f;		// X 方向长度（远离梯子的方向）
	const float PlatNearX = 16.f;		// 平台近边：梯子碰撞盒到 +12，留 4cm 就不会撞

	const float PlatCenterX = Local.X + PlatNearX + PlatDepth * 0.5f;
	const float PlatCenterY = Local.Y;

	SpawnRockSlab(FVector(PlatCenterX, PlatCenterY, CeilTopZ + PlatformThick * 0.5f),
		FVector(PlatDepth, PlatWidth, PlatformThick));

	// ---- 把整个"梯子通道 + 平台"封成一个房间 ----
	// 墙从天花板顶面（600）一直砌到顶盖（950），所以从梯子往上看到的是石头，不是虚空。
	const float WallBottom = CeilTopZ;					// 600
	const float WallTop = PlatformTopZ + AlcoveH;		// 950
	const float WallH = WallTop - WallBottom;
	const float WallZ = (WallTop + WallBottom) * 0.5f;

	const float RoomMinX = Local.X - 110.f;											// 梯子后方留 1.1 米
	const float RoomMaxX = PlatCenterX + PlatDepth * 0.5f;							// 平台末端
	const float RoomMinY = Local.Y - PlatWidth * 0.5f;								// 和平台同宽，墙贴死在平台边上
	const float RoomMaxY = Local.Y + PlatWidth * 0.5f;
	const float RoomLenX = RoomMaxX - RoomMinX;
	const float RoomLenY = RoomMaxY - RoomMinY;
	const float RoomCenterX = (RoomMinX + RoomMaxX) * 0.5f;
	const float RoomCenterY = (RoomMinY + RoomMaxY) * 0.5f;

	// 三面墙 + 一面远墙（近边 X- 是梯子通道，不能再堵）
	SpawnRockSlab(FVector(RoomMinX - WallThick * 0.5f, RoomCenterY, WallZ),
		FVector(WallThick, RoomLenY + WallThick * 2.f, WallH));
	SpawnRockSlab(FVector(RoomMaxX + WallThick * 0.5f, RoomCenterY, WallZ),
		FVector(WallThick, RoomLenY + WallThick * 2.f, WallH));
	SpawnRockSlab(FVector(RoomCenterX, RoomMinY - WallThick * 0.5f, WallZ),
		FVector(RoomLenX + WallThick * 2.f, WallThick, WallH));
	SpawnRockSlab(FVector(RoomCenterX, RoomMaxY + WallThick * 0.5f, WallZ),
		FVector(RoomLenX + WallThick * 2.f, WallThick, WallH));

	// ---- 顶盖 ----
	SpawnRockSlab(FVector(RoomCenterX, RoomCenterY, WallTop + WallThick * 0.5f),
		FVector(RoomLenX + WallThick * 2.f, RoomLenY + WallThick * 2.f, WallThick));

	// ---- 梯子：上端正好到平台顶面（不再冒出去）----
	{
		const FTransform LadderXf(FRotator::ZeroRotator,
			GetActorLocation() + FVector(Local.X, Local.Y, -15.f));

		ALadderActor* Ladder = World->SpawnActorDeferred<ALadderActor>(
			ALadderActor::StaticClass(), LadderXf, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Ladder)
		{
			Ladder->Height = PlatformTopZ + 15.f;

			if (UStaticMesh* LadderMesh = LoadObject<UStaticMesh>(nullptr,
				TEXT("/Game/Ladder/SM_Ladder.SM_Ladder")))
			{
				Ladder->Frame->SetStaticMesh(LadderMesh);
			}

			Ladder->FinishSpawning(LadderXf);
		}
	}

	// ---- 暂存点：平台最里面那个 2×2 的区域 ----
	{
		const FTransform StashXf(FRotator::ZeroRotator,
			GetActorLocation() + FVector(PlatCenterX + PlatDepth * 0.5f - 100.f, PlatCenterY, PlatformTopZ + 10.f));

		AUnloadZone* Stash = World->SpawnActorDeferred<AUnloadZone>(
			AUnloadZone::StaticClass(), StashXf, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Stash)
		{
			Stash->bStashOnly = true;

			// 缩到贴合 2×2 的格子，不要伸到平台外面
			if (Stash->Trigger)
			{
				Stash->Trigger->SetBoxExtent(FVector(95.f, 95.f, 110.f));
			}

			Stash->FinishSpawning(StashXf);
		}
	}

	// ---- 矿洞向导：站在平台上、后背贴着左侧（-Y）墙，离梯子 1.5 米不挡路 ----
	{
		// 平台 Y 范围是 Local.Y ± PlatWidth/2；人形模型进深约 30cm，所以放在 Y-80 处，
		// 后背正好贴上 -Y 那面墙。
		const FTransform GuideXf(FRotator(0.f, 0.f, 0.f),
			GetActorLocation() + FVector(Local.X + 150.f, Local.Y - 80.f, PlatformTopZ));

		AMerchant* Guide = World->SpawnActorDeferred<AMerchant>(
			AMerchant::StaticClass(), GuideXf, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Guide)
		{
			Guide->Kind = EMerchantKind::MineGuide;
			Guide->DisplayName = TEXT("矿洞向导");
			Guide->FinishSpawning(GuideXf);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[Mine] 头顶暂存平台已生成（平台 %.0fcm，净空 %.0fcm，洞在 %.0f,%.0f）"),
		StashPlatformSizeCm, AlcoveH, Local.X, Local.Y);
}

float AMineGenerator::GetBuildProgress() const
{
	if (PendingBlocks.Num() <= 0)
	{
		return 1.f;
	}
	return FMath::Clamp(static_cast<float>(NextBlockIndex) / static_cast<float>(PendingBlocks.Num()), 0.f, 1.f);
}
