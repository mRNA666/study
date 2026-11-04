// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DigVolume.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogDig, Log, All);

class UDynamicMeshComponent;

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

	UPROPERTY(EditAnywhere,Category = "Dig")
	float SizeCm = 100.0f;

	UPROPERTY(EditAnywhere, Category = "Dig")
	float SampleStepCm = 2.f;

	UPROPERTY(EditAnywhere, Category = "Dig")
	float NoiseAmplitudeCm = 6.f;

	UPROPERTY(EditAnywhere, Category = "Dig")
	float NoiseScale = 0.03f;

	UPROPERTY(EditAnywhere, Category = "Dig")
	TArray<FDigHole> DigHoles;

protected :
	virtual void OnConstruction(const FTransform& Transform) override;
	double EvalFBM(const FVector3d& P, double Scale) const;
	double EvalSDF(const FVector3d& P) const;

	void RebuildMesh();

};
