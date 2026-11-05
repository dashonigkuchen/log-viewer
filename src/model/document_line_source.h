#pragma once

#include "model/line_cache.h"
#include "model/line_source.h"

namespace lv::model
{

class Document;

// The full file of a Document as a LineSource (rows are line numbers). Decodes lines with the
// document's encoding and caches them per generation.
//
// Thread-safety: UI thread only.
class DocumentLineSource : public LineSource
{
    Q_OBJECT

public:
    explicit DocumentLineSource(const Document& document, QObject* parent = nullptr,
                                std::size_t cacheCapacity = LineCache::kDefaultCapacity);

    [[nodiscard]] core::LineCount lineCount() const override;
    [[nodiscard]] core::LineNumber originalLineNumber(core::LineNumber row) const override;
    [[nodiscard]] std::vector<DisplayLine> lines(core::LineNumber first,
                                                 core::LineCount count) const override;

    [[nodiscard]] std::vector<DisplayLine> plainLines(core::LineNumber first,
                                                      core::LineCount count) const override;

    [[nodiscard]] const LineCache& cache() const noexcept { return m_cache; }

private:
    // Reads, decodes, and caches rows [first, first + count) into out[offset...].
    void load(core::LineNumber first, core::LineCount count, std::vector<DisplayLine>& out,
              std::size_t offset) const;

    const Document* m_document;
    mutable LineCache m_cache;
};

}  // namespace lv::model
