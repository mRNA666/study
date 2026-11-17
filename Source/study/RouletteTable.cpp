// Fill out your copyright notice in the Description page of Project Settings.


#include "RouletteTable.h"

#include "studyCharacter.h"
#include "studyGameInstance.h"
#include "StudyRun.h"
#include "StudySounds.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "Rendering/PositionVertexBuffer.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	/** 欧式轮盘 37 格：0 是绿，其余 36 格 18 红 18 黑 */
	constexpr int32 GRoulettePockets = 37;
	constexpr float GRouletteSlotAngle = 360.f / static_cast<float>(GRoulettePockets);

	/** 真实轮盘上号码的排列顺序（从 0 开始、按一个方向数） */
	const int32 GStandardSequence[GRoulettePockets] =
	{
		0, 32, 15, 19, 4, 21, 2, 25, 17, 34, 6, 27, 13, 36, 11, 30, 8, 23, 10,
		5, 24, 16, 33, 1, 20, 14, 31, 9, 22, 18, 29, 7, 28, 12, 35, 3, 26
	};

	/** 红色格号（其余非 0 的都是黑） —— 现在统一由 UstudyGameInstance::RouletteColorOf 提供 */
}

ARouletteTable::ARouletteTable()
{
	// ---- 复用商人那套交互 ----
	Kind = EMerchantKind::Roulette;
	DisplayName = TEXT("轮盘赌台");
	InteractRadiusCm = 320.f;
	bHideBody = true;


	// 商人默认不 tick；轮盘要转，必须开
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	USceneComponent* RootComp = GetRootComponent();

	// 隐藏的圆柱身体别再挡路（否则轮盘正中有一堵看不见的墙）
	if (BodyMesh)
	{
		BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	// ---- 底座：不转 ----
	BowlMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BowlMesh"));
	BowlMesh->SetupAttachment(RootComp);
	BowlMesh->SetMobility(EComponentMobility::Movable);
	BowlMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	BowlMesh->SetCollisionResponseToAllChannels(ECR_Block);

	// ---- 转子轴心：空节点，摆在盘面中心 ----
	// 多这一层的好处：真正的旋转中心永远由它决定，网格原点万一偏了也只改这里。
	RotorPivot = CreateDefaultSubobject<USceneComponent>(TEXT("RotorPivot"));
	RotorPivot->SetupAttachment(RootComp);

	// ---- 转子：只转这个 ----
	// 碰撞开着是为了"读颜色"（在球下面打线，看命中哪块材质）；
	// 只挡 Visibility（不挡 Pawn），所以不会推玩家、也不会撞飞球。
	RotorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RotorMesh"));
	RotorMesh->SetupAttachment(RotorPivot);
	RotorMesh->SetMobility(EComponentMobility::Movable);	// 不设 Movable，SetRelativeRotation 会被静默忽略
	RotorMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	RotorMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	RotorMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	// ---- 球：自己算位置，不挂转子 ----
	// 碰撞：只挡准星（挡 Visibility），完全不参与物理、也不推玩家 ——
	// 球是纯代码驱动的（每帧 SetRelativeLocation），一旦开物理会被挤飞、还会把玩家顶开。
	BallMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BallMesh"));
	BallMesh->SetupAttachment(RootComp);
	BallMesh->SetMobility(EComponentMobility::Movable);
	BallMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	BallMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	BallMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	// 默认网格：可以在这套 Details 里换成别的，路径不对也不会崩（只是没模型）
	static ConstructorHelpers::FObjectFinder<UStaticMesh> RotorFinder(TEXT("/Game/Roulette/SM_Roulette_Rotor.SM_Roulette_Rotor"));
	if (RotorFinder.Succeeded())
	{
		RotorAsset = RotorFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UStaticMesh> BowlFinder(TEXT("/Game/Roulette/SM_Roulette_Bowl.SM_Roulette_Bowl"));
	if (BowlFinder.Succeeded())
	{
		BowlAsset = BowlFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UStaticMesh> BallFinder(TEXT("/Game/Roulette/ball.ball"));
	if (BallFinder.Succeeded())
	{
		BallAsset = BallFinder.Object;
	}
}

void ARouletteTable::BeginPlay()
{
	Super::BeginPlay();

	// 自检：网格没指就直说。否则现象是"对话能开、钱照扣、轮盘一动不动"，很难查。
	const bool bRotorOk = RotorMesh && RotorMesh->GetStaticMesh();
	const bool bBowlOk = BowlMesh && BowlMesh->GetStaticMesh();
	const bool bBallOk = BallMesh && BallMesh->GetStaticMesh();

	if (!bRotorOk || !bBowlOk || !bBallOk)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Roulette] 网格没指全（转子=%d 底座=%d 球=%d）：选中 %s → Details → Roulette|Mesh 里指一下"),
			bRotorOk ? 1 : 0, bBowlOk ? 1 : 0, bBallOk ? 1 : 0, *GetName());
	}

	BuildColorTableFromMesh();
	RecomputeBallOrbit();

	// 把自己的绿赔率推给 GameInstance，让 AI 也用同一个数（一处配置，两边一致）
	if (UstudyGameInstance* Run = Cast<UstudyGameInstance>(UGameplayStatics::GetGameInstance(this)))
	{
		Run->RouletteGreenMultiplier = GreenMultiplier;
		UE_LOG(LogTemp, Log, TEXT("[Roulette] 绿赔率 = %d 倍（AI 也用这个数）"), GreenMultiplier);
	}
}

