// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>

#include <deque>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

namespace mmm {

struct HttpResult
{
    QUrl url;          // as requested
    int status = 0;    // HTTP status; 200 for a successful non-HTTP (file://) request
    QByteArray body;
    QString error;     // transport error (no HTTP response); "" otherwise

    bool ok() const { return error.isEmpty() && status >= 200 && status < 300; }
    // Readable reason for a failed result.
    QString describe() const;
};

// Queued GET requests over a QNetworkAccessManager, for web services with usage rules:
// at most `maxConcurrent` requests in flight, a request starts only `minIntervalMs` after
// both the previous start and the previous answer (so a slow first request, e.g. one that
// also did the TLS handshake, can't make the next arrive at the server too soon), and 503 /
// 429 answers retried (after Retry-After, capped at 10 s) up to `maxRetries` times, with the
// spacing growing while the server keeps saying "slow down" (and shrinking back after).
// Lives on one thread (normally the UI thread: QNetworkAccessManager is asynchronous).
class HttpFetcher : public QObject
{
    Q_OBJECT

public:
    struct Options
    {
        int maxConcurrent = 1;
        int minIntervalMs = 0;
        int maxRetries = 2;
        int timeoutMs = 20000;
        int retryDelayMs = 2000; // when a 503/429 has no Retry-After
        // Adaptive pacing: each 503/429 adds `backoffStepMs` to the spacing between requests
        // (up to `maxBackoffMs`); each success takes a fifth of a step off again.
        int backoffStepMs = 500;
        int maxBackoffMs = 4000;
    };
    using Callback = std::function<void(const HttpResult &)>;

    HttpFetcher(QNetworkAccessManager *nam, Options options, QObject *parent = nullptr);
    ~HttpFetcher() override;

    void setUserAgent(const QString &userAgent) { m_userAgent = userAgent.toUtf8(); }
    Options options() const { return m_options; }
    void setOptions(const Options &options) { m_options = options; }

    // Queues a GET. `done` runs on this object's thread, unless `context` is destroyed or
    // cancelAll() is called first. `urgent` jumps the queue (still rate limited).
    void get(const QUrl &url, QObject *context, Callback done, bool urgent = false);
    // Drops queued requests and aborts running ones; their callbacks never run.
    void cancelAll();
    int queuedCount() const { return static_cast<int>(m_queue.size()); }
    int runningCount() const { return m_running; }
    // Extra spacing currently added because the server said "slow down".
    int backoffMs() const { return m_backoffMs; }

private:
    struct Job
    {
        QUrl url;
        QPointer<QObject> context;
        Callback done;
        int attempt = 0;
    };

    void pump();
    void start(Job job);
    void finished(QNetworkReply *reply, Job job, quint64 generation);

    QPointer<QNetworkAccessManager> m_nam;
    Options m_options;
    QByteArray m_userAgent;
    std::deque<Job> m_queue;
    int m_running = 0;
    quint64 m_generation = 0;
    QElapsedTimer m_sinceLast; // since the last request started or its answer arrived
    QDeadlineTimer m_notBefore; // set by Retry-After
    int m_backoffMs = 0;
    QTimer m_timer;
};

} // namespace mmm
