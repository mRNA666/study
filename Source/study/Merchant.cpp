// Fill out your copyright notice in the Description page of Project Settings.


#include "Merchant.h"
#include "studyCharacter.h"
#include "studyGameInstance.h"
#include "studyGameState.h"
#include "studyGameMode.h"
#include "StudySounds.h"
#include "BagHUD.h"
#include "Components/SkeletalMeshComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

namespace
{
	UstudyGameInstance* GetRun(class UObject* Context)
	{
		return Context ? Cast<UstudyGameInstance>(UGameplayStatics::GetGameInstance(Context)) : nullptr;
	}
}

AMerchant::AMerchant()
{
	// 默认不 tick；只有配了 MinDay 的（比如正规买家）才需要每帧检查日子
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	Body = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Body"));
	SetRootComponent(Body);

	// 模型在构造函数里就指定 —— 这样在编辑器的视口里也能看见，方便手动摆放。
	// （之前是在 BeginPlay 里 LoadObject，编辑器里就只有一个空组件）
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> MannyMesh(
		TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple"));

	if (MannyMesh.Succeeded())
	{
		Body->SetSkeletalMeshAsset(MannyMesh.Object);
	}

	// 挡路用：玩家不能从商人身体里穿过去
	Body->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Body->SetCollisionResponseToAllChannels(ECR_Block);
}

void AMerchant::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// 床不显示机器人：在编辑器里勾了 bHideBody 就立刻生效
	if (Body)
	{
		Body->SetVisibility(!bHideBody);
	}
}

void AMerchant::BeginPlay()
{
	Super::BeginPlay();

	// 配了 MinDay 的 NPC（正规买家）需要每帧检查日子，见 UpdateDayGate 的注释
	if (MinDay > 0)
	{
		SetActorTickEnabled(true);
		UpdateDayGate();
	}
}

void AMerchant::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (MinDay > 0)
	{
		UpdateDayGate();
	}
}

void AMerchant::UpdateDayGate()
{
	// 为什么每帧算、而不是在 BeginPlay 里算一次：
	//   按 R 重开时，"把天数重置成 1"发生在 GameMode::BeginPlay 里，
	//   而关卡里各个 Actor 的 BeginPlay 顺序是不保证的 ——
	//   如果商人先跑，它看到的还是上一局的第 2 天，于是不会藏起来。
	//   每帧对一次就永远不会错。
	UstudyGameInstance* GI = GetRun(this);
	if (!GI)
	{
		return;
	}

	const bool bShouldHide = (GI->DayIndex < MinDay);
	if (bShouldHide != IsHidden())
	{
		SetActorHiddenInGame(bShouldHide);
		SetActorEnableCollision(!bShouldHide);
	}
}

bool AMerchant::IsInRange(const AActor* Other) const
{
	if (!Other)
	{
		return false;
	}

	return FVector::DistSquared(Other->GetActorLocation(), GetActorLocation())
		<= FMath::Square(InteractRadiusCm);
}

bool AMerchant::Interact(AstudyCharacter* Character)
{
	if (!Character || !IsInRange(Character) || IsHidden())
	{
		return false;
	}

	Character->SetDialogMerchant(this);
	bBuyFailed = false;
	BuildDialog(Character);
	return true;
}

// ---------------- 对话内容 ----------------

namespace
{
	/** 含金量 -> 听泉鉴宝的评语 */
	FString AppraiseLine(float Purity)
	{
		if (Purity >= 0.85f) { return TEXT("这个东西很开门啊！"); }
		if (Purity >= 0.60f) { return TEXT("这个东西不错。"); }
		if (Purity >= 0.40f) { return TEXT("这个东西一般般。"); }
		return TEXT("这个啥呀，就一破烂。");
	}

	/** 背包里还没鉴定过的下标（最多 3 个，按背包顺序） */
	void CollectUnappraised(const TArray<FHandicraftItem>& Bag, TArray<int32>& Out)
	{
		Out.Reset();
		for (int32 i = 0; i < Bag.Num() && Out.Num() < 3; ++i)
		{
			if (!Bag[i].bAppraised)
			{
				Out.Add(i);
			}
		}
	}
}

