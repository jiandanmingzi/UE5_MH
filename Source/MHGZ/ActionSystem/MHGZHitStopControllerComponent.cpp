// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZHitStopControllerComponent.h"

UMHGZHitStopControllerComponent::UMHGZHitStopControllerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

int64 UMHGZHitStopControllerComponent::RequestHitStop(float Duration)
{
	if (Duration <= 0.f || bIsEnding)
	{
		return 0;
	}

	UWorld* World = GetWorld();
	AActor* Owner = GetOwner();
	if (!World || !Owner)
	{
		return 0;
	}

	const int64 Token = NextToken;
	NextToken = (NextToken == TNumericLimits<int64>::Max()) ? 1 : NextToken + 1;

	FHitStopRequest Request;
	Request.EndTime = World->GetTimeSeconds() + Duration;
	ActiveRequests.Add(Token, Request);

	// 首次请求：保存原 Dilation 并应用卡肉倍率；后续请求只合并 Token。
	if (!bAppliedDilation)
	{
		OriginalTimeDilation = Owner->CustomTimeDilation;
		Owner->CustomTimeDilation = HitStopTimeDilation;
		bAppliedDilation = true;
	}

	return Token;
}

void UMHGZHitStopControllerComponent::ReleaseHitStop(int64 Token)
{
	if (Token == 0)
	{
		return;
	}
	ActiveRequests.Remove(Token);
	TryRestoreDilationIfEmpty();
}

void UMHGZHitStopControllerComponent::ClearAll()
{
	ActiveRequests.Empty();
	TryRestoreDilationIfEmpty();
}

float UMHGZHitStopControllerComponent::GetCurrentTimeDilation() const
{
	const AActor* Owner = GetOwner();
	return Owner ? Owner->CustomTimeDilation : 1.f;
}

void UMHGZHitStopControllerComponent::TickComponent(
	float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bAppliedDilation || ActiveRequests.IsEmpty())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();
	TArray<int64> ExpiredTokens;
	for (const TPair<int64, FHitStopRequest>& Pair : ActiveRequests)
	{
		if (Pair.Value.EndTime <= Now)
		{
			ExpiredTokens.Add(Pair.Key);
		}
	}
	for (const int64 Token : ExpiredTokens)
	{
		ActiveRequests.Remove(Token);
	}

	// 其他请求尚存时不恢复；全部到期才恢复原值。
	TryRestoreDilationIfEmpty();
}

void UMHGZHitStopControllerComponent::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	ClearAll();
	bIsEnding = true;
	Super::EndPlay(EndPlayReason);
}

void UMHGZHitStopControllerComponent::TryRestoreDilationIfEmpty()
{
	if (!bAppliedDilation || !ActiveRequests.IsEmpty())
	{
		return;
	}

	if (AActor* Owner = GetOwner())
	{
		Owner->CustomTimeDilation = OriginalTimeDilation;
	}
	bAppliedDilation = false;
}
