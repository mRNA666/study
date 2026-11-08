// Fill out your copyright notice in the Description page of Project Settings.


#include "DigVolume.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/MeshNormals.h"
#include "Generators/MarchingCubes.h"
#include "Selections/MeshConnectedComponents.h"
#include "HAL/PlatformTime.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "DropItem.h"

DEFINE_LOG_CATEGORY(LogDig);

namespace
{
	// 用显式 double 常量：引擎的 PI 宏已废弃，UE_PI 是 float（会掉精度）
	constexpr double kPi = 3.14159265358979323846;

	/** 半径 R 的球体积（cm³） */
	double SphereVolumeCm3(double R)
	{
		return (4.0 / 3.0) * kPi * R * R * R;
	}
}

ADigVolume::ADigVolume()
{
	PrimaryActorTick.bCanEverTick = false;
	MeshComp = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("MeshComp"));
	SetRootComponent(MeshComp);
}
void ADigVolume::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (NoiseAmplitudeCm < 0.01f)
	{
		UE_LOG(LogDig, Warning, TEXT("[Dig] SDF center=%.2f face=%.2f out=%.2f corner=%.2f"),
			EvalSDF(FVector3d(0, 0, 0)),
			EvalSDF(FVector3d(50, 0, 0)),
			EvalSDF(FVector3d(60, 0, 0)),
			EvalSDF(FVector3d(60, 60, 60)));
	}
	RebuildMesh();

	// 关卡加载 / 编辑器里改参数时立即烘焙，保证一进游戏就有碰撞可打。
	// （游戏里挖洞走的是延迟烘焙，不走这里）
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PhysicsTimerHandle);
	}
	FlushPhysics();
}

void ADigVolume::RebuildMesh()
{
	using namespace UE::Geometry;

	const double H = SizeCm * 0.5;
	const double Amp = NoiseAmplitudeCm;

	// 复用成员 MC：Reset() 清空输出数组但保留容量，省掉每次重建的重新分配
	// 注意：Generate() 内部的 BuildMesh() 是"追加"，不 Reset 的话每挖一次面数就翻倍
	FMarchingCubes& MC = MarchingCubes;
	MC.Reset();

	MC.Implicit = [this](const FVector3d& P) -> double { return -EvalSDF(P); };
	MC.IsoValue = 0.0;
	const double Pad = Amp * 2.0 + SampleStepCm * 2.0;
	MC.Bounds = FAxisAlignedBox3d(FVector3d(-H - Pad, -H - Pad, -H - Pad),
		FVector3d(H + Pad, H + Pad, H + Pad));
	MC.CubeSize = SampleStepCm;
	MC.bParallelCompute = true;
	MC.bEnableValueCaching = false;

	const double T0 = FPlatformTime::Seconds();
	MC.Generate();
	const double T1 = FPlatformTime::Seconds();

	FDynamicMesh3 Mesh;
	if (!Mesh.Copy(&MC)) {
		UE_LOG(LogDig, Error, TEXT("[Dig] Mesh.copy failed - MC produced no triangles"));
		return;
	}
	const double T2 = FPlatformTime::Seconds();

	// ---- 只保留最大的连通块，删掉被挖断后浮在空中的碎块 ----
	// 说明：挖穿石头时，被切断的小块会变成互不相连的壳，MC 照样会把它画出来。
	// 这里按"边连通"分块，只留三角形最多的那一块，其余全删。
	int32 RemovedChunks = 0;
	int32 RemovedTris = 0;
	{
		FMeshConnectedComponents Comp(&Mesh);
		Comp.FindConnectedTriangles();

		if (Comp.Num() > 1)
		{
			const int32 KeepIndex = Comp.GetLargestIndexByCount();

			// 先把要删的三角形 ID 全部收集起来，再统一删。
			// （RemoveTriangle 只删单个，没有批量接口；删之前收集，避免边删边遍历）
			TArray<int32> ToRemove;
			for (int32 ci = 0; ci < Comp.Num(); ++ci)
			{
				if (ci == KeepIndex)
				{
					continue;
				}
				RemovedChunks++;
				ToRemove.Append(Comp.GetComponent(ci).Indices);
			}

			for (int32 tid : ToRemove)
			{
				Mesh.RemoveTriangle(tid, /*bRemoveIsolatedVertices*/ false, /*bPreserveManifold*/ false);
			}
			RemovedTris = ToRemove.Num();

			// 清掉删三角形后留下的孤立顶点
			Mesh.CompactInPlace();
		}
	}
	const double T2b = FPlatformTime::Seconds();

	const int32 VertCount = Mesh.VertexCount();
	const int32 TriCount = Mesh.TriangleCount();

	FMeshNormals::QuickComputeVertexNormals(Mesh);
	const double T3 = FPlatformTime::Seconds();

	// 先关掉"网格变化时自动烘物理"，改成下面手动烘一次 —— 这样能把这 100+ms 单独计时
	MeshComp->SetDeferredCollisionUpdatesEnabled(true, false);

	MeshComp->SetMesh(MoveTemp(Mesh));
	const double T4 = FPlatformTime::Seconds();

	// 碰撞：组件默认是 NoCollision，属性要设，但**不在这里烘焙**
	MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	MeshComp->SetCollisionObjectType(ECC_WorldStatic);
	MeshComp->SetCollisionResponseToAllChannels(ECR_Block);

	// 把烘焙推迟到"挖掘停手 0.35 秒后"，每挖一次就重置计时器
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			PhysicsTimerHandle, this, &ADigVolume::FlushPhysics, 0.35f, false);
	}
	const double T5 = FPlatformTime::Seconds();

	UE_LOG(LogDig, Warning,
		TEXT("[Dig] verts=%d tris=%d | MC=%.1f Copy=%.1f 碎片=%.1f Normals=%.1f SetMesh=%.1f | 总计=%.1fms | holes=%d | 删掉%d块/%d三角形 | size=%.0f step=%.1f amp=%.1f | 物理烘焙已延后"),
		VertCount, TriCount,
		(T1 - T0) * 1000.0, (T2 - T1) * 1000.0, (T2b - T2) * 1000.0,
		(T3 - T2b) * 1000.0, (T4 - T3) * 1000.0, (T5 - T0) * 1000.0,
		DigHoles.Num(), RemovedChunks, RemovedTris,
		SizeCm, SampleStepCm, NoiseAmplitudeCm);
}

