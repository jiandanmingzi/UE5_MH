// Copyright MHGZ Project. All Rights Reserved.
#include "Commandlets/MHGZFieldDocumentationCommandlet.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "K2Node_FunctionEntry.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectIterator.h"

namespace
{
bool HasChinese(const FString& Text)
{
	for (TCHAR Ch : Text) if (Ch >= 0x3400 && Ch <= 0x9fff) return true;
	return false;
}

TSharedRef<FJsonObject> Row(const FString& Owner, const FString& Name, const FString& Tip)
{
	auto R = MakeShared<FJsonObject>();
	R->SetStringField(TEXT("owner"), Owner);
	R->SetStringField(TEXT("name"), Name);
	R->SetStringField(TEXT("tooltip"), Tip);
	R->SetBoolField(TEXT("chinese"), HasChinese(Tip));
	return R;
}

// 只记录图结构/变量语义；元数据变更不得改动连线、类型、默认值或变量身份。
FString GraphSignature(UBlueprint* BP)
{
	TArray<FString> Parts;
	auto AddVariable = [&](const FBPVariableDescription& Var, const FString& Scope)
	{
		FBPVariableDescription Copy = Var;
		Copy.RemoveMetaData(TEXT("tooltip"));
		FString Text;
		FBPVariableDescription::StaticStruct()->ExportText(Text, &Copy, nullptr, BP, PPF_None, nullptr);
		Parts.Add(TEXT("V:") + Scope + TEXT(":") + Text);
	};
	for (const auto& Var : BP->NewVariables) AddVariable(Var, FString());
	TArray<UEdGraph*> Graphs;
	BP->GetAllGraphs(Graphs);
	for (const UEdGraph* Graph : Graphs)
	{
		Parts.Add(TEXT("G:") + Graph->GetName() + TEXT(":") + Graph->GraphGuid.ToString());
		for (const UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node) continue;
			if (const auto* Entry = Cast<UK2Node_FunctionEntry>(Node))
				for (const auto& Var : Entry->LocalVariables) AddVariable(Var, Graph->GetName());
			Parts.Add(TEXT("N:") + Node->NodeGuid.ToString() + TEXT(":") + Node->GetClass()->GetPathName());
			for (const UEdGraphPin* Pin : Node->Pins)
			{
				if (!Pin) continue;
				FString P = TEXT("P:") + Pin->PinId.ToString() + TEXT(":") + Pin->PinName.ToString()
					+ TEXT(":") + Pin->DefaultValue + TEXT(":") + GetPathNameSafe(Pin->DefaultObject);
				TArray<FString> Links;
				for (const auto* Link : Pin->LinkedTo) if (Link) Links.Add(Link->PinId.ToString());
				Links.Sort(); Parts.Add(P + TEXT(":") + FString::Join(Links, TEXT(",")));
			}
		}
	}
	Parts.Sort(); return FString::Join(Parts, TEXT("\n"));
}
}

UMHGZFieldDocumentationCommandlet::UMHGZFieldDocumentationCommandlet()
{
	IsClient = false; IsServer = false; IsEditor = true; LogToConsole = true;
}

