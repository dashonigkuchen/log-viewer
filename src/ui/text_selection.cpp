#include "ui/text_selection.h"

#include <algorithm>

namespace lv::ui
{

std::optional<SelectedColumns> TextSelection::columnsForRow(std::uint64_t row,
                                                            qsizetype length) const
{
    if (isEmpty()) {
        return std::nullopt;
    }
    const TextPosition first = start();
    const TextPosition last = end();
    if (row < first.row || row > last.row) {
        return std::nullopt;
    }
    if (lineMode) {
        return SelectedColumns{.from = 0, .to = length, .lineEnd = true};
    }
    const qsizetype from = row == first.row ? std::clamp<qsizetype>(first.column, 0, length) : 0;
    const qsizetype to = row == last.row ? std::clamp<qsizetype>(last.column, 0, length) : length;
    const bool lineEnd = row < last.row;
    if (from >= to && !lineEnd) {
        return std::nullopt;
    }
    return SelectedColumns{.from = from, .to = std::max(from, to), .lineEnd = lineEnd};
}

}  // namespace lv::ui
