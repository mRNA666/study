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
	RebuildMesh();
}

void ADigVolume::RebuildMesh()
{
	using namespace UE::Geometry;

	const double H = SizeCm * 0.5;

	auto FBM = [](const FVector3d& P, double Scale)->double
	{
			double Sum = 0.0, OctaveAmp = 1.0, Freq = 1.0;
			for(int32 i=0;i<3;i++)
			{
				Sum += OctaveAmp * FMath::PerlinNoise3D(FVector(P.X * Scale * Freq,
					P.Y * Scale * Freq,
					P.Z * Scale * Freq));
				OctaveAmp *= 0.5;
				Freq *= 2.0;
			}
			return Sum;
	};
	const double Amp = NoiseAmplitudeCm;
	const double Scale = NoiseScale;

	auto StoneSDF = [H, Amp, Scale, FBM](const FVector3d& P) -> double
		{
			const FVector3d Q(FMath::Abs(P.X) - H,
				FMath::Abs(P.Y) - H,
				FMath::Abs(P.Z) - H);
			const FVector3d Outside(FMath::Max(Q.X, 0.0),
				FMath::Max(Q.Y, 0.0),
				FMath::Max(Q.Z, 0.0));
			const double Inside = FMath::Min(FMath::Max(Q.X, FMath::Max(Q.Y, Q.Z)), 0.0);
			const double BoxDist = Outside.Length() + Inside;

			return BoxDist + FBM(P, Scale) * Amp;
		};


	auto Field = [StoneSDF](const FVector3d& P) -> double
	{
		return -StoneSDF(P);
	};

	FMarchingCubes MC;
	MC.Implicit = Field;
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
		UE_LOG(LogDig, Error, TEXT("[Dig] Mesh,copy failed - MC produced no triangles"));
		return;
	}

	UE_LOG(LogDig, Warning,
		TEXT("[Dig] MC verts=%d tris=%d size=%.0f step=%.1f amp=%.1f scale=%.3f par=%d cache=%d build=%.1fms"),
		Mesh.VertexCount(), Mesh.TriangleCount(),
		SizeCm, SampleStepCm, NoiseAmplitudeCm, NoiseScale,
		MC.bParallelCompute ? 1 : 0, MC.bEnableValueCaching ? 1 : 0,
		BuildMs);

	FMeshNormals::QuickComputeVertexNormals(Mesh);

	// ④ 交给组件
	MeshComp->SetMesh(MoveTemp(Mesh));
}