void ARouletteTable::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// 编辑器里改了 Asset 立刻同步到组件（不用等 BeginPlay）
	if (BowlMesh)
	{
		if (BowlAsset)
		{
			BowlMesh->SetStaticMesh(BowlAsset);
		}
		BowlMesh->SetRelativeLocation(FVector(0.f, 0.f, WheelLiftCm));
	}

	if (RotorPivot)
	{
		RotorPivot->SetRelativeLocation(FVector(0.f, 0.f, WheelLiftCm));
	}

	if (RotorMesh && RotorAsset)
	{
		RotorMesh->SetStaticMesh(RotorAsset);
	}

	if (BallMesh && BallAsset)
	{
		BallMesh->SetStaticMesh(BallAsset);
	}

	ApplyCalibration();
	BuildColorTableFromMesh();
	RecomputeBallOrbit();
}

void ARouletteTable::ApplyCalibration()
{
	// 一次性校准：玩家上一把（强制号码模式）看到球下面实际是 N 号，
	// 由此反推 PocketZeroYaw 差了多少格。
	//
	// 推导：我们瞄准的是"第 i0 格（= DebugForceNumber）"，实际停在球下面的是第 iSeen 格。
	// 因为格子是等间距的，一个整体偏移就能解释这个差 —— 把偏移量补进 PocketZeroYaw 即可。
	if (CalibrateFromNumber < 0 || DebugForceNumber < 0)
	{
		return;
	}

	const int32 i0 = IndexOfNumber(DebugForceNumber);
	const int32 SeenNumber = CalibrateFromNumber;
	const int32 iSeen = IndexOfNumber(SeenNumber);
	const float Dir = bNumbersClockwise ? 1.f : -1.f;

	const float Delta = Dir * static_cast<float>(i0 - iSeen) * GRouletteSlotAngle;
	PocketZeroYaw = FMath::UnwindDegrees(PocketZeroYaw + Delta);
	CalibrateFromNumber = -1;

	UE_LOG(LogTemp, Log,
		TEXT("[Roulette] 校准：瞄准 %d（第 %d 格），实际 %d（第 %d 格）→ PocketZeroYaw 改动 %.1f°，现在 %.1f°"),
		DebugForceNumber, i0, SeenNumber, iSeen, Delta, PocketZeroYaw);
}

void ARouletteTable::SolveCalibration(AstudyCharacter* Character)
{
	if (CalibSamples.Num() < 2)
	{
		return;
	}

	// 两个样本、两个未知数（偏移 Z、方向 d）：
	//   实测角度 M = Z + d × 格号 × 格角    （mod 360）
	// 给 d 分别试 +1 / -1，看哪种能让两个样本算出的 Z 一致。
	const FCalibSample S0 = CalibSamples[0];
	const FCalibSample S1 = CalibSamples[1];
	CalibSamples.Reset();

	float BestZ = PocketZeroYaw;
	bool bBestClockwise = bNumbersClockwise;
	float BestDiff = 1.e9f;

	for (int32 DirIdx = 0; DirIdx < 2; ++DirIdx)
	{
		const bool bClockwise = (DirIdx == 0);
		const float Dir = bClockwise ? 1.f : -1.f;

		const float Z0 = FMath::UnwindDegrees(S0.Angle - Dir * static_cast<float>(S0.Index) * GRouletteSlotAngle);
		const float Z1 = FMath::UnwindDegrees(S1.Angle - Dir * static_cast<float>(S1.Index) * GRouletteSlotAngle);

		const float Diff = FMath::Abs(FMath::UnwindDegrees(Z1 - Z0));
		if (Diff < BestDiff)
		{
			BestDiff = Diff;
			BestZ = FMath::UnwindDegrees((Z0 + Z1) * 0.5f);
			bBestClockwise = bClockwise;
		}
	}

	const bool bSameNumber = (S0.Index == S1.Index);
	const bool bConsistent = !bSameNumber && (BestDiff < GRouletteSlotAngle * 0.5f);

	PocketZeroYaw = BestZ;
	if (!bSameNumber)
	{
		bNumbersClockwise = bBestClockwise;
	}

	UE_LOG(LogTemp, Log,
		TEXT("[Roulette] 校准解：PocketZeroYaw = %.1f°，号码%s时针，残差 %.1f°（%s）"),
		PocketZeroYaw, bNumbersClockwise ? TEXT("顺") : TEXT("逆"), BestDiff,
		bConsistent ? TEXT("一致") : (bSameNumber ? TEXT("两把同一个号，方向判不出") : TEXT("不一致")));

	FString Msg = FString::Printf(
		TEXT("校准结果\n\nPocketZeroYaw = %.1f°\n号码方向 = %s时针\n两样本残差 %.2f°"),
		PocketZeroYaw, bNumbersClockwise ? TEXT("顺") : TEXT("逆"), BestDiff);

	if (bSameNumber)
	{
		Msg += TEXT("\n\n⚠ 两把都停在同一个号上，只能算出偏移、判不了方向。\n再转一把换个数。");
	}
	else if (!bConsistent)
	{
		Msg += TEXT("\n\n⚠ 两个样本对不上（残差太大）：格子可能不是等间距，或者号看错了。\n"
					"再校准一次。");
	}
	else
	{
		Msg += TEXT("\n\n✅ 这台机器上已经生效（退出 PIE 会丢）。\n"
					"把上面两个数抄到 Details，然后关掉 bCalibrationMode。");
	}

	if (Character)
	{
		ShowResult(Character, Msg);
	}
}