void AMerchant::BuildDialog(AstudyCharacter* Character)
{
	UstudyGameInstance* GI = GetRun(this);
	if (!GI || !Character)
	{
		return;
	}

	// 必须在这里重新认领发起者：
	// 玩家点一次选项后，DialogChoose 会先 CloseDialog()（那一步把 DialogMerchant 清成了 nullptr），
	// 然后才回调 OnDialogOption → 里面可能又调 BuildDialog 重刷对话框。
	// 不重新认领的话，重刷出来的对话框是"没有主人"的，之后点任何选项都没反应。
	Character->SetDialogMerchant(this);

	// 工艺品卖货列表（自己一套选项，不和其他商人的菜单混）
	if (CraftFlow != 0)
	{
		BuildCraftSellList(Character, CraftFlow == 2);
		return;
	}

	// ---- 听泉鉴宝 ----
	if (Kind == EMerchantKind::Appraiser)
	{
		Character->SetDialogNumberEnabled(0.f);

		const int32 FEE = 500;

		if (GI->HandicraftBag.Num() == 0)
		{
			Character->OpenOptionDialog(DisplayName,
				TEXT("你去买点好玩意儿回来再找我。"),
				{ TEXT("离开") });
			return;
		}

		TArray<int32> Candidates;
		CollectUnappraised(GI->HandicraftBag, Candidates);

		if (Candidates.Num() == 0)
		{
			Character->OpenOptionDialog(DisplayName,
				TEXT("你这些我都看过了，我没什么可说的。"),
				{ TEXT("离开") });
			return;
		}

		if (GI->Cash < FEE)
		{
			Character->OpenOptionDialog(DisplayName,
				FString::Printf(TEXT("看一件 %d 币。你口袋里只有 %d 币，先去挣够钱再来。"), FEE, GI->Cash),
				{ TEXT("离开") });
			return;
		}

		FString DlgBody = FString::Printf(TEXT("拿一件给我瞧瞧。看一件 %d 币。\n\n"), FEE);
		TArray<FString> Options;
		for (int32 n = 0; n < Candidates.Num(); ++n)
		{
			const FHandicraftItem& It = GI->HandicraftBag[Candidates[n]];
			DlgBody += FString::Printf(TEXT("%d. %s   标称 %.0f g   黑商报 %.0f%%\n"),
				n + 1, *UstudyGameInstance::GetCategoryName(It.Category), It.NominalGrams,
				It.DeclaredPurity * 100.f);
			Options.Add(FString::Printf(TEXT("鉴：%s（%.0f g，报 %.0f%%）"),
				*UstudyGameInstance::GetCategoryName(It.Category), It.NominalGrams,
				It.DeclaredPurity * 100.f));
		}
		Options.Add(TEXT("离开"));
		Character->OpenOptionDialog(DisplayName, DlgBody, Options);
		return;
	}

	// ---- 床：睡到下一天（晚上才用，所以不参与"太晚了"封锁） ----
	if (Kind == EMerchantKind::Bed)
	{
		Character->SetDialogNumberEnabled(0.f);

		const bool bLastDay = (GI->DayIndex >= UstudyGameInstance::TotalDays);

		TArray<FString> Options;
		Options.Add(bLastDay
			? TEXT("睡觉 —— 收工结算")
			: FString::Printf(TEXT("睡觉，进入第 %d 天"), GI->DayIndex + 1));
		Options.Add(TEXT("再等等"));

		Character->OpenOptionDialog(
			DisplayName,
			bLastDay
				? TEXT("今天是最后一天了，睡下去这一局就结束。")
				: FString::Printf(TEXT("第 %d 天要过去了，睡一觉天就亮了。"), GI->DayIndex),
			Options);
		return;
	}

	// ---- 20:00 之后，所有商人都收摊 ----
	if (GI->IsDayOver())
	{
		Character->SetDialogNumberEnabled(0.f);

		TArray<FString> Options;
		Options.Add(TEXT("知道了"));

		Character->OpenOptionDialog(DisplayName, TEXT("今天太晚了，明天再来吧。"), Options);
		return;
	}

	// ---- 正规买家：全额收，每天有配额 ----
	if (Kind == EMerchantKind::LegitBuyer)
	{
		const float Sellable = FMath::Min(GI->GoldGrams, GI->BuyerQuotaGrams);
		const int32 Estimate = FMath::RoundToInt(Sellable * static_cast<float>(AstudyGameState::BasePricePerGram));

		Character->SetDialogNumberUnit(TEXT("g"));
		Character->SetDialogNumberEnabled(Sellable);

		TArray<FString> Options;
		Options.Add(FString::Printf(TEXT("全部卖掉（%.1f g → %d 币，全额）"), Sellable, Estimate));
		Options.Add(TEXT("卖工艺品"));
		Options.Add(TEXT("算了"));

		Character->OpenOptionDialog(
			DisplayName,
			FString::Printf(TEXT("正规价：100%%（不压价）\n今天还能收：%.1f g\n你手上有：%.1f g\n现金：%d 币"),
				GI->BuyerQuotaGrams, GI->GoldGrams, GI->Cash),
			Options);
		return;
	}

	// ---- 矿洞向导 ----
	if (Kind == EMerchantKind::MineGuide)
	{
		Character->SetDialogNumberEnabled(0.f);

		FString GuideBody = TEXT("要提前上去吗？还是再挖一会儿。");
		if (bStashRevealed)
		{
			GuideBody += FString::Printf(TEXT("\n\n这里已经存放：%.1f g 黄金"), GI->StashedGoldGrams);
			GuideBody += TEXT("\n回到营地会自动结算。");
		}

		TArray<FString> Options;
		Options.Add(TEXT("我要提前返回营地"));
		Options.Add(TEXT("这里已经放了多少黄金？"));

		Character->OpenOptionDialog(DisplayName, GuideBody, Options);
		return;
	}

	// ---- 收黄金 ----
	if (Kind == EMerchantKind::SellGold)
	{
		const float EffectiveRate = GI->BlackMarketDailyRate * GI->BlackMarketMultiplier;
		const int32 Estimate = FMath::RoundToInt(
			GI->GoldGrams * EffectiveRate * static_cast<float>(AstudyGameState::BasePricePerGram));

		// 打开行内数字输入（框里填"卖多少克"）
		Character->SetDialogNumberUnit(TEXT("g"));
		Character->SetDialogNumberEnabled(GI->GoldGrams);

		TArray<FString> Options;
		Options.Add(FString::Printf(TEXT("全部卖掉（%.1f g → 约 %d 币）"), GI->GoldGrams, Estimate));
		Options.Add(TEXT("购买工艺品（看看柜台）"));
		Options.Add(TEXT("卖工艺品"));
		Options.Add(TEXT("算了"));

		Character->OpenOptionDialog(
			DisplayName,
			FString::Printf(TEXT("手上的金重：%.1f g\n黑市价：正规价的 %.0f%%（抛售越多，价越低）\n现金：%d 币"),
				GI->GoldGrams, EffectiveRate * 100.f, GI->Cash),
			Options);
		return;
	}

	// ---- 卖开采权（没有数字输入） ----
	Character->SetDialogNumberEnabled(0.f);
	const bool bSoldOut = (GI->RightsBoughtToday >= UstudyGameInstance::MaxMineRightsPerDay);

	TArray<FString> Options;
	if (bSoldOut)
	{
		Options.Add(TEXT("今天的开采权卖完了，明天再来"));
		Options.Add(TEXT("知道了"));
	}
	else
	{
		Options.Add(FString::Printf(TEXT("买开采权（%d 币）"), MineRightPrice));
		Options.Add(TEXT("不买了"));
	}

	// 钱不够的提示写在正文最前面，一眼能看到
	FString BuyBody;
	if (bBuyFailed)
	{
		const int32 Short_ = FMath::Max(MineRightPrice - GI->Cash, 0);
		BuyBody = FString::Printf(TEXT("⚠ 金钱不够！要 %d 币，你只有 %d 币（还差 %d）\n\n"),
			MineRightPrice, GI->Cash, Short_);
	}

	BuyBody += FString::Printf(TEXT("现金：%d 币\n今天已买 %d / %d 次\n一次能挖 3 分钟，到点自动送回营地"),
		GI->Cash, GI->RightsBoughtToday, UstudyGameInstance::MaxMineRightsPerDay);

	Character->OpenOptionDialog(DisplayName, BuyBody, Options);
}