void ADigVolume::FlushPhysics()
{
	const double T0 = FPlatformTime::Seconds();

	// 这里才是真正烘物理的地方：用整个网格生成 PhysX trimesh
	MeshComp->EnableComplexAsSimpleCollision();

	UE_LOG(LogDig, Warning, TEXT("[Dig] 物理烘焙 = %.1fms"), (FPlatformTime::Seconds() - T0) * 1000.0);
}
double ADigVolume::EvalFBM(const FVector3d& P, double Scale) const
{
	double Sum = 0.0, OctaveAmp = 1.0, Freq = 1.0;
	for (int32 i = 0;i < 3;i++)
	{
		Sum += OctaveAmp * FMath::PerlinNoise3D(FVector(P.X * Scale * Freq,
			P.Y * Scale * Freq,
			P.Z * Scale * Freq));
		OctaveAmp *= 0.5;
		Freq *= 2.0;
	}
	return Sum;
}

double ADigVolume::EvalSDF(const FVector3d& P) const
{
	const double H = SizeCm * 0.5;
	const FVector3d Q(FMath::Abs(P.X) - H,FMath::Abs(P.Y) - H,FMath::Abs(P.Z) - H);
	const FVector3d Outside(FMath::Max(Q.X, 0.0),FMath::Max(Q.Y, 0.0),FMath::Max(Q.Z, 0.0));
	const double Inside = FMath::Min(FMath::Max(Q.X, FMath::Max(Q.Y, Q.Z)), 0.0);
	const double BoxDist = Outside.Length() + Inside;

	const double StoneDist = BoxDist + EvalFBM(P, NoiseScale) * NoiseAmplitudeCm;
	double Result = StoneDist;

	for (const FDigHole& Hole : DigHoles)
	{
		// 只有 |P-C| < R - Result 时，这个洞才可能把 Result 往上推。
		// 用平方比较避免开根号；Result 单调变大，所以这个上界会越用越紧。
		const double Limit = Hole.RadiusCm - Result;
		if (Limit <= 0.0)
		{
			continue;
		}

		const FVector3d D = P - Hole.Center;
		const double DistSq = D.SizeSquared();
		if (DistSq >= Limit * Limit)
		{
			continue;
		}

		const double SphereDist = FMath::Sqrt(DistSq) - Hole.RadiusCm;
		Result = FMath::Max(Result, -SphereDist);
	}

	return Result;
}

