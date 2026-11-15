// Fill out your copyright notice in the Description page of Project Settings.


#include "StudySounds.h"
#include "Sound/SoundBase.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	/**
	 * 懒加载 + 缓存。
	 *
	 * 必须用 TStrongObjectPtr：这些变量在【命名空间作用域】，不是 UPROPERTY，
	 * 也就【不是 GC 根】。用裸指针 / TObjectPtr 的话，打包版里 GC 一跑就把音效回收掉，
	 * 留下野指针 —— 播放时直接崩溃（编辑器里不崩，因为编辑器自己还引用着这些资产）。
	 */
	USoundBase* LoadOnce(TStrongObjectPtr<USoundBase>& Cache, const TCHAR* Path)
	{
		if (!Cache.IsValid())
		{
			USoundBase* Loaded = LoadObject<USoundBase>(nullptr, Path);
			if (!Loaded)
			{
				UE_LOG(LogTemp, Warning, TEXT("[Sound] 加载失败：%s"), Path);
				return nullptr;
			}
			Cache.Reset(Loaded);
		}
		return Cache.Get();
	}

	TStrongObjectPtr<USoundBase> Snd_Dig;
	TStrongObjectPtr<USoundBase> Snd_Debris;
	TStrongObjectPtr<USoundBase> Snd_Pickup;
	TStrongObjectPtr<USoundBase> Snd_Cash;
	TStrongObjectPtr<USoundBase> Snd_Click;
	TStrongObjectPtr<USoundBase> Snd_Switch;
	TStrongObjectPtr<USoundBase> Snd_Error;
	TStrongObjectPtr<USoundBase> Snd_GameOver;
	TStrongObjectPtr<USoundBase> Snd_Footstep;
}

namespace StudySounds
{
	USoundBase* Dig()
	{
		return LoadOnce(Snd_Dig, TEXT("/Game/Free_Sounds_Pack/wav/Rock_Impact_37.Rock_Impact_37"));
	}

	USoundBase* Debris()
	{
		return LoadOnce(Snd_Debris, TEXT("/Game/Free_Sounds_Pack/wav/Rock_Large_Debris_1-3.Rock_Large_Debris_1-3"));
	}

	USoundBase* Pickup()
	{
		return LoadOnce(Snd_Pickup, TEXT("/Game/Free_Sounds_Pack/wav/Bag_Handle_1-5.Bag_Handle_1-5"));
	}

	USoundBase* Cash()
	{
		return LoadOnce(Snd_Cash, TEXT("/Game/Free_Sounds_Pack/wav/Cash_Register_1-2.Cash_Register_1-2"));
	}

	USoundBase* Click()
	{
		return LoadOnce(Snd_Click, TEXT("/Game/Interface_And_Item_Sounds/WAV/Click_01.Click_01"));
	}

	USoundBase* Switch()
	{
		return LoadOnce(Snd_Switch, TEXT("/Game/Interface_And_Item_Sounds/WAV/Flick_Switch_01.Flick_Switch_01"));
	}

	USoundBase* Error()
	{
		return LoadOnce(Snd_Error, TEXT("/Game/Interface_And_Item_Sounds/WAV/Error_Buzz_01.Error_Buzz_01"));
	}

	USoundBase* GameOver()
	{
		return LoadOnce(Snd_GameOver, TEXT("/Game/Interface_And_Item_Sounds/WAV/Discordant_GameOver_Musical_Short.Discordant_GameOver_Musical_Short"));
	}

	USoundBase* Footstep()
	{
		// 这两个包里没有专门的脚步，借用"通用打击"当脚步（播放时压低音高）
		return LoadOnce(Snd_Footstep, TEXT("/Game/Free_Sounds_Pack/wav/Hit_Generic_2-1.Hit_Generic_2-1"));
	}
}
