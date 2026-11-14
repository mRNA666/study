// Copyright Epic Games, Inc. All Rights Reserved.

#include "studyCharacter.h"
#include "Animation/AnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "InputActionValue.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "BagHUD.h"
#include "GameFramework/PlayerController.h"
#include "DigVolume.h"
#include "DropItem.h"
#include "studyGameState.h"
#include "studyGameInstance.h"
#include "Engine/OverlapResult.h"
#include "Merchant.h"
#include "StudySounds.h"
#include "Kismet/GameplayStatics.h"
#include "LadderActor.h"
#include "EngineUtils.h"
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

	// 预热音效缓存：第一次 LoadObject 要读盘，正好撞上第一下挖掘会听出延迟
	StudySounds::Dig();
	StudySounds::Pickup();
	StudySounds::Debris();

	// 装备栏的初始状态（镐子显示/隐藏）
}

void AstudyCharacter::ShowTransition(const FString& Text, float Seconds)
{
	TransitionText = Text;
	TransitionTimeLeft = FMath::Max(Seconds, 0.f);
}

void AstudyCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (TransitionTimeLeft > 0.f)
	{
		TransitionTimeLeft = FMath::Max(TransitionTimeLeft - DeltaSeconds, 0.f);
	}

	// ---- 脚步：按"走过的距离"触发，而不是按时间 ----
	// 按时间触发的话，跑起来脚步会变稀、慢走会变密，听起来很假。
	const UCharacterMovementComponent* Move = GetCharacterMovement();
	const float Speed2D = Move ? Move->Velocity.Size2D() : 0.f;

	if (Move && Move->IsMovingOnGround() && Speed2D > 40.f)
	{
		FootstepAccumCm += Speed2D * DeltaSeconds;

		if (FootstepAccumCm >= FootstepDistanceCm)
		{
			FootstepAccumCm = 0.f;
			// 脚步音效暂时关掉（素材不匹配，听起来像打鼓）
		}
	}
	else
	{
		// 停下来时归零，避免下次起步就立刻响一声
		FootstepAccumCm = 0.f;
	}

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
		AActor* HitActor = Hit.GetActor();
		FocusedDrop = Cast<ADropItem>(HitActor);

		// 准星指到梯子 → HUD 显示"按 E 爬梯"
		bFocusingLadder = HitActor
			&& (Cast<ALadderActor>(HitActor) != nullptr || HitActor->ActorHasTag(TEXT("Ladder")));
	}
	else
	{
		bFocusingLadder = false;
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

	// 拾取音效（Bag_Handle_1-5 开头有 90ms 静音，跳过它）
	UGameplayStatics::SpawnSound2D(this, StudySounds::Pickup(), 0.7f,
		FMath::FRandRange(0.95f, 1.08f), PickupSoundStartTime);

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

	// 清掉"已开局"标记，这样新关卡 BeginPlay 时会 ResetRun 重开一局
	if (UstudyGameInstance* GI = Cast<UstudyGameInstance>(UGameplayStatics::GetGameInstance(this)))
	{
		GI->bRunStarted = false;
	}

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

		// T：开关头灯（矿洞里黑，和 E 分开一个键）
		PlayerInputComponent->BindKey(EKeys::T, IE_Pressed, this, &AstudyCharacter::ToggleHeadLight);

		// 对话：数字键选选项 / 输入数字，Enter 确认，Backspace 退格，Esc 取消
		PlayerInputComponent->BindKey(EKeys::Zero, IE_Pressed, this, &AstudyCharacter::DialogDigit0);
		PlayerInputComponent->BindKey(EKeys::One, IE_Pressed, this, &AstudyCharacter::DialogDigit1);
		PlayerInputComponent->BindKey(EKeys::Two, IE_Pressed, this, &AstudyCharacter::DialogDigit2);
		PlayerInputComponent->BindKey(EKeys::Three, IE_Pressed, this, &AstudyCharacter::DialogDigit3);
		PlayerInputComponent->BindKey(EKeys::Four, IE_Pressed, this, &AstudyCharacter::DialogDigit4);
		PlayerInputComponent->BindKey(EKeys::Five, IE_Pressed, this, &AstudyCharacter::DialogDigit5);
		PlayerInputComponent->BindKey(EKeys::Six, IE_Pressed, this, &AstudyCharacter::DialogDigit6);
		PlayerInputComponent->BindKey(EKeys::Seven, IE_Pressed, this, &AstudyCharacter::DialogDigit7);
		PlayerInputComponent->BindKey(EKeys::Eight, IE_Pressed, this, &AstudyCharacter::DialogDigit8);
		PlayerInputComponent->BindKey(EKeys::Nine, IE_Pressed, this, &AstudyCharacter::DialogDigit9);
		PlayerInputComponent->BindKey(EKeys::Enter, IE_Pressed, this, &AstudyCharacter::DialogConfirm);
		PlayerInputComponent->BindKey(EKeys::BackSpace, IE_Pressed, this, &AstudyCharacter::DialogBackspace);
		// ESC：没开对话框时开关设置菜单，开了就取消对话（在 OnEscapePressed 里分发）
		PlayerInputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &AstudyCharacter::OnEscapePressed);

		// Tab：资产排行（按住看，再按一下关）
		PlayerInputComponent->BindKey(EKeys::Tab, IE_Pressed, this, &AstudyCharacter::OnRankPressed);
	}
}

