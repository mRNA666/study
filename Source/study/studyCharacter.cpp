// Copyright Epic Games, Inc. All Rights Reserved.

#include "studyCharacter.h"
#include "Animation/AnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SpotLightComponent.h"
#include "EnhancedInputComponent.h"
#include "InputActionValue.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "DigVolume.h"
#include "DropItem.h"
#include "studyGameState.h"
#include "study.h"

DEFINE_LOG_CATEGORY(LogTemplateCharacter);

AstudyCharacter::AstudyCharacter()
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(55.f, 96.0f);
	
	// Create the first person mesh that will be viewed only by this character's owner
	FirstPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("First Person Mesh"));

	FirstPersonMesh->SetupAttachment(GetMesh());
	FirstPersonMesh->SetOnlyOwnerSee(true);
	FirstPersonMesh->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson;
	FirstPersonMesh->SetCollisionProfileName(FName("NoCollision"));

	// Create the Camera Component	
	FirstPersonCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("First Person Camera"));
	FirstPersonCameraComponent->SetupAttachment(FirstPersonMesh, FName("head"));
	FirstPersonCameraComponent->SetRelativeLocationAndRotation(FVector(-2.8f, 5.89f, 0.0f), FRotator(0.0f, 90.0f, -90.0f));
	FirstPersonCameraComponent->bUsePawnControlRotation = true;
	FirstPersonCameraComponent->bEnableFirstPersonFieldOfView = true;
	FirstPersonCameraComponent->bEnableFirstPersonScale = true;
	FirstPersonCameraComponent->FirstPersonFieldOfView = 70.0f;
	FirstPersonCameraComponent->FirstPersonScale = 0.6f;

	// configure the character comps
	GetMesh()->SetOwnerNoSee(true);
	GetMesh()->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::WorldSpaceRepresentation;

	GetCapsuleComponent()->SetCapsuleSize(34.0f, 96.0f);

	// Configure character movement
	GetCharacterMovement()->BrakingDecelerationFalling = 1500.0f;
	GetCharacterMovement()->AirControl = 0.5f;

	// 记住空手时的移速，后面按负重往下调
	BaseMaxWalkSpeed = GetCharacterMovement()->MaxWalkSpeed;

	// 明确打开 Tick：准星焦点每帧要更新一次
	PrimaryActorTick.bCanEverTick = true;

	// 头灯：挂在第一人称相机上，跟着视角走。矿洞里没有它什么都看不见。
	HeadLight = CreateDefaultSubobject<USpotLightComponent>(TEXT("HeadLight"));
	HeadLight->SetupAttachment(FirstPersonCameraComponent);
	HeadLight->SetRelativeLocation(FVector(8.f, 0.f, -8.f));
	HeadLight->SetIntensity(HeadLightIntensity);
	HeadLight->SetAttenuationRadius(HeadLightRadiusCm);
	HeadLight->SetInnerConeAngle(16.f);
	HeadLight->SetOuterConeAngle(38.f);
	HeadLight->SetLightColor(FLinearColor(1.f, 0.94f, 0.82f));
	HeadLight->SetCastShadows(true);
}

void AstudyCharacter::BeginPlay()
{
	Super::BeginPlay();

	// 相机位置：由 CameraOffset 决定（可在角色蓝图的类默认值里调）
	if (UCameraComponent* Cam = GetFirstPersonCameraComponent())
	{
		Cam->SetRelativeLocation(CameraOffset);
	}

	// 头灯参数走 UPROPERTY，方便在角色蓝图里调
	if (HeadLight)
	{
		HeadLight->SetRelativeLocation(HeadLightOffset);
		HeadLight->SetIntensity(HeadLightIntensity);
		HeadLight->SetAttenuationRadius(HeadLightRadiusCm);
	}

	// 记住相机原始相对位置：震屏是从这里偏移的，结束时要还原回来。
	// 必须放在设完 CameraOffset 之后。
	if (UCameraComponent* Cam = GetFirstPersonCameraComponent())
	{
		CameraBaseRelativeLocation = Cam->GetRelativeLocation();
	}
}

void AstudyCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 每帧更新一次"准星对准了哪块石头"。只做一次直线检测，很便宜。
	UpdateFocus();

	// 震屏偏移（没有在震的话直接返回）
	UpdateCameraShake(DeltaSeconds);
}

void AstudyCharacter::UpdateFocus()
{
	FocusedDrop = nullptr;

	UCameraComponent* Cam = GetFirstPersonCameraComponent();
	UWorld* World = GetWorld();
	if (!Cam || !World)
	{
		return;
	}

	const FVector Start = Cam->GetComponentLocation();
	const FVector End = Start + Cam->GetForwardVector() * FocusReachCm;

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PickupFocus), false, this);

	// 走 Visibility 通道：石头（DigVolume）和掉落物都会挡住它，
	// 所以被岩石遮住的石头不会被"隔墙识别"。
	if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
	{
		FocusedDrop = Cast<ADropItem>(Hit.GetActor());
	}
}

void AstudyCharacter::TryPickup()
{
	UE_LOG(LogTemplateCharacter, Warning, TEXT("[Bag] 按了 E，准星对准的是：%s"),
		FocusedDrop ? *GetNameSafe(FocusedDrop) : TEXT("（没对准任何石头）"));

	// 只捡准星对准的那一块。没对准就什么都不做（不做"附近兜底"）。
	ADropItem* Best = FocusedDrop;
	if (!Best)
	{
		return;
	}

	// 背包装不下就不捡（PRD：装满了就装不进去）
	if (CarriedWeightKg + Best->WeightKg > MaxCarryKg)
	{
		UE_LOG(LogTemplateCharacter, Warning,
			TEXT("[Bag] 背包装不下 %.1f kg（当前 %.1f / %.1f）"),
			Best->WeightKg, CarriedWeightKg, MaxCarryKg);
		return;
	}

	const float Added = Best->WeightKg;
	const float Gold = Best->GoldContent;
	const float GoldGrams = Best->GoldGrams;
	const bool bRubble = Best->bIsRubble;

	CarriedWeightKg += Added;
	CarriedGoldGrams += GoldGrams;
	Best->Destroy();

	UpdateSpeedPenalty();

	UE_LOG(LogTemplateCharacter, Warning,
		TEXT("[Bag] 捡起 %.1f kg %s（含金 %.0f%%，值 %.2f g）→ %.1f / %.1f kg（%.0f%%）移速 x%.2f"),
		Added, bRubble ? TEXT("石渣") : TEXT("矿石"), Gold * 100.0f, GoldGrams,
		CarriedWeightKg, MaxCarryKg,
		(CarriedWeightKg / MaxCarryKg) * 100.0f,
		GetCharacterMovement()->MaxWalkSpeed / BaseMaxWalkSpeed);
}

float AstudyCharacter::UnloadAll(){
	const float Delivered = CarriedGoldGrams;
	if (CarriedWeightKg <= 0.f && Delivered <= 0.f)
	{
		return 0.f;
	}

	UE_LOG(LogTemplateCharacter, Warning,
		TEXT("[Bag] 卸货：放下 %.1f kg，交回 %.2f g 黄金"), CarriedWeightKg, Delivered);

	CarriedWeightKg = 0.f;
	CarriedGoldGrams = 0.f;
	UpdateSpeedPenalty();

	return Delivered;
}

void AstudyCharacter::RequestRestart()
{
	// 只在结算之后才允许重开
	const UWorld* World = GetWorld();
	const AstudyGameState* GS = World ? World->GetGameState<AstudyGameState>() : nullptr;
	if (!GS || GS->GetPhase() != EGamePhase::Finished)
	{
		return;
	}

	// 重新加载当前关卡。
	// GetCurrentLevelName 的第二个参数会把 PIE 前缀（UEDPIE_0_）去掉，
	// 不去的话在编辑器里会找不到关卡名。
	const FString LevelName = UGameplayStatics::GetCurrentLevelName(this, /*bRemovePrefixString*/ true);
	UE_LOG(LogTemplateCharacter, Warning, TEXT("[Match] 按 R 重开：%s"), *LevelName);
	UGameplayStatics::OpenLevel(this, FName(*LevelName));
}

