#include "model/document.h"

#include "core/index/indexer.h"
#include "core/util/log.h"
#include "model/file_watcher.h"

#include <QMetaObject>

#include <algorithm>
#include <cstddef>
#include <span>
#include <utility>
#include <vector>

namespace lv::model
{

DocumentContent::DocumentContent(core::File openedFile, core::Encoding encoding,
                                 std::uint64_t dataStart, std::uint64_t contentGeneration)
    : file(std::move(openedFile))
    , index(encoding, dataStart)
    , reader(file, index)
    , generation(contentGeneration)
{}

Document::Document(std::filesystem::path path, QObject* parent)
    : QObject(parent)
    , m_path(std::move(path))
{}

Document::~Document()
{
    stopWorker();
    delete m_watcher;  // its probe refers to m_content
    m_watcher = nullptr;
}

void Document::open()
{
    startWorker();
}

void Document::reload()
{
    startWorker();
}

void Document::setEncodingOverride(std::optional<core::Encoding> encoding)
{
    if (encoding == m_encodingOverride) {
        return;
    }
    m_encodingOverride = encoding;
    if (m_state != State::kClosed) {
        startWorker();
    }
}

void Document::close()
{
    stopWorker();
    m_workerBusy = false;
    delete m_watcher;
    m_watcher = nullptr;
    ++m_generation;  // drops events still queued by the old worker
    m_content.reset();
    setState(State::kClosed);
}

core::LineCount Document::lineCount() const
{
    return m_content ? m_content->index.snapshot().lineCount() : core::LineCount(0);
}

void Document::setPollInterval(int milliseconds)
{
    m_pollInterval = milliseconds;
    if (m_watcher != nullptr) {
        m_watcher->setPollInterval(milliseconds);
    }
}

void Document::setWatchNotificationsEnabled(bool enabled)
{
    m_watchNotifications = enabled;
    if (m_watcher != nullptr) {
        m_watcher->setNotificationsEnabled(enabled);
    }
}

void Document::ensureWatcher()
{
    if (m_watcher != nullptr) {
        return;
    }
    m_watcher = new FileWatcher(m_path, this);
    m_watcher->setPollInterval(m_pollInterval);
    m_watcher->setNotificationsEnabled(m_watchNotifications);
    // The open handle's size: NTFS updates the directory entry lazily while a writer is active.
    m_watcher->setHandleSizeProbe([this]() -> std::optional<std::uint64_t> {
        if (!m_content) {
            return std::nullopt;
        }
        const auto size = m_content->file.size();
        return size ? std::optional(*size) : std::nullopt;
    });
    connect(m_watcher, &FileWatcher::changed, this, &Document::scheduleTailCheck);
}

void Document::scheduleTailCheck()
{
    if (m_state != State::kReady || !m_content) {
        return;  // initial indexing reads up to the size at its start; checked when ready
    }
    if (m_workerBusy) {
        m_recheck = true;
        return;
    }
    stopWorker();  // the previous worker has finished; join it
    m_workerBusy = true;
    m_recheck = false;
    m_worker =
        std::jthread([this, generation = m_generation, content = m_content](
                         const std::stop_token& stop) { runTail(stop, generation, content); });
}

void Document::workerDone()
{
    m_workerBusy = false;
    if (std::exchange(m_recheck, false)) {
        scheduleTailCheck();
    }
}

void Document::stopWorker()
{
    if (m_worker.joinable()) {
        m_worker.request_stop();
        m_worker.join();
    }
}

void Document::startWorker()
{
    stopWorker();
    const std::uint64_t generation = ++m_generation;
    m_content.reset();
    m_errorMessage.clear();
    m_workerBusy = true;
    m_recheck = false;
    setState(State::kIndexing);
    emit reset(generation);
    m_worker =
        std::jthread([this, generation, encoding = m_encodingOverride](
                         const std::stop_token& stop) { runWorker(stop, generation, encoding); });
}

void Document::post(std::uint64_t generation, std::function<void()> function)
{
    bool schedule = false;
    {
        const std::scoped_lock lock(m_eventMutex);
        m_events.push_back({.generation = generation, .function = std::move(function)});
        schedule = !std::exchange(m_drainScheduled, true);
    }
    if (schedule) {
        // Pending wake-ups die with this object; the destructor joins the worker first.
        QMetaObject::invokeMethod(this, "drainEvents", Qt::QueuedConnection);
    }
}

void Document::drainEvents()
{
    std::vector<PendingEvent> events;
    {
        const std::scoped_lock lock(m_eventMutex);
        events.swap(m_events);
        m_drainScheduled = false;
    }
    for (auto& event : events) {
        if (event.generation == m_generation) {
            event.function();
        }
    }
}

void Document::setState(State state)
{
    if (state != m_state) {
        m_state = state;
        emit stateChanged(state);
    }
}

namespace
{

struct EncodingChoice
{
    core::Encoding encoding = core::Encoding::kUtf8;
    std::uint64_t dataStart = 0;
};

core::Result<EncodingChoice> chooseEncoding(const core::File& file,
                                            std::optional<core::Encoding> encodingOverride)
{
    const auto size = file.size();
    if (!size) {
        return tl::unexpected(size.error());
    }
    std::vector<std::byte> sample(
        static_cast<std::size_t>(std::min<std::uint64_t>(*size, core::kDetectionSampleSize)));
    const auto read = file.readAt(core::ByteOffset(0), sample);
    if (!read) {
        return tl::unexpected(read.error());
    }
    sample.resize(*read);
    const auto detected = core::detectEncoding(sample, sample.size() == *size);
    if (!encodingOverride) {
        return EncodingChoice{.encoding = detected.encoding, .dataStart = detected.bomLength};
    }
    // With an override, skip a byte order mark only if it belongs to the chosen encoding.
    const auto bom = core::traits(*encodingOverride).byteOrderMark;
    const bool hasBom = !bom.empty() && sample.size() >= bom.size() &&
                        std::ranges::equal(bom, std::span(sample).first(bom.size()));
    return EncodingChoice{.encoding = *encodingOverride, .dataStart = hasBom ? bom.size() : 0};
}

QString describe(const core::Error& error)
{
    return QString::fromStdString(error.message);
}

}  // namespace

void Document::runWorker(const std::stop_token& stop, std::uint64_t generation,
                         std::optional<core::Encoding> encodingOverride)
{
    auto fail = [this, generation](const core::Error& error) {
        core::log(core::LogLevel::kWarning,
                  "cannot open '" + m_path.string() + "': " + error.toString());
        post(generation, [this, message = describe(error)] {
            m_content.reset();
            m_errorMessage = message;
            m_workerBusy = false;
            setState(State::kError);
        });
    };

    auto file = core::File::open(m_path);
    if (!file) {
        fail(file.error());
        return;
    }
    const auto choice = chooseEncoding(*file, encodingOverride);
    if (!choice) {
        fail(choice.error());
        return;
    }
    auto content = std::make_shared<DocumentContent>(std::move(*file), choice->encoding,
                                                     choice->dataStart, generation);
    post(generation, [this, content] { m_content = content; });

    std::uint64_t reportedLines = 0;
    const auto result = core::Indexer::run(
        content->file, content->index, stop, [&](const core::IndexProgress& progress) {
            const std::uint64_t lines = progress.lines.get();
            const bool grew = lines != reportedLines;
            reportedLines = lines;
            post(generation, [this, progress, lines, grew] {
                emit indexingProgress(progress.indexedBytes, progress.totalBytes);
                if (grew) {
                    emit linesAppended(lines);
                }
            });
        });
    if (!result) {
        if (result.error().code != core::ErrorCode::kCanceled) {
            fail(result.error());
        }
        return;
    }
    const auto watched =
        core::ChangeDetector::snapshotAt(content->file, content->index.snapshot().indexedEnd.get());
    if (!watched) {
        fail(watched.error());
        return;
    }
    content->watched = *watched;
    post(generation, [this] {
        setState(State::kReady);
        ensureWatcher();
        m_workerBusy = false;
        m_recheck = false;
        scheduleTailCheck();  // the file may have grown while it was indexed
    });
}

void Document::runTail(const std::stop_token& stop, std::uint64_t generation,
                       const std::shared_ptr<DocumentContent>& content)
{
    const auto done = [this, generation] {
        post(generation, [this] { workerDone(); });
    };
    const auto reloadLater = [this, generation](const char* why) {
        core::log(core::LogLevel::kInfo, "'" + m_path.string() + "' " + why + "; reloading");
        post(generation, [this] { startWorker(); });
    };
    const auto change = core::ChangeDetector::check(content->file, m_path, content->watched);
    if (!change) {
        done();  // retried on the next change or poll
        return;
    }
    switch (*change) {
    case core::Change::kUnchanged:
    case core::Change::kMissing:  // content stays; deletion handling follows in M4-06
        done();
        return;
    case core::Change::kTruncated:
        reloadLater("was truncated");
        return;
    case core::Change::kReplaced:
        reloadLater("was replaced");
        return;
    case core::Change::kGrown:
        break;
    }
    const auto before = content->index.snapshot();
    const auto result = core::Indexer::run(
        content->file, content->index, stop, [&](const core::IndexProgress& progress) {
            const std::uint64_t lines = progress.lines.get();
            post(generation, [this, lines] { emit linesAppended(lines); });
        });
    if (!result) {
        if (result.error().code == core::ErrorCode::kOutOfRange) {
            reloadLater("shrank while indexing");
        } else if (result.error().code != core::ErrorCode::kCanceled) {
            done();
        }
        return;
    }
    const auto after = content->index.snapshot();
    if (auto watched = core::ChangeDetector::snapshotAt(content->file, after.indexedEnd.get())) {
        content->watched = *watched;
    }
    post(generation, [this, lines = after.lineCount().get(), grew = after != before] {
        ++m_tailRuns;
        if (grew) {
            emit linesAppended(lines);  // also when only the partial last line grew
        }
        workerDone();
    });
}

}  // namespace lv::model
