---
description: "Generate a new Unreal Engine C++ class for the MHGZ project following all conventions"
argument-hint: "Class name (e.g. AMHGZEnemy) and parent class (e.g. ACharacter)..."
agent: agent
---

为 MHGZ 项目生成一个新的 UE5 C++ 类。

## 要求
- 类名使用 `MHGZ` 前缀，遵循 UE 前缀约定（A/U/F）
- 放置于 `Source/MHGZ/` 下合适的子目录
- 头文件必须包含：
  - `#pragma once`
  - `#include "CoreMinimal.h"`
  - `<ClassName>.generated.h`（最后一行 include）
- 类声明包含 `GENERATED_BODY()` 和 `MHGZ_API` 宏
- 构造函数中初始化默认值
- 重写 `BeginPlay()` 虚函数
- 需要 Tick 则重写 `Tick(float DeltaTime)`
- 所有需要反射的属性和函数添加 UPROPERTY/UFUNCTION 宏
- 保持与项目现有代码风格一致
