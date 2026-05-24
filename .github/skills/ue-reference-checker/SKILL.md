---
name: ue-reference-checker
description: "Check Unreal Engine asset references, find broken or missing references, identify unused assets. Use when: reorganizing Content folders, moving or renaming assets, cleaning up unused resources"
argument-hint: "Target directory in Content/ to scan..."
---

# UE 资源引用检查器

## 使用场景
- 重构 `Content/` 目录结构
- 移动蓝图、材质、纹理等资源后检查引用断裂
- 清理未被任何其他资源引用的孤立资产
- 迁移资源到新模块目录

## 操作步骤

### 1. 扫描资源目录
```powershell
# 列出指定目录下所有 .uasset 文件
Get-ChildItem -Path "Content/<TargetDir>" -Recurse -Filter "*.uasset"
```

### 2. 检查交叉引用
- 分析蓝图之间的硬引用（Hard Reference）和软引用（Soft Reference）
- 硬引用：通过 `UPROPERTY()` 直接引用，加载时一起加载
- 软引用：`TSoftObjectPtr`/`FSoftObjectPath`，按需异步加载

### 3. 引用报告格式
```
资源路径 | 被引用次数 | 引用者列表 | 引用类型
```

### 4. 常见问题排查
- `.uasset` 移动后引用路径未更新 → 使用 Redirector 或 Core Redirects
- 被引用的资产被删除 → 检查引用者列表，恢复或更新引用
- 循环引用导致加载问题 → 将其中一方改为软引用

## 注意事项
- `__ExternalActors__/` 和 `__ExternalObjects__/` 目录是引擎自动管理的，不要手动修改
- `DerivedDataCache/` 是缓存目录，可安全清理
- 引用检查后建议在 UE 编辑器中执行 Resave Packages 修复
