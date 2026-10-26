// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MHGZInputComponent.generated.h"

class APlayerController;
class UEnhancedInputComponent;
class UInputMappingContext;
class UMHGZWeaponInputRouterComponent;

/**
 * UMHGZInputComponent - input component (mounted on the local PlayerController).
 *
 * Sole owner of Enhanced Input mapping-context additions and raw-action binding
 * handles. Every IMC added and every BindAction/BindCharacterInput handle is
 * stored here and removed exactly once by ShutdownInput, so repeated
 * Setup/UnPossess/EndPlay never stack bindings.
 */
UCLASS(ClassGroup = (MHGZ))
class UMHGZInputComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMHGZInputComponent();

	/** 默认启用的输入映射上下文及其优先级配置。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TArray<UInputMappingContext*> DefaultIMCs;

	/**
	 * Idempotent initialization: adds the default IMCs (once per context) and
	 * (re)binds every raw weapon action plus the character locomotion actions.
	 * Repeated calls (repeat Setup / repeat Possess) do not stack; if already
	 * initialized with a different PC/router, the previous state is shut down first.
	 */
	void InitializeInput(APlayerController* InPC, UMHGZWeaponInputRouterComponent* InRouter);

	/** Re-binds the raw weapon actions from the router's current input profile (idempotent). */
	void RefreshRawBindings();

	/** Idempotent teardown: removes every stored binding handle and added IMC. */
	void ShutdownInput();

	/** 持有/释放运行时输入映射；重复持有计数，无持有权的释放不影响其它映射。 */
	void PushIMC(UInputMappingContext* IMC, int32 Priority);
	void PopIMC(UInputMappingContext* IMC);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UEnhancedInputComponent* GetEnhancedInputComponent() const;
	void AddDefaultContexts();
	void RemoveAllContexts();
	void UnbindAll();
	void OnRouterProfileChanged();

	/** 拥有该组件的玩家控制器缓存，重绑定时更新。 */
	UPROPERTY()
	TWeakObjectPtr<APlayerController> OwnerPC;

	/** 负责组合键解析与输入快照的武器输入路由器。 */
	UPROPERTY()
	TWeakObjectPtr<UMHGZWeaponInputRouterComponent> Router;

	/** 当前绑定句柄所属的原生增强输入组件弱引用；重初始化后旧绑定不再有效。 */
	UPROPERTY()
	TWeakObjectPtr<UEnhancedInputComponent> BoundInputComponent;

	/** Binding handles for raw weapon actions owned by this component. */
	TArray<uint32> RawBindingHandles;

	/** Binding handles appended by AMHGZCharacter::BindCharacterInput (Move/Look/Sprint). */
	TArray<uint32> CharacterBindingHandles;

	/** 本组件对各输入映射的持有次数；按引擎注册模式配对释放，关停时清空。 */
	TMap<TWeakObjectPtr<UInputMappingContext>, int32> AddedContextCounts;

	FDelegateHandle OnRouterProfileChangedHandle;
	bool bInputInitialized = false;
};
