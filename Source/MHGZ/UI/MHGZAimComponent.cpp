// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZAimComponent.h"
#include "MHGZPlayerState.h"
#include "ActionSystem/MHGZAbilitySystemComponent.h"
#include "Monster/MHGZMonsterHitzoneComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/Character.h"

namespace
{
	FGameplayTag AimingTag()
	{
		static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Aiming.Kinsect"));
		return Tag;
	}

	FGameplayTag AimHitstunTag()
	{
		static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Hitstun"));
		return Tag;
	}

	FGameplayTag AimKnockdownTag()
	{
		static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Knockdown"));
		return Tag;
	}

	/** 仅 Hitzone 组件且 ObjectType==ECC_GameTraceChannel3 才算有效瞄准命中（墙体会被 Visibility Block 遮挡）。 */
	UMHGZMonsterHitzoneComponent* ResolveAimHitzone(const FHitResult& Hit)
	{
		UMHGZMonsterHitzoneComponent* Hitzone = Cast<UMHGZMonsterHitzoneComponent>(Hit.Component.Get());
		return Hitzone && Hitzone->GetCollisionObjectType() == ECC_GameTraceChannel3 ? Hitzone : nullptr;
	}
}

UMHGZAimComponent::UMHGZAimComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.05f; // 20Hz 检测
}

void UMHGZAimComponent::BeginPlay()
{
	Super::BeginPlay();

	// 尝试获取 owner ASC（PossessedBy 完成 ASC Init 后会显式 Bind，此处仅兜底）
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (Character && Character->GetPlayerState())
	{
		if (AMHGZPlayerState* PS = Cast<AMHGZPlayerState>(Character->GetPlayerState()))
		{
			BindToAbilitySystem(PS->GetMHGZAbilitySystemComponent());
		}
	}
}

void UMHGZAimComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindFromAbilitySystem();
	Super::EndPlay(EndPlayReason);
}

void UMHGZAimComponent::BindToAbilitySystem(UAbilitySystemComponent* InASC)
{
	// 幂等：同一 ASC 且已订阅则直接返回
	if (ASC == InASC && AimTagDelegateHandle.IsValid())
	{
		return;
	}

	// 切换 ASC：先解绑旧订阅并清 UI
	UnbindFromAbilitySystem();

	ASC = InASC;
	if (!ASC)
	{
		return;
	}

	AimTagDelegateHandle = ASC->RegisterGameplayTagEvent(
		AimingTag(), EGameplayTagEventType::NewOrRemoved)
		.AddUObject(this, &UMHGZAimComponent::OnAimingTagChanged);

	// 同步当前 tag count（Bind 可能发生在瞄准进行中）
	OnAimingTagChanged(AimingTag(), ASC->GetTagCount(AimingTag()));
}

void UMHGZAimComponent::UnbindFromAbilitySystem()
{
	if (ASC && AimTagDelegateHandle.IsValid())
	{
		ASC->UnregisterGameplayTagEvent(
			AimTagDelegateHandle, AimingTag(), EGameplayTagEventType::NewOrRemoved);
	}

	AimTagDelegateHandle.Reset();
	ASC = nullptr;
	bIsAiming = false;
	ClearTargetDisplay();
}

void UMHGZAimComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bIsAiming) return;

	// 受击/击倒：仅暂停射线并清空当前目标显示；不触碰任何 Tag（Tag 由持有时方移除）
	if (ASC && (ASC->HasMatchingGameplayTag(AimHitstunTag()) ||
		ASC->HasMatchingGameplayTag(AimKnockdownTag())))
	{
		ClearTargetDisplay();
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
		// 命中——仅怪物 Hitzone（ObjectType==ECC_GameTraceChannel3）视为有效目标；
		// 墙体等 Visibility Block 物体会先被遮挡，不会成为目标
		UMHGZMonsterHitzoneComponent* Hitzone = ResolveAimHitzone(Hit);
		if (Hitzone)
		{
			CurrentAimTarget = Hit.GetActor();
			CurrentAimHitzoneTag = Hitzone->HitzoneTag;

			// 颜色直接读部位 ExtractColorTag，不做 URes 映射
			CurrentAimExtractColor = Hitzone->ExtractColorTag;
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
		if (ResolveAimHitzone(Hit))
		{
			Snapshot.bHasHitResult = true;
			Snapshot.HitResult = Hit;
			Snapshot.TargetPoint = Hit.ImpactPoint;
		}
	}
	return Snapshot;
}

void UMHGZAimComponent::ClearTargetDisplay()
{
	// 无显示内容时静默，避免重复广播空目标
	if (!PreviousTarget.IsValid() && !PreviousHitzoneTag.IsValid() && !PreviousExtractColor.IsValid())
	{
		return;
	}

	CurrentAimTarget = nullptr;
	CurrentAimHitzoneTag = FGameplayTag();
	CurrentAimExtractColor = FGameplayTag();
	PreviousTarget = nullptr;
	PreviousHitzoneTag = FGameplayTag();
	PreviousExtractColor = FGameplayTag();

	OnAimTargetChanged.Broadcast(nullptr, FGameplayTag(), FGameplayTag());
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
