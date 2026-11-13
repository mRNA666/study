// Copyright Epic Games, Inc. All Rights Reserved.

#include "studyGameMode.h"
#include "BagHUD.h"
#include "studyGameState.h"
#include "studyGameInstance.h"
#include "UObject/ConstructorHelpers.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMeshActor.h"
#include "Components/LightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "MineGenerator.h"
#include "Merchant.h"
#include "GameFramework/PlayerStart.h"
#include "EngineUtils.h"
#include "studyCharacter.h"
#include "TimerManager.h"

namespace
{
	/** 把"分钟"格式化成 08:35 */
	FString FormatClock(float Minutes)
	{
		const int32 Total = FMath::FloorToInt(Minutes);
		return FString::Printf(TEXT("%02d:%02d"), Total / 60, Total % 60);
	}
}

AstudyGameMode::AstudyGameMode()
{
	// HUD：如果找得到蓝图子类 BP_BagHUD 就用它 —— 这样字号 / 边距 / 颜色
	// 都能在编辑器里调，不用改代码重编译；找不到就退回 C++ 类本身。
	// 蓝图要放在 Content/UI/BP_BagHUD（父类选 BagHUD）。
	static ConstructorHelpers::FClassFinder<ABagHUD> HUDBP(TEXT("/Game/UI/BP_BagHUD"));
	if (HUDBP.Succeeded())
	{
		HUDClass = HUDBP.Class;
	}
	else
	{
		HUDClass = ABagHUD::StaticClass();
	}

	// 让引擎为本局创建我们的 GameState，而不是默认的 AGameStateBase。
	// 注意：这个值在蓝图里可以被覆盖，所以 BeginPlay 里还要检查一遍
	GameStateClass = AstudyGameState::StaticClass();

	// 时钟推进需要 Tick
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

void AstudyGameMode::BeginPlay()
{
	Super::BeginPlay();

	AstudyGameState* GS = GetStudyGameState();
	UstudyGameInstance* GI = Cast<UstudyGameInstance>(UGameplayStatics::GetGameInstance(this));

	if (!GS)
	{
		// BP 里如果手动改过 "Game State Class"，构造函数里的设置会被它的存档值覆盖
		UE_LOG(LogTemp, Error,
			TEXT("[Match] GameState 不是 AstudyGameState！请打开 BP_FirstPersonGameMode，"
				 "把 Game State Class 设成 studyGameState"));
		return;
	}

	if (!GI)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[Match] Game Instance 不是 studyGameInstance！"
				 "请到 项目设置 → 地图和模式 → Game Instance Class 选 studyGameInstance"));
		return;
	}

	// 第一次进入（或按 R 重开之后 bRunStarted 被清掉）：重置整局
	if (!GI->bRunStarted)
	{
		GI->ResetRun();
		GI->bRunStarted = true;
	}

	GS->SetClock(GI->ClockMinutes, GI->DayIndex);
	GS->SetFinalCash(GI->Cash);
	GS->SetPhase(EGamePhase::InProgress);

	// 回到营地：把矿洞暂存点收下的金重一次性结算，并弹提示
	const FString MapName = GetWorld() ? GetWorld()->GetMapName() : FString();
	if (MapName.Contains(TEXT("Camp")))
	{
		FString Msg = GI->CampNoticeText;
		GI->CampNoticeText.Reset();

		if (GI->StashedGoldGrams > 0.f)
		{
			const float BroughtBack = GI->StashedGoldGrams;
			GI->GoldGrams += BroughtBack;
			GI->StashedGoldGrams = 0.f;

			const FString GoldLine = FString::Printf(TEXT("本趟带回 %.1f g 黄金"), BroughtBack);
			Msg = Msg.IsEmpty() ? GoldLine : (Msg + TEXT("\n") + GoldLine);

			UE_LOG(LogTemp, Log, TEXT("[Stash] 回到营地结算：+%.1f g，金重累计 %.1f g"), BroughtBack, GI->GoldGrams);
		}

		if (!Msg.IsEmpty())
		{
			GS->ShowTextNotice(Msg, 7.f);
		}
	}

	EnsureWorldBasics();

	if (MapName.Contains(TEXT("Camp")) && bAutoSpawnCampMerchants)
	{
		SpawnCampMerchants();
	}

	UE_LOG(LogTemp, Log, TEXT("[Match] 开局：第 %d 天 %s"), GI->DayIndex, *FormatClock(GI->ClockMinutes));
}

