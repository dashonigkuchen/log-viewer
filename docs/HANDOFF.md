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
- M0-07 done: `bench/` (`lv_bench`, Google Benchmark) with `BM_ReadOnly_Sequential/<profile>` as the P-IDX-2 reference; data is generated on demand by `loggen` into `<build>/bench-data` (override `LV_BENCH_DATA`), profiles via `LV_BENCH_PROFILES` (default `small`). `tools/bench_compare.py` + unit tests (CTest `tools.bench_compare`). CTest `bench.smoke` in release presets. Measured on the agent machine (warm cache): 7.3 GiB/s on `small`, 5.7 GiB/s on `short`.
- `bench/baseline-ci-linux.json` does not exist yet; it is created in M1-10 together with the first real benchmarks, and the CI comparison job comes with M0-08's nightly workflow.
- `tools/lint.py` skips `.cpp` files that are not in the build's `compile_commands.json` (for example `bench/` in debug builds); CI's `format-and-tidy` configures with benchmarks enabled so they are checked.
- M0-08 done: `.github/dependabot.yml` (GitHub Actions incl. the composite action, vcpkg baseline; weekly, label `dependencies`), `.github/workflows/nightly.yml` (Mondays 03:00 UTC + manual: TSan on core/model/tools, release build with all tests and benchmarks + baseline compare once it exists, Windows build via the reusable `ci-windows.yml`, Qt patch and aqtinstall release check that opens `qt-update`/`maintenance` issues). `THIRD-PARTY-NOTICES.md`, `CHANGELOG.md`, `SECURITY.md`. Labels `dependencies`, `qt-update`, `maintenance` exist.
- Agent procedure for Dependabot PRs and nightly issues: DEVELOPMENT §7.
- M0-09 dropped: the UI is English only (ADR 0013). M0 is complete.
- M1-01 done: `src/core/util/` has `strong_types.h` (`LineNumber`, `LineCount`, `ByteOffset`, `ByteRange`, overflow checked with `LV_ASSERT`), `result.h` (`Result<T>` = `tl::expected<T, Error>`, `ErrorCode`, `makeError`), `assert.h` (`LV_ASSERT`, active unless `NDEBUG`; `setAssertHandler` lets tests throw instead of abort), `scope_guard.h`. `lv_core` links `tl::expected` publicly.
- Pitfall: GCC 14 with LTO reports a false `-Wstringop-overflow` for `"literal" + std::to_string(x)`; build strings with `std::to_chars` or `+=` instead.
- M1-02 done: `lv::core::File` (`src/core/platform/file.h`, `file_posix.cpp`, `file_win32.cpp`, `file_common.cpp`): read-only open with full sharing, positional `readAt` (short read at EOF), `size()` from the handle, `identity()` / `identityOfPath()` (volume + file ID / dev + inode). Test helpers in `tests/support/` (`TempDir`, `LogWriter` that opens like a logger: Windows `FILE_SHARE_READ` only). File-system tests are in `lv_fs_tests` (CTest label `fs`).
- Windows target is `_WIN32_WINNT=0x0A00` (Windows 10+), set in `LvCompilerOptions.cmake`.
- M1-03 done: `src/core/encoding/encoding.h` – `Encoding` (`kUtf8`, `kUtf16Le`, `kUtf16Be`, `kWindows1252`, `kIso88591`), `traits()` (code unit size, LF/CR bytes, BOM), `displayName()`/`encodingFromName()`, `detectEncoding(sample, atEndOfFile)` (BOM → simdutf UTF-8 validation, tolerant of a sequence cut at the sample end → Windows-1252). `lv_core` links simdutf privately.
- M1-04 done: `lv::core::SortedU64Sequence` (`src/core/index/`): blocks of 128 values, base + bit-packed deltas (width 0–64, so huge deltas need no special raw block), headers in never-moving segments, single writer (`append` + `publish`) and lock-free readers for sealed blocks (the open last block is copied under a short mutex). API: `at`, `copy` (fast bulk read), `lowerBound`, `memoryBytes`. Measured (release, 1M values with 110–150 byte deltas): 1.19 bytes/value, append 384 M values/s, random `at` 89 ns, `lowerBound` 354 ns. Concurrency test (1 writer, 4 readers, 2M values) passes under TSan.
- Note: ARCHITECTURE §3.3 mentioned a raw fallback block for deltas > 32 bits; the implementation instead allows bit widths up to 64, which covers it without a second format.
- `.clang-tidy`: `cert-msc32-c`/`cert-msc51-cpp` disabled (deterministic seeds are intended).
- M1-05 done: `lv::core::LineSplitter` (`src/core/index/line_splitter.h`): streaming search for line feeds with `memchr`, reports line *end offsets* (just after `\n`). CRLF needs no special case (readers drop a `\r` before `\n`); lone `\r` is not a break; the unterminated last line is not reported. UTF-16 LE/BE: only aligned `\n` code units count; an odd byte at a buffer end is carried over. BOM skipping = construct with `startOffset` = BOM length.
- M1-06 done: `lv::core::LineIndex` (`src/core/index/line_index.h`): line end offsets in a `SortedU64Sequence`, published `Snapshot` (complete lines, indexed end, last line end, generation); the unterminated tail is derived (`indexedEnd > lastLineEnd`) and becomes a normal line once its `\n` arrives, so it is never counted twice. `rawLineRange` excludes the LF code unit but keeps a CR (LineReader strips it in M1-09). `lineContaining`, `maxLineLength`, `reset` (new generation).
- M1-07 done: `lv::core::Indexer::run(file, index, stop, progress, options)` (`src/core/index/indexer.h`): read-ahead `std::jthread` + ring of reusable 4 MiB buffers, scanning with `LineSplitter`, publish after every block, progress at most every 50 ms, cancellation per block (`kCanceled`, prefix stays valid), incremental from `index.appendedEnd()`, truncation detected as `kOutOfRange`. Benchmark `BM_Index/<profile>`.
- Measured (release, warm cache, agent machine): `small` 50 MiB in 15 ms (3.2 GiB/s, read-only reference 5.1 GiB/s), 1.20 bytes/line; `short` 1 GiB in 611 ms (1.6 GiB/s vs. 7.1 GiB/s read-only), 0.81 bytes/line. P-IDX-1 met; P-IDX-2 (≥ 90% of read speed) is the job of M1-10 (scanner is the bottleneck for short lines).
- Next step: **M1-08 Line decoding and column mapping** (M1-09 is also unblocked).

## Environment notes

- Agent machine: Debian 13 LXC, 4 cores, 8 GB RAM, toolchain installed (GCC 14.2, Clang 19.1, CMake 3.31, Ninja 1.12, Qt 6.8.2 from Debian). vcpkg is cloned at `~/vcpkg` (`VCPKG_ROOT` set in `~/.bashrc`). Clean builds of all presets take under a minute each after the first vcpkg install.
- The vcpkg `builtin-baseline` is the `2026.10.16` release commit. Dependabot will move it forward once M0-08 is in.
- Use `gh run list`/`gh run view` to read CI status (see DEVELOPMENT §6).
- Repository is private; mind the CI budget (`docs/DEVELOPMENT.md` §6).

## Open questions for the owner

- None.

## Known issues

- aqtinstall is pinned to an unreleased fix commit (ADR 0012). Remove the pin when an aqtinstall release supports Qt 6.12.
