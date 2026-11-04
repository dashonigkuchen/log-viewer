# AGENTS.md

These rules are **binding** for every contributor, human or AI agent. Read this file completely at the start of every session. When a rule here conflicts with any other document, this file wins. When something is unclear or missing, stop and ask the owner instead of guessing.

---

## 0. Session start checklist (do this every time)

1. Read, in this order:
   1. `AGENTS.md` (this file)
   2. `docs/HANDOFF.md` (current state, open problems, notes from the last session)
   3. `docs/ROADMAP.md` (find the next step, see §3)
   4. The documents the step references (`docs/SPEC.md`, `docs/ARCHITECTURE.md`, `docs/PERFORMANCE.md`, ADRs)
2. Sync: `git checkout main && git pull --ff-only`.
3. Check for open pull requests (`gh pr list`). If a previous step's PR is still open, do **not** start a step that depends on it. Report this to the owner.
4. Build and run the full test suite once on `main` to confirm a green baseline (see §6). If `main` is red, fixing it is the next step.

---

## 1. Language policy

| What | Language |
| --- | --- |
| Code, identifiers, comments, log messages, error messages | American English |
| Commit messages, branch names, PR titles and bodies | American English |
| README, AGENTS.md, everything in `docs/` except `docs/plans/` | American English |
| User-visible UI strings | American English only; no translations (ADR 0013) |
| Temporary plans (`docs/plans/`) | German, marked as temporary, deleted after implementation |
| Chat with the owner | German |

Use American spelling (`color`, `behavior`, `initialize`, `canceled`).

---

## 2. Project in one paragraph

A fast native desktop log viewer (C++20, Qt 6 Widgets). It indexes large text log files without loading them into memory, shows a main view and a filtered view below it, filters with regular expressions and boolean expressions, follows live logs, and supports marks, bookmarks, highlight rules, tabs, and a filter history. Primary platform is Windows 11 (portable zip). Linux is built and tested in CI. Details: `docs/VISION.md`, `docs/SPEC.md`.

---

## 3. Workflow: one roadmap step = one pull request

1. **Pick the step.** The next step is the first unchecked `- [ ]` step in `docs/ROADMAP.md` whose `Depends on` steps are all checked. Do exactly one step per session unless the owner says otherwise.
2. **Too big?** If the step clearly cannot be finished in one focused PR (rule of thumb: more than ~800 changed lines excluding tests, or touches more than one module boundary in a non-trivial way), do not start coding. Open a PR that only splits the step into smaller steps in `ROADMAP.md` (`M3-03a`, `M3-03b`, ...).
3. **Spec gap?** If the step needs a product decision that `SPEC.md` does not answer, stop and ask the owner. Record the answer in `SPEC.md` (or an ADR) in the same PR.
4. **Branch:** `step/<id>-<short-kebab-name>`, for example `step/m1-05-line-splitter`. Always branch from the latest `main`. Never stack on an unmerged branch.
5. **Implement** only what the step describes. No drive-by refactors outside the step's scope. If you find a bug outside the scope, add it to the "Known issues" section of `docs/HANDOFF.md` or open a GitHub issue.
6. **Tests first where practical.** Every behavior in the acceptance criteria has an automated test. Bug fixes start with a failing test.
7. **Verify locally** (§6): build, all tests, format check, clang-tidy on changed files. If you cannot build locally (for example no toolchain on the machine), say so explicitly in the PR body and rely on CI. Never claim a check passed that you did not run.
8. **Update documents in the same PR:**
   - Tick the step in `docs/ROADMAP.md` (`- [x]`) and add the PR number.
   - Update `docs/HANDOFF.md` (what changed, what the next step should know, new known issues).
   - Update `SPEC.md`, `ARCHITECTURE.md`, `PERFORMANCE.md`, or `DEVELOPMENT.md` when behavior, structure, budgets, or commands changed.
   - Add an ADR for every decision that a future agent could otherwise reverse by accident (§10).
9. **Commit** using Conventional Commits (§9). **Push**, open the PR with `gh pr create`, fill in the PR template.
10. **Wait for CI.** If CI fails, fix it in the same PR. If CI status cannot be read, tell the owner.
11. **Do not merge** unless the owner explicitly allowed it in the current session. Report to the owner in German: PR link, what was done, what was tested and how, anything not verified, the next step.

### Definition of done (all must be true)

