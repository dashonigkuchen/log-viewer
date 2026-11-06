#include "core/util/log.h"
#include "model/app_paths.h"
#include "model/file_log_sink.h"
#include "model/model_info.h"
#include "model/recent_files.h"
#include "ui/main_window.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QSettings>
#include <QStringList>

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
    parser.process(app);  // Handles --help and --version and exits.

    installFileLog();
    lv::core::log(
        lv::core::LogLevel::kInfo,
        QStringLiteral("Log Viewer %1 starting").arg(lv::model::versionString()).toStdString());
    // Settings file next to the application log (typed settings follow in M2-08).
    QSettings settings(lv::model::settingsDirectory() + QStringLiteral("/logviewer.ini"),
                       QSettings::IniFormat);
    lv::model::RecentFiles recent;
    recent.load(settings);
    QObject::connect(&recent, &lv::model::RecentFiles::changed,
                     [&recent, &settings] { recent.save(settings); });

    lv::ui::MainWindow window;
    window.setRecentFiles(&recent);
    window.show();
    window.openFiles(parser.positionalArguments());  // F-FILE-1
    const int exitCode = QApplication::exec();

    lv::core::log(lv::core::LogLevel::kInfo, "Log Viewer exiting");
    lv::core::setLogSink({});
    return exitCode;
}