int32 ARouletteTable::SampleSurfaceColor() const
{
	// 注意：这里**不能**要求 RotorMesh 有网格 —— 场景里真正显示的那个轮盘
	// 说不定是另一个 Actor 的网格（你早先单独摆的那两个）。射线打到谁，就读谁的材质。
	if (!GetWorld())
	{
		return -1;
	}

	const float Rad = FMath::DegreesToRadians(BallLandingYaw);
	const FVector BallXY(
		FMath::Cos(Rad) * BallInnerRadius,
		FMath::Sin(Rad) * BallInnerRadius,
		0.f);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(RouletteColor), /*bTraceComplex=*/true);
	if (const UPrimitiveComponent* BallComp = static_cast<const UPrimitiveComponent*>(BallMesh.Get()))
	{
		Params.AddIgnoredComponent(BallComp);
	}

	// 三个采样点（球心 + 左右各 2cm）：球可能正好压在隔板上，取多数票
	const FVector Offsets[3] = { FVector::ZeroVector, FVector(2.f, 0.f, 0.f), FVector(-2.f, 0.f, 0.f) };
	int32 Votes[3] = { 0, 0, 0 };
	int32 Valid = 0;

	for (int32 i = 0; i < 3; ++i)
	{
		const FVector& Off = Offsets[i];
		const FVector Start(BallXY.X + Off.X, BallXY.Y + Off.Y, BallZ + 150.f);
		const FVector End(Start.X, Start.Y, BallZ - 150.f);

		FHitResult Hit;
		if (!GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Roulette] 采样 %d：射线没打到东西（球心 XY = %.1f, %.1f，Z = %.1f）"),
				i, Start.X, Start.Y, BallZ);
			continue;
		}

		// ★ 从"被打中的组件"取材质，不要从 RotorMesh 取：
		//   打中的可能是场景里另一个网格（比如你另外摆的轮盘 Actor / 底座），
		//   拿本组件的材质表去索引它的面，就会张冠李戴 —— 颜色"反了"的最大嫌疑。
		const UPrimitiveComponent* HitComp = Hit.GetComponent();
		const UMaterialInterface* Mat = HitComp
			? HitComp->GetMaterial(static_cast<int32>(Hit.ElementIndex))
			: nullptr;

		const FString MatName = Mat ? Mat->GetName() : FString(TEXT("null"));

		UE_LOG(LogTemp, Log,
			TEXT("[Roulette] 采样 %d：Actor=%s 组件=%s 元素=%d 材质=%s 命中点=(%.1f, %.1f, %.1f)"),
			i, *GetNameSafe(Hit.GetActor()), *GetNameSafe(HitComp),
			static_cast<int32>(Hit.ElementIndex), *MatName,
			Hit.Location.X, Hit.Location.Y, Hit.Location.Z);

		int32 Color = -1;
		if (MatName.Contains(TEXT("green")))
		{
			Color = 2;
		}
		else if (MatName.Contains(TEXT("red")))
		{
			Color = 0;
		}
		else if (MatName.Contains(TEXT("black")))
		{
			Color = 1;
		}

		if (Color >= 0)
		{
			++Votes[Color];
			++Valid;
		}
	}

	if (Valid == 0)
	{
		return -1;
	}

	int32 Best = 0;
	for (int32 i = 1; i < 3; ++i)
	{
		if (Votes[i] > Votes[Best])
		{
			Best = i;
		}
	}

	return (Votes[Best] > 0) ? Best : -1;
}

