// Copyright MHGZ Project. All Rights Reserved.

#include "Res_InsectGlaive.h"
#include "InsectGlaive/Kinsect/Kinsect.h"
#include "InsectGlaive/Kinsect/KinsectCollisionComponent.h"
#include "InsectGlaive/Kinsect/InsectGlaiveKinsectData.h"
#include "Monster/MHGZMonsterHitzoneComponent.h"
#include "ActionSystem/MHGZAbilitySystemComponent.h"
#include "MHGZPlayerState.h"
#include "AttributeSystem/MHGZAttributeSet.h"
#include "AbilitySystemGlobals.h"
#include "GameplayEffect.h"
#include "Camera/PlayerCameraManager.h"

URes_InsectGlaive::URes_InsectGlaive()
{
	PrimaryComponentTick.bCanEverTick = true;
	WeaponTypeTag = FGameplayTag::RequestGameplayTag(TEXT("Weapon.InsectGlaive"));
}

void URes_InsectGlaive::BeginPlay()
{
	Super::BeginPlay();
	if (!IsComponentTickEnabled())
	{
		SetComponentTickEnabled(true);
	}
}

void URes_InsectGlaive::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (KinsectActor == nullptr) return;

	// 猎虫耐力管理
	if (bKinsectDeployed)
	{
		float DrainRate = 0.f;
		switch (KinsectActor->GetState())
		{
		case EKinsectState::Flying:
			DrainRate = KinsectActor->GetFlightDrainRate() * FlightDrainRateMultiplier;
			break;
		case EKinsectState::Hovering:
			DrainRate = KinsectActor->GetHoverDrainRate() * HoverDrainRateMultiplier;
			break;
		case EKinsectState::Returning:
			DrainRate = KinsectActor->GetFlightDrainRate() * 0.5f * FlightDrainRateMultiplier;
			break;
		default:
			break;
		}

		KinsectStamina -= DrainRate * DeltaTime;
		if (KinsectStamina <= 0.f)
		{
			KinsectStamina = 0.f;
			PlayResourceSound(KinsectDepletedSound);
			bForceRecalling = true;
			KinsectActor->ForceRecall();
		}
	}
	else
	{
		// 回复耐力
		KinsectStamina += KinsectData->StaminaRegenRate * KinsectRegenRateMultiplier * DeltaTime;
		KinsectStamina = FMath::Min(KinsectStamina, MaxKinsectStamina);
	}

	// 广播耐力变化
	OnKinsectStaminaChanged.Broadcast(KinsectStamina, MaxKinsectStamina);
}

// ═══════════════════════════════════════════
// 猎虫生命周期
// ═══════════════════════════════════════════

void URes_InsectGlaive::OnWeaponEquipped(UInsectGlaiveKinsectData* Data, USceneComponent* ArmSocket)
{
	if (!Data) return;

	KinsectData = Data;

	// Spawn 猎虫
	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = GetOwner();
	KinsectActor = GetWorld()->SpawnActor<AKinsect>(AKinsect::StaticClass(), SpawnParams);

	if (KinsectActor!= nullptr)
	{
		KinsectActor->KinsectData = Data;
		KinsectActor->ResourceComponent = this;

		// 加载模型
		if (USkeletalMesh* Mesh = Data->KinsectMesh.LoadSynchronous())
		{
			KinsectActor->Mesh->SetSkeletalMesh(Mesh);
		}

		KinsectActor->AttachToPlayer(ArmSocket);

		// 初始化耐力
		MaxKinsectStamina = Data->StaminaPool;
		KinsectStamina = MaxKinsectStamina;
	}
}

void URes_InsectGlaive::OnWeaponUnequipped()
{
	if (KinsectActor!= nullptr)
	{
		KinsectActor->Destroy();
		KinsectActor = nullptr;
	}
	bKinsectDeployed = false;
	bTripleUpActive = false;

	// 清除所有萃取 GE
	if (UAbilitySystemComponent* ASC = GetPlayerASC())
	{
		for (auto& Pair : ActiveExtractHandles)
		{
			ASC->RemoveActiveGameplayEffect(Pair.Value);
		}
		ActiveExtractHandles.Empty();

		if (TripleUpHandle.IsValid())
		{
			ASC->RemoveActiveGameplayEffect(TripleUpHandle);
		}
	}
}

