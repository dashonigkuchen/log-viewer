#include "log_writer.h"
#include "model/document.h"
#include "temp_dir.h"
#include "ui/document_tab.h"
#include "ui/log_view.h"
#include "ui/main_window.h"
#include "ui/single_instance.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTest>

#include <atomic>
#include <future>
#include <string>

using lv::core::LineNumber;
using lv::ui::OpenRequest;
using lv::ui::SingleInstance;

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

QString keyFor(const lv::test::TempDir& dir)
{
    return QString::fromStdU16String(dir.path().u16string());
}

// Sends from another thread: the server needs this thread's event loop to answer.
bool sendInBackground(const SingleInstance& instance, const OpenRequest& request)
{
    auto result = std::async(
        std::launch::async, [&instance, request] { return instance.sendToRunning(request, 3000); });
    waitFor([&result] {
        return result.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    });
    return result.get();
}

}  // namespace

class SingleInstanceTest : public QObject
{
    Q_OBJECT

private slots:

    static void requestsRoundTrip()
    {
        const OpenRequest request{.files = {QStringLiteral("/a.log"), QStringLiteral("/b c.log")},
                                  .follow = true,
                                  .line = 42};
        QCOMPARE(OpenRequest::fromBytes(request.toBytes()), std::optional(request));
        QVERIFY(!OpenRequest::fromBytes("garbage").has_value());
        QVERIFY(!OpenRequest::fromBytes(R"({"files":["/x"]})").has_value());  // no format tag
    }

    static void secondStartReachesTheFirst()
    {
        const lv::test::TempDir dir;
        SingleInstance first(keyFor(dir));
        const SingleInstance second(keyFor(dir));
        QCOMPARE(first.serverName(), second.serverName());
        QVERIFY(first.serverName().size() < 40);

        // Nobody listens yet.
        QVERIFY(!second.sendToRunning(OpenRequest{}, 200));

        QVERIFY(first.listen());
        const QSignalSpy spy(&first, &SingleInstance::openRequested);
        const OpenRequest request{.files = {QStringLiteral("/x.log")}, .follow = false, .line = 7};
        QVERIFY(sendInBackground(second, request));
        QVERIFY(waitFor([&spy] { return spy.count() == 1; }));
        QCOMPARE(spy.at(0).at(0).value<OpenRequest>(), request);

        // Another settings directory is another group.
        const lv::test::TempDir other;
        QVERIFY(SingleInstance(keyFor(other)).serverName() != first.serverName());
    }

    static void openRequestOpensFollowsAndJumps()
    {
        const lv::test::TempDir dir;
        {
            lv::test::LogWriter writer(dir / "a.log", true);
            std::string text;
            for (int i = 1; i <= 3000; ++i) {
                text += "line " + std::to_string(i) + "\n";
            }
            writer.append(text);
            lv::test::LogWriter(dir / "b.log", true).append("b\n");
        }
        lv::ui::MainWindow window;
        window.resize(800, 500);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        const auto path = [&dir](const char* name) {
            return QString::fromStdU16String((dir / name).u16string());
        };
        window.openRequest({.files = {path("a.log")}, .follow = false, .line = 2500});
        auto* a = window.documentTab(0);
        QVERIFY(a != nullptr);
        QVERIFY(waitFor([a] {
            return a->document().state() == lv::model::Document::State::kReady &&
                   a->mainView()->currentRow() == LineNumber(2499);
        }));
        window.openRequest({.files = {path("b.log")}, .follow = true, .line = std::nullopt});
        QCOMPARE(window.findChild<QTabWidget*>()->count(), 2);
        QVERIFY(window.documentTab(1)->isFollowing());
    }
};

QTEST_MAIN(SingleInstanceTest)
#include "single_instance_test.moc"
