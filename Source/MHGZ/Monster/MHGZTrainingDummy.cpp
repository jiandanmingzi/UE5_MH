// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZTrainingDummy.h"
#include "MHGZ.h"
#include "MHGZDummyConfig.h"
#include "AttributeSystem/MHGZAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
constexpr float DummyDiameter = 100.0f;
constexpr float DummySegmentHeight = 75.0f;
constexpr float DummyCapsuleHalfHeight = DummySegmentHeight * 1.5f;
constexpr float FireGuideSegmentLength = 30.0f;
constexpr float FireGuideRadialThickness = 16.0f;
constexpr float FireGuideHeight = 5.0f;

FVector ScaleStaticMeshToSize(const UStaticMesh* Mesh, const float DesiredX,
	const float DesiredY, const float DesiredZ)
{
	if (!Mesh)
	{
		return FVector::OneVector;
	}
	const FVector Size = Mesh->GetBounds().BoxExtent * 2.0f;
	return FVector(
		Size.X > KINDA_SMALL_NUMBER ? DesiredX / Size.X : 1.0f,
		Size.Y > KINDA_SMALL_NUMBER ? DesiredY / Size.Y : 1.0f,
		Size.Z > KINDA_SMALL_NUMBER ? DesiredZ / Size.Z : 1.0f);
}

void ConfigureExtractLight(UPointLightComponent& Light, const FLinearColor& Color)
{
	Light.SetLightColor(Color);
	Light.SetIntensity(1600.0f);
	Light.SetAttenuationRadius(220.0f);
	Light.SetSourceRadius(8.0f);
	Light.SetCastShadows(false);
}
}