void ARouletteTable::BuildColorTableFromMesh()
{
	bColorTableReady = false;
	ColorByAngle.Init(255, 360);

	UStaticMesh* SM = RotorMesh ? RotorMesh->GetStaticMesh() : nullptr;
	if (!SM)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Roulette] 转子没网格，建不了颜色表"));
		return;
	}

	const FStaticMeshRenderData* RD = SM->GetRenderData();
	if (!RD || RD->LODResources.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Roulette] 拿不到 %s 的渲染数据，建不了颜色表"), *SM->GetName());
		return;
	}

	const FStaticMeshLODResources& LOD = RD->LODResources[0];
	const FPositionVertexBuffer& VB = LOD.VertexBuffers.PositionVertexBuffer;
	const FRawStaticIndexBuffer& IB = LOD.IndexBuffer;

	// ---- 打包版的坑（踩过一次，记在这里）----
	// 静态网格默认 bAllowCPUAccess = false：cook 时几何数据会上传 GPU、然后从 CPU 内存释放，
	// 于是 FRawStaticIndexBuffer 里一个索引都没有，GetIndex() 里的 check() 当场断言崩溃。
	// 编辑器里永远正常，因为编辑器不 cook。
	//   修法：打开 SM_Roulette_Rotor 的 Details → 搜 "CPU" → 勾 Allow CPU Access，再重新打包。
	//   下面这段是兜底：没有 CPU 数据就不建表，退回"号码序列 + 角度"那套判断，绝不崩。
	const uint32 NumIdx = IB.GetNumIndices();
	const uint32 NumVerts = VB.GetNumVertices();
	if (NumIdx == 0u || NumVerts == 0u)
	{
		bColorTableReady = false;
		UE_LOG(LogTemp, Error,
			TEXT("[Roulette] %s 在打包版里没有 CPU 几何数据（索引=%u 顶点=%u）——"
				 "静态网格的 Allow CPU Access 没勾。本次退回号码序列判断颜色；"
				 "要恢复几何表，勾上那个开关再重新打包。"),
			*SM->GetName(), NumIdx, NumVerts);
		return;
	}

	// 三个颜色的"角度直方图"
	TArray<int32> Votes[3];
	for (int32 c = 0; c < 3; ++c)
	{
		Votes[c].Init(0, 360);
	}

	double RadiusSum[3] = { 0.0, 0.0, 0.0 };
	int64 RadiusCount[3] = { 0, 0, 0 };
	int32 TriCount = 0;

	// 先把材质槽全列出来：万一名字和预期不一致，日志里有据可查
	for (int32 i = 0; i < RotorMesh->GetNumMaterials(); ++i)
	{
		const UMaterialInterface* Mat = RotorMesh->GetMaterial(i);
		UE_LOG(LogTemp, Log, TEXT("[Roulette] 材质槽 %d = %s"),
			i, Mat ? *Mat->GetName() : TEXT("null"));
	}

	for (const FStaticMeshSection& Sec : LOD.Sections)
	{
		const UMaterialInterface* Mat = RotorMesh->GetMaterial(Sec.MaterialIndex);
		const FString Name = Mat ? Mat->GetName() : FString();

		// 认红/黑/绿三种漆。
		// 宽容一点：名字里带 red / green 就算；black 要排除黑钢（black_steel）那类结构件。
		int32 Color = -1;
		if (Name.Contains(TEXT("green")))
		{
			Color = 2;
		}
		else if (Name.Contains(TEXT("red")))
		{
			Color = 0;
		}
		else if (Name.Contains(TEXT("black")) && !Name.Contains(TEXT("steel")))
		{
			Color = 1;
		}

		UE_LOG(LogTemp, Log, TEXT("[Roulette] 段 材质=%s 三角形=%u → %s"),
			Name.IsEmpty() ? TEXT("null") : *Name, Sec.NumTriangles,
			Color < 0 ? TEXT("忽略") : *ColorName(Color));

		if (Color < 0)
		{
			continue;
		}

		// 段的范围也要夹一下：cook 之后段的 FirstIndex/NumTriangles 理论上一致，
		// 但这里宁可多一次判断，也不要再让 check() 把游戏打崩。
		const uint32 Last = FMath::Min<uint32>(Sec.FirstIndex + Sec.NumTriangles * 3u, NumIdx);
		for (uint32 i = Sec.FirstIndex; i + 2 < Last; i += 3)
		{
			const uint32 I0 = IB.GetIndex(i);
			const uint32 I1 = IB.GetIndex(i + 1);
			const uint32 I2 = IB.GetIndex(i + 2);

			if (I0 >= NumVerts || I1 >= NumVerts || I2 >= NumVerts)
			{
				continue;
			}

			const FVector3f A = VB.VertexPosition(I0);
			const FVector3f B = VB.VertexPosition(I1);
			const FVector3f C = VB.VertexPosition(I2);

			const float X = (A.X + B.X + C.X) / 3.f;
			const float Y = (A.Y + B.Y + C.Y) / 3.f;
			const float R = FMath::Sqrt(X * X + Y * Y);

			if (R < 1.f)
			{
				continue;	// 中轴上的东西（塔底）不算
			}

			// 注意：UnwindDegrees 返回的是 (-180, 180]，必须先搬到 [0, 360)，
			// 否则负角度会被 clamp 到 0 格，整张表全错（这个 bug 之前就埋在这里）。
			float Deg = FMath::RadiansToDegrees(FMath::Atan2(Y, X));
			Deg = FMath::Fmod(Deg + 360.f, 360.f);

			const int32 Bin = FMath::Clamp(FMath::RoundToInt(Deg), 0, 359);

			Votes[Color][Bin] += 1;
			RadiusSum[Color] += R;
			++RadiusCount[Color];
			++TriCount;
		}
	}

	int32 Covered = 0;
	for (int32 b = 0; b < 360; ++b)
	{
		int32 BestColor = -1;
		int32 BestVotes = 0;
		for (int32 c = 0; c < 3; ++c)
		{
			if (Votes[c][b] > BestVotes)
			{
				BestVotes = Votes[c][b];
				BestColor = c;
			}
		}

		if (BestColor >= 0 && BestVotes > 0)
		{
			ColorByAngle[b] = static_cast<uint8>(BestColor);
			++Covered;
		}
	}

	bColorTableReady = (Covered > 180);

	// 三种颜色的平均半径 → 球该落在哪个半径（只用这次分析出来的数当默认值）
	double BestRadius = 0.0;
	int64 BestRadiusN = 0;
	for (int32 c = 0; c < 3; ++c)
	{
		if (RadiusCount[c] > BestRadiusN)
		{
			BestRadiusN = RadiusCount[c];
			BestRadius = (RadiusCount[c] > 0) ? (RadiusSum[c] / static_cast<double>(RadiusCount[c])) : 0.0;
		}
	}

	if (BestRadius > 1.0 && BallInnerCm <= 0.f)
	{
		BallInnerCm = static_cast<float>(BestRadius);
	}

	// 打一张 36 格的缩略图（每 10° 一个字母）方便肉眼核对
	FString Map;
	for (int32 b = 0; b < 360; b += 10)
	{
		const uint8 C = ColorByAngle[b];
		Map.AppendChar(C == 0 ? TEXT('R') : C == 1 ? TEXT('B') : C == 2 ? TEXT('G') : TEXT('.'));
	}

	UE_LOG(LogTemp, Log,
		TEXT("[Roulette] 颜色表：%d 个三角形，覆盖 %d/360°，球道半径=%.1f cm%s"),
		TriCount, Covered, BestRadius,
		bColorTableReady ? TEXT("") : TEXT("（覆盖不足，表不可用）"));

	UE_LOG(LogTemp, Log, TEXT("[Roulette] 角度→颜色（0° 起，每 10°）：%s"), *Map);
}

