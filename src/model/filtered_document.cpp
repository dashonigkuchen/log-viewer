#include "model/filtered_document.h"

#include "core/util/assert.h"
#include "model/document.h"

#include <QMetaObject>

#include <utility>

namespace lv::model
{

// State the worker hands to the UI thread. Updates are coalesced: the worker overwrites the
// latest values and schedules one drain while none is pending.
struct FilteredDocument::Shared
{
    std::mutex mutex;
    std::uint64_t generation = 0;
    bool drainScheduled = false;
    bool changed = false;
    std::uint64_t matchCount = 0;
    core::SearchProgress progress;
    bool finished = false;
    std::optional<core::Error> error;
};

namespace
{

// Appends to the generation's match list and reports to the UI thread after every chunk.
class PostingSink final : public core::ResultSink
{
public:
    PostingSink(core::SortedU64Sequence& matches,
                std::function<void(const core::SearchProgress&)> post)
        : m_matches(&matches)
        , m_post(std::move(post))
    {}

    void append(std::span<const std::uint64_t> lines, const core::SearchProgress& progress) override
    {
        m_matches->append(lines);
        m_matches->publish();
        m_post(progress);
    }

private:
    core::SortedU64Sequence* m_matches;
    std::function<void(const core::SearchProgress&)> m_post;
};

}  // namespace

FilteredDocument::FilteredDocument(const Document& document, core::ThreadPool& pool,
                                   QObject* parent)
    : QObject(parent)
    , m_document(&document)
    , m_pool(&pool)
    , m_matches(std::make_shared<core::SortedU64Sequence>())
    , m_shared(std::make_shared<Shared>())
{
    connect(&document, &Document::reset, this, &FilteredDocument::onDocumentReset);
    connect(&document, &Document::stateChanged, this, &FilteredDocument::onDocumentStateChanged);
}

FilteredDocument::~FilteredDocument()
{
    stopWorker();
}

std::optional<core::QueryError> FilteredDocument::apply(const core::Query& query)
{
    auto compiled = core::CompiledQuery::compile(query);
    if (!compiled) {
        return compiled.error();
    }
    m_query = (*compiled)->query();
    m_compiled = std::move(*compiled);
    restart();
    startSearch();
    return std::nullopt;
}

void FilteredDocument::clear()
{
    m_query.reset();
    m_compiled.reset();
    restart();
    setState(State::kInactive);
}

core::LineNumber FilteredDocument::matchAt(std::uint64_t index) const
{
    LV_ASSERT(index < m_matchCount);
    return core::LineNumber(m_matches->at(static_cast<std::size_t>(index)));
}

std::uint64_t FilteredDocument::lowerBound(core::LineNumber line) const
{
    // The sequence may already hold more published matches than announced; clamp.
    return std::min<std::uint64_t>(m_matches->lowerBound(line.get()), m_matchCount);
}

void FilteredDocument::stopWorker()
{
    if (m_worker.joinable()) {
        m_worker.request_stop();
        m_worker.join();
    }
}

void FilteredDocument::restart()
{
    stopWorker();
    ++m_generation;
    {
        const std::scoped_lock lock(m_shared->mutex);
        m_shared->generation = m_generation;
        m_shared->changed = false;
        m_shared->finished = false;
        m_shared->error.reset();
        m_shared->matchCount = 0;
        m_shared->progress = {};
    }
    m_matches = std::make_shared<core::SortedU64Sequence>();
    m_matchCount = 0;
    m_progress = {};
    m_errorMessage.clear();
    emit reset();
}

void FilteredDocument::startSearch()
{
    if (!m_compiled) {
        return;
    }
    const auto content = m_document->content();
    if (!content || m_document->state() != Document::State::kReady) {
        setState(m_document->state() == Document::State::kError ? State::kError : State::kPending);
        if (m_document->state() == Document::State::kError) {
            m_errorMessage = m_document->errorMessage();
        }
        return;
    }
    setState(State::kSearching);
    const std::uint64_t generation = m_generation;
    auto matches = m_matches;
    auto compiled = m_compiled;
    auto shared = m_shared;
    core::ThreadPool* pool = m_pool;
    // Raw pointer for the wake-up only: the destructor joins the worker before `this` dies, and
    // drainEvents() runs on the UI thread.
    FilteredDocument* self = this;
    const auto schedule = [shared, self, generation](auto&& update) {
        bool wake = false;
        {
            const std::scoped_lock lock(shared->mutex);
            if (shared->generation != generation) {
                return;
            }
            update(*shared);
            shared->changed = true;
            wake = !std::exchange(shared->drainScheduled, true);
        }
        if (wake) {
            QMetaObject::invokeMethod(self, "drainEvents", Qt::QueuedConnection);
        }
    };
    m_worker =
        std::jthread([content, matches, compiled, pool, schedule](const std::stop_token& stop) {
            PostingSink sink(*matches, [&](const core::SearchProgress& progress) {
                schedule([&](Shared& state) {
                    state.matchCount = matches->size();
                    state.progress = progress;
                });
            });
            core::SearchRequest request;
            request.file = &content->file;
            request.index = &content->index;
            request.snapshot = content->index.snapshot();
            request.matcher = compiled.get();
            const auto result = core::SearchEngine::run(request, *pool, stop, sink);
            if (!result && result.error().code == core::ErrorCode::kCanceled) {
                return;  // a newer generation (or destruction) replaced this search
            }
            schedule([&](Shared& state) {
                state.finished = true;
                if (!result) {
                    state.error = result.error();
                } else {
                    state.matchCount = matches->size();
                    state.progress.searchedLines = result->searchedLines;
                    state.progress.totalLines = request.snapshot.lineCount();
                    state.progress.matches = result->matches;
                    state.progress.skippedLines = result->skippedLines;
                }
            });
        });
}

void FilteredDocument::drainEvents()
{
    std::uint64_t matchCount = 0;
    core::SearchProgress progress;
    bool finished = false;
    std::optional<core::Error> error;
    {
        const std::scoped_lock lock(m_shared->mutex);
        m_shared->drainScheduled = false;
        if (m_shared->generation != m_generation || !m_shared->changed) {
            return;
        }
        m_shared->changed = false;
        matchCount = m_shared->matchCount;
        progress = m_shared->progress;
        finished = m_shared->finished;
        error = m_shared->error;
    }
    m_progress = progress;
    if (matchCount > m_matchCount) {
        m_matchCount = matchCount;
        emit matchesAppended(m_matchCount);
    }
    emit progressChanged(progress.searchedLines.get(), progress.totalLines.get(), progress.matches,
                         progress.skippedLines);
    if (finished) {
        if (error) {
            m_errorMessage = QString::fromStdString(error->message);
            setState(State::kError);
        } else {
            setState(State::kDone);
        }
    }
}

void FilteredDocument::onDocumentReset()
{
    if (!m_compiled) {
        return;
    }
    restart();
    startSearch();  // pending until the new generation is indexed
}

void FilteredDocument::onDocumentStateChanged()
{
    // A filter applied while the document was indexing runs once indexing has finished.
    if (m_compiled && m_state == State::kPending &&
        m_document->state() != Document::State::kIndexing) {
        startSearch();
    }
}

void FilteredDocument::setState(State state)
{
    if (state != m_state) {
        m_state = state;
        emit stateChanged(state);
    }
}

}  // namespace lv::model
