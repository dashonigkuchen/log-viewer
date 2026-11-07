#include "ui/document_tab.h"

#include "core/encoding/encoding.h"
#include "model/document.h"
#include "model/document_line_source.h"
#include "model/filtered_document.h"
#include "model/filtered_line_source.h"
#include "model/search_pool.h"
#include "ui/log_view.h"

#include <QDir>
#include <QLocale>
#include <QSplitter>
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
    , m_splitter(new QSplitter(Qt::Vertical, this))
    , m_view(new LogView(m_splitter))
    , m_filteredView(new LogView(m_splitter))
{
    m_view->setObjectName(QStringLiteral("mainView"));
    m_view->setLineSource(m_source.get());
    m_filteredView->setObjectName(QStringLiteral("filteredView"));
    m_filteredView->setLineSource(m_filteredSource.get());
    m_splitter->setObjectName(QStringLiteral("filterSplitter"));
    m_splitter->setChildrenCollapsible(false);
    m_splitter->addWidget(m_view);
    m_splitter->addWidget(m_filteredView);
    m_splitter->setStretchFactor(0, 3);
    m_splitter->setStretchFactor(1, 1);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_splitter);
    setFocusProxy(m_view);
    connect(m_splitter, &QSplitter::splitterMoved, this, &DocumentTab::splitterMoved);
    connect(m_filteredView, &LogView::currentRowChanged, this, &DocumentTab::syncFilteredToMain);
    connect(m_view, &LogView::currentRowChanged, this, &DocumentTab::syncMainToFiltered);

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

void DocumentTab::syncFilteredToMain(core::LineNumber row)
{
    if (m_syncing || row.get() >= m_filteredSource->lineCount().get()) {
        return;
    }
    m_syncing = true;
    m_view->goToRow(m_filteredSource->originalLineNumber(row));
    m_syncing = false;
}

void DocumentTab::syncMainToFiltered(core::LineNumber line)
{
    if (m_syncing) {
        return;
    }
    // Nearest match at or before `line`: the one before the first match after it.
    const std::uint64_t after = m_filter->lowerBound(core::LineNumber(line.get() + 1));
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
    // The view refers to the source, the source to the document: detach before destroying them.
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
    return parts.join(QStringLiteral("  |  "));
}

}  // namespace lv::ui
