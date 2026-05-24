---
description: "Explain how to implement a gameplay mechanic using Unreal Engine Blueprint nodes, with MHGZ project context"
argument-hint: "Describe the gameplay mechanic you want to implement..."
agent: agent
---

以 MHGZ 项目为例，解释如何在 UE5 蓝图中实现指定的游戏功能。

## 回答结构
1. **需求分析** — 简述该机制的游戏设计意图
2. **所需节点** — 列出所有需要的蓝图节点及其作用
3. **连接逻辑** — 描述节点间的执行流和数据流
4. **C++ 依赖** — 如果涉及 C++ 暴露的函数或事件，注明在哪个类中定义
5. **最佳实践** — 相关的性能优化建议

## 项目上下文
- 第三人称游戏，支持战斗、平台跳跃、横版卷轴三种变体
- 蓝图位于 `Content/ThirdPerson/Blueprints/`、`Content/Variant_Combat/Blueprints/` 等目录
- 输入使用 Enhanced Input System（IMC_Default.uasset）
- 角色基于 Mannequin 骨骼网格