void AMerchant::OnDialogOption(AstudyCharacter* Character, int32 Index)
{
	UstudyGameInstance* GI = GetRun(this);
	if (!GI || !Character)
	{
		return;
	}

	if (CraftFlow != 0)
	{
		HandleCraftSellOption(Character, Index);
		return;
	}

	// ---- 床 ----
	// ---- 听泉鉴宝 ----
	if (Kind == EMerchantKind::Appraiser)
	{
		const int32 FEE = 500;

		TArray<int32> Candidates;
		CollectUnappraised(GI->HandicraftBag, Candidates);

		if (Index < 0 || Index >= Candidates.Num())
		{
			Character->CloseDialog();
			return;
		}

		if (GI->Cash < FEE)
		{
			Character->CloseDialog();
			return;
		}

		FHandicraftItem& It = GI->HandicraftBag[Candidates[Index]];
		GI->Cash -= FEE;
		It.bAppraised = true;

		UGameplayStatics::PlaySound2D(this, StudySounds::Cash(), 0.8f);

		const FString Verdict = FString::Printf(
			TEXT("（他把东西翻来覆去看了半天，又掂了掂）\n\n「%s」\n\n真实含金量：%.0f%%"),
			*AppraiseLine(It.RealPurity), It.RealPurity * 100.f);

		UE_LOG(LogTemp, Log, TEXT("[Appraise] %s 真实含金量 %.1f%%（收了 %d 币）"),
			*UstudyGameInstance::GetCategoryName(It.Category), It.RealPurity * 100.f, FEE);

		Character->OpenOptionDialog(DisplayName, Verdict, { TEXT("知道了") });
		return;
	}

	if (Kind == EMerchantKind::Bed)
	{
		if (Index == 0)
		{
			// 先黑屏 1.5 秒，再真的睡/结算 —— 给玩家一个"时间过去了"的感觉
			Character->ShowTransition(TEXT("睡觉中 zzzzz"), 1.5f);

			FTimerHandle SleepHandle;
			GetWorldTimerManager().SetTimer(SleepHandle,
				FTimerDelegate::CreateWeakLambda(this, [this]()
				{
					UstudyGameInstance* Run = GetRun(this);
					if (!Run)
					{
						return;
					}

					// 最后一天：睡觉 = 收工结算，不进第 3 天
					if (Run->DayIndex >= UstudyGameInstance::TotalDays)
					{
						if (AstudyGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AstudyGameMode>() : nullptr)
						{
							UE_LOG(LogTemp, Log, TEXT("[Merchant] 最后一天睡觉 → 结算"));
							GM->ForceSettle();
						}
						return;
					}

					Run->SleepToNextDay();
					UE_LOG(LogTemp, Log, TEXT("[Merchant] 睡到第 %d 天"), Run->DayIndex);
					UGameplayStatics::OpenLevel(this, FName(TEXT("Camp")));
				}),
				1.5f, false);
		}
		return;
	}

	// ---- 正规买家 ----
	if (Kind == EMerchantKind::LegitBuyer)
	{
		if (Index == 1)
		{
			CraftFlow = 2;
			BuildDialog(Character);
			return;
		}
		if (Index == 0)
		{
			SellGoldLegit(FMath::Min(GI->GoldGrams, GI->BuyerQuotaGrams));
			BuildDialog(Character);
		}
		return;
	}

	// 20:00 之后所有对话只有"知道了"，点了不做任何事
	if (GI->IsDayOver())
	{
		return;
	}

	// ---- 矿洞向导 ----
	if (Kind == EMerchantKind::MineGuide)
	{
		if (Index == 0)
		{
			// 提前返回营地：清掉开采倒计时，免得刚回去又被强制传送
			GI->MineTimeLeft = 0.f;
			GI->MineStopClockMinutes = 0.f;

			UE_LOG(LogTemp, Log, TEXT("[Merchant] %s：提前返回营地"), *DisplayName);

			// 先黑屏 1.5 秒再传送
			Character->ShowTransition(TEXT("返回营地......"), 1.5f);

			FTimerHandle TravelHandle;
			GetWorldTimerManager().SetTimer(TravelHandle,
				FTimerDelegate::CreateWeakLambda(this, [this]()
				{
					// 走之前把背包里没交的黄金一起带走
					UstudyGameInstance::CollectCarriedGoldFromPlayers(GetWorld());
					UGameplayStatics::OpenLevel(this, FName(TEXT("Camp")));
				}),
				1.5f, false);
		}
		else if (Index == 1)
		{
			bStashRevealed = true;
			BuildDialog(Character);
		}
		return;
	}

	// ---- 收黄金 ----
	if (Kind == EMerchantKind::SellGold)
	{
		if (Index == 1)
		{
			// 先关对话框（它会把输入模式设回 GameOnly），再开面板
			Character->CloseDialog();
			if (APlayerController* PC = Cast<APlayerController>(Character->GetController()))
			{
				if (ABagHUD* HUD = Cast<ABagHUD>(PC->GetHUD()))
				{
					HUD->OpenShelf(this);
				}
			}
			return;
		}
		if (Index == 2)
		{
			CraftFlow = 1;
			BuildDialog(Character);
			return;
		}
		if (Index != 0)
		{
			return;
		}
		if (Index == 0)
		{
			SellGold(GI->GoldGrams);
			// 卖完重刷对话框，让"手上的金重"立刻反映新数值
			BuildDialog(Character);
		}
		return;
	}

	// ---- 卖开采权 ----
	if (Index != 0)
	{
		return;
	}

	if (GI->RightsBoughtToday >= UstudyGameInstance::MaxMineRightsPerDay)
	{
		UE_LOG(LogTemp, Log, TEXT("[Merchant] %s：今天的开采权已经卖完了"), *DisplayName);
		return;
	}

	// 钱不够：标一下，让对话框弹提示
	if (!BuyMineRight())
	{
		bBuyFailed = true;
		BuildDialog(Character);
	}
}


