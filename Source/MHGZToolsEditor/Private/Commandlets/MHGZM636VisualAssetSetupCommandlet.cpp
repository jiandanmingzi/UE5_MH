#include "MHGZIGActionConfigAccess.h"
// Copyright MHGZ Project. All Rights Reserved.

#include "Commandlets/MHGZM636VisualAssetSetupCommandlet.h"

#if WITH_EDITOR

#include "AssetRegistry/AssetRegistryModule.h"
#include "Distributions/DistributionFloatConstant.h"
#include "Distributions/DistributionFloatUniform.h"
#include "Distributions/DistributionVectorUniform.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/DirectionalLight.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/LightComponent.h"
#include "ImageUtils.h"
#include "ShaderCompiler.h"
#include "Serialization/BufferArchive.h"
#include "ParticleEmitterInstances.h"
#include "GameplayTagContainer.h"
#include "InsectGlaive/InsectGlaiveCombatConfig.h"
#include "InsectGlaive/IGPowderActor.h"
#include "Monster/MHGZTrainingDummy.h"
#include "Monster/MHGZMonsterHitzoneComponent.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Particles/Color/ParticleModuleColorOverLife.h"
#include "Particles/Color/ParticleModuleColor.h"
#include "Particles/Acceleration/ParticleModuleAcceleration.h"
#include "Particles/Lifetime/ParticleModuleLifetime.h"
#include "Particles/Location/ParticleModuleLocation.h"
#include "Particles/Location/ParticleModuleLocationPrimitiveCylinder.h"
#include "Particles/Location/ParticleModuleLocationPrimitiveSphere.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleLODLevel.h"
#include "Particles/ParticleModuleRequired.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "Particles/Rotation/ParticleModuleRotation.h"
#include "Particles/Rotation/ParticleModuleMeshRotation.h"
#include "Particles/RotationRate/ParticleModuleRotationRate.h"
#include "Particles/RotationRate/ParticleModuleMeshRotationRate.h"
#include "Particles/Size/ParticleModuleSize.h"
#include "Particles/Spawn/ParticleModuleSpawn.h"
#include "Particles/TypeData/ParticleModuleTypeDataMesh.h"
#include "Particles/Velocity/ParticleModuleVelocity.h"
#include "UObject/MetaData.h"
#include "UObject/SavePackage.h"

