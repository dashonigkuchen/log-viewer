#include "ui/document_tab.h"

#include "core/encoding/encoding.h"
#include "model/bookmarks.h"
#include "model/document.h"
#include "model/document_line_source.h"
#include "model/filtered_document.h"
#include "model/filtered_line_source.h"
#include "model/line_highlighter.h"
#include "model/mark_set.h"
#include "model/match_highlighter.h"
#include "model/search_pool.h"
#include "ui/filter_bar.h"
#include "ui/find_bar.h"
#include "ui/log_view.h"

#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QSplitter>
#include <QTime>
#include <QToolButton>
#include <QVBoxLayout>

#include <system_error>

namespace lv::ui
{

std::filesystem::path normalizedPath(const std::filesystem::path& path)
{
    std::error_code error;
    auto canonical = std::filesystem::weakly_canonical(path, error);
    if (error) {
        canonical = std::filesystem::absolute(path, error).lexically_normal();
    }
    return canonical;
}

DocumentTab::DocumentTab(const std::filesystem::path& path, QWidget* parent)
    : QWidget(parent)
    , m_path(normalizedPath(path))
    , m_document(std::make_unique<model::Document>(m_path))
    , m_source(std::make_unique<model::DocumentLineSource>(*m_document))
    , m_filter(std::make_unique<model::FilteredDocument>(*m_document, model::searchThreadPool()))
    , m_filteredSource(std::make_unique<model::FilteredLineSource>(*m_filter, *m_source))
    , m_highlighter(std::make_unique<model::MatchHighlighter>(*m_filter))
    , m_marks(std::make_unique<model::MarkSet>(*m_document))
    , m_bookmarks(std::make_unique<model::Bookmarks>(*m_document))
    , m_lineHighlighter(std::make_unique<model::LineHighlighter>())
    , m_splitter(new QSplitter(Qt::Vertical, this))
    , m_view(new LogView(m_splitter))
{
    auto* pane = new QWidget(m_splitter);
    pane->setObjectName(QStringLiteral("filterPane"));
    m_filterBar = new FilterBar(*m_filter, pane);
    m_filteredView = new LogView(pane);
    auto* paneLayout = new QVBoxLayout(pane);
    paneLayout->setContentsMargins(0, 0, 0, 0);
    paneLayout->setSpacing(0);
    paneLayout->addWidget(m_filterBar);
    paneLayout->addWidget(m_filteredView, 1);
    m_view->setObjectName(QStringLiteral("mainView"));
    m_view->setLineSource(m_source.get());
    m_filteredView->setObjectName(QStringLiteral("filteredView"));
    m_filteredView->setLineSource(m_filteredSource.get());
    m_view->setHighlighter(m_highlighter.get());
    m_filteredView->setHighlighter(m_highlighter.get());
    m_view->setMarks(m_marks.get());
    m_filteredView->setMarks(m_marks.get());
    m_filteredSource->setMarks(m_marks.get());
    m_view->setBookmarks(m_bookmarks.get());
    m_filteredView->setBookmarks(m_bookmarks.get());
    m_lineHighlighter->watch(*m_document);
    m_view->setLineHighlighter(m_lineHighlighter.get());
    m_filteredView->setLineHighlighter(m_lineHighlighter.get());
    for (LogView* view : {m_view, m_filteredView}) {
        connect(view, &LogView::highlightTextRequested, this, &DocumentTab::highlightTextRequested);
    }
    m_splitter->setObjectName(QStringLiteral("filterSplitter"));
    m_splitter->setChildrenCollapsible(false);
    m_splitter->addWidget(m_view);
    m_splitter->addWidget(pane);
    m_splitter->setStretchFactor(0, 3);
    m_splitter->setStretchFactor(1, 1);
    // NOLINTNEXTLINE(cppcoreguidelines-prefer-member-initializer): needs the views created above
    m_findBar = new FindBar(*m_document, *m_filter, *m_view, *m_filteredView, this);
    // NOLINTNEXTLINE(cppcoreguidelines-prefer-member-initializer): built with the layout below
    m_noticeBar = new QWidget(this);
    m_noticeBar->setObjectName(QStringLiteral("noticeBar"));
    m_noticeBar->setAutoFillBackground(true);
    m_noticeBar->setStyleSheet(
        QStringLiteral("#noticeBar { background: #fff4ce; color: #3b2f00; }"));
    // NOLINTNEXTLINE(cppcoreguidelines-prefer-member-initializer): child of the notice bar
    m_noticeLabel = new QLabel(m_noticeBar);
    m_noticeLabel->setObjectName(QStringLiteral("noticeText"));
    m_noticeLabel->setWordWrap(true);
    auto* closeNotice = new QToolButton(m_noticeBar);
    closeNotice->setObjectName(QStringLiteral("noticeClose"));
    closeNotice->setText(QStringLiteral("\u2715"));
    closeNotice->setAutoRaise(true);
    closeNotice->setToolTip(QStringLiteral("Dismiss"));
    connect(closeNotice, &QToolButton::clicked, this, &DocumentTab::hideNotice);
    auto* noticeLayout = new QHBoxLayout(m_noticeBar);
    noticeLayout->setContentsMargins(8, 4, 4, 4);
    noticeLayout->addWidget(m_noticeLabel, 1);
    noticeLayout->addWidget(closeNotice);
    m_noticeBar->hide();
    connect(m_document.get(), &model::Document::notice, this, &DocumentTab::showNotice);
    // Marks refer to line numbers of the old generation: they are dropped (F-TAIL-5).
    const auto appendNotice = [this](const QString& lost) {
        const QString previous = noticeText().section(QStringLiteral("  "), 1);
        showNotice(previous.isEmpty() ? lost : previous + QStringLiteral(" ") + lost);
    };
    connect(m_marks.get(), &model::MarkSet::cleared, this, [appendNotice](quint64 count) {
        appendNotice(QStringLiteral("%1 mark(s) were cleared.").arg(count));
    });
    connect(m_bookmarks.get(), &model::Bookmarks::cleared, this, [appendNotice](quint64 count) {
        appendNotice(QStringLiteral("%1 bookmark(s) were cleared.").arg(count));
    });
    connect(m_document.get(), &model::Document::missingChanged, this, &DocumentTab::statusChanged);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_noticeBar);
    layout->addWidget(m_findBar);
    layout->addWidget(m_splitter);
    setFocusProxy(m_view);
    connect(m_splitter, &QSplitter::splitterMoved, this, &DocumentTab::splitterMoved);
    connect(m_filteredView, &LogView::currentRowChanged, this, &DocumentTab::syncFilteredToMain);
    connect(m_view, &LogView::currentRowChanged, this, &DocumentTab::syncMainToFiltered);
    connect(m_view, &LogView::followRequested, this, &DocumentTab::setFollowing);
    connect(m_filteredView, &LogView::followRequested, this, &DocumentTab::setFollowing);
    connect(m_document.get(), &model::Document::linesAppended, this, [this] {
        if (m_document->state() == model::Document::State::kReady) {
            emit contentChanged();
        }
    });
    connect(m_document.get(), &model::Document::reset, this, &DocumentTab::contentChanged);

