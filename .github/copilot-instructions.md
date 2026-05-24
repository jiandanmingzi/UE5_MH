# MHGZ 项目指南

## 项目概述
- Unreal Engine 5 第三人称动作游戏项目
- C++ 源码位于 `Source/MHGZ/`
- 蓝图资源位于 `Content/` 下各模块文件夹
- 构建目标：`MHGZEditor.Target.cs`（编辑器）、`MHGZ.Target.cs`（游戏）

## 模块结构
| 模块 | 路径 | 说明 |
|------|------|------|
| ThirdPerson | `Content/ThirdPerson/` | 第三人称基础角色与关卡 |
| Variant_Combat | `Content/Variant_Combat/` | 战斗系统变体（动画、输入、UI、VFX） |
| Variant_Platforming | `Content/Variant_Platforming/` | 平台跳跃变体（动画、输入、VFX） |
| Variant_SideScrolling | `Content/Variant_SideScrolling/` | 横版卷轴变体（动画、输入、UI） |

## 代码风格（C++）
- 遵循 [Unreal Engine C++ 编码规范](https://docs.unrealengine.com/5.0/en-US/epic-cplusplus-coding-standard-for-unreal-engine/)
- 类名使用 UE 前缀约定：`U`（UObject 派生）、`A`（Actor 派生）、`F`（纯 C++ 结构体/类）
- 头文件必须包含 `#pragma once`
- 所有需要反射的属性加 `UPROPERTY()` 宏，函数加 `UFUNCTION()` 宏
- 模块 API 宏：类声明使用 `MHGZ_API`

## 蓝图约定
- 蓝图类命名以 `BP_` 前缀
- 蓝图接口以 `BPI_` 前缀
- 枚举以 `E_` 前缀
- 使用命名空间或文件夹组织蓝图，避免平铺在根目录

## 构建与运行
- 生成项目文件：右键 `.uproject` → Generate Visual Studio project files
- 编译：通过 IDE 或 `Engine\Build\BatchFiles\Build.bat MHGZEditor Win64 Development`
- 不要直接修改 `Intermediate/` 和 `DerivedDataCache/` 目录的内容

## 通用约定
- 新增功能先在对应 `Variant_*` 或模块子目录下组织资源
- 共享资源（角色、材质、纹理）放在合适的共享目录
- 配置文件修改仅限 `Config/` 目录下的 `.ini` 文件