void ADigVolume::DigAt(const FVector& WorldLocation, float RadiusCm)
{
	// EvalSDF 里的 P 是"相对本 Actor 原点"的局部坐标，而射线给的是世界坐标。
	// Actor 一旦移动或旋转，两种坐标就不一样了，所以必须先转换。
	const FVector LocalPos = GetActorTransform().InverseTransformPosition(WorldLocation);

	// ① 先估算"这一下挖掉了多少体积"——必须在加洞之前算，否则量到的是挖完之后的状态
	const double RemovedVolume = EstimateRemovedVolume(LocalPos, RadiusCm);
	const double SphereVolume = SphereVolumeCm3((double)RadiusCm);

	FDigHole NewHole;
	NewHole.Center = LocalPos;
	NewHole.RadiusCm = RadiusCm;
	DigHoles.Add(NewHole);

	RebuildMesh();

	// ② 体积比例 → 三档 → 每档固定的重量
	const EDropSize Size = ClassifyDropSize(RemovedVolume, SphereVolume);
	const double Ratio = (SphereVolume > 0.0) ? (RemovedVolume / SphereVolume) : 0.0;

	const TCHAR* SizeName = TEXT("不掉（碎屑）");
	double WeightKg = 0.0;
	switch (Size)
	{
	case EDropSize::Small:  SizeName = TEXT("小块"); WeightKg = 0.3; break;
	case EDropSize::Medium: SizeName = TEXT("中块"); WeightKg = 1.0; break;
	case EDropSize::Large:  SizeName = TEXT("大块"); WeightKg = 2.5; break;
	default:                                             break;
	}

	UE_LOG(LogDig, Warning,
		TEXT("[Drop] 半径=%.1f 实心比=%.0f%% 挖掉=%.0f/%.0f cm³ → %s | 重量=%.1f kg | 位置=%.1f,%.1f,%.1f"),
		RadiusCm, Ratio * 100.0, RemovedVolume, SphereVolume, SizeName, WeightKg,
		WorldLocation.X, WorldLocation.Y, WorldLocation.Z);

	// ③ 生成实物
	if (Size != EDropSize::None && DropItemClass)
	{
		if (UWorld* World = GetWorld())
		{
			// 沿"从石头中心指向挖点"的方向往外挪一点，避免生成在岩石内部
			const FVector Outward = (WorldLocation - GetActorLocation()).GetSafeNormal();
			const FVector SpawnLoc = WorldLocation + Outward * 12.0f;

			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

			if (ADropItem* Drop = World->SpawnActor<ADropItem>(
				DropItemClass, SpawnLoc, FRotator::ZeroRotator, SpawnParams))
			{
				// 目标尺寸（最长边，厘米）。三个值都能在石头 Actor 的细节面板里调
				const float TargetSizeCm =
					(Size == EDropSize::Small)  ? DropSizeSmallCm :
					(Size == EDropSize::Medium) ? DropSizeMediumCm : DropSizeLargeCm;

				Drop->InitDrop(WeightKg, GoldContent, TargetSizeCm);

				UE_LOG(LogDig, Warning, TEXT("[Drop] 已生成 %s（%.1f kg，含金 %.0f%%）"),
					*GetNameSafe(Drop), WeightKg, GoldContent * 100.0f);
			}
		}
	}
}

double ADigVolume::EstimateRemovedVolume(const FVector3d& Center, float RadiusCm) const
{
	// 在球内随机撒点，数有多少个是"实心"（SDF < 0），用比例乘球体积。
	// 64 个点的误差约 ±12%，判断档位够用；成本约等于 0。
	const int32 NumSamples = 64;
	const double R = (double)RadiusCm;
	const double SphereVolume = SphereVolumeCm3(R);

	int32 SolidCount = 0;
	for (int32 i = 0; i < NumSamples; ++i)
	{
		// 球内均匀采样：方向随机，半径按 cbrt(u) 分布（否则会挤在球心附近）
		const FVector3d Dir = FMath::VRand();
		const double SampleRadius = R * FMath::Pow((double)FMath::FRand(), 1.0 / 3.0);
		const FVector3d P = Center + Dir * SampleRadius;

		if (EvalSDF(P) < 0.0)
		{
			++SolidCount;
		}
	}

	return SphereVolume * (double(SolidCount) / double(NumSamples));
}

ADigVolume::EDropSize ADigVolume::ClassifyDropSize(double RemovedVolume, double SphereVolume) const
{
	if (SphereVolume <= 0.0)
	{
		return EDropSize::None;
	}

	// 按"挖掉体积 / 整个球体积"的比例判定，和半径无关：
	//   这一下有多少比例真的啃到了实心石头
	const double Ratio = RemovedVolume / SphereVolume;

	if (Ratio < 0.05) { return EDropSize::None; }    // 几乎全在空气里 → 碎屑，不掉
	if (Ratio < 0.33) { return EDropSize::Small; }   // 蹭到一点
	if (Ratio < 0.66) { return EDropSize::Medium; }  // 正常命中
	return EDropSize::Large;                          // 几乎整个球都在实心里
}