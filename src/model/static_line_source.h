#pragma once

#include "model/line_source.h"

#include <QStringList>

namespace lv::model
{

// A LineSource over a fixed list of lines in memory (previews, tests). Row n is line n.
//
// Thread-safety: UI thread only.
class StaticLineSource : public LineSource
{
    Q_OBJECT

public:
    explicit StaticLineSource(QStringList lines = {}, QObject* parent = nullptr);

    // Replaces the lines and emits reset().
    void setLines(QStringList lines);

    [[nodiscard]] core::LineCount lineCount() const override;
    [[nodiscard]] core::LineNumber originalLineNumber(core::LineNumber row) const override;
    [[nodiscard]] std::vector<DisplayLine> lines(core::LineNumber first,
                                                 core::LineCount count) const override;
    [[nodiscard]] std::vector<DisplayLine> plainLines(core::LineNumber first,
                                                      core::LineCount count) const override;

private:
    QStringList m_lines;
};

}  // namespace lv::model
