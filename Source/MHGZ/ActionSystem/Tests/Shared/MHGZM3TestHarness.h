// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "Abilities/GameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "ActionSystem/MHGZAbilitySystemComponent.h"
#include "ActionSystem/MHGZInsectGlaiveKinsectAbilities.h"
#include "ActionSystem/MHGZWeaponComboData.h"
#include "AttributeSystem/Res_InsectGlaive.h"
#include "Engine/World.h"
#include "Equipment/MHGZEquipmentComponent.h"
#include "Equipment/MHGZEquipmentDefinition.h"
#include "Equipment/MHGZEquipmentInstance.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InsectGlaive/InsectGlaiveCombatConfig.h"
#include "InsectGlaive/Kinsect/InsectGlaiveKinsectData.h"
#include "InsectGlaive/Kinsect/Kinsect.h"
#include "MHGZPlayerState.h"
#include "MHGZM3TestTypes.h"
#include "WeaponRuntime/MHGZWeaponRuntimeDefinition.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"
#include "WeaponRuntime/MHGZWeaponRuntimeTypes.h"

namespace M3
{
inline FGameplayTag Tag(const TCHAR* Name)
{
	return FGameplayTag::RequestGameplayTag(Name);
}

/** 构造带姿态上下文标签的输入快照。 */
inline FWeaponInputSnapshot MakePosedInput(bool bGrounded, bool bSheathed)
{
	FWeaponInputSnapshot Input;
	Input.ContextTags.AddTag(Tag(bGrounded ? TEXT("Combat.State.Grounded")
		: TEXT("Combat.State.Aerial")));
	Input.ContextTags.AddTag(Tag(bSheathed ? TEXT("Combat.State.Sheathed")
		: TEXT("Combat.State.Unsheathed")));
	return Input;
}
}

/**
 * M3 虫棍资源/猎虫测试 harness。
 * 完整复刻 M2 Equipment 测试链：PlayerState ASC → Equipment → RuntimeHost →
 * URes_InsectGlaive（Host 自建）→ AKinsect（Resource 自建）；
 * CombatConfig/KinsectData 全部为 transient NewObject，不依赖任何 E4 资产。
 */
struct FMHGZM3Harness
{
	UWorld* World = nullptr;
	ACharacter* Character = nullptr;
	AMHGZPlayerState* PlayerState = nullptr;
	UMHGZAbilitySystemComponent* ASC = nullptr;
	UMHGZEquipmentComponent* Equipment = nullptr;
	UMHGZWeaponRuntimeHostComponent* Host = nullptr;
	URes_InsectGlaive* Resource = nullptr;
	AKinsect* Kinsect = nullptr;
	UInsectGlaiveCombatConfig* CombatConfig = nullptr;
	UInsectGlaiveKinsectData* KinsectData = nullptr;
	FGameplayTag WeaponSlot;

	/**
	 * 建立完整运行时。bBlockTripleUpGE 为 true 时注入必然 Apply 失败的三灯 GE，
	 * 用于验证"Triple GE Apply 失败保留三单灯"。
	 */
	bool Setup(bool bBlockTripleUpGE = false);

	void Teardown();

	/** 授予 M3 猎虫能力（独立于 ComboData 的直接 GiveAbility）。 */
	FGameplayAbilitySpecHandle GiveAbility(TSubclassOf<UGameplayAbility> AbilityClass);

	/** 以当前 Runtime Token + 自定义 Input 快照构造一次性激活上下文并激活。 */
	bool TryActivateWithInput(const FGameplayAbilitySpecHandle& Handle,
		const FWeaponInputSnapshot& Input);

	/** 构造当前 Runtime 下的有效 FWeaponActionToken（用于资源预留）。 */
	FWeaponActionToken MakeActionToken(TSubclassOf<UGameplayAbility> AbilityClass);
};

