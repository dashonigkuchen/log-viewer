#pragma once

#include "core/util/strong_types.h"

#include <QObject>

#include <cstdint>
#include <optional>
#include <set>
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

    [[nodiscard]] bool contains(core::LineNumber line) const;

    [[nodiscard]] std::size_t size() const noexcept { return m_lines.size(); }

    [[nodiscard]] bool empty() const noexcept { return m_lines.empty(); }

    // Marked lines in ascending order.
    [[nodiscard]] std::vector<core::LineNumber> lines() const;

    void toggle(core::LineNumber line);
    // Several lines (a selection): if all are marked they are unmarked, otherwise all are marked.
    void toggle(std::span<const core::LineNumber> lines);
    void clear();

    // First mark after / last mark before `line`, if any.
    [[nodiscard]] std::optional<core::LineNumber> next(core::LineNumber line) const;
    [[nodiscard]] std::optional<core::LineNumber> previous(core::LineNumber line) const;

signals:
    void changed();
    // Marks were dropped because the document started a new generation.
    void cleared(quint64 count);

private:
    std::set<std::uint64_t> m_lines;
};

}  // namespace lv::model
