#!/usr/bin/env python3
"""Formatting and static analysis helper used by the CMake targets format, format-check, tidy.

Usage:
  lint.py format       --clang-format EXE
  lint.py format-check --clang-format EXE
  lint.py tidy         --clang-tidy EXE --build-dir DIR [--all] [--base REF] [--jobs N]
  lint.py no-network

no-network (also part of format-check) fails if src/ uses network APIs (SPEC F-ROB-4; only
QLocalServer/QLocalSocket for the single instance pipe are allowed) or translation machinery
(QTranslator, tr(), .ts files; SPEC F-I18N-1).

Files are the C++ sources tracked by git (plus untracked, not ignored ones) in
src/, tests/, bench/, and tools/. The tidy command checks only .cpp files changed
against the merge base with --base (default origin/main), or all with --all or
the environment variable LV_TIDY_ALL=1. If a header changed, all .cpp files are checked.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import json
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


# SPEC F-ROB-4: no network access of any kind. Local IPC (QLocalServer/QLocalSocket) is fine.
NETWORK_PATTERNS = re.compile(
    r"\b(QNetworkAccessManager|QNetworkRequest|QNetworkReply|QTcpSocket|QTcpServer|QUdpSocket|"
    r"QSslSocket|QHostAddress|QHostInfo|QDnsLookup|QHttp\w*|QWebSocket\w*|QAbstractSocket|"
    r"WSAStartup|WinHttp\w*|InternetOpen\w*|URLDownloadToFile\w*|getaddrinfo|gethostbyname|"
    r"curl_\w+)\b|#\s*include\s*<(sys/socket\.h|netdb\.h|winsock2?\.h|ws2tcpip\.h|"
    r"wininet\.h|winhttp\.h)>"
)


# SPEC F-I18N-1 / ADR 0013: English-only UI, no translation machinery.
TRANSLATION_PATTERNS = re.compile(r"\b(QTranslator|installTranslator|qsTr|QT_TR_NOOP|tr)\s*\(|\bQTranslator\b")


def find_pattern_uses(files: list[Path], pattern: re.Pattern[str]) -> list[str]:
    problems = []
    for path in files:
        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            code = line.split("//", 1)[0]
            match = pattern.search(code)
            if match:
                problems.append(f"{path.relative_to(REPO)}:{number}: {match.group(0)}")
    return problems


def find_network_uses(files: list[Path]) -> list[str]:
    return find_pattern_uses(files, NETWORK_PATTERNS)


def find_translation_uses(files: list[Path]) -> list[str]:
    return find_pattern_uses(files, TRANSLATION_PATTERNS)


def run_no_network() -> int:
    files = [path for path in source_files() if path.relative_to(REPO).parts[0] == "src"]
    network = find_network_uses(files)
    for problem in network:
        print(f"network API used (SPEC F-ROB-4): {problem}")
    translation = find_translation_uses(files)
    # Only tracked files: CI checks out vcpkg (with TypeScript .ts scripts) inside the repo.
    tracked = subprocess.run(["git", "ls-files", "*.ts"], cwd=REPO, capture_output=True,
                             text=True, check=True).stdout.split()
    translation += tracked
    for problem in translation:
        print(f"translation used (SPEC F-I18N-1): {problem}")
    problems = len(network) + len(translation)
    print(f"no-network: {len(files)} files checked, {problems} problem(s).")
    return 1 if problems else 0


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
    compiled = {
        Path(entry["file"]).resolve()
        for entry in json.loads((build_dir / "compile_commands.json").read_text(encoding="utf-8"))
    }
    all_cpp = [p for p in source_files() if p.suffix == ".cpp"]
    skipped = [p for p in all_cpp if p.resolve() not in compiled]
    for path in skipped:
        print(f"--- {path.relative_to(REPO)}: skipped (not built in {build_dir.name})")
    all_cpp = [p for p in all_cpp if p not in skipped]
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
    parser.add_argument("command", choices=["format", "format-check", "tidy", "no-network"])
    parser.add_argument("--clang-format", default="clang-format")
    parser.add_argument("--clang-tidy", default="clang-tidy")
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--all", action="store_true")
    parser.add_argument("--base", default="origin/main")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 2)
    args = parser.parse_args()

    if args.command == "no-network":
        return run_no_network()
    if args.command == "format":
        return run_format(args.clang_format, False)
    if args.command == "format-check":
        # The CI format job also enforces F-ROB-4.
        return run_format(args.clang_format, True) or run_no_network()
    if args.build_dir is None:
        parser.error("tidy needs --build-dir")
    check_all = args.all or os.environ.get("LV_TIDY_ALL") == "1"
    return run_tidy(args.clang_tidy, args.build_dir.resolve(), check_all, args.base, args.jobs)


if __name__ == "__main__":
    sys.exit(main())
