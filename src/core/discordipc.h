// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>

#include <optional>

class QLocalSocket;

namespace mmm {

// The Discord desktop client's local IPC socket (M15; no SDK, no internet). Finds the socket
// (discord-ipc-0…9 in the runtime dir, Flatpak / Snap / Vesktop dirs, then the temp dirs),
// does the handshake with the user's application id and sends commands as FRAMEs. If Discord
// isn't running, quits or restarts, it tries again (5 s, doubling to 60 s); if Discord refuses
// the application id it stops and says so until the id changes or it's started again.
//
// Wire format: little-endian uint32 opcode + uint32 length + JSON, written as one buffer.
// Opcodes: 0 handshake, 1 frame, 2 close, 3 ping, 4 pong.
class DiscordIpc : public QObject
{
    Q_OBJECT

public:
    enum class State {
        Stopped,    // not started (or stopped)
        NotRunning, // no Discord to talk to: trying again later
        Connecting, // connected, waiting for READY
        Ready,
        Error,      // Discord refused us (e.g. an unknown application id): not retrying
    };
    Q_ENUM(State)

    enum Opcode : quint32 { Handshake = 0, Frame = 1, Close = 2, Ping = 3, Pong = 4 };

    struct Options
    {
        // Where to look. Unset: MMM_DISCORD_IPC_DIR if set (ctest: an empty dir, so the real
        // Discord is never found), else defaultIpcDirs().
        std::optional<QStringList> ipcDirs;
        int firstRetryMs = 5000;
        int maxRetryMs = 60'000;
        int handshakeTimeoutMs = 5000;
        quint32 maxFrameBytes = 256 * 1024;
    };

    explicit DiscordIpc(Options options, QObject *parent = nullptr);
    ~DiscordIpc() override;

    static QStringList defaultIpcDirs();
    static QByteArray frame(quint32 opcode, const QByteArray &json);
    QStringList ipcDirs() const;

    void setClientId(const QString &id); // restarts a running connection
    QString clientId() const { return m_clientId; }
    void start();
    void stop();
    // Sends {"cmd", "args", "nonce"}; false (nothing sent) unless Ready.
    bool send(const QString &cmd, const QJsonObject &args);
    // Sends, flushes and hangs up (Discord clears the activity when the connection goes).
    void sendAndClose(const QString &cmd, const QJsonObject &args);

    State state() const { return m_state; }
    QString lastError() const { return m_lastError; }

Q_SIGNALS:
    void stateChanged(mmm::DiscordIpc::State state);
    void commandError(const QString &message); // a command Discord didn't like (still connected)

private:
    void setState(State s);
    void tryNext();
    void retryLater();
    void dropSocket();
    void onReadyRead();
    void handle(quint32 opcode, const QByteArray &payload);
    void lost();                        // the connection went: try again later
    void dropped();                     // a bad answer: lost() if connected, else the next socket
    void refused(const QString &error); // Discord said no: stop

    Options m_options;
    QString m_clientId;
    State m_state = State::Stopped;
    QString m_lastError;
    QStringList m_candidates;
    QPointer<QLocalSocket> m_socket;
    QByteArray m_buffer;
    QTimer m_retryTimer;
    QTimer m_handshakeTimer;
    int m_retryMs = 0;
};

} // namespace mmm