int32 ARouletteTable::ColorAtLocalAngle(float LocalYaw) const
{
	if (!bColorTableReady || ColorByAngle.Num() < 360)
	{
		return -1;
	}

	int32 Bin = FMath::RoundToInt(LocalYaw) % 360;
	if (Bin < 0)
	{
		Bin += 360;
	}

	const uint8 C = ColorByAngle[FMath::Clamp(Bin, 0, 359)];
	return (C <= 2) ? static_cast<int32>(C) : -1;
}

void ARouletteTable::RecomputeBallOrbit()
{
	if (!RotorMesh || !RotorMesh->GetStaticMesh())
	{
		return;
	}

	const FBoxSphereBounds RotorBounds = RotorMesh->GetStaticMesh()->GetBounds();
	const float HalfWidth = FMath::Max(RotorBounds.BoxExtent.X, RotorBounds.BoxExtent.Y);
	if (HalfWidth <= 0.f)
	{
		return;
	}

	float BallHalfHeight = 2.f;
	if (BallMesh && BallMesh->GetStaticMesh())
	{
		BallHalfHeight = FMath::Max(BallMesh->GetStaticMesh()->GetBounds().SphereRadius, 0.5f);
	}

	// 第一次（还是 0）自动按网格尺寸填，填完就变成可手调的数字（单位 cm）
	if (BallOuterCm <= 0.f)
	{
		BallOuterCm = HalfWidth * 0.92f;
	}
	if (BallInnerCm <= 0.f)
	{
		// 0.75：让球落在号码带的中段，而不是贴着内圈/外圈（贴着边缘容易跨两格）
		BallInnerCm = HalfWidth * 0.75f;
	}
	if (BallHeightCm <= 0.f)
	{
		BallHeightCm = WheelLiftCm + RotorBounds.BoxExtent.Z * 0.6f + BallHalfHeight;
	}

	BallOuterRadius = BallOuterCm;
	BallInnerRadius = BallInnerCm;
	BallZ = BallHeightCm;

	// 没在转的时候：球停在起点
	if (!bSpinning && BallMesh)
	{
		BallMesh->SetRelativeLocation(FVector(BallOuterRadius, 0.f, BallZ));
	}
}

// ==================== 号码 / 角度映射 ====================

float ARouletteTable::PocketLocalYaw(int32 Index) const
{
	const float Dir = bNumbersClockwise ? 1.f : -1.f;
	return PocketZeroYaw + Dir * static_cast<float>(Index) * GRouletteSlotAngle;
}

int32 ARouletteTable::IndexFromLocalYaw(float LocalYaw) const
{
	// 球落在本地角度 LocalYaw 上，落在第几格？
	// 格子等间距，所以直接除以格角、四舍五入 —— 得到的格子就是"球压住的那一格"。
	const float Dir = bNumbersClockwise ? 1.f : -1.f;
	const float Rel = FMath::UnwindDegrees(LocalYaw - PocketZeroYaw) * Dir;

	int32 Index = FMath::RoundToInt(Rel / GRouletteSlotAngle);
	Index = ((Index % GRoulettePockets) + GRoulettePockets) % GRoulettePockets;
	return Index;
}

int32 ARouletteTable::NumberAtPocketIndex(int32 Index) const
{
	const int32 Clamped = FMath::Clamp(Index, 0, GRoulettePockets - 1);
	return bStandardWheelSequence ? GStandardSequence[Clamped] : Clamped;
}

int32 ARouletteTable::IndexOfNumber(int32 Number) const
{
	for (int32 i = 0; i < GRoulettePockets; ++i)
	{
		if (NumberAtPocketIndex(i) == Number)
		{
			return i;
		}
	}
	return 0;
}

// ==================== 对话 ====================

void ARouletteTable::BuildDialog(AstudyCharacter* Character)
{
	UstudyGameInstance* GI = GetRun(this);
	if (!GI || !Character)
	{
		return;
	}

	// 必须重新认领：DialogChoose 会先 CloseDialog（把 DialogMerchant 清空）再回调这里
	Character->SetDialogMerchant(this);
	Character->SetDialogNumberEnabled(0.f);
	Character->SetDialogNumberEnabled2(0.f);

	if (bSpinning)
	{
		Character->OpenOptionDialog(DisplayName,
			TEXT("轮子还在转。\n\n等它停稳了再说。"),
			{ TEXT("知道了") });
		return;
	}

	if (Stage == EStage::PickColor)
	{
		const FString Body = FString::Printf(
			TEXT("押注：%d 币\n现金：%d 币\n\n押哪个颜色？\n"
				 "· 红 / 黑：中了返还 2 倍\n"
				 "· 绿：中了返还 %d 倍"),
			PendingBet, GI->Cash, GreenMultiplier);

		Character->OpenOptionDialog(DisplayName, Body,
			{
				TEXT("押红"),
				TEXT("押黑"),
				FString::Printf(TEXT("押绿（%d 倍）"), GreenMultiplier),
				TEXT("算了")
			});
		return;
	}

	FString Body = FString::Printf(
		TEXT("「坐下玩一把？」\n\n现金：%d 币\n"
			 "37 格：18 红 18 黑，0 是绿。\n红黑 1 赔 1，绿 %d 倍。"),
		GI->Cash, GreenMultiplier);

	// 网格没指时在页面上提醒一句（只在没指的时候出现）
	if (!RotorMesh || !RotorMesh->GetStaticMesh())
	{
		Body += TEXT("\n\n（这台机器没指定转子网格：选中本 Actor → Details → Roulette|Mesh → RotorAsset）");
	}

	// 校准模式：让人一眼看出现在是强制号码，不是随机
	if (DebugForceNumber >= 0)
	{
		Body += FString::Printf(TEXT("\n\n[调试] 强制号码 %d（校准时用，改回 -1 恢复正常）"), DebugForceNumber);
	}

	Character->OpenOptionDialog(DisplayName, Body,
		{ TEXT("押一注"), TEXT("算了") });
}

