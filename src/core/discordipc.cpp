// SPDX-License-Identifier: GPL-3.0-or-later
#include "discordipc.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QLoggingCategory>
#include <QUuid>
#include <QtEndian>

#include <algorithm>

Q_LOGGING_CATEGORY(lcDiscord, "mmm.discord")

namespace mmm {

namespace {

// A little-endian uint32 at `at` (byte by byte: no aliasing, no alignment).
quint32 le32(const QByteArray &bytes, qsizetype at)
{
    quint32 v = 0;
    for (qsizetype i = 3; i >= 0; --i)
        v = (v << 8) | static_cast<uchar>(bytes.at(at + i));
    return v;
}

} // namespace

DiscordIpc::DiscordIpc(Options options, QObject *parent)
    : QObject(parent)
    , m_options(std::move(options))
    , m_retryMs(m_options.firstRetryMs)
{
    m_retryTimer.setSingleShot(true);
    connect(&m_retryTimer, &QTimer::timeout, this, [this] {
        if (m_state != State::NotRunning)
            return;
        m_candidates.clear();
        for (const QString &dir : ipcDirs()) {
            for (int n = 0; n < 10; ++n) {
                const QString path = QDir(dir).filePath(QStringLiteral("discord-ipc-%1").arg(n));
                if (QFileInfo::exists(path))
                    m_candidates << path;
            }
        }
        tryNext();
    });
    m_handshakeTimer.setSingleShot(true);
    connect(&m_handshakeTimer, &QTimer::timeout, this, [this] {
        if (m_state == State::Connecting && m_socket) {
            qCInfo(lcDiscord) << "no answer to the handshake from" << m_socket->serverName();
            tryNext();
        }
    });
}

DiscordIpc::~DiscordIpc()
{
    dropSocket(); // Discord clears the activity when the connection goes
}

QStringList DiscordIpc::defaultIpcDirs()
{
    QStringList dirs;
    const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (!runtime.isEmpty()) {
        dirs << runtime << runtime + QStringLiteral("/app/com.discordapp.Discord")
             << runtime + QStringLiteral("/snap.discord") << runtime + QStringLiteral("/app/dev.vencord.Vesktop");
    }
    for (const char *var : {"TMPDIR", "TMP", "TEMP"}) {
        const QString dir = qEnvironmentVariable(var);
        if (!dir.isEmpty())
            dirs << dir;
    }
    dirs << QStringLiteral("/tmp");
    dirs.removeDuplicates();
    return dirs;
}

QStringList DiscordIpc::ipcDirs() const
{
    if (m_options.ipcDirs)
        return *m_options.ipcDirs;
    if (qEnvironmentVariableIsSet("MMM_DISCORD_IPC_DIR")) {
        const QString dir = qEnvironmentVariable("MMM_DISCORD_IPC_DIR");
        return dir.isEmpty() ? QStringList() : QStringList{dir};
    }
    return defaultIpcDirs();
}

QByteArray DiscordIpc::frame(quint32 opcode, const QByteArray &json)
{
    QByteArray out(8, Qt::Uninitialized);
    qToLittleEndian<quint32>(opcode, out.data());
    qToLittleEndian<quint32>(static_cast<quint32>(json.size()), out.data() + 4);
    return out + json;
}

void DiscordIpc::setState(State s)
{
    if (s == m_state)
        return;
    m_state = s;
    Q_EMIT stateChanged(s);
}

void DiscordIpc::setClientId(const QString &id)
{
    const QString trimmed = id.trimmed();
    if (trimmed == m_clientId)
        return;
    m_clientId = trimmed;
    if (m_state != State::Stopped) {
        stop();
        start();
    }
}

void DiscordIpc::start()
{
    if (m_clientId.isEmpty() || (m_state != State::Stopped && m_state != State::Error))
        return;
    m_lastError.clear();
    m_retryMs = m_options.firstRetryMs;
    setState(State::NotRunning);
    m_retryTimer.start(0); // looks for the socket
}

void DiscordIpc::stop()
{
    m_retryTimer.stop();
    m_candidates.clear();
    dropSocket();
    setState(State::Stopped);
}

void DiscordIpc::dropSocket()
{
    m_handshakeTimer.stop();
    m_buffer.clear();
    if (QLocalSocket *s = m_socket.data()) {
        m_socket = nullptr;
        s->disconnect(this);
        s->abort();
        s->deleteLater();
    }
}

void DiscordIpc::tryNext()
{
    dropSocket();
    if (m_candidates.isEmpty()) {
        retryLater();
        return;
    }
    const QString path = m_candidates.takeFirst();
    setState(State::Connecting);
    auto *s = new QLocalSocket(this);
    m_socket = s;
    connect(s, &QLocalSocket::connected, this, [this, s] {
        if (s != m_socket)
            return;
        const QJsonObject hello{{QStringLiteral("v"), 1}, {QStringLiteral("client_id"), m_clientId}};
        s->write(frame(Handshake, QJsonDocument(hello).toJson(QJsonDocument::Compact)));
        m_handshakeTimer.start(m_options.handshakeTimeoutMs); // READY within this, too
    });
    connect(s, &QLocalSocket::readyRead, this, [this, s] {
        if (s == m_socket)
            onReadyRead();
    });
    // Queued: a refused connect may report while connectToServer() is still running.
    const auto gone = [this, s = QPointer<QLocalSocket>(s)] {
        if (!s || s != m_socket)
            return;
        if (m_state == State::Ready)
            lost();
        else
            tryNext(); // a stale socket file, or it hung up on the handshake: the next one
    };
    connect(s, &QLocalSocket::errorOccurred, this, gone, Qt::QueuedConnection);
    connect(s, &QLocalSocket::disconnected, this, gone, Qt::QueuedConnection);
    m_handshakeTimer.start(m_options.handshakeTimeoutMs); // a socket stuck connecting is left too
    s->connectToServer(path);
}

void DiscordIpc::retryLater()
{
    dropSocket();
    setState(State::NotRunning);
    m_retryTimer.start(m_retryMs);
    m_retryMs = std::min(m_retryMs * 2, m_options.maxRetryMs);
}

void DiscordIpc::dropped()
{
    // While connecting: this one isn't it, try the next (backoff kept). Connected: Discord went.
    if (m_state == State::Ready)
        lost();
    else
        tryNext();
}

void DiscordIpc::lost()
{
    qCInfo(lcDiscord) << "Discord went away: trying again";
    m_retryMs = m_options.firstRetryMs;
    retryLater();
}

void DiscordIpc::refused(const QString &error)
{
    qCWarning(lcDiscord) << "Discord refused the connection:" << error;
    m_retryTimer.stop();
    m_candidates.clear();
    dropSocket();
    m_lastError = error;
    setState(State::Error);
}

void DiscordIpc::onReadyRead()
{
    m_buffer += m_socket->readAll();
    while (m_socket && m_buffer.size() >= 8) {
        const quint32 opcode = le32(m_buffer, 0);
        const quint32 length = le32(m_buffer, 4);
        if (length > m_options.maxFrameBytes) {
            qCWarning(lcDiscord) << "a frame too big to be Discord's:" << length << "bytes";
            dropped();
            return;
        }
        if (m_buffer.size() < 8 + qsizetype(length))
            return;
        const QByteArray payload = m_buffer.mid(8, length);
        m_buffer.remove(0, 8 + qsizetype(length));
        handle(opcode, payload);
    }
}

void DiscordIpc::handle(quint32 opcode, const QByteArray &payload)
{
    const QJsonObject json = QJsonDocument::fromJson(payload).object();
    switch (opcode) {
    case Ping:
        m_socket->write(frame(Pong, payload));
        return;
    case Close: {
        // 4xxx: Discord won't have us (an unknown application id, say). Else it's going away.
        const int code = json.value(QStringLiteral("code")).toInt();
        const QString message = json.value(QStringLiteral("message")).toString();
        if (code >= 4000 && code < 5000)
            refused(message.isEmpty() ? QStringLiteral("error %1").arg(code) : message);
        else
            dropped();
        return;
    }
    case Frame: {
        const QString evt = json.value(QStringLiteral("evt")).toString();
        if (json.value(QStringLiteral("cmd")).toString() == QLatin1String("DISPATCH") && evt == QLatin1String("READY")) {
            m_handshakeTimer.stop();
            m_retryMs = m_options.firstRetryMs;
            m_lastError.clear();
            setState(State::Ready);
        } else if (evt == QLatin1String("ERROR")) {
            const QString message = json.value(QStringLiteral("data")).toObject().value(QStringLiteral("message")).toString();
            if (m_state != State::Ready) {
                refused(message.isEmpty() ? QStringLiteral("an error during the handshake") : message);
            } else {
                qCInfo(lcDiscord) << "Discord didn't take a command:" << message;
                Q_EMIT commandError(message);
            }
        }
        return;
    }
    default:
        return;
    }
}

bool DiscordIpc::send(const QString &cmd, const QJsonObject &args)
{
    if (m_state != State::Ready || !m_socket)
        return false;
    const QJsonObject message{{QStringLiteral("cmd"), cmd},
                              {QStringLiteral("args"), args},
                              {QStringLiteral("nonce"), QUuid::createUuid().toString(QUuid::WithoutBraces)}};
    m_socket->write(frame(Frame, QJsonDocument(message).toJson(QJsonDocument::Compact)));
    return true;
}

void DiscordIpc::sendAndClose(const QString &cmd, const QJsonObject &args)
{
    if (send(cmd, args)) {
        // Let it go out, then hang up (not abort(): that would drop it).
        QLocalSocket *s = m_socket.data();
        m_socket = nullptr;
        s->disconnect(this);
        s->flush();
        connect(s, &QLocalSocket::disconnected, s, &QObject::deleteLater);
        connect(s, &QLocalSocket::errorOccurred, s, &QObject::deleteLater);
        s->disconnectFromServer();
        if (s->state() == QLocalSocket::UnconnectedState)
            s->deleteLater();
    }
    stop();
}

} // namespace mmm
