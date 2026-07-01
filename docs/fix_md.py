import re

with open('motion-matching.md', 'r', encoding='utf-8') as f:
    lines = f.readlines()

# Find the CMC section boundaries
start = None
end = None
for i, line in enumerate(lines):
    if line.startswith('## 4. CMC'):
        start = i
    if start is not None and line.startswith('---') and i > start + 5:
        end = i
        break

if start and end:
    new_lines = [
        '## 4. CMC 配置\n',
        '\n',
        '**不需要 `MOVE_Flying`。** RootMotion 在 `MOVE_Walking` 下完全正常工作——UE5 从 AnimGraph 提取根骨骼位移、应用到 CMC 胶囊体，不受行走模式限制。\n',
        '\n',
        '`MOVE_Walking` 保留以下关键能力：\n',
        '- 重力（`GravityScale`）——滞空/下落正常\n',
        '- 落地检测（`OnLanded()`）——着陆后重置协调器和 Tags\n',
        '- 地面约束——斜坡、台阶步上、胶囊体推挤\n',
        '- 空中速度衰减（`BrakingDecelerationFalling`）\n',
        '\n',
        '```cpp\n',
        '// MHGZCharacter 构造函数中（只需改两行）\n',
        'UCharacterMovementComponent* CMC = GetCharacterMovement();\n',
        'CMC->bOrientRotationToMovement = false;    // 旋转由 DoMove 手动控制\n',
        'CMC->bUseControllerDesiredRotation = false;\n',
        '// MaxWalkSpeed 设到足够大，防止 CMC 钳制 RootMotion 速度\n',
        '// SprintCruise(650) × MoveSpeedMultiplier(上限 ~1.5) ≈ 975 → 设 1200 有余量\n',
        'CMC->MaxWalkSpeed = 1200.f;\n',
        '```\n',
        '\n',
        '### 4.1 当前 CMC 配置项\n',
        '\n',
        '| 配置项 | 操作 | 原因 |\n',
        '|-------|------|------|\n',
        '| `MovementMode` | **保持 MOVE_Walking** | 重力、落地检测、地面物理全部依赖此模式 |\n',
        '| `MaxWalkSpeed` | **设 1200** | 大于 RootMotion 最高可能速度，防止 CMC 钳制 |\n',
        '| `MaxAcceleration` | 保留原值 | 影响 AddMovementInput 但不调它，无实际作用 |\n',
        '| `BrakingDecelerationWalking` | 保留原值 | 同上 |\n',
        '| `bOrientRotationToMovement` | `false` | 已有手动旋转 |\n',
        '| `RotationRate` | 不动 | 手动旋转不用此值 |\n',
        '| `JumpZVelocity` | 保留 | 跳跃仍可能用到 |\n',
        '| `AirControl` | 保留 | 空中摇杆微调 |\n',
        '| `GravityScale` | 保留 | 滞空/下落/落地检测全依赖 |\n',
        '| `BrakingDecelerationFalling` | 保留 | 空中水平速度衰减 |\n',
        '\n',
        '---\n',
    ]
    result = lines[:start] + new_lines + lines[end+1:]
else:
    result = lines

with open('motion-matching.md', 'w', encoding='utf-8') as f:
    f.writelines(result)

print(f'Replaced lines {start}-{end} ({len(new_lines)} new lines)')
