# 0009. Dependency management with vcpkg and aqtinstall

- Status: Accepted
- Date: 2026-11-04

## Context
Dependencies must update automatically so the owner only gets notified when something breaks.

## Decision
Non-Qt libraries come from vcpkg in manifest mode with a pinned `builtin-baseline`, updated weekly by Dependabot (`package-ecosystem: vcpkg`). Qt is installed with aqtinstall on CI and Windows (version pinned in `.github/qt-version`, checked weekly by the nightly workflow), and from distribution packages on Linux development machines (minimum Qt 6.8). GitHub Actions are pinned by SHA and updated by Dependabot.

## Consequences
Updates arrive as PRs that CI validates. Building Qt from source through vcpkg is avoided (too slow for CI minutes).

## Alternatives considered
CPM/FetchContent with Git tags: Dependabot cannot update them. Conan: another tool for the owner to learn; no advantage here.