AMHGZTrainingDummy::AMHGZTrainingDummy()
{
	PrimaryActorTick.bCanEverTick = false;
	GetCapsuleComponent()->InitCapsuleSize(DummyDiameter * 0.5f, DummyCapsuleHalfHeight);

	// The Character mesh remains available to skeletal dummy subclasses, but this E5.1
	// training target is visually and physically represented by its 1 m cylinder.
	GetMesh()->SetVisibility(false, true);
	GetMesh()->SetHiddenInGame(true);
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(
		TEXT("/Game/Environment/DemoArena/Meshes/SM_Cylinder.SM_Cylinder"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> WhiteMaterial(
		TEXT("/Game/Monster/TrainingDummy/Materials/MI_TD_White.MI_TD_White"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> OrangeMaterial(
		TEXT("/Game/Monster/TrainingDummy/Materials/MI_TD_Orange.MI_TD_Orange"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> RedMaterial(
		TEXT("/Game/Monster/TrainingDummy/Materials/MI_TD_Red.MI_TD_Red"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FireMaterial(
		TEXT("/Game/Monster/TrainingDummy/Materials/MI_TD_Fire.MI_TD_Fire"));

	WhiteSegmentVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WhiteSegmentVisual"));
	OrangeSegmentVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("OrangeSegmentVisual"));
	RedSegmentVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RedSegmentVisual"));
	const TArray<UStaticMeshComponent*> Segments = {
		WhiteSegmentVisual, OrangeSegmentVisual, RedSegmentVisual};
	// SM_Cylinder's origin is on its lower cap (Z=0), not at its volume centre.
	// Keep visual bases and light/hitzone centres separate so a 0.75 m segment
	// exactly occupies the same vertical band as its configured hitzone.
	const TArray<float> SegmentBaseHeights = {
		-DummyCapsuleHalfHeight,
		-DummyCapsuleHalfHeight + DummySegmentHeight,
		-DummyCapsuleHalfHeight + DummySegmentHeight * 2.0f};
	const TArray<float> SegmentCentreHeights = {
		-DummySegmentHeight, 0.0f, DummySegmentHeight};
	const TArray<UMaterialInterface*> SegmentMaterials = {
		WhiteMaterial.Object, OrangeMaterial.Object, RedMaterial.Object};
	for (int32 Index = 0; Index < Segments.Num(); ++Index)
	{
		UStaticMeshComponent* Segment = Segments[Index];
		Segment->SetupAttachment(GetCapsuleComponent());
		Segment->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Segment->SetGenerateOverlapEvents(false);
		Segment->SetCastShadow(true);
		Segment->SetRelativeLocation(FVector(0.0f, 0.0f, SegmentBaseHeights[Index]));
		if (CylinderMesh.Succeeded())
		{
			Segment->SetStaticMesh(CylinderMesh.Object);
			Segment->SetRelativeScale3D(
				ScaleStaticMeshToSize(CylinderMesh.Object,
					DummyDiameter, DummyDiameter, DummySegmentHeight));
		}
		if (SegmentMaterials[Index])
		{
			Segment->SetMaterial(0, SegmentMaterials[Index]);
		}
	}

	WhiteExtractLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("WhiteExtractLight"));
	OrangeExtractLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("OrangeExtractLight"));
	RedExtractLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("RedExtractLight"));
	const TArray<UPointLightComponent*> ExtractLights = {
		WhiteExtractLight, OrangeExtractLight, RedExtractLight};
	const TArray<FLinearColor> ExtractColors = {
		FLinearColor::White, FLinearColor(1.0f, 0.30f, 0.02f), FLinearColor(1.0f, 0.02f, 0.01f)};
	for (int32 Index = 0; Index < ExtractLights.Num(); ++Index)
	{
		UPointLightComponent* Light = ExtractLights[Index];
		Light->SetupAttachment(GetCapsuleComponent());
		Light->SetRelativeLocation(FVector(0.0f, 0.0f, SegmentCentreHeights[Index]));
		ConfigureExtractLight(*Light, ExtractColors[Index]);
	}

	FireRingVisual = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("FireRingVisual"));
	FireRingVisual->SetupAttachment(GetCapsuleComponent());
	FireRingVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FireRingVisual->SetGenerateOverlapEvents(false);
	FireRingVisual->SetCastShadow(false);
	FireRingVisual->SetVisibility(false, true);
	if (CylinderMesh.Succeeded())
	{
		FireRingVisual->SetStaticMesh(CylinderMesh.Object);
	}
	if (FireMaterial.Succeeded())
	{
		FireRingVisual->SetMaterial(0, FireMaterial.Object);
	}

	FireRingFX = CreateDefaultSubobject<UParticleSystemComponent>(TEXT("FireRingFX"));
	FireRingFX->SetupAttachment(GetCapsuleComponent());
	FireRingFX->bAutoActivate = false;
	FireRingFX->bAutoDestroy = false;
	FireRingFX->SetCastShadow(false);

	FireRingLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FireRingLight"));
	FireRingLight->SetupAttachment(GetCapsuleComponent());
	FireRingLight->SetLightColor(FLinearColor(1.0f, 0.14f, 0.01f));
	FireRingLight->SetIntensity(9000.0f);
	FireRingLight->SetAttenuationRadius(360.0f);
	FireRingLight->SetSourceRadius(45.0f);
	FireRingLight->SetCastShadows(false);
	FireRingLight->SetVisibility(false, true);
}

void AMHGZTrainingDummy::BeginPlay()
{
	Super::BeginPlay();

	if (UMHGZAttributeSet* Attributes = GetAttributeSet())
	{
		Attributes->InitMaxHealth(DummyMaxHealth);
		Attributes->InitHealth(DummyMaxHealth);
		GetAbilitySystemComponent()->GetGameplayAttributeValueChangeDelegate(
			UMHGZAttributeSet::GetHealthAttribute()).AddUObject(
				this, &AMHGZTrainingDummy::HandleHealthChanged);
	}

	if (DummyConfig)
	{
		ApplyConfig(DummyConfig);
	}

	OnHealthChanged.Broadcast(GetCurrentHealth(), GetMaxHealth());
}

void AMHGZTrainingDummy::ApplyConfig(UMHGZDummyConfig* Config)
{
	if (!Config) return;
	DummyConfig = Config;

	// 设置骨骼模型
	if (USkeletalMesh* SKMesh = Config->DisplayMesh.LoadSynchronous())
	{
		GetMesh()->SetSkeletalMesh(SKMesh);
	}

	FString ValidationError;
	if (!UMHGZDummyConfig::ValidateHitzoneConfigs(Config->Hitzones, ValidationError))
	{
		UE_LOG(LogMHGZ, Error,
			TEXT("[TrainingDummy] %s config validation failed: %s"),
			*GetName(), *ValidationError);
	}

	// 生成部位碰撞体
	GenerateHitzonesFromConfig(Config);
	bFireRingRuntimeEnabled = Config->FireRing.bEnabled;
	RebuildFireRingVisuals();
	RefreshFireRingSchedule();

	// 播放待机动画
	if (UAnimMontage* IdleMontage = Config->LoopingMontage.LoadSynchronous())
	{
		if (UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance())
		{
			AnimInstance->Montage_Play(IdleMontage);
		}
	}
}

void AMHGZTrainingDummy::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RebuildFireRingVisuals();
	SetFireRingVisualActive(false);
}

void AMHGZTrainingDummy::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FireRingEmissionTimer);
		World->GetTimerManager().ClearTimer(FireRingEndTimer);
		World->GetTimerManager().ClearTimer(FireRingHitTimer);
	}
	bFireRingActive = false;
	SetFireRingVisualActive(false);
	Super::EndPlay(EndPlayReason);
}