// ==================== 工艺品交易 ====================

void AMerchant::BuildCraftSellList(AstudyCharacter* Character, bool bToLegit)
{
	UstudyGameInstance* GI = GetRun(this);
	if (!GI || !Character)
	{
		return;
	}

	Character->SetDialogMerchant(this);
	Character->SetDialogNumberEnabled(0.f);

	CraftCandidates.Reset();

	if (GI->HandicraftBag.Num() == 0)
	{
		CraftFlow = 0;
		Character->OpenOptionDialog(DisplayName,
			TEXT("你身上一件工艺品都没有。"),
			{ TEXT("知道了") });
		return;
	}

	for (int32 i = 0; i < GI->HandicraftBag.Num() && CraftCandidates.Num() < 3; ++i)
	{
		CraftCandidates.Add(i);
	}

	FString CraftBody = bToLegit
		? TEXT("他戴上眼镜，把东西翻过来看背面。\n「我这儿是要过秤验成色的，报虚的没用。」\n\n")
		: TEXT("「我这可是会验含量的，你小子不要骗我。」\n\n");

	TArray<FString> Options;
	for (int32 n = 0; n < CraftCandidates.Num(); ++n)
	{
		const FHandicraftItem& It = GI->HandicraftBag[CraftCandidates[n]];
		CraftBody += FString::Printf(TEXT("%d. %s   标称 %.0f g   %s\n"),
			n + 1, *UstudyGameInstance::GetCategoryName(It.Category), It.NominalGrams,
			It.bAppraised
				? *FString::Printf(TEXT("真实 %.0f%%"), It.RealPurity * 100.f)
				: *FString::Printf(TEXT("黑商报 %.0f%%"), It.DeclaredPurity * 100.f));

		Options.Add(FString::Printf(TEXT("卖：%s（%.0f g）"),
			*UstudyGameInstance::GetCategoryName(It.Category), It.NominalGrams));
	}
	Options.Add(TEXT("算了"));

	Character->OpenOptionDialog(DisplayName, CraftBody, Options);
}

