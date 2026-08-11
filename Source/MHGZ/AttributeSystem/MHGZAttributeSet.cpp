// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZAttributeSet.h"
#include "ActionSystem/MHGZGameplayEffectContext.h"
#include "ActionSystem/MHGZHitFeedbackRouterComponent.h"
#include "ActionSystem/MHGZHitFeedbackTypes.h"
#include "GameplayEffectExtension.h"
#include "Net/UnrealNetwork.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"

UMHGZAttributeSet::UMHGZAttributeSet()
{
	// 基础值
	InitHealth(100.f);
	InitMaxHealth(100.f);
	InitStamina(100.f);
	InitMaxStamina(100.f);
	InitStaminaRegenRate(1.0f);
	InitStaminaDeductionRate(1.0f);
	InitStaminaConsumptionRate(1.0f);
	InitAttackPower(0.f);
	InitDefense(0.f);
	InitCriticalRate(0.f);
	InitStaggerMultiplier(1.0f);
	InitMoveSpeedMultiplier(1.0f);

	// 命中结算 Meta（非复制；只由 ExecCalc 写入）
	InitIncomingDamage(0.f);
	InitIncomingStagger(0.f);
	InitIncomingCriticalFlag(0.f);
	InitIncomingHitSignal(0.f);
}

void UMHGZAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION_NOTIFY(UMHGZAttributeSet, Health, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMHGZAttributeSet, MaxHealth, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMHGZAttributeSet, Stamina, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMHGZAttributeSet, MaxStamina, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMHGZAttributeSet, StaminaRegenRate, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMHGZAttributeSet, StaminaDeductionRate, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMHGZAttributeSet, StaminaConsumptionRate, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMHGZAttributeSet, AttackPower, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMHGZAttributeSet, Defense, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMHGZAttributeSet, CriticalRate, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMHGZAttributeSet, StaggerMultiplier, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMHGZAttributeSet, MoveSpeedMultiplier, COND_None, REPNOTIFY_Always);
}

void UMHGZAttributeSet::ClampAttribute(const FGameplayAttribute& Attribute, float& NewValue) const
{
	if (Attribute == GetHealthAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.f, GetMaxHealth());
	}
	else if (Attribute == GetMaxHealthAttribute())
	{
		// 木桩默认 1000；不能把上限写死为 200。
		NewValue = FMath::Max(NewValue, 1.f);
	}
	else if (Attribute == GetStaminaAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.f, GetMaxStamina());
	}
	else if (Attribute == GetMaxStaminaAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 1.f, 200.f);
	}
	else if (Attribute == GetCriticalRateAttribute())
	{
		NewValue = FMath::Clamp(NewValue, -100.f, 100.f);
	}
	else if (Attribute == GetAttackPowerAttribute() || Attribute == GetDefenseAttribute())
	{
		NewValue = FMath::Max(NewValue, 0.f);
	}
	else if (Attribute == GetStaggerMultiplierAttribute())
	{
		NewValue = FMath::Max(NewValue, 0.f);
	}
	else if (Attribute == GetStaminaRegenRateAttribute() ||
		Attribute == GetStaminaDeductionRateAttribute() ||
		Attribute == GetStaminaConsumptionRateAttribute())
	{
		NewValue = FMath::Max(NewValue, 0.f);
	}
	else if (Attribute == GetMoveSpeedMultiplierAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.1f, 3.0f);
	}
}

void UMHGZAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);
	ClampAttribute(Attribute, NewValue);
}

