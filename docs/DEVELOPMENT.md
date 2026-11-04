# Development

How to set up a machine, build, test, run CI, and release. Commands here are the source of truth; `AGENTS.md` §6 is a summary.

> Most of this is created in milestone M0. Until a step is merged, the commands it introduces do not work yet. Keep this file in sync with reality.

## 1. Toolchain

| Tool | Version | Notes |
| --- | --- | --- |
| CMake | ≥ 3.28 | Presets v6 |
| Ninja | ≥ 1.11 | Generator for all presets |
| GCC | ≥ 13 | Linux |
| Clang / clang-tidy / clang-format | ≥ 18 | Linux; sanitizer and tidy builds. Format rules must not depend on newer clang-format features than 18 |
| MSVC | Visual Studio 2022 17.10+ or Visual Studio 2026 (v143/v145 toolset) | Windows |
| vcpkg | Manifest mode, baseline pinned in `vcpkg.json` (`builtin-baseline`) | Non-Qt dependencies |
| Qt | ≥ 6.8 for development; releases use `.github/qt-version` (6.12 LTS line) | Linux dev: distribution packages; Windows and CI: `aqtinstall` |
| Python | ≥ 3.11 | Only for `aqtinstall` and small scripts in `tools/` |

### Linux (Debian 13 / Ubuntu 24.04) – the agent machine

```bash
sudo apt install build-essential g++ clang clang-tidy clang-format lld \
  cmake ninja-build ccache git curl zip unzip tar pkg-config \
  qt6-base-dev qt6-tools-dev qt6-tools-dev-tools qt6-l10n-tools \
  libgl-dev libxkbcommon-dev gdb
# vcpkg (once)
git clone https://github.com/microsoft/vcpkg ~/vcpkg && ~/vcpkg/bootstrap-vcpkg.sh -disableMetrics
echo 'export VCPKG_ROOT=$HOME/vcpkg' >> ~/.bashrc
```

Machine sizing for development: at least 4 cores, 8 GB RAM, 15 GB free disk (vcpkg binary cache + build trees + benchmark data). With less, use `-j2` and skip the `large` benchmark data.

### Windows 11 – owner machine

1. Visual Studio 2022 or 2026 with workload "Desktop development with C++" (includes CMake and Ninja).
2. vcpkg: `git clone https://github.com/microsoft/vcpkg C:\dev\vcpkg` and `C:\dev\vcpkg\bootstrap-vcpkg.bat -disableMetrics`; set user environment variable `VCPKG_ROOT=C:\dev\vcpkg`.
3. Qt: `pip install aqtinstall` then `aqt install-qt windows desktop <version from .github/qt-version> win64_msvc2022_64 -m qttranslations -O C:\dev\Qt`; set `QT_ROOT_DIR=C:\dev\Qt\<version>\msvc2022_64`.
4. Open "x64 Native Tools Command Prompt" (or let Visual Studio open the folder) and use the `windows-msvc-*` presets.

## 2. Build presets (created in M0-01)

| Preset | Platform | Compiler | Type | Purpose |
| --- | --- | --- | --- | --- |
| `linux-gcc-debug` | Linux | GCC | Debug | Default development build |
| `linux-clang-debug` | Linux | Clang | Debug | Second compiler |
| `linux-clang-asan` | Linux | Clang | Debug + ASan + UBSan | Memory and UB checks |
| `linux-clang-tsan` | Linux | Clang | Debug + TSan | Concurrency checks (core and model tests only) |
| `linux-release` | Linux | GCC | Release + LTO | Benchmarks |
| `windows-msvc-debug` | Windows | MSVC | Debug | Development on Windows |
| `windows-msvc-release` | Windows | MSVC | Release + LTCG | Benchmarks and release packages |

Every configure preset has a matching build and test preset of the same name. Build directory: `build/<preset>/`.

CMake options (all prefixed `LV_`): `LV_BUILD_TESTS` (ON), `LV_BUILD_BENCH` (ON in release presets), `LV_WARNINGS_AS_ERRORS` (ON), `LV_ENABLE_ASAN`, `LV_ENABLE_UBSAN`, `LV_ENABLE_TSAN`.

Compiler flags (set in `cmake/LvCompilerOptions.cmake`, never per target):

- GCC/Clang: `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Wnull-dereference -Wdouble-promotion -Wformat=2 -Wimplicit-fallthrough`, plus `-Werror`.
- MSVC: `/W4 /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor /EHsc /WX`, release `/O2 /GL` + linker `/LTCG`.
- Third-party headers are included as `SYSTEM`.
- Release builds target generic x86-64-v2 (SSE4.2, POPCNT); no `-march=native`.

## 3. Tests

```bash
ctest --preset linux-gcc-debug                 # all tests
ctest --preset linux-gcc-debug -L core          # by label: core, model, ui, fs (file-system integration), slow
ctest --preset linux-gcc-debug -R LineIndex     # by name
```

- UI tests run with `QT_QPA_PLATFORM=offscreen` (set by CTest properties).
- Tests labeled `slow` (> 5 s) do not run in the default PR CI job; they run in the nightly job.
- Fuzz-style tests: property tests with seeded random inputs inside Catch2 (`GENERATE(take(...))`). Real libFuzzer targets may be added later (`fuzz/`).