void AMerchant::HandleCraftSellOption(AstudyCharacter* Character, int32 Index)
{
	UstudyGameInstance* GI = GetRun(this);
	if (!GI || !Character)
	{
		return;
	}

	if (!CraftCandidates.IsValidIndex(Index))
	{
		CraftFlow = 0;
		BuildDialog(Character);
		return;
	}

	const int32 BagIndex = CraftCandidates[Index];
	if (!GI->HandicraftBag.IsValidIndex(BagIndex))
	{
		CraftFlow = 0;
		BuildDialog(Character);
		return;
	}

	if (CraftFlow == 2)
	{
		SellHandicraftToLegit(Character, BagIndex);
		return;
	}

	// 黑市：先让玩家自己报含金量
	PendingCraftSell = BagIndex;
	CraftFlow = 0;
	Character->SetDialogNumberUnit(TEXT("%"));

	const FHandicraftItem& It = GI->HandicraftBag[BagIndex];
	Character->OpenNumberDialog(
		DisplayName,
		FString::Printf(TEXT("%s，标称 %.0f g。\n\n你打算报多少含金量？（0-100）\n%s"),
			*UstudyGameInstance::GetCategoryName(It.Category), It.NominalGrams,
			It.bAppraised
				? *FString::Printf(TEXT("（你鉴定过：真实 %.0f%%）"), It.RealPurity * 100.f)
				: *FString::Printf(TEXT("（你只有黑商那句：%.0f%%）"), It.DeclaredPurity * 100.f)),
		TEXT("报个含金量（0-100）"),
		100.f);

	// 关键：DialogChoose 先 CloseDialog 把 DialogMerchant 清空了，
	// 这里必须重新认领，否则「确认卖出」时找不到商人，点了没反应。
	Character->SetDialogMerchant(this);
}

