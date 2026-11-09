# Functional specification

Status of each requirement: **Must** (required for 1.0), **Should** (planned for 1.0, can slip), **Later** (backlog after 1.0).
Every requirement has an ID (`F-<area>-<n>`). Roadmap steps and tests reference these IDs.

When behavior is unclear, agents ask the owner and record the answer here.

---

## 1. Files and documents

| ID | Status | Requirement |
| --- | --- | --- |
| F-FILE-1 | Must | Open a file via menu (`Ctrl+O`), drag and drop onto the window, or command line arguments (`logviewer file1.log file2.log`). |
| F-FILE-2 | Must | Files are opened **read-only with full sharing**: other processes can keep writing, rename, and delete the file while it is open (Windows: `FILE_SHARE_READ \| FILE_SHARE_WRITE \| FILE_SHARE_DELETE`). |
| F-FILE-3 | Must | The file is never loaded completely into memory. Memory use for the line index is at most 2 bytes per line on average plus a fixed overhead (see `PERFORMANCE.md`). |
| F-FILE-4 | Must | Indexing runs in the background. The first screen of lines is shown as soon as it is indexed. Line count and a progress indicator update while indexing. Indexing can be canceled; a canceled file shows the lines indexed so far. |
| F-FILE-5 | Must | Line endings `\n`, `\r\n` are supported; the `\r` is not displayed. A lone `\r` is not a line break. The last line without a trailing newline is a line. An empty file has 0 lines. |
| F-FILE-6 | Must | Files of any size up to 2^63 bytes and up to 2^63 lines are supported by the data structures (tested with synthetic indexes, real tests up to 10 GB on CI where disk space allows). |
| F-FILE-7 | Must | Very long lines (up to 16 MiB per line) are displayed correctly; display is truncated after a configurable column limit (default 100,000 characters) with a visible marker. Search always uses the full line. |
| F-FILE-8 | Must | Reload (`F5`): re-index the file from the beginning, keep the scroll position by line number when possible. |
| F-FILE-9 | Must | Recent files list (last 20), shown in the File menu. Missing files are shown disabled and can be removed. |
| F-FILE-10 | Should | Session restore: on start, reopen the tabs of the last session (file, scroll position, filter, follow mode). Option to disable. |
| F-FILE-11 | Should | Single instance: opening a file while the app is running opens it as a new tab in the running instance (IPC via `QLocalServer`). Option to disable. |
| F-FILE-12 | Must | If the file cannot be opened (missing, no permission, is a directory), show a clear error message; never crash. |

## 2. Encoding

| ID | Status | Requirement |
| --- | --- | --- |
| F-ENC-1 | Must | Supported encodings: UTF-8 (with and without BOM), Windows-1252 ("ANSI" Western), ISO-8859-1, UTF-16 LE/BE (BOM). |
| F-ENC-2 | Must | Auto detection: BOM if present; otherwise UTF-8 if the first 1 MiB is valid UTF-8; otherwise Windows-1252. The detected encoding is shown in the status bar. |
| F-ENC-3 | Must | The user can override the encoding per tab (menu `View > Encoding`); this re-indexes the file. |
| F-ENC-4 | Must | Invalid UTF-8 byte sequences are displayed as U+FFFD and never break line indexing or searching. |
| F-ENC-5 | Should | Additional single-byte code pages: Windows-1250 (Central European), Windows-1251, ISO-8859-15. |
| F-ENC-6 | Later | Other multi-byte encodings (Shift-JIS, GB18030). |

## 3. Main view

| ID | Status | Requirement |
| --- | --- | --- |
| F-VIEW-1 | Must | Custom-painted view showing only visible lines; scrolling is smooth with files of any size (scroll bar maps to line numbers with 64-bit precision). |
| F-VIEW-2 | Must | Line number gutter (1-based), can be toggled. |
| F-VIEW-3 | Must | Monospace font, configurable family and size; `Ctrl+Plus`, `Ctrl+Minus`, `Ctrl+0` zoom. |
| F-VIEW-4 | Must | Tabs are expanded to a configurable tab width (default 8). |
| F-VIEW-5 | Must | Horizontal scrolling; no line wrapping in 1.0. |
| F-VIEW-6 | Must | Keyboard navigation: arrows, Page Up/Down, `Home`/`End` (line), `Ctrl+Home`/`Ctrl+End` (file), `Ctrl+G` go to line. |
| F-VIEW-7 | Must | Text selection with the mouse (single click and drag; double-click selects a word; triple-click selects a line) and with `Shift`+navigation; multi-line selection. |
| F-VIEW-8 | Must | Copy (`Ctrl+C`) copies the selection as text. Selections of more than 100 MiB ask for confirmation; copying runs in the background with progress. |
| F-VIEW-9 | Must | `Ctrl+A` selects all lines (copy then follows F-VIEW-8). |
| F-VIEW-10 | Must | A current line is highlighted. Clicking a line makes it current. |
| F-VIEW-11 | Should | Overview bar next to the vertical scroll bar showing positions of filter matches, marks, and bookmarks; clicking jumps there. |
| F-VIEW-12 | Later | Line wrapping. |

