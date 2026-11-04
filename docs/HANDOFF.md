# Handoff

Current state of the project and notes for the next session. Update this file in every PR. Keep it short: replace outdated content instead of appending a diary.

## Current state

- Planning documents exist (README, AGENTS.md, VISION, SPEC, ARCHITECTURE, ROADMAP, PERFORMANCE, DEVELOPMENT, ADRs 0001–0011).
- M0-01 done: CMake skeleton with `lv_core`, `lv_model`, `lv_ui`, `lv_app` (`logviewer`), `lv_core_tests`; all Linux presets build and pass. The libraries contain only placeholders (`versionString()`, a bare `QMainWindow` factory). `src/ui/main_window_factory.*` is replaced by the real `MainWindow` in M0-05.
- M0-02 done: `.clang-format`, `.clang-tidy` (naming rules from AGENTS.md §5 enforced), `.editorconfig`, targets `format`/`format-check`/`tidy`. All sources are clean.
- M0-03 done: `ci.yml` with `changes`, `linux-gcc`, `linux-clang-asan`, `format-and-tidy`; Qt 6.12.0 on CI (Linux dev machine uses Debian's 6.8.2, so both versions are covered).
- M0-04 done: `ci-windows.yml`, version resource, manifest, placeholder icon (`src/app/resources/`), portable zip artifact. Owner still has to start the exe on Windows 11 once (roadmap note).
- M0-05 done: `lv::ui::MainWindow` (empty tab widget, File/Help menus, status bar, About dialog), `--help`/`--version` via `QCommandLineParser`, core log interface (`src/core/util/log.h`, part of M1-01 pulled forward), `lv::model::FileLogSink` (writes `logviewer.log` in the settings directory, rotated at 1 MiB to `.1`), `lv::model::settingsDirectory()` (`LV_SETTINGS_DIR` overrides it for tests; portable mode follows in M2-08). Test executables: `lv_core_tests`, `lv_model_tests`, `lv_ui_tests` (Qt Test, offscreen), plus CTest `app.version`/`app.help` (not on Windows: GUI exe shows a message box).
- Command line file arguments are accepted but only logged as "not implemented" until M2-06.
- M0-06 done: `tools/loggen` (library `lv_loggen` + CLI `loggen`, Qt-free). Own PRNG (xoshiro256**), integer-only formatting, so output is byte-identical on every platform; golden FNV-1a hashes in `tests/tools/loggen_test.cpp` guard it. Line format: `2026-01-01 00:00:00.036 DEBUG [thread] com.example.Logger - message`; 10% of INFO lines are `heartbeat ok` (for the boolean filter benchmark). Speed: 50 MB `small` in 0.14 s (release). New test executable `lv_tools_tests` (label `tools`).
- Next step: **M0-07 Benchmark harness** (M0-08 and M0-09 are also unblocked).

## Environment notes

- Agent machine: Debian 13 LXC, 4 cores, 8 GB RAM, toolchain installed (GCC 14.2, Clang 19.1, CMake 3.31, Ninja 1.12, Qt 6.8.2 from Debian). vcpkg is cloned at `~/vcpkg` (`VCPKG_ROOT` set in `~/.bashrc`). Clean builds of all presets take under a minute each after the first vcpkg install.
- The vcpkg `builtin-baseline` is the `2026.10.16` release commit. Dependabot will move it forward once M0-08 is in.
- Use `gh run list`/`gh run view` to read CI status (see DEVELOPMENT §6).
- Repository is private; mind the CI budget (`docs/DEVELOPMENT.md` §6).

## Open questions for the owner

- None.

## Known issues

- aqtinstall is pinned to an unreleased fix commit (ADR 0012). Remove the pin when an aqtinstall release supports Qt 6.12.