namespace UE::MHGZ::M636VisualAssetSetup
{
namespace
{
constexpr TCHAR GeneratorMarker[] = TEXT("MHGZ.M6.3.6.VisualAssets.v1");
constexpr TCHAR MarkerTexturePath[] = TEXT("/Game/Weapons/InsectGlaive/VFX/T_IG_KinsectCloudAlpha");
constexpr TCHAR MarkMaterialPath[] = TEXT("/Game/Weapons/InsectGlaive/VFX/M_IG_KinsectMarkCloud");
constexpr TCHAR PowderMaterialPath[] = TEXT("/Game/Weapons/InsectGlaive/VFX/M_IG_PowderRedCloud");
constexpr TCHAR PowderEmberMaterialPath[] = TEXT("/Game/Weapons/InsectGlaive/VFX/M_IG_PowderEmber");
constexpr TCHAR MarkSystemPath[] = TEXT("/Game/Weapons/InsectGlaive/VFX/PS_IG_KinsectMarkCloud");
constexpr TCHAR PowderSystemPath[] = TEXT("/Game/Weapons/InsectGlaive/VFX/PS_IG_PowderRedCloud");
constexpr TCHAR PowderBlueprintPath[] = TEXT("/Game/Weapons/InsectGlaive/BP/BP_IG_PowderCloud");
constexpr TCHAR CombatConfigPath[] = TEXT("/Game/Weapons/InsectGlaive/Data/DA_IG_Combat.DA_IG_Combat");
constexpr TCHAR LegacyMarkPath[] = TEXT("/Game/Weapons/InsectGlaive/VFX/PS_IG_MarkYellowPowder.PS_IG_MarkYellowPowder");
constexpr TCHAR ReportPath[] = TEXT("Saved/M6Audit/m6_3_6_visual_asset_setup_report.json");
constexpr float MarkLifetimeSeconds = 91.0f;
constexpr float PowderLifetimeSeconds = 180.0f;
constexpr float MarkParticleLifetimeSeconds = 1.2f;
constexpr float PowderParticleLifetimeSeconds = 1.2f;
constexpr float MarkParticleSpawnRate = 96.0f;
constexpr float PowderParticleSpawnRate = 144.0f;
constexpr float PowderEmberSpawnRate = 18.0f;
constexpr int32 MarkInitialParticleBurst = 144;
constexpr int32 PowderInitialParticleBurst = 108;
constexpr int32 PowderEmberInitialBurst = 12;
// Uniformly scale the prior 52 cm mark cloud to a 100 cm visual diameter;
// distribute spherical motes through a filled 3D volume instead of a zero-height disk.
// The gameplay footprint remains a separate 100 cm surface-tangent square.
constexpr float PreviousMarkMoteRadiusCm = 17.0f;
constexpr float PreviousMarkMoteMinSizeCm = 10.0f;
constexpr float PreviousMarkMoteMaxSizeCm = 18.0f;
constexpr float MarkVisualScale = 100.0f /
	(PreviousMarkMoteRadiusCm * 2.0f + PreviousMarkMoteMaxSizeCm);
constexpr float MarkMoteRadiusCm = PreviousMarkMoteRadiusCm * MarkVisualScale;
constexpr float MarkMoteMinSizeCm = PreviousMarkMoteMinSizeCm * MarkVisualScale;
constexpr float MarkMoteMaxSizeCm = PreviousMarkMoteMaxSizeCm * MarkVisualScale;
constexpr float MarkMoteOpacity = 0.58f;
constexpr float PowderSmokeOpacity = 0.58f;
constexpr float PowderSmokeMinSizeCm = 12.0f;
constexpr float PowderSmokeMaxSizeCm = 18.0f;
constexpr float PowderVisualRadiusCm = 75.0f;
constexpr float PowderSmokeSpawnRadiusCm = PowderVisualRadiusCm - PowderSmokeMaxSizeCm * 0.5f;
constexpr float PowderEmberParticleLifetimeSeconds = 0.7f;
constexpr float PowderEmberMinSizeCm = 0.8f;
constexpr float PowderEmberMaxSizeCm = 1.8f;
constexpr float PowderEmberSpawnRadiusMinCm = 20.0f;
constexpr float PowderEmberSpawnRadiusMaxCm =
	PowderVisualRadiusCm - PowderEmberMaxSizeCm * 0.5f;
constexpr float ApprovedArenaHalfSideCm = 3000.0f;
constexpr float ApprovedAutoMarkAttackIntervalSeconds = 2.0f;
constexpr float ApprovedMarkedHitPowderCooldownSeconds = 10.0f;
constexpr int32 ApprovedPowderMaxCount = 100;
constexpr float ApprovedPowderMotionValue = 100.0f;
constexpr float ApprovedPowderDespawnDistanceCm = 10000.0f;
constexpr float ApprovedPowderNaturalExplosionRadiusCm = 150.0f;
constexpr float ApprovedPowderVortexExplosionRadiusCm = 300.0f;
constexpr float ApprovedPowderVortexDelaySeconds = 1.0f;
constexpr float ApprovedPowderDamageAt1 = 10.0f;
constexpr float ApprovedPowderDamageAt5 = 50.0f;
constexpr float ApprovedPowderDamageAt10 = 100.0f;
constexpr TCHAR PowderHitCueTagName[] = TEXT("GameplayCue.Hit.IG.Powder");
constexpr TCHAR PowderExplosionCueTagName[] = TEXT("GameplayCue.IG.PowderVortex");

struct FRun
{
	bool bApply = false;
	bool bDiagnose = false;
	bool bVisualOnly = false;
	bool bPowderRadiiOnly = false;
	bool bSuccess = true;
	bool bChangedConfig = false;
	TArray<FString> Errors;
	TArray<FString> CreatedAssets;
	TArray<FString> ModifiedAssets;
	TArray<FString> ReusedGeneratedAssets;
	UInsectGlaiveCombatConfig* CombatConfig = nullptr;
	UParticleSystem* MarkSystem = nullptr;
	UParticleSystem* PowderSystem = nullptr;
	UBlueprint* PowderBlueprint = nullptr;
};

FString ObjectPath(const TCHAR* PackagePath)
{
	const FString Package(PackagePath);
	const int32 Slash = Package.Find(TEXT("/"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
	return Slash == INDEX_NONE
		? Package + TEXT(".") + Package
		: Package + TEXT(".") + Package.Mid(Slash + 1);
}

FString GetGeneratorMarker(const UObject* Object)
{
	return Object && Object->GetOutermost()
		? Object->GetOutermost()->GetMetaData().GetValue(Object, TEXT("MHGZ.Generator"))
		: FString();
}

bool HasCompatibleOrMissingAsset(const TCHAR* PackagePath, const UClass* ExpectedClass,
	UObject*& OutExisting, FRun& Run)
{
	OutExisting = StaticLoadObject(UObject::StaticClass(), nullptr, *ObjectPath(PackagePath),
		nullptr, LOAD_NoWarn);
	if (!OutExisting)
	{
		return true;
	}
	if (!OutExisting->IsA(ExpectedClass))
	{
		Run.Errors.Add(FString::Printf(TEXT("Asset path %s exists with class %s; refusing to overwrite."),
			PackagePath, *OutExisting->GetClass()->GetName()));
		return false;
	}
	if (GetGeneratorMarker(OutExisting) != GeneratorMarker)
	{
		Run.Errors.Add(FString::Printf(TEXT("Asset path %s is not marked as generated by this tool; refusing to overwrite."),
			PackagePath));
		return false;
	}
	Run.ReusedGeneratedAssets.Add(PackagePath);
	return true;
}

template <typename T>
T* CreateTopLevelAsset(const TCHAR* PackagePath, FRun& Run)
{
	const FString PackageName(PackagePath);
	const int32 Slash = PackageName.Find(TEXT("/"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
	if (Slash == INDEX_NONE)
	{
		Run.Errors.Add(FString::Printf(TEXT("Invalid asset package path: %s"), PackagePath));
		return nullptr;
	}
	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		Run.Errors.Add(FString::Printf(TEXT("Could not create package: %s"), PackagePath));
		return nullptr;
	}
	const FName AssetName(*PackageName.Mid(Slash + 1));
	T* Asset = NewObject<T>(Package, AssetName, RF_Public | RF_Standalone | RF_Transactional);
	if (!Asset)
	{
		Run.Errors.Add(FString::Printf(TEXT("Could not create asset: %s"), PackagePath));
		return nullptr;
	}
	Package->GetMetaData().SetValue(Asset, TEXT("MHGZ.Generator"), GeneratorMarker);
	FAssetRegistryModule::AssetCreated(Asset);
	Run.CreatedAssets.Add(PackagePath);
	return Asset;
}

bool SaveAsset(UObject* Asset, FRun& Run)
{
	if (!Asset || !Asset->GetOutermost())
	{
		Run.Errors.Add(TEXT("Cannot save null asset or asset without an outer package."));
		return false;
	}
	UPackage* Package = Asset->GetOutermost();
	FString Filename;
	if (!FPackageName::TryConvertLongPackageNameToFilename(Package->GetName(), Filename,
		FPackageName::GetAssetPackageExtension()))
	{
		Run.Errors.Add(FString::Printf(TEXT("Could not convert package path to filename: %s"), *Package->GetName()));
		return false;
	}
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
	Package->MarkPackageDirty();
	FSavePackageArgs Args;
	Args.TopLevelFlags = RF_Public | RF_Standalone;
	if (!UPackage::SavePackage(Package, Asset, *Filename, Args))
	{
		Run.Errors.Add(FString::Printf(TEXT("Failed to save asset: %s"), *Asset->GetPathName()));
		return false;
	}
	return true;
}

uint8 CloudAlpha(const float U, const float V, const float Seed)
{
	const FVector2D P(U * 2.0f - 1.0f, V * 2.0f - 1.0f);
	const float Noise = 0.52f * FMath::PerlinNoise2D(P * 3.1f + FVector2D(Seed, Seed * 0.37f))
		+ 0.31f * FMath::PerlinNoise2D(P * 6.4f + FVector2D(Seed * 1.7f, -Seed))
		+ 0.17f * FMath::PerlinNoise2D(P * 12.8f + FVector2D(-Seed * 0.8f, Seed * 2.1f));
	const float Radius = P.Size();
	const float RaggedEdge = 0.78f + Noise * 0.12f;
	const float Edge = FMath::Clamp((RaggedEdge - Radius) / 0.22f, 0.0f, 1.0f);
	const float Density = FMath::Clamp(0.42f + Noise * 0.35f, 0.12f, 0.88f);
	const float Alpha = FMath::SmoothStep(0.0f, 1.0f, Edge) * Density;
	return static_cast<uint8>(FMath::RoundToInt(Alpha * 255.0f));
}

UTexture2D* CreateCloudTexture(FRun& Run)
{
	UObject* ExistingObject = nullptr;
	if (!HasCompatibleOrMissingAsset(MarkerTexturePath, UTexture2D::StaticClass(), ExistingObject, Run))
	{
		return nullptr;
	}
	if (ExistingObject)
	{
		UTexture2D* Existing = CastChecked<UTexture2D>(ExistingObject);
		const FIntPoint ImportedSize = Existing->GetImportedSize();
		if (ImportedSize.X != 128 || ImportedSize.Y != 128)
		{
			Run.Errors.Add(FString::Printf(TEXT("Generated cloud texture has unexpected imported dimensions (%d x %d); refusing to rewrite it."),
				ImportedSize.X, ImportedSize.Y));
			return nullptr;
		}
		return Existing;
	}

	UTexture2D* Texture = CreateTopLevelAsset<UTexture2D>(MarkerTexturePath, Run);
	if (!Texture)
	{
		return nullptr;
	}
	constexpr int32 Size = 128;
	TArray<FColor> Pixels;
	Pixels.SetNumUninitialized(Size * Size);
	for (int32 Y = 0; Y < Size; ++Y)
	{
		for (int32 X = 0; X < Size; ++X)
		{
			const float U = (static_cast<float>(X) + 0.5f) / static_cast<float>(Size);
			const float V = (static_cast<float>(Y) + 0.5f) / static_cast<float>(Size);
			Pixels[Y * Size + X] = FColor(255, 255, 255, CloudAlpha(U, V, 3.73f));
		}
	}
	Texture->PreEditChange(nullptr);
	Texture->Source.Init(Size, Size, 1, 1, TSF_BGRA8,
		reinterpret_cast<const uint8*>(Pixels.GetData()));
	Texture->SRGB = false;
	Texture->CompressionSettings = TC_Default;
	Texture->MipGenSettings = TMGS_FromTextureGroup;
	Texture->AddressX = TA_Clamp;
	Texture->AddressY = TA_Clamp;
	Texture->Filter = TF_Bilinear;
	Texture->LODGroup = TEXTUREGROUP_Effects;
	Texture->NeverStream = true;
	Texture->PostEditChange();
	return Texture;
}

UMaterialExpression* AddExpression(UMaterial& Material, UClass* ExpressionClass,
	const TCHAR* Name, const int32 X, const int32 Y)
{
	UMaterialExpression* Expression = NewObject<UMaterialExpression>(&Material,
		ExpressionClass, FName(Name), RF_Transactional);
	if (!Expression)
	{
		return nullptr;
	}
	Expression->MaterialExpressionEditorX = X;
	Expression->MaterialExpressionEditorY = Y;
	Material.GetEditorOnlyData()->ExpressionCollection.Expressions.Add(Expression);
	return Expression;
}

bool ValidateOrRepairCloudMaterial(UMaterial* Material, UTexture2D* Texture,
	const TCHAR* PackagePath, const bool bRepair, FRun& Run)
{
	if (!Material || !Texture || !Material->GetEditorOnlyData())
	{
		Run.Errors.Add(FString::Printf(TEXT("Cannot validate generated cloud material %s."), PackagePath));
		return false;
	}
	if (Texture->SRGB)
	{
		Run.Errors.Add(FString::Printf(TEXT("Cloud opacity texture for %s must use linear (sRGB disabled) data."), PackagePath));
		return false;
	}

	TArray<UMaterialExpressionTextureSample*> Samples;
	for (UMaterialExpression* Expression : Material->GetEditorOnlyData()->ExpressionCollection.Expressions)
	{
		if (UMaterialExpressionTextureSample* Sample = Cast<UMaterialExpressionTextureSample>(Expression);
			Sample && Sample->Texture == Texture)
		{
			Samples.Add(Sample);
		}
	}
	if (Samples.IsEmpty())
	{
		Run.Errors.Add(FString::Printf(TEXT("Generated cloud material %s does not sample its opacity texture."), PackagePath));
		return false;
	}
	const bool bMarkMaterial = FCString::Stricmp(PackagePath, MarkMaterialPath) == 0;
	const FLinearColor ExpectedTint = bMarkMaterial
		? FLinearColor(1.0f, 0.68f, 0.28f, 1.0f)
		: FLinearColor(0.90f, 0.18f, 0.025f, 1.0f);
	UMaterialExpressionConstant3Vector* Tint = nullptr;
	UMaterialExpressionConstant* OpacityConstant = nullptr;
	UMaterialExpressionMultiply* OpacityMultiply = nullptr;
	for (UMaterialExpression* Expression : Material->GetEditorOnlyData()->ExpressionCollection.Expressions)
	{
		if (Expression && Expression->GetFName() == FName(TEXT("CloudTint")))
		{
			Tint = Cast<UMaterialExpressionConstant3Vector>(Expression);
		}
		else if (Expression && Expression->GetFName() == FName(TEXT("CloudOpacity")))
		{
			OpacityConstant = Cast<UMaterialExpressionConstant>(Expression);
		}
		else if (Expression && Expression->GetFName() == FName(TEXT("CloudOpacityMultiply")))
		{
			OpacityMultiply = Cast<UMaterialExpressionMultiply>(Expression);
		}
	}
	FExpressionInput* BaseColor = Material->GetExpressionInputForProperty(MP_BaseColor);
	FExpressionInput* Opacity = Material->GetExpressionInputForProperty(MP_Opacity);
	FExpressionInput* EmissiveColor = Material->GetExpressionInputForProperty(MP_EmissiveColor);
	if (!Tint || !BaseColor || !Opacity)
	{
		Run.Errors.Add(FString::Printf(TEXT("Generated cloud material %s is missing a required tint/color/opacity graph."), PackagePath));
		return false;
	}

	bool bChanged = false;
	if (Material->BlendMode != BLEND_Translucent || !Material->TwoSided)
	{
		if (!bRepair)
		{
			Run.Errors.Add(FString::Printf(TEXT("Generated cloud material %s must be two-sided and translucent."), PackagePath));
			return false;
		}
		Material->Modify();
		Material->BlendMode = BLEND_Translucent;
		Material->TwoSided = true;
		bChanged = true;
	}
	const bool bAppearanceNeedsRepair = !Tint->Constant.Equals(ExpectedTint, 0.001f)
		|| BaseColor->Expression != Tint
		|| (EmissiveColor && EmissiveColor->Expression != nullptr)
		|| Samples.Num() != 1
		|| !OpacityConstant || !OpacityMultiply
		|| (OpacityConstant && !FMath::IsNearlyEqual(OpacityConstant->R,
			bMarkMaterial ? MarkMoteOpacity : PowderSmokeOpacity))
		|| (OpacityMultiply && OpacityConstant && OpacityMultiply->B.Expression != OpacityConstant)
		|| (OpacityMultiply && Samples.Num() == 1 && OpacityMultiply->A.Expression != Samples[0])
		|| Opacity->Expression != OpacityMultiply;
	if (bAppearanceNeedsRepair)
	{
		if (!bRepair)
		{
			Run.Errors.Add(FString::Printf(TEXT("Generated cloud material %s has the wrong tint or emissive output."), PackagePath));
			return false;
		}
		if (!bChanged)
		{
			Material->Modify();
		}
		if (!OpacityConstant)
		{
			OpacityConstant = Cast<UMaterialExpressionConstant>(AddExpression(*Material,
				UMaterialExpressionConstant::StaticClass(), TEXT("CloudOpacity"), -10, 180));
		}
		if (!OpacityMultiply)
		{
			OpacityMultiply = Cast<UMaterialExpressionMultiply>(AddExpression(*Material,
				UMaterialExpressionMultiply::StaticClass(), TEXT("CloudOpacityMultiply"), 210, 0));
		}
		if (!OpacityConstant || !OpacityMultiply || Samples.Num() != 1)
		{
			Run.Errors.Add(FString::Printf(TEXT("Could not repair particle-opacity graph for %s."), PackagePath));
			return false;
		}
		Tint->Modify();
		OpacityConstant->Modify();
		OpacityMultiply->Modify();
		Tint->Constant = ExpectedTint;
		BaseColor->Connect(0, Tint);
		OpacityConstant->R = bMarkMaterial ? MarkMoteOpacity : PowderSmokeOpacity;
		OpacityMultiply->A.Connect(4, Samples[0]);
		OpacityMultiply->B.Connect(0, OpacityConstant);
		Opacity->Connect(0, OpacityMultiply);
		if (EmissiveColor)
		{
			EmissiveColor->Expression = nullptr;
		}
		Material->SetShadingModel(MSM_DefaultLit);
		bChanged = true;
	}

	for (UMaterialExpressionTextureSample* Sample : Samples)
	{
		if (Sample->SamplerType != SAMPLERTYPE_LinearColor)
		{
			if (!bRepair)
			{
				Run.Errors.Add(FString::Printf(TEXT("Texture sample in %s must use Linear Color sampler because its opacity texture has sRGB disabled."), PackagePath));
				return false;
			}
			if (!bChanged)
			{
				Material->Modify();
			}
			Sample->Modify();
			Sample->SamplerType = SAMPLERTYPE_LinearColor;
			bChanged = true;
		}
	}

	if (bChanged)
	{
		Material->PostEditChange();
		Material->MarkPackageDirty();
		Run.ModifiedAssets.AddUnique(PackagePath);
	}
	return true;
}

UMaterial* CreateCloudMaterial(const TCHAR* PackagePath, UTexture2D* Texture,
	FRun& Run)
{
	UObject* ExistingObject = nullptr;
	if (!HasCompatibleOrMissingAsset(PackagePath, UMaterial::StaticClass(), ExistingObject, Run))
	{
		return nullptr;
	}
	if (ExistingObject)
	{
		UMaterial* ExistingMaterial = CastChecked<UMaterial>(ExistingObject);
		return ValidateOrRepairCloudMaterial(ExistingMaterial, Texture, PackagePath, true, Run)
			? ExistingMaterial : nullptr;
	}
	if (!Texture)
	{
		Run.Errors.Add(FString::Printf(TEXT("Cannot create cloud material without texture: %s"), PackagePath));
		return nullptr;
	}
	UMaterial* Material = CreateTopLevelAsset<UMaterial>(PackagePath, Run);
	if (!Material)
	{
		return nullptr;
	}
	Material->PreEditChange(nullptr);
	Material->MaterialDomain = MD_Surface;
	Material->BlendMode = BLEND_Translucent;
	Material->TwoSided = true;
	Material->SetShadingModel(MSM_DefaultLit);

	UMaterialExpressionTextureCoordinate* UV = Cast<UMaterialExpressionTextureCoordinate>(
		AddExpression(*Material, UMaterialExpressionTextureCoordinate::StaticClass(),
			TEXT("CloudUV"), -440, 0));
	UMaterialExpressionTextureSample* Sample = Cast<UMaterialExpressionTextureSample>(
		AddExpression(*Material, UMaterialExpressionTextureSample::StaticClass(),
			TEXT("CloudAlpha"), -210, 0));
	UMaterialExpressionConstant3Vector* Tint = Cast<UMaterialExpressionConstant3Vector>(
		AddExpression(*Material, UMaterialExpressionConstant3Vector::StaticClass(),
			TEXT("CloudTint"), -210, 180));
	UMaterialExpressionConstant* OpacityConstant = Cast<UMaterialExpressionConstant>(
		AddExpression(*Material, UMaterialExpressionConstant::StaticClass(),
			TEXT("CloudOpacity"), -10, 180));
	UMaterialExpressionMultiply* OpacityMultiply = Cast<UMaterialExpressionMultiply>(
		AddExpression(*Material, UMaterialExpressionMultiply::StaticClass(),
			TEXT("CloudOpacityMultiply"), 210, 0));
	if (!UV || !Sample || !Tint || !OpacityConstant || !OpacityMultiply)
	{
		Run.Errors.Add(FString::Printf(TEXT("Could not create expression graph for %s"), PackagePath));
		return nullptr;
	}
	Sample->Texture = Texture;
	Sample->SamplerType = SAMPLERTYPE_LinearColor;
	Sample->Coordinates.Connect(0, UV);
	const bool bMarkMaterial = FCString::Stricmp(PackagePath, MarkMaterialPath) == 0;
	Tint->Constant = bMarkMaterial
		? FLinearColor(1.0f, 0.68f, 0.28f, 1.0f)
		: FLinearColor(0.90f, 0.18f, 0.025f, 1.0f);
	FExpressionInput* Opacity = Material->GetExpressionInputForProperty(MP_Opacity);
	FExpressionInput* Color = Material->GetExpressionInputForProperty(MP_BaseColor);
	if (!Opacity || !Color)
	{
		Run.Errors.Add(FString::Printf(TEXT("Required material output pin missing for %s"), PackagePath));
		return nullptr;
	}
	OpacityConstant->R = bMarkMaterial ? MarkMoteOpacity : PowderSmokeOpacity;
	OpacityMultiply->A.Connect(4, Sample);
	OpacityMultiply->B.Connect(0, OpacityConstant);
	Opacity->Connect(0, OpacityMultiply);
	Color->Connect(0, Tint);
	bool bNeedsRecompile = false;
	Material->SetMaterialUsage(bNeedsRecompile,
		bMarkMaterial ? MATUSAGE_MeshParticles : MATUSAGE_ParticleSprites);
	Material->PostEditChange();
	return Material;
}

bool ValidatePowderEmberMaterial(UMaterial* Material, UTexture2D* Texture, FRun& Run)
{
	if (!Material || !Texture || !Material->GetEditorOnlyData())
	{
		Run.Errors.Add(TEXT("Powder ember material or its source texture is missing."));
		return false;
	}
	TArray<UMaterialExpressionTextureSample*> Samples;
	UMaterialExpressionConstant3Vector* Tint = nullptr;
	UMaterialExpressionMultiply* Glow = nullptr;
	for (UMaterialExpression* Expression : Material->GetEditorOnlyData()->ExpressionCollection.Expressions)
	{
		if (UMaterialExpressionTextureSample* Sample = Cast<UMaterialExpressionTextureSample>(Expression);
			Sample && Sample->Texture == Texture)
		{
			Samples.Add(Sample);
		}
		if (Expression && Expression->GetFName() == FName(TEXT("EmberTint")))
		{
			Tint = Cast<UMaterialExpressionConstant3Vector>(Expression);
		}
		if (Expression && Expression->GetFName() == FName(TEXT("EmberGlowMultiply")))
		{
			Glow = Cast<UMaterialExpressionMultiply>(Expression);
		}
	}
	const FExpressionInput* Emissive = Material->GetExpressionInputForProperty(MP_EmissiveColor);
	const bool bValid = Material->BlendMode == BLEND_Additive && Material->TwoSided
		&& Samples.Num() == 1 && Samples[0]->SamplerType == SAMPLERTYPE_LinearColor
		&& Tint && Tint->Constant.Equals(FLinearColor(6.0f, 4.0f, 1.5f, 1.0f), 0.001f)
		&& Glow && Glow->A.Expression == Samples[0] && Glow->B.Expression == Tint
		&& Emissive && Emissive->Expression == Glow;
	if (!bValid)
	{
		Run.Errors.Add(TEXT("Generated powder ember material has an unexpected additive/emissive graph."));
	}
	return bValid;
}

UMaterial* CreatePowderEmberMaterial(UTexture2D* Texture, FRun& Run)
{
	UObject* ExistingObject = nullptr;
	if (!HasCompatibleOrMissingAsset(PowderEmberMaterialPath, UMaterial::StaticClass(),
		ExistingObject, Run))
	{
		return nullptr;
	}
	if (ExistingObject)
	{
		UMaterial* Material = CastChecked<UMaterial>(ExistingObject);
		UMaterialExpressionConstant3Vector* ExistingTint = nullptr;
		for (UMaterialExpression* Expression : Material->GetEditorOnlyData()->ExpressionCollection.Expressions)
		{
			if (Expression && Expression->GetFName() == FName(TEXT("EmberTint")))
			{
				ExistingTint = Cast<UMaterialExpressionConstant3Vector>(Expression);
				break;
			}
		}
		const FLinearColor ExpectedEmberTint(6.0f, 4.0f, 1.5f, 1.0f);
		if (ExistingTint && !ExistingTint->Constant.Equals(ExpectedEmberTint, 0.001f))
		{
			Material->Modify();
			ExistingTint->Modify();
			ExistingTint->Constant = ExpectedEmberTint;
			Material->PostEditChange();
			Material->MarkPackageDirty();
			Run.ModifiedAssets.AddUnique(Material->GetOutermost()->GetName());
		}
		return ValidatePowderEmberMaterial(Material, Texture, Run) ? Material : nullptr;
	}
	if (!Texture)
	{
		Run.Errors.Add(TEXT("Cannot create powder ember material without the linear cloud-alpha texture."));
		return nullptr;
	}

	UMaterial* Material = CreateTopLevelAsset<UMaterial>(PowderEmberMaterialPath, Run);
	if (!Material)
	{
		return nullptr;
	}
	Material->PreEditChange(nullptr);
	Material->MaterialDomain = MD_Surface;
	Material->BlendMode = BLEND_Additive;
	Material->TwoSided = true;
	Material->SetShadingModel(MSM_Unlit);
	UMaterialExpressionTextureCoordinate* UV = Cast<UMaterialExpressionTextureCoordinate>(
		AddExpression(*Material, UMaterialExpressionTextureCoordinate::StaticClass(),
			TEXT("EmberUV"), -440, 0));
	UMaterialExpressionTextureSample* Sample = Cast<UMaterialExpressionTextureSample>(
		AddExpression(*Material, UMaterialExpressionTextureSample::StaticClass(),
			TEXT("EmberAlpha"), -210, 0));
	UMaterialExpressionConstant3Vector* Tint = Cast<UMaterialExpressionConstant3Vector>(
		AddExpression(*Material, UMaterialExpressionConstant3Vector::StaticClass(),
			TEXT("EmberTint"), -210, 180));
	UMaterialExpressionMultiply* Glow = Cast<UMaterialExpressionMultiply>(
		AddExpression(*Material, UMaterialExpressionMultiply::StaticClass(),
			TEXT("EmberGlowMultiply"), 210, 0));
	FExpressionInput* Emissive = Material->GetExpressionInputForProperty(MP_EmissiveColor);
	if (!UV || !Sample || !Tint || !Glow || !Emissive)
	{
		Run.Errors.Add(TEXT("Could not build the powder ember emissive material graph."));
		return nullptr;
	}
	Sample->Texture = Texture;
	Sample->SamplerType = SAMPLERTYPE_LinearColor;
	Sample->Coordinates.Connect(0, UV);
	Tint->Constant = FLinearColor(6.0f, 4.0f, 1.5f, 1.0f);
	Glow->A.Connect(4, Sample);
	Glow->B.Connect(0, Tint);
	Emissive->Connect(0, Glow);
	bool bNeedsRecompile = false;
	Material->SetMaterialUsage(bNeedsRecompile, MATUSAGE_ParticleSprites);
	Material->PostEditChange();
	return Material;
}

void SetFloatUniform(FRawDistributionFloat& Raw, UObject& Outer, const float Min, const float Max)
{
	UDistributionFloatUniform* Distribution = NewObject<UDistributionFloatUniform>(
		&Outer, NAME_None, RF_Transactional);
	Distribution->Min = Min;
	Distribution->Max = Max;
	Distribution->bIsDirty = true;
	Raw.Distribution = Distribution;
	Raw.Initialize();
}

void SetVectorUniform(FRawDistributionVector& Raw, UObject& Outer,
	const FVector& Min, const FVector& Max)
{
	UDistributionVectorUniform* Distribution = NewObject<UDistributionVectorUniform>(
		&Outer, NAME_None, RF_Transactional);
	Distribution->Min = Min;
	Distribution->Max = Max;
	Distribution->bIsDirty = true;
	Raw.Distribution = Distribution;
	Raw.Initialize();
}

bool RebuildPowderEmberEmitter(UParticleSystem* System, UMaterial* Material, FRun& Run)
{
	if (!System || !Material || System->Emitters.Num() < 1 || System->Emitters.Num() > 2)
	{
		Run.Errors.Add(TEXT("Powder ember emitter requires a powder system with one existing smoke emitter."));
		return false;
	}
	UParticleSpriteEmitter* Emitter = System->Emitters.Num() == 2
		? Cast<UParticleSpriteEmitter>(System->Emitters[1]) : nullptr;
	UParticleLODLevel* LOD = Emitter && Emitter->LODLevels.Num() == 1
		? Emitter->LODLevels[0] : nullptr;
	if (!Emitter)
	{
		Emitter = NewObject<UParticleSpriteEmitter>(System,
			TEXT("PowderEmberEmitter"), RF_Transactional);
		LOD = NewObject<UParticleLODLevel>(Emitter, TEXT("PowderEmberLOD0"), RF_Transactional);
		if (!Emitter || !LOD)
		{
			Run.Errors.Add(TEXT("Could not allocate the powder ember Cascade emitter."));
			return false;
		}
		Emitter->LODLevels.Add(LOD);
		LOD->Level = 0;
		LOD->RequiredModule = NewObject<UParticleModuleRequired>(System,
			TEXT("PowderEmberRequired"), RF_Transactional);
		LOD->SpawnModule = NewObject<UParticleModuleSpawn>(System,
			TEXT("PowderEmberSpawn"), RF_Transactional);
		if (!LOD->RequiredModule || !LOD->SpawnModule)
		{
			Run.Errors.Add(TEXT("Could not allocate required/spawn modules for powder embers."));
			return false;
		}
		System->Emitters.Add(Emitter);
		Emitter->SetToSensibleDefaults();
	}
	if (!LOD || !LOD->RequiredModule || !LOD->SpawnModule)
	{
		Run.Errors.Add(TEXT("Powder ember emitter has an invalid LOD or required/spawn module."));
		return false;
	}

	Emitter->Modify();
	LOD->Modify();
	LOD->Modules.Reset();
	LOD->TypeDataModule = nullptr;
	Emitter->EmitterName = FName(TEXT("Embers"));
	Emitter->EmitterRenderMode = ERM_Normal;
	LOD->RequiredModule->Material = Material;
	LOD->RequiredModule->bUseLocalSpace = false;
	LOD->RequiredModule->ScreenAlignment = PSA_Square;
	LOD->RequiredModule->EmitterDuration = PowderLifetimeSeconds;
	LOD->RequiredModule->EmitterLoops = 0;
	LOD->RequiredModule->bKillOnDeactivate = false;
	LOD->RequiredModule->bKillOnCompleted = false;
	SetFloatUniform(LOD->SpawnModule->Rate, *LOD->SpawnModule,
		PowderEmberSpawnRate, PowderEmberSpawnRate);
	LOD->SpawnModule->BurstList.Reset();
	FParticleBurst Burst;
	Burst.Count = PowderEmberInitialBurst;
	Burst.CountLow = PowderEmberInitialBurst;
	Burst.Time = 0.0f;
	LOD->SpawnModule->BurstList.Add(Burst);

	UParticleModuleLifetime* Lifetime = NewObject<UParticleModuleLifetime>(System,
		TEXT("PowderEmberLifetime"), RF_Transactional);
	Lifetime->LODValidity = 1;
	SetFloatUniform(Lifetime->Lifetime, *Lifetime,
		PowderEmberParticleLifetimeSeconds * 0.75f,
		PowderEmberParticleLifetimeSeconds * 1.25f);
	LOD->Modules.Add(Lifetime);

	UParticleModuleSize* Size = NewObject<UParticleModuleSize>(System,
		TEXT("PowderEmberSize"), RF_Transactional);
	Size->LODValidity = 1;
	SetVectorUniform(Size->StartSize, *Size,
		FVector(PowderEmberMinSizeCm, PowderEmberMinSizeCm, 1.0f),
		FVector(PowderEmberMaxSizeCm, PowderEmberMaxSizeCm, 1.0f));
	LOD->Modules.Add(Size);

	UParticleModuleColor* Color = NewObject<UParticleModuleColor>(System,
		TEXT("PowderEmberColor"), RF_Transactional);
	Color->LODValidity = 1;
	SetVectorUniform(Color->StartColor, *Color, FVector::OneVector, FVector::OneVector);
	SetFloatUniform(Color->StartAlpha, *Color, 1.0f, 1.0f);
	LOD->Modules.Add(Color);

	UParticleModuleLocationPrimitiveSphere* Location =
		NewObject<UParticleModuleLocationPrimitiveSphere>(System,
			TEXT("PowderEmberScatter"), RF_Transactional);
	Location->LODValidity = 1;
	Location->InitializeDefaults();
	Location->SurfaceOnly = false;
	SetFloatUniform(Location->StartRadius, *Location,
		PowderEmberSpawnRadiusMinCm, PowderEmberSpawnRadiusMaxCm);
	LOD->Modules.Add(Location);

	UParticleModuleVelocity* Velocity = NewObject<UParticleModuleVelocity>(System,
		TEXT("PowderEmberVelocity"), RF_Transactional);
	Velocity->LODValidity = 1;
	Velocity->bInWorldSpace = false;
	Velocity->bApplyOwnerScale = false;
	SetVectorUniform(Velocity->StartVelocity, *Velocity,
		FVector(-18.0f, -18.0f, -8.0f), FVector(18.0f, 18.0f, 28.0f));
	LOD->Modules.Add(Velocity);

	UParticleModuleAcceleration* Gravity = NewObject<UParticleModuleAcceleration>(System,
		TEXT("PowderEmberFall"), RF_Transactional);
	Gravity->LODValidity = 1;
	Gravity->bApplyOwnerScale = false;
	SetVectorUniform(Gravity->Acceleration, *Gravity,
		FVector(-3.0f, -3.0f, -38.0f), FVector(3.0f, 3.0f, -22.0f));
	LOD->Modules.Add(Gravity);

	UParticleModuleRotationRate* RotationRate = NewObject<UParticleModuleRotationRate>(System,
		TEXT("PowderEmberRotation"), RF_Transactional);
	RotationRate->LODValidity = 1;
	SetFloatUniform(RotationRate->StartRotationRate, *RotationRate, -0.8f, 0.8f);
	LOD->Modules.Add(RotationRate);

	LOD->UpdateModuleLists();
	Emitter->UpdateModuleLists();
	return true;
}

bool RebuildCloudParticleSystem(UParticleSystem* System, UMaterial* Material,
	const bool bSurfacePatch, FRun& Run, UMaterial* PowderEmberMaterial = nullptr)
{
	if (!System || !Material || System->Emitters.Num() < 1
		|| System->Emitters.Num() > (bSurfacePatch ? 1 : 2)
		|| (!bSurfacePatch && !PowderEmberMaterial))
	{
		Run.Errors.Add(FString::Printf(TEXT("Generated particle system %s has an unsupported emitter/material layout."),
			*GetPathNameSafe(System)));
		return false;
	}
	UParticleSpriteEmitter* Emitter = Cast<UParticleSpriteEmitter>(System->Emitters[0]);
	if (!Emitter || Emitter->LODLevels.Num() != 1 || !Emitter->LODLevels[0])
	{
		Run.Errors.Add(FString::Printf(TEXT("Generated particle system %s has an unsupported emitter/LOD layout."),
			*System->GetPathName()));
		return false;
	}
	UParticleLODLevel* LOD = Emitter->LODLevels[0];
	if (!LOD->RequiredModule || !LOD->SpawnModule)
	{
		Run.Errors.Add(FString::Printf(TEXT("Generated particle system %s is missing required Cascade modules."),
			*System->GetPathName()));
		return false;
	}

	System->Modify();
	Emitter->Modify();
	LOD->Modify();
	LOD->Modules.Reset();
	LOD->TypeDataModule = nullptr;
	Emitter->EmitterName = FName(TEXT("Cloud"));
	Emitter->EmitterRenderMode = ERM_Normal;
	LOD->RequiredModule->Material = Material;
	LOD->RequiredModule->bUseLocalSpace = bSurfacePatch;
	// PSA_TypeSpecific forces Cascade mesh particles to face the camera. Keep this
	// mesh in particle-local orientation so its local Z follows the hit surface.
	LOD->RequiredModule->ScreenAlignment = PSA_Square;
	LOD->RequiredModule->EmitterDuration = bSurfacePatch ? MarkLifetimeSeconds : PowderLifetimeSeconds;
	LOD->RequiredModule->EmitterLoops = 0;
	LOD->RequiredModule->bKillOnDeactivate = false;
	LOD->RequiredModule->bKillOnCompleted = false;
	const float SpawnRate = bSurfacePatch ? MarkParticleSpawnRate : PowderParticleSpawnRate;
	SetFloatUniform(LOD->SpawnModule->Rate, *LOD->SpawnModule, SpawnRate, SpawnRate);
	LOD->SpawnModule->BurstList.Reset();
	FParticleBurst Burst;
	Burst.Count = bSurfacePatch ? MarkInitialParticleBurst : PowderInitialParticleBurst;
	Burst.CountLow = Burst.Count;
	Burst.Time = 0.0f;
	LOD->SpawnModule->BurstList.Add(Burst);

	UParticleModuleLifetime* Lifetime = NewObject<UParticleModuleLifetime>(System,
		TEXT("CloudLifetime"), RF_Transactional);
	Lifetime->LODValidity = 1;
	const float LifetimeSeconds = bSurfacePatch
		? MarkParticleLifetimeSeconds : PowderParticleLifetimeSeconds;
	SetFloatUniform(Lifetime->Lifetime, *Lifetime, LifetimeSeconds, LifetimeSeconds);
	LOD->Modules.Add(Lifetime);

	UParticleModuleSize* Size = NewObject<UParticleModuleSize>(System,
		TEXT("CloudInitialSize"), RF_Transactional);
	Size->LODValidity = 1;
	if (bSurfacePatch)
	{
		// The sphere mesh is 100 cm across, so scale all axes equally to retain
		// true 3D mote thickness instead of flattening Z to a 1 cm sheet.
		SetVectorUniform(Size->StartSize, *Size,
			FVector(MarkMoteMinSizeCm / 100.0f, MarkMoteMinSizeCm / 100.0f, MarkMoteMinSizeCm / 100.0f),
			FVector(MarkMoteMaxSizeCm / 100.0f, MarkMoteMaxSizeCm / 100.0f, MarkMoteMaxSizeCm / 100.0f));
	}
	else
	{
		SetVectorUniform(Size->StartSize, *Size,
			FVector(PowderSmokeMinSizeCm, PowderSmokeMinSizeCm, 1.0f),
			FVector(PowderSmokeMaxSizeCm, PowderSmokeMaxSizeCm, 1.0f));
	}
	LOD->Modules.Add(Size);
	UParticleModuleColor* Color = NewObject<UParticleModuleColor>(System,
		TEXT("CloudInitialColor"), RF_Transactional);
	Color->LODValidity = 1;
	SetVectorUniform(Color->StartColor, *Color, FVector::OneVector, FVector::OneVector);
	// Opacity is authored in the material because mesh-particle vertex alpha is not
	// available through Particle Color in every target shader permutation.
	SetFloatUniform(Color->StartAlpha, *Color, 1.0f, 1.0f);
	LOD->Modules.Add(Color);

	UParticleModuleVelocity* Velocity = NewObject<UParticleModuleVelocity>(System,
		TEXT("CloudDriftVelocity"), RF_Transactional);
	Velocity->LODValidity = 1;
	Velocity->bInWorldSpace = false;
	Velocity->bApplyOwnerScale = false;
	SetVectorUniform(Velocity->StartVelocity, *Velocity,
		bSurfacePatch ? FVector(-4.5f, -4.5f, -4.5f) : FVector(-7.0f, -7.0f, -7.0f),
		bSurfacePatch ? FVector(4.5f, 4.5f, 4.5f) : FVector(7.0f, 7.0f, 7.0f));
	LOD->Modules.Add(Velocity);

	UParticleModuleAcceleration* Acceleration = NewObject<UParticleModuleAcceleration>(System,
		TEXT("CloudTurbulence"), RF_Transactional);
	Acceleration->LODValidity = 1;
	Acceleration->bApplyOwnerScale = false;
	SetVectorUniform(Acceleration->Acceleration, *Acceleration,
		bSurfacePatch ? FVector(-2.0f, -2.0f, -2.0f) : FVector(-4.0f, -4.0f, -4.0f),
		bSurfacePatch ? FVector(2.0f, 2.0f, 2.0f) : FVector(4.0f, 4.0f, 4.0f));
	LOD->Modules.Add(Acceleration);

	if (bSurfacePatch)
	{
		UParticleModuleLocationPrimitiveSphere* Location =
			NewObject<UParticleModuleLocationPrimitiveSphere>(System,
			TEXT("CloudMarkVolume"), RF_Transactional);
		Location->InitializeDefaults();
		Location->LODValidity = 1;
		Location->SurfaceOnly = false;
		Location->Velocity = false;
		SetFloatUniform(Location->StartRadius, *Location, MarkMoteRadiusCm, MarkMoteRadiusCm);
		SetVectorUniform(Location->StartLocation, *Location, FVector::ZeroVector, FVector::ZeroVector);
		LOD->Modules.Add(Location);

		UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr,
			TEXT("/Engine/BasicShapes/Sphere.Sphere"), nullptr, LOAD_NoWarn);
		UParticleModuleTypeDataMesh* MeshData = NewObject<UParticleModuleTypeDataMesh>(System,
			TEXT("CloudMeshData"), RF_Transactional);
		if (!Sphere || !MeshData)
		{
			Run.Errors.Add(TEXT("Engine BasicShapes/Sphere or Cascade Mesh Data module is unavailable."));
			return false;
		}
		MeshData->Mesh = Sphere;
		MeshData->bOverrideMaterial = true;
		MeshData->bCameraFacing = false;
		MeshData->AxisLockOption = EPAL_NONE;
		MeshData->CastShadows = false;
		MeshData->DoCollisions = false;
		MeshData->bUseStaticMeshLODs = true;
		LOD->TypeDataModule = MeshData;

		UParticleModuleMeshRotation* Rotation = NewObject<UParticleModuleMeshRotation>(System,
			TEXT("CloudSurfaceOrientation"), RF_Transactional);
		Rotation->LODValidity = 1;
		// Local-space mesh rendering already applies the component transform;
		// spherical motes receive mild randomized 3D rotation.
		Rotation->bInheritParent = false;
		SetVectorUniform(Rotation->StartRotation, *Rotation,
			FVector::ZeroVector, FVector::ZeroVector);
		LOD->Modules.Add(Rotation);

		UParticleModuleMeshRotationRate* RotationRate =
			NewObject<UParticleModuleMeshRotationRate>(System,
				TEXT("CloudSurfaceSwirl"), RF_Transactional);
		RotationRate->LODValidity = 1;
		SetVectorUniform(RotationRate->StartRotationRate, *RotationRate,
			FVector(-0.30f, -0.30f, -0.30f), FVector(0.30f, 0.30f, 0.30f));
		LOD->Modules.Add(RotationRate);
	}
	else
	{
		UParticleModuleLocationPrimitiveSphere* Location =
			NewObject<UParticleModuleLocationPrimitiveSphere>(System,
				TEXT("CloudPowderVolume"), RF_Transactional);
		Location->LODValidity = 1;
		Location->InitializeDefaults();
		// Populate the whole volume so the powder cloud reads as a filled mote cluster, not a hollow shell.
		Location->SurfaceOnly = false;
		SetFloatUniform(Location->StartRadius, *Location, 0.0f, PowderSmokeSpawnRadiusCm);
		LOD->Modules.Add(Location);

		UParticleModuleRotationRate* RotationRate =
			NewObject<UParticleModuleRotationRate>(System,
				TEXT("CloudPowderSwirl"), RF_Transactional);
		RotationRate->LODValidity = 1;
		SetFloatUniform(RotationRate->StartRotationRate, *RotationRate, -0.5f, 0.5f);
		LOD->Modules.Add(RotationRate);
	}

	if (!bSurfacePatch && !RebuildPowderEmberEmitter(System, PowderEmberMaterial, Run))
	{
		return false;
	}

	LOD->UpdateModuleLists();
	System->UpdateAllModuleLists();
	System->PostEditChange();
	System->MarkPackageDirty();
	return true;
}

UParticleSystem* CreateParticleSystem(const TCHAR* PackagePath, UMaterial* Material,
	const bool bSurfacePatch, FRun& Run, UMaterial* PowderEmberMaterial = nullptr)
{
	if (!Material || (!bSurfacePatch && !PowderEmberMaterial))
	{
		Run.Errors.Add(FString::Printf(TEXT("Cannot create or repair particle system without material: %s"), PackagePath));
		return nullptr;
	}
	UObject* ExistingObject = nullptr;
	if (!HasCompatibleOrMissingAsset(PackagePath, UParticleSystem::StaticClass(), ExistingObject, Run))
	{
		return nullptr;
	}
	if (ExistingObject)
	{
		UParticleSystem* ExistingSystem = CastChecked<UParticleSystem>(ExistingObject);
		bool bRepairedAsset = false;
		// The first generator run created Cascade modules under the emitter. UE 5.6
		// requires UParticleModule objects to be outered to their UParticleSystem.
		// Repair only assets carrying this commandlet's ownership marker.
		for (UParticleEmitter* Emitter : ExistingSystem->Emitters)
		{
			if (!Emitter || Emitter->GetOuter() != ExistingSystem)
			{
				Run.Errors.Add(FString::Printf(TEXT("Generated particle system %s has an unexpected emitter outer."),
					*ExistingSystem->GetPathName()));
				return nullptr;
			}
			for (UParticleLODLevel* LOD : Emitter->LODLevels)
			{
				if (!LOD)
				{
					Run.Errors.Add(FString::Printf(TEXT("Generated particle system %s contains a null LOD."),
						*ExistingSystem->GetPathName()));
					return nullptr;
				}
				TArray<UObject*> ModuleObjects;
				ModuleObjects.Add(LOD->RequiredModule.Get());
				ModuleObjects.Add(LOD->SpawnModule.Get());
				ModuleObjects.Add(LOD->TypeDataModule.Get());
				for (UParticleModule* Module : LOD->Modules)
				{
					ModuleObjects.Add(Module);
				}
				for (UObject* ModuleObject : ModuleObjects)
				{
					if (!ModuleObject)
					{
						continue;
					}
					if (UParticleModuleRotation* RotationModule = Cast<UParticleModuleRotation>(ModuleObject))
					{
						if (UDistributionFloatUniform* Distribution =
							Cast<UDistributionFloatUniform>(RotationModule->StartRotation.Distribution))
						{
							// Cascade stores rotation as turns (1 == 360 degrees); only
							// migrate the exact radians range emitted by this tool's first run.
							if (FMath::IsNearlyZero(Distribution->Min)
								&& FMath::IsNearlyEqual(Distribution->Max, 2.0f * PI, 0.001f))
							{
								Distribution->Modify();
								Distribution->Max = 1.0f;
								Distribution->bIsDirty = true;
								RotationModule->StartRotation.Initialize();
								ExistingSystem->MarkPackageDirty();
								bRepairedAsset = true;
							}
						}
					}
					if (ModuleObject->GetOuter() == ExistingSystem)
					{
						continue;
					}
					if (ModuleObject->GetOuter() != Emitter)
					{
						Run.Errors.Add(FString::Printf(TEXT("Generated module %s has an unexpected outer %s."),
							*ModuleObject->GetPathName(), *GetPathNameSafe(ModuleObject->GetOuter())));
						return nullptr;
					}
					const FName SuggestedName(*FString::Printf(TEXT("%s_%s"),
						*Emitter->GetName(), *ModuleObject->GetName()));
					const FName UniqueName = MakeUniqueObjectName(
						ExistingSystem, ModuleObject->GetClass(), SuggestedName);
					if (!ModuleObject->Rename(*UniqueName.ToString(), ExistingSystem,
						REN_DontCreateRedirectors | REN_NonTransactional))
					{
						Run.Errors.Add(FString::Printf(TEXT("Could not repair outer for generated module %s."),
							*ModuleObject->GetPathName()));
						return nullptr;
					}
					ExistingSystem->MarkPackageDirty();
					bRepairedAsset = true;
				}
				LOD->UpdateModuleLists();
			}
		}
		ExistingSystem->UpdateAllModuleLists();
		if (bRepairedAsset)
		{
			Run.ModifiedAssets.AddUnique(ExistingSystem->GetOutermost()->GetName());
		}
		if (!RebuildCloudParticleSystem(ExistingSystem, Material, bSurfacePatch, Run,
			PowderEmberMaterial))
		{
			return nullptr;
		}
		Run.ModifiedAssets.AddUnique(ExistingSystem->GetOutermost()->GetName());
		return ExistingSystem;
	}

	UParticleSystem* System = CreateTopLevelAsset<UParticleSystem>(PackagePath, Run);
	if (!System)
	{
		return nullptr;
	}
	UParticleSpriteEmitter* Emitter = NewObject<UParticleSpriteEmitter>(System,
		TEXT("CloudEmitter"), RF_Transactional);
	UParticleLODLevel* LOD = NewObject<UParticleLODLevel>(Emitter,
		TEXT("CloudLOD0"), RF_Transactional);
	if (!Emitter || !LOD)
	{
		Run.Errors.Add(FString::Printf(TEXT("Could not create Cascade emitter for %s"), PackagePath));
		return nullptr;
	}
	System->Emitters.Add(Emitter);
	Emitter->LODLevels.Add(LOD);
	LOD->Level = 0;
	LOD->RequiredModule = NewObject<UParticleModuleRequired>(System,
		TEXT("Required"), RF_Transactional);
	LOD->SpawnModule = NewObject<UParticleModuleSpawn>(System,
		TEXT("Spawn"), RF_Transactional);
	if (!LOD->RequiredModule || !LOD->SpawnModule)
	{
		Run.Errors.Add(FString::Printf(TEXT("Could not create required Cascade modules for %s"), PackagePath));
		return nullptr;
	}
	Emitter->SetToSensibleDefaults();
	Emitter->EmitterName = FName(TEXT("Cloud"));
	Emitter->EmitterRenderMode = ERM_Normal;
	LOD->RequiredModule->Material = Material;
	LOD->RequiredModule->bUseLocalSpace = bSurfacePatch;
	LOD->RequiredModule->ScreenAlignment = bSurfacePatch ? PSA_TypeSpecific : PSA_Square;
	LOD->RequiredModule->EmitterDuration = bSurfacePatch ? MarkLifetimeSeconds : PowderLifetimeSeconds;
	LOD->RequiredModule->EmitterLoops = 0;
	LOD->RequiredModule->bKillOnDeactivate = false;
	LOD->RequiredModule->bKillOnCompleted = false;

	const float SpawnRate = bSurfacePatch ? MarkParticleSpawnRate : PowderParticleSpawnRate;
	SetFloatUniform(LOD->SpawnModule->Rate, *LOD->SpawnModule, SpawnRate, SpawnRate);
	LOD->SpawnModule->BurstList.Reset();
	FParticleBurst Burst;
	Burst.Count = bSurfacePatch ? MarkInitialParticleBurst : PowderInitialParticleBurst;
	Burst.CountLow = Burst.Count;
	Burst.Time = 0.0f;
	LOD->SpawnModule->BurstList.Add(Burst);

	for (int32 Index = LOD->Modules.Num() - 1; Index >= 0; --Index)
	{
		UParticleModule* Module = LOD->Modules[Index];
		if (Module && (Module->IsA(UParticleModuleColorOverLife::StaticClass())
			|| Module->IsA(UParticleModuleVelocity::StaticClass())))
		{
			LOD->Modules.RemoveAt(Index);
		}
	}

	UParticleModuleLifetime* Lifetime = NewObject<UParticleModuleLifetime>(System,
		TEXT("Lifetime"), RF_Transactional);
	Lifetime->LODValidity = 1;
	SetFloatUniform(Lifetime->Lifetime, *Lifetime,
		bSurfacePatch ? MarkParticleLifetimeSeconds : PowderParticleLifetimeSeconds,
		bSurfacePatch ? MarkParticleLifetimeSeconds : PowderParticleLifetimeSeconds);
	LOD->Modules.Add(Lifetime);

	UParticleModuleSize* Size = NewObject<UParticleModuleSize>(System,
		TEXT("InitialSize"), RF_Transactional);
	Size->LODValidity = 1;
	if (bSurfacePatch)
	{
		// The Engine BasicShapes sphere is 100 cm across; use isotropic scale for volumetric motes.
		SetVectorUniform(Size->StartSize, *Size,
			FVector(MarkMoteMinSizeCm / 100.0f, MarkMoteMinSizeCm / 100.0f, MarkMoteMinSizeCm / 100.0f),
			FVector(MarkMoteMaxSizeCm / 100.0f, MarkMoteMaxSizeCm / 100.0f, MarkMoteMaxSizeCm / 100.0f));
	}
	else
	{
		SetVectorUniform(Size->StartSize, *Size,
			FVector(1.5f, 1.5f, 1.0f), FVector(2.5f, 2.5f, 1.0f));
	}
	LOD->Modules.Add(Size);

	if (bSurfacePatch)
	{
		UParticleModuleLocationPrimitiveSphere* Location =
			NewObject<UParticleModuleLocationPrimitiveSphere>(System,
			TEXT("MarkVolumeSpread"), RF_Transactional);
		Location->InitializeDefaults();
		Location->LODValidity = 1;
		Location->SurfaceOnly = false;
		Location->Velocity = false;
		SetFloatUniform(Location->StartRadius, *Location, MarkMoteRadiusCm, MarkMoteRadiusCm);
		SetVectorUniform(Location->StartLocation, *Location, FVector::ZeroVector, FVector::ZeroVector);
		LOD->Modules.Add(Location);

		UParticleModuleRotation* Rotation = NewObject<UParticleModuleRotation>(System,
			TEXT("PatchRotation"), RF_Transactional);
		Rotation->LODValidity = 1;
		SetFloatUniform(Rotation->StartRotation, *Rotation, 0.0f, 1.0f);
		LOD->Modules.Add(Rotation);

		UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr,
			TEXT("/Engine/BasicShapes/Sphere.Sphere"), nullptr, LOAD_NoWarn);
		UParticleModuleTypeDataMesh* MeshData = NewObject<UParticleModuleTypeDataMesh>(System,
			TEXT("MeshData"), RF_Transactional);
		if (!Sphere || !MeshData)
		{
			Run.Errors.Add(TEXT("Engine BasicShapes/Sphere or Cascade Mesh Data module is unavailable."));
			return nullptr;
		}
		MeshData->Mesh = Sphere;
		MeshData->bOverrideMaterial = true;
		MeshData->bCameraFacing = false;
		MeshData->AxisLockOption = EPAL_NONE;
		MeshData->CastShadows = false;
		MeshData->DoCollisions = false;
		MeshData->bUseStaticMeshLODs = true;
		LOD->TypeDataModule = MeshData;
	}
	else
	{
		UParticleModuleLocationPrimitiveSphere* Location =
			NewObject<UParticleModuleLocationPrimitiveSphere>(System,
				TEXT("PowderVolume"), RF_Transactional);
		Location->LODValidity = 1;
		Location->InitializeDefaults();
		// Populate the whole volume so the powder cloud reads as a filled mote cluster, not a hollow shell.
		Location->SurfaceOnly = false;
		SetFloatUniform(Location->StartRadius, *Location, 0.0f, PowderSmokeSpawnRadiusCm);
		LOD->Modules.Add(Location);
	}

	LOD->UpdateModuleLists();
	System->UpdateAllModuleLists();
	System->PostEditChange();
	if (!RebuildCloudParticleSystem(System, Material, bSurfacePatch, Run,
		PowderEmberMaterial))
	{
		return nullptr;
	}
	return System;
}

bool ValidateParticleSystemModuleOuters(UParticleSystem* System, FRun& Run)
{
	if (!System || System->Emitters.IsEmpty())
	{
		Run.Errors.Add(TEXT("Generated particle system is missing its emitter."));
		return false;
	}
	bool bValid = true;
	auto CheckModuleOuter = [&System, &Run, &bValid](UObject* ModuleObject)
	{
		if (ModuleObject && ModuleObject->GetOuter() != System)
		{
			Run.Errors.Add(FString::Printf(TEXT("Particle module %s must be outered to %s, got %s."),
				*ModuleObject->GetPathName(), *System->GetPathName(),
				*GetPathNameSafe(ModuleObject->GetOuter())));
			bValid = false;
		}
	};
	for (UParticleEmitter* Emitter : System->Emitters)
	{
		if (!Emitter || Emitter->GetOuter() != System || Emitter->LODLevels.IsEmpty())
		{
			Run.Errors.Add(FString::Printf(TEXT("Particle system %s has an invalid emitter/LOD structure."),
				*System->GetPathName()));
			bValid = false;
			continue;
		}
		for (UParticleLODLevel* LOD : Emitter->LODLevels)
		{
			if (!LOD || !LOD->RequiredModule || !LOD->SpawnModule)
			{
				Run.Errors.Add(FString::Printf(TEXT("Particle system %s has a missing required or spawn module."),
					*System->GetPathName()));
				bValid = false;
				continue;
			}
			CheckModuleOuter(LOD->RequiredModule.Get());
			CheckModuleOuter(LOD->SpawnModule.Get());
			CheckModuleOuter(LOD->TypeDataModule.Get());
			for (UParticleModule* Module : LOD->Modules)
			{
				CheckModuleOuter(Module);
			}
		}
	}
	return bValid;
}

bool ValidatePowderEmberEmitter(UParticleSystem* System, FRun& Run)
{
	if (!System || System->Emitters.Num() != 2)
	{
		Run.Errors.Add(TEXT("Powder particle system must have a smoke emitter and a separate ember emitter."));
		return false;
	}
	const UParticleSpriteEmitter* Emitter = Cast<UParticleSpriteEmitter>(System->Emitters[1]);
	const UParticleLODLevel* LOD = Emitter && Emitter->LODLevels.Num() == 1
		? Emitter->LODLevels[0] : nullptr;
	if (!LOD || !LOD->RequiredModule || !LOD->SpawnModule)
	{
		Run.Errors.Add(TEXT("Powder ember emitter has invalid required/spawn modules."));
		return false;
	}
	bool bValid = true;
	auto Require = [&Run, &System, &bValid](const bool bCondition, const TCHAR* Message)
	{
		if (!bCondition)
		{
			Run.Errors.Add(FString::Printf(TEXT("%s: %s"), *System->GetPathName(), Message));
			bValid = false;
		}
	};
	Require(Emitter->EmitterName == FName(TEXT("Embers"))
		&& LOD->RequiredModule->Material
		&& LOD->RequiredModule->Material->GetPathName() == ObjectPath(PowderEmberMaterialPath)
		&& !LOD->RequiredModule->bUseLocalSpace
		&& LOD->RequiredModule->ScreenAlignment == PSA_Square
		&& LOD->RequiredModule->EmitterLoops == 0
		&& FMath::IsNearlyEqual(LOD->RequiredModule->EmitterDuration, PowderLifetimeSeconds),
		TEXT("ember emitter must use its additive material and continuously follow the powder actor."));
	const UDistributionFloatUniform* SpawnRate =
		Cast<UDistributionFloatUniform>(LOD->SpawnModule->Rate.Distribution);
	Require(SpawnRate && FMath::IsNearlyEqual(SpawnRate->Min, PowderEmberSpawnRate)
		&& FMath::IsNearlyEqual(SpawnRate->Max, PowderEmberSpawnRate)
		&& LOD->SpawnModule->BurstList.Num() == 1
		&& LOD->SpawnModule->BurstList[0].Count == PowderEmberInitialBurst
		&& LOD->SpawnModule->BurstList[0].CountLow == PowderEmberInitialBurst
		&& FMath::IsNearlyZero(LOD->SpawnModule->BurstList[0].Time),
		TEXT("ember emitter is missing its sparse, continuous spawn and initial burst."));
	Require(LOD->Modules.Num() == 7,
		TEXT("ember emitter needs lifetime, size, color, scatter, velocity, fall, and rotation modules."));

	int32 LifetimeCount = 0;
	int32 SizeCount = 0;
	int32 ColorCount = 0;
	int32 LocationCount = 0;
	int32 VelocityCount = 0;
	int32 AccelerationCount = 0;
	int32 RotationRateCount = 0;
	for (const UParticleModule* Module : LOD->Modules)
	{
		if (const UParticleModuleLifetime* Lifetime = Cast<UParticleModuleLifetime>(Module))
		{
			++LifetimeCount;
			const UDistributionFloatUniform* Distribution =
				Cast<UDistributionFloatUniform>(Lifetime->Lifetime.Distribution);
			Require(Distribution
				&& FMath::IsNearlyEqual(Distribution->Min, PowderEmberParticleLifetimeSeconds * 0.75f)
				&& FMath::IsNearlyEqual(Distribution->Max, PowderEmberParticleLifetimeSeconds * 1.25f),
				TEXT("ember lifetime should vary around the short-lived reference sparks."));
		}
		else if (const UParticleModuleSize* Size = Cast<UParticleModuleSize>(Module))
		{
			++SizeCount;
			const UDistributionVectorUniform* Distribution =
				Cast<UDistributionVectorUniform>(Size->StartSize.Distribution);
			Require(Distribution
				&& Distribution->Min.Equals(FVector(PowderEmberMinSizeCm, PowderEmberMinSizeCm, 1.0f))
				&& Distribution->Max.Equals(FVector(PowderEmberMaxSizeCm, PowderEmberMaxSizeCm, 1.0f)),
				TEXT("ember size is outside its small bright mote range."));
		}
		else if (const UParticleModuleColor* Color = Cast<UParticleModuleColor>(Module))
		{
			++ColorCount;
			const UDistributionVectorUniform* RGB =
				Cast<UDistributionVectorUniform>(Color->StartColor.Distribution);
			const UDistributionFloatUniform* Alpha =
				Cast<UDistributionFloatUniform>(Color->StartAlpha.Distribution);
			Require(RGB && RGB->Min.Equals(FVector::OneVector) && RGB->Max.Equals(FVector::OneVector)
				&& Alpha && FMath::IsNearlyEqual(Alpha->Min, 1.0f)
				&& FMath::IsNearlyEqual(Alpha->Max, 1.0f),
				TEXT("ember particles must preserve the authored emissive color and alpha."));
		}
		else if (const UParticleModuleLocationPrimitiveSphere* Location =
			Cast<UParticleModuleLocationPrimitiveSphere>(Module))
		{
			++LocationCount;
			const UDistributionFloatUniform* Radius =
				Cast<UDistributionFloatUniform>(Location->StartRadius.Distribution);
			Require(!Location->SurfaceOnly && Radius
				&& FMath::IsNearlyEqual(Radius->Min, PowderEmberSpawnRadiusMinCm)
				&& FMath::IsNearlyEqual(Radius->Max, PowderEmberSpawnRadiusMaxCm),
				TEXT("embers must scatter through a filled outer volume around the smoke."));
		}
		else if (const UParticleModuleVelocity* Velocity = Cast<UParticleModuleVelocity>(Module))
		{
			++VelocityCount;
			const UDistributionVectorUniform* Distribution =
				Cast<UDistributionVectorUniform>(Velocity->StartVelocity.Distribution);
			Require(Distribution
				&& Distribution->Min.Equals(FVector(-18.0f, -18.0f, -8.0f))
				&& Distribution->Max.Equals(FVector(18.0f, 18.0f, 28.0f)),
				TEXT("ember velocity must spread small sparks away from the impact cloud."));
		}
		else if (const UParticleModuleAcceleration* Acceleration =
			Cast<UParticleModuleAcceleration>(Module))
		{
			++AccelerationCount;
			const UDistributionVectorUniform* Distribution =
				Cast<UDistributionVectorUniform>(Acceleration->Acceleration.Distribution);
			Require(Distribution
				&& Distribution->Min.Equals(FVector(-3.0f, -3.0f, -38.0f))
				&& Distribution->Max.Equals(FVector(3.0f, 3.0f, -22.0f)),
				TEXT("ember particles must fall away instead of remaining as a static ring."));
		}
		else if (Module && Module->IsA<UParticleModuleRotationRate>())
		{
			++RotationRateCount;
		}
	}
	Require(LifetimeCount == 1 && SizeCount == 1 && ColorCount == 1
		&& LocationCount == 1 && VelocityCount == 1 && AccelerationCount == 1
		&& RotationRateCount == 1,
		TEXT("ember emitter has duplicate or missing behavior modules."));
	Require(LOD->TypeDataModule == nullptr,
		TEXT("ember layer should remain lightweight camera-facing sprite particles."));
	return bValid;
}

bool ValidateCloudParticleSystem(UParticleSystem* System, const bool bSurfacePatch,
	FRun& Run)
{
	if (!System || System->Emitters.Num() != (bSurfacePatch ? 1 : 2))
	{
		Run.Errors.Add(TEXT("Cloud particle system has an unexpected emitter count."));
		return false;
	}
	const UParticleSpriteEmitter* Emitter = Cast<UParticleSpriteEmitter>(System->Emitters[0]);
	const UParticleLODLevel* LOD = Emitter && Emitter->LODLevels.Num() == 1
		? Emitter->LODLevels[0] : nullptr;
	if (!LOD || !LOD->RequiredModule || !LOD->SpawnModule)
	{
		Run.Errors.Add(FString::Printf(TEXT("Cloud particle system %s has invalid required/spawn modules."),
			*System->GetPathName()));
		return false;
	}

	bool bValid = true;
	auto Require = [&Run, &System, &bValid](const bool bCondition, const TCHAR* Message)
	{
		if (!bCondition)
		{
			Run.Errors.Add(FString::Printf(TEXT("%s: %s"), *System->GetPathName(), Message));
			bValid = false;
		}
	};
	Require(LOD->RequiredModule->ScreenAlignment == PSA_Square,
		TEXT("ScreenAlignment must not force mesh particles to face the camera."));
	Require(LOD->RequiredModule->bUseLocalSpace == bSurfacePatch,
		TEXT("particle local-space setting does not match its surface/sprite role."));
	Require(LOD->RequiredModule->Material != nullptr,
		TEXT("Required module has no material."));
	const float ExpectedSystemLifetime = bSurfacePatch ? MarkLifetimeSeconds : PowderLifetimeSeconds;
	const float ExpectedParticleLifetime = bSurfacePatch
		? MarkParticleLifetimeSeconds : PowderParticleLifetimeSeconds;
	const float ExpectedSpawnRate = bSurfacePatch ? MarkParticleSpawnRate : PowderParticleSpawnRate;
	const int32 ExpectedBurstCount = bSurfacePatch
		? MarkInitialParticleBurst : PowderInitialParticleBurst;
	Require(LOD->RequiredModule->EmitterLoops == 0
		&& FMath::IsNearlyEqual(LOD->RequiredModule->EmitterDuration, ExpectedSystemLifetime),
		TEXT("emitter must loop continuously for the owning mark/powder lifetime."));
	const UDistributionFloatUniform* SpawnRate =
		Cast<UDistributionFloatUniform>(LOD->SpawnModule->Rate.Distribution);
	Require(SpawnRate && FMath::IsNearlyEqual(SpawnRate->Min, ExpectedSpawnRate)
		&& FMath::IsNearlyEqual(SpawnRate->Max, ExpectedSpawnRate)
		&& LOD->SpawnModule->BurstList.Num() == 1
		&& LOD->SpawnModule->BurstList[0].Count == ExpectedBurstCount
		&& LOD->SpawnModule->BurstList[0].CountLow == ExpectedBurstCount
		&& FMath::IsNearlyZero(LOD->SpawnModule->BurstList[0].Time),
		TEXT("emitter must have the expected continuous rate and single initial burst."));
	Require(LOD->Modules.Num() == (bSurfacePatch ? 8 : 7),
		TEXT("expected lifetime, size, color, location, drift, turbulence, and orientation modules."));

	int32 LifetimeCount = 0;
	int32 SizeCount = 0;
	int32 LocationCount = 0;
	int32 MeshRotationCount = 0;
	int32 VelocityCount = 0;
	int32 AccelerationCount = 0;
	int32 RotationRateCount = 0;
	int32 ColorCount = 0;
	for (const UParticleModule* Module : LOD->Modules)
	{
		LifetimeCount += Module && Module->IsA<UParticleModuleLifetime>() ? 1 : 0;
		SizeCount += Module && Module->IsA<UParticleModuleSize>() ? 1 : 0;
		LocationCount += Module && (Module->IsA<UParticleModuleLocation>()
			|| Module->IsA<UParticleModuleLocationPrimitiveSphere>()
			|| Module->IsA<UParticleModuleLocationPrimitiveCylinder>()) ? 1 : 0;
		MeshRotationCount += Module && Module->IsA<UParticleModuleMeshRotation>() ? 1 : 0;
		VelocityCount += Module && Module->IsA<UParticleModuleVelocity>() ? 1 : 0;
		AccelerationCount += Module && Module->IsA<UParticleModuleAcceleration>() ? 1 : 0;
		RotationRateCount += Module && (Module->IsA<UParticleModuleRotationRate>()
			|| Module->IsA<UParticleModuleMeshRotationRate>()) ? 1 : 0;
		if (const UParticleModuleLifetime* Lifetime = Cast<UParticleModuleLifetime>(Module))
		{
			const UDistributionFloatUniform* Distribution =
				Cast<UDistributionFloatUniform>(Lifetime->Lifetime.Distribution);
			Require(Distribution && FMath::IsNearlyEqual(Distribution->Min, ExpectedParticleLifetime)
				&& FMath::IsNearlyEqual(Distribution->Max, ExpectedParticleLifetime),
				TEXT("particle lifetime does not match the animated cloud setup."));
		}
		if (const UParticleModuleVelocity* Velocity = Cast<UParticleModuleVelocity>(Module))
		{
			const UDistributionVectorUniform* Distribution =
				Cast<UDistributionVectorUniform>(Velocity->StartVelocity.Distribution);
			const FVector ExpectedMin = bSurfacePatch
				? FVector(-4.5f, -4.5f, -4.5f) : FVector(-7.0f, -7.0f, -7.0f);
			const FVector ExpectedMax = bSurfacePatch
				? FVector(4.5f, 4.5f, 4.5f) : FVector(7.0f, 7.0f, 7.0f);
			Require(Distribution && Distribution->Min.Equals(ExpectedMin)
				&& Distribution->Max.Equals(ExpectedMax),
				TEXT("random drift velocity distribution is missing or out of range."));
		}
		if (const UParticleModuleAcceleration* Acceleration = Cast<UParticleModuleAcceleration>(Module))
		{
			const UDistributionVectorUniform* Distribution =
				Cast<UDistributionVectorUniform>(Acceleration->Acceleration.Distribution);
			const FVector ExpectedMin = bSurfacePatch
				? FVector(-2.0f, -2.0f, -2.0f) : FVector(-4.0f, -4.0f, -4.0f);
			const FVector ExpectedMax = bSurfacePatch
				? FVector(2.0f, 2.0f, 2.0f) : FVector(4.0f, 4.0f, 4.0f);
			Require(Distribution && Distribution->Min.Equals(ExpectedMin)
				&& Distribution->Max.Equals(ExpectedMax),
				TEXT("random turbulence acceleration distribution is missing or out of range."));
		}
		if (const UParticleModuleLocationPrimitiveSphere* SphereLocation =
			Cast<UParticleModuleLocationPrimitiveSphere>(Module))
		{
			const UDistributionFloatUniform* Radius =
				Cast<UDistributionFloatUniform>(SphereLocation->StartRadius.Distribution);
			if (bSurfacePatch)
			{
				Require(!SphereLocation->SurfaceOnly && Radius
					&& FMath::IsNearlyEqual(Radius->Min, MarkMoteRadiusCm)
					&& FMath::IsNearlyEqual(Radius->Max, MarkMoteRadiusCm),
					TEXT("mark motes must occupy a filled 3D sphere volume matching the 1 m mark envelope."));
			}
			else
			{
				Require(!SphereLocation->SurfaceOnly && Radius
					&& FMath::IsNearlyZero(Radius->Min)
					&& FMath::IsNearlyEqual(Radius->Max, PowderSmokeSpawnRadiusCm),
					TEXT("powder motes must fill the 75 cm visual volume instead of forming a hollow shell."));
			}
		}
		if (const UParticleModuleColor* Color = Cast<UParticleModuleColor>(Module))
		{
			++ColorCount;
			const UDistributionVectorUniform* RGB = Cast<UDistributionVectorUniform>(Color->StartColor.Distribution);
			const UDistributionFloatUniform* Alpha = Cast<UDistributionFloatUniform>(Color->StartAlpha.Distribution);
			const float ExpectedAlpha = 1.0f;
			Require(RGB && Alpha && RGB->Min.Equals(FVector::OneVector) && RGB->Max.Equals(FVector::OneVector)
				&& FMath::IsNearlyEqual(Alpha->Min, ExpectedAlpha)
				&& FMath::IsNearlyEqual(Alpha->Max, ExpectedAlpha),
				TEXT("initial particles must have white color and the expected mark/powder opacity."));
		}
		if (const UParticleModuleSize* Size = Cast<UParticleModuleSize>(Module))
		{
			const UDistributionVectorUniform* Distribution =
				Cast<UDistributionVectorUniform>(Size->StartSize.Distribution);
			const FVector ExpectedMin = bSurfacePatch
				? FVector(MarkMoteMinSizeCm / 100.0f, MarkMoteMinSizeCm / 100.0f, MarkMoteMinSizeCm / 100.0f)
				: FVector(PowderSmokeMinSizeCm, PowderSmokeMinSizeCm, 1.0f);
			const FVector ExpectedMax = bSurfacePatch
				? FVector(MarkMoteMaxSizeCm / 100.0f, MarkMoteMaxSizeCm / 100.0f, MarkMoteMaxSizeCm / 100.0f)
				: FVector(PowderSmokeMaxSizeCm, PowderSmokeMaxSizeCm, 1.0f);
			Require(Distribution && Distribution->Min.Equals(ExpectedMin)
				&& Distribution->Max.Equals(ExpectedMax),
				TEXT("particle size distribution does not match the approved cloud appearance."));
		}
		if (bSurfacePatch)
		{
			if (const UParticleModuleLocationPrimitiveSphere* MarkVolume =
				Cast<UParticleModuleLocationPrimitiveSphere>(Module))
			{
				const UDistributionFloatUniform* Radius =
					Cast<UDistributionFloatUniform>(MarkVolume->StartRadius.Distribution);
				const UDistributionVectorUniform* Offset =
					Cast<UDistributionVectorUniform>(MarkVolume->StartLocation.Distribution);
				Require(Radius && FMath::IsNearlyEqual(Radius->Min, MarkMoteRadiusCm)
					&& FMath::IsNearlyEqual(Radius->Max, MarkMoteRadiusCm)
					&& Offset && Offset->Min.IsNearlyZero() && Offset->Max.IsNearlyZero()
					&& !MarkVolume->SurfaceOnly,
					TEXT("mark mote spawn must fill a 3D spherical volume around the surface attachment."));
			}
		}
		Require(!Module || (!Module->IsA<UParticleModuleColorOverLife>()
			&& !Module->IsA<UParticleModuleRotation>()),
			TEXT("unexpected default color or rotation module remains."));
	}
	Require(LifetimeCount == 1 && SizeCount == 1 && LocationCount == 1,
		TEXT("duplicate lifetime/size/location modules would compound particle properties."));
	Require(MeshRotationCount == (bSurfacePatch ? 1 : 0),
		TEXT("mesh orientation module does not match the surface/sprite role."));
	Require(VelocityCount == 1 && AccelerationCount == 1 && RotationRateCount == 1,
		TEXT("animated cloud must have exactly one drift, turbulence, and swirl module."));
	Require(ColorCount == 1, TEXT("expected exactly one initial color module."));

	if (bSurfacePatch)
	{
		const UParticleModuleTypeDataMesh* MeshData =
			Cast<UParticleModuleTypeDataMesh>(LOD->TypeDataModule);
		const UParticleModuleMeshRotation* MeshRotation = nullptr;
		for (const UParticleModule* Module : LOD->Modules)
		{
			if (const UParticleModuleMeshRotation* Candidate = Cast<UParticleModuleMeshRotation>(Module))
			{
				MeshRotation = Candidate;
				break;
			}
		}
		Require(MeshData && MeshData->Mesh
			&& MeshData->Mesh->GetPathName() == TEXT("/Engine/BasicShapes/Sphere.Sphere")
			&& !MeshData->bCameraFacing
			&& MeshData->AxisLockOption == EPAL_NONE,
			TEXT("mark effect must use a non-camera-facing volumetric sphere mesh."));
		Require(MeshRotation && !MeshRotation->bInheritParent,
			TEXT("local-space surface mark must not apply the component rotation twice."));
	}
	else
	{
		Require(LOD->TypeDataModule == nullptr,
			TEXT("powder cloud should use the sprite renderer, not mesh type data."));
		bValid = ValidatePowderEmberEmitter(System, Run) && bValid;
	}
	return bValid;
}

UBlueprint* CreatePowderBlueprint(UParticleSystem* PowderSystem, FRun& Run)
{
	UObject* ExistingObject = nullptr;
	if (!HasCompatibleOrMissingAsset(PowderBlueprintPath, UBlueprint::StaticClass(), ExistingObject, Run))
	{
		return nullptr;
	}
	if (ExistingObject)
	{
		UBlueprint* Existing = CastChecked<UBlueprint>(ExistingObject);
		if (!Existing->GeneratedClass
			|| !Existing->GeneratedClass->IsChildOf(AIGPowderActor::StaticClass()))
		{
			Run.Errors.Add(TEXT("Existing generated BP_IG_PowderCloud is not derived from AIGPowderActor."));
			return nullptr;
		}
		return Existing;
	}
	if (!PowderSystem)
	{
		Run.Errors.Add(TEXT("Cannot create powder actor blueprint without PS_IG_PowderRedCloud."));
		return nullptr;
	}

	UPackage* Package = CreatePackage(PowderBlueprintPath);
	if (!Package)
	{
		Run.Errors.Add(TEXT("Could not create BP_IG_PowderCloud package."));
		return nullptr;
	}
	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
		AIGPowderActor::StaticClass(), Package, FName(TEXT("BP_IG_PowderCloud")),
		BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(),
		FName(TEXT("MHGZM636VisualAssetSetup")));
	if (!Blueprint || !Blueprint->GeneratedClass || !Blueprint->SimpleConstructionScript)
	{
		Run.Errors.Add(TEXT("Could not create Blueprint derived from AIGPowderActor."));
		return nullptr;
	}
	Package->GetMetaData().SetValue(Blueprint, TEXT("MHGZ.Generator"), GeneratorMarker);
	FAssetRegistryModule::AssetCreated(Blueprint);
	Run.CreatedAssets.Add(PowderBlueprintPath);

	USCS_Node* VisualNode = Blueprint->SimpleConstructionScript->CreateNode(
		UParticleSystemComponent::StaticClass(), FName(TEXT("PowderVisual")));
	UParticleSystemComponent* VisualTemplate = VisualNode
		? Cast<UParticleSystemComponent>(VisualNode->ComponentTemplate) : nullptr;
	if (!VisualNode || !VisualTemplate)
	{
		Run.Errors.Add(TEXT("Could not create the powder visual SCS component."));
		return nullptr;
	}
	VisualNode->ParentComponentOrVariableName = FName(TEXT("SceneRoot"));
	VisualNode->ParentComponentOwnerClassName = AIGPowderActor::StaticClass()->GetFName();
	VisualNode->bIsParentComponentNative = true;
	Blueprint->SimpleConstructionScript->AddNode(VisualNode);
	VisualTemplate->SetTemplate(PowderSystem);
	VisualTemplate->bAutoActivate = true;
	VisualTemplate->bAutoDestroy = false;
	VisualTemplate->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	VisualTemplate->SetGenerateOverlapEvents(false);
	VisualTemplate->SetSimulatePhysics(false);
	Blueprint->MarkPackageDirty();
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	if (!Blueprint->GeneratedClass)
	{
		Run.Errors.Add(TEXT("Blueprint compilation failed: GeneratedClass is missing."));
		return nullptr;
	}
	return Blueprint;
}

bool ValidatePowderBlueprint(UBlueprint* Blueprint, UParticleSystem* PowderSystem,
	FRun& Run)
{
	if (!Blueprint || !Blueprint->GeneratedClass
		|| !Blueprint->GeneratedClass->IsChildOf(AIGPowderActor::StaticClass())
		|| !Blueprint->SimpleConstructionScript)
	{
		Run.Errors.Add(TEXT("BP_IG_PowderCloud is missing its generated class, SCS, or native parent."));
		return false;
	}
	USCS_Node* VisualNode = Blueprint->SimpleConstructionScript->FindSCSNode(
		FName(TEXT("PowderVisual")));
	UParticleSystemComponent* VisualTemplate = VisualNode
		? Cast<UParticleSystemComponent>(VisualNode->ComponentTemplate) : nullptr;
	if (!VisualTemplate || VisualTemplate->Template != PowderSystem
		|| !VisualTemplate->bAutoActivate
		|| VisualTemplate->GetCollisionEnabled() != ECollisionEnabled::NoCollision
		|| VisualTemplate->GetGenerateOverlapEvents())
	{
		Run.Errors.Add(TEXT("BP_IG_PowderCloud.PowderVisual must use PS_IG_PowderRedCloud, auto-activate, and have collision/overlaps disabled."));
		return false;
	}
	return true;
}

bool Preflight(FRun& Run)
{
	Run.CombatConfig = LoadObject<UInsectGlaiveCombatConfig>(nullptr,
		CombatConfigPath, nullptr, LOAD_NoWarn);
	if (!Run.CombatConfig)
	{
		Run.Errors.Add(FString::Printf(TEXT("Could not load %s"), CombatConfigPath));
		return false;
	}
	if (!FMath::IsNearlyEqual(Run.CombatConfig->KinsectMarkDuration, 90.0f, 0.001f))
	{
		Run.Errors.Add(TEXT("DA_IG_Combat mark duration/patch/tolerance differ from the approved 90s/50cm-half-extent/5cm contract; refusing unrelated edits."));
		return false;
	}
	UParticleSystem* ExistingMark = LoadObject<UParticleSystem>(nullptr,
		LegacyMarkPath, nullptr, LOAD_NoWarn);
	if (Run.CombatConfig->KinsectMarkEstablishedEffect
		&& Run.CombatConfig->KinsectMarkEstablishedEffect != ExistingMark
		&& Run.CombatConfig->KinsectMarkEstablishedEffect->GetPathName() != ObjectPath(MarkSystemPath))
	{
		Run.Errors.Add(FString::Printf(TEXT("DA_IG_Combat.KinsectMarkEstablishedEffect currently points to %s; refusing to replace an unexpected reference."),
			*GetPathNameSafe(Run.CombatConfig->KinsectMarkEstablishedEffect)));
		return false;
	}
	if (Run.CombatConfig->PowderActorClass
		&& Run.CombatConfig->PowderActorClass->GetPathName() != ObjectPath(PowderBlueprintPath) + TEXT("_C"))
	{
		Run.Errors.Add(FString::Printf(TEXT("DA_IG_Combat.PowderActorClass already points to %s; refusing to replace it."),
			*GetPathNameSafe(Run.CombatConfig->PowderActorClass.Get())));
		return false;
	}
	return true;
}

bool BuildOrLoadAssets(FRun& Run)
{
	UObject* ExistingTextureObject = nullptr;
	UObject* ExistingMarkMaterialObject = nullptr;
	UObject* ExistingPowderMaterialObject = nullptr;
	UObject* ExistingPowderEmberMaterialObject = nullptr;
	UObject* ExistingMarkSystemObject = nullptr;
	UObject* ExistingPowderSystemObject = nullptr;
	UObject* ExistingBlueprintObject = nullptr;
	if (!HasCompatibleOrMissingAsset(MarkerTexturePath, UTexture2D::StaticClass(), ExistingTextureObject, Run)
		|| !HasCompatibleOrMissingAsset(MarkMaterialPath, UMaterial::StaticClass(), ExistingMarkMaterialObject, Run)
		|| !HasCompatibleOrMissingAsset(PowderMaterialPath, UMaterial::StaticClass(), ExistingPowderMaterialObject, Run)
		|| !HasCompatibleOrMissingAsset(PowderEmberMaterialPath, UMaterial::StaticClass(), ExistingPowderEmberMaterialObject, Run)
		|| !HasCompatibleOrMissingAsset(MarkSystemPath, UParticleSystem::StaticClass(), ExistingMarkSystemObject, Run)
		|| !HasCompatibleOrMissingAsset(PowderSystemPath, UParticleSystem::StaticClass(), ExistingPowderSystemObject, Run)
		|| !HasCompatibleOrMissingAsset(PowderBlueprintPath, UBlueprint::StaticClass(), ExistingBlueprintObject, Run))
	{
		return false;
	}

	UTexture2D* Texture = CreateCloudTexture(Run);
	UMaterial* MarkMaterial = CreateCloudMaterial(MarkMaterialPath, Texture, Run);
	UMaterial* PowderMaterial = CreateCloudMaterial(PowderMaterialPath, Texture, Run);
	UMaterial* PowderEmberMaterial = CreatePowderEmberMaterial(Texture, Run);
	Run.MarkSystem = CreateParticleSystem(MarkSystemPath, MarkMaterial, true, Run);
	Run.PowderSystem = CreateParticleSystem(PowderSystemPath, PowderMaterial, false, Run,
		PowderEmberMaterial);
	Run.PowderBlueprint = CreatePowderBlueprint(Run.PowderSystem, Run);
	return Run.Errors.IsEmpty() && Run.MarkSystem && Run.PowderSystem && Run.PowderBlueprint;
}

bool ConnectApprovedConfig(FRun& Run)
{
	const FGameplayTag PowderHitCueTag = FGameplayTag::RequestGameplayTag(
		FName(PowderHitCueTagName), false);
	const FGameplayTag PowderExplosionCueTag = FGameplayTag::RequestGameplayTag(
		FName(PowderExplosionCueTagName), false);
	if (!PowderHitCueTag.IsValid() || !PowderExplosionCueTag.IsValid())
	{
		Run.Errors.Add(TEXT("The approved powder GameplayCue tags are not registered; refusing to enable the powder actor."));
		return false;
	}
	if (!Run.PowderBlueprint || !Run.PowderBlueprint->GeneratedClass)
	{
		Run.Errors.Add(TEXT("The generated powder Blueprint has no GeneratedClass."));
		return false;
	}

	const FString CurrentEffect = GetPathNameSafe(Run.CombatConfig->KinsectMarkEstablishedEffect);
	const FString NewEffectPath = ObjectPath(MarkSystemPath);
	const bool bNeedsChange = CurrentEffect != NewEffectPath
		|| Run.CombatConfig->KinsectAutoMarkSearchRadius != ApprovedArenaHalfSideCm
		|| Run.CombatConfig->KinsectAutoMarkAttackInterval != ApprovedAutoMarkAttackIntervalSeconds
		|| Run.CombatConfig->MarkedHitPowderCooldown != ApprovedMarkedHitPowderCooldownSeconds
		|| ::MHGZ::IGEditor::RequireAction<UMHGZPowderVortexAbility>(TEXT("PowderVortex")).PowderVortex.GatherRadius != ApprovedArenaHalfSideCm
		|| ::MHGZ::IGEditor::RequireAction<UMHGZPowderVortexAbility>(TEXT("PowderVortex")).PowderVortex.GatherDelay != ApprovedPowderVortexDelaySeconds
		|| ::MHGZ::IGEditor::RequireAction<UMHGZPowderVortexAbility>(TEXT("PowderVortex")).PowderVortex.MotionValueAt1 != ApprovedPowderDamageAt1
		|| ::MHGZ::IGEditor::RequireAction<UMHGZPowderVortexAbility>(TEXT("PowderVortex")).PowderVortex.MotionValueAt5 != ApprovedPowderDamageAt5
		|| ::MHGZ::IGEditor::RequireAction<UMHGZPowderVortexAbility>(TEXT("PowderVortex")).PowderVortex.MotionValueAt10 != ApprovedPowderDamageAt10
		|| Run.CombatConfig->PowderActorClass.Get() != Run.PowderBlueprint->GeneratedClass
		|| Run.CombatConfig->PowderLifetime != PowderLifetimeSeconds
		|| Run.CombatConfig->PowderMaxCount != ApprovedPowderMaxCount
		|| Run.CombatConfig->PowderNaturalExplosionRadius != ApprovedPowderNaturalExplosionRadiusCm
		|| Run.CombatConfig->PowderMotionValue != ApprovedPowderMotionValue
		|| Run.CombatConfig->PowderDespawnDistance != ApprovedPowderDespawnDistanceCm
		|| ::MHGZ::IGEditor::RequireAction<UMHGZPowderVortexAbility>(TEXT("PowderVortex")).PowderVortex.ExplosionRadius != ApprovedPowderVortexExplosionRadiusCm
		|| ::MHGZ::IGEditor::RequireAction<UMHGZPowderVortexAbility>(TEXT("PowderVortex")).PowderVortex.HitCueTag != PowderHitCueTag
		|| ::MHGZ::IGEditor::RequireAction<UMHGZPowderVortexAbility>(TEXT("PowderVortex")).PowderVortex.ExplosionCueTag != PowderExplosionCueTag;
	if (bNeedsChange)
	{
		Run.CombatConfig->Modify();
		Run.bChangedConfig = true;
	}
	Run.CombatConfig->KinsectMarkEstablishedEffect = Run.MarkSystem;
	Run.CombatConfig->KinsectAutoMarkSearchRadius = ApprovedArenaHalfSideCm;
	Run.CombatConfig->KinsectAutoMarkAttackInterval = ApprovedAutoMarkAttackIntervalSeconds;
	Run.CombatConfig->MarkedHitPowderCooldown = ApprovedMarkedHitPowderCooldownSeconds;
	::MHGZ::IGEditor::RequireAction<UMHGZPowderVortexAbility>(TEXT("PowderVortex")).Modify();
	::MHGZ::IGEditor::RequireAction<UMHGZPowderVortexAbility>(TEXT("PowderVortex")).PowderVortex.GatherRadius = ApprovedArenaHalfSideCm;
	::MHGZ::IGEditor::RequireAction<UMHGZPowderVortexAbility>(TEXT("PowderVortex")).Modify();
	::MHGZ::IGEditor::RequireAction<UMHGZPowderVortexAbility>(TEXT("PowderVortex")).PowderVortex.GatherDelay = ApprovedPowderVortexDelaySeconds;
	::MHGZ::IGEditor::RequireAction<UMHGZPowderVortexAbility>(TEXT("PowderVortex")).Modify();
	::MHGZ::IGEditor::RequireAction<UMHGZPowderVortexAbility>(TEXT("PowderVortex")).PowderVortex.MotionValueAt1 = ApprovedPowderDamageAt1;
	::MHGZ::IGEditor::RequireAction<UMHGZPowderVortexAbility>(TEXT("PowderVortex")).Modify();
	::MHGZ::IGEditor::RequireAction<UMHGZPowderVortexAbility>(TEXT("PowderVortex")).PowderVortex.MotionValueAt5 = ApprovedPowderDamageAt5;
	::MHGZ::IGEditor::RequireAction<UMHGZPowderVortexAbility>(TEXT("PowderVortex")).Modify();
	::MHGZ::IGEditor::RequireAction<UMHGZPowderVortexAbility>(TEXT("PowderVortex")).PowderVortex.MotionValueAt10 = ApprovedPowderDamageAt10;
	Run.CombatConfig->PowderActorClass = Run.PowderBlueprint->GeneratedClass;
	Run.CombatConfig->PowderLifetime = PowderLifetimeSeconds;
	Run.CombatConfig->PowderMaxCount = ApprovedPowderMaxCount;
	Run.CombatConfig->PowderNaturalExplosionRadius = ApprovedPowderNaturalExplosionRadiusCm;
	Run.CombatConfig->PowderMotionValue = ApprovedPowderMotionValue;
	Run.CombatConfig->PowderDespawnDistance = ApprovedPowderDespawnDistanceCm;
	::MHGZ::IGEditor::RequireAction<UMHGZPowderVortexAbility>(TEXT("PowderVortex")).Modify();
	::MHGZ::IGEditor::RequireAction<UMHGZPowderVortexAbility>(TEXT("PowderVortex")).PowderVortex.ExplosionRadius = ApprovedPowderVortexExplosionRadiusCm;
	::MHGZ::IGEditor::RequireAction<UMHGZPowderVortexAbility>(TEXT("PowderVortex")).Modify();
	::MHGZ::IGEditor::RequireAction<UMHGZPowderVortexAbility>(TEXT("PowderVortex")).PowderVortex.HitCueTag = PowderHitCueTag;
	::MHGZ::IGEditor::RequireAction<UMHGZPowderVortexAbility>(TEXT("PowderVortex")).Modify();
	::MHGZ::IGEditor::RequireAction<UMHGZPowderVortexAbility>(TEXT("PowderVortex")).PowderVortex.ExplosionCueTag = PowderExplosionCueTag;
	return true;
}

bool ValidateExistingVisualConfigReferences(const FRun& Run, TArray<FString>& Errors)
{
	if (!Run.CombatConfig || Run.CombatConfig->KinsectMarkEstablishedEffect != Run.MarkSystem)
	{
		Errors.Add(TEXT("Visual-only Apply requires DA_IG_Combat.KinsectMarkEstablishedEffect to already reference PS_IG_KinsectMarkCloud."));
	}
	if (!Run.CombatConfig || !Run.PowderBlueprint || !Run.PowderBlueprint->GeneratedClass
		|| Run.CombatConfig->PowderActorClass.Get() != Run.PowderBlueprint->GeneratedClass)
	{
		Errors.Add(TEXT("Visual-only Apply requires DA_IG_Combat.PowderActorClass to already reference BP_IG_PowderCloud."));
	}
	return Errors.IsEmpty();
}

bool AuditApprovedConfig(FRun& Run)
{
	if (!Run.PowderBlueprint || !Run.PowderBlueprint->GeneratedClass)
	{
		Run.Errors.Add(TEXT("Audit could not resolve the generated powder Blueprint class."));
		return false;
	}
	const FGameplayTag PowderHitCueTag = FGameplayTag::RequestGameplayTag(
		FName(PowderHitCueTagName), false);
	const FGameplayTag PowderExplosionCueTag = FGameplayTag::RequestGameplayTag(
		FName(PowderExplosionCueTagName), false);
	if (!PowderHitCueTag.IsValid() || !PowderExplosionCueTag.IsValid())
	{
		Run.Errors.Add(TEXT("Audit could not resolve one or both approved powder GameplayCue tags."));
	}
	auto CheckFloat = [&Run](const TCHAR* Name, float Actual, float Expected)
	{
		if (!FMath::IsNearlyEqual(Actual, Expected, 0.001f))
		{
			Run.Errors.Add(FString::Printf(TEXT("DA_IG_Combat.%s is %.6g; expected %.6g."),
				Name, Actual, Expected));
		}
	};
	if (Run.CombatConfig->KinsectMarkEstablishedEffect != Run.MarkSystem)
	{
		Run.Errors.Add(TEXT("DA_IG_Combat.KinsectMarkEstablishedEffect is not connected to PS_IG_KinsectMarkCloud."));
	}
	if (Run.CombatConfig->PowderActorClass.Get() != Run.PowderBlueprint->GeneratedClass)
	{
		Run.Errors.Add(TEXT("DA_IG_Combat.PowderActorClass is not connected to BP_IG_PowderCloud_C."));
	}
	if (Run.CombatConfig->PowderMaxCount != ApprovedPowderMaxCount)
	{
		Run.Errors.Add(FString::Printf(TEXT("DA_IG_Combat.PowderMaxCount is %d; expected %d."),
			Run.CombatConfig->PowderMaxCount, ApprovedPowderMaxCount));
	}
	CheckFloat(TEXT("KinsectAutoMarkSearchRadius"), Run.CombatConfig->KinsectAutoMarkSearchRadius, ApprovedArenaHalfSideCm);
	CheckFloat(TEXT("KinsectAutoMarkAttackInterval"), Run.CombatConfig->KinsectAutoMarkAttackInterval, ApprovedAutoMarkAttackIntervalSeconds);
	CheckFloat(TEXT("MarkedHitPowderCooldown"), Run.CombatConfig->MarkedHitPowderCooldown, ApprovedMarkedHitPowderCooldownSeconds);
	CheckFloat(TEXT("PowderGatherRadius"), ::MHGZ::IGEditor::RequireAction<UMHGZPowderVortexAbility>(TEXT("PowderVortex")).PowderVortex.GatherRadius, ApprovedArenaHalfSideCm);
	CheckFloat(TEXT("PowderVortexDelay"), ::MHGZ::IGEditor::RequireAction<UMHGZPowderVortexAbility>(TEXT("PowderVortex")).PowderVortex.GatherDelay, ApprovedPowderVortexDelaySeconds);
	CheckFloat(TEXT("PowderVortexDamageAt1"), ::MHGZ::IGEditor::RequireAction<UMHGZPowderVortexAbility>(TEXT("PowderVortex")).PowderVortex.MotionValueAt1, ApprovedPowderDamageAt1);
	CheckFloat(TEXT("PowderVortexDamageAt5"), ::MHGZ::IGEditor::RequireAction<UMHGZPowderVortexAbility>(TEXT("PowderVortex")).PowderVortex.MotionValueAt5, ApprovedPowderDamageAt5);
	CheckFloat(TEXT("PowderVortexDamageAt10"), ::MHGZ::IGEditor::RequireAction<UMHGZPowderVortexAbility>(TEXT("PowderVortex")).PowderVortex.MotionValueAt10, ApprovedPowderDamageAt10);
	CheckFloat(TEXT("PowderLifetime"), Run.CombatConfig->PowderLifetime, PowderLifetimeSeconds);
	CheckFloat(TEXT("PowderNaturalExplosionRadius"), Run.CombatConfig->PowderNaturalExplosionRadius, ApprovedPowderNaturalExplosionRadiusCm);
	CheckFloat(TEXT("PowderMotionValue"), Run.CombatConfig->PowderMotionValue, ApprovedPowderMotionValue);
	CheckFloat(TEXT("PowderDespawnDistance"), Run.CombatConfig->PowderDespawnDistance, ApprovedPowderDespawnDistanceCm);
	CheckFloat(TEXT("PowderVortexExplosionRadius"), ::MHGZ::IGEditor::RequireAction<UMHGZPowderVortexAbility>(TEXT("PowderVortex")).PowderVortex.ExplosionRadius, ApprovedPowderVortexExplosionRadiusCm);
	if (::MHGZ::IGEditor::RequireAction<UMHGZPowderVortexAbility>(TEXT("PowderVortex")).PowderVortex.HitCueTag != PowderHitCueTag)
	{
		Run.Errors.Add(TEXT("DA_IG_Combat.PowderVortexHitCueTag is not GameplayCue.Hit.IG.Powder."));
	}
	if (::MHGZ::IGEditor::RequireAction<UMHGZPowderVortexAbility>(TEXT("PowderVortex")).PowderVortex.ExplosionCueTag != PowderExplosionCueTag)
	{
		Run.Errors.Add(TEXT("DA_IG_Combat.PowderVortexExplosionCueTag is not GameplayCue.IG.PowderVortex."));
	}
	return Run.Errors.IsEmpty();
}

bool WriteReport(const FRun& Run)
{
	const FString FullPath = FPaths::Combine(FPaths::ProjectDir(), ReportPath);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(FullPath), true);
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("task"), TEXT("M6.3.6 persistent visual asset setup"));
	Root->SetStringField(TEXT("mode"), !Run.bApply ? TEXT("AuditOnly")
		: (Run.bPowderRadiiOnly ? TEXT("ApplyPowderRadiiOnly")
			: (Run.bVisualOnly ? TEXT("ApplyVisualOnly") : TEXT("Apply"))));
	Root->SetBoolField(TEXT("success"), Run.bSuccess);
	Root->SetStringField(TEXT("mark_particle_system"), ObjectPath(MarkSystemPath));
	Root->SetStringField(TEXT("powder_particle_system"), ObjectPath(PowderSystemPath));
	Root->SetStringField(TEXT("powder_ember_material"), ObjectPath(PowderEmberMaterialPath));
	Root->SetStringField(TEXT("powder_blueprint"), ObjectPath(PowderBlueprintPath));
	Root->SetStringField(TEXT("mark_effect_after"), Run.CombatConfig
		? GetPathNameSafe(Run.CombatConfig->KinsectMarkEstablishedEffect) : TEXT("None"));
	Root->SetStringField(TEXT("powder_actor_class_after"), Run.CombatConfig
		? GetPathNameSafe(Run.CombatConfig->PowderActorClass.Get()) : TEXT("None"));
	Root->SetNumberField(TEXT("powder_motion_value"), Run.CombatConfig
		? Run.CombatConfig->PowderMotionValue : 0.0);
	Root->SetNumberField(TEXT("powder_despawn_distance_cm"), Run.CombatConfig
		? Run.CombatConfig->PowderDespawnDistance : 0.0);
	Root->SetNumberField(TEXT("auto_mark_search_radius_cm"), Run.CombatConfig
		? Run.CombatConfig->KinsectAutoMarkSearchRadius : 0.0);
	Root->SetNumberField(TEXT("auto_mark_attack_interval_seconds"), Run.CombatConfig
		? Run.CombatConfig->KinsectAutoMarkAttackInterval : 0.0);
	Root->SetNumberField(TEXT("marked_hit_powder_cooldown_seconds"), Run.CombatConfig
		? Run.CombatConfig->MarkedHitPowderCooldown : 0.0);
	Root->SetNumberField(TEXT("powder_gather_radius_cm"), Run.CombatConfig
		? ::MHGZ::IGEditor::RequireAction<UMHGZPowderVortexAbility>(TEXT("PowderVortex")).PowderVortex.GatherRadius : 0.0);
	Root->SetNumberField(TEXT("powder_vortex_delay_seconds"), Run.CombatConfig
		? ::MHGZ::IGEditor::RequireAction<UMHGZPowderVortexAbility>(TEXT("PowderVortex")).PowderVortex.GatherDelay : 0.0);
	Root->SetNumberField(TEXT("powder_vortex_damage_at_1"), Run.CombatConfig
		? ::MHGZ::IGEditor::RequireAction<UMHGZPowderVortexAbility>(TEXT("PowderVortex")).PowderVortex.MotionValueAt1 : 0.0);
	Root->SetNumberField(TEXT("powder_vortex_damage_at_5"), Run.CombatConfig
		? ::MHGZ::IGEditor::RequireAction<UMHGZPowderVortexAbility>(TEXT("PowderVortex")).PowderVortex.MotionValueAt5 : 0.0);
	Root->SetNumberField(TEXT("powder_vortex_damage_at_10"), Run.CombatConfig
		? ::MHGZ::IGEditor::RequireAction<UMHGZPowderVortexAbility>(TEXT("PowderVortex")).PowderVortex.MotionValueAt10 : 0.0);
	Root->SetNumberField(TEXT("natural_explosion_radius_cm"), Run.CombatConfig
		? Run.CombatConfig->PowderNaturalExplosionRadius : 0.0);
	Root->SetNumberField(TEXT("powder_max_count"), Run.CombatConfig
		? Run.CombatConfig->PowderMaxCount : 0);
	Root->SetNumberField(TEXT("vortex_explosion_radius_cm"), Run.CombatConfig
		? ::MHGZ::IGEditor::RequireAction<UMHGZPowderVortexAbility>(TEXT("PowderVortex")).PowderVortex.ExplosionRadius : 0.0);
	Root->SetBoolField(TEXT("powder_runtime_enabled"), Run.CombatConfig
		&& Run.CombatConfig->PowderActorClass && Run.CombatConfig->PowderMotionValue > 0.0f);
	Root->SetStringField(TEXT("powder_class_connection"), Run.CombatConfig
		&& Run.CombatConfig->PowderActorClass ? TEXT("connected") : TEXT("missing"));
	Root->SetNumberField(TEXT("mark_particle_initial_burst"), MarkInitialParticleBurst);
	Root->SetNumberField(TEXT("mark_particle_spawn_rate_hz"), MarkParticleSpawnRate);
	Root->SetNumberField(TEXT("mark_particle_lifetime_seconds"), MarkParticleLifetimeSeconds);
	Root->SetNumberField(TEXT("mark_visual_radius_cm"), MarkMoteRadiusCm);
	Root->SetNumberField(TEXT("mark_particle_size_min_cm"), MarkMoteMinSizeCm);
	Root->SetNumberField(TEXT("mark_particle_size_max_cm"), MarkMoteMaxSizeCm);
	Root->SetNumberField(TEXT("mark_particle_opacity"), MarkMoteOpacity);
	Root->SetStringField(TEXT("mark_tint_linear"), TEXT("1.0,0.68,0.28"));
	Root->SetNumberField(TEXT("mark_approx_visual_diameter_cm"),
		MarkMoteRadiusCm * 2.0f + MarkMoteMaxSizeCm);
	Root->SetStringField(TEXT("mark_particle_shape"), TEXT("filled_3d_spherical_volume"));
	Root->SetNumberField(TEXT("mark_visual_depth_cm"), MarkMoteRadiusCm * 2.0f + MarkMoteMaxSizeCm);
	Root->SetNumberField(TEXT("powder_visual_radius_cm"), PowderVisualRadiusCm);
	Root->SetNumberField(TEXT("powder_smoke_spawn_radius_cm"), PowderSmokeSpawnRadiusCm);
	Root->SetNumberField(TEXT("powder_particle_initial_burst"), PowderInitialParticleBurst);
	Root->SetNumberField(TEXT("powder_particle_spawn_rate_hz"), PowderParticleSpawnRate);
	Root->SetNumberField(TEXT("powder_particle_lifetime_seconds"), PowderParticleLifetimeSeconds);
	Root->SetNumberField(TEXT("powder_particle_opacity"), PowderSmokeOpacity);
	Root->SetStringField(TEXT("powder_tint_linear"), TEXT("0.90,0.18,0.025"));
	Root->SetNumberField(TEXT("powder_ember_initial_burst"), PowderEmberInitialBurst);
	Root->SetNumberField(TEXT("powder_ember_spawn_rate_hz"), PowderEmberSpawnRate);
	Root->SetNumberField(TEXT("powder_ember_visual_radius_cm"), PowderEmberSpawnRadiusMaxCm);
	auto AddStrings = [&Root](const TCHAR* Field, const TArray<FString>& Strings)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const FString& Value : Strings)
		{
			Values.Add(MakeShared<FJsonValueString>(Value));
		}
		Root->SetArrayField(Field, Values);
	};
	AddStrings(TEXT("created_assets"), Run.CreatedAssets);
	AddStrings(TEXT("modified_assets"), Run.ModifiedAssets);
	AddStrings(TEXT("reused_generated_assets"), Run.ReusedGeneratedAssets);
	AddStrings(TEXT("errors"), Run.Errors);
	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	return FJsonSerializer::Serialize(Root, Writer)
		&& FFileHelper::SaveStringToFile(Json, *FullPath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

bool Execute(FRun& Run)
{
	if (!Preflight(Run))
	{
		return false;
	}
	if (Run.bPowderRadiiOnly)
	{
		const bool bNeedsChange =
			!FMath::IsNearlyEqual(Run.CombatConfig->PowderNaturalExplosionRadius,
				ApprovedPowderNaturalExplosionRadiusCm)
			|| !FMath::IsNearlyEqual(::MHGZ::IGEditor::RequireAction<UMHGZPowderVortexAbility>(TEXT("PowderVortex")).PowderVortex.ExplosionRadius,
				ApprovedPowderVortexExplosionRadiusCm);
		if (bNeedsChange)
		{
			Run.CombatConfig->Modify();
			Run.CombatConfig->PowderNaturalExplosionRadius =
				ApprovedPowderNaturalExplosionRadiusCm;
			::MHGZ::IGEditor::RequireAction<UMHGZPowderVortexAbility>(TEXT("PowderVortex")).Modify();
			::MHGZ::IGEditor::RequireAction<UMHGZPowderVortexAbility>(TEXT("PowderVortex")).PowderVortex.ExplosionRadius =
				ApprovedPowderVortexExplosionRadiusCm;
			Run.bChangedConfig = true;
			Run.ModifiedAssets.Add(CombatConfigPath);
			if (!(SaveAsset(Run.CombatConfig, Run) && SaveAsset(::MHGZ::IGEditor::OwnerAsset(::MHGZ::IGEditor::RequireAction<UMHGZPowderVortexAbility>(TEXT("PowderVortex"))), Run)))
			{
				return false;
			}
		}
		return true;
	}
	if (!Run.bApply)
	{
		for (const TCHAR* Path : { MarkerTexturePath, MarkMaterialPath, PowderMaterialPath,
			PowderEmberMaterialPath,
			MarkSystemPath, PowderSystemPath, PowderBlueprintPath })
		{
			UObject* Existing = StaticLoadObject(UObject::StaticClass(), nullptr,
				*ObjectPath(Path), nullptr, LOAD_NoWarn);
			if (Existing && GetGeneratorMarker(Existing) != GeneratorMarker)
			{
				Run.Errors.Add(FString::Printf(TEXT("Audit found an existing unowned asset at %s."), Path));
			}
		}
		for (const TCHAR* SystemPath : { MarkSystemPath, PowderSystemPath })
		{
			if (UParticleSystem* ExistingSystem = LoadObject<UParticleSystem>(nullptr,
				*ObjectPath(SystemPath), nullptr, LOAD_NoWarn);
				ExistingSystem && GetGeneratorMarker(ExistingSystem) == GeneratorMarker)
			{
				ValidateParticleSystemModuleOuters(ExistingSystem, Run);
				ValidateCloudParticleSystem(ExistingSystem,
					FCString::Stricmp(SystemPath, MarkSystemPath) == 0, Run);
			}
		}
		UTexture2D* ExistingTexture = LoadObject<UTexture2D>(nullptr,
			*ObjectPath(MarkerTexturePath), nullptr, LOAD_NoWarn);
		for (const TCHAR* MaterialPath : { MarkMaterialPath, PowderMaterialPath })
		{
			UMaterial* ExistingMaterial = LoadObject<UMaterial>(nullptr,
				*ObjectPath(MaterialPath), nullptr, LOAD_NoWarn);
			if (ExistingMaterial && GetGeneratorMarker(ExistingMaterial) == GeneratorMarker)
			{
				ValidateOrRepairCloudMaterial(ExistingMaterial, ExistingTexture,
					MaterialPath, false, Run);
			}
		}
		UMaterial* ExistingPowderEmberMaterial = LoadObject<UMaterial>(nullptr,
			*ObjectPath(PowderEmberMaterialPath), nullptr, LOAD_NoWarn);
		if (!ExistingPowderEmberMaterial
			|| GetGeneratorMarker(ExistingPowderEmberMaterial) != GeneratorMarker
			|| !ValidatePowderEmberMaterial(ExistingPowderEmberMaterial, ExistingTexture, Run))
		{
			Run.Errors.Add(TEXT("Audit expected a valid generated additive powder ember material."));
		}
		UBlueprint* ExistingBlueprint = LoadObject<UBlueprint>(nullptr,
			*ObjectPath(PowderBlueprintPath), nullptr, LOAD_NoWarn);
		Run.PowderBlueprint = ExistingBlueprint;
		UParticleSystem* ExistingPowderSystem = LoadObject<UParticleSystem>(nullptr,
			*ObjectPath(PowderSystemPath), nullptr, LOAD_NoWarn);
		if (!ExistingBlueprint || GetGeneratorMarker(ExistingBlueprint) != GeneratorMarker
			|| !ExistingPowderSystem || GetGeneratorMarker(ExistingPowderSystem) != GeneratorMarker)
		{
			Run.Errors.Add(TEXT("Audit expected the generated powder Blueprint and particle system to exist."));
		}
		else
		{
			Run.PowderSystem = ExistingPowderSystem;
			ValidatePowderBlueprint(ExistingBlueprint, ExistingPowderSystem, Run);
		}
		Run.MarkSystem = LoadObject<UParticleSystem>(nullptr,
			*ObjectPath(MarkSystemPath), nullptr, LOAD_NoWarn);
		if (!Run.MarkSystem || GetGeneratorMarker(Run.MarkSystem) != GeneratorMarker)
		{
			Run.Errors.Add(TEXT("Audit expected the generated mark particle system to exist."));
		}
		else
		{
			AuditApprovedConfig(Run);
		}
		return Run.Errors.IsEmpty();
	}
	if (!BuildOrLoadAssets(Run))
	{
		return false;
	}
	if (!ValidateParticleSystemModuleOuters(Run.MarkSystem, Run)
		|| !ValidateParticleSystemModuleOuters(Run.PowderSystem, Run)
		|| !ValidateCloudParticleSystem(Run.MarkSystem, true, Run)
		|| !ValidateCloudParticleSystem(Run.PowderSystem, false, Run)
		|| !ValidatePowderBlueprint(Run.PowderBlueprint, Run.PowderSystem, Run))
	{
		return false;
	}
	if (Run.bVisualOnly
		? !ValidateExistingVisualConfigReferences(Run, Run.Errors)
		: !ConnectApprovedConfig(Run))
	{
		return false;
	}
	// Save dependencies first; the modified DataAsset is saved last and only when
	// its approved mark-effect pointer actually changed.
	TArray<FString> AssetsToSave = Run.CreatedAssets;
	AssetsToSave.Append(Run.ModifiedAssets);
	for (const FString& CreatedPath : AssetsToSave)
	{
		UObject* Asset = StaticLoadObject(UObject::StaticClass(), nullptr,
			*ObjectPath(*CreatedPath), nullptr, LOAD_NoWarn);
		if (!Asset || !SaveAsset(Asset, Run))
		{
			return false;
		}
	}
	if (Run.bChangedConfig && !(SaveAsset(Run.CombatConfig, Run) && SaveAsset(::MHGZ::IGEditor::OwnerAsset(::MHGZ::IGEditor::RequireAction<UMHGZPowderVortexAbility>(TEXT("PowderVortex"))), Run)))
	{
		return false;
	}
	return Run.Errors.IsEmpty();
}

void DiagnoseMarkContactGeometry(UWorld& World, FRun& Run)
{
	UClass* DummyClass = LoadClass<AMHGZTrainingDummy>(nullptr,
		TEXT("/Game/Blueprints/Monster/TrainingDummy/BP_TrainingDummy.BP_TrainingDummy_C"));
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMHGZTrainingDummy* Dummy = DummyClass ? World.SpawnActor<AMHGZTrainingDummy>(
		DummyClass, FVector(5000.0f, 0.0f, 168.75f), FRotator::ZeroRotator, SpawnParams) : nullptr;
	if (!Dummy)
	{
		Run.Errors.Add(TEXT("Mark contact diagnostic could not spawn the real training target blueprint."));
		Run.bSuccess = false;
		return;
	}
	Dummy->DispatchBeginPlay();
	World.InitializeActorsForPlay(FURL());
	World.BeginPlay();
	World.Tick(LEVELTICK_All, 1.0f / 60.0f);
	TArray<UMHGZMonsterHitzoneComponent*> Hitzones;
	Dummy->GetComponents(Hitzones);
	int32 Samples = 0;
	int32 Failures = 0;
	for (UMHGZMonsterHitzoneComponent* Hitzone : Hitzones)
	{
		for (const FVector& Direction : { FVector::ForwardVector, -FVector::ForwardVector,
			FVector::RightVector, -FVector::RightVector,
			FVector(1.0f, 1.0f, -1.0f).GetSafeNormal(),
			FVector(1.0f, 1.0f, 1.0f).GetSafeNormal() })
		{
			FVector MarkPoint;
			FVector MarkNormal;
			const FVector Center = Hitzone->GetComponentLocation();
			if (!Dummy->ResolveHitEffectVisualSurface(Hitzone,
				Center + Direction * Hitzone->GetQueryBoundingRadius(), Direction,
				MarkPoint, MarkNormal))
			{
				++Failures;
				continue;
			}
			++Samples;
			const FVector Radial = (MarkPoint - Center).GetSafeNormal(SMALL_NUMBER, MarkNormal);
			const float Depth = FVector::Distance(MarkPoint, Center) + Hitzone->GetQueryBoundingRadius() + 2.0f;
			FCollisionObjectQueryParams Objects;
			Objects.AddObjectTypesToQuery(ECC_GameTraceChannel3);
			FCollisionQueryParams Params(SCENE_QUERY_STAT(IGMarkContactDiagnostic), false);
			TArray<FHitResult> Hits;
			World.LineTraceMultiByObjectType(Hits, MarkPoint + Radial * Depth,
				Center - Radial * Depth, Objects, Params);
			bool bFoundContact = false;
			for (const FHitResult& Hit : Hits)
			{
				if (Hit.GetComponent() != Hitzone || !Hit.bBlockingHit) { continue; }
				FVector ContactPoint;
				FVector ContactNormal;
				if (Dummy->ResolveHitEffectVisualSurface(Hitzone,
					Hit.ImpactPoint, Hit.ImpactNormal, ContactPoint, ContactNormal))
				{
					const FVector Delta = ContactPoint - MarkPoint;
					const float NormalDelta = FVector::DotProduct(Delta, MarkNormal);
					bFoundContact = true; // The resolved surface belongs to this exact Hitzone.
					if (bFoundContact) { break; }
				}
			}
			if (!bFoundContact) { ++Failures; }
		}
	}
	UE_LOG(LogTemp, Display, TEXT("[IGVisualRuntime] realTarget=%s hitzones=%d contactSamples=%d contactFailures=%d"),
		*GetPathNameSafe(DummyClass), Hitzones.Num(), Samples, Failures);
	if (Hitzones.IsEmpty() || Samples == 0 || Failures > 0)
	{
		Run.Errors.Add(TEXT("Real-mesh mark/contact projection diagnostic failed; see IGVisualRuntime."));
		Run.bSuccess = false;
	}
	Dummy->Destroy();
}

void DiagnoseParticleRuntime(FRun& Run)
{
	if (!FApp::CanEverRender())
	{
		Run.Errors.Add(TEXT("-Diagnose requires -AllowCommandletRendering and a real rendering backend; omit -NullRHI."));
		Run.bSuccess = false;
		return;
	}
	TGuardValue<bool> ClientGuard(GIsClient, true);
	UWorld* World = nullptr;
	{
		// Cascade needs the temporary world's FX system. Restore commandlet mode
		// immediately afterwards: the commandlet renderer has no game dynamic-
		// resolution state and must keep its normal rendering contract.
		TGuardValue<bool> CreateFXGuard(PRIVATE_GIsRunningCommandlet, false);
		World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("IGVisualDiagnosticWorld"));
	}
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	ADirectionalLight* Light = World->SpawnActor<ADirectionalLight>();
	Light->SetActorRotation(FRotator(0.0f, 90.0f, 0.0f));
	Light->GetLightComponent()->SetIntensity(10.0f);
	ASceneCapture2D* Camera = World->SpawnActor<ASceneCapture2D>();
	Camera->SetActorLocationAndRotation(FVector(0.0f, -100.0f, 0.0f), FRotator(0.0f, 90.0f, 0.0f));
	USceneCaptureComponent2D* Capture = Camera->GetCaptureComponent2D();
	UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(Camera);
	Target->RenderTargetFormat = RTF_RGBA8;
	Target->ClearColor = FLinearColor(0.01f, 0.01f, 0.01f, 1.0f);
	Target->InitAutoFormat(512, 512);
	Capture->TextureTarget = Target;
	Capture->ProjectionType = ECameraProjectionMode::Orthographic;
	Capture->OrthoWidth = 50.0f;
	Capture->CaptureSource = SCS_FinalColorLDR;
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->ShowFlags.SetAtmosphere(false);
	Capture->ShowFlags.SetFog(false);
	for (const TCHAR* Path : { MarkSystemPath, PowderSystemPath })
	{
		UParticleSystem* System = LoadObject<UParticleSystem>(nullptr, *ObjectPath(Path));
		if (!System)
		{
			Run.Errors.Add(FString::Printf(TEXT("Diagnostic particle asset missing: %s"), Path));
			Run.bSuccess = false;
			continue;
		}
		AActor* Owner = World->SpawnActor<AActor>();
		Owner->SetActorHiddenInGame(false);
		UParticleSystemComponent* Component = NewObject<UParticleSystemComponent>(Owner);
		Owner->SetRootComponent(Component);
		Component->bAutoActivate = false;
		Component->SetTemplate(System);
		Component->RegisterComponent();
		Component->SetWorldRotation(FRotator(0.0f, 0.0f, 90.0f));
		Component->ActivateSystem(true);
		UE_LOG(LogTemp, Display, TEXT("[IGVisualRuntime] template=%s active=%d registered=%d instances=%d"),
			*GetPathNameSafe(Component->Template), Component->IsActive(), Component->IsRegistered(), Component->EmitterInstances.Num());
		for (int32 Step = 0; Step < 12; ++Step)
		{
			for (FParticleEmitterInstance* Instance : Component->EmitterInstances)
			{
				if (Instance) { Instance->Tick(1.0f / 60.0f, false); }
			}
		}
		Component->UpdateBounds();
		Component->MarkRenderDynamicDataDirty();
		UE_LOG(LogTemp, Display, TEXT("[IGVisualRuntime] %s particles=%d bounds=%s rotation=%s"),
			Path, Component->GetNumActiveParticles(), *Component->Bounds.ToString(),
			*Component->GetComponentRotation().ToString());
		for (FParticleEmitterInstance* Instance : Component->EmitterInstances)
		{
			FBaseParticle* Particle = Instance ? Instance->GetParticle(0) : nullptr;
			if (Particle)
			{
				UE_LOG(LogTemp, Display, TEXT("[IGVisualRuntime] size=%s position=%s color=%s lifetimeInverse=%g material=%s"),
					*Particle->Size.ToString(), *Particle->Location.ToString(), *Particle->Color.ToString(),
					Particle->OneOverMaxLifetime, *GetPathNameSafe(Instance->CurrentMaterial));
				if (const int32 RotationOffset = Instance->GetMeshRotationOffset())
				{
					const FMeshRotationPayloadData* Rotation = reinterpret_cast<const FMeshRotationPayloadData*>(
						reinterpret_cast<const uint8*>(Particle) + RotationOffset);
					UE_LOG(LogTemp, Display, TEXT("[IGVisualRuntime] meshLocalRotation=%s worldPlaneNormal=%s"),
						*Rotation->Rotation.ToString(), *Component->GetComponentQuat().RotateVector(
							FRotator::MakeFromEuler(FVector(Rotation->Rotation)).RotateVector(FVector::UpVector)).ToString());
				}
			}
		}
		if (GShaderCompilingManager) { GShaderCompilingManager->FinishAllCompilation(); }
		World->SendAllEndOfFrameUpdates();
		Capture->CaptureScene();
		FBufferArchive Png;
		const FString ImagePath = FPaths::Combine(FPaths::ProjectSavedDir(),
			TEXT("M6Audit"), System->GetName() + TEXT("_Runtime.png"));
		if (!FImageUtils::ExportRenderTarget2DAsPNG(Target, Png)
			|| !FFileHelper::SaveArrayToFile(Png, *ImagePath)
			|| Component->GetNumActiveParticles() == 0)
		{
			Run.Errors.Add(FString::Printf(TEXT("Particle diagnostic simulation/capture failed: %s"), Path));
			Run.bSuccess = false;
		}
		UE_LOG(LogTemp, Display, TEXT("[IGVisualRuntime] image=%s"), *ImagePath);
		Owner->Destroy();
	}
	DiagnoseMarkContactGeometry(*World, Run);
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
}
}
}