int32 UMHGZFieldDocumentationCommandlet::Main(const FString& Params)
{
	FString ReportPath, DocsPath;
	FParse::Value(*Params, TEXT("Report="), ReportPath);
	FParse::Value(*Params, TEXT("BlueprintDocs="), DocsPath);
	if (ReportPath.IsEmpty()) ReportPath = FPaths::ProjectSavedDir() / TEXT("Maintenance/FieldDocumentation/report.json");
	const bool bApply = FParse::Param(*Params, TEXT("Apply"));
	const bool bRequireChinese = FParse::Param(*Params, TEXT("RequireChinese"));
	TSharedPtr<FJsonObject> Docs;
	if (!DocsPath.IsEmpty())
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *DocsPath)
			|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Docs)) return 1;
	}
	if (bApply && !Docs.IsValid()) return 1;
	auto Report = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Native, Variables, Changed;
	int32 MissingNative = 0, MissingVariables = 0;
	for (TObjectIterator<UStruct> It; It; ++It)
	{
		if (It->IsA<UFunction>()) continue;
		const FString Package = It->GetOutermost()->GetName();
		if (Package != TEXT("/Script/MHGZ") && Package != TEXT("/Script/MHGZToolsEditor")) continue;
		for (TFieldIterator<FProperty> Prop(*It, EFieldIterationFlags::None); Prop; ++Prop)
		{
			const FString Tip = Prop->GetMetaData(TEXT("ToolTip"));
			MissingNative += HasChinese(Tip) ? 0 : 1;
			Native.Add(MakeShared<FJsonValueObject>(Row(It->GetName(), Prop->GetName(), Tip)));
		}
	}
	FAssetRegistryModule& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	Registry.Get().SearchAllAssets(true);
	TArray<FAssetData> Assets;
	Registry.Get().GetAssetsByPath(TEXT("/Game"), Assets, true);
	Assets.Sort([](const auto& A, const auto& B) { return A.PackageName.LexicalLess(B.PackageName); });
	for (const auto& Data : Assets)
	{
		const FString Package = Data.PackageName.ToString();
		const FName Kind = Data.AssetClassPath.GetAssetName();
		if (Package.StartsWith(TEXT("/Game/TemplateAssets/"))
			|| (Kind != TEXT("Blueprint") && Kind != TEXT("AnimBlueprint") && Kind != TEXT("WidgetBlueprint"))) continue;
		UBlueprint* BP = Cast<UBlueprint>(Data.GetAsset());
		if (!BP) continue;
		const FString Before = GraphSignature(BP);
		bool bChanged = false;
		auto Process = [&](FBPVariableDescription& Var, const FString& Scope)
		{
			const FString Key = Scope.IsEmpty() ? Var.VarName.ToString() : Scope + TEXT("::") + Var.VarName.ToString();
			FString Tip = Var.HasMetaData(TEXT("tooltip")) ? Var.GetMetaData(TEXT("tooltip")) : FString();
			if (bApply)
			{
				const TSharedPtr<FJsonObject>* PackageDocs;
				FString NewTip;
				if (Docs->TryGetObjectField(Package, PackageDocs) && (*PackageDocs)->TryGetStringField(Key, NewTip))
				{
					if (!HasChinese(NewTip)) { UE_LOG(LogTemp, Error, TEXT("字段说明缺少中文：%s %s"), *Package, *Key); return; }
					if (NewTip != Tip)
					{
						Var.SetMetaData(TEXT("tooltip"), NewTip); Tip = NewTip; bChanged = true;
						// 与编辑器变量元数据设置一致，同步骨架类和生成类；只改说明，不重建图或字节码。
						for (UClass* Class : { BP->GeneratedClass, BP->SkeletonGeneratedClass })
						{
							if (!Class) continue;
							UStruct* Owner = Scope.IsEmpty() ? static_cast<UStruct*>(Class) : Class->FindFunctionByName(*Scope);
							if (Owner) if (FProperty* Property = FindFProperty<FProperty>(Owner, Var.VarName))
								Property->SetMetaData(FName(TEXT("ToolTip")), *NewTip);
						}
					}
				}
			}
			auto R = Row(Package, Key, Tip);
			MissingVariables += HasChinese(Tip) ? 0 : 1;
			R->SetStringField(TEXT("type"), Var.VarType.PinCategory.ToString());
			R->SetStringField(TEXT("type_object"), GetPathNameSafe(Var.VarType.PinSubCategoryObject.Get()));
			R->SetStringField(TEXT("default"), Var.DefaultValue);
			R->SetStringField(TEXT("guid"), Var.VarGuid.ToString());
			if (BP->GeneratedClass)
			{
				UStruct* ReflectedOwner = Scope.IsEmpty() ? static_cast<UStruct*>(BP->GeneratedClass) : BP->GeneratedClass->FindFunctionByName(*Scope);
				if (ReflectedOwner) if (const FProperty* Property = FindFProperty<FProperty>(ReflectedOwner, Var.VarName))
					R->SetStringField(TEXT("reflected_tooltip"), Property->GetMetaData(TEXT("ToolTip")));
			}
			Variables.Add(MakeShared<FJsonValueObject>(R));
		};
		for (auto& Var : BP->NewVariables) Process(Var, FString());
		TArray<UEdGraph*> Graphs;
		BP->GetAllGraphs(Graphs);
		for (UEdGraph* Graph : Graphs) for (UEdGraphNode* Node : Graph->Nodes)
			if (auto* Entry = Cast<UK2Node_FunctionEntry>(Node)) for (auto& Var : Entry->LocalVariables) Process(Var, Graph->GetName());
		if (bChanged)
		{
			if (BP->Status == BS_Error || Before != GraphSignature(BP))
			{
				UE_LOG(LogTemp, Error, TEXT("蓝图状态或结构保护失败：%s"), *Package); return 1;
			}
			BP->MarkPackageDirty();
			FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone; Args.SaveFlags = SAVE_NoError;
			if (!UPackage::SavePackage(BP->GetOutermost(), BP,
				*FPackageName::LongPackageNameToFilename(Package, FPackageName::GetAssetPackageExtension()), Args)) return 1;
			Changed.Add(MakeShared<FJsonValueString>(Package));
		}
	}
	Report->SetArrayField(TEXT("native"), Native);
	Report->SetArrayField(TEXT("blueprint_variables"), Variables);
	Report->SetArrayField(TEXT("changed_blueprints"), Changed);
	Report->SetNumberField(TEXT("missing_native"), MissingNative);
	Report->SetNumberField(TEXT("missing_blueprint_variables"), MissingVariables);
	FString Json;
	FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json));
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(ReportPath), true);
	if (!FFileHelper::SaveStringToFile(Json, *ReportPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) return 1;
	if (bRequireChinese && (MissingNative || MissingVariables))
	{
		UE_LOG(LogTemp, Error, TEXT("字段中文说明不完整：原生%d，蓝图%d"), MissingNative, MissingVariables);
		return 1;
	}
	return 0;
}