    connect(m_document.get(), &model::Document::stateChanged, this, &DocumentTab::statusChanged);
    connect(m_document.get(), &model::Document::linesAppended, this, &DocumentTab::statusChanged);
    connect(m_document.get(), &model::Document::indexingProgress, this,
            [this](quint64 indexed, quint64 total) {
                m_indexedBytes = indexed;
                m_totalBytes = total;
                emit statusChanged();
            });
    connect(m_view, &LogView::currentRowChanged, this, &DocumentTab::statusChanged);
    connect(m_document.get(), &model::Document::stateChanged, this,
            [this](model::Document::State state) {
                if (state == model::Document::State::kReady) {
                    restorePosition();
                }
            });
    m_document->open();
}

void DocumentTab::openFindBar()
{
    LogView* target = m_view;
    if (m_filteredView->hasFocus() || m_filteredView->isAncestorOf(focusWidget())) {
        target = m_filteredView;
    } else if (m_findBar->isVisible() &&
               (m_findBar->isAncestorOf(focusWidget()) || m_findBar->hasFocus())) {
        target = m_findBar->target();  // already open: keep its view
    }
    m_findBar->open(target);
}

QString DocumentTab::noticeText() const
{
    return m_noticeBar->isHidden() ? QString() : m_noticeLabel->text();
}

void DocumentTab::showNotice(const QString& message)
{
    m_noticeLabel->setText(QStringLiteral("%1  %2").arg(
        QTime::currentTime().toString(QStringLiteral("HH:mm:ss")), message));
    m_noticeBar->show();
}

void DocumentTab::hideNotice()
{
    m_noticeBar->hide();
}

LogView* DocumentTab::activeView() const
{
    return m_filteredView->hasFocus() ? m_filteredView : m_view;
}

void DocumentTab::goToLine(core::LineNumber line)
{
    m_view->goToRow(line);
    m_view->setFocus();
}

void DocumentTab::setShowMarksInFilter(bool show)
{
    m_filteredSource->setShowMarks(show);
}

