// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZTrainingDummy.h"
#include "MHGZDummyConfig.h"

AMHGZTrainingDummy::AMHGZTrainingDummy()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AMHGZTrainingDummy::BeginPlay()
{
	Super::BeginPlay();
}

void AMHGZTrainingDummy::ApplyConfig(UMHGZDummyConfig* Config)
{
	if (!Config) return;

	// 设置骨骼模型
	if (USkeletalMesh* SKMesh = Config->DisplayMesh.LoadSynchronous())
	{
		GetMesh()->SetSkeletalMesh(SKMesh);
	}

	// 生成部位碰撞体
	GenerateHitzonesFromConfig(Config);

	// 播放待机动画
	if (UAnimMontage* IdleMontage = Config->LoopingMontage.LoadSynchronous())
	{
		if (UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance())
		{
			AnimInstance->Montage_Play(IdleMontage);
		}
	}
}