void ARouletteTable::AskBet(AstudyCharacter* Character, const FString& Warning)
{
	UstudyGameInstance* GI = GetRun(this);
	if (!GI || !Character)
	{
		return;
	}

	GI->Cash = FMath::Max(GI->Cash, 0);
	if (GI->Cash <= 0)
	{
		Stage = EStage::Idle;
		ShowResult(Character, TEXT("你身上一分钱都没有。\n\n先去矿里挖一趟再回来。"));
		return;
	}

	Stage = EStage::AskBet;

	const FString Head = Warning.IsEmpty() ? FString() : (Warning + TEXT("\n\n"));

	Character->SetDialogNumberStyle(TEXT("押"), TEXT("赌注"), TEXT("币"));
	Character->SetDialogNumberEnabled(static_cast<float>(GI->Cash));

	Character->OpenNumberDialog(DisplayName,
		Head + FString::Printf(TEXT("押多少？\n\n现金：%d 币"), GI->Cash),
		TEXT("下注金额（币）"),
		static_cast<float>(GI->Cash));

	// 默认填一个方便的手气注：1000 或全部家当（取小）
	Character->SetDialogNumberPrefill(static_cast<float>(FMath::Min(1000, GI->Cash)));

	// OpenNumberDialog 之前 DialogChoose 已经把 DialogMerchant 清了，这里重新认领
	Character->SetDialogMerchant(this);
}

void ARouletteTable::OnDialogNumber(AstudyCharacter* Character, float Value)
{
	UstudyGameInstance* GI = GetRun(this);
	if (!GI || !Character)
	{
		return;
	}

	const int32 Bet = FMath::RoundToInt(Value);

	// ---- 校准模式：这个数字是"球实际停在几号" ----
	if (Stage == EStage::CalibrateInput)
	{
		const int32 SeenNumber = FMath::Clamp(Bet, 0, 36);

		FCalibSample Sample;
		Sample.Index = IndexOfNumber(SeenNumber);
		Sample.Angle = LastLandingLocalYaw;
		CalibSamples.Add(Sample);

		Stage = EStage::Idle;

		UE_LOG(LogTemp, Log,
			TEXT("[Roulette] 校准样本 #%d：看到 %d 号（第 %d 格），实测本地角度 %.1f°"),
			CalibSamples.Num(), SeenNumber, Sample.Index, Sample.Angle);

		if (CalibSamples.Num() < 2)
		{
			ShowResult(Character, FString::Printf(
				TEXT("收到第 1 个样本：%d 号 / %.1f°\n\n"
					 "再押一把（最好停在不同号上），凑够两个样本就能同时算出"
					 "**偏移量**和**号码方向**。"),
				SeenNumber, Sample.Angle));
			return;
		}

		SolveCalibration(Character);
		return;
	}

	if (Bet <= 0)
	{
		AskBet(Character, TEXT("赌注得大于 0。"));
		return;
	}

	if (Bet > GI->Cash)
	{
		AskBet(Character, FString::Printf(TEXT("你只有 %d 币，押不了 %d。"), GI->Cash, Bet));
		return;
	}

	PendingBet = Bet;
	Stage = EStage::PickColor;
	BuildDialog(Character);
}

void ARouletteTable::OnDialogOption(AstudyCharacter* Character, int32 Index)
{
	if (!Character)
	{
		return;
	}

	// 转的时候那页只有一个「知道了」，点了也不该有反应
	if (bSpinning)
	{
		return;
	}

	if (Stage == EStage::PickColor)
	{
		if (Index < 0 || Index > 2)
		{
			// 「算了」：对话已经被 DialogChoose 关掉了，清一下状态就行
			Stage = EStage::Idle;
			PendingBet = 0;
			return;
		}

		StartSpin(Character, Index);
		return;
	}

	if (Index == 0)
	{
		AskBet(Character);
		return;
	}

	// 「算了」
	Stage = EStage::Idle;
	PendingBet = 0;
}

// ==================== 转盘 ====================

