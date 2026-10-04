# ActionSystem automation tests

Automation test source is grouped by the project's main `M` milestone, with reusable fixtures under `Shared`.
Moving a source file does not rename or change its Unreal automation test path, so existing automation filters remain stable.

## Layout

- `M0`–`M5`: tests belonging to the corresponding main milestone.
- `M4/PMM`: legacy PMM regression and audit tests. The milestone gate records this completed work as M4.2 predecessor/foundation evidence; `PMM` is retained as a subsystem label, not a separate active milestone.
- `M5/DescendingThrust`: descending-thrust implementation tests. Files named `MHGZM2...` cover a local M2 subphase inside the main M5 feature; they are intentionally stored under M5. Their `MHGZ.M2.DescendingThrust.*` automation paths are preserved for compatibility.
- `M5/KinsectSlash`, `M5/KinsectPiercer`, `M5/Aerial`, and `M5/Movement`: M5 feature slices.
- `M5/Shared`: M5-only reusable test types.
- `Shared`: fixtures and reflected test types shared across milestones/features. Keep each reflected type's `.h` and `.cpp` together where applicable.

## Maintenance

When adding a test, classify it by the main milestone gate in `docs/design/milestone-gates.md`, not only by a local feature subphase or its Unreal test-path prefix. Keep the test registration string unchanged when reorganizing files unless a deliberate automation-filter migration is intended.
