// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

class USoundBase;

/**
 * 全项目的音效入口。
 *
 * 为什么单独放一个文件而不是给每个类加 UPROPERTY：
 *   这些音效是"全局资源"，不需要在编辑器里一个个指定；
 *   而且同一个音效会被好几个类用到（挖矿、碎块、结算…）。
 *
 * 懒加载：第一次用到才 LoadObject，之后走指针缓存。
 * 想换音色，改这里的路径就行（或者把文件替换掉，路径不变）。
 */
namespace StudySounds
{
	/** 镐子敲石头 */
	USoundBase* Dig();

	/** 石头碎块 / 落地 */
	USoundBase* Debris();

	/** 拾取 */
	USoundBase* Pickup();

	/** 收银（卖金成交、买开采权） */
	USoundBase* Cash();

	/** UI 点击 */
	USoundBase* Click();

	/** 开关（头灯） */
	USoundBase* Switch();

	/** 出错（钱不够 / 太晚） */
	USoundBase* Error();

	/** 结算 */
	USoundBase* GameOver();

	/** 脚步（凑合用打击音压低音高） */
	USoundBase* Footstep();
}