void AstudyCharacter::UpdateSpeedPenalty()
{
	// PRD：背包越满走得越慢，装满时只剩 60%
	const float LoadRatio = FMath::Clamp(CarriedWeightKg / FMath::Max(MaxCarryKg, 1.f), 0.f, 1.f);
	const float Scale = FMath::Lerp(1.f, FullLoadSpeedScale, LoadRatio);

	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->MaxWalkSpeed = BaseMaxWalkSpeed * Scale;
	}
}

void AstudyCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{	
	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		// Jumping
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &AstudyCharacter::DoJumpStart);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &AstudyCharacter::DoJumpEnd);
		// Digging：按下挖一次并开始连挖，松开停止
		EnhancedInputComponent->BindAction(DigAction, ETriggerEvent::Started, this, &AstudyCharacter::DoDig);
		EnhancedInputComponent->BindAction(DigAction, ETriggerEvent::Completed, this, &AstudyCharacter::StopDigRepeat);
		// Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AstudyCharacter::MoveInput);

		// Looking/Aiming
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AstudyCharacter::LookInput);
		EnhancedInputComponent->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &AstudyCharacter::LookInput);
	}
	else
	{
		UE_LOG(Logstudy, Error, TEXT("'%s' Failed to find an Enhanced Input Component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
	}

	// 按 E 拾取。
	// 这里用传统按键绑定（BindKey），不用建 IA 资产 —— 少一步编辑器配置。
	// 注意：ClimbComponent 也绑了 E（爬梯子）。两个绑定会同时响应，
	// 站在梯子旁边又踩着石头时会一起触发。以后要合并再说。
	if (PlayerInputComponent)
	{
		// E：按下立刻捡一块，按住每 PickupInterval 秒再捡一块
		PlayerInputComponent->BindKey(EKeys::E, IE_Pressed, this, &AstudyCharacter::OnPickupPressed);
		PlayerInputComponent->BindKey(EKeys::E, IE_Released, this, &AstudyCharacter::OnPickupReleased);

		// R：结算之后重开一局（其他阶段按了没反应）
		PlayerInputComponent->BindKey(EKeys::R, IE_Pressed, this, &AstudyCharacter::RequestRestart);
	}
}

void AstudyCharacter::OnPickupPressed()
{
	TryPickup();

	// 按住 E 连续捡：每 PickupInterval 秒尝试捡一块
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			PickupRepeatHandle, this, &AstudyCharacter::TryPickup,
			FMath::Max(PickupInterval, 0.02f), /*bLoop*/ true);
	}
}

void AstudyCharacter::OnPickupReleased()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PickupRepeatHandle);
	}
}

void AstudyCharacter::TriggerDigShake()
{
	// 没在震的时候才重新取基准位置（正在震时取会把偏移当成基准，越震越偏）
	if (ShakeTimeLeft <= 0.f)
	{
		if (UCameraComponent* Cam = GetFirstPersonCameraComponent())
		{
			CameraBaseRelativeLocation = Cam->GetRelativeLocation();
		}
	}

	ShakeTimeLeft = DigShakeDuration;
	ShakeTotalTime = FMath::Max(DigShakeDuration, 0.001f);
	ShakeAmplitude = DigShakeOffsetCm;
}

void AstudyCharacter::UpdateCameraShake(float DeltaSeconds)
{
	UCameraComponent* Cam = GetFirstPersonCameraComponent();
	if (!Cam || ShakeTimeLeft <= 0.f)
	{
		return;
	}

	ShakeTimeLeft -= DeltaSeconds;

	if (ShakeTimeLeft <= 0.f)
	{
		// 结束：还原到原位
		Cam->SetRelativeLocation(CameraBaseRelativeLocation);
		return;
	}

	// 越接近结束，幅度越小（线性衰减）
	const float Alpha = ShakeTimeLeft / ShakeTotalTime;
	const float Amp = ShakeAmplitude * Alpha;

	const FVector Offset(
		FMath::FRandRange(-Amp, Amp),
		FMath::FRandRange(-Amp, Amp),
		FMath::FRandRange(-Amp, Amp));

	Cam->SetRelativeLocation(CameraBaseRelativeLocation + Offset);
}


