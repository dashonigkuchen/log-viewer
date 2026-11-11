# Changelog

All notable changes to this project are documented in this file.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project
adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [1.0.0] - 2026-11-11

First release: a fast, read-only log viewer for Windows 11 (portable zip).

### Added

- Opening files of any size: background indexing with progress, the first lines show while
  indexing runs, compact line index (delta bit packing), page cache for line reads.
- Encodings: UTF-8, UTF-16 LE/BE, Latin-1, Windows-1250/1251/1252, ISO-8859-15, with automatic
  detection and a manual override.
- Log view that paints only the visible rows; keyboard and mouse navigation, go to line, text
  selection and copy (large copies run in the background), zoom.
- Tabs with indicators (indexing, changed, missing), drag to reorder, context menu (copy path,
  open folder, reload, close others/all), recent files.
- Filtering: literal, regular expression (PCRE2), and boolean expressions; case, whole-word, and
  invert options; multi-threaded search with progress, match count, and a Stop button; matches
  highlighted in both views; filter history with completion.
- Quick find bar.
- Follow mode for live logs: incremental indexing and filtering of appended lines, handling of
  truncation, rotation, and deletion; polling every 250 ms plus file change notifications.
- Marks in nine colors, shown in the filtered view; bookmarks with a panel; both persisted per
  file.
- Highlight rules and rule sets with a dialog.
- Overview bar with the visible range, filter matches, marks, and bookmarks.
- Settings dialog (font, colors, polling, columns, history, session, single instance, threads),
  dark mode, configurable keyboard shortcuts, portable mode.
- Session restore (tabs, positions, filters, follow mode, window and dock layout).
- Single instance: opening a file from Explorer or the command line reuses the running window;
  command line options `--follow`, `--line N`, `--new-instance`.
- Local crash dumps on Windows (at most five, never uploaded); no network access at all.
- User guide, release workflow (draft release with checksums), performance acceptance script.

### Changed

- The UI is English only; translations were removed from the plan.
- C++23 with `std::expected`.

### Fixed

- Running out of memory while indexing or filtering shows an error in the tab instead of ending
  the program.
- The default polling interval of a document is 250 ms (it was 1000 ms, so new lines could take
  about one second to appear).

[Unreleased]: https://github.com/dashonigkuchen/log-viewer/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/dashonigkuchen/log-viewer/releases/tag/v1.0.0