void ARouletteTable::StartSpin(AstudyCharacter* Character, int32 Color)
{
	UstudyGameInstance* GI = GetRun(this);
	if (!GI || !Character)
	{
		return;
	}

	if (PendingBet <= 0 || GI->Cash < PendingBet)
	{
		Stage = EStage::Idle;
		return;
	}

	// ① 先把钱扣掉（押注已经离开口袋）
	GI->Cash -= PendingBet;

	// ② 盘怎么转：**纯视觉，跟结果无关** —— 转几整圈 + 一个随机零头，
	//    停在哪无所谓，因为结果是由球决定的（③④）。
	StartYaw = RotorMesh ? RotorMesh->GetRelativeRotation().Yaw : 0.f;
	TotalSpin = 360.f * FMath::Max(RotorTurns, 0.f) + FMath::FRandRange(0.f, 360.f);

	const float RotorFinalYaw = StartYaw + TotalSpin;

	// 号码盘是固定的（外碗）就以 Actor 为基准；在转子上就以转子终角为基准
	const float RefYaw = bNumbersOnBowl ? 0.f : RotorFinalYaw;

	// ③ 球落在哪 —— 这就是结果本身
	//    只有校准模式（DebugForceNumber >= 0）才反过来：先把球瞄准指定号码
	if (DebugForceNumber >= 0)
	{
		const int32 iForce = IndexOfNumber(DebugForceNumber);
		BallLandingYaw = FMath::UnwindDegrees(RefYaw + PocketLocalYaw(iForce));
	}
	else
	{
		BallLandingYaw = FMath::FRandRange(0.f, 360.f);
	}

	// ④ 由"球压在哪"反推是第几格 —— 球落在哪一格，那一格就是结果。
	//    再也不会出现"瞄准了却停偏"这种事：判定就是照着球算的。
	ResultIndex = IndexFromLocalYaw(FMath::UnwindDegrees(BallLandingYaw - RefYaw));
	ResultNumber = NumberAtPocketIndex(ResultIndex);

	// ★ 吸附到这一格的**正中心**：球永远停在某色块正中，不会跨在两格的分界线上。
	//   球直径 4cm、一格弧长可能只有 5cm，不吸附就会"视觉压左、判定取右"。
	if (bSnapBallToPocketCenter)
	{
		BallLandingYaw = FMath::UnwindDegrees(RefYaw + PocketLocalYaw(ResultIndex));
	}

	const int32 PocketColor = PocketColorOf(ResultNumber);

	// 校准用的"实测角度"：球落在转子本地坐标的哪个角度。它和 PocketZeroYaw 无关，
	// 所以拿两次这样的样本就能解出"偏移 + 方向"两个未知数。
	LastLandingLocalYaw = FMath::UnwindDegrees(BallLandingYaw - RefYaw);

	// ⑤ 球的运动：从它现在待的位置起飞，逆着盘转几圈，最后停在 BallLandingYaw
	if (BallMesh)
	{
		const FVector Cur = BallMesh->GetRelativeLocation();
		BallStartYaw = FMath::RadiansToDegrees(FMath::Atan2(Cur.Y, Cur.X));
	}
	else
	{
		BallStartYaw = 0.f;
	}

	// 负号 = 逆着盘转（真轮盘就是反向的）；＋几整圈保证"转得够久"
	BallSweep = -(360.f * FMath::Max(BallTurns, 0.f)
		+ FMath::Fmod(BallStartYaw - BallLandingYaw + 1440.f, 360.f));

	// ⑥ 记下这一注，等停稳再结算（视觉和到账同一刻）
	//    注意：输赢**不在这里算** —— 要等球真的停下来，读它压着什么颜色（见 FinishSpin）
	PendingColor = Color;
	PendingPayout = 0;

	bSpinning = true;
	SpinTime = 0.f;
	SpinningChar = Character;

	UGameplayStatics::PlaySound2D(this, StudySounds::Click(), 0.8f);

	UE_LOG(LogTemp, Log,
		TEXT("[Roulette] 押 %s %d 币 → 第 %d 格 = 号码 %d（%s），%s（球落在 %.0f°，转子总转 %.0f°）"),
		*ColorName(PendingColor), PendingBet, ResultIndex, ResultNumber, *ColorName(PocketColor),
		PendingPayout > 0 ? TEXT("中") : TEXT("没中"),
		BallLandingYaw, TotalSpin);
}

void ARouletteTable::UpdateSpinVisual(float Ease)
{
	if (RotorMesh)
	{
		RotorMesh->SetRelativeRotation(FRotator(0.f, StartYaw + TotalSpin * Ease, 0.f));
	}

	if (!BallMesh)
	{
		return;
	}

	// 半径：三次方 → 前半段一直贴着外圈，最后才"掉"进内圈（像真的滚下去）
	const float Radius = FMath::Lerp(BallOuterRadius, BallInnerRadius, Ease * Ease * Ease);

	const float Yaw = BallStartYaw + BallSweep * Ease;
	const float Rad = FMath::DegreesToRadians(Yaw);

	BallMesh->SetRelativeLocation(FVector(
		FMath::Cos(Rad) * Radius,
		FMath::Sin(Rad) * Radius,
		BallZ));
}

void ARouletteTable::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bSpinning)
	{
		return;
	}

	SpinTime += DeltaSeconds;

	const float T = FMath::Clamp(SpinTime / FMath::Max(SpinDuration, 0.1f), 0.f, 1.f);
	const float Ease = 1.f - FMath::Pow(1.f - T, 3.f);		// 先快后慢

	UpdateSpinVisual(Ease);

	if (T >= 1.f)
	{
		FinishSpin();
	}
}

