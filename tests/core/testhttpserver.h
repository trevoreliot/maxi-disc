// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <functional>

namespace mmm::test {

// A minimal HTTP/1.1 server on 127.0.0.1 for network tests: GET / POST, one request per
// connection ("Connection: close"). Routes match the path exactly; unknown paths get 404.
// Tests must never reach the real internet: point clients' base URLs at url().
class TestHttpServer : public QObject
{
public:
    struct Request
    {
        QByteArray method;                      // "GET", "POST", ...
        QString path;
        QUrlQuery query;
        QByteArray body;                        // a POST's (Content-Length bytes)
        QHash<QByteArray, QByteArray> headers; // names lower-cased
        qint64 atMs = 0;                        // since the server started
    };
    struct Response
    {
        int status = 200;
        QByteArray body;
        QList<std::pair<QByteArray, QByteArray>> headers;
        int delayMs = 0;
        int sendBytes = -1; // >= 0: send only this much of the body, then hang up (truncated)
    };
    using Handler = std::function<Response(const Request &)>;

    explicit TestHttpServer(QObject *parent = nullptr) : QObject(parent)
    {
        m_clock.start();
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *s = m_server.nextPendingConnection())
                serve(s);
        });
    }

    bool listen() { return m_server.listen(QHostAddress::LocalHost); }
    QUrl url(const QString &path = {}) const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(m_server.serverPort()).arg(path));
    }

    void route(const QString &path, Handler handler) { m_routes.insert(path, std::move(handler)); }
    void route(const QString &path, int status, const QByteArray &body = {})
    {
        route(path, [status, body](const Request &) { return Response{status, body, {}, 0, -1}; });
    }

    const QList<Request> &requests() const { return m_requests; }
    QList<Request> requestsTo(const QString &path) const
    {
        QList<Request> out;
        for (const Request &r : m_requests) {
            if (r.path == path)
                out << r;
        }
        return out;
    }

private:
    void serve(QTcpSocket *socket)
    {
        auto buffer = std::make_shared<QByteArray>();
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer] {
            buffer->append(socket->readAll());
            const qsizetype end = buffer->indexOf("\r\n\r\n");
            if (end < 0)
                return;
            const QList<QByteArray> lines = buffer->left(end).split('\n');
            // A body follows the headers (POST): wait for all of it.
            qsizetype bodyLength = 0;
            for (const QByteArray &line : lines) {
                if (line.trimmed().toLower().startsWith("content-length:"))
                    bodyLength = line.mid(line.indexOf(':') + 1).trimmed().toLongLong();
            }
            if (buffer->size() < end + 4 + bodyLength)
                return;
            const QByteArray body = buffer->mid(end + 4, bodyLength);
            buffer->clear();
            const QList<QByteArray> first = lines.value(0).trimmed().split(' ');
            const QUrl target(QStringLiteral("http://x") + QString::fromLatin1(first.value(1)));
            Request req;
            req.method = first.value(0);
            req.body = body;
            req.path = target.path();
            req.query = QUrlQuery(target);
            req.atMs = m_clock.elapsed();
            for (qsizetype i = 1; i < lines.size(); ++i) {
                const QByteArray line = lines[i].trimmed();
                const qsizetype colon = line.indexOf(':');
                if (colon > 0)
                    req.headers.insert(line.left(colon).trimmed().toLower(), line.mid(colon + 1).trimmed());
            }
            m_requests << req;
            const auto it = m_routes.constFind(req.path);
            const Response resp = it == m_routes.cend() ? Response{404, "not found", {}, 0, -1} : (*it)(req);
            QPointer<QTcpSocket> guard(socket);
            auto send = [guard, resp] {
                if (!guard)
                    return;
                QByteArray out = "HTTP/1.1 " + QByteArray::number(resp.status) + " X\r\n";
                out += "Content-Length: " + QByteArray::number(resp.body.size()) + "\r\nConnection: close\r\n";
                for (const auto &[k, v] : resp.headers)
                    out += k + ": " + v + "\r\n";
                out += "\r\n" + (resp.sendBytes >= 0 ? resp.body.left(resp.sendBytes) : resp.body);
                guard->write(out);
                guard->disconnectFromHost();
            };
            if (resp.delayMs > 0)
                QTimer::singleShot(resp.delayMs, this, send);
            else
                send();
        });
    }

    QTcpServer m_server;
    QElapsedTimer m_clock;
    QHash<QString, Handler> m_routes;
    QList<Request> m_requests;
};

} // namespace mmm::test