void URes_InsectGlaive::DeployKinsect()
{
	if (KinsectActor == nullptr) return;

	// 耐力归零强制召回时不可放虫打断
	if (bForceRecalling) return;

	// 互斥打断
	if (bKinsectDeployed)
		KinsectActor->Interrupt();

	KinsectActor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

	const EKinsectState CurrentState = KinsectActor->GetState();
	if (CurrentState == EKinsectState::Attached)
	{
		// 臂上放虫——沿准心方向
		FVector CameraLoc;
		FRotator CameraRot;
		if (APlayerController* PC = Cast<APlayerController>(
			Cast<APawn>(GetOwner())->GetController()))
		{
			PC->GetPlayerViewPoint(CameraLoc, CameraRot);
		}
		KinsectActor->StartFlightAlongRay(CameraRot.Vector(), KinsectData->MaxFlightRange);
	}
	else
	{
		// 悬停放虫——直线飞向准心命中点
		FVector CameraLoc;
		FRotator CameraRot;
		if (APlayerController* PC = Cast<APlayerController>(
			Cast<APawn>(GetOwner())->GetController()))
		{
			PC->GetPlayerViewPoint(CameraLoc, CameraRot);
		}
		const FVector CameraDir = CameraRot.Vector();
		const FVector TraceEnd = CameraLoc + CameraDir * KinsectData->MaxFlightRange;

		// 射线检测——找到准心实际命中的位置
		FVector TargetPoint = TraceEnd; // 默认：射程终点
		FHitResult Hit;
		FCollisionQueryParams QueryParams;
		QueryParams.AddIgnoredActor(GetOwner());
		if (GetWorld()->LineTraceSingleByChannel(Hit, CameraLoc, TraceEnd,
			ECC_GameTraceChannel1, QueryParams))
		{
			// 准心命中物体 → 猎虫飞向命中点
			TargetPoint = Hit.Location;
		}

		KinsectActor->StartFlightToPoint(TargetPoint);
	}

	KinsectActor->Collision->EnableKinsectCollision();

	if (UAbilitySystemComponent* ASC = GetPlayerASC())
	{
		ASC->AddLooseGameplayTag(
			FGameplayTag::RequestGameplayTag(TEXT("WeaponResource.IG.Kinsect.Active")));
	}

	bKinsectDeployed = true;
}

void URes_InsectGlaive::DeployKinsectAlongDirection(FVector Direction, float Distance)
{
	if (KinsectActor == nullptr) return;

	// 耐力归零强制召回时不可放虫打断
	if (bForceRecalling) return;

	if (bKinsectDeployed)
		KinsectActor->Interrupt();

	KinsectActor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	KinsectActor->StartFlightAlongRay(Direction.GetSafeNormal(), Distance);
	KinsectActor->Collision->EnableKinsectCollision();

	if (UAbilitySystemComponent* ASC = GetPlayerASC())
	{
		ASC->AddLooseGameplayTag(
			FGameplayTag::RequestGameplayTag(TEXT("WeaponResource.IG.Kinsect.Active")));
	}

	bKinsectDeployed = true;
}

void URes_InsectGlaive::RecallKinsect()
{
	if (KinsectActor == nullptr) return;
	if (KinsectActor->GetState() == EKinsectState::Returning) return;

	KinsectActor->StartReturn();
}

void URes_InsectGlaive::OnKinsectReachedPlayer(FGameplayTag ExtractColor)
{
	// 先吸附
	if (KinsectActor!= nullptr)
	{
		// AttachToPlayer 由 Kinsect::Tick 的到达检测外调处理
	}

	// Apply 萃取
	if (ExtractColor.IsValid())
	{
		ApplyExtract(ExtractColor);
	}

	// 移除 Kinsect.Active Tag
	if (UAbilitySystemComponent* ASC = GetPlayerASC())
	{
		ASC->RemoveLooseGameplayTag(
			FGameplayTag::RequestGameplayTag(TEXT("WeaponResource.IG.Kinsect.Active")));
	}

	bForceRecalling = false;
	bKinsectDeployed = false;
}

// ═══════════════════════════════════════════
// 萃取系统
// ═══════════════════════════════════════════