## 4. Benchmarks and test data

```bash
cmake --preset linux-release && cmake --build --preset linux-release
./build/linux-release/tools/loggen/loggen --profile small --seed 1 --out build/bench-data/small.log
./build/linux-release/bench/lv_bench --benchmark_filter=Index
```

See `docs/PERFORMANCE.md` for budgets and the results log.

## 5. Formatting and static analysis

- `.clang-format`: based on LLVM style, 4-space indent, 100 columns, pointer left (`int* p`), braces on own line for functions and classes, attached for control statements.
- `.clang-tidy`: `bugprone-*`, `cert-*` (selected), `concurrency-*`, `cppcoreguidelines-*` (selected), `misc-*`, `modernize-*`, `performance-*`, `portability-*`, `readability-*` (selected); `WarningsAsErrors: '*'`. Disabled checks are listed with a reason in the file.
- Targets: `format` (rewrite), `format-check` (CI), `tidy` (changed files vs. `origin/main`, or all with `LV_TIDY_ALL=1`).

## 6. Continuous integration (GitHub Actions)

The repository is private. GitHub Free includes USD 12 of runner usage per month for private repositories (about 2,000 Linux minutes or 1,200 Windows minutes at current rates). **No payment method is configured, so CI is blocked rather than billed when the allowance is used up.** When the repository becomes public, standard runners are free and the restrictions below can be relaxed.

Rules to stay within the allowance:

| Workflow | Trigger | Jobs | Budget per run |
| --- | --- | --- | --- |
| `ci.yml` | Pull request, push to `main` | `linux-gcc` (build + test), `linux-clang-asan` (build + test), `format-and-tidy` | ≤ 15 min total with caches |
| `ci-windows.yml` | Push to `main` when `src/`, `CMakeLists.txt`, `cmake/`, or `vcpkg.json` changed; PRs with label `windows`; manual | `windows-msvc-release` build + test + package artifact | ≤ 20 min |
| `nightly.yml` | Weekly (Monday 03:00 UTC) and manual | TSan, `slow` tests, full benchmark smoke run, Windows build | ≤ 40 min |
| `release.yml` | Tag `v*` (created only by the owner) | Windows release build, portable zip, SHA-256, draft GitHub release | ≤ 25 min |

- Caching: vcpkg binary cache (`VCPKG_BINARY_SOURCES` with the GitHub Actions cache), Qt install cache, ccache (`hendrikmuhs/ccache-action`).
- `concurrency:` groups cancel superseded runs of the same PR.
- `timeout-minutes` is set on every job.
- Docs-only changes (`**.md`, `docs/**`) skip `ci.yml` via `paths-ignore`, but a lightweight `docs` job still runs so the required status check reports success.
- Actions are pinned to a full commit SHA with the version in a comment; Dependabot updates them.
- The owner can check usage at GitHub → Settings → Billing and plans → Usage.

## 7. Dependency updates (Dependabot)

`.github/dependabot.yml` covers:

- `github-actions` (weekly): action versions.
- `vcpkg` (weekly): updates `builtin-baseline` in `vcpkg.json`, which moves PCRE2, simdutf, and the other vcpkg ports to their latest versions.
- Qt is not managed by Dependabot. The pinned version in `.github/qt-version` is checked weekly by `nightly.yml` against the latest patch release of the same minor line; if newer, the job opens an issue (`qt-update`) so an agent can bump it.

Security alerts: Dependabot alerts are enabled in the repository settings (owner). Owner gets e-mail notifications from GitHub for failed workflows and new Dependabot PRs.

Agent procedure for a Dependabot PR: check out the PR branch, build and run all tests, read the upstream changelogs for breaking changes, then comment the result. Merging stays with the owner (or auto-merge once the owner enables it).

## 8. Release

1. Owner decides to release; an agent prepares a PR that updates `CHANGELOG.md` and the version in `CMakeLists.txt` (`project(... VERSION x.y.z)`).
2. After merge, the owner creates the tag `vX.Y.Z` on `main`.
3. `release.yml` builds `LogViewer-X.Y.Z-windows-x64.zip`: `logviewer.exe`, Qt DLLs and plugins via `windeployqt --release --no-translations --no-system-d3d-compiler --no-opengl-sw` (Qt translations for de/en are added explicitly), MSVC runtime DLLs (app-local), `LICENSE`, `THIRD-PARTY-NOTICES.md`, `README.txt`. Plus `SHA256SUMS.txt`.
4. The release is created as a **draft**; the owner tests the zip on Windows 11 and publishes it.

Versioning: Semantic Versioning, `0.x` until 1.0.

The executable is not code-signed (ADR 0010). It carries version information (`VERSIONINFO` resource) and an application manifest (`longPathAware`, PerMonitorV2 DPI awareness, UTF-8 active code page, `supportedOS` Windows 10/11).

## 9. Third-party notices

`THIRD-PARTY-NOTICES.md` lists every shipped dependency with its license text (Qt LGPL-3.0 including how to obtain the Qt source and replace the DLLs, PCRE2, simdutf). It is updated in the same PR that adds or changes a shipped dependency.
