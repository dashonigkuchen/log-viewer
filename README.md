# Log Viewer

> Working title. The project will likely be renamed before its first public release.

A fast, native desktop log viewer for large text log files, written in C++20 and Qt 6 Widgets.

**Status:** planning. No usable build yet. See [docs/ROADMAP.md](docs/ROADMAP.md).

## Goals

- Open large log files instantly and scroll through them smoothly. Files are read directly from disk and never fully loaded into memory.
- Filter with regular expressions and boolean expressions (`AND`, `OR`, `NOT`) in a separate filtered view below the main view.
- Follow live logs (tail) while they are being written, rotated, or truncated.
- Marks, bookmarks, configurable highlight rules, multiple tabs, and a filter history.
- Primary platform: Windows 11 (portable, no installer). Linux is built and tested in CI as a second platform.
- No network access, no telemetry, no update checker.

See [docs/VISION.md](docs/VISION.md) for the full scope and [docs/SPEC.md](docs/SPEC.md) for the detailed behavior.

## How this project is built

This project is developed almost entirely by AI coding agents (mainly [Hermes Agent](https://github.com/NousResearch/hermes-agent), sometimes Claude Code). A human owner sets the direction, reviews, and tests.

The agents work through a detailed roadmap of small steps. Each step is one pull request that must pass the test suite, sanitizers, static analysis, and CI before it is merged. All rules that agents follow are in [AGENTS.md](AGENTS.md).

The code is a clean-room implementation. It contains no code from other log viewers. See [ADR 0003](docs/adr/0003-clean-room-mit-license.md).

## Documentation

| Document | Purpose |
| --- | --- |
| [AGENTS.md](AGENTS.md) | Binding rules and workflow for every contributor, human or AI |
| [docs/VISION.md](docs/VISION.md) | What the product is and is not |
| [docs/SPEC.md](docs/SPEC.md) | Detailed functional specification |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Modules, threading, data flow |
| [docs/ROADMAP.md](docs/ROADMAP.md) | Milestones and step-by-step plan |
| [docs/PERFORMANCE.md](docs/PERFORMANCE.md) | Performance budgets and how they are measured |
| [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) | Toolchain, build, test, CI, release |
| [docs/HANDOFF.md](docs/HANDOFF.md) | Current state and notes for the next session |
| [docs/adr/](docs/adr/README.md) | Architecture decision records |

## License

[MIT](LICENSE). Qt is used under the LGPL v3 and is linked dynamically.