void AMerchant::SellHandicraftToFence(AstudyCharacter* Character, int32 BagIndex, float Reported01)
{
	UstudyGameInstance* GI = GetRun(this);
	if (!GI || !Character || !GI->HandicraftBag.IsValidIndex(BagIndex))
	{
		return;
	}

	const FHandicraftItem It = GI->HandicraftBag[BagIndex];
	const float Reported = FMath::Clamp(Reported01, 0.f, 1.f);
	const float Real = It.RealPurity;
	const float Lie = FMath::Max(0.f, Reported - Real);

	// 被验概率：好感度越高越信你；虚报越多越容易被看穿
	float CatchChance = 0.72f - 0.0052f * static_cast<float>(GI->FenceFavor) + Lie * 1.5f;
	CatchChance = FMath::Clamp(CatchChance, 0.f, 1.f);

	const bool bCaught = FMath::FRand() < CatchChance;
	const float UsedPurity = bCaught ? Real : Reported;

	const int32 Earned = FMath::RoundToInt(
		It.NominalGrams * UsedPurity * 1000.f * GI->BlackMarketDailyRate * 0.95f);

	GI->Cash += Earned;
	GI->HandicraftBag.RemoveAt(BagIndex);

	FString Verdict;
	if (bCaught)
	{
		GI->FenceFavor = FMath::Max(0, GI->FenceFavor - 15);
		Verdict = FString::Printf(
			TEXT("「你小子拿这玩意儿糊弄我？」\n\n他当场砸开看了一眼。\n真实含金量 %.0f%%，按这个给你 %d 币。\n（好感度 -15）"),
			Real * 100.f, Earned);
	}
	else
	{
		Verdict = FString::Printf(TEXT("「行，收下了。」\n\n按 %.0f%% 算，给你 %d 币。"), Reported * 100.f, Earned);
	}

	UGameplayStatics::PlaySound2D(this, StudySounds::Cash(), 0.9f);

	UE_LOG(LogTemp, Log, TEXT("[CraftSell] 黑商 真实 %.0f%% 报 %.0f%% → %s，%d 币（好感 %d）"),
		Real * 100.f, Reported * 100.f, bCaught ? TEXT("识破") : TEXT("蒙混"), Earned, GI->FenceFavor);

	CraftFlow = 0;
	Character->SetDialogNumberEnabled(0.f);		// 结算完就别再摆着"报多少 %"那一行了
	Character->OpenOptionDialog(DisplayName, Verdict, { TEXT("知道了") });
}