void AstudyGameMode::SpawnCampMerchants()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 以关卡里的 PlayerStart 为基准摆商人；找不到就退回原点
	FVector Origin = FVector::ZeroVector;
	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		Origin = It->GetActorLocation();
		break;
	}

	auto SpawnOne = [World, Origin](EMerchantKind Kind, const TCHAR* InName, const FVector& Offset)
	{
		// 从目标点上方往下打一条射线，把商人放到地面上 ——
		// PlayerStart 的高度不一定是地面（通常比地面高 1 米左右）。
		FVector SpawnLoc = Origin + Offset;
		{
			const FVector TraceStart = SpawnLoc + FVector(0.f, 0.f, 300.f);
			const FVector TraceEnd = SpawnLoc - FVector(0.f, 0.f, 2000.f);

			FHitResult Hit;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(CampMerchantDrop), false, nullptr);
			if (World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, Params))
			{
				SpawnLoc = Hit.ImpactPoint;
			}
		}

		// 面朝 -Y，正好对着出生点方向
		const FTransform Xf(FRotator(0.f, 180.f, 0.f), SpawnLoc);

		AMerchant* M = World->SpawnActorDeferred<AMerchant>(
			AMerchant::StaticClass(), Xf, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!M)
		{
			return;
		}

		M->Kind = Kind;
		M->DisplayName = InName;
		M->bHideBody = (Kind == EMerchantKind::Bed);		// 床不显示白色机器人
		M->FinishSpawning(Xf);
	};

	// 一个卖开采权（你花钱下矿），一个收黄金（黑市价 65%），一张床
	SpawnOne(EMerchantKind::BuyMineRight, TEXT("矿长"), FVector(350.f, 250.f, 0.f));
	SpawnOne(EMerchantKind::SellGold, TEXT("黑市金商"), FVector(-350.f, 250.f, 0.f));
	SpawnOne(EMerchantKind::Bed, TEXT("床"), FVector(0.f, -350.f, 0.f));

	// 正规买家：第 2 天才出现（第 1 天他"还没来"）
	if (GetStudyGameState() && GetStudyGameState()->GetDayIndex() >= 2)
	{
		SpawnOne(EMerchantKind::LegitBuyer, TEXT("正规买家"), FVector(0.f, 450.f, 0.f));
	}

	// 床的实体：一个扁长方体（机器人已经被 bHideBody 藏掉了）
	{
		if (UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
		{
			const FVector BedLoc = Origin + FVector(0.f, -350.f, 10.f);
			const FTransform BedXf(FRotator::ZeroRotator, BedLoc, FVector(2.0f, 1.0f, 0.2f));

			if (AStaticMeshActor* Bed = World->SpawnActorDeferred<AStaticMeshActor>(
				AStaticMeshActor::StaticClass(), BedXf, nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
			{
				if (UStaticMeshComponent* Comp = Bed->GetStaticMeshComponent())
				{
					Comp->SetMobility(EComponentMobility::Movable);
					Comp->SetStaticMesh(CubeMesh);
				}
				Bed->FinishSpawning(BedXf);
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[Match] 营地商人已就位（基准点 %.0f,%.0f,%.0f）"),
		Origin.X, Origin.Y, Origin.Z);
}

void AstudyGameMode::BeginReturnToCamp(const FString& TransitionText)
{
	if (bReturnToCampPending)
	{
		return;
	}
	bReturnToCampPending = true;

	// 黑屏提示（1.5 秒），这期间玩家输入也被锁住
	if (AstudyCharacter* Char = Cast<AstudyCharacter>(UGameplayStatics::GetPlayerCharacter(this, 0)))
	{
		Char->ShowTransition(TransitionText, 1.5f);
	}

	FTimerHandle TravelHandle;
	GetWorldTimerManager().SetTimer(TravelHandle,
		FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			UstudyGameInstance::CollectCarriedGoldFromPlayers(GetWorld());
			UGameplayStatics::OpenLevel(this, FName(TEXT("Camp")));
		}),
		1.5f, false);
}

void AstudyGameMode::UpdateDayNightLighting()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 只在营地改光：矿洞本来就该是黑的
	if (!World->GetMapName().Contains(TEXT("Camp")))
	{
		return;
	}

	if (!SunLight.IsValid())
	{
		for (TActorIterator<ADirectionalLight> It(World); It; ++It)
		{
			SunLight = *It;
			break;
		}

		if (SunLight.IsValid())
		{
			if (ULightComponent* LC = SunLight->GetLightComponent())
			{
				// Basic 模板里的太阳是 Stationary，运行时改强度/角度要转成 Movable
				LC->SetMobility(EComponentMobility::Movable);
			}
		}
	}

	if (!SkyLightActor.IsValid())
	{
		for (TActorIterator<ASkyLight> It(World); It; ++It)
		{
			SkyLightActor = *It;
			break;
		}

		if (SkyLightActor.IsValid())
		{
			if (USkyLightComponent* SC = SkyLightActor->GetLightComponent())
			{
				SC->SetMobility(EComponentMobility::Movable);
			}
		}
	}

	UstudyGameInstance* GI = Cast<UstudyGameInstance>(UGameplayStatics::GetGameInstance(this));
	if (!GI)
	{
		return;
	}

	// 0 = 8:00（太阳高挂），1 = 20:00（太阳落山）
	const float Span = FMath::Max(
		UstudyGameInstance::DayEndMinutes - UstudyGameInstance::DayStartMinutes, 1.f);
	const float Alpha = FMath::Clamp(
		(GI->ClockMinutes - UstudyGameInstance::DayStartMinutes) / Span, 0.f, 1.f);

	// 越晚越暗（平方让前半天几乎不变，后半段才明显暗下来）
	const float Dim = Alpha * Alpha;

	if (SunLight.IsValid())
	{
		if (ULightComponent* LC = SunLight->GetLightComponent())
		{
			LC->SetIntensity(FMath::Lerp(10.f, 0.15f, Dim));
		}

		// 俯仰从 -70°（高）压到 -5°（贴地平线）
		SunLight->SetActorRotation(FRotator(FMath::Lerp(-70.f, -5.f, Alpha), 30.f, 0.f));
	}

	if (SkyLightActor.IsValid())
	{
		if (USkyLightComponent* SC = SkyLightActor->GetLightComponent())
		{
			SC->SetIntensity(FMath::Lerp(1.f, 0.05f, Dim));
		}
	}
}

void AstudyGameMode::EnsureWorldBasics()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FString MapName = World->GetMapName();

	// 营地：用编辑器的 Basic 模板（自带地板 / 天空 / 阳光），这里什么都不做。
	// 矿洞：运行时生成整个洞（底面 + 四面墙 + 顶）。
	if (MapName.Contains(TEXT("Mine")))
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		if (AMineGenerator* Gen = World->SpawnActor<AMineGenerator>(
			AMineGenerator::StaticClass(), FTransform::Identity, SpawnParams))
		{
			UE_LOG(LogTemp, Log, TEXT("[Match] 矿洞生成器已启动"));
		}
	}
}