void AstudyCharacter::MoveInput(const FInputActionValue& Value)
{
	// get the Vector2D move axis
	FVector2D MovementVector = Value.Get<FVector2D>();

	// pass the axis values to the move input
	DoMove(MovementVector.X, MovementVector.Y);

}

void AstudyCharacter::LookInput(const FInputActionValue& Value)
{
	// get the Vector2D look axis
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	// pass the axis values to the aim input
	DoAim(LookAxisVector.X, LookAxisVector.Y);

}

void AstudyCharacter::DoAim(float Yaw, float Pitch)
{
	if (GetController())
	{
		// pass the rotation inputs
		AddControllerYawInput(Yaw);
		AddControllerPitchInput(Pitch);
	}
}

void AstudyCharacter::DoMove(float Right, float Forward)
{
	if (GetController())
	{
		// pass the move inputs
		AddMovementInput(GetActorRightVector(), Right);
		AddMovementInput(GetActorForwardVector(), Forward);
	}
}

void AstudyCharacter::DoJumpStart()
{
	// pass Jump to the character
	Jump();
}

void AstudyCharacter::DoJumpEnd()
{
	// pass StopJumping to the character
	StopJumping();
}
void AstudyCharacter::DoDig()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 只有"进行中"阶段才能挖。准备阶段和结算之后按左键不生效。
	// 取不到我们的 GameState 时（比如在别的测试地图里跑）不拦截，保持原行为。
	if (const AstudyGameState* GS = World->GetGameState<AstudyGameState>())
	{
		if (GS->GetPhase() != EGamePhase::InProgress)
		{
			return;
		}
	}

	// 冷却中：忽略这次（长按的循环计时器会自己再叫）
	const double Now = World->GetTimeSeconds();
	if (Now - LastDigTime < (double)DigCooldownSeconds)
	{
		return;
	}
	LastDigTime = Now;

	PerformDig();

	// 按住左键：每 DigCooldownSeconds 自动再挖一次
	World->GetTimerManager().SetTimer(
		DigRepeatHandle, this, &AstudyCharacter::DoDigRepeat, DigCooldownSeconds, /*bLoop*/ true);
}

void AstudyCharacter::DoDigRepeat()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 阶段变了（结算了）：停掉连挖，别让它一直空转
	if (const AstudyGameState* GS = World->GetGameState<AstudyGameState>())
	{
		if (GS->GetPhase() != EGamePhase::InProgress)
		{
			World->GetTimerManager().ClearTimer(DigRepeatHandle);
			return;
		}
	}

	// 计时器回调：周期本身就等于冷却，所以这里不再检查
	LastDigTime = World->GetTimeSeconds();
	PerformDig();
}

void AstudyCharacter::StopDigRepeat()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DigRepeatHandle);
	}
}

void AstudyCharacter::PerformDig()
{
	UCameraComponent* Cam = GetFirstPersonCameraComponent();
	if (!Cam || !GetWorld())
	{
		return;
	}

	// 挖掘射程由成员变量 DigReachCm 决定（默认 230cm，可在角色蓝图里调）

	const FVector Start = Cam->GetComponentLocation();
	const FVector End   = Start + Cam->GetForwardVector() * DigReachCm;

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DigTrace), false, this);

	const bool bHit = GetWorld()->LineTraceSingleByChannel(
		Hit, Start, End, ECC_Visibility, Params);

	UE_LOG(LogTemplateCharacter, Warning,
		TEXT("[Dig] hit=%d actor=%s loc=%s"),
		bHit ? 1 : 0,
		*GetNameSafe(Hit.GetActor()),
		*Hit.ImpactPoint.ToString());

	if (bHit)
	{
		if (ADigVolume* DigVolume = Cast<ADigVolume>(Hit.GetActor()))
		{
			DigVolume->DigAt(Hit.ImpactPoint, 20.f);

			// 挖中了才震一下（打空气不震）
			TriggerDigShake();
		}
	}
}
