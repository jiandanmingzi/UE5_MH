// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZMonsterBase.h"
#include "MHGZMonsterHitzoneComponent.h"
#include "MHGZDummyConfig.h"
#include "ActionSystem/MHGZHitFeedbackRouterComponent.h"
#include "AttributeSystem/MHGZAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Components/CapsuleComponent.h"

AMHGZMonsterBase::AMHGZMonsterBase()
{
	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AttributeSet = CreateDefaultSubobject<UMHGZAttributeSet>(TEXT("AttributeSet"));
	AbilitySystemComponent->AddAttributeSetSubobject(AttributeSet.Get());

	// M2 parallel-domain dependency: this header is provided by the feedback writer.
	// The dummy routes AttributeSet -> Router through this component.
	HitFeedbackRouter = CreateDefaultSubobject<UMHGZHitFeedbackRouterComponent>(TEXT("HitFeedbackRouter"));

	// 实体 Body 只负责物理阻挡；Hitzone 独立承担武器、准心与肉质查询。
	GetCapsuleComponent()->SetCollisionProfileName(TEXT("MonsterBody"));
	GetMesh()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	GetMesh()->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Ignore);

	// 怪物不可站立/行走：玩家不能把怪物身体当地板踩。
	//
	// 这**不是碰撞预设能表达的** —— FWalkableSlopeOverride 是组件属性，
	// FCollisionProfileName 里没有它。而 UCharacterMovementComponent::IsWalkable
	// 正是通过它改写坡度阈值：
	//     TestWalkableZ = HitComponent->GetWalkableSlopeOverride()
	//                         .ModifyWalkableFloorZ(WalkableFloorZ);
	// WalkableSlope_Unwalkable 让任何角度都判为不可行走。
	//
	// 为什么必须有：木桩的碰撞面就是这条角色胶囊，而胶囊圆顶的法线 Z ≈ 0.72
	// 恰好高于 UE 的可行走坡度阈值（cos 44.765° ≈ 0.71），于是 PhysFalling 认定
	// 它能站、CMC 自己落地、HandleLanded 报 Landed；但胶囊在那么小的曲面上无法
	// 稳定接触，陷入每 ~0.5 s 一次的穿透极限环，MovementMode 一直是 Falling ——
	// 实测角色因此在木桩顶滞留 3.2 秒、全程无法操作，最后靠几何滑落才脱困。
	FWalkableSlopeOverride UnwalkableSlope;
	UnwalkableSlope.WalkableSlopeBehavior = WalkableSlope_Unwalkable;
	GetCapsuleComponent()->SetWalkableSlopeOverride(UnwalkableSlope);
}

UAbilitySystemComponent* AMHGZMonsterBase::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void AMHGZMonsterBase::BeginPlay()
{
	Super::BeginPlay();
	AbilitySystemComponent->InitAbilityActorInfo(this, this);
}

void AMHGZMonsterBase::ForceRestoreAllChannels()
{
	TArray<UMHGZMonsterHitzoneComponent*> Hitzones;
	GetComponents<UMHGZMonsterHitzoneComponent>(Hitzones);
	for (UMHGZMonsterHitzoneComponent* HZ : Hitzones)
	{
		HZ->ForceRestoreAllChannels();
	}
}

void AMHGZMonsterBase::GenerateHitzonesFromConfig(UMHGZDummyConfig* Config)
{
	if (!Config) return;

	// ApplyConfig 可重复调用；重建前先移除旧的动态/蓝图部位，避免重复伤害判定。
	TArray<UMHGZMonsterHitzoneComponent*> ExistingHitzones;
	GetComponents<UMHGZMonsterHitzoneComponent>(ExistingHitzones);
	for (UMHGZMonsterHitzoneComponent* Existing : ExistingHitzones)
	{
		if (Existing)
		{
			Existing->DestroyComponent();
		}
	}

	for (const FDummyHitzoneConfig& HZConfig : Config->Hitzones)
	{
		const FName ComponentName = MakeUniqueObjectName(
			this, UMHGZMonsterHitzoneComponent::StaticClass(),
			FName(*FString::Printf(TEXT("Hitzone_%s"), *HZConfig.BoneName.ToString())));
		UMHGZMonsterHitzoneComponent* Hitzone =
			NewObject<UMHGZMonsterHitzoneComponent>(this, ComponentName);
		Hitzone->BoneName = HZConfig.BoneName;
		Hitzone->HitzoneTag = HZConfig.HitzoneTag;
		Hitzone->ExtractColorTag = HZConfig.ExtractColorTag;
		Hitzone->DefenseMultiplier = HZConfig.DefenseMultiplier;
		Hitzone->StaggerRate = HZConfig.StaggerRate;

		// 静态训练柱使用 None，直接挂到角色 Root；骨骼怪物仍跟随指定骨骼。
		USceneComponent* AttachmentParent = HZConfig.BoneName.IsNone()
			? GetRootComponent() : GetMesh();
		if (!AttachmentParent)
		{
			Hitzone->DestroyComponent();
			continue;
		}
		Hitzone->AttachToComponent(
			AttachmentParent,
			FAttachmentTransformRules::KeepRelativeTransform,
			HZConfig.BoneName);
		AddInstanceComponent(Hitzone);
		Hitzone->RegisterComponent();
		Hitzone->SetRelativeLocation(HZConfig.RelativeLocation);
		Hitzone->Radius = FMath::Max(1.f, HZConfig.Radius);
		Hitzone->SetSphereRadius(Hitzone->Radius);
	}
}
