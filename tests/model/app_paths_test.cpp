#include "model/app_paths.h"

#include <QByteArray>
#include <QDir>
#include <QtGlobal>

#include <catch2/catch_test_macros.hpp>

namespace
{

// Sets an environment variable for the lifetime of the object and restores it afterwards.
class EnvGuard
{
public:
    EnvGuard(const char* name, const QByteArray& value)
        : m_name(name)
        , m_hadValue(qEnvironmentVariableIsSet(name))
        , m_oldValue(qgetenv(name))
    {
        qputenv(name, value);
    }

    EnvGuard(const EnvGuard&) = delete;
    EnvGuard& operator=(const EnvGuard&) = delete;
    EnvGuard(EnvGuard&&) = delete;
    EnvGuard& operator=(EnvGuard&&) = delete;

    ~EnvGuard()
    {
        if (m_hadValue) {
            qputenv(m_name, m_oldValue);
        } else {
            qunsetenv(m_name);
        }
    }

private:
    const char* m_name;
    bool m_hadValue;
    QByteArray m_oldValue;
};

}  // namespace

TEST_CASE("settingsDirectory honors LV_SETTINGS_DIR", "[model][paths]")
{
    const EnvGuard env("LV_SETTINGS_DIR", "/tmp/lv-settings-test/./sub/..");
    CHECK(lv::model::settingsDirectory() == QStringLiteral("/tmp/lv-settings-test"));
}

TEST_CASE("settingsDirectory uses APPDATA when it is set", "[model][paths]")
{
    const EnvGuard noOverride("LV_SETTINGS_DIR", "");
    const EnvGuard appData("APPDATA", "/fake/AppData/Roaming");
    CHECK(lv::model::settingsDirectory() == QStringLiteral("/fake/AppData/Roaming/LogViewer"));
}

TEST_CASE("settingsDirectory ends with LogViewer by default", "[model][paths]")
{
    const EnvGuard noOverride("LV_SETTINGS_DIR", "");
    const QString directory = lv::model::settingsDirectory();
    CHECK(directory.endsWith(QStringLiteral("/LogViewer")));
    CHECK(QDir::isAbsolutePath(directory));
}