void AstudyCharacter::OpenOptionDialog(const FString& Title, const FString& Body, const TArray<FString>& Options)
{
	DialogMode = EDialogMode::Options;
	DialogTitle = Title;
	DialogBody = Body;
	DialogOptions = Options;
	DialogPrompt.Reset();
	DialogInput.Reset();
}

void AstudyCharacter::OpenNumberDialog(const FString& Title, const FString& Body, const FString& Prompt, float MaxValue)
{
	DialogMode = EDialogMode::NumberInput;
	DialogTitle = Title;
	DialogBody = Body;
	DialogPrompt = Prompt;
	DialogMaxValue = FMath::Max(MaxValue, 0.f);
	DialogOptions.Reset();
	DialogInput.Reset();
}

void AstudyCharacter::CloseDialog()
{
	DialogMode = EDialogMode::None;
	DialogTitle.Reset();
	DialogBody.Reset();
	DialogPrompt.Reset();
	DialogInput.Reset();
	DialogOptions.Reset();
	DialogMerchant = nullptr;
}

void AstudyCharacter::OnDialogDigit(int32 Digit)
{
	if (DialogMode == EDialogMode::Options)
	{
		// 选项模式：1/2/3 直接选
		if (Digit >= 1 && Digit <= DialogOptions.Num())
		{
			DialogChoose(Digit - 1);
		}
		return;
	}

	if (DialogMode == EDialogMode::NumberInput)
	{
		// 数字模式：往输入框里塞
		if (DialogInput.Len() < 6)
		{
			DialogInput.AppendChar(static_cast<TCHAR>(TEXT('0') + Digit));
		}
		return;
	}
}

void AstudyCharacter::DialogBackspace()
{
	if (DialogMode != EDialogMode::NumberInput || DialogInput.IsEmpty())
	{
		return;
	}

	DialogInput.LeftChopInline(1);
}

void AstudyCharacter::DialogConfirm()
{
	if (DialogMode != EDialogMode::NumberInput)
	{
		return;
	}

	const float Value = FCString::Atof(*DialogInput);

	// 先关对话再回调，避免回调里又开新对话被清掉
	AMerchant* M = DialogMerchant;
	CloseDialog();

	if (M && Value > 0.f)
	{
		M->OnDialogNumber(this, Value);
	}
}

void AstudyCharacter::DialogSubmitNumber(float Value)
{
	// 行内输入：不关对话 —— 让商人自己决定要不要重刷一遍内容
	AMerchant* M = DialogMerchant;
	if (!M || Value <= 0.f)
	{
		return;
	}

	M->OnDialogNumber(this, Value);
}

void AstudyCharacter::DialogCancel()
{
	CloseDialog();
}

void AstudyCharacter::DialogChoose(int32 Index)
{
	if (DialogMode != EDialogMode::Options || !DialogOptions.IsValidIndex(Index))
	{
		return;
	}

	AMerchant* M = DialogMerchant;
	CloseDialog();

	if (M)
	{
		M->OnDialogOption(this, Index);
	}
}

void AstudyCharacter::OnRankPressed()
{
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (ABagHUD* HUD = Cast<ABagHUD>(PC->GetHUD()))
		{
			HUD->ToggleRank();
		}
	}
}

void AstudyCharacter::OnEscapePressed()
{
	// 有对话框时：ESC 还是「取消对话」
	if (IsDialogOpen())
	{
		DialogCancel();
		return;
	}

	// 没对话时：开关 ESC 设置菜单
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (ABagHUD* HUD = Cast<ABagHUD>(PC->GetHUD()))
		{
			HUD->TogglePauseMenu();
		}
	}
}

void AstudyCharacter::SetHeadLightScale(float Scale)
{
	if (HeadLight)
	{
		HeadLight->SetIntensity(HeadLightIntensity * FMath::Clamp(Scale, 0.f, 3.f));
	}
}