#endif // WITH_EDITOR

UMHGZM636VisualAssetSetupCommandlet::UMHGZM636VisualAssetSetupCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UMHGZM636VisualAssetSetupCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	using namespace UE::MHGZ::M636VisualAssetSetup;
	FRun Run;
	Run.bApply = FParse::Param(*Params, TEXT("Apply"));
	Run.bDiagnose = FParse::Param(*Params, TEXT("Diagnose"));
	Run.bVisualOnly = FParse::Param(*Params, TEXT("VisualOnly"));
	Run.bPowderRadiiOnly = FParse::Param(*Params, TEXT("ApplyPowderRadiiOnly"));
	Run.bApply = Run.bApply || Run.bPowderRadiiOnly;
	if (Run.bVisualOnly && Run.bPowderRadiiOnly)
	{
		Run.Errors.Add(TEXT("-VisualOnly and -ApplyPowderRadiiOnly are mutually exclusive."));
		Run.bSuccess = false;
	}
	else if (Run.bVisualOnly && !Run.bApply)
	{
		Run.Errors.Add(TEXT("-VisualOnly requires -Apply."));
		Run.bSuccess = false;
	}
	else
	{
		Run.bSuccess = Execute(Run);
		if (Run.bSuccess && Run.bDiagnose && !Run.bApply)
		{
			DiagnoseParticleRuntime(Run);
		}
	}
	for (const FString& Error : Run.Errors)
	{
		UE_LOG(LogTemp, Error, TEXT("[M6.3.6VisualAssetSetup] %s"), *Error);
	}
	UE_LOG(LogTemp, Display,
		TEXT("[M6.3.6VisualAssetSetup] mode=%s success=%s created=%d reused=%d configChanged=%s"),
		!Run.bApply ? TEXT("AuditOnly") : (Run.bPowderRadiiOnly ? TEXT("ApplyPowderRadiiOnly")
			: (Run.bVisualOnly ? TEXT("ApplyVisualOnly") : TEXT("Apply"))),
		Run.bSuccess ? TEXT("true") : TEXT("false"),
		Run.CreatedAssets.Num(), Run.ReusedGeneratedAssets.Num(), Run.bChangedConfig ? TEXT("true") : TEXT("false"));
	if (!WriteReport(Run))
	{
		UE_LOG(LogTemp, Error, TEXT("[M6.3.6VisualAssetSetup] Failed to write %s"), ReportPath);
		return 1;
	}
	return Run.bSuccess ? 0 : 1;
#else
	UE_LOG(LogTemp, Error, TEXT("This commandlet requires an editor build."));
	return 1;
#endif
}