- [ ] Acceptance criteria of the step are met and each has a test (UI-only behavior: Qt Test with `QT_QPA_PLATFORM=offscreen`, or an explicit manual test note in the PR when automation is not possible).
- [ ] Builds without warnings (warnings are errors) on GCC, Clang, and MSVC (MSVC verified by CI when required by the step, see `docs/DEVELOPMENT.md`).
- [ ] All tests pass, including AddressSanitizer and UndefinedBehaviorSanitizer runs.
- [ ] `clang-format` clean, no new `clang-tidy` findings.
- [ ] No performance budget in `docs/PERFORMANCE.md` regressed (run the relevant benchmarks when the step touches indexing, reading, search, or painting; paste the numbers into the PR).
- [ ] Documents updated (§3.8).
- [ ] PR is small, focused, and describes what and why.

---

## 4. Hard rules (never break these)

1. **Clean room.** Do not open, read, copy, or paraphrase source code of other log viewers (klogg, glogg, LogExpert, lnav, Notepad++ plugins, or similar), even if it is open source. Feature descriptions, public user documentation, and screenshots are fine. General algorithms from papers, textbooks, or library documentation are fine. This keeps the project cleanly MIT-licensed. See ADR 0003.
2. **No new dependency** without the owner's approval and an ADR. Allowed dependencies are listed in `docs/ARCHITECTURE.md` §7. Only permissive licenses (MIT, BSD, Apache-2.0, BSL-1.0, Zlib, CC0). Qt only under LGPL v3, dynamically linked.
3. **No network access** in the product: no update checks, no telemetry, no crash upload, no downloads. QtNetwork may be linked only for `QLocalServer`/`QLocalSocket` (single-instance IPC).
4. **Never write to opened log files.** The viewer opens files read-only and must not prevent other processes from writing, renaming, or deleting them.
5. **The UI thread never does file I/O or searching** on more than a bounded amount of data (one screen of lines from the cache). Everything else runs on worker threads. See `docs/ARCHITECTURE.md` §4.
6. **`lv_core` has no Qt dependency.** See ADR 0006.
7. **Git safety:** never force-push, never rewrite published history, never delete branches or tags on the remote, never use `--no-verify`. Undo with `git revert`. A guard on the agent machine blocks these operations; never try to bypass it.
8. **No secrets** in the repo, ever.
9. **Never weaken a test, a sanitizer, a warning flag, or a performance budget** to make CI green. If a budget is wrong, propose a change to the owner with measurements.
10. **Generated big test files are never committed.** Use `tools/loggen` (deterministic, seeded). Small fixtures (< 100 KiB each) live in `tests/data/`.

---

## 5. Code rules

### Language and style

- C++20. Compile with warnings as errors. Formatting is defined by `.clang-format`; never hand-format against it.
- Names: types `PascalCase`, functions and variables `camelCase`, member variables `m_camelCase`, constants and enumerators `kPascalCase`, namespaces `lowercase`, macros `LV_UPPER_CASE` (avoid macros). Files `snake_case.h` / `snake_case.cpp`, one main class per file.
- Namespaces: `lv::core`, `lv::model`, `lv::ui`.
- Headers use `#pragma once`.
- Prefer value types, `std::span`, `std::string_view`, `std::unique_ptr`. No raw owning pointers outside Qt parent-child ownership. No `new` without immediate ownership transfer.
- No exceptions across module boundaries. Core APIs that can fail return `lv::core::Result<T>` (`tl::expected<T, Error>`). Exceptions from the standard library (for example `std::bad_alloc`) are allowed to terminate.
- Use strong types for line numbers and byte offsets (`LineNumber`, `ByteOffset`, `LineCount`) in public APIs; never mix them with plain `int`. Line numbers are 64-bit and 0-based internally; the UI shows 1-based numbers.
- No `int` for sizes or indexes into files. Use `std::uint64_t` / `std::size_t` / the strong types.
- Thread-safety is documented on every class that is used from more than one thread (`// Thread-safety: ...`).
- Every public function in `lv_core` and `lv_model` has a short doc comment saying what it does, its preconditions, and its complexity when not obvious.
- Platform-specific code lives only in `src/core/platform/` behind an interface. No `#ifdef _WIN32` anywhere else.
- Performance-sensitive code (`index`, `search`, `text` modules) avoids per-line heap allocations in hot loops. Measure before and after with the benchmarks.

### Qt rules

