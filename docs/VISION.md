# Vision

## Problem

Developers on Windows need to read and filter large, live-growing text log files. Existing fast viewers are either unmaintained or slow on big files. The owner needs a tool that is as fast as the best existing native log viewers, is maintained with automated dependency updates, and can be built from source.

## Product

A native desktop log viewer that:

1. Opens a log file instantly, even if it is several gigabytes, and lets the user scroll anywhere without delay.
2. Shows a **filtered view below the main view** with all lines that match a filter (regular expression, literal text, or a boolean combination with `AND`, `OR`, `NOT`), and keeps it in sync with the main view.
3. **Follows live logs**: new lines appear within a fraction of a second; truncation and rotation are handled without user action.
4. Lets the user **mark lines**, set **bookmarks**, and define **highlight rules** that color lines or matches.
5. Opens **multiple files in tabs**.
6. Remembers a **filter history** so that common filters are one click away.

## Primary user and environment

- One developer on a Windows 11 work notebook. Logs are local files, typically about 50 MB, UTF-8, sometimes in a legacy Windows code page ("ANSI", Windows-1252). Network shares are not a target; users copy files locally first.
- The tool must still handle files of 10 GB and more without loading them into memory.
- Distribution: unsigned portable zip, no installer, no administrator rights needed.

## Quality goals (in priority order)

1. **Correctness.** Never show wrong content, wrong line numbers, or miss a match.
2. **Speed.** Indexing and searching are bound by disk and memory bandwidth, not by the implementation. Budgets: `docs/PERFORMANCE.md`.
3. **Responsiveness.** The UI never freezes. Every long operation shows progress and can be canceled.
4. **Low maintenance.** Few dependencies, all updated automatically by Dependabot and CI.
5. **Safety.** Read-only access to logs, no network access, no telemetry.

## Platforms

- **Windows 11 x64:** primary, released, manually tested by the owner.
- **Linux x64:** built and tested in CI, used by agents for development. Not packaged.
- **macOS:** not a goal. Code should stay portable (Qt), but nobody tests it.

## Non-goals for version 1.0

- Compressed files (`.gz`, `.zip`).
- Scratchpad or note windows.
- Predefined filter library (the filter history replaces it).
- Remote files, SSH, network shares optimizations, cloud log services.
- Merging several files into one timeline, timestamp or column parsing.
- Editing files.
- Plugins or scripting.
- Code signing (possible later, see ADR 0010).

These can move into the backlog after 1.0 if the owner decides so.

## Success criteria for 1.0

- The owner replaces klogg in daily work with this tool.
- All budgets in `docs/PERFORMANCE.md` are met on the owner's notebook.
- All features marked **Must** in `docs/SPEC.md` are implemented and tested.