## 4. Quick find (within the current view)

| ID | Status | Requirement |
| --- | --- | --- |
| F-FIND-1 | Must | `Ctrl+F` opens an inline find bar for the focused view (main or filtered). `F3` / `Shift+F3` next / previous match; `Enter` / `Shift+Enter` in the field do the same. |
| F-FIND-2 | Must | Options: case sensitive, whole word, regex (same engine and syntax as filters). |
| F-FIND-3 | Must | All matches in visible lines are highlighted. Searching forward or backward runs in the background through the whole file; it wraps around with a notice. |
| F-FIND-4 | Must | Invalid regex is shown inline in red with the error message; no search runs. |

## 5. Filters and the filtered view

| ID | Status | Requirement |
| --- | --- | --- |
| F-FILT-1 | Must | Each tab has a **filtered view below the main view**, separated by a splitter (position remembered). It shows all lines of the file that match the current filter, with their original line numbers. |
| F-FILT-2 | Must | Filter input field above the filtered view with a mode selector: **Literal**, **Regex**, **Boolean expression**. Options: case sensitive (default off), whole word (Literal mode only), invert (show non-matching lines). |
| F-FILT-3 | Must | Regex syntax: PCRE2 (Perl-compatible), UTF mode when the document is UTF-8 or UTF-16 (converted), JIT enabled. |
| F-FILT-4 | Must | Boolean expression syntax (mode **Boolean**): terms combined with `AND`, `OR`, `NOT` and parentheses; precedence `NOT` > `AND` > `OR`. A term is either a quoted literal `"text"` or a regex `/pattern/` (escape `/` as `\/`, `"` as `\"`). Operators are case-insensitive keywords. Example: `/ERROR\|FATAL/ AND NOT "heartbeat"`. Grammar in §5.1. |
| F-FILT-5 | Must | The filter applies when the user presses `Enter` (not while typing); changing the mode or an option applies the entered text immediately (nothing happens while the field is empty; an invalid query shows the error and keeps the previous result). `Esc` in the field restores the last applied filter. A **Clear** button removes the filter (filtered view becomes empty). |
| F-FILT-6 | Must | Filtering runs in the background on all CPU cores, shows progress and match count, and can be canceled. Partial results are shown while it runs, in line order. |
| F-FILT-7 | Must | Clicking a line in the filtered view scrolls the main view to that line and makes it current (and vice versa: the filtered view selects the nearest matching line at or before the main view's current line). |
| F-FILT-8 | Must | Matched substrings are highlighted in the filtered view and in the main view (configurable color). |
| F-FILT-9 | Must | Option "Show marks in filtered view" (default on): marked lines (F-MARK) are always included in the filtered view, visually distinct from matches. |
| F-FILT-10 | Must | Invalid filter syntax is reported inline with position and message; the previous result is kept. |
| F-FILT-11 | Should | Search range: limit filtering to a line range (from the current line to end, or a selected range). |
| F-FILT-12 | Later | Multiple filter tabs per document. |

### 5.1 Boolean expression grammar

```
expr     := orExpr
orExpr   := andExpr ( "OR" andExpr )*
andExpr  := notExpr ( "AND" notExpr )*
notExpr  := "NOT" notExpr | primary
primary  := "(" expr ")" | literal | regex
literal  := '"' ( any char except '"' and '\' | '\"' | '\\' )* '"'
regex    := '/' ( any char except '/' and '\' | '\/' | '\' any )* '/' flags?
flags    := "i"            (case-insensitive for this term only)
```

- Whitespace between tokens is ignored. Keywords are case-insensitive and must be separated by whitespace or parentheses.
- An expression consisting only of `NOT` terms is allowed (matches lines that contain none of them).
- Matching highlights (F-FILT-8) show only positive (non-negated) terms.

## 6. Filter history

| ID | Status | Requirement |
| --- | --- | --- |
| F-HIST-1 | Must | Every applied filter (text plus mode and options) is stored in a global history, most recent first, deduplicated (re-applying moves it to the top). Maximum 100 entries (configurable). |
| F-HIST-2 | Must | The filter field is an editable combo box: the dropdown shows the history; `Up`/`Down` in the field cycle through history entries; typing shows matching history entries as completion. |
| F-HIST-3 | Must | History entries can be deleted individually (`Del` in the dropdown) and the whole history can be cleared in settings. |
| F-HIST-4 | Must | The history is persisted across restarts. |
| F-HIST-5 | Should | Entries can be pinned; pinned entries stay at the top and are never evicted. |

## 7. Marks and bookmarks

Two separate concepts, matching how the owner works:

- A **mark** colors a line so it stands out and (optionally) keeps it in the filtered view. Used for "these lines matter".
- A **bookmark** is a named jump target. Used for "I want to come back here".

| ID | Status | Requirement |
| --- | --- | --- |
| F-MARK-1 | Must | Toggle a mark on the current line or all selected lines: `M` key, context menu, or click in the mark column of the gutter. Works in both views; marks belong to the document. |
| F-MARK-2 | Must | Marked lines have a distinct background color (configurable) and a mark symbol in the gutter. |
| F-MARK-3 | Must | Next / previous mark: `Ctrl+Down` / `Ctrl+Up` (configurable). |
| F-MARK-4 | Must | "Clear all marks" in the context menu with confirmation. |
| F-MARK-5 | Must | "Copy marked lines" copies all marked lines in line order. |
| F-MARK-6 | Should | Up to 9 mark colors: `Ctrl+1` ... `Ctrl+9` mark with color n. |
| F-BOOK-1 | Must | Add a bookmark on the current line (`Ctrl+B`); optional name (default: first 60 characters of the line). |
| F-BOOK-2 | Must | Bookmarks panel (dockable, toggle with `Ctrl+Shift+B`) lists bookmarks of the current tab: line number, name, line preview. Double-click jumps. Rename and delete in the panel. |
| F-BOOK-3 | Must | Next / previous bookmark: `F2` / `Shift+F2`. |
| F-BOOK-4 | Must | Bookmarked lines show a bookmark symbol in the gutter. |
| F-PERS-1 | Should | Marks and bookmarks are persisted per file (key: absolute path plus a fingerprint of the first 64 KiB) and restored when the same file is opened again. If the fingerprint differs, they are discarded with a notice. |

## 8. Highlight rules

| ID | Status | Requirement |
| --- | --- | --- |
| F-HL-1 | Must | A highlight rule has: pattern (Literal or Regex), case sensitivity, scope (**whole line** or **match only**), foreground color, background color, enabled flag. |
| F-HL-2 | Must | Rules are ordered; for whole-line scope, the first matching rule wins; match-only rules are painted on top in order. |
| F-HL-3 | Must | Rules are grouped into **highlight sets**. One set is active per tab (default: the global default set). Switching sets repaints immediately. |
| F-HL-4 | Must | A dialog to create, edit, reorder (drag and drop), enable/disable, duplicate, and delete rules and sets, with a live preview. |
| F-HL-5 | Must | Highlighting is computed only for visible lines and never blocks scrolling (cached per line; long lines are limited by F-FILE-7). |
| F-HL-6 | Must | Import and export highlight sets as JSON. |
| F-HL-7 | Should | Quick action: context menu on a selection "Highlight this text" creates a rule in the active set. |

## 9. Live logs (follow mode)

| ID | Status | Requirement |
| --- | --- | --- |
| F-TAIL-1 | Must | Each document watches its file. When the file grows, new lines are indexed incrementally and appended to the main view and, if they match, to the filtered view (incremental filtering, never a full re-scan). |
| F-TAIL-2 | Must | Change detection uses OS notifications and a polling fallback (default every 1000 ms, configurable 100–10,000 ms). Polling compares size and modification time. The polling fallback is always active as a safety net. |
| F-TAIL-3 | Must | **Follow mode** (`F10` or toolbar toggle, per tab): when on, both views stay scrolled to the end as lines arrive. Scrolling up manually turns follow mode off; `Ctrl+End` or the toggle turns it back on. |
| F-TAIL-4 | Must | New lines appear on screen within 500 ms of being written (polling at default interval) and within 100 ms when OS notifications work. |
| F-TAIL-5 | Must | **Truncation** (file size smaller than indexed size, or the first 4 KiB changed): the document re-indexes from the beginning, marks and bookmarks are cleared, the user sees a non-modal notice in the tab. |
| F-TAIL-6 | Must | **Rotation** (the path now refers to a different file, detected by file ID: Windows file index, POSIX inode/device): the document reopens the path and re-indexes from the beginning, with the same notice as F-TAIL-5. Until the new file exists, the tab keeps the old content and shows "file missing". |
| F-TAIL-7 | Must | **Deletion:** the tab keeps the indexed content (still readable while the handle is open on POSIX; on Windows, content already displayed stays visible from the cache, and reading further shows a notice) and shows "file deleted". If the file reappears, it behaves like rotation. |
| F-TAIL-8 | Must | A partially written last line (no trailing newline yet) is shown and updated when it is completed; it is not counted twice. |
| F-TAIL-9 | Must | Change bursts are coalesced; a file growing at 50 MB/s must not freeze the UI or grow memory unboundedly. |
| F-TAIL-10 | Should | Option to pause watching per tab. |

## 10. Tabs and window

| ID | Status | Requirement |
| --- | --- | --- |
| F-TAB-1 | Must | Each opened file is a tab; tab title is the file name, tooltip is the full path. The same file can be opened only once (opening it again activates its tab). |
| F-TAB-2 | Must | Close tab (`Ctrl+W`, middle click, close button); close others; close all. Reorder by drag. `Ctrl+Tab` / `Ctrl+Shift+Tab` switch tabs. |
| F-TAB-3 | Must | Tab shows activity: an indicator when the file changed while the tab is not active; a different indicator while indexing or filtering. |
| F-TAB-4 | Must | Context menu on tab: copy full path, open containing folder (Windows Explorer with the file selected), reload, close. |
| F-TAB-5 | Must | Window geometry, splitter positions, and dock layout are restored on start. |
| F-TAB-6 | Must | Status bar: file size, line count, encoding, current line, indexing/filtering progress, follow mode state. |

## 11. Settings

| ID | Status | Requirement |
| --- | --- | --- |
| F-SET-1 | Must | Settings dialog: font, tab width, line number gutter, colors (current line, marks, match highlight), polling interval, max display columns, filter history size, session restore, single instance, number of search threads (0 = automatic). |
| F-SET-2 | Must | Settings are stored as INI files in the user profile (Windows: `%APPDATA%\LogViewer\`, Linux: `~/.config/LogViewer/`). **Portable mode:** if a file `portable.ini` exists next to the executable, all settings are stored next to the executable instead. |
| F-SET-3 | Must | Settings files have a schema version; older versions are migrated, unknown keys are ignored, corrupt files are backed up and replaced with defaults with a notice. |
| F-SET-4 | Must | Dark mode: follow the Windows theme (Qt `colorScheme`), with manual override Light / Dark / System. Log view colors have light and dark defaults. |
| F-SET-5 | Should | Configurable keyboard shortcuts. |

## 12. Language

| ID | Status | Requirement |
| --- | --- | --- |
| F-I18N-1 | Must | The UI is English only. No translations and no language setting (ADR 0013). Log content in any language and encoding is still displayed correctly (section 2). |

## 13. Robustness and safety

| ID | Status | Requirement |
| --- | --- | --- |
| F-ROB-1 | Must | No crash on: empty files, binary files, files with NUL bytes, files with only `\r`, a single 16 MiB line, invalid encodings, files that disappear while being read, I/O errors. Binary files are displayed (non-printable bytes as visible placeholders). |
| F-ROB-2 | Must | Out of memory or I/O errors in a worker show an error in the tab instead of terminating the app where feasible. |
| F-ROB-3 | Must | A pathological regex (catastrophic backtracking) cannot freeze the app: PCRE2 match limits are set; lines that hit the limit are counted and reported ("N lines skipped: regex too complex"). |
| F-ROB-4 | Must | No network access of any kind (see AGENTS.md §4.3). |
| F-ROB-5 | Must | Crash diagnostics stay local: on Windows a minidump is written to the settings directory (`dumps/`, max 5 files), nothing is uploaded. |

## 14. Command line

| ID | Status | Requirement |
| --- | --- | --- |
| F-CLI-1 | Must | `logviewer [files...]` opens files. `--help`, `--version`. |
| F-CLI-2 | Should | `--follow` opens files in follow mode; `--new-instance` bypasses single instance; `--line N` jumps to line N in the first file. |
