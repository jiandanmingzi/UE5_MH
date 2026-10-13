# ActionSystem automation tests

> **2026-10-13：回归套件已封存为按需启用的阶段基线。** Demo 已阶段性完成，后续小修小补不默认运行回归套件，也不默认验证全部 Content。现行规则见 [维护规范](../../../../docs/using/README.md)，历史成绩与恢复入口见 [封存记录](../../../../docs/archive/demo-2026-10-13/regression-baseline.md)。

封存保留测试源码、注册名与 M 阶段分类，不删除、不改断言、不搬出编译目录，也不禁用反射夹具或引擎正常的资产检查。仅在用户指明、重大/跨系统修改、确认回归风险或正式发布等必要情况下，按最小相关范围手动启用；不把每次修改都升级为全量测试。遥测/游戏录制器和定向诊断工具不因此停用。

Automation test source is grouped by the project's main `M` milestone, with reusable fixtures under `Shared`.
Moving a source file does not rename or change its Unreal automation test path, so existing automation filters remain stable.

## Layout

- `M0`–`M6`: tests belonging to the corresponding main milestone.
- `M4/PMM`: legacy PMM regression and audit tests. The milestone gate records this completed work as M4.2 predecessor/foundation evidence; `PMM` is retained as a subsystem label, not a separate active milestone.
- `M5/DescendingThrust`: descending-thrust implementation tests. Files named `MHGZM2...` cover a local M2 subphase inside the main M5 feature; they are intentionally stored under M5. Their `MHGZ.M2.DescendingThrust.*` automation paths are preserved for compatibility.
- `M5/KinsectSlash`, `M5/KinsectPiercer`, `M5/Aerial`, and `M5/Movement`: M5 feature slices.
- `M5/Shared`: M5-only reusable test types.
- `M6`: Kinsect, mark, powder, powder vortex, resource, and UI feature tests.
- `Shared`: fixtures and reflected test types shared across milestones/features. Keep each reflected type's `.h` and `.cpp` together where applicable.

## Maintenance

When adding a test, classify it by the main milestone gate in `docs/design/milestone-gates.md`, not only by a local feature subphase or its Unreal test-path prefix. Keep the test registration string unchanged when reorganizing files unless a deliberate automation-filter migration is intended.
