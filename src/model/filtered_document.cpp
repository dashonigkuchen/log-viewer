#include "model/filtered_document.h"

#include "core/util/assert.h"
#include "model/document.h"

#include <QMetaObject>

#include <condition_variable>
#include <functional>
#include <memory>
#include <new>
#include <optional>
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
    bool restart = false;  // a listed partial line stopped matching: start over
    // UI -> worker: the document appended lines.
    bool more = false;
    std::condition_variable_any wake;
};

namespace
{

// Appends to the generation's match list and reports to the UI thread after every chunk.
// Lines up to `listed` (already in the list: a re-searched partial line) are not appended again.
class PostingSink final : public core::ResultSink
{
public:
    PostingSink(core::SortedU64Sequence& matches, std::optional<std::uint64_t> listed,
                std::function<void(const core::SearchProgress&)> post)
        : m_matches(&matches)
        , m_listed(listed)
        , m_post(std::move(post))
    {}

    void append(std::span<const std::uint64_t> lines, const core::SearchProgress& progress) override
    {
        for (const std::uint64_t line : lines) {
            if (m_listed && line <= *m_listed) {
                m_sawListed = m_sawListed || line == *m_listed;
                continue;
            }
            m_matches->append(line);
        }
        m_matches->publish();
        m_post(progress);
    }

    [[nodiscard]] bool sawListed() const noexcept { return m_sawListed; }

private:
    core::SortedU64Sequence* m_matches;
    std::optional<std::uint64_t> m_listed;
    std::function<void(const core::SearchProgress&)> m_post;
    bool m_sawListed = false;
};

}  // namespace

// The search loop of one filter generation: a full search, then incremental searches of
// appended lines whenever the UI thread signals more (Shared::more).
struct FilteredDocument::Worker
{
    using Update = std::function<void(Shared&)>;
    using Schedule = std::function<void(const Update&)>;

    enum class Outcome : std::uint8_t
    {
        kWait,  // searched (or nothing to do): wait for appended lines
        kStop,  // canceled, failed, or restarting
    };

    std::shared_ptr<const DocumentContent> content;
    std::shared_ptr<core::SortedU64Sequence> matches;
    std::shared_ptr<const core::CompiledQuery> compiled;
    core::ThreadPool* pool = nullptr;
    std::shared_ptr<Shared> shared;
    Schedule schedule;

    std::uint64_t start = 0;  // first line not searched as a complete line yet
    std::uint64_t skipped = 0;
    bool first = true;

    void run(const std::stop_token& stop)
    {
        while (!stop.stop_requested()) {
            if (pass(stop) == Outcome::kStop) {
                return;
            }
            std::unique_lock lock(shared->mutex);
            if (!shared->wake.wait(lock, stop, [this] { return shared->more; })) {
                return;
            }
            shared->more = false;
        }
    }

    void report(std::uint64_t searched, std::uint64_t total, std::uint64_t skippedLines,
                bool finished)
    {
        schedule([&](Shared& state) {
            state.finished = finished;
            state.matchCount = matches->size();
            state.progress.searchedLines = core::LineCount(searched);
            state.progress.totalLines = core::LineCount(total);
            state.progress.matches = matches->size();
            state.progress.skippedLines = skippedLines;
        });
    }

    Outcome pass(const std::stop_token& stop)
    {
        const auto snapshot = content->index.snapshot();
        const std::uint64_t end = snapshot.lineCount().get();
        if (!first && end <= start) {
            return Outcome::kWait;
        }
        // The last listed match is `start` only if it was a partial line before.
        std::optional<std::uint64_t> listed;
        if (!matches->empty()) {
            listed = matches->at(matches->size() - 1);
        }
        const bool startListed = !first && listed == start;
        PostingSink sink(*matches, first ? std::nullopt : listed,
                         [&](const core::SearchProgress& progress) {
                             report(start + progress.searchedLines.get(), end,
                                    skipped + progress.skippedLines, false);
                         });
        core::SearchRequest request;
        request.file = &content->file;
        request.index = &content->index;
        request.snapshot = snapshot;
        request.range = core::SearchRange{.first = core::LineNumber(start),
                                          .count = core::LineCount(end - start)};
        request.matcher = compiled.get();
        const auto result = core::SearchEngine::run(request, *pool, stop, sink);
        if (!result) {
            const auto code = result.error().code;
            if (code == core::ErrorCode::kCanceled) {
                return Outcome::kStop;  // a newer generation (or destruction) replaced it
            }
            if (!first && code == core::ErrorCode::kOutOfRange) {
                return Outcome::kWait;  // the file shrank: the document reload restarts us
            }
            schedule([&](Shared& state) {
                state.finished = true;
                state.error = result.error();
            });
            return Outcome::kStop;
        }
        if (startListed && !sink.sawListed()) {
            schedule([](Shared& state) { state.restart = true; });
            return Outcome::kStop;
        }
        skipped += result->skippedLines;
        start = snapshot.completeLines.get();
        first = false;
        report(end, end, skipped, true);
        return Outcome::kWait;
    }
};

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
    connect(&document, &Document::linesAppended, this, &FilteredDocument::onLinesAppended);
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
        m_shared->restart = false;
        m_shared->more = false;
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
    const Worker::Schedule schedule = [shared, self, generation](const Worker::Update& update) {
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
    auto worker = std::make_shared<Worker>(Worker{.content = content,
                                                  .matches = matches,
                                                  .compiled = compiled,
                                                  .pool = pool,
                                                  .shared = shared,
                                                  .schedule = schedule});
    m_worker = std::jthread([worker](const std::stop_token& stop) {
        try {
            worker->run(stop);
        } catch (const std::bad_alloc&) {
            // F-ROB-2: the results so far stay visible; the tab shows the error.
            worker->schedule([](Shared& state) {
                state.finished = true;
                state.error = core::Error{.code = core::ErrorCode::kOutOfMemory,
                                          .systemCode = 0,
                                          .message = "not enough memory for the filter results"};
            });
        }
    });
}

void FilteredDocument::drainEvents()
{
    std::uint64_t matchCount = 0;
    core::SearchProgress progress;
    bool finished = false;
    std::optional<core::Error> error;
    bool restartNeeded = false;
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
        restartNeeded = std::exchange(m_shared->restart, false);
    }
    if (restartNeeded) {
        restart();
        startSearch();
        return;
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

void FilteredDocument::onLinesAppended()
{
    if (!m_worker.joinable()) {
        return;  // no search yet (inactive, or pending until the document is indexed)
    }
    {
        const std::scoped_lock lock(m_shared->mutex);
        m_shared->more = true;
    }
    m_shared->wake.notify_all();
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
