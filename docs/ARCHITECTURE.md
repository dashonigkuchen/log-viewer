# Architecture

This document describes the target architecture. Roadmap steps build it piece by piece. Change this document in the same PR whenever the structure changes, and record lasting decisions as ADRs.

## 1. Overview

```
+-----------------------------------------------------------------+
| lv_app      main.cpp, resources, Windows manifest/version info  |
+-----------------------------------------------------------------+
| lv_ui       Qt Widgets: MainWindow, TabWidget, LogView,         |
|             FilterBar, FindBar, BookmarksPanel, dialogs         |
+-----------------------------------------------------------------+
| lv_model    QtCore only: Document, FilteredDocument, LineCache, |
|             MarkSet, Bookmarks, HighlightSets, FilterHistory,   |
|             Settings, FileWatcher, Session                      |
+-----------------------------------------------------------------+
| lv_core     C++20 + std only (+ PCRE2, simdutf): platform file, |
|             encoding, line index, indexer, line reader, search, |
|             boolean expressions, thread pool, change detector   |
+-----------------------------------------------------------------+
```

Dependencies point **downwards only**. `lv_core` must not include any Qt header (enforced by CMake: it does not link Qt). `lv_model` must not include `QtWidgets`/`QtGui` widgets headers (it may use `QColor` from QtGui). See ADR 0006.

## 2. Core concepts

| Concept | Meaning |
| --- | --- |
| `ByteOffset` | 64-bit offset into the raw file. |
| `LineNumber` | 64-bit, 0-based line index into a document (the UI shows +1). |
| `LineCount` | 64-bit number of lines. |
| Generation | A counter per document, incremented whenever the document restarts from scratch (reload, encoding change, truncation, rotation). Every asynchronous result carries the generation it was computed for; results of an old generation are dropped. |
| Published line count | The number of lines that are fully indexed and visible to readers. Grows monotonically within a generation. |