void ARouletteTable::FinishSpin()
{
	bSpinning = false;
	Stage = EStage::Idle;

	UstudyGameInstance* GI = GetRun(this);
	if (!GI)
	{
		return;
	}

	// 结果 = 球压着什么颜色。三种取法，从最可靠往下退：
	//   ① 几何表（逐三角形分析出来的"角度→颜色"，不需要校准）
	//   ② 盘面材质射线（需要轮盘有碰撞体）
	//   ③ 号码序列 + 角度映射（需要校准过）
	int32 FinalColor = -1;
	FString ColorSource = TEXT("角度映射");

	if (bColorTableReady)
	{
		FinalColor = ColorAtLocalAngle(LastLandingLocalYaw);
		if (FinalColor >= 0)
		{
			ColorSource = TEXT("几何表");
		}
	}

	if (FinalColor < 0 && bReadColorFromSurface)
	{
		FinalColor = SampleSurfaceColor();
		if (FinalColor >= 0)
		{
			ColorSource = TEXT("盘面材质");
		}
	}

	if (FinalColor < 0)
	{
		FinalColor = PocketColorOf(ResultNumber);
	}

	const bool bFromSurface = (ColorSource != TEXT("角度映射"));

	// 红黑对调：如果球明明停在红格、结果却报黑（或反过来），勾上这一个就对了。绿不受影响。
	if (bSwapRedBlack)
	{
		if (FinalColor == 0)
		{
			FinalColor = 1;
		}
		else if (FinalColor == 1)
		{
			FinalColor = 0;
		}
	}

	const bool bWon = (FinalColor == PendingColor);
	PendingPayout = bWon ? (PendingBet * (PendingColor == 2 ? GreenMultiplier : 2)) : 0;

	// 赛季统计：净盈亏（赢了是 +返还-押注，输了是 -押注）
	GI->RouletteProfit += (PendingPayout - PendingBet);

	if (bWon)
	{
		GI->Cash += PendingPayout;
		UGameplayStatics::PlaySound2D(this, StudySounds::Cash(), 0.9f);
	}

	const int32 PocketColor = FinalColor;

	UE_LOG(LogTemp, Log,
		TEXT("[Roulette] 判定：球下读出 %s（来源=%s），你押 %s → %s，结算 %d 币"),
		*ColorName(FinalColor), *ColorSource,
		*ColorName(PendingColor), bWon ? TEXT("中") : TEXT("没中"), PendingPayout);

	// 校准模式：转完先问"球停在几号"，不弹普通结果页
	AstudyCharacter* Char = SpinningChar.Get();
	if (bCalibrationMode && Char)
	{
		Char->SetDialogNumberStyle(TEXT("看"), TEXT("号码"), TEXT(""));
		Char->SetDialogNumberEnabled(36.f);
		Char->SetDialogNumberPrefill(0.f);

		Char->OpenNumberDialog(DisplayName,
			FString::Printf(
				TEXT("【校准模式】\n\n球停在几号？（0-36）\n\n程序算的是第 %d 格 = %d 号\n\n"
					 "填进去，两个样本之后自动算出偏移和方向。"),
				ResultIndex, ResultNumber),
			TEXT("球下面的号码"),
			36.f);

		Char->SetDialogMerchant(this);
		Stage = EStage::CalibrateInput;

		// 不立刻清掉这一注的信息（校准样本还要用），但钱已经结算完
		PendingBet = 0;
		PendingColor = -1;
		PendingPayout = 0;
		return;
	}

	FString Body;
	if (bFromSurface)
	{
		// 颜色是从盘面上读出来的，就不报具体号码（号码是贴图，代码读不到，乱报会矛盾）
		Body = FString::Printf(TEXT("球停在%s格\n\n你押%s，%s\n\n"),
			*ColorName(PocketColor), *ColorName(PendingColor),
			bWon ? TEXT("中了") : TEXT("没中"));
	}
	else
	{
		Body = FString::Printf(TEXT("「%d —— %s」\n\n你押%s，%s\n\n"),
			ResultNumber, *ColorName(PocketColor), *ColorName(PendingColor),
			bWon ? TEXT("中了") : TEXT("没中"));
	}

	if (bWon)
	{
		Body += FString::Printf(TEXT("返还 %d 币（押 %d）\n现金：%d 币"),
			PendingPayout, PendingBet, GI->Cash);
	}
	else
	{
		Body += FString::Printf(TEXT("押的 %d 币没了\n现金：%d 币"), PendingBet, GI->Cash);
	}

	GI->PushFeed(FString::Printf(TEXT("你在轮盘押%s %d 币，%s"),
		*ColorName(PendingColor), PendingBet, bWon ? TEXT("赢了") : TEXT("输了")));

	// 校准模式提示（老的手填路径，保留）
	if (DebugForceNumber >= 0)
	{
		Body += TEXT("\n\n[校准] 球下面实际是几号？\n"
					 "填到 Details → Roulette|Calibration → CalibrateFromNumber，自动校准。");
	}

	UE_LOG(LogTemp, Log, TEXT("[Roulette] 结算 %s %d 币（现金 %d）"),
		bWon ? TEXT("返还") : TEXT("输掉"), bWon ? PendingPayout : PendingBet, GI->Cash);

	if (Char)
	{
		ShowResult(Char, Body);
	}

	PendingBet = 0;
	PendingColor = -1;
	PendingPayout = 0;
	SpinningChar = nullptr;
}

// ==================== 小工具 ====================

int32 ARouletteTable::PocketColorOf(int32 Number)
{
	// 和 AI 共用同一份规则（UstudyGameInstance::RouletteColorOf），避免两边算得不一样
	return UstudyGameInstance::RouletteColorOf(Number);
}

FString ARouletteTable::ColorName(int32 Color)
{
	switch (Color)
	{
	case 0:  return TEXT("红");
	case 1:  return TEXT("黑");
	case 2:  return TEXT("绿");
	default: return TEXT("？");
	}
}