float AMHGZTrainingDummy::GetCurrentHealth() const
{
	return GetAttributeSet() ? GetAttributeSet()->GetHealth() : 0.f;
}

float AMHGZTrainingDummy::GetMaxHealth() const
{
	return GetAttributeSet() ? GetAttributeSet()->GetMaxHealth() : 0.f;
}

void AMHGZTrainingDummy::SetFireRingEnabled(const bool bEnabled)
{
	bFireRingRuntimeEnabled = bEnabled;
	if (!bEnabled && bFireRingActive)
	{
		EndFireRing();
	}
	RefreshFireRingSchedule();
}

void AMHGZTrainingDummy::TriggerFireRingNow()
{
	if (!bFireRingRuntimeEnabled)
	{
		return;
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FireRingEmissionTimer);
	}
	BeginFireRing();
}

const FDummyFireRingConfig* AMHGZTrainingDummy::GetFireRingConfig() const
{
	return DummyConfig ? &DummyConfig->FireRing : nullptr;
}

void AMHGZTrainingDummy::RefreshFireRingSchedule()
{
	UWorld* World = GetWorld();
	const FDummyFireRingConfig* FireConfig = GetFireRingConfig();
	if (!World)
	{
		return;
	}
	World->GetTimerManager().ClearTimer(FireRingEmissionTimer);
	World->GetTimerManager().ClearTimer(FireRingEndTimer);
	World->GetTimerManager().ClearTimer(FireRingHitTimer);
	if (!bFireRingRuntimeEnabled || !FireConfig)
	{
		bFireRingActive = false;
		SetFireRingVisualActive(false);
		return;
	}

	World->GetTimerManager().SetTimer(FireRingEmissionTimer, this,
		&AMHGZTrainingDummy::BeginFireRing,
		FMath::Max(0.01f, FireConfig->InitialDelay), false);
}

void AMHGZTrainingDummy::BeginFireRing()
{
	const FDummyFireRingConfig* FireConfig = GetFireRingConfig();
	UWorld* World = GetWorld();
	if (!FireConfig || !World || !bFireRingRuntimeEnabled || bFireRingActive)
	{
		return;
	}

	bFireRingActive = true;
	SetFireRingVisualActive(true);
	OnFireRingStateChanged.Broadcast(true);
	ApplyFireRingHit();
	World->GetTimerManager().SetTimer(FireRingHitTimer, this,
		&AMHGZTrainingDummy::ApplyFireRingHit,
		FMath::Max(0.01f, FireConfig->HitInterval), true);
	World->GetTimerManager().SetTimer(FireRingEndTimer, this,
		&AMHGZTrainingDummy::EndFireRing,
		FMath::Max(0.01f, FireConfig->ActiveDuration), false);
}

void AMHGZTrainingDummy::EndFireRing()
{
	if (!bFireRingActive)
	{
		return;
	}
	bFireRingActive = false;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FireRingHitTimer);
		if (const FDummyFireRingConfig* FireConfig = GetFireRingConfig();
			bFireRingRuntimeEnabled && FireConfig)
		{
			// Schedule from this emission's start rather than relying on timer-order at
			// an equal End/Interval boundary. Validation guarantees this is non-negative.
			World->GetTimerManager().SetTimer(FireRingEmissionTimer, this,
				&AMHGZTrainingDummy::BeginFireRing,
				FMath::Max(0.01f, FireConfig->EmissionInterval - FireConfig->ActiveDuration), false);
		}
	}
	SetFireRingVisualActive(false);
	OnFireRingStateChanged.Broadcast(false);
}

