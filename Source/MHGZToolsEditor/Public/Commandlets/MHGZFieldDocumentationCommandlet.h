// Copyright MHGZ Project. All Rights Reserved.
#pragma once
#include "Commandlets/Commandlet.h"
#include "MHGZFieldDocumentationCommandlet.generated.h"

/** 检查项目原生反射字段和蓝图变量的中文说明；仅显式Apply时保存变量元数据。 */
UCLASS()
class MHGZTOOLSEDITOR_API UMHGZFieldDocumentationCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	UMHGZFieldDocumentationCommandlet();
	virtual int32 Main(const FString& Params) override;
};
