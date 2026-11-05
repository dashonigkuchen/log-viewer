#pragma once

#include "model/line_source.h"

#include <QString>

#include <cstdint>
#include <utility>
#include <vector>

namespace lv::test
{

// A LineSource with a configurable number of rows; row N reads "line N" (no file needed).
class FakeLineSource : public lv::model::LineSource
{
public:
    explicit FakeLineSource(std::uint64_t rows)
        : m_rows(rows)
    {}

    void setRows(std::uint64_t rows, bool reset)
    {
        m_rows = rows;
        if (reset) {
            emit this->reset();
        } else {
            emit linesAppended(rows);
        }
    }

    void setLongRow(std::uint64_t row, QString text)
    {
        m_longRow = row;
        m_longText = std::move(text);
    }

    [[nodiscard]] core::LineCount lineCount() const override { return core::LineCount(m_rows); }

    [[nodiscard]] core::LineNumber originalLineNumber(core::LineNumber row) const override
    {
        return row;
    }

    [[nodiscard]] std::vector<lv::model::DisplayLine> lines(core::LineNumber first,
                                                            core::LineCount count) const override
    {
        std::vector<lv::model::DisplayLine> out;
        out.reserve(static_cast<std::size_t>(count.get()));
        for (std::uint64_t i = 0; i < count.get(); ++i) {
            const std::uint64_t row = first.get() + i;
            lv::model::DisplayLine line;
            line.text = row == m_longRow ? m_longText : QStringLiteral("line %1").arg(row);
            out.push_back(std::move(line));
        }
        ++m_requests;
        m_lastFirst = first.get();
        m_lastCount = count.get();
        return out;
    }

    [[nodiscard]] std::vector<lv::model::DisplayLine>
    plainLines(core::LineNumber first, core::LineCount count) const override
    {
        return lines(first, count);
    }

    [[nodiscard]] int requests() const noexcept { return m_requests; }

    [[nodiscard]] std::uint64_t lastFirst() const noexcept { return m_lastFirst; }

    [[nodiscard]] std::uint64_t lastCount() const noexcept { return m_lastCount; }

private:
    std::uint64_t m_rows;
    std::uint64_t m_longRow = UINT64_MAX;
    QString m_longText;
    mutable int m_requests = 0;
    mutable std::uint64_t m_lastFirst = 0;
    mutable std::uint64_t m_lastCount = 0;
};

}  // namespace lv::test
