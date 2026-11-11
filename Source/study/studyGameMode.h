// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "studyGameMode.generated.h"

class AstudyGameState;

/**
 *  这一局的"裁判"：什么时候开局、什么时候结束。
 *
 *  它只跑在服务器上，所以这里不放任何要显示给玩家的数据 ——
 *  那些放 GameState（会复制到客户端）。这是一个很常见的分工：
 *    GameMode  = 规则和流程（谁能做什么、什么时候结束）
 *    GameState = 所有人共享的数据（剩余时间、比分）
 */
UCLASS(abstract)
class AstudyGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AstudyGameMode();

	/** 一局多长时间（秒） */
	UPROPERTY(EditAnywhere, Category = "Match", meta = (ClampMin = "5", ClampMax = "1800"))
	float MatchDurationSeconds = 180.f;

	/** 开局前的准备时间（秒）。0 = 立刻开局 */
	UPROPERTY(EditAnywhere, Category = "Match", meta = (ClampMin = "0", ClampMax = "10"))
	float StartDelaySeconds = 3.f;

	/** 取本局的 GameState（不是我们的类型时返回 nullptr） */
	UFUNCTION(BlueprintPure, Category = "Match")
	AstudyGameState* GetStudyGameState() const;

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

	/** 准备结束，正式开局 */
	void StartMatch();

	/** 时间到，结算 */
	void EndMatch();

private:
	/**
	 * 结束时刻的"世界时间戳"。
	 * 剩余时间是"派生值"：唯一真相只有这一个时间戳，
	 * 加时 / 改时长只要动它，剩余时间自动正确。
	 */
	double MatchEndTimeSeconds = 0.0;

	/** 开局倒计时的计时器 */
	FTimerHandle StartTimerHandle;
};