void AMHGZTrainingDummy::ApplyFireRingHit()
{
	if (!bFireRingActive || !bFireRingRuntimeEnabled)
	{
		return;
	}
	ACharacter* Target = UGameplayStatics::GetPlayerCharacter(this, 0);
	if (Target && IsCharacterInsideFireRing(*Target))
	{
		SubmitFireRingHit(*Target, FGuid::NewGuid());
	}
}

bool AMHGZTrainingDummy::IsCharacterInsideFireRing(const ACharacter& Character) const
{
	const FDummyFireRingConfig* FireConfig = GetFireRingConfig();
	const UCapsuleComponent* Capsule = Character.GetCapsuleComponent();
	if (!FireConfig || !Capsule)
	{
		return false;
	}

	const FVector Centre = GetActorTransform().TransformPosition(FireConfig->RelativeLocation);
	const FVector Delta = Character.GetActorLocation() - Centre;
	const float Distance2D = FVector(Delta.X, Delta.Y, 0.0f).Size();
	return FMath::Abs(Distance2D - FireConfig->Radius)
		<= FireConfig->RingHalfThickness + Capsule->GetScaledCapsuleRadius()
		&& FMath::Abs(Delta.Z)
		<= FireConfig->VerticalHalfHeight + Capsule->GetScaledCapsuleHalfHeight();
}

EIncomingHitSubmitResult AMHGZTrainingDummy::SubmitFireRingHit(
	ACharacter& TargetCharacter, const FGuid AttackInstanceID)
{
	const FDummyFireRingConfig* FireConfig = GetFireRingConfig();
	UMHGZIncomingHitResolverComponent* Resolver =
		TargetCharacter.FindComponentByClass<UMHGZIncomingHitResolverComponent>();
	UCapsuleComponent* Capsule = TargetCharacter.GetCapsuleComponent();
	if (!FireConfig || !Resolver || !Capsule)
	{
		return EIncomingHitSubmitResult::Rejected;
	}

	const FVector Centre = GetActorTransform().TransformPosition(FireConfig->RelativeLocation);
	FVector Radial = TargetCharacter.GetActorLocation() - Centre;
	Radial.Z = 0.0f;
	const FVector Normal = Radial.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
	const FVector ImpactPoint = Centre + Normal * FireConfig->Radius;

	FIncomingHitContext Context;
	Context.AttackInstanceID = AttackInstanceID;
	Context.SourceActor = this;
	Context.SourceAttackTag = FireConfig->SourceActionTag;
	Context.bCounterable = FireConfig->bCounterable;
	Context.Damage = FireConfig->Damage;
	Context.StaggerTag = FireConfig->StaggerTag;
	Context.Hit = FHitResult(&TargetCharacter, Capsule, ImpactPoint, -Normal);
	Context.Hit.bBlockingHit = true;
	Context.Hit.bStartPenetrating = false;
	Context.Hit.Time = 0.0f;
	Context.Hit.Distance = FVector::Dist(TargetCharacter.GetActorLocation(), ImpactPoint);
	Context.Hit.Location = ImpactPoint;
	Context.Hit.ImpactPoint = ImpactPoint;
	Context.Hit.Normal = -Normal;
	Context.Hit.ImpactNormal = -Normal;
	return Resolver->SubmitIncomingHit(Context);
}

void AMHGZTrainingDummy::SetFireRingVisualActive(const bool bActive)
{
	if (FireRingVisual)
	{
		FireRingVisual->SetVisibility(bActive, true);
	}
	if (FireRingFX)
	{
		if (bActive)
		{
			FireRingFX->ActivateSystem(true);
		}
		else
		{
			FireRingFX->DeactivateSystem();
		}
	}
	if (FireRingLight)
	{
		FireRingLight->SetVisibility(bActive, true);
	}
}

