#include "model/line_finder.h"

#include "core/index/sorted_u64_sequence.h"
#include "core/search/search_engine.h"
#include "model/document.h"
#include "model/search_pool.h"

#include <QMetaObject>

#include <algorithm>
#include <span>

namespace lv::model
{
namespace
{

bool contains(const core::SortedU64Sequence& sequence, std::uint64_t line)
{
    const std::size_t index = sequence.lowerBound(line);
    return index < sequence.size() && sequence.at(index) == line;
}

// Collects the first (forward) or last match of a range and stops the search after the first.
class FindSink final : public core::ResultSink
{
public:
    FindSink(const core::SortedU64Sequence* restrict, bool wantLast, std::stop_source& stop)
        : m_restrict(restrict)
        , m_wantLast(wantLast)
        , m_stop(&stop)
    {}

    void append(std::span<const std::uint64_t> lines,
                const core::SearchProgress& /*progress*/) override
    {
        for (const std::uint64_t line : lines) {
            if (m_restrict != nullptr && !contains(*m_restrict, line)) {
                continue;
            }
            m_line = line;
            if (!m_wantLast) {
                m_stop->request_stop();
                return;
            }
        }
    }

    [[nodiscard]] std::optional<std::uint64_t> line() const noexcept { return m_line; }

private:
    const core::SortedU64Sequence* m_restrict;
    bool m_wantLast;
    std::stop_source* m_stop;
    std::optional<std::uint64_t> m_line;
};

struct RangeResult
{
    std::optional<std::uint64_t> line;
    bool failed = false;
};

RangeResult searchRange(const DocumentContent& content, const core::LineIndex::Snapshot& snapshot,
                        const core::Matcher& matcher, const core::SortedU64Sequence* restrict,
                        std::uint64_t first, std::uint64_t count, bool wantLast,
                        const std::stop_token& outer)
{
    if (count == 0) {
        return {};
    }
    std::stop_source local;
    const std::stop_callback forward(outer, [&local] { local.request_stop(); });
    core::SearchRequest request;
    request.file = &content.file;
    request.index = &content.index;
    request.snapshot = snapshot;
    request.range =
        core::SearchRange{.first = core::LineNumber(first), .count = core::LineCount(count)};
    request.matcher = &matcher;
    FindSink sink(restrict, wantLast, local);
    const auto result =
        core::SearchEngine::run(request, searchThreadPool(), local.get_token(), sink);
    if (sink.line()) {
        return {.line = sink.line(), .failed = false};
    }
    return {.line = std::nullopt, .failed = !result.has_value() && !outer.stop_requested()};
}

// Last match in [first, end), searching blocks from the end so that a nearby match is found
// without scanning the whole range.
RangeResult searchBackward(const DocumentContent& content,
                           const core::LineIndex::Snapshot& snapshot, const core::Matcher& matcher,
                           const core::SortedU64Sequence* restrict, std::uint64_t first,
                           std::uint64_t end, const std::stop_token& stop)
{
    std::uint64_t block = 65536;
    while (end > first && !stop.stop_requested()) {
        const std::uint64_t begin = end - std::min(block, end - first);
        auto result =
            searchRange(content, snapshot, matcher, restrict, begin, end - begin, true, stop);
        if (result.line || result.failed) {
            return result;
        }
        end = begin;
        block = std::min<std::uint64_t>(block * 2, std::uint64_t{1} << 24U);
    }
    return {};
}

}  // namespace

FindResult findLine(const DocumentContent& content, const core::Matcher& matcher,
                    const core::SortedU64Sequence* restrict, std::optional<std::uint64_t> from,
                    FindDirection direction, const std::stop_token& stop)
{
    const auto snapshot = content.index.snapshot();
    const std::uint64_t total = snapshot.lineCount().get();
    if (total == 0) {
        return {};
    }
    const auto finish = [&](const RangeResult& range, bool wrapped) {
        return FindResult{.line = range.line,
                          .wrapped = wrapped && range.line.has_value(),
                          .failed = range.failed};
    };
    if (direction == FindDirection::kForward) {
        const std::uint64_t start = from ? std::min(*from + 1, total) : 0;
        auto first =
            searchRange(content, snapshot, matcher, restrict, start, total - start, false, stop);
        if (first.line || first.failed || !from || stop.stop_requested()) {
            return finish(first, false);
        }
        return finish(searchRange(content, snapshot, matcher, restrict, 0, start, false, stop),
                      true);
    }
    const std::uint64_t start = from ? std::min(*from, total) : total;
    auto first = searchBackward(content, snapshot, matcher, restrict, 0, start, stop);
    if (first.line || first.failed || !from || stop.stop_requested()) {
        return finish(first, false);
    }
    return finish(searchBackward(content, snapshot, matcher, restrict, start, total, stop), true);
}

LineFinder::LineFinder(QObject* parent)
    : QObject(parent)
{}

LineFinder::~LineFinder()
{
    cancel();
}

void LineFinder::cancel()
{
    ++m_ticket;
    m_running = false;
    if (m_worker.joinable()) {
        m_worker.request_stop();
        m_worker.join();
    }
}

void LineFinder::start(std::shared_ptr<const DocumentContent> content,
                       std::shared_ptr<const core::CompiledQuery> query,
                       std::shared_ptr<const core::SortedU64Sequence> restrict,
                       std::optional<std::uint64_t> from, FindDirection direction)
{
    cancel();
    if (!content || !query) {
        emit finished(FindResult{});
        return;
    }
    m_running = true;
    const std::uint64_t ticket = m_ticket;
    m_worker = std::jthread([this, ticket, content = std::move(content), query = std::move(query),
                             restrict = std::move(restrict), from,
                             direction](const std::stop_token& stop) {
        const FindResult result = findLine(*content, *query, restrict.get(), from, direction, stop);
        if (stop.stop_requested()) {
            return;
        }
        // Queued to the UI thread; dropped if this object is destroyed first (it joins us).
        QMetaObject::invokeMethod(
            this,
            [this, ticket, result] {
                if (ticket != m_ticket) {
                    return;  // canceled or superseded
                }
                m_running = false;
                emit finished(result);
            },
            Qt::QueuedConnection);
    });
}

}  // namespace lv::model