void UMHGZAttributeSet::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)
{
	Super::PostGameplayEffectExecute(Data);

	// 结算只由最后一个 IncomingHitSignal 驱动：一次 GE 恰好产生一次反馈。
	// UE5.6 对 OutputModifiers 逐项调用本函数，HitSignal 是 ExecCalc 的最后输出。
	if (Data.EvaluatedData.Attribute != GetIncomingHitSignalAttribute())
	{
		return;
	}

	UAbilitySystemComponent* TargetASC = &Data.Target;
	if (!TargetASC)
	{
		return;
	}

	// ── 原子读出并清零四个 Meta（HitSignal 最后执行，清零不会被后续 Modifier 覆盖） ──
	const float IncomingDamageValue = GetIncomingDamage();
	const float IncomingStaggerValue = GetIncomingStagger();
	const bool bCritical = GetIncomingCriticalFlag() > 0.5f;

	IncomingDamage.SetBaseValue(0.f);
	IncomingDamage.SetCurrentValue(0.f);
	IncomingStagger.SetBaseValue(0.f);
	IncomingStagger.SetCurrentValue(0.f);
	IncomingCriticalFlag.SetBaseValue(0.f);
	IncomingCriticalFlag.SetCurrentValue(0.f);
	IncomingHitSignal.SetBaseValue(0.f);
	IncomingHitSignal.SetCurrentValue(0.f);

	// ── 按当前 Health Clamp 得到 ActualDamage 并扣血 ──
	const float OldHealth = GetHealth();
	const float ActualDamage = FMath::Clamp(IncomingDamageValue, 0.f, OldHealth);
	if (ActualDamage > 0.f)
	{
		const float NewHealth = FMath::Clamp(OldHealth - ActualDamage, 0.f, GetMaxHealth());
		SetHealth(NewHealth);
	}

	const FMHGZGameplayEffectContext* CombatContext =
		FMHGZGameplayEffectContext::ExtractEffectContext(Data.EffectSpec.GetContext());

	// 真实 Hit 是结算目标的权威来源，也覆盖测试/预览中无完整 ActorInfo 的 ASC。
	AActor* PhysicalTarget = nullptr;
	if (CombatContext)
	{
		if (const FHitResult* Hit = CombatContext->GetHitResult())
		{
			if (UPrimitiveComponent* HitComponent = Hit->GetComponent())
			{
				PhysicalTarget = HitComponent->GetOwner();
			}
		}
	}
	if (!PhysicalTarget)
	{
		PhysicalTarget = TargetASC->GetAvatarActor_Direct();
	}
	if (!PhysicalTarget)
	{
		PhysicalTarget = TargetASC->GetOwnerActor();
	}

	// ── 构造一次 FMHGZHitFeedbackResult ──
	FMHGZHitFeedbackResult Result;
	Result.ActualDamage = ActualDamage;
	Result.ActualStagger = IncomingStaggerValue;
	Result.bCritical = bCritical;
	Result.TargetActor = PhysicalTarget;
	Result.ContextHandle = Data.EffectSpec.GetContext();

	if (CombatContext)
	{
		if (const FHitResult* Hit = CombatContext->GetHitResult())
		{
			Result.Hit = *Hit;
		}
		Result.HitCueTag = CombatContext->HitCueTag;
		Result.ElementCueTag = CombatContext->ElementCueTag;
		Result.HitStaggerTag = CombatContext->HitStaggerTag;
		Result.AttackInstanceID = CombatContext->AttackInstanceID;
		Result.SourceActor = CombatContext->GetInstigator();
		Result.HitStopDuration = CombatContext->HitStopDuration;
		Result.CameraShakeClass = CombatContext->CameraShakeClass;
		Result.CameraShakeScale = CombatContext->CameraShakeScale;
	}

	// ── 先 Router，再发硬直事件 ──
	if (PhysicalTarget)
	{
		if (UMHGZHitFeedbackRouterComponent* Router =
			PhysicalTarget->FindComponentByClass<UMHGZHitFeedbackRouterComponent>())
		{
			Router->RouteHitFeedback(Result);
		}
	}

	// 仅当命中携带有效硬直等级且硬直值 > 0 时广播 HitStagger 事件。
	if (Result.HitStaggerTag.IsValid() && Result.ActualStagger > 0.f)
	{
		FGameplayEventData EventData;
		EventData.Instigator = Result.SourceActor.Get();
		EventData.Target = PhysicalTarget;
		EventData.EventTag = FGameplayTag::RequestGameplayTag(TEXT("Combat.Event.HitStagger"));
		EventData.ContextHandle = Result.ContextHandle;
		TargetASC->HandleGameplayEvent(EventData.EventTag, &EventData);
	}

	// 死亡是 Runtime 的硬边界：清 Ability、资源、命中停顿和所有旧 Token。
	if (PhysicalTarget && OldHealth > 0.f && GetHealth() <= 0.f)
	{
		if (UMHGZWeaponRuntimeHostComponent* RuntimeHost =
			PhysicalTarget->FindComponentByClass<UMHGZWeaponRuntimeHostComponent>())
		{
			RuntimeHost->ShutdownRuntime(EWeaponRuntimeEndReason::Death);
		}
	}
}

