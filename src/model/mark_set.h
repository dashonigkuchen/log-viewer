#pragma once

#include "core/util/strong_types.h"

#include <QObject>

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <vector>

namespace lv::model
{

class Document;

// The marked lines of one document (SPEC F-MARK-1 … F-MARK-5), by original line number. Marks
// belong to the document, so both views share them. A new document generation (reload,
// truncation, rotation) invalidates line numbers: the marks are cleared and cleared() reports
// how many were lost, so that the tab can show a notice (F-TAIL-5).
//
// Thread-safety: UI thread only.
class MarkSet : public QObject
{
    Q_OBJECT

public:
    explicit MarkSet(const Document& document, QObject* parent = nullptr);

    // Mark colors (F-MARK-6): 0 is the default color, 1 … kColorCount - 1 come from Ctrl+1 …
    // Ctrl+9.
    static constexpr int kColorCount = 10;

    [[nodiscard]] bool contains(core::LineNumber line) const;
    // Color index of a marked line, or nullopt if the line is not marked.
    [[nodiscard]] std::optional<int> colorOf(core::LineNumber line) const;

    [[nodiscard]] std::size_t size() const noexcept { return m_lines.size(); }

    [[nodiscard]] bool empty() const noexcept { return m_lines.empty(); }

    // Marked lines in ascending order.
    [[nodiscard]] std::vector<core::LineNumber> lines() const;

    void toggle(core::LineNumber line);
    // Several lines (a selection): if all are marked they are unmarked, otherwise all are marked
    // (unmarked lines get the default color, marked ones keep theirs).
    void toggle(std::span<const core::LineNumber> lines);
    // Ctrl+n: if all lines already have color `color` they are unmarked, otherwise all are marked
    // with that color.
    void toggleColor(std::span<const core::LineNumber> lines, int color);
    void clear();

    // First mark after / last mark before `line`, if any.
    [[nodiscard]] std::optional<core::LineNumber> next(core::LineNumber line) const;
    [[nodiscard]] std::optional<core::LineNumber> previous(core::LineNumber line) const;

signals:
    void changed();
    // Marks were dropped because the document started a new generation.
    void cleared(quint64 count);

private:
    std::map<std::uint64_t, std::uint8_t> m_lines;  // line -> color index
};

}  // namespace lv::model
