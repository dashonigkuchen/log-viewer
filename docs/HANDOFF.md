# Handoff

Current state of the project and notes for the next session. Update this file in every PR. Keep it short: replace outdated content instead of appending a diary.

## Current state

- Planning documents exist (README, AGENTS.md, VISION, SPEC, ARCHITECTURE, ROADMAP, PERFORMANCE, DEVELOPMENT, ADRs 0001–0011).
- M0-01 done: CMake skeleton with `lv_core`, `lv_model`, `lv_ui`, `lv_app` (`logviewer`), `lv_core_tests`; all Linux presets build and pass. The libraries contain only placeholders (`versionString()`, a bare `QMainWindow` factory). `src/ui/main_window_factory.*` is replaced by the real `MainWindow` in M0-05.
- Next step: **M0-02 Formatting and static analysis** (M0-05 and M0-06 are also unblocked).

## Environment notes

- Agent machine: Debian 13 LXC, 4 cores, 8 GB RAM, toolchain installed (GCC 14.2, Clang 19.1, CMake 3.31, Ninja 1.12, Qt 6.8.2 from Debian). vcpkg is cloned at `~/vcpkg` (`VCPKG_ROOT` set in `~/.bashrc`). Clean builds of all presets take under a minute each after the first vcpkg install.
- The vcpkg `builtin-baseline` is the `2026.10.16` release commit. Dependabot will move it forward once M0-08 is in.
- No CI exists until M0-03; PRs before that are verified locally only.
- Repository is private; mind the CI budget (`docs/DEVELOPMENT.md` §6).

## Open questions for the owner

- None.

## Known issues

- None.