- Qt 6 Widgets only (no QML). Minimum Qt for building: 6.8. CI and releases use the pinned version in `.github/qt-version`.
- The UI is English only (ADR 0013). Plain string literals are fine; `tr()` is allowed but not required, and there is no translation tooling.
- Cross-thread communication from workers to the UI uses queued signals or `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`. Never touch a widget from a worker thread.
- Custom painting for log views (`QAbstractScrollArea`), never `QListView`/`QTableView` for log lines (they do not scale to hundreds of millions of rows).

### Tests

- Core: Catch2 v3 in `tests/core/`. Model: Catch2 plus Qt event loop helpers in `tests/model/`. UI: Qt Test in `tests/ui/`, run with `QT_QPA_PLATFORM=offscreen`.
- Tests must be deterministic: no sleeps for synchronization (wait on conditions with timeouts), seeded randomness, temp files in a per-test temp directory that is removed afterwards.
- File-system behavior (sharing, rename, truncate, delete while open) is tested with real files, on both Linux and Windows CI.
- Benchmarks: Google Benchmark in `bench/`. They are not run as tests, but CI builds them and runs a short smoke run.

---

## 6. Commands

Full details and setup: `docs/DEVELOPMENT.md`.

```bash
# Configure, build, test (Linux, GCC, debug)
cmake --preset linux-gcc-debug
cmake --build --preset linux-gcc-debug
ctest --preset linux-gcc-debug

# Sanitizers (ASan + UBSan, Clang)
cmake --preset linux-clang-asan && cmake --build --preset linux-clang-asan && ctest --preset linux-clang-asan

# Format check and static analysis
cmake --build --preset linux-gcc-debug --target format-check
cmake --build --preset linux-gcc-debug --target tidy

# Benchmarks (release build)
cmake --preset linux-release && cmake --build --preset linux-release --target lv_bench
./build/linux-release/bench/lv_bench --benchmark_filter=<name>
```

These presets are created in roadmap step M0-01. Until then, there is nothing to build.

---

## 7. Repository layout (target)

```
src/core/        lv_core: Qt-free engine (io, encoding, index, text, search, tail, concurrency)
src/core/platform/  OS abstraction (win32, posix)
src/model/       lv_model: QtCore glue (Document, FilteredDocument, settings, history, marks, highlights)
src/ui/          lv_ui: widgets (LogView, FilterBar, MainWindow, dialogs)
src/app/         main.cpp, resources, Windows manifest and version info
tests/core|model|ui/  tests, tests/data/ small fixtures
bench/           Google Benchmark targets
tools/loggen/    deterministic test log generator
docs/            documentation, docs/adr/ decisions, docs/plans/ temporary German plans
.github/         CI workflows, Dependabot, PR template
```

---

## 8. Documents and their owners

| Document | Changes when |
| --- | --- |
| `docs/VISION.md` | Only with the owner's decision |
| `docs/SPEC.md` | A behavior is added, changed, or clarified |
| `docs/ARCHITECTURE.md` | Module boundaries, threading, data flow, or dependencies change |
| `docs/ROADMAP.md` | A step is done, split, added, or reordered |
| `docs/PERFORMANCE.md` | Budgets are agreed or measured results are recorded |
| `docs/DEVELOPMENT.md` | Build, test, CI, or release commands change |
| `docs/HANDOFF.md` | Every PR |
| `docs/adr/` | Every lasting decision |

---

## 9. Git conventions

- Conventional Commits: `feat(core): add line splitter for UTF-16`, `fix(ui): ...`, `test(search): ...`, `perf(index): ...`, `docs: ...`, `build: ...`, `ci: ...`, `chore: ...`, `refactor(...)`.
- Scopes: `core`, `index`, `search`, `tail`, `encoding`, `model`, `ui`, `app`, `build`, `ci`, `docs`.
- Commit body explains why, not just what. Reference the roadmap step: `Roadmap: M1-05`.
- One logical change per commit; squash-merge is used on `main`.
- Commit author for agents: the machine's configured agent identity. Do not commit as the owner.

---

## 10. Architecture decision records

- Location: `docs/adr/NNNN-kebab-title.md`, index in `docs/adr/README.md`.
- Format: Status, Context, Decision, Consequences, Alternatives considered. Short is fine.
- Superseding: never edit the decision of an accepted ADR. Write a new one and mark the old one `Superseded by NNNN`.

---

## 11. When to stop and ask the owner

- The spec does not answer a product question.
- A new dependency, a license question, or a change to a hard rule.
- A performance budget cannot be met with a reasonable design.
- CI cannot be made green without weakening checks.
- The step turns out to be wrong or obsolete.
- Anything irreversible (releases, tags, repository settings, making the repo public).
