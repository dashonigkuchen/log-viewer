#include "loggen/loggen.h"
#include "model/document.h"
#include "model/document_line_source.h"
#include "model/text_copy.h"
#include "temp_dir.h"
#include "ui/log_view.h"

#include <QApplication>
#include <QClipboard>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

#include <cstdint>
#include <memory>
#include <stop_token>

namespace
{

// A document with a generated file, indexed and shown in a view.
struct Fixture
{
    explicit Fixture(std::uint64_t sizeBytes)
    {
        lv::loggen::WriteOptions write;
        write.sizeBytes = sizeBytes;
        write.out = dir / "gen.log";
        QVERIFY(lv::loggen::writeLog(write).ok());
        path = write.out;
        document = std::make_unique<lv::model::Document>(path);
        source = std::make_unique<lv::model::DocumentLineSource>(*document);
        view.setLineSource(source.get());
        document->open();
        QElapsedTimer timer;
        timer.start();
        while (document->state() != lv::model::Document::State::kReady && timer.elapsed() < 20000) {
            QApplication::processEvents(QEventLoop::AllEvents, 10);
        }
        QCOMPARE(document->state(), lv::model::Document::State::kReady);
    }

    [[nodiscard]] QString fileText() const
    {
        QFile file(QString::fromStdString(path.string()));
        if (!file.open(QIODevice::ReadOnly)) {
            return {};
        }
        return QString::fromUtf8(file.readAll());
    }

    lv::test::TempDir dir;
    std::filesystem::path path;
    std::unique_ptr<lv::model::Document> document;
    std::unique_ptr<lv::model::DocumentLineSource> source;
    lv::ui::LogView view;
};

}  // namespace

class LogViewCopyTest : public QObject
{
    Q_OBJECT

private slots:

    static void estimateMatchesTheCopiedSize()
    {
        const Fixture f(std::uint64_t{8} * 1024 * 1024);
        const auto optionalReader = f.source->rowReader();
        QVERIFY(optionalReader.has_value());
        const lv::model::RowReader reader = optionalReader.value_or(lv::model::RowReader{});
        const std::uint64_t rows = f.document->lineCount().get();
        lv::model::CopyRange all;
        all.lastRow = rows - 1;
        const std::uint64_t estimate = lv::model::estimateCopyBytes(reader, all);
        const auto text = lv::model::extractText(reader, all, std::stop_token{});
        QVERIFY(text.has_value());
        const auto actual = static_cast<std::uint64_t>(text.value_or(QString()).toUtf8().size());
        QVERIFY(actual > 0);
        const double error = std::abs(static_cast<double>(estimate) - static_cast<double>(actual)) /
                             static_cast<double>(actual);
        QVERIFY2(error < 0.01,
                 qPrintable(QStringLiteral("estimate %1, actual %2").arg(estimate).arg(actual)));

        // The non-contiguous path gives the same estimate.
        auto mapped = reader;
        mapped.contiguous = false;
        QCOMPARE(lv::model::estimateCopyBytes(mapped, all), estimate);
    }

    static void largeCopiesAskForConfirmation()
    {
        Fixture f(std::uint64_t{2} * 1024 * 1024);
        f.view.setCopyLimits({.backgroundBytes = 1024, .confirmBytes = 4096});
        int asked = 0;
        f.view.setCopyConfirmation([&](std::uint64_t bytes) {
            ++asked;
            return bytes < 4096;  // decline
        });
        QApplication::clipboard()->setText(QStringLiteral("unchanged"));
        QSignalSpy finished(&f.view, &lv::ui::LogView::copyFinished);
        f.view.selectAll();
        f.view.copy();
        QCOMPARE(asked, 1);
        QCOMPARE(finished.count(), 1);
        QCOMPARE(finished.first().at(0).toBool(), false);
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("unchanged"));

        // A small selection is copied without asking.
        f.view.setSelection(lv::ui::TextSelection::lines(0, 0));
        f.view.copy();
        QCOMPARE(asked, 1);
        QVERIFY(QApplication::clipboard()->text().endsWith(u'\n'));
    }

    static void backgroundCopyKeepsTheEventLoopRunning()
    {
        Fixture f(std::uint64_t{200} * 1024 * 1024);
        f.view.setCopyConfirmation([](std::uint64_t) { return true; });
        QSignalSpy finished(&f.view, &lv::ui::LogView::copyFinished);
        int ticks = 0;
        QTimer ticker;
        QObject::connect(&ticker, &QTimer::timeout, [&] { ++ticks; });
        ticker.start(10);

        f.view.selectAll();
        f.view.copy();
        QVERIFY(f.view.isCopying());
        QVERIFY(finished.wait(120000));
        QCOMPARE(finished.first().at(0).toBool(), true);
        QVERIFY2(ticks > 0, "the event loop was blocked during the copy");
        QVERIFY(!f.view.isCopying());

        QString expected = f.fileText();
        expected.remove(u'\r');
        QCOMPARE(QApplication::clipboard()->text().size(), expected.size());
    }

    static void cancelLeavesTheClipboardUnchanged()
    {
        Fixture f(std::uint64_t{100} * 1024 * 1024);
        f.view.setCopyLimits({.backgroundBytes = 1024, .confirmBytes = UINT64_MAX});
        QApplication::clipboard()->setText(QStringLiteral("unchanged"));
        QSignalSpy finished(&f.view, &lv::ui::LogView::copyFinished);
        f.view.selectAll();
        f.view.copy();
        QVERIFY(f.view.isCopying());
        f.view.cancelCopy();
        QVERIFY(!f.view.isCopying());
        QCOMPARE(finished.count(), 1);
        QCOMPARE(finished.first().at(0).toBool(), false);
        QTest::qWait(100);  // no late result arrives
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("unchanged"));
    }

    static void destroyingTheViewDuringACopyIsSafe()
    {
        auto f = std::make_unique<Fixture>(std::uint64_t{50} * 1024 * 1024);
        f->view.setCopyLimits({.backgroundBytes = 1024, .confirmBytes = UINT64_MAX});
        f->view.selectAll();
        f->view.copy();
        QVERIFY(f->view.isCopying());
        f.reset();
        QApplication::processEvents();
    }
};

QTEST_MAIN(LogViewCopyTest)
#include "log_view_copy_test.moc"