void AMHGZTrainingDummy::RebuildFireRingVisuals()
{
	if (!FireRingVisual || !FireRingVisual->GetStaticMesh())
	{
		return;
	}
	const FDummyFireRingConfig* FireConfig = GetFireRingConfig();
	const int32 SegmentCount = FireConfig ? FireConfig->VisualSegmentCount : 24;
	const float Radius = FireConfig ? FireConfig->Radius : 100.0f;
	const FVector RelativeLocation = FireConfig
		? FireConfig->RelativeLocation : FVector(0.0f, 0.0f, -37.5f);
	const FVector Scale = ScaleStaticMeshToSize(FireRingVisual->GetStaticMesh(),
		FireGuideSegmentLength, FireGuideRadialThickness, FireGuideHeight);

	FireRingVisual->ClearInstances();
	for (int32 Index = 0; Index < SegmentCount; ++Index)
	{
		const float Angle = 2.0f * PI * static_cast<float>(Index) / static_cast<float>(SegmentCount);
		const FVector Location = RelativeLocation + FVector(
			FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius,
			-FireGuideHeight * 0.5f);
		const FRotator Rotation(0.0f, FMath::RadiansToDegrees(Angle) + 90.0f, 0.0f);
		FireRingVisual->AddInstance(FTransform(Rotation, Location, Scale));
	}
	if (FireRingFX)
	{
		static constexpr TCHAR FireParticlePath[] =
			TEXT("/Game/Monster/TrainingDummy/VFX/PS_TD_FireRing.PS_TD_FireRing");
		if (!FireRingFX->Template)
		{
			FireRingFX->SetTemplate(LoadObject<UParticleSystem>(nullptr, FireParticlePath));
		}
		FireRingFX->SetRelativeLocation(RelativeLocation);
		const float RadiusScale = Radius / 100.0f;
		FireRingFX->SetRelativeScale3D(FVector(RadiusScale, RadiusScale, 1.0f));
	}
	if (FireRingLight)
	{
		FireRingLight->SetRelativeLocation(RelativeLocation + FVector(0.0f, 0.0f, 22.0f));
	}
}

EIncomingHitSubmitResult AMHGZTrainingDummy::SubmitCounterTestAttack(
	ACharacter* TargetCharacter, FGuid AttackInstanceID)
{
	if (!TargetCharacter)
	{
		return EIncomingHitSubmitResult::Rejected;
	}

	UMHGZIncomingHitResolverComponent* Resolver =
		TargetCharacter->FindComponentByClass<UMHGZIncomingHitResolverComponent>();
	if (!Resolver)
	{
		UE_LOG(LogMHGZ, Warning,
			TEXT("[TrainingDummy] %s cannot submit: target %s has no IncomingHitResolver"),
			*GetName(), *TargetCharacter->GetName());
		return EIncomingHitSubmitResult::Rejected;
	}

	const FDummyCounterAttackConfig& Attack =
		DummyConfig ? DummyConfig->CounterTestAttack : FDummyCounterAttackConfig();

	FIncomingHitContext Context;
	Context.AttackInstanceID = AttackInstanceID;
	Context.SourceActor = this;
	Context.Damage = Attack.Damage;
	Context.bCounterable = Attack.bCounterable;
	Context.SourceAttackTag = Attack.SourceActionTag;
	Context.StaggerTag = Attack.StaggerTag;

	UCapsuleComponent* Capsule = TargetCharacter->GetCapsuleComponent();
	if (!Capsule)
	{
		return EIncomingHitSubmitResult::Rejected;
	}

	// Fixed, reproducible hit against the target capsule/impact.
	const FVector CapsuleCenter = Capsule->GetComponentLocation();
	const FVector Offset =
		Attack.HitOffset.IsNearlyZero() ? FVector::UpVector * 10.f : Attack.HitOffset;
	const FVector ImpactPoint = CapsuleCenter + Offset;
	Context.Hit = FHitResult(TargetCharacter, Capsule, ImpactPoint, FVector::UpVector);
	Context.Hit.bBlockingHit = true;
	Context.Hit.bStartPenetrating = false;
	Context.Hit.Time = 0.f;
	Context.Hit.Distance = Offset.Size();
	Context.Hit.Location = ImpactPoint;
	Context.Hit.ImpactPoint = ImpactPoint;
	Context.Hit.ImpactNormal = FVector::UpVector;
	Context.Hit.Normal = FVector::UpVector;

	return Resolver->SubmitIncomingHit(Context);
}

void AMHGZTrainingDummy::HandleHealthChanged(const FOnAttributeChangeData& ChangeData)
{
	const float MaxHealth = GetMaxHealth();
	UE_LOG(LogTemp, Log, TEXT("[TrainingDummy] %s Health %.1f / %.1f"),
		*GetName(), ChangeData.NewValue, MaxHealth);
	OnHealthChanged.Broadcast(ChangeData.NewValue, MaxHealth);
}