inline bool FMHGZM3Harness::Setup(bool bBlockTripleUpGE)
{
	UAbilitySystemGlobals& Globals = UAbilitySystemGlobals::Get();
	if (!Globals.IsAbilitySystemGlobalsInitialized())
	{
		Globals.InitGlobalData();
	}

	World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!World)
	{
		return false;
	}

	Character = World->SpawnActor<ACharacter>(
		ACharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
	if (!Character)
	{
		return false;
	}

	PlayerState = World->SpawnActor<AMHGZPlayerState>(
		AMHGZPlayerState::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
	if (!PlayerState)
	{
		return false;
	}

	ASC = PlayerState->GetMHGZAbilitySystemComponent();
	if (!ASC)
	{
		return false;
	}
	ASC->InitAbilityActorInfo(PlayerState, Character);

	Equipment = PlayerState->GetEquipmentComponent();
	if (!Equipment)
	{
		return false;
	}

	Host = NewObject<UMHGZWeaponRuntimeHostComponent>(Character);
	Character->AddInstanceComponent(Host);
	Host->RegisterComponent();
	Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);

	WeaponSlot = M3::Tag(TEXT("Equipment.Slot.Weapon"));
	if (!WeaponSlot.IsValid())
	{
		return false;
	}

	// 完整武器链：WeaponDefinition -> RuntimeDefinition -> CombatConfig -> ComboData。
	UMHGZWeaponDefinition* Definition = NewObject<UMHGZWeaponDefinition>(PlayerState);
	Definition->ItemID = FName(*FString::Printf(TEXT("M3TestWeapon_%p"), (void*)Definition));
	UWeaponRuntimeDefinition* RuntimeDefinition =
		NewObject<UWeaponRuntimeDefinition>(PlayerState);
	RuntimeDefinition->ResourceComponentClass = URes_InsectGlaive::StaticClass();
	CombatConfig = NewObject<UInsectGlaiveCombatConfig>(PlayerState);
	UMHGZWeaponComboData* ComboData = NewObject<UMHGZWeaponComboData>(PlayerState);
	CombatConfig->ComboData = ComboData;
	RuntimeDefinition->CombatConfig = CombatConfig;
	Definition->RuntimeDefinition = RuntimeDefinition;

	KinsectData = NewObject<UInsectGlaiveKinsectData>(PlayerState);
	KinsectData->FlightSpeed = 2000.f;
	KinsectData->ReturnSpeed = 2500.f;
	KinsectData->MaxFlightRange = 3000.f;
	KinsectData->StraightFlightDistance = 1500.f;
	KinsectData->StaminaPool = 100.f;
	KinsectData->StaminaRegenRate = 15.f;
	KinsectData->HoverDrainRate = 3.f;
	KinsectData->FlightDrainRate = 8.f;
	KinsectData->KinsectAttackPower = 10.f;
	CombatConfig->KinsectData = KinsectData;
	// 测试骨架的 ACharacter Mesh 没有 Kinsect_Arm_Socket；Resource 允许 None
	// 回退到组件根（正式资产 DataValidation 禁止 None，与运行时无关）。
	CombatConfig->KinsectAttachSocket = NAME_None;
	CombatConfig->KinsectMarkLaunchSocket = NAME_None;

	CombatConfig->WhiteEffectClass = UMHGZM3WhiteExtractGE::StaticClass();
	CombatConfig->OrangeEffectClass = UMHGZM3OrangeExtractGE::StaticClass();
	CombatConfig->RedEffectClass = UMHGZM3RedExtractGE::StaticClass();
	CombatConfig->TripleUpEffectClass = bBlockTripleUpGE
		? UMHGZM3BlockedTripleUpGE::StaticClass()
		: UMHGZM3TripleUpGE::StaticClass();

	Host->InitializePawnRuntime(Character, nullptr, ASC, Equipment);
	UMHGZEquipmentInstance* Instance =
		UMHGZEquipmentInstance::CreateEquipmentInstance(PlayerState, Definition);
	Equipment->EquipItem(WeaponSlot, Instance);

	Resource = Character->FindComponentByClass<URes_InsectGlaive>();
	if (!Resource)
	{
		return false;
	}
	Kinsect = Resource->GetKinsectActor();
	if (!Kinsect)
	{
		return false;
	}
	return true;
}

inline void FMHGZM3Harness::Teardown()
{
	if (Host)
	{
		Host->ShutdownRuntime(EWeaponRuntimeEndReason::RuntimeShutdown);
	}
	if (World)
	{
		World->DestroyWorld(false);
	}
	World = nullptr;
	Character = nullptr;
	PlayerState = nullptr;
	ASC = nullptr;
	Equipment = nullptr;
	Host = nullptr;
	Resource = nullptr;
	Kinsect = nullptr;
	CombatConfig = nullptr;
	KinsectData = nullptr;
}

inline FGameplayAbilitySpecHandle FMHGZM3Harness::GiveAbility(
	TSubclassOf<UGameplayAbility> AbilityClass)
{
	return ASC && AbilityClass
		? ASC->GiveAbility(FGameplayAbilitySpec(AbilityClass, 1, INDEX_NONE, ASC))
		: FGameplayAbilitySpecHandle();
}

inline bool FMHGZM3Harness::TryActivateWithInput(
	const FGameplayAbilitySpecHandle& Handle, const FWeaponInputSnapshot& Input)
{
	if (!Handle.IsValid() || !Host || !ASC)
	{
		return false;
	}
	FWeaponAbilityActivationContext Context;
	Context.RuntimeToken = Host->GetCurrentToken();
	Context.ActivationSequenceID = Host->AllocateActivationSequenceID();
	Context.Input = Input;
	ASC->PrepareWeaponAbilityActivation(Handle, Context);
	return ASC->TryActivateAbility(Handle);
}

inline FWeaponActionToken FMHGZM3Harness::MakeActionToken(
	TSubclassOf<UGameplayAbility> AbilityClass)
{
	FWeaponActionToken Token;
	Token.RuntimeToken = Host ? Host->GetCurrentToken() : FWeaponRuntimeToken();
	Token.AbilityHandle = GiveAbility(AbilityClass);
	Token.ActivationSequenceID = Host ? Host->AllocateActivationSequenceID() : 0;
	Token.AbilityInstance = ASC && AbilityClass
		? NewObject<UGameplayAbility>(ASC, AbilityClass) : nullptr;
	return Token;
}
