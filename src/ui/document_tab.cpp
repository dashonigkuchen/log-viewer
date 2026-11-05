#include "ui/document_tab.h"

#include "core/encoding/encoding.h"
#include "model/document.h"
#include "model/document_line_source.h"
#include "ui/log_view.h"

#include <QDir>
#include <QLocale>
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
    , m_view(new LogView(this))
{
    m_view->setObjectName(QStringLiteral("mainView"));
    m_view->setLineSource(m_source.get());
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_view);
    setFocusProxy(m_view);

    connect(m_document.get(), &model::Document::stateChanged, this, &DocumentTab::statusChanged);
    connect(m_document.get(), &model::Document::linesAppended, this, &DocumentTab::statusChanged);
    connect(m_document.get(), &model::Document::indexingProgress, this,
            [this](quint64 indexed, quint64 total) {
                m_indexedBytes = indexed;
                m_totalBytes = total;
                emit statusChanged();
            });
    connect(m_view, &LogView::currentRowChanged, this, &DocumentTab::statusChanged);
    m_document->open();
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