AstudyGameState* AstudyGameMode::GetStudyGameState() const
{
	return GetGameState<AstudyGameState>();
}

void AstudyGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	AstudyGameState* GS = GetStudyGameState();
	UstudyGameInstance* GI = Cast<UstudyGameInstance>(UGameplayStatics::GetGameInstance(this));

	// 提示的倒计时：不管什么阶段都要走（结算面板上也可能要显示）
	if (GS)
	{
		GS->TickNotice(DeltaSeconds);
	}

	if (!GS || !GI || GS->GetPhase() != EGamePhase::InProgress)
	{
		return;
	}

	// 推进游戏时钟：1 游戏小时 = RealSecondsPerGameHour 真实秒
	// （默认 60 秒 → 1 真实秒 = 1 游戏分钟，8:00 → 18:00 正好 10 分钟）
	if (!GI->IsDayOver())
	{
		const float GameMinutesPerSecond = 60.f / FMath::Max(UstudyGameInstance::RealSecondsPerGameHour, 1.f);
		GI->ClockMinutes += DeltaSeconds * GameMinutesPerSecond;
		GI->ClampClock();
		GS->SetClock(GI->ClockMinutes, GI->DayIndex);
	}

	// 昼夜光照（只在营地生效）
	UpdateDayNightLighting();

	// 矿洞里：过了 20:00 立刻送回营地，并扣向导来接的路费
	const FString MapName = GetWorld() ? GetWorld()->GetMapName() : FString();
	if (MapName.Contains(TEXT("Mine")) && GI->IsDayOver())
	{
		GI->MineTimeLeft = 0.f;
		GI->MineStopClockMinutes = 0.f;
		GS->SetMineStopClock(0.f);

		// 背包里没交的黄金先带走，别让人白挖
		UstudyGameInstance::CollectCarriedGoldFromPlayers(GetWorld());

		GI->Cash = FMath::Max(GI->Cash - UstudyGameInstance::LatePickupFee, 0);
		GI->CampNoticeText = FString::Printf(
			TEXT("太晚了，向导已经回去了。\n你要支付他来接你的路费 %d 币。"),
			UstudyGameInstance::LatePickupFee);

		UE_LOG(LogTemp, Log, TEXT("[Mine] 20:00 还在矿洞，扣路费 %d 币，送回营地"),
			UstudyGameInstance::LatePickupFee);

		BeginReturnToCamp(TEXT("太晚了，向导已经回去了......"));
		return;
	}

	// 矿洞里：开采倒计时，到点强制送回营地
	if (MapName.Contains(TEXT("Mine")) && GI->MineTimeLeft > 0.f)
	{
		GI->MineTimeLeft -= DeltaSeconds;
		GS->SetMineStopClock(GI->MineStopClockMinutes);

		if (GI->MineTimeLeft <= 0.f)
		{
			GI->MineTimeLeft = 0.f;
			GS->SetMineStopClock(0.f);

			UE_LOG(LogTemp, Log, TEXT("[Mine] 开采时间到，强制送回营地"));

			BeginReturnToCamp(TEXT("返回营地......"));
		}
	}
	else if (GS->GetMineStopClock() != 0.f)
	{
		GS->SetMineStopClock(0.f);
	}

	// 最后一天 20:00 → 结算。
	// 放在矿洞那两段之后：先保证人被送回营地（上面 OpenLevel 会直接 return），
	// 下一帧在营地里才结算，玩家能看到自己站在营地里看到结算面板。
	if (GI->DayIndex >= UstudyGameInstance::TotalDays && GI->IsDayOver())
	{
		EndMatch();
	}
}

