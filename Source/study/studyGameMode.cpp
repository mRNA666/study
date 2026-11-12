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

	EnsureWorldBasics();

	UE_LOG(LogTemp, Log, TEXT("[Match] 开局：第 %d 天 %s"), GI->DayIndex, *FormatClock(GI->ClockMinutes));
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

	// 最后一天 18:00 → 结算
	if (GI->DayIndex >= UstudyGameInstance::TotalDays && GI->IsDayOver())
	{
		EndMatch();
	}
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
