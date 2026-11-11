// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "studyGameState.generated.h"

/**
 * 一局游戏的阶段。
 *
 * 为什么放在 GameState 而不是 GameMode：
 *   GameMode 只存在于服务器（客户端根本没有这个对象），
 *   GameState 会被复制到每个客户端。
 * 现在单机看不出区别，但 HUD 要读这个值 —— 将来联机时 HUD 跑在客户端，
 * 读 GameMode 永远是空，读 GameState 才拿得到。
 */
UENUM(BlueprintType)
enum class EGamePhase : uint8
{
	WaitingToStart	UMETA(DisplayName = "准备中"),
	InProgress		UMETA(DisplayName = "进行中"),
	Finished		UMETA(DisplayName = "已结束")
};

/**
 * 一局比赛的共享状态：现在什么阶段、还剩多少秒。
 * 只存"所有人都该知道的数据"，规则和流程放在 GameMode。
 */
UCLASS()
class STUDY_API AstudyGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	/** 当前阶段 */
	UPROPERTY(ReplicatedUsing = OnRep_Phase, BlueprintReadOnly, Category = "Match")
	EGamePhase Phase = EGamePhase::WaitingToStart;

	/** 距离结束还剩多少秒（只在 InProgress 阶段有意义） */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Match")
	float TimeRemaining = 0.f;

	/** 读阶段。HUD / Character 用这个，不要直接碰成员变量 */
	UFUNCTION(BlueprintPure, Category = "Match")
	EGamePhase GetPhase() const { return Phase; }

	UFUNCTION(BlueprintPure, Category = "Match")
	float GetTimeRemaining() const { return TimeRemaining; }

	/** 只在服务器调用：切阶段 */
	void SetPhase(EGamePhase NewPhase);

	/** 只在服务器调用：更新剩余时间 */
	void SetTimeRemaining(float NewRemaining);

protected:
	/** 阶段变化时的回调。服务器手动调一次，客户端由复制触发 —— 两边行为一致 */
	UFUNCTION()
	void OnRep_Phase();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
