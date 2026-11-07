# Roadmap

How to use this file (details in `AGENTS.md` §3):

- The **next step** is the first unchecked step whose `Depends on` steps are all checked.
- One step = one branch = one pull request. When a step turns out too big, split it (`M1-05a`, `M1-05b`) in a separate small PR first.
- Tick the step (`- [x]`) and add the PR number in the PR that implements it.
- Requirement IDs (`F-…`, `P-…`) refer to `docs/SPEC.md` and `docs/PERFORMANCE.md`.
- Steps marked **[Windows]** need the Windows CI job (add the `windows` label to the PR). Steps marked **[Owner]** need a manual action or test by the owner; the agent prepares everything and asks.

Step format:

```
- [ ] **ID Title** — Depends on: …
  - Goal: what exists afterwards.
  - Scope: files/modules to create or change.
  - Done when: acceptance criteria (each needs a test unless stated).
```

---

## M0 – Foundation

Goal: an empty but complete project: builds on Linux and Windows, tests run, CI enforces quality, dependencies update themselves.

- [x] **M0-01 CMake skeleton and presets** — Depends on: – (PR #2)
  - Goal: top-level CMake project with the three libraries, the app, and a test executable, all building.
  - Scope: `CMakeLists.txt` (project `LogViewer`, version `0.1.0`, C++20, `LV_*` options), `CMakePresets.json` (all presets from `DEVELOPMENT.md` §2 incl. build/test presets), `cmake/LvCompilerOptions.cmake` (flags from `DEVELOPMENT.md` §2 as an interface target `lv_compile_options`), `cmake/LvSanitizers.cmake`, `vcpkg.json` (manifest with `builtin-baseline` = latest vcpkg release commit; deps: `pcre2`, `simdutf`, `tl-expected`, `catch2`, `benchmark` (benchmark only via a `bench` feature)), `vcpkg-configuration.json` if needed. Targets: `lv_core` (static, one placeholder source `version.cpp` exposing `lv::core::versionString()`), `lv_model` (static, QtCore), `lv_ui` (static, QtWidgets), `lv_app` → executable `logviewer`, `lv_core_tests` (Catch2 with one test of `versionString()`), CTest integration with labels.
  - Done when: `linux-gcc-debug`, `linux-clang-debug`, `linux-clang-asan`, `linux-release` configure, build, and pass tests locally; `lv_core` does not link Qt (verified by a CMake check that fails if `lv_core` links any `Qt6::` target); `DEVELOPMENT.md` reflects the real commands.

- [x] **M0-02 Formatting and static analysis** — Depends on: M0-01 (PR #3)
  - Scope: `.clang-format`, `.clang-tidy` (as described in `DEVELOPMENT.md` §5, every disabled check with a reason), `.editorconfig`, CMake targets `format`, `format-check`, `tidy` (tidy on files changed vs. `origin/main`, all files with `LV_TIDY_ALL=1`).
  - Done when: all existing sources pass `format-check` and `tidy`; a deliberately misformatted file makes `format-check` fail (verified once locally, not committed).

- [x] **M0-03 Linux CI** — Depends on: M0-02 (PR #4)
  - Scope: `.github/workflows/ci.yml` with jobs `linux-gcc`, `linux-clang-asan`, `format-and-tidy`, `docs` (see `DEVELOPMENT.md` §6). Qt on CI via `jurplel/install-qt-action` with the version from `.github/qt-version` (file created here, content `6.12.0` or the latest 6.12 patch). vcpkg binary caching through the Actions cache, ccache. Actions pinned by SHA. `concurrency` and `timeout-minutes` on all jobs. Docs-only changes run only the `docs` job.
  - Done when: CI is green on the PR; a second push to the PR shows cache hits (paste timings into the PR); total runtime of `ci.yml` with warm caches ≤ 15 min.
  - [Owner] After merge: add a ruleset for `main` requiring PRs and the status checks `linux-gcc`, `linux-clang-asan`, `format-and-tidy` (with `docs` allowed to satisfy docs-only PRs — the agent documents the exact setup in the PR).

- [x] **M0-04 Windows CI and portable package** — Depends on: M0-03 **[Windows]** (PR #5; owner start test done in M2-09)
  - Scope: `.github/workflows/ci-windows.yml` (triggers from `DEVELOPMENT.md` §6), MSVC release build via presets, tests, `cmake --install` + `windeployqt` into `dist/`, upload zip artifact (retention 7 days). `src/app/resources/logviewer.rc` with `VERSIONINFO` generated from the CMake project version, `src/app/resources/logviewer.manifest` (longPathAware, PerMonitorV2, `activeCodePage` UTF-8, supportedOS Windows 10/11), placeholder icon.
  - Done when: Windows job green; the artifact zip contains `logviewer.exe` with version info visible in Explorer properties.
  - [Owner] Download the artifact, start `logviewer.exe` on Windows 11, confirm an (empty) window opens.

- [x] **M0-05 Application skeleton** — Depends on: M0-01 (PR #6)
  - Scope: `lv_app/main.cpp` (`QApplication`, organization/application name `LogViewer`, `--help`/`--version` via `QCommandLineParser` (F-CLI-1)), `lv_ui::MainWindow` with empty `QTabWidget`, menu bar (File: Open (disabled), Exit; Help: About), status bar, About dialog (version, MIT license, Qt version, "built with AI agents" note). App log sink writing `logviewer.log` in the settings directory (ARCHITECTURE §3.8).
  - Done when: Qt Test (offscreen) creates `MainWindow`, checks menus exist; `logviewer --version` prints the version (CTest).

- [x] **M0-06 Test log generator `loggen`** — Depends on: M0-01 (PR #7)
  - Scope: `tools/loggen/` (C++ executable, no Qt): profiles `small`, `large`, `huge`, `short`, `long`, `utf16`, `cp1252` exactly as in `PERFORMANCE.md` §2; options `--profile`, `--seed`, `--size`, `--out`, `--append-rate <MB/s> --duration <s>` (simulated live writer, flushes every write), `--rotate-every <MB>` (rename + recreate) and `--truncate-every <MB>`.
  - Done when: same seed produces byte-identical output (test compares SHA-256 of a 5 MB run twice); line counts and level distribution of a 5 MB `small` run are within 1% of the profile; writer modes have a smoke test.

- [x] **M0-07 Benchmark harness** — Depends on: M0-06 (PR #8)
  - Scope: `bench/` with Google Benchmark, `lv_bench` executable, data cache dir (`LV_BENCH_DATA`), a baseline benchmark `BM_ReadOnly_Sequential` (plain sequential read of `small` with 4 MiB buffers → reference for P-IDX-2), JSON output, `tools/bench_compare.py` comparing a result JSON with `bench/baseline-ci-linux.json` (fails on > 25% slowdown).
  - Done when: `lv_bench` runs locally; `bench_compare.py` has a unit test with two fake JSON files.

- [x] **M0-08 Dependabot, nightly workflow, notices** — Depends on: M0-03 (PR #9)
  - Scope: `.github/dependabot.yml` (`github-actions` weekly, `vcpkg` weekly, grouped minor/patch, PR labels `dependencies`), `.github/workflows/nightly.yml` (weekly + manual: TSan job, `slow` tests, Windows build, benchmark smoke + compare, Qt patch-version check that opens an issue labeled `qt-update` using `gh` when a newer 6.12.x exists), `THIRD-PARTY-NOTICES.md`, `CHANGELOG.md` (Keep a Changelog format), `SECURITY.md` (how to report, "no network features" statement).
  - Done when: workflow files pass `actionlint` (run locally or in CI); nightly runs green once via manual dispatch.
  - [Owner] Enable Dependabot alerts and security updates in repo settings; enable "Automatically delete head branches"; enable "Allow auto-merge" if desired.

- ~~**M0-09 Translation infrastructure**~~ — Dropped: the UI is English only (ADR 0013).

**M0 exit:** CI green on Linux and Windows, an empty window starts on the owner's Windows 11 machine, Dependabot PRs appear.

---

## M1 – Core engine: files, encodings, line index

Goal: `lv_core` can open any file, detect its encoding, index it at disk speed with tiny memory, and read any line. No UI yet. Everything is benchmarked.

- [x] **M1-01 Core utilities** — Depends on: M0-02 (PR #11)
  - Scope: `src/core/util/`: strong types `LineNumber`, `LineCount`, `ByteOffset`, `ByteRange` (arithmetic only where meaningful, explicit construction, formatting), `Result<T>`/`Error` (ARCHITECTURE §3.8), `LV_ASSERT`, scope guard. (The logging interface `log.h` already exists since M0-05.)
  - Done when: tests cover construction, comparisons, overflow-sensitive arithmetic (asserts), `Error` formatting.

- [x] **M1-02 Platform file access** — Depends on: M1-01 (PR #12)
  - Scope: `src/core/platform/file.h` interface + `file_win32.cpp` / `file_posix.cpp` (ARCHITECTURE §3.1): `open`, `readAt`, `size`, `identity`, `identityOfPath`.
  - Done when (tests label `fs`, run on Linux; Windows in CI with label): read at arbitrary offsets incl. past EOF (short read); another handle can write, append, rename, and delete the file while it is open (F-FILE-2) — on Windows the test opens the writer with `FILE_SHARE_READ` like typical loggers; size reflects appended data via the handle; identity equal for same file, different after rename-and-recreate; open errors return proper `Error` codes for missing file, directory, no permission (POSIX). **[Windows]**

- [x] **M1-03 Encoding traits and detection** — Depends on: M1-01 (PR #13)
  - Scope: `src/core/encoding/encoding.h`, `detect.cpp` (F-ENC-1, F-ENC-2) using simdutf for UTF-8 validation; single-byte tables for Windows-1252 and ISO-8859-1.
  - Done when: tests for every BOM, pure ASCII, valid UTF-8, invalid UTF-8 → Windows-1252, UTF-8 sequence cut at the end of the 1 MiB sample still detected as UTF-8, empty input → UTF-8.

- [x] **M1-04 Compressed sorted sequence** — Depends on: M1-01 (PR #14)
  - Scope: `src/core/index/sorted_u64_sequence.h/.cpp` (ARCHITECTURE §3.3): blocks of 128, bit-packed deltas, raw fallback block, segmented storage, single-writer append with atomic published count, `at`, `lowerBound`, iteration, `memoryBytes()`.
  - Done when: property tests against `std::vector<uint64_t>` with random sequences (small deltas, huge deltas, mixed, 1 element, block boundaries 127/128/129); concurrent test: one writer appends 10M values while 4 readers read random published positions and verify values (runs under TSan in nightly); `BM_LineIndex_Append` / `BM_LineIndex_RandomAccess` benchmarks; memory ≤ 1.2 bytes/value for deltas < 256.

- [x] **M1-05 Line splitter** — Depends on: M1-03 (PR #15)
  - Scope: `src/core/index/line_splitter.h/.cpp`: given a buffer, the encoding, and carry-over state, emits line end offsets. Handles `\n`, `\r\n` split across buffer boundaries, UTF-16 LE/BE (2-byte aligned `\n` code unit only), BOM skipping at file start.
  - Done when: table tests for all F-FILE-5 cases in UTF-8 and UTF-16 LE/BE; property test: splitting any input in random buffer sizes (1 byte … 1 MiB) gives the same result as a naive reference splitter.

- [x] **M1-06 LineIndex with pending tail** — Depends on: M1-04, M1-05 (PR #16)
  - Scope: `src/core/index/line_index.h/.cpp`: line ranges excluding terminators, `lineContaining`, `maxLineLength`, pending unterminated tail (ARCHITECTURE §3.3, F-TAIL-8), generation number.
  - Done when: tests: empty file → 0 lines; `"a"` → 1 line (pending); `"a\n"` → 1 line; `"a\r\n\r\n"` → 2 lines, second empty; tail completion appends exactly once; `lineContaining` at every byte of a small file.

- [x] **M1-07 Indexer** — Depends on: M1-02, M1-06 (PR #17)
  - Scope: `src/core/index/indexer.h/.cpp` (ARCHITECTURE §3.4): read-ahead thread with buffer ring, progress callback, `std::stop_token`, incremental mode from an offset.
  - Done when: result equals a naive reference index on all `tests/data/` fixtures and on 20 MB generated files of each `loggen` profile; cancellation stops within one block and leaves a consistent prefix; incremental indexing of appended data equals a full index; read errors return `Error` and keep the published prefix; benchmarks `BM_Index_small`, `BM_Index_short` recorded in the PR.

- [x] **M1-08 Line decoding and column mapping** — Depends on: M1-03 (PR #18)
  - Scope: `src/core/encoding/decode.cpp`: raw → UTF-16 (`std::u16string`) for all encodings, invalid UTF-8 → U+FFFD (F-ENC-4), control characters and NUL mapped to visible placeholders for display (F-ROB-1), tab expansion helper, mapping between byte offset ↔ UTF-16 offset ↔ display column.
  - Done when: tests for umlauts in UTF-8/CP1252/UTF-16, surrogate pairs, invalid sequences at start/middle/end, tabs at various columns, round-trip offset mapping property tests; `BM_Decode_*` benchmarks.

- [x] **M1-09 Line reader with page cache** — Depends on: M1-07 (PR #19)
  - Scope: `src/core/reader/line_reader.h/.cpp` (ARCHITECTURE §3.5), thread-safe, 64 KiB page LRU (configurable size), max line length handling (F-FILE-7).
  - Done when: random line reads equal reference on generated files; reading lines at a block of 1,000 consecutive lines does one or few reads (counted through a test hook); 16 MiB line read correctly; lines in a truncated file return an error, not garbage.

- [x] **M1-10 Indexing performance pass** — Depends on: M1-07, M0-07 (PR #20)
  - Scope: measure P-IDX-1/2/3 on `ci-linux` and locally; tune buffer size, buffer count, and the newline scan (SIMD only if the scanner is the bottleneck per profile); record results in `PERFORMANCE.md` §5; create `bench/baseline-ci-linux.json`.
  - Done when: index throughput ≥ 90% of `BM_ReadOnly_Sequential` on `large` and `short` (warm cache) on the agent machine; memory per line within P-IDX-3; results logged.

**M1 exit:** `lv_core` indexes and reads any test file correctly and at near read speed; all index budgets met on Linux.

---

## M2 – Viewing: open files, scroll, select, copy, tabs

Goal: a usable read-only viewer with tabs (no filtering yet). First build the owner can try on Windows.

- [x] **M2-01 Document model** — Depends on: M1-09, M0-05 (PR #21)
  - Scope: `src/model/document.h/.cpp` (ARCHITECTURE §5): owns file, index, reader, indexer thread; states; signals with generation; encoding override and reload (F-FILE-8, F-ENC-3); clean shutdown.
  - Done when: model tests with Qt event loop: open → progress signals → `Ready` with correct line count; reload resets generation; close during indexing joins threads within 1 s; open errors produce `Error` state with message (F-FILE-12).

- [x] **M2-02 LineSource and LineCache** — Depends on: M2-01 (PR #22)
  - Scope: `src/model/line_source.h`, `document_line_source.cpp`, `line_cache.cpp` (ARCHITECTURE §5).
  - Done when: tests: cache hit/miss/eviction, entries invalidated on generation change, decoded text equals `decodeLine` output.

- [x] **M2-03 LogView painting** — Depends on: M2-02 (PR #23)
  - Scope: `src/ui/log_view.h/.cpp`: `QAbstractScrollArea`, paints visible rows from a `LineSource`, line number gutter (F-VIEW-2), font from settings with defaults, tab expansion (F-VIEW-4), current line highlight (F-VIEW-10), horizontal scrolling (F-VIEW-5), long line truncation marker (F-FILE-7). 64-bit scroll mapping (ARCHITECTURE §6).
  - Done when: Qt Tests (offscreen) render to a `QImage` and verify: first visible line number, scroll to line N shows N at top, scroll range with a fake `LineSource` of 2^40 lines works (top line exact after `scrollToLine`), truncation marker painted for long lines; paint of one screen with a fake source ≤ 16 ms in release (P-UI-1, relaxed factor 2 on CI).

- [x] **M2-04 Keyboard and mouse navigation** — Depends on: M2-03 (PR #24)
  - Scope: F-VIEW-6 keys, mouse wheel (smooth, respects system settings), click sets current line, Go to line dialog (`Ctrl+G`).
  - Done when: Qt Tests send key/mouse events and check current/top line for each binding, including at file start/end and with 0 lines.

- [x] **M2-05a Selection and copy** — Depends on: M2-04, M1-08 (PR #26)
  - Scope: F-VIEW-7/9 and the synchronous part of F-VIEW-8: selection model in (row, UTF-16 column) coordinates, selection painting, drag selection, double-click word, triple-click line, `Shift`+click and `Shift`+navigation (whole lines), `Ctrl+A`, `Ctrl+C` copy of the original text (no tab expansion, no control pictures) with `\n` separators (Qt converts to `\r\n` on the Windows clipboard). `LineSource` gets a plain-text accessor for copying.
  - Done when: tests: drag selection across lines, double/triple click, shift+arrows, copy of partial lines with umlauts and tabs gives the exact original text, `Ctrl+A` + copy of a 5 MB file equals the file content minus CRs.

- [x] **M2-05b Large copies** — Depends on: M2-05a (PR #27)
  - Scope: rest of F-VIEW-8: estimate the selection size from the line index; above 100 MiB ask for confirmation; copies above a few MiB run on a worker thread (thread-safe reads through `DocumentContent`, not the UI-thread `LineSource`) with a cancellable progress dialog; the UI stays responsive.
  - Done when: tests: size estimate within 1% of the real size, confirmation shown above the threshold (threshold injectable for tests), cancel leaves the clipboard unchanged, a 200 MB copy keeps the event loop responsive (event processed during the copy).

- [x] **M2-06 Document tab and main window integration** — Depends on: M2-03 (PR #28)
  - Scope: `src/ui/document_tab.cpp` (only the main view for now), `MainWindow` open file (`Ctrl+O`), drag and drop, command line files (F-FILE-1), tabs (F-TAB-1, F-TAB-2 incl. reorder and keyboard switching), tab context menu (F-TAB-4), status bar (F-TAB-6), progress indicator in tab (F-TAB-3 indexing part).
  - Done when: Qt Tests open two fixture files via the API used by drag and drop and command line, verify tab titles/tooltips, opening the same path twice activates the existing tab, closing tabs releases documents.

- [x] **M2-07 Recent files and encoding menu** — Depends on: M2-06 (PR #29)
  - Scope: `src/model/recent_files.cpp` (F-FILE-9), File → Recent menu, View → Encoding menu (F-ENC-3), Reload (`F5`).
  - Done when: tests for recent list ordering, deduplication, limit 20, missing files disabled; encoding switch re-indexes and changes displayed text of a CP1252 fixture.

- [x] **M2-08 Font, zoom, and minimal settings** — Depends on: M2-06 (PR #30)
  - Scope: `src/model/settings.cpp` (typed `QSettings` wrapper with schema version, portable mode detection F-SET-2), font family/size, zoom shortcuts (F-VIEW-3), tab width.
  - Done when: settings round-trip tests in a temp dir; portable mode uses the exe directory when `portable.ini` exists (test with a fake app dir).

- [x] **M2-09 First Windows preview build** — Depends on: M2-05b, M2-07, M2-08, M0-04 **[Windows] [Owner]** (PR #31)
  - Scope: fix whatever the Windows build reveals; README "Try it" section for the artifact.
  - Done when: Windows CI artifact works; owner opens a real 50 MB log on Windows 11 and reports feedback; feedback is added to `HANDOFF.md` and, where needed, as new roadmap steps.

**M2 exit:** the owner can open real logs in tabs, scroll, select, and copy on Windows 11.

---

## M3 – Filtering and search

Goal: the filtered view with literal, regex, and boolean filters, running on all cores, plus filter history and quick find.

- [x] **M3-01 Matcher interface and LiteralMatcher** — Depends on: M1-01 (PR #32)
  - Scope: `src/core/search/matcher.h`, `literal_matcher.cpp` (ARCHITECTURE §3.6), case-insensitive ASCII path, whole word option.
  - Done when: table tests incl. empty pattern (matches everything), patterns longer than line, case folding of ASCII, whole-word boundaries (underscore and digits count as word chars), `collectRanges` returns all non-overlapping matches.

- [x] **M3-02 RegexMatcher (PCRE2)** — Depends on: M3-01 (PR #33)
  - Scope: `regex_matcher.cpp`: compile with UTF and JIT, per-thread scratch, case-insensitive option, match limits (F-ROB-3) with "skipped" reporting, compile errors with offset.
  - Done when: tests: common log patterns, Unicode case folding (`/über/i` matches `ÜBER`), invalid regex error and position, catastrophic pattern `(a+)+$` on a long line returns "skipped" within 100 ms, JIT used when available (assert via `pcre2_pattern_info`).

- [x] **M3-03 Boolean expression parser and evaluator** — Depends on: M3-02 (PR #34)
  - Scope: `bool_expr.cpp`: tokenizer, recursive descent parser for SPEC §5.1, AST, evaluator with short-circuit, `collectRanges` for positive terms only, error messages with character positions.
  - Done when: parser tests for precedence, parentheses, escapes, keyword case-insensitivity, all error cases (unterminated literal/regex, missing operand, unbalanced parentheses, unknown flag); evaluator truth-table tests; property test comparing evaluator with a naive reference over random expressions and lines.

- [x] **M3-04 Query compilation** — Depends on: M3-03 (PR #35)
  - Scope: `query.h/.cpp`: `Query` (text, mode Literal/Regex/Boolean, case sensitive, whole word, invert) → `CompiledQuery`; equality and hashing (for history and caching).
  - Done when: tests for every mode/option combination and invert semantics.

- [x] **M3-05 Thread pool** — Depends on: M1-01 (PR #36)
  - Scope: `src/core/concurrency/thread_pool.cpp` (ARCHITECTURE §3.6), configurable size (0 = auto).
  - Done when: tests: tasks run, stop tokens cancel queued and running tasks cooperatively, destruction joins, no deadlock with more tasks than threads; runs clean under TSan (nightly).

- [x] **M3-06 Search engine** — Depends on: M3-04, M3-05, M1-09 (PR #37)
  - Scope: `search_engine.cpp`: chunking by lines, per-worker reads, UTF-8 conversion for non-UTF-8 documents, reorder buffer, ordered `ResultSink` into a `SortedU64Sequence`, progress, cancellation, line range limit (F-FILT-11 core part), skipped-line counting.
  - Done when: results equal a single-threaded reference on all `loggen` profiles (20 MB) for literal, regex, and boolean queries with 1, 2, and 8 threads; partial results are always an ordered prefix (checked during the run); cancellation leaves a consistent prefix; benchmarks `BM_Search_*` recorded.

- [x] **M3-07 FilteredDocument** — Depends on: M3-06, M2-01 (PR #38)
  - Scope: `src/model/filtered_document.cpp`: apply/clear filter, cancel previous search on new filter, generation handling, match list, `FilteredLineSource` (original line numbers), progress and match count signals.
  - Done when: model tests: apply → results; apply new filter while running → old results never appear; document reload clears and re-runs filter.

- [x] **M3-08 Filtered view and synchronization** — Depends on: M3-07, M2-06 (PR #39)
  - Scope: `DocumentTab` with splitter (position persisted), filtered `LogView` with original line numbers, sync in both directions (F-FILT-1, F-FILT-7).
  - Done when: Qt Tests: click in filtered view scrolls main view to the line and makes it current; moving in main view selects nearest match at or before it in the filtered view.

- [x] **M3-09 Filter bar** — Depends on: M3-08 (PR #40)
  - Scope: `src/ui/filter_bar.cpp`: mode selector, options, apply on Enter, `Esc` restores, Clear, progress, match count, skipped count, inline error with position (F-FILT-2, F-FILT-5, F-FILT-6, F-FILT-10).
  - Done when: Qt Tests for apply/escape/clear, error display for invalid regex and invalid boolean expression, mode switching keeps text.

- [x] **M3-10 Match highlighting** — Depends on: M3-09 (PR #41)
  - Scope: highlight match ranges (positive terms) in both views (F-FILT-8), computed per visible line via `collectRanges`, cached with the line cache.
  - Done when: rendering tests check highlighted cell positions for ASCII, umlauts, tabs, and multiple matches per line.

- [x] **M3-11 Search performance pass** — Depends on: M3-06, M0-07 (PR #42)
  - Scope: chunk-wide matching optimization (ARCHITECTURE §3.6), literal prefilter for regexes where safe, tuning of chunk size; record P-SRCH results.
  - Done when: P-SRCH-1 … P-SRCH-5 met on the agent machine (or documented with profiles and a plan); results logged in `PERFORMANCE.md`.

- [x] **M3-12 Filter history** — Depends on: M3-09, M2-08 (PR #43)
  - Scope: `src/model/filter_history.cpp` (F-HIST-1 … F-HIST-4), editable combo box with dropdown, `Up`/`Down` cycling, completion, delete entry, clear in settings (settings dialog comes later; provide a menu action for now).
  - Done when: model tests for ordering, dedup by (text, mode, options), limit, persistence; Qt Tests for dropdown selection applying the filter and `Up`/`Down` cycling.

- [ ] **M3-13 Quick find** — Depends on: M3-06, M3-10
  - Scope: `src/ui/find_bar.cpp` (F-FIND-1 … F-FIND-4): search forward/backward from current line using the search engine with a line range, wrap-around notice, highlight visible matches.
  - Done when: Qt Tests: next/previous match in main and filtered view, wrap-around, invalid regex error.

**M3 exit:** the owner can filter real logs with regex and AND/OR/NOT and uses the history. Owner test on Windows **[Owner]**.

---

## M4 – Live logs

Goal: follow growing files reliably, including rotation and truncation, with incremental filtering.

- [ ] **M4-01 Change detector** — Depends on: M1-02
  - Scope: `src/core/tail/change_detector.cpp` (ARCHITECTURE §3.7).
  - Done when: unit tests with fake observations for every classification; `fs` integration tests with real files for grow, truncate, replace (rename + recreate), delete, recreate. **[Windows]**

- [ ] **M4-02 File watcher** — Depends on: M4-01, M2-01
  - Scope: `src/model/file_watcher.cpp`: `QFileSystemWatcher` on file and directory, polling timer fallback (F-TAIL-2), coalescing.
  - Done when: model tests with a writer thread: change events arrive with notifications and with notifications disabled (polling only); burst of 10,000 appends yields bounded number of checks.

- [ ] **M4-03 Incremental indexing in Document** — Depends on: M4-02, M1-07
  - Scope: `Document` reacts to `Grown` with incremental indexing, emits `linesAppended`; pending tail handling (F-TAIL-8).
  - Done when: tests: appending lines (with and without trailing newline, partial line completed later) yields exactly the same index as a fresh open; P-TAIL-1 latency measured in a test with polling interval 100 ms.

- [ ] **M4-04 Incremental filtering** — Depends on: M4-03, M3-07
  - Scope: `FilteredDocument` searches only appended lines and appends matches; a running full search and incremental appends do not conflict (appends queue behind the full search range).
  - Done when: tests: results after many appends equal a full search of the final file; appends during a running full search are not lost or duplicated.

- [ ] **M4-05 Follow mode** — Depends on: M4-04, M3-08
  - Scope: F-TAIL-3 for both views, toolbar toggle, `F10`, status bar indicator, tab change indicator (F-TAB-3).
  - Done when: Qt Tests: follow keeps last line visible while appending; scrolling up disables; `Ctrl+End` re-enables.

- [ ] **M4-06 Truncation, rotation, deletion** — Depends on: M4-03
  - Scope: F-TAIL-5, F-TAIL-6, F-TAIL-7 in `Document`, notices in the tab, generation reset, filter re-run.
  - Done when: `fs` tests with `loggen --rotate-every` and `--truncate-every`: views end up with exactly the content of the current file; no crash when the file is missing; marks cleared with notice. **[Windows]**

- [ ] **M4-07 Live stress test** — Depends on: M4-05, M4-06
  - Scope: `slow` test: `loggen --append-rate 50 --duration 30` with active filter and follow mode; measure UI stalls and memory (P-TAIL-2, P-UI-2).
  - Done when: test passes on Linux CI (nightly) and is documented for manual runs on Windows; results logged in `PERFORMANCE.md`.

**M4 exit:** the owner follows live application logs on Windows, including rotation **[Owner]**.

---

## M5 – Marks, bookmarks, highlight rules

- [ ] **M5-01 Marks** — Depends on: M3-08
  - Scope: `src/model/mark_set.cpp`, gutter mark column, `M` key, context menu, next/previous (F-MARK-1 … F-MARK-5).
  - Done when: model and Qt Tests for toggle on single and multiple lines, navigation, clear with confirmation, copy marked lines.

- [ ] **M5-02 Marks in filtered view** — Depends on: M5-01, M3-07
  - Scope: merge of matches and marks in `FilteredLineSource` (F-FILT-9) with visual distinction; option toggle.
  - Done when: tests: marked non-matching lines appear in order; unmarking removes them; toggling the option.

- [ ] **M5-03 Mark colors** — Depends on: M5-01
  - Scope: F-MARK-6.
  - Done when: tests for color assignment and painting.

- [ ] **M5-04 Bookmarks and panel** — Depends on: M5-01
  - Scope: `src/model/bookmarks.cpp`, `src/ui/bookmarks_panel.cpp` (F-BOOK-1 … F-BOOK-4).
  - Done when: Qt Tests: add with default name, rename, delete, jump on double-click, `F2`/`Shift+F2`.

- [ ] **M5-05 Highlight rules model** — Depends on: M3-04
  - Scope: `src/model/highlight_sets.cpp` (F-HL-1 … F-HL-3, F-HL-6): rules, sets, ordering, compiled matcher cache, JSON import/export, persistence.
  - Done when: tests for precedence (first whole-line rule wins, match rules layered), JSON round trip, invalid JSON error handling.

- [ ] **M5-06 Highlight painting** — Depends on: M5-05, M3-10
  - Scope: apply rules in `LogView` for visible lines with caching (F-HL-5); per-tab active set.
  - Done when: rendering tests for whole-line and match-only rules; P-UI-1 with 20 rules on `long` profile (relaxed on CI).

- [ ] **M5-07 Highlight dialog** — Depends on: M5-06
  - Scope: `src/ui/highlight_dialog.cpp` (F-HL-4) with live preview; "Highlight this text" action (F-HL-7).
  - Done when: Qt Tests for create/edit/reorder/delete rules and sets, preview updates.

- [ ] **M5-08 Persist marks and bookmarks per file** — Depends on: M5-04
  - Scope: F-PERS-1.
  - Done when: tests: reopen restores; changed fingerprint discards with notice; LRU limit of 500 files.

**M5 exit:** all features the owner uses daily in klogg exist.

---

## M6 – Settings, session, polish

- [ ] **M6-01 Settings dialog** — Depends on: M2-08, M5-06
  - Scope: F-SET-1, settings migration and corrupt file handling (F-SET-3).
  - Done when: Qt Tests change each setting and verify effect and persistence; corrupt INI file is backed up and replaced.

- [ ] **M6-02 Dark mode and color defaults** — Depends on: M6-01
  - Scope: F-SET-4.
  - Done when: rendering tests in both schemes; owner visual check on Windows **[Owner]**.

- [ ] **M6-03 Session restore and window state** — Depends on: M6-01, M4-05
  - Scope: F-FILE-10, F-TAB-5.
  - Done when: tests: save session, restart main window object, same tabs/filters/positions restored; missing files skipped with notice.

- [ ] **M6-04 Single instance and CLI options** — Depends on: M2-06
  - Scope: F-FILE-11, F-CLI-2 via `QLocalServer` (per-user name).
  - Done when: integration test starting two app processes; second forwards files and exits.

- [ ] **M6-05 Overview bar** — Depends on: M5-04
  - Scope: F-VIEW-11.
  - Done when: rendering and click tests.

- [ ] **M6-06 Configurable shortcuts** — Depends on: M6-01
  - Scope: F-SET-5.
  - Done when: tests for rebinding and conflict detection.

- ~~**M6-07 German translation complete**~~ — Dropped: the UI is English only (ADR 0013).

- [ ] **M6-08 Additional code pages** — Depends on: M1-08
  - Scope: F-ENC-5.
  - Done when: decode tests per code page.

---

## M7 – Hardening and 1.0 release

- [ ] **M7-01 Robustness test suite** — Depends on: M4-06
  - Scope: F-ROB-1, F-ROB-2: fixtures and tests for binary files, NUL bytes, `\r`-only files, 16 MiB line, I/O error injection via a fake `File`.
  - Done when: all pass under ASan/UBSan.

- [ ] **M7-02 Crash dumps (Windows)** — Depends on: M0-04
  - Scope: F-ROB-5 with `SetUnhandledExceptionFilter` + `MiniDumpWriteDump` (dbghelp, system library), limit 5 files.
  - Done when: hidden debug command line flag `--crash-test` produces a dump in CI. **[Windows]**

- [ ] **M7-03 Release workflow** — Depends on: M0-08, M0-04
  - Scope: `release.yml` as in `DEVELOPMENT.md` §8, `README.txt` for the zip, `THIRD-PARTY-NOTICES.md` complete.
  - Done when: a test tag on a fork or `workflow_dispatch` dry run produces the draft-release artifacts (owner creates real tags only).

- [ ] **M7-04 Performance acceptance on owner notebook** — Depends on: M5-06, M4-07 **[Owner]**
  - Scope: script `tools/perf_acceptance.ps1` that generates data, runs benchmarks, and prints a results table; owner runs it, also measures klogg for comparison.
  - Done when: all budgets met or the owner accepts documented deviations; results logged.

- [ ] **M7-05 Documentation for users** — Depends on: M6-01
  - Scope: `docs/USER_GUIDE.md` (features, shortcuts, boolean syntax, settings, portable mode), README screenshots.
  - Done when: owner review **[Owner]**.

- [ ] **M7-06 Version 1.0.0** — Depends on: all Must requirements implemented
  - Scope: audit `SPEC.md`: every Must has an implementing step and a test (table in the PR), `CHANGELOG.md`, version bump.
  - Done when: owner tags `v1.0.0` **[Owner]**.

---

## Backlog (after 1.0, not planned yet)

- Line wrapping (F-VIEW-12), multiple filter tabs (F-FILT-12), more encodings (F-ENC-6).
- Compressed files, merged timelines, column/timestamp parsing.
- Code signing via SignPath Foundation once the repository is public (ADR 0010).
- Rename the project (working title "Log Viewer").
