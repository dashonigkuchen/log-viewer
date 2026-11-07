#pragma once

#include "core/search/query.h"

#include <QObject>

#include <cstdint>
#include <memory>
#include <optional>
#include <thread>

namespace lv::core
{
class SortedU64Sequence;
}

namespace lv::model
{

struct DocumentContent;

enum class FindDirection : std::uint8_t
{
    kForward,
    kBackward,
};

struct FindResult
{
    std::optional<std::uint64_t> line;  // nullopt = no match
    bool wrapped = false;               // found only after wrapping around the file end/start
    bool failed = false;                // the search could not run (I/O error, file changed)
};

// Finds the next or previous matching line (SPEC F-FIND-3) with the search engine. Forward
// searches lines after `from`, then wraps to [0, from]; backward searches lines before `from`,
// then wraps to [from, end). `from` = nullopt starts at the first (forward) or last (backward)
// line without wrapping. `restrict` limits the result to lines in that sequence (the filtered
// view). Blocks; returns an empty result when `stop` was requested.
[[nodiscard]] FindResult findLine(const DocumentContent& content, const core::Matcher& matcher,
                                  const core::SortedU64Sequence* restrict,
                                  std::optional<std::uint64_t> from, FindDirection direction,
                                  const std::stop_token& stop);

// Runs findLine on a worker thread; one search at a time, a new start() cancels the previous
// one. Results arrive on the UI thread.
//
// Thread-safety: UI thread only.
class LineFinder : public QObject
{
    Q_OBJECT

public:
    explicit LineFinder(QObject* parent = nullptr);
    LineFinder(const LineFinder&) = delete;
    LineFinder& operator=(const LineFinder&) = delete;
    LineFinder(LineFinder&&) = delete;
    LineFinder& operator=(LineFinder&&) = delete;
    ~LineFinder() override;

    void start(std::shared_ptr<const DocumentContent> content,
               std::shared_ptr<const core::CompiledQuery> query,
               std::shared_ptr<const core::SortedU64Sequence> restrict,
               std::optional<std::uint64_t> from, FindDirection direction);
    void cancel();

    [[nodiscard]] bool isRunning() const noexcept { return m_running; }

signals:
    void finished(const lv::model::FindResult& result);

private:
    std::jthread m_worker;
    std::uint64_t m_ticket = 0;
    bool m_running = false;
};

}  // namespace lv::model