void AstudyCharacter::ToggleHeadLight()
{
	if (!HeadLight)
	{
		return;
	}

	bHeadLightOn = !bHeadLightOn;
	HeadLight->SetVisibility(bHeadLightOn, true);

	UGameplayStatics::PlaySound2D(this, StudySounds::Switch(), 0.7f);

	UE_LOG(LogTemplateCharacter, Log, TEXT("[HeadLight] %s"), bHeadLightOn ? TEXT("开") : TEXT("关"));
}

void AstudyCharacter::OnPickupPressed()
{
	// ① 对话开着的时候，E 只是关掉对话
	if (IsDialogOpen())
	{
		CloseDialog();
		return;
	}

	// ② 附近有商人：优先和他交互（营地里按 E 不该去捡石头）
	if (AMerchant* Nearby = FindNearbyMerchant())
	{
		Nearby->Interact(this);
		return;
	}

	// ③ 捡东西（准星对准的，或者附近最近的）
	TryPickup();

	// 按住 E 连续捡：每 PickupInterval 秒尝试捡一块
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			PickupRepeatHandle, this, &AstudyCharacter::TryPickup,
			FMath::Max(PickupInterval, 0.02f), /*bLoop*/ true);
	}
}

AMerchant* AstudyCharacter::FindNearbyMerchant() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	for (TActorIterator<AMerchant> It(World); It; ++It)
	{
		// 隐藏的 NPC（比如第 1 天还没来的正规买家）不算"可交互"：
		// 否则 HUD 会提示"按 E 说话"，而且 E 会被一个看不见的人吃掉。
		if (!It->IsHidden() && It->IsInRange(this))
		{
			return *It;
		}
	}

	return nullptr;
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
		// 一次挖掘是一个半径 DigRadiusCm 的球，它会同时切到相邻的好几块。
		// 如果只挖射线打中的那一块，洞的边缘就会被邻居的实体挡住 ——
		// 玩家看到的现像就是"挖了但挖不穿 / 挖开又合上"。
		//
		// 做法：把球碰到的所有 DigVolume 都挖一遍，
		// **把各块挖掉的体积加起来**，最后只生成【一块】掉落物。
		ADigVolume* Primary = Cast<ADigVolume>(Hit.GetActor());

		// 先出声，再干活：
		// 挖一次要重建网格（2~3ms）再生成掉落物，等这些做完才播声音，
		// 耳朵能听出"敲了但没响"的延迟。所以一确认打到石头就立刻播。
		//
		// StartTime：这些素材开头带一点静音，用 SpawnSoundAtLocation 的 StartTime
		// 直接跳过它（不用去改资产）。可在角色蓝图的 Feel 分类里调。
		if (Primary)
		{
			// 用 2D：第一人称下音源就在耳朵边，不需要衰减定位。
			// （打包版里带衰减的 3D 音效会在 FAudioDevice::CreateComponent 里崩）
			UGameplayStatics::SpawnSound2D(this, StudySounds::Dig(),
				0.85f, FMath::FRandRange(0.88f, 1.12f), DigSoundStartTime);
		}

		TArray<FOverlapResult> Overlaps;
		const FCollisionShape DigSphere = FCollisionShape::MakeSphere(DigRadiusCm);
		GetWorld()->OverlapMultiByChannel(
			Overlaps, Hit.ImpactPoint, FQuat::Identity, ECC_Visibility, DigSphere, Params);

		float TotalRemovedCm3 = 0.f;
		ADigVolume* AnyDugVolume = nullptr;

		for (const FOverlapResult& Overlap : Overlaps)
		{
			if (ADigVolume* Vol = Cast<ADigVolume>(Overlap.GetActor()))
			{
				const float Removed = Vol->CarveAt(Hit.ImpactPoint, DigRadiusCm);
				TotalRemovedCm3 += Removed;
				if (Removed > 0.f && !AnyDugVolume)
				{
					AnyDugVolume = Vol;
				}
			}
		}

		// 兜底：范围查询没碰到（碰撞体和网格可能有偏差），至少挖掉射线打到的那块
		if (!AnyDugVolume && Primary)
		{
			TotalRemovedCm3 += Primary->CarveAt(Hit.ImpactPoint, DigRadiusCm);
			AnyDugVolume = Primary;
		}

		if (TotalRemovedCm3 > 0.f && AnyDugVolume)
		{
			// 挖掘球的体积，用来算"这一下挖到的实心比例"
			const float SphereVolumeCm3 = static_cast<float>(
				4.0 / 3.0 * PI * FMath::Pow(static_cast<double>(DigRadiusCm), 3.0));

			AnyDugVolume->SpawnDropForRemovedVolume(TotalRemovedCm3, SphereVolumeCm3, Hit.ImpactPoint);

			// 真挖到东西了才震（打空气不震）
			TriggerDigShake();
		}
	}
}
