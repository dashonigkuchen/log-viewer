# Log Viewer

> Working title. The project will likely be renamed before its first public release.

A fast, native desktop log viewer for large text log files, written in C++23 and Qt 6 Widgets.

**Status:** planning. No usable build yet. See [docs/ROADMAP.md](docs/ROADMAP.md).

## Goals

- Open large log files instantly and scroll through them smoothly. Files are read directly from disk and never fully loaded into memory.
- Filter with regular expressions and boolean expressions (`AND`, `OR`, `NOT`) in a separate filtered view below the main view.
- Follow live logs (tail) while they are being written, rotated, or truncated.
- Marks, bookmarks, configurable highlight rules, multiple tabs, and a filter history.
- Primary platform: Windows 11 (portable, no installer). Linux is built and tested in CI as a second platform.
- No network access, no telemetry, no update checker.

See [docs/VISION.md](docs/VISION.md) for the full scope and [docs/SPEC.md](docs/SPEC.md) for the detailed behavior.

## Try it

There is no release yet. Preview builds for Windows 11 (x64) are produced by CI:

1. Open the latest successful [CI Windows run on `main`](https://github.com/dashonigkuchen/log-viewer/actions/workflows/ci-windows.yml?query=branch%3Amain+is%3Asuccess) (requires access to this repository).
2. Download the artifact `LogViewer-<version>-<sha>-windows-x64` (kept for 7 days) and unzip it.
3. Start `logviewer.exe`. The executable is not signed yet, so SmartScreen may ask: choose "More info", then "Run anyway".
4. Open a log file with `Ctrl+O`, drag and drop, or `logviewer.exe path\to\file.log`.

Put an empty `portable.ini` next to `logviewer.exe` to keep settings in that folder instead of `%APPDATA%\LogViewer`.

To generate a test log of about 60 MB in PowerShell:

```powershell
$path = "$env:USERPROFILE\Desktop\test.log"
$w = [System.IO.StreamWriter]::new($path, $false, [System.Text.UTF8Encoding]::new($false))
$levels = 'INFO','INFO','INFO','DEBUG','WARN','ERROR'
$rnd = [Random]::new(42)
$t = [datetime]'2026-11-06 08:00:00'
for ($i = 0; $i -lt 500000; $i++) {
  $t = $t.AddMilliseconds($rnd.Next(1, 50))
  $lvl = $levels[$rnd.Next($levels.Count)]
  $line = "{0:yyyy-MM-dd HH:mm:ss.fff} [{1,-5}] [thread-{2}] OrderService - Processed request id={3} duration={4}ms" -f $t, $lvl, $rnd.Next(1,16), $i, $rnd.Next(1,900)
  $w.WriteLine($line)
}
$w.Close()
```

Filtering (M3) and live tailing (M4) are not available yet; see [the roadmap](docs/ROADMAP.md).

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
