// Fill out your copyright notice in the Description page of Project Settings.


#include "DigVolume.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/MeshNormals.h"
#include "Generators/MarchingCubes.h"
#include "HAL/PlatformTime.h"

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
}

void ADigVolume::RebuildMesh()
{
	using namespace UE::Geometry;

	const double H = SizeCm * 0.5;
	const double Amp = NoiseAmplitudeCm;

	FMarchingCubes MC;
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
	const double BuildMs = (FPlatformTime::Seconds() - T0) * 1000.0;
	FDynamicMesh3 Mesh;
	if (!Mesh.Copy(&MC)) {
		UE_LOG(LogDig, Error, TEXT("[Dig] Mesh.copy failed - MC produced no triangles"));
		return;
	}

	UE_LOG(LogDig, Warning,
		TEXT("[Dig] MC verts=%d tris=%d size=%.0f step=%.1f amp=%.1f scale=%.3f par=%d cache=%d build=%.1fms"),
		Mesh.VertexCount(), Mesh.TriangleCount(),
		SizeCm, SampleStepCm, NoiseAmplitudeCm, NoiseScale,
		MC.bParallelCompute ? 1 : 0, MC.bEnableValueCaching ? 1 : 0,
		BuildMs);

	FMeshNormals::QuickComputeVertexNormals(Mesh);

	MeshComp->SetMesh(MoveTemp(Mesh));

	// 让射线/角色能撞到这块石头：UDynamicMeshComponent 默认是 NoCollision，必须显式打开
	MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	MeshComp->SetCollisionObjectType(ECC_WorldStatic);
	MeshComp->SetCollisionResponseToAllChannels(ECR_Block);
	MeshComp->EnableComplexAsSimpleCollision();
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
		const double SphereDist = (P - Hole.Center).Length() - Hole.RadiusCm;
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