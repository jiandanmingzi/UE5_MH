---
description: "Use when writing or modifying Unreal Engine C++ code, including Actor, Pawn, Character, Component, GameMode, GameState, PlayerController, or any UObject-derived class"
applyTo: ["Source/**/*.h", "Source/**/*.cpp"]
---

# UE5 C++ 编码规范

## 文件结构
- 头文件使用 `#pragma once`
- 包含顺序：本类头文件 → 引擎头文件 → 项目头文件 → 第三方头文件
- 每个 `.h` 对应一个主要类声明

## 类声明模板

```cpp
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MHGZMyActor.generated.h"

UCLASS()
class MHGZ_API AMHGZMyActor : public AActor
{
    GENERATED_BODY()

public:
    AMHGZMyActor();

protected:
    virtual void BeginPlay() override;

public:
    virtual void Tick(float DeltaTime) override;
};
```

## 属性暴露规范
- 编辑器可编辑：`UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")`
- 蓝图只读：`UPROPERTY(VisibleAnywhere, BlueprintReadOnly)`
- 组件指针：`UPROPERTY(VisibleAnywhere)` 并在构造函数中用 `CreateDefaultSubobject<T>()` 创建
- 所有 Category 使用中文或英文统一语义分组

## 函数约定
| 宏 | 用途 |
|----|------|
| `UFUNCTION(BlueprintCallable)` | 蓝图可调用 |
| `UFUNCTION(BlueprintNativeEvent)` | 蓝图可重写（C++ 有默认实现，函数名加 `_Implementation`） |
| `UFUNCTION(BlueprintImplementableEvent)` | 纯蓝图实现（C++ 不实现函数体） |
| `UFUNCTION(Server, Reliable)` | 服务器 RPC |
| `UFUNCTION(NetMulticast, Reliable)` | 多播 RPC |

## 性能要点
- 避免在 `Tick()` 中做重计算或 `Cast`
- `TArray` 大量添加前调用 `Reserve()`
- 使用 `FObjectFinder`/`FClassFinder` 在构造函数中加载资产
- 避免在 `BeginPlay()` 中使用 `LoadObject` 同步加载

## 禁止事项
- 禁止使用裸指针管理 UObject（必须用 `UPROPERTY()` 或 `TWeakObjectPtr`/`TObjectPtr`）
- 禁止在析构函数中手动 `delete` UObject
- 禁止在构造函数中调用虚函数
