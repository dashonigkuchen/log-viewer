#pragma once

#include <QtTypes>

#include <compare>
#include <cstdint>
#include <optional>

namespace lv::ui
{

// A position between two UTF-16 code units of a row's decoded text (column 0 = line start).
struct TextPosition
{
    std::uint64_t row = 0;
    qsizetype column = 0;

    auto operator<=>(const TextPosition&) const = default;
};

// The part of one row covered by a selection.
struct SelectedColumns
{
    qsizetype from = 0;
    qsizetype to = 0;
    bool lineEnd = false;  // the line terminator after `to` is selected as well
};

// A text selection in a LogView (SPEC F-VIEW-7): from an anchor (where it started) to a cursor
// (where it ends now). In character mode the selection covers the text between the two
// positions; in line mode it covers the complete rows from the anchor row to the cursor row,
// including their line terminators (triple click, Shift+arrows, Ctrl+A).
struct TextSelection
{
    TextPosition anchor;
    TextPosition cursor;
    bool lineMode = false;

    [[nodiscard]] static TextSelection characters(TextPosition from, TextPosition to)
    {
        return {.anchor = from, .cursor = to, .lineMode = false};
    }

    [[nodiscard]] static TextSelection lines(std::uint64_t anchorRow, std::uint64_t cursorRow)
    {
        return {.anchor = {.row = anchorRow, .column = 0},
                .cursor = {.row = cursorRow, .column = 0},
                .lineMode = true};
    }

    [[nodiscard]] bool isEmpty() const noexcept { return !lineMode && anchor == cursor; }

    [[nodiscard]] TextPosition start() const noexcept { return anchor < cursor ? anchor : cursor; }

    [[nodiscard]] TextPosition end() const noexcept { return anchor < cursor ? cursor : anchor; }

    [[nodiscard]] bool operator==(const TextSelection&) const = default;

    // The selected columns of `row` whose text has `length` code units, or nullopt.
    [[nodiscard]] std::optional<SelectedColumns> columnsForRow(std::uint64_t row,
                                                               qsizetype length) const;
};

}  // namespace lv::ui
