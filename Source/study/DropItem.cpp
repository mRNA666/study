// Fill out your copyright notice in the Description page of Project Settings.


#include "DropItem.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

ADropItem::ADropItem()
{
	PrimaryActorTick.bCanEverTick = false;

	MeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComp"));
	SetRootComponent(MeshComp);

	// 掉落物要能被物理推着走、能掉在地上
	MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	MeshComp->SetCollisionObjectType(ECC_PhysicsBody);
	MeshComp->SetCollisionResponseToAllChannels(ECR_Block);
	MeshComp->SetGenerateOverlapEvents(true);

	// 关键：不要挡住玩家。
	// 否则小石头会把角色顶起来（视角乱飞）、或者被角色一脚踢飞。
	// Overlap 表示"玩家穿过去，但仍然能被检测到"（以后做拾取用）。
	MeshComp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);

	MeshComp->SetSimulatePhysics(true);

	// 阻尼：让石头滚两下就停，不会一直转个不停
	MeshComp->SetLinearDamping(0.8f);
	MeshComp->SetAngularDamping(4.0f);
}

void ADropItem::InitDrop(float InWeightKg, float InGoldContent, float TargetSizeCm)
{
	WeightKg = InWeightKg;
	GoldContent = InGoldContent;
	bIsRubble = (GoldContent < 0.05f);

	// 运行时再强制设一次"不挡玩家"。
	// 原因：BP_DropItem 是把类保存进蓝图的，蓝图里的碰撞预设会覆盖构造函数里设的值，
	// 所以只在构造函数里设不保险，必须每次生成时再压一遍。
	MeshComp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);

	// 随机挑一个碎石模型
	if (RockMeshes.Num() > 0)
	{
		const int32 Index = FMath::RandRange(0, RockMeshes.Num() - 1);
		MeshComp->SetStaticMesh(RockMeshes[Index]);
	}

	// --- 尺寸归一化 ---
	// 源模型有多大是不确定的（Blender 里可能按米做的），所以按模型包围盒缩放到目标尺寸。
	// 这样"大块 12cm"就一定是 12cm，不会出现"比洞还大"。
	float NormalizeScale = 1.0f;
	if (const UStaticMesh* Mesh = MeshComp->GetStaticMesh())
	{
		const FBoxSphereBounds Bounds = Mesh->GetBounds();
		const float MeshRadius = FMath::Max3(Bounds.BoxExtent.X, Bounds.BoxExtent.Y, Bounds.BoxExtent.Z);
		if (MeshRadius > KINDA_SMALL_NUMBER)
		{
			// BoxExtent 是"半尺寸"，所以最长边 = 2 * MeshRadius
			NormalizeScale = (TargetSizeCm * 0.5f) / MeshRadius;
		}
	}

	// --- 物理质量 ---
	// 不设的话，UE 会按体积 × 默认密度算，一块 5cm 的石头只有几十克，一碰就飞。
	// 这里直接用设计重量当质量。
	MeshComp->SetMassOverrideInKg(NAME_None, FMath::Max(InWeightKg, 0.05f), true);

	// 随机旋转 + 随机非等比缩放（同一个模型也能看起来像不同的石头）
	const FRotator RandomRot(
		FMath::FRandRange(0.0f, 360.0f),
		FMath::FRandRange(0.0f, 360.0f),
		FMath::FRandRange(0.0f, 360.0f));

	const FVector RandomScale(
		NormalizeScale * FMath::FRandRange(0.90f, 1.10f),
		NormalizeScale * FMath::FRandRange(0.80f, 1.00f),
		NormalizeScale * FMath::FRandRange(0.70f, 1.00f));

	SetActorRotation(RandomRot);
	MeshComp->SetWorldScale3D(RandomScale);

	// 掉下来时轻轻弹一下（很快衰减，不会乱飞）
	const FVector Impulse(
		FMath::FRandRange(-20.0f, 20.0f),
		FMath::FRandRange(-20.0f, 20.0f),
		FMath::FRandRange(30.0f, 60.0f));
	MeshComp->AddImpulse(Impulse, NAME_None, /*bVelChange*/ true);
}
