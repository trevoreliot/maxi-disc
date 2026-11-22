// SPDX-License-Identifier: GPL-3.0-or-later
#include "httpfetcher.h"

#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include <algorithm>

Q_LOGGING_CATEGORY(lcHttp, "mmm.http")

namespace mmm {

QString HttpResult::describe() const
{
    if (!error.isEmpty())
        return error;
    if (status == 503 || status == 429)
        return QStringLiteral("the server is busy (HTTP %1); try again later").arg(status);
    return QStringLiteral("HTTP %1").arg(status);
}

HttpFetcher::HttpFetcher(QNetworkAccessManager *nam, Options options, QObject *parent)
    : QObject(parent), m_nam(nam), m_options(options)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &HttpFetcher::pump);
}

HttpFetcher::~HttpFetcher()
{
    cancelAll();
}

void HttpFetcher::get(const QUrl &url, QObject *context, Callback done, bool urgent)
{
    Job job{url, context, std::move(done), 0};
    if (urgent)
        m_queue.push_front(std::move(job));
    else
        m_queue.push_back(std::move(job));
    pump();
}

void HttpFetcher::cancelAll()
{
    m_queue.clear();
    ++m_generation; // running replies finish into the void
    m_timer.stop();
    // Abort (finished() follows synchronously or queued; the generation check drops it).
    const auto replies = findChildren<QNetworkReply *>(Qt::FindDirectChildrenOnly);
    for (QNetworkReply *r : replies)
        r->abort();
}

void HttpFetcher::pump()
{
    while (m_running < m_options.maxConcurrent && !m_queue.empty()) {
        // Drop requests whose receiver is gone without spending a slot on them.
        if (!m_queue.front().context) {
            m_queue.pop_front();
            continue;
        }
        qint64 wait = m_notBefore.isForever() ? 0 : m_notBefore.remainingTime();
        if (m_sinceLast.isValid())
            wait = std::max(wait, m_options.minIntervalMs + m_backoffMs - m_sinceLast.elapsed());
        if (wait > 0) {
            if (!m_timer.isActive())
                m_timer.start(static_cast<int>(wait));
            return;
        }
        Job job = std::move(m_queue.front());
        m_queue.pop_front();
        start(std::move(job));
    }
}

void HttpFetcher::start(Job job)
{
    if (!m_nam) {
        // Answer later, like a real request: callers may not expect a callback inside get().
        QTimer::singleShot(0, this, [job = std::move(job)] {
            if (job.context)
                job.done(HttpResult{job.url, 0, {}, QStringLiteral("no network access")});
        });
        return;
    }
    QNetworkRequest request(job.url);
    if (!m_userAgent.isEmpty())
        request.setHeader(QNetworkRequest::UserAgentHeader, m_userAgent);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(m_options.timeoutMs);

    m_sinceLast.start();
    ++m_running;
    QNetworkReply *reply = m_nam->get(request);
    reply->setParent(this); // so cancelAll() finds it
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, job = std::move(job), generation = m_generation]() mutable {
                finished(reply, std::move(job), generation);
            });
}

void HttpFetcher::finished(QNetworkReply *reply, Job job, quint64 generation)
{
    reply->deleteLater();
    --m_running;
    if (m_options.minIntervalMs > 0 || m_backoffMs > 0)
        m_sinceLast.start(); // the next request waits a full interval from this answer
    if (generation != m_generation) {
        pump();
        return;
    }

    HttpResult r;
    r.url = job.url;
    const QVariant status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    r.status = status.isValid() ? status.toInt() : 0;
    if (r.status == 0 && reply->error() == QNetworkReply::NoError)
        r.status = 200; // file:// and other non-HTTP schemes
    // A transport error after a 2xx/3xx status (connection dropped or timed out mid-body,
    // refused redirect) is a failure, not a short body. 4xx/5xx keep their status.
    if (reply->error() != QNetworkReply::NoError && r.status < 400)
        r.status = 0;
    if (r.status == 0)
        r.error = reply->errorString();
    else
        r.body = reply->readAll();

    const bool busy = r.status == 503 || r.status == 429;
    if (busy)
        m_backoffMs = std::min(m_options.maxBackoffMs, m_backoffMs + m_options.backoffStepMs);
    else if (r.ok())
        m_backoffMs = std::max(0, m_backoffMs - std::max(1, m_options.backoffStepMs / 5));
    if (busy && job.attempt < m_options.maxRetries) {
        bool ok = false;
        const int retryAfter = reply->rawHeader("Retry-After").trimmed().toInt(&ok);
        const qint64 delay = ok ? std::clamp(retryAfter, 0, 10) * 1000LL : qint64(m_options.retryDelayMs) * (job.attempt + 1);
        // Expected now and then (MusicBrainz's limiter): only worth a debug line.
        qCDebug(lcHttp) << "HTTP" << r.status << "for" << job.url.toDisplayString() << "- retrying in" << delay
                        << "ms; spacing now +" << m_backoffMs << "ms";
        // Never shorten a wait another answer asked for.
        const QDeadlineTimer next(delay);
        if (m_notBefore.hasExpired() || next.deadline() > m_notBefore.deadline())
            m_notBefore = next;
        ++job.attempt;
        m_queue.push_front(std::move(job));
        pump();
        return;
    }
    // 404 is an answer ("no such release / no cover"), not a problem.
    if (!r.ok() && r.status != 404)
        qCInfo(lcHttp).noquote() << job.url.toDisplayString() << "->" << r.describe();
    const QPointer<HttpFetcher> self(this);
    if (job.context)
        job.done(r);
    if (self) // the callback may have destroyed us
        pump();
}

} // namespace mmm
