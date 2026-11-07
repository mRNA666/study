// Fill out your copyright notice in the Description page of Project Settings.


#include "DigVolume.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/MeshNormals.h"
#include "Generators/MarchingCubes.h"
#include "HAL/PlatformTime.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY(LogDig);

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
		TEXT("[Dig] verts=%d tris=%d | MC=%.1f Copy=%.1f Normals=%.1f SetMesh=%.1f | 总计=%.1fms | holes=%d | size=%.0f step=%.1f amp=%.1f | 物理烘焙已延后"),
		VertCount, TriCount,
		(T1 - T0) * 1000.0, (T2 - T1) * 1000.0, (T3 - T2) * 1000.0,
		(T4 - T3) * 1000.0, (T5 - T0) * 1000.0, DigHoles.Num(),
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

	FDigHole NewHole;
	NewHole.Center = LocalPos;
	NewHole.RadiusCm = RadiusCm;
	DigHoles.Add(NewHole);

	RebuildMesh();
}