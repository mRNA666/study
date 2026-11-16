// Fill out your copyright notice in the Description page of Project Settings.


#include "Merchant.h"
#include "studyCharacter.h"
#include "studyGameInstance.h"
#include "studyGameState.h"
#include "studyGameMode.h"
#include "StudySounds.h"
#include "BagHUD.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
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

	// 【B 方案】身体不依赖任何项目内模型，只用引擎自带的圆柱 ——
	// 这样别人 clone 下来就能看见商人。原来引用的是
	// /Game/Characters/Mannequins/.../SKM_Manny_Simple，而 Content/Characters 在 .gitignore 里，
	// 仓库里没有那个网格 → clone 后 ConstructorHelpers 失败，商人变成一个看不见的空 actor。
	//
	// 结构：SceneRoot 在脚下（actor 原点 = 地面），圆柱子组件抬 90cm 当身体。
	USceneComponent* SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(SceneRoot);

	// 注意：成员名必须叫 BodyMesh 而不是 Body —— 很多函数里有局部变量 FString Body（对话正文），
	// 叫 Body 会撞名字（C4458 变量遮蔽），而且读代码时容易看错。
	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	BodyMesh->SetupAttachment(SceneRoot);
	BodyMesh->SetRelativeLocation(FVector(0.f, 0.f, 90.f));		// 100cm 高的圆柱，中心抬到 90
	BodyMesh->SetRelativeScale3D(FVector(0.3f, 0.3f, 1.8f));	// → 直径 60 × 高 180，人形比例

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));

	if (CylinderMesh.Succeeded())
	{
		BodyMesh->SetStaticMesh(CylinderMesh.Object);
	}

	// 挡路用：玩家不能从商人身体里穿过去
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	BodyMesh->SetCollisionResponseToAllChannels(ECR_Block);
}

void AMerchant::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// 床不显示机器人：在编辑器里勾了 bHideBody 就立刻生效
	if (BodyMesh)
	{
		BodyMesh->SetVisibility(!bHideBody);
	}

	// 黑社会：旧名字（商人 / 银行行长）自动换成新称呼；你自己起的名不会被覆盖
	if (Kind == EMerchantKind::Banker
		&& (DisplayName.Equals(TEXT("商人"))
			|| DisplayName.Equals(TEXT("银行行长"))
			|| DisplayName.IsEmpty()))
	{
		DisplayName = TEXT("黑社会");
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

	// 正规买家只做双数天（第 2/4/6/8/10/12 天）—— 除了对话里说"明天再来"，人也藏起来
	const bool bOddDay = (GI->DayIndex % 2) == 1;
	const bool bShouldHide = (GI->DayIndex < MinDay)
		|| (Kind == EMerchantKind::LegitBuyer && bOddDay);
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
	SellFlow = 0;
	PendingBrass = 0.f;

	// 每次重新搭话都从第一页、主菜单开始。
	// （不清 CraftFlow 的话：上次用 ESC 关掉卖货页，这次点他会直接跳进卖货列表。）
	ListScroll = 0;
	CraftFlow = 0;
	AppraiseCandidates.Reset();

	// 黑社会页面的进度也归零
	BankFlow = 0;
	BankWarning.Reset();

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

	/** 一页摆几件（UI 一共 6 个按钮，最后一个固定留给「算了/离开」） */
	constexpr int32 GCraftListMax = 5;

	/** 背包里还没鉴定过的下标（全部，翻页时再切片） */
	void CollectUnappraised(const TArray<FHandicraftItem>& Bag, TArray<int32>& Out)
	{
		Out.Reset();
		for (int32 i = 0; i < Bag.Num(); ++i)
		{
			if (!Bag[i].bAppraised)
			{
				Out.Add(i);
			}
		}
	}

	/** 列表头那一行：「第 3–7 件 / 共 12 件」 */
	FString MakeRangeLine(int32 Scroll, int32 Shown, int32 Total)
	{
		if (Total <= Shown)
		{
			return FString();
		}

		return FString::Printf(TEXT("「第 %d–%d 件 / 共 %d 件」↑↓ 或滚轮翻页\n\n"),
			Scroll + 1, Scroll + Shown, Total);
	}
}

