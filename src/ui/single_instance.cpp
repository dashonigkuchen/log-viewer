#include "ui/single_instance.h"

#include <QCryptographicHash>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>

namespace lv::ui
{
namespace
{

constexpr auto kFormat = "logviewer-open";
// Requests are small; anything larger is not ours.
constexpr qint64 kMaxRequestBytes = qint64{4} * 1024 * 1024;

QString userName()
{
    QString user = qEnvironmentVariable("USERNAME");  // Windows
    if (user.isEmpty()) {
        user = qEnvironmentVariable("USER");
    }
    return user;
}

}  // namespace

QByteArray OpenRequest::toBytes() const
{
    QJsonObject object{{QStringLiteral("format"), QString::fromLatin1(kFormat)},
                       {QStringLiteral("files"), QJsonArray::fromStringList(files)},
                       {QStringLiteral("follow"), follow}};
    if (line) {
        object.insert(QStringLiteral("line"), static_cast<qint64>(*line));
    }
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

std::optional<OpenRequest> OpenRequest::fromBytes(const QByteArray& bytes)
{
    const QJsonObject object = QJsonDocument::fromJson(bytes).object();
    if (object.value(QStringLiteral("format")).toString() != QString::fromLatin1(kFormat)) {
        return std::nullopt;
    }
    OpenRequest request;
    for (const auto& file : object.value(QStringLiteral("files")).toArray()) {
        if (file.isString() && !file.toString().isEmpty()) {
            request.files << file.toString();
        }
    }
    request.follow = object.value(QStringLiteral("follow")).toBool(false);
    const qint64 line = object.value(QStringLiteral("line")).toInteger(0);
    if (line > 0) {
        request.line = static_cast<std::uint64_t>(line);
    }
    return request;
}

SingleInstance::SingleInstance(const QString& key, QObject* parent)
    : QObject(parent)
{
    // Per user and per settings directory; short enough for the Unix socket path limit.
    const QByteArray id = (userName() + u'|' + QDir::cleanPath(key)).toUtf8();
    m_name = QStringLiteral("logviewer-") +
             QString::fromLatin1(
                 QCryptographicHash::hash(id, QCryptographicHash::Sha256).toHex().left(24));
}

SingleInstance::~SingleInstance() = default;

bool SingleInstance::sendToRunning(const OpenRequest& request, int timeoutMs) const
{
    QLocalSocket socket;
    socket.connectToServer(m_name);
    if (!socket.waitForConnected(timeoutMs)) {
        return false;
    }
    socket.write(request.toBytes());
    if (!socket.waitForBytesWritten(timeoutMs)) {
        return false;
    }
    // The server answers with one byte once it took the request.
    if (!socket.waitForReadyRead(timeoutMs) || socket.read(1) != "1") {
        return false;
    }
    socket.disconnectFromServer();
    return true;
}

bool SingleInstance::listen()
{
    if (m_server != nullptr) {
        return true;
    }
    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server->listen(m_name)) {
        // A socket file left by a crashed instance (Unix): remove it and try again.
        QLocalServer::removeServer(m_name);
        if (!m_server->listen(m_name)) {
            delete m_server;
            m_server = nullptr;
            return false;
        }
    }
    connect(m_server, &QLocalServer::newConnection, this, &SingleInstance::onNewConnection);
    return true;
}

void SingleInstance::onNewConnection()
{
    while (QLocalSocket* socket = m_server->nextPendingConnection()) {
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
            if (socket->bytesAvailable() > kMaxRequestBytes) {
                socket->abort();
                return;
            }
            // The client writes the whole request at once; a JSON object is complete when it
            // parses.
            const QByteArray bytes = socket->peek(socket->bytesAvailable());
            if (const auto request = OpenRequest::fromBytes(bytes)) {
                socket->readAll();
                socket->write("1");
                socket->flush();
                emit openRequested(*request);
            }
        });
    }
}

}  // namespace lv::ui
