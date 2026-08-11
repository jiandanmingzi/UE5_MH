// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZAimComponent.h"
#include "MHGZPlayerState.h"
#include "ActionSystem/MHGZAbilitySystemComponent.h"
#include "Monster/MHGZMonsterHitzoneComponent.h"
#include "AttributeSystem/Res_InsectGlaive.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/Character.h"

UMHGZAimComponent::UMHGZAimComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.05f; // 20Hz 检测
}

void UMHGZAimComponent::BeginPlay()
{
	Super::BeginPlay();

	// 获取 ASC
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (Character && Character->GetPlayerState())
	{
		if (AMHGZPlayerState* PS = Cast<AMHGZPlayerState>(Character->GetPlayerState()))
		{
			ASC = PS->GetMHGZAbilitySystemComponent();
		}
	}

	// 订阅 Aiming Tag 变化
	if (ASC)
	{
		ASC->RegisterGameplayTagEvent(
			FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Aiming")),
			EGameplayTagEventType::NewOrRemoved)
			.AddUObject(this, &UMHGZAimComponent::OnAimingTagChanged);
	}
}

void UMHGZAimComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bIsAiming) return;

	// 瞄准中且受击/击倒 → 主动移除 Aiming Tag
	if (ASC && (ASC->HasMatchingGameplayTag(
		FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Hitstun"))) ||
		ASC->HasMatchingGameplayTag(
			FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Knockdown")))))
	{
		return;
	}

	PerformAimTrace();
}

void UMHGZAimComponent::PerformAimTrace()
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character) return;

	APlayerController* PC = Cast<APlayerController>(Character->GetController());
	if (!PC) return;

	// 相机朝向
	FVector CameraLoc;
	FRotator CameraRot;
	PC->GetPlayerViewPoint(CameraLoc, CameraRot);
	CachedCameraDir = CameraRot.Vector();

	const FVector Start = CameraLoc;
	const FVector End = CameraLoc + CachedCameraDir * AimMaxDistance;

	FHitResult Hit;
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(Character);

	if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, AimChannel, QueryParams))
	{
		// 命中——检查是否怪物部位
		UMHGZMonsterHitzoneComponent* Hitzone = Cast<UMHGZMonsterHitzoneComponent>(Hit.Component.Get());
		if (Hitzone)
		{
			CurrentAimTarget = Hit.GetActor();
			CurrentAimHitzoneTag = Hitzone->HitzoneTag;

			// 部位→萃取颜色映射
			CurrentAimExtractColor = URes_InsectGlaive::StaticMapHitzoneToExtract(Hitzone->HitzoneTag);
		}
		else
		{
			CurrentAimTarget = nullptr;
			CurrentAimHitzoneTag = FGameplayTag();
			CurrentAimExtractColor = FGameplayTag();
		}
	}
	else
	{
		CurrentAimTarget = nullptr;
		CurrentAimHitzoneTag = FGameplayTag();
		CurrentAimExtractColor = FGameplayTag();
	}

	// 仅在变化时广播
	if (CurrentAimTarget != PreviousTarget ||
		CurrentAimHitzoneTag != PreviousHitzoneTag ||
		CurrentAimExtractColor != PreviousExtractColor)
	{
		PreviousTarget = CurrentAimTarget;
		PreviousHitzoneTag = CurrentAimHitzoneTag;
		PreviousExtractColor = CurrentAimExtractColor;

		OnAimTargetChanged.Broadcast(
			CurrentAimTarget.Get(),
			CurrentAimHitzoneTag,
			CurrentAimExtractColor);
	}
}

FWeaponAimSnapshot UMHGZAimComponent::CaptureAimSnapshot(EWeaponAimSnapshotContext Context) const
{
	FWeaponAimSnapshot Snapshot;
	Snapshot.Context = Context;
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	const APlayerController* PC = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
	if (!Character || !PC || Context == EWeaponAimSnapshotContext::None)
	{
		return Snapshot;
	}

	FRotator CameraRotation;
	PC->GetPlayerViewPoint(Snapshot.Origin, CameraRotation);
	Snapshot.Direction = CameraRotation.Vector().GetSafeNormal();
	Snapshot.TargetPoint = Snapshot.Origin + Snapshot.Direction * AimMaxDistance;

	FHitResult Hit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MHGZAimSnapshot), false, Character);
	if (GetWorld() && GetWorld()->LineTraceSingleByChannel(
		Hit, Snapshot.Origin, Snapshot.TargetPoint, AimChannel, QueryParams))
	{
		Snapshot.bHasHitResult = true;
		Snapshot.HitResult = Hit;
		Snapshot.TargetPoint = Hit.ImpactPoint;
	}
	return Snapshot;
}

void UMHGZAimComponent::OnAimingTagChanged(const FGameplayTag Tag, int32 NewCount)
{
	bIsAiming = (NewCount > 0);

	if (!bIsAiming)
	{
		CurrentAimTarget = nullptr;
		CurrentAimHitzoneTag = FGameplayTag();
		CurrentAimExtractColor = FGameplayTag();
		PreviousTarget = nullptr;
		PreviousHitzoneTag = FGameplayTag();
		PreviousExtractColor = FGameplayTag();

		OnAimTargetChanged.Broadcast(nullptr, FGameplayTag(), FGameplayTag());
	}
}