void AMerchant::SellHandicraftToLegit(AstudyCharacter* Character, int32 BagIndex)
{
	UstudyGameInstance* GI = GetRun(this);
	if (!GI || !Character || !GI->HandicraftBag.IsValidIndex(BagIndex))
	{
		return;
	}

	const FHandicraftItem It = GI->HandicraftBag[BagIndex];
	const int32 Earned = FMath::RoundToInt(It.NominalGrams * It.RealPurity * 1000.f);

	GI->Cash += Earned;
	GI->HandicraftBag.RemoveAt(BagIndex);

	UGameplayStatics::PlaySound2D(this, StudySounds::Cash(), 0.9f);

	UE_LOG(LogTemp, Log, TEXT("[CraftSell] 正规买家 真实 %.0f%% → %d 币"),
		It.RealPurity * 100.f, Earned);

	CraftFlow = 0;
	Character->SetDialogNumberEnabled(0.f);		// 结算完就别再摆着数字输入行了
	Character->OpenOptionDialog(DisplayName,
		FString::Printf(TEXT("（他当着你的面过了秤、化了点边角料）\n\n成色 %.0f%%。\n按全价给你 %d 币。"),
			It.RealPurity * 100.f, Earned),
		{ TEXT("知道了") });
}

void AMerchant::OnDialogNumber(AstudyCharacter* Character, float Value)
{
	UstudyGameInstance* GI = GetRun(this);
	if (!GI)
	{
		return;
	}

	// 工艺品报价（0-100）
	if (PendingCraftSell >= 0)
	{
		const int32 BagIndex = PendingCraftSell;
		PendingCraftSell = -1;
		SellHandicraftToFence(Character, BagIndex, Value / 100.f);
		return;
	}

	if (Kind == EMerchantKind::SellGold)
	{
		// 输入比存量还大 → 直接按存量算（不用报错，玩家想卖完就该卖完）
		SellGold(FMath::Clamp(Value, 0.f, GI->GoldGrams));
	}
	else if (Kind == EMerchantKind::LegitBuyer)
	{
		SellGoldLegit(FMath::Clamp(Value, 0.f, FMath::Min(GI->GoldGrams, GI->BuyerQuotaGrams)));
	}
	else
	{
		return;
	}

	// 重刷对话框（金重 / 现金 / 可卖数量都变了）
	if (Character && Character->IsDialogOpen())
	{
		BuildDialog(Character);
	}
}

// ---------------- 实际逻辑 ----------------

void AMerchant::SellGold(float Grams)
{
	UstudyGameInstance* GI = GetRun(this);
	if (!GI || Grams <= 0.f)
	{
		return;
	}

	const float Sold = FMath::Min(Grams, GI->GoldGrams);
	if (Sold <= 0.f)
	{
		return;
	}

	const float EffectiveRate = GI->BlackMarketDailyRate * GI->BlackMarketMultiplier;
	const int32 Earned = FMath::RoundToInt(
		Sold * EffectiveRate * static_cast<float>(AstudyGameState::BasePricePerGram));

	GI->GoldGrams -= Sold;
	GI->Cash += Earned;

	// 好感度：每卖给他 5 g 黄金 +1（上限 100）；被识破一次 -15 见 DESIGN-shelf §7
	GI->FenceFavor = FMath::Clamp(GI->FenceFavor + FMath::FloorToInt(Sold / 5.f), 0, 100);

	// 抛售砸价：卖得越多，黑市价掉得越多（对手也会砸，所以这是真竞争）
	GI->ApplyBlackMarketDump(Sold);

	// 2D：柜台就在面前，不需要衰减定位
	UGameplayStatics::PlaySound2D(this, StudySounds::Cash(), 0.9f);

	UE_LOG(LogTemp, Log, TEXT("[Merchant] %s 收走 %.1f g（黑市价 %.0f%%）→ +%d 币；剩 %.1f g，现金 %d，市场 x%.2f"),
		*DisplayName, Sold, EffectiveRate * 100.f, Earned, GI->GoldGrams, GI->Cash, GI->BlackMarketMultiplier);
}