void AstudyGameMode::ForceSettle()
{
	EndMatch();
}

void AstudyGameMode::EndMatch()
{
	AstudyGameState* GS = GetStudyGameState();
	UstudyGameInstance* GI = Cast<UstudyGameInstance>(UGameplayStatics::GetGameInstance(this));
	if (!GS || !GI || GS->GetPhase() == EGamePhase::Finished)
	{
		return;
	}

	// 剩下没卖的金重按黑市收购价（PRD §2.9：好感度"熟人"档 65%）折算进现金
	const float UnsoldGrams = GI->GoldGrams;
	const int32 UnsoldCash = FMath::RoundToInt(
		UnsoldGrams
		* AstudyGameState::BlackMarketRate
		* static_cast<float>(AstudyGameState::BasePricePerGram));

	GI->Cash += UnsoldCash;
	GI->GoldGrams = 0.f;
	GI->bRunFinished = true;

	GS->SetFinalUnsoldCash(UnsoldCash);
	GS->SetFinalCash(GI->Cash);
	GS->SetPhase(EGamePhase::Finished);

	UE_LOG(LogTemp, Log, TEXT("[Match] 结算：未卖 %.1f g 折算 +%d 币，最终现金 %d 币"),
		UnsoldGrams, UnsoldCash, GI->Cash);

	// 冻结移动和转视角（不禁用输入组件，按 R 重开还要用）
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		PC->SetIgnoreMoveInput(true);
		PC->SetIgnoreLookInput(true);
	}
}