bool DocumentTab::showMarksInFilter() const
{
    return m_filteredSource->showMarks();
}

void DocumentTab::setFollowing(bool following)
{
    if (following == m_following) {
        return;
    }
    m_following = following;
    m_view->setFollowing(following);
    m_filteredView->setFollowing(following);
    emit followingChanged(following);
    emit statusChanged();
}

void DocumentTab::syncFilteredToMain(core::LineNumber row)
{
    // While following, each view moves to its own end; syncing would pull the other one away.
    if (m_syncing || m_filteredView->isAutoScrolling() ||
        row.get() >= m_filteredSource->lineCount().get()) {
        return;
    }
    m_syncing = true;
    m_view->goToRow(m_filteredSource->originalLineNumber(row));
    m_syncing = false;
}

void DocumentTab::syncMainToFiltered(core::LineNumber line)
{
    if (m_syncing || m_view->isAutoScrolling()) {
        return;
    }
    // Nearest filtered row at or before `line`: the one before the first row after it.
    const std::uint64_t after = m_filteredSource->rowsBefore(core::LineNumber(line.get() + 1));
    if (after == 0) {
        return;  // no match at or before the line
    }
    const core::LineNumber row(after - 1);
    m_syncing = true;
    m_filteredView->setCurrentRow(row);
    m_filteredView->ensureRowVisible(row);
    m_syncing = false;
}

void DocumentTab::rememberPosition()
{
    if (m_restoreTop) {
        return;  // a reload is already pending; keep the original position
    }
    m_restoreTop = m_view->topRow();
    m_restoreCurrent = m_view->currentRow();
}

void DocumentTab::restorePosition()
{
    if (!m_restoreTop) {
        return;
    }
    // Rows that no longer exist are clamped by the view.
    if (m_restoreCurrent) {
        m_view->setCurrentRow(*m_restoreCurrent);
    }
    m_view->scrollToRow(*m_restoreTop);
    m_restoreTop.reset();
    m_restoreCurrent.reset();
}

void DocumentTab::reload()
{
    rememberPosition();
    m_document->reload();
}

void DocumentTab::setEncodingOverride(std::optional<core::Encoding> encoding)
{
    if (encoding == m_document->encodingOverride()) {
        return;
    }
    rememberPosition();
    m_document->setEncodingOverride(encoding);
}

DocumentTab::~DocumentTab()
{
    delete m_findBar;  // detaches its highlighter from the views first
    m_findBar = nullptr;
    // The view refers to the source, the source to the document: detach before destroying them.
    m_view->setMarks(nullptr);
    m_filteredView->setMarks(nullptr);
    m_filteredSource->setMarks(nullptr);
    m_view->setBookmarks(nullptr);
    m_filteredView->setBookmarks(nullptr);
    m_view->setLineHighlighter(nullptr);
    m_filteredView->setLineHighlighter(nullptr);
    m_view->setLineSource(nullptr);
}

QString DocumentTab::title() const
{
    return QString::fromStdString(m_path.filename().string());
}

QString DocumentTab::toolTip() const
{
    return QDir::toNativeSeparators(QString::fromStdString(m_path.string()));
}

QString DocumentTab::statusText() const
{
    using State = model::Document::State;
    const QLocale locale;
    switch (m_document->state()) {
    case State::kClosed:
        return {};
    case State::kError:
        return QStringLiteral("Error: %1").arg(m_document->errorMessage());
    case State::kIndexing:
    case State::kReady:
        break;
    }
    QStringList parts;
    const auto content = m_document->content();
    if (content) {
        const auto size = content->file.size();
        if (size) {
            parts << locale.formattedDataSize(static_cast<qint64>(*size));
        }
        parts << QString::fromUtf8(core::displayName(content->index.encoding()).data());
    }
    parts << QStringLiteral("%1 lines")
                 .arg(locale.toString(static_cast<qulonglong>(m_document->lineCount().get())));
    if (const auto row = m_view->currentRow()) {
        parts << QStringLiteral("Line %1").arg(
            locale.toString(static_cast<qulonglong>(row->get()) + 1));
    }
    if (m_document->state() == State::kIndexing && m_totalBytes > 0) {
        parts << QStringLiteral("Indexing %1%").arg(m_indexedBytes * 100 / m_totalBytes);
    }
    if (m_following) {
        parts << QStringLiteral("Following");
    }
    if (m_document->isMissing()) {
        parts << QStringLiteral("File deleted");
    }
    return parts.join(QStringLiteral("  |  "));
}

}  // namespace lv::ui
