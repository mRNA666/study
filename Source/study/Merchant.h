// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Merchant.generated.h"

class UStaticMeshComponent;
class AstudyCharacter;

/** 商人的职能 */
UENUM(BlueprintType)
enum class EMerchantKind : uint8
{
	/** 卖矿洞开采权：付钱 → 传送到矿洞 */
	BuyMineRight UMETA(DisplayName = "卖开采权"),

	/** 收黄金：把金重按黑市价换成现金 */
	SellGold UMETA(DisplayName = "收黄金"),

	/** 矿洞里的向导：提前返回 / 查看已存放的黄金 */
	MineGuide UMETA(DisplayName = "矿洞向导"),

	/** 床：睡到下一天 */
	Bed UMETA(DisplayName = "床"),

	/** 正规买家：第二天白天来，全额收，每天有配额 */
	LegitBuyer UMETA(DisplayName = "正规买家"),

	/** 听泉鉴宝：花 500 鉴定一件工艺品的真实含金量 */
	Appraiser UMETA(DisplayName = "听泉鉴宝")
};

/**
 * 营地里的 NPC。走近按 E 打开对话框。
 *
 * 对话系统的架构：
 *   Interact()        → 打开对话
 *   BuildDialog()     → 子类重写这里决定"弹什么、有几个选项"
 *   OnDialogOption(i) → 玩家选了第 i 条
 *   OnDialogNumber(v) → 玩家输入数字并确认（比如卖多少克）
 *
 * 以后加"装备店""情报贩子"只要继承本类、重写 BuildDialog / OnDialogOption 即可，
 * 不用碰 UI 和输入那一层。
 */
UCLASS()
class STUDY_API AMerchant : public AActor
{
	GENERATED_BODY()

public:
	AMerchant();

	virtual void Tick(float DeltaSeconds) override;

	/** 身体（B 方案：引擎自带的圆柱，零项目素材依赖） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Merchant")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Merchant")
	EMerchantKind Kind = EMerchantKind::BuyMineRight;

	// ---------------- 工艺品交易状态 ----------------

	/** 0 = 正常对话框；1 = 黑市卖货列表；2 = 正规买家卖货列表 */
	int32 CraftFlow = 0;

	/** 当前卖货列表里的候选（背包下标，只装**当前这一页**的几件） */
	TArray<int32> CraftCandidates;

	/** 工艺品列表的翻页起点（0 起；0 = 从头看）。卖货页和鉴宝页共用 */
	int32 ListScroll = 0;

	/** 听泉鉴宝：全部没鉴定的下标（翻页时按 ListScroll 切片） */
	TArray<int32> AppraiseCandidates;

	/** 正在报价的那一件（背包下标，-1 = 没在报） */
	int32 PendingCraftSell = -1;

	/** 名字（对话框标题） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Merchant")
	FString DisplayName = TEXT("商人");

	/** 开采权价格（币） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Merchant")
	int32 MineRightPrice = 4000;

	/** 交互半径（cm） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Merchant")
	float InteractRadiusCm = 250.f;

	/** 隐藏白色机器人（床用它，自己另外摆一个长方体） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Merchant")
	bool bHideBody = false;

	/**
	 * 第几天之后才出现（0 = 一直都在）。
	 * 手动摆的正规买家填 2：第 1 天会自己藏起来、也交互不到。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Merchant")
	int32 MinDay = 0;

	UFUNCTION(BlueprintPure, Category = "Merchant")
	bool IsInRange(const AActor* Other) const;

	/** 玩家按 E 时调用。返回 true = 这次按键被消费掉了 */
	bool Interact(AstudyCharacter* Character);

	// ---------------- 对话（子类重写这三条） ----------------

	/** 决定弹什么内容、几个选项 */
	virtual void BuildDialog(AstudyCharacter* Character);

	/** 玩家选了第 Index 条（0 起） */
	virtual void OnDialogOption(AstudyCharacter* Character, int32 Index);