void AMerchant::SellGoldLegit(float Grams)
{
	UstudyGameInstance* GI = GetRun(this);
	if (!GI || Grams <= 0.f)
	{
		return;
	}

	// 三个上限取最小：想卖的、手上的、今天剩下的配额
	const float Sold = FMath::Min(Grams, FMath::Min(GI->GoldGrams, GI->BuyerQuotaGrams));
	if (Sold <= 0.f)
	{
		UE_LOG(LogTemp, Log, TEXT("[Merchant] %s：今天收不了更多了（配额剩 %.1f g）"), *DisplayName, GI->BuyerQuotaGrams);
		return;
	}

	const int32 Earned = FMath::RoundToInt(Sold * static_cast<float>(AstudyGameState::BasePricePerGram));

	GI->GoldGrams -= Sold;
	GI->BuyerQuotaGrams -= Sold;
	GI->Cash += Earned;

	// 2D：柜台就在面前，不需要衰减定位
	UGameplayStatics::PlaySound2D(this, StudySounds::Cash(), 0.9f);

	UE_LOG(LogTemp, Log, TEXT("[Merchant] %s 全额收走 %.1f g → +%d 币；配额剩 %.1f g，现金 %d"),
		*DisplayName, Sold, Earned, GI->BuyerQuotaGrams, GI->Cash);
}

bool AMerchant::BuyMineRight()
{
	UstudyGameInstance* GI = GetRun(this);
	if (!GI)
	{
		return false;
	}

	if (GI->Cash < MineRightPrice)
	{
		UE_LOG(LogTemp, Log, TEXT("[Merchant] %s：金钱不够（要 %d，只有 %d）"), *DisplayName, MineRightPrice, GI->Cash);
		return false;
	}

	GI->Cash -= MineRightPrice;
	GI->RightsBoughtToday++;

	// 2D：柜台就在面前，不需要衰减定位
	UGameplayStatics::PlaySound2D(this, StudySounds::Cash(), 0.9f);

	// 一次开采 3 分钟（真实时间）。
	// 换算成游戏分钟必须跟着流速走：游戏分钟 = 真实秒 × (60 / RealSecondsPerGameHour)。
	// 写死 +180 只在"1 真实秒 = 1 游戏分钟"时才对，改流速就全错了。
	const float GameMinutesPerSecond = 60.f / FMath::Max(UstudyGameInstance::RealSecondsPerGameHour, 1.f);

	GI->MineTimeLeft = UstudyGameInstance::MineDurationSeconds;

	// 收工时间不能超过当天收工点（20:00）—— 否则会显示成"26:16"这种不存在的钟点
	GI->MineStopClockMinutes = FMath::Min(
		GI->ClockMinutes + UstudyGameInstance::MineDurationSeconds * GameMinutesPerSecond,
		UstudyGameInstance::DayEndMinutes);

	const int32 StartH = FMath::FloorToInt(GI->ClockMinutes / 60.f);
	const int32 StartM = FMath::FloorToInt(GI->ClockMinutes) % 60;
	const int32 StopH = FMath::FloorToInt(GI->MineStopClockMinutes / 60.f);
	const int32 StopM = FMath::FloorToInt(GI->MineStopClockMinutes) % 60;

	UE_LOG(LogTemp, Log, TEXT("[Merchant] %s：开采权买到（今天第 %d/%d 次，花了 %d 币），%02d:%02d 采到 %02d:%02d"),
		*DisplayName, GI->RightsBoughtToday, UstudyGameInstance::MaxMineRightsPerDay, MineRightPrice,
		StartH, StartM, StopH, StopM);

	UGameplayStatics::OpenLevel(this, FName(TEXT("Mine1")));
	return true;
}
