#include "model/document_line_source.h"

#include "core/encoding/decode.h"
#include "core/util/assert.h"
#include "model/document.h"

#include <string>
#include <utility>

namespace lv::model
{

DocumentLineSource::DocumentLineSource(const Document& document, QObject* parent,
                                       std::size_t cacheCapacity)
    : LineSource(parent)
    , m_document(&document)
    , m_cache(cacheCapacity)
{
    connect(&document, &Document::linesAppended, this, &LineSource::linesAppended);
    connect(&document, &Document::reset, this, [this](quint64 generation) {
        m_cache.setGeneration(generation);
        emit reset();
    });
}

core::LineCount DocumentLineSource::lineCount() const
{
    return m_document->lineCount();
}

core::LineNumber DocumentLineSource::originalLineNumber(core::LineNumber row) const
{
    return row;
}

std::vector<DisplayLine> DocumentLineSource::lines(core::LineNumber first,
                                                   core::LineCount count) const
{
    LV_ASSERT(first.get() + count.get() <= lineCount().get());
    const auto content = m_document->content();
    std::vector<DisplayLine> out(static_cast<std::size_t>(count.get()));
    if (!content) {
        return out;
    }
    m_cache.setGeneration(content->generation);

    // Serve cached rows, read the missing ones in contiguous runs.
    std::size_t runStart = 0;
    bool inRun = false;
    for (std::size_t i = 0; i <= out.size(); ++i) {
        std::optional<DisplayLine> cached;
        if (i < out.size()) {
            cached = m_cache.find(first + core::LineCount(i));
        }
        if (i < out.size() && !cached) {
            if (!inRun) {
                runStart = i;
                inRun = true;
            }
            continue;
        }
        if (inRun) {
            load(first + core::LineCount(runStart), core::LineCount(i - runStart), out, runStart);
            inRun = false;
        }
        if (cached) {
            out[i] = std::move(*cached);
        }
    }
    return out;
}

std::vector<DisplayLine> DocumentLineSource::plainLines(core::LineNumber first,
                                                        core::LineCount count) const
{
    LV_ASSERT(first.get() + count.get() <= lineCount().get());
    std::vector<DisplayLine> out(static_cast<std::size_t>(count.get()));
    const auto content = m_document->content();
    if (!content) {
        return out;
    }
    const auto snapshot = content->index.snapshot();
    const auto block = content->reader.readLines(snapshot, first, count);
    if (!block) {
        for (auto& line : out) {
            line.unavailable = true;
        }
        return out;
    }
    std::u16string text;
    for (std::size_t i = 0; i < block->lines.size(); ++i) {
        core::decodeLine(block->line(i), content->index.encoding(), text);
        auto& line = out[i];
        line.text = QString::fromUtf16(text.data(), static_cast<qsizetype>(text.size()));
        line.truncated = block->lines[i].truncated;
        line.partial = first.get() + i >= snapshot.completeLines.get();
    }
    return out;
}

void DocumentLineSource::load(core::LineNumber first, core::LineCount count,
                              std::vector<DisplayLine>& out, std::size_t offset) const
{
    const auto content = m_document->content();
    const auto snapshot = content->index.snapshot();
    const auto block = content->reader.readLines(snapshot, first, count);
    if (!block) {
        for (std::uint64_t i = 0; i < count.get(); ++i) {
            DisplayLine unavailable;
            unavailable.unavailable = true;
            out[offset + i] = std::move(unavailable);
        }
        return;
    }
    std::u16string text;
    for (std::size_t i = 0; i < block->lines.size(); ++i) {
        core::decodeLine(block->line(i), content->index.encoding(), text, nullptr,
                         {.visibleControls = true});
        const core::LineNumber line = first + core::LineCount(i);
        DisplayLine display{
            .text = QString::fromUtf16(text.data(), static_cast<qsizetype>(text.size())),
            .truncated = block->lines[i].truncated,
            .partial = line.get() >= snapshot.completeLines.get(),
        };
        if (!display.partial) {
            m_cache.insert(line, display);  // a partial line may still change (F-TAIL-8)
        }
        out[offset + i] = std::move(display);
    }
}

}  // namespace lv::model