FGameplayTag URes_InsectGlaive::StaticMapHitzoneToExtract(FGameplayTag HitzoneTag)
{
	// 默认部位→颜色映射（M-10 修复：单一静态函数，AimComponent 共用）
	if (HitzoneTag.MatchesTag(FGameplayTag::RequestGameplayTag(TEXT("Hitzone.Head"))))
		return FGameplayTag::RequestGameplayTag(TEXT("WeaponResource.IG.Extract.Red"));
	if (HitzoneTag.MatchesTag(FGameplayTag::RequestGameplayTag(TEXT("Hitzone.TailTip"))))
		return FGameplayTag::RequestGameplayTag(TEXT("WeaponResource.IG.Extract.Red"));
	if (HitzoneTag.MatchesTag(FGameplayTag::RequestGameplayTag(TEXT("Hitzone.Torso"))))
		return FGameplayTag::RequestGameplayTag(TEXT("WeaponResource.IG.Extract.Yellow"));
	if (HitzoneTag.MatchesTagExact(FGameplayTag::RequestGameplayTag(TEXT("Hitzone.LeftWing"))) ||
		HitzoneTag.MatchesTagExact(FGameplayTag::RequestGameplayTag(TEXT("Hitzone.RightWing"))))
		return FGameplayTag::RequestGameplayTag(TEXT("WeaponResource.IG.Extract.Yellow"));
	if (HitzoneTag.MatchesTag(FGameplayTag::RequestGameplayTag(TEXT("Hitzone.Back"))))
		return FGameplayTag::RequestGameplayTag(TEXT("WeaponResource.IG.Extract.Yellow"));
	if (HitzoneTag.MatchesTag(FGameplayTag::RequestGameplayTag(TEXT("Hitzone.Neck"))))
		return FGameplayTag::RequestGameplayTag(TEXT("WeaponResource.IG.Extract.Yellow"));

	return FGameplayTag::RequestGameplayTag(TEXT("WeaponResource.IG.Extract.White"));
}

FGameplayTag URes_InsectGlaive::MapHitzoneToExtract(FGameplayTag HitzoneTag) const
{
	return StaticMapHitzoneToExtract(HitzoneTag);
}

void URes_InsectGlaive::ApplyExtract(FGameplayTag ExtractColor)
{
	if (!ExtractColor.IsValid()) return;

	UAbilitySystemComponent* ASC = GetPlayerASC();
	if (!ASC) return;

	// ★ H-8 修复：同色灯已存在 → 移除旧 GE
	if (FActiveGameplayEffectHandle* OldHandle = ActiveExtractHandles.Find(ExtractColor))
	{
		if (OldHandle->IsValid())
		{
			ASC->RemoveActiveGameplayEffect(*OldHandle);
		}
	}

	// 选择对应 GE 类
	TSubclassOf<UGameplayEffect> GEClass = nullptr;
	float Duration = 0.f;

	if (ExtractColor.MatchesTag(
		FGameplayTag::RequestGameplayTag(TEXT("WeaponResource.IG.Extract.White"))))
	{
		GEClass = LoadClass<UGameplayEffect>(nullptr,
			TEXT("/Game/GameplayEffects/InsectGlaive/GE_IG_WhiteExtract.GE_IG_WhiteExtract_C"));
		Duration = WHITE_DURATION;
	}
	else if (ExtractColor.MatchesTag(
		FGameplayTag::RequestGameplayTag(TEXT("WeaponResource.IG.Extract.Yellow"))))
	{
		GEClass = LoadClass<UGameplayEffect>(nullptr,
			TEXT("/Game/GameplayEffects/InsectGlaive/GE_IG_YellowExtract.GE_IG_YellowExtract_C"));
		Duration = YELLOW_DURATION;
	}
	else if (ExtractColor.MatchesTag(
		FGameplayTag::RequestGameplayTag(TEXT("WeaponResource.IG.Extract.Red"))))
	{
		GEClass = LoadClass<UGameplayEffect>(nullptr,
			TEXT("/Game/GameplayEffects/InsectGlaive/GE_IG_RedExtract.GE_IG_RedExtract_C"));
		Duration = RED_DURATION;
	}

	if (!GEClass) return;

	// Apply GE
	FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(GEClass, 1.0f, ASC->MakeEffectContext());
	if (Spec.IsValid())
	{
		Spec.Data->SetDuration(Duration, true);
		FActiveGameplayEffectHandle Handle = ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
		ActiveExtractHandles.Add(ExtractColor, Handle);
	}

	PlayResourceSound(ExtractCollectedSound);

	// 检查三灯
	CheckAndActivateTripleUp();
}