	/** 玩家输入数字并确认 */
	virtual void OnDialogNumber(AstudyCharacter* Character, float Value);

	/** 玩家在第二行数字输入框确认（掺黄铜那页） */
	virtual void OnDialogNumber2(AstudyCharacter* Character, float Value);

	/**
	 * 列表翻页（Delta = +1 下一件 / -1 上一件 / ±5 整页）。
	 * 只有"卖工艺品"和"听泉鉴宝"这两个列表会响应，其它对话忽略。
	 */
	void OnDialogScroll(AstudyCharacter* Character, int32 Delta);

protected:
	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;

	/** 把玩家的金重按黑市价卖出指定克数 */
	/** 列出可卖的工艺品（一页 5 件，↑↓ / 滚轮翻页） */
	void BuildCraftSellList(AstudyCharacter* Character, bool bToLegit);

	/** 卖货列表里的选项回调 */
	void HandleCraftSellOption(AstudyCharacter* Character, int32 Index);

	/** 卖给黑商：自己报价，可能被识破 */
	void SellHandicraftToFence(AstudyCharacter* Character, int32 BagIndex, float Reported01);

	/** 卖给正规买家：必验，按真实含金量全价 */
	void SellHandicraftToLegit(AstudyCharacter* Character, int32 BagIndex);

	void SellGold(float Grams);

	/** 卖给正规买家：全额，但要吃配额 */
	void SellGoldLegit(float Grams);

	// ---------------- 掺假（卖黄金的两级页面） ----------------

	/**
	 * 卖黄金的流程（黑市和正规买家共用）：
	 *   0 = 正常菜单
	 *   1 = 入口页：老老实实卖 / 掺点黄铜卖 / 上一步
	 *   2 = 老实卖页：全部 / 填克数 / 上一步
	 *   3 = 掺着卖页：第一行填卖多少 g、第二行填掺多少 g，两个都填了才能卖
	 */
	int32 SellFlow = 0;

	/** 掺入的黄铜克数（掺着卖页） */
	float PendingBrass = 0.f;

	/** 掺着卖页：第一行（卖多少 g）填了吗 */
	bool bSellGramsFilled = false;

	/** 掺着卖页：第二行（掺多少 g）填了吗 */
	bool bBrassFilled = false;

	/** 掺着卖页：第一行的值 */
	float PendingSellGrams = 0.f;

	/** 填超了的时候要显示在页面顶部的一句警告（显示一次就清空） */
	FString SellWarning;

	/** 打开「卖黄金」入口页（三个选项） */
	void OpenGoldSellPage(AstudyCharacter* Character);

	/** 老实卖页 */
	void BuildHonestSellPage(AstudyCharacter* Character);

	/** 掺着卖页（两行输入） */
	void BuildAdulteratedSellPage(AstudyCharacter* Character);

	/** 真正结算：黑市（bToLegit=false）走掂量 + 抽查，正规买家走抽检 */
	void SettleGoldSale(AstudyCharacter* Character, float SellGrams, float BrassGrams, bool bToLegit);

	/**
	 * 只弹一条「知道了」，点完就结束，不再回到菜单。
	 * 关键：把 DialogMerchant 清空 —— 否则点「知道了」会被当成又点了菜单第 0 项，
	 * 于是卖完黄金/工艺品之后会莫名其妙弹出"你身上没金子"。
	 */
	void ShowResult(AstudyCharacter* Character, const FString& Body);

	/** 尝试买一次开采权并传送进矿洞。返回 false = 钱不够 */
	bool BuyMineRight();

	/** 上一次买开采权是不是因为"钱不够"失败（用来在对话框上提示） */
	UPROPERTY(Transient)
	bool bBuyFailed = false;

	/** 矿洞向导：玩家是不是刚点过"查看已存放"（点了才把数字写进正文） */
	UPROPERTY(Transient)
	bool bStashRevealed = false;

	/** 按 MinDay 决定露不露面。要每帧算 —— 见 .cpp 里的注释 */
	void UpdateDayGate();
};
