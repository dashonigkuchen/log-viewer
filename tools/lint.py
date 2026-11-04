#!/usr/bin/env python3
"""Formatting and static analysis helper used by the CMake targets format, format-check, tidy.

Usage:
  lint.py format       --clang-format EXE
  lint.py format-check --clang-format EXE
  lint.py tidy         --clang-tidy EXE --build-dir DIR [--all] [--base REF] [--jobs N]

Files are the C++ sources tracked by git (plus untracked, not ignored ones) in
src/, tests/, bench/, and tools/. The tidy command checks only .cpp files changed
against the merge base with --base (default origin/main), or all with --all or
the environment variable LV_TIDY_ALL=1. If a header changed, all .cpp files are checked.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import os
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SOURCE_DIRS = ("src", "tests", "bench", "tools")
CPP_SUFFIXES = {".cpp", ".h", ".hpp"}
REQUIRED_MAJOR = 19


def git(*args: str) -> str:
    return subprocess.run(
        ["git", *args], cwd=REPO, check=True, capture_output=True, text=True
    ).stdout


def source_files() -> list[Path]:
    out = git("ls-files", "--cached", "--others", "--exclude-standard", "--", *SOURCE_DIRS)
    files = []
    for line in out.splitlines():
        path = REPO / line
        if path.suffix in CPP_SUFFIXES and path.is_file():
            files.append(path)
    return sorted(set(files))


def check_version(exe: str) -> None:
    out = subprocess.run([exe, "--version"], check=True, capture_output=True, text=True).stdout
    match = re.search(r"version (\d+)\.", out)
    if not match or int(match.group(1)) != REQUIRED_MAJOR:
        sys.exit(f"{exe} must be major version {REQUIRED_MAJOR} (found: {out.strip()})")


def run_format(exe: str, check: bool) -> int:
    check_version(exe)
    files = source_files()
    args = [exe, "--dry-run", "--Werror"] if check else [exe, "-i"]
    result = subprocess.run([*args, *map(str, files)], cwd=REPO)
    if check and result.returncode != 0:
        print("format-check failed: run the 'format' target to fix.", file=sys.stderr)
    elif result.returncode == 0:
        print(f"{'Checked' if check else 'Formatted'} {len(files)} files.")
    return result.returncode


def changed_files(base: str) -> list[Path] | None:
    """Changed C++ files vs. the merge base, or None when everything must be checked."""
    try:
        merge_base = git("merge-base", base, "HEAD").strip()
    except subprocess.CalledProcessError:
        print(f"Cannot find merge base with {base}; checking all files.")
        return None
    names = set(git("diff", "--name-only", "--diff-filter=d", merge_base).splitlines())
    names |= set(git("ls-files", "--others", "--exclude-standard").splitlines())
    paths = [REPO / n for n in names if (REPO / n).suffix in CPP_SUFFIXES]
    paths = [p for p in paths if p.is_file() and p.relative_to(REPO).parts[0] in SOURCE_DIRS]
    if any(p.suffix != ".cpp" for p in paths):
        return None
    return paths


def run_tidy(exe: str, build_dir: Path, check_all: bool, base: str, jobs: int) -> int:
    check_version(exe)
    if not (build_dir / "compile_commands.json").is_file():
        sys.exit(f"{build_dir}/compile_commands.json not found; configure the preset first.")
    all_cpp = [p for p in source_files() if p.suffix == ".cpp"]
    selected = None if check_all else changed_files(base)
    files = all_cpp if selected is None else [p for p in selected if p in set(all_cpp)]
    if not files:
        print("tidy: no changed .cpp files.")
        return 0

    def tidy_one(path: Path) -> tuple[Path, int, str]:
        proc = subprocess.run(
            [exe, "-p", str(build_dir), "--quiet", str(path)],
            cwd=REPO,
            capture_output=True,
            text=True,
        )
        return path, proc.returncode, proc.stdout + proc.stderr

    failed = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        for path, code, output in pool.map(tidy_one, files):
            rel = path.relative_to(REPO)
            if code != 0:
                failed += 1
                print(f"--- {rel}: FAILED\n{output}")
            else:
                print(f"--- {rel}: ok")
    print(f"tidy: {len(files) - failed}/{len(files)} files clean.")
    return 1 if failed else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", choices=["format", "format-check", "tidy"])
    parser.add_argument("--clang-format", default="clang-format")
    parser.add_argument("--clang-tidy", default="clang-tidy")
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--all", action="store_true")
    parser.add_argument("--base", default="origin/main")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 2)
    args = parser.parse_args()

    if args.command in ("format", "format-check"):
        return run_format(args.clang_format, args.command == "format-check")
    if args.build_dir is None:
        parser.error("tidy needs --build-dir")
    check_all = args.all or os.environ.get("LV_TIDY_ALL") == "1"
    return run_tidy(args.clang_tidy, args.build_dir.resolve(), check_all, args.base, args.jobs)


if __name__ == "__main__":
    sys.exit(main())
