// Copyright Epic Games, Inc. All Rights Reserved.

#include "studyGameMode.h"
#include "BagHUD.h"
#include "studyGameState.h"
#include "UObject/ConstructorHelpers.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

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

	// GameMode 默认不 Tick，但倒计时需要它
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

void AstudyGameMode::BeginPlay()
{
	Super::BeginPlay();

	AstudyGameState* GS = GetStudyGameState();
	if (!GS)
	{
		// BP 里如果手动改过 "Game State Class"，构造函数里的设置会被它的存档值覆盖
		UE_LOG(LogTemp, Error,
			TEXT("[Match] GameState 不是 AstudyGameState！请打开 BP_FirstPersonGameMode，"
				 "把 Game State Class 设成 studyGameState"));
		return;
	}

	GS->SetPhase(EGamePhase::WaitingToStart);
	UE_LOG(LogTemp, Log, TEXT("[Match] 准备开始：%.1f 秒后开局，一局 %.0f 秒"),
		StartDelaySeconds, MatchDurationSeconds);

	if (StartDelaySeconds <= 0.f)
	{
		StartMatch();
	}
	else
	{
		GetWorldTimerManager().SetTimer(
			StartTimerHandle, this, &AstudyGameMode::StartMatch, StartDelaySeconds, /*bLoop*/ false);
	}
}

AstudyGameState* AstudyGameMode::GetStudyGameState() const
{
	return GetGameState<AstudyGameState>();
}

void AstudyGameMode::StartMatch()
{
	AstudyGameState* GS = GetStudyGameState();
	if (!GS || !GetWorld())
	{
		return;
	}

	// 先记下结束时刻，再切阶段（顺序反了会让第一帧的剩余时间算错）
	MatchEndTimeSeconds = GetWorld()->GetTimeSeconds() + MatchDurationSeconds;

	GS->SetTimeRemaining(MatchDurationSeconds);
	GS->SetPhase(EGamePhase::InProgress);

	UE_LOG(LogTemp, Log, TEXT("[Match] 开局"));
}

void AstudyGameMode::EndMatch()
{
	AstudyGameState* GS = GetStudyGameState();
	if (!GS)
	{
		return;
	}

	GS->SetTimeRemaining(0.f);
	GS->SetPhase(EGamePhase::Finished);

	// 冻结移动和转视角。注意：不禁用整个输入组件 ——
	// 将来"按 R 重开"还要响应按键，所以只关移动/视角这一层。
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		PC->SetIgnoreMoveInput(true);
		PC->SetIgnoreLookInput(true);
	}

	UE_LOG(LogTemp, Log, TEXT("[Match] 结束"));
}

void AstudyGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	AstudyGameState* GS = GetStudyGameState();
	if (!GS || !GetWorld() || GS->GetPhase() != EGamePhase::InProgress)
	{
		return;
	}

	// 剩余 = 结束时间戳 - 当前世界时间。
	// 用 GetTimeSeconds（游戏时间）而不是 GetRealTimeSeconds（墙上时间），
	// 这样以后加暂停 / 子弹时间，对局时钟会跟着慢下来。
	const float Remaining = static_cast<float>(MatchEndTimeSeconds - GetWorld()->GetTimeSeconds());
	GS->SetTimeRemaining(FMath::Max(Remaining, 0.f));

	if (Remaining <= 0.f)
	{
		EndMatch();
	}
}
