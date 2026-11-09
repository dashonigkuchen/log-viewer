#include "log_writer.h"
#include "model/annotation_store.h"
#include "model/bookmarks.h"
#include "model/document.h"
#include "model/mark_set.h"
#include "temp_dir.h"
#include "ui/document_tab.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QTest>

#include <memory>

using lv::core::LineNumber;
using lv::model::AnnotationStore;
using lv::model::Document;
using lv::ui::DocumentTab;

namespace
{

template <typename Condition>
bool waitFor(Condition condition, int timeoutMs = 10000)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition()) {
        if (timer.elapsed() > timeoutMs) {
            return false;
        }
        QApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    QApplication::processEvents();
    return true;
}

std::unique_ptr<DocumentTab> openTab(const std::filesystem::path& path, AnnotationStore& store)
{
    auto tab = std::make_unique<DocumentTab>(path);
    tab->setAnnotationStore(&store);
    if (!waitFor([&] { return tab->document().state() == Document::State::kReady; })) {
        return nullptr;
    }
    return tab;
}

}  // namespace

class AnnotationsTest : public QObject
{
    Q_OBJECT

private slots:

    static void reopeningRestoresMarksAndBookmarks()
    {
        const lv::test::TempDir dir;
        {
            lv::test::LogWriter writer(dir / "a.log", true);
            writer.append("INFO a\nERROR b\nINFO c\nINFO d\n");
        }
        AnnotationStore store(QString::fromStdU16String((dir / "annotations.json").u16string()));
        {
            auto tab = openTab(dir / "a.log", store);
            QVERIFY(tab != nullptr);
            tab->marks().toggle(LineNumber(1));
            const LineNumber three(3);
            tab->marks().toggleColor(std::span(&three, 1), 4);
            tab->bookmarks().add(LineNumber(2), QStringLiteral("here"));
        }  // closing the tab saves
        auto tab = openTab(dir / "a.log", store);
        QVERIFY(tab != nullptr);
        QCOMPARE(tab->marks().size(), std::size_t{2});
        QCOMPARE(tab->marks().colorOf(LineNumber(3)).value_or(-1), 4);
        QCOMPARE(tab->bookmarks().name(LineNumber(2)).value_or(QString()), QStringLiteral("here"));
        QVERIFY(tab->noticeText().isEmpty());
    }

    static void changedFileDiscardsWithANotice()
    {
        const lv::test::TempDir dir;
        {
            lv::test::LogWriter writer(dir / "a.log", true);
            writer.append("one\ntwo\n");
        }
        AnnotationStore store(QString::fromStdU16String((dir / "annotations.json").u16string()));
        {
            auto tab = openTab(dir / "a.log", store);
            QVERIFY(tab != nullptr);
            tab->marks().toggle(LineNumber(0));
            tab->saveAnnotations();
        }
        {
            lv::test::LogWriter writer(dir / "a.log", true);
            writer.append("different\ncontent\n");
        }
        auto tab = openTab(dir / "a.log", store);
        QVERIFY(tab != nullptr);
        QVERIFY(tab->marks().empty());
        QVERIFY(tab->noticeText().contains(QStringLiteral("discarded")));
    }
};

QTEST_MAIN(AnnotationsTest)
#include "annotations_test.moc"