// ═══════════════════════════════════════════
// OnRep 回调（当前单机，预留网络接口）
// ═══════════════════════════════════════════
void UMHGZAttributeSet::OnRep_Health(const FGameplayAttributeData& OldValue) { GAMEPLAYATTRIBUTE_REPNOTIFY(UMHGZAttributeSet, Health, OldValue); }
void UMHGZAttributeSet::OnRep_MaxHealth(const FGameplayAttributeData& OldValue) { GAMEPLAYATTRIBUTE_REPNOTIFY(UMHGZAttributeSet, MaxHealth, OldValue); }
void UMHGZAttributeSet::OnRep_Stamina(const FGameplayAttributeData& OldValue) { GAMEPLAYATTRIBUTE_REPNOTIFY(UMHGZAttributeSet, Stamina, OldValue); }
void UMHGZAttributeSet::OnRep_MaxStamina(const FGameplayAttributeData& OldValue) { GAMEPLAYATTRIBUTE_REPNOTIFY(UMHGZAttributeSet, MaxStamina, OldValue); }
void UMHGZAttributeSet::OnRep_StaminaRegenRate(const FGameplayAttributeData& OldValue) { GAMEPLAYATTRIBUTE_REPNOTIFY(UMHGZAttributeSet, StaminaRegenRate, OldValue); }
void UMHGZAttributeSet::OnRep_StaminaDeductionRate(const FGameplayAttributeData& OldValue) { GAMEPLAYATTRIBUTE_REPNOTIFY(UMHGZAttributeSet, StaminaDeductionRate, OldValue); }
void UMHGZAttributeSet::OnRep_StaminaConsumptionRate(const FGameplayAttributeData& OldValue) { GAMEPLAYATTRIBUTE_REPNOTIFY(UMHGZAttributeSet, StaminaConsumptionRate, OldValue); }
void UMHGZAttributeSet::OnRep_AttackPower(const FGameplayAttributeData& OldValue) { GAMEPLAYATTRIBUTE_REPNOTIFY(UMHGZAttributeSet, AttackPower, OldValue); }
void UMHGZAttributeSet::OnRep_Defense(const FGameplayAttributeData& OldValue) { GAMEPLAYATTRIBUTE_REPNOTIFY(UMHGZAttributeSet, Defense, OldValue); }
void UMHGZAttributeSet::OnRep_CriticalRate(const FGameplayAttributeData& OldValue) { GAMEPLAYATTRIBUTE_REPNOTIFY(UMHGZAttributeSet, CriticalRate, OldValue); }
void UMHGZAttributeSet::OnRep_StaggerMultiplier(const FGameplayAttributeData& OldValue) { GAMEPLAYATTRIBUTE_REPNOTIFY(UMHGZAttributeSet, StaggerMultiplier, OldValue); }
void UMHGZAttributeSet::OnRep_MoveSpeedMultiplier(const FGameplayAttributeData& OldValue) { GAMEPLAYATTRIBUTE_REPNOTIFY(UMHGZAttributeSet, MoveSpeedMultiplier, OldValue); }
