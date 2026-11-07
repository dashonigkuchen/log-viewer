#pragma once

#include "core/index/sorted_u64_sequence.h"
#include "core/search/query.h"
#include "core/search/search_engine.h"

#include <QObject>
#include <QString>

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>

namespace lv::core
{
class ThreadPool;
}

namespace lv::model
{

class Document;

// The filter of one document (ARCHITECTURE §5, SPEC F-FILT-1): runs the search on a worker
// thread and exposes the growing, ordered list of matching line numbers.
//
// Every apply(), clear(), and document reset starts a new filter generation with a new match
// list; results and signals of older generations never reach listeners. While the document is
// indexing, the filter waits and runs when indexing has finished (incremental filtering of
// growing files follows in M4). A document reload clears the matches and re-runs the filter.
//
// Thread-safety: UI thread only.
class FilteredDocument : public QObject
{
    Q_OBJECT

public:
    enum class State : std::uint8_t
    {
        kInactive,   // no filter
        kPending,    // waiting for the document to finish indexing
        kSearching,  // results are growing
        kDone,       // all lines searched
        kError,      // the search failed (see errorMessage()); results so far stay visible
    };
    Q_ENUM(State)

    FilteredDocument(const Document& document, core::ThreadPool& pool, QObject* parent = nullptr);
    FilteredDocument(const FilteredDocument&) = delete;
    FilteredDocument& operator=(const FilteredDocument&) = delete;
    FilteredDocument(FilteredDocument&&) = delete;
    FilteredDocument& operator=(FilteredDocument&&) = delete;
    ~FilteredDocument() override;

    // Compiles and applies `query`, canceling a running search. Returns the compile error (and
    // keeps the previous filter) if the query is invalid.
    [[nodiscard]] std::optional<core::QueryError> apply(const core::Query& query);

    // Removes the filter and its results.
    void clear();

    [[nodiscard]] State state() const noexcept { return m_state; }

    [[nodiscard]] const std::optional<core::Query>& query() const noexcept { return m_query; }

    [[nodiscard]] std::shared_ptr<const core::CompiledQuery> compiledQuery() const
    {
        return m_compiled;
    }

    [[nodiscard]] std::uint64_t generation() const noexcept { return m_generation; }

    [[nodiscard]] const QString& errorMessage() const noexcept { return m_errorMessage; }

    // Number of matches announced to listeners so far (a prefix of the final result).
    [[nodiscard]] std::uint64_t matchCount() const noexcept { return m_matchCount; }

    // Line number of match `index` (< matchCount()).
    [[nodiscard]] core::LineNumber matchAt(std::uint64_t index) const;

    // Index of the first match at or after `line` (matchCount() if none).
    [[nodiscard]] std::uint64_t lowerBound(core::LineNumber line) const;

    // The match list of the current generation (shared with the worker; for RowReader).
    [[nodiscard]] std::shared_ptr<const core::SortedU64Sequence> matches() const
    {
        return m_matches;
    }

    [[nodiscard]] const core::SearchProgress& progress() const noexcept { return m_progress; }

    [[nodiscard]] const Document& document() const noexcept { return *m_document; }

signals:
    void stateChanged(lv::model::FilteredDocument::State state);
    // Matches grew to `matchCount`.
    void matchesAppended(quint64 matchCount);
    // The match list was replaced (new filter, cleared filter, document reset).
    void reset();
    void progressChanged(quint64 searchedLines, quint64 totalLines, quint64 matches,
                         quint64 skippedLines);

private:
    struct Shared;

    // Starts a new generation: cancels the worker and replaces the match list.
    void restart();
    void startSearch();
    void stopWorker();
    void setState(State state);
    Q_INVOKABLE void drainEvents();
    void onDocumentReset();
    void onDocumentStateChanged();

    const Document* m_document;
    core::ThreadPool* m_pool;
    std::optional<core::Query> m_query;
    std::shared_ptr<const core::CompiledQuery> m_compiled;
    std::shared_ptr<core::SortedU64Sequence> m_matches;
    std::uint64_t m_generation = 0;
    std::uint64_t m_matchCount = 0;
    core::SearchProgress m_progress;
    State m_state = State::kInactive;
    QString m_errorMessage;
    std::shared_ptr<Shared> m_shared;  // worker -> UI hand-over

    std::jthread m_worker;  // last member: joined first
};

}  // namespace lv::model
