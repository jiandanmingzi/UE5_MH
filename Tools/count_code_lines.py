#!/usr/bin/env python3
"""Count source-file lines in the MHGZ action-system tests and module.

Physical lines include blank lines. Non-empty lines exclude whitespace-only
lines but include comments. Only common C/C++ and Unreal Build.cs source files
are counted; documentation and generated/binary files are ignored.
"""

from pathlib import Path


CODE_EXTENSIONS = {
    ".c",
    ".cc",
    ".cpp",
    ".cs",
    ".cxx",
    ".h",
    ".hpp",
    ".inl",
}


def count_lines(directory: Path) -> tuple[int, int, int]:
    """Return (file count, physical line count, non-empty line count)."""
    files = sorted(
        path
        for path in directory.rglob("*")
        if path.is_file() and path.suffix.lower() in CODE_EXTENSIONS
    )

    physical_lines = 0
    non_empty_lines = 0
    for path in files:
        lines = path.read_text(encoding="utf-8-sig", errors="replace").splitlines()
        physical_lines += len(lines)
        non_empty_lines += sum(bool(line.strip()) for line in lines)

    return len(files), physical_lines, non_empty_lines


def print_report(label: str, directory: Path, result: tuple[int, int, int]) -> None:
    file_count, physical_lines, non_empty_lines = result
    print(f"{label}: {directory}")
    print(f"  文件数: {file_count}")
    print(f"  总行数（含空行）: {physical_lines}")
    print(f"  非空行数（含注释）: {non_empty_lines}")


def main() -> int:
    project_root = Path(__file__).resolve().parent.parent
    tests_directory = project_root / "Source" / "MHGZ" / "ActionSystem" / "Tests"
    module_directory = project_root / "Source" / "MHGZ"

    missing = [path for path in (tests_directory, module_directory) if not path.is_dir()]
    if missing:
        for path in missing:
            print(f"目录不存在: {path}")
        return 1

    print("代码行数统计（范围：.c/.cc/.cpp/.cs/.cxx/.h/.hpp/.inl）")
    print_report("ActionSystem/Tests", tests_directory, count_lines(tests_directory))
    print_report("MHGZ 模块总计（包含上述 Tests）", module_directory, count_lines(module_directory))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