Line content is addressed as a byte range `[start, end)` in the raw file, **excluding** the line terminator (`\n` or `\r\n`, encoded in the document's encoding).

## 3. lv_core modules

All in namespace `lv::core`. Every module has its own directory under `src/core/` and its own tests under `tests/core/`.

### 3.1 `platform/` – file access (ADR 0005)

- `File`: opens a path **read-only with full sharing** (Windows: `CreateFileW` with `FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE`; POSIX: `open(O_RDONLY | O_CLOEXEC)`).
- `readAt(ByteOffset, std::span<std::byte>) -> Result<std::size_t>`: positional read (`ReadFile` with `OVERLAPPED` offset / `pread`). Thread-safe; does not use a shared file pointer.
- `size() -> Result<std::uint64_t>`: size **from the open handle** (`GetFileSizeEx` / `fstat`). Never from a directory listing: NTFS updates directory entries lazily while a writer keeps the file open.
- `identity() -> FileIdentity`: Windows volume serial + 128-bit file ID (`GetFileInformationByHandleEx(FileIdInfo)`); POSIX `st_dev` + `st_ino`.
- `static identityOfPath(path)`: identity of whatever file the path currently refers to (opened briefly with full sharing).
- No memory mapping (ADR 0005): on Windows, an open mapping makes `SetEndOfFile` in the writing process fail, which would break log rotation by truncation, and access to truncated mapped pages raises exceptions.
- Paths are `std::filesystem::path` (wide on Windows). Long paths are supported via the application manifest (`longPathAware`).

### 3.2 `encoding/`

- `Encoding` enum: `Utf8`, `Utf16Le`, `Utf16Be`, `Windows1252`, `Iso8859_1`, later more single-byte code pages.
- `EncodingTraits`: code unit size (1 or 2), byte representation of `\n` and `\r`, BOM length.
- `detectEncoding(std::span<const std::byte> head) -> DetectionResult` (BOM → UTF-8 validity via simdutf → Windows-1252). See F-ENC-2.
- `decodeLine(std::span<const std::byte>, Encoding, DecodeOptions) -> std::u16string`: converts raw bytes to UTF-16 for display (UTF-8 invalid sequences → U+FFFD, single-byte code pages via 256-entry tables, UTF-16 byte swapping). Also offers `toUtf8` for search when the document is not UTF-8 or single-byte ASCII-compatible.
- Column mapping utilities: expand tabs, map between byte offsets, UTF-16 code unit offsets, and display columns (needed for selection and match highlighting).

### 3.3 `index/` – line index (ADR 0007)

- `SortedU64Sequence`: append-only, strictly increasing sequence of 64-bit values, stored in blocks of 128 values. Each block stores the first value (64 bit), a bit width `w`, and 127 deltas bit-packed with `w` bits. Random access is O(block) with a one-block decode cache; `lowerBound` is a binary search over block heads plus a scan. Typical cost: about 1 byte per line for logs with lines under 255 bytes. Blocks whose deltas exceed 32 bits fall back to raw storage.
  - Used for line end offsets (line index) and for match lists (filtered line numbers).
- Storage is **segmented**: blocks live in fixed-size segments that never move, and a segment directory with fixed capacity, so readers can access the published range **without locks** while the single writer appends. The published count is a `std::atomic<std::uint64_t>` written with release and read with acquire semantics.
- `LineIndex`: wraps a `SortedU64Sequence` of line **end** offsets (position after the terminator) plus per-line flag "terminated with CRLF" encoded implicitly by checking the byte before `\n` when reading. API: `lineCount()`, `lineRange(LineNumber) -> ByteRange` (excluding terminator), `lineContaining(ByteOffset)`, `maxLineLength()`.
- An unterminated last line is **not** in the index; it is tracked separately as a "pending tail" (offset and length) and exposed as a virtual last line (F-TAIL-8). When it is completed, it is appended to the index.

### 3.4 `index/` – indexer

- `Indexer::run(File&, Encoding, ByteOffset from, LineIndex&, std::stop_token, ProgressCallback)`.
- Pipeline: one reader thread reads blocks of 4 MiB (constant, tuned by benchmarks) into a small ring of reusable buffers (3–4 buffers, no per-block allocation); the calling thread scans each buffer for line terminators and appends to the index. Single-pass, sequential.
- Newline search: `std::memchr` for 1-byte encodings; for UTF-16 a 2-byte aligned search. A SIMD path is added only if benchmarks show the scanner (not the disk) is the bottleneck (step M1-09).
- Progress is reported at most every 50 ms or 64 MiB. Cancellation is checked per block.
- Incremental mode: `from` = end of the last complete line; used for live logs.

### 3.5 `reader/` – line reader

- `LineReader::readLines(LineNumber first, LineCount count) -> Result<LineBlock>` reads the byte range of several consecutive lines in one positional read and returns views to each line. A small LRU cache of 64 KiB file pages avoids repeated reads while scrolling.
- Long lines are read fully up to the configured maximum (16 MiB); display truncation happens later (F-FILE-7).

### 3.6 `search/` – matching and filtering

- `Matcher` interface: `bool matches(std::string_view line, MatchScratch&) const` and `void collectRanges(std::string_view line, std::vector<ByteRange>&) const` (for highlighting). Matchers are immutable and shared between threads; per-thread mutable state lives in `MatchScratch` (for example PCRE2 match data and JIT stack).
- `LiteralMatcher`: case-sensitive via `std::boyer_moore_horspool_searcher` or `memchr`-based search; case-insensitive ASCII fast path; non-ASCII case-insensitive literals are compiled to a PCRE2 pattern.
- `RegexMatcher` (PCRE2, 8-bit library, `PCRE2_UTF` for UTF-8 input, JIT compiled, `PCRE2_NO_UTF_CHECK` after the line was validated or sanitized). Match and depth limits are set (F-ROB-3); lines exceeding them count as "skipped".
- `BoolExpr`: tokenizer + recursive descent parser for SPEC §5.1 producing an AST (`And`, `Or`, `Not`, `Term`). Errors carry a character position. Evaluation short-circuits; cheap literal terms are evaluated before regex terms where semantics allow.
- `Query`: what the user entered (text, mode, options) → `CompiledQuery` (a `Matcher`). Compilation errors are returned, never thrown.
- Search runs on **UTF-8** text: documents in UTF-8 or ASCII-compatible single-byte encodings are searched in place when the pattern is pure ASCII; otherwise lines are converted to UTF-8 per chunk (simdutf / tables) before matching.
- `SearchEngine::run(Index snapshot, LineRange, CompiledQuery, ThreadPool&, std::stop_token, ResultSink&)`:
  - Splits the range into chunks of about 1 MiB of text (aligned to lines via the index).
  - Workers read their chunk with their own positional reads, match line by line, and produce a sorted vector of matching line numbers per chunk.
  - A reorder buffer emits chunk results **in line order** to the `ResultSink`, which appends them to a `SortedU64Sequence` (the match list) and publishes progress. Partial results are therefore always a correct prefix.
  - Optimization (step M3-11): for a single regex or literal, scan a whole chunk with one search call and map hits back to lines instead of matching every line separately.
- `ThreadPool`: fixed number of `std::jthread` workers (default: hardware concurrency − 1, min 1), a bounded MPMC queue, task cancellation through `std::stop_token`. One global pool for searches. No TBB (ADR 0008).

### 3.7 `tail/` – change detection

- `ChangeDetector::check(const File& open, const path&, Snapshot last) -> Change` classifies: `Unchanged`, `Grown`, `Truncated` (size smaller, or the first 4 KiB fingerprint changed), `Replaced` (path identity differs from open handle identity: rotation), `Missing` (path does not exist).
- Pure function over observations, unit-tested with fakes; plus integration tests with real files.

### 3.8 `concurrency/`, `util/`

- `Result<T>` = `tl::expected<T, Error>`; `Error` has a code enum, an OS error code, and an English message.
- Strong types (`LineNumber`, …), `LV_ASSERT` (active in debug and sanitizer builds), scope guards.
- Logging: `lv::core::log(Level, std::string_view)` forwards to a sink installed by the app (the app writes to `logviewer.log` in the settings directory, rotated at 1 MiB). No logging in hot loops.

## 4. Threading model

| Thread | Work | Talks to UI via |
| --- | --- | --- |
| UI thread | Painting, input, reading **visible lines only** (bounded: at most one screen, at most 4 MiB per paint) through `LineCache` | – |
| Indexer thread (one per document) | Full and incremental indexing; owns the write side of the `LineIndex` | Queued signals from `Document` |
| Indexer read-ahead thread (one per active indexing) | Sequential block reads | internal ring buffer |
| Search pool (global) | Filter, quick find, highlight pre-computation for large copies | Queued signals from `FilteredDocument` |
| Copy worker | Large selections to clipboard text | Queued signal, then clipboard set on UI thread |

Rules:

- Workers never touch `QObject`s that live in the UI thread. Results are posted with queued connections.
- Every asynchronous job captures the document **generation** and a `std::stop_source`. Starting a new job of the same kind (for example a new filter) requests stop on the previous one.
- Readers of the line index and match lists use only the published range; there are no locks on the read path.
- Shutdown: closing a tab requests stop on all jobs of the document and joins its threads before destroying the index (with a timeout and a log entry if a worker hangs).

## 5. lv_model (QtCore)

- `Document`: one opened file. Owns `File`, `LineIndex`, `LineReader`, the indexer thread, and the change watcher. Signals: `indexingProgress`, `linesAppended(LineCount newCount)`, `reset(Generation)`, `stateChanged(State)` (`Indexing`, `Ready`, `Missing`, `Error`), `notice(QString)`.
- `FilteredDocument`: the filter state of a tab. Holds the current `CompiledQuery`, the match list, and the merge with marks (F-FILT-9). Runs full searches and incremental searches when `linesAppended` arrives.
- `LineSource` interface used by views: `lineCount()`, `originalLineNumber(row)`, `text(row)`, `decorations(row)`. Implemented by `DocumentLineSource` (main view) and `FilteredLineSource` (filtered view). The view does not know whether it shows the full file or a filter.
- `LineCache`: LRU cache of decoded lines (`QString` plus metadata) for the views, keyed by `(generation, LineNumber)`. Default capacity 10,000 lines.
- `MarkSet` (per document): sorted container of marked line numbers with optional color index. `Bookmarks` (per document): line → name.
- `HighlightSets` (global): rule sets; compiled matchers are cached and shared with the views.
- `FilterHistory` (global), `RecentFiles` (global), `Session`.
- `Settings`: typed accessors over `QSettings` INI files, with schema version and migration (F-SET-2, F-SET-3).
- `FileWatcher`: `QFileSystemWatcher` on the file and its directory plus a `QTimer` polling fallback, both feeding `ChangeDetector`. Events are coalesced (max one check per 50 ms).

## 6. lv_ui (Qt Widgets)

- `LogView` (`QAbstractScrollArea`): custom painting of visible rows only, gutter (line numbers, mark and bookmark symbols), current line, selection, match and highlight rule painting, horizontal scrolling, keyboard and mouse handling.
  - Vertical scroll bar: `QScrollBar` is `int`-based; when `lineCount > 2^30`, the scroll bar range is scaled and the exact top line is kept as a 64-bit value.
  - Text layout: fixed-pitch font assumption for column math is **not** made; glyph positions are measured with `QFontMetricsF`/`QTextLayout` per visible line and cached, so non-ASCII and wide characters select correctly.
- `FilterBar`: mode selector, options, editable history combo box, apply, clear, progress, match count, error label.
- `FindBar`, `BookmarksPanel` (dock), `HighlightDialog`, `SettingsDialog`, `GoToLineDialog`.
- `DocumentTab`: main `LogView` + splitter + `FilterBar` + filtered `LogView` for one `Document`.
- `MainWindow`: tabs, menus, toolbar, status bar, drag and drop, recent files, session.

## 7. Dependencies

Only these. Adding one needs the owner's approval and an ADR (AGENTS.md §4.2).

| Dependency | Version policy | License | Used by | Source |
| --- | --- | --- | --- | --- |
| Qt 6 (Core, Gui, Widgets, Network for `QLocalServer` only, Test, LinguistTools) | Build minimum 6.8; releases use the pinned version in `.github/qt-version` (currently 6.12 LTS) | LGPL-3.0, dynamic linking | model, ui, app | System packages (Linux dev), `aqtinstall` (CI, Windows) |
| PCRE2 (8-bit, JIT) | Latest via vcpkg baseline | BSD-3-Clause with exception | core/search | vcpkg |
| simdutf | Latest via vcpkg baseline | Apache-2.0 or MIT | core/encoding | vcpkg |
| tl-expected | Latest via vcpkg baseline | CC0-1.0 | core | vcpkg |
| Catch2 v3 | Latest via vcpkg baseline | BSL-1.0 | tests only | vcpkg |
| Google Benchmark | Latest via vcpkg baseline | Apache-2.0 | bench only | vcpkg |

Not used, on purpose: TBB (own small pool is enough, ADR 0008), efsw (`QFileSystemWatcher` + polling), Hyperscan/Vectorscan (only if PCRE2 cannot meet the search budgets; would need a new ADR), uchardet (GPL/MPL; simple detection is enough for the target encodings), any network library.

## 8. Error handling

- `lv_core` returns `Result<T>`; no exceptions cross module boundaries.
- `lv_model` converts errors into `Document` states and user-facing, translated messages.
- Programming errors use `LV_ASSERT` (abort in debug/sanitizer builds, logged in release).

## 9. Persistence

| Data | Location (normal) | Format |
| --- | --- | --- |
| Settings, highlight sets, filter history, recent files, session | `%APPDATA%\LogViewer\` / `~/.config/LogViewer/` | INI via `QSettings` (highlight sets also JSON export) |
| Marks and bookmarks per file (F-PERS-1) | same directory, `files/<hash>.ini` | INI, at most 500 files, LRU eviction |
| App log, minidumps | same directory, `logviewer.log`, `dumps/` | text, `.dmp` |

Portable mode (F-SET-2) replaces the directory with the executable's directory.
