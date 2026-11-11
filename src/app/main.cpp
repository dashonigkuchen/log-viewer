#include "core/util/log.h"
#include "model/annotation_store.h"
#include "model/app_paths.h"
#include "model/crash_handler.h"
#include "model/file_log_sink.h"
#include "model/filter_history.h"
#include "model/highlight_sets.h"
#include "model/model_info.h"
#include "model/recent_files.h"
#include "model/search_pool.h"
#include "model/session.h"
#include "model/settings.h"
#include "ui/main_window.h"
#include "ui/single_instance.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QStringList>

#include <cstdio>
#include <memory>

namespace
{

void installFileLog()
{
    const QString directory = lv::model::settingsDirectory();
    if (!QDir().mkpath(directory)) {
        return;  // Logging is best effort; the application works without it.
    }
    auto sink =
        std::make_shared<lv::model::FileLogSink>(directory + QStringLiteral("/logviewer.log"));
    lv::core::setLogSink([sink](lv::core::LogLevel level, std::string_view message) {
        sink->write(level, message);
    });
    lv::model::installQtMessageHandler();
}

}  // namespace

int main(int argc, char* argv[])
{
    const QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("LogViewer"));
    QApplication::setApplicationName(QStringLiteral("LogViewer"));
    QApplication::setApplicationDisplayName(QStringLiteral("Log Viewer"));
    QApplication::setApplicationVersion(lv::model::versionString());

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QApplication::translate("main", "A fast native viewer for large log files."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("files"),
                                 QApplication::translate("main", "Log files to open."),
                                 QStringLiteral("[files...]"));
    const QCommandLineOption followOption(
        QStringLiteral("follow"),
        QApplication::translate("main", "Open the files in follow mode."));
    const QCommandLineOption lineOption(
        QStringLiteral("line"), QApplication::translate("main", "Go to line N of the first file."),
        QStringLiteral("N"));
    const QCommandLineOption newInstanceOption(
        QStringLiteral("new-instance"),
        QApplication::translate("main", "Start a new window instead of using a running one."));
    // Hidden: crashes on purpose so CI can check that a dump is written (F-ROB-5).
    QCommandLineOption crashTestOption(QStringLiteral("crash-test"));
    crashTestOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOptions({followOption, lineOption, newInstanceOption, crashTestOption});
    parser.process(app);  // Handles --help and --version and exits.

    // F-CLI-2: absolute paths, so a running instance with another working directory finds them.
    lv::ui::OpenRequest request;
    for (const QString& file : parser.positionalArguments()) {
        request.files << QDir::cleanPath(QFileInfo(file).absoluteFilePath());
    }
    request.follow = parser.isSet(followOption);
    if (parser.isSet(lineOption)) {
        bool ok = false;
        const qulonglong line = parser.value(lineOption).toULongLong(&ok);
        if (!ok || line == 0) {
            (void)std::fputs("logviewer: --line expects a number >= 1\n", stderr);
            return 1;
        }
        request.line = line;
    }

    installFileLog();
    // F-ROB-5: local minidumps in the settings directory (Windows only).
    lv::model::installCrashHandler(lv::model::settingsDirectory() + QStringLiteral("/dumps"));
    if (parser.isSet(crashTestOption)) {
        lv::core::log(lv::core::LogLevel::kWarning, "--crash-test: crashing on purpose");
        lv::model::crashForTest();
    }
    lv::core::log(
        lv::core::LogLevel::kInfo,
        QStringLiteral("Log Viewer %1 starting").arg(lv::model::versionString()).toStdString());
    lv::model::Settings settings(lv::model::settingsDirectory());

    // F-FILE-11: hand the files to a running instance and exit.
    lv::ui::SingleInstance instance(lv::model::settingsDirectory());
    const bool single = settings.singleInstance() && !parser.isSet(newInstanceOption);
    if (single && instance.sendToRunning(request)) {
        lv::core::log(lv::core::LogLevel::kInfo, "Files handed to the running instance");
        lv::core::setLogSink({});
        return 0;
    }
    lv::model::setSearchThreadCount(settings.searchThreads());  // before the pool is used
    lv::model::RecentFiles recent;
    recent.load(settings.store());
    QObject::connect(&recent, &lv::model::RecentFiles::changed,
                     [&recent, &settings] { recent.save(settings.store()); });

    lv::model::FilterHistory history(nullptr, settings.filterHistorySize());
    history.load(settings.store());
    QObject::connect(&history, &lv::model::FilterHistory::changed,
                     [&history, &settings] { history.save(settings.store()); });

    lv::model::HighlightSets highlightSets;
    highlightSets.load(settings.store());
    const auto saveHighlightSets = [&highlightSets, &settings] {
        highlightSets.save(settings.store());
    };
    QObject::connect(&highlightSets, &lv::model::HighlightSets::setChanged, saveHighlightSets);
    QObject::connect(&highlightSets, &lv::model::HighlightSets::setsChanged, saveHighlightSets);
    QObject::connect(&highlightSets, &lv::model::HighlightSets::defaultChanged, saveHighlightSets);

    lv::model::AnnotationStore annotations(lv::model::settingsDirectory() +
                                           QStringLiteral("/annotations.json"));

    lv::ui::MainWindow window;
    window.setAnnotationStore(&annotations);
    window.setFilterHistory(&history);
    window.setHighlightSets(&highlightSets);
    window.setSettings(&settings);
    window.setRecentFiles(&recent);
    // F-FILE-10 / F-TAB-5: window layout always, tabs only if enabled and no files were given.
    window.restoreSession(lv::model::Session::load(settings.store()),
                          settings.restoreSession() && request.files.isEmpty());
    window.show();
    window.openRequest(request);  // F-FILE-1, F-CLI-2
    if (single) {
        QObject::connect(&instance, &lv::ui::SingleInstance::openRequested, &window,
                         &lv::ui::MainWindow::openRequest);
        if (!instance.listen()) {
            lv::core::log(lv::core::LogLevel::kWarning,
                          "Single instance: could not listen; later starts open new windows");
        }
    }
    const int exitCode = QApplication::exec();

    lv::core::log(lv::core::LogLevel::kInfo, "Log Viewer exiting");
    lv::core::setLogSink({});
    return exitCode;
}