void URes_InsectGlaive::CheckAndActivateTripleUp()
{
	if (bTripleUpActive) return; // 不可刷新

	UAbilitySystemComponent* ASC = GetPlayerASC();
	if (!ASC) return;

	// 检查是否同时持有三种灯
	static const FGameplayTag WhiteTag = FGameplayTag::RequestGameplayTag(TEXT("WeaponResource.IG.Extract.White"));
	static const FGameplayTag YellowTag = FGameplayTag::RequestGameplayTag(TEXT("WeaponResource.IG.Extract.Yellow"));
	static const FGameplayTag RedTag = FGameplayTag::RequestGameplayTag(TEXT("WeaponResource.IG.Extract.Red"));

	if (!ASC->HasMatchingGameplayTag(WhiteTag) ||
		!ASC->HasMatchingGameplayTag(YellowTag) ||
		!ASC->HasMatchingGameplayTag(RedTag))
	{
		return;
	}

	// 移除三个单灯 GE
	for (auto& Pair : ActiveExtractHandles)
	{
		if (Pair.Value.IsValid())
		{
			ASC->RemoveActiveGameplayEffect(Pair.Value);
		}
	}
	ActiveExtractHandles.Empty();

	// Apply 三灯 GE
	TSubclassOf<UGameplayEffect> TripleUpGEClass = LoadClass<UGameplayEffect>(nullptr,
		TEXT("/Game/GameplayEffects/InsectGlaive/GE_IG_TripleUp.GE_IG_TripleUp_C"));

	if (TripleUpGEClass)
	{
		FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(TripleUpGEClass, 1.0f, ASC->MakeEffectContext());
		if (Spec.IsValid())
		{
			Spec.Data->SetDuration(TripleUpDuration, true);
			TripleUpHandle = ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
		}
	}

	bTripleUpActive = true;
	PlayResourceSound(TripleUpActivatedSound);
	OnTripleUpChanged.Broadcast();
}

void URes_InsectGlaive::ConsumeExtract(FGameplayTag ExtractType)
{
	UAbilitySystemComponent* ASC = GetPlayerASC();
	if (!ASC) return;

	const bool bWasTripleUp = bTripleUpActive;

	if (bWasTripleUp)
	{
		// 解除三灯
		if (TripleUpHandle.IsValid())
		{
			ASC->RemoveActiveGameplayEffect(TripleUpHandle);
		}
		bTripleUpActive = false;

		// 重新 Apply 剩余灯（白+黄）
		static const FGameplayTag WhiteTag = FGameplayTag::RequestGameplayTag(TEXT("WeaponResource.IG.Extract.White"));
		static const FGameplayTag YellowTag = FGameplayTag::RequestGameplayTag(TEXT("WeaponResource.IG.Extract.Yellow"));
		static const FGameplayTag RedTag = FGameplayTag::RequestGameplayTag(TEXT("WeaponResource.IG.Extract.Red"));

		TArray<FGameplayTag> Remaining;
		if (ExtractType != WhiteTag) Remaining.Add(WhiteTag);
		if (ExtractType != YellowTag) Remaining.Add(YellowTag);
		if (ExtractType != RedTag) Remaining.Add(RedTag);
		ReapplyRemainingExtracts(Remaining);
	}
	else
	{
		// 移除指定单灯
		if (FActiveGameplayEffectHandle* Handle = ActiveExtractHandles.Find(ExtractType))
		{
			if (Handle->IsValid())
			{
				ASC->RemoveActiveGameplayEffect(*Handle);
			}
			ActiveExtractHandles.Remove(ExtractType);
		}
	}
}

void URes_InsectGlaive::ConsumeTripleUp()
{
	if (!bTripleUpActive) return;

	UAbilitySystemComponent* ASC = GetPlayerASC();
	if (!ASC) return;

	if (TripleUpHandle.IsValid())
	{
		ASC->RemoveActiveGameplayEffect(TripleUpHandle);
	}
	bTripleUpActive = false;
	// 三灯全清，不 Reapply 单灯
}

void URes_InsectGlaive::ReapplyRemainingExtracts(const TArray<FGameplayTag>& RemainingColors)
{
	for (const FGameplayTag& Color : RemainingColors)
	{
		ApplyExtract(Color);
	}
}

