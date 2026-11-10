#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <optional>

class QLocalServer;

namespace lv::ui
{

// What a second start asks the running instance to do (SPEC F-FILE-11, F-CLI-2).
struct OpenRequest
{
    QStringList files;  // absolute paths
    bool follow = false;
    std::optional<std::uint64_t> line;  // 1-based, for the first file

    [[nodiscard]] QByteArray toBytes() const;
    [[nodiscard]] static std::optional<OpenRequest> fromBytes(const QByteArray& bytes);

    friend bool operator==(const OpenRequest&, const OpenRequest&) = default;
};

// Single instance through a per-user QLocalServer (F-FILE-11). The first instance listens;
// later starts send their OpenRequest to it and exit.
//
// Thread-safety: UI thread only.
class SingleInstance : public QObject
{
    Q_OBJECT

public:
    // `key` identifies the instance group, for example the settings directory (so portable
    // installs and tests do not talk to each other); it is hashed into the server name.
    explicit SingleInstance(const QString& key, QObject* parent = nullptr);
    ~SingleInstance() override;
    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;
    SingleInstance(SingleInstance&&) = delete;
    SingleInstance& operator=(SingleInstance&&) = delete;

    [[nodiscard]] const QString& serverName() const noexcept { return m_name; }

    // Sends `request` to a running instance; true if it was delivered.
    [[nodiscard]] bool sendToRunning(const OpenRequest& request, int timeoutMs = 1000) const;

    // Starts listening for later instances; false if that failed (the app still works).
    bool listen();

signals:
    void openRequested(const lv::ui::OpenRequest& request);

private:
    void onNewConnection();

    QString m_name;
    QLocalServer* m_server = nullptr;
};

}  // namespace lv::ui

Q_DECLARE_METATYPE(lv::ui::OpenRequest)
