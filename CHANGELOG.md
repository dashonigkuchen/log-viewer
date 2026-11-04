# Changelog

All notable changes to this project are documented in this file.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project
adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- Project plan, agent rules, roadmap, and architecture decision records.
- CMake build with presets for Linux (GCC, Clang, sanitizers) and Windows (MSVC), vcpkg manifest.
- Formatting and static analysis (clang-format, clang-tidy), Linux and Windows CI, portable
  Windows package.
- Main window skeleton with About dialog, `--help` and `--version`, application log file.
- Test log generator `loggen` and benchmark harness `lv_bench`.
- Dependabot updates and a weekly nightly workflow.
