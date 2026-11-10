#include "model/session.h"

#include <QSettings>

namespace lv::model
{
namespace
{

QString key(const char* name)
{
    return QString::fromLatin1(name);
}

}  // namespace

void Session::save(QSettings& settings) const
{
    settings.remove(key("Session"));
    settings.beginGroup(key("Session"));
    settings.setValue(key("currentTab"), currentTab);
    settings.setValue(key("geometry"), geometry);
    settings.setValue(key("windowState"), windowState);
    settings.beginWriteArray(key("tabs"), static_cast<int>(tabs.size()));
    for (std::size_t i = 0; i < tabs.size(); ++i) {
        const SessionTab& tab = tabs[i];
        settings.setArrayIndex(static_cast<int>(i));
        settings.setValue(key("path"), tab.path);
        settings.setValue(key("topRow"), static_cast<qulonglong>(tab.topRow));
        if (tab.currentRow) {
            settings.setValue(key("currentRow"), static_cast<qulonglong>(*tab.currentRow));
        }
        settings.setValue(key("following"), tab.following);
        if (tab.filter) {
            settings.setValue(key("filterText"), QString::fromStdString(tab.filter->text));
            settings.setValue(key("filterMode"), static_cast<int>(tab.filter->mode));
            settings.setValue(key("filterCase"), tab.filter->caseSensitive);
            settings.setValue(key("filterWord"), tab.filter->wholeWord);
            settings.setValue(key("filterInvert"), tab.filter->invert);
        }
    }
    settings.endArray();
    settings.endGroup();
}

Session Session::load(QSettings& settings)
{
    Session session;
    settings.beginGroup(key("Session"));
    session.geometry = settings.value(key("geometry")).toByteArray();
    session.windowState = settings.value(key("windowState")).toByteArray();
    const int size = settings.beginReadArray(key("tabs"));
    for (int i = 0; i < size; ++i) {
        settings.setArrayIndex(i);
        SessionTab tab;
        tab.path = settings.value(key("path")).toString();
        if (tab.path.isEmpty()) {
            continue;  // broken entry
        }
        tab.topRow = settings.value(key("topRow"), 0).toULongLong();
        if (settings.contains(key("currentRow"))) {
            tab.currentRow = settings.value(key("currentRow")).toULongLong();
        }
        tab.following = settings.value(key("following"), false).toBool();
        const QString text = settings.value(key("filterText")).toString();
        const int mode = settings.value(key("filterMode"), 0).toInt();
        if (!text.isEmpty() && mode >= 0 && mode <= static_cast<int>(core::QueryMode::kBoolean)) {
            tab.filter =
                core::Query{.text = text.toStdString(),
                            .mode = static_cast<core::QueryMode>(mode),
                            .caseSensitive = settings.value(key("filterCase"), false).toBool(),
                            .wholeWord = settings.value(key("filterWord"), false).toBool(),
                            .invert = settings.value(key("filterInvert"), false).toBool()};
        }
        session.tabs.push_back(std::move(tab));
    }
    settings.endArray();
    const int current = settings.value(key("currentTab"), 0).toInt();
    session.currentTab =
        current >= 0 && current < static_cast<int>(session.tabs.size()) ? current : 0;
    settings.endGroup();
    return session;
}

}  // namespace lv::model
