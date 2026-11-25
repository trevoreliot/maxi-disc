// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QLocalServer>
#include <QLocalSocket>
#include <QObject>
#include <QPointer>
#include <QtEndian>

namespace mmm::test {

// A stand-in Discord desktop client (M15): listens on `dir`/discord-ipc-N, answers the
// handshake with READY (or refuses an application id it doesn't know) and records every frame.
// Tests point DiscordIpc at `dir`: the real Discord is never touched.
class FakeDiscord : public QObject
{
public:
    struct Received
    {
        quint32 opcode = 0;
        QJsonObject json;
        QByteArray raw;
    };

    QString knownClientId = QStringLiteral("123456789012345678");
    bool answerHandshake = true; // false: accept, then say nothing (a hung Discord)
    int closeOnHandshake = 0;    // non-zero: answer the handshake with CLOSE and this code

    explicit FakeDiscord(QString dir, int n = 0, QObject *parent = nullptr)
        : QObject(parent)
        , m_path(QDir(dir).filePath(QStringLiteral("discord-ipc-%1").arg(n)))
    {
        connect(&m_server, &QLocalServer::newConnection, this, [this] {
            while (QLocalSocket *s = m_server.nextPendingConnection())
                serve(s);
        });
    }

    bool listen()
    {
        QLocalServer::removeServer(m_path);
        return m_server.listen(m_path);
    }
    // Discord quits: every connection goes, the socket file too.
    void quit()
    {
        for (const QPointer<QLocalSocket> &s : std::as_const(m_clients)) {
            if (s)
                s->abort();
        }
        m_clients.clear();
        m_server.close();
    }
    QString path() const { return m_path; }
    int connections() const { return m_connections; }
    const QList<Received> &received() const { return m_received; }
    // The SET_ACTIVITY commands, oldest first.
    QList<QJsonObject> activities() const
    {
        QList<QJsonObject> out;
        for (const Received &r : m_received) {
            if (r.opcode == 1 && r.json.value(QStringLiteral("cmd")).toString() == QLatin1String("SET_ACTIVITY"))
                out << r.json.value(QStringLiteral("args")).toObject();
        }
        return out;
    }
    // To every client: a raw frame (opcode + JSON), or a byte string as is.
    void sendToAll(quint32 opcode, const QByteArray &json)
    {
        for (const QPointer<QLocalSocket> &s : std::as_const(m_clients)) {
            if (s)
                s->write(frame(opcode, json));
        }
    }
    void writeRawToAll(const QByteArray &bytes)
    {
        for (const QPointer<QLocalSocket> &s : std::as_const(m_clients)) {
            if (s)
                s->write(bytes);
        }
    }
    static QByteArray frame(quint32 opcode, const QByteArray &json)
    {
        QByteArray out(8, Qt::Uninitialized);
        qToLittleEndian<quint32>(opcode, out.data());
        qToLittleEndian<quint32>(static_cast<quint32>(json.size()), out.data() + 4);
        return out + json;
    }
    void clear() { m_received.clear(); }
    static quint32 le32(const QByteArray &bytes, qsizetype at)
    {
        quint32 v = 0;
        for (qsizetype i = 3; i >= 0; --i)
            v = (v << 8) | static_cast<uchar>(bytes.at(at + i));
        return v;
    }

private:
    void serve(QLocalSocket *s)
    {
        ++m_connections;
        m_clients << s;
        auto buffer = std::make_shared<QByteArray>();
        connect(s, &QLocalSocket::disconnected, s, &QObject::deleteLater);
        connect(s, &QLocalSocket::readyRead, this, [this, s, buffer] {
            buffer->append(s->readAll());
            while (buffer->size() >= 8) {
                const quint32 op = le32(*buffer, 0);
                const quint32 len = le32(*buffer, 4);
                if (buffer->size() < 8 + qsizetype(len))
                    return;
                const QByteArray payload = buffer->mid(8, len);
                buffer->remove(0, 8 + qsizetype(len));
                const QJsonObject json = QJsonDocument::fromJson(payload).object();
                m_received << Received{op, json, payload};
                if (op == 0 && closeOnHandshake != 0) {
                    s->write(frame(2, "{\"code\":" + QByteArray::number(closeOnHandshake) + ",\"message\":\"bye\"}"));
                } else if (op == 0 && answerHandshake) {
                    if (json.value(QStringLiteral("client_id")).toString() == knownClientId)
                        s->write(frame(1, "{\"cmd\":\"DISPATCH\",\"evt\":\"READY\",\"data\":{\"v\":1}}"));
                    else
                        s->write(frame(2, "{\"code\":4000,\"message\":\"Invalid Client ID\"}"));
                }
            }
        });
    }

    QString m_path;
    QLocalServer m_server;
    QList<QPointer<QLocalSocket>> m_clients;
    QList<Received> m_received;
    int m_connections = 0;
};

} // namespace mmm::test
