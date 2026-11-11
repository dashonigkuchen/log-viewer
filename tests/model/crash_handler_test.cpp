#include "model/crash_handler.h"
#include "temp_dir.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("old crash dumps are pruned", "[model][crash]")
{
    const lv::test::TempDir dir;
    const QString path = QString::fromStdU16String(dir.path().u16string());
    const QDateTime base = QDateTime::currentDateTime().addDays(-1);
    for (int i = 0; i < 8; ++i) {
        const QString name = path + QStringLiteral("/dump%1.dmp").arg(i);
        {
            QFile file(name);
            REQUIRE(file.open(QIODevice::WriteOnly));
            file.write("x");
        }
        // dump0 is the oldest, dump7 the newest. Set after closing: writing updates the time.
        QFile file(name);
        REQUIRE(file.open(QIODevice::ReadWrite));
        REQUIRE(file.setFileTime(base.addSecs(qint64{i} * 60), QFileDevice::FileModificationTime));
        file.close();
        REQUIRE(QFileInfo(name).lastModified() == base.addSecs(qint64{i} * 60));
    }
    QFile other(path + QStringLiteral("/notes.txt"));
    REQUIRE(other.open(QIODevice::WriteOnly));
    other.close();

    const QStringList removed = lv::model::pruneCrashDumps(path, lv::model::kMaxCrashDumps);
    CHECK(removed.size() == 3);
    CHECK(removed.contains(QStringLiteral("dump0.dmp")));
    CHECK(removed.contains(QStringLiteral("dump2.dmp")));
    const QStringList left = QDir(path).entryList({QStringLiteral("*.dmp")}, QDir::Files);
    CHECK(left.size() == lv::model::kMaxCrashDumps);
    CHECK(left.contains(QStringLiteral("dump7.dmp")));
    CHECK(QFileInfo::exists(path + QStringLiteral("/notes.txt")));  // other files stay
    CHECK(lv::model::pruneCrashDumps(path + QStringLiteral("/missing"), 5).isEmpty());
}

TEST_CASE("crash handler support depends on the platform", "[model][crash]")
{
#ifdef _WIN32
    const lv::test::TempDir dir;
    CHECK(lv::model::installCrashHandler(QString::fromStdU16String((dir / "dumps").u16string())));
#else
    CHECK_FALSE(lv::model::installCrashHandler(QStringLiteral("/tmp/dumps")));
#endif
}