void AMerchant::BuildDialog(AstudyCharacter* Character)
{
	UstudyGameInstance* GI = GetRun(this);
	if (!GI || !Character)
	{
		return;
	}

	// 回到主菜单：第二行输入框（掺黄铜）一定收起来；子页面会自己再打开
	Character->SetDialogNumberEnabled2(0.f);

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

		// 全部没鉴定的（翻页从这里切片）
		CollectUnappraised(GI->HandicraftBag, AppraiseCandidates);

		if (AppraiseCandidates.Num() == 0)
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

		const int32 Total = AppraiseCandidates.Num();
		ListScroll = FMath::Clamp(ListScroll, 0, FMath::Max(0, Total - GCraftListMax));

		// 这一页摆哪几件（按钮序号 1..5 对应这一页的第 1..5 件）
		TArray<int32> Page;
		for (int32 n = ListScroll; n < Total && Page.Num() < GCraftListMax; ++n)
		{
			Page.Add(AppraiseCandidates[n]);
		}

		FString DlgBody = MakeRangeLine(ListScroll, Page.Num(), Total);
		DlgBody += FString::Printf(TEXT("拿一件给我瞧瞧。看一件 %d 币。\n\n"), FEE);

		TArray<FString> Options;
		for (int32 n = 0; n < Page.Num(); ++n)
		{
			const FHandicraftItem& It = GI->HandicraftBag[Page[n]];
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

	// ---- 黑社会：借钱 / 还钱 ----
	// （和别的 NPC 一样，就是 AMerchant + Kind，不用单独写类；在关卡里复制一个圆柱改 Kind 即可）
	if (Kind == EMerchantKind::Banker)
	{
		BuildBankDialog(Character);
		return;
	}

	// ---- 正规买家：全额收，每两天开一次窗口 ----
	if (Kind == EMerchantKind::LegitBuyer)
	{
		SellFlow = 0;
		PendingBrass = 0.f;
		Character->SetDialogNumberEnabled(0.f);

		// PRD §2.9：一季 6 场，第 2/4/6/8/10/12 天
		if (GI->DayIndex % 2 != 0)
		{
			Character->OpenOptionDialog(DisplayName,
				TEXT("「我今天不收货。明天再来吧。」\n\n（正规买家每两天来一趟）"),
				{ TEXT("知道了") });
			return;
		}

		if (GI->bBuyerRejectedToday)
		{
			Character->OpenOptionDialog(DisplayName,
				TEXT("「刚才那一包我抽了样，你还是带回去吧。」\n\n（今天这个窗口对你不开了，等下一个窗口）"),
				{ TEXT("知道了") });
			return;
		}

		TArray<FString> Options;
		Options.Add(TEXT("卖黄金"));
		Options.Add(TEXT("卖工艺品"));
		Options.Add(TEXT("算了"));

		Character->OpenOptionDialog(DisplayName,
			FString::Printf(
				TEXT("正规价：100%%（不压价）\n今天还能收：%.1f g\n你手上有：%.1f g\n现金：%d 币\n\n他每次都抽检，掺了黄铜被抽到这一趟就白跑。"),
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
		SellFlow = 0;
		PendingBrass = 0.f;
		Character->SetDialogNumberEnabled(0.f);		// 主菜单不显示输入框，进「卖黄金」页才有

		TArray<FString> Options;
		Options.Add(TEXT("卖黄金"));
		Options.Add(TEXT("购买工艺品（看看柜台）"));
		Options.Add(TEXT("卖工艺品"));
		Options.Add(TEXT("算了"));

		Character->OpenOptionDialog(
			DisplayName,
			FString::Printf(
				TEXT("手上的金重：%.1f g\n他给你的价：%.0f 币/g\n好感度：%d\n%s\n现金：%d 币%s"),
				GI->GoldGrams, GI->GetFencePricePerGram(), GI->FenceFavor,
				*GI->GetStrictnessHint(), GI->Cash,
				GI->GoldGrams < 0.05f ? TEXT("\n（身上没金子，先去矿里挖一趟）") : TEXT("")),
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

	// 黑社会：自己一套页面（贷款 / 还款）
	if (Kind == EMerchantKind::Banker)
	{
		HandleBankOption(Character, Index);
		return;
	}

	if (CraftFlow != 0)
	{
		HandleCraftSellOption(Character, Index);
		return;
	}

	// ---- 卖黄金的三级页面 ----
	if (SellFlow == 1)		// 入口页：老老实实卖 / 掺点黄铜卖 / 上一步
	{
		if (Index == 0) { BuildHonestSellPage(Character); return; }
		if (Index == 1) { BuildAdulteratedSellPage(Character); return; }

		SellFlow = 0;
		BuildDialog(Character);		// 上一步
		return;
	}

	if (SellFlow == 2)		// 老实卖页：卖出全部 / 上一步
	{
		if (Index == 0)
		{
			SettleGoldSale(Character, GI->GoldGrams, 0.f, Kind == EMerchantKind::LegitBuyer);
			return;
		}

		OpenGoldSellPage(Character);
		return;
	}

	if (SellFlow == 3)		// 掺着卖页：就这么卖 / 上一步
	{
		if (Index == 0)
		{
			if (!bSellGramsFilled || !bBrassFilled)
			{
				ShowResult(Character, TEXT("两行都要填：先填卖多少克黄金，再填掺多少克黄铜。"));
				return;
			}

			SettleGoldSale(Character, PendingSellGrams, PendingBrass, Kind == EMerchantKind::LegitBuyer);
			return;
		}

		OpenGoldSellPage(Character);
		return;
	}

	// ---- 床 ----
	// ---- 听泉鉴宝 ----
	if (Kind == EMerchantKind::Appraiser)
	{
		const int32 FEE = 500;

		// 点的是当前这一页的第 Index 件 → 换成它在"没鉴定列表"里的绝对位置
		const int32 Slot = ListScroll + Index;

		if (Index < 0 || !AppraiseCandidates.IsValidIndex(Slot))
		{
			Character->CloseDialog();
			return;
		}

		if (GI->Cash < FEE)
		{
			Character->CloseDialog();
			return;
		}

		FHandicraftItem& It = GI->HandicraftBag[AppraiseCandidates[Slot]];
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
			OpenGoldSellPage(Character);
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
			// 进「卖黄金」页：先填掺多少黄铜，再确认
			OpenGoldSellPage(Character);
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
		// 必须用 ShowResult：它会把 DialogMerchant 摘掉。
		// 不摘的话点「知道了」会被当成又点了菜单第 0 项（卖黄金），
		// 于是刚说完"没有工艺品"，紧接着又弹一句"你身上没金子"。
		CraftFlow = 0;
		ShowResult(Character, TEXT("你身上一件工艺品都没有。\n\n先去黑市柜台上看看？"));
		return;
	}

	const int32 Total = GI->HandicraftBag.Num();
	ListScroll = FMath::Clamp(ListScroll, 0, FMath::Max(0, Total - GCraftListMax));

	// 这一页摆哪几件（按背包顺序）
	for (int32 i = ListScroll; i < Total && CraftCandidates.Num() < GCraftListMax; ++i)
	{
		CraftCandidates.Add(i);
	}

	FString CraftBody = MakeRangeLine(ListScroll, CraftCandidates.Num(), Total);

	CraftBody += bToLegit
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
	Character->SetDialogNumberStyle(TEXT("卖"), TEXT("含量"), TEXT("%"));

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

void AMerchant::OnDialogScroll(AstudyCharacter* Character, int32 Delta)
{
	UstudyGameInstance* GI = GetRun(this);
	if (!GI || !Character || Delta == 0)
	{
		return;
	}

	// 现在看的是哪张列表？（只有这两张能翻页）
	int32 Total = 0;
	if (CraftFlow != 0)
	{
		Total = GI->HandicraftBag.Num();
	}
	else if (Kind == EMerchantKind::Appraiser)
	{
		Total = AppraiseCandidates.Num();
	}
	else
	{
		return;	// 菜单 / 填数字 / 结算页：翻页没意义
	}

	const int32 MaxScroll = FMath::Max(0, Total - GCraftListMax);
	if (MaxScroll <= 0)
	{
		return;	// 一页就装得下
	}

	const int32 NewScroll = FMath::Clamp(ListScroll + Delta, 0, MaxScroll);
	if (NewScroll == ListScroll)
	{
		return;	// 已经顶到头了：不重刷，免得闪
	}

	ListScroll = NewScroll;
	BuildDialog(Character);
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

	// 报价 − 实际：>0 是往高报（想坑他），<0 是你自己报低了
	const float Gap = Reported - Real;

	// 结算价 = 标称 × 含金量 × 1000 × 当天黑市价 × 0.95（他抽走 5%）
	auto Payout = [&](float Purity)
	{
		return FMath::RoundToInt(It.NominalGrams * Purity * 1000.f * GI->BlackMarketDailyRate * 0.95f);
	};

	// ---- 第一步：他验不验？----
	// 这是**一个独立的概率**，跟你报多少没关系：好感越高，他越懒得验。
	// 验了之后才拿"你报的"和"真的"比 —— 报低了他捡漏，报高了他识破。
	const float InspectChance = FMath::Clamp(
		0.72f - 0.0052f * static_cast<float>(GI->FenceFavor), 0.10f, 0.80f);
	const bool bInspected = FMath::FRand() < InspectChance;

	int32 Earned = 0;
	FString Verdict;

	if (!bInspected)
	{
		// 没验：按你报的价成交（报高了就是白赚，报低了就是白亏）
		Earned = Payout(Reported);
		Verdict = FString::Printf(
			TEXT("「行，收下了。」\n\n他没多看，直接过了秤。\n按 %.0f%% 算，给你 %d 币。"),
			Reported * 100.f, Earned);
	}
	else if (Gap < -0.02f)
	{
		// 验了，而且成色比你说的还好 → 他觉得捡漏了
		Earned = Payout(Reported);
		GI->FenceFavor = FMath::Clamp(GI->FenceFavor + 1, 0, 100);
		Verdict = FString::Printf(
			TEXT("「哟——」他把东西凑到灯下又看了看，「这成色比你报的还好啊。」\n\n"
				"「你自己都没看出来吧？行，就按你说的价，我收了。」\n\n"
				"按 %.0f%% 算，给你 %d 币。\n（好感度 +1，他捡漏了）"),
			Reported * 100.f, Earned);
	}
	else if (Gap > 0.005f)
	{
		// 验了，而且你报高了 → 当场识破
		Earned = Payout(Real);
		GI->FenceFavor = FMath::Clamp(GI->FenceFavor - 5, 0, 100);
		Verdict = FString::Printf(
			TEXT("「你小子拿这玩意儿糊弄我？」\n\n他当场砸开看了一眼。\n真实含金量 %.0f%%，按这个给你 %d 币。\n（好感度 -5）"),
			Real * 100.f, Earned);
	}
	else
	{
		// 验了，报价也没差 → 挑不出毛病
		Earned = Payout(Reported);
		Verdict = FString::Printf(
			TEXT("「……嗯，成色跟你说的一样。」\n\n他过了秤，按 %.0f%% 算，给你 %d 币。"),
			Reported * 100.f, Earned);
	}

	GI->Cash += Earned;
	GI->HandicraftBag.RemoveAt(BagIndex);

	UGameplayStatics::PlaySound2D(this, StudySounds::Cash(), 0.9f);

	UE_LOG(LogTemp, Log, TEXT("[CraftSell] 黑商 真实 %.0f%% 报 %.0f%%（差 %+.0f 点）验=%d → %s，%d 币（好感 %d）"),
		Real * 100.f, Reported * 100.f, Gap * 100.f, bInspected ? 1 : 0,
		!bInspected ? TEXT("没验") : (Gap < -0.02f ? TEXT("捡漏") : (Gap > 0.005f ? TEXT("识破") : TEXT("对得上"))),
		Earned, GI->FenceFavor);

	CraftFlow = 0;
	Character->SetDialogNumberEnabled(0.f);
	ShowResult(Character, Verdict);
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
	ShowResult(Character,
		FString::Printf(TEXT("（他当着你的面过了秤、化了点边角料）\n\n成色 %.0f%%。\n按全价给你 %d 币。"),
			It.RealPurity * 100.f, Earned));
}

void AMerchant::OnDialogNumber2(AstudyCharacter* Character, float Value)
{
	UstudyGameInstance* GI = GetRun(this);
	if (!GI || !Character)
	{
		return;
	}

	// 掺着卖页：第二行 = 掺多少克黄铜（买不起就压到买得起的量并提示）
	if (SellFlow == 3)
	{
		const float MaxBrass = GI->Cash / static_cast<float>(UstudyGameInstance::BrassPricePerGram);
		float BrassValue = FMath::Max(Value, 0.f);

		if (BrassValue > MaxBrass + 0.01f)
		{
			SellWarning = FString::Printf(TEXT("你的钱不够买这么多黄铜（最多 %.1f g）"), MaxBrass);
			Character->SetDialogNumberPrefill(MaxBrass, true);
			BrassValue = MaxBrass;
		}

		PendingBrass = BrassValue;
		bBrassFilled = true;
		BuildAdulteratedSellPage(Character);
	}
}

void AMerchant::OnDialogNumber(AstudyCharacter* Character, float Value)
{
	UstudyGameInstance* GI = GetRun(this);
	if (!GI)
	{
		return;
	}

	// 黑社会：这个数字是"借多少 / 还多少"
	if (Kind == EMerchantKind::Banker)
	{
		HandleBankNumber(Character, Value);
		return;
	}

	// 老实卖页：填多少克就卖多少克，不掺
	if (SellFlow == 2)
	{
		if (Value > GI->GoldGrams + 0.01f)
		{
			// 填超了：不给卖，提示一句，并把输入框改成最大值
			SellWarning = FString::Printf(TEXT("你哪有这么多黄金（手上只有 %.1f g）"), GI->GoldGrams);
			Character->SetDialogNumberPrefill(GI->GoldGrams, false);
			BuildHonestSellPage(Character);
			return;
		}

		SettleGoldSale(Character, FMath::Clamp(Value, 0.f, GI->GoldGrams), 0.f,
			Kind == EMerchantKind::LegitBuyer);
		return;
	}

	// 掺着卖页：第一行 = 卖多少克黄金
	if (SellFlow == 3)
	{
		if (Value > GI->GoldGrams + 0.01f)
		{
			SellWarning = FString::Printf(TEXT("你哪有这么多黄金（手上只有 %.1f g）"), GI->GoldGrams);
			Character->SetDialogNumberPrefill(GI->GoldGrams, false);
			BuildAdulteratedSellPage(Character);
			return;
		}

		PendingSellGrams = FMath::Clamp(Value, 0.f, GI->GoldGrams);
		bSellGramsFilled = true;
		BuildAdulteratedSellPage(Character);
		return;
	}

	// 工艺品报价（0-100）
	if (PendingCraftSell >= 0)
	{
		if (Value <= 0.f)
		{
			return;
		}

		const int32 BagIndex = PendingCraftSell;
		PendingCraftSell = -1;
		SellHandicraftToFence(Character, BagIndex, FMath::Clamp(Value, 0.f, 100.f) / 100.f);
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

// ==================== 卖黄金：掺假 ====================

void AMerchant::OpenGoldSellPage(AstudyCharacter* Character)
{
	UstudyGameInstance* GI = GetRun(this);
	if (!GI || !Character)
	{
		return;
	}

	// 没金子就明说一句，不要静悄悄地把菜单重画一遍（那样看起来像"点了没反应"）
	if (GI->GoldGrams < 0.05f)
	{
		GI->GoldGrams = 0.f;		// 顺手清掉采矿留下的浮点残渣（0.03 g 这种）
		SellFlow = 0;
		SellWarning.Reset();
		ShowResult(Character, TEXT("你身上没金子。\n\n先去矿里挖一趟再回来。"));
		return;
	}

	const bool bToLegit = (Kind == EMerchantKind::LegitBuyer);

	SellFlow = 1;
	PendingBrass = 0.f;
	PendingSellGrams = 0.f;
	bSellGramsFilled = false;
	bBrassFilled = false;
	SellWarning.Reset();

	Character->SetDialogNumberEnabled(0.f);
	Character->SetDialogNumberEnabled2(0.f);

	TArray<FString> Options;
	Options.Add(TEXT("老老实实卖"));
	Options.Add(TEXT("掺点黄铜卖"));
	Options.Add(TEXT("上一步"));

	FString Body = FString::Printf(TEXT("手上的金重：%.1f g\n"), GI->GoldGrams);

	if (bToLegit)
	{
		Body += FString::Printf(
			TEXT("他按正规价 100%% 收，今天还能收 %.1f g。\n"
				 "他每次都抽检 —— 掺了黄铜被抽到，这一包退回来，今天这个窗口就没了。"),
			GI->BuyerQuotaGrams);
	}
	else
	{
		Body += FString::Printf(
			TEXT("他给你的价：%.0f 币/g\n好感度 %d\n%s"),
			GI->GetFencePricePerGram(), GI->FenceFavor, *GI->GetStrictnessHint());
	}

	Body += FString::Printf(TEXT("\n\n黄铜 %d 币/g，按掺进去的克数现钱扣。"),
		UstudyGameInstance::BrassPricePerGram);

	Character->OpenOptionDialog(DisplayName, Body, Options);
	Character->SetDialogMerchant(this);
}

void AMerchant::BuildHonestSellPage(AstudyCharacter* Character)
{
	UstudyGameInstance* GI = GetRun(this);
	if (!GI || !Character)
	{
		return;
	}

	const bool bToLegit = (Kind == EMerchantKind::LegitBuyer);
	const float Unit = bToLegit ? static_cast<float>(AstudyGameState::BasePricePerGram)
								: GI->GetFencePricePerGram();

	SellFlow = 2;
	PendingBrass = 0.f;
	bSellGramsFilled = false;
	bBrassFilled = false;

	Character->SetDialogNumberStyle(TEXT("卖"), TEXT("克数"), TEXT("g"));
	Character->SetDialogNumberEnabled(GI->GoldGrams);	// 第一行：卖多少 g
	Character->SetDialogNumberEnabled2(0.f);

	// 默认就填最大值：一次卖光是最常见的操作，玩家按一下确认就行
	Character->SetDialogNumberPrefill(GI->GoldGrams, false);

	TArray<FString> Options;
	Options.Add(TEXT("卖出全部"));
	Options.Add(TEXT("上一步"));

	FString Body;
	if (!SellWarning.IsEmpty())
	{
		Body = FString::Printf(TEXT("⚠ %s\n\n"), *SellWarning);
		SellWarning.Reset();
	}

	Body += FString::Printf(
		TEXT("手上的金重：%.1f g\n单价：%.0f 币/g\n\n"
			 "上面那行填要卖多少克（已替你填成全部，改小就卖一部分）。\n不掺黄铜，一手交一手验。"),
		GI->GoldGrams, Unit);

	Character->OpenOptionDialog(DisplayName, Body, Options);
	Character->SetDialogMerchant(this);
}

void AMerchant::BuildAdulteratedSellPage(AstudyCharacter* Character)
{
	UstudyGameInstance* GI = GetRun(this);
	if (!GI || !Character)
	{
		return;
	}

	const bool bToLegit = (Kind == EMerchantKind::LegitBuyer);
	const float Unit = bToLegit ? static_cast<float>(AstudyGameState::BasePricePerGram)
								: GI->GetFencePricePerGram();

	SellFlow = 3;

	// 第一行填"卖多少 g"，第二行填"掺多少 g"
	Character->SetDialogNumberStyle(TEXT("卖"), TEXT("克数"), TEXT("g"));
	Character->SetDialogNumberEnabled(GI->GoldGrams);
	Character->SetDialogNumberEnabled2(FMath::Max(GI->GoldGrams, 1.f));

	// 第一行默认填满（卖光最常见）；填过什么就回显什么
	Character->SetDialogNumberPrefill(bSellGramsFilled ? PendingSellGrams : GI->GoldGrams, false);
	if (bBrassFilled)
	{
		Character->SetDialogNumberPrefill(PendingBrass, true);
	}

	TArray<FString> Options;
	Options.Add(TEXT("就这么卖"));
	Options.Add(TEXT("上一步"));

	const float SellG = bSellGramsFilled ? PendingSellGrams : 0.f;
	const float BrassG = bBrassFilled ? PendingBrass : 0.f;
	const float Declared = SellG + BrassG;
	const int32 BrassCost = FMath::RoundToInt(BrassG * UstudyGameInstance::BrassPricePerGram);
	const int32 Gain = FMath::RoundToInt(Declared * Unit) - FMath::RoundToInt(SellG * Unit);

	FString Body;
	if (!SellWarning.IsEmpty())
	{
		Body = FString::Printf(TEXT("⚠ %s\n\n"), *SellWarning);
		SellWarning.Reset();
	}

	Body += FString::Printf(
		TEXT("手上的金重：%.1f g\n单价：%.0f 币/g\n\n"
			 "① 第一行填要卖多少克：%s\n"
			 "② 第二行填掺多少克黄铜：%s\n\n"
			 "黄铜 %d 币/g，按掺入克数现钱扣。"),
		GI->GoldGrams, Unit,
		bSellGramsFilled ? *FString::Printf(TEXT("%.1f g"), SellG) : TEXT("还没填"),
		bBrassFilled ? *FString::Printf(TEXT("%.1f g"), BrassG) : TEXT("还没填"),
		UstudyGameInstance::BrassPricePerGram);

	if (bSellGramsFilled && bBrassFilled)
	{
		Body += FString::Printf(
			TEXT("\n申报 %.1f g → 约多拿 %d 币（铜费 %d 币）"), Declared, Gain, BrassCost);

		if (BrassG > 0.01f)
		{
			Body += FString::Printf(TEXT("\n风险：%s"), *GI->GetAdulterationHintEx(SellG, BrassG));
		}
	}
	else
	{
		Body += TEXT("\n两行都填了才能卖。");
	}

	Character->OpenOptionDialog(DisplayName, Body, Options);
	Character->SetDialogMerchant(this);
}

void AMerchant::ShowResult(AstudyCharacter* Character, const FString& Body)
{
	if (!Character)
	{
		return;
	}

	Character->SetDialogNumberEnabled(0.f);
	Character->SetDialogNumberEnabled2(0.f);
	Character->OpenOptionDialog(DisplayName, Body, { TEXT("知道了") });

	// 关键：结果页不挂商人。否则点「知道了」会被当成又点了菜单第 0 项
	Character->SetDialogMerchant(nullptr);
}

void AMerchant::SettleGoldSale(AstudyCharacter* Character, float SellGrams, float BrassGrams, bool bToLegit)
{
	UstudyGameInstance* GI = GetRun(this);
	if (!GI || !Character || GI->GoldGrams <= 0.f)
	{
		SellFlow = 0;
		if (Character)
		{
			BuildDialog(Character);
		}
		return;
	}

	SellFlow = 0;
	SellWarning.Reset();

	const float Total = GI->GoldGrams;
	const float G = FMath::Clamp(SellGrams, 0.f, Total);		// 这一笔卖掉多少真金
	const float Brass = FMath::Clamp(BrassGrams, 0.f, Total * 2.f);
	if (G <= 0.f)
	{
		ShowResult(Character, TEXT("克数得填大于 0 的数。"));
		return;
	}

	const float R = (G + Brass > 0.f) ? (Brass / (G + Brass)) : 0.f;
	const int32 BrassCost = FMath::RoundToInt(Brass * UstudyGameInstance::BrassPricePerGram);

	// ⚠ 概率要先算（它读金重），再动库存
	const float BlackCatch = GI->GetAdulterationCatchChanceEx(G, Brass);
	const float SpotChance = FMath::Clamp(0.10f + 1.0f * R, 0.f, 0.95f);

	// 只扣掉这一笔；剩下的金子还在手上
	GI->GoldGrams = FMath::Max(Total - G, 0.f);
	GI->Cash = FMath::Max(GI->Cash - BrassCost, 0);

	FString Body;

	if (bToLegit)
	{
		const bool bSpot = (Brass > 0.01f) && !GI->bBuyerFirstPass && (FMath::FRand() < SpotChance);

		if (bSpot)
		{
			// 整包退回：这一笔的金重还回去，黄铜钱白花，今天这个窗口不再收
			GI->GoldGrams = FMath::Min(Total, GI->GoldGrams + G);
			GI->bBuyerRejectedToday = true;

			Body = FString::Printf(
				TEXT("（他剪了一小块丢进坩埚，看了一会儿，没说话）\n\n"
					 "「这一包我不收。」他把袋子推回来。\n\n"
					 "金重退回 %.1f g，黄铜那 %d 币白花了。今天这个窗口对你不开了。"),
				G, BrassCost);

			GI->PushFeed(TEXT("你在正规买家那儿被抽检出来，整包退回"));
			UE_LOG(LogTemp, Log, TEXT("[Gold] 正规买家抽检命中（掺 %.0f%%，概率 %.0f%%）→ 退回"),
				R * 100.f, SpotChance * 100.f);
		}
		else
		{
			const float Declared = G + Brass;
			const float Accepted = FMath::Min(Declared, GI->BuyerQuotaGrams);
			const float Ratio = (Declared > 0.f) ? (Accepted / Declared) : 1.f;

			GI->Cash += FMath::RoundToInt(Accepted * static_cast<float>(AstudyGameState::BasePricePerGram));
			GI->BuyerQuotaGrams = FMath::Max(GI->BuyerQuotaGrams - Accepted, 0.f);

			// 配额不够时，剩下的真金还给你（按比例，简化）
			if (Accepted < Declared - 0.01f)
			{
				GI->GoldGrams += G * (1.f - Ratio);
			}

			GI->bBuyerFirstPass = false;

			Body = FString::Printf(
				TEXT("（他过了秤，按你申报的 %.1f g 记账）\n\n收了 %.1f g，给你 %d 币。"),
				Declared, Accepted,
				FMath::RoundToInt(Accepted * static_cast<float>(AstudyGameState::BasePricePerGram)));

			GI->PushFeed(FString::Printf(TEXT("你把 %.1f g 卖给正规买家，+%d 币"), Accepted,
				FMath::RoundToInt(Accepted * static_cast<float>(AstudyGameState::BasePricePerGram))));
			UE_LOG(LogTemp, Log, TEXT("[Gold] 卖正规 申报 %.1f g（真实 %.1f）→ %d 币"),
				Declared, G, FMath::RoundToInt(Accepted * static_cast<float>(AstudyGameState::BasePricePerGram)));
		}
	}
	else
	{
		const bool bCaught = (Brass > 0.01f) && (FMath::FRand() < BlackCatch);
		const float Unit = GI->GetFencePricePerGram();

		if (bCaught)
		{
			const int32 Gross = FMath::RoundToInt(G * Unit);
			const int32 Fine = FMath::RoundToInt(Gross * 0.15f);
			const int32 Earned = FMath::Max(Gross - Fine, 0);
			GI->Cash += Earned;

			// 好感降幅按掺的比例走（v3.23：−2 / −5 / −8）
			int32 FavorDrop = 2;
			if (R > 0.30f) { FavorDrop = 8; }
			else if (R > 0.15f) { FavorDrop = 5; }
			GI->FenceFavor = FMath::Clamp(GI->FenceFavor - FavorDrop, 0, 100);

			Body = FString::Printf(
				TEXT("他把袋子往秤上一扔，手一掂就放下了。\n\n"
					 "「你这手感不对。」\n\n"
					 "他倒出来过了秤，只算 %.1f g 真金，还扣了 15%% 的罚款。\n"
					 "到手 %d 币（黄铜那 %d 币也白花了）。\n好感 −%d，现在他是%s。"),
				G, Earned, BrassCost, FavorDrop, *GI->GetFavorLabel());

			GI->PushFeed(FString::Printf(TEXT("你在黑市掺了黄铜被当场识破（好感 −%d）"), FavorDrop));
			UE_LOG(LogTemp, Log, TEXT("[Gold] 黑市识破（掺 %.0f%%，概率 %.0f%%）→ 真金 %.1f g，%d 币，好感 %d"),
				R * 100.f, BlackCatch * 100.f, G, Earned, GI->FenceFavor);
		}
		else
		{
			const float Declared = G + Brass;
			const int32 Earned = FMath::RoundToInt(Declared * Unit);
			GI->Cash += Earned;

			// 没被抓住就加好感：每卖 5 g +1，按【申报重量】算（掺了黄铜也算重量）
			GI->FenceFavor = FMath::Clamp(
				GI->FenceFavor + FMath::FloorToInt(Declared / 5.f), 0, 100);

			// 抛出去的量大，价就掉
			GI->ApplyBlackMarketDump(Declared);

			Body = FString::Printf(
				TEXT("他没多问，过了秤就付钱。\n\n申报 %.1f g，按 %.0f 币/g 给你 %d 币。"),
				Declared, Unit, Earned);

			GI->PushFeed(FString::Printf(TEXT("你把 %.1f g 卖给黑市，+%d 币"), Declared, Earned));
			UE_LOG(LogTemp, Log, TEXT("[Gold] 卖黑市 申报 %.1f g（真实 %.1f）→ %d 币，好感 %d"),
				Declared, G, Earned, GI->FenceFavor);
		}
	}

	UGameplayStatics::PlaySound2D(this, StudySounds::Cash(), 0.9f);

	PendingBrass = 0.f;
	PendingSellGrams = 0.f;
	bSellGramsFilled = false;
	bBrassFilled = false;
	ShowResult(Character, Body);
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

// ==================== 黑社会（放数） ====================
//
// 和别的 NPC 一样：AMerchant + Kind == Banker，在关卡里复制一个圆柱改 Kind 即可。
// 页面结构复用"卖黄金"那套两级页：一个选项页 + 一个行内数字输入行。
//   0 主菜单：没欠款 [贷款][离开]；有欠款 [贷款][还款][离开]
//   1 贷款页：数字行「借 ___ 币」+ [上一步]（填好点数字行上的「确认」）
//   2 还款页：显示未还金额，数字行「还 ___ 币」+ [全部还清][上一步]
// 数值（日息 / 额度上限 / 结算扣款）都在 UstudyGameInstance 里。

void AMerchant::BuildBankDialog(AstudyCharacter* Character)
{
	UstudyGameInstance* GI = GetRun(this);
	if (!GI || !Character)
	{
		return;
	}

	// DialogChoose 会先关对话（清掉 DialogMerchant）再回调，所以这里必须重新认领
	Character->SetDialogMerchant(this);
	Character->SetDialogNumberEnabled2(0.f);

	const int32 Rate = FMath::RoundToInt(UstudyGameInstance::LoanDailyRate * 100.f);
	const FString Mult = FString::Printf(TEXT("%.2f"), 1.f + UstudyGameInstance::LoanDailyRate);

	// ---------- 主菜单 ----------
	if (BankFlow == 0)
	{
		Character->SetDialogNumberEnabled(0.f);

		FString Body;
		if (!BankWarning.IsEmpty())
		{
			Body += FString::Printf(TEXT("⚠ %s\n\n"), *BankWarning);
			BankWarning.Reset();
		}

		Body += FString::Printf(TEXT("「手头紧？我这儿放数。日息 %d%%，利滚利。」\n\n"), Rate);
		Body += FString::Printf(TEXT("现金：%d 币\n欠款：%d 币"), GI->Cash, GI->GetLoanOwed());

		if (GI->HasLoan())
		{
			Body += FString::Printf(TEXT("（每天 ×%s，明天会 +%d）"), *Mult, GI->GetLoanDailyInterest());
		}

		Body += FString::Printf(
			TEXT("\n\n额度 %d 币，还能借 %d 币。\n"
				 "十二天到期还不清，结算的时候我直接从你的钱里扣。"),
			UstudyGameInstance::LoanLimit, GI->GetLoanRoom());

		TArray<FString> Options;
		Options.Add(TEXT("贷款"));
		if (GI->HasLoan())
		{
			Options.Add(TEXT("还款"));
		}
		Options.Add(TEXT("离开"));

		Character->OpenOptionDialog(DisplayName, Body, Options);
		return;
	}

	// ---------- 贷款页 ----------
	if (BankFlow == 1)
	{
		const int32 Room = GI->GetLoanRoom();

		// 已经顶到额度了：别让玩家填一个注定失败的数，直接说清楚
		if (Room <= 0)
		{
			BankWarning = FString::Printf(TEXT("你已经欠了 %d 币，到额度上限了，先还上再说。"), GI->GetLoanOwed());
			BankFlow = 0;
			BuildBankDialog(Character);
			return;
		}

		Character->SetDialogNumberStyle(TEXT("借"), TEXT("金额"), TEXT("币"));
		Character->SetDialogNumberEnabled(static_cast<float>(Room));

		FString Body;
		if (!BankWarning.IsEmpty())
		{
			Body += FString::Printf(TEXT("⚠ %s\n\n"), *BankWarning);
			BankWarning.Reset();
		}

		Body += FString::Printf(
			TEXT("「借多少？」\n\n"
				 "现金：%d 币\n"
				 "已欠：%d 币\n"
				 "日息 %d%% —— 每天醒来欠款 ×%s\n"
				 "还能借：%d 币\n\n"
				 "填好金额后，点下面那行的「确认」。"),
			GI->Cash, GI->GetLoanOwed(), Rate, *Mult, Room);

		Character->OpenOptionDialog(DisplayName, Body, { TEXT("上一步") });
		Character->SetDialogNumberPrefill(static_cast<float>(FMath::Min(1000, FMath::Max(Room, 1))));
		Character->SetDialogMerchant(this);
		return;
	}

	// ---------- 还款页 ----------
	Character->SetDialogNumberStyle(TEXT("还"), TEXT("金额"), TEXT("币"));

	const int32 Owed = GI->GetLoanOwed();
	const int32 Payable = FMath::Min(GI->Cash, Owed);
	Character->SetDialogNumberEnabled(static_cast<float>(FMath::Max(Payable, 0)));

	FString Body;
	if (!BankWarning.IsEmpty())
	{
		Body += FString::Printf(TEXT("⚠ %s\n\n"), *BankWarning);
		BankWarning.Reset();
	}

	Body += FString::Printf(
		TEXT("「还多少？」\n\n"
			 "现金：%d 币\n"
			 "未还：%d 币\n"
			 "日息 %d%%（明天会 +%d）\n\n"
			 "填好金额点「确认」；也可以点「全部还清」一次结清。"),
		GI->Cash, Owed, Rate, GI->GetLoanDailyInterest());

	if (GI->Cash <= 0)
	{
		Body += TEXT("\n\n（你一分钱都没有，我可不做慈善。）");
	}

	TArray<FString> Options;
	Options.Add(TEXT("全部还清"));
	Options.Add(TEXT("上一步"));

	Character->OpenOptionDialog(DisplayName, Body, Options);
	Character->SetDialogNumberPrefill(static_cast<float>(FMath::Max(Payable, 0)));
	Character->SetDialogMerchant(this);
}

void AMerchant::HandleBankOption(AstudyCharacter* Character, int32 Index)
{
	UstudyGameInstance* GI = GetRun(this);
	if (!GI || !Character)
	{
		return;
	}

	// 贷款页：只有「上一步」
	if (BankFlow == 1)
	{
		BankFlow = 0;
		BuildBankDialog(Character);
		return;
	}

	// 还款页：[全部还清] [上一步]
	if (BankFlow == 2)
	{
		if (Index == 0)
		{
			const int32 Pay = FMath::Min(GI->Cash, GI->GetLoanOwed());
			if (Pay > 0)
			{
				GI->RepayLoan(Pay);
				UGameplayStatics::PlaySound2D(this, StudySounds::Cash(), 0.8f);
			}
			else
			{
				BankWarning = TEXT("你手上的现金不够还。");
			}
		}

		BankFlow = 0;
		BuildBankDialog(Character);
		return;
	}

	// 主菜单：[贷款] [还款] [离开]
	if (Index == 0)
	{
		BankFlow = 1;
		BuildBankDialog(Character);
		return;
	}

	if (GI->HasLoan() && Index == 1)
	{
		BankFlow = 2;
		BuildBankDialog(Character);
		return;
	}

	// 「离开」：DialogChoose 已经把对话关掉了
	BankFlow = 0;
}

void AMerchant::HandleBankNumber(AstudyCharacter* Character, float Value)
{
	UstudyGameInstance* GI = GetRun(this);
	if (!GI || !Character)
	{
		return;
	}

	const int32 Amount = FMath::RoundToInt(Value);

	// 贷款页：额度上限 = 欠款总额不许超过 LoanLimit
	if (BankFlow == 1)
	{
		if (Amount <= 0)
		{
			BankWarning = TEXT("借款金额得大于 0。");
		}
		else if (Amount > GI->GetLoanRoom())
		{
			BankWarning = FString::Printf(TEXT("最多还能借 %d 币。"), GI->GetLoanRoom());
		}
		else
		{
			GI->TakeLoan(Amount);
			UGameplayStatics::PlaySound2D(this, StudySounds::Cash(), 0.8f);
			BankFlow = 0;
		}

		BuildBankDialog(Character);
		return;
	}

	// 还款页：自动夹在"手上的现金"和"实际欠款"之间（填超了不报错，直接还满）
	if (BankFlow == 2)
	{
		const int32 Payable = FMath::Min(GI->Cash, GI->GetLoanOwed());

		if (Amount <= 0)
		{
			BankWarning = TEXT("还款金额得大于 0。");
		}
		else if (Payable <= 0)
		{
			BankWarning = TEXT("你手上的现金不够还。");
		}
		else
		{
			GI->RepayLoan(FMath::Min(Amount, Payable));
			UGameplayStatics::PlaySound2D(this, StudySounds::Cash(), 0.8f);
			BankFlow = 0;
		}

		BuildBankDialog(Character);
		return;
	}

	// 主菜单不该走到这里
	BuildBankDialog(Character);
}
