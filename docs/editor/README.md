# UE5 编辑器操作文档

本目录只说明代码完成后需要在 Unreal Editor 5.6 中进行的操作，包括资产迁移、蓝图/DataAsset 接线、Montage/碰撞/UI 配置和人工验收。这里不是 C++ 架构或玩法规则的真相源。

## 使用顺序

1. 完成并编译 [设计目录](../design/README.md) 中对应里程碑的代码。
2. 按 [UE5.6 编辑器接线指南](demo-setup.md) 从 E0 开始操作。
3. 按 [验证方案](verification.md) 执行当前 Demo 验收。
4. 只有需要移动或重存资产时，读取 [资产目录整理规范](asset-organization.md)。

## 文档

| 文档 | 用途 |
|---|---|
| [demo-setup.md](demo-setup.md) | 重构代码完成后的完整编辑器操作顺序 |
| [verification.md](verification.md) | PIE、生命周期、伤害、输入和打包验收 |
| [asset-organization.md](asset-organization.md) | Content Browser/AssetTools 移动、Redirector 和引用保护 |

## 边界

- 字段或父类与指南不一致时停止操作，回到设计文档和 C++ 检查；不要在蓝图中猜测替代字段。
- 不在 Windows 资源管理器中移动、复制或覆盖 `.uasset/.umap`。
- 不用蓝图 Tick 复制 C++ 已负责的连招、资源、位移、猎虫和生命周期逻辑。
- 编辑器操作产生的新设计决策必须先写回 `../design/`，再继续制作资产。