bool URes_InsectGlaive::HasExtract(FGameplayTag ExtractType) const
{
	if (UAbilitySystemComponent* ASC = GetPlayerASC())
	{
		return ASC->HasMatchingGameplayTag(ExtractType);
	}
	return false;
}

// ═══════════════════════════════════════════
// 猎虫伤害
// ═══════════════════════════════════════════

void URes_InsectGlaive::ApplyKinsectDamage(
	UMHGZMonsterHitzoneComponent* Hitzone, AActor* Monster, float MotionValue)
{
	if (!Hitzone || !Monster) return;

	UAbilitySystemComponent* PlayerASC = GetPlayerASC();
	if (!PlayerASC) return;

	UAbilitySystemComponent* MonsterASC = Monster->FindComponentByClass<UAbilitySystemComponent>();
	if (!MonsterASC) return;

	// 加载 GE_KinsectDamage
	UGameplayEffect* GEClass = LoadObject<UGameplayEffect>(nullptr,
		TEXT("/Game/GameplayEffects/Core/GE_KinsectDamage.GE_KinsectDamage"));

	FGameplayEffectSpecHandle Spec = PlayerASC->MakeOutgoingSpec(
		GEClass ? GEClass->GetClass() : nullptr, 1.0f, PlayerASC->MakeEffectContext());

	if (!Spec.IsValid()) return;

	// 统一使用 Damage.MotionValue
	Spec.Data->SetSetByCallerMagnitude(
		FGameplayTag::RequestGameplayTag(TEXT("Damage.MotionValue")), MotionValue);

	// 猎虫攻击力覆写
	Spec.Data->SetSetByCallerMagnitude(
		FGameplayTag::RequestGameplayTag(TEXT("Damage.AttackPower")),
		GetModifiedKinsectAttackPower());

	// 部位信息
	Spec.Data->AddDynamicAssetTag(Hitzone->HitzoneTag);

	// GameplayCue——猎虫命中
	Spec.Data->AddDynamicAssetTag(
		FGameplayTag::RequestGameplayTag(TEXT("GameplayCue.Hit.Kinsect")));
	Spec.Data->AddDynamicAssetTag(
		FGameplayTag::RequestGameplayTag(TEXT("GameplayCue.Hit.DamageNumber")));

	PlayerASC->ApplyGameplayEffectSpecToTarget(*Spec.Data, MonsterASC);
}

float URes_InsectGlaive::GetModifiedKinsectAttackPower() const
{
	if (KinsectData)
	{
		return KinsectData->KinsectAttackPower;
	}
	return 10.0f;
}

// ═══════════════════════════════════════════
// 词条修饰器
// ═══════════════════════════════════════════

void URes_InsectGlaive::ApplyEntryModifier(FGameplayTag AttributeTag, float Value, TEnumAsByte<EGameplayModOp::Type> Op)
{
	if (!AttributeTag.MatchesTag(FGameplayTag::RequestGameplayTag(TEXT("WeaponResource"))))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Res_InsectGlaive] ApplyEntryModifier: Tag %s 不在 WeaponResource 命名空间下，跳过"),
			*AttributeTag.ToString());
		return;
	}

	// 按 Tag 路由到内部倍率
	static const FGameplayTag RegenTag = FGameplayTag::RequestGameplayTag(TEXT("WeaponResource.IG.KinsectRegenRate"));
	static const FGameplayTag HoverDrainTag = FGameplayTag::RequestGameplayTag(TEXT("WeaponResource.IG.HoverDrainRate"));
	static const FGameplayTag FlightDrainTag = FGameplayTag::RequestGameplayTag(TEXT("WeaponResource.IG.FlightDrainRate"));

	if (AttributeTag == RegenTag)
		KinsectRegenRateMultiplier *= Value;
	else if (AttributeTag == HoverDrainTag)
		HoverDrainRateMultiplier *= Value;
	else if (AttributeTag == FlightDrainTag)
		FlightDrainRateMultiplier *= Value;

	Super::ApplyEntryModifier(AttributeTag, Value, Op);
}

void URes_InsectGlaive::ClearAllEntryModifiers()
{
	KinsectRegenRateMultiplier = 1.0f;
	HoverDrainRateMultiplier = 1.0f;
	FlightDrainRateMultiplier = 1.0f;
	Super::ClearAllEntryModifiers();
}
