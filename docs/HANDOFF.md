# Handoff

Current state of the project and notes for the next session. Update this file in every PR. Keep it short: replace outdated content instead of appending a diary.

## Current state

- Planning documents exist (README, AGENTS.md, VISION, SPEC, ARCHITECTURE, ROADMAP, PERFORMANCE, DEVELOPMENT, ADRs 0001–0011).
- No code yet. Next step: **M0-01 CMake skeleton and presets**.

## Environment notes

- The agent machine (Debian 13 LXC) does not have a C++ toolchain installed yet. Before M0-01, the owner installs the packages listed in `docs/DEVELOPMENT.md` §1 (Linux). Debian 13 ships Qt 6.8.2, GCC 14, Clang 19, CMake 3.31.
- Until then, agents can only rely on CI and must state this in the PR.
- Repository is private; mind the CI budget (`docs/DEVELOPMENT.md` §6).

## Open questions for the owner

- None.

## Known issues

- None.